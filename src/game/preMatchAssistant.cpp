#include "preMatchAssistant.h"
#include "compactFont.h"
#include "controls.h"
#include "darkRectangle.h"
#include "drawMenu.h"
#include "gameControls.h"
#include "gameFieldMapping.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "keyBuffer.h"
#include "llmClient.h"
#include "llmReportMenu.h"
#include "log.h"
#include "matchTelemetry.h"
#include "menuBackground.h"
#include "menus.h"
#include "pitch.h"
#include "pressCharisma.h"
#include "render.h"
#include "stadiumFacilities.h"
#include "swos.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace {

constexpr uint8_t kMinSwosSkill = 0;
constexpr uint8_t kMaxSwosSkill = 7;

struct AssistantMatchSession {
    bool eligible = false;
    bool humanIsTeam1 = true;
    bool isCareer = false;
    int divisionTier = 0;
    PreMatchScoutIntel intel;
    PreMatchPrepPlan plan;
    PreMatchInjuredCandidate injured;
    std::vector<PreMatchSpotlightCandidate> spotlightCandidates;

    uint64_t llmTipRequestId = 0;
    std::string llmTipText;

    // Post-match payoff popup lines (only shown if notable outcome occurred)
    std::vector<std::string> postMatchPayoffLines;
};

AssistantMatchSession s_session;

uint8_t clampSkill(int v)
{
    return static_cast<uint8_t>(std::clamp(v, static_cast<int>(kMinSwosSkill), static_cast<int>(kMaxSwosSkill)));
}

void adjustSkill(byte& field, int delta)
{
    field = clampSkill(static_cast<int>(field) + delta);
}

bool isDefenderPos(PlayerPosition pos)
{
    return pos == PlayerPosition::kDefender || pos == PlayerPosition::kLeftBack || pos == PlayerPosition::kRightBack;
}

bool isMidOrWingPos(PlayerPosition pos)
{
    return pos == PlayerPosition::kMidfielder || pos == PlayerPosition::kLeftWing || pos == PlayerPosition::kRightWing;
}

bool isAttOrWingPos(PlayerPosition pos)
{
    return pos == PlayerPosition::kAttacker || pos == PlayerPosition::kLeftWing || pos == PlayerPosition::kRightWing;
}

const char *shortPosName(PlayerPosition pos)
{
    switch (pos) {
    case PlayerPosition::kGoalkeeper: return "GK";
    case PlayerPosition::kRightBack: return "RB";
    case PlayerPosition::kLeftBack: return "LB";
    case PlayerPosition::kDefender: return "DEF";
    case PlayerPosition::kRightWing: return "RW";
    case PlayerPosition::kLeftWing: return "LW";
    case PlayerPosition::kMidfielder: return "MID";
    case PlayerPosition::kAttacker: return "ATT";
    default: return "SUB";
    }
}

std::string toUpperAscii(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string cleanName(const char *raw, size_t maxLen)
{
    std::string out;
    for (size_t i = 0; i < maxLen && raw[i] != '\0'; ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c >= 0x20 && c <= 0x7E)
            out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string formatPoundsShort(int64_t amt)
{
    bool neg = amt < 0;
    int64_t a = neg ? -amt : amt;
    char buf[48];
    if (a >= 1000000)
        std::snprintf(buf, sizeof(buf), "%sGBP %.2fM", neg ? "-" : "", static_cast<double>(a) / 1000000.0);
    else if (a >= 1000)
        std::snprintf(buf, sizeof(buf), "%sGBP %lldK", neg ? "-" : "", static_cast<long long>(a / 1000));
    else
        std::snprintf(buf, sizeof(buf), "%sGBP %lld", neg ? "-" : "", static_cast<long long>(a));
    return buf;
}

std::string formatSwosPriceTier(uint8_t priceIdx)
{
    static constexpr int32_t kSwosPrices[] = {
        25000, 30000, 40000, 50000, 65000, 75000, 85000, 100000, 110000, 130000,
        150000, 160000, 180000, 200000, 225000, 250000, 275000, 300000, 350000, 400000,
        450000, 500000, 550000, 600000, 650000, 700000, 750000, 800000, 850000, 900000,
        950000, 1000000, 1100000, 1300000, 1500000, 1750000, 1900000, 2000000, 2250000, 2500000,
        2750000, 3000000, 3500000, 4000000, 4500000, 5000000, 6000000, 7000000, 8000000, 10000000
    };
    size_t idx = std::min<size_t>(priceIdx, std::size(kSwosPrices) - 1);
    return formatPoundsShort(kSwosPrices[idx]);
}

int32_t readVmClubCash()
{
    return *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
}

void applyVmClubCashDelta(int32_t delta)
{
    auto *vmCash = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    int64_t next = std::clamp<int64_t>(static_cast<int64_t>(*vmCash) + delta, -50000000LL, 2000000000LL);
    *vmCash = static_cast<int32_t>(next);

    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        mgr.activePlayer().newBalance = *vmCash;
    }
}

int readDivisionTier()
{
    const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
    if (hdr && hdr->league <= 3)
        return static_cast<int>(hdr->league);
    return 0;
}

bool resolveHumanSide(const TeamGame *topTeam, const TeamGame *bottomTeam, bool& outHumanIsTeam1)
{
    if (!topTeam || !bottomTeam)
        return false;

    const TeamFile *tf1 = swos.gameTeam1.asAligned();
    const TeamFile *tf2 = swos.gameTeam2.asAligned();
    if (!tf1) tf1 = swos.playMatchTeam1Ptr.asAligned();
    if (!tf2) tf2 = swos.playMatchTeam2Ptr.asAligned();

    bool t1Human = (swos.topTeamData.playerNumber != 0 || swos.topTeamData.playerCoachNumber != 0 || swos.topTeamData.isPlCoach != 0);
    bool t2Human = (swos.bottomTeamData.playerNumber != 0 || swos.bottomTeamData.playerCoachNumber != 0 || swos.bottomTeamData.isPlCoach != 0);
    if (!t1Human && !t2Human) {
        if (tf1) t1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
        if (tf2) t2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);
    }
    if (t1Human == t2Human && swos.g_gameType == kGameTypeCareer) {
        std::string careerClub;
        const auto& mgr = HotseatCareerManager::get();
        if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
            careerClub = toUpperAscii(mgr.activePlayer().teamName);
        } else {
            const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
            if (hdr && hdr->teamName[0] != '\0')
                careerClub = toUpperAscii(hdr->teamName);
        }
        if (!careerClub.empty()) {
            bool m1 = (toUpperAscii(topTeam->teamName) == careerClub);
            bool m2 = (toUpperAscii(bottomTeam->teamName) == careerClub);
            if (m1 != m2) {
                t1Human = m1;
                t2Human = m2;
            }
        }
    }
    if (t1Human == t2Human)
        return false;
    outHumanIsTeam1 = t1Human;
    return true;
}

