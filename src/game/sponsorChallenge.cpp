#include "sponsorChallenge.h"
#include "achievements.h"
#include "compactFont.h"
#include "controls.h"
#include "darkRectangle.h"
#include "drawMenu.h"
#include "gameFieldMapping.h"
#include "gameTime.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "llmClient.h"
#include "llmReportMenu.h"
#include "log.h"
#include "matchTelemetry.h"
#include "menuBackground.h"
#include "menus.h"
#include "render.h"
#include "stadiumFacilities.h"
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
#include <map>
#include <set>
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
bool s_prevKeyDown[6] = { false, false, false, false, false, false }; // 1..5, 0

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
        s_state.activeContract = p.sponsorContract;
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
        p.sponsorContract = s_state.activeContract;
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
    if (u.find("NO_CARDS") != std::string::npos || u.find("DISCIPLIN") != std::string::npos ||
        u.find("FAIR_PLAY") != std::string::npos)
        return SponsorChallengeType::kNoCardsWin;
    if (u.find("EARLY_GOAL") != std::string::npos || u.find("EARLY_BLITZ") != std::string::npos)
        return SponsorChallengeType::kEarlyGoal;
    if (u.find("BOTH_HALVES") != std::string::npos)
        return SponsorChallengeType::kBothHalvesGoal;
    if (u.find("SECOND_HALF") != std::string::npos)
        return SponsorChallengeType::kSecondHalfGoals;
    if (u.find("LATE_GOAL") != std::string::npos || u.find("LATE_DAGGER") != std::string::npos)
        return SponsorChallengeType::kLateGoal;
    if (u.find("LIMIT_OPP") != std::string::npos || u.find("SHUTDOWN") != std::string::npos)
        return SponsorChallengeType::kLimitOppShots;
    if (u.find("CLINICAL") != std::string::npos || u.find("ACCURACY") != std::string::npos)
        return SponsorChallengeType::kClinicalFinishing;
    if (u.find("PLAYER_ASSIST") != std::string::npos || u.find("PLAYMAKER") != std::string::npos ||
        u.find("ASSIST") != std::string::npos)
        return SponsorChallengeType::kPlayerAssist;
    if (u.find("NON_ATTACKER") != std::string::npos || u.find("MIDFIELD_GOAL") != std::string::npos ||
        u.find("DEFENDER_GOAL") != std::string::npos)
        return SponsorChallengeType::kNonAttackerGoal;
    if (u.find("DIFFERENT_SCORER") != std::string::npos || u.find("SPREAD_THE_WEALTH") != std::string::npos)
        return SponsorChallengeType::kDifferentScorers;
    if (u.find("HAT_TRICK") != std::string::npos || u.find("HATTRICK") != std::string::npos)
        return SponsorChallengeType::kHatTrickHero;
    if (u.find("SET_PIECE") != std::string::npos || u.find("CORNER_OR_FK") != std::string::npos)
        return SponsorChallengeType::kSetPieceGoal;
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

const SponsorStarterInfo *pickBestPureAttacker(const std::vector<SponsorStarterInfo>& starters)
{
    bool anyExplicitPure = false;
    for (const auto& s : starters) {
        if (s.isPureAttacker) {
            anyExplicitPure = true;
            break;
        }
    }
    const SponsorStarterInfo *best = nullptr;
    for (const auto& s : starters) {
        if (s.shortName.empty())
            continue;
        bool eligible = anyExplicitPure ? s.isPureAttacker : s.isAttacker;
        if (!eligible)
            continue;
        if (!best || s.seasonGoals > best->seasonGoals)
            best = &s;
    }
    return best;
}

const SponsorStarterInfo *pickBestPlaymaker(const std::vector<SponsorStarterInfo>& starters, uint32_t seed)
{
    std::vector<const SponsorStarterInfo *> mids;
    std::vector<const SponsorStarterInfo *> all;
    for (const auto& s : starters) {
        if (s.shortName.empty())
            continue;
        all.push_back(&s);
        if (s.isMidfielder)
            mids.push_back(&s);
    }
    if (!mids.empty())
        return mids[seed % mids.size()];
    if (!all.empty())
        return all[seed % all.size()];
    return nullptr;
}

const SponsorStarterInfo *matchStarterByName(const std::vector<SponsorStarterInfo>& starters, const std::string& name, bool pureAttackerOnly)
{
    if (name.empty())
        return nullptr;
    std::string needle = toUpperAscii(trimCopy(name));
    if (needle.empty() || needle == "NONE" || needle == "N/A" || needle == "-")
        return nullptr;

    bool anyExplicitPure = false;
    for (const auto& s : starters) {
        if (s.isPureAttacker) {
            anyExplicitPure = true;
            break;
        }
    }
    auto okRole = [&](const SponsorStarterInfo& s) {
        if (!pureAttackerOnly) return true;
        return anyExplicitPure ? s.isPureAttacker : s.isAttacker;
    };

    for (const auto& s : starters) {
        if (!okRole(s)) continue;
        std::string cand = toUpperAscii(s.shortName);
        if (cand == needle)
            return &s;
    }
    for (const auto& s : starters) {
        if (!okRole(s)) continue;
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
    case SponsorChallengeType::kFirstHalfGoals: return "BLITZKRIEG HALF";
    case SponsorChallengeType::kPossessionWin: return "TOTAL FOOTBALL";
    case SponsorChallengeType::kShotsOnTarget: return "RELENTLESS ATTACK";
    case SponsorChallengeType::kWinByMargin: return "STATEMENT WIN";
    case SponsorChallengeType::kTeamGoals: return "GOAL RUSH";
    case SponsorChallengeType::kLongRangeGoal: return "LONG RANGE STRIKE";
    case SponsorChallengeType::kHeaderGoal: return "AERIAL DOMINANCE";
    case SponsorChallengeType::kNoCardsWin: return "DISCIPLINED WIN";
    case SponsorChallengeType::kEarlyGoal: return "EARLY BLITZ";
    case SponsorChallengeType::kBothHalvesGoal: return "BOTH HALVES";
    case SponsorChallengeType::kSecondHalfGoals: return "2ND HALF SURGE";
    case SponsorChallengeType::kLateGoal: return "LATE DAGGER";
    case SponsorChallengeType::kLimitOppShots: return "SHUTDOWN DEFENCE";
    case SponsorChallengeType::kClinicalFinishing: return "CLINICAL FINISH";
    case SponsorChallengeType::kPlayerAssist: return "PLAYMAKER";
    case SponsorChallengeType::kNonAttackerGoal: return "MID/DEF SCORER";
    case SponsorChallengeType::kDifferentScorers: return "SPREAD THE WEALTH";
    case SponsorChallengeType::kHatTrickHero: return "HAT-TRICK HERO";
    case SponsorChallengeType::kSetPieceGoal: return "SET-PIECE SPECIAL";
    default: return "MATCH BONUS";
    }
}

std::string defaultDescriptionForChallenge(const SponsorChallenge& c)
{
    switch (c.type) {
    case SponsorChallengeType::kCleanSheetWin:
        return "Win the match without conceding a single goal.";
    case SponsorChallengeType::kPlayerGoals:
        return c.targetPlayerName + " (ATT) must score at least " + std::to_string(std::max<int>(2, c.thresholdValue)) + " goals.";
    case SponsorChallengeType::kFirstHalfGoals:
        if (c.thresholdValue <= 1)
            return "Score at least 1 goal in the first half.";
        return "Score at least " + std::to_string(c.thresholdValue) + " goals in the first half.";
    case SponsorChallengeType::kPossessionWin:
        return "Win the match with " + std::to_string(std::max<int>(60, c.thresholdValue)) + "% or higher possession.";
    case SponsorChallengeType::kShotsOnTarget:
        return "Record at least " + std::to_string(c.thresholdValue) + " shots on target.";
    case SponsorChallengeType::kWinByMargin:
        return "Win the match by at least " + std::to_string(c.thresholdValue) + " clear goals.";
    case SponsorChallengeType::kTeamGoals:
        return "Score at least " + std::to_string(c.thresholdValue) + " total goals in the match.";
    case SponsorChallengeType::kLongRangeGoal:
        if (c.thresholdValue <= 1)
            return "Score at least 1 goal from outside the 18-yard box.";
        return "Score at least " + std::to_string(c.thresholdValue) + " goals from outside the 18-yard box.";
    case SponsorChallengeType::kHeaderGoal:
        if (c.thresholdValue <= 1)
            return "Score at least 1 goal with a header.";
        return "Score at least " + std::to_string(c.thresholdValue) + " goals with a header.";
    case SponsorChallengeType::kNoCardsWin:
        return "Win the match with 0 yellow cards and 0 red cards.";
    case SponsorChallengeType::kEarlyGoal:
        return "Score at least 1 goal within the first 15 minutes.";
    case SponsorChallengeType::kBothHalvesGoal:
        return "Score at least 1 goal in the 1st half and 1 in the 2nd half.";
    case SponsorChallengeType::kSecondHalfGoals:
        return "Score at least " + std::to_string(std::max<int>(2, c.thresholdValue)) + " goals in the second half.";
    case SponsorChallengeType::kLateGoal:
        return "Score a goal in the 75th minute or later.";
    case SponsorChallengeType::kLimitOppShots:
        return "Win while holding opponent to " + std::to_string(c.thresholdValue) + " or fewer shots on target.";
    case SponsorChallengeType::kClinicalFinishing:
        return "Score 2+ goals converting at least " + std::to_string(c.thresholdValue) + "% of shots on target.";
    case SponsorChallengeType::kPlayerAssist:
        return c.targetPlayerName + " must record at least 1 assist.";
    case SponsorChallengeType::kNonAttackerGoal:
        return "A midfielder or defender (non-ATT) must score a goal.";
    case SponsorChallengeType::kDifferentScorers:
        return "Have at least " + std::to_string(std::max<int>(3, c.thresholdValue)) + " different players score for your club.";
    case SponsorChallengeType::kHatTrickHero:
        return "Any single player on your team scores a hat-trick (3+ goals).";
    case SponsorChallengeType::kSetPieceGoal:
        return "Score directly from a free kick or a corner kick routine.";
    default:
        return "Complete today's sponsor objective.";
    }
}

