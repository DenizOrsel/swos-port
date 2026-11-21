#include "goalNet.h"
#include "pitchConstants.h"
#include "pitch.h"
#include "camera.h"
#include "ball.h"
#include "gameSprites.h"
#include "render.h"
#include "gameFieldMapping.h"
#include "replays.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace {

constexpr int kCols = 39; // Doubled horizontal mesh density along goal width (299.5..368.5 -> ~1.81 px spacing)
constexpr int kRows = 21; // Doubled vertical/depth mesh density: 0..9 roof (front-to-back), 9..20 back wall (top-to-ground)
constexpr int kRoofBackRow = 9;
constexpr int kSideCols = 10; // Doubled U divisions for left/right side curtains
constexpr int kSideRows = 12; // Doubled V divisions for left/right side curtains

constexpr float kGoalLeftX = 299.5f;
constexpr float kGoalRightX = 368.5f;
constexpr float kGoalWidth = kGoalRightX - kGoalLeftX;

constexpr float kTopGoalFrontY = 129.0f;
constexpr float kTopGoalBackY = 117.0f;
constexpr float kBottomGoalFrontY = 769.0f;
constexpr float kBottomGoalBackY = 783.0f;

constexpr float kFrontHeightZ = 15.0f;
constexpr float kTopBackHeightZ = 13.0f;
constexpr float kBottomBackHeightZ = 11.0f;

struct NetNode
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float prevX = 0.0f;
    float prevY = 0.0f;
    float prevZ = 0.0f;
    float restX = 0.0f;
    float restY = 0.0f;
    float restZ = 0.0f;
    float invMass = 1.0f;
    uint8_t layer = 0; // 0 = back/side floor layer, 1 = roof/front overhang layer
};

struct NetSpring
{
    int a = 0;
    int b = 0;
    float restLen = 0.0f;
    float stiffness = 0.85f;
    bool draw = true;
};

struct NetQuad
{
    int n00 = 0;
    int n10 = 0;
    int n11 = 0;
    int n01 = 0;
    uint8_t layer = 0;
};

struct GoalNetMesh
{
    bool isTopGoal = true;
    std::vector<NetNode> nodes;
    std::vector<NetSpring> springs;
    std::vector<NetQuad> quads;
    int mainGridOffset = 0;
    int leftSideOffset = 0;
    int rightSideOffset = 0;
    float prevBallX = 0.0f;
    float prevBallY = 0.0f;
    float prevBallZ = 0.0f;
    float prevBallVX = 0.0f;
    float prevBallVY = 0.0f;
    float prevBallVZ = 0.0f;
    bool prevBallValid = false;
    std::vector<ReplayNetActor> prevReplayPlayers;

    // Goalframe vibration & impact flash state
    float frameVibTimer = 0.0f;
    float frameVibMaxTimer = 24.0f;
    float frameVibAmpX = 0.0f;
    float frameVibAmpY = 0.0f;
    float frameVibAmpZ = 0.0f;
    int frameVibPart = 0; // 0 = crossbar, -1 = left post, +1 = right post
    float impactFlashTimer = 0.0f;
    float impactX = 0.0f;
    float impactY = 0.0f;
    float impactZ = 0.0f;
};

struct BallTrailSample
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float speed = 0.0f;
    float spinCurl = 0.0f;
    float life = 1.0f;
};

struct FeedbackParticle
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
    float size = 1.0f;
    float life = 1.0f;
    float decay = 0.1f;
};

static bool m_physicsGoalNetsEnabled = true;
static bool m_ballAndGoalframeEffectsEnabled = true;
static GoalNetMesh m_goalNets[2];
static bool m_initialized = false;
static bool m_hasCustomRenderCamera = false;
static float m_customCameraX = 0.0f;
static float m_customCameraY = 0.0f;

static std::vector<BallTrailSample> m_ballTrail;
static std::vector<FeedbackParticle> m_particles;
static float m_prevFeedbackX = 0.0f;
static float m_prevFeedbackY = 0.0f;
static float m_prevFeedbackZ = 0.0f;
static float m_prevFeedbackVX = 0.0f;
static float m_prevFeedbackVY = 0.0f;
static float m_prevFeedbackVZ = 0.0f;
static bool m_prevFeedbackValid = false;
static uint32_t m_fxRngState = 0x57052026u;

static float nextFxRandSigned()
{
    m_fxRngState = m_fxRngState * 1664525u + 1013904223u;
    float u = static_cast<float>((m_fxRngState >> 8) & 0xFFFF) / 32767.5f;
    return u - 1.0f;
}

static void spawnWoodworkImpactParticles(float hitX, float hitY, float hitZ, float outDirX, float outDirY)
{
    if (m_particles.size() > 160)
        return;

    int pitchType = getPitchType();
    for (int i = 0; i < 14; ++i) {
        FeedbackParticle p{};
        p.x = hitX + nextFxRandSigned() * 1.2f;
        p.y = hitY + nextFxRandSigned() * 0.8f;
        p.z = std::max(0.5f, hitZ + nextFxRandSigned() * 1.2f);
        p.vx = outDirX * (0.45f + 0.35f * std::abs(nextFxRandSigned())) + nextFxRandSigned() * 1.1f;
        p.vy = outDirY * (0.45f + 0.35f * std::abs(nextFxRandSigned())) + nextFxRandSigned() * 0.9f;
        p.vz = 0.35f + std::abs(nextFxRandSigned()) * 1.15f;
        if (pitchType == 2 && (i & 1)) { // Wet pitch moisture spray off post
            p.r = 200; p.g = 232; p.b = 255; p.a = 210;
        } else { // White/silver post paint & specular flecks
            p.r = 248; p.g = 252; p.b = 255; p.a = 235;
        }
        p.size = (i % 3 == 0) ? 1.35f : 0.95f;
        p.life = 1.0f;
        p.decay = 0.075f + std::abs(nextFxRandSigned()) * 0.035f;
        m_particles.push_back(p);
    }
}

static void stepBallFeedback(float bx, float by, float bz, float bvx, float bvy, float bvz, float activeSpinSign)
{
    for (auto& s : m_ballTrail)
        s.life -= 0.085f;
    m_ballTrail.erase(
        std::remove_if(m_ballTrail.begin(), m_ballTrail.end(), [](const BallTrailSample& s) { return s.life <= 0.0f; }),
        m_ballTrail.end());

    for (auto& p : m_particles) {
        p.x += p.vx;
        p.y += p.vy;
        p.z += p.vz;
        p.vz -= 0.14f;
        if (p.z < 0.0f) {
            p.z = 0.0f;
            p.vx *= 0.55f;
            p.vy *= 0.55f;
            p.vz = 0.0f;
        }
        p.life -= p.decay;
    }
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(), [](const FeedbackParticle& p) { return p.life <= 0.0f; }),
        m_particles.end());

    if (!m_ballAndGoalframeEffectsEnabled)
        return;

    if (m_prevFeedbackValid && (std::abs(bx - m_prevFeedbackX) > 45.0f || std::abs(by - m_prevFeedbackY) > 45.0f)) {
        m_ballTrail.clear();
        m_prevFeedbackValid = false;
    }

    float planarSpeed = std::sqrt(bvx * bvx + bvy * bvy);
    float speed3D = std::sqrt(bvx * bvx + bvy * bvy + bvz * bvz);
    float prevPlanarSpeed = std::sqrt(m_prevFeedbackVX * m_prevFeedbackVX + m_prevFeedbackVY * m_prevFeedbackVY);

    float spinCurl = activeSpinSign;
    if (m_prevFeedbackValid && planarSpeed > 1.2f && prevPlanarSpeed > 1.2f) {
        float cross = (m_prevFeedbackVX * bvy - m_prevFeedbackVY * bvx) / (planarSpeed * prevPlanarSpeed);
        float measuredCurl = std::clamp(cross * 8.5f, -1.25f, 1.25f);
        if (std::abs(measuredCurl) > std::abs(spinCurl))
            spinCurl = measuredCurl;
    }

    if (speed3D > 0.35f) {
        BallTrailSample s{};
        s.x = bx;
        s.y = by;
        s.z = bz;
        s.vx = bvx;
        s.vy = bvy;
        s.vz = bvz;
        s.speed = speed3D;
        s.spinCurl = spinCurl;
        s.life = 1.0f;
        m_ballTrail.push_back(s);
        if (m_ballTrail.size() > 14)
            m_ballTrail.erase(m_ballTrail.begin());
    }

    // Pitch-condition surface spray on fast ground skid, hard bounce, or power kick launch
    int pitchType = getPitchType();
    bool fastGroundSkid = (bz <= 1.1f && planarSpeed >= 2.15f);
    bool groundBounce = (m_prevFeedbackValid && m_prevFeedbackZ > 1.2f && bz <= 0.85f && m_prevFeedbackVZ < -0.35f && speed3D >= 1.3f);
    bool powerKickLaunch = (m_prevFeedbackValid && prevPlanarSpeed < 1.3f && speed3D >= 3.3f && bz <= 2.5f);

    if ((fastGroundSkid || groundBounce || powerKickLaunch) && m_particles.size() < 150) {
        int count = (groundBounce || powerKickLaunch) ? 5 : ((pitchType <= 2) ? 2 : 0);
        for (int i = 0; i < count; ++i) {
            FeedbackParticle p{};
            p.x = bx - bvx * 0.35f + nextFxRandSigned() * 1.1f;
            p.y = by - bvy * 0.35f + nextFxRandSigned() * 1.1f;
            p.z = std::max(0.1f, bz * 0.5f);
            p.vx = -bvx * (0.18f + 0.12f * std::abs(nextFxRandSigned())) + nextFxRandSigned() * 0.65f;
            p.vy = -bvy * (0.18f + 0.12f * std::abs(nextFxRandSigned())) + nextFxRandSigned() * 0.65f;
            p.vz = (groundBounce || powerKickLaunch)
                ? (0.55f + std::abs(nextFxRandSigned()) * 0.95f)
                : (0.28f + std::abs(nextFxRandSigned()) * 0.55f);

            if (pitchType == 2) { // Wet: water spray droplets
                p.r = 190; p.g = 226; p.b = 248; p.a = 175;
            } else if (pitchType == 1) { // Muddy: mud splash droplets
                p.r = 76; p.g = 52; p.b = 26; p.a = 190;
            } else if (pitchType == 0) { // Frozen: frost/ice crystals
                p.r = 225; p.g = 242; p.b = 255; p.a = 180;
            } else if (pitchType == 5 || pitchType == 6) { // Dry / Hard: dust puff
                p.r = 205; p.g = 194; p.b = 162; p.a = 150;
            } else { // Soft / Normal: grass & turf blades
                p.r = 62; p.g = 128; p.b = 28; p.a = 180;
            }
            p.size = (groundBounce || powerKickLaunch) ? 1.15f : 0.9f;
            p.life = 1.0f;
            p.decay = 0.09f + std::abs(nextFxRandSigned()) * 0.04f;
            m_particles.push_back(p);
        }
    }

    m_prevFeedbackX = bx;
    m_prevFeedbackY = by;
    m_prevFeedbackZ = bz;
    m_prevFeedbackVX = bvx;
    m_prevFeedbackVY = bvy;
    m_prevFeedbackVZ = bvz;
    m_prevFeedbackValid = true;
}

