#include "pitch.h"
#include "pitchDatabase.h"
#include "windowManager.h"
#include "loadTexture.h"
#include "camera.h"
#include "game.h"
#include "replays.h"
#include "render.h"
#include "gameFieldMapping.h"
#include "file.h"
#include "util.h"
#include "random.h"
#include "goalNet.h"
#include "pitchEffects.h"
#include "crowdData.h"
#include "color.h"

constexpr int kSwosPatternSize = 16;

static_assert(kPitchWidth == kPitchPatternWidth * kSwosPatternSize &&
    kPitchHeight == kPitchPatternHeight * kSwosPatternSize, "Pitch dimensions mismatch");

constexpr float kZoomIncrement = 1.f / 70 / 4;
constexpr float kMaxZoom = 2.5;
constexpr float kMaxZoomVelocityFactor = .1f;

enum PitchTypes {
    kRandom = -1,
    kFrozen = 0,
    kMuddy = 1,
    kWet = 2,
    kSoft = 3,
    kNormal = 4,
    kDry = 5,
    kHard = 6,
    kMaxPitchType = 6,
};

static int m_pitchType;
static int m_pitchNumber;

static int m_res;
static float m_zoom;
static Uint32 m_lastZoomChangeTicks;
static bool m_loadedWithWeatherGrading = false;
static int m_loadedPitchType = -1;
static std::array<SDL_Texture *, 2> m_crowdTextures = { nullptr, nullptr };
static int m_crowdCheerLevel[2] = { 0, 0 }; // 0=idle, 1=medium, 2=wild cheering
static uint32_t m_crowdAnimTick = 0;

static void setPitchType();
static void setPitchNumber();
static void drawPitch(float xOfs, float yOfs, int row, int column, int numPatternsX, int numPatternsY);
static void reloadPitch(AssetResolution oldResolution, AssetResolution newResolution);
static std::pair<float, float> clipPitch(float widthNormalized, float heightNormalized, float& x, float& y);
static float getMinimumZoom(float scale = getGameScale());
static float getDefaultZoom(float scale);
static float getZoomIncrement();
static void clipZoom();

void initPitches()
{
    m_res = static_cast<int>(getAssetResolution());
    registerAssetResolutionChangeHandler(reloadPitch);
}

void setPitchTypeAndNumber()
{
    setPitchType();
    setPitchNumber();
}

int getPitchType()
{
    return m_pitchType;
}

int getPitchNumber()
{
    return m_pitchNumber;
}

static void remapWeatherGrassPixel(uint8_t& r, uint8_t& g, uint8_t& b, int pitchType)
{
    if (pitchType == kNormal || g < 55 || g < static_cast<int>(r * 0.78f))
        return;

    float wt = std::clamp((static_cast<float>(std::min(r, g) - b) - 6.0f) / 28.0f, 0.0f, 1.0f);
    if (wt <= 0.0f)
        return;

    float lum = (0.299f * r + 0.587f * g + 0.114f * b) / 132.0f;
    float tr = r;
    float tg = g;
    float tb = b;

    switch (pitchType) {
    case kFrozen: // Crisp icy blue-white winter pitch
        tr = 146.0f * lum + 24.0f;
        tg = 182.0f * lum + 24.0f;
        tb = 214.0f * lum + 26.0f;
        break;
    case kMuddy: // Earthy damp mud-brown / dark olive
        tr = 104.0f * lum + 8.0f;
        tg = 86.0f * lum + 6.0f;
        tb = 36.0f * lum + 4.0f;
        break;
    case kWet: // Deep rain-soaked emerald
        tr = 78.0f * lum + 4.0f;
        tg = 124.0f * lum + 6.0f;
        tb = 44.0f * lum + 8.0f;
        break;
    case kSoft: // Lush soft green
        tr = 106.0f * lum + 4.0f;
        tg = 144.0f * lum + 6.0f;
        tb = 22.0f * lum + 4.0f;
        break;
    case kDry: // Warm sun-dried yellow-green
        tr = 146.0f * lum + 8.0f;
        tg = 140.0f * lum + 6.0f;
        tb = 22.0f * lum + 2.0f;
        break;
    case kHard: // Sun-baked firm golden-olive
        tr = 156.0f * lum + 10.0f;
        tg = 136.0f * lum + 6.0f;
        tb = 30.0f * lum + 4.0f;
        break;
    default:
        break;
    }

    r = static_cast<uint8_t>(std::clamp(r * (1.0f - wt) + tr * wt, 0.0f, 255.0f));
    g = static_cast<uint8_t>(std::clamp(g * (1.0f - wt) + tg * wt, 0.0f, 255.0f));
    b = static_cast<uint8_t>(std::clamp(b * (1.0f - wt) + tb * wt, 0.0f, 255.0f));
}