std::string formatContractProgressShort(const MultiMatchContract& mc)
{
    if (!mc.isActive || mc.type == MultiMatchContractType::kNone)
        return {};
    int rem = std::max(0, static_cast<int>(mc.matchesTotal) - static_cast<int>(mc.matchesPlayed));
    switch (mc.type) {
    case MultiMatchContractType::kWinStreak:
        return std::to_string(mc.currentProgress) + "/" + std::to_string(mc.targetValue) + "W (" + std::to_string(rem) + " left)";
    case MultiMatchContractType::kUnbeatenRun:
        return std::to_string(mc.currentProgress) + "/" + std::to_string(mc.targetValue) + " UNB (" + std::to_string(rem) + " left)";
    case MultiMatchContractType::kGoalsInMatches:
        return std::to_string(mc.currentProgress) + "/" + std::to_string(mc.targetValue) + "G (" + std::to_string(rem) + " left)";
    case MultiMatchContractType::kCleanSheetsInMatches:
        return std::to_string(mc.currentProgress) + "/" + std::to_string(mc.targetValue) + "CS (" + std::to_string(rem) + " left)";
    case MultiMatchContractType::kMinGoalsEachMatch:
        return std::to_string(mc.currentProgress) + "/" + std::to_string(mc.targetValue) + "x2G (" + std::to_string(rem) + " left)";
    default:
        return {};
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
        // Strict ATT only for Star Striker; wingers count as midfielders/playmakers
        info.isPureAttacker = (pl.position == PlayerPosition::kAttacker);
        info.isAttacker = info.isPureAttacker;
        info.isMidfielder = (pl.position == PlayerPosition::kMidfielder ||
                             pl.position == PlayerPosition::kLeftWing ||
                             pl.position == PlayerPosition::kRightWing);

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
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame || !topTeam || !bottomTeam)
        return false;

    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive()) {
        if (mgr.activePlayerIndex() < 0 || mgr.activePlayerIndex() >= mgr.numPlayers())
            return false;
        if (HotseatInternal::isVmNationalSelector())
            return false;
    }

    bool t1Human = false;
    bool t2Human = false;
    const TeamFile *tf1 = swos.gameTeam1.asAligned();
    const TeamFile *tf2 = swos.gameTeam2.asAligned();
    if (!tf1) tf1 = swos.playMatchTeam1Ptr.asAligned();
    if (!tf2) tf2 = swos.playMatchTeam2Ptr.asAligned();
    if (tf1) t1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
    if (tf2) t2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);

    if (t1Human == t2Human) {
        std::string careerClub;
        if (mgr.isActive()) {
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

int computeJackpotPct(int selectedSingleCount, int totalSingleOffered)
{
    if (selectedSingleCount <= 0 || totalSingleOffered <= 0)
        return 0;
    if (selectedSingleCount == totalSingleOffered) {
        if (totalSingleOffered >= 5) return 75;
        if (totalSingleOffered >= 3) return 50;
    }
    if (selectedSingleCount >= 3)
        return 25;
    return 0;
}

void refreshSponsorChallengeButtons()
{
    MenuEntry *entries[kSponsorOfferedCount] = {
        &challenge1Entry, &challenge2Entry, &challenge3Entry, &challenge4Entry, &challenge5Entry
    };
    int selectedSingleCount = 0;
    int32_t singleStake = 0;
    bool contractSelected = false;

    for (int i = 0; i < kSponsorOfferedCount; ++i) {
        bool checked = s_state.selectedOffer[i];
        bool isContract = (i == kSponsorOfferedCount - 1 && s_state.slot5IsContractOffer);
        if (isContract) {
            if (checked) contractSelected = true;
            entries[i]->setBackgroundColor(checked ? kGreen : kPurple);
            std::string mark = checked ? "[X] " : "[ ] ";
            std::string label = mark + "CONTRACT: " + toUpperAscii(s_state.offeredContract.title) +
                                " +" + std::to_string(s_state.offeredContract.rewardAmount / 1000) + "K/-" +
                                std::to_string(s_state.offeredContract.penaltyAmount / 1000) + "K";
            entries[i]->copyString(label.c_str());
        } else {
            const auto& c = s_state.offered[i];
            if (checked) {
                ++selectedSingleCount;
                singleStake += c.rewardAmount;
            }
            entries[i]->setBackgroundColor(checked ? kGreen : kDarkBlue);
            std::string mark = checked ? "[X] " : "[ ] ";
            std::string label = mark + toUpperAscii(c.title) + "  +/-" + formatSponsorPounds(c.rewardAmount);
            entries[i]->copyString(label.c_str());
        }
    }

    int totalSingleOffered = s_state.slot5IsContractOffer ? (kSponsorOfferedCount - 1) : kSponsorOfferedCount;
    int jpPct = computeJackpotPct(selectedSingleCount, totalSingleOffered);
    int totalSelected = selectedSingleCount + (contractSelected ? 1 : 0);

    if (totalSelected == 0) {
        confirmBtnEntry.copyString("START (0 SELECTED)");
    } else if (jpPct >= 50) {
        int32_t maxPayout = singleStake + (singleStake * jpPct) / 100;
        std::string lbl = "ALL-IN +" + std::to_string(jpPct) + "%! MAX +" + formatSponsorPounds(maxPayout);
        confirmBtnEntry.copyString(lbl.c_str());
    } else if (jpPct == 25) {
        int32_t maxPayout = singleStake + (singleStake / 4);
        std::string lbl = "ACCEPT " + std::to_string(totalSelected) + " (+25% -> +" + formatSponsorPounds(maxPayout) + ")";
        confirmBtnEntry.copyString(lbl.c_str());
    } else if (selectedSingleCount == 0 && contractSelected) {
        std::string lbl = "SIGN CONTRACT (+" + std::to_string(s_state.offeredContract.rewardAmount / 1000) + "K)";
        confirmBtnEntry.copyString(lbl.c_str());
    } else {
        std::string lbl = "ACCEPT " + std::to_string(totalSelected) + " (+/-" + formatSponsorPounds(singleStake) + ")";
        confirmBtnEntry.copyString(lbl.c_str());
    }
}

void commitSelectedChallenges(bool declineAll)
{
    s_state.activeChallenges.clear();
    int singleSlots = s_state.slot5IsContractOffer ? (kSponsorOfferedCount - 1) : kSponsorOfferedCount;
    s_state.offeredSingleMatchCount = singleSlots;

    if (!declineAll && s_state.offersReady) {
        for (int i = 0; i < singleSlots; ++i) {
            if (s_state.selectedOffer[i]) {
                SponsorChallenge c = s_state.offered[i];
                c.isActive = true;
                s_state.activeChallenges.push_back(std::move(c));
            }
        }
        if (s_state.slot5IsContractOffer && s_state.selectedOffer[kSponsorOfferedCount - 1]) {
            s_state.activeContract = s_state.offeredContract;
            s_state.activeContract.isActive = true;
            s_state.activeContract.matchesPlayed = 0;
            s_state.activeContract.currentProgress = 0;
            syncTotalsToHotseatIfActive();
            logInfo("Signed multi-match sponsor contract: %s (+%s / -%s)",
                s_state.activeContract.title.c_str(),
                formatSponsorPounds(s_state.activeContract.rewardAmount).c_str(),
                formatSponsorPounds(s_state.activeContract.penaltyAmount).c_str());
        }
    }

    if (s_state.activeChallenges.empty()) {
        logInfo("Single-match sponsor challenges declined for this match.");
    } else {
        int jpPct = computeJackpotPct(static_cast<int>(s_state.activeChallenges.size()), singleSlots);
        logInfo("Accepted %zu single-match sponsor challenge(s) (combo/jackpot=+%d%%).",
            s_state.activeChallenges.size(), jpPct);
    }
    SetExitMenuFlag();
}

std::string formatSignedPounds(int64_t delta)
{
    if (delta > 0)
        return "+" + formatSponsorPounds(delta);
    return formatSponsorPounds(delta);
}

SponsorChallenge buildChallengeForType(
    SponsorChallengeType type,
    const std::string& opponentName,
    int divisionTier,
    const std::vector<SponsorStarterInfo>& starters,
    uint32_t seed,
    int32_t targetReward)
{
    SponsorChallenge c;
    c.type = type;
    c.title = defaultTitleForType(type);
    c.rewardAmount = clampSponsorRewardForDivision(targetReward, divisionTier);

    switch (type) {
    case SponsorChallengeType::kCleanSheetWin:
        c.thresholdValue = 0;
        c.description = "Beat " + opponentName + " without conceding a single goal.";
        break;
    case SponsorChallengeType::kPlayerGoals: {
        const SponsorStarterInfo *att = pickBestPureAttacker(starters);
        if (att) {
            c.targetShirtNumber = static_cast<int16_t>(att->shirtNumber);
            c.targetPlayerName = att->shortName;
        }
        c.thresholdValue = 2; // Strictly 2+ goals for pure attacker
        c.description = defaultDescriptionForChallenge(c);
        break;
    }
    case SponsorChallengeType::kFirstHalfGoals:
        c.thresholdValue = clampSponsorThreshold(type, static_cast<int16_t>(1 + (seed % 2u)));
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kPossessionWin:
        c.thresholdValue = clampSponsorThreshold(type, static_cast<int16_t>(60 + ((seed % 2u) * 2)));
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kShotsOnTarget:
        c.thresholdValue = clampSponsorThreshold(type, static_cast<int16_t>(7 + (seed % 3u)));
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kWinByMargin:
        c.thresholdValue = clampSponsorThreshold(type, static_cast<int16_t>(2 + ((seed / 5u) % 2u)));
        c.description = "Beat " + opponentName + " by at least " + std::to_string(c.thresholdValue) + " clear goals.";
        break;
    case SponsorChallengeType::kTeamGoals:
        c.thresholdValue = clampSponsorThreshold(type, static_cast<int16_t>(2 + (seed % 2u)));
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kLongRangeGoal:
        c.thresholdValue = 1;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kHeaderGoal:
        c.thresholdValue = 1;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kNoCardsWin:
        c.thresholdValue = 0;
        c.description = "Beat " + opponentName + " with 0 yellow and 0 red cards.";
        break;
    case SponsorChallengeType::kEarlyGoal:
        c.thresholdValue = 15;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kBothHalvesGoal:
        c.thresholdValue = 1;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kSecondHalfGoals:
        c.thresholdValue = 2;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kLateGoal:
        c.thresholdValue = 75;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kLimitOppShots:
        c.thresholdValue = 2;
        c.description = "Beat " + opponentName + " holding them to 2 or fewer shots on target.";
        break;
    case SponsorChallengeType::kClinicalFinishing:
        c.thresholdValue = 50;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kPlayerAssist: {
        const SponsorStarterInfo *pm = pickBestPlaymaker(starters, seed);
        if (pm) {
            c.targetShirtNumber = static_cast<int16_t>(pm->shirtNumber);
            c.targetPlayerName = pm->shortName;
        }
        c.thresholdValue = 1;
        c.description = defaultDescriptionForChallenge(c);
        break;
    }
    case SponsorChallengeType::kNonAttackerGoal:
        c.thresholdValue = 1;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kDifferentScorers:
        c.thresholdValue = 3;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kHatTrickHero:
        c.thresholdValue = 3;
        c.description = defaultDescriptionForChallenge(c);
        break;
    case SponsorChallengeType::kSetPieceGoal:
        c.thresholdValue = 1;
        c.description = defaultDescriptionForChallenge(c);
        break;
    default:
        break;
    }
    return c;
}

} // namespace

// --- Native Menu Callbacks (referenced by sponsorChallenge.mnu) ---

static void initSponsorChallengeMenu()
{
    for (int i = 0; i < kSponsorOfferedCount; ++i)
        s_state.selectedOffer[i] = false;
    std::memset(s_prevKeyDown, 0, sizeof(s_prevKeyDown));
    refreshSponsorChallengeButtons();
    setCurrentEntry(challenge1);
}

static void drawSponsorChallengeMenu()
{
    int selectedSingleCount = 0;
    int32_t singleStake = 0;
    int singleSlots = s_state.slot5IsContractOffer ? (kSponsorOfferedCount - 1) : kSponsorOfferedCount;
    for (int i = 0; i < singleSlots; ++i) {
        if (s_state.selectedOffer[i]) {
            ++selectedSingleCount;
            singleStake += s_state.offered[i].rewardAmount;
        }
    }
    int jpPct = computeJackpotPct(selectedSingleCount, singleSlots);

    // Top info strip (y=13..29)
    drawDarkRectangle({ 16.0f, 13.0f, 288.0f, 16.0f }, true);

    // Compact description bars directly under each of the 5 clickable buttons
    static constexpr int kCardYs[kSponsorOfferedCount] = { 44, 71, 98, 125, 152 };
    for (int i = 0; i < kSponsorOfferedCount; ++i) {
        drawDarkRectangle({ 16.0f, static_cast<float>(kCardYs[i]), 288.0f, 12.0f }, false);
    }

    // Bottom hint bar (y=187..198)
    drawDarkRectangle({ 16.0f, 187.0f, 288.0f, 11.0f }, false);

    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = getCompactFontAtlas(r);
    if (r && atlas) {
        float gameScale = getGameScale();
        float px = computeCompactPixelScaleForVga(gameScale, 0.40f);
        float offX = getGameScreenOffsetX();
        float offY = getGameScreenOffsetY();

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_RenderSetScale(r, 1.0f, 1.0f);

        SDL_Color whiteCol{ 244, 246, 248, 255 };
        SDL_Color yellowCol{ 255, 226, 78, 255 };
        SDL_Color greenCol{ 96, 236, 134, 255 };
        SDL_Color cyanCol{ 118, 224, 255, 255 };
        SDL_Color hintCol{ 176, 212, 236, 255 };

        if (jpPct > 0) {
            int32_t bonus = (singleStake * jpPct) / 100;
            std::string jp = (jpPct >= 50 ? "CLEAN SWEEP +" : "COMBO +") + std::to_string(jpPct) +
                             "%! Extra +" + formatSponsorPounds(bonus);
            drawCompactText(r, atlas, offX + 20.0f * gameScale, offY + 14.5f * gameScale, px,
                jp, greenCol, 255, true);
        } else {
            std::string oppLine = "vs " + s_matchOpponentName + " (failing deducts stake!)";
            drawCompactText(r, atlas, offX + 20.0f * gameScale, offY + 14.5f * gameScale, px,
                oppLine, whiteCol, 255, true);
        }

        int vipPct = getCurrentCareerCommercialSponsorBonusPct();
        std::string bankStr = (vipPct > 0 ? ("VIP +" + std::to_string(vipPct) + "% | ") : "") +
                              "Bank: " + formatSponsorPounds(readCurrentClubBalance());
        drawCompactTextRightAligned(r, atlas, offX + 300.0f * gameScale, offY + 14.5f * gameScale, px,
            bankStr, yellowCol, 255, true);

        if (s_state.activeContract.isActive) {
            std::string activeLine = "ACTIVE CONTRACT: " + s_state.activeContract.title + " [" +
                                     formatContractProgressShort(s_state.activeContract) + "] +" +
                                     std::to_string(s_state.activeContract.rewardAmount / 1000) + "K";
            drawCompactText(r, atlas, offX + 20.0f * gameScale, offY + 22.0f * gameScale, px,
                activeLine, cyanCol, 250, false);
        } else {
            drawCompactText(r, atlas, offX + 20.0f * gameScale, offY + 22.0f * gameScale, px,
                "Slot 5 offers a Multi-Match Contract (50% breach fee if failed)", cyanCol, 235, false);
        }

        int maxDescUnits = std::max(80, static_cast<int>((278.0f * gameScale) / px));
        for (int i = 0; i < kSponsorOfferedCount; ++i) {
            bool isContract = (i == kSponsorOfferedCount - 1 && s_state.slot5IsContractOffer);
            std::string desc = isContract ? s_state.offeredContract.description : s_state.offered[i].description;
            auto lines = wrapCompactTextUnits(desc, maxDescUnits);
            float lineY = offY + static_cast<float>(kCardYs[i] + 2.5f) * gameScale;
            SDL_Color col = s_state.selectedOffer[i] ? yellowCol : (isContract ? cyanCol : whiteCol);
            if (!lines.empty()) {
                drawCompactText(r, atlas, offX + 20.0f * gameScale, lineY, px,
                    lines[0], col, 255, s_state.selectedOffer[i]);
            }
        }

        std::string hint = s_state.slot5IsContractOffer
            ? "Keys 1-5 toggle | 0 decline | 3 singles: +25% | All 4 singles: +50% Jackpot"
            : "Keys 1-5 toggle | 0 decline | 3-4: +25% Combo | All 5: +75% Clean Sweep!";
        drawCompactText(r, atlas, offX + 20.0f * gameScale, offY + 189.0f * gameScale, px,
            hint, hintCol, 240, false);

        SDL_RenderSetScale(r, origSX, origSY);
    }

    // Edge-triggered keyboard shortcuts: 1..5 toggle offers, 0 declines all
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        bool k1 = keys[SDL_SCANCODE_1] != 0 || keys[SDL_SCANCODE_KP_1] != 0;
        bool k2 = keys[SDL_SCANCODE_2] != 0 || keys[SDL_SCANCODE_KP_2] != 0;
        bool k3 = keys[SDL_SCANCODE_3] != 0 || keys[SDL_SCANCODE_KP_3] != 0;
        bool k4 = keys[SDL_SCANCODE_4] != 0 || keys[SDL_SCANCODE_KP_4] != 0;
        bool k5 = keys[SDL_SCANCODE_5] != 0 || keys[SDL_SCANCODE_KP_5] != 0;
        bool k0 = keys[SDL_SCANCODE_0] != 0 || keys[SDL_SCANCODE_KP_0] != 0;
        bool cur[6] = { k1, k2, k3, k4, k5, k0 };
        for (int i = 0; i < kSponsorOfferedCount; ++i) {
            if (cur[i] && !s_prevKeyDown[i]) {
                s_state.selectedOffer[i] = !s_state.selectedOffer[i];
                refreshSponsorChallengeButtons();
            }
        }
        if (k0 && !s_prevKeyDown[5])
            commitSelectedChallenges(true);
        for (int i = 0; i < 6; ++i)
            s_prevKeyDown[i] = cur[i];
    }
}

