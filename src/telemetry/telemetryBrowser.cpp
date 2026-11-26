#include "telemetryBrowser.h"
#include "compactFont.h"
#include "log.h"
#include "render.h"
#include "windowManager.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#ifndef SWOS_TEST
#include <SDL_syswm.h>
#include <windows.h>
#include <winhttp.h>
#include <objbase.h>
#include "WebView2.h"
#endif

namespace {

constexpr const char *kDefaultTelemetryHost = "localhost";
constexpr uint16_t kDefaultTelemetryPort = 3000;
constexpr const char *kDefaultTelemetryUrl = "http://localhost:3000";

#ifndef SWOS_TEST

std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty())
        return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0)
        return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

void tbFillRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{ x, y, w, h };
    SDL_RenderFillRectF(r, &rc);
}

void tbDrawRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{ x, y, w, h };
    SDL_RenderDrawRectF(r, &rc);
}

bool pointInRect(float px, float py, const SDL_FRect& rc)
{
    return px >= rc.x && px <= rc.x + rc.w && py >= rc.y && py <= rc.y + rc.h;
}

std::wstring getWebViewUserDataFolder()
{
    wchar_t appData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH) > 0) {
        std::wstring base(appData);
        base += L"\\swos-port";
        CreateDirectoryW(base.c_str(), nullptr);
        base += L"\\WebView2";
        CreateDirectoryW(base.c_str(), nullptr);
        return base;
    }
    return L"WebView2Data";
}

// Exact WebView2 handler IIDs (avoids __uuidof / external GUID lib dependencies)
constexpr GUID kIidEnvCompleted    = { 0x4e8a3389, 0xc9d8, 0x4bd2, { 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d } };
constexpr GUID kIidCtrlCompleted   = { 0x6c4819f3, 0xc9b7, 0x4260, { 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c } };
constexpr GUID kIidAccelKeyPressed = { 0xb29c7e28, 0xfa79, 0x41a8, { 0x8e, 0x44, 0x65, 0x81, 0x1c, 0x76, 0xdc, 0xb2 } };
constexpr GUID kIidNavCompleted    = { 0xd33a35bf, 0x1c49, 0x4f98, { 0x93, 0xab, 0x00, 0x6e, 0x05, 0x33, 0xfe, 0x1c } };

// Lightweight COM callback helper (zero WRL/WIL dependency)
template <typename Interface, typename... Args>
class SimpleComHandler final : public Interface {
public:
    using Func = std::function<HRESULT(Args...)>;
    SimpleComHandler(const IID& iid, Func fn) : m_iid(iid), m_fn(std::move(fn)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, m_iid)) {
            *ppv = static_cast<Interface *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++m_refCount;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --m_refCount;
        if (c == 0)
            delete this;
        return c;
    }

    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        return m_fn(args...);
    }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    IID m_iid;
    Func m_fn;
};

using CreateEnvFn = HRESULT(STDAPICALLTYPE *)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions *environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *environmentCreatedHandler);

HMODULE loadWebView2Loader()
{
    if (HMODULE h = LoadLibraryW(L"WebView2Loader.dll"))
        return h;

    // Try next to the executable
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        std::wstring dir(exePath);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            dir.resize(slash + 1);
            if (HMODULE h = LoadLibraryW((dir + L"WebView2Loader.dll").c_str()))
                return h;
        }
    }

    // Dev fallback in repo
    if (HMODULE h = LoadLibraryW(L"3rd-party\\WebView2\\lib\\win64\\WebView2Loader.dll"))
        return h;

    return nullptr;
}

struct WebViewSession {
    HMODULE loaderDll = nullptr;
    ICoreWebView2Environment *env = nullptr;
    ICoreWebView2Controller *controller = nullptr;
    ICoreWebView2 *webview = nullptr;
    EventRegistrationToken accelToken{};
    EventRegistrationToken navToken{};
    bool hasAccelToken = false;
    bool hasNavToken = false;
    bool creating = false;
    bool ready = false;
    bool runtimeMissing = false;
    bool navFailedOffline = false;
    bool closeRequested = false;
    bool reloadRequested = false;
    HWND parentHwnd = nullptr;
    int topBarHeight = 44;
    std::wstring targetUrl;

