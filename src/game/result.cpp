#include "result.h"
#include "clubLogo.h"
#include "compactFont.h"
#include "gameTime.h"
#include "gameFieldMapping.h"
#include "goalToaster.h"
#include "render.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <string>

constexpr int kMaxScorersForDisplay = 8;
constexpr int kMaxGoalsPerScorer = 10;

constexpr int kMaxScorerLineWidth = 120;
constexpr int kCharactersPerLine = 61;  // theoretical maximum (all characters 2 pixels)

constexpr int kResultAtHalfTimeLength = 275;
constexpr int kResultAtGameBreakLength = 165;

constexpr int kGameBreakResult = 31'000;
constexpr int kMaxResultTicks = 32'000;
constexpr int kResultTickClamped = 29'000;

static int m_clearResultInterval = 660;
static int m_clearResultHalftimeInterval = 385;

int clearResultInterval()
{
    return m_clearResultInterval;
}

int clearResultHalftimeInterval()
{
    return m_clearResultHalftimeInterval;
}

void setClearResultInterval(int interval)
{
    m_clearResultInterval = interval;
}

void setClearResultHalftimeInterval(int interval)
{
    m_clearResultHalftimeInterval = interval;
}

struct GoalInfo
{
    GoalType type;
    char time[3];

    void update(GoalType goalType, const std::tuple<int, int, int>& gameTime) {
        type = goalType;
        time[0] = std::get<0>(gameTime) + '0';
        time[1] = std::get<1>(gameTime) + '0';
        time[2] = std::get<2>(gameTime) + '0';
    }
    int storeTime(char *buf) const {
        int i = 0;

        if (time[0] != '0')
            buf[i++] = time[0];
        if (time[1] != '0')
            buf[i++] = time[1];
        buf[i++] = time[2];

        return i;
    }
};

struct ScorerInfo
{
    int shirtNum;
    int numGoals;
    int numLines;
    std::array<GoalInfo, kMaxGoalsPerScorer> goals;
};

static std::array<ScorerInfo, kMaxScorersForDisplay> m_team1Scorers;
static std::array<ScorerInfo, kMaxScorersForDisplay> m_team2Scorers;

using ScorerLines = std::array<std::array<char, kCharactersPerLine>, kMaxScorersForDisplay>;
static ScorerLines m_team1ScorerLines;
static ScorerLines m_team2ScorerLines;

static const char *m_team1Name;
static const char *m_team2Name;

static bool m_showResult;

static void resetResultTimer();
static void updateScorersText(const Sprite& scorer, const TeamGame& team, int teamNum, ScorerInfo& scorerInfo, int currentLine);
static bool bumpTeamGoals(int teamNum);

#ifndef SWOS_TEST
static SDL_Color resultKitAccentRgb(uint8_t idx)
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

static void resultFillRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{ x, y, w, h };
    SDL_RenderFillRectF(r, &rc);
}

static void resultDrawRectF(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{ x, y, w, h };
    SDL_RenderDrawRectF(r, &rc);
}

static std::string formatScorerDisplayLine(const char *raw)
{
    if (!raw || !raw[0])
        return {};
    std::string out;
    for (const char *p = raw; *p; ++p) {
        if (*p == '\t') {
            out += "  ";
        } else if (*p >= 'a' && *p <= 'z') {
            out.push_back(static_cast<char>(*p - 'a' + 'A'));
        } else {
            out.push_back(*p);
        }
    }
    return out;
}

static std::string toUpperResultName(const char *raw)
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

void resetResult(const char *team1Name, const char *team2Name)
{
    m_showResult = false;

    for (int i = 0; i < kMaxScorersForDisplay; i++) {
        m_team1Scorers[i].shirtNum = 0;
        m_team2Scorers[i].shirtNum = 0;
        m_team1ScorerLines[i][0] = '\0';
        m_team2ScorerLines[i][0] = '\0';
    }

    m_team1Name = team1Name;
    m_team2Name = team2Name;
}