static int mainNodeIndex(const GoalNetMesh& mesh, int r, int c)
{
    return mesh.mainGridOffset + r * kCols + c;
}

static int leftSideNodeIndex(const GoalNetMesh& mesh, int r, int c)
{
    return mesh.leftSideOffset + r * kSideCols + c;
}

static int rightSideNodeIndex(const GoalNetMesh& mesh, int r, int c)
{
    return mesh.rightSideOffset + r * kSideCols + c;
}

static void stepGoalFrameVibration(GoalNetMesh& mesh)
{
    if (mesh.impactFlashTimer > 0.0f)
        mesh.impactFlashTimer = std::max(0.0f, mesh.impactFlashTimer - 1.0f);

    if (mesh.frameVibTimer <= 0.0f)
        return;

    mesh.frameVibTimer = std::max(0.0f, mesh.frameVibTimer - 1.0f);
    if (!m_ballAndGoalframeEffectsEnabled || !m_physicsGoalNetsEnabled)
        return;

    // While the struck post or crossbar is ringing (first 16 frames), transmit its harmonic vibration
    // into the adjacent unpinned roof and side-curtain net nodes so the net shivers with the woodwork.
    float elapsed = mesh.frameVibMaxTimer - mesh.frameVibTimer;
    if (elapsed <= 16.0f) {
        float decay = mesh.frameVibTimer / mesh.frameVibMaxTimer;
        float oscVel = -decay * std::sin(elapsed * 1.95f) * 0.42f;
        float vx = mesh.frameVibAmpX * oscVel;
        float vy = mesh.frameVibAmpY * oscVel;
        float vz = mesh.frameVibAmpZ * oscVel;

        for (int r = 1; r <= 3 && r < kRows; ++r) {
            float rowWeight = (4 - r) / 3.0f;
            for (int c = 0; c < kCols; ++c) {
                float u = static_cast<float>(c) / static_cast<float>(kCols - 1);
                float colWeight = 0.0f;
                if (mesh.frameVibPart == 0) {
                    colWeight = 0.35f + 0.65f * std::sin(u * 3.14159265f);
                } else if (mesh.frameVibPart < 0) {
                    colWeight = std::max(0.0f, 1.0f - u * 2.2f);
                } else {
                    colWeight = std::max(0.0f, (u - 0.55f) * 2.2f);
                }
                if (colWeight > 0.01f) {
                    auto& n = mesh.nodes[mainNodeIndex(mesh, r, c)];
                    if (n.invMass > 0.0f) {
                        float w = rowWeight * colWeight * n.invMass;
                        n.x += vx * w;
                        n.y += vy * w;
                        n.z = std::max(0.0f, n.z + vz * w);
                    }
                }
            }
        }

        if (mesh.frameVibPart != 0) {
            for (int r = 0; r < kSideRows; ++r) {
                for (int c = 1; c <= 3 && c < kSideCols; ++c) {
                    int idx = (mesh.frameVibPart < 0)
                        ? leftSideNodeIndex(mesh, r, c)
                        : rightSideNodeIndex(mesh, r, c);
                    auto& n = mesh.nodes[idx];
                    if (n.invMass > 0.0f) {
                        float w = ((4 - c) / 3.0f) * n.invMass;
                        n.x += vx * w * 1.15f;
                        n.y += vy * w * 0.85f;
                        n.z = std::max(0.0f, n.z + vz * w * 0.65f);
                    }
                }
            }
        }
    }
}

static void addSpring(GoalNetMesh& mesh, int a, int b, float stiffness, bool draw)
{
    const auto& na = mesh.nodes[a];
    const auto& nb = mesh.nodes[b];
    float dx = nb.restX - na.restX;
    float dy = nb.restY - na.restY;
    float dz = nb.restZ - na.restZ;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 0.001f) {
        mesh.springs.push_back({ a, b, len, stiffness, draw });
    }
}

static void buildGoalNetMesh(GoalNetMesh& mesh, bool isTopGoal)
{
    mesh.isTopGoal = isTopGoal;
    mesh.nodes.clear();
    mesh.springs.clear();
    mesh.quads.clear();
    mesh.prevBallValid = false;

    const float frontY = isTopGoal ? kTopGoalFrontY : kBottomGoalFrontY;
    const float backY = isTopGoal ? kTopGoalBackY : kBottomGoalBackY;
    const float backHeightZ = isTopGoal ? kTopBackHeightZ : kBottomBackHeightZ;
    const float botBackY = isTopGoal ? (backY + 1.5f) : (backY + 2.0f);

    // 1. Main roof + rear sheet (kRows x kCols)
    mesh.mainGridOffset = 0;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            float u = static_cast<float>(c) / static_cast<float>(kCols - 1);
            float x = kGoalLeftX + u * kGoalWidth;
            float y = 0.0f;
            float z = 0.0f;
            uint8_t layer = 0;

            if (r <= kRoofBackRow) {
                float t = static_cast<float>(r) / static_cast<float>(kRoofBackRow);
                y = frontY + t * (backY - frontY);
                // Slight natural catenary sag in the roof mesh
                float sag = 0.85f * std::sin(t * 3.14159265f) * std::sin(u * 3.14159265f);
                z = kFrontHeightZ + t * (backHeightZ - kFrontHeightZ) - sag;
                layer = 1;
            } else {
                float t = static_cast<float>(r - kRoofBackRow) / static_cast<float>(kRows - 1 - kRoofBackRow);
                y = backY + t * (botBackY - backY);
                z = backHeightZ * (1.0f - t);
                // For the bottom goal (camera looking from south/above), the rear wall leans over the inside of the goal
                layer = isTopGoal ? 0 : 1;
            }

            NetNode node{};
            node.x = node.prevX = node.restX = x;
            node.y = node.prevY = node.restY = y;
            node.z = node.prevZ = node.restZ = z;
            node.layer = layer;

            // Pin the front crossbar (r == 0) and ground pegs (r == kRows - 1),
            // and give the rear top stanchion corners a stiffer tether
            if (r == 0) {
                node.invMass = 0.0f; // Rigidly attached to front crossbar
            } else if (r == kRows - 1) {
                node.invMass = (c == 0 || c == kCols - 1 || (c % 4 == 0)) ? 0.0f : 0.35f;
            } else if (r == kRoofBackRow && (c == 0 || c == kCols - 1)) {
                node.invMass = 0.15f; // Stanchion corner support
            } else {
                node.invMass = 1.0f;
            }

            mesh.nodes.push_back(node);
        }
    }

    // Structural, shear, and bend springs for main sheet
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            int cur = mainNodeIndex(mesh, r, c);
            if (c + 1 < kCols) {
                addSpring(mesh, cur, mainNodeIndex(mesh, r, c + 1), 0.90f, true);
            }
            if (r + 1 < kRows) {
                // Keep vertical structural springs for physics, but hide vertical lines (draw = false)
                addSpring(mesh, cur, mainNodeIndex(mesh, r + 1, c), 0.90f, false);
            }
            // Diagonal shear springs for diamond weave & wave propagation
            if (r + 1 < kRows && c + 1 < kCols) {
                addSpring(mesh, cur, mainNodeIndex(mesh, r + 1, c + 1), 0.55f, false);
                addSpring(mesh, mainNodeIndex(mesh, r + 1, c), mainNodeIndex(mesh, r, c + 1), 0.55f, false);
                uint8_t qLayer = (r < kRoofBackRow || !isTopGoal) ? 1 : 0;
                mesh.quads.push_back({
                    cur,
                    mainNodeIndex(mesh, r, c + 1),
                    mainNodeIndex(mesh, r + 1, c + 1),
                    mainNodeIndex(mesh, r + 1, c),
                    qLayer
                });
            }
            // Bend springs
            if (c + 2 < kCols) {
                addSpring(mesh, cur, mainNodeIndex(mesh, r, c + 2), 0.35f, false);
            }
            if (r + 2 < kRows) {
                addSpring(mesh, cur, mainNodeIndex(mesh, r + 2, c), 0.35f, false);
            }
        }
    }

    // 2. Left and Right Side Curtains (kSideRows x kSideCols each)
    auto buildSideCurtain = [&](bool isLeft) {
        int baseOfs = static_cast<int>(mesh.nodes.size());
        if (isLeft)
            mesh.leftSideOffset = baseOfs;
        else
            mesh.rightSideOffset = baseOfs;

        float sideX = isLeft ? kGoalLeftX : kGoalRightX;
        for (int r = 0; r < kSideRows; ++r) {
            float v = static_cast<float>(r) / static_cast<float>(kSideRows - 1);
            for (int c = 0; c < kSideCols; ++c) {
                float u = static_cast<float>(c) / static_cast<float>(kSideCols - 1);
                float topY = frontY + u * (backY - frontY);
                float botY = frontY + u * (botBackY - frontY);
                float y = topY + v * (botY - topY);

                float topZ = kFrontHeightZ + u * (backHeightZ - kFrontHeightZ);
                float z = topZ * (1.0f - v);

                NetNode node{};
                node.x = node.prevX = node.restX = sideX;
                node.y = node.prevY = node.restY = y;
                node.z = node.prevZ = node.restZ = z;
                node.layer = 1;

                if (c == 0) {
                    node.invMass = 0.0f; // Pinned to front vertical goalpost
                } else if (r == kSideRows - 1 && (c == kSideCols - 1 || c % 2 == 0)) {
                    node.invMass = 0.0f; // Ground peg along side netting
                } else if (r == 0 && c == kSideCols - 1) {
                    node.invMass = 0.15f; // Top rear stanchion corner
                } else {
                    node.invMass = 1.0f;
                }
                mesh.nodes.push_back(node);
            }
        }

        auto sideIdx = [&](int r, int c) {
            return baseOfs + r * kSideCols + c;
        };

        for (int r = 0; r < kSideRows; ++r) {
            for (int c = 0; c < kSideCols; ++c) {
                int cur = sideIdx(r, c);
                if (c + 1 < kSideCols)
                    addSpring(mesh, cur, sideIdx(r, c + 1), 0.90f, true);
                if (r + 1 < kSideRows)
                    addSpring(mesh, cur, sideIdx(r + 1, c), 0.90f, false); // Hide vertical side lines too
                if (r + 1 < kSideRows && c + 1 < kSideCols) {
                    addSpring(mesh, cur, sideIdx(r + 1, c + 1), 0.55f, false);
                    addSpring(mesh, sideIdx(r + 1, c), sideIdx(r, c + 1), 0.55f, false);
                    mesh.quads.push_back({
                        cur,
                        sideIdx(r, c + 1),
                        sideIdx(r + 1, c + 1),
                        sideIdx(r + 1, c),
                        1
                    });
                }
            }
        }

        // Stitch side curtain's top edge (r == 0, c = 0..kSideCols-1) to main roof edge (r = 0..kRoofBackRow)
        int mainEdgeCol = isLeft ? 0 : (kCols - 1);
        for (int c = 0; c < kSideCols && c <= kRoofBackRow; ++c) {
            addSpring(mesh, sideIdx(0, c), mainNodeIndex(mesh, c, mainEdgeCol), 0.95f, false);
        }
        // Stitch side curtain's rear edge (c == kSideCols-1, r = 0..kSideRows-1) to main rear wall edge (r = kRoofBackRow..kRows-1)
        for (int r = 0; r < kSideRows && (kRoofBackRow + r) < kRows; ++r) {
            addSpring(mesh, sideIdx(r, kSideCols - 1), mainNodeIndex(mesh, kRoofBackRow + r, mainEdgeCol), 0.95f, false);
        }
    };

    buildSideCurtain(true);
    buildSideCurtain(false);
}

