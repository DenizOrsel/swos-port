#include "sponsorChallenge.h"
#include "compactFont.h"
#include "controls.h"
#include "darkRectangle.h"
#include "drawMenu.h"
#include "gameFieldMapping.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "llmClient.h"
#include "llmReportMenu.h"
#include "log.h"
#include "matchTelemetry.h"
#include "menuBackground.h"
#include "menus.h"
#include "render.h"
#include "stats.h"
#include "swos.h"
#include "text.h"
#include "sponsorChallenge.mnu.h"

#include <SimpleIni.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

using namespace SponsorChallengeMenu;

namespace {

constexpr int kOfferedChallengeCount = 3;

CareerChallengeState s_state;
uint64_t s_pendingRequestId = 0;
bool s_matchEligible = false;
bool s_humanIsTeam1 = true;
std::string s_matchClubName;
std::string s_matchOpponentName;
int s_matchDivisionTier = 0;
std::vector<SponsorStarterInfo> s_matchStarters;
uint32_t s_matchSeed = 1;
bool s_prevKeyDown[4] = { false, false, false, false };

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

std::string sanitizePlain(const std::string& in, size_t maxLen)
{
    std::string out = sanitizeCompactMixedCase(in);
    if (out.size() > maxLen) {
        out.resize(maxLen);
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
    }
    return out;
}

int32_t readCurrentClubBalance()
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers())
        return mgr.activePlayer().newBalance;
    return *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
}

void applyClubBalanceDelta(int32_t delta)
{
    if (delta == 0)
        return;

    auto *vmBal = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    auto *vmSpon = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kSensisoftPlcSponsorship]);
    *vmBal += delta;
    *vmSpon += delta;

    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        auto& p = mgr.activePlayer();
        p.newBalance += delta;
        p.sensisoftPlcSponsorship += delta;
    }
}

void syncTotalsFromHotseatIfActive()
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        const auto& p = mgr.activePlayer();
        s_state.challengesCompleted = p.sponsorChallengesCompleted;
        s_state.challengesFailed = p.sponsorChallengesFailed;
        s_state.totalBonusEarned = p.sponsorTotalBonusEarned;
    }
}

void syncTotalsToHotseatIfActive()
{
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        auto& p = mgr.activePlayer();
        p.sponsorChallengesCompleted = s_state.challengesCompleted;
        p.sponsorChallengesFailed = s_state.challengesFailed;
        p.sponsorTotalBonusEarned = s_state.totalBonusEarned;
    }
}

int readDivisionTier()
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers())
        return static_cast<int>(mgr.activePlayer().careerTeamLeague);
    return static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]));
}

SponsorChallengeType parseChallengeTypeString(const std::string& raw)
{
    std::string u = toUpperAscii(trimCopy(raw));
    if (u.find("CLEAN_SHEET") != std::string::npos || u.find("SHUTOUT") != std::string::npos)
        return SponsorChallengeType::kCleanSheetWin;
    if (u.find("LONG_RANGE") != std::string::npos || u.find("OUTSIDE_BOX") != std::string::npos ||
        u.find("OUTSIDE_PENALTY") != std::string::npos)
        return SponsorChallengeType::kLongRangeGoal;
    if (u.find("HEADER") != std::string::npos || u.find("HEADED") != std::string::npos)
        return SponsorChallengeType::kHeaderGoal;
    if (u.find("TEAM_GOAL") != std::string::npos || u.find("TOTAL_GOAL") != std::string::npos ||
        u.find("SCORE_GOAL") != std::string::npos)
        return SponsorChallengeType::kTeamGoals;
    if (u.find("PLAYER_GOAL") != std::string::npos || u.find("STRIKER") != std::string::npos)
        return SponsorChallengeType::kPlayerGoals;
    if (u.find("FIRST_HALF") != std::string::npos || u.find("HALF") != std::string::npos)
        return SponsorChallengeType::kFirstHalfGoals;
    if (u.find("POSSESSION") != std::string::npos)
        return SponsorChallengeType::kPossessionWin;
    if (u.find("SHOTS") != std::string::npos || u.find("TARGET") != std::string::npos)
        return SponsorChallengeType::kShotsOnTarget;
    if (u.find("MARGIN") != std::string::npos || u.find("BIG_WIN") != std::string::npos)
        return SponsorChallengeType::kWinByMargin;
    return SponsorChallengeType::kNone;
}

const SponsorStarterInfo *pickBestScorerStarter(const std::vector<SponsorStarterInfo>& starters)
{
    const SponsorStarterInfo *best = nullptr;
    for (const auto& s : starters) {
        if (s.shortName.empty())
            continue;
        if (!best) {
            best = &s;
            continue;
        }
        int scoreCur = (s.isAttacker ? 100 : (s.isMidfielder ? 40 : 0)) + s.seasonGoals * 5;
        int scoreBest = (best->isAttacker ? 100 : (best->isMidfielder ? 40 : 0)) + best->seasonGoals * 5;
        if (scoreCur > scoreBest)
            best = &s;
    }
    return best;
}

const SponsorStarterInfo *matchStarterByName(const std::vector<SponsorStarterInfo>& starters, const std::string& name)
{
    if (name.empty())
        return nullptr;
    std::string needle = toUpperAscii(trimCopy(name));
    if (needle.empty() || needle == "NONE" || needle == "N/A" || needle == "-")
        return nullptr;

    for (const auto& s : starters) {
        std::string cand = toUpperAscii(s.shortName);
        if (cand == needle)
            return &s;
    }
    for (const auto& s : starters) {
        std::string cand = toUpperAscii(s.shortName);
        if (!cand.empty() && (cand.find(needle) != std::string::npos || needle.find(cand) != std::string::npos))
            return &s;
    }
    return nullptr;
}

std::string defaultTitleForType(SponsorChallengeType type)
{
    switch (type) {
    case SponsorChallengeType::kCleanSheetWin: return "SHUTOUT VICTORY";
    case SponsorChallengeType::kPlayerGoals: return "STAR STRIKER";
    case SponsorChallengeType::kFirstHalfGoals: return "FAST START";
    case SponsorChallengeType::kPossessionWin: return "DOMINANT POSSESSION";
    case SponsorChallengeType::kShotsOnTarget: return "TARGET PRACTICE";
    case SponsorChallengeType::kWinByMargin: return "STATEMENT WIN";
    case SponsorChallengeType::kTeamGoals: return "GOAL RUSH";
    case SponsorChallengeType::kLongRangeGoal: return "LONG RANGE STRIKE";
    case SponsorChallengeType::kHeaderGoal: return "AERIAL DOMINANCE";
    default: return "MATCH BONUS";
    }
}