static SDL_Texture *loadWeatherGradedPitchTexture(const char *filename, int pitchType, bool applyGrading)
{
    if (!applyGrading || pitchType == kNormal)
        return loadTexture(filename);

    auto resPath = joinPaths(getAssetDir(), filename);
    SDL_Surface *surf = nullptr;
    for (int attempt = 0; attempt < 25; ++attempt) {
        if (auto f = openFile(resPath.c_str()))
            surf = IMG_Load_RW(f, true);
        else
            errorExit("Failed to open texture file %s", filename);
        if (surf)
            break;
        SDL_Delay(50);
    }
    if (!surf)
        errorExit("Failed to load texture %s: %s", filename, IMG_GetError());

    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surf);
    if (!rgba)
        return loadTexture(filename);

    SDL_LockSurface(rgba);
    const int w = rgba->w;
    const int h = rgba->h;
    const int pitchBytes = rgba->pitch;
    auto *base = static_cast<uint8_t *>(rgba->pixels);

    for (int y = 0; y < h; ++y) {
        auto *row = reinterpret_cast<uint32_t *>(base + y * pitchBytes);
        for (int x = 0; x < w; ++x) {
            uint8_t r, g, b, a;
            SDL_GetRGBA(row[x], rgba->format, &r, &g, &b, &a);
            if (a == 0)
                continue;
            remapWeatherGrassPixel(r, g, b, pitchType);
            row[x] = SDL_MapRGBA(rgba->format, r, g, b, a);
        }
    }
    SDL_UnlockSurface(rgba);

    SDL_Texture *tex = SDL_CreateTextureFromSurface(getRenderer(), rgba);
    SDL_FreeSurface(rgba);
    if (!tex)
        return loadTexture(filename);
    return tex;
}

static void destroyCrowdTextures()
{
    for (auto& tex : m_crowdTextures) {
        if (tex) {
            SDL_DestroyTexture(tex);
            tex = nullptr;
        }
    }
}

static SDL_Texture *buildCrowdAtlasForTeam(const TeamGame& team)
{
    auto renderer = getRenderer();
    if (!renderer)
        return nullptr;

    constexpr int kTileSize = 16;
    constexpr int kNumTiles = 24;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, kNumTiles * kTileSize, kTileSize, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf)
        return nullptr;

    const Color& primary = kGamePalette[team.prShirtCol & 0x0f];
    const Color& secondary = kGamePalette[team.prShortsCol & 0x0f];

    uint8_t pal[256][3];
    memcpy(pal, kCrowdPalette, sizeof(pal));
    // Weather-grade any grass border pixels in the crowd tiles (before applying team kit colors)
    if (getPitchWeatherEffects()) {
        for (int i = 0; i < 256; ++i) {
            if (i != 10 && i != 11)
                remapWeatherGrassPixel(pal[i][0], pal[i][1], pal[i][2], m_pitchType);
        }
    }
    // Palette indices 10 & 11 are SWOS's dynamic fan kit color slots
    pal[10][0] = secondary.r; pal[10][1] = secondary.g; pal[10][2] = secondary.b;
    pal[11][0] = primary.r;   pal[11][1] = primary.g;   pal[11][2] = primary.b;

    SDL_LockSurface(surf);
    const int pitchBytes = surf->pitch;
    auto *pixels = static_cast<uint8_t *>(surf->pixels);
    for (int t = 0; t < kNumTiles; ++t) {
        const auto *pat = kCrowdPatterns[t];
        for (int y = 0; y < kTileSize; ++y) {
            auto *row = reinterpret_cast<uint32_t *>(pixels + y * pitchBytes);
            for (int x = 0; x < kTileSize; ++x) {
                uint8_t c = pat[y * kTileSize + x];
                row[t * kTileSize + x] = SDL_MapRGBA(surf->format, pal[c][0], pal[c][1], pal[c][2], 255);
            }
        }
    }
    SDL_UnlockSurface(surf);

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (tex)
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
    return tex;
}