    void updateBounds() {
        if (!controller || !parentHwnd)
            return;
        RECT rc{};
        GetClientRect(parentHwnd, &rc);
        RECT bounds{ 0, topBarHeight, rc.right, std::max<LONG>(topBarHeight + 1, rc.bottom) };
        controller->put_Bounds(bounds);
    }

    void teardown() {
        if (controller && hasAccelToken) {
            controller->remove_AcceleratorKeyPressed(accelToken);
            hasAccelToken = false;
        }
        if (webview && hasNavToken) {
            webview->remove_NavigationCompleted(navToken);
            hasNavToken = false;
        }
        if (controller) {
            controller->Close();
            controller->Release();
            controller = nullptr;
        }
        if (webview) {
            webview->Release();
            webview = nullptr;
        }
        if (env) {
            env->Release();
            env = nullptr;
        }
        ready = false;
        creating = false;
    }

    bool start(HWND hwnd, int barH, const std::wstring& url) {
        teardown();
        parentHwnd = hwnd;
        topBarHeight = barH;
        targetUrl = url;
        navFailedOffline = false;
        runtimeMissing = false;
        closeRequested = false;
        reloadRequested = false;

        if (!loaderDll)
            loaderDll = loadWebView2Loader();
        if (!loaderDll) {
            logWarn("TelemetryBrowser: WebView2Loader.dll not found");
            runtimeMissing = true;
            return false;
        }

        auto createEnv = reinterpret_cast<CreateEnvFn>(
            GetProcAddress(loaderDll, "CreateCoreWebView2EnvironmentWithOptions"));
        if (!createEnv) {
            logWarn("TelemetryBrowser: CreateCoreWebView2EnvironmentWithOptions not found");
            runtimeMissing = true;
            return false;
        }

        creating = true;
        std::wstring userData = getWebViewUserDataFolder();

        auto *envHandler = new SimpleComHandler<
            ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
            HRESULT, ICoreWebView2Environment *>(
            kIidEnvCompleted,
            [this](HRESULT hr, ICoreWebView2Environment *createdEnv) -> HRESULT {
                if (FAILED(hr) || !createdEnv) {
                    logWarn("TelemetryBrowser: WebView2 environment creation failed (0x%08X)", static_cast<unsigned>(hr));
                    creating = false;
                    runtimeMissing = true;
                    return S_OK;
                }
                env = createdEnv;
                env->AddRef();

                auto *ctrlHandler = new SimpleComHandler<
                    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                    HRESULT, ICoreWebView2Controller *>(
                    kIidCtrlCompleted,
                    [this](HRESULT hrCtrl, ICoreWebView2Controller *createdCtrl) -> HRESULT {
                        creating = false;
                        if (FAILED(hrCtrl) || !createdCtrl) {
                            logWarn("TelemetryBrowser: WebView2 controller creation failed (0x%08X)", static_cast<unsigned>(hrCtrl));
                            runtimeMissing = true;
                            return S_OK;
                        }
                        controller = createdCtrl;
                        controller->AddRef();
                        controller->get_CoreWebView2(&webview);

                        // Capture ESC (close) and F5 (reload) even while WebView2 has keyboard focus
                        auto *accelHandler = new SimpleComHandler<
                            ICoreWebView2AcceleratorKeyPressedEventHandler,
                            ICoreWebView2Controller *, ICoreWebView2AcceleratorKeyPressedEventArgs *>(
                            kIidAccelKeyPressed,
                            [this](ICoreWebView2Controller *, ICoreWebView2AcceleratorKeyPressedEventArgs *args) -> HRESULT {
                                COREWEBVIEW2_KEY_EVENT_KIND kind{};
                                UINT vkey = 0;
                                if (args && SUCCEEDED(args->get_KeyEventKind(&kind)) && SUCCEEDED(args->get_VirtualKey(&vkey))) {
                                    if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                                        if (vkey == VK_ESCAPE) {
                                            closeRequested = true;
                                            args->put_Handled(TRUE);
                                        } else if (vkey == VK_F5) {
                                            reloadRequested = true;
                                            args->put_Handled(TRUE);
                                        }
                                    }
                                }
                                return S_OK;
                            });
                        if (SUCCEEDED(controller->add_AcceleratorKeyPressed(accelHandler, &accelToken)))
                            hasAccelToken = true;
                        accelHandler->Release();

                        if (webview) {
                            // Detect server going offline during navigation
                            auto *navHandler = new SimpleComHandler<
                                ICoreWebView2NavigationCompletedEventHandler,
                                ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *>(
                                kIidNavCompleted,
                                [this](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                                    BOOL isSuccess = TRUE;
                                    COREWEBVIEW2_WEB_ERROR_STATUS errStatus = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                    if (args && SUCCEEDED(args->get_IsSuccess(&isSuccess)) && !isSuccess) {
                                        args->get_WebErrorStatus(&errStatus);
                                        if (errStatus == COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT ||
                                            errStatus == COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET ||
                                            errStatus == COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED ||
                                            errStatus == COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED ||
                                            errStatus == COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED ||
                                            errStatus == COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT) {
                                            navFailedOffline = true;
                                        }
                                    }
                                    return S_OK;
                                });
                            if (SUCCEEDED(webview->add_NavigationCompleted(navHandler, &navToken)))
                                hasNavToken = true;
                            navHandler->Release();

                            webview->Navigate(targetUrl.c_str());
                        }

                        updateBounds();
                        controller->put_IsVisible(TRUE);
                        ready = true;
                        return S_OK;
                    });

                HRESULT hr2 = env->CreateCoreWebView2Controller(parentHwnd, ctrlHandler);
                ctrlHandler->Release();
                if (FAILED(hr2)) {
                    creating = false;
                    runtimeMissing = true;
                }
                return S_OK;
            });