std::string defaultDescriptionForChallenge(const SponsorChallenge& c)
{
    switch (c.type) {
    case SponsorChallengeType::kCleanSheetWin:
        return "Win the match without conceding a single goal.";
    case SponsorChallengeType::kPlayerGoals:
        if (c.thresholdValue <= 1)
            return c.targetPlayerName + " must score at least 1 goal.";
        return c.targetPlayerName + " must score at least " + std::to_string(c.thresholdValue) + " goals.";
    case SponsorChallengeType::kFirstHalfGoals:
        if (c.thresholdValue <= 1)
            return "Score at least 1 goal in the first half.";
        return "Score at least " + std::to_string(c.thresholdValue) + " goals in the first half.";
    case SponsorChallengeType::kPossessionWin:
        return "Win the match with " + std::to_string(c.thresholdValue) + "% or higher possession.";
    case SponsorChallengeType::kShotsOnTarget:
        return "Record at least " + std::to_string(c.thresholdValue) + " shots on target.";
    case SponsorChallengeType::kWinByMargin:
        return "Win the match by at least " + std::to_string(c.thresholdValue) + " goals.";
    case SponsorChallengeType::kTeamGoals:
        return "Score at least " + std::to_string(c.thresholdValue) + " goals in the match.";
    case SponsorChallengeType::kLongRangeGoal:
        if (c.thresholdValue <= 1)
            return "Score at least 1 goal from outside the penalty area.";
        return "Score at least " + std::to_string(c.thresholdValue) + " goals from outside the penalty area.";
    case SponsorChallengeType::kHeaderGoal:
        if (c.thresholdValue <= 1)
            return "Score at least 1 goal with a header.";
        return "Score at least " + std::to_string(c.thresholdValue) + " goals with a header.";
    default:
        return "Complete today's sponsor objective.";
    }
}

std::vector<SponsorStarterInfo> extractStartingXi(const TeamGame *team)
{
    std::vector<SponsorStarterInfo> starters;
    if (!team)
        return starters;

    std::vector<std::pair<std::string, int>> careerSeasonGoals;
    if (swos.g_gameType == kGameTypeCareer) {
        uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        if (numPlayers > 0 && numPlayers <= kCurrentMatchPlayersSize) {
            const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];
            bool seenSlot[kCurrentMatchPlayersSize] = {};
            for (uint16_t i = 0; i < numPlayers; ++i) {
                uint8_t slot = matchPlayers[i];
                if (slot >= kCurrentMatchPlayersSize || seenSlot[slot])
                    continue;
                seenSlot[slot] = true;
                size_t ofs = CareerOffsets::kCareerTeam +
                             HotseatInternal::kTeamFileHeaderSize +
                             static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
                if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
                    continue;
                const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
                if (pf->playerName[0] == '\0' || std::strncmp(pf->playerName, "*ERR*", 5) == 0)
                    continue;
                int g = static_cast<int>(pf->unk_2) + static_cast<int>(pf->unk_3) +
                        static_cast<int>(pf->unk_4) + static_cast<int>(pf->unk_5);
                careerSeasonGoals.emplace_back(toUpperAscii(pf->playerName), g);
            }
        }
    }

    for (int i = 1; i < kNumPlayersInLineup; ++i) {
        const auto& pl = team->players[i];
        if (pl.shortName[0] == '\0')
            continue;
        SponsorStarterInfo info;
        info.shirtNumber = pl.shirtNumber;
        info.shortName = pl.shortName;
        info.isAttacker = (pl.position == PlayerPosition::kAttacker ||
                           pl.position == PlayerPosition::kLeftWing ||
                           pl.position == PlayerPosition::kRightWing);
        info.isMidfielder = (pl.position == PlayerPosition::kMidfielder);

        std::string upperShort = toUpperAscii(info.shortName);
        for (const auto& cg : careerSeasonGoals) {
            if (!cg.first.empty() &&
                (cg.first == upperShort || cg.first.find(upperShort) != std::string::npos ||
                 upperShort.find(cg.first) != std::string::npos)) {
                info.seasonGoals = cg.second;
                break;
            }
        }
        starters.push_back(std::move(info));
    }
    return starters;
}

bool resolveHumanCareerSide(const TeamGame *topTeam, const TeamGame *bottomTeam, bool& outHumanIsTeam1)
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame)
        return false;
    if (!topTeam || !bottomTeam)
        return false;

    const TeamGeneralInfo *team1Info = nullptr;
    const TeamGeneralInfo *team2Info = nullptr;
    if (swos.topTeamData.inGameTeamPtr.asAligned() == &swos.topTeamInGame) {
        team1Info = &swos.topTeamData;
        team2Info = &swos.bottomTeamData;
    } else {
        team1Info = &swos.bottomTeamData;
        team2Info = &swos.topTeamData;
    }

    // 1. Primary: exact in-game team controls set by initializeIngameTeams()
    const TeamFile *gt1 = swos.gameTeam1.asAligned();
    const TeamFile *gt2 = swos.gameTeam2.asAligned();
    bool t1Human = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0 || team1Info->isPlCoach != 0);
    bool t2Human = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0 || team2Info->isPlCoach != 0);

    if (!t1Human && !t2Human) {
        if (gt1)
            t1Human = (gt1->teamControls != kComputerTeam && gt1->teamControls != kTeamNotSelected);
        if (gt2)
            t2Human = (gt2->teamControls != kComputerTeam && gt2->teamControls != kTeamNotSelected);
    }

    if (!t1Human && !t2Human) {
        t1Human = (swos.team1Computer == 0);
        t2Human = (swos.team2Computer == 0);
    }

    // 2. Fallback: match active career club name directly against topTeam / bottomTeam
    if (t1Human == t2Human) {
        std::string careerClub;
        const auto& mgr = HotseatCareerManager::get();
        if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
            careerClub = toUpperAscii(trimCopy(mgr.activePlayer().teamName));
        } else {
            const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
            if (hdr && hdr->teamName[0] != '\0')
                careerClub = toUpperAscii(trimCopy(hdr->teamName));
        }
        if (!careerClub.empty()) {
            std::string topName = toUpperAscii(trimCopy(topTeam->teamName));
            std::string botName = toUpperAscii(trimCopy(bottomTeam->teamName));
            bool matchTop = (topName == careerClub);
            bool matchBot = (botName == careerClub);
            if (matchTop != matchBot) {
                t1Human = matchTop;
                t2Human = matchBot;
            }
        }
    }

    if (t1Human == t2Human) {
        logInfo("Sponsor challenge skipped: ambiguous human side (t1Human=%d, t2Human=%d, top='%s', bot='%s')",
            t1Human ? 1 : 0, t2Human ? 1 : 0, topTeam->teamName, bottomTeam->teamName);
        return false;
    }

    outHumanIsTeam1 = t1Human;
    return true;
}

void refreshSponsorChallengeButtons()
{
    MenuEntry *entries[3] = { &challenge1Entry, &challenge2Entry, &challenge3Entry };
    int selectedCount = 0;
    int32_t totalStake = 0;

    for (int i = 0; i < 3; ++i) {
        const auto& c = s_state.offered[i];
        bool checked = s_state.selectedOffer[i];
        if (checked) {
            ++selectedCount;
            totalStake += c.rewardAmount;
        }
        entries[i]->setBackgroundColor(checked ? kGreen : kDarkBlue);
        std::string mark = checked ? "[X] " : "[ ] ";
        std::string label = mark + toUpperAscii(c.title) + "  +/-" + formatSponsorPounds(c.rewardAmount);
        entries[i]->copyString(label.c_str());
    }

    if (selectedCount == 0) {
        confirmBtnEntry.copyString("START (0 SELECTED)");
    } else if (selectedCount == kOfferedChallengeCount) {
        int32_t maxPayout = totalStake + (totalStake / 2);
        std::string lbl = "ALL-IN! MAX +" + formatSponsorPounds(maxPayout);
        confirmBtnEntry.copyString(lbl.c_str());
    } else {
        std::string lbl = "ACCEPT " + std::to_string(selectedCount) + " (+/-" + formatSponsorPounds(totalStake) + ")";
        confirmBtnEntry.copyString(lbl.c_str());
    }
}