static void toggleOfferSlot(int idx)
{
    if (idx < 0 || idx >= kSponsorOfferedCount)
        return;
    s_state.selectedOffer[idx] = !s_state.selectedOffer[idx];
    refreshSponsorChallengeButtons();
}

static void toggleSponsorChallenge1() { toggleOfferSlot(0); }
static void toggleSponsorChallenge2() { toggleOfferSlot(1); }
static void toggleSponsorChallenge3() { toggleOfferSlot(2); }
static void toggleSponsorChallenge4() { toggleOfferSlot(3); }
static void toggleSponsorChallenge5() { toggleOfferSlot(4); }
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

int32_t clampMultiMatchRewardForDivision(int32_t rawReward, int divisionTier)
{
    int32_t minR = 20000;
    int32_t maxR = 45000;
    if (divisionTier <= 0) {
        minR = 50000;
        maxR = 120000;
    } else if (divisionTier == 1) {
        minR = 30000;
        maxR = 70000;
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
        return std::clamp<int16_t>(rawThreshold, 2, 3); // Strictly 2+ goals for ATT
    case SponsorChallengeType::kFirstHalfGoals:
        return std::clamp<int16_t>(rawThreshold, 1, 3);
    case SponsorChallengeType::kPossessionWin:
        return std::clamp<int16_t>(rawThreshold, 60, 70); // Strictly >= 60%
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
    case SponsorChallengeType::kNoCardsWin:
        return 0;
    case SponsorChallengeType::kEarlyGoal:
        return 15;
    case SponsorChallengeType::kBothHalvesGoal:
        return 1;
    case SponsorChallengeType::kSecondHalfGoals:
        return std::clamp<int16_t>(rawThreshold, 2, 3);
    case SponsorChallengeType::kLateGoal:
        return 75;
    case SponsorChallengeType::kLimitOppShots:
        return std::clamp<int16_t>(rawThreshold, 1, 3);
    case SponsorChallengeType::kClinicalFinishing:
        return std::clamp<int16_t>(rawThreshold, 50, 75);
    case SponsorChallengeType::kPlayerAssist:
        return 1;
    case SponsorChallengeType::kNonAttackerGoal:
        return 1;
    case SponsorChallengeType::kDifferentScorers:
        return std::clamp<int16_t>(rawThreshold, 3, 4);
    case SponsorChallengeType::kHatTrickHero:
        return 3;
    case SponsorChallengeType::kSetPieceGoal:
        return 1;
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
        return count >= std::max<int>(2, challenge.thresholdValue);
    }

    case SponsorChallengeType::kFirstHalfGoals:
        return result.clubHtGoals >= std::max<int>(1, challenge.thresholdValue);

    case SponsorChallengeType::kPossessionWin:
        return (result.clubGoals > result.opponentGoals) &&
               (result.clubPossessionPct >= std::max<int>(60, challenge.thresholdValue));

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

    case SponsorChallengeType::kNoCardsWin:
        return (result.clubGoals > result.opponentGoals) &&
               (result.clubYellowCards == 0) && (result.clubRedCards == 0);

    case SponsorChallengeType::kEarlyGoal:
        return result.clubEarlyGoals >= 1;

    case SponsorChallengeType::kBothHalvesGoal: {
        int secondHalf = std::max(0, result.clubGoals - result.clubHtGoals);
        return (result.clubHtGoals >= 1) && (secondHalf >= 1);
    }

    case SponsorChallengeType::kSecondHalfGoals: {
        int secondHalf = std::max(0, result.clubGoals - result.clubHtGoals);
        return secondHalf >= std::max<int>(2, challenge.thresholdValue);
    }

    case SponsorChallengeType::kLateGoal:
        return result.clubLateGoals >= 1;

    case SponsorChallengeType::kLimitOppShots:
        return (result.clubGoals > result.opponentGoals) &&
               (result.opponentShotsOnTarget <= std::max<int>(0, challenge.thresholdValue));

    case SponsorChallengeType::kClinicalFinishing: {
        int pct = (result.clubShotsOnTarget > 0)
            ? (result.clubGoals * 100) / result.clubShotsOnTarget
            : 0;
        return (result.clubGoals >= 2) && (pct >= std::max<int>(50, challenge.thresholdValue));
    }

    case SponsorChallengeType::kPlayerAssist: {
        int count = 0;
        std::string targetUpper = toUpperAscii(challenge.targetPlayerName);
        for (const auto& entry : result.assistEntries) {
            bool shirtMatch = (challenge.targetShirtNumber > 0 && entry.first == challenge.targetShirtNumber);
            bool nameMatch = (!targetUpper.empty() && toUpperAscii(entry.second) == targetUpper);
            if (shirtMatch || nameMatch)
                ++count;
        }
        return count >= std::max<int>(1, challenge.thresholdValue);
    }

    case SponsorChallengeType::kNonAttackerGoal:
        return result.clubNonAttackerGoals >= 1;

    case SponsorChallengeType::kDifferentScorers: {
        std::set<std::string> uniqueScorers;
        for (const auto& entry : result.scorerEntries) {
            std::string key = !entry.second.empty() ? toUpperAscii(entry.second) : std::to_string(entry.first);
            uniqueScorers.insert(key);
        }
        return static_cast<int>(uniqueScorers.size()) >= std::max<int>(3, challenge.thresholdValue);
    }

    case SponsorChallengeType::kHatTrickHero: {
        std::map<std::string, int> counts;
        int maxGoals = 0;
        for (const auto& entry : result.scorerEntries) {
            std::string key = !entry.second.empty() ? toUpperAscii(entry.second) : std::to_string(entry.first);
            maxGoals = std::max(maxGoals, ++counts[key]);
        }
        return maxGoals >= std::max<int>(3, challenge.thresholdValue);
    }

    case SponsorChallengeType::kSetPieceGoal:
        return result.clubSetPieceGoals >= 1;

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

    if (eval.failedCount == 0 && eval.baseNetDelta > 0) {
        int jpPct = computeJackpotPct(eval.completedCount, totalOfferedCount);
        if (jpPct > 0) {
            eval.allInJackpotTriggered = true;
            eval.jackpotPct = jpPct;
            eval.jackpotBonus = (eval.baseNetDelta * jpPct) / 100;
        }
    }

    eval.totalNetDelta = eval.baseNetDelta + eval.jackpotBonus;
    return eval;
}