void updateResult()
{
    if (swos.resultTimer < 0) {
        hideResult();
    } else if (swos.resultTimer > 0) {
        if (swos.resultTimer == kEndOfHalfResultTimer || swos.resultTimer == kGameBreakResult || swos.resultTimer == kMaxResultTicks) {
            resetResultTimer();
            m_showResult = true;
        }

        swos.resultTimer -= swos.lastFrameTicks;
        if (swos.resultTimer <= 0) {
            if (swos.gameState == GameState::kResultOnHalftime || swos.gameState == GameState::kResultAfterTheGame)
                swos.statsTimer = kMaxResultTicks;
            hideResult();
        }
    }
}

void hideResult()
{
    swos.resultTimer = 0;
    m_showResult = false;
}

void drawResult()
{
#ifdef SWOS_TEST
    return;
#else
    if (!m_showResult)
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

    const char *t1Raw = (m_team1Name && m_team1Name[0]) ? m_team1Name : swos.topTeamInGame.teamName;
    const char *t2Raw = (m_team2Name && m_team2Name[0]) ? m_team2Name : swos.bottomTeamInGame.teamName;
    std::string team1Str = toUpperResultName(t1Raw);
    std::string team2Str = toUpperResultName(t2Raw);

    int g1 = swos.team1GoalsDigit1 * 10 + swos.team1GoalsDigit2;
    int g2 = swos.team2GoalsDigit1 * 10 + swos.team2GoalsDigit2;
    std::string scoreStr = std::to_string(g1) + " - " + std::to_string(g2);

    bool showAgg = (swos.dontShowScorers || swos.secondLeg);
    std::string aggStr = showAgg
        ? ("AGG  " + std::to_string(swos.team1TotalGoals) + " - " + std::to_string(swos.team2TotalGoals))
        : std::string{};

    // Count active scorer lines
    int numScorerRows = 0;
    if (!swos.dontShowScorers) {
        for (int i = 0; i < kMaxScorersForDisplay; ++i) {
            if (m_team1ScorerLines[i][0] || m_team2ScorerLines[i][0])
                numScorerRows = i + 1;
        }
    }

    // Determine match phase badge
    std::string statusLabel;
    SDL_Color badgeBg{ 28, 42, 64, 240 };
    SDL_Color badgeFg{ 246, 212, 78, 255 };
    if (swos.gameState == GameState::kResultOnHalftime) {
        statusLabel = "HALF TIME";
        badgeBg = { 230, 178, 32, 245 };
        badgeFg = { 16, 18, 24, 255 };
    } else if (swos.gameState == GameState::kResultAfterTheGame) {
        statusLabel = "FULL TIME";
        badgeBg = { 46, 168, 92, 245 };
        badgeFg = { 12, 18, 24, 255 };
    } else if (swos.playingPenalties) {
        statusLabel = "PENALTIES";
        badgeBg = { 196, 42, 48, 245 };
        badgeFg = { 250, 252, 255, 255 };
    } else {
        int curMin = static_cast<int>(gameTimeInMinutes());
        statusLabel = (curMin > 0) ? ("LIVE  " + std::to_string(curMin) + "'") : "KICK OFF";
    }

    // Dimensions
    float cardW = std::min(static_cast<float>(vp.w) * 0.84f, std::max(270.0f, 252.0f * px));
    float headerH = std::max(14.0f, 12.5f * px);
    float mainRowH = std::max(26.0f, 22.5f * px);
    float aggRowH = showAgg ? std::max(11.0f, 9.5f * px) : 0.0f;
    float scorerLineH = std::max(9.0f, 8.2f * px);
    float scorersH = (numScorerRows > 0) ? (4.0f * px + static_cast<float>(numScorerRows) * scorerLineH + 3.0f * px) : 0.0f;
    float cardH = headerH + mainRowH + aggRowH + scorersH;

    float marginBottom = std::max(10.0f, static_cast<float>(vp.h) * 0.04f);
    // Stack cleanly above the Goal Toaster if both are active simultaneously
    if (isGoalToasterActive() && !swos.g_trainingGame) {
        float toasterH = std::max(40.0f, 35.0f * px);
        float toasterMargin = std::max(8.0f, static_cast<float>(vp.h) * 0.035f);
        marginBottom = toasterMargin + toasterH + std::max(6.0f, 5.0f * px);
    }

    float cardX = std::floor((static_cast<float>(vp.w) - cardW) * 0.5f);
    float cardY = std::floor(std::max(10.0f, static_cast<float>(vp.h) - marginBottom - cardH));

    SDL_Color bgCol{ 12, 18, 28, 236 };
    SDL_Color headerBg{ 18, 27, 42, 244 };
    SDL_Color borderCol{ 234, 198, 72, 215 };
    SDL_Color dividerCol{ 74, 92, 118, 175 };
    SDL_Color whiteCol{ 248, 250, 252, 255 };
    SDL_Color goldCol{ 255, 222, 76, 255 };
    SDL_Color mutedCol{ 186, 202, 222, 255 };
    SDL_Color col1 = resultKitAccentRgb(static_cast<uint8_t>(swos.topTeamInGame.prShirtCol));
    SDL_Color col2 = resultKitAccentRgb(static_cast<uint8_t>(swos.bottomTeamInGame.prShirtCol));

    // Drop shadow + main glass panel + top highlight sheen + gold outer frame
    resultFillRectF(r, cardX + 3.0f, cardY + 3.5f, cardW, cardH, { 0, 0, 0, 145 });
    resultFillRectF(r, cardX, cardY, cardW, cardH, bgCol);
    resultFillRectF(r, cardX + 1.0f, cardY + 1.0f, cardW - 2.0f, headerH - 1.0f, headerBg);
    resultFillRectF(r, cardX + 2.0f, cardY + 1.0f, cardW - 4.0f, 1.0f, { 255, 255, 255, 48 });
    resultFillRectF(r, cardX + 1.0f, cardY + headerH, cardW - 2.0f, 1.0f, dividerCol);
    resultDrawRectF(r, cardX, cardY, cardW, cardH, borderCol);

    // Left & right kit-color accent rails along the main score row
    float pipW = std::max(3.0f, 3.0f * px);
    float mainY = cardY + headerH + 1.0f;
    resultFillRectF(r, cardX + 1.0f, mainY, pipW, mainRowH + aggRowH - 1.0f, col1);
    resultFillRectF(r, cardX + cardW - 1.0f - pipW, mainY, pipW, mainRowH + aggRowH - 1.0f, col2);

    // Header bar: optional league emblem on left + centered match phase pill
    std::string leagueKey = resolveCurrentMatchLeagueLogoKey();
    if (!leagueKey.empty()) {
        float lgSz = std::max(9.0f, headerH - 3.0f * px);
        drawLeagueLogo(r, leagueKey.c_str(), cardX + 5.0f * px, cardY + (headerH - lgSz) * 0.5f, lgSz, 255);
    }
    float hdrPx = std::max(0.75f, px * 0.72f);
    float badgeW = static_cast<float>(measureCompactTextUnits(statusLabel)) * hdrPx + 10.0f * px;
    float badgeH = std::max(9.0f, headerH - 3.0f * px);
    float badgeX = cardX + (cardW - badgeW) * 0.5f;
    float badgeY = cardY + (headerH - badgeH) * 0.5f;
    resultFillRectF(r, badgeX, badgeY, badgeW, badgeH, badgeBg);
    resultDrawRectF(r, badgeX, badgeY, badgeW, badgeH, { 255, 232, 130, 145 });
    drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, badgeY + (badgeH - 8.0f * hdrPx) * 0.5f, hdrPx, statusLabel, badgeFg, 255, true);

    // Main score row: [Crest 1] [Team 1 Name] [SCORE BOX] [Team 2 Name] [Crest 2]
    float scorePx = std::max(1.1f, px * 1.15f);
    float scoreBoxW = std::max(34.0f * px, static_cast<float>(measureCompactTextUnits(scoreStr)) * scorePx + 12.0f * px);
    float scoreBoxH = std::max(16.0f, mainRowH - 4.5f * px);
    float scoreBoxX = cardX + (cardW - scoreBoxW) * 0.5f;
    float scoreBoxY = mainY + (mainRowH - scoreBoxH) * 0.5f;

    resultFillRectF(r, scoreBoxX, scoreBoxY, scoreBoxW, scoreBoxH, { 24, 34, 52, 246 });
    resultDrawRectF(r, scoreBoxX, scoreBoxY, scoreBoxW, scoreBoxH, { 112, 136, 168, 195 });
    drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, scoreBoxY + (scoreBoxH - 8.0f * scorePx) * 0.5f, scorePx, scoreStr, goldCol, 255, true);

    // Club / National crests
    float crestSz = std::max(14.0f, mainRowH - 5.0f * px);
    float crestPad = 4.0f * px;
    float leftCrestX = cardX + 1.0f + pipW + crestPad;
    float rightCrestX = cardX + cardW - 1.0f - pipW - crestPad - crestSz;
    float crestY = mainY + (mainRowH - crestSz) * 0.5f;
    bool drewLogo1 = drawClubLogo(r, t1Raw, leftCrestX, crestY, crestSz, 255);
    bool drewLogo2 = drawClubLogo(r, t2Raw, rightCrestX, crestY, crestSz, 255);

    // Team names (auto-fit into their half-column)
    float namePx = std::max(0.9f, px * 0.92f);
    float leftNameMinX = leftCrestX + (drewLogo1 ? (crestSz + 4.0f * px) : 0.0f);
    float leftNameMaxX = scoreBoxX - 5.0f * px;
    float leftMaxW = std::max(20.0f, leftNameMaxX - leftNameMinX);
    float t1RawW = static_cast<float>(measureCompactTextUnits(team1Str)) * namePx;
    float t1Px = (t1RawW > leftMaxW && t1RawW > 0.0f) ? std::max(0.65f, namePx * (leftMaxW / t1RawW)) : namePx;
    drawCompactTextRightAligned(r, atlas, leftNameMaxX, mainY + (mainRowH - 8.0f * t1Px) * 0.5f, t1Px, team1Str, whiteCol, 255, true);

    float rightNameMinX = scoreBoxX + scoreBoxW + 5.0f * px;
    float rightNameMaxX = rightCrestX - (drewLogo2 ? (4.0f * px) : -crestSz);
    float rightMaxW = std::max(20.0f, rightNameMaxX - rightNameMinX);
    float t2RawW = static_cast<float>(measureCompactTextUnits(team2Str)) * namePx;
    float t2Px = (t2RawW > rightMaxW && t2RawW > 0.0f) ? std::max(0.65f, namePx * (rightMaxW / t2RawW)) : namePx;
    drawCompactText(r, atlas, rightNameMinX, mainY + (mainRowH - 8.0f * t2Px) * 0.5f, t2Px, team2Str, whiteCol, 255, true);

    // Optional 1st leg / aggregate row
    float cursorY = mainY + mainRowH;
    if (showAgg) {
        float aggPx = std::max(0.7f, px * 0.68f);
        drawCompactTextCentered(r, atlas, cardX + cardW * 0.5f, cursorY + (aggRowH - 8.0f * aggPx) * 0.5f, aggPx, aggStr, mutedCol, 255, true);
        cursorY += aggRowH;
    }

    // Scorers footer section
    if (numScorerRows > 0) {
        resultFillRectF(r, cardX + 6.0f * px, cursorY, cardW - 12.0f * px, 1.0f, { 74, 92, 118, 130 });
        float scPx = std::max(0.70f, px * 0.66f);
        float scY = cursorY + 3.5f * px;
        float leftAnchor = cardX + cardW * 0.5f - 8.0f * px;
        float rightAnchor = cardX + cardW * 0.5f + 8.0f * px;
        float halfMaxW = std::max(24.0f, cardW * 0.5f - 14.0f * px);

        for (int i = 0; i < numScorerRows; ++i) {
            if (m_team1ScorerLines[i][0]) {
                std::string s = formatScorerDisplayLine(m_team1ScorerLines[i].data());
                float sw = static_cast<float>(measureCompactTextUnits(s)) * scPx;
                float fitPx = (sw > halfMaxW && sw > 0.0f) ? std::max(0.55f, scPx * (halfMaxW / sw)) : scPx;
                drawCompactTextRightAligned(r, atlas, leftAnchor, scY, fitPx, s, mutedCol, 255, false);
            }
            if (m_team2ScorerLines[i][0]) {
                std::string s = formatScorerDisplayLine(m_team2ScorerLines[i].data());
                float sw = static_cast<float>(measureCompactTextUnits(s)) * scPx;
                float fitPx = (sw > halfMaxW && sw > 0.0f) ? std::max(0.55f, scPx * (halfMaxW / sw)) : scPx;
                drawCompactText(r, atlas, rightAnchor, scY, fitPx, s, mutedCol, 255, false);
            }
            scY += scorerLineH;
        }
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_RenderSetScale(r, origSX, origSY);
#endif
}