void commitSelectedChallenges(bool declineAll)
{
    s_state.activeChallenges.clear();
    if (!declineAll && s_state.offersReady) {
        for (int i = 0; i < kOfferedChallengeCount; ++i) {
            if (s_state.selectedOffer[i]) {
                SponsorChallenge c = s_state.offered[i];
                c.isActive = true;
                s_state.activeChallenges.push_back(std::move(c));
            }
        }
    }

    if (s_state.activeChallenges.empty()) {
        logInfo("Sponsor challenges declined for this match.");
    } else {
        logInfo("Accepted %zu sponsor challenge(s) (all-in=%s).",
            s_state.activeChallenges.size(),
            s_state.activeChallenges.size() == kOfferedChallengeCount ? "YES (+50% jackpot)" : "NO");
    }
    SetExitMenuFlag();
}

std::string formatSignedPounds(int64_t delta)
{
    if (delta > 0)
        return "+" + formatSponsorPounds(delta);
    return formatSponsorPounds(delta);
}

} // namespace

// --- Native Menu Callbacks (referenced by sponsorChallenge.mnu) ---

static void initSponsorChallengeMenu()
{
    s_state.selectedOffer[0] = false;
    s_state.selectedOffer[1] = false;
    s_state.selectedOffer[2] = false;
    std::memset(s_prevKeyDown, 0, sizeof(s_prevKeyDown));
    refreshSponsorChallengeButtons();
    setCurrentEntry(challenge1);
}

static void drawSponsorChallengeMenu()
{
    int selectedCount = 0;
    int32_t totalStake = 0;
    for (int i = 0; i < 3; ++i) {
        if (s_state.selectedOffer[i]) {
            ++selectedCount;
            totalStake += s_state.offered[i].rewardAmount;
        }
    }

    // Bank & stake/jackpot strip (y=16..35)
    drawDarkRectangle({ 16.0f, 16.0f, 288.0f, 19.0f }, true);

    // Description panels directly under each of the 3 clickable challenge buttons
    static constexpr int kCardYs[3] = { 54, 100, 146 };
    for (int i = 0; i < 3; ++i) {
        drawDarkRectangle({ 16.0f, static_cast<float>(kCardYs[i]), 288.0f, 25.0f }, false);
    }

    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = getCompactFontAtlas(r);
    if (r && atlas) {
        float gameScale = getGameScale();
        float px = computeCompactPixelScaleForVga(gameScale, 0.44f);
        float offX = getGameScreenOffsetX();
        float offY = getGameScreenOffsetY();

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_RenderSetScale(r, 1.0f, 1.0f);

        SDL_Color whiteCol{ 244, 246, 248, 255 };
        SDL_Color yellowCol{ 255, 226, 78, 255 };
        SDL_Color greenCol{ 96, 236, 134, 255 };
        SDL_Color hintCol{ 176, 212, 236, 255 };

        if (selectedCount == kOfferedChallengeCount) {
            int32_t jackpot = totalStake / 2;
            std::string jp = "All 3 selected! +" + formatSponsorPounds(jackpot) + " Clean Sweep Bonus!";
            drawCompactText(r, atlas, offX + 22.0f * gameScale, offY + 18.0f * gameScale, px,
                jp, greenCol, 255, true);
        } else {
            std::string oppLine = "vs " + s_matchOpponentName + " (failing deducts stake!)";
            drawCompactText(r, atlas, offX + 22.0f * gameScale, offY + 18.0f * gameScale, px,
                oppLine, whiteCol, 255, true);
        }

        std::string bankStr = "Bank: " + formatSponsorPounds(readCurrentClubBalance());
        drawCompactTextRightAligned(r, atlas, offX + 298.0f * gameScale, offY + 18.0f * gameScale, px,
            bankStr, yellowCol, 255, true);

        drawCompactText(r, atlas, offX + 22.0f * gameScale, offY + 27.0f * gameScale, px,
            "Click or press 1-3 to toggle. Complete all 3 for +50% extra jackpot!", hintCol, 240, false);

        int maxDescUnits = std::max(80, static_cast<int>((276.0f * gameScale) / px));
        for (int i = 0; i < 3; ++i) {
            auto lines = wrapCompactTextUnits(s_state.offered[i].description, maxDescUnits);
            float lineY = offY + static_cast<float>(kCardYs[i] + 4) * gameScale;
            SDL_Color col = s_state.selectedOffer[i] ? yellowCol : whiteCol;
            for (size_t li = 0; li < lines.size() && li < 2; ++li) {
                drawCompactText(r, atlas, offX + 22.0f * gameScale, lineY, px,
                    lines[li], col, 255, s_state.selectedOffer[i]);
                lineY += 10.0f * px;
            }
        }

        SDL_RenderSetScale(r, origSX, origSY);
    }

    // Edge-triggered keyboard shortcuts: 1/2/3 toggle challenges, 0 declines all
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        bool down[4] = {
            keys[SDL_SCANCODE_1] || keys[SDL_SCANCODE_KP_1],
            keys[SDL_SCANCODE_2] || keys[SDL_SCANCODE_KP_2],
            keys[SDL_SCANCODE_3] || keys[SDL_SCANCODE_KP_3],
            keys[SDL_SCANCODE_0] || keys[SDL_SCANCODE_KP_0],
        };
        for (int i = 0; i < 3; ++i) {
            if (down[i] && !s_prevKeyDown[i]) {
                s_state.selectedOffer[i] = !s_state.selectedOffer[i];
                refreshSponsorChallengeButtons();
            }
        }
        if (down[3] && !s_prevKeyDown[3]) {
            commitSelectedChallenges(true);
        }
        for (int i = 0; i < 4; ++i)
            s_prevKeyDown[i] = down[i];
    }
}

static void toggleSponsorChallenge1()
{
    s_state.selectedOffer[0] = !s_state.selectedOffer[0];
    refreshSponsorChallengeButtons();
}

static void toggleSponsorChallenge2()
{
    s_state.selectedOffer[1] = !s_state.selectedOffer[1];
    refreshSponsorChallengeButtons();
}

static void toggleSponsorChallenge3()
{
    s_state.selectedOffer[2] = !s_state.selectedOffer[2];
    refreshSponsorChallengeButtons();
}

static void confirmSponsorChallenges() { commitSelectedChallenges(false); }
static void declineSponsorChallenges() { commitSelectedChallenges(true); }

// --- Public Pure Deterministic Logic ---

std::string formatSponsorPounds(int64_t amount)
{
    bool neg = amount < 0;
    uint64_t absVal = static_cast<uint64_t>(neg ? -amount : amount);
    std::string digits = std::to_string(absVal);
    std::string grouped;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count > 0 && (count % 3) == 0)
            grouped.push_back(',');
        grouped.push_back(*it);
        ++count;
    }
    std::reverse(grouped.begin(), grouped.end());
    return std::string(neg ? "-GBP " : "GBP ") + grouped;
}