static void loadCrowdTextures()
{
    destroyCrowdTextures();
    m_crowdCheerLevel[0] = 0;
    m_crowdCheerLevel[1] = 0;
    m_crowdAnimTick = 0;

    if (swos.g_trainingGame)
        return;

    m_crowdTextures[0] = buildCrowdAtlasForTeam(swos.topTeamInGame);
    m_crowdTextures[1] = buildCrowdAtlasForTeam(swos.bottomTeamInGame);
}

void loadPitch()
{
    const bool applyGrading = getPitchWeatherEffects();
    logInfo("Loading pitch %d (type: %d), asset resolution %d", m_pitchNumber, m_pitchType, m_res);

    if (m_loadedWithWeatherGrading != applyGrading || m_loadedPitchType != m_pitchType) {
        for (auto& tex : m_pitchTextures[m_res]) {
            if (tex) {
                SDL_DestroyTexture(tex);
                tex = nullptr;
            }
        }
        m_loadedWithWeatherGrading = applyGrading;
        m_loadedPitchType = m_pitchType;
    }

    auto pitchIndex = kPitchIndices[m_pitchNumber];
    auto start = kPitchPatternStartIndices[m_pitchNumber];

    for (int i = 0; i < kPitchPatternHeight; i++) {
        for (int j = 0; j < kPitchPatternWidth; j++) {
            auto patternIndex = pitchIndex[i][j];
            const auto& pattern = kPatterns[m_res][start + patternIndex];
            auto textureNo = pattern.texture;
            auto& texture = m_pitchTextures[m_res][textureNo];
            if (!texture) {
                auto filename = kTextureFilenames[textureNo];
                texture = loadWeatherGradedPitchTexture(filename, m_pitchType, applyGrading);
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
            }
        }
    }

    loadCrowdTextures();
}

void reloadPitchTexturesIfNeeded()
{
    if (isMatchRunning() && (m_loadedWithWeatherGrading != getPitchWeatherEffects() || m_loadedPitchType != m_pitchType))
        loadPitch();
}

static uint16_t getCleanPitchPatternIndex(int pitchNumber, int row, int col)
{
    const auto& indices = kPitchIndices[pitchNumber];
    const int stride = (pitchNumber == 4) ? 6 : 4; // pitch5 (index 4) has a 6-column diagonal mowing cycle; others use 4

    auto mapCleanCol = [&](int c, int dirtyLo, int dirtyHi, std::initializer_list<int> extraDirty) {
        const int deltas[4] = { -stride, stride, -2 * stride, 2 * stride };
        for (int d : deltas) {
            int cand = c + d;
            if (cand < 6 || cand > 35 || (cand >= dirtyLo && cand <= dirtyHi))
                continue;
            bool inExtra = false;
            for (int ex : extraDirty) {
                if (cand == ex) {
                    inExtra = true;
                    break;
                }
            }
            if (!inExtra)
                return cand;
        }
        return c;
    };

    if ((row == 5 || row == 6) && col >= 18 && col <= 24) {
        return indices[row][mapCleanCol(col, 18, 24, { 9, 32 })];
    }
    if (row == 7) {
        if (col >= 18 && col <= 23)
            return indices[row][mapCleanCol(col, 17, 24, { 9, 12, 29, 32 })];
        if (col == 24)
            return indices[row][(pitchNumber == 0 || pitchNumber == 5) ? 28 : 29];
    }
    if ((row == 45 || row == 46) && col >= 18 && col <= 23) {
        return indices[row][mapCleanCol(col, 17, 24, { 12, 29 })];
    }
    if (row == 47) {
        if (col >= 18 && col <= 23)
            return indices[row][mapCleanCol(col, 17, 24, { 9, 12, 29, 32 })];
        if (col == 24)
            return indices[row][(pitchNumber == 0 || pitchNumber == 5) ? 28 : 29];
    }
    if (row == 48 && col >= 19 && col <= 23) {
        return indices[row][mapCleanCol(col, 19, 23, {})];
    }
    return indices[row][col];
}