void registerScorer(const Sprite& scorer, int teamNum, GoalType goalType)
{
    assert(swos.topTeamPtr && swos.bottomTeamPtr);

    ScorerInfo *scorers;
    TeamGame *scoringTeam, *concedingTeam;

    if (teamNum == 1) {
        scorers = m_team1Scorers.data();
        scoringTeam = swos.topTeamPtr;
        concedingTeam = swos.bottomTeamPtr;
    } else {
        scorers = m_team2Scorers.data();
        scoringTeam = swos.bottomTeamPtr;
        concedingTeam = swos.topTeamPtr;
    }

    auto& player = scoringTeam->players[scorer.playerOrdinal - 1];
    int shirtNum = player.shirtNumber;

    if (goalType == GoalType::kOwnGoal) {
        std::swap(scoringTeam, concedingTeam);
        concedingTeam->numOwnGoals++;
        shirtNum += 1'000;  // this is to differentiate a player that scored own goal from a guy with the same
                            // number in the opposite team (since own-goals appear under the opponent's goals)
    } else {
        player.goalsScored++;
    }

    int currentSlot = 0;
    int currentLine = 0;

    while (currentSlot < kMaxScorersForDisplay && currentLine < kMaxScorersForDisplay) {
        auto& scorerInfo = scorers[currentSlot];
        if (!scorerInfo.shirtNum || scorerInfo.shirtNum == shirtNum) {
            if (!scorerInfo.shirtNum) {
                scorerInfo.numGoals = 0;
                scorerInfo.numLines = 1;
            }
            scorerInfo.shirtNum = shirtNum;
            if (scorerInfo.numGoals != kMaxGoalsPerScorer) {
                auto& goal = scorerInfo.goals[scorerInfo.numGoals++];
                const auto& gameTime = gameTimeAsBcd();
                goal.update(goalType, gameTime);
                updateScorersText(scorer, *scoringTeam, teamNum, scorerInfo, currentLine);
            }
            break;
        } else {
            currentSlot++;
            currentLine += scorerInfo.numLines;
        }
    }
}