int32_t clampSponsorRewardForDivision(int32_t rawReward, int divisionTier)
{
    int32_t minR = 5000;
    int32_t maxR = 18000;
    if (divisionTier <= 0) {
        minR = 15000;
        maxR = 50000;
    } else if (divisionTier == 1) {
        minR = 10000;
        maxR = 30000;
    }
    int32_t clamped = std::clamp(rawReward, minR, maxR);
    return ((clamped + 500) / 1000) * 1000;
}

int16_t clampSponsorThreshold(SponsorChallengeType type, int16_t rawThreshold)
{
    switch (type) {
    case SponsorChallengeType::kCleanSheetWin:
        return 0;
    case SponsorChallengeType::kPlayerGoals:
        return std::clamp<int16_t>(rawThreshold, 1, 2);
    case SponsorChallengeType::kFirstHalfGoals:
        return std::clamp<int16_t>(rawThreshold, 1, 3);
    case SponsorChallengeType::kPossessionWin:
        return std::clamp<int16_t>(rawThreshold, 55, 65);
    case SponsorChallengeType::kShotsOnTarget:
        return std::clamp<int16_t>(rawThreshold, 6, 12);
    case SponsorChallengeType::kWinByMargin:
        return std::clamp<int16_t>(rawThreshold, 2, 3);
    case SponsorChallengeType::kTeamGoals:
        return std::clamp<int16_t>(rawThreshold, 2, 4);
    case SponsorChallengeType::kLongRangeGoal:
        return std::clamp<int16_t>(rawThreshold, 1, 2);
    case SponsorChallengeType::kHeaderGoal:
        return std::clamp<int16_t>(rawThreshold, 1, 2);
    default:
        return 0;
    }
}

bool evaluateSponsorChallenge(const SponsorChallenge& challenge, const SponsorMatchResult& result)
{
    if (!challenge.isActive || challenge.type == SponsorChallengeType::kNone)
        return false;

    switch (challenge.type) {
    case SponsorChallengeType::kCleanSheetWin:
        return (result.clubGoals > result.opponentGoals) &&
               (result.opponentGoals <= challenge.thresholdValue);

    case SponsorChallengeType::kPlayerGoals: {
        int count = 0;
        std::string targetUpper = toUpperAscii(challenge.targetPlayerName);
        for (const auto& entry : result.scorerEntries) {
            bool shirtMatch = (challenge.targetShirtNumber > 0 && entry.first == challenge.targetShirtNumber);
            bool nameMatch = (!targetUpper.empty() && toUpperAscii(entry.second) == targetUpper);
            if (shirtMatch || nameMatch)
                ++count;
        }
        return count >= std::max<int>(1, challenge.thresholdValue);
    }

    case SponsorChallengeType::kFirstHalfGoals:
        return result.clubHtGoals >= std::max<int>(1, challenge.thresholdValue);

    case SponsorChallengeType::kPossessionWin:
        return (result.clubGoals > result.opponentGoals) &&
               (result.clubPossessionPct >= challenge.thresholdValue);

    case SponsorChallengeType::kShotsOnTarget:
        return result.clubShotsOnTarget >= std::max<int>(1, challenge.thresholdValue);

    case SponsorChallengeType::kWinByMargin:
        return (result.clubGoals - result.opponentGoals) >= std::max<int>(1, challenge.thresholdValue);

    case SponsorChallengeType::kTeamGoals:
        return result.clubGoals >= std::max<int>(1, challenge.thresholdValue);

    case SponsorChallengeType::kLongRangeGoal:
        return result.clubLongRangeGoals >= std::max<int>(1, challenge.thresholdValue);

    case SponsorChallengeType::kHeaderGoal:
        return result.clubHeaderGoals >= std::max<int>(1, challenge.thresholdValue);

    default:
        return false;
    }
}

SponsorBatchEvaluation evaluateSponsorChallengeBatch(
    const std::vector<SponsorChallenge>& activeChallenges,
    int totalOfferedCount,
    const SponsorMatchResult& result)
{
    SponsorBatchEvaluation eval;
    for (const auto& c : activeChallenges) {
        if (!c.isActive || c.type == SponsorChallengeType::kNone)
            continue;
        bool ok = evaluateSponsorChallenge(c, result);
        int32_t delta = ok ? c.rewardAmount : -c.rewardAmount;
        eval.outcomes.push_back({ c.title, c.description, c.rewardAmount, ok, delta });
        eval.baseNetDelta += delta;
        if (ok)
            ++eval.completedCount;
        else
            ++eval.failedCount;
    }

    // All-in clean sweep jackpot: ALL offered challenges taken AND ALL completed -> +50% extra bonus!
    if (totalOfferedCount > 0 &&
        static_cast<int>(eval.outcomes.size()) == totalOfferedCount &&
        eval.failedCount == 0 &&
        eval.completedCount == totalOfferedCount &&
        eval.baseNetDelta > 0) {
        eval.allInJackpotTriggered = true;
        eval.jackpotBonus = eval.baseNetDelta / 2;
    }

    eval.totalNetDelta = eval.baseNetDelta + eval.jackpotBonus;
    return eval;
}