// Renders pitch at cameraX and cameraY.
std::pair<float, float> drawPitch(FixedPoint cameraX, FixedPoint cameraY)
{
    // clip zoom first since the screen dimensions might have changed
    clipZoom();

    assert(m_zoom > 0);

    auto width = getFieldWidth();
    auto height = getFieldHeight();

    auto scale = getGameScale();

    auto widthNormalized = width / m_zoom;
    auto heightNormalized = height / m_zoom;

    auto x = cameraX.asFloat() + (kVgaWidth - widthNormalized) / 2;
    auto y = cameraY.asFloat() + (kVgaHeight - heightNormalized) / 2;

    auto offsets = clipPitch(widthNormalized, heightNormalized, x, y);

    auto numPatternsX = widthNormalized / kSwosPatternSize + 1;
    auto numPatternsY = heightNormalized / kSwosPatternSize + 1;
    numPatternsX = std::min(static_cast<float>(kPitchPatternWidth), numPatternsX);
    numPatternsY = std::min(static_cast<float>(kPitchPatternHeight), numPatternsY);
    numPatternsX = std::ceil(numPatternsX);
    numPatternsY = std::ceil(numPatternsY);

    assert(numPatternsX > 0 && numPatternsY > 0);

    int xInt = static_cast<int>(x);
    auto xDiv = std::div(xInt, kSwosPatternSize);
    int column = xDiv.quot;
    auto xOfs = (x - xInt + xDiv.rem) * scale * m_zoom;

    int yInt = static_cast<int>(y);
    auto yDiv = std::div(yInt, kSwosPatternSize);
    // make sure to skip the top invisible row
    int row = yDiv.quot - 1;
    row = std::max(0, row);
    auto yOfs = (y - yInt + yDiv.rem) * scale * m_zoom;

    assert(row >= 0 && column >= 0);

#ifndef SWOS_TEST
    drawPitch(xOfs, yOfs, row, column, static_cast<int>(numPatternsX), static_cast<int>(numPatternsY));
#endif

    // unfortunately, this has to stay until UpdateCameraBreakMode() is converted
    swos.cameraCoordinatesValid = 1;

    return offsets;
}

std::pair<float, float> drawPitchAtCurrentCamera()
{
#ifndef SWOS_TEST
    return drawPitch(getCameraX(), getCameraY());
#else
    swos.cameraCoordinatesValid = 1;
    return { 0.f, 0.f };
#endif
}

void SWOS::ScrollToCurrent()
{
    drawPitchAtCurrentCamera();
}

bool zoomIn(float step /* = 0 */)
{
    return setZoomFactor(m_zoom + getZoomIncrement(), step);
}

bool zoomOut(float step /* = 0 */)
{
    return setZoomFactor(m_zoom - getZoomIncrement(), step);
}

bool resetZoom()
{
    return setZoomFactor(getDefaultZoom(getGameScale()));
}

float getZoomFactor()
{
    return m_zoom;
}

void initZoomFactor(float zoom)
{
    m_zoom = zoom;
}

bool setZoomFactor(float zoom, float step /* = 0 */)
{
    auto initialZoom = m_zoom;

    if (auto scale = getGameScale()) {
        if (!zoom) {
            m_zoom = getDefaultZoom(scale);
        } else {
            if (step && m_zoom != zoom) {
                auto mul = zoom > m_zoom ? std::ceil(zoom / step) : std::floor(zoom / step);
                zoom = step * mul;
            }
            m_zoom = std::min(kMaxZoom, zoom);
            auto minZoom = getMinimumZoom(scale);
            m_zoom = std::max(minZoom, m_zoom);
        }
    } else {
        assert(false);
    }

    bool zoomChanged = initialZoom != m_zoom;
    if (zoomChanged)
        m_lastZoomChangeTicks = SDL_GetTicks();

    return zoomChanged;
}

Uint32 getLastZoomChangeTime()
{
    return m_lastZoomChangeTicks;
}