// in:
//      teamNum  - number of team that scored (1 or 2)
//      scorer  -> in-game sprite of the player that scored
//
// Updates goal variables. Puts name of scorer and minute into scorers list.
// If the goal list is too long, it will be broken into up to 8 lines.
// Each line is one sprite. Skips result sprites updating if it's penalty shootout.
//
void goalScored(int teamNum, Sprite& scorer)
{
    swos.goalScored = 1;
    swos.runSlower = 1;
    swos.lastTeamScoredNumber = teamNum;
    swos.lastPlayerScored = &scorer;
    swos.currentScorer = &scorer;

    auto teamGame = scorer.teamNumber == 1 ? &swos.topTeamInGame : &swos.bottomTeamInGame;
    auto team = swos.topTeamData.inGameTeamPtr.asAligned() == teamGame ? &swos.topTeamData : &swos.bottomTeamData;

    swos.lastTeamScored = team;

    if (!bumpTeamGoals(teamNum) || swos.playingPenalties)
        return;

    auto goalType = GoalType::kRegular;
    swos.goalTypeScored = static_cast<int>(GoalType::kRegular);

    if (scorer.teamNumber != teamNum) {
        goalType = GoalType::kOwnGoal;
        swos.goalTypeScored = static_cast<int>(GoalType::kOwnGoal);
    } else if (swos.penalty) {
        goalType = GoalType::kPenalty;
    }

    registerScorer(scorer, teamNum, goalType);
}