std::vector<SponsorChallenge> generateOfflineSponsorChallenges(
    const std::string& clubName, const std::string& opponentName,
    int divisionTier, const std::vector<SponsorStarterInfo>& starters, uint32_t seed)
{
    (void)clubName;
    int32_t baseLow = (divisionTier <= 0) ? 15000 : (divisionTier == 1 ? 10000 : 5000);
    int32_t baseMid = (divisionTier <= 0) ? 25000 : (divisionTier == 1 ? 18000 : 10000);
    int32_t baseHigh = (divisionTier <= 0) ? 40000 : (divisionTier == 1 ? 26000 : 15000);

    std::vector<SponsorChallenge> out;

    SponsorChallenge c1;
    uint32_t pick1 = seed % 3u;
    if (pick1 == 0u) {
        c1.type = SponsorChallengeType::kCleanSheetWin;
        c1.title = "SHUTOUT VICTORY";
        c1.thresholdValue = 0;
        c1.rewardAmount = clampSponsorRewardForDivision(baseLow + 3000, divisionTier);
        c1.description = "Beat " + opponentName + " without conceding a single goal.";
    } else if (pick1 == 1u) {
        c1.type = SponsorChallengeType::kTeamGoals;
        c1.title = "GOAL RUSH";
        c1.thresholdValue = clampSponsorThreshold(c1.type, static_cast<int16_t>(2 + ((seed / 3u) % 2u)));
        c1.rewardAmount = clampSponsorRewardForDivision(baseLow + (c1.thresholdValue >= 3 ? 8000 : 3000), divisionTier);
        c1.description = defaultDescriptionForChallenge(c1);
    } else {
        c1.type = SponsorChallengeType::kFirstHalfGoals;
        c1.title = "BLITZKRIEG HALF";
        c1.thresholdValue = clampSponsorThreshold(c1.type, static_cast<int16_t>(1 + ((seed / 2u) % 2u)));
        c1.rewardAmount = clampSponsorRewardForDivision(baseLow + (c1.thresholdValue == 2 ? 7000 : 2000), divisionTier);
        c1.description = defaultDescriptionForChallenge(c1);
    }
    out.push_back(std::move(c1));

    const SponsorStarterInfo *star = pickBestScorerStarter(starters);
    SponsorChallenge c2;
    uint32_t pick2 = (seed / 2u) % 3u;
    if (pick2 == 0u && star) {
        c2.type = SponsorChallengeType::kPlayerGoals;
        c2.title = "STAR STRIKER";
        c2.targetShirtNumber = static_cast<int16_t>(star->shirtNumber);
        c2.targetPlayerName = star->shortName;
        c2.thresholdValue = (star->seasonGoals >= 6 || (seed % 3u) == 0u) ? 2 : 1;
        c2.rewardAmount = clampSponsorRewardForDivision(
            c2.thresholdValue >= 2 ? (baseMid + 6000) : baseMid, divisionTier);
        c2.description = defaultDescriptionForChallenge(c2);
    } else if (pick2 == 1u) {
        c2.type = SponsorChallengeType::kHeaderGoal;
        c2.title = "AERIAL DOMINANCE";
        c2.thresholdValue = 1;
        c2.rewardAmount = clampSponsorRewardForDivision(baseMid + 4000, divisionTier);
        c2.description = defaultDescriptionForChallenge(c2);
    } else {
        c2.type = SponsorChallengeType::kLongRangeGoal;
        c2.title = "LONG RANGE STRIKE";
        c2.thresholdValue = 1;
        c2.rewardAmount = clampSponsorRewardForDivision(baseMid + 5000, divisionTier);
        c2.description = defaultDescriptionForChallenge(c2);
    }
    out.push_back(std::move(c2));

    SponsorChallenge c3;
    uint32_t pick3 = (seed / 3u) % 3u;
    if (pick3 == 0u) {
        c3.type = SponsorChallengeType::kPossessionWin;
        c3.title = "TOTAL FOOTBALL";
        c3.thresholdValue = 60;
        c3.rewardAmount = clampSponsorRewardForDivision(baseHigh, divisionTier);
        c3.description = defaultDescriptionForChallenge(c3);
    } else if (pick3 == 1u) {
        c3.type = SponsorChallengeType::kWinByMargin;
        c3.title = "STATEMENT WIN";
        c3.thresholdValue = 2;
        c3.rewardAmount = clampSponsorRewardForDivision(baseHigh, divisionTier);
        c3.description = "Beat " + opponentName + " by at least 2 clear goals.";
    } else {
        c3.type = SponsorChallengeType::kShotsOnTarget;
        c3.title = "RELENTLESS ATTACK";
        c3.thresholdValue = 8;
        c3.rewardAmount = clampSponsorRewardForDivision(baseHigh - 4000, divisionTier);
        c3.description = defaultDescriptionForChallenge(c3);
    }
    out.push_back(std::move(c3));

    return out;
}

std::vector<SponsorChallenge> parseLlmSponsorChallenges(
    const std::string& rawReply, const std::string& clubName, const std::string& opponentName,
    int divisionTier, const std::vector<SponsorStarterInfo>& starters, uint32_t seed)
{
    std::vector<SponsorChallenge> parsed;
    auto hasType = [&](SponsorChallengeType t) {
        for (const auto& c : parsed) {
            if (c.type == t)
                return true;
        }
        return false;
    };

    std::istringstream stream(rawReply);
    std::string line;
    while (std::getline(stream, line) && parsed.size() < 3) {
        line = trimCopy(line);
        if (line.empty())
            continue;
        if (line.find('|') == std::string::npos)
            continue;

        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos <= line.size()) {
            size_t sep = line.find('|', pos);
            parts.push_back(trimCopy(line.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos)));
            if (sep == std::string::npos)
                break;
            pos = sep + 1;
        }
        if (parts.size() < 6)
            continue;

        SponsorChallengeType type = parseChallengeTypeString(parts[0]);
        if (type == SponsorChallengeType::kNone || hasType(type))
            continue;

        SponsorChallenge c;
        c.type = type;
        c.title = sanitizePlain(parts[1], 24);
        if (c.title.empty())
            c.title = defaultTitleForType(type);
        c.description = sanitizePlain(parts[2], 96);

        int rawThresh = std::atoi(parts[4].c_str());
        c.thresholdValue = clampSponsorThreshold(type, static_cast<int16_t>(rawThresh));

        std::string digitsOnly;
        for (char ch : parts[5]) {
            if (std::isdigit(static_cast<unsigned char>(ch)))
                digitsOnly.push_back(ch);
        }
        int32_t rawReward = digitsOnly.empty() ? 15000 : static_cast<int32_t>(std::strtol(digitsOnly.c_str(), nullptr, 10));
        c.rewardAmount = clampSponsorRewardForDivision(rawReward, divisionTier);

        if (type == SponsorChallengeType::kPlayerGoals) {
            const SponsorStarterInfo *matched = matchStarterByName(starters, parts[3]);
            if (!matched)
                matched = pickBestScorerStarter(starters);
            if (!matched)
                continue;
            c.targetShirtNumber = static_cast<int16_t>(matched->shirtNumber);
            c.targetPlayerName = matched->shortName;
            if (toUpperAscii(c.description).find(toUpperAscii(c.targetPlayerName)) == std::string::npos)
                c.description = defaultDescriptionForChallenge(c);
        }

        if (c.description.empty())
            c.description = defaultDescriptionForChallenge(c);

        parsed.push_back(std::move(c));
    }

    if (parsed.size() < 3) {
        auto fallback = generateOfflineSponsorChallenges(clubName, opponentName, divisionTier, starters, seed);
        for (auto& fb : fallback) {
            if (parsed.size() >= 3)
                break;
            if (!hasType(fb.type))
                parsed.push_back(std::move(fb));
        }
        static constexpr SponsorChallengeType kAllTypes[] = {
            SponsorChallengeType::kTeamGoals,
            SponsorChallengeType::kLongRangeGoal,
            SponsorChallengeType::kHeaderGoal,
            SponsorChallengeType::kCleanSheetWin,
            SponsorChallengeType::kFirstHalfGoals,
            SponsorChallengeType::kPossessionWin,
            SponsorChallengeType::kShotsOnTarget,
            SponsorChallengeType::kWinByMargin,
        };
        for (auto t : kAllTypes) {
            if (parsed.size() >= 3)
                break;
            if (!hasType(t)) {
                SponsorChallenge c;
                c.type = t;
                c.title = defaultTitleForType(t);
                int16_t defThresh = (t == SponsorChallengeType::kPossessionWin) ? 58 :
                                    (t == SponsorChallengeType::kShotsOnTarget) ? 7 :
                                    (t == SponsorChallengeType::kLongRangeGoal || t == SponsorChallengeType::kHeaderGoal) ? 1 : 2;
                c.thresholdValue = clampSponsorThreshold(t, defThresh);
                c.rewardAmount = clampSponsorRewardForDivision(20000, divisionTier);
                c.description = defaultDescriptionForChallenge(c);
                parsed.push_back(std::move(c));
            }
        }
    }

    return parsed;
}

// --- Career Lifecycle & UI Hooks ---

void resetSponsorChallengeState()
{
    s_state = CareerChallengeState{};
    s_pendingRequestId = 0;
    s_matchEligible = false;
    s_matchStarters.clear();
}