static void setPitchType()
{
    // 12 (months) x 7 (pitch types)
    // probabilities in percents (sum = 100)
    static const std::array<std::array<byte, 7>, 12> kPitchTypeSeasonalProbabilities = {{
        30, 20, 30, 20, 0, 0, 0,
        20, 30, 20, 20, 10, 0, 0,
        10, 30, 10, 30, 20, 0, 0,
        0, 10, 10, 30, 40, 10, 0,
        0, 0, 0, 10, 40, 40, 10,
        0, 0, 0, 0, 40, 40, 20,
        0, 0, 0, 0, 30, 30, 40,
        0, 0, 0, 0, 50, 30, 20,
        0, 0, 0, 20, 40, 30, 10,
        0, 20, 0, 40, 30, 10, 0,
        10, 30, 10, 40, 10, 0, 0,
        20, 30, 20, 30, 0, 0, 0,
    }};
    static const std::array<byte, 7> kPitchTypeProbabilities = { 5, 5, 10, 20, 30, 20, 10 };

    m_pitchType = 0;

    if (swos.gamePitchTypeOrSeason || swos.gamePitchType == -1) {
        auto probabilities = swos.gamePitchTypeOrSeason ?
            kPitchTypeSeasonalProbabilities[swos.gameSeason].data() : kPitchTypeProbabilities.data();
        auto probability = SWOS::rand() * 100 / 256;    // range 0..99
        while (probability >= *probabilities) {
            probability -= *probabilities++;
            m_pitchType++;
        }
    } else {
        m_pitchType = swos.gamePitchType;
    }

    assert(m_pitchType >= kRandom && m_pitchType <= kMaxPitchType);
}

static void setPitchNumber()
{
    static const std::array<byte, 16> kPitchNumberProbabilities = {
        0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 4, 4, 2, 2, 3, 3
    };

    if (swos.g_trainingGame) {
        m_pitchNumber = kTrainingPitchIndex;
    } else {
        int index;
        if (swos.plg_D0_param) {
            // pick random pitch (used in friendlies)
            index = SWOS::rand() & 0xf;
        } else {
            // pseudo-random, but always the same in regards to the teams that are playing; used in career
            index = swos.topTeamInGame.teamName[0] | (swos.topTeamInGame.teamName[1] << 8);
            index ^= swos.topTeamInGame.prShirtCol;
            index ^= swos.topTeamInGame.prShortsCol;
            index &= 0xf;
        }

        m_pitchNumber = kPitchNumberProbabilities[index];
    }

    assert(m_pitchNumber >= 0 && m_pitchNumber < kNumPitches);
}

