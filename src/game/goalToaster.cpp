#include "goalToaster.h"
#include "compactFont.h"
#include "gameFieldMapping.h"
#include "llmClient.h"
#include "render.h"
#include "swos.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr Uint32 kSlideInMs = 380;
constexpr Uint32 kHoldMs = 5600;
constexpr Uint32 kSlideOutMs = 420;
constexpr Uint32 kTotalDurationMs = kSlideInMs + kHoldMs + kSlideOutMs;

struct GoalToasterState {
    bool active = false;
    Uint32 startTick = 0;
    GoalToasterEvent ev;
    std::string comment;
    uint64_t llmReqId = 0;
} s_toaster;

std::string toUpperAscii(const std::string& s)
{
    std::string out = s;
    for (char& c : out) {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

std::string cleanScorerQuote(const std::string& raw)
{
    std::string s = sanitizeCompactMixedCase(raw);
    // Trim surrounding quotes or speaker prefix if LLM included one
    while (!s.empty() && (s.front() == ' ' || s.front() == '"' || s.front() == '\''))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '"' || s.back() == '\''))
        s.pop_back();
    if (s.size() > 118) {
        size_t sp = s.rfind(' ', 115);
        s.resize(sp != std::string::npos ? sp : 115);
        s += "...";
    }
    return s;
}

SDL_Color kitAccentRgb(uint8_t idx)
{
    switch (idx & 0x0Fu) {
    case 0: return { 172, 178, 186, 255 };
    case 1: return { 238, 236, 228, 255 };
    case 2: return { 96, 104, 118, 255 };
    case 3: return { 236, 138, 42, 255 };
    case 4: return { 222, 54, 58, 255 };
    case 5: return { 62, 128, 236, 255 };
    case 6: return { 166, 108, 66, 255 };
    case 7: return { 104, 194, 242, 255 };
    case 8: return { 64, 188, 92, 255 };
    case 9: return { 244, 206, 56, 255 };
    default: return { 244, 206, 56, 255 };
    }
}

#ifndef SWOS_TEST
void fillRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{ x, y, w, h };
    SDL_RenderFillRectF(r, &rc);
}

void drawRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{ x, y, w, h };
    SDL_RenderDrawRectF(r, &rc);
}

void drawFilledCircle(SDL_Renderer *r, float cx, float cy, float radius, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    int ir = std::max(1, static_cast<int>(std::round(radius)));
    for (int dy = -ir; dy <= ir; ++dy) {
        float span = std::sqrt(std::max(0.0f, radius * radius - static_cast<float>(dy * dy)));
        SDL_FRect strip{ cx - span, cy + static_cast<float>(dy), span * 2.0f, 1.0f };
        SDL_RenderFillRectF(r, &strip);
    }
}

// Stylish animated 1996/97 classic panelled football icon inside the toaster
void drawAnimatedSoccerBall(SDL_Renderer *r, float cx, float cy, float radius, float spinRad, uint8_t alpha)
{
    auto withA = [&](Uint8 rr, Uint8 gg, Uint8 bb, float mult = 1.0f) -> SDL_Color {
        return { rr, gg, bb, static_cast<Uint8>(std::clamp(static_cast<int>(alpha * mult), 0, 255)) };
    };

    // Soft ground shadow
    drawFilledCircle(r, cx + radius * 0.08f, cy + radius * 0.14f, radius * 1.04f, withA(4, 6, 10, 0.55f));
    // Outer dark rim
    drawFilledCircle(r, cx, cy, radius, withA(22, 26, 32, 1.0f));
    // Main white leather sphere
    drawFilledCircle(r, cx, cy, radius * 0.90f, withA(242, 244, 246, 1.0f));
    // Subtle lower-right shading
    drawFilledCircle(r, cx + radius * 0.12f, cy + radius * 0.14f, radius * 0.72f, withA(214, 218, 224, 0.45f));
    // Top-left specular gleam
    drawFilledCircle(r, cx - radius * 0.28f, cy - radius * 0.28f, radius * 0.30f, withA(255, 255, 255, 0.85f));

    // Rotating classic black pentagon patches (1 center + 5 outer)
    float centerR = radius * 0.28f;
    float patchOffX = std::cos(spinRad) * radius * 0.10f;
    float patchOffY = std::sin(spinRad) * radius * 0.10f;
    drawFilledCircle(r, cx + patchOffX, cy + patchOffY, centerR, withA(24, 28, 34, 1.0f));

    for (int i = 0; i < 5; ++i) {
        float ang = spinRad + static_cast<float>(i) * (6.2831853f / 5.0f);
        float px = cx + std::cos(ang) * radius * 0.72f;
        float py = cy + std::sin(ang) * radius * 0.72f;
        drawFilledCircle(r, px, py, radius * 0.20f, withA(26, 30, 36, 0.95f));
    }
}
#endif

} // namespace