void beginSponsorChallengeGeneration(const TeamGame *topTeam, const TeamGame *bottomTeam)
{
    s_pendingRequestId = 0;
    s_matchEligible = false;
    s_state.offersReady = false;
    s_state.activeChallenges.clear();
    s_state.lastMatchHadChallenge = false;
    s_state.lastMatchEval = SponsorBatchEvaluation{};

    const auto& cfg = getLlmConfig();
    if (!cfg.features.sponsorChallenges)
        return;

    bool humanIsTeam1 = true;
    if (!resolveHumanCareerSide(topTeam, bottomTeam, humanIsTeam1))
        return;

    syncTotalsFromHotseatIfActive();

    s_matchEligible = true;
    s_humanIsTeam1 = humanIsTeam1;

    const TeamGame *humanTeam = humanIsTeam1 ? topTeam : bottomTeam;
    const TeamGame *oppTeam = humanIsTeam1 ? bottomTeam : topTeam;
    s_matchClubName = humanTeam->teamName[0] ? humanTeam->teamName : "CLUB";
    s_matchOpponentName = oppTeam->teamName[0] ? oppTeam->teamName : "OPPONENT";
    s_matchDivisionTier = readDivisionTier();
    s_matchStarters = extractStartingXi(humanTeam);
    s_matchSeed = static_cast<uint32_t>(SDL_GetTicks() ^ (s_state.challengesCompleted * 31u + s_state.challengesFailed * 17u + 7u));

    auto offline = generateOfflineSponsorChallenges(
        s_matchClubName, s_matchOpponentName, s_matchDivisionTier, s_matchStarters, s_matchSeed);
    for (size_t i = 0; i < 3 && i < offline.size(); ++i)
        s_state.offered[i] = offline[i];
    s_state.offersReady = true;

    if (!cfg.enabled)
        return;

    std::string starterList;
    for (const auto& s : s_matchStarters) {
        if (!starterList.empty())
            starterList += ", ";
        starterList += s.shortName + (s.isAttacker ? " (ATT)" : (s.isMidfielder ? " (MID)" : " (DEF)"));
        if (s.seasonGoals > 0)
            starterList += " [" + std::to_string(s.seasonGoals) + "G]";
    }

    int32_t minR = (s_matchDivisionTier <= 0) ? 15000 : (s_matchDivisionTier == 1 ? 10000 : 5000);
    int32_t maxR = (s_matchDivisionTier <= 0) ? 50000 : (s_matchDivisionTier == 1 ? 30000 : 18000);

    static const char *const kFeaturedTrios[6] = {
        "CHALLENGE_TEAM_GOALS, CHALLENGE_LONG_RANGE_GOAL, CHALLENGE_CLEAN_SHEET_WIN",
        "CHALLENGE_HEADER_GOAL, CHALLENGE_PLAYER_GOALS, CHALLENGE_POSSESSION_WIN",
        "CHALLENGE_LONG_RANGE_GOAL, CHALLENGE_FIRST_HALF_GOALS, CHALLENGE_WIN_BY_MARGIN",
        "CHALLENGE_HEADER_GOAL, CHALLENGE_TEAM_GOALS, CHALLENGE_SHOTS_ON_TARGET",
        "CHALLENGE_TEAM_GOALS, CHALLENGE_HEADER_GOAL, CHALLENGE_CLEAN_SHEET_WIN",
        "CHALLENGE_LONG_RANGE_GOAL, CHALLENGE_PLAYER_GOALS, CHALLENGE_TEAM_GOALS",
    };
    const char *suggestedMix = kFeaturedTrios[s_matchSeed % 6u];

    std::string prompt = "Club: " + s_matchClubName + " (Division tier " + std::to_string(s_matchDivisionTier + 1) +
                         ", Bank: " + formatSponsorPounds(readCurrentClubBalance()) + ").\n" +
                         "Opponent: " + s_matchOpponentName + ".\n" +
                         "Starting outfield XI: " + starterList + ".\n" +
                         "Create 3 distinct 1997 matchday sponsor bonus challenges for " + s_matchClubName + ".\n" +
                         "Allowed TYPE values (pick 3 DIFFERENT types; for today's variety prefer using: " + suggestedMix + "):\n" +
                         "- CHALLENGE_TEAM_GOALS (club scores at least X goals in the match; threshold 2, 3, or 4)\n" +
                         "- CHALLENGE_LONG_RANGE_GOAL (score from outside the penalty area; threshold 1)\n" +
                         "- CHALLENGE_HEADER_GOAL (score a goal with a header; threshold 1)\n" +
                         "- CHALLENGE_CLEAN_SHEET_WIN (win without conceding; threshold 0)\n" +
                         "- CHALLENGE_PLAYER_GOALS (threshold 1 or 2, PLAYER_NAME must be an exact surname from Starting outfield XI)\n" +
                         "- CHALLENGE_FIRST_HALF_GOALS (threshold 1, 2, or 3)\n" +
                         "- CHALLENGE_POSSESSION_WIN (threshold 55 to 65)\n" +
                         "- CHALLENGE_SHOTS_ON_TARGET (threshold 6 to 12)\n" +
                         "- CHALLENGE_WIN_BY_MARGIN (threshold 2 or 3)\n" +
                         "Scale REWARD between " + std::to_string(minR) + " and " + std::to_string(maxR) + " (integer pounds).\n" +
                         "Format STRICTLY one challenge per line as:\n" +
                         "TYPE|TITLE|DESCRIPTION|PLAYER_NAME|THRESHOLD|REWARD\n" +
                         "- TITLE: punchy 2-3 word 1997 sponsor challenge name (max 20 chars)\n" +
                         "- DESCRIPTION: crisp 1-sentence objective matching the exact threshold/player (max 85 chars)\n" +
                         "- PLAYER_NAME: exact player surname for CHALLENGE_PLAYER_GOALS, otherwise NONE\n" +
                         "Output ONLY the 3 lines. No intro, no markdown, no numbering.";

    s_pendingRequestId = enqueueLlmRequest(
        "You are a 1997 football commercial sponsor offering matchday bonus challenges to a club manager. "
        "Output strictly 3 pipe-delimited lines in TYPE|TITLE|DESCRIPTION|PLAYER_NAME|THRESHOLD|REWARD format.",
        prompt);
}

void showPreMatchSponsorChallengeMenuIfReady()
{
    if (!s_matchEligible || !s_state.offersReady)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.sponsorChallenges)
        return;

    if (s_pendingRequestId != 0) {
        std::string reply;
        if (!tryTakeLlmResponse(s_pendingRequestId, reply)) {
            waitForLlmResponse(s_pendingRequestId, std::min(cfg.timeoutMs, 3500), reply);
        }
        s_pendingRequestId = 0;
        if (!reply.empty()) {
            auto parsed = parseLlmSponsorChallenges(
                reply, s_matchClubName, s_matchOpponentName, s_matchDivisionTier, s_matchStarters, s_matchSeed);
            for (size_t i = 0; i < 3 && i < parsed.size(); ++i)
                s_state.offered[i] = parsed[i];
        }
    }

#ifndef SWOS_TEST
    setStandardMenuBackgroundImage();
    menuFadeIn();
    showMenu(sponsorChallengeMenu);
    menuFadeOut();
#endif
}

