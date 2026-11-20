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

constexpr int kCols = 23; // X divisions along goal width (299..369 -> ~3.18 px spacing)
constexpr int kRows = 12; // V divisions: 0..5 roof (front-to-back), 5..11 back wall (top-to-ground)
constexpr int kRoofBackRow = 5;
constexpr int kSideCols = 6; // U divisions for left/right side curtains (front post to rear stanchion)
constexpr int kSideRows = 7; // V divisions for left/right side curtains (top edge to ground)

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
    bool prevBallValid = false;
};

static bool m_physicsGoalNetsEnabled = true;
static GoalNetMesh m_goalNets[2];
static bool m_initialized = false;
static bool m_hasCustomRenderCamera = false;
static float m_customCameraX = 0.0f;
static float m_customCameraY = 0.0f;

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
                addSpring(mesh, cur, mainNodeIndex(mesh, r + 1, c), 0.90f, true);
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
                    addSpring(mesh, cur, sideIdx(r + 1, c), 0.90f, true);
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
    const float frontY = mesh.isTopGoal ? kTopGoalFrontY : kBottomGoalFrontY;
    const float backY = mesh.isTopGoal ? kTopGoalBackY : kBottomGoalBackY;

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

static void drawSolidBarRect(SDL_Renderer *renderer, float x0, float y0, float x1, float y1,
    SDL_Color fillColor, SDL_Color borderColor, float borderPx)
{
    float left = std::floor(std::min(x0, x1));
    float top = std::floor(std::min(y0, y1));
    float right = std::ceil(std::max(x0, x1));
    float bottom = std::ceil(std::max(y0, y1));
    float b = std::max(1.0f, std::round(borderPx));

    SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    SDL_FRect outerRect{ left - b, top - b, (right - left) + 2.0f * b, (bottom - top) + 2.0f * b };
    SDL_RenderFillRectF(renderer, &outerRect);

    SDL_SetRenderDrawColor(renderer, fillColor.r, fillColor.g, fillColor.b, fillColor.a);
    SDL_FRect innerRect{ left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top) };
    SDL_RenderFillRectF(renderer, &innerRect);
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
    // (+0.65 * Z in X, +0.38 * Z in Y), so the shadow on the real pitch tiles ripples with the net!
    constexpr float kSunDirX = 0.65f;
    constexpr float kSunDirY = 0.38f;

    static std::vector<SDL_FPoint> s_shadowProj;
    s_shadowProj.resize(mesh.nodes.size());
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        const auto& n = mesh.nodes[i];
        s_shadowProj[i] = mapper.project(n.x + n.z * kSunDirX, n.y + n.z * kSunDirY, 0.0f);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 1. Soft shadow mesh strands on the grass
    SDL_SetRenderDrawColor(renderer, 10, 25, 5, 58);
    for (const auto& sp : mesh.springs) {
        if (!sp.draw)
            continue;
        SDL_RenderDrawLineF(renderer,
            s_shadowProj[sp.a].x, s_shadowProj[sp.a].y,
            s_shadowProj[sp.b].x, s_shadowProj[sp.b].y);
    }

    // 2. Shadow knots on the grass (mimics the classic pixelated SWOS goal shadow pattern, now dynamic!)
    float shadowDot = std::clamp(0.65f * pixelScale, 1.2f, 3.8f);
    SDL_SetRenderDrawColor(renderer, 8, 22, 4, 72);
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (mesh.nodes[i].z < 0.5f)
            continue;
        SDL_FRect dotRect{
            s_shadowProj[i].x - shadowDot * 0.5f,
            s_shadowProj[i].y - shadowDot * 0.5f,
            shadowDot,
            shadowDot
        };
        SDL_RenderFillRectF(renderer, &dotRect);
    }

    // 3. Goalpost & crossbar ground shadow
    const float frontY = mesh.isTopGoal ? kTopGoalFrontY : kBottomGoalFrontY;
    SDL_FPoint postLB = mapper.project(kGoalLeftX, frontY, 0.0f);
    SDL_FPoint postLTShadow = mapper.project(kGoalLeftX + kFrontHeightZ * kSunDirX, frontY + kFrontHeightZ * kSunDirY, 0.0f);
    SDL_FPoint postRB = mapper.project(kGoalRightX, frontY, 0.0f);
    SDL_FPoint postRTShadow = mapper.project(kGoalRightX + kFrontHeightZ * kSunDirX, frontY + kFrontHeightZ * kSunDirY, 0.0f);
    float postShadowThick = std::max(1.0f, 0.85f * pixelScale);
    SDL_Color postShadowColor{ 8, 22, 4, 85 };
    drawThickSegment(renderer, postLB, postLTShadow, postShadowThick, postShadowColor);
    drawThickSegment(renderer, postRB, postRTShadow, postShadowThick, postShadowColor);
    drawThickSegment(renderer, postLTShadow, postRTShadow, postShadowThick, postShadowColor);
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

    // 2. Translucent mesh weave diagonals shaded by height Z and dynamic ripple strain
    for (const auto& q : mesh.quads) {
        if (q.layer != layer)
            continue;

        const auto& n00 = mesh.nodes[q.n00];
        const auto& n11 = mesh.nodes[q.n11];
        float zFactor = std::clamp(0.5f * (n00.z + n11.z) / kFrontHeightZ, 0.0f, 1.0f);
        float strain = std::clamp(0.25f * (s_disp[q.n00] + s_disp[q.n10] + s_disp[q.n11] + s_disp[q.n01]) / 3.5f, 0.0f, 1.0f);
        uint8_t lum = static_cast<uint8_t>(std::clamp(175.0f + 50.0f * zFactor + 30.0f * strain, 130.0f, 255.0f));
        uint8_t alpha = static_cast<uint8_t>(std::clamp(45.0f + 35.0f * zFactor + 55.0f * strain, 35.0f, 145.0f));

        SDL_SetRenderDrawColor(renderer, lum, lum, static_cast<uint8_t>(std::min(255, lum + 5)), alpha);
        SDL_RenderDrawLineF(renderer, s_proj[q.n00].x, s_proj[q.n00].y, s_proj[q.n11].x, s_proj[q.n11].y);
        SDL_RenderDrawLineF(renderer, s_proj[q.n10].x, s_proj[q.n10].y, s_proj[q.n01].x, s_proj[q.n01].y);
    }

    // 3. Draw net mesh strands & diagonal weave lines
    for (const auto& sp : mesh.springs) {
        const auto& na = mesh.nodes[sp.a];
        const auto& nb = mesh.nodes[sp.b];
        uint8_t edgeLayer = std::max(na.layer, nb.layer);
        if (edgeLayer != layer)
            continue;

        float avgZ = 0.5f * (na.z + nb.z) / kFrontHeightZ;
        float strain = std::clamp(0.5f * (s_disp[sp.a] + s_disp[sp.b]) / 4.0f, 0.0f, 1.0f);
        uint8_t lum = static_cast<uint8_t>(std::clamp(185.0f + 45.0f * avgZ + 25.0f * strain, 140.0f, 255.0f));
        uint8_t alpha = sp.draw
            ? static_cast<uint8_t>(std::clamp(135.0f + 55.0f * avgZ + 55.0f * strain, 110.0f, 245.0f))
            : static_cast<uint8_t>(std::clamp(70.0f + 35.0f * avgZ + 45.0f * strain, 55.0f, 165.0f));

        SDL_SetRenderDrawColor(renderer, lum, lum, lum, alpha);
        SDL_RenderDrawLineF(renderer, s_proj[sp.a].x, s_proj[sp.a].y, s_proj[sp.b].x, s_proj[sp.b].y);
    }

    // 4. Draw point-cloud knots at each mesh intersection
    float knotSize = std::clamp(0.55f * pixelScale, 1.2f, 3.5f);
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        const auto& n = mesh.nodes[i];
        if (n.layer != layer)
            continue;

        float zFactor = std::clamp(n.z / kFrontHeightZ, 0.0f, 1.0f);
        float strain = std::clamp(s_disp[i] / 3.5f, 0.0f, 1.0f);
        uint8_t lum = static_cast<uint8_t>(std::clamp(210.0f + 30.0f * zFactor + 15.0f * strain, 180.0f, 255.0f));
        uint8_t alpha = static_cast<uint8_t>(std::clamp(185.0f + 40.0f * zFactor + 30.0f * strain, 160.0f, 255.0f));
        SDL_SetRenderDrawColor(renderer, lum, lum, lum, alpha);
        SDL_FRect knotRect{
            s_proj[i].x - knotSize * 0.5f,
            s_proj[i].y - knotSize * 0.5f,
            knotSize,
            knotSize
        };
        SDL_RenderFillRectF(renderer, &knotRect);
    }

    // 5. On the top overlay layer (layer == 1), draw the rigid 3D goalposts, crossbar, and rear stanchions
    if (layer == 1) {
        const float frontY = mesh.isTopGoal ? kTopGoalFrontY : kBottomGoalFrontY;
        const float backY = mesh.isTopGoal ? kTopGoalBackY : kBottomGoalBackY;
        const float backHeightZ = mesh.isTopGoal ? kTopBackHeightZ : kBottomBackHeightZ;
        const float botBackY = mesh.isTopGoal ? (backY + 1.5f) : (backY + 2.0f);

        // Dark rear stanchion support bars
        SDL_FPoint stL0 = mapper.project(kGoalLeftX, frontY, kFrontHeightZ);
        SDL_FPoint stL1 = mapper.project(kGoalLeftX, backY, backHeightZ);
        SDL_FPoint stL2 = mapper.project(kGoalLeftX, botBackY, 0.0f);
        SDL_FPoint stR0 = mapper.project(kGoalRightX, frontY, kFrontHeightZ);
        SDL_FPoint stR1 = mapper.project(kGoalRightX, backY, backHeightZ);
        SDL_FPoint stR2 = mapper.project(kGoalRightX, botBackY, 0.0f);

        float stanchionThick = std::max(1.0f, 0.55f * pixelScale);
        SDL_Color stanchionColor{ 130, 140, 145, 220 };
        drawThickSegment(renderer, stL0, stL1, stanchionThick, stanchionColor);
        drawThickSegment(renderer, stL1, stL2, stanchionThick, stanchionColor);
        drawThickSegment(renderer, stR0, stR1, stanchionThick, stanchionColor);
        drawThickSegment(renderer, stR1, stR2, stanchionThick, stanchionColor);

        // Rigid white front goalposts and crossbar drawn as solid pixel-aligned rectangles (never fuzzy or striped!)
        SDL_FPoint postLB = mapper.project(kGoalLeftX, frontY, 0.0f);
        SDL_FPoint postLT = mapper.project(kGoalLeftX, frontY, kFrontHeightZ);
        SDL_FPoint postRB = mapper.project(kGoalRightX, frontY, 0.0f);
        SDL_FPoint postRT = mapper.project(kGoalRightX, frontY, kFrontHeightZ);

        float postHalfW = std::max(1.0f, 0.90f * pixelScale);
        float borderPx = std::max(1.0f, 0.38f * pixelScale);
        SDL_Color outlineColor{ 22, 22, 22, 245 };
        SDL_Color postWhite{ 246, 248, 246, 255 };
        SDL_Color postShade{ 195, 200, 205, 255 };

        // Left & right vertical goalposts (outer black border + solid white post + inner shaded edge)
        drawSolidBarRect(renderer,
            postLB.x - postHalfW, postLT.y - postHalfW,
            postLB.x + postHalfW, postLB.y,
            postWhite, outlineColor, borderPx);
        drawSolidBarRect(renderer,
            postRB.x - postHalfW, postRT.y - postHalfW,
            postRB.x + postHalfW, postRB.y,
            postWhite, outlineColor, borderPx);

        // Horizontal crossbar spanning across both posts
        drawSolidBarRect(renderer,
            postLT.x - postHalfW, postLT.y - postHalfW,
            postRT.x + postHalfW, postRT.y + postHalfW,
            postWhite, outlineColor, borderPx);

        // Subtle inner shadow along the underside of the crossbar and inside edges of the posts (classic SWOS 3D cylinder look)
        float shadeH = std::max(1.0f, 0.45f * pixelScale);
        SDL_SetRenderDrawColor(renderer, postShade.r, postShade.g, postShade.b, postShade.a);
        SDL_FRect crossbarUnderShade{
            std::floor(postLT.x + postHalfW),
            std::ceil(postLT.y + postHalfW) - shadeH,
            std::max(1.0f, std::ceil(postRT.x - postHalfW) - std::floor(postLT.x + postHalfW)),
            shadeH
        };
        SDL_RenderFillRectF(renderer, &crossbarUnderShade);
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

void updateGoalNets()
{
    if (!m_initialized)
        initGoalNets();

    if (!m_physicsGoalNetsEnabled)
        return;

    stepGoalNetPhysics(m_goalNets[0]);
    stepGoalNetPhysics(m_goalNets[1]);
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

bool getPhysicsGoalNets()
{
    return m_physicsGoalNetsEnabled;
}

void setPhysicsGoalNets(bool enabled)
{
    m_physicsGoalNetsEnabled = enabled;
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