MultiMatchContract generateOfflineMultiMatchContract(int divisionTier, uint32_t seed)
{
    int32_t baseMid = (divisionTier <= 0) ? 75000 : (divisionTier == 1 ? 45000 : 28000);
    int32_t baseHigh = (divisionTier <= 0) ? 110000 : (divisionTier == 1 ? 65000 : 42000);

    MultiMatchContract mc;
    mc.isActive = false;
    uint32_t pick = seed % 5u;
    switch (pick) {
    case 0u:
        mc.type = MultiMatchContractType::kWinStreak;
        mc.title = "HOT STREAK";
        mc.description = "Win 5 of your next 5 matches in a row.";
        mc.matchesTotal = 5;
        mc.targetValue = 5;
        mc.perMatchMin = 0;
        mc.rewardAmount = clampMultiMatchRewardForDivision(baseHigh, divisionTier);
        break;
    case 1u:
        mc.type = MultiMatchContractType::kUnbeatenRun;
        mc.title = "UNBEATEN RUN";
        mc.description = "Go unbeaten (win or draw) across your next 3 matches.";
        mc.matchesTotal = 3;
        mc.targetValue = 3;
        mc.perMatchMin = 0;
        mc.rewardAmount = clampMultiMatchRewardForDivision(baseMid, divisionTier);
        break;
    case 2u:
        mc.type = MultiMatchContractType::kGoalsInMatches;
        mc.title = "GOAL MACHINE";
        mc.description = "Score 10 or more total goals across your next 3 matches.";
        mc.matchesTotal = 3;
        mc.targetValue = 10;
        mc.perMatchMin = 0;
        mc.rewardAmount = clampMultiMatchRewardForDivision(baseHigh - 8000, divisionTier);
        break;
    case 3u:
        mc.type = MultiMatchContractType::kCleanSheetsInMatches;
        mc.title = "IRON FORTRESS";
        mc.description = "Keep 2 clean sheets across your next 3 matches.";
        mc.matchesTotal = 3;
        mc.targetValue = 2;
        mc.perMatchMin = 0;
        mc.rewardAmount = clampMultiMatchRewardForDivision(baseMid + 4000, divisionTier);
        break;
    default:
        mc.type = MultiMatchContractType::kMinGoalsEachMatch;
        mc.title = "SCORING CONSISTENCY";
        mc.description = "Score at least 2 goals in each of your next 5 matches.";
        mc.matchesTotal = 5;
        mc.targetValue = 5;
        mc.perMatchMin = 2;
        mc.rewardAmount = clampMultiMatchRewardForDivision(baseHigh, divisionTier);
        break;
    }
    mc.penaltyAmount = ((mc.rewardAmount / 2 + 500) / 1000) * 1000;
    return mc;
}

MultiMatchContractUpdate advanceMultiMatchContract(MultiMatchContract& contract, int clubGoals, int opponentGoals)
{
    MultiMatchContractUpdate upd;
    if (!contract.isActive || contract.type == MultiMatchContractType::kNone)
        return upd;

    upd.title = contract.title;
    upd.description = contract.description;
    ++contract.matchesPlayed;

    bool won = (clubGoals > opponentGoals);
    bool unbeaten = (clubGoals >= opponentGoals);
    bool cleanSheet = (opponentGoals == 0);

    bool completed = false;
    bool failed = false;

    switch (contract.type) {
    case MultiMatchContractType::kWinStreak:
        if (won) {
            ++contract.currentProgress;
            if (contract.currentProgress >= contract.targetValue)
                completed = true;
        } else {
            failed = true; // Must win 5 of 5 -> any non-win immediately breaches
        }
        break;

    case MultiMatchContractType::kUnbeatenRun:
        if (unbeaten) {
            ++contract.currentProgress;
            if (contract.currentProgress >= contract.targetValue)
                completed = true;
        } else {
            failed = true;
        }
        break;

    case MultiMatchContractType::kGoalsInMatches:
        contract.currentProgress = static_cast<uint16_t>(contract.currentProgress + std::max(0, clubGoals));
        if (contract.currentProgress >= contract.targetValue)
            completed = true;
        else if (contract.matchesPlayed >= contract.matchesTotal)
            failed = true;
        break;

    case MultiMatchContractType::kCleanSheetsInMatches:
        if (cleanSheet)
            ++contract.currentProgress;
        if (contract.currentProgress >= contract.targetValue) {
            completed = true;
        } else {
            int remaining = std::max(0, static_cast<int>(contract.matchesTotal) - static_cast<int>(contract.matchesPlayed));
            if (static_cast<int>(contract.currentProgress) + remaining < static_cast<int>(contract.targetValue))
                failed = true;
        }
        break;

    case MultiMatchContractType::kMinGoalsEachMatch: {
        int minG = std::max<int>(2, contract.perMatchMin);
        if (clubGoals >= minG) {
            ++contract.currentProgress;
            if (contract.currentProgress >= contract.targetValue)
                completed = true;
        } else {
            failed = true; // Must score >= 2 in EACH match
        }
        break;
    }

    default:
        failed = true;
        break;
    }

    if (completed) {
        upd.status = ContractStepStatus::kCompleted;
        upd.deltaCash = contract.rewardAmount;
        upd.progressText = "CONTRACT COMPLETED!";
        contract = MultiMatchContract{};
    } else if (failed) {
        upd.status = ContractStepStatus::kFailed;
        upd.deltaCash = -contract.penaltyAmount;
        upd.progressText = "CONTRACT BREACHED (50% FEE)";
        contract = MultiMatchContract{};
    } else {
        upd.status = ContractStepStatus::kInProgress;
        upd.deltaCash = 0;
        upd.progressText = formatContractProgressShort(contract);
    }

    return upd;
}

