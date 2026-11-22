#include "sponsorChallenge.h"
#include "controls.h"
#include "darkRectangle.h"
#include "drawMenu.h"
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

CareerChallengeState s_state;
uint64_t s_pendingRequestId = 0;
bool s_matchEligible = false;
bool s_humanIsTeam1 = true;
std::string s_matchClubName;
std::string s_matchOpponentName;
int s_matchDivisionTier = 0;
std::vector<SponsorStarterInfo> s_matchStarters;
uint32_t s_matchSeed = 1;

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
    std::string out;
    out.reserve(std::min(in.size(), maxLen));
    bool lastSpace = false;
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(in[i]);
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            if (!lastSpace && !out.empty()) {
                out.push_back(' ');
                lastSpace = true;
            }
            continue;
        }
        if (ch == '*' || ch == '`' || ch == '"')
            continue;
        if (ch >= 0x20 && ch <= 0x7E) {
            if (ch == ' ') {
                if (!lastSpace && !out.empty()) {
                    out.push_back(' ');
                    lastSpace = true;
                }
            } else {
                out.push_back(static_cast<char>(ch));
                lastSpace = false;
            }
        }
        if (out.size() >= maxLen)
            break;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

int32_t readCurrentClubBalance()
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers())
        return mgr.activePlayer().newBalance;
    return *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
}

void creditClubBalance(int32_t amount)
{
    if (amount <= 0)
        return;

    auto *vmBal = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    auto *vmSpon = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kSensisoftPlcSponsorship]);
    *vmBal += amount;
    *vmSpon += amount;

    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        auto& p = mgr.activePlayer();
        p.newBalance += amount;
        p.sensisoftPlcSponsorship += amount;
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
    default:
        return "Complete today's sponsor objective.";
    }
}

std::vector<SponsorStarterInfo> extractStartingXi(const TeamGame *team)
{
    std::vector<SponsorStarterInfo> starters;
    if (!team)
        return starters;

    // Gather season goals from CareerOffsets::kCareerTeam by matching player names
    std::vector<std::pair<std::string, int>> careerSeasonGoals;
    if (swos.g_gameType == kGameTypeCareer) {
        uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        if (numPlayers > 0 && numPlayers <= kMaxCareerSquadPlayers) {
            for (uint16_t i = 0; i < numPlayers; ++i) {
                size_t ofs = CareerOffsets::kCareerTeam +
                             HotseatInternal::kTeamFileHeaderSize +
                             static_cast<size_t>(i) * HotseatInternal::kPlayerFileSize;
                const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
                if (pf->playerName[0] == '\0')
                    continue;
                int g = static_cast<int>(pf->unk_2) + static_cast<int>(pf->unk_3) +
                        static_cast<int>(pf->unk_4) + static_cast<int>(pf->unk_5);
                careerSeasonGoals.emplace_back(toUpperAscii(pf->playerName), g);
            }
        }
    }

    // Outfield starting XI: indices 1..10 (index 0 is GK)
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

    bool t1Human = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0);
    bool t2Human = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0);

    const TeamFile *tf1 = swos.playMatchTeam1Ptr.asAligned();
    const TeamFile *tf2 = swos.playMatchTeam2Ptr.asAligned();
    if (tf1 && !t1Human)
        t1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
    if (tf2 && !t2Human)
        t2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);

    // Skip CPU-vs-CPU or 2-human head-to-head matches for unambiguous single-club payout
    if (t1Human == t2Human)
        return false;

    outHumanIsTeam1 = t1Human;
    return true;
}

void finalizeSelection(int index)
{
    if (index >= 0 && index < 3 && s_state.offersReady) {
        s_state.activeChallenge = s_state.offered[index];
        s_state.activeChallenge.isActive = true;
        logInfo("Sponsor challenge accepted: %s (+%s)",
            s_state.activeChallenge.title.c_str(),
            formatSponsorPounds(s_state.activeChallenge.rewardAmount).c_str());
    } else {
        s_state.activeChallenge = SponsorChallenge{};
        logInfo("Sponsor challenges declined for this match.");
    }
    SetExitMenuFlag();
}

} // namespace

// --- Native Menu Callbacks (referenced by sponsorChallenge.mnu) ---