void scanCareerInjuriesAndSpotlight(const TeamGame *humanTeam)
{
    s_session.injured = PreMatchInjuredCandidate{};
    s_session.spotlightCandidates.clear();

    if (!s_session.isCareer || HotseatInternal::isVmNationalSelector() || !humanTeam)
        return;

    uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    uint16_t count = std::min<uint16_t>(numPlayers, kCurrentMatchPlayersSize);
    const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];

    // 1. Find the most valuable injured squad player for Emergency Physio
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t slot = matchPlayers[i];
        if (slot >= kCurrentMatchPlayersSize)
            continue;
        size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
        if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
            continue;
        const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
        std::string nm = cleanName(pf->playerName, sizeof(pf->playerName));
        if (nm.empty() || nm.rfind("*ERR*", 0) == 0)
            continue;
        uint8_t injMatches = (pf->cardsInjuries >> 4) & 0x0Fu;
        if (injMatches > 0) {
            if (!s_session.injured.valid || pf->price > s_session.injured.priceTier) {
                s_session.injured.valid = true;
                s_session.injured.careerSlot = static_cast<int>(slot);
                s_session.injured.playerName = nm;
                s_session.injured.injuryMatches = injMatches;
                s_session.injured.priceTier = pf->price;
                s_session.injured.isPainkiller = (injMatches <= 2);
                s_session.injured.treatmentCost = computeEmergencyPhysioCost(
                    s_session.divisionTier, s_session.plan.medicalLevel, s_session.injured.isPainkiller);
                s_session.injured.aggravationRiskPct = s_session.injured.isPainkiller
                    ? computePainkillerAggravationPct(s_session.plan.medicalLevel) : 0;
            }
        }
    }

    // 2. Collect starting outfield XI matched to Career PlayerFile slots for Shop Window Spotlight
    for (int i = 1; i < kNumPlayersInLineup; ++i) {
        const auto& pl = humanTeam->players[i];
        if (pl.shortName[0] == '\0')
            continue;
        std::string shortUp = toUpperAscii(pl.shortName);
        int matchedSlot = -1;
        uint8_t priceTier = 10;

        for (uint16_t j = 0; j < count; ++j) {
            uint8_t slot = matchPlayers[j];
            if (slot >= kCurrentMatchPlayersSize)
                continue;
            size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                         static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
            if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
                continue;
            const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
            if (pf->shirtNumber == pl.shirtNumber) {
                matchedSlot = static_cast<int>(slot);
                priceTier = pf->price;
                break;
            }
            std::string pfUp = toUpperAscii(cleanName(pf->playerName, sizeof(pf->playerName)));
            if (!pfUp.empty() && pfUp.find(shortUp) != std::string::npos) {
                matchedSlot = static_cast<int>(slot);
                priceTier = pf->price;
            }
        }
        if (matchedSlot >= 0) {
            PreMatchSpotlightCandidate cand;
            cand.careerSlot = matchedSlot;
            cand.shirtNumber = static_cast<int>(pl.shirtNumber);
            cand.shortName = pl.shortName;
            cand.pos = shortPosName(pl.position);
            cand.priceTier = priceTier;
            cand.marketValueStr = formatSwosPriceTier(priceTier);
            s_session.spotlightCandidates.push_back(std::move(cand));
        }
    }
}

bool applyEmergencyPhysioTreatment(std::string& outFeedback)
{
    auto& inj = s_session.injured;
    if (!inj.valid || inj.applied || inj.careerSlot < 0)
        return false;

    int32_t cash = readVmClubCash();
    if (cash < inj.treatmentCost) {
        outFeedback = "INSUFFICIENT FUNDS (" + formatPoundsShort(inj.treatmentCost) + " NEEDED)";
        return false;
    }

    size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                 static_cast<size_t>(inj.careerSlot) * HotseatInternal::kPlayerFileSize;
    if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
        return false;

    auto *pf = reinterpret_cast<PlayerFile *>(&SwosVM::g_memByte[ofs]);
    uint8_t curInj = (pf->cardsInjuries >> 4) & 0x0Fu;
    if (curInj == 0)
        return false;

    applyVmClubCashDelta(-inj.treatmentCost);
    inj.applied = true;

    if (inj.isPainkiller) {
        pf->cardsInjuries = static_cast<uint8_t>(pf->cardsInjuries & 0x0Fu);
        // Also clear live TeamGame injury flag if the player is in the match squad
        TeamGame& myTg = s_session.humanIsTeam1 ? swos.topTeamInGame : swos.bottomTeamInGame;
        for (int i = 0; i < kNumPlayersInTeam; ++i) {
            if (myTg.players[i].shirtNumber == pf->shirtNumber) {
                myTg.players[i].isInjured = 0;
                myTg.players[i].injuriesBitfield = 0;
            }
        }
        s_session.plan.painkillerUsed = true;
        s_session.plan.painkillerCareerSlot = inj.careerSlot;
        s_session.plan.painkillerPlayerName = inj.playerName;
        s_session.plan.painkillerAggravationPct = inj.aggravationRiskPct;
        outFeedback = "PAINKILLER APPLIED: " + inj.playerName + " CLEARED FIT! (" +
                      std::to_string(inj.aggravationRiskPct) + "% RISK)";
        logInfo("Pre-Match Physio: Painkiller cleared %s (-%d GBP, %d%% aggravation risk)",
            inj.playerName.c_str(), inj.treatmentCost, inj.aggravationRiskPct);
    } else {
        uint8_t nextInj = (curInj > 2) ? static_cast<uint8_t>(curInj - 2) : uint8_t{ 1 };
        pf->cardsInjuries = static_cast<uint8_t>((pf->cardsInjuries & 0x0Fu) | ((nextInj & 0x0Fu) << 4));
        outFeedback = "REHAB BOOST: " + inj.playerName + " CUT " + std::to_string(curInj) +
                      "M -> " + std::to_string(nextInj) + "M!";
        logInfo("Pre-Match Physio: Intensive Rehab shortened %s injury %u -> %u matches (-%d GBP)",
            inj.playerName.c_str(), static_cast<unsigned>(curInj), static_cast<unsigned>(nextInj), inj.treatmentCost);
    }

    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        // Keep hotseat cash synced (careerTeam buffer is written back on turn end)
        mgr.activePlayer().newBalance = readVmClubCash();
    }
    return true;
}

std::string upgradePrimarySkillNibble(PlayerFile *pf)
{
    if (!pf)
        return {};
    auto pos = static_cast<PlayerPosition>((pf->positionAndFace >> 5) & 0x07u);
    auto bumpHigh = [](uint8_t& b) -> bool {
        uint8_t v = (b >> 4) & 0x07u;
        if (v >= 7) return false;
        b = static_cast<uint8_t>((b & 0x8Fu) | ((v + 1) << 4));
        return true;
    };
    auto bumpLow = [](uint8_t& b) -> bool {
        uint8_t v = b & 0x07u;
        if (v >= 7) return false;
        b = static_cast<uint8_t>((b & 0xF8u) | (v + 1));
        return true;
    };

    if (pos == PlayerPosition::kAttacker) {
        if (bumpLow(pf->speedFinishing)) return "FINISHING";
        if (bumpHigh(pf->shootingHeading)) return "SHOOTING";
        if (bumpHigh(pf->speedFinishing)) return "SPEED";
    } else if (pos == PlayerPosition::kMidfielder || pos == PlayerPosition::kLeftWing || pos == PlayerPosition::kRightWing) {
        if (bumpLow(pf->passing)) return "PASSING";
        if (bumpLow(pf->tacklingBallControl)) return "BALL CONTROL";
        if (bumpHigh(pf->speedFinishing)) return "SPEED";
    } else {
        if (bumpHigh(pf->tacklingBallControl)) return "TACKLING";
        if (bumpLow(pf->shootingHeading)) return "HEADING";
        if (bumpLow(pf->passing)) return "PASSING";
    }
    // Fallback any skill < 7
    if (bumpLow(pf->passing)) return "PASSING";
    if (bumpHigh(pf->shootingHeading)) return "SHOOTING";
    if (bumpLow(pf->speedFinishing)) return "FINISHING";
    if (bumpHigh(pf->speedFinishing)) return "SPEED";
    return {};
}

} // namespace

