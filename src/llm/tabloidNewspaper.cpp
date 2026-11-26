#include "tabloidNewspaper.h"
#include "clubLogo.h"
#include "compactFont.h"
#include "controls.h"
#include "file.h"
#include "gameControls.h"
#include "gameFieldMapping.h"
#include "keyBuffer.h"
#include "log.h"
#include "menuBackground.h"
#include "menus.h"
#include "render.h"
#include "swos.h"

#include <SDL_image.h>

#if defined(_WIN32) && !defined(SWOS_TEST)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

namespace {

constexpr int kPhotoWidth = 224;
constexpr int kPhotoHeight = 140;

#ifndef SWOS_TEST
static SDL_Texture *getMotmPortraitTexture(SDL_Renderer *r, const TabloidMotmCard& motm)
{
    static SDL_Texture *s_portraits[4] = {};
    static bool s_attempted[4] = {};
    static SDL_Renderer *s_cachedRenderer = nullptr;
    if (s_cachedRenderer != r) {
        for (int i = 0; i < 4; ++i) {
            if (s_portraits[i]) {
                SDL_DestroyTexture(s_portraits[i]);
                s_portraits[i] = nullptr;
            }
            s_attempted[i] = false;
        }
        s_cachedRenderer = r;
    }

    int idx = 0;
    if (motm.pos == "GK") {
        idx = 3; // motm_4.png = retro goalkeeper bust
    } else {
        uint32_t h = 2166136261u;
        for (unsigned char c : motm.name)
            h = (h ^ c) * 16777619u;
        idx = static_cast<int>(h % 3u); // motm_1..3.png = varied 90s outfielder portraits
    }

    if (s_attempted[idx])
        return s_portraits[idx];
    s_attempted[idx] = true;

    std::string rel = "assets/newspaper/motm_" + std::to_string(idx + 1) + ".png";
    std::string full = pathInRootDir(rel.c_str());
    SDL_Texture *tex = IMG_LoadTexture(r, full.c_str());
    if (!tex)
        tex = IMG_LoadTexture(r, rel.c_str());
    if (tex) {
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
        s_portraits[idx] = tex;
    }
    return s_portraits[idx];
}
#endif

struct CapturedMatchPhoto {
    bool valid = false;
    int priority = 0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // Static 1996/97 action photo (ABGR8888)
    std::string caption;
};

CapturedMatchPhoto s_photo;
bool s_capturePending = false;
int s_pendingPriority = 0;
int s_pendingDelayFrames = 0;
std::string s_pendingCaption;

std::string trimCopy(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string toUpperAscii(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string cleanTabloidText(const std::string& in)
{
    std::string s = sanitizeCompactMixedCase(in);
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '*' || c == '#' || c == '`')
            continue;
        out.push_back(c);
    }
    return trimCopy(out);
}

SDL_Color swosKitColorRgb(uint8_t colorIdx)
{
    switch (colorIdx & 0x0Fu) {
    case 0: return { 155, 160, 168, 255 }; // gray
    case 1: return { 236, 234, 224, 255 }; // off-white
    case 2: return { 36, 38, 42, 255 };    // black
    case 3: return { 218, 126, 38, 255 };  // orange
    case 4: return { 196, 44, 48, 255 };   // red
    case 5: return { 42, 88, 184, 255 };   // blue
    case 6: return { 126, 80, 48, 255 };   // brown
    case 7: return { 96, 174, 218, 255 };  // light blue
    case 8: return { 48, 152, 68, 255 };   // green
    case 9: return { 226, 190, 48, 255 };  // yellow
    default: return { 84, 138, 194, 255 };
    }
}

bool stripTaggedPrefix(const std::string& line, const char *tag, std::string& outValue)
{
    std::string up = toUpperAscii(line);
    std::string tagUp = toUpperAscii(tag);
    if (up.rfind(tagUp, 0) != 0)
        return false;
    size_t pos = tagUp.size();
    while (pos < line.size() && (line[pos] == ':' || line[pos] == '-' || std::isspace(static_cast<unsigned char>(line[pos]))))
        ++pos;
    outValue = trimCopy(line.substr(pos));
    return true;
}

#ifndef SWOS_TEST

void fillRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color col)
{
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_FRect fr{ x, y, w, h };
    SDL_RenderFillRectF(r, &fr);
}

void drawRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color col)
{
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_FRect fr{ x, y, w, h };
    SDL_RenderDrawRectF(r, &fr);
}

#ifdef _WIN32
struct SerifTex {
    SDL_Texture *tex = nullptr;
    int w = 0;
    int h = 0;
    void destroy()
    {
        if (tex) {
            SDL_DestroyTexture(tex);
            tex = nullptr;
        }
        w = 0;
        h = 0;
    }
};

SerifTex rasterizeTimesNewRoman(
    SDL_Renderer *r,
    const std::string& utf8Text,
    int maxW,
    int maxH,
    int desiredPx,
    int minPx,
    bool bold,
    bool italic,
    bool centered,
    SDL_Color col,
    bool singleLine = false)
{
    SerifTex out;
    if (!r || utf8Text.empty() || maxW <= 4 || maxH <= 4)
        return out;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, nullptr, 0);
    if (wlen <= 1)
        return out;
    std::wstring wtext(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, wtext.data(), wlen);

    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc)
        return out;

    UINT baseFlags = DT_NOPREFIX | (centered ? DT_CENTER : DT_LEFT) |
                     (singleLine ? (DT_SINGLELINE | DT_END_ELLIPSIS) : DT_WORDBREAK);

    int chosenPx = std::max(minPx, desiredPx);
    int measuredW = maxW;
    int measuredH = maxH;
    HFONT bestFont = nullptr;

    for (int pxTry = std::max(minPx, desiredPx); pxTry >= minPx; --pxTry) {
        HFONT f = CreateFontW(
            -pxTry, 0, 0, 0,
            bold ? FW_BOLD : FW_NORMAL,
            italic ? TRUE : FALSE,
            FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_TT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,
            VARIABLE_PITCH | FF_ROMAN,
            L"Times New Roman");
        if (!f)
            continue;

        HGDIOBJ oldF = SelectObject(hdc, f);
        RECT rc{ 0, 0, maxW, 0 };
        DrawTextW(hdc, wtext.c_str(), -1, &rc, baseFlags | DT_CALCRECT);
        SelectObject(hdc, oldF);

        int reqW = std::max(1, static_cast<int>(rc.right - rc.left));
        int reqH = std::max(1, static_cast<int>(rc.bottom - rc.top));
        bool fits = singleLine ? (reqW <= maxW && reqH <= maxH) : (reqH <= maxH && reqW <= maxW + 2);

        if (bestFont)
            DeleteObject(bestFont);
        bestFont = f;
        chosenPx = pxTry;
        measuredW = centered ? maxW : std::min(maxW, reqW + 2);
        measuredH = std::min(maxH, reqH + 2);

        if (fits)
            break;
    }

    if (!bestFont) {
        DeleteDC(hdc);
        return out;
    }

    int texW = std::max(1, std::min(maxW, measuredW));
    int texH = std::max(1, std::min(maxH, measuredH));

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = texW;
    bmi.bmiHeader.biHeight = -texH; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *dibBits = nullptr;
    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    if (!bmp || !dibBits) {
        if (bmp) DeleteObject(bmp);
        DeleteObject(bestFont);
        DeleteDC(hdc);
        return out;
    }

    std::memset(dibBits, 0, static_cast<size_t>(texW) * static_cast<size_t>(texH) * 4u);
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);
    HGDIOBJ oldFont = SelectObject(hdc, bestFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    RECT drawRc{ 0, 0, texW, texH };
    DrawTextW(hdc, wtext.c_str(), -1, &drawRc, baseFlags);
    GdiFlush();

    // Convert grayscale GDI coverage into colored ABGR8888 (R, G, B, A bytes) for crisp alpha blending on newsprint
    std::vector<uint8_t> rgba(static_cast<size_t>(texW) * static_cast<size_t>(texH) * 4u);
    const auto *src = static_cast<const uint8_t *>(dibBits);
    const size_t pixelCount = static_cast<size_t>(texW) * static_cast<size_t>(texH);
    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t b = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t rr = src[i * 4 + 2];
        uint8_t cov = std::max(rr, std::max(g, b));
        rgba[i * 4 + 0] = col.r;
        rgba[i * 4 + 1] = col.g;
        rgba[i * 4 + 2] = col.b;
        rgba[i * 4 + 3] = static_cast<uint8_t>((static_cast<uint32_t>(cov) * col.a) / 255u);
    }

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBmp);
    DeleteObject(bmp);
    DeleteObject(bestFont);
    DeleteDC(hdc);

    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, texW, texH);
    if (tex) {
        SDL_UpdateTexture(tex, nullptr, rgba.data(), texW * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        out.tex = tex;
        out.w = texW;
        out.h = texH;
    }
    (void)chosenPx;
    return out;
}

