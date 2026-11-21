#include "pitchEffects.h"
#include "pitch.h"
#include "pitchConstants.h"
#include "camera.h"
#include "ball.h"
#include "gameSprites.h"
#include "render.h"
#include "gameFieldMapping.h"
#include "windowManager.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kMaxSkidMarks = 260;
constexpr int kMaxSurfaceParticles = 180;
constexpr int kNumRainDrops = 115;
constexpr int kNumSnowFlakes = 95;
constexpr int kMaxPuddleRipples = 24;

struct SkidMark
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float width = 2.0f;
    uint8_t r = 40;
    uint8_t g = 45;
    uint8_t b = 14;
    uint8_t a = 70;
};

struct SurfaceParticle
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 200;
    float size = 1.1f;
    float life = 1.0f;
    float decay = 0.08f;
    bool isBreath = false;
};

struct WeatherDrop
{
    float u = 0.0f; // normalized viewport X [0..1]
    float v = 0.0f; // normalized viewport Y [0..1]
    float speed = 1.0f;
    float length = 6.0f;
    float phase = 0.0f;
    uint8_t alpha = 120;
};

struct PuddleRipple
{
    float x = 0.0f;
    float y = 0.0f;
    float age = 0.0f;
    float maxAge = 18.0f;
    float maxRadius = 4.0f;
};

struct ReplayTrackedActor
{
    float x = 0.0f;
    float y = 0.0f;
};

static bool m_pitchWeatherEffectsEnabled = true;
static bool m_initialized = false;
static bool m_hasCustomCamera = false;
static float m_customCamX = 0.0f;
static float m_customCamY = 0.0f;

static std::vector<SkidMark> m_skidMarks;
static int m_skidWriteIndex = 0;
static std::vector<SurfaceParticle> m_particles;
static std::vector<WeatherDrop> m_weatherDrops;
static std::vector<PuddleRipple> m_puddleRipples;
static std::vector<ReplayTrackedActor> m_prevReplayActors;

static float m_prevPlayerX[22] = {};
static float m_prevPlayerY[22] = {};
static bool m_prevPlayerValid[22] = {};
static int m_breathTick = 0;
static uint32_t m_rng = 0x9E3779B9u;

static float nextRand01()
{
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>((m_rng >> 8) & 0xFFFF) / 65535.0f;
}

static float nextRandSigned()
{
    return nextRand01() * 2.0f - 1.0f;
}

static void initWeatherDrops()
{
    m_weatherDrops.clear();
    int pitchType = getPitchType();
    int count = (pitchType == 0) ? kNumSnowFlakes : ((pitchType == 1 || pitchType == 2) ? kNumRainDrops : 0);
    for (int i = 0; i < count; ++i) {
        WeatherDrop d{};
        d.u = nextRand01();
        d.v = nextRand01();
        d.phase = nextRand01() * 6.283185f;
        if (pitchType == 0) { // Frozen: drifting snowflakes
            d.speed = 0.0025f + nextRand01() * 0.0035f;
            d.length = 1.0f + nextRand01() * 1.1f;
            d.alpha = static_cast<uint8_t>(110 + nextRand01() * 95);
        } else { // Wet / Muddy: slanted rain streaks
            d.speed = (pitchType == 2 ? 0.024f : 0.017f) + nextRand01() * 0.012f;
            d.length = (pitchType == 2 ? 7.5f : 5.5f) + nextRand01() * 4.5f;
            d.alpha = static_cast<uint8_t>((pitchType == 2 ? 75 : 55) + nextRand01() * 55);
        }
        m_weatherDrops.push_back(d);
    }
}

