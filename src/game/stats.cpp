#include "stats.h"
#include "clubLogo.h"
#include "compactFont.h"
#include "gameTime.h"
#include "render.h"
#include "result.h"
#include "replays.h"
#include "pitchConstants.h"

#include <algorithm>
#include <cmath>
#include <string>

static bool m_showStats;
static bool m_showingUserRequestedStats;

static bool m_isGoalAttempt;

static void drawStats();
static std::pair<const TeamStatsData *, const TeamStatsData *> getTeamStatsPointers();
static void drawModernMatchStats(int team1Goals, int team2Goals, const TeamStatsData *leftTeamStats, const TeamStatsData *rightTeamStats);
static void checkStatsTimer();

void initStats()
{
    m_isGoalAttempt = false;
    m_showStats = false;
}

void toggleStats()
{
    if (m_showingUserRequestedStats) {
        hideStats();
    } else {
        m_showStats = true;
        swos.statsTimer = 1;
        hideResult();
    }
}

void hideStats()
{
    m_showStats = false;
    m_showingUserRequestedStats = false;
    swos.statsTimer = 0;
}

bool statsEnqueued()
{
    return m_showStats;
}

bool showingUserRequestedStats()
{
    if (m_showStats) {
        m_showStats = false;
        m_showingUserRequestedStats = true;
        swos.statsTimer = 1;
    }

    return m_showingUserRequestedStats;
}

bool showingPostGameStats()
{
    return swos.statsTimer > 0;
}

void updateStatistics()
{
    if (!m_showingUserRequestedStats && !swos.playingPenalties) {
        if (swos.gameStatePl == GameState::kInProgress) {
            if (swos.lastTeamPlayed == &swos.topTeamData || swos.lastTeamPlayed == &swos.bottomTeamData)
                swos.lastTeamPlayed->teamStatsPtr.asAligned()->ballPossession++;

            auto team = &swos.topTeamData;
            auto stats = team->teamStatsPtr.asAligned();
            auto ballDelta = swos.ballSprite.deltaY;

            if (m_isGoalAttempt) {
                if (swos.ballSprite.y >= kPitchCenterY) {
                    team = &swos.bottomTeamData;
                    ballDelta = -ballDelta;
                }
                if (team == swos.lastTeamPlayed || ballDelta >= 0)
                    m_isGoalAttempt = false;
            } else if (swos.ballInGoalkeeperArea) {
                if (swos.ballSprite.y <= kPitchCenterY) {
                    team = &swos.bottomTeamData;
                    ballDelta = -ballDelta;
                    stats = team->teamStatsPtr.asAligned();
                }
                if (ballDelta > 0 && team == swos.lastTeamPlayed && !team->playerHasBall &&
                    swos.strikeDestX >= kGoalAttemptLeft && swos.strikeDestX <= kGoalAttemptRight) {
                    if (swos.strikeDestX >= kGoalLeft && swos.strikeDestX <= kGoalRight)
                        stats->onTarget++;
                    stats->goalAttempts++;
                    m_isGoalAttempt = true;
                }
            }
        } else {
            m_isGoalAttempt = 0;
        }
    }

    checkStatsTimer();
}

const GameStats getStats()
{
    GameStats stats;

    const TeamStatsData *leftStats, *rightStats;
    std::tie(leftStats, rightStats) = getTeamStatsPointers();

    stats.team1 = *leftStats;
    stats.team2 = *rightStats;

    return stats;
}

void drawStats(int team1Goals, int team2Goals, const GameStats& stats)
{
    drawModernMatchStats(team1Goals, team2Goals, &stats.team1, &stats.team2);
}

// Called once per frame from main loop.
void drawStatsIfNeeded()
{
    if (m_showingUserRequestedStats || swos.statsTimer > 0)
        drawStats();
}

static void drawStats()
{
    const TeamStatsData *leftStats, *rightStats;
    std::tie(leftStats, rightStats) = getTeamStatsPointers();

    drawModernMatchStats(swos.statsTeam1Goals, swos.statsTeam2Goals, leftStats, rightStats);

    if (!m_showingUserRequestedStats) {
        GameStats stats{ *leftStats, *rightStats };
        saveStatsForHighlights(stats);
    }
}