// --- Pure Deterministic Helpers ---

const char *getTacticalFocusName(TacticalFocusType focus)
{
    switch (focus) {
    case TacticalFocusType::kAllOutAttack:    return "ALL-OUT ATTACK";
    case TacticalFocusType::kIronDefence:     return "IRON DEFENCE";
    case TacticalFocusType::kMidfieldControl: return "MIDFIELD CONTROL";
    case TacticalFocusType::kSetPieceLongShot:return "SET-PIECE & LONG SHOT";
    case TacticalFocusType::kBalanced:
    default:                                  return "BALANCED (DEFAULT)";
    }
}

const char *getTacticalFocusBoostDesc(TacticalFocusType focus)
{
    switch (focus) {
    case TacticalFocusType::kAllOutAttack:    return "ATT/W +2 FIN/SHT, +1 SPD";
    case TacticalFocusType::kIronDefence:     return "DEF +2 TKL/HED, GK +1";
    case TacticalFocusType::kMidfieldControl: return "MID/W +2 PAS, +1 CTL";
    case TacticalFocusType::kSetPieceLongShot:return "XI +2 HED, +2 SHT";
    case TacticalFocusType::kBalanced:
    default:                                  return "Standard match ratings";
    }
}

const char *getTacticalFocusTradeoffDesc(TacticalFocusType focus, int trainingLevel)
{
    if (focus == TacticalFocusType::kBalanced)
        return "No trade-offs";
    if (trainingLevel >= 4)
        return "NONE (Training L4+ perk!)";
    switch (focus) {
    case TacticalFocusType::kAllOutAttack:    return "DEF -1 TKL";
    case TacticalFocusType::kIronDefence:     return "ATT -1 FIN/SHT";
    case TacticalFocusType::kMidfieldControl: return "ATT -1 SPD";
    case TacticalFocusType::kSetPieceLongShot:return "MID -1 CTL";
    default:                                  return "None";
    }
}

int32_t computeEmergencyPhysioCost(int divisionTier, int medicalLevel, bool isPainkiller)
{
    int32_t base = (divisionTier <= 0) ? 32000 :
                   (divisionTier == 1) ? 20000 :
                   (divisionTier == 2) ? 12000 : 6000;
    if (!isPainkiller)
        base = (base * 135) / 100; // Intensive multi-match rehab costs slightly more than a painkiller shot

    // Medical Center L2..L5 discounts physio fees by 10%..40%
    int discountPct = std::clamp(medicalLevel - 1, 0, 4) * 10;
    int32_t cost = (base * (100 - discountPct)) / 100;
    return std::max<int32_t>(2000, ((cost + 500) / 1000) * 1000);
}

int computePainkillerAggravationPct(int medicalLevel)
{
    // L1: 28%, L2: 22%, L3: 16%, L4: 11%, L5: 7%
    static constexpr int kRisk[5] = { 28, 22, 16, 11, 7 };
    int idx = std::clamp(medicalLevel - 1, 0, 4);
    return kRisk[idx];
}

PreMatchScoutIntel analyzeOpponentScouting(const TeamGame& myTeam, const TeamGame& oppTeam)
{
    PreMatchScoutIntel info;
    info.myClub = myTeam.teamName[0] ? myTeam.teamName : "OUR CLUB";
    info.oppClub = oppTeam.teamName[0] ? oppTeam.teamName : "OPPONENT";

    // 1. Find Opponent Danger Man (highest attacking/overall rating among outfield starters 1..10)
    int bestIdx = -1;
    int bestScore = -1;
    for (int i = 1; i < kNumPlayersInLineup; ++i) {
        const auto& pl = oppTeam.players[i];
        if (pl.shortName[0] == '\0')
            continue;
        int score = pl.finishing * 2 + pl.shooting * 2 + pl.speed * 2 + pl.ballControl + pl.passing + pl.heading;
        if (pl.position == PlayerPosition::kAttacker)
            score += 6;
        else if (isMidOrWingPos(pl.position))
            score += 3;
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }
    if (bestIdx >= 0) {
        const auto& dm = oppTeam.players[bestIdx];
        info.dangerManIndex = bestIdx;
        info.dangerManName = dm.shortName;
        info.dangerManPos = shortPosName(dm.position);
        info.dangerManStats = "FIN " + std::to_string(dm.finishing) +
                              "  SHT " + std::to_string(dm.shooting) +
                              "  SPD " + std::to_string(dm.speed) +
                              "  CTL " + std::to_string(dm.ballControl);
    }

    // 2. Find Opponent Weakest Link (compare GK vs slowest/weakest starting defender)
    const auto& oppGk = oppTeam.players[0];
    int gkRating = oppGk.shortName[0] ? static_cast<int>(oppGk.goalieSkill) : 7;

    int worstDefIdx = -1;
    int worstDefScore = 999;
    for (int i = 1; i < kNumPlayersInLineup; ++i) {
        const auto& pl = oppTeam.players[i];
        if (pl.shortName[0] == '\0' || !isDefenderPos(pl.position))
            continue;
        int dScore = static_cast<int>(pl.speed) * 2 + static_cast<int>(pl.tackling) * 2 + static_cast<int>(pl.heading);
        if (dScore < worstDefScore) {
            worstDefScore = dScore;
            worstDefIdx = i;
        }
    }

    if (oppGk.shortName[0] != '\0' && (gkRating <= 3 || worstDefIdx < 0 || gkRating * 5 <= worstDefScore)) {
        info.weakLinkLabel = std::string("GK ") + oppGk.shortName + " (GK " + std::to_string(gkRating) + "/7)";
        info.weakLinkAdvice = (gkRating <= 3)
            ? "Shaky keeper! Shoot early and test from outside the box."
            : "Keeper is solid, work high-percentage chances in the box.";
    } else if (worstDefIdx >= 0) {
        const auto& wd = oppTeam.players[worstDefIdx];
        info.weakLinkLabel = std::string(shortPosName(wd.position)) + " " + wd.shortName +
                             " (SPD " + std::to_string(wd.speed) + ", TKL " + std::to_string(wd.tackling) + ")";
        if (wd.speed <= 2)
            info.weakLinkAdvice = "Slow defender! Hit balls in behind and run at them with pace.";
        else if (wd.tackling <= 2)
            info.weakLinkAdvice = "Lightweight tackler — dribble directly at their back line.";
        else
            info.weakLinkAdvice = "Target this side with quick one-twos and overlapping wingers.";
    } else {
        info.weakLinkLabel = "Compact opponent XI";
        info.weakLinkAdvice = "Stay patient, stretch play wide, and punish mistakes.";
    }

    return info;
}