static void interactNodeWithSphere(NetNode& node, float sx, float sy, float sz,
    float vx, float vy, float vz, float radius, float pushStrength)
{
    if (node.invMass <= 0.0f)
        return;

    float dx = node.x - sx;
    float dy = node.y - sy;
    float dz = node.z - sz;
    float distSq = dx * dx + dy * dy + dz * dz;
    if (distSq >= radius * radius)
        return;

    float dist = std::sqrt(std::max(distSq, 0.0001f));
    float falloff = 1.0f - (dist / radius);
    float falloffSq = falloff * falloff;

    // Push outward along sphere normal + carry momentum from ball/player velocity
    float nx = dx / dist;
    float ny = dy / dist;
    float nz = dz / dist;

    float pen = (radius - dist) * 0.45f * node.invMass;
    node.x += nx * pen + vx * pushStrength * falloffSq * node.invMass;
    node.y += ny * pen + vy * pushStrength * falloffSq * node.invMass;
    node.z += nz * pen + vz * pushStrength * falloffSq * node.invMass;
    if (node.z < 0.0f)
        node.z = 0.0f;
}

static void stepGoalNetPhysics(GoalNetMesh& mesh)
{
    // 1. Continuous proximity interaction with the moving ball when inside/near the goal volume
    const auto& ball = swos.ballSprite;
    float bx = ball.x.asFloat();
    float by = ball.y.asFloat();
    float bz = std::max(0.0f, ball.z.asFloat());

    float bvx = ball.deltaX.asFloat();
    float bvy = ball.deltaY.asFloat();
    float bvz = ball.deltaZ.asFloat();
    if (mesh.prevBallValid && std::abs(bvx) < 0.001f && std::abs(bvy) < 0.001f) {
        bvx = bx - mesh.prevBallX;
        bvy = by - mesh.prevBallY;
        bvz = bz - mesh.prevBallZ;
    }
    mesh.prevBallX = bx;
    mesh.prevBallY = by;
    mesh.prevBallZ = bz;
    mesh.prevBallValid = true;

    bool ballNearGoalY = mesh.isTopGoal
        ? (by >= kTopGoalBackY - 10.0f && by <= kTopGoalFrontY + 8.0f)
        : (by >= kBottomGoalFrontY - 8.0f && by <= kBottomGoalBackY + 10.0f);
    bool ballNearGoalX = (bx >= kGoalLeftX - 10.0f && bx <= kGoalRightX + 10.0f);

    if (ballNearGoalX && ballNearGoalY && bz <= kFrontHeightZ + 8.0f) {
        float speedMag = std::sqrt(bvx * bvx + bvy * bvy + bvz * bvz);
        if (speedMag > 0.05f) {
            for (auto& node : mesh.nodes) {
                interactNodeWithSphere(node, bx, by, bz, bvx, bvy, bvz, 11.0f, 0.55f);
            }
        }
    }

    // 2. Also let players running inside the goal mouth brush the net
    PlayerSprites players = getPlayerSprites();
    if (players) {
        for (int i = 0; i < 22; ++i) {
            const Sprite *pl = players[i];
            if (!pl || !pl->visible)
                continue;
            float px = pl->x.asFloat();
            float py = pl->y.asFloat();
            float pz = std::max(0.0f, pl->z.asFloat()) + 6.0f;
            bool plNearY = mesh.isTopGoal
                ? (py >= kTopGoalBackY - 6.0f && py <= kTopGoalFrontY + 3.0f)
                : (py >= kBottomGoalFrontY - 3.0f && py <= kBottomGoalBackY + 6.0f);
            if (plNearY && px >= kGoalLeftX - 6.0f && px <= kGoalRightX + 6.0f) {
                float pvx = pl->deltaX.asFloat();
                float pvy = pl->deltaY.asFloat();
                if (std::abs(pvx) > 0.05f || std::abs(pvy) > 0.05f) {
                    for (auto& node : mesh.nodes) {
                        interactNodeWithSphere(node, px, py, pz, pvx, pvy, 0.0f, 8.5f, 0.30f);
                    }
                }
            }
        }
    }

    // 3. Verlet Integration + soft tether toward rest shape (keeps the net taut and prevents collapse)
}

static void integrateAndSolveGoalNet(GoalNetMesh& mesh)
{
    const float frontY = mesh.isTopGoal ? kTopGoalFrontY : kBottomGoalFrontY;
    const float backY = mesh.isTopGoal ? kTopGoalBackY : kBottomGoalBackY;

    constexpr float kDamping = 0.88f;
    constexpr float kRestRestore = 0.045f;
    constexpr float kGravityZ = -0.015f;

    for (auto& node : mesh.nodes) {
        if (node.invMass <= 0.0f)
            continue;

        float vx = (node.x - node.prevX) * kDamping;
        float vy = (node.y - node.prevY) * kDamping;
        float vz = (node.z - node.prevZ) * kDamping;

        node.prevX = node.x;
        node.prevY = node.y;
        node.prevZ = node.z;

        node.x += vx + (node.restX - node.x) * kRestRestore;
        node.y += vy + (node.restY - node.y) * kRestRestore;
        node.z += vz + (node.restZ - node.z) * kRestRestore + kGravityZ * node.invMass;

        if (node.z < 0.0f)
            node.z = 0.0f;

        // Prevent net from inverting forward onto the pitch past the goal line
        if (mesh.isTopGoal) {
            if (node.y > frontY + 0.5f)
                node.y = frontY + 0.5f;
        } else {
            if (node.y < frontY - 0.5f)
                node.y = frontY - 0.5f;
        }
        (void)backY;
    }

    // 4. Iterative Gauss-Seidel spring distance constraint solver (4 sub-iterations)
    constexpr int kSolverIterations = 4;
    for (int iter = 0; iter < kSolverIterations; ++iter) {
        for (const auto& sp : mesh.springs) {
            auto& na = mesh.nodes[sp.a];
            auto& nb = mesh.nodes[sp.b];
            float wSum = na.invMass + nb.invMass;
            if (wSum <= 0.0f)
                continue;

            float dx = nb.x - na.x;
            float dy = nb.y - na.y;
            float dz = nb.z - na.z;
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 0.0001f)
                continue;

            float diff = (dist - sp.restLen) / dist;
            // Allow net strands to compress slightly more easily than they stretch (fabric behavior)
            float k = (dist >= sp.restLen) ? sp.stiffness : (sp.stiffness * 0.45f);
            float corrX = dx * diff * k / wSum;
            float corrY = dy * diff * k / wSum;
            float corrZ = dz * diff * k / wSum;

            if (na.invMass > 0.0f) {
                na.x += corrX * na.invMass;
                na.y += corrY * na.invMass;
                na.z += corrZ * na.invMass;
            }
            if (nb.invMass > 0.0f) {
                nb.x -= corrX * nb.invMass;
                nb.y -= corrY * nb.invMass;
                nb.z -= corrZ * nb.invMass;
            }
        }
    }
}