std::string buildOfflineScorerComment(const GoalToasterEvent& ev, uint32_t seed)
{
    if (ev.isOwnGoal) {
        static const char *const kOwnQuotes[] = {
            "Unfortunate deflection — nothing the keeper could do about that one!",
            "Total mix-up at the back, they all count on the scoresheet!",
            "Wrong end of the pitch, lad! Brutal bit of luck there."
        };
        return kOwnQuotes[seed % 3u];
    }

    if (ev.scorerMatchGoals >= 3) {
        static const char *const kHatQuotes[] = {
            "That's the match ball mine! Everything I hit today is flying in!",
            "Hat-trick hero! Get the champagne on ice, lads!",
            "Three in the bag! Pure 1997 magic out there today!"
        };
        return kHatQuotes[seed % 3u];
    }

    if (ev.isPenalty) {
        static const char *const kPenQuotes[] = {
            "Sent the keeper the wrong way — cool as ice from twelve yards!",
            "Picked my corner and buried it! Never in doubt from the spot!",
            "Pure composure from the penalty spot — top bin, no chance!"
        };
        return kPenQuotes[seed % 3u];
    }

    if (ev.isFreeKick) {
        static const char *const kFkQuotes[] = {
            "Bent it straight over the wall — been practicing that all week!",
            "Whipped it with pure swerve! Keeper was rooted to the spot!",
            "Dead-ball perfection! Straight into the postage stamp!"
        };
        return kFkQuotes[seed % 3u];
    }

    if (ev.isOutsideBox) {
        static const char *const kLongQuotes[] = {
            "Caught it sweet off the laces — keeper never even saw it!",
            "Had a pop from range and hit it like a thunderbolt!",
            "Pure pile-driver from outside the box! Put that on Match of the Day!"
        };
        return kLongQuotes[seed % 3u];
    }

    if (ev.isHeader) {
        static const char *const kHeadQuotes[] = {
            "Hung in the air and powered it home — textbook header!",
            "What a cross! Just had to nod it past the keeper!",
            "Rose highest in the box and buried the bullet header!"
        };
        return kHeadQuotes[seed % 3u];
    }

    if (ev.minute >= 80 && ev.isGoAheadGoal) {
        static const char *const kLateQuotes[] = {
            "What a time to score! Listen to that terrace roar!",
            "Late dagger! Never stop believing until the final whistle!",
            "Left it late, didn't we? Pure limbs in the away end!"
        };
        return kLateQuotes[seed % 3u];
    }

    if (!ev.assisterName.empty()) {
        return "Brilliant ball from " + ev.assisterName + " — just had to slot it home!";
    }

    static const char *const kGenQuotes[] = {
        "Saw the gap, kept my head, and tucked it right into the corner!",
        "Clinical finish! That's what we worked on in training all week!",
        "Right place, right time — get in there, lads!",
        "Keeper came out, I stayed calm, and rolled it straight past him!"
    };
    return kGenQuotes[seed % 4u];
}

void resetGoalToaster()
{
    s_toaster = GoalToasterState{};
}