void applyTacticalFocusToTeams(
    TeamGame& myTeam,
    TeamGame& oppTeam,
    TacticalFocusType focus,
    int trainingLevel,
    bool manMarkDangerMan,
    int dangerManIndex)
{
    bool skipTradeoff = (trainingLevel >= 4);

    // 1. Apply Tactical Matchday Focus strictly clamped to [0, 7]
    for (int i = 0; i < kNumPlayersInTeam; ++i) {
        auto& pl = myTeam.players[i];
        if (pl.shortName[0] == '\0')
            continue;

        switch (focus) {
        case TacticalFocusType::kAllOutAttack:
            if (isAttOrWingPos(pl.position)) {
                adjustSkill(pl.finishing, +2);
                adjustSkill(pl.shooting, +2);
                adjustSkill(pl.speed, +1);
            } else if (!skipTradeoff && isDefenderPos(pl.position)) {
                adjustSkill(pl.tackling, -1);
            }
            break;

        case TacticalFocusType::kIronDefence:
            if (pl.position == PlayerPosition::kGoalkeeper || i == 0) {
                adjustSkill(pl.goalieSkill, +1);
            } else if (isDefenderPos(pl.position)) {
                adjustSkill(pl.tackling, +2);
                adjustSkill(pl.heading, +2);
            } else if (!skipTradeoff && pl.position == PlayerPosition::kAttacker) {
                adjustSkill(pl.finishing, -1);
                adjustSkill(pl.shooting, -1);
            }
            break;

        case TacticalFocusType::kMidfieldControl:
            if (isMidOrWingPos(pl.position)) {
                adjustSkill(pl.passing, +2);
                adjustSkill(pl.ballControl, +1);
            } else if (!skipTradeoff && pl.position == PlayerPosition::kAttacker) {
                adjustSkill(pl.speed, -1);
            }
            break;

        case TacticalFocusType::kSetPieceLongShot:
            if (i > 0 && pl.position != PlayerPosition::kGoalkeeper) {
                adjustSkill(pl.heading, +2);
                adjustSkill(pl.shooting, +2);
                if (!skipTradeoff && pl.position == PlayerPosition::kMidfielder)
                    adjustSkill(pl.ballControl, -1);
            }
            break;

        case TacticalFocusType::kBalanced:
        default:
            break;
        }
    }

    // 2. Optional Man-Mark Opponent Danger Man (reduces their key attacking skills; costs 1 midfielder -1 passing)
    if (manMarkDangerMan && dangerManIndex >= 1 && dangerManIndex < kNumPlayersInLineup) {
        auto& dm = oppTeam.players[dangerManIndex];
        if (dm.shortName[0] != '\0') {
            adjustSkill(dm.finishing, -1);
            adjustSkill(dm.shooting, -1);
            adjustSkill(dm.ballControl, -1);
        }
        if (!skipTradeoff) {
            int shadowIdx = -1;
            int minPas = 99;
            for (int i = 1; i < kNumPlayersInLineup; ++i) {
                if (isMidOrWingPos(myTeam.players[i].position) && myTeam.players[i].passing < minPas) {
                    minPas = myTeam.players[i].passing;
                    shadowIdx = i;
                }
            }
            if (shadowIdx >= 0)
                adjustSkill(myTeam.players[shadowIdx].passing, -1);
        }
    }
}

SpotlightMatchEvaluation evaluateSpotlightPerformance(
    const std::string& playerName,
    int goals,
    int assists,
    int redCards,
    double rating)
{
    SpotlightMatchEvaluation ev;
    ev.active = !playerName.empty();
    ev.playerName = playerName;
    ev.goals = goals;
    ev.assists = assists;
    ev.rating = rating;
    if (!ev.active)
        return ev;

    char rBuf[16];
    std::snprintf(rBuf, sizeof(rBuf), "%.1f", rating);

    if (redCards > 0 || rating < 5.8) {
        ev.flopped = true;
        ev.summaryLine = "SHOP WINDOW FLOP: " + playerName + " (" + std::string(rBuf) + "/10" +
                         (redCards > 0 ? ", RED CARD" : "") + ") failed to impress watching scouts (-2 CP).";
    } else if (goals >= 1 || assists >= 1 || rating >= 7.5) {
        ev.succeeded = true;
        ev.masterclassSkillBoost = (rating >= 8.2 || goals >= 2);
        ev.summaryLine = "SHOP WINDOW HIT: " + playerName + " (" + std::string(rBuf) + "/10, " +
                         std::to_string(goals) + "G " + std::to_string(assists) + "A) impressed scouts — MARKET VALUE +1 TIER!";
    } else {
        ev.summaryLine = "SHOP WINDOW: " + playerName + " (" + std::string(rBuf) + "/10) had a quiet outing — no value change.";
    }
    return ev;
}

// --- Career & Match Lifecycle Hooks ---

void resetPreMatchAssistantState()
{
    s_session = AssistantMatchSession{};
}

void beginPreMatchAssistantBriefing(const TeamGame *topTeam, const TeamGame *bottomTeam)
{
    resetPreMatchAssistantState();
    if (!topTeam || !bottomTeam || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.preMatchScouting)
        return;

    bool humanIsTeam1 = true;
    if (!resolveHumanSide(topTeam, bottomTeam, humanIsTeam1))
        return;

    s_session.eligible = true;
    s_session.humanIsTeam1 = humanIsTeam1;
    s_session.isCareer = (swos.g_gameType == kGameTypeCareer);
    s_session.divisionTier = s_session.isCareer ? readDivisionTier() : 0;

    if (s_session.isCareer && !HotseatInternal::isVmNationalSelector()) {
        const auto& stad = getCurrentCareerStadium();
        if (stad.initialized) {
            s_session.plan.trainingLevel = stad.trainingLevel;
            s_session.plan.medicalLevel = stad.medicalLevel;
        }
    }

    const TeamGame *myTeam = humanIsTeam1 ? topTeam : bottomTeam;
    const TeamGame *oppTeam = humanIsTeam1 ? bottomTeam : topTeam;
    s_session.intel = analyzeOpponentScouting(*myTeam, *oppTeam);
    scanCareerInjuriesAndSpotlight(myTeam);

    // Optional async 2-sentence LLM tactical tip (never blocks instant skip!)
    if (cfg.enabled) {
        std::string prompt = "You are the loyal 1997 Assistant Manager of " + s_session.intel.myClub +
                             " briefing your boss in the tunnel before facing " + s_session.intel.oppClub + ".\n" +
                             "Real SWOS scout intel:\n" +
                             "- Opponent Danger Man: " + s_session.intel.dangerManName + " (" + s_session.intel.dangerManPos + ", " + s_session.intel.dangerManStats + ")\n" +
                             "- Opponent Weak Link: " + s_session.intel.weakLinkLabel + " (" + s_session.intel.weakLinkAdvice + ")\n" +
                             "Give a sharp, punchy 2-sentence tactical briefing (max 150 chars total) referencing ONLY " +
                             s_session.intel.oppClub + "'s players above. Plain text only.";
        s_session.llmTipRequestId = enqueueLlmRequest(
            "You are a sharp 1997 football assistant manager giving a concise 2-sentence pre-match tactical tip. Plain text only.",
            prompt);
    }
}