static void stepGoalNetReplayPhysics(GoalNetMesh& mesh, bool hasBall, float bx, float by, float bz,
    const ReplayNetActor *players, int numPlayers)
{
    if (hasBall) {
        float bvx = 0.0f;
        float bvy = 0.0f;
        float bvz = 0.0f;
        if (mesh.prevBallValid) {
            bvx = bx - mesh.prevBallX;
            bvy = by - mesh.prevBallY;
            bvz = bz - mesh.prevBallZ;
            if (std::abs(bvx) > 45.0f || std::abs(bvy) > 45.0f) {
                bvx = bvy = bvz = 0.0f;
                mesh.prevBallVX = mesh.prevBallVY = mesh.prevBallVZ = 0.0f;
            }
        }

        bool ballNearGoalY = mesh.isTopGoal
            ? (by >= kTopGoalBackY - 12.0f && by <= kTopGoalFrontY + 8.0f)
            : (by >= kBottomGoalFrontY - 8.0f && by <= kBottomGoalBackY + 12.0f);
        bool ballNearGoalX = (bx >= kGoalLeftX - 10.0f && bx <= kGoalRightX + 10.0f);

        if (ballNearGoalX && ballNearGoalY && bz <= kFrontHeightZ + 8.0f) {
            float curSpeed = std::sqrt(bvx * bvx + bvy * bvy + bvz * bvz);
            float prevSpeed = std::sqrt(
                mesh.prevBallVX * mesh.prevBallVX +
                mesh.prevBallVY * mesh.prevBallVY +
                mesh.prevBallVZ * mesh.prevBallVZ);

            // In SWOS, handleGoalFrameCollisions() restores ballSprite.x/y to previousX/Y on the collision tick
            // before replay coordinates are recorded. Detect that sudden stop/rebound inside the goal volume
            // and apply the full un-clamped impact impulse into the rear wall, side curtains, or roof.
            bool insideGoalMouthY = mesh.isTopGoal
                ? (by >= kTopGoalBackY - 6.0f && by <= kTopGoalFrontY + 2.0f)
                : (by >= kBottomGoalFrontY - 2.0f && by <= kBottomGoalBackY + 6.0f);
            bool movingIntoGoal = mesh.isTopGoal
                ? (mesh.prevBallVY < -0.25f || std::abs(mesh.prevBallVX) > 0.35f)
                : (mesh.prevBallVY > 0.25f || std::abs(mesh.prevBallVX) > 0.35f);
            float dotPrevCur = bvx * mesh.prevBallVX + bvy * mesh.prevBallVY;

            // Check if the ball rebounded off the front goalpost or crossbar at the goal line during a replay
            const float frontY = mesh.isTopGoal ? kTopGoalFrontY : kBottomGoalFrontY;
            float projX = bx + mesh.prevBallVX;
            float projY = by + mesh.prevBallVY;
            float projZ = std::max(0.0f, bz + mesh.prevBallVZ);
            bool nearFrontLine = std::abs(by - frontY) <= 5.5f || std::abs(projY - frontY) <= 4.5f;
            bool hitLeftPost = std::abs(projX - kGoalLeftX) <= 4.5f && projZ <= kFrontHeightZ + 1.5f;
            bool hitRightPost = std::abs(projX - kGoalRightX) <= 4.5f && projZ <= kFrontHeightZ + 1.5f;
            bool hitBar = projX >= kGoalLeftX - 3.5f && projX <= kGoalRightX + 3.5f &&
                projZ >= 10.5f && projZ <= kFrontHeightZ + 3.5f;

            if (nearFrontLine && prevSpeed > 0.65f && movingIntoGoal &&
                (hitLeftPost || hitRightPost || hitBar) &&
                (curSpeed < prevSpeed * 0.75f || dotPrevCur <= 0.0f || (bvz * mesh.prevBallVZ) < 0.0f)) {
                notifyGoalFrameImpact(mesh.isTopGoal ? 0 : 1, projX, frontY, projZ,
                    mesh.prevBallVX, mesh.prevBallVY, mesh.prevBallVZ, hitBar && !hitLeftPost && !hitRightPost);
                mesh.prevBallVX = 0.0f;
                mesh.prevBallVY = 0.0f;
                mesh.prevBallVZ = 0.0f;
            } else if (insideGoalMouthY && prevSpeed > 0.55f && movingIntoGoal &&
                (curSpeed < prevSpeed * 0.55f || dotPrevCur <= 0.0f)) {
                if (m_physicsGoalNetsEnabled) {
                    float hitX = bx + mesh.prevBallVX * 1.35f;
                    float hitY = by + mesh.prevBallVY * 1.35f;
                    float hitZ = std::max(0.0f, bz + mesh.prevBallVZ);
                    constexpr float kImpactRadius = 19.5f;
                    for (auto& node : mesh.nodes) {
                        interactNodeWithSphere(node, hitX, hitY, hitZ,
                            mesh.prevBallVX * 1.4f, mesh.prevBallVY * 1.4f, mesh.prevBallVZ * 1.1f,
                            kImpactRadius, 1.35f);
                    }
                }
                mesh.prevBallVX = 0.0f;
                mesh.prevBallVY = 0.0f;
                mesh.prevBallVZ = 0.0f;
            } else if (curSpeed > 0.05f) {
                if (m_physicsGoalNetsEnabled) {
                    float leadX = bx + bvx * 0.85f;
                    float leadY = by + bvy * 0.85f;
                    float leadZ = std::max(0.0f, bz + bvz * 0.5f);
                    for (auto& node : mesh.nodes) {
                        interactNodeWithSphere(node, bx, by, bz, bvx, bvy, bvz, 11.0f, 0.45f);
                        interactNodeWithSphere(node, leadX, leadY, leadZ, bvx, bvy, bvz, 15.5f, 0.65f);
                    }
                }
                mesh.prevBallVX = bvx;
                mesh.prevBallVY = bvy;
                mesh.prevBallVZ = bvz;
            }
        } else {
            mesh.prevBallVX = bvx;
            mesh.prevBallVY = bvy;
            mesh.prevBallVZ = bvz;
        }

        mesh.prevBallX = bx;
        mesh.prevBallY = by;
        mesh.prevBallZ = bz;
        mesh.prevBallValid = true;
    }

    // 2. Also let players running inside the goal mouth brush the net during replays
    std::vector<ReplayNetActor> currentNearbyPlayers;
    if (players && numPlayers > 0) {
        for (int i = 0; i < numPlayers; ++i) {
            float px = players[i].x;
            float py = players[i].y;
            float pz = players[i].z;
            bool plNearY = mesh.isTopGoal
                ? (py >= kTopGoalBackY - 8.0f && py <= kTopGoalFrontY + 5.0f)
                : (py >= kBottomGoalFrontY - 5.0f && py <= kBottomGoalBackY + 8.0f);
            if (plNearY && px >= kGoalLeftX - 8.0f && px <= kGoalRightX + 8.0f) {
                currentNearbyPlayers.push_back({ px, py, pz });
                float bestDistSq = 100.0f;
                float pvx = 0.0f;
                float pvy = 0.0f;
                for (const auto& prevPl : mesh.prevReplayPlayers) {
                    float dx = px - prevPl.x;
                    float dy = py - prevPl.y;
                    float d2 = dx * dx + dy * dy;
                    if (d2 < bestDistSq) {
                        bestDistSq = d2;
                        pvx = dx;
                        pvy = dy;
                    }
                }
                if (std::abs(pvx) > 0.05f || std::abs(pvy) > 0.05f) {
                    for (auto& node : mesh.nodes) {
                        interactNodeWithSphere(node, px, py, pz, pvx, pvy, 0.0f, 8.5f, 0.30f);
                    }
                }
            }
        }
    }
    mesh.prevReplayPlayers = std::move(currentNearbyPlayers);

    stepGoalFrameVibration(mesh);
    if (m_physicsGoalNetsEnabled)
        integrateAndSolveGoalNet(mesh);
}

struct ScreenMapper
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