std::vector<SponsorChallenge> generateOfflineSponsorChallenges(
    const std::string& clubName, const std::string& opponentName,
    int divisionTier, const std::vector<SponsorStarterInfo>& starters, uint32_t seed,
    int desiredCount)
{
    (void)clubName;
    desiredCount = std::clamp(desiredCount, 1, kSponsorOfferedCount);

    int32_t baseSafe = (divisionTier <= 0) ? 17000 : (divisionTier == 1 ? 11000 : 6000);
    int32_t baseStd = (divisionTier <= 0) ? 24000 : (divisionTier == 1 ? 16000 : 9000);
    int32_t baseMid = (divisionTier <= 0) ? 32000 : (divisionTier == 1 ? 21000 : 12000);
    int32_t baseHigh = (divisionTier <= 0) ? 42000 : (divisionTier == 1 ? 27000 : 15000);
    int32_t baseWild = (divisionTier <= 0) ? 50000 : (divisionTier == 1 ? 30000 : 18000);

    bool hasPureAtt = (pickBestPureAttacker(starters) != nullptr);
    bool hasPlaymaker = (pickBestPlaymaker(starters, seed) != nullptr);

    // 5 curated pools (Safe, Standard, Tactical/Player, High-Risk Skill, Wildcard) covering all 21 types
    static constexpr SponsorChallengeType kPool1[] = {
        SponsorChallengeType::kNoCardsWin,
        SponsorChallengeType::kFirstHalfGoals,
        SponsorChallengeType::kTeamGoals,
        SponsorChallengeType::kBothHalvesGoal,
    };
    static constexpr SponsorChallengeType kPool2[] = {
        SponsorChallengeType::kCleanSheetWin,
        SponsorChallengeType::kEarlyGoal,
        SponsorChallengeType::kShotsOnTarget,
        SponsorChallengeType::kLateGoal,
    };
    static constexpr SponsorChallengeType kPool3[] = {
        SponsorChallengeType::kPossessionWin,
        SponsorChallengeType::kLimitOppShots,
        SponsorChallengeType::kSecondHalfGoals,
        SponsorChallengeType::kClinicalFinishing,
        SponsorChallengeType::kPlayerAssist,
    };
    static constexpr SponsorChallengeType kPool4[] = {
        SponsorChallengeType::kPlayerGoals,
        SponsorChallengeType::kLongRangeGoal,
        SponsorChallengeType::kHeaderGoal,
        SponsorChallengeType::kWinByMargin,
        SponsorChallengeType::kNonAttackerGoal,
    };
    static constexpr SponsorChallengeType kPool5[] = {
        SponsorChallengeType::kHatTrickHero,
        SponsorChallengeType::kSetPieceGoal,
        SponsorChallengeType::kDifferentScorers,
        SponsorChallengeType::kLongRangeGoal,
        SponsorChallengeType::kHeaderGoal,
    };

    std::vector<SponsorChallenge> out;
    auto hasType = [&](SponsorChallengeType t) {
        for (const auto& c : out) {
            if (c.type == t) return true;
        }
        return false;
    };

    auto pickFromPool = [&](const SponsorChallengeType *pool, size_t n, uint32_t s, int32_t reward) {
        for (size_t off = 0; off < n; ++off) {
            SponsorChallengeType t = pool[(s + off) % n];
            if (t == SponsorChallengeType::kPlayerGoals && !hasPureAtt) continue;
            if (t == SponsorChallengeType::kPlayerAssist && !hasPlaymaker) continue;
            if (hasType(t)) continue;
            out.push_back(buildChallengeForType(t, opponentName, divisionTier, starters, s, reward));
            return;
        }
    };

    if (desiredCount >= 1) pickFromPool(kPool1, std::size(kPool1), seed, baseSafe);
    if (desiredCount >= 2) pickFromPool(kPool2, std::size(kPool2), seed / 3u + 1u, baseStd);
    if (desiredCount >= 3) pickFromPool(kPool3, std::size(kPool3), seed / 5u + 2u, baseMid);
    if (desiredCount >= 4) pickFromPool(kPool4, std::size(kPool4), seed / 7u + 3u, baseHigh);
    if (desiredCount >= 5) pickFromPool(kPool5, std::size(kPool5), seed / 11u + 4u, baseWild);

    // Safety fill if any pool had duplicates
    static constexpr SponsorChallengeType kFallbackAll[] = {
        SponsorChallengeType::kCleanSheetWin, SponsorChallengeType::kTeamGoals,
        SponsorChallengeType::kFirstHalfGoals, SponsorChallengeType::kShotsOnTarget,
        SponsorChallengeType::kPossessionWin, SponsorChallengeType::kWinByMargin,
        SponsorChallengeType::kLongRangeGoal, SponsorChallengeType::kHeaderGoal,
        SponsorChallengeType::kNoCardsWin, SponsorChallengeType::kEarlyGoal,
        SponsorChallengeType::kBothHalvesGoal, SponsorChallengeType::kSecondHalfGoals,
        SponsorChallengeType::kLateGoal, SponsorChallengeType::kLimitOppShots,
        SponsorChallengeType::kClinicalFinishing, SponsorChallengeType::kNonAttackerGoal,
        SponsorChallengeType::kDifferentScorers, SponsorChallengeType::kHatTrickHero,
        SponsorChallengeType::kSetPieceGoal,
    };
    for (auto t : kFallbackAll) {
        if (static_cast<int>(out.size()) >= desiredCount) break;
        if (!hasType(t))
            out.push_back(buildChallengeForType(t, opponentName, divisionTier, starters, seed, baseMid));
    }

    return out;
}