void triggerGoalToaster(const GoalToasterEvent& ev)
{
    if (swos.g_trainingGame)
        return;

    s_toaster.active = true;
#ifdef SWOS_TEST
    s_toaster.startTick = 1000;
#else
    s_toaster.startTick = SDL_GetTicks();
#endif
    s_toaster.ev = ev;
    uint32_t seed = static_cast<uint32_t>(s_toaster.startTick ^ (ev.minute * 131u + ev.scorerMatchGoals * 17u));
    s_toaster.comment = buildOfflineScorerComment(ev, seed);
    s_toaster.llmReqId = 0;

#ifndef SWOS_TEST
    // Optional fast async LLM first-person quote from the scorer (updates seamlessly if it arrives before fade-out)
    const auto& cfg = getLlmConfig();
    if (cfg.enabled && !ev.isOwnGoal && !ev.scorerName.empty()) {
        std::string finishDesc = ev.isPenalty ? "penalty kick" :
                                 ev.isFreeKick ? "direct free kick" :
                                 ev.isOutsideBox ? "long-range strike from outside the box" :
                                 ev.isHeader ? "header" :
                                 ev.isVolley ? "volley" : "footed finish";
        std::string prompt = "Scorer: " + ev.scorerName + " (" + ev.teamName + ").\n" +
                             "Minute: " + std::to_string(ev.minute) + "'. Finish: " + finishDesc + ".\n" +
                             "Goals by this player today: " + std::to_string(ev.scorerMatchGoals) +
                             (ev.assisterName.empty() ? "" : (". Assisted by: " + ev.assisterName)) + ".\n" +
                             "Write ONE punchy, joyful 1997 first-person comment (max 95 chars) from " + ev.scorerName +
                             " right after scoring that exact goal! Plain text only, no quotes, no speaker name prefix.";
        s_toaster.llmReqId = enqueueLlmRequest(
            "You are a 1997 footballer reacting immediately after scoring a goal in one punchy first-person sentence (max 95 chars). Plain text only.",
            prompt, 120);
    }
#endif
}

bool isGoalToasterActive()
{
    return s_toaster.active;
}