static void initSponsorChallengeMenu()
{
    MenuEntry *entries[3] = { &challenge1Entry, &challenge2Entry, &challenge3Entry };
    for (int i = 0; i < 3; ++i) {
        const auto& c = s_state.offered[i];
        std::string label = "[" + std::to_string(i + 1) + "] " + toUpperAscii(c.title) +
                            "   [+" + formatSponsorPounds(c.rewardAmount) + "]";
        entries[i]->copyString(label.c_str());
    }
    setCurrentEntry(challenge1);
}

static void drawSponsorChallengeMenu()
{
    // Bank & match subtitle strip (y=17..34)
    drawDarkRectangle({ 16.0f, 17.0f, 288.0f, 17.0f }, true);
    std::string oppLine = "VS " + toUpperAscii(s_matchOpponentName);
    if (oppLine.size() > 22)
        oppLine.resize(22);
    drawText(22, 22, oppLine.c_str(), 150, kWhiteText2, false);

    std::string bankStr = "BANK: " + formatSponsorPounds(readCurrentClubBalance());
    drawTextRightAligned(298, 22, bankStr.c_str(), 130, kYellowText, false);

    // Description panels directly under each of the 3 clickable challenge buttons
    static constexpr int kCardYs[3] = { 54, 100, 146 };
    for (int i = 0; i < 3; ++i) {
        int y = kCardYs[i];
        drawDarkRectangle({ 16.0f, static_cast<float>(y), 288.0f, 25.0f }, false);
        auto lines = wrapLlmReportLines(s_state.offered[i].description, 274);
        int lineY = y + 4;
        for (size_t li = 0; li < lines.size() && li < 2; ++li) {
            drawText(22, lineY, lines[li].c_str(), 274, kWhiteText2, false);
            lineY += 9;
        }
    }

    // Quick keyboard shortcuts 1 / 2 / 3 / 0
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        if (keys[SDL_SCANCODE_1] || keys[SDL_SCANCODE_KP_1])
            finalizeSelection(0);
        else if (keys[SDL_SCANCODE_2] || keys[SDL_SCANCODE_KP_2])
            finalizeSelection(1);
        else if (keys[SDL_SCANCODE_3] || keys[SDL_SCANCODE_KP_3])
            finalizeSelection(2);
        else if (keys[SDL_SCANCODE_0] || keys[SDL_SCANCODE_KP_0])
            finalizeSelection(-1);
    }
}