std::vector<SponsorChallenge> parseLlmSponsorChallenges(
    const std::string& rawReply, const std::string& clubName, const std::string& opponentName,
    int divisionTier, const std::vector<SponsorStarterInfo>& starters, uint32_t seed,
    int desiredCount)
{
    desiredCount = std::clamp(desiredCount, 1, kSponsorOfferedCount);
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
    while (std::getline(stream, line) && static_cast<int>(parsed.size()) < desiredCount) {
        line = trimCopy(line);
        if (line.empty() || line.find('|') == std::string::npos)
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
        c.title = sanitizePlain(parts[1], 20);
        if (c.title.empty())
            c.title = defaultTitleForType(type);
        c.description = sanitizePlain(parts[2], 74);

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
            const SponsorStarterInfo *matched = matchStarterByName(starters, parts[3], true);
            if (!matched)
                matched = pickBestPureAttacker(starters);
            if (!matched)
                continue;
            c.targetShirtNumber = static_cast<int16_t>(matched->shirtNumber);
            c.targetPlayerName = matched->shortName;
            if (toUpperAscii(c.description).find(toUpperAscii(c.targetPlayerName)) == std::string::npos)
                c.description = defaultDescriptionForChallenge(c);
        } else if (type == SponsorChallengeType::kPlayerAssist) {
            const SponsorStarterInfo *matched = matchStarterByName(starters, parts[3], false);
            if (!matched)
                matched = pickBestPlaymaker(starters, seed);
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

    if (static_cast<int>(parsed.size()) < desiredCount) {
        auto fallback = generateOfflineSponsorChallenges(clubName, opponentName, divisionTier, starters, seed, desiredCount);
        for (auto& fb : fallback) {
            if (static_cast<int>(parsed.size()) >= desiredCount)
                break;
            if (!hasType(fb.type))
                parsed.push_back(std::move(fb));
        }
        if (static_cast<int>(parsed.size()) < desiredCount) {
            auto extra = generateOfflineSponsorChallenges(clubName, opponentName, divisionTier, starters, seed + 137u, kSponsorOfferedCount);
            for (auto& fb : extra) {
                if (static_cast<int>(parsed.size()) >= desiredCount)
                    break;
                if (!hasType(fb.type))
                    parsed.push_back(std::move(fb));
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
    s_state.lastContractUpdate = MultiMatchContractUpdate{};

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

    // If no multi-match contract is running, Slot 5 offers a Multi-Match Contract; otherwise all 5 are single-match!
    s_state.slot5IsContractOffer = !s_state.activeContract.isActive;
    int singleCount = s_state.slot5IsContractOffer ? (kSponsorOfferedCount - 1) : kSponsorOfferedCount;
    s_state.offeredSingleMatchCount = singleCount;

    auto applyVipSponsorBoost = [&](int32_t baseAmt) -> int32_t {
        int vipPct = getCurrentCareerCommercialSponsorBonusPct();
        if (vipPct <= 0) return baseAmt;
        int32_t boosted = baseAmt + (baseAmt * vipPct) / 100;
        return ((boosted + 500) / 1000) * 1000;
    };

    auto offline = generateOfflineSponsorChallenges(
        s_matchClubName, s_matchOpponentName, s_matchDivisionTier, s_matchStarters, s_matchSeed, singleCount);
    for (int i = 0; i < singleCount && i < static_cast<int>(offline.size()); ++i) {
        offline[i].rewardAmount = applyVipSponsorBoost(offline[i].rewardAmount);
        s_state.offered[i] = offline[i];
    }
    if (s_state.slot5IsContractOffer) {
        auto mc = generateOfflineMultiMatchContract(s_matchDivisionTier, s_matchSeed / 3u);
        mc.rewardAmount = applyVipSponsorBoost(mc.rewardAmount);
        mc.penaltyAmount = ((mc.rewardAmount / 2 + 500) / 1000) * 1000;
        s_state.offeredContract = std::move(mc);
    }
    s_state.offersReady = true;

    if (!cfg.enabled)
        return;

    std::string attList, midList;
    for (const auto& s : s_matchStarters) {
        std::string entry = s.shortName + (s.seasonGoals > 0 ? (" [" + std::to_string(s.seasonGoals) + "G]") : "");
        if (s.isPureAttacker) {
            if (!attList.empty()) attList += ", ";
            attList += entry;
        } else if (s.isMidfielder) {
            if (!midList.empty()) midList += ", ";
            midList += entry;
        }
    }

    int32_t minR = (s_matchDivisionTier <= 0) ? 15000 : (s_matchDivisionTier == 1 ? 10000 : 5000);
    int32_t maxR = (s_matchDivisionTier <= 0) ? 50000 : (s_matchDivisionTier == 1 ? 30000 : 18000);

    std::string prompt = "Club: " + s_matchClubName + " (Division tier " + std::to_string(s_matchDivisionTier + 1) +
                         ", Bank: " + formatSponsorPounds(readCurrentClubBalance()) + ").\n" +
                         "Opponent: " + s_matchOpponentName + ".\n" +
                         "Starting Attackers (ONLY valid for CHALLENGE_PLAYER_GOALS): " + (attList.empty() ? "NONE" : attList) + ".\n" +
                         "Starting Midfielders/Wingers: " + (midList.empty() ? "NONE" : midList) + ".\n" +
                         "Create " + std::to_string(singleCount) + " DISTINCT 1997 matchday sponsor bonus challenges for " + s_matchClubName + ".\n" +
                         "Allowed TYPE values (pick " + std::to_string(singleCount) + " DIFFERENT types from across safe, tactical, and bold skill tiers):\n" +
                         "- CHALLENGE_CLEAN_SHEET_WIN (win conceding 0; threshold 0)\n" +
                         "- CHALLENGE_NO_CARDS_WIN (win with 0 yellow & 0 red cards; threshold 0)\n" +
                         "- CHALLENGE_EARLY_GOAL (score in first 15 min; threshold 15)\n" +
                         "- CHALLENGE_BOTH_HALVES_GOAL (score in 1st AND 2nd half; threshold 1)\n" +
                         "- CHALLENGE_FIRST_HALF_GOALS (score X in 1st half; threshold 1 or 2)\n" +
                         "- CHALLENGE_SECOND_HALF_GOALS (score X in 2nd half; threshold 2)\n" +
                         "- CHALLENGE_LATE_GOAL (score in 75th min or later; threshold 75)\n" +
                         "- CHALLENGE_TEAM_GOALS (score X total goals; threshold 2, 3, or 4)\n" +
                         "- CHALLENGE_WIN_BY_MARGIN (win by X+ goals; threshold 2 or 3)\n" +
                         "- CHALLENGE_POSSESSION_WIN (win with >= X% possession; threshold 60 to 65)\n" +
                         "- CHALLENGE_SHOTS_ON_TARGET (record X+ shots on target; threshold 7 to 11)\n" +
                         "- CHALLENGE_LIMIT_OPP_SHOTS (win holding opponent <= 2 shots on target; threshold 2)\n" +
                         "- CHALLENGE_CLINICAL_FINISHING (score 2+ with >= 50% SOT conversion; threshold 50)\n" +
                         "- CHALLENGE_PLAYER_GOALS (ONLY a listed Starting Attacker scores 2+ goals; threshold 2)\n" +
                         "- CHALLENGE_PLAYER_ASSIST (listed starter records 1+ assist; threshold 1)\n" +
                         "- CHALLENGE_LONG_RANGE_GOAL (score outside 18-yard box; threshold 1)\n" +
                         "- CHALLENGE_HEADER_GOAL (score with a header; threshold 1)\n" +
                         "- CHALLENGE_NON_ATTACKER_GOAL (midfielder or defender scores; threshold 1)\n" +
                         "- CHALLENGE_DIFFERENT_SCORERS (3+ different scorers; threshold 3)\n" +
                         "- CHALLENGE_HAT_TRICK_HERO (any player scores 3+; threshold 3)\n" +
                         "- CHALLENGE_SET_PIECE_GOAL (score from corner or free kick; threshold 1)\n" +
                         "Scale REWARD between " + std::to_string(minR) + " and " + std::to_string(maxR) + " (integer pounds).\n" +
                         "Format STRICTLY one challenge per line as:\n" +
                         "TYPE|TITLE|DESCRIPTION|PLAYER_NAME|THRESHOLD|REWARD\n" +
                         "- TITLE: punchy 2-3 word 1997 challenge name (max 18 chars)\n" +
                         "- DESCRIPTION: crisp 1-sentence objective matching threshold/player (max 70 chars)\n" +
                         "- PLAYER_NAME: exact surname for PLAYER_GOALS/PLAYER_ASSIST, otherwise NONE\n" +
                         "Output ONLY the " + std::to_string(singleCount) + " lines. No intro, no markdown, no numbering.";

    s_pendingRequestId = enqueueLlmRequest(
        "You are a 1997 football commercial sponsor offering matchday bonus challenges to a club manager. "
        "Output strictly pipe-delimited lines in TYPE|TITLE|DESCRIPTION|PLAYER_NAME|THRESHOLD|REWARD format.",
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
            int singleCount = s_state.slot5IsContractOffer ? (kSponsorOfferedCount - 1) : kSponsorOfferedCount;
            auto parsed = parseLlmSponsorChallenges(
                reply, s_matchClubName, s_matchOpponentName, s_matchDivisionTier, s_matchStarters, s_matchSeed, singleCount);
            int vipPct = getCurrentCareerCommercialSponsorBonusPct();
            for (int i = 0; i < singleCount && i < static_cast<int>(parsed.size()); ++i) {
                if (vipPct > 0) {
                    int32_t boosted = parsed[i].rewardAmount + (parsed[i].rewardAmount * vipPct) / 100;
                    parsed[i].rewardAmount = ((boosted + 500) / 1000) * 1000;
                }
                s_state.offered[i] = parsed[i];
            }
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
    s_state.lastMatchEval = SponsorBatchEvaluation{};
    s_state.lastContractUpdate = MultiMatchContractUpdate{};

    if (!s_matchEligible || swos.gameCanceled || swos.g_trainingGame)
        return;

    syncTotalsFromHotseatIfActive();

    if (s_state.activeChallenges.empty() && !s_state.activeContract.isActive)
        return;

    MatchTelemetrySnapshot snap = captureMatchTelemetrySnapshot();
    const auto stats = getStats();
    const TeamGame& humanTg = s_humanIsTeam1 ? swos.topTeamInGame : swos.bottomTeamInGame;
    const auto& humanStats = s_humanIsTeam1 ? stats.team1 : stats.team2;
    const auto& oppStats = s_humanIsTeam1 ? stats.team2 : stats.team1;

    auto [early1, early2] = getEarlyTelemetryGoals();
    auto [late1, late2] = getLateTelemetryGoals();
    auto [sp1, sp2] = getSetPieceTelemetryGoals();

    SponsorMatchResult result;
    result.clubGoals = s_humanIsTeam1 ? snap.goals1 : snap.goals2;
    result.opponentGoals = s_humanIsTeam1 ? snap.goals2 : snap.goals1;
    result.clubHtGoals = s_humanIsTeam1 ? snap.htGoals1 : snap.htGoals2;
    result.opponentHtGoals = s_humanIsTeam1 ? snap.htGoals2 : snap.htGoals1;
    result.clubPossessionPct = s_humanIsTeam1 ? snap.possession1 : snap.possession2;
    result.clubShotsOnTarget = s_humanIsTeam1 ? snap.onTarget1 : snap.onTarget2;
    result.opponentShotsOnTarget = oppStats.onTarget;
    result.clubYellowCards = humanStats.bookings;
    result.clubRedCards = humanStats.sendingsOff;
    result.clubLongRangeGoals = s_humanIsTeam1 ? snap.longRangeGoals1 : snap.longRangeGoals2;
    result.clubHeaderGoals = s_humanIsTeam1 ? snap.headerGoals1 : snap.headerGoals2;
    result.clubEarlyGoals = s_humanIsTeam1 ? early1 : early2;
    result.clubLateGoals = s_humanIsTeam1 ? late1 : late2;
    result.clubSetPieceGoals = s_humanIsTeam1 ? sp1 : sp2;
    result.assistEntries = getMatchAssistEntries(s_humanIsTeam1 ? 1 : 2);

    for (int i = 0; i < kNumPlayersInTeam; ++i) {
        const auto& pl = humanTg.players[i];
        int gCount = static_cast<int>(pl.goalsScored);
        if (gCount <= 0)
            continue;
        bool isNonAtt = (pl.position != PlayerPosition::kAttacker);
        if (isNonAtt)
            result.clubNonAttackerGoals += gCount;
        for (int g = 0; g < gCount; ++g) {
            result.scorerEntries.emplace_back(static_cast<int>(pl.shirtNumber), std::string(pl.shortName));
        }
    }

    int32_t netCashDelta = 0;
    int newlyCompleted = 0;
    int newlyFailed = 0;

    if (!s_state.activeChallenges.empty()) {
        SponsorBatchEvaluation eval = evaluateSponsorChallengeBatch(
            s_state.activeChallenges, s_state.offeredSingleMatchCount, result);
        s_state.lastMatchEval = eval;
        netCashDelta += eval.totalNetDelta;
        newlyCompleted += eval.completedCount;
        newlyFailed += eval.failedCount;
    }

    if (s_state.activeContract.isActive) {
        MultiMatchContractUpdate cu = advanceMultiMatchContract(
            s_state.activeContract, result.clubGoals, result.opponentGoals);
        s_state.lastContractUpdate = cu;
        netCashDelta += cu.deltaCash;
        if (cu.status == ContractStepStatus::kCompleted)
            ++newlyCompleted;
        else if (cu.status == ContractStepStatus::kFailed)
            ++newlyFailed;
    }

    s_state.lastMatchHadChallenge = (!s_state.lastMatchEval.outcomes.empty() ||
                                     s_state.lastContractUpdate.status != ContractStepStatus::kNone);

    applyClubBalanceDelta(netCashDelta);
    s_state.challengesCompleted = static_cast<uint16_t>(s_state.challengesCompleted + newlyCompleted);
    s_state.challengesFailed = static_cast<uint16_t>(s_state.challengesFailed + newlyFailed);
    s_state.totalBonusEarned += netCashDelta;
    s_state.lastMatchNewBalance = readCurrentClubBalance();
    syncTotalsToHotseatIfActive();

    bool cleanSweepWon = (s_state.lastMatchEval.allInJackpotTriggered && s_state.lastMatchEval.jackpotPct >= 50);
    bool contractWon = (s_state.lastContractUpdate.status == ContractStepStatus::kCompleted);
    if (newlyCompleted > 0 || cleanSweepWon || contractWon || netCashDelta > 0) {
        achievementsOnSponsorChallengesResolved(
            static_cast<uint32_t>(newlyCompleted),
            cleanSweepWon,
            contractWon,
            netCashDelta > 0 ? static_cast<int64_t>(netCashDelta) : 0);
    }

    logInfo("Sponsor challenges resolved: %d won, %d failed, jackpot=+%d%%, contractStatus=%u, net=%s, balance=%s",
        newlyCompleted, newlyFailed, s_state.lastMatchEval.jackpotPct,
        static_cast<unsigned>(s_state.lastContractUpdate.status),
        formatSignedPounds(netCashDelta).c_str(),
        formatSponsorPounds(s_state.lastMatchNewBalance).c_str());

    s_state.activeChallenges.clear();
}

void onViewResultSponsorContractUpdate(int clubGoals, int opponentGoals)
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame || swos.gameCanceled)
        return;
    syncTotalsFromHotseatIfActive();
    if (!s_state.activeContract.isActive)
        return;

    MultiMatchContractUpdate cu = advanceMultiMatchContract(s_state.activeContract, clubGoals, opponentGoals);
    if (cu.status == ContractStepStatus::kNone)
        return;

    applyClubBalanceDelta(cu.deltaCash);
    if (cu.status == ContractStepStatus::kCompleted) {
        ++s_state.challengesCompleted;
        achievementsOnSponsorChallengesResolved(1, false, true, cu.deltaCash > 0 ? cu.deltaCash : 0);
    } else if (cu.status == ContractStepStatus::kFailed) {
        ++s_state.challengesFailed;
    }
    s_state.totalBonusEarned += cu.deltaCash;
    syncTotalsToHotseatIfActive();

    logInfo("View Result advanced sponsor contract '%s': status=%u (%s) delta=%s",
        cu.title.c_str(), static_cast<unsigned>(cu.status),
        cu.progressText.c_str(), formatSignedPounds(cu.deltaCash).c_str());
}

void showPostMatchSponsorPayoutIfNeeded()
{
    if (!s_state.lastMatchHadChallenge || swos.gameCanceled || swos.g_trainingGame)
        return;

#ifndef SWOS_TEST
    const auto& eval = s_state.lastMatchEval;
    const auto& cu = s_state.lastContractUpdate;
    int32_t combinedDelta = eval.totalNetDelta + cu.deltaCash;

    std::string headline;
    if (eval.allInJackpotTriggered && eval.jackpotPct >= 50) {
        headline = "ALL-IN CLEAN SWEEP! +" + std::to_string(eval.jackpotPct) + "% JACKPOT WON!";
    } else if (eval.allInJackpotTriggered) {
        headline = "COMBO BONUS! +" + std::to_string(eval.jackpotPct) + "% EXTRA WON!";
    } else if (cu.status == ContractStepStatus::kCompleted && eval.outcomes.empty()) {
        headline = "MULTI-MATCH CONTRACT COMPLETED! " + formatSignedPounds(cu.deltaCash);
    } else if (combinedDelta > 0) {
        headline = "SPONSOR CHALLENGES: NET PROFIT " + formatSignedPounds(combinedDelta);
    } else if (combinedDelta < 0) {
        headline = "SPONSOR CHALLENGES: NET PENALTY " + formatSignedPounds(combinedDelta);
    } else {
        headline = "SPONSOR CHALLENGES: STATUS UPDATE";
    }

    std::string body = headline + "\n\n";
    for (const auto& o : eval.outcomes) {
        body += std::string(o.completed ? "[WON] " : "[FAILED] ") +
                toUpperAscii(o.title) + ": " + formatSignedPounds(o.deltaCash) + "\n";
        body += "  " + o.description + "\n";
    }
    if (eval.allInJackpotTriggered) {
        body += "\n" + std::string(eval.jackpotPct >= 50 ? "CLEAN SWEEP BONUS (+" : "COMBO BONUS (+") +
                std::to_string(eval.jackpotPct) + "%): " + formatSignedPounds(eval.jackpotBonus) + "\n";
    }
    if (cu.status != ContractStepStatus::kNone) {
        const char *tag = (cu.status == ContractStepStatus::kCompleted) ? "[CONTRACT WON] " :
                          (cu.status == ContractStepStatus::kFailed) ? "[CONTRACT FAILED] " : "[CONTRACT] ";
        body += "\n" + std::string(tag) + toUpperAscii(cu.title) + ": " + cu.progressText;
        if (cu.deltaCash != 0)
            body += " (" + formatSignedPounds(cu.deltaCash) + ")";
        body += "\n  " + cu.description + "\n";
    }

    body += "\nNET MATCHDAY SPONSOR P/L: " + formatSignedPounds(combinedDelta) + "\n";
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
    return s_matchEligible && (!s_state.activeChallenges.empty() || s_state.activeContract.isActive) && !swos.g_trainingGame;
}

static std::string describeSingleChallengeProgress(const SponsorChallenge& c, int& outColorMode)
{
    outColorMode = 0; // 0 = gold/live, 1 = green/achieved, 2 = red/failed
    const auto stats = getStats();
    const auto& humanStats = s_humanIsTeam1 ? stats.team1 : stats.team2;
    const auto& oppStats = s_humanIsTeam1 ? stats.team2 : stats.team1;
    int clubG = s_humanIsTeam1 ? swos.statsTeam1Goals : swos.statsTeam2Goals;
    int oppG = s_humanIsTeam1 ? swos.statsTeam2Goals : swos.statsTeam1Goals;
    int totalPoss = stats.team1.ballPossession + stats.team2.ballPossession;
    int poss1Pct = totalPoss > 0 ? (100 * stats.team1.ballPossession) / totalPoss : 50;
    int poss = s_humanIsTeam1 ? poss1Pct : (100 - poss1Pct);
    int sot = humanStats.onTarget;
    int oppSot = oppStats.onTarget;
    int curMin = static_cast<int>(gameTimeInMinutes());
    const TeamGame& tg = s_humanIsTeam1 ? swos.topTeamInGame : swos.bottomTeamInGame;

    switch (c.type) {
    case SponsorChallengeType::kCleanSheetWin:
        if (oppG > 0) { outColorMode = 2; return "LOST"; }
        if (clubG > oppG) { outColorMode = 1; return "ON TRACK"; }
        return "0-0";
    case SponsorChallengeType::kPlayerGoals: {
        int scored = 0;
        std::string targetUpper = toUpperAscii(c.targetPlayerName);
        for (int i = 0; i < kNumPlayersInTeam; ++i) {
            if ((c.targetShirtNumber > 0 && tg.players[i].shirtNumber == c.targetShirtNumber) ||
                (!targetUpper.empty() && toUpperAscii(tg.players[i].shortName) == targetUpper))
                scored += tg.players[i].goalsScored;
        }
        int need = std::max<int>(2, c.thresholdValue);
        if (scored >= need) outColorMode = 1;
        return std::to_string(scored) + "/" + std::to_string(need);
    }
    case SponsorChallengeType::kFirstHalfGoals:
        if (swos.halfNumber <= 1) {
            if (clubG >= c.thresholdValue) outColorMode = 1;
            return "HT " + std::to_string(clubG) + "/" + std::to_string(c.thresholdValue);
        } else {
            auto [ht1, ht2] = getHalfTimeTelemetryGoals();
            int htG = s_humanIsTeam1 ? ht1 : ht2;
            if (htG >= c.thresholdValue) { outColorMode = 1; return "HT WON"; }
            outColorMode = 2; return "HT LOST";
        }
    case SponsorChallengeType::kPossessionWin:
        if (clubG > oppG && poss >= std::max<int>(60, c.thresholdValue)) outColorMode = 1;
        return std::to_string(poss) + "% (" + std::to_string(std::max<int>(60, c.thresholdValue)) + "%+)";
    case SponsorChallengeType::kShotsOnTarget:
        if (sot >= c.thresholdValue) outColorMode = 1;
        return std::to_string(sot) + "/" + std::to_string(c.thresholdValue) + " SOT";
    case SponsorChallengeType::kWinByMargin: {
        int diff = clubG - oppG;
        if (diff >= c.thresholdValue) outColorMode = 1;
        return (diff >= 0 ? "+" : "") + std::to_string(diff) + "/+" + std::to_string(c.thresholdValue);
    }
    case SponsorChallengeType::kTeamGoals:
        if (clubG >= c.thresholdValue) outColorMode = 1;
        return std::to_string(clubG) + "/" + std::to_string(c.thresholdValue) + " G";
    case SponsorChallengeType::kLongRangeGoal: {
        auto [lr1, lr2] = getLongRangeTelemetryGoals();
        int lrG = s_humanIsTeam1 ? lr1 : lr2;
        if (lrG >= c.thresholdValue) outColorMode = 1;
        return std::to_string(lrG) + "/" + std::to_string(c.thresholdValue) + " LONG";
    }
    case SponsorChallengeType::kHeaderGoal: {
        auto [hd1, hd2] = getHeaderTelemetryGoals();
        int hdG = s_humanIsTeam1 ? hd1 : hd2;
        if (hdG >= c.thresholdValue) outColorMode = 1;
        return std::to_string(hdG) + "/" + std::to_string(c.thresholdValue) + " HEAD";
    }
    case SponsorChallengeType::kNoCardsWin:
        if (humanStats.bookings > 0 || humanStats.sendingsOff > 0) { outColorMode = 2; return "CARD!"; }
        if (clubG > oppG) { outColorMode = 1; return "CLEAN"; }
        return "0 CARDS";
    case SponsorChallengeType::kEarlyGoal: {
        auto [e1, e2] = getEarlyTelemetryGoals();
        int eg = s_humanIsTeam1 ? e1 : e2;
        if (eg >= 1) { outColorMode = 1; return "WON"; }
        if (curMin > 15) { outColorMode = 2; return "MISSED"; }
        return std::to_string(curMin) + "'/15'";
    }
    case SponsorChallengeType::kBothHalvesGoal: {
        auto [ht1, ht2] = getHalfTimeTelemetryGoals();
        int htG = s_humanIsTeam1 ? ht1 : ht2;
        if (swos.halfNumber <= 1) {
            return (clubG >= 1) ? "1H OK" : "0/1 1H";
        }
        int shG = std::max(0, clubG - htG);
        if (htG == 0) { outColorMode = 2; return "1H MISS"; }
        if (shG >= 1) { outColorMode = 1; return "BOTH OK"; }
        return "NEED 2H";
    }
    case SponsorChallengeType::kSecondHalfGoals: {
        auto [ht1, ht2] = getHalfTimeTelemetryGoals();
        int htG = s_humanIsTeam1 ? ht1 : ht2;
        int shG = (swos.halfNumber <= 1) ? 0 : std::max(0, clubG - htG);
        int need = std::max<int>(2, c.thresholdValue);
        if (shG >= need) outColorMode = 1;
        return "2H " + std::to_string(shG) + "/" + std::to_string(need);
    }
    case SponsorChallengeType::kLateGoal: {
        auto [l1, l2] = getLateTelemetryGoals();
        int lg = s_humanIsTeam1 ? l1 : l2;
        if (lg >= 1) { outColorMode = 1; return "WON"; }
        return (curMin >= 75) ? "LIVE 75+" : "WAIT 75'";
    }
    case SponsorChallengeType::kLimitOppShots:
        if (oppSot > c.thresholdValue) { outColorMode = 2; return std::to_string(oppSot) + " SOT!"; }
        if (clubG > oppG) outColorMode = 1;
        return "OPP " + std::to_string(oppSot) + "/" + std::to_string(c.thresholdValue);
    case SponsorChallengeType::kClinicalFinishing: {
        int pct = (sot > 0) ? (clubG * 100) / sot : 0;
        if (clubG >= 2 && pct >= c.thresholdValue) outColorMode = 1;
        return std::to_string(clubG) + "G " + std::to_string(pct) + "%";
    }
    case SponsorChallengeType::kPlayerAssist: {
        auto assists = getMatchAssistEntries(s_humanIsTeam1 ? 1 : 2);
        int cnt = 0;
        std::string targetUpper = toUpperAscii(c.targetPlayerName);
        for (const auto& a : assists) {
            if ((c.targetShirtNumber > 0 && a.first == c.targetShirtNumber) ||
                (!targetUpper.empty() && toUpperAscii(a.second) == targetUpper))
                ++cnt;
        }
        if (cnt >= 1) outColorMode = 1;
        return std::to_string(cnt) + "/1 AST";
    }
    case SponsorChallengeType::kNonAttackerGoal: {
        int cnt = 0;
        for (int i = 0; i < kNumPlayersInTeam; ++i) {
            if (tg.players[i].position != PlayerPosition::kAttacker)
                cnt += tg.players[i].goalsScored;
        }
        if (cnt >= 1) outColorMode = 1;
        return std::to_string(cnt) + "/1 MID/D";
    }
    case SponsorChallengeType::kDifferentScorers: {
        int uniq = 0;
        for (int i = 0; i < kNumPlayersInTeam; ++i) {
            if (tg.players[i].goalsScored > 0) ++uniq;
        }
        int need = std::max<int>(3, c.thresholdValue);
        if (uniq >= need) outColorMode = 1;
        return std::to_string(uniq) + "/" + std::to_string(need) + " PLR";
    }
    case SponsorChallengeType::kHatTrickHero: {
        int mx = 0;
        for (int i = 0; i < kNumPlayersInTeam; ++i)
            mx = std::max<int>(mx, tg.players[i].goalsScored);
        if (mx >= 3) outColorMode = 1;
        return std::to_string(mx) + "/3 HAT";
    }
    case SponsorChallengeType::kSetPieceGoal: {
        auto [sp1, sp2] = getSetPieceTelemetryGoals();
        int spG = s_humanIsTeam1 ? sp1 : sp2;
        if (spG >= 1) outColorMode = 1;
        return std::to_string(spG) + "/1 SET";
    }
    default:
        return "ACTIVE";
    }
}

std::string formatActiveSponsorChallengeForTelemetry()
{
    if (!hasActiveSponsorChallenge())
        return {};

    int jpPct = computeJackpotPct(static_cast<int>(s_state.activeChallenges.size()), s_state.offeredSingleMatchCount);
    std::string out = "Active Sponsor Challenges for " + s_matchClubName + " (failing deducts the stake";
    if (jpPct > 0)
        out += "; winning all selected pays +" + std::to_string(jpPct) + "% BONUS!";
    out += "):";

    for (const auto& c : s_state.activeChallenges) {
        int mode = 0;
        std::string status = describeSingleChallengeProgress(c, mode);
        out += " [" + c.title + ": " + c.description + " Stake +/-" + formatSponsorPounds(c.rewardAmount) +
               " -> " + status + "]";
    }
    if (s_state.activeContract.isActive) {
        out += " [MULTI-MATCH CONTRACT: " + s_state.activeContract.title + " (" +
               formatContractProgressShort(s_state.activeContract) + ") Reward +" +
               formatSponsorPounds(s_state.activeContract.rewardAmount) + "]";
    }
    return out;
}

std::vector<std::pair<std::string, int>> formatActiveSponsorChallengeHudLines()
{
    std::vector<std::pair<std::string, int>> lines;
    if (!hasActiveSponsorChallenge())
        return lines;

    int jpPct = computeJackpotPct(static_cast<int>(s_state.activeChallenges.size()), s_state.offeredSingleMatchCount);
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

    if (jpPct > 0) {
        int activeCount = static_cast<int>(s_state.activeChallenges.size());
        int jpMode = anyRed ? 2 : (allGreen == activeCount ? 1 : 0);
        std::string tag = (jpPct >= 50 ? "SWEEP +" : "COMBO +") + std::to_string(jpPct) + "%: ";
        std::string jpText = tag + (anyRed ? "LOST" : (allGreen == activeCount ? "ON TRACK!" : "ACTIVE"));
        lines.emplace_back(std::move(jpText), jpMode);
    }

    if (s_state.activeContract.isActive) {
        std::string prog = formatContractProgressShort(s_state.activeContract);
        std::string suffix = " [" + prog + "]";
        std::string titleShort = "CONTRACT: " + s_state.activeContract.title;
        while (!titleShort.empty() && measureCompactTextUnits(titleShort + suffix) > kMaxHudLineUnits) {
            titleShort.pop_back();
            while (!titleShort.empty() && titleShort.back() == ' ')
                titleShort.pop_back();
        }
        lines.emplace_back(titleShort + suffix, 0);
    }

    return lines;
}

std::string formatLastSponsorChallengeOutcomeForNewspaper()
{
    if (!s_state.lastMatchHadChallenge)
        return {};
    const auto& eval = s_state.lastMatchEval;
    const auto& cu = s_state.lastContractUpdate;
    std::string out = "Sponsor Challenges Outcome for " + s_matchClubName + ": " +
                      std::to_string(eval.completedCount) + " completed, " +
                      std::to_string(eval.failedCount) + " failed";
    if (eval.allInJackpotTriggered) {
        out += " — won an extra +" + std::to_string(eval.jackpotPct) + "% bonus (" +
               formatSignedPounds(eval.jackpotBonus) + ")";
    }
    if (cu.status == ContractStepStatus::kCompleted) {
        out += ". Completed multi-match contract '" + cu.title + "' (" + formatSignedPounds(cu.deltaCash) + ")";
    } else if (cu.status == ContractStepStatus::kFailed) {
        out += ". Breached multi-match contract '" + cu.title + "' (" + formatSignedPounds(cu.deltaCash) + ")";
    }
    out += ". Net financial impact: " + formatSignedPounds(eval.totalNetDelta + cu.deltaCash) + ".";
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

    const auto& mc = s_state.activeContract;
    ini.SetBoolValue("contract", "active", mc.isActive);
    if (mc.isActive) {
        ini.SetLongValue("contract", "type", static_cast<long>(mc.type));
        ini.SetValue("contract", "title", mc.title.c_str());
        ini.SetValue("contract", "description", mc.description.c_str());
        ini.SetLongValue("contract", "rewardAmount", static_cast<long>(mc.rewardAmount));
        ini.SetLongValue("contract", "penaltyAmount", static_cast<long>(mc.penaltyAmount));
        ini.SetLongValue("contract", "matchesTotal", static_cast<long>(mc.matchesTotal));
        ini.SetLongValue("contract", "matchesPlayed", static_cast<long>(mc.matchesPlayed));
        ini.SetLongValue("contract", "targetValue", static_cast<long>(mc.targetValue));
        ini.SetLongValue("contract", "currentProgress", static_cast<long>(mc.currentProgress));
        ini.SetLongValue("contract", "perMatchMin", static_cast<long>(mc.perMatchMin));
    }
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

    MultiMatchContract mc;
    if (ini.GetBoolValue("contract", "active", false)) {
        long t = ini.GetLongValue("contract", "type", 0);
        if (t >= 1 && t <= 5) {
            mc.isActive = true;
            mc.type = static_cast<MultiMatchContractType>(t);
            mc.title = ini.GetValue("contract", "title", "SPONSOR CONTRACT");
            mc.description = ini.GetValue("contract", "description", "");
            mc.rewardAmount = static_cast<int32_t>(std::max(0L, ini.GetLongValue("contract", "rewardAmount", 0)));
            mc.penaltyAmount = static_cast<int32_t>(std::max(0L, ini.GetLongValue("contract", "penaltyAmount", 0)));
            mc.matchesTotal = static_cast<uint8_t>(std::clamp(ini.GetLongValue("contract", "matchesTotal", 3), 1L, 10L));
            mc.matchesPlayed = static_cast<uint8_t>(std::clamp(ini.GetLongValue("contract", "matchesPlayed", 0), 0L, 10L));
            mc.targetValue = static_cast<uint16_t>(std::max(0L, ini.GetLongValue("contract", "targetValue", 0)));
            mc.currentProgress = static_cast<uint16_t>(std::max(0L, ini.GetLongValue("contract", "currentProgress", 0)));
            mc.perMatchMin = static_cast<uint8_t>(std::clamp(ini.GetLongValue("contract", "perMatchMin", 0), 0L, 10L));
        }
    }
    s_state.activeContract = std::move(mc);
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

MultiMatchContract getActiveSponsorContract()
{
    syncTotalsFromHotseatIfActive();
    return s_state.activeContract;
}

void setActiveSponsorContract(const MultiMatchContract& contract)
{
    s_state.activeContract = contract;
    syncTotalsToHotseatIfActive();
}