static ScreenMapper makeScreenMapper(float xOffset, float yOffset)
{
    ScreenMapper m;
    m.camX = m_hasCustomRenderCamera ? m_customCameraX : getCameraX().asFloat();
    m.camY = m_hasCustomRenderCamera ? m_customCameraY : getCameraY().asFloat();
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

static void drawCylindricalBar(SDL_Renderer *renderer, float x0, float y0, float x1, float y1,
    bool isHorizontal, int postSide = 0)
{
    float left = std::min(x0, x1);
    float top = std::min(y0, y1);
    float totalW = std::max(1.5f, std::max(x0, x1) - left);
    float totalH = std::max(1.5f, std::max(y0, y1) - top);

    // 7-band cylindrical shading profile from lit outer edge to soft inner roll-off,
    // WITHOUT any dark/black border on the inside section of the goal mouth!
    struct BandSpec {
        uint8_t r, g, b, a;
    };
    static constexpr BandSpec kCrossbarBands[7] = {
        { 140, 148, 155, 160 }, // 0: soft top outer edge
        { 230, 234, 238, 252 }, // 1: upper curvature
        { 252, 254, 255, 255 }, // 2: bright specular core
        { 245, 248, 250, 255 }, // 3: main cylinder body
        { 228, 233, 238, 255 }, // 4: lower curvature roll-off
        { 206, 212, 218, 250 }, // 5: soft underside silver shade
        { 188, 195, 202, 225 }, // 6: clean light-gray inner underside (no black inner border)
    };
    static constexpr BandSpec kLeftPostBands[7] = {
        { 140, 148, 155, 160 }, // 0: outer left edge
        { 230, 234, 238, 252 }, // 1: outer left curvature
        { 252, 254, 255, 255 }, // 2: bright specular core
        { 245, 248, 250, 255 }, // 3: main cylinder body
        { 228, 233, 238, 255 }, // 4: inner right curvature
        { 206, 212, 218, 250 }, // 5: soft inner shade
        { 188, 195, 202, 225 }, // 6: clean light-gray inside edge (no black inner border)
    };
    static constexpr BandSpec kRightPostBands[7] = {
        { 188, 195, 202, 225 }, // 0: clean light-gray inside edge (no black inner border)
        { 206, 212, 218, 250 }, // 1: soft inner shade
        { 228, 233, 238, 255 }, // 2: inner left curvature
        { 245, 248, 250, 255 }, // 3: main cylinder body
        { 252, 254, 255, 255 }, // 4: bright specular core
        { 230, 234, 238, 252 }, // 5: outer right curvature
        { 140, 148, 155, 160 }, // 6: outer right edge
    };

    if (isHorizontal) {
        float bandH = totalH / 7.0f;
        for (int i = 0; i < 7; ++i) {
            const auto& b = kCrossbarBands[i];
            SDL_SetRenderDrawColor(renderer, b.r, b.g, b.b, b.a);
            SDL_FRect r{ left, top + i * bandH, totalW, std::max(0.75f, bandH + 0.35f) };
            SDL_RenderFillRectF(renderer, &r);
        }
    } else {
        const auto *bands = (postSide > 0) ? kRightPostBands : kLeftPostBands;
        float bandW = totalW / 7.0f;
        for (int i = 0; i < 7; ++i) {
            const auto& b = bands[i];
            SDL_SetRenderDrawColor(renderer, b.r, b.g, b.b, b.a);
            SDL_FRect r{ left + i * bandW, top, std::max(0.75f, bandW + 0.35f), totalH };
            SDL_RenderFillRectF(renderer, &r);
        }
    }
}

static void drawThickSegment(SDL_Renderer *renderer, SDL_FPoint p0, SDL_FPoint p1,
    float halfThickness, SDL_Color color)
{
    float dx = p1.x - p0.x;
    float dy = p1.y - p0.y;
    if (std::abs(dx) < 0.75f || std::abs(dy) < 0.75f) {
        // Purely horizontal or vertical bar: use a solid filled rectangle (zero sub-pixel gaps!)
        float left = std::min(p0.x, p1.x) - (std::abs(dx) < 0.75f ? halfThickness : 0.0f);
        float right = std::max(p0.x, p1.x) + (std::abs(dx) < 0.75f ? halfThickness : 0.0f);
        float top = std::min(p0.y, p1.y) - (std::abs(dy) < 0.75f ? halfThickness : 0.0f);
        float bottom = std::max(p0.y, p1.y) + (std::abs(dy) < 0.75f ? halfThickness : 0.0f);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_FRect rect{ left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top) };
        SDL_RenderFillRectF(renderer, &rect);
        return;
    }

    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f)
        return;

    float nx = -dy / len;
    float ny = dx / len;
    int numPasses = std::clamp(static_cast<int>(std::ceil(halfThickness * 4.0f)) + 1, 2, 25);

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < numPasses; ++i) {
        float t = -halfThickness + (2.0f * halfThickness * i) / (numPasses - 1);
        SDL_RenderDrawLineF(renderer, p0.x + nx * t, p0.y + ny * t, p1.x + nx * t, p1.y + ny * t);
    }
}

static void drawDynamicGoalNetShadow(SDL_Renderer *renderer, const ScreenMapper& mapper,
    const GoalNetMesh& mesh, float pixelScale)
{
    // Projects the live 3D net and goalposts onto the grass (Z = 0) along SWOS's oblique sun vector
    // (+0.65 * Z in X, +0.38 * Z in Y), with a multi-pass penumbra so the shadow on the grass is
    // soft, dimmed, and blurry while rippling dynamically with the net.
    constexpr float kSunDirX = 0.65f;
    constexpr float kSunDirY = 0.38f;

    static std::vector<SDL_FPoint> s_shadowProj;
    s_shadowProj.resize(mesh.nodes.size());
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        const auto& n = mesh.nodes[i];
        s_shadowProj[i] = mapper.project(n.x + n.z * kSunDirX, n.y + n.z * kSunDirY, 0.0f);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 1. Soft, blurry ambient penumbra under the net footprint (3 concentric soft passes)
    float blurStep = std::max(0.75f, 0.45f * pixelScale);
    static constexpr float kBlurOffsets[5][2] = {
        {  0.0f,  0.0f },
        { -1.0f,  0.0f },
        {  1.0f,  0.0f },
        {  0.0f, -1.0f },
        {  0.0f,  1.0f },
    };

    // Ultra-low-alpha blurred mesh strands (gives a diffused shadow rather than sharp black wires)
    for (int pass = 0; pass < 5; ++pass) {
        float ox = kBlurOffsets[pass][0] * blurStep;
        float oy = kBlurOffsets[pass][1] * blurStep;
        uint8_t strandAlpha = (pass == 0) ? 10 : 5;
        SDL_SetRenderDrawColor(renderer, 8, 22, 4, strandAlpha);
        for (const auto& q : mesh.quads) {
            SDL_RenderDrawLineF(renderer,
                s_shadowProj[q.n00].x + ox, s_shadowProj[q.n00].y + oy,
                s_shadowProj[q.n11].x + ox, s_shadowProj[q.n11].y + oy);
            SDL_RenderDrawLineF(renderer,
                s_shadowProj[q.n10].x + ox, s_shadowProj[q.n10].y + oy,
                s_shadowProj[q.n01].x + ox, s_shadowProj[q.n01].y + oy);
        }
    }

    // 2. Soft multi-pass blurred goalpost & crossbar ground shadow (vibrates in sync when woodwork is struck!)
    const float frontY = mesh.isTopGoal ? kTopGoalFrontY : kBottomGoalFrontY;
    float vibDX = 0.0f;
    float vibDY = 0.0f;
    float vibDZ = 0.0f;
    if (m_ballAndGoalframeEffectsEnabled && mesh.frameVibTimer > 0.0f) {
        float elapsed = mesh.frameVibMaxTimer - mesh.frameVibTimer;
        float decay = mesh.frameVibTimer / mesh.frameVibMaxTimer;
        float osc = decay * decay * std::cos(elapsed * 1.95f);
        vibDX = mesh.frameVibAmpX * osc;
        vibDY = mesh.frameVibAmpY * osc;
        vibDZ = mesh.frameVibAmpZ * osc;
    }

    float lTopW = (mesh.frameVibPart < 0) ? 1.0f : ((mesh.frameVibPart == 0) ? 0.45f : 0.0f);
    float rTopW = (mesh.frameVibPart > 0) ? 1.0f : ((mesh.frameVibPart == 0) ? 0.45f : 0.0f);

    SDL_FPoint postLB = mapper.project(kGoalLeftX, frontY, 0.0f);
    SDL_FPoint postLTShadow = mapper.project(
        kGoalLeftX + vibDX * lTopW + (kFrontHeightZ + vibDZ * lTopW) * kSunDirX,
        frontY + vibDY * lTopW + (kFrontHeightZ + vibDZ * lTopW) * kSunDirY, 0.0f);
    SDL_FPoint postRB = mapper.project(kGoalRightX, frontY, 0.0f);
    SDL_FPoint postRTShadow = mapper.project(
        kGoalRightX + vibDX * rTopW + (kFrontHeightZ + vibDZ * rTopW) * kSunDirX,
        frontY + vibDY * rTopW + (kFrontHeightZ + vibDZ * rTopW) * kSunDirY, 0.0f);

    float outerShadowThick = std::max(1.2f, 0.95f * pixelScale);
    float innerShadowThick = std::max(0.7f, 0.48f * pixelScale);
    SDL_Color penumbraColor{ 8, 22, 4, 18 };
    SDL_Color umbraColor{ 8, 22, 4, 26 };

    drawThickSegment(renderer, postLB, postLTShadow, outerShadowThick, penumbraColor);
    drawThickSegment(renderer, postRB, postRTShadow, outerShadowThick, penumbraColor);
    drawThickSegment(renderer, postLTShadow, postRTShadow, outerShadowThick, penumbraColor);

    drawThickSegment(renderer, postLB, postLTShadow, innerShadowThick, umbraColor);
    drawThickSegment(renderer, postRB, postRTShadow, innerShadowThick, umbraColor);
    drawThickSegment(renderer, postLTShadow, postRTShadow, innerShadowThick, umbraColor);
}