static void resetResultTimer()
{
    assert(swos.resultTimer == kEndOfHalfResultTimer || swos.resultTimer == kGameBreakResult || swos.resultTimer == kMaxResultTicks);

    switch (swos.resultTimer) {
    case kEndOfHalfResultTimer:
        swos.resultTimer = kResultAtHalfTimeLength;
        break;
    case kGameBreakResult:
        swos.resultTimer = kResultAtGameBreakLength;
        break;
    case kMaxResultTicks:
        swos.resultTimer = kResultTickClamped;
        break;
    }
}

static const char *getPlayerName(int playerOrdinal, const TeamGame& team)
{
    auto name = team.players[playerOrdinal].shortName;
    int offset = name[0] && name[1] && name[1] == '.' && name[2] == ' ' ? 3 : 0;
    return name + offset;
}

static bool shiftLinesDown(ScorerLines& lines, int lineToFree)
{
    if (lineToFree >= kMaxScorersForDisplay)
        return false;

    memmove(lines[lineToFree + 1].data(), lines[lineToFree].data(), (lines.size() - lineToFree - 1) * lines[0].size());

    return true;
}

static void updateScorersText(const Sprite& scorer, const TeamGame& team, int teamNum, ScorerInfo& scorerInfo, int currentLine)
{
    assert(currentLine < kMaxScorersForDisplay);

    auto name = getPlayerName(scorer.playerOrdinal - 1, team);
    auto& lines = teamNum == 1 ? m_team1ScorerLines : m_team2ScorerLines;
    auto line = lines[currentLine].data();

    int i = 0;
    while (line[i++] = *name++)
        ;

    line[i - 1] = '\t'; // preserve 5 pixels spacing as in the original
    line[i] = '\0';

    int width = getStringPixelLength(line);
    line += i;

    int currentScorerLine = 0;
    char goalMinuteBuf[16];

    for (int i = 0; i < scorerInfo.numGoals; i++) {
        const auto& goal = scorerInfo.goals[i];
        int minuteBufLen = goal.storeTime(goalMinuteBuf);

        switch (goal.type) {
        case GoalType::kPenalty:
            strcpy(&goalMinuteBuf[minuteBufLen], "(PEN)");
            minuteBufLen += 5;
            break;
        case GoalType::kOwnGoal:
            strcpy(&goalMinuteBuf[minuteBufLen], "(OG)");
            minuteBufLen += 4;
            break;
        }

        if (i != scorerInfo.numGoals - 1)
            goalMinuteBuf[minuteBufLen++] = ',';

        goalMinuteBuf[minuteBufLen] = '\0';
        int goalMinuteLen = getStringPixelLength(goalMinuteBuf);

        if (width + goalMinuteLen <= kMaxScorerLineWidth) {
            width += goalMinuteLen;
        } else {
            if (++currentLine >= kMaxScorersForDisplay)
                break;

            if (++currentScorerLine >= scorerInfo.numLines) {
                if (!shiftLinesDown(lines, currentLine))
                    break;
                scorerInfo.numLines++;
            }

            *line = '\0';
            line = lines[currentLine].data();
            width = goalMinuteLen;
        }

        assert(line + minuteBufLen < lines[currentLine].data() + kCharactersPerLine);

        memcpy(line, goalMinuteBuf, minuteBufLen);
        line += minuteBufLen;
    }

    *line = '\0';
}