void showPreMatchAssistantMenuIfReady()
{
    if (!s_session.eligible || swos.g_trainingGame || swos.gameCanceled)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.features.preMatchScouting)
        return;

#ifndef SWOS_TEST
    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = r ? getCompactFontAtlas(r) : nullptr;
    if (!r || !atlas)
        return;

    setStandardMenuBackgroundImage();
    menuFadeIn();
    flushKeyBuffer();
    waitForKeyboardAndMouseIdle();

    std::string statusBanner;
    Uint32 statusTick = 0;
    bool prevKeys[12] = {};
    bool prevMouseDown = false;
    Uint32 openTick = SDL_GetTicks();

    const SDL_Color whiteCol{ 244, 246, 248, 255 };
    const SDL_Color mutedCol{ 166, 178, 188, 255 };
    const SDL_Color yellowCol{ 255, 224, 78, 255 };
    const SDL_Color cyanCol{ 118, 232, 246, 255 };
    const SDL_Color greenCol{ 96, 238, 134, 255 };
    const SDL_Color redCol{ 248, 114, 114, 255 };
    const SDL_Color orangeCol{ 250, 176, 82, 255 };

    auto fitPx = [](const std::string& s, float basePx, float maxW) -> float {
        int u = measureCompactTextUnits(s);
        if (u <= 0 || maxW <= 1.0f) return basePx;
        float needed = static_cast<float>(u) * basePx;
        return (needed <= maxW) ? basePx : std::max(0.75f, basePx * (maxW / needed));
    };

    auto setStatus = [&](const std::string& msg) {
        statusBanner = msg;
        statusTick = SDL_GetTicks();
    };

    while (true) {
        processControlEvents();

        // Non-blocking poll for optional LLM assistant tip
        if (s_session.llmTipRequestId != 0) {
            std::string reply;
            if (tryTakeLlmResponse(s_session.llmTipRequestId, reply)) {
                s_session.llmTipRequestId = 0;
                s_session.llmTipText = sanitizeCompactMixedCase(reply);
                if (s_session.llmTipText.size() > 195)
                    s_session.llmTipText.resize(195);
            }
        }

        drawMenuBackground();

        // Dark tactical chalkboard panels in VGA coordinates (320x200)
        drawDarkRectangle({ 8.0f, 3.0f, 304.0f, 13.0f }, true);    // Header
        drawDarkRectangle({ 8.0f, 18.0f, 304.0f, 40.0f }, true);   // Opponent Scout Intel + Assistant Tip
        drawDarkRectangle({ 8.0f, 60.0f, 168.0f, 114.0f }, true);  // Left: Tactical Matchday Focus (1-5)
        drawDarkRectangle({ 179.0f, 60.0f, 133.0f, 114.0f }, true);// Right: Man-Mark, Shop Window, Physio
        drawDarkRectangle({ 8.0f, 177.0f, 304.0f, 19.0f }, true);  // Footer Confirm / Skip

        float gameScale = getGameScale();
        float offX = getGameScreenOffsetX();
        float offY = getGameScreenOffsetY();
        float px = computeCompactPixelScaleForVga(gameScale, 0.40f);

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

        auto toRect = [&](float vx, float vy, float vw, float vh) -> SDL_FRect {
            return { offX + vx * gameScale, offY + vy * gameScale, vw * gameScale, vh * gameScale };
        };

        // 1. Header
        std::string title = "ASSISTANT MANAGER  -  MATCHDAY PREP (" + toUpperAscii(s_session.intel.myClub) +
                            " vs " + toUpperAscii(s_session.intel.oppClub) + ")";
        drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 6.2f * gameScale,
            fitPx(title, px * 0.96f, 294.0f * gameScale), title, yellowCol, 255, true);

        // 2. Opponent Scout Intel + Live Assistant Tip (y=18..58)
        std::string dmLine = "DANGER MAN: " + toUpperAscii(s_session.intel.dangerManName) +
                             " [" + s_session.intel.dangerManPos + "]  " + s_session.intel.dangerManStats;
        drawCompactText(r, atlas, offX + 13.0f * gameScale, offY + 21.0f * gameScale,
            fitPx(dmLine, px * 0.88f, 294.0f * gameScale), dmLine, redCol, 255, true);

        std::string wlLine = "WEAK LINK: " + s_session.intel.weakLinkLabel + "  -  " + s_session.intel.weakLinkAdvice;
        drawCompactText(r, atlas, offX + 13.0f * gameScale, offY + 30.0f * gameScale,
            fitPx(wlLine, px * 0.84f, 294.0f * gameScale), wlLine, greenCol, 255, false);

        std::string tipText = !s_session.llmTipText.empty()
            ? ("ASS'T: " + s_session.llmTipText)
            : (s_session.llmTipRequestId != 0
                ? "ASS'T: Reviewing opponent match tape... (you can pick drills or press ENTER anytime)"
                : ("ASS'T: Pick a matchday focus below or hit ENTER/ESC for instant kickoff."));
        int tipUnits = std::max(60, static_cast<int>((292.0f * gameScale) / (px * 0.80f)));
        auto tipLines = wrapCompactTextUnits(tipText, tipUnits);
        float tipY = offY + 39.5f * gameScale;
        for (size_t i = 0; i < tipLines.size() && i < 2; ++i) {
            drawCompactText(r, atlas, offX + 13.0f * gameScale, tipY, px * 0.80f, tipLines[i], cyanCol, 240, false);
            tipY += 8.6f * px * 0.80f;
        }

        // 3. Left Column: Tactical Matchday Focus (5 clickable cards, keys 1..5)
        drawCompactText(r, atlas, offX + 13.0f * gameScale, offY + 63.0f * gameScale,
            px * 0.86f, "TACTICAL FOCUS [KEYS 1-5]", yellowCol, 255, true);
        if (s_session.plan.trainingLevel >= 4) {
            drawCompactTextRightAligned(r, atlas, offX + 172.0f * gameScale, offY + 63.0f * gameScale,
                px * 0.72f, "TRN L" + std::to_string(s_session.plan.trainingLevel) + ": 0 PENALTY!", greenCol, 255, true);
        }

        SDL_FRect focusRects[5];
        for (int i = 0; i < 5; ++i) {
            auto ft = static_cast<TacticalFocusType>(i);
            bool sel = (s_session.plan.focus == ft);
            focusRects[i] = toRect(12.0f, 73.0f + static_cast<float>(i) * 19.5f, 160.0f, 17.5f);

            SDL_SetRenderDrawColor(r, sel ? 28 : 16, sel ? 62 : 24, sel ? 42 : 28, sel ? 230 : 175);
            SDL_RenderFillRectF(r, &focusRects[i]);
            SDL_SetRenderDrawColor(r, sel ? 96 : 108, sel ? 238 : 122, sel ? 134 : 132, sel ? 245 : 130);
            SDL_RenderDrawRectF(r, &focusRects[i]);

            std::string fTitle = "[" + std::to_string(i + 1) + "] " + std::string(getTacticalFocusName(ft));
            if (sel) fTitle += "  [ACTIVE]";
            drawCompactText(r, atlas, focusRects[i].x + 4.0f, focusRects[i].y + 2.5f,
                fitPx(fTitle, px * 0.82f, focusRects[i].w - 8.0f), fTitle, sel ? greenCol : whiteCol, 255, true);

            std::string fDesc = std::string(getTacticalFocusBoostDesc(ft)) + " | " +
                                std::string(getTacticalFocusTradeoffDesc(ft, s_session.plan.trainingLevel));
            drawCompactText(r, atlas, focusRects[i].x + 4.0f, focusRects[i].y + 2.5f + 8.6f * px * 0.80f,
                fitPx(fDesc, px * 0.72f, focusRects[i].w - 8.0f), fDesc, sel ? yellowCol : mutedCol, 240, false);
        }

        // 4. Right Column: [M] Man-Mark, [W] Shop Window Spotlight, [P] Emergency Physio
        drawCompactText(r, atlas, offX + 183.0f * gameScale, offY + 63.0f * gameScale,
            px * 0.86f, "SPECIAL MATCHDAY CALLS", yellowCol, 255, true);

        // Box 1: Man-Mark Danger Man (y=73..102)
        SDL_FRect markRect = toRect(183.0f, 73.0f, 125.0f, 29.0f);
        bool markOn = s_session.plan.manMarkDangerMan;
        SDL_SetRenderDrawColor(r, markOn ? 58 : 18, markOn ? 30 : 24, markOn ? 28 : 30, markOn ? 230 : 180);
        SDL_RenderFillRectF(r, &markRect);
        SDL_SetRenderDrawColor(r, markOn ? 250 : 112, markOn ? 176 : 124, markOn ? 82 : 134, 220);
        SDL_RenderDrawRectF(r, &markRect);
        std::string markHdr = "[M] MAN-MARK: " + std::string(markOn ? "ON" : "OFF");
        drawCompactText(r, atlas, markRect.x + 4.0f, markRect.y + 3.0f,
            fitPx(markHdr, px * 0.82f, markRect.w - 8.0f), markHdr, markOn ? orangeCol : whiteCol, 255, true);
        std::string markSub = "Target " + toUpperAscii(s_session.intel.dangerManName) + " (-1 FIN/SHT/CTL)";
        drawCompactText(r, atlas, markRect.x + 4.0f, markRect.y + 3.0f + 9.0f * px * 0.80f,
            fitPx(markSub, px * 0.72f, markRect.w - 8.0f), markSub, cyanCol, 240, false);
        std::string markCost = (s_session.plan.trainingLevel >= 4) ? "Cost: 0 (Training L4+)" : "Cost: 1 MID -1 PAS";
        drawCompactText(r, atlas, markRect.x + 4.0f, markRect.y + 3.0f + 17.5f * px * 0.78f,
            fitPx(markCost, px * 0.70f, markRect.w - 8.0f), markCost, mutedCol, 230, false);

        // Box 2: Shop Window / Scout Spotlight (y=106..136)
        SDL_FRect spotRect = toRect(183.0f, 106.0f, 125.0f, 30.0f);
        bool spotOn = (s_session.plan.spotlightCandidateIndex >= 0 &&
                       s_session.plan.spotlightCandidateIndex < static_cast<int>(s_session.spotlightCandidates.size()));
        SDL_SetRenderDrawColor(r, spotOn ? 26 : 18, spotOn ? 52 : 24, spotOn ? 62 : 30, spotOn ? 230 : 180);
        SDL_RenderFillRectF(r, &spotRect);
        SDL_SetRenderDrawColor(r, spotOn ? 118 : 112, spotOn ? 232 : 124, spotOn ? 246 : 134, 220);
        SDL_RenderDrawRectF(r, &spotRect);
        if (s_session.isCareer && !s_session.spotlightCandidates.empty()) {
            std::string spotHdr = "[W] SHOP WINDOW (CYCLE)";
            drawCompactText(r, atlas, spotRect.x + 4.0f, spotRect.y + 3.0f,
                fitPx(spotHdr, px * 0.80f, spotRect.w - 8.0f), spotHdr, spotOn ? cyanCol : whiteCol, 255, true);
            std::string spotWho = spotOn
                ? (toUpperAscii(s_session.spotlightCandidates[s_session.plan.spotlightCandidateIndex].shortName) +
                   " [" + s_session.spotlightCandidates[s_session.plan.spotlightCandidateIndex].pos + "] " +
                   s_session.spotlightCandidates[s_session.plan.spotlightCandidateIndex].marketValueStr)
                : "NONE (click or press W to pick starter)";
            drawCompactText(r, atlas, spotRect.x + 4.0f, spotRect.y + 3.0f + 9.0f * px * 0.80f,
                fitPx(spotWho, px * 0.74f, spotRect.w - 8.0f), spotWho, spotOn ? yellowCol : mutedCol, 250, true);
            std::string spotRule = "Goal/Ast/7.5+: +1 VALUE! (8.2+: +1 SKILL)";
            drawCompactText(r, atlas, spotRect.x + 4.0f, spotRect.y + 3.0f + 18.0f * px * 0.78f,
                fitPx(spotRule, px * 0.68f, spotRect.w - 8.0f), spotRule, greenCol, 235, false);
        } else {
            drawCompactText(r, atlas, spotRect.x + 4.0f, spotRect.y + 3.0f, px * 0.80f, "SHOP WINDOW", mutedCol, 210, true);
            drawCompactText(r, atlas, spotRect.x + 4.0f, spotRect.y + 3.0f + 10.0f * px * 0.78f,
                px * 0.72f, "Available in Career Mode", mutedCol, 190, false);
        }

        // Box 3: Emergency Physio / Injury Rush (y=140..170)
        SDL_FRect physRect = toRect(183.0f, 140.0f, 125.0f, 30.0f);
        bool hasInj = s_session.injured.valid;
        bool injApplied = s_session.injured.applied;
        SDL_SetRenderDrawColor(r, injApplied ? 22 : (hasInj ? 54 : 16), injApplied ? 56 : (hasInj ? 26 : 22), injApplied ? 34 : 26, 215);
        SDL_RenderFillRectF(r, &physRect);
        SDL_SetRenderDrawColor(r, injApplied ? 96 : (hasInj ? 248 : 90), injApplied ? 238 : (hasInj ? 114 : 100), injApplied ? 134 : (hasInj ? 114 : 108), 220);
        SDL_RenderDrawRectF(r, &physRect);
        if (hasInj) {
            std::string pHdr = injApplied
                ? "PHYSIO TREATMENT APPLIED!"
                : (s_session.injured.isPainkiller ? "[P] PAINKILLER INJECTION" : "[P] INTENSIVE REHAB (-2M)");
            drawCompactText(r, atlas, physRect.x + 4.0f, physRect.y + 3.0f,
                fitPx(pHdr, px * 0.78f, physRect.w - 8.0f), pHdr, injApplied ? greenCol : redCol, 255, true);
            std::string pWho = toUpperAscii(s_session.injured.playerName) + " (OUT " +
                               std::to_string(s_session.injured.injuryMatches) + "M) - " +
                               formatPoundsShort(s_session.injured.treatmentCost);
            drawCompactText(r, atlas, physRect.x + 4.0f, physRect.y + 3.0f + 9.0f * px * 0.80f,
                fitPx(pWho, px * 0.72f, physRect.w - 8.0f), pWho, whiteCol, 250, true);
            std::string pNote = injApplied
                ? "Medical staff cleared squad update"
                : (s_session.injured.isPainkiller
                    ? ("Clears FIT today! (" + std::to_string(s_session.injured.aggravationRiskPct) + "% relapse risk)")
                    : "Cuts 2 matches off recovery now (0% risk)");
            drawCompactText(r, atlas, physRect.x + 4.0f, physRect.y + 3.0f + 18.0f * px * 0.78f,
                fitPx(pNote, px * 0.68f, physRect.w - 8.0f), pNote, yellowCol, 235, false);
        } else {
            drawCompactText(r, atlas, physRect.x + 4.0f, physRect.y + 3.0f, px * 0.80f, "MEDICAL BAY: ALL FIT", greenCol, 230, true);
            drawCompactText(r, atlas, physRect.x + 4.0f, physRect.y + 3.0f + 10.0f * px * 0.78f,
                fitPx("No injured players need emergency physio", px * 0.70f, physRect.w - 8.0f),
                "No injured players need emergency physio", mutedCol, 205, false);
        }

        // 5. Footer Confirm / Instant Skip Bar (y=177..196)
        SDL_FRect kickBtn = toRect(198.0f, 180.0f, 110.0f, 13.0f);
        SDL_SetRenderDrawColor(r, 28, 96, 52, 235);
        SDL_RenderFillRectF(r, &kickBtn);
        SDL_SetRenderDrawColor(r, 128, 246, 158, 240);
        SDL_RenderDrawRectF(r, &kickBtn);
        drawCompactTextCentered(r, atlas, kickBtn.x + kickBtn.w * 0.5f, kickBtn.y + (kickBtn.h - 8.0f * px * 0.80f) * 0.5f,
            fitPx("[ENTER / ESC] KICKOFF", px * 0.80f, kickBtn.w - 6.0f), "[ENTER / ESC] KICKOFF", whiteCol, 255, true);

        bool showBanner = !statusBanner.empty() && (SDL_GetTicks() - statusTick < 3500);
        std::string footLeft = showBanner
            ? statusBanner
            : ("ACTIVE: " + std::string(getTacticalFocusName(s_session.plan.focus)) +
               (s_session.plan.manMarkDangerMan ? " + MAN-MARK" : "") +
               (spotOn ? (" + SPOTLIGHT " + toUpperAscii(s_session.spotlightCandidates[s_session.plan.spotlightCandidateIndex].shortName)) : ""));
        drawCompactText(r, atlas, offX + 13.0f * gameScale, offY + 183.5f * gameScale,
            fitPx(footLeft, px * 0.80f, 180.0f * gameScale), footLeft, showBanner ? greenCol : yellowCol, 255, true);

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_RenderSetScale(r, origSX, origSY);
        updateScreen(false);

        // Input handling (tiny 140ms debounce so held keys from Stadium screen don't double-skip)
        bool ready = (SDL_GetTicks() - openTick) >= 140;
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        bool curKeys[12] = {
            keys && (keys[SDL_SCANCODE_1] || keys[SDL_SCANCODE_KP_1]),
            keys && (keys[SDL_SCANCODE_2] || keys[SDL_SCANCODE_KP_2]),
            keys && (keys[SDL_SCANCODE_3] || keys[SDL_SCANCODE_KP_3]),
            keys && (keys[SDL_SCANCODE_4] || keys[SDL_SCANCODE_KP_4]),
            keys && (keys[SDL_SCANCODE_5] || keys[SDL_SCANCODE_KP_5]),
            keys && (keys[SDL_SCANCODE_M] != 0),
            keys && (keys[SDL_SCANCODE_W] != 0),
            keys && (keys[SDL_SCANCODE_P] != 0),
            keys && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]),
            keys && (keys[SDL_SCANCODE_ESCAPE] != 0),
            keys && (keys[SDL_SCANCODE_SPACE] != 0),
            false,
        };

        int mx = 0, my = 0;
        Uint32 mb = SDL_GetMouseState(&mx, &my);
        bool mouseDown = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        bool clicked = ready && mouseDown && !prevMouseDown;

        auto inRect = [&](const SDL_FRect& rc) {
            return mx >= rc.x && mx <= rc.x + rc.w && my >= rc.y && my <= rc.y + rc.h;
        };

        bool done = false;
        if (ready) {
            for (int i = 0; i < 5; ++i) {
                if ((curKeys[i] && !prevKeys[i]) || (clicked && inRect(focusRects[i]))) {
                    s_session.plan.focus = static_cast<TacticalFocusType>(i);
                    setStatus("TACTICAL PLAN: " + std::string(getTacticalFocusName(s_session.plan.focus)));
                }
            }
            if ((curKeys[5] && !prevKeys[5]) || (clicked && inRect(markRect))) {
                s_session.plan.manMarkDangerMan = !s_session.plan.manMarkDangerMan;
                setStatus(s_session.plan.manMarkDangerMan
                    ? ("MAN-MARKING " + toUpperAscii(s_session.intel.dangerManName))
                    : "MAN-MARKING OFF");
            }
            if ((curKeys[6] && !prevKeys[6]) || (clicked && inRect(spotRect))) {
                if (s_session.isCareer && !s_session.spotlightCandidates.empty()) {
                    ++s_session.plan.spotlightCandidateIndex;
                    if (s_session.plan.spotlightCandidateIndex >= static_cast<int>(s_session.spotlightCandidates.size()))
                        s_session.plan.spotlightCandidateIndex = -1;
                    if (s_session.plan.spotlightCandidateIndex >= 0) {
                        const auto& c = s_session.spotlightCandidates[s_session.plan.spotlightCandidateIndex];
                        setStatus("SPOTLIGHT: " + toUpperAscii(c.shortName) + " (" + c.marketValueStr + ")");
                    } else {
                        setStatus("SHOP WINDOW SPOTLIGHT: OFF");
                    }
                }
            }
            if ((curKeys[7] && !prevKeys[7]) || (clicked && inRect(physRect))) {
                if (s_session.injured.valid && !s_session.injured.applied) {
                    std::string fb;
                    applyEmergencyPhysioTreatment(fb);
                    if (!fb.empty())
                        setStatus(fb);
                }
            }
            if ((curKeys[8] && !prevKeys[8]) || (curKeys[9] && !prevKeys[9]) ||
                (curKeys[10] && !prevKeys[10]) || (clicked && inRect(kickBtn)) || isAnyPlayerFiring()) {
                done = true;
            }
        }

        for (int i = 0; i < 12; ++i)
            prevKeys[i] = curKeys[i];
        prevMouseDown = mouseDown;

        if (done)
            break;
        SDL_Delay(16);
    }

    // Apply chosen Tactical Focus & Man-Marking directly to the live match TeamGame structs!
    TeamGame& myTeam = s_session.humanIsTeam1 ? swos.topTeamInGame : swos.bottomTeamInGame;
    TeamGame& oppTeam = s_session.humanIsTeam1 ? swos.bottomTeamInGame : swos.topTeamInGame;
    applyTacticalFocusToTeams(
        myTeam, oppTeam,
        s_session.plan.focus,
        s_session.plan.trainingLevel,
        s_session.plan.manMarkDangerMan,
        s_session.intel.dangerManIndex);

    logInfo("Pre-Match Assistant locked in: Focus=%s, ManMark=%s, Spotlight=%d, Painkiller=%s",
        getTacticalFocusName(s_session.plan.focus),
        s_session.plan.manMarkDangerMan ? "YES" : "NO",
        s_session.plan.spotlightCandidateIndex,
        s_session.plan.painkillerUsed ? "YES" : "NO");

    menuFadeOut();
    flushKeyBuffer();
    waitForKeyboardAndMouseIdle();
