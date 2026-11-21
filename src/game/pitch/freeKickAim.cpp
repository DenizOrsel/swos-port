#include "freeKickAim.h"
#include "ball.h"
#include "camera.h"
#include "pitch.h"
#include "pitchConstants.h"
#include "render.h"
#include "gameFieldMapping.h"
#include "windowManager.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kMinElevation = 0.06f;  // Driven low ground shot
constexpr float kMaxElevation = 0.78f;  // High lob over defensive wall
constexpr float kDefaultElevation = 0.28f;
constexpr float kAimTurnSpeed = 0.024f;
constexpr float kElevationSpeed = 0.018f;
constexpr float kCurlAdjustSpeed = 0.035f;
constexpr float kCurlFlightAccel = 0.068f;
constexpr float kAimLineSpeed = 4.6f; // Reference preview speed (pixels/tick)

static bool m_freeKickAimingEnabled = true;
static bool m_aimInitialized = false;
static GameState m_lastState = GameState::kStartingGame;
static int m_lastFoulX = 0;
static int m_lastFoulY = 0;

static float m_aimAngle = -kPi * 0.5f;
static float m_elevation = kDefaultElevation;
static float m_curl = 0.0f;
static float m_powerCharge = 0.55f;
static int m_holdTicks = 0;

// Active ball flight state after a human free kick is struck
static bool m_flightCurlActive = false;
static float m_flightCurl = 0.0f;
static int m_flightTicks = 0;
static float m_flightDirX = 0.0f;
static float m_flightDirY = 0.0f;

static bool isFreeKickGameState(GameState state)
{
    return (state >= GameState::kFreeKickOuterLeft && state <= GameState::kFreeKickOuterRight) ||
        state == GameState::kFoul;
}

static void resetAimForBallPosition()
{
    float bx = swos.ballSprite.x.asFloat();
    float by = swos.ballSprite.y.asFloat();
    bool attackingTop = (swos.cameraDirection == Direction::kTop) || (by <= kPitchCenterY);
    float targetX = 334.0f;
    float targetY = attackingTop ? 129.0f : 769.0f;

    m_aimAngle = std::atan2(targetY - by, targetX - bx);
    m_elevation = kDefaultElevation;
    m_curl = 0.0f;
    m_powerCharge = 0.55f;
    m_holdTicks = 0;
    m_aimInitialized = true;
}

} // namespace

void initFreeKickAim()
{
    m_aimInitialized = false;
    m_lastState = GameState::kStartingGame;
    m_lastFoulX = 0;
    m_lastFoulY = 0;
    m_aimAngle = -kPi * 0.5f;
    m_elevation = kDefaultElevation;
    m_curl = 0.0f;
    m_powerCharge = 0.55f;
    m_holdTicks = 0;
    m_flightCurlActive = false;
    m_flightCurl = 0.0f;
    m_flightTicks = 0;
}

bool isFreeKickAimingActive()
{
    if (!m_freeKickAimingEnabled)
        return false;
    if (swos.gameStatePl != GameState::kWaitingOnPlayer)
        return false;
    if (!isFreeKickGameState(swos.gameState))
        return false;
    if (!swos.lastTeamPlayedBeforeBreak || !swos.lastTeamPlayedBeforeBreak->playerNumber)
        return false;
    return true;
}

bool isFreeKickAimingActiveForTeam(const TeamGeneralInfo *team)
{
    return isFreeKickAimingActive() && team == swos.lastTeamPlayedBeforeBreak;
}