void drawSerifTex(SDL_Renderer *r, const SerifTex& st, float x, float y)
{
    if (!st.tex || st.w <= 0 || st.h <= 0)
        return;
    SDL_FRect dst{ std::floor(x), std::floor(y), static_cast<float>(st.w), static_cast<float>(st.h) };
    SDL_RenderCopyF(r, st.tex, nullptr, &dst);
}
#endif

// Procedural pixel-art SWOS footballer sprite drawn in club's actual kit colors (12x16 logical units)
void drawPixelSwosPlayerSprite(SDL_Renderer *r, float x, float y, float u, const TabloidKitColors& kit, bool celebrating)
{
    SDL_Color shirt = swosKitColorRgb(kit.shirtCol);
    SDL_Color stripe = swosKitColorRgb(kit.stripesCol);
    SDL_Color shorts = swosKitColorRgb(kit.shortsCol);
    SDL_Color socks = swosKitColorRgb(kit.socksCol);
    SDL_Color skin{ 228, 178, 136, 255 };
    SDL_Color hair{ 64, 42, 26, 255 };
    SDL_Color boots{ 30, 28, 26, 255 };
    SDL_Color shadow{ 42, 38, 30, 90 };

    auto pxRect = [&](float gx, float gy, float gw, float gh, SDL_Color c) {
        fillRectF(r, x + gx * u, y + gy * u, gw * u, gh * u, c);
    };

    // Ground shadow
    pxRect(2, 15, 8, 1, shadow);

    // Head & hair
    pxRect(4, 1, 4, 2, hair);
    pxRect(4, 3, 4, 2, skin);
    pxRect(5, 3.5f, 0.8f, 0.8f, boots);
    pxRect(6.5f, 3.5f, 0.8f, 0.8f, boots);

    // Arms (raised if celebrating!)
    if (celebrating) {
        pxRect(1, 2, 2, 3, shirt);
        pxRect(1, 1, 2, 1, skin);
        pxRect(9, 2, 2, 3, shirt);
        pxRect(9, 1, 2, 1, skin);
    } else {
        pxRect(1, 5, 2, 4, shirt);
        pxRect(1, 9, 2, 1, skin);
        pxRect(9, 5, 2, 4, shirt);
        pxRect(9, 9, 2, 1, skin);
    }

    // Jersey torso with vertical stripe detail
    pxRect(3, 5, 6, 5, shirt);
    pxRect(5, 5, 2, 5, stripe);
    // Collar
    pxRect(5, 5, 2, 1, { 244, 240, 228, 255 });

    // Shorts
    pxRect(3, 10, 6, 2.5f, shorts);
    pxRect(5.5f, 11.5f, 1, 1, { 28, 26, 24, 120 });

    // Legs, socks & boots
    pxRect(3.2f, 12.5f, 2, 1, skin);
    pxRect(6.8f, 12.5f, 2, 1, skin);
    pxRect(3.2f, 13.5f, 2, 1.2f, socks);
    pxRect(6.8f, 13.5f, 2, 1.2f, socks);
    pxRect(2.8f, 14.5f, 2.6f, 1.0f, boots);
    pxRect(6.6f, 14.5f, 2.6f, 1.0f, boots);
}

// Retro 1996/97 pixel-art club shield crest
void drawPixelClubCrest(SDL_Renderer *r, float x, float y, float u, const TabloidKitColors& kit)
{
    SDL_Color rim{ 34, 30, 26, 255 };
    SDL_Color gold{ 216, 178, 62, 255 };
    SDL_Color primary = swosKitColorRgb(kit.shirtCol);
    SDL_Color accent = swosKitColorRgb(kit.stripesCol);

    auto pxRect = [&](float gx, float gy, float gw, float gh, SDL_Color c) {
        fillRectF(r, x + gx * u, y + gy * u, gw * u, gh * u, c);
    };

    pxRect(0, 0, 12, 9, rim);
    pxRect(1, 9, 10, 2, rim);
    pxRect(2, 11, 8, 2, rim);
    pxRect(4, 13, 4, 1, rim);

    pxRect(1, 1, 10, 8, gold);
    pxRect(2, 9, 8, 2, gold);
    pxRect(3, 11, 6, 1, gold);

    pxRect(2, 2, 8, 7, primary);
    pxRect(3, 9, 6, 1.5f, primary);
    pxRect(4, 2, 2, 8, accent);
    pxRect(7, 2, 2, 7.5f, accent);
}

// Retro pixel football icon (10x10 units)
void drawPixelFootball(SDL_Renderer *r, float x, float y, float u)
{
    SDL_Color dark{ 30, 28, 24, 255 };
    SDL_Color ball{ 244, 240, 228, 255 };
    auto pxRect = [&](float gx, float gy, float gw, float gh, SDL_Color c) {
        fillRectF(r, x + gx * u, y + gy * u, gw * u, gh * u, c);
    };
    pxRect(2, 0, 6, 10, dark);
    pxRect(0, 2, 10, 6, dark);
    pxRect(1, 1, 8, 8, dark);
    pxRect(2, 1, 6, 8, ball);
    pxRect(1, 2, 8, 6, ball);
    pxRect(4, 4, 2, 2, dark);
    pxRect(2, 2, 1.5f, 1.5f, dark);
    pxRect(6.5f, 2, 1.5f, 1.5f, dark);
    pxRect(2, 6.5f, 1.5f, 1.5f, dark);
    pxRect(6.5f, 6.5f, 1.5f, 1.5f, dark);
}

// Fallback vintage 90s sports graphic when match was fast-simulated (no live pitch photo)
void drawFallbackMatchupPhoto(SDL_Renderer *r, SDL_Texture *atlas, const SDL_FRect& box,
    const TabloidNewspaperEdition& ed, float px)
{
    // Striped vintage 90s floodlit pitch backdrop in warm newsprint greens
    int bands = 7;
    float bandH = box.h / static_cast<float>(bands);
    for (int i = 0; i < bands; ++i) {
        SDL_Color c = (i < 2)
            ? SDL_Color{ 44, 62, 78, 255 }      // dusk stadium sky
            : ((i % 2 == 0) ? SDL_Color{ 62, 126, 66, 255 } : SDL_Color{ 74, 140, 74, 255 });
        fillRectF(r, box.x, box.y + i * bandH, box.w, bandH + 1.0f, c);
    }

    // Floodlight glow dots
    fillRectF(r, box.x + box.w * 0.14f, box.y + box.h * 0.08f, box.w * 0.06f, box.h * 0.07f, { 246, 238, 198, 235 });
    fillRectF(r, box.x + box.w * 0.80f, box.y + box.h * 0.08f, box.w * 0.06f, box.h * 0.07f, { 246, 238, 198, 235 });

    // Pitch touchline & centre circle hint
    fillRectF(r, box.x + box.w * 0.06f, box.y + box.h * 0.30f, box.w * 0.88f, 1.5f, { 232, 228, 210, 175 });
    fillRectF(r, box.x + box.w * 0.50f - 0.75f, box.y + box.h * 0.30f, 1.5f, box.h * 0.64f, { 232, 228, 210, 145 });

    // Both clubs' pixel SWOS players + crests flanking the retro scoreboard
    float unit = std::max(1.5f, box.h / 32.0f);
    bool t1Won = ed.playingPenalties ? (ed.penGoals1 > ed.penGoals2) : (ed.goals1 > ed.goals2);
    bool t2Won = ed.playingPenalties ? (ed.penGoals2 > ed.penGoals1) : (ed.goals2 > ed.goals1);

    drawPixelClubCrest(r, box.x + box.w * 0.08f, box.y + box.h * 0.14f, unit * 0.72f, ed.kit1);
    drawPixelClubCrest(r, box.x + box.w * 0.83f, box.y + box.h * 0.14f, unit * 0.72f, ed.kit2);

    drawPixelSwosPlayerSprite(r, box.x + box.w * 0.19f, box.y + box.h * 0.36f, unit * 1.15f, ed.kit1, t1Won);
    drawPixelSwosPlayerSprite(r, box.x + box.w * 0.69f, box.y + box.h * 0.36f, unit * 1.15f, ed.kit2, t2Won);
    drawPixelFootball(r, box.x + box.w * 0.50f - unit * 4.0f, box.y + box.h * 0.70f, unit * 0.8f);

    // Vintage score plate in center
    SDL_FRect plate{ box.x + box.w * 0.31f, box.y + box.h * 0.33f, box.w * 0.38f, box.h * 0.28f };
    fillRectF(r, plate.x, plate.y, plate.w, plate.h, { 24, 24, 22, 225 });
    drawRectF(r, plate.x, plate.y, plate.w, plate.h, { 236, 222, 178, 230 });
    std::string sc = std::to_string(ed.goals1) + " - " + std::to_string(ed.goals2);
    drawCompactTextCentered(r, atlas, plate.x + plate.w * 0.5f, plate.y + plate.h * 0.24f,
        px * 1.45f, sc, { 248, 222, 88, 255 }, 255, true);
}