static void selectSponsorChallenge1() { finalizeSelection(0); }
static void selectSponsorChallenge2() { finalizeSelection(1); }
static void selectSponsorChallenge3() { finalizeSelection(2); }
static void declineSponsorChallenges() { finalizeSelection(-1); }

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
    // Round cleanly to nearest £1,000
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

    default:
        return false;
    }
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

    // Slot 1: Defensive or early-attack objective
    SponsorChallenge c1;
    if ((seed % 2u) == 0u) {
        c1.type = SponsorChallengeType::kCleanSheetWin;
        c1.title = "SHUTOUT VICTORY";
        c1.thresholdValue = 0;
        c1.rewardAmount = clampSponsorRewardForDivision(baseLow + 3000, divisionTier);
        c1.description = "Beat " + opponentName + " without conceding a single goal.";
    } else {
        c1.type = SponsorChallengeType::kFirstHalfGoals;
        c1.title = "BLITZKRIEG HALF";
        c1.thresholdValue = clampSponsorThreshold(c1.type, static_cast<int16_t>(1 + ((seed / 2u) % 2u)));
        c1.rewardAmount = clampSponsorRewardForDivision(baseLow + (c1.thresholdValue == 2 ? 7000 : 2000), divisionTier);
        c1.description = defaultDescriptionForChallenge(c1);
    }
    out.push_back(std::move(c1));

    // Slot 2: Star Striker player-goals challenge (if we have starters) or Shots on Target
    const SponsorStarterInfo *star = pickBestScorerStarter(starters);
    SponsorChallenge c2;
    if (star) {
        c2.type = SponsorChallengeType::kPlayerGoals;
        c2.title = "STAR STRIKER";
        c2.targetShirtNumber = static_cast<int16_t>(star->shirtNumber);
        c2.targetPlayerName = star->shortName;
        c2.thresholdValue = (star->seasonGoals >= 6 || (seed % 3u) == 0u) ? 2 : 1;
        c2.rewardAmount = clampSponsorRewardForDivision(
            c2.thresholdValue >= 2 ? (baseMid + 6000) : baseMid, divisionTier);
        c2.description = defaultDescriptionForChallenge(c2);
    } else {
        c2.type = SponsorChallengeType::kShotsOnTarget;
        c2.title = "TARGET PRACTICE";
        c2.thresholdValue = 7;
        c2.rewardAmount = clampSponsorRewardForDivision(baseMid, divisionTier);
        c2.description = defaultDescriptionForChallenge(c2);
    }
    out.push_back(std::move(c2));

    // Slot 3: High-reward dominance objective (Possession Win, Win by Margin, or Shots on Target)
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

        // Expected: TYPE|TITLE|DESCRIPTION|PLAYER_NAME|THRESHOLD|REWARD
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
        c.title = sanitizePlain(parts[1], 26);
        if (c.title.empty())
            c.title = defaultTitleForType(type);
        c.description = sanitizePlain(parts[2], 78);

        int rawThresh = std::atoi(parts[4].c_str());
        c.thresholdValue = clampSponsorThreshold(type, static_cast<int16_t>(rawThresh));

        // Strip currency symbols/commas from reward before parsing
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
            // Ensure description actually names the validated real player
            if (toUpperAscii(c.description).find(toUpperAscii(c.targetPlayerName)) == std::string::npos)
                c.description = defaultDescriptionForChallenge(c);
        }

        if (c.description.empty())
            c.description = defaultDescriptionForChallenge(c);

        parsed.push_back(std::move(c));
    }

    // Backfill any missing slots deterministically so we ALWAYS have 3 distinct valid challenges
    if (parsed.size() < 3) {
        auto fallback = generateOfflineSponsorChallenges(clubName, opponentName, divisionTier, starters, seed);
        for (auto& fb : fallback) {
            if (parsed.size() >= 3)
                break;
            if (!hasType(fb.type))
                parsed.push_back(std::move(fb));
        }
        // Extra safety if seed's 3 fallback types all collided with parsed
        static constexpr SponsorChallengeType kAllTypes[] = {
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
                c.thresholdValue = clampSponsorThreshold(t, t == SponsorChallengeType::kPossessionWin ? 58 :
                                                            (t == SponsorChallengeType::kShotsOnTarget ? 7 : 2));
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
    s_state.activeChallenge = SponsorChallenge{};
    s_state.lastMatchHadChallenge = false;

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

    // Pre-populate instant offline fallback offers so UI never blocks if LLM is off/slow
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

    std::string prompt = "Club: " + s_matchClubName + " (Division tier " + std::to_string(s_matchDivisionTier + 1) +
                         ", Bank: " + formatSponsorPounds(readCurrentClubBalance()) + ").\n" +
                         "Opponent: " + s_matchOpponentName + ".\n" +
                         "Starting outfield XI: " + starterList + ".\n" +
                         "Create 3 distinct 1997 matchday sponsor bonus challenges for " + s_matchClubName + ".\n" +
                         "Allowed TYPE values (pick 3 DIFFERENT types):\n" +
                         "- CHALLENGE_CLEAN_SHEET_WIN (threshold 0)\n" +
                         "- CHALLENGE_PLAYER_GOALS (threshold 1 or 2, PLAYER_NAME must be an exact surname from Starting outfield XI)\n" +
                         "- CHALLENGE_FIRST_HALF_GOALS (threshold 1, 2, or 3)\n" +
                         "- CHALLENGE_POSSESSION_WIN (threshold 55 to 65)\n" +
                         "- CHALLENGE_SHOTS_ON_TARGET (threshold 6 to 12)\n" +
                         "- CHALLENGE_WIN_BY_MARGIN (threshold 2 or 3)\n" +
                         "Scale REWARD between " + std::to_string(minR) + " and " + std::to_string(maxR) + " (integer pounds).\n" +
                         "Format STRICTLY one challenge per line as:\n" +
                         "TYPE|TITLE|DESCRIPTION|PLAYER_NAME|THRESHOLD|REWARD\n" +
                         "- TITLE: punchy 2-3 word 1997 sponsor challenge name (max 24 chars)\n" +
                         "- DESCRIPTION: crisp 1-sentence objective matching the exact threshold/player (max 70 chars)\n" +
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

    // Give an in-flight LLM request a brief non-intrusive chance to finish (already ran during stadium/scouting!)
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
    if (!s_matchEligible || !s_state.activeChallenge.isActive || swos.gameCanceled || swos.g_trainingGame)
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

    for (int i = 0; i < kNumPlayersInTeam; ++i) {
        const auto& pl = humanTg.players[i];
        for (int g = 0; g < static_cast<int>(pl.goalsScored); ++g) {
            result.scorerEntries.emplace_back(static_cast<int>(pl.shirtNumber), std::string(pl.shortName));
        }
    }

    bool completed = evaluateSponsorChallenge(s_state.activeChallenge, result);

    syncTotalsFromHotseatIfActive();
    s_state.lastMatchHadChallenge = true;
    s_state.lastMatchCompleted = completed;
    s_state.lastMatchTitle = s_state.activeChallenge.title;
    s_state.lastMatchDescription = s_state.activeChallenge.description;
    s_state.lastMatchPayout = completed ? s_state.activeChallenge.rewardAmount : 0;

    if (completed) {
        creditClubBalance(s_state.activeChallenge.rewardAmount);
        ++s_state.challengesCompleted;
        s_state.totalBonusEarned += s_state.activeChallenge.rewardAmount;
    } else {
        ++s_state.challengesFailed;
    }

    s_state.lastMatchNewBalance = readCurrentClubBalance();
    syncTotalsToHotseatIfActive();

    logInfo("Sponsor challenge '%s' %s (payout: %s, new balance: %s)",
        s_state.lastMatchTitle.c_str(),
        completed ? "COMPLETED" : "FAILED",
        formatSponsorPounds(s_state.lastMatchPayout).c_str(),
        formatSponsorPounds(s_state.lastMatchNewBalance).c_str());

    s_state.activeChallenge = SponsorChallenge{};
}

void showPostMatchSponsorPayoutIfNeeded()
{
    if (!s_state.lastMatchHadChallenge || swos.gameCanceled || swos.g_trainingGame)
        return;

#ifndef SWOS_TEST
    std::string body;
    body += "SPONSOR CHALLENGE: " + toUpperAscii(s_state.lastMatchTitle) +
            (s_state.lastMatchCompleted ? " (COMPLETED!)" : " (FAILED)") + "\n\n";
    body += "OBJECTIVE:\n" + s_state.lastMatchDescription + "\n\n";
    body += "SPONSOR BONUS PAID: " +
            (s_state.lastMatchCompleted ? ("+" + formatSponsorPounds(s_state.lastMatchPayout)) : std::string("GBP 0")) + "\n";
    body += "UPDATED BANK BALANCE: " + formatSponsorPounds(s_state.lastMatchNewBalance) + "\n\n";
    body += "CAREER SPONSOR RECORD:\n";
    body += "COMPLETED: " + std::to_string(s_state.challengesCompleted) +
            "   FAILED: " + std::to_string(s_state.challengesFailed) +
            "   TOTAL BONUS: " + formatSponsorPounds(s_state.totalBonusEarned);

    setStandardMenuBackgroundImage();
    menuFadeIn();
    showLlmReportDialog("POST-MATCH SPONSOR REPORT", body);
    menuFadeOut();
#endif
}

// --- Live Telemetry, HUD & Newspaper Integration ---

bool hasActiveSponsorChallenge()
{
    return s_matchEligible && s_state.activeChallenge.isActive && !swos.g_trainingGame;
}

std::string formatActiveSponsorChallengeForTelemetry()
{
    if (!hasActiveSponsorChallenge())
        return {};

    const auto& c = s_state.activeChallenge;
    const auto stats = getStats();
    int clubG = s_humanIsTeam1 ? swos.statsTeam1Goals : swos.statsTeam2Goals;
    int oppG = s_humanIsTeam1 ? swos.statsTeam2Goals : swos.statsTeam1Goals;
    int poss = s_humanIsTeam1 ? stats.team1.ballPossession : stats.team2.ballPossession;
    int sot = s_humanIsTeam1 ? stats.team1.onTarget : stats.team2.onTarget;

    std::string progress;
    switch (c.type) {
    case SponsorChallengeType::kCleanSheetWin:
        if (oppG > 0) progress = "CLEAN SHEET LOST (conceded " + std::to_string(oppG) + ")";
        else if (clubG > oppG) progress = "ON TRACK (winning with clean sheet!)";
        else progress = "Clean sheet intact, need a goal to win";
        break;
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
        progress = c.targetPlayerName + " has " + std::to_string(scored) + "/" + std::to_string(c.thresholdValue) + " goals" +
                   (scored >= c.thresholdValue ? " (TARGET ACHIEVED!)" : "");
        break;
    }
    case SponsorChallengeType::kFirstHalfGoals:
        progress = (swos.halfNumber <= 1)
            ? ("1st half goals: " + std::to_string(clubG) + "/" + std::to_string(c.thresholdValue))
            : "1st half finished";
        break;
    case SponsorChallengeType::kPossessionWin:
        progress = "Possession " + std::to_string(poss) + "% (need " + std::to_string(c.thresholdValue) + "%+ and win)";
        break;
    case SponsorChallengeType::kShotsOnTarget:
        progress = "Shots on target " + std::to_string(sot) + "/" + std::to_string(c.thresholdValue) +
                   (sot >= c.thresholdValue ? " (TARGET ACHIEVED!)" : "");
        break;
    case SponsorChallengeType::kWinByMargin:
        progress = "Current goal margin: " + std::to_string(clubG - oppG) + " (need +" + std::to_string(c.thresholdValue) + ")";
        break;
    default:
        break;
    }

    return "Active Sponsor Challenge for " + s_matchClubName + ": " + c.title +
           " (" + c.description + " Bonus: " + formatSponsorPounds(c.rewardAmount) + ") -> Status: " + progress;
}