static std::pair<const TeamStatsData *, const TeamStatsData *> getTeamStatsPointers()
{
    auto leftTeam = &swos.topTeamData;
    auto rightTeam = &swos.bottomTeamData;

    if (leftTeam->inGameTeamPtr.asAligned() != &swos.topTeamInGame)
        std::swap(leftTeam, rightTeam);

    return { leftTeam->teamStatsPtr.asAligned(), rightTeam->teamStatsPtr.asAligned() };
}

#ifndef SWOS_TEST
static SDL_Color statsKitAccentRgb(uint8_t idx)
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

static void statsFillRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{ x, y, w, h };
    SDL_RenderFillRectF(r, &rc);
}

static void statsDrawRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{ x, y, w, h };
    SDL_RenderDrawRectF(r, &rc);
}

static std::string toUpperStatsName(const char *raw)
{
    if (!raw || !raw[0])
        return {};
    std::string out;
    for (const char *p = raw; *p; ++p) {
        if (*p >= 'a' && *p <= 'z')
            out.push_back(static_cast<char>(*p - 'a' + 'A'));
        else
            out.push_back(*p);
    }
    return out;
}
#endif

static void drawModernMatchStats(int team1Goals, int team2Goals, const TeamStatsData *leftTeamStats, const TeamStatsData *rightTeamStats)
{
#ifdef SWOS_TEST
    (void)team1Goals;
    (void)team2Goals;
    (void)leftTeamStats;
    (void)rightTeamStats;
    return;
#else
    if (!leftTeamStats || !rightTeamStats)
        return;

    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = r ? getCompactFontAtlas(r) : nullptr;
    if (!r || !atlas)
        return;

    SDL_Rect vp = getViewport();
    if (vp.w < 160 || vp.h < 100)
        return;

    float origSX = 1.0f, origSY = 1.0f;
    SDL_RenderGetScale(r, &origSX, &origSY);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    float px = 1.0f;
    if (vp.h >= 900)
        px = std::clamp(std::floor(static_cast<float>(vp.h) / 480.0f + 0.5f), 2.0f, 4.0f);
    else if (vp.h >= 540)
        px = 1.5f;

    // Subtle full-viewport backdrop vignette so stats pop cleanly over the pitch
    statsFillRectF(r, 0.0f, 0.0f, static_cast<float>(vp.w), static_cast<float>(vp.h), { 6, 10, 18, 128 });

    struct StatRow {
        const char *label;
        int leftVal;
        int rightVal;
        bool isPercent;
        bool isEmpty;
        int cardIcon; // 0 = none, 1 = yellow card, 2 = red card
    };

    int totalPoss = leftTeamStats->ballPossession + rightTeamStats->ballPossession;
    int leftPoss = (totalPoss > 0) ? (100 * leftTeamStats->ballPossession / totalPoss) : 0;
    int rightPoss = (totalPoss > 0) ? (100 - leftPoss) : 0;

    const StatRow rows[] = {
        { "POSSESSION",     leftPoss,                         rightPoss,                          true,  totalPoss == 0, 0 },
        { "GOAL ATTEMPTS",  leftTeamStats->goalAttempts,      rightTeamStats->goalAttempts,       false, false,          0 },
        { "ON TARGET",      leftTeamStats->onTarget,          rightTeamStats->onTarget,           false, false,          0 },
        { "CORNERS WON",    leftTeamStats->cornersWon,        rightTeamStats->cornersWon,         false, false,          0 },
        { "FOULS CONCEDED", leftTeamStats->foulsConceded,     rightTeamStats->foulsConceded,      false, false,          0 },
        { "BOOKINGS",       leftTeamStats->bookings,          rightTeamStats->bookings,           false, false,          1 },
        { "SENDINGS OFF",   leftTeamStats->sendingsOff,       rightTeamStats->sendingsOff,        false, false,          2 },
    };
    constexpr int kNumRows = static_cast<int>(sizeof(rows) / sizeof(rows[0]));

    float cardW = std::min(static_cast<float>(vp.w) * 0.80f, std::max(290.0f, 256.0f * px));
    float headerH = std::max(18.0f, 16.0f * px);
    float heroH = std::max(28.0f, 24.0f * px);
    float rowH = std::max(17.0f, 15.5f * px);
    float footerH = m_showingUserRequestedStats ? std::max(13.0f, 11.5f * px) : std::max(5.0f, 4.0f * px);
    float cardH = headerH + heroH + static_cast<float>(kNumRows) * rowH + footerH;

    // Clamp to viewport height if running at very small window size
    if (cardH > static_cast<float>(vp.h) - 12.0f) {
        float scaleDown = (static_cast<float>(vp.h) - 12.0f) / cardH;
        headerH *= scaleDown;
        heroH *= scaleDown;
        rowH *= scaleDown;
        footerH *= scaleDown;
        cardH = headerH + heroH + static_cast<float>(kNumRows) * rowH + footerH;
    }

    float cardX = std::floor((static_cast<float>(vp.w) - cardW) * 0.5f);
    float cardY = std::floor((static_cast<float>(vp.h) - cardH) * 0.5f);

    SDL_Color bgCol{ 12, 18, 28, 240 };
    SDL_Color headerBg{ 18, 28, 44, 248 };
    SDL_Color heroBg{ 16, 24, 38, 244 };
    SDL_Color borderCol{ 234, 198, 72, 224 };
    SDL_Color dividerCol{ 74, 92, 118, 175 };
    SDL_Color whiteCol{ 248, 250, 252, 255 };
    SDL_Color goldCol{ 255, 222, 76, 255 };
    SDL_Color mutedCol{ 168, 186, 210, 255 };
    SDL_Color dimCol{ 128, 146, 170, 255 };
    SDL_Color col1 = statsKitAccentRgb(static_cast<uint8_t>(swos.topTeamInGame.prShirtCol));
    SDL_Color col2 = statsKitAccentRgb(static_cast<uint8_t>(swos.bottomTeamInGame.prShirtCol));

    // Drop shadow + dark glass frame + top sheen + gold border
    statsFillRectF(r, cardX + 4.0f, cardY + 4.5f, cardW, cardH, { 0, 0, 0, 165 });
    statsFillRectF(r, cardX, cardY, cardW, cardH, bgCol);
    statsFillRectF(r, cardX + 1.0f, cardY + 1.0f, cardW - 2.0f, headerH - 1.0f, headerBg);
    statsFillRectF(r, cardX + 2.0f, cardY + 1.0f, cardW - 4.0f, 1.0f, { 255, 255, 255, 52 });
    statsFillRectF(r, cardX + 1.0f, cardY + headerH, cardW - 2.0f, 1.0f, dividerCol);
    statsDrawRectF(r, cardX, cardY, cardW, cardH, borderCol);

    // Header bar: competition logo + "MATCH STATISTICS" + phase/minute badge
    std::string leagueKey = resolveCurrentMatchLeagueLogoKey();
    if (!leagueKey.empty()) {
        float lgSz = std::max(10.0f, headerH - 4.0f * px);
        drawLeagueLogo(r, leagueKey.c_str(), cardX + 6.0f * px, cardY + (headerH - lgSz) * 0.5f, lgSz, 255);
    }

    float titlePx = std::max(0.85f, px * 0.88f);
    drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, cardY + (headerH - 8.0f * titlePx) * 0.5f, titlePx, "MATCH STATISTICS", goldCol, 255, true);

    std::string phaseStr;
    if (swos.gameState == GameState::kResultOnHalftime)
        phaseStr = "HT";
    else if (swos.gameState == GameState::kResultAfterTheGame)
        phaseStr = "FT";
    else if (swos.playingPenalties)
        phaseStr = "PEN";
    else
        phaseStr = std::to_string(static_cast<int>(gameTimeInMinutes())) + "'";

    float phasePx = std::max(0.70f, px * 0.70f);
    float phaseW = static_cast<float>(measureCompactTextUnits(phaseStr)) * phasePx + 7.0f * px;
    float phaseH = std::max(10.0f, headerH - 5.0f * px);
    float phaseX = cardX + cardW - 6.0f * px - phaseW;
    float phaseY = cardY + (headerH - phaseH) * 0.5f;
    statsFillRectF(r, phaseX, phaseY, phaseW, phaseH, { 30, 44, 66, 240 });
    statsDrawRectF(r, phaseX, phaseY, phaseW, phaseH, { 112, 136, 168, 170 });
    drawCompactTextCentered(r, atlas, phaseX + phaseW * 0.5f, phaseY + (phaseH - 8.0f * phasePx) * 0.5f, phasePx, phaseStr, whiteCol, 255, true);

    // Teams & Score Hero Row
    float heroY = cardY + headerH + 1.0f;
    statsFillRectF(r, cardX + 1.0f, heroY, cardW - 2.0f, heroH - 1.0f, heroBg);
    statsFillRectF(r, cardX + 1.0f, heroY + heroH - 1.0f, cardW - 2.0f, 1.0f, dividerCol);

    float pipW = std::max(3.0f, 3.0f * px);
    statsFillRectF(r, cardX + 1.0f, heroY, pipW, heroH - 1.0f, col1);
    statsFillRectF(r, cardX + cardW - 1.0f - pipW, heroY, pipW, heroH - 1.0f, col2);

    std::string scoreStr = std::to_string(team1Goals) + " - " + std::to_string(team2Goals);
    float scorePx = std::max(1.1f, px * 1.15f);
    float scoreBoxW = std::max(34.0f * px, static_cast<float>(measureCompactTextUnits(scoreStr)) * scorePx + 12.0f * px);
    float scoreBoxH = std::max(15.0f, heroH - 5.0f * px);
    float scoreBoxX = cardX + (cardW - scoreBoxW) * 0.5f;
    float scoreBoxY = heroY + (heroH - scoreBoxH) * 0.5f;
    statsFillRectF(r, scoreBoxX, scoreBoxY, scoreBoxW, scoreBoxH, { 26, 38, 58, 248 });
    statsDrawRectF(r, scoreBoxX, scoreBoxY, scoreBoxW, scoreBoxH, { 234, 198, 72, 195 });
    drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, scoreBoxY + (scoreBoxH - 8.0f * scorePx) * 0.5f, scorePx, scoreStr, goldCol, 255, true);

    float crestSz = std::max(14.0f, heroH - 5.0f * px);
    float crestPad = 4.5f * px;
    float leftCrestX = cardX + 1.0f + pipW + crestPad;
    float rightCrestX = cardX + cardW - 1.0f - pipW - crestPad - crestSz;
    float crestY = heroY + (heroH - crestSz) * 0.5f;
    bool drewLogo1 = drawClubLogo(r, swos.topTeamInGame.teamName, leftCrestX, crestY, crestSz, 255);
    bool drewLogo2 = drawClubLogo(r, swos.bottomTeamInGame.teamName, rightCrestX, crestY, crestSz, 255);

    std::string t1Name = toUpperStatsName(swos.topTeamInGame.teamName);
    std::string t2Name = toUpperStatsName(swos.bottomTeamInGame.teamName);
    float namePx = std::max(0.85f, px * 0.88f);

    float lNameMin = leftCrestX + (drewLogo1 ? (crestSz + 4.0f * px) : 0.0f);
    float lNameMax = scoreBoxX - 5.0f * px;
    float lMaxW = std::max(20.0f, lNameMax - lNameMin);
    float t1W = static_cast<float>(measureCompactTextUnits(t1Name)) * namePx;
    float t1Px = (t1W > lMaxW && t1W > 0.0f) ? std::max(0.60f, namePx * (lMaxW / t1W)) : namePx;
    drawCompactTextRightAligned(r, atlas, lNameMax, heroY + (heroH - 8.0f * t1Px) * 0.5f, t1Px, t1Name, whiteCol, 255, true);

    float rNameMin = scoreBoxX + scoreBoxW + 5.0f * px;
    float rNameMax = rightCrestX - (drewLogo2 ? (4.0f * px) : -crestSz);
    float rMaxW = std::max(20.0f, rNameMax - rNameMin);
    float t2W = static_cast<float>(measureCompactTextUnits(t2Name)) * namePx;
    float t2Px = (t2W > rMaxW && t2W > 0.0f) ? std::max(0.60f, namePx * (rMaxW / t2W)) : namePx;
    drawCompactText(r, atlas, rNameMin, heroY + (heroH - 8.0f * t2Px) * 0.5f, t2Px, t2Name, whiteCol, 255, true);

    // Stat comparison rows with dual-color proportional bars
    float curY = heroY + heroH;
    float numPx = std::max(0.80f, px * 0.82f);
    float lblPx = std::max(0.70f, px * 0.70f);
    float innerMargin = 10.0f * px;
    float barH = std::max(2.5f, 2.6f * px);

    for (int i = 0; i < kNumRows; ++i) {
        const auto& row = rows[i];
        if (i % 2 == 1) {
            statsFillRectF(r, cardX + 1.0f, curY, cardW - 2.0f, rowH, { 18, 26, 40, 165 });
        }

        std::string leftStr = row.isEmpty ? "-" : (std::to_string(row.leftVal) + (row.isPercent ? "%" : ""));
        std::string rightStr = row.isEmpty ? "-" : (std::to_string(row.rightVal) + (row.isPercent ? "%" : ""));

        bool leftLeads = !row.isEmpty && (row.leftVal > row.rightVal);
        bool rightLeads = !row.isEmpty && (row.rightVal > row.leftVal);

        SDL_Color lCol = leftLeads ? goldCol : (row.isEmpty ? dimCol : whiteCol);
        SDL_Color rCol = rightLeads ? goldCol : (row.isEmpty ? dimCol : whiteCol);

        float textY = curY + 2.0f * px;
        drawCompactText(r, atlas, cardX + innerMargin, textY, numPx, leftStr, lCol, 255, leftLeads);
        drawCompactTextRightAligned(r, atlas, cardX + cardW - innerMargin, textY, numPx, rightStr, rCol, 255, rightLeads);

        // Centered stat label (+ subtle yellow/red card rectangle for bookings/sendings off)
        float lblY = textY + (8.0f * numPx - 8.0f * lblPx) * 0.5f;
        drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, lblY, lblPx, row.label, mutedCol, 255, false);
        if (row.cardIcon != 0) {
            float lblHalfW = static_cast<float>(measureCompactTextUnits(row.label)) * lblPx * 0.5f;
            float icW = std::max(4.0f, 3.8f * px);
            float icH = std::max(5.5f, 5.2f * px);
            float icX = cardX + cardW * 0.5f - lblHalfW - icW - 3.5f * px;
            float icY = lblY + (8.0f * lblPx - icH) * 0.5f;
            SDL_Color cardCol = (row.cardIcon == 1) ? SDL_Color{ 246, 206, 46, 255 } : SDL_Color{ 222, 46, 52, 255 };
            statsFillRectF(r, icX, icY, icW, icH, cardCol);
            statsDrawRectF(r, icX, icY, icW, icH, { 16, 20, 28, 220 });
        }

        // Proportional comparison bar directly under the row numbers
        float barX = cardX + innerMargin;
        float barW = cardW - 2.0f * innerMargin;
        float barY = curY + rowH - barH - 2.2f * px;
        statsFillRectF(r, barX, barY, barW, barH, { 28, 38, 54, 220 });

        int sum = row.isEmpty ? 0 : (row.leftVal + row.rightVal);
        if (sum > 0) {
            float leftFrac = static_cast<float>(row.leftVal) / static_cast<float>(sum);
            float leftW = std::round(barW * leftFrac);
            float rightW = barW - leftW;
            if (leftW >= 1.0f)
                statsFillRectF(r, barX, barY, leftW, barH, col1);
            if (rightW >= 1.0f)
                statsFillRectF(r, barX + leftW, barY, rightW, barH, col2);
            // Crisp center split tick when both teams have > 0
            if (leftW >= 2.0f && rightW >= 2.0f)
                statsFillRectF(r, barX + leftW - 0.5f, barY, 1.0f, barH, { 10, 14, 22, 240 });
        }
        statsDrawRectF(r, barX, barY, barW, barH, { 72, 88, 112, 130 });

        curY += rowH;
    }

    // Subtle footer hint when user opened stats manually
    if (m_showingUserRequestedStats) {
        statsFillRectF(r, cardX + 1.0f, curY, cardW - 2.0f, 1.0f, { 74, 92, 118, 120 });
        float fPx = std::max(0.65f, px * 0.62f);
        drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, curY + (footerH - 8.0f * fPx) * 0.5f, fPx, "PRESS S OR FIRE TO RESUME", dimCol, 255, false);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_RenderSetScale(r, origSX, origSY);
#endif
}

static void checkStatsTimer()
{
    if (swos.statsTimer < 0)
        hideStats();
}