// Save the rendered newspaper sheet cleanly to screenshots/daily-echo-YYYYMMDD-HHMMSS.png
bool saveNewspaperRectToPng(SDL_Renderer *r, const SDL_Rect& captureRect, std::string& outFilename)
{
    if (!r || captureRect.w <= 16 || captureRect.h <= 16)
        return false;

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, captureRect.w, captureRect.h, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!surf)
        return false;

    if (SDL_RenderReadPixels(r, &captureRect, SDL_PIXELFORMAT_ABGR8888, surf->pixels, surf->pitch) < 0) {
        logWarn("Newspaper PNG save: SDL_RenderReadPixels failed: %s", SDL_GetError());
        SDL_FreeSurface(surf);
        return false;
    }

    std::string dir = pathInRootDir("screenshots");
    createDir(dir.c_str());

    std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &now);
    char fname[64];
    std::snprintf(fname, sizeof(fname), "daily-echo-%04d%02d%02d-%02d%02d%02d.png",
        tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
        tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    std::string fullPath = joinPaths(dir.c_str(), fname);

    bool ok = (IMG_SavePNG(surf, fullPath.c_str()) == 0);
    SDL_FreeSurface(surf);
    if (ok) {
        outFilename = fname;
        logInfo("Saved Daily Soccer Echo tabloid edition to %s", fullPath.c_str());
    } else {
        logWarn("Failed to save Daily Soccer Echo PNG: %s", IMG_GetError());
    }
    return ok;
}

#endif // !SWOS_TEST

} // namespace

// --- Live Match Action Photo Capture ---

void resetTabloidMatchPhoto()
{
    s_photo = CapturedMatchPhoto{};
    s_capturePending = false;
    s_pendingPriority = 0;
    s_pendingDelayFrames = 0;
    s_pendingCaption.clear();
}

void requestTabloidMatchPhoto(int priority, const std::string& caption, int delayFrames)
{
    if (swos.g_trainingGame || priority <= 0)
        return;

    // Upgrade if higher priority OR equal priority (later dramatic goal replaces earlier routine goal)
    if (s_photo.valid && priority < s_photo.priority)
        return;
    if (s_capturePending && priority < s_pendingPriority)
        return;

    s_capturePending = true;
    s_pendingPriority = priority;
    s_pendingDelayFrames = std::clamp(delayFrames, 0, 30);
    s_pendingCaption = cleanTabloidText(caption);
}

bool hasTabloidMatchPhoto()
{
    return s_photo.valid && !s_photo.rgba.empty();
}

std::string getTabloidMatchPhotoCaption()
{
    return s_photo.valid ? s_photo.caption : std::string();
}

void captureTabloidMatchPhotoIfPending(float cameraXOffset, float cameraYOffset)
{
#ifdef SWOS_TEST
    (void)cameraXOffset;
    (void)cameraYOffset;
    return;
#else
    if (!s_capturePending || swos.g_trainingGame)
        return;

    if (s_pendingDelayFrames > 0) {
        --s_pendingDelayFrames;
        return;
    }

    SDL_Renderer *r = getRenderer();
    if (!r) {
        s_capturePending = false;
        return;
    }

    float origSX = 1.0f, origSY = 1.0f;
    SDL_RenderGetScale(r, &origSX, &origSY);
    SDL_RenderSetScale(r, 1.0f, 1.0f);

    int outW = 0, outH = 0;
    if (SDL_GetRendererOutputSize(r, &outW, &outH) != 0 || outW < 160 || outH < 100) {
        SDL_RenderSetScale(r, origSX, origSY);
        s_capturePending = false;
        return;
    }

    // Center the 16:10 photo crop around the ball/action on screen (clamped cleanly inside viewport)
    float gameScale = getGameScale();
    float offX = getGameScreenOffsetX();
    float offY = getGameScreenOffsetY();

    float ballScreenX = static_cast<float>(outW) * 0.5f;
    float ballScreenY = static_cast<float>(outH) * 0.5f;
    if (gameScale > 0.1f) {
        float bx = swos.ballSprite.x.asFloat() - cameraXOffset;
        float by = swos.ballSprite.y.asFloat() - cameraYOffset;
        if (bx >= 0.0f && bx <= static_cast<float>(kVgaWidth) &&
            by >= 0.0f && by <= static_cast<float>(kVgaHeight)) {
            ballScreenX = offX + bx * gameScale;
            ballScreenY = offY + by * gameScale;
        }
    }

    int cropH = std::clamp(static_cast<int>(static_cast<float>(outH) * 0.64f), 100, outH);
    int cropW = std::clamp((cropH * kPhotoWidth) / kPhotoHeight, 160, outW);
    if (cropW > outW) {
        cropW = outW;
        cropH = std::max(80, (cropW * kPhotoHeight) / kPhotoWidth);
    }

    int minX = std::max(0, static_cast<int>(offX));
    int maxX = std::min(outW, static_cast<int>(offX + static_cast<float>(kVgaWidth) * gameScale));
    if (maxX - minX < cropW) {
        minX = 0;
        maxX = outW;
    }
    int cx = std::clamp(static_cast<int>(ballScreenX) - cropW / 2, minX, std::max(minX, maxX - cropW));
    int cy = std::clamp(static_cast<int>(ballScreenY) - cropH / 2, 0, std::max(0, outH - cropH));

    SDL_Rect cropRect{ cx, cy, cropW, cropH };
    std::vector<uint8_t> raw(static_cast<size_t>(cropW) * static_cast<size_t>(cropH) * 4u);
    int rc = SDL_RenderReadPixels(r, &cropRect, SDL_PIXELFORMAT_ABGR8888, raw.data(), cropW * 4);
    SDL_RenderSetScale(r, origSX, origSY);
    s_capturePending = false;

    if (rc < 0) {
        logWarn("Tabloid photo capture failed: %s", SDL_GetError());
        return;
    }

    // Crisp area downsample into kPhotoWidth x kPhotoHeight
    std::vector<uint8_t> scaled(static_cast<size_t>(kPhotoWidth) * static_cast<size_t>(kPhotoHeight) * 4u);
    for (int dy = 0; dy < kPhotoHeight; ++dy) {
        int sy0 = (dy * cropH) / kPhotoHeight;
        int sy1 = std::min(cropH - 1, ((dy + 1) * cropH) / kPhotoHeight);
        for (int dx = 0; dx < kPhotoWidth; ++dx) {
            int sx0 = (dx * cropW) / kPhotoWidth;
            int sx1 = std::min(cropW - 1, ((dx + 1) * cropW) / kPhotoWidth);
            uint32_t sumR = 0, sumG = 0, sumB = 0, cnt = 0;
            for (int sy = sy0; sy <= sy1; ++sy) {
                const uint8_t *row = raw.data() + (static_cast<size_t>(sy) * static_cast<size_t>(cropW)) * 4u;
                for (int sx = sx0; sx <= sx1; ++sx) {
                    const uint8_t *p = row + static_cast<size_t>(sx) * 4u;
                    sumR += p[0];
                    sumG += p[1];
                    sumB += p[2];
                    ++cnt;
                }
            }
            if (cnt == 0) cnt = 1;
            uint8_t *dst = scaled.data() + (static_cast<size_t>(dy) * static_cast<size_t>(kPhotoWidth) + static_cast<size_t>(dx)) * 4u;
            dst[0] = static_cast<uint8_t>(sumR / cnt);
            dst[1] = static_cast<uint8_t>(sumG / cnt);
            dst[2] = static_cast<uint8_t>(sumB / cnt);
            dst[3] = 255;
        }
    }

    apply90sTabloidColorPrintFilter(scaled, kPhotoWidth, kPhotoHeight);

    s_photo.valid = true;
    s_photo.priority = s_pendingPriority;
    s_photo.width = kPhotoWidth;
    s_photo.height = kPhotoHeight;
    s_photo.rgba = std::move(scaled);
    s_photo.caption = s_pendingCaption;
#endif
}

// --- Pure Deterministic Helpers ---