void updateFreeKickAimInput(TeamGeneralInfo *team, GameControlEvents events)
{
    if (!isFreeKickAimingActiveForTeam(team)) {
        if (!isFreeKickAimingActive())
            m_aimInitialized = false;
        return;
    }

    if (!m_aimInitialized || m_lastState != swos.gameState ||
        m_lastFoulX != swos.foulXCoordinate || m_lastFoulY != swos.foulYCoordinate) {
        resetAimForBallPosition();
        m_lastState = swos.gameState;
        m_lastFoulX = swos.foulXCoordinate;
        m_lastFoulY = swos.foulYCoordinate;
    }

    const bool left = (events & kGameEventLeft) != 0;
    const bool right = (events & kGameEventRight) != 0;
    const bool up = (events & kGameEventUp) != 0;
    const bool down = (events & kGameEventDown) != 0;
    const bool curlMod = (events & kGameEventFreeKickCurl) != 0;
    const bool fire = (events & kGameEventKick) != 0;

    if (up)
        m_elevation = std::max(kMinElevation, m_elevation - kElevationSpeed);
    if (down)
        m_elevation = std::min(kMaxElevation, m_elevation + kElevationSpeed);

    if (curlMod) {
        if (left)
            m_curl = std::max(-1.0f, m_curl - kCurlAdjustSpeed);
        if (right)
            m_curl = std::min(1.0f, m_curl + kCurlAdjustSpeed);
    } else {
        if (left)
            m_aimAngle -= kAimTurnSpeed;
        if (right)
            m_aimAngle += kAimTurnSpeed;
    }

    if (fire) {
        ++m_holdTicks;
        m_powerCharge = std::clamp(0.35f + static_cast<float>(m_holdTicks) / 14.0f * 0.65f, 0.35f, 1.0f);
    } else {
        m_holdTicks = 0;
    }

    // Keep kicker's facing direction synced with the aimed trajectory
    if (team->controlledPlayer) {
        float deg = m_aimAngle * (180.0f / kPi);
        // SWOS Direction: 0=Top(-90deg), 1=TopRight(-45deg), 2=Right(0deg), 3=BottomRight(45deg)...
        int dir = static_cast<int>(std::round((deg + 90.0f) / 45.0f)) & 7;
        team->controlledPlayer->direction = static_cast<Direction>(dir);
    }
}

void onFreeKickLaunched()
{
    if (!m_freeKickAimingEnabled || !m_aimInitialized)
        return;

    // Combine SWOS's kick power with our held charge (clamped to crisp, realistic free-kick speed)
    float swosSpeed = FixedPoint::fromRaw(static_cast<int32_t>(static_cast<uint16_t>(swos.ballSprite.speed)) << 7).asFloat();
    float baseSpeed = std::clamp(std::max(swosSpeed, 2.8f + m_powerCharge * 2.55f), 2.8f, 5.45f);

    float cosElev = std::cos(m_elevation * 0.85f);
    float sinElev = std::sin(m_elevation * 0.85f);
    float planarSpeed = std::max(1.8f, baseSpeed * (0.72f + 0.28f * cosElev));

    float dirX = std::cos(m_aimAngle);
    float dirY = std::sin(m_aimAngle);
    constexpr float kTargetDist = 600.0f;

    swos.ballSprite.destX = static_cast<int16_t>(std::clamp(swos.ballSprite.x.asFloat() + dirX * kTargetDist, -4000.0f, 4000.0f));
    swos.ballSprite.destY = static_cast<int16_t>(std::clamp(swos.ballSprite.y.asFloat() + dirY * kTargetDist, -4000.0f, 4000.0f));
    swos.ballSprite.speed = static_cast<int16_t>(std::clamp(planarSpeed * 512.0f, 1024.0f, 3200.0f));

    // Elevation maps smoothly from flat ground shot (deltaZ ~ 0.18) up to high curling lob (deltaZ ~ 2.45)
    float launchVZ = std::clamp(0.18f + sinElev * (1.75f + m_powerCharge * 1.15f), 0.18f, 2.55f);
    swos.ballSprite.deltaZ = FixedPoint::fromRaw(static_cast<int32_t>(launchVZ * 65536.0f));

    m_flightCurl = m_curl;
    m_flightCurlActive = (std::abs(m_flightCurl) > 0.02f);
    m_flightTicks = 0;
    m_flightDirX = dirX;
    m_flightDirY = dirY;
    m_aimInitialized = false;
}