void onMatchFinishedSponsorChallenge()
{
    s_state.lastMatchHadChallenge = false;
    if (!s_matchEligible || s_state.activeChallenges.empty() || swos.gameCanceled || swos.g_trainingGame)
        return;

    MatchTelemetrySnapshot snap = captureMatchTelemetrySnapshot();
    const TeamGame& humanTg = s_humanIsTeam1 ? swos.topTeamInGame : swos.bottomTeamInGame;

    SponsorMatchResult result;
    result.clubGoals = s_humanIsTeam1 ? snap.goals1 : snap.goals2;
    result.opponentGoals = s_humanIsTeam1 ? snap.goals2 : snap.goals1;
    result.clubHtGoals = s_humanIsTeam1 ? snap.htGoals1 : snap.htGoals2;
    result.opponentHtGoals = s_humanIsTeam1 ? snap.htGoals2 : snap.htGoals1;
    result.clubPossessionPct = s_humanIsTeam1 ? snap.possession1 : snap.possession2;
    result.clubShotsOnTarget = s_humanIsTeam1 ? snap.onTarget1 : snap.onTarget2;
    result.clubLongRangeGoals = s_humanIsTeam1 ? snap.longRangeGoals1 : snap.longRangeGoals2;
    result.clubHeaderGoals = s_humanIsTeam1 ? snap.headerGoals1 : snap.headerGoals2;

    for (int i = 0; i < kNumPlayersInTeam; ++i) {
        const auto& pl = humanTg.players[i];
        for (int g = 0; g < static_cast<int>(pl.goalsScored); ++g) {
            result.scorerEntries.emplace_back(static_cast<int>(pl.shirtNumber), std::string(pl.shortName));
        }
    }

    SponsorBatchEvaluation eval = evaluateSponsorChallengeBatch(
        s_state.activeChallenges, kOfferedChallengeCount, result);

    syncTotalsFromHotseatIfActive();
    s_state.lastMatchHadChallenge = !eval.outcomes.empty();
    s_state.lastMatchEval = eval;

    applyClubBalanceDelta(eval.totalNetDelta);
    s_state.challengesCompleted = static_cast<uint16_t>(s_state.challengesCompleted + eval.completedCount);
    s_state.challengesFailed = static_cast<uint16_t>(s_state.challengesFailed + eval.failedCount);
    s_state.totalBonusEarned += eval.totalNetDelta;
    s_state.lastMatchNewBalance = readCurrentClubBalance();
    syncTotalsToHotseatIfActive();

    logInfo("Sponsor challenges resolved: %d won, %d failed, jackpot=%s, net=%s, new balance=%s",
        eval.completedCount, eval.failedCount,
        eval.allInJackpotTriggered ? "YES" : "NO",
        formatSignedPounds(eval.totalNetDelta).c_str(),
        formatSponsorPounds(s_state.lastMatchNewBalance).c_str());

    s_state.activeChallenges.clear();
}

void showPostMatchSponsorPayoutIfNeeded()
{
    if (!s_state.lastMatchHadChallenge || swos.gameCanceled || swos.g_trainingGame)
        return;

#ifndef SWOS_TEST
    const auto& eval = s_state.lastMatchEval;
    std::string headline;
    if (eval.allInJackpotTriggered) {
        headline = "ALL-IN CLEAN SWEEP! +50% JACKPOT WON!";
    } else if (eval.totalNetDelta > 0) {
        headline = "SPONSOR CHALLENGES: NET PROFIT " + formatSignedPounds(eval.totalNetDelta);
    } else if (eval.totalNetDelta < 0) {
        headline = "SPONSOR CHALLENGES: NET PENALTY " + formatSignedPounds(eval.totalNetDelta);
    } else {
        headline = "SPONSOR CHALLENGES: BROKE EVEN (GBP 0)";
    }

    std::string body = headline + "\n\n";
    for (const auto& o : eval.outcomes) {
        body += std::string(o.completed ? "[WON] " : "[FAILED] ") +
                toUpperAscii(o.title) + ": " + formatSignedPounds(o.deltaCash) + "\n";
        body += "  " + o.description + "\n";
    }
    if (eval.allInJackpotTriggered) {
        body += "\nALL-IN CLEAN SWEEP BONUS (+50%): " + formatSignedPounds(eval.jackpotBonus) + "\n";
    }
    body += "\nNET MATCHDAY SPONSOR P/L: " + formatSignedPounds(eval.totalNetDelta) + "\n";
    body += "UPDATED BANK BALANCE: " + formatSponsorPounds(s_state.lastMatchNewBalance) + "\n\n";
    body += "CAREER SPONSOR RECORD: " + std::to_string(s_state.challengesCompleted) + " WON / " +
            std::to_string(s_state.challengesFailed) + " FAILED  (NET: " +
            formatSignedPounds(s_state.totalBonusEarned) + ")";

    setStandardMenuBackgroundImage();
    menuFadeIn();
    showLlmReportDialog("POST-MATCH SPONSOR REPORT", body);
    menuFadeOut();
#endif
}

// --- Live Telemetry, HUD & Newspaper Integration ---

bool hasActiveSponsorChallenge()
{
    return s_matchEligible && !s_state.activeChallenges.empty() && !swos.g_trainingGame;
}

static std::string describeSingleChallengeProgress(const SponsorChallenge& c, int& outColorMode)
{
    outColorMode = 0; // 0 = gold/live, 1 = green/achieved, 2 = red/failed
    const auto stats = getStats();
    int clubG = s_humanIsTeam1 ? swos.statsTeam1Goals : swos.statsTeam2Goals;
    int oppG = s_humanIsTeam1 ? swos.statsTeam2Goals : swos.statsTeam1Goals;
    int totalPoss = stats.team1.ballPossession + stats.team2.ballPossession;
    int poss1Pct = totalPoss > 0 ? (100 * stats.team1.ballPossession) / totalPoss : 50;
    int poss = s_humanIsTeam1 ? poss1Pct : (100 - poss1Pct);
    int sot = s_humanIsTeam1 ? stats.team1.onTarget : stats.team2.onTarget;

    switch (c.type) {
    case SponsorChallengeType::kCleanSheetWin:
        if (oppG > 0) {
            outColorMode = 2;
            return "LOST";
        }
        if (clubG > oppG) {
            outColorMode = 1;
            return "ON TRACK";
        }
        return "0-0";
    case SponsorChallengeType::kPlayerGoals: {
        const TeamGame& tg = s_humanIsTeam1 ? swos.topTeamInGame : swos.bottomTeamInGame;
        int scored = 0;
        std::string targetUpper = toUpperAscii(c.targetPlayerName);
        for (int i = 0; i < kNumPlayersInTeam; ++i) {
            if ((c.targetShirtNumber > 0 && tg.players[i].shirtNumber == c.targetShirtNumber) ||
                (!targetUpper.empty() && toUpperAscii(tg.players[i].shortName) == targetUpper)) {
                scored += tg.players[i].goalsScored;
            }
        }
        if (scored >= c.thresholdValue)
            outColorMode = 1;
        return std::to_string(scored) + "/" + std::to_string(c.thresholdValue);
    }
    case SponsorChallengeType::kFirstHalfGoals:
        if (swos.halfNumber <= 1) {
            if (clubG >= c.thresholdValue)
                outColorMode = 1;
            return "HT " + std::to_string(clubG) + "/" + std::to_string(c.thresholdValue);
        } else {
            auto [ht1, ht2] = getHalfTimeTelemetryGoals();
            int htG = s_humanIsTeam1 ? ht1 : ht2;
            if (htG >= c.thresholdValue) {
                outColorMode = 1;
                return "HT WON";
            }
            outColorMode = 2;
            return "HT LOST";
        }
    case SponsorChallengeType::kPossessionWin:
        if (clubG > oppG && poss >= c.thresholdValue)
            outColorMode = 1;
        return std::to_string(poss) + "% (" + std::to_string(c.thresholdValue) + "%+)";
    case SponsorChallengeType::kShotsOnTarget:
        if (sot >= c.thresholdValue)
            outColorMode = 1;
        return std::to_string(sot) + "/" + std::to_string(c.thresholdValue) + " SOT";
    case SponsorChallengeType::kWinByMargin: {
        int diff = clubG - oppG;
        if (diff >= c.thresholdValue)
            outColorMode = 1;
        return (diff >= 0 ? "+" : "") + std::to_string(diff) + "/+" + std::to_string(c.thresholdValue);
    }
    case SponsorChallengeType::kTeamGoals:
        if (clubG >= c.thresholdValue)
            outColorMode = 1;
        return std::to_string(clubG) + "/" + std::to_string(c.thresholdValue) + " G";
    case SponsorChallengeType::kLongRangeGoal: {
        auto [lr1, lr2] = getLongRangeTelemetryGoals();
        int lrG = s_humanIsTeam1 ? lr1 : lr2;
        if (lrG >= c.thresholdValue)
            outColorMode = 1;
        return std::to_string(lrG) + "/" + std::to_string(c.thresholdValue) + " LONG";
    }
    case SponsorChallengeType::kHeaderGoal: {
        auto [hd1, hd2] = getHeaderTelemetryGoals();
        int hdG = s_humanIsTeam1 ? hd1 : hd2;
        if (hdG >= c.thresholdValue)
            outColorMode = 1;
        return std::to_string(hdG) + "/" + std::to_string(c.thresholdValue) + " HEAD";
    }
    default:
        return "ACTIVE";
    }
}