static void drawPitch(float xOfs, float yOfs, int row, int column, int numPatternsX, int numPatternsY)
{
    int patternSize = kPatternSizes[m_res];
    auto start = kPitchPatternStartIndices[m_pitchNumber];
    auto indices = kPitchIndices[m_pitchNumber];

    auto scale = getGameScale();

    auto destPatternSize = kSwosPatternSize * scale * m_zoom;

    auto destXOfs = -xOfs;
    auto destYOfs = -yOfs;
    auto currentXOfs = destXOfs;
    auto currentYOfs = destYOfs;

    struct RenderPattern {
        RenderPattern() = default;
        RenderPattern(const SDL_Rect& src, const SDL_FRect& dst, SDL_Texture *texture, int8_t textureIndex)
            : src(src), dst(dst), texture(texture), textureIndex(textureIndex) {}

        SDL_Rect src;
        SDL_FRect dst;
        SDL_Texture *texture;
        int8_t textureIndex;

        bool operator<(const RenderPattern& other) const {
            return textureIndex < other.textureIndex;
        }
    };

    std::array<RenderPattern, kPitchPatternWidth * kPitchPatternHeight + 16> renderPatterns;
    int numPatterns = 0;

    auto getCrowdMapRow = [](int r) -> int {
        if (r >= 0 && r <= 2)
            return r;
        if (r >= 50 && r <= 52)
            return r - 47;
        return -1;
    };

    const bool drawCrowd = !swos.g_trainingGame && m_crowdTextures[0] && m_crowdTextures[1];
    const bool useCleanGoalTiles = getPhysicsGoalNets();
    for (int i = row; i < row + numPatternsY; i++) {
        int crowdMapRow = drawCrowd ? getCrowdMapRow(i) : -1;
        for (int j = column; j < column + numPatternsX; j++) {
            int8_t crowdBase = (crowdMapRow >= 0 && j >= 0 && j < kPitchPatternWidth) ? kCrowdRowMap[crowdMapRow][j] : -1;
            if (crowdBase >= 0) {
                int standIdx = (i <= 2) ? 0 : 1;
                int cheer = m_crowdCheerLevel[standIdx];
                bool wave = false;
                if (cheer == 2) {
                    wave = (((m_crowdAnimTick / 6) + static_cast<uint32_t>(i * 3 + j)) & 1) != 0;
                } else if (cheer == 1) {
                    wave = (((m_crowdAnimTick / 10) + static_cast<uint32_t>(i * 5 + j * 7)) % 3) == 0;
                } else {
                    wave = (((m_crowdAnimTick / 18) + static_cast<uint32_t>(i * 11 + j * 13)) % 11) == 0;
                }
                int tileIdx = crowdBase + (wave ? 1 : 0);
                SDL_Rect srcRect{ tileIdx * 16, 0, 16, 16 };
                SDL_FRect dstRect{ currentXOfs, currentYOfs, destPatternSize, destPatternSize };
                renderPatterns[numPatterns++] = { srcRect, dstRect, m_crowdTextures[standIdx], static_cast<int8_t>(16 + standIdx) };
                currentXOfs += destPatternSize;
                continue;
            }
            auto patternIndex = useCleanGoalTiles ? getCleanPitchPatternIndex(m_pitchNumber, i, j) : indices[i][j];
            if (patternIndex != UINT16_MAX) {
                if (useCleanGoalTiles && i == 45 && j >= 18 && j <= 23) {
                    // Row 45 (cols 18..23) contains BOTH the 6-yard box horizontal white line near the top of the tile
                    // AND the baked-in top edge of the static bottom goal crossbar in the bottom 3 logical pixels.
                    // Render the top 13/16 of the original tile (keeping the 6-yard box line) and the bottom 3/16
                    // from the clean grass tile returned by getCleanPitchPatternIndex (erasing the extra crossbar line).
                    int topH = (patternSize * 13) / 16;
                    int botH = patternSize - topH;
                    float topDestH = destPatternSize * (13.0f / 16.0f);
                    float botDestH = destPatternSize - topDestH;

                    const auto& topPat = kPatterns[m_res][start + indices[i][j]];
                    SDL_Rect topSrc{ topPat.x, topPat.y, patternSize, topH };
                    SDL_FRect topDst{ currentXOfs, currentYOfs, destPatternSize, topDestH };
                    renderPatterns[numPatterns++] = { topSrc, topDst, m_pitchTextures[m_res][topPat.texture], topPat.texture };

                    const auto& botPat = kPatterns[m_res][start + patternIndex];
                    SDL_Rect botSrc{ botPat.x, botPat.y + topH, patternSize, botH };
                    SDL_FRect botDst{ currentXOfs, currentYOfs + topDestH, destPatternSize, botDestH };
                    renderPatterns[numPatterns++] = { botSrc, botDst, m_pitchTextures[m_res][botPat.texture], botPat.texture };
                } else if (useCleanGoalTiles && (i == 7 || i == 47) && j == 24) {
                    // Goal-line row, column 24: the clean replacement tile erases the baked goalpost/shadow,
                    // but it also drops the 6-yard box's right vertical line, which sits in the rightmost
                    // 5/16 of the original tile. Composite: left 11/16 from the clean tile, right 5/16 original.
                    int leftW = (patternSize * 11) / 16;
                    int rightW = patternSize - leftW;
                    float leftDestW = destPatternSize * (11.0f / 16.0f);
                    float rightDestW = destPatternSize - leftDestW;

                    const auto& leftPat = kPatterns[m_res][start + patternIndex];
                    SDL_Rect leftSrc{ leftPat.x, leftPat.y, leftW, patternSize };
                    SDL_FRect leftDst{ currentXOfs, currentYOfs, leftDestW, destPatternSize };
                    renderPatterns[numPatterns++] = { leftSrc, leftDst, m_pitchTextures[m_res][leftPat.texture], leftPat.texture };

                    const auto& rightPat = kPatterns[m_res][start + indices[i][j]];
                    SDL_Rect rightSrc{ rightPat.x + leftW, rightPat.y, rightW, patternSize };
                    SDL_FRect rightDst{ currentXOfs + leftDestW, currentYOfs, rightDestW, destPatternSize };
                    renderPatterns[numPatterns++] = { rightSrc, rightDst, m_pitchTextures[m_res][rightPat.texture], rightPat.texture };
                } else {
                    const auto& pattern = kPatterns[m_res][start + patternIndex];
                    const auto texture = m_pitchTextures[m_res][pattern.texture];

                    SDL_Rect srcRect{ pattern.x, pattern.y, patternSize, patternSize };
                    SDL_FRect dstRect{ currentXOfs, currentYOfs, destPatternSize, destPatternSize };

                    renderPatterns[numPatterns++] = { srcRect, dstRect, texture, pattern.texture };
                }
            }

            currentXOfs += destPatternSize;
        }

        currentXOfs = destXOfs;
        currentYOfs += destPatternSize;
    }

    // reduce the number of texture switch calls
    std::sort(renderPatterns.begin(), renderPatterns.begin() + numPatterns);

    auto renderer = getRenderer();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    for (int i = 0; i < numPatterns; i++) {
        const auto& renderPattern = renderPatterns[i];
        SDL_RenderCopyF(renderer, renderPattern.texture, &renderPattern.src, &renderPattern.dst);
    }
}