void apply90sTabloidColorPrintFilter(std::vector<uint8_t>& rgbaPixels, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (rgbaPixels.size() < expected)
        return;

    // 4x4 Bayer halftone offset matrix (-7..+7) for subtle 1996/97 color newsprint grain
    static constexpr int kBayer4x4[4][4] = {
        { -7,  1, -5,  3 },
        {  5, -3,  7, -1 },
        { -4,  4, -6,  2 },
        {  8,  0,  6, -2 },
    };

    const float halfW = static_cast<float>(width) * 0.5f;
    const float halfH = static_cast<float>(height) * 0.5f;

    for (int y = 0; y < height; ++y) {
        float ny = (static_cast<float>(y) - halfH) / std::max(1.0f, halfH);
        for (int x = 0; x < width; ++x) {
            float nx = (static_cast<float>(x) - halfW) / std::max(1.0f, halfW);
            size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;

            float r = static_cast<float>(rgbaPixels[idx + 0]);
            float g = static_cast<float>(rgbaPixels[idx + 1]);
            float b = static_cast<float>(rgbaPixels[idx + 2]);

            // 1. Slight 90s tabloid ink desaturation (~18%) so SWOS pitch & kits keep authentic vividness without neon glare
            float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            r = luma + (r - luma) * 0.82f;
            g = luma + (g - luma) * 0.82f;
            b = luma + (b - luma) * 0.82f;

            // 2. Warm offset newsprint paper tint (creamy highlights, lifted ink blacks)
            r = r * 0.94f + 20.0f;
            g = g * 0.92f + 16.0f;
            b = b * 0.84f + 10.0f;

            // 3. Subtle 90s press edge vignette + ordered halftone dot grain
            float distSq = nx * nx + ny * ny;
            float vig = 1.0f - std::clamp(distSq * 0.08f, 0.0f, 0.14f);
            float grain = static_cast<float>(kBayer4x4[y & 3][x & 3]) * 0.85f;

            rgbaPixels[idx + 0] = static_cast<uint8_t>(std::clamp(r * vig + grain, 18.0f, 246.0f));
            rgbaPixels[idx + 1] = static_cast<uint8_t>(std::clamp(g * vig + grain, 16.0f, 240.0f));
            rgbaPixels[idx + 2] = static_cast<uint8_t>(std::clamp(b * vig + grain, 14.0f, 224.0f));
            rgbaPixels[idx + 3] = 255;
        }
    }
}

TabloidNewspaperEdition buildOfflineTabloidEdition(const TabloidNewspaperEdition& baseMeta)
{
    TabloidNewspaperEdition ed = baseMeta;
    const std::string t1 = ed.team1.empty() ? "HOME" : ed.team1;
    const std::string t2 = ed.team2.empty() ? "AWAY" : ed.team2;
    bool t1Won = ed.playingPenalties ? (ed.penGoals1 > ed.penGoals2) : (ed.goals1 > ed.goals2);
    bool t2Won = ed.playingPenalties ? (ed.penGoals2 > ed.penGoals1) : (ed.goals2 > ed.goals1);
    int margin = std::abs(ed.goals1 - ed.goals2);

    if (t1Won) {
        if (margin >= 3)
            ed.headline = toUpperAscii(t1) + " RUN RIOT IN " + std::to_string(ed.goals1) + "-" + std::to_string(ed.goals2) + " ROUT!";
        else
            ed.headline = toUpperAscii(t1) + " EDGE PAST " + toUpperAscii(t2) + "!";
        ed.subhead = t1 + " claim a " + std::to_string(ed.goals1) + "-" + std::to_string(ed.goals2) +
                     " victory over " + t2 + (ed.playingPenalties ? " after penalty drama." : " in a lively contest.");
    } else if (t2Won) {
        if (margin >= 3)
            ed.headline = toUpperAscii(t2) + " STORM TO " + std::to_string(ed.goals2) + "-" + std::to_string(ed.goals1) + " AWAY TRIUMPH!";
        else
            ed.headline = toUpperAscii(t2) + " STUN " + toUpperAscii(t1) + " ON THE ROAD!";
        ed.subhead = t2 + " take the spoils " + std::to_string(ed.goals1) + "-" + std::to_string(ed.goals2) +
                     " away at " + t1 + (ed.playingPenalties ? " in a tense shootout." : ".");
    } else {
        ed.headline = (ed.goals1 == 0)
            ? ("STALEMATE AS " + toUpperAscii(t1) + " AND " + toUpperAscii(t2) + " SHARE SPOILS")
            : ("ALL SQUARE IN " + std::to_string(ed.goals1) + "-" + std::to_string(ed.goals2) + " THRILLER!");
        ed.subhead = "Neither side could find a knockout blow as " + t1 + " and " + t2 + " finish level.";
    }

    if (ed.photoCaption.empty()) {
        if (ed.motm.valid)
            ed.photoCaption = ed.motm.name + " (" + ed.motm.team + ") led the charge in " + t1 + " " +
                              std::to_string(ed.goals1) + "-" + std::to_string(ed.goals2) + " " + t2 + ".";
        else
            ed.photoCaption = "Full-time drama as " + t1 + " met " + t2 + " in " +
                              (ed.competition.empty() ? "action" : ed.competition) + ".";
    }

    std::string story = t1 + " and " + t2 + " served up a memorable " +
                        std::to_string(ed.goals1) + "-" + std::to_string(ed.goals2) + " clash.";
    if (!ed.scorers1.empty())
        story += " " + t1 + " found the net through " + ed.scorers1 + ".";
    if (!ed.scorers2.empty())
        story += " " + t2 + " replied via " + ed.scorers2 + ".";
    if (ed.motm.valid) {
        char rBuf[16];
        std::snprintf(rBuf, sizeof(rBuf), "%.1f", ed.motm.rating);
        story += "\n\nStar man " + ed.motm.name + " earned a " + std::string(rBuf) +
                 " match rating as supporters soaked up the 90-minute drama.";
    }
    ed.story = std::move(story);

    bool humanSingle = (ed.team1Human != ed.team2Human);
    bool humanWon = humanSingle && ((ed.team1Human && t1Won) || (ed.team2Human && t2Won));
    bool humanLost = humanSingle && ((ed.team1Human && t2Won) || (ed.team2Human && t1Won));
    if (humanWon)
        ed.chairmanQuote = "That is the spirit our supporters pay to see. Keep this momentum rolling into next week!";
    else if (humanLost)
        ed.chairmanQuote = "Not good enough today and the board expects a sharp response on the training pitch.";
    else
        ed.chairmanQuote = "A hard-earned point, though we know this squad has another gear to find.";

    return ed;
}

TabloidNewspaperEdition parseTabloidEditionReply(
    const std::string& rawReply, const TabloidNewspaperEdition& baseMeta)
{
    TabloidNewspaperEdition ed = buildOfflineTabloidEdition(baseMeta);
    if (trimCopy(rawReply).empty())
        return ed;

    std::string parsedHeadline;
    std::string parsedSubhead;
    std::string parsedCaption;
    std::string parsedStory;
    std::string parsedChairman;

    enum class Section { kNone, kStory, kChairman };
    Section curSection = Section::kNone;
    bool sawAnyStructuredTag = false;
    std::vector<std::string> rawLines;

    std::istringstream iss(rawReply);
    std::string rawLine;
    while (std::getline(iss, rawLine)) {
        std::string line = cleanTabloidText(rawLine);
        if (line.empty()) {
            if (curSection == Section::kStory && !parsedStory.empty())
                parsedStory += "\n\n";
            continue;
        }
        rawLines.push_back(line);

        std::string val;
        if (stripTaggedPrefix(line, "HEADLINE", val)) {
            sawAnyStructuredTag = true;
            if (!val.empty()) parsedHeadline = val;
            curSection = Section::kNone;
        } else if (stripTaggedPrefix(line, "SUBHEADLINE", val) || stripTaggedPrefix(line, "SUBHEAD", val)) {
            sawAnyStructuredTag = true;
            if (!val.empty()) parsedSubhead = val;
            curSection = Section::kNone;
        } else if (stripTaggedPrefix(line, "CAPTION", val) || stripTaggedPrefix(line, "PHOTO", val)) {
            sawAnyStructuredTag = true;
            if (!val.empty()) parsedCaption = val;
            curSection = Section::kNone;
        } else if (stripTaggedPrefix(line, "STORY", val) || stripTaggedPrefix(line, "REPORT", val)) {
            sawAnyStructuredTag = true;
            if (!val.empty()) {
                if (!parsedStory.empty() && parsedStory.back() != '\n') parsedStory += " ";
                parsedStory += val;
            }
            curSection = Section::kStory;
        } else if (stripTaggedPrefix(line, "CHAIRMAN", val)) {
            sawAnyStructuredTag = true;
            if (!val.empty()) parsedChairman = val;
            curSection = Section::kChairman;
        } else if (curSection == Section::kStory) {
            if (!parsedStory.empty() && parsedStory.back() != '\n')
                parsedStory += " ";
            parsedStory += line;
        } else if (curSection == Section::kChairman) {
            if (!parsedChairman.empty())
                parsedChairman += " ";
            parsedChairman += line;
        }
    }

    // Graceful fallback if the LLM ignored tags and returned classic 3-part prose
    if (!sawAnyStructuredTag && !rawLines.empty()) {
        parsedHeadline = rawLines[0];
        for (size_t i = 1; i < rawLines.size(); ++i) {
            std::string chVal;
            if (stripTaggedPrefix(rawLines[i], "CHAIRMAN", chVal)) {
                parsedChairman = chVal;
            } else {
                if (!parsedStory.empty())
                    parsedStory += "\n\n";
                parsedStory += rawLines[i];
            }
        }
    }

    if (!parsedHeadline.empty()) {
        ed.headline = toUpperAscii(parsedHeadline);
        if (ed.headline.size() > 68)
            ed.headline.resize(68);
    }
    if (!parsedSubhead.empty()) {
        ed.subhead = parsedSubhead;
        if (ed.subhead.size() > 110)
            ed.subhead.resize(110);
    }
    if (!parsedCaption.empty()) {
        ed.photoCaption = parsedCaption;
        if (ed.photoCaption.size() > 96)
            ed.photoCaption.resize(96);
    }
    if (!parsedStory.empty()) {
        ed.story = trimCopy(parsedStory);
    }
    if (!parsedChairman.empty()) {
        ed.chairmanQuote = parsedChairman;
    }

    return ed;
}