static void drawGoalNetLayer(const GoalNetMesh& mesh, uint8_t layer, float xOffset, float yOffset)
{
#ifdef SWOS_TEST
    (void)mesh;
    (void)layer;
    (void)xOffset;
    (void)yOffset;
    return;
#else
    auto renderer = getRenderer();
    if (!renderer)
        return;

    ScreenMapper mapper = makeScreenMapper(xOffset, yOffset);
    float pixelScale = std::max(1.0f, mapper.scale * mapper.zoom);

    if (layer == 0) {
        drawDynamicGoalNetShadow(renderer, mapper, mesh, pixelScale);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 1. Project all nodes once
    static std::vector<SDL_FPoint> s_proj;
    static std::vector<float> s_disp;
    s_proj.resize(mesh.nodes.size());
    s_disp.resize(mesh.nodes.size());

    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        const auto& n = mesh.nodes[i];
        s_proj[i] = mapper.project(n.x, n.y, n.z);
        float dx = n.x - n.restX;
        float dy = n.y - n.restY;
        float dz = n.z - n.restZ;
        s_disp[i] = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // 2. Translucent diamond mesh weave diagonals shaded by height Z and dynamic ripple strain
    for (const auto& q : mesh.quads) {
        if (q.layer != layer)
            continue;

        const auto& n00 = mesh.nodes[q.n00];
        const auto& n11 = mesh.nodes[q.n11];
        float zFactor = std::clamp(0.5f * (n00.z + n11.z) / kFrontHeightZ, 0.0f, 1.0f);
        float strain = std::clamp(0.25f * (s_disp[q.n00] + s_disp[q.n10] + s_disp[q.n11] + s_disp[q.n01]) / 3.5f, 0.0f, 1.0f);
        uint8_t lum = static_cast<uint8_t>(std::clamp(185.0f + 45.0f * zFactor + 25.0f * strain, 140.0f, 255.0f));
        uint8_t alpha = static_cast<uint8_t>(std::clamp(80.0f + 45.0f * zFactor + 60.0f * strain, 60.0f, 185.0f));

        SDL_SetRenderDrawColor(renderer, lum, lum, static_cast<uint8_t>(std::min(255, lum + 5)), alpha);
        SDL_RenderDrawLineF(renderer, s_proj[q.n00].x, s_proj[q.n00].y, s_proj[q.n11].x, s_proj[q.n11].y);
        SDL_RenderDrawLineF(renderer, s_proj[q.n10].x, s_proj[q.n10].y, s_proj[q.n01].x, s_proj[q.n01].y);
    }

    // 3. Draw horizontal net mesh strands only (vertical strands have sp.draw == false and point knots are hidden)
    for (const auto& sp : mesh.springs) {
        if (!sp.draw)
            continue;
        const auto& na = mesh.nodes[sp.a];
        const auto& nb = mesh.nodes[sp.b];
        uint8_t edgeLayer = std::max(na.layer, nb.layer);
        if (edgeLayer != layer)
            continue;

        float avgZ = 0.5f * (na.z + nb.z) / kFrontHeightZ;
        float strain = std::clamp(0.5f * (s_disp[sp.a] + s_disp[sp.b]) / 4.0f, 0.0f, 1.0f);
        uint8_t lum = static_cast<uint8_t>(std::clamp(190.0f + 45.0f * avgZ + 20.0f * strain, 145.0f, 255.0f));
        uint8_t alpha = static_cast<uint8_t>(std::clamp(115.0f + 50.0f * avgZ + 55.0f * strain, 90.0f, 220.0f));

        SDL_SetRenderDrawColor(renderer, lum, lum, lum, alpha);
        SDL_RenderDrawLineF(renderer, s_proj[sp.a].x, s_proj[sp.a].y, s_proj[sp.b].x, s_proj[sp.b].y);
    }

    // 4. On the top overlay layer (layer == 1), draw the rigid/vibrating 3D goalposts, crossbar, and rear stanchions
    if (layer == 1) {
        const float frontY = mesh.isTopGoal ? kTopGoalFrontY : kBottomGoalFrontY;
        const float backY = mesh.isTopGoal ? kTopGoalBackY : kBottomGoalBackY;
        const float backHeightZ = mesh.isTopGoal ? kTopBackHeightZ : kBottomBackHeightZ;
        const float botBackY = mesh.isTopGoal ? (backY + 1.5f) : (backY + 2.0f);

        float vibDX = 0.0f;
        float vibDY = 0.0f;
        float vibDZ = 0.0f;
        bool vibrating = (m_ballAndGoalframeEffectsEnabled && mesh.frameVibTimer > 0.0f);
        if (vibrating) {
            float elapsed = mesh.frameVibMaxTimer - mesh.frameVibTimer;
            float decay = mesh.frameVibTimer / mesh.frameVibMaxTimer;
            float osc = decay * decay * std::cos(elapsed * 1.95f);
            vibDX = mesh.frameVibAmpX * osc;
            vibDY = mesh.frameVibAmpY * osc;
            vibDZ = mesh.frameVibAmpZ * osc;
        }

        float lTopW = (mesh.frameVibPart < 0) ? 0.85f : ((mesh.frameVibPart == 0) ? 0.40f : 0.0f);
        float rTopW = (mesh.frameVibPart > 0) ? 0.85f : ((mesh.frameVibPart == 0) ? 0.40f : 0.0f);

        // Dark rear stanchion support bars
        SDL_FPoint stL0 = mapper.project(kGoalLeftX + vibDX * lTopW, frontY + vibDY * lTopW, kFrontHeightZ + vibDZ * lTopW);
        SDL_FPoint stL1 = mapper.project(kGoalLeftX, backY, backHeightZ);
        SDL_FPoint stL2 = mapper.project(kGoalLeftX, botBackY, 0.0f);
        SDL_FPoint stR0 = mapper.project(kGoalRightX + vibDX * rTopW, frontY + vibDY * rTopW, kFrontHeightZ + vibDZ * rTopW);
        SDL_FPoint stR1 = mapper.project(kGoalRightX, backY, backHeightZ);
        SDL_FPoint stR2 = mapper.project(kGoalRightX, botBackY, 0.0f);

        float stanchionThick = std::max(0.75f, 0.36f * pixelScale);
        SDL_Color stanchionColor{ 140, 150, 156, 175 };
        drawThickSegment(renderer, stL0, stL1, stanchionThick, stanchionColor);
        drawThickSegment(renderer, stL1, stL2, stanchionThick, stanchionColor);
        drawThickSegment(renderer, stR0, stR1, stanchionThick, stanchionColor);
        drawThickSegment(renderer, stR1, stR2, stanchionThick, stanchionColor);

        // Slender cylindrical 3D-shaded front goalposts and crossbar (with dynamic flex vibration when struck)
        SDL_FPoint postLB = mapper.project(kGoalLeftX, frontY, 0.0f);
        SDL_FPoint postLT = stL0;
        SDL_FPoint postRB = mapper.project(kGoalRightX, frontY, 0.0f);
        SDL_FPoint postRT = stR0;

        float postHalfW = std::max(1.1f, 0.56f * pixelScale);

        // Left vertical cylindrical goalpost (segmented flex when vibrating)
        if (vibrating && mesh.frameVibPart < 0) {
            constexpr int kPostSegs = 4;
            for (int s = 0; s < kPostSegs; ++s) {
                float t0 = static_cast<float>(s) / kPostSegs;
                float t1 = static_cast<float>(s + 1) / kPostSegs;
                float bow0 = std::sin(t0 * 3.14159265f) * 1.15f + t0 * lTopW;
                float bow1 = std::sin(t1 * 3.14159265f) * 1.15f + t1 * lTopW;
                SDL_FPoint p0 = mapper.project(kGoalLeftX + vibDX * bow0, frontY + vibDY * bow0, kFrontHeightZ * t0);
                SDL_FPoint p1 = mapper.project(kGoalLeftX + vibDX * bow1, frontY + vibDY * bow1, kFrontHeightZ * t1);
                float midX = 0.5f * (p0.x + p1.x);
                drawCylindricalBar(renderer, midX - postHalfW, std::min(p0.y, p1.y) - 0.5f, midX + postHalfW, std::max(p0.y, p1.y) + 0.5f, false, -1);
            }
        } else {
            drawCylindricalBar(renderer,
                postLB.x - postHalfW, postLT.y,
                postLB.x + postHalfW, postLB.y,
                false, -1);
        }

        // Right vertical cylindrical goalpost (segmented flex when vibrating)
        if (vibrating && mesh.frameVibPart > 0) {
            constexpr int kPostSegs = 4;
            for (int s = 0; s < kPostSegs; ++s) {
                float t0 = static_cast<float>(s) / kPostSegs;
                float t1 = static_cast<float>(s + 1) / kPostSegs;
                float bow0 = std::sin(t0 * 3.14159265f) * 1.15f + t0 * rTopW;
                float bow1 = std::sin(t1 * 3.14159265f) * 1.15f + t1 * rTopW;
                SDL_FPoint p0 = mapper.project(kGoalRightX + vibDX * bow0, frontY + vibDY * bow0, kFrontHeightZ * t0);
                SDL_FPoint p1 = mapper.project(kGoalRightX + vibDX * bow1, frontY + vibDY * bow1, kFrontHeightZ * t1);
                float midX = 0.5f * (p0.x + p1.x);
                drawCylindricalBar(renderer, midX - postHalfW, std::min(p0.y, p1.y) - 0.5f, midX + postHalfW, std::max(p0.y, p1.y) + 0.5f, false, +1);
            }
        } else {
            drawCylindricalBar(renderer,
                postRB.x - postHalfW, postRT.y,
                postRB.x + postHalfW, postRB.y,
                false, +1);
        }

        // Horizontal cylindrical crossbar (segmented flex bow when vibrating)
        if (vibrating && mesh.frameVibPart == 0) {
            constexpr int kBarSegs = 6;
            for (int s = 0; s < kBarSegs; ++s) {
                float u0 = static_cast<float>(s) / kBarSegs;
                float u1 = static_cast<float>(s + 1) / kBarSegs;
                float bow0 = 0.40f + 0.95f * std::sin(u0 * 3.14159265f);
                float bow1 = 0.40f + 0.95f * std::sin(u1 * 3.14159265f);
                SDL_FPoint p0 = mapper.project(kGoalLeftX + u0 * kGoalWidth + vibDX * bow0, frontY + vibDY * bow0, kFrontHeightZ + vibDZ * bow0);
                SDL_FPoint p1 = mapper.project(kGoalLeftX + u1 * kGoalWidth + vibDX * bow1, frontY + vibDY * bow1, kFrontHeightZ + vibDZ * bow1);
                float midY = 0.5f * (p0.y + p1.y);
                drawCylindricalBar(renderer, std::min(p0.x, p1.x) - 0.5f, midY - postHalfW, std::max(p0.x, p1.x) + 0.5f, midY + postHalfW, true, 0);
            }
        } else {
            drawCylindricalBar(renderer,
                postLT.x - postHalfW, postLT.y - postHalfW,
                postRT.x + postHalfW, postRT.y + postHalfW,
                true, 0);
        }

        // Seamless mitered white corner joints where vertical posts meet the horizontal crossbar
        SDL_SetRenderDrawColor(renderer, 245, 248, 250, 255);
        SDL_FRect leftCornerJoin{
            postLT.x - postHalfW * 0.65f,
            postLT.y - postHalfW * 0.45f,
            postHalfW * 1.45f,
            postHalfW * 1.45f
        };
        SDL_FRect rightCornerJoin{
            postRT.x - postHalfW * 0.80f,
            postRT.y - postHalfW * 0.45f,
            postHalfW * 1.45f,
            postHalfW * 1.45f
        };
        SDL_RenderFillRectF(renderer, &leftCornerJoin);
        SDL_RenderFillRectF(renderer, &rightCornerJoin);

        // Specular impact ring at the exact point the ball struck the goalpost or crossbar
        if (m_ballAndGoalframeEffectsEnabled && mesh.impactFlashTimer > 0.0f) {
            float t = 1.0f - (mesh.impactFlashTimer / 10.0f);
            SDL_FPoint hitPt = mapper.project(mesh.impactX, mesh.impactY, mesh.impactZ);
            float radius = (2.2f + t * 7.5f) * pixelScale;
            uint8_t ringAlpha = static_cast<uint8_t>(std::clamp((1.0f - t) * 235.0f, 0.0f, 235.0f));
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, ringAlpha);
            constexpr int kRingSegs = 16;
            for (int i = 0; i < kRingSegs; ++i) {
                float a0 = (i * 2.0f * 3.14159265f) / kRingSegs;
                float a1 = ((i + 1) * 2.0f * 3.14159265f) / kRingSegs;
                SDL_RenderDrawLineF(renderer,
                    hitPt.x + std::cos(a0) * radius, hitPt.y + std::sin(a0) * radius * 0.8f,
                    hitPt.x + std::cos(a1) * radius, hitPt.y + std::sin(a1) * radius * 0.8f);
            }
            if (t < 0.45f) {
                float coreR = (1.6f - t * 2.0f) * pixelScale;
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, static_cast<uint8_t>((1.0f - t * 2.0f) * 220.0f));
                SDL_FRect coreRect{ hitPt.x - coreR, hitPt.y - coreR, coreR * 2.0f, coreR * 2.0f };
                SDL_RenderFillRectF(renderer, &coreRect);
            }
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
#endif
}

} // namespace