static void addSkidMark(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float distSq = dx * dx + dy * dy;
    if (distSq < 0.2f || distSq > 400.0f)
        return;

    int pitchType = getPitchType();
    SkidMark m{};
    m.x0 = x0;
    m.y0 = y0;
    m.x1 = x1;
    m.y1 = y1;
    m.width = (pitchType == 1) ? 2.1f : 1.6f;

    if (pitchType == 0) { // Frozen: subtle scraped ice-white scuff
        m.r = 228; m.g = 242; m.b = 255; m.a = 38;
    } else if (pitchType == 1) { // Muddy: dark mud rut
        m.r = 58; m.g = 40; m.b = 16; m.a = 68;
    } else if (pitchType == 2) { // Wet: dark wet turf streak
        m.r = 28; m.g = 62; m.b = 14; m.a = 48;
    } else if (pitchType == 5 || pitchType == 6) { // Dry / Hard: dusty scuff
        m.r = 165; m.g = 152; m.b = 98; m.a = 36;
    } else { // Soft / Normal: torn grass streak
        m.r = 46; m.g = 78; m.b = 14; m.a = 44;
    }

    if (static_cast<int>(m_skidMarks.size()) < kMaxSkidMarks) {
        m_skidMarks.push_back(m);
    } else {
        m_skidMarks[m_skidWriteIndex] = m;
        m_skidWriteIndex = (m_skidWriteIndex + 1) % kMaxSkidMarks;
    }
}

static void spawnSurfaceSpray(float x, float y, float z, float vx, float vy, int count)
{
    if (static_cast<int>(m_particles.size()) >= kMaxSurfaceParticles)
        return;

    int pitchType = getPitchType();
    for (int i = 0; i < count; ++i) {
        SurfaceParticle p{};
        p.x = x + nextRandSigned() * 2.2f;
        p.y = y + nextRandSigned() * 2.0f;
        p.z = std::max(0.2f, z + nextRand01() * 1.5f);
        p.vx = -vx * (0.18f + nextRand01() * 0.25f) + nextRandSigned() * 0.75f;
        p.vy = -vy * (0.18f + nextRand01() * 0.25f) + nextRandSigned() * 0.65f;
        p.vz = 0.55f + nextRand01() * 1.15f;

        if (pitchType == 0) { // Frozen: frost/ice shards
            p.r = 225; p.g = 244; p.b = 255; p.a = 215;
            p.size = 1.05f;
        } else if (pitchType == 1) { // Muddy: mud clumps & splashes
            p.r = (i & 1) ? 74 : 92;
            p.g = (i & 1) ? 48 : 62;
            p.b = 22;
            p.a = 220;
            p.size = 1.25f;
        } else if (pitchType == 2) { // Wet: water spray droplets
            p.r = 188; p.g = 226; p.b = 250; p.a = 195;
            p.size = 1.05f;
        } else if (pitchType == 5 || pitchType == 6) { // Dry / Hard: dust puff
            p.r = 205; p.g = 194; p.b = 158; p.a = 155;
            p.size = 1.25f;
        } else { // Soft / Normal: grass blades & turf divots
            p.r = (i % 3 == 0) ? 78 : 58;
            p.g = (i % 3 == 0) ? 148 : 118;
            p.b = 24;
            p.a = 200;
            p.size = 1.1f;
        }

        p.life = 1.0f;
        p.decay = 0.065f + nextRand01() * 0.045f;
        m_particles.push_back(p);
    }
}

static void spawnColdBreathPuff(float x, float y, float z)
{
    if (static_cast<int>(m_particles.size()) >= kMaxSurfaceParticles)
        return;

    for (int i = 0; i < 2; ++i) {
        SurfaceParticle p{};
        p.x = x + nextRandSigned() * 1.5f;
        p.y = y - 1.0f;
        p.z = z + 9.0f + nextRand01() * 1.5f;
        p.vx = 0.08f + nextRandSigned() * 0.12f;
        p.vy = -0.05f + nextRandSigned() * 0.08f;
        p.vz = 0.18f + nextRand01() * 0.12f;
        p.r = 236; p.g = 245; p.b = 255; p.a = 115;
        p.size = 1.85f;
        p.life = 1.0f;
        p.decay = 0.045f;
        p.isBreath = true;
        m_particles.push_back(p);
    }
}