void SWOS::DrawAnimatedPatterns()
{
    ++m_crowdAnimTick;

    // must decrease this so the camera would start moving at the start of the game
    if (!replayingNow() && !swos.g_trainingGame && swos.showFansCounter)
        swos.showFansCounter--;

    if (swos.g_trainingGame) {
        m_crowdCheerLevel[0] = 0;
        m_crowdCheerLevel[1] = 0;
        return;
    }

    if (swos.patternsGoalCounter) {
        if (++swos.patternsGoalCounter >= 300)
            swos.patternsGoalCounter = 0;
    } else if (swos.goalCounter) {
        swos.patternsGoalCounter = 1;
    }

    if (swos.showFansCounter > 0) {
        m_crowdCheerLevel[0] = 1;
        m_crowdCheerLevel[1] = 1;
    } else if (swos.patternsGoalCounter > 0 && swos.patternsGoalCounter < 225) {
        bool topScored = (swos.teamScoredDataPtr && swos.teamScoredDataPtr->inGameTeamPtr.asPtr() == &swos.topTeamInGame);
        int scoringStand = topScored ? 0 : 1;
        int cheer = (swos.patternsGoalCounter < 60) ? 2 : 1;
        m_crowdCheerLevel[scoringStand] = cheer;
        m_crowdCheerLevel[1 - scoringStand] = 0;
    } else {
        m_crowdCheerLevel[0] = 0;
        m_crowdCheerLevel[1] = 0;
    }
}

static void reloadPitch(AssetResolution oldResolution, AssetResolution newResolution)
{
    if (oldResolution != AssetResolution::kInvalid) {
        for (auto& texture : m_pitchTextures[static_cast<int>(oldResolution)]) {
            if (texture) {
                SDL_DestroyTexture(texture);
                texture = nullptr;
            }
        }
    }

    m_res = static_cast<int>(newResolution);

    if (isMatchRunning() && newResolution != AssetResolution::kInvalid)
        loadPitch();
}

static std::pair<float, float> clipPitch(float widthNormalized, float heightNormalized, float& x, float& y)
{
    auto xOffset = 0.f;
    auto yOffset = 0.f;

    if (x < 0) {
        xOffset = -x;
        x = 0;
    } else if (x + widthNormalized > kPitchWidth) {
        xOffset = kPitchWidth - widthNormalized - x;
        x = kPitchWidth - widthNormalized;
    }

    if (y < kSwosPatternSize) {
        yOffset = kSwosPatternSize - y;
        y = kSwosPatternSize;
    } else if (y + heightNormalized > kPitchHeight) {
        yOffset = kPitchHeight - heightNormalized - y;
        y = kPitchHeight - heightNormalized;
    }

    return { xOffset, yOffset };
}

static float getMinimumZoom(float scale /* = getGameScale() */)
{
    assert(scale);

    auto fieldWidth = getFieldWidth();
    auto fieldHeight = getFieldHeight();

    assert(fieldWidth <= kPitchWidth && fieldHeight <= kPitchHeight);

    return std::max(fieldWidth / kPitchWidth, fieldHeight / kPitchHeight);
}

static float getDefaultZoom(float scale)
{
    constexpr float kTargetVgaWidth = 435;

    int width, height;
    std::tie(width, height) = getWindowSize();
    return width / (kTargetVgaWidth * scale);
}

static float getZoomIncrement()
{
    auto zoomRange = kMaxZoom - getMinimumZoom();
    return (m_zoom - getMinimumZoom()) / zoomRange * kMaxZoomVelocityFactor + kZoomIncrement;
}

static void clipZoom()
{
    m_zoom = std::max(m_zoom, getMinimumZoom());
    m_zoom = std::min(m_zoom, kMaxZoom);
}