// --- Interactive 1996/97 Color Tabloid Modal Renderer ---

void showTabloidNewspaperModal(const TabloidNewspaperEdition& edition)
{
#ifdef SWOS_TEST
    (void)edition;
    return;
#else
    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = r ? getCompactFontAtlas(r) : nullptr;
    if (!r || !atlas)
        return;

    // Upload captured live match photo (already processed with 90s color tabloid filter)
    SDL_Texture *photoTex = nullptr;
    if (edition.isPlayedMatch && s_photo.valid && s_photo.width > 0 && s_photo.height > 0 && !s_photo.rgba.empty()) {
        photoTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
            s_photo.width, s_photo.height);
        if (photoTex) {
            SDL_UpdateTexture(photoTex, nullptr, s_photo.rgba.data(), s_photo.width * 4);
            SDL_SetTextureBlendMode(photoTex, SDL_BLENDMODE_NONE);
        }
    }

    flushKeyBuffer();
    waitForKeyboardAndMouseIdle();

    bool prevMouseDown = false;
    bool prevSaveKey = false;
    bool prevCloseKey = false;
    bool savedToastActive = false;
    std::string savedToastMsg;
    Uint32 savedToastTick = 0;
    bool wantSaveNow = false;
    Uint32 openTick = SDL_GetTicks();

    // Newsprint 1996/97 color palette
    const SDL_Color deskBg{ 20, 22, 20, 255 };
    const SDL_Color paperDrop{ 8, 9, 8, 170 };
    const SDL_Color paperBase{ 236, 226, 202, 255 };
    const SDL_Color paperWarmBand{ 229, 218, 192, 255 };
    const SDL_Color paperEdge{ 206, 194, 166, 255 };
    const SDL_Color paperFold{ 188, 174, 144, 255 };
    const SDL_Color inkBlack{ 24, 22, 19, 255 };
    const SDL_Color inkCharcoal{ 52, 48, 42, 255 };
    const SDL_Color inkMuted{ 94, 86, 74, 255 };
    const SDL_Color tabloidRed{ 182, 28, 34, 255 };
    const SDL_Color tabloidRedDark{ 132, 18, 24, 255 };
    const SDL_Color calloutBg{ 222, 210, 182, 255 };
    const SDL_Color motmBg{ 232, 218, 176, 255 };
    const SDL_Color whiteInk{ 250, 246, 236, 255 };
    const SDL_Color goldInk{ 244, 204, 68, 255 };
    const SDL_Color stampGreen{ 30, 118, 56, 255 };

    auto fitPx = [](const std::string& s, float basePx, float maxW) -> float {
        int u = measureCompactTextUnits(s);
        if (u <= 0 || maxW <= 1.0f) return basePx;
        float needed = static_cast<float>(u) * basePx;
        return (needed <= maxW) ? basePx : std::max(0.75f, basePx * (maxW / needed));
    };

#ifdef _WIN32
    struct SerifSheetCache {
        int cachedOutW = 0;
        int cachedOutH = 0;
        SerifTex masthead;
        SerifTex headline;
        SerifTex subhead;
        SerifTex caption;
        SerifTex chairman;
        SerifTex story;
        SerifTex briefing;
        SerifTex motmName;
        SerifTex motmStats;
        void clear()
        {
            masthead.destroy();
            headline.destroy();
            subhead.destroy();
            caption.destroy();
            chairman.destroy();
            story.destroy();
            briefing.destroy();
            motmName.destroy();
            motmStats.destroy();
            cachedOutW = 0;
            cachedOutH = 0;
        }
    } serifCache;