        HRESULT hr = createEnv(nullptr, userData.c_str(), nullptr, envHandler);
        envHandler->Release();
        if (FAILED(hr)) {
            logWarn("TelemetryBrowser: CreateCoreWebView2EnvironmentWithOptions failed (0x%08X)", static_cast<unsigned>(hr));
            creating = false;
            runtimeMissing = true;
            return false;
        }
        return true;
    }

    ~WebViewSession() {
        teardown();
        if (loaderDll) {
            FreeLibrary(loaderDll);
            loaderDll = nullptr;
        }
    }
};

#endif // !SWOS_TEST

} // namespace

bool isTelemetryServerReachable(const std::string& host, uint16_t port, int timeoutMs)
{
#ifdef SWOS_TEST
    (void)host;
    (void)port;
    (void)timeoutMs;
    return false;
#else
    std::wstring wHost = utf8ToWide(host);
    if (wHost.empty())
        return false;

    HINTERNET hSession = WinHttpOpen(L"SWOS-Port-Telemetry/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
        return false;

    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    bool reachable = false;
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/",
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {
                reachable = true;
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }

    WinHttpCloseHandle(hSession);
    return reachable;
#endif
}

void showTelemetryBrowserMenu()
{
#ifdef SWOS_TEST
    return;
#else
    SDL_Window *win = getWindow();
    SDL_Renderer *r = getRenderer();
    if (!win || !r)
        return;

    SDL_Texture *atlas = getCompactFontAtlas(r);
    if (!atlas)
        return;

    // Extract native Win32 HWND for WebView2 parenting
    SDL_SysWMinfo wmInfo{};
    SDL_VERSION(&wmInfo.version);
    HWND hwnd = nullptr;
    if (SDL_GetWindowWMInfo(win, &wmInfo) && wmInfo.subsystem == SDL_SYSWM_WINDOWS) {
        hwnd = wmInfo.info.win.window;
    }

    // Exclusive fullscreen doesn't composite child HWNDs cleanly; temporarily use borderless fullscreen
    Uint32 origFlags = SDL_GetWindowFlags(win);
    bool wasExclusiveFullscreen = ((origFlags & SDL_WINDOW_FULLSCREEN) != 0) &&
                                  ((origFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) != SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (wasExclusiveFullscreen) {
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninitCom = SUCCEEDED(comHr);

    constexpr int kTopBarH = 44;
    std::string urlStr = kDefaultTelemetryUrl;
    std::wstring urlWide = utf8ToWide(urlStr);

    bool serverOnline = isTelemetryServerReachable(kDefaultTelemetryHost, kDefaultTelemetryPort, 650);
    WebViewSession session;
    if (serverOnline && hwnd) {
        session.start(hwnd, kTopBarH, urlWide);
    }

    bool done = false;
    SDL_FRect btnBackRect{ 0, 0, 0, 0 };
    SDL_FRect btnReloadRect{ 0, 0, 0, 0 };
    SDL_FRect btnOfflineRetryRect{ 0, 0, 0, 0 };
    SDL_FRect btnOfflineBackRect{ 0, 0, 0, 0 };

    auto triggerRetry = [&]() {
        serverOnline = isTelemetryServerReachable(kDefaultTelemetryHost, kDefaultTelemetryPort, 650);
        if (serverOnline && hwnd) {
            session.start(hwnd, kTopBarH, urlWide);
        } else {
            session.teardown();
        }
    };

    while (!done) {
        // Pump SDL events
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                done = true;
            } else if (ev.type == SDL_WINDOWEVENT) {
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                    ev.window.event == SDL_WINDOWEVENT_MAXIMIZED ||
                    ev.window.event == SDL_WINDOWEVENT_RESTORED) {
                    session.updateBounds();
                }
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_BACKSPACE) {
                    done = true;
                } else if (ev.key.keysym.sym == SDLK_r || ev.key.keysym.sym == SDLK_F5 ||
                           (!serverOnline && (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_SPACE))) {
                    if (serverOnline && session.ready && session.webview)
                        session.webview->Reload();
                    else
                        triggerRetry();
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = static_cast<float>(ev.button.x);
                float my = static_cast<float>(ev.button.y);
                if (pointInRect(mx, my, btnBackRect)) {
                    done = true;
                } else if (pointInRect(mx, my, btnReloadRect)) {
                    if (serverOnline && session.ready && session.webview)
                        session.webview->Reload();
                    else
                        triggerRetry();
                } else if (!serverOnline || session.runtimeMissing) {
                    if (pointInRect(mx, my, btnOfflineBackRect))
                        done = true;
                    else if (pointInRect(mx, my, btnOfflineRetryRect))
                        triggerRetry();
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_RIGHT) {
                if (!serverOnline || session.runtimeMissing)
                    done = true;
            }
        }

        // Handle WebView2 async callbacks
        if (session.closeRequested) {
            done = true;
            break;
        }
        if (session.reloadRequested) {
            session.reloadRequested = false;
            if (session.webview)
                session.webview->Reload();
        }
        if (session.navFailedOffline) {
            session.navFailedOffline = false;
            session.teardown();
            serverOnline = false;
        }

        // Render SDL2 chrome (top navigation bar + offline / loading state)
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        if (winW < 160) winW = 640;
        if (winH < 100) winH = 400;

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_Rect origVp{};
        SDL_RenderGetViewport(r, &origVp);

        SDL_RenderSetViewport(r, nullptr);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

        // Background
        tbFillRectF(r, 0, 0, static_cast<float>(winW), static_cast<float>(winH), { 10, 15, 24, 255 });

        // Top Navigation Bar
        float barH = static_cast<float>(kTopBarH);
        tbFillRectF(r, 0, 0, static_cast<float>(winW), barH, { 16, 24, 38, 255 });
        tbFillRectF(r, 0, barH - 2.0f, static_cast<float>(winW), 2.0f, { 234, 198, 72, 220 });

        float barPx = 2.0f;
        // Left title badge + status dot + URL
        tbFillRectF(r, 12.0f, 8.0f, 126.0f, 26.0f, { 28, 42, 64, 255 });
        tbDrawRectF(r, 12.0f, 8.0f, 126.0f, 26.0f, { 234, 198, 72, 200 });
        drawCompactTextCentered(r, atlas, 75.0f, 13.0f, barPx, "TELEMETRY", { 255, 222, 76, 255 }, 255, true);

        SDL_Color dotCol = (serverOnline && !session.runtimeMissing)
            ? SDL_Color{ 64, 214, 112, 255 }
            : SDL_Color{ 226, 62, 68, 255 };
        tbFillRectF(r, 150.0f, 17.0f, 8.0f, 8.0f, dotCol);
        drawCompactText(r, atlas, 166.0f, 14.0f, 1.6f, "HTTP://LOCALHOST:3000", { 190, 206, 226, 255 }, 255, false);

        // Right buttons: [RELOAD] and [ESC BACK]
        float backW = 112.0f;
        float reloadW = 104.0f;
        btnBackRect = { static_cast<float>(winW) - backW - 12.0f, 8.0f, backW, 26.0f };
        btnReloadRect = { btnBackRect.x - reloadW - 8.0f, 8.0f, reloadW, 26.0f };

        int mouseX = 0, mouseY = 0;
        SDL_GetMouseState(&mouseX, &mouseY);
        float fmx = static_cast<float>(mouseX);
        float fmy = static_cast<float>(mouseY);

        bool hoverBack = pointInRect(fmx, fmy, btnBackRect);
        bool hoverReload = pointInRect(fmx, fmy, btnReloadRect);

        tbFillRectF(r, btnReloadRect.x, btnReloadRect.y, btnReloadRect.w, btnReloadRect.h,
            hoverReload ? SDL_Color{ 38, 62, 92, 255 } : SDL_Color{ 24, 38, 58, 255 });
        tbDrawRectF(r, btnReloadRect.x, btnReloadRect.y, btnReloadRect.w, btnReloadRect.h, { 112, 148, 186, 210 });
        drawCompactTextCentered(r, atlas, btnReloadRect.x + btnReloadRect.w * 0.5f, btnReloadRect.y + 7.0f, 1.5f, "[R] RELOAD", { 236, 244, 252, 255 }, 255, true);

        tbFillRectF(r, btnBackRect.x, btnBackRect.y, btnBackRect.w, btnBackRect.h,
            hoverBack ? SDL_Color{ 172, 46, 52, 255 } : SDL_Color{ 126, 32, 38, 255 });
        tbDrawRectF(r, btnBackRect.x, btnBackRect.y, btnBackRect.w, btnBackRect.h, { 244, 152, 156, 220 });
        drawCompactTextCentered(r, atlas, btnBackRect.x + btnBackRect.w * 0.5f, btnBackRect.y + 7.0f, 1.5f, "[ESC] BACK", { 255, 248, 248, 255 }, 255, true);

        // Main body area (visible when offline, runtime missing, or while WebView2 initializes)
        if (!serverOnline || session.runtimeMissing || !session.ready) {
            float bodyY = barH;
            float bodyH = static_cast<float>(winH) - barH;
            float cardW = std::min(static_cast<float>(winW) * 0.78f, 560.0f);
            float cardH = 220.0f;
            float cardX = std::floor((static_cast<float>(winW) - cardW) * 0.5f);
            float cardY = std::floor(bodyY + (bodyH - cardH) * 0.5f);

            tbFillRectF(r, cardX + 5.0f, cardY + 5.0f, cardW, cardH, { 0, 0, 0, 150 });
            tbFillRectF(r, cardX, cardY, cardW, cardH, { 14, 21, 34, 248 });
            tbDrawRectF(r, cardX, cardY, cardW, cardH, { 234, 198, 72, 220 });

            if (!serverOnline) {
                // Red OFFLINE pill
                float pillW = 110.0f;
                tbFillRectF(r, cardX + (cardW - pillW) * 0.5f, cardY + 24.0f, pillW, 24.0f, { 166, 34, 42, 255 });
                tbDrawRectF(r, cardX + (cardW - pillW) * 0.5f, cardY + 24.0f, pillW, 24.0f, { 248, 128, 134, 220 });
                drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, cardY + 30.0f, 1.6f, "OFFLINE", { 255, 246, 246, 255 }, 255, true);

                drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, cardY + 68.0f, 2.2f,
                    "TELEMETRY SERVER IS OFFLINE", { 255, 222, 76, 255 }, 255, true);
                drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, cardY + 104.0f, 1.4f,
                    "START YOUR NEXT.JS APP ON HTTP://LOCALHOST:3000", { 176, 194, 216, 255 }, 255, false);

                float bW = 186.0f, bH = 34.0f, gap = 16.0f;
                float startX = cardX + (cardW - (bW * 2.0f + gap)) * 0.5f;
                float bY = cardY + 150.0f;
                btnOfflineRetryRect = { startX, bY, bW, bH };
                btnOfflineBackRect = { startX + bW + gap, bY, bW, bH };

                bool hovR = pointInRect(fmx, fmy, btnOfflineRetryRect);
                bool hovB = pointInRect(fmx, fmy, btnOfflineBackRect);

                tbFillRectF(r, btnOfflineRetryRect.x, btnOfflineRetryRect.y, btnOfflineRetryRect.w, btnOfflineRetryRect.h,
                    hovR ? SDL_Color{ 46, 134, 82, 255 } : SDL_Color{ 32, 98, 60, 255 });
                tbDrawRectF(r, btnOfflineRetryRect.x, btnOfflineRetryRect.y, btnOfflineRetryRect.w, btnOfflineRetryRect.h, { 120, 232, 160, 220 });
                drawCompactTextCentered(r, atlas, btnOfflineRetryRect.x + bW * 0.5f, bY + 11.0f, 1.5f, "[R] RETRY CONNECTION", { 248, 255, 250, 255 }, 255, true);

                tbFillRectF(r, btnOfflineBackRect.x, btnOfflineBackRect.y, btnOfflineBackRect.w, btnOfflineBackRect.h,
                    hovB ? SDL_Color{ 58, 72, 94, 255 } : SDL_Color{ 34, 44, 62, 255 });
                tbDrawRectF(r, btnOfflineBackRect.x, btnOfflineBackRect.y, btnOfflineBackRect.w, btnOfflineBackRect.h, { 142, 162, 190, 200 });
                drawCompactTextCentered(r, atlas, btnOfflineBackRect.x + bW * 0.5f, bY + 11.0f, 1.5f, "[ESC] BACK TO HUB", { 240, 246, 252, 255 }, 255, true);
            } else if (session.runtimeMissing) {
                drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, cardY + 70.0f, 2.0f,
                    "EDGE WEBVIEW2 RUNTIME MISSING", { 255, 196, 76, 255 }, 255, true);
                drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, cardY + 106.0f, 1.4f,
                    "ENSURE WEBVIEW2LOADER.DLL & EDGE RUNTIME ARE INSTALLED", { 176, 194, 216, 255 }, 255, false);

                float bW = 200.0f, bH = 34.0f;
                btnOfflineRetryRect = { 0, 0, 0, 0 };
                btnOfflineBackRect = { cardX + (cardW - bW) * 0.5f, cardY + 150.0f, bW, bH };
                tbFillRectF(r, btnOfflineBackRect.x, btnOfflineBackRect.y, bW, bH, { 42, 54, 74, 255 });
                tbDrawRectF(r, btnOfflineBackRect.x, btnOfflineBackRect.y, bW, bH, { 160, 180, 208, 210 });
                drawCompactTextCentered(r, atlas, btnOfflineBackRect.x + bW * 0.5f, btnOfflineBackRect.y + 11.0f, 1.5f, "[ESC] BACK TO HUB", { 248, 250, 252, 255 }, 255, true);
            } else {
                drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, cardY + 86.0f, 2.0f,
                    "CONNECTING TO TELEMETRY...", { 255, 222, 76, 255 }, 255, true);
                drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, cardY + 122.0f, 1.4f,
                    "LOADING HTTP://LOCALHOST:3000", { 176, 194, 216, 255 }, 255, false);
            }
        }

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_RenderSetScale(r, origSX, origSY);
        SDL_RenderSetViewport(r, &origVp);
        SDL_RenderPresent(r);

        SDL_Delay(12);
    }

    // Clean teardown & focus restoration
    session.teardown();

    if (shouldUninitCom)
        CoUninitialize();

    if (wasExclusiveFullscreen)
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN);

    if (hwnd)
        SetFocus(hwnd);
    SDL_RaiseWindow(win);

    // Drain any buffered key/mouse events so ESC doesn't bounce out of Career Hub
    SDL_Event flushEv;
    while (SDL_PollEvent(&flushEv)) {
        if (flushEv.type == SDL_QUIT) {
            SDL_PushEvent(&flushEv);
            break;
        }
    }
#endif
}