static void stepParticlesAndWeather()
{
    for (auto& p : m_particles) {
        p.x += p.vx;
        p.y += p.vy;
        p.z += p.vz;
        if (!p.isBreath) {
            p.vz -= 0.14f;
            if (p.z < 0.0f) {
                p.z = 0.0f;
                p.vx *= 0.45f;
                p.vy *= 0.45f;
                p.vz = 0.0f;
            }
        } else {
            p.size += 0.05f;
        }
        p.life -= p.decay;
    }
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(), [](const SurfaceParticle& p) { return p.life <= 0.0f; }),
        m_particles.end());

    int pitchType = getPitchType();
    if (m_weatherDrops.empty() && (pitchType == 0 || pitchType == 1 || pitchType == 2))
        initWeatherDrops();

    for (auto& d : m_weatherDrops) {
        if (pitchType == 0) { // Snow drift
            d.phase += 0.07f;
            d.u += std::sin(d.phase) * 0.0011f + 0.0005f;
            d.v += d.speed;
        } else { // Slanted rain
            d.u -= d.speed * 0.22f;
            d.v += d.speed;
        }
        if (d.v > 1.04f) {
            d.v = -0.04f;
            d.u = nextRand01();
        }
        if (d.u < -0.04f)
            d.u += 1.08f;
        else if (d.u > 1.04f)
            d.u -= 1.08f;
    }

    for (auto& r : m_puddleRipples)
        r.age += 1.0f;
    m_puddleRipples.erase(
        std::remove_if(m_puddleRipples.begin(), m_puddleRipples.end(), [](const PuddleRipple& r) { return r.age >= r.maxAge; }),
        m_puddleRipples.end());

    // Spawn rain puddle ripples on Wet (2) and Muddy (1) pitches around the visible camera
    if ((pitchType == 2 || pitchType == 1) && static_cast<int>(m_puddleRipples.size()) < kMaxPuddleRipples) {
        int spawnChance = (pitchType == 2) ? 2 : 1;
        for (int i = 0; i < spawnChance; ++i) {
            if (nextRand01() < 0.45f) {
                float camX = m_hasCustomCamera ? m_customCamX : getCameraX().asFloat();
                float camY = m_hasCustomCamera ? m_customCamY : getCameraY().asFloat();
                PuddleRipple rip{};
                rip.x = std::clamp(camX + nextRand01() * 320.0f, static_cast<float>(kLeftThrowInLine - 8), static_cast<float>(kRightThrowInLine + 8));
                rip.y = std::clamp(camY + nextRand01() * 200.0f, static_cast<float>(kTopPitchLine - 10), static_cast<float>(kBottomPitchLine + 10));
                rip.age = 0.0f;
                rip.maxAge = 14.0f + nextRand01() * 10.0f;
                rip.maxRadius = 2.6f + nextRand01() * 2.2f;
                m_puddleRipples.push_back(rip);
            }
        }
    }
}

struct PitchScreenMapper
{
    float camX = 0.0f;
    float camY = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
    float scale = 1.0f;
    float screenOfsX = 0.0f;
    float screenOfsY = 0.0f;
    float zoom = 1.0f;
    float halfViewW = 160.0f;
    float halfViewH = 100.0f;

    SDL_FPoint project(float worldX, float worldY, float worldZ) const
    {
        float relX = (worldX - camX) - xOffset;
        float relY = (worldY - camY - worldZ) - yOffset;
        float sx = screenOfsX + relX * scale;
        float sy = screenOfsY + relY * scale;
        sx -= (sx - halfViewW) * (1.0f - zoom);
        sy -= (sy - halfViewH) * (1.0f - zoom);
        return { sx, sy };
    }
};