void applyActiveFreeKickCurl()
{
    if (!m_flightCurlActive)
        return;

    if (swos.gameStatePl != GameState::kInProgress ||
        swos.topTeamData.playerHasBall || swos.bottomTeamData.playerHasBall ||
        swos.ballSprite.speed < 220 || m_flightTicks > 48) {
        m_flightCurlActive = false;
        return;
    }

    ++m_flightTicks;
    // Stop curling once the ball drops to the grass after its initial arc
    if (m_flightTicks > 6 && swos.ballSprite.z.whole() <= 0 && swos.ballSprite.deltaZ.raw() >= 0) {
        m_flightCurlActive = false;
        return;
    }

    // Rotate the ball's destination vector smoothly around the ball to create true Magnus swerve
    float curDX = static_cast<float>(swos.ballSprite.destX) - swos.ballSprite.x.asFloat();
    float curDY = static_cast<float>(swos.ballSprite.destY) - swos.ballSprite.y.asFloat();
    float dist = std::sqrt(curDX * curDX + curDY * curDY);
    if (dist < 10.0f) {
        m_flightCurlActive = false;
        return;
    }

    float decay = 1.0f - std::clamp(static_cast<float>(m_flightTicks) / 55.0f, 0.0f, 0.7f);
    float dTheta = m_flightCurl * kCurlFlightAccel * 0.42f * decay;
    float cs = std::cos(dTheta);
    float sn = std::sin(dTheta);
    float rotX = curDX * cs - curDY * sn;
    float rotY = curDX * sn + curDY * cs;

    swos.ballSprite.destX = static_cast<int16_t>(std::clamp(swos.ballSprite.x.asFloat() + rotX, -4000.0f, 4000.0f));
    swos.ballSprite.destY = static_cast<int16_t>(std::clamp(swos.ballSprite.y.asFloat() + rotY, -4000.0f, 4000.0f));
}