void updateAndDrawGoalToaster(float cameraXOffset, float cameraYOffset)
{
#ifdef SWOS_TEST
    (void)cameraXOffset;
    (void)cameraYOffset;
    return;
#else
    if (!s_toaster.active || swos.g_trainingGame)
        return;

    // Poll optional async LLM scorer quote
    if (s_toaster.llmReqId != 0) {
        std::string reply;
        if (tryTakeLlmResponse(s_toaster.llmReqId, reply)) {
            s_toaster.llmReqId = 0;
            std::string clean = cleanScorerQuote(reply);
            if (!clean.empty())
                s_toaster.comment = std::move(clean);
        }
    }

    Uint32 elapsed = SDL_GetTicks() - s_toaster.startTick;
    if (elapsed >= kTotalDurationMs) {
        s_toaster.active = false;
        s_toaster.llmReqId = 0;
        return;
    }

    // Smooth slide-in / hold / slide-out animation curves
    float animProgress = 1.0f; // 0 = offscreen bottom, 1 = fully visible
    if (elapsed < kSlideInMs) {
        float t = static_cast<float>(elapsed) / static_cast<float>(kSlideInMs);
        float inv = 1.0f - t;
        animProgress = 1.0f - inv * inv * inv; // cubic ease-out
    } else if (elapsed > kSlideInMs + kHoldMs) {
        float t = static_cast<float>(elapsed - (kSlideInMs + kHoldMs)) / static_cast<float>(kSlideOutMs);
        t = std::clamp(t, 0.0f, 1.0f);
        animProgress = 1.0f - t * t * t; // cubic ease-in
    }

    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = r ? getCompactFontAtlas(r) : nullptr;
    if (!r || !atlas)
        return;

    int outW = 0, outH = 0;
    if (SDL_GetRendererOutputSize(r, &outW, &outH) != 0 || outW < 200 || outH < 120)
        return;

    float origSX = 1.0f, origSY = 1.0f;
    SDL_RenderGetScale(r, &origSX, &origSY);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    float gameScale = std::max(1.0f, getGameScale());
    float px = std::clamp(std::floor(static_cast<float>(outH) / 360.0f + 0.5f), 1.0f, 3.0f);
    if (outH < 520) px = std::max(1.0f, static_cast<float>(outH) / 380.0f);

    // Toaster dimensions: sleek bottom-center broadcast lower-third card
    float cardW = std::min(static_cast<float>(outW) * 0.78f, std::max(260.0f, 228.0f * px));
    float cardH = std::max(44.0f, 34.0f * px);
    float marginBottom = std::max(10.0f, static_cast<float>(outH) * 0.032f);
    float targetY = static_cast<float>(outH) - marginBottom - cardH;
    float hiddenY = static_cast<float>(outH) + 8.0f;
    float cardX = std::floor((static_cast<float>(outW) - cardW) * 0.5f);
    float cardY = std::floor(hiddenY + (targetY - hiddenY) * animProgress);

    // Auto-ghost subtly if the live ball passes directly underneath the toaster
    float ghostMult = 1.0f;
    if (swos.gameStatePl == GameState::kInProgress) {
        float offX = getGameScreenOffsetX();
        float offY = getGameScreenOffsetY();
        float bx = offX + (swos.ballSprite.x.asFloat() - cameraXOffset) * gameScale;
        float by = offY + (swos.ballSprite.y.asFloat() - cameraYOffset) * gameScale;
        if (bx >= cardX - 20.0f && bx <= cardX + cardW + 20.0f &&
            by >= cardY - 20.0f && by <= cardY + cardH + 20.0f) {
            ghostMult = 0.28f;
        }
    }

    float masterAlpha = std::clamp(animProgress * ghostMult, 0.0f, 1.0f);
    Uint8 uiAlpha = static_cast<Uint8>(masterAlpha * 255.0f);
    Uint8 bgAlpha = static_cast<Uint8>(masterAlpha * 234.0f);

    SDL_Color accentCol = kitAccentRgb(s_toaster.ev.teamShirtColorIdx);
    accentCol.a = uiAlpha;
    SDL_Color goldCol{ 255, 218, 72, uiAlpha };
    SDL_Color whiteCol{ 248, 250, 252, uiAlpha };
    SDL_Color quoteCol{ 226, 236, 246, uiAlpha };
    SDL_Color darkBg{ 14, 18, 26, bgAlpha };
    SDL_Color borderCol{ 244, 208, 78, static_cast<Uint8>(masterAlpha * 210.0f) };

    // Drop shadow + dark slate broadcast pill + team accent left edge
    fillRectF(r, cardX + 3.0f, cardY + 4.0f, cardW, cardH, { 0, 0, 0, static_cast<Uint8>(masterAlpha * 145.0f) });
    fillRectF(r, cardX, cardY, cardW, cardH, darkBg);
    fillRectF(r, cardX, cardY, 4.0f * (px * 0.55f + 0.45f), cardH, accentCol);
    // Subtle top sheen line
    fillRectF(r, cardX + 4.0f, cardY + 1.0f, cardW - 8.0f, 1.0f, { 255, 255, 255, static_cast<Uint8>(masterAlpha * 42.0f) });
    drawRectF(r, cardX, cardY, cardW, cardH, borderCol);

    // Left zone: animated spinning soccer ball inside the toaster!
    float ballZoneW = cardH * 0.96f;
    float ballCx = cardX + 6.0f + ballZoneW * 0.46f;
    float ballCy = cardY + cardH * 0.50f;
    float baseBallR = cardH * 0.34f;
    float pulse = 1.0f + 0.045f * std::sin(static_cast<float>(elapsed) * 0.011f);
    float spin = static_cast<float>(elapsed) * 0.0055f;
    drawAnimatedSoccerBall(r, ballCx, ballCy, baseBallR * pulse, spin, uiAlpha);

    // Right zone: Minute pill + Scorer Name + Scorer's Comment underneath
    float textX = cardX + 8.0f + ballZoneW;
    float textMaxW = std::max(60.0f, (cardX + cardW - 10.0f) - textX);
    float topRowY = cardY + std::max(4.0f, cardH * 0.13f);

    auto fitPx = [](const std::string& s, float basePx, float maxW) -> float {
        int u = measureCompactTextUnits(s);
        if (u <= 0 || maxW <= 1.0f) return basePx;
        float needed = static_cast<float>(u) * basePx;
        return (needed <= maxW) ? basePx : std::max(0.75f, basePx * (maxW / needed));
    };

    // Minute badge (e.g. "43'")
    std::string minTxt = std::to_string(s_toaster.ev.minute) + "'";
    float minPx = px * 0.92f;
    float minBadgeW = std::max(22.0f, static_cast<float>(measureCompactTextUnits(minTxt)) * minPx + 8.0f);
    float minBadgeH = std::max(10.0f, 9.5f * minPx);
    fillRectF(r, textX, topRowY - 1.0f, minBadgeW, minBadgeH,
        s_toaster.ev.isOwnGoal ? SDL_Color{ 176, 36, 40, uiAlpha } : SDL_Color{ 230, 178, 32, uiAlpha });
    drawCompactTextCentered(r, atlas, textX + minBadgeW * 0.5f, topRowY + (minBadgeH - 8.0f * minPx) * 0.5f - 0.5f,
        minPx, minTxt, s_toaster.ev.isOwnGoal ? whiteCol : SDL_Color{ 18, 20, 24, uiAlpha }, uiAlpha, true);

    // Scorer Name + Team + Flair tag
    std::string scorerLine = s_toaster.ev.isOwnGoal
        ? ("OWN GOAL (" + toUpperAscii(s_toaster.ev.teamName) + ")")
        : (toUpperAscii(s_toaster.ev.scorerName.empty() ? s_toaster.ev.teamName : s_toaster.ev.scorerName) +
           "  -  " + toUpperAscii(s_toaster.ev.teamName));
    if (!s_toaster.ev.isOwnGoal && s_toaster.ev.scorerMatchGoals >= 3)
        scorerLine += "  [HAT-TRICK!]";
    else if (!s_toaster.ev.isOwnGoal && s_toaster.ev.isOutsideBox)
        scorerLine += "  [SCREAMER!]";
    else if (!s_toaster.ev.isOwnGoal && s_toaster.ev.isHeader)
        scorerLine += "  [HEADER]";

    float nameX = textX + minBadgeW + 6.0f;
    float nameMaxW = std::max(30.0f, (cardX + cardW - 10.0f) - nameX);
    float namePx = fitPx(scorerLine, px * 0.96f, nameMaxW);
    drawCompactText(r, atlas, nameX, topRowY, namePx, scorerLine, whiteCol, uiAlpha, true);

    // Comment from the scorer directly underneath (up to 2 crisp wrapped lines)
    float quoteY = topRowY + minBadgeH + std::max(3.0f, cardH * 0.08f);
    float quotePx = px * 0.80f;
    std::string quoted = "\"" + s_toaster.comment + "\"";
    int qMaxUnits = std::max(30, static_cast<int>(textMaxW / quotePx));
    auto qLines = wrapCompactTextUnits(quoted, qMaxUnits);
    if (qLines.size() > 2) {
        quotePx = std::max(0.75f, quotePx * 0.86f);
        qMaxUnits = std::max(36, static_cast<int>(textMaxW / quotePx));
        qLines = wrapCompactTextUnits(quoted, qMaxUnits);
    }
    for (size_t i = 0; i < qLines.size() && i < 2; ++i) {
        drawCompactText(r, atlas, textX, quoteY, quotePx, qLines[i], quoteCol, uiAlpha, false);
        quoteY += 8.8f * quotePx;
    }

    // Subtle countdown progress bar along bottom edge during hold phase
    if (elapsed >= kSlideInMs && elapsed <= kSlideInMs + kHoldMs) {
        float remFrac = 1.0f - static_cast<float>(elapsed - kSlideInMs) / static_cast<float>(kHoldMs);
        float barH = std::max(2.0f, px * 0.85f);
        fillRectF(r, cardX + 4.0f, cardY + cardH - barH, (cardW - 8.0f) * remFrac, barH, goldCol);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_RenderSetScale(r, origSX, origSY);
#endif
}