#endif
}

void onMatchFinishedPreMatchAssistant()
{
    s_session.postMatchPayoffLines.clear();
    if (!s_session.eligible || !s_session.isCareer || swos.g_trainingGame || swos.gameCanceled ||
        HotseatInternal::isVmNationalSelector())
        return;

    // 1. Evaluate Shop Window / Scout Spotlight player performance!
    int idx = s_session.plan.spotlightCandidateIndex;
    if (idx >= 0 && idx < static_cast<int>(s_session.spotlightCandidates.size())) {
        const auto& cand = s_session.spotlightCandidates[idx];
        auto perf = getPlayerMatchPerformance(s_session.humanIsTeam1 ? 1 : 2, cand.shirtNumber, cand.shortName);
        if (perf.found) {
            auto ev = evaluateSpotlightPerformance(cand.shortName, perf.goals, perf.assists, perf.redCards, perf.rating);
            if (ev.succeeded && cand.careerSlot >= 0) {
                size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                             static_cast<size_t>(cand.careerSlot) * HotseatInternal::kPlayerFileSize;
                if (ofs + HotseatInternal::kPlayerFileSize <= CareerOffsets::kCareerTeam + kCareerTeamBufferSize) {
                    auto *pf = reinterpret_cast<PlayerFile *>(&SwosVM::g_memByte[ofs]);
                    std::string oldVal = formatSwosPriceTier(pf->price);
                    if (pf->price < 48)
                        ++pf->price;
                    std::string newVal = formatSwosPriceTier(pf->price);
                    std::string line = "SHOP WINDOW SUCCESS: " + toUpperAscii(cand.shortName) +
                                       " shone (" + std::to_string(perf.goals) + "G " + std::to_string(perf.assists) +
                                       "A, rating " + std::to_string(static_cast<int>(perf.rating)) + "." +
                                       std::to_string(static_cast<int>(std::round(perf.rating * 10.0)) % 10) +
                                       ")! Market Value jumped " + oldVal + " -> " + newVal + "!";
                    s_session.postMatchPayoffLines.push_back(line);
                    logInfo("%s", line.c_str());

                    if (ev.masterclassSkillBoost) {
                        std::string skillName = upgradePrimarySkillNibble(pf);
                        if (!skillName.empty()) {
                            std::string sLine = "SCOUT MASTERCLASS BONUS: " + toUpperAscii(cand.shortName) +
                                                " gained permanent +1 " + skillName + "!";
                            s_session.postMatchPayoffLines.push_back(sLine);
                            logInfo("%s", sLine.c_str());
                        }
                    }
                }
            } else if (ev.flopped) {
                int16_t cp = 0; uint16_t ints = 0; int64_t lsb = 0; uint8_t st = 0; std::string lq;
                getPressCareerTotals(cp, ints, lsb, st, lq);
                setPressCareerTotals(applyCharismaDelta(cp, -2), ints, lsb, st, lq);
                s_session.postMatchPayoffLines.push_back(ev.summaryLine);
            }
        }
    }

    // 2. Resolve Painkiller Injection post-match aggravation risk
    if (s_session.plan.painkillerUsed && s_session.plan.painkillerCareerSlot >= 0) {
        uint32_t roll = static_cast<uint32_t>(SDL_GetTicks() ^ (s_session.plan.painkillerCareerSlot * 2654435761u + 97u)) % 100u;
        if (static_cast<int>(roll) < s_session.plan.painkillerAggravationPct) {
            size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                         static_cast<size_t>(s_session.plan.painkillerCareerSlot) * HotseatInternal::kPlayerFileSize;
            if (ofs + HotseatInternal::kPlayerFileSize <= CareerOffsets::kCareerTeam + kCareerTeamBufferSize) {
                auto *pf = reinterpret_cast<PlayerFile *>(&SwosVM::g_memByte[ofs]);
                uint8_t curInj = (pf->cardsInjuries >> 4) & 0x0Fu;
                uint8_t nextInj = std::min<uint8_t>(15, static_cast<uint8_t>(curInj + 2));
                pf->cardsInjuries = static_cast<uint8_t>((pf->cardsInjuries & 0x0Fu) | ((nextInj & 0x0Fu) << 4));
                s_session.postMatchPayoffLines.push_back(
                    "PAINKILLER SETBACK: " + toUpperAscii(s_session.plan.painkillerPlayerName) +
                    " aggravated the knock post-match (out " + std::to_string(nextInj) + " matches).");
            }
        } else {
            s_session.postMatchPayoffLines.push_back(
                "PAINKILLER GAMBLE PAID OFF: " + toUpperAscii(s_session.plan.painkillerPlayerName) +
                " came through the match completely unscathed!");
        }
    }
}