static PitchScreenMapper makeMapper(float xOffset, float yOffset)
{
    PitchScreenMapper m;
    m.camX = m_hasCustomCamera ? m_customCamX : getCameraX().asFloat();
    m.camY = m_hasCustomCamera ? m_customCamY : getCameraY().asFloat();
    m.xOffset = xOffset;
    m.yOffset = yOffset;
    m.scale = getGameScale();
    m.screenOfsX = getGameScreenOffsetX();
    m.screenOfsY = getGameScreenOffsetY();
    m.zoom = getZoomFactor();
    auto vp = getViewport();
    m.halfViewW = static_cast<float>(vp.w) * 0.5f;
    m.halfViewH = static_cast<float>(vp.h) * 0.5f;
    return m;
}

static void drawFloodlightCastForActor(SDL_Renderer *renderer, const PitchScreenMapper& mapper,
    float worldX, float worldY, float radiusX, float radiusY, float pixelScale)
{
    static constexpr float kTowers[4][2] = {
        { 55.0f, 95.0f },
        { 617.0f, 95.0f },
        { 55.0f, 795.0f },
        { 617.0f, 795.0f },
    };

    SDL_FPoint base = mapper.project(worldX, worldY, 0.0f);
    for (int t = 0; t < 4; ++t) {
        float dx = worldX - kTowers[t][0];
        float dy = worldY - kTowers[t][1];
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f)
            continue;
        // Closer towers cast stronger, shorter shadows; distant towers cast softer, slightly elongated ones
        float dirX = dx / len;
        float dirY = dy / len;
        float reach = std::clamp(2.2f + (len / 420.0f) * 3.8f, 2.2f, 6.0f);
        uint8_t alpha = static_cast<uint8_t>(std::clamp(14.0f - (len / 750.0f) * 7.0f, 6.0f, 14.0f));

        SDL_FPoint tip = mapper.project(worldX + dirX * reach, worldY + dirY * reach * 0.72f, 0.0f);
        float cx = 0.5f * (base.x + tip.x);
        float cy = 0.5f * (base.y + tip.y);
        float rw = radiusX * pixelScale;
        float rh = radiusY * pixelScale;
        SDL_SetRenderDrawColor(renderer, 6, 18, 4, alpha);
        SDL_FRect r1{ cx - rw, cy - rh * 0.65f, rw * 2.0f, rh * 1.3f };
        SDL_FRect r2{ cx - rw * 0.65f, cy - rh, rw * 1.3f, rh * 2.0f };
        SDL_RenderFillRectF(renderer, &r1);
        SDL_RenderFillRectF(renderer, &r2);
    }
}

static void drawThickGroundSegment(SDL_Renderer *renderer, SDL_FPoint p0, SDL_FPoint p1,
    float halfW, SDL_Color color)
{
    float dx = p1.x - p0.x;
    float dy = p1.y - p0.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.25f)
        return;

    // Render as soft, broken turf/cleat scuffs rather than a solid geometric line
    int steps = std::clamp(static_cast<int>(std::ceil(len / std::max(1.2f, halfW * 1.2f))), 2, 10);
    float sz = std::max(1.0f, halfW * 1.35f);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < steps; ++i) {
        if ((i & 1) && steps > 3)
            continue;
        float t = static_cast<float>(i) / static_cast<float>(steps - 1);
        float sx = p0.x + dx * t;
        float sy = p0.y + dy * t;
        SDL_FRect tuft{ sx - sz * 0.5f, sy - sz * 0.4f, sz, sz * 0.8f };
        SDL_RenderFillRectF(renderer, &tuft);
    }
}

} // namespace

void initPitchEffects()
{
    m_skidMarks.clear();
    m_skidWriteIndex = 0;
    m_particles.clear();
    m_puddleRipples.clear();
    m_prevReplayActors.clear();
    std::fill(std::begin(m_prevPlayerValid), std::end(m_prevPlayerValid), false);
    m_breathTick = 0;
    initWeatherDrops();
    m_initialized = true;
}