void initGoalNets()
{
    buildGoalNetMesh(m_goalNets[0], true);
    buildGoalNetMesh(m_goalNets[1], false);
    m_initialized = true;
}

void resetGoalNets()
{
    if (!m_initialized) {
        initGoalNets();
        return;
    }
    for (auto& mesh : m_goalNets) {
        mesh.prevBallValid = false;
        mesh.prevBallVX = 0.0f;
        mesh.prevBallVY = 0.0f;
        mesh.prevBallVZ = 0.0f;
        mesh.prevReplayPlayers.clear();
        mesh.frameVibTimer = 0.0f;
        mesh.impactFlashTimer = 0.0f;
        for (auto& node : mesh.nodes) {
            node.x = node.prevX = node.restX;
            node.y = node.prevY = node.restY;
            node.z = node.prevZ = node.restZ;
        }
    }
    m_ballTrail.clear();
    m_particles.clear();
    m_prevFeedbackValid = false;
}

void updateGoalNets()
{
    if (!m_initialized)
        initGoalNets();

    if (m_ballAndGoalframeEffectsEnabled) {
        const auto& ball = swos.ballSprite;
        float bx = ball.x.asFloat();
        float by = ball.y.asFloat();
        float bz = std::max(0.0f, ball.z.asFloat());
        float bvx = ball.deltaX.asFloat();
        float bvy = ball.deltaY.asFloat();
        float bvz = ball.deltaZ.asFloat();
        float activeSpin = 0.0f;
        if (swos.topTeamData.leftSpin || swos.bottomTeamData.leftSpin)
            activeSpin = -0.85f;
        else if (swos.topTeamData.rightSpin || swos.bottomTeamData.rightSpin)
            activeSpin = 0.85f;
        stepBallFeedback(bx, by, bz, bvx, bvy, bvz, activeSpin);
    }

    stepGoalFrameVibration(m_goalNets[0]);
    stepGoalFrameVibration(m_goalNets[1]);

    if (!m_physicsGoalNetsEnabled)
        return;

    stepGoalNetPhysics(m_goalNets[0]);
    integrateAndSolveGoalNet(m_goalNets[0]);
    stepGoalNetPhysics(m_goalNets[1]);
    integrateAndSolveGoalNet(m_goalNets[1]);
}

void updateGoalNetsFromReplay(bool hasBall, float ballX, float ballY, float ballZ,
    const ReplayNetActor *players, int numPlayers)
{
    if (!m_initialized)
        initGoalNets();

    if (hasBall && m_ballAndGoalframeEffectsEnabled) {
        float bvx = m_prevFeedbackValid ? (ballX - m_prevFeedbackX) : 0.0f;
        float bvy = m_prevFeedbackValid ? (ballY - m_prevFeedbackY) : 0.0f;
        float bvz = m_prevFeedbackValid ? (ballZ - m_prevFeedbackZ) : 0.0f;
        if (std::abs(bvx) > 45.0f || std::abs(bvy) > 45.0f)
            bvx = bvy = bvz = 0.0f;
        stepBallFeedback(ballX, ballY, ballZ, bvx, bvy, bvz, 0.0f);
    }

    if (!m_physicsGoalNetsEnabled && !m_ballAndGoalframeEffectsEnabled)
        return;

    stepGoalNetReplayPhysics(m_goalNets[0], hasBall, ballX, ballY, ballZ, players, numPlayers);
    stepGoalNetReplayPhysics(m_goalNets[1], hasBall, ballX, ballY, ballZ, players, numPlayers);
}

void notifyGoalNetBallImpact(int goalIndex, float hitX, float hitY, float hitZ,
    float velX, float velY, float velZ)
{
    if (!m_initialized)
        initGoalNets();

    if (goalIndex < 0 || goalIndex > 1)
        return;

    auto& mesh = m_goalNets[goalIndex];
    constexpr float kImpactRadius = 19.0f;
    for (auto& node : mesh.nodes) {
        interactNodeWithSphere(node, hitX, hitY, hitZ, velX, velY, velZ, kImpactRadius, 1.35f);
    }
}

void notifyGoalFrameImpact(int goalIndex, float hitX, float hitY, float hitZ,
    float velX, float velY, float velZ, bool hitCrossbar)
{
    if (!m_initialized)
        initGoalNets();

    if (goalIndex < 0 || goalIndex > 1 || !m_ballAndGoalframeEffectsEnabled)
        return;

    auto& mesh = m_goalNets[goalIndex];
    int part = hitCrossbar ? 0 : ((hitX < 334.0f) ? -1 : 1);
    float speed = std::sqrt(velX * velX + velY * velY + velZ * velZ);
    float mag = std::clamp(0.85f + speed * 0.34f, 0.95f, 2.45f);

    float dirX = (speed > 0.05f) ? (velX / speed) : 0.0f;
    float dirY = (speed > 0.05f) ? (velY / speed) : (mesh.isTopGoal ? -1.0f : 1.0f);
    float dirZ = (speed > 0.05f) ? (velZ / speed) : (hitCrossbar ? 0.65f : 0.0f);
    if (part != 0 && std::abs(dirX) < 0.35f)
        dirX = (part < 0) ? -0.65f : 0.65f;

    mesh.frameVibMaxTimer = 24.0f;
    mesh.frameVibTimer = 24.0f;
    mesh.frameVibPart = part;
    mesh.frameVibAmpX = dirX * mag;
    mesh.frameVibAmpY = dirY * mag * 0.75f;
    mesh.frameVibAmpZ = (hitCrossbar ? std::max(0.45f, std::abs(dirZ)) : dirZ * 0.4f) * mag;

    const float frontY = mesh.isTopGoal ? kTopGoalFrontY : kBottomGoalFrontY;
    mesh.impactX = (part < 0) ? kGoalLeftX : ((part > 0) ? kGoalRightX : std::clamp(hitX, kGoalLeftX, kGoalRightX));
    mesh.impactY = frontY;
    mesh.impactZ = hitCrossbar ? kFrontHeightZ : std::clamp(hitZ, 1.0f, kFrontHeightZ);
    mesh.impactFlashTimer = 10.0f;

    spawnWoodworkImpactParticles(mesh.impactX, mesh.impactY, mesh.impactZ, -dirX, -dirY);

    // Transmit an immediate shockwave ripple from the struck goalpost/crossbar into the attached 3D net
    if (m_physicsGoalNetsEnabled) {
        float netPushY = (mesh.isTopGoal ? -1.0f : 1.0f) * std::max(1.8f, std::abs(velY) * 0.85f);
        float netPushX = (part < 0) ? -std::max(1.2f, std::abs(velX)) : ((part > 0) ? std::max(1.2f, std::abs(velX)) : velX * 0.7f);
        float netPushZ = hitCrossbar ? std::max(1.0f, std::abs(velZ)) : velZ * 0.5f;
        constexpr float kFrameShockRadius = 26.0f;
        for (auto& node : mesh.nodes) {
            interactNodeWithSphere(node, mesh.impactX, mesh.impactY, mesh.impactZ,
                netPushX, netPushY, netPushZ, kFrameShockRadius, 1.05f);
        }
    }
}

void setGoalNetRenderCamera(FixedPoint cameraX, FixedPoint cameraY)
{
    m_hasCustomRenderCamera = true;
    m_customCameraX = cameraX.asFloat();
    m_customCameraY = cameraY.asFloat();
}

void clearGoalNetRenderCamera()
{
    m_hasCustomRenderCamera = false;
}

void drawTopGoalBackNet(float xOffset, float yOffset)
{
    if (!m_physicsGoalNetsEnabled)
        return;
    if (!m_initialized)
        initGoalNets();
    drawGoalNetLayer(m_goalNets[0], 0, xOffset, yOffset);
}

void drawTopGoalOverlayNet(float xOffset, float yOffset)
{
    if (!m_physicsGoalNetsEnabled)
        return;
    if (!m_initialized)
        initGoalNets();
    drawGoalNetLayer(m_goalNets[0], 1, xOffset, yOffset);
}

void drawBottomGoalBackNet(float xOffset, float yOffset)
{
    if (!m_physicsGoalNetsEnabled)
        return;
    if (!m_initialized)
        initGoalNets();
    drawGoalNetLayer(m_goalNets[1], 0, xOffset, yOffset);
}