static bool bumpTeamGoals(int teamNum)
{
    constexpr int kMaxGoals = 99;

    static word *goalVars[2][2] = {
        &swos.team1TotalGoals, &swos.team2TotalGoals,
        &swos.team1PenaltyGoals, &swos.team2PenaltyGoals,
    };

    bool isSecondTeam = teamNum == 2;

    if (*goalVars[0][isSecondTeam] == kMaxGoals)
        return false;

    ++*goalVars[swos.playingPenalties != 0][isSecondTeam];

    static word * const kDigitsAndStats[2][3] = {
        &swos.team1GoalsDigit1, &swos.team1GoalsDigit2, &swos.statsTeam1Goals,
        &swos.team2GoalsDigit1, &swos.team2GoalsDigit2, &swos.statsTeam2Goals,
    };

    auto teamDigitsAndStats = kDigitsAndStats[isSecondTeam];

    if (++*teamDigitsAndStats[1] == 10) {
        ++*teamDigitsAndStats[0];
        *teamDigitsAndStats[1] = 0;
    }
    ++*teamDigitsAndStats[2];

    return true;
}

std::string getMatchScorersSummary(int teamNum)
{
    const auto& lines = (teamNum == 1) ? m_team1ScorerLines : m_team2ScorerLines;
    std::string summary;
    for (const auto& line : lines) {
        if (line[0] == '\0')
            continue;
        if (!summary.empty())
            summary += "; ";
        summary += line.data();
    }
    return summary;
}