void resetPitchEffects()
{
    initPitchEffects();
}

void updatePitchEffects()
{
    if (!m_initialized)
        initPitchEffects();

    if (!m_pitchWeatherEffectsEnabled)
        return;

    ++m_breathTick;
    int pitchType = getPitchType();

    PlayerSprites players = getPlayerSprites();
    if (players) {
        for (int i = 0; i < 22; ++i) {
            const Sprite *pl = players[i];
            if (!pl || !pl->visible) {
                m_prevPlayerValid[i] = false;
                continue;
            }
            float px = pl->x.asFloat();
            float py = pl->y.asFloat();
            float pz = std::max(0.0f, pl->z.asFloat());

            float vx = pl->deltaX.asFloat();
            float vy = pl->deltaY.asFloat();
            if (m_prevPlayerValid[i] && std::abs(vx) < 0.001f && std::abs(vy) < 0.001f) {
                vx = px - m_prevPlayerX[i];
                vy = py - m_prevPlayerY[i];
            }

            bool isSliding = (pl->state == PlayerState::kTackling || pl->state == PlayerState::kTackled);
            bool isDivingOnGround = (pl->state == PlayerState::kGoalieDivingLow || pl->state == PlayerState::kGoalieDivingHigh) && pz <= 4.0f;
            float speed = std::sqrt(vx * vx + vy * vy);

            if ((isSliding || isDivingOnGround) && speed > 0.35f) {
                if (m_prevPlayerValid[i])
                    addSkidMark(m_prevPlayerX[i], m_prevPlayerY[i], px, py);
                spawnSurfaceSpray(px, py, pz, vx, vy, isSliding ? 3 : 2);
            } else if (pitchType == 0 && ((m_breathTick + i * 17) % 75 == 0)) {
                spawnColdBreathPuff(px, py, pz);
            }

            m_prevPlayerX[i] = px;
            m_prevPlayerY[i] = py;
            m_prevPlayerValid[i] = true;
        }
    }

    stepParticlesAndWeather();
}

void updatePitchEffectsFromReplay(bool hasBall, float ballX, float ballY, float ballZ,
    const ReplayWeatherActor *players, int numPlayers)
{
    (void)hasBall;
    (void)ballX;
    (void)ballY;
    (void)ballZ;

    if (!m_initialized)
        initPitchEffects();

    if (!m_pitchWeatherEffectsEnabled)
        return;

    ++m_breathTick;
    int pitchType = getPitchType();

    std::vector<ReplayTrackedActor> nextActors;
    if (players && numPlayers > 0) {
        nextActors.reserve(numPlayers);
        for (int i = 0; i < numPlayers; ++i) {
            float px = players[i].x;
            float py = players[i].y;
            float pz = players[i].z;
            nextActors.push_back({ px, py });

            float bestDistSq = 120.0f;
            float prevX = px;
            float prevY = py;
            bool foundPrev = false;
            for (const auto& prev : m_prevReplayActors) {
                float dx = px - prev.x;
                float dy = py - prev.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < bestDistSq) {
                    bestDistSq = d2;
                    prevX = prev.x;
                    prevY = prev.y;
                    foundPrev = true;
                }
            }

            if (foundPrev) {
                float vx = px - prevX;
                float vy = py - prevY;
                float speed = std::sqrt(vx * vx + vy * vy);
                if (players[i].slidingOrDiving && speed > 0.35f) {
                    addSkidMark(prevX, prevY, px, py);
                    spawnSurfaceSpray(px, py, pz, vx, vy, 3);
                } else if (pitchType == 0 && ((m_breathTick + i * 17) % 75 == 0)) {
                    spawnColdBreathPuff(px, py, pz);
                }
            }
        }
    }
    m_prevReplayActors = std::move(nextActors);

    stepParticlesAndWeather();
}