void drawBottomGoalOverlayNet(float xOffset, float yOffset)
{
    if (!m_physicsGoalNetsEnabled)
        return;
    if (!m_initialized)
        initGoalNets();
    drawGoalNetLayer(m_goalNets[1], 1, xOffset, yOffset);
}

void drawBallGroundEffects(float xOffset, float yOffset)
{
#ifdef SWOS_TEST
    (void)xOffset;
    (void)yOffset;
    return;
#else
    if (!m_ballAndGoalframeEffectsEnabled || m_ballTrail.size() < 2)
        return;

    auto renderer = getRenderer();
    if (!renderer)
        return;

    int pitchType = getPitchType();
    // Draw wet/muddy/frozen grass skid wake when the ball rolls/skids fast near the turf
    if (pitchType > 2)
        return;

    ScreenMapper mapper = makeScreenMapper(xOffset, yOffset);
    float pixelScale = std::max(1.0f, mapper.scale * mapper.zoom);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (size_t i = 1; i < m_ballTrail.size(); ++i) {
        const auto& p0 = m_ballTrail[i - 1];
        const auto& p1 = m_ballTrail[i];
        if (p0.z > 1.35f || p1.z > 1.35f || p1.speed < 1.85f)
            continue;
        if (std::abs(p1.x - p0.x) > 25.0f || std::abs(p1.y - p0.y) > 25.0f)
            continue;

        float frac = static_cast<float>(i) / static_cast<float>(m_ballTrail.size());
        float alphaScale = frac * p1.life * std::clamp((p1.speed - 1.7f) / 2.8f, 0.15f, 1.0f);
        SDL_FPoint s0 = mapper.project(p0.x, p0.y, 0.0f);
        SDL_FPoint s1 = mapper.project(p1.x, p1.y, 0.0f);

        SDL_Color wakeColor;
        if (pitchType == 2) { // Wet pitch dark water wake
            wakeColor = { 14, 46, 12, static_cast<uint8_t>(55.0f * alphaScale) };
        } else if (pitchType == 1) { // Muddy pitch rut wake
            wakeColor = { 56, 38, 18, static_cast<uint8_t>(68.0f * alphaScale) };
        } else { // Frozen pitch frost wake
            wakeColor = { 215, 236, 252, static_cast<uint8_t>(58.0f * alphaScale) };
        }
        float halfW = std::max(0.75f, (0.45f + 0.55f * frac) * pixelScale);
        drawThickSegment(renderer, s0, s1, halfW, wakeColor);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
#endif
}

void drawBallAirEffects(float xOffset, float yOffset)
{
#ifdef SWOS_TEST
    (void)xOffset;
    (void)yOffset;
    return;
#else
    if (!m_ballAndGoalframeEffectsEnabled)
        return;

    auto renderer = getRenderer();
    if (!renderer)
        return;

    ScreenMapper mapper = makeScreenMapper(xOffset, yOffset);
    float pixelScale = std::max(1.0f, mapper.scale * mapper.zoom);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 1. High-velocity shot & aftertouch spin vapor ribbon following the ball's 3D trajectory
    if (m_ballTrail.size() >= 2) {
        for (size_t i = 1; i < m_ballTrail.size(); ++i) {
            const auto& p0 = m_ballTrail[i - 1];
            const auto& p1 = m_ballTrail[i];
            float spinMag = std::abs(p1.spinCurl);
            if (p1.speed < 2.25f && spinMag < 0.15f)
                continue;
            if (std::abs(p1.x - p0.x) > 25.0f || std::abs(p1.y - p0.y) > 25.0f)
                continue;

            float frac = static_cast<float>(i) / static_cast<float>(m_ballTrail.size());
            float speedFactor = std::clamp((p1.speed - 2.0f) / 3.2f, 0.0f, 1.0f);
            float intensity = std::clamp(speedFactor + spinMag * 0.55f, 0.0f, 1.0f) * frac * p1.life;
            if (intensity <= 0.03f)
                continue;

            SDL_FPoint s0 = mapper.project(p0.x, p0.y, p0.z + 1.8f);
            SDL_FPoint s1 = mapper.project(p1.x, p1.y, p1.z + 1.8f);

            float outerW = std::max(0.8f, (0.4f + 0.95f * frac) * pixelScale);
            float coreW = std::max(0.45f, (0.2f + 0.45f * frac) * pixelScale);

            uint8_t outerAlpha = static_cast<uint8_t>(std::clamp(intensity * 78.0f, 8.0f, 85.0f));
            uint8_t coreAlpha = static_cast<uint8_t>(std::clamp(intensity * 135.0f, 12.0f, 145.0f));

            drawThickSegment(renderer, s0, s1, outerW, SDL_Color{ 225, 240, 255, outerAlpha });
            drawThickSegment(renderer, s0, s1, coreW, SDL_Color{ 252, 254, 255, coreAlpha });

            // Curved aftertouch vortex edge filament when spin/swerve is active
            if (spinMag >= 0.15f) {
                float dx = s1.x - s0.x;
                float dy = s1.y - s0.y;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.5f) {
                    float nx = (-dy / len) * (p1.spinCurl >= 0.0f ? 1.0f : -1.0f);
                    float ny = (dx / len) * (p1.spinCurl >= 0.0f ? 1.0f : -1.0f);
                    float curlOffset = (0.85f + 0.75f * std::sin(frac * 4.5f)) * pixelScale;
                    uint8_t curlAlpha = static_cast<uint8_t>(std::clamp(intensity * spinMag * 125.0f, 15.0f, 130.0f));
                    SDL_SetRenderDrawColor(renderer, 210, 238, 255, curlAlpha);
                    SDL_RenderDrawLineF(renderer,
                        s0.x + nx * curlOffset, s0.y + ny * curlOffset,
                        s1.x + nx * curlOffset, s1.y + ny * curlOffset);
                }
            }
        }
    }

    // 2. Render 3D surface spray & woodwork impact micro-particles
    for (const auto& p : m_particles) {
        if (p.life <= 0.0f)
            continue;
        SDL_FPoint sp = mapper.project(p.x, p.y, p.z);
        float sz = std::max(1.0f, p.size * p.life * pixelScale * 0.75f);
        uint8_t alpha = static_cast<uint8_t>(std::clamp(p.a * p.life, 0.0f, 255.0f));
        SDL_SetRenderDrawColor(renderer, p.r, p.g, p.b, alpha);
        SDL_FRect r{ sp.x - sz * 0.5f, sp.y - sz * 0.5f, sz, sz };
        SDL_RenderFillRectF(renderer, &r);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
#endif
}

void drawGoalFrameImpactOverlay(int goalIndex, float xOffset, float yOffset)
{
#ifdef SWOS_TEST
    (void)goalIndex;
    (void)xOffset;
    (void)yOffset;
    return;
#else
    if (!m_ballAndGoalframeEffectsEnabled || goalIndex < 0 || goalIndex > 1)
        return;
    const auto& mesh = m_goalNets[goalIndex];
    if (mesh.impactFlashTimer <= 0.0f)
        return;

    auto renderer = getRenderer();
    if (!renderer)
        return;

    ScreenMapper mapper = makeScreenMapper(xOffset, yOffset);
    float pixelScale = std::max(1.0f, mapper.scale * mapper.zoom);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    float t = 1.0f - (mesh.impactFlashTimer / 10.0f);
    SDL_FPoint hitPt = mapper.project(mesh.impactX, mesh.impactY, mesh.impactZ);
    float radius = (2.2f + t * 7.5f) * pixelScale;
    uint8_t ringAlpha = static_cast<uint8_t>(std::clamp((1.0f - t) * 235.0f, 0.0f, 235.0f));
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, ringAlpha);
    constexpr int kRingSegs = 16;
    for (int i = 0; i < kRingSegs; ++i) {
        float a0 = (i * 2.0f * 3.14159265f) / kRingSegs;
        float a1 = ((i + 1) * 2.0f * 3.14159265f) / kRingSegs;
        SDL_RenderDrawLineF(renderer,
            hitPt.x + std::cos(a0) * radius, hitPt.y + std::sin(a0) * radius * 0.8f,
            hitPt.x + std::cos(a1) * radius, hitPt.y + std::sin(a1) * radius * 0.8f);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
#endif
}

bool getPhysicsGoalNets()
{
    return m_physicsGoalNetsEnabled;
}

void setPhysicsGoalNets(bool enabled)
{
    m_physicsGoalNetsEnabled = enabled;
}

bool getBallAndGoalframeEffects()
{
    return m_ballAndGoalframeEffectsEnabled;
}

void setBallAndGoalframeEffects(bool enabled)
{
    m_ballAndGoalframeEffectsEnabled = enabled;
    if (!enabled) {
        m_ballTrail.clear();
        m_particles.clear();
        for (auto& mesh : m_goalNets) {
            mesh.frameVibTimer = 0.0f;
            mesh.impactFlashTimer = 0.0f;
        }
    }
}

GoalNetTestState getGoalNetTestState(int goalIndex)
{
    if (!m_initialized)
        initGoalNets();

    GoalNetTestState st{};
    if (goalIndex < 0 || goalIndex > 1)
        return st;

    const auto& mesh = m_goalNets[goalIndex];
    st.totalNodes = static_cast<int>(mesh.nodes.size());
    st.frameVibrationTimer = mesh.frameVibTimer;
    for (const auto& n : mesh.nodes) {
        if (n.invMass <= 0.0f) {
            ++st.pinnedNodes;
            continue;
        }
        float dx = n.x - n.restX;
        float dy = n.y - n.restY;
        float dz = n.z - n.restZ;
        float disp = std::sqrt(dx * dx + dy * dy + dz * dz);
        st.maxDisplacement = std::max(st.maxDisplacement, disp);

        float vx = n.x - n.prevX;
        float vy = n.y - n.prevY;
        float vz = n.z - n.prevZ;
        st.totalKineticEnergy += 0.5f * (vx * vx + vy * vy + vz * vz);
    }
    return st;
}