void showPostMatchAssistantPayoffIfNeeded()
{
    if (s_session.postMatchPayoffLines.empty() || swos.gameCanceled || swos.g_trainingGame)
        return;

#ifndef SWOS_TEST
    std::string body = "PRE-MATCH PREPARATION OUTCOMES:\n\n";
    for (const auto& line : s_session.postMatchPayoffLines)
        body += "- " + line + "\n\n";
    body += "Press any key to continue.";

    setStandardMenuBackgroundImage();
    menuFadeIn();
    showLlmReportDialog("ASSISTANT MANAGER REPORT", body);
    menuFadeOut();
#endif
    s_session.postMatchPayoffLines.clear();
}

std::string formatPreMatchAssistantContextForTelemetry()
{
    if (!s_session.eligible)
        return {};
    bool anyActive = (s_session.plan.focus != TacticalFocusType::kBalanced) ||
                     s_session.plan.manMarkDangerMan ||
                     (s_session.plan.spotlightCandidateIndex >= 0) ||
                     s_session.plan.painkillerUsed;
    if (!anyActive)
        return {};

    std::string out = "Pre-Match Tactical Call for " + s_session.intel.myClub + ": " +
                      std::string(getTacticalFocusName(s_session.plan.focus));
    if (s_session.plan.manMarkDangerMan)
        out += "; man-marking " + s_session.intel.dangerManName;
    if (s_session.plan.spotlightCandidateIndex >= 0 &&
        s_session.plan.spotlightCandidateIndex < static_cast<int>(s_session.spotlightCandidates.size())) {
        out += "; Shop Window spotlight on " + s_session.spotlightCandidates[s_session.plan.spotlightCandidateIndex].shortName;
    }
    if (s_session.plan.painkillerUsed)
        out += "; risked " + s_session.plan.painkillerPlayerName + " on painkiller injection";
    return out;
}