void drawFreeKickAimTrajectory(float xOffset, float yOffset)
{
#ifdef SWOS_TEST
    (void)xOffset;
    (void)yOffset;
    return;
#else
    if (!isFreeKickAimingActive() || !m_aimInitialized)
        return;

    auto renderer = getRenderer();
    if (!renderer)
        return;

    float camX = getCameraX().asFloat();
    float camY = getCameraY().asFloat();
    float scale = getGameScale();
    float screenOfsX = getGameScreenOffsetX();
    float screenOfsY = getGameScreenOffsetY();
    float zoom = getZoomFactor();
    auto vp = getViewport();
    float halfViewW = static_cast<float>(vp.w) * 0.5f;
    float halfViewH = static_cast<float>(vp.h) * 0.5f;

    auto project = [&](float wx, float wy, float wz) -> SDL_FPoint {
        float relX = (wx - camX) - xOffset;
        float relY = (wy - camY - wz) - yOffset;
        float sx = screenOfsX + relX * scale;
        float sy = screenOfsY + relY * scale;
        sx -= (sx - halfViewW) * (1.0f - zoom);
        sy -= (sy - halfViewH) * (1.0f - zoom);
        return { sx, sy };
    };

    float pixelScale = std::max(1.0f, scale * zoom);
    float simX = swos.ballSprite.x.asFloat();
    float simY = swos.ballSprite.y.asFloat();
    float simZ = std::max(0.0f, swos.ballSprite.z.asFloat());

    float cosElev = std::cos(m_elevation * 0.85f);
    float sinElev = std::sin(m_elevation * 0.85f);
    float previewSpeed = std::clamp(2.8f + m_powerCharge * 2.55f, 2.8f, kAimLineSpeed + 0.6f);
    float planarSpeed = std::max(1.8f, previewSpeed * (0.72f + 0.28f * cosElev));
    float angle = m_aimAngle;
    float simVZ = std::clamp(0.18f + sinElev * (1.75f + m_powerCharge * 1.15f), 0.18f, 2.55f);
    float gravity = FixedPoint::fromRaw(getBallAirFriction()).asFloat();
    float airDrag = FixedPoint::fromRaw(getBallAirSpeedReduction() << 7).asFloat();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    constexpr int kTotalSimTicks = 28;
    for (int t = 1; t <= kTotalSimTicks; ++t) {
        if (std::abs(m_curl) > 0.02f && (simZ > 0.0f || t <= 6)) {
            float decay = 1.0f - std::clamp(static_cast<float>(t) / 55.0f, 0.0f, 0.7f);
            angle += m_curl * kCurlFlightAccel * 0.42f * decay;
        }
        simX += std::cos(angle) * planarSpeed;
        simY += std::sin(angle) * planarSpeed;
        planarSpeed = std::max(0.4f, planarSpeed - airDrag);
        simVZ -= gravity;
        simZ += simVZ;
        if (simZ < 0.0f) {
            simZ = 0.0f;
            simVZ = -simVZ * 0.58f;
            if (simVZ < 0.35f)
                simVZ = 0.0f;
        }

        if (t % 2 == 0) {
            float frac = 1.0f - static_cast<float>(t) / static_cast<float>(kTotalSimTicks + 4);
            SDL_FPoint shadowPt = project(simX + simZ * 0.5f, simY + simZ * 0.25f, 0.0f);
            SDL_FPoint airPt = project(simX, simY, simZ + 1.5f);

            // Ground shadow dot
            float sSz = std::max(1.2f, 1.35f * frac * pixelScale);
            uint8_t sAlpha = static_cast<uint8_t>(std::clamp(85.0f * frac, 18.0f, 85.0f));
            SDL_SetRenderDrawColor(renderer, 10, 22, 6, sAlpha);
            SDL_FRect sr{ shadowPt.x - sSz * 0.5f, shadowPt.y - sSz * 0.5f, sSz, sSz * 0.8f };
            SDL_RenderFillRectF(renderer, &sr);

            // Crisp Striker-style trajectory dot (tints slightly cyan-gold when curling)
            float dSz = std::max(1.8f, (1.9f + 0.5f * frac) * pixelScale);
            uint8_t dAlpha = static_cast<uint8_t>(std::clamp(235.0f * frac, 55.0f, 240.0f));
            uint8_t r = (std::abs(m_curl) > 0.15f) ? 255 : 255;
            uint8_t g = (std::abs(m_curl) > 0.15f) ? 235 : 252;
            uint8_t b = (std::abs(m_curl) > 0.15f) ? 135 : 240;

            // Subtle dark outline pixel for visibility on Frozen/bright pitches
            SDL_SetRenderDrawColor(renderer, 18, 24, 32, static_cast<uint8_t>(dAlpha * 0.65f));
            SDL_FRect outline{ airPt.x - (dSz + 1.2f) * 0.5f, airPt.y - (dSz + 1.2f) * 0.5f, dSz + 1.2f, dSz + 1.2f };
            SDL_RenderFillRectF(renderer, &outline);

            SDL_SetRenderDrawColor(renderer, r, g, b, dAlpha);
            SDL_FRect dot{ airPt.x - dSz * 0.5f, airPt.y - dSz * 0.5f, dSz, dSz };
            SDL_RenderFillRectF(renderer, &dot);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
#endif
}

bool getFreeKickAiming()
{
    return m_freeKickAimingEnabled;
}

void setFreeKickAiming(bool enabled)
{
    m_freeKickAimingEnabled = enabled;
    if (!enabled) {
        m_aimInitialized = false;
        m_flightCurlActive = false;
    }
}

FreeKickAimTestState getFreeKickAimTestState()
{
    FreeKickAimTestState st{};
    st.active = isFreeKickAimingActive();
    st.aimAngle = m_aimAngle;
    st.elevation = m_elevation;
    st.curl = m_curl;
    st.flightCurlActive = m_flightCurlActive;
    return st;
}