std::string formatActiveSponsorChallengeForTelemetry()
{
    if (!hasActiveSponsorChallenge())
        return {};

    bool allIn = (static_cast<int>(s_state.activeChallenges.size()) == kOfferedChallengeCount);
    std::string out = "Active Sponsor Challenges for " + s_matchClubName + " (failing deducts the stake";
    if (allIn)
        out += "; ALL 3 TAKEN - winning all 3 pays a +50% CLEAN SWEEP JACKPOT!";
    out += "):";

    for (const auto& c : s_state.activeChallenges) {
        int mode = 0;
        std::string status = describeSingleChallengeProgress(c, mode);
        out += " [" + c.title + ": " + c.description + " Stake +/-" + formatSponsorPounds(c.rewardAmount) +
               " -> " + status + "]";
    }
    return out;
}

std::vector<std::pair<std::string, int>> formatActiveSponsorChallengeHudLines()
{
    std::vector<std::pair<std::string, int>> lines;
    if (!hasActiveSponsorChallenge())
        return lines;

    bool allIn = (static_cast<int>(s_state.activeChallenges.size()) == kOfferedChallengeCount);
    int allGreen = 0;
    bool anyRed = false;

    constexpr int kMaxHudLineUnits = 148;
    for (const auto& c : s_state.activeChallenges) {
        int mode = 0;
        std::string status = describeSingleChallengeProgress(c, mode);
        if (mode == 1) ++allGreen;
        if (mode == 2) anyRed = true;
        int32_t kStake = c.rewardAmount / 1000;
        std::string suffix = " [" + status + "] +/-" + std::to_string(kStake) + "K";
        std::string titleShort = c.title;
        while (!titleShort.empty() && measureCompactTextUnits(titleShort + suffix) > kMaxHudLineUnits) {
            titleShort.pop_back();
            while (!titleShort.empty() && titleShort.back() == ' ')
                titleShort.pop_back();
        }
        std::string text = titleShort.empty() ? ("[" + status + "] +/-" + std::to_string(kStake) + "K") : (titleShort + suffix);
        lines.emplace_back(std::move(text), mode);
    }

    if (allIn) {
        int jpMode = anyRed ? 2 : (allGreen == kOfferedChallengeCount ? 1 : 0);
        std::string jpText = anyRed ? "ALL-IN +50% JACKPOT: LOST" :
                             (allGreen == kOfferedChallengeCount ? "ALL-IN +50% JACKPOT: ON TRACK!" : "ALL-IN +50% JACKPOT ACTIVE");
        lines.emplace_back(std::move(jpText), jpMode);
    }
    return lines;
}

std::string formatLastSponsorChallengeOutcomeForNewspaper()
{
    if (!s_state.lastMatchHadChallenge)
        return {};
    const auto& eval = s_state.lastMatchEval;
    std::string out = "Sponsor Challenges Outcome for " + s_matchClubName + ": " +
                      std::to_string(eval.completedCount) + " completed, " +
                      std::to_string(eval.failedCount) + " failed";
    if (eval.allInJackpotTriggered) {
        out += " — ALL-IN CLEAN SWEEP! Won an extra +50% jackpot bonus (" +
               formatSignedPounds(eval.jackpotBonus) + ")";
    }
    out += ". Net financial impact: " + formatSignedPounds(eval.totalNetDelta) + ".";
    return out;
}

// --- Persistence (Single-player sidecar .sch + Hotseat getters/setters) ---

bool saveSponsorChallengeSidecar(const std::string& path)
{
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.SetLongValue("sponsor", "challengesCompleted", static_cast<long>(s_state.challengesCompleted));
    ini.SetLongValue("sponsor", "challengesFailed", static_cast<long>(s_state.challengesFailed));
    std::string bonusStr = std::to_string(s_state.totalBonusEarned);
    ini.SetValue("sponsor", "totalBonusEarned", bonusStr.c_str());
    return ini.SaveFile(path.c_str()) >= 0;
}

bool loadSponsorChallengeSidecar(const std::string& path)
{
    CSimpleIniA ini;
    ini.SetUnicode();
    if (ini.LoadFile(path.c_str()) < 0)
        return false;

    s_state.challengesCompleted = static_cast<uint16_t>(std::max(0L, ini.GetLongValue("sponsor", "challengesCompleted", 0)));
    s_state.challengesFailed = static_cast<uint16_t>(std::max(0L, ini.GetLongValue("sponsor", "challengesFailed", 0)));
    const char *bonusVal = ini.GetValue("sponsor", "totalBonusEarned", "0");
    s_state.totalBonusEarned = bonusVal ? std::strtoll(bonusVal, nullptr, 10) : 0;
    return true;
}

void getSponsorCareerTotals(uint16_t& outCompleted, uint16_t& outFailed, int64_t& outTotalBonus)
{
    syncTotalsFromHotseatIfActive();
    outCompleted = s_state.challengesCompleted;
    outFailed = s_state.challengesFailed;
    outTotalBonus = s_state.totalBonusEarned;
}

void setSponsorCareerTotals(uint16_t completed, uint16_t failed, int64_t totalBonus)
{
    s_state.challengesCompleted = completed;
    s_state.challengesFailed = failed;
    s_state.totalBonusEarned = totalBonus;
    syncTotalsToHotseatIfActive();
}