void setPitchEffectsRenderCamera(FixedPoint cameraX, FixedPoint cameraY)
{
    m_hasCustomCamera = true;
    m_customCamX = cameraX.asFloat();
    m_customCamY = cameraY.asFloat();
}

void clearPitchEffectsRenderCamera()
{
    m_hasCustomCamera = false;
}

void drawPitchEffectsGround(float xOffset, float yOffset)
{
#ifdef SWOS_TEST
    (void)xOffset;
    (void)yOffset;
    return;
#else
    if (!m_pitchWeatherEffectsEnabled)
        return;

    auto renderer = getRenderer();
    if (!renderer)
        return;

    PitchScreenMapper mapper = makeMapper(xOffset, yOffset);
    float pixelScale = std::max(1.0f, mapper.scale * mapper.zoom);
    int pitchType = getPitchType();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 1. Organic scattered mud speckles in worn goalmouths & centre circle on Muddy pitches (no boxy rects)
    if (pitchType == 1) {
        static constexpr float kWearCenters[3][4] = {
            { 334.0f, 138.0f, 24.0f, 8.0f },
            { 334.0f, 760.0f, 24.0f, 8.0f },
            { 334.0f, 449.0f, 18.0f, 11.0f },
        };
        SDL_SetRenderDrawColor(renderer, 66, 44, 18, 52);
        for (int z = 0; z < 3; ++z) {
            for (int s = 0; s < 26; ++s) {
                float ang = s * 2.39996f + z * 1.1f;
                float rad = std::sqrt(static_cast<float>(s + 1) / 26.0f);
                float wx = kWearCenters[z][0] + std::cos(ang) * rad * kWearCenters[z][2];
                float wy = kWearCenters[z][1] + std::sin(ang) * rad * kWearCenters[z][3];
                SDL_FPoint sp = mapper.project(wx, wy, 0.0f);
                float sz = std::max(1.0f, (1.2f + (s % 3) * 0.45f) * pixelScale * 0.7f);
                SDL_FRect speck{ sp.x - sz * 0.5f, sp.y - sz * 0.5f, sz, sz * 0.75f };
                SDL_RenderFillRectF(renderer, &speck);
            }
        }
    }

    // 2. Persistent slide-tackle & keeper-dive skid marks
    for (const auto& m : m_skidMarks) {
        SDL_FPoint p0 = mapper.project(m.x0, m.y0, 0.0f);
        SDL_FPoint p1 = mapper.project(m.x1, m.y1, 0.0f);
        drawThickGroundSegment(renderer, p0, p1, m.width * 0.5f * pixelScale, SDL_Color{ m.r, m.g, m.b, m.a });
    }

    // 3. Rain puddle ripples on Wet / Muddy pitches
    for (const auto& r : m_puddleRipples) {
        float t = r.age / r.maxAge;
        SDL_FPoint cp = mapper.project(r.x, r.y, 0.0f);
        float rad = (0.6f + t * r.maxRadius) * pixelScale;
        uint8_t alpha = static_cast<uint8_t>(std::clamp((1.0f - t) * 115.0f, 0.0f, 115.0f));
        SDL_SetRenderDrawColor(renderer, 200, 232, 250, alpha);
        constexpr int kSegs = 10;
        for (int s = 0; s < kSegs; ++s) {
            float a0 = (s * 2.0f * 3.14159265f) / kSegs;
            float a1 = ((s + 1) * 2.0f * 3.14159265f) / kSegs;
            SDL_RenderDrawLineF(renderer,
                cp.x + std::cos(a0) * rad, cp.y + std::sin(a0) * rad * 0.65f,
                cp.x + std::cos(a1) * rad, cp.y + std::sin(a1) * rad * 0.65f);
        }
    }

    // 4. 4-Corner Stadium Floodlight Shadows under live players & ball
    if (!m_hasCustomCamera) {
        PlayerSprites players = getPlayerSprites();
        if (players) {
            for (int i = 0; i < 22; ++i) {
                const Sprite *pl = players[i];
                if (!pl || !pl->visible || pl->hasNoImage())
                    continue;
                drawFloodlightCastForActor(renderer, mapper, pl->x.asFloat(), pl->y.asFloat(), 3.2f, 2.0f, pixelScale);
            }
        }
        const auto& ball = swos.ballSprite;
        if (ball.visible && ball.hasImage()) {
            drawFloodlightCastForActor(renderer, mapper, ball.x.asFloat(), ball.y.asFloat(), 1.6f, 1.1f, pixelScale);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
#endif
}

void drawPitchEffectsAir(float xOffset, float yOffset)
{
#ifdef SWOS_TEST
    (void)xOffset;
    (void)yOffset;
    return;
#else
    if (!m_pitchWeatherEffectsEnabled)
        return;

    auto renderer = getRenderer();
    if (!renderer)
        return;

    PitchScreenMapper mapper = makeMapper(xOffset, yOffset);
    float pixelScale = std::max(1.0f, mapper.scale * mapper.zoom);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 1. Slide-tackle / diving-keeper surface particles and frozen breath puffs
    for (const auto& p : m_particles) {
        if (p.life <= 0.0f)
            continue;
        SDL_FPoint sp = mapper.project(p.x, p.y, p.z);
        float sz = std::max(1.0f, p.size * (p.isBreath ? 1.0f : p.life) * pixelScale * 0.8f);
        uint8_t alpha = static_cast<uint8_t>(std::clamp(p.a * p.life, 0.0f, 255.0f));
        SDL_SetRenderDrawColor(renderer, p.r, p.g, p.b, alpha);
        SDL_FRect r{ sp.x - sz * 0.5f, sp.y - sz * 0.5f, sz, sz };
        SDL_RenderFillRectF(renderer, &r);
    }

    // 2. Live Rain Streaks (Wet / Muddy) or Snowfall (Frozen) across the game viewport
    int pitchType = getPitchType();
    if (!m_weatherDrops.empty() && (pitchType == 0 || pitchType == 1 || pitchType == 2)) {
        auto vp = getViewport();
        float vpW = static_cast<float>(vp.w);
        float vpH = static_cast<float>(vp.h);
        float scale = std::max(1.0f, mapper.scale);

        for (const auto& d : m_weatherDrops) {
            float sx = d.u * vpW;
            float sy = d.v * vpH;
            if (pitchType == 0) { // Snowflakes
                float sz = std::max(1.2f, d.length * scale * 0.45f);
                SDL_SetRenderDrawColor(renderer, 240, 248, 255, d.alpha);
                SDL_FRect r{ sx, sy, sz, sz };
                SDL_RenderFillRectF(renderer, &r);
            } else { // Slanted falling rain streaks
                float len = d.length * scale * 0.55f;
                SDL_SetRenderDrawColor(renderer, 200, 226, 246, d.alpha);
                SDL_RenderDrawLineF(renderer, sx, sy, sx - len * 0.22f, sy + len);
            }
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
#endif
}

bool getPitchWeatherEffects()
{
    return m_pitchWeatherEffectsEnabled;
}

void setPitchWeatherEffects(bool enabled)
{
    m_pitchWeatherEffectsEnabled = enabled;
    if (!enabled) {
        m_skidMarks.clear();
        m_skidWriteIndex = 0;
        m_particles.clear();
        m_puddleRipples.clear();
    }
    reloadPitchTexturesIfNeeded();
}

PitchEffectsTestState getPitchEffectsTestState()
{
    PitchEffectsTestState st{};
    st.skidMarkCount = static_cast<int>(m_skidMarks.size());
    st.particleCount = static_cast<int>(m_particles.size());
    st.weatherDropCount = static_cast<int>(m_weatherDrops.size());
    st.puddleRippleCount = static_cast<int>(m_puddleRipples.size());
    return st;
}