std::string formatActiveSponsorChallengeHudLine(int& outStatusColorMode)
{
    outStatusColorMode = 0; // 0 = gold/active, 1 = green/achieved, 2 = red/failed
    if (!hasActiveSponsorChallenge())
        return {};

    const auto& c = s_state.activeChallenge;
    const auto stats = getStats();
    int clubG = s_humanIsTeam1 ? swos.statsTeam1Goals : swos.statsTeam2Goals;
    int oppG = s_humanIsTeam1 ? swos.statsTeam2Goals : swos.statsTeam1Goals;
    int poss = s_humanIsTeam1 ? stats.team1.ballPossession : stats.team2.ballPossession;
    int sot = s_humanIsTeam1 ? stats.team1.onTarget : stats.team2.onTarget;

    std::string status;
    switch (c.type) {
    case SponsorChallengeType::kCleanSheetWin:
        if (oppG > 0) {
            status = "LOST";
            outStatusColorMode = 2;
        } else if (clubG > oppG) {
            status = "ON TRACK";
            outStatusColorMode = 1;
        } else {
            status = "0-0";
        }
        break;
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
        status = std::to_string(scored) + "/" + std::to_string(c.thresholdValue);
        if (scored >= c.thresholdValue)
            outStatusColorMode = 1;
        break;
    }
    case SponsorChallengeType::kFirstHalfGoals:
        if (swos.halfNumber <= 1) {
            status = "HT " + std::to_string(clubG) + "/" + std::to_string(c.thresholdValue);
            if (clubG >= c.thresholdValue)
                outStatusColorMode = 1;
        } else {
            status = "HT DONE";
        }
        break;
    case SponsorChallengeType::kPossessionWin:
        status = std::to_string(poss) + "%/" + std::to_string(c.thresholdValue) + "%";
        if (clubG > oppG && poss >= c.thresholdValue)
            outStatusColorMode = 1;
        break;
    case SponsorChallengeType::kShotsOnTarget:
        status = std::to_string(sot) + "/" + std::to_string(c.thresholdValue) + " SOT";
        if (sot >= c.thresholdValue)
            outStatusColorMode = 1;
        break;
    case SponsorChallengeType::kWinByMargin: {
        int diff = clubG - oppG;
        status = (diff >= 0 ? "+" : "") + std::to_string(diff) + "/+" + std::to_string(c.thresholdValue);
        if (diff >= c.thresholdValue)
            outStatusColorMode = 1;
        break;
    }
    default:
        break;
    }

    int32_t kBonus = c.rewardAmount / 1000;
    std::string titleShort = c.title.size() > 14 ? c.title.substr(0, 14) : c.title;
    return "BONUS: " + titleShort + " [" + status + "] +" + std::to_string(kBonus) + "K";
}

std::string formatLastSponsorChallengeOutcomeForNewspaper()
{
    if (!s_state.lastMatchHadChallenge)
        return {};
    if (s_state.lastMatchCompleted) {
        return "Sponsor Challenge Outcome: " + s_matchClubName + " COMPLETED '" + s_state.lastMatchTitle +
               "' (" + s_state.lastMatchDescription + ") and earned a " +
               formatSponsorPounds(s_state.lastMatchPayout) + " cash bonus!";
    }
    return "Sponsor Challenge Outcome: " + s_matchClubName + " FAILED '" + s_state.lastMatchTitle +
           "' (" + s_state.lastMatchDescription + ") and missed out on the sponsor bonus.";
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
