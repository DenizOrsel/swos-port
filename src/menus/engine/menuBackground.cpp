#include "menuBackground.h"
#include "menuItemRenderer.h"
#include "assetManager.h"
#include "windowManager.h"
#include "gameFieldMapping.h"
#include "game.h"
#include "file.h"
#include "util.h"

static std::string m_backgroundName;
static SDL_Texture *m_background;

static void loadMenuBackground(const std::string& name);
static void reloadMenuBackground(AssetResolution = AssetResolution::kInvalid, AssetResolution = AssetResolution::kInvalid);
static SDL_RWops *openImageFile(const std::string& path);

void initMenuBackground()
{
    registerAssetResolutionChangeHandler(reloadMenuBackground);
    setStandardMenuBackgroundImage();
    initMenuItemRenderer();
}

void drawMenuBackground()
{
    auto renderer = getRenderer();

    if (m_background) {
        SDL_Rect src{ 0, 0 };
        SDL_QueryTexture(m_background, nullptr, nullptr, &src.w, &src.h);

        auto w = static_cast<float>(src.w);
        auto h = static_cast<float>(src.h);

        auto viewPort = getViewport();
        auto scale = std::max(viewPort.w / w, viewPort.h / h);

        w *= scale;
        h *= scale;

        SDL_FRect dst{ (viewPort.w - w) / 2, (viewPort.h - h) / 2, w, h };
        SDL_RenderCopyF(renderer, m_background, &src, &dst);
    } else {
        // Sleek fallback stadium-navy gradient when swtitle/stad background image is not present
        auto viewPort = getViewport();
        constexpr int kBands = 16;
        float bandH = static_cast<float>(viewPort.h) / static_cast<float>(kBands);
        for (int b = 0; b < kBands; ++b) {
            float t = static_cast<float>(b) / static_cast<float>(kBands - 1);
            Uint8 rr = static_cast<Uint8>(10.0f + t * 8.0f);
            Uint8 gg = static_cast<Uint8>(18.0f + t * 22.0f);
            Uint8 bb = static_cast<Uint8>(34.0f - t * 6.0f);
            SDL_SetRenderDrawColor(renderer, rr, gg, bb, 255);
            SDL_FRect band{ 0.0f, std::floor(b * bandH), static_cast<float>(viewPort.w), std::ceil(bandH) + 1.0f };
            SDL_RenderFillRectF(renderer, &band);
        }
    }
}

void setStandardMenuBackgroundImage()
{
    constexpr char kBackgroundImageBasename[] = "swtitle";
    setMenuBackgroundImage(kBackgroundImageBasename);
}

void setMenuBackgroundImage(const char *name)
{
    if (strcmp(m_backgroundName.c_str(), name)) {
        m_backgroundName = name;
        loadMenuBackground(name);
    }
}

void unloadMenuBackground()
{
    if (m_background) {
        logInfo("Destroying menu background");
        SDL_DestroyTexture(m_background);
    }
    m_background = nullptr;
    m_backgroundName.clear();
}

// Not a reference since background name gets cleared when the old background is destroyed.
static void loadMenuBackground(const std::string& name)
{
    if (name.empty() || m_background)
        unloadMenuBackground();
    if (!name.empty()) {
        auto path = getPathInAssetDir(name.c_str());
        for (int attempt = 0; attempt < 10; ++attempt) {
            if (auto f = openImageFile(path)) {
                m_background = IMG_LoadTexture_RW(getRenderer(), f, true);
                if (m_background) {
                    m_backgroundName = name;
                    logInfo("Menu background set to \"%s\"", name.c_str());
                    break;
                } else {
                    logWarn("Failed to load background image \"%s\" (attempt %d), error: %s", name.c_str(), attempt + 1, IMG_GetError());
                    if (auto renderer = getRenderer())
                        SDL_RenderPresent(renderer);
                    SDL_Delay(50);
                }
            } else {
                logWarn("Failed to open background image file \"%s\"", name.c_str());
                break;
            }
        }
    }
}

static void reloadMenuBackground(AssetResolution, AssetResolution)
{
    // copy the string so the reference doesn't get cleared on unload
    auto backgroundName = m_backgroundName;
    loadMenuBackground(backgroundName);
}

static SDL_RWops *openImageFile(const std::string& path)
{
    for (auto ext : { "png", "jpg", "jpeg", "bmp", }) {
        const auto rel = path + '.' + ext;
        const auto rootPath = pathInRootDir(rel.c_str());
        if (auto f = SDL_RWFromFile(rootPath.c_str(), "rb"))
            return f;
        const auto exePath = pathInExeDir(rel.c_str());
        if (exePath != rootPath) {
            if (auto f = SDL_RWFromFile(exePath.c_str(), "rb"))
                return f;
        }
    }

    return nullptr;
}