#endif

    while (true) {
        processControlEvents();

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

        int outW = 0, outH = 0;
        SDL_GetRendererOutputSize(r, &outW, &outH);
        if (outW <= 0 || outH <= 0) {
            SDL_RenderSetScale(r, origSX, origSY);
            break;
        }

        // Desk backdrop
        fillRectF(r, 0, 0, static_cast<float>(outW), static_cast<float>(outH), deskBg);

        // Compute centered 4:3-ish broadsheet rectangle (filling ~93% height, clamped cleanly inside window)
        float sheetH = static_cast<float>(outH) * 0.93f;
        float sheetW = std::min(static_cast<float>(outW) * 0.94f, sheetH * 1.48f);
        if (sheetW < static_cast<float>(outW) * 0.72f && static_cast<float>(outW) > sheetH * 1.55f)
            sheetW = std::min(static_cast<float>(outW) * 0.88f, sheetH * 1.55f);
        float sx = std::floor((static_cast<float>(outW) - sheetW) * 0.5f);
        float sy = std::floor((static_cast<float>(outH) - sheetH) * 0.5f);

        // Compact pixel text scale tuned to sheet size
        float px = std::clamp(std::floor(sheetH / 345.0f + 0.5f), 1.0f, 4.0f);
        if (sheetH < 520.0f) px = std::max(1.0f, sheetH / 360.0f);
        float pad = std::max(8.0f, sheetW * 0.022f);

        // 1. Paper Drop Shadow + Aged Newsprint Sheet + Subtle Horizontal Paper Texture
        fillRectF(r, sx + 6.0f, sy + 7.0f, sheetW, sheetH, paperDrop);
        fillRectF(r, sx, sy, sheetW, sheetH, paperBase);

        // Subtle vintage newsprint paper bands & edge patina
        for (float by = sy + 2.0f; by < sy + sheetH - 2.0f; by += 6.0f) {
            fillRectF(r, sx + 2.0f, by, sheetW - 4.0f, 2.0f, paperWarmBand);
        }
        drawRectF(r, sx, sy, sheetW, sheetH, paperEdge);
        drawRectF(r, sx + 3.0f, sy + 3.0f, sheetW - 6.0f, sheetH - 6.0f, inkBlack);
        drawRectF(r, sx + 5.0f, sy + 5.0f, sheetW - 10.0f, sheetH - 10.0f, { 24, 22, 19, 130 });

        // Top-right folded corner dog-ear accent
        float foldSz = std::max(10.0f, pad * 0.85f);
        fillRectF(r, sx + sheetW - foldSz, sy, foldSz, foldSz, deskBg);
        fillRectF(r, sx + sheetW - foldSz, sy + foldSz * 0.25f, foldSz * 0.75f, foldSz * 0.75f, paperFold);
        drawRectF(r, sx + sheetW - foldSz, sy + foldSz * 0.25f, foldSz * 0.75f, foldSz * 0.75f, inkMuted);

        float innerX = sx + pad;
        float innerW = sheetW - 2.0f * pad;
        float curY = sy + pad * 0.72f;

        // 2. Classic 1996/97 British Tabloid Masthead
        float mastH = std::max(26.0f, 21.0f * px);
        // Left crimson SPORT / BACK PAGE badge
        float badgeW = std::min(innerW * 0.19f, 78.0f * px);
        fillRectF(r, innerX, curY, badgeW, mastH, tabloidRed);
        drawRectF(r, innerX, curY, badgeW, mastH, tabloidRedDark);
        drawCompactTextCentered(r, atlas, innerX + badgeW * 0.5f, curY + mastH * 0.16f,
            fitPx("SPORT", px * 1.15f, badgeW - 6.0f), "SPORT", whiteInk, 255, true);
        drawCompactTextCentered(r, atlas, innerX + badgeW * 0.5f, curY + mastH * 0.60f,
            fitPx("BACK PAGE", px * 0.78f, badgeW - 6.0f), "BACK PAGE", goldInk, 255, true);

        // Right price & edition box
        float priceW = std::min(innerW * 0.19f, 78.0f * px);
        float priceX = innerX + innerW - priceW;
        fillRectF(r, priceX, curY, priceW, mastH, calloutBg);
        drawRectF(r, priceX, curY, priceW, mastH, inkBlack);
        drawCompactTextCentered(r, atlas, priceX + priceW * 0.5f, curY + mastH * 0.16f,
            fitPx("LATE FINAL", px * 0.85f, priceW - 6.0f), "LATE FINAL", tabloidRed, 255, true);
        drawCompactTextCentered(r, atlas, priceX + priceW * 0.5f, curY + mastH * 0.58f,
            fitPx("EST.1996 | 35p", px * 0.78f, priceW - 6.0f), "EST.1996 | 35p", inkBlack, 255, true);

        // Center newspaper title flanked by both clubs' crests + optional competition logo
        float titleAreaX = innerX + badgeW + 6.0f;
        float titleAreaW = std::max(40.0f, priceX - titleAreaX - 6.0f);
        float mastCrestSz = std::max(18.0f, mastH * 0.82f);
        float mastCrestY = curY + (mastH - mastCrestSz) * 0.5f;
        if (drawClubLogo(r, edition.team1.c_str(), titleAreaX, mastCrestY, mastCrestSz, 255)) {
            titleAreaX += mastCrestSz + 5.0f;
            titleAreaW = std::max(40.0f, titleAreaW - (mastCrestSz + 5.0f));
        }
        if (drawClubLogo(r, edition.team2.c_str(), titleAreaX + titleAreaW - mastCrestSz, mastCrestY, mastCrestSz, 255)) {
            titleAreaW = std::max(40.0f, titleAreaW - (mastCrestSz + 5.0f));
        }
        std::string paperLeagueKey = resolveCurrentMatchLeagueLogoKey();
        if (!paperLeagueKey.empty()) {
            float lSz = std::max(14.0f, mastH * 0.56f);
            drawLeagueLogo(r, paperLeagueKey.c_str(), innerX + 3.0f, curY + (mastH - lSz) * 0.5f, lSz, 235);
        }
        std::string paperTitle = "THE DAILY SOCCER ECHO";
#ifdef _WIN32
        bool rebuildSerif = (serifCache.cachedOutW != outW || serifCache.cachedOutH != outH);
        if (rebuildSerif) {
            serifCache.clear();
            serifCache.cachedOutW = outW;
            serifCache.cachedOutH = outH;
            int mastPx = std::clamp(static_cast<int>(std::round(sheetH * 0.046f)), 18, 52);
            serifCache.masthead = rasterizeTimesNewRoman(
                r, paperTitle, static_cast<int>(titleAreaW), static_cast<int>(mastH * 0.68f),
                mastPx, 13, true, false, true, inkBlack, true);

            int hlDesired = std::clamp(static_cast<int>(std::round(sheetH * 0.045f)), 18, 48);
            serifCache.headline = rasterizeTimesNewRoman(
                r, edition.headline, static_cast<int>(innerW - 6.0f), static_cast<int>(sheetH * 0.068f),
                hlDesired, 14, true, false, true, inkBlack, true);

            if (!edition.subhead.empty()) {
                int shDesired = std::clamp(static_cast<int>(std::round(sheetH * 0.024f)), 13, 28);
                serifCache.subhead = rasterizeTimesNewRoman(
                    r, edition.subhead, static_cast<int>(innerW - 8.0f), static_cast<int>(sheetH * 0.042f),
                    shDesired, 11, true, true, true, tabloidRedDark, true);
            }
        }
        if (serifCache.masthead.tex) {
            drawSerifTex(r, serifCache.masthead, titleAreaX,
                curY + std::max(1.0f, (mastH * 0.68f - static_cast<float>(serifCache.masthead.h)) * 0.5f));
        } else {
            float titlePx = fitPx(paperTitle, px * 1.72f, titleAreaW);
            drawCompactTextCentered(r, atlas, titleAreaX + titleAreaW * 0.5f, curY + (mastH - 8.0f * titlePx) * 0.36f,
                titlePx, paperTitle, inkBlack, 255, true);
        }
#else
        float titlePx = fitPx(paperTitle, px * 1.72f, titleAreaW);
        drawCompactTextCentered(r, atlas, titleAreaX + titleAreaW * 0.5f, curY + (mastH - 8.0f * titlePx) * 0.36f,
            titlePx, paperTitle, inkBlack, 255, true);
#endif
        drawCompactTextCentered(r, atlas, titleAreaX + titleAreaW * 0.5f, curY + mastH - 8.5f * px * 0.76f,
            fitPx("BRITAIN'S NO.1 SENSIBLE FOOTBALL TABLOID", px * 0.72f, titleAreaW),
            "BRITAIN'S NO.1 SENSIBLE FOOTBALL TABLOID", inkMuted, 255, false);

        curY += mastH + 3.0f;
        // Double black ink masthead rule + match ribbon
        fillRectF(r, innerX, curY, innerW, 2.0f, inkBlack);
        curY += 4.0f;

        std::string scoreBanner = toUpperAscii(edition.competition.empty() ? "MATCHDAY" : edition.competition) +
            "  *  " + toUpperAscii(edition.team1) + " " + std::to_string(edition.goals1) + " - " +
            std::to_string(edition.goals2) + " " + toUpperAscii(edition.team2);
        if (edition.playingPenalties) {
            scoreBanner += " (PENS " + std::to_string(edition.penGoals1) + "-" + std::to_string(edition.penGoals2) + ")";
        }
        if (!edition.statsLine.empty())
            scoreBanner += "  *  " + edition.statsLine;
        float ribH = 11.0f * px;
        fillRectF(r, innerX, curY, innerW, ribH, inkBlack);
        drawCompactTextCentered(r, atlas, innerX + innerW * 0.5f, curY + (ribH - 8.0f * px * 0.85f) * 0.5f,
            fitPx(scoreBanner, px * 0.85f, innerW - 10.0f), scoreBanner, whiteInk, 255, true);
        curY += ribH + 5.0f;

        // 3. Screaming Tabloid Banner Headline + Sub-Headline
#ifdef _WIN32
        if (serifCache.headline.tex) {
            drawSerifTex(r, serifCache.headline, innerX + 3.0f, curY);
            curY += static_cast<float>(serifCache.headline.h) + 2.0f;
        } else {
            float hlPx = fitPx(edition.headline, px * 1.85f, innerW - 4.0f);
            drawCompactTextCentered(r, atlas, innerX + innerW * 0.5f, curY, hlPx, edition.headline, inkBlack, 255, true);
            curY += 9.0f * hlPx + 2.0f;
        }
        if (!edition.subhead.empty()) {
            if (serifCache.subhead.tex) {
                drawSerifTex(r, serifCache.subhead, innerX + 4.0f, curY);
                curY += static_cast<float>(serifCache.subhead.h) + 3.0f;
            } else {
                float shPx = fitPx(edition.subhead, px * 0.96f, innerW - 6.0f);
                drawCompactTextCentered(r, atlas, innerX + innerW * 0.5f, curY, shPx, edition.subhead, tabloidRedDark, 255, true);
                curY += 9.5f * shPx + 2.0f;
            }
        }
#else
        float hlPx = fitPx(edition.headline, px * 1.85f, innerW - 4.0f);
        drawCompactTextCentered(r, atlas, innerX + innerW * 0.5f, curY, hlPx, edition.headline, inkBlack, 255, true);
        curY += 9.0f * hlPx + 2.0f;

        if (!edition.subhead.empty()) {
            float shPx = fitPx(edition.subhead, px * 0.96f, innerW - 6.0f);
            drawCompactTextCentered(r, atlas, innerX + innerW * 0.5f, curY, shPx, edition.subhead, tabloidRedDark, 255, true);
            curY += 9.5f * shPx + 2.0f;
        }
#endif
        fillRectF(r, innerX, curY, innerW, 1.5f, inkBlack);
        curY += 5.0f;

        // 4. Two-Column Tabloid Body (Footer anchored cleanly at bottom)
        float footerH = std::max(22.0f, 16.0f * px);
        float footerY = sy + sheetH - pad * 0.65f - footerH;
        float bodyH = std::max(60.0f, footerY - curY - 5.0f);

        float colGap = std::max(10.0f, pad * 0.9f);
        float leftW = std::floor((innerW - colGap) * 0.51f);
        float rightX = innerX + leftW + colGap;
        float rightW = innerW - leftW - colGap;

        // Vertical column ink divider
        fillRectF(r, innerX + leftW + colGap * 0.5f, curY, 1.0f, bodyH, { 34, 30, 24, 150 });

        // --- LEFT COLUMN: 90s Action Match Photo + Caption + Chairman's Verdict Box ---
        float photoW = leftW;
        float maxPhotoH = bodyH * 0.60f;
        float photoH = std::min(maxPhotoH, photoW * (static_cast<float>(kPhotoHeight) / static_cast<float>(kPhotoWidth)));
        float photoX = innerX;
        float photoY = curY;

        // Cream photo mat + crisp black tabloid ink frame
        fillRectF(r, photoX, photoY, photoW, photoH, { 246, 240, 224, 255 });
        drawRectF(r, photoX, photoY, photoW, photoH, inkBlack);
        SDL_FRect imgRect{ photoX + 3.0f, photoY + 3.0f, photoW - 6.0f, photoH - 6.0f };
        if (photoTex) {
            SDL_RenderCopyF(r, photoTex, nullptr, &imgRect);
            // Tiny "MATCH PHOTO" red press tag in top-left corner of the photo
            float tagW = 48.0f * px * 0.72f;
            float tagH = 8.5f * px * 0.72f;
            fillRectF(r, imgRect.x + 3.0f, imgRect.y + 3.0f, tagW, tagH, { 182, 28, 34, 220 });
            drawCompactTextCentered(r, atlas, imgRect.x + 3.0f + tagW * 0.5f, imgRect.y + 4.0f,
                px * 0.62f, "MATCH PHOTO", whiteInk, 255, true);
        } else {
            drawFallbackMatchupPhoto(r, atlas, imgRect, edition, px);
        }
        drawRectF(r, imgRect.x, imgRect.y, imgRect.w, imgRect.h, { 24, 22, 19, 180 });

        float leftCurY = photoY + photoH + 3.0f;

        // Photo Caption (up to 2 crisp lines in Times New Roman Italic)
        if (!edition.photoCaption.empty()) {
#ifdef _WIN32
            if (rebuildSerif) {
                int capDesired = std::clamp(static_cast<int>(std::round(sheetH * 0.021f)), 12, 23);
                int capMaxH = std::max(24, static_cast<int>(std::round(capDesired * 2.5f)));
                serifCache.caption = rasterizeTimesNewRoman(
                    r, edition.photoCaption, static_cast<int>(leftW - 4.0f), capMaxH,
                    capDesired, 10, false, true, false, inkCharcoal, false);
            }
            if (serifCache.caption.tex) {
                drawSerifTex(r, serifCache.caption, innerX + 2.0f, leftCurY);
                leftCurY += static_cast<float>(serifCache.caption.h) + 4.0f;
            } else
#endif
            {
                float capPx = px * 0.80f;
                int capUnits = std::max(30, static_cast<int>((leftW - 4.0f) / capPx));
                auto capLines = wrapCompactTextUnits(edition.photoCaption, capUnits);
                for (size_t i = 0; i < capLines.size() && i < 2; ++i) {
                    drawCompactText(r, atlas, innerX + 2.0f, leftCurY, capPx, capLines[i], inkCharcoal, 255, i == 0);
                    leftCurY += 9.0f * capPx;
                }
                leftCurY += 3.0f;
            }
        }

        // Chairman's Verdict Box (fills remainder of left column cleanly)
        float chairH = std::max(28.0f, (curY + bodyH) - leftCurY);
        fillRectF(r, innerX, leftCurY, leftW, chairH, calloutBg);
        drawRectF(r, innerX, leftCurY, leftW, chairH, inkBlack);
        fillRectF(r, innerX, leftCurY, 4.0f, chairH, tabloidRed);

        float chPadX = innerX + 8.0f;
        float chY = leftCurY + 4.0f;
        drawCompactText(r, atlas, chPadX, chY, px * 0.86f, "CHAIRMAN'S VERDICT", tabloidRedDark, 255, true);
        chY += 10.0f * px * 0.86f + 2.0f;

        float chBottom = leftCurY + chairH - 4.0f;
#ifdef _WIN32
        if (rebuildSerif) {
            int chDesired = std::clamp(static_cast<int>(std::round(sheetH * 0.022f)), 13, 24);
            int chAvailH = std::max(16, static_cast<int>(chBottom - chY));
            serifCache.chairman = rasterizeTimesNewRoman(
                r, "\"" + edition.chairmanQuote + "\"", static_cast<int>(leftW - 14.0f), chAvailH,
                chDesired, 10, false, true, false, inkBlack, false);
        }
        if (serifCache.chairman.tex) {
            drawSerifTex(r, serifCache.chairman, chPadX, chY);
        } else
#endif
        {
            float chBodyPx = px * 0.84f;
            int chMaxUnits = std::max(28, static_cast<int>((leftW - 14.0f) / chBodyPx));
            auto chLines = wrapCompactTextUnits("\"" + edition.chairmanQuote + "\"", chMaxUnits);
            for (const auto& cl : chLines) {
                if (chY + 8.0f * chBodyPx > chBottom) break;
                drawCompactText(r, atlas, chPadX, chY, chBodyPx, cl, inkBlack, 255, false);
                chY += 9.2f * chBodyPx;
            }
        }

        // --- RIGHT COLUMN: Match Report + Star Man (MOTM) Card + Back-Page Briefing ---
        float rightCurY = curY;

        // Reserve bottom space for Star Man Card + Briefing Strip
        float motmH = edition.motm.valid ? std::max(46.0f, 32.0f * px) : 0.0f;
        float briefH = edition.briefingBullets.empty() ? 0.0f : std::max(36.0f, (static_cast<float>(std::min<size_t>(4, edition.briefingBullets.size())) * 10.2f + 12.0f) * px * 0.82f);
        float reservedBottom = (motmH > 0 ? motmH + 4.0f : 0.0f) + (briefH > 0 ? briefH + 4.0f : 0.0f);
        float storyMaxBottom = curY + bodyH - reservedBottom;

        drawCompactText(r, atlas, rightX, rightCurY, px * 0.88f, "MATCH REPORT", tabloidRedDark, 255, true);
        rightCurY += 9.5f * px * 0.88f;
        fillRectF(r, rightX, rightCurY, rightW * 0.45f, 1.0f, tabloidRedDark);
        rightCurY += 4.0f;

        float availStoryH = std::max(24.0f, storyMaxBottom - rightCurY);
#ifdef _WIN32
        if (rebuildSerif) {
            int storyDesired = std::clamp(static_cast<int>(std::round(sheetH * 0.0235f)), 14, 26);
            serifCache.story = rasterizeTimesNewRoman(
                r, edition.story, static_cast<int>(rightW - 2.0f), static_cast<int>(availStoryH),
                storyDesired, 11, false, false, false, inkBlack, false);
        }
        if (serifCache.story.tex) {
            drawSerifTex(r, serifCache.story, rightX, rightCurY);
        } else
#endif
        {
            float storyPx = px * 0.85f;
            int storyUnits = std::max(32, static_cast<int>((rightW - 2.0f) / storyPx));
            auto storyLines = wrapCompactTextUnits(edition.story, storyUnits);
            if (static_cast<float>(storyLines.size()) * 9.4f * storyPx > availStoryH) {
                storyPx = std::max(0.75f, storyPx * 0.86f);
                storyUnits = std::max(34, static_cast<int>((rightW - 2.0f) / storyPx));
                storyLines = wrapCompactTextUnits(edition.story, storyUnits);
            }
            for (const auto& sl : storyLines) {
                if (sl.empty()) {
                    rightCurY += 4.5f * storyPx;
                    continue;
                }
                if (rightCurY + 8.0f * storyPx > storyMaxBottom)
                    break;
                drawCompactText(r, atlas, rightX, rightCurY, storyPx, sl, inkBlack, 255, false);
                rightCurY += 9.2f * storyPx;
            }
        }

        // ★ STAR MAN (MOTM) Box with pixel-art SWOS player sprite in real kit colors!
        float bottomAnchorY = curY + bodyH;
        if (briefH > 0.0f) {
            float bY = bottomAnchorY - briefH;
            fillRectF(r, rightX, bY, rightW, briefH, calloutBg);
            drawRectF(r, rightX, bY, rightW, briefH, inkBlack);
            drawCompactText(r, atlas, rightX + 5.0f, bY + 3.0f, px * 0.78f, "BACK-PAGE BRIEFING", inkBlack, 255, true);
            float lineY = bY + 3.0f + 9.2f * px * 0.78f + 1.0f;
#ifdef _WIN32
            if (rebuildSerif) {
                std::string joined;
                for (size_t i = 0; i < edition.briefingBullets.size() && i < 4; ++i) {
                    if (!joined.empty()) joined += "\n";
                    joined += "- " + edition.briefingBullets[i];
                }
                int bDesired = std::clamp(static_cast<int>(std::round(sheetH * 0.019f)), 12, 21);
                int bAvailH = std::max(14, static_cast<int>((bY + briefH - 3.0f) - lineY));
                serifCache.briefing = rasterizeTimesNewRoman(
                    r, joined, static_cast<int>(rightW - 10.0f), bAvailH,
                    bDesired, 10, false, false, false, inkCharcoal, false);
            }
            if (serifCache.briefing.tex) {
                drawSerifTex(r, serifCache.briefing, rightX + 5.0f, lineY);
            } else
#endif
            {
                float bPx = px * 0.76f;
                for (size_t i = 0; i < edition.briefingBullets.size() && i < 4; ++i) {
                    std::string bullet = "* " + edition.briefingBullets[i];
                    drawCompactText(r, atlas, rightX + 5.0f, lineY,
                        fitPx(bullet, bPx, rightW - 10.0f), bullet, inkCharcoal, 255, false);
                    lineY += 9.0f * bPx;
                }
            }
            bottomAnchorY = bY - 4.0f;
        }

        if (edition.motm.valid && motmH > 0.0f) {
            float mY = bottomAnchorY - motmH;
            fillRectF(r, rightX, mY, rightW, motmH, motmBg);
            drawRectF(r, rightX, mY, rightW, motmH, inkBlack);

            // SWOS-style pixel-art MOTM portrait bust (fallback to procedural SWOS kit sprite if PNG missing)
            float portraitSize = std::max(28.0f, motmH - 6.0f);
            float portraitX = rightX + 4.0f;
            float portraitY = mY + 3.0f;
            float textX = portraitX + portraitSize + 6.0f;
#ifndef SWOS_TEST
            if (SDL_Texture *motmTex = getMotmPortraitTexture(r, edition.motm)) {
                SDL_FRect pDst{ portraitX, portraitY, portraitSize, portraitSize };
                fillRectF(r, portraitX - 1.0f, portraitY - 1.0f, portraitSize + 2.0f, portraitSize + 2.0f, inkBlack);
                SDL_RenderCopyF(r, motmTex, nullptr, &pDst);
            } else
#endif
            {
                float sprUnit = std::max(1.0f, (motmH - 6.0f) / 16.0f);
                drawPixelSwosPlayerSprite(r, rightX + 5.0f, mY + 3.0f, sprUnit, edition.motm.kit, true);
                textX = rightX + 10.0f + 12.0f * sprUnit;
            }

            float textMaxW = std::max(30.0f, (rightX + rightW - 6.0f) - textX);
            drawCompactText(r, atlas, textX, mY + 3.5f, px * 0.76f, "STAR MAN (MOTM)", tabloidRedDark, 255, true);

            std::string plLine = toUpperAscii(edition.motm.name) + " (" + toUpperAscii(edition.motm.team) + ")";
            char statBuf[96];
            if (edition.motm.pos == "GK") {
                std::snprintf(statBuf, sizeof(statBuf), "Rating %.1f/10  |  GK  |  %d Saves",
                    edition.motm.rating, edition.motm.gkSaves);
            } else {
                std::snprintf(statBuf, sizeof(statBuf), "Rating %.1f/10  |  %s  |  %dG %dA",
                    edition.motm.rating, edition.motm.pos.c_str(), edition.motm.goals, edition.motm.assists);
            }
#ifdef _WIN32
            if (rebuildSerif) {
                int mnDesired = std::clamp(static_cast<int>(std::round(sheetH * 0.022f)), 13, 24);
                int msDesired = std::clamp(static_cast<int>(std::round(sheetH * 0.019f)), 11, 20);
                serifCache.motmName = rasterizeTimesNewRoman(
                    r, plLine, static_cast<int>(textMaxW), static_cast<int>(motmH * 0.40f),
                    mnDesired, 10, true, false, false, inkBlack, true);
                serifCache.motmStats = rasterizeTimesNewRoman(
                    r, statBuf, static_cast<int>(textMaxW), static_cast<int>(motmH * 0.36f),
                    msDesired, 10, false, false, false, inkCharcoal, true);
            }
            float nameY = mY + 3.5f + 8.8f * px * 0.76f;
            if (serifCache.motmName.tex) {
                drawSerifTex(r, serifCache.motmName, textX, nameY);
                if (serifCache.motmStats.tex)
                    drawSerifTex(r, serifCache.motmStats, textX, nameY + static_cast<float>(serifCache.motmName.h) + 1.0f);
            } else
#endif
            {
                drawCompactText(r, atlas, textX, mY + 3.5f + 8.8f * px * 0.76f,
                    fitPx(plLine, px * 0.88f, textMaxW), plLine, inkBlack, 255, true);
                drawCompactText(r, atlas, textX, mY + 3.5f + 18.2f * px * 0.80f,
                    fitPx(statBuf, px * 0.78f, textMaxW), statBuf, inkCharcoal, 255, true);
            }
        }

        // 5. Bottom Footer Bar ([S] Save Edition PNG + [ENTER / SPACE] Fold Paper)
        fillRectF(r, innerX, footerY, innerW, footerH, inkBlack);

        float btnPad = 3.0f;
        float btnH = footerH - 2.0f * btnPad;
        float saveBtnW = std::min(innerW * 0.38f, 130.0f * px);
        float closeBtnW = std::min(innerW * 0.40f, 140.0f * px);
        SDL_FRect saveBtn{ innerX + btnPad, footerY + btnPad, saveBtnW, btnH };
        SDL_FRect closeBtn{ innerX + innerW - btnPad - closeBtnW, footerY + btnPad, closeBtnW, btnH };

        // Render without footer highlight right when snapping PNG if requested, OR keep crisp clean paper footer
        bool toastVisible = savedToastActive && (SDL_GetTicks() - savedToastTick < 3500);
        fillRectF(r, saveBtn.x, saveBtn.y, saveBtn.w, saveBtn.h, toastVisible ? stampGreen : tabloidRed);
        drawRectF(r, saveBtn.x, saveBtn.y, saveBtn.w, saveBtn.h, whiteInk);
        std::string saveLabel = toastVisible ? "SAVED TO SCREENSHOTS!" : "[S] SAVE EDITION PNG";
        drawCompactTextCentered(r, atlas, saveBtn.x + saveBtn.w * 0.5f, saveBtn.y + (saveBtn.h - 8.0f * px * 0.82f) * 0.5f,
            fitPx(saveLabel, px * 0.82f, saveBtn.w - 8.0f), saveLabel, whiteInk, 255, true);

        fillRectF(r, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, { 54, 58, 52, 255 });
        drawRectF(r, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, goldInk);
        std::string closeLabel = "[ENTER / SPACE] FOLD PAPER";
        drawCompactTextCentered(r, atlas, closeBtn.x + closeBtn.w * 0.5f, closeBtn.y + (closeBtn.h - 8.0f * px * 0.82f) * 0.5f,
            fitPx(closeLabel, px * 0.82f, closeBtn.w - 8.0f), closeLabel, goldInk, 255, true);

        // If user pressed [S] / clicked Save Edition, snap the rendered newspaper sheet right now!
        if (wantSaveNow) {
            wantSaveNow = false;
            SDL_Rect snapRect{
                std::max(0, static_cast<int>(sx) - 4),
                std::max(0, static_cast<int>(sy) - 4),
                std::min(outW - std::max(0, static_cast<int>(sx) - 4), static_cast<int>(sheetW) + 8),
                std::min(outH - std::max(0, static_cast<int>(sy) - 4), static_cast<int>(sheetH) + 8),
            };
            std::string savedFile;
            if (saveNewspaperRectToPng(r, snapRect, savedFile)) {
                savedToastActive = true;
                savedToastMsg = savedFile;
                savedToastTick = SDL_GetTicks();
            }
        }

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_RenderSetScale(r, origSX, origSY);
        updateScreen(false);

        // Input handling (grace period 220ms after open so held match keys don't instantly dismiss)
        bool inputReady = (SDL_GetTicks() - openTick) >= 220;
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        bool saveKeyNow = keys && (keys[SDL_SCANCODE_S] != 0);
        bool closeKeyNow = keys && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER] ||
                                    keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_ESCAPE]);

        int mx = 0, my = 0;
        Uint32 mButtons = SDL_GetMouseState(&mx, &my);
        bool mouseDown = (mButtons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

        bool exitPaper = false;
        if (inputReady) {
            if (saveKeyNow && !prevSaveKey) {
                wantSaveNow = true;
            }
            if (closeKeyNow && !prevCloseKey) {
                exitPaper = true;
            }
            if (mouseDown && !prevMouseDown) {
                if (mx >= saveBtn.x && mx <= saveBtn.x + saveBtn.w &&
                    my >= saveBtn.y && my <= saveBtn.y + saveBtn.h) {
                    wantSaveNow = true;
                } else {
                    exitPaper = true;
                }
            }
            if (!wantSaveNow && isAnyPlayerFiring()) {
                exitPaper = true;
            }
        }
        prevSaveKey = saveKeyNow;
        prevCloseKey = closeKeyNow;
        prevMouseDown = mouseDown;

        if (exitPaper)
            break;

        SDL_Delay(16);
    }

#ifdef _WIN32
    serifCache.clear();
#endif
    if (photoTex)
        SDL_DestroyTexture(photoTex);

    flushKeyBuffer();
    waitForKeyboardAndMouseIdle();
#endif
}
