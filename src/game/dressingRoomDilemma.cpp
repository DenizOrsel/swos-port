#include "dressingRoomDilemma.h"
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
#include "pressCharisma.h"
#include "render.h"
#include "stadiumFacilities.h"
#include "stats.h"
#include "swos.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace {

int s_matchesSinceLastDilemma = 3; // allow first real flashpoint right away
PendingDressingRoomEffect s_pendingNextMatch;
PendingDressingRoomEffect s_activeThisMatch;
bool s_activeThisMatchHumanIsTeam1 = true;
std::string s_lastDecisionSummary;

uint8_t clampSwosSkill(int v)
{
    return static_cast<uint8_t>(std::clamp(v, 0, 7));
}

void adjustByteSkill(byte& field, int delta)
{
    if (delta != 0)
        field = clampSwosSkill(static_cast<int>(field) + delta);
}

std::string toUpperAscii(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string cleanBoundedName(const char *raw, size_t maxLen)
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

const char *playerPosLabelFromByte(uint8_t posAndFace)
{
    switch (static_cast<PlayerPosition>((posAndFace >> 5) & 0x07u)) {
    case PlayerPosition::kGoalkeeper: return "GK";
    case PlayerPosition::kRightBack:  return "RB";
    case PlayerPosition::kLeftBack:   return "LB";
    case PlayerPosition::kDefender:   return "DEF";
    case PlayerPosition::kRightWing:  return "RW";
    case PlayerPosition::kLeftWing:   return "LW";
    case PlayerPosition::kMidfielder: return "MID";
    case PlayerPosition::kAttacker:   return "ATT";
    default:                          return "SUB";
    }
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

int32_t readVmClubCash()
{
    return *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
}

void applyVmClubCashDelta(int32_t delta)
{
    if (delta == 0)
        return;
    auto *vmBal = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    int64_t next = std::clamp<int64_t>(static_cast<int64_t>(*vmBal) + delta, -2000000000LL, 2000000000LL);
    *vmBal = static_cast<int32_t>(next);

    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        mgr.activePlayer().newBalance = *vmBal;
    }
}

void applyCharismaChange(int8_t delta)
{
    if (delta == 0)
        return;
    int16_t cp = 0; uint16_t ints = 0; int64_t lsb = 0; uint8_t st = 0; std::string lq;
    getPressCareerTotals(cp, ints, lsb, st, lq);
    setPressCareerTotals(applyCharismaDelta(cp, delta), ints, lsb, st, lq);
}

int readVmDivisionTier()
{
    uint16_t raw = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]);
    return std::clamp<int>(static_cast<int>(raw), 0, 3);
}

std::string readCurrentCareerClubName()
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        if (!mgr.activePlayer().teamName.empty())
            return mgr.activePlayer().teamName;
    }
    const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
    if (hdr && hdr->teamName[0] != '\0')
        return hdr->teamName;
    return "THE CLUB";
}

bool resolveHumanCareerSide(const TeamGame *topTeam, const TeamGame *bottomTeam, bool& outHumanIsTeam1)
{
    if (!topTeam || !bottomTeam)
        return false;
    const TeamFile *tf1 = swos.gameTeam1.asAligned();
    const TeamFile *tf2 = swos.gameTeam2.asAligned();
    if (!tf1) tf1 = swos.playMatchTeam1Ptr.asAligned();
    if (!tf2) tf2 = swos.playMatchTeam2Ptr.asAligned();

    const TeamGeneralInfo *team1Data = &swos.topTeamData;
    const TeamGeneralInfo *team2Data = &swos.bottomTeamData;
    if (swos.topTeamData.inGameTeamPtr.asAligned() == &swos.bottomTeamInGame ||
        swos.bottomTeamData.inGameTeamPtr.asAligned() == &swos.topTeamInGame) {
        team1Data = &swos.bottomTeamData;
        team2Data = &swos.topTeamData;
    }

    bool t1 = (team1Data->playerNumber != 0 || team1Data->playerCoachNumber != 0 || team1Data->isPlCoach != 0);
    bool t2 = (team2Data->playerNumber != 0 || team2Data->playerCoachNumber != 0 || team2Data->isPlCoach != 0);
    if (!t1 && !t2) {
        if (tf1) t1 = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
        if (tf2) t2 = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);
    }
    if (t1 == t2 && swos.g_gameType == kGameTypeCareer) {
        std::string clubUp = toUpperAscii(readCurrentCareerClubName());
        bool m1 = (toUpperAscii(topTeam->teamName) == clubUp);
        bool m2 = (toUpperAscii(bottomTeam->teamName) == clubUp);
        if (m1 != m2) {
            t1 = m1;
            t2 = m2;
        }
    }
    if (t1 == t2)
        return false;
    outHumanIsTeam1 = t1;
    return true;
}

std::string upgradeOneSkillNibble(PlayerFile *pf)
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
    if (bumpLow(pf->passing)) return "PASSING";
    if (bumpHigh(pf->shootingHeading)) return "SHOOTING";
    if (bumpLow(pf->speedFinishing)) return "FINISHING";
    if (bumpHigh(pf->speedFinishing)) return "SPEED";
    return {};
}

struct CareerSquadEntry {
    int slot = -1;
    int shirtNumber = 0;
    std::string name;
    std::string pos;
    uint8_t price = 0;
    uint8_t injMatches = 0;
    bool isStarter = false;
    bool isRedCarded = false;
};

std::vector<CareerSquadEntry> scanCareerSquad(const TeamGame *humanTeam)
{
    std::vector<CareerSquadEntry> out;
    uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    uint16_t count = std::min<uint16_t>(numPlayers, kCurrentMatchPlayersSize);
    const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];
    bool seen[kCurrentMatchPlayersSize] = {};

    for (uint16_t i = 0; i < count; ++i) {
        uint8_t slot = matchPlayers[i];
        if (slot >= kCurrentMatchPlayersSize || seen[slot])
            continue;
        seen[slot] = true;
        size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
        if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
            continue;
        const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
        std::string nm = cleanBoundedName(pf->playerName, sizeof(pf->playerName));
        if (nm.empty() || nm.rfind("*ERR*", 0) == 0)
            continue;
        CareerSquadEntry e;
        e.slot = static_cast<int>(slot);
        e.shirtNumber = static_cast<int>(pf->shirtNumber);
        e.name = std::move(nm);
        e.pos = playerPosLabelFromByte(pf->positionAndFace);
        e.price = pf->price;
        e.injMatches = (pf->cardsInjuries >> 4) & 0x0Fu;
        e.isStarter = (i < 11);
        if (humanTeam) {
            for (int k = 0; k < kNumPlayersInTeam; ++k) {
                if (humanTeam->players[k].shirtNumber == pf->shirtNumber && humanTeam->players[k].sentOff()) {
                    e.isRedCarded = true;
                    break;
                }
            }
        }
        out.push_back(std::move(e));
    }
    return out;
}

PlayerFile *playerFileAtSlot(int slot)
{
    if (slot < 0 || slot >= kCurrentMatchPlayersSize)
        return nullptr;
    size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                 static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
    if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
        return nullptr;
    return reinterpret_cast<PlayerFile *>(&SwosVM::g_memByte[ofs]);
}

} // namespace

// --- Pure Deterministic Logic ---

int32_t computeDressingRoomCashScale(int divisionTier, int baseDiv4Amount)
{
    // Div 4 (3) = 1.0x, Div 3 (2) = 1.6x, Div 2 (1) = 2.5x, Div 1 (0) = 4.0x
    int mult10 = (divisionTier <= 0) ? 40 :
                 (divisionTier == 1) ? 25 :
                 (divisionTier == 2) ? 16 : 10;
    int32_t raw = (baseDiv4Amount * mult10) / 10;
    return std::max<int32_t>(1000, ((raw + 500) / 1000) * 1000);
}

DressingRoomFlashpointType detectDressingRoomFlashpoint(const DressingRoomFlashpointContext& ctx)
{
    int margin = ctx.clubGoals - ctx.opponentGoals;
    bool wonMatch = (margin > 0);
    bool bigWin = (margin >= 4);
    bool heavyDefeat = (margin <= -3);
    bool urgentRed = (ctx.clubRedCards > 0);

    // Urgent crises bypass normal 3-match cooldown if >= 2 matches passed; routine flashpoints require >= 3 matches
    if (urgentRed && ctx.matchesSinceLastDilemma >= 2)
        return DressingRoomFlashpointType::kRedCardDiscipline;
    if (heavyDefeat && ctx.matchesSinceLastDilemma >= 2)
        return DressingRoomFlashpointType::kSlumpCrisis;

    if (ctx.matchesSinceLastDilemma < 3)
        return DressingRoomFlashpointType::kNone;

    // Inspect recentForm (oldest -> newest)
    int n = static_cast<int>(ctx.recentForm.size());
    int winStreak = 0, lossStreak = 0, winlessStreak = 0;
    for (int i = n - 1; i >= 0 && ctx.recentForm[i] == 'W'; --i) ++winStreak;
    for (int i = n - 1; i >= 0 && ctx.recentForm[i] == 'L'; --i) ++lossStreak;
    for (int i = n - 1; i >= 0 && ctx.recentForm[i] != 'W'; --i) ++winlessStreak;

    // Never trigger a slump crisis right after winning a match!
    if (!wonMatch && (lossStreak >= 3 || winlessStreak >= 4))
        return DressingRoomFlashpointType::kSlumpCrisis;
    // Trigger bonus buzz on a 4+ win streak OR an emphatic 4+ goal demolition (e.g. 7-0!)
    if (winStreak >= 4 || bigWin)
        return DressingRoomFlashpointType::kWinStreakBonus;
    if (ctx.injuredCount >= 2 || ctx.maxInjuryMatches >= 3)
        return DressingRoomFlashpointType::kSquadRecovery;

    // Occasional player 1-on-1 door knock after cooldown (>= 4 matches or 40% roll after 3 matches)
    if (ctx.matchesSinceLastDilemma >= 4 || (ctx.seed % 100u) < 40u)
        return DressingRoomFlashpointType::kPlayerDoorKnock;

    return DressingRoomFlashpointType::kNone;
}

DressingRoomDilemmaOffer buildDressingRoomDilemmaOffer(
    DressingRoomFlashpointType type,
    const std::string& clubName,
    const std::string& playerName,
    const std::string& playerPos,
    int playerCareerSlot,
    int playerShirtNumber,
    int divisionTier,
    int injuredCount)
{
    DressingRoomDilemmaOffer o;
    o.type = type;
    o.playerName = playerName.empty() ? "CLUB CAPTAIN" : playerName;
    o.playerPos = playerPos.empty() ? "MID" : playerPos;
    o.playerCareerSlot = playerCareerSlot;
    o.playerShirtNumber = playerShirtNumber;

    switch (type) {
    case DressingRoomFlashpointType::kRedCardDiscipline: {
        int32_t fine = computeDressingRoomCashScale(divisionTier, 10000);
        o.bannerTitle = "RED CARD BUST-UP!";
        o.offlineSpeech = o.playerName + ": \"Ref lost the plot out there, Boss! Are you backing me or fining me?\"";
        o.choices[0] = { "FINE 2 WEEKS' WAGES (+" + formatPoundsShort(fine) + ")",
                         "+" + formatPoundsShort(fine) + " Cash & +8 Charisma",
                         o.playerName + " sulks (-1 PAS/CTL next match)", fine };
        o.choices[1] = { "PUBLICLY BACK HIM",
                         o.playerName + " fired up (+2 FIN/TKL next match)",
                         "-6 Charisma (press slams indiscipline)", 0 };
        o.choices[2] = { "EXTRA TRAINING SESSIONS",
                         "55% chance permanent +1 skill on " + o.playerName,
                         "Otherwise +1 Passing next match (free)", 0 };
        break;
    }

    case DressingRoomFlashpointType::kSlumpCrisis: {
        int32_t bondCost = computeDressingRoomCashScale(divisionTier, 6000);
        o.bannerTitle = "DRESSING ROOM SLUMP CRISIS";
        o.offlineSpeech = o.playerName + ": \"Heads are down in here, Gaffer. The lads need a spark before the fans turn on " + clubName + ".\"";
        o.choices[0] = { "FERGIE HAIRDRYER TREATMENT",
                         "Whole XI +1 SPD & +1 TKL next match (FREE)",
                         "FAIL to win next match -> -8 Charisma!", 0 };
        o.choices[1] = { "TEAM BONDING NIGHT (-" + formatPoundsShort(bondCost) + ")",
                         "Whole XI +1 PAS & +1 CTL next match & +6 CP",
                         "Costs " + formatPoundsShort(bondCost), -bondCost };
        o.choices[2] = { "BACK THE YOUTH / SHAKE UP",
                         "Cheapest starter gets permanent +1 SKILL & +1 VALUE!",
                         "Senior players grumble (-1 next match)", 0 };
        break;
    }

    case DressingRoomFlashpointType::kWinStreakBonus: {
        int32_t bonusCost = computeDressingRoomCashScale(divisionTier, 10000);
        o.bannerTitle = "HOT-STREAK BONUS DEMAND";
        o.offlineSpeech = o.playerName + ": \"We're flying, Boss! The dressing room is asking if the board can drop a win-streak bonus.\"";
        o.choices[0] = { "PAY SQUAD BONUS (-" + formatPoundsShort(bonusCost) + ")",
                         "Whole XI +1 FIN/SPD/PAS next match & +10 CP!",
                         "Costs " + formatPoundsShort(bonusCost), -bonusCost };
        o.choices[1] = { "PROMISE DOUBLE IF WE WIN NEXT",
                         "FREE now! Win next -> +15 CP & +1 VALUE on " + o.playerName,
                         "Win pays -" + formatPoundsShort(bonusCost) + "; fail -> -5 CP", 0 };
        o.choices[2] = { "REFUSE (\"DO YOUR JOB!\")",
                         "Save club cash (FREE)",
                         "Whole XI -1 Ball Control next match & -5 CP", 0 };
        break;
    }

    case DressingRoomFlashpointType::kPlayerDoorKnock: {
        int32_t drillCost = computeDressingRoomCashScale(divisionTier, 5000);
        o.bannerTitle = "OFFICE DOOR KNOCK";
        o.offlineSpeech = o.playerName + ": \"Got a minute, Gaffer? I'm hungry to step up — give me extra coaching or a challenge!\"";
        o.choices[0] = { "1-ON-1 PRIVATE DRILL (-" + formatPoundsShort(drillCost) + ")",
                         "PERMANENT +1 SKILL & +1 VALUE on " + o.playerName + "!",
                         "Costs " + formatPoundsShort(drillCost), -drillCost };
        o.choices[1] = { "CHALLENGE HIM ON THE PITCH",
                         o.playerName + " +2 FIN/SPD next; goal/ast -> +1 VALUE!",
                         "FREE high-upside motivation", 0 };
        o.choices[2] = { "PUT HIM IN HIS PLACE",
                         "+5 Charisma (firm boss authority)",
                         o.playerName + " -1 Passing/Control next match", 0 };
        break;
    }

    case DressingRoomFlashpointType::kSquadRecovery: {
        int32_t restCost = computeDressingRoomCashScale(divisionTier, 5000);
        o.bannerTitle = "TIRED LEGS & TREATMENT TABLE";
        o.offlineSpeech = o.playerName + ": \"The physio room is packed (" + std::to_string(std::max(1, injuredCount)) +
                          " injured) and legs are heavy, Boss. What's the call this week?\"";
        o.choices[0] = { "REST & RECOVERY DAY (-" + formatPoundsShort(restCost) + ")",
                         "Cuts -1 match off ALL injured players right now!",
                         "Costs " + formatPoundsShort(restCost), -restCost };
        o.choices[1] = { "HARD FITNESS DRILL (RISKY)",
                         "Whole XI +1 Speed next match (FREE)",
                         "15% risk of a 1-match minor knock", 0 };
        o.choices[2] = { "LIGHT TACTICAL WALKTHROUGH",
                         "Midfielders +1 Passing next match & +3 CP",
                         "Safe & free", 0 };
        break;
    }

    default:
        break;
    }

    return o;
}

void applyPendingDressingRoomEffectToTeam(TeamGame& myTeam, const PendingDressingRoomEffect& eff)
{
    if (!eff.active)
        return;

    std::string targetUp = toUpperAscii(eff.targetPlayerName);

    for (int i = 0; i < kNumPlayersInTeam; ++i) {
        auto& pl = myTeam.players[i];
        if (pl.shortName[0] == '\0')
            continue;

        if (i > 0 && pl.position != PlayerPosition::kGoalkeeper) {
            adjustByteSkill(pl.speed, eff.squadSpeedDelta);
            adjustByteSkill(pl.passing, eff.squadPassingDelta);
            adjustByteSkill(pl.ballControl, eff.squadControlDelta);
            adjustByteSkill(pl.tackling, eff.squadTacklingDelta);
            adjustByteSkill(pl.finishing, eff.squadFinishingDelta);
            if (eff.veteranPenaltyDelta != 0 && i >= 6 && i < 11) {
                adjustByteSkill(pl.passing, eff.veteranPenaltyDelta);
            }
        }

        bool isTarget = (eff.targetShirtNumber > 0 && static_cast<int>(pl.shirtNumber) == eff.targetShirtNumber) ||
                        (!targetUp.empty() && toUpperAscii(pl.shortName) == targetUp);
        if (isTarget) {
            adjustByteSkill(pl.finishing, eff.playerFinishingDelta);
            adjustByteSkill(pl.tackling, eff.playerTacklingDelta);
            adjustByteSkill(pl.speed, eff.playerSpeedDelta);
            adjustByteSkill(pl.passing, eff.playerPassingDelta);
            adjustByteSkill(pl.ballControl, eff.playerControlDelta);
        }
    }
}

// --- Career & Match Lifecycle Hooks ---

void resetDressingRoomDilemmaState()
{
    s_matchesSinceLastDilemma = 3;
    s_pendingNextMatch = PendingDressingRoomEffect{};
    s_activeThisMatch = PendingDressingRoomEffect{};
    s_lastDecisionSummary.clear();
}

void applyPendingDressingRoomEffectsForMatch(TeamGame *topTeam, TeamGame *bottomTeam)
{
    s_activeThisMatch = PendingDressingRoomEffect{};
    if (!s_pendingNextMatch.active || !topTeam || !bottomTeam || swos.g_trainingGame ||
        swos.g_gameType != kGameTypeCareer || HotseatInternal::isVmNationalSelector()) {
        return;
    }

    bool humanIsTeam1 = true;
    if (!resolveHumanCareerSide(topTeam, bottomTeam, humanIsTeam1))
        return;

    s_activeThisMatch = s_pendingNextMatch;
    s_activeThisMatchHumanIsTeam1 = humanIsTeam1;
    s_pendingNextMatch = PendingDressingRoomEffect{};

    TeamGame& myTeam = humanIsTeam1 ? *topTeam : *bottomTeam;
    applyPendingDressingRoomEffectToTeam(myTeam, s_activeThisMatch);
    logInfo("Dressing Room effect applied to match: %s", s_activeThisMatch.label.c_str());
}

static void resolvePendingDressingRoomPromises(int topGoals, int botGoals)
{
    if (!s_activeThisMatch.active)
        return;

    PendingDressingRoomEffect eff = s_activeThisMatch;
    s_activeThisMatch = PendingDressingRoomEffect{};

    int clubG = s_activeThisMatchHumanIsTeam1 ? topGoals : botGoals;
    int oppG = s_activeThisMatchHumanIsTeam1 ? botGoals : topGoals;
    bool won = (clubG > oppG);

    std::vector<std::string> notices;

    if (eff.hairdryerWinRequired) {
        if (won) {
            notices.push_back("HAIRDRYER PAYOFF: The squad responded with a vital win! (+5 Charisma)");
            applyCharismaChange(+5);
        } else {
            notices.push_back("HAIRDRYER BACKFIRE: Failed to win after blasting the dressing room (-8 Charisma).");
            applyCharismaChange(-8);
        }
    }

    if (eff.doubleBonusWinPromise) {
        if (won) {
            applyVmClubCashDelta(-eff.promisedBonusCost);
            applyCharismaChange(+15);
            if (PlayerFile *pf = playerFileAtSlot(eff.targetCareerSlot)) {
                if (pf->price < 48) ++pf->price;
            }
            notices.push_back("WIN BONUS PROMISE KEPT: Paid " + formatPoundsShort(eff.promisedBonusCost) +
                              " — +15 Charisma & +1 Market Value on " + toUpperAscii(eff.targetPlayerName) + "!");
        } else {
            applyCharismaChange(-5);
            notices.push_back("WIN BONUS MISSED: Squad fell short of the promised win (-5 Charisma).");
        }
    }

    if (eff.proveItChallenge && eff.targetCareerSlot >= 0) {
        auto perf = getPlayerMatchPerformance(
            s_activeThisMatchHumanIsTeam1 ? 1 : 2, eff.targetShirtNumber, eff.targetPlayerName);
        if (perf.found && (perf.goals >= 1 || perf.assists >= 1)) {
            if (PlayerFile *pf = playerFileAtSlot(eff.targetCareerSlot)) {
                if (pf->price < 48) ++pf->price;
            }
            notices.push_back("CHALLENGE ACCEPTED: " + toUpperAscii(eff.targetPlayerName) +
                              " delivered (" + std::to_string(perf.goals) + "G " + std::to_string(perf.assists) +
                              "A) — PERMANENT +1 MARKET VALUE TIER!");
        }
    }

#ifndef SWOS_TEST
    if (!notices.empty() && !swos.gameCanceled) {
        std::string body = "DRESSING ROOM OUTCOMES:\n\n";
        for (const auto& n : notices)
            body += "- " + n + "\n\n";
        setStandardMenuBackgroundImage();
        menuFadeIn();
        showLlmReportDialog("DRESSING ROOM FOLLOW-UP", body);
        menuFadeOut();
    }
#endif
}

static std::string resolveDilemmaChoice(
    const DressingRoomDilemmaOffer& offer,
    int choiceIdx,
    const std::vector<CareerSquadEntry>& squad,
    uint32_t seed)
{
    s_pendingNextMatch = PendingDressingRoomEffect{};

    if (choiceIdx < 0 || choiceIdx > 2) {
        // [ESC] Brush off
        bool wasCrisis = (offer.type == DressingRoomFlashpointType::kRedCardDiscipline ||
                          offer.type == DressingRoomFlashpointType::kSlumpCrisis);
        if (wasCrisis)
            applyCharismaChange(-2);
        s_lastDecisionSummary = "Brushed off dressing room " + offer.bannerTitle;
        return wasCrisis
            ? "You brushed off the dressing room tension (-2 Charisma)."
            : "You moved straight on to the next training session (no changes).";
    }

    const auto& ch = offer.choices[choiceIdx];
    if (ch.cashDelta < 0 && readVmClubCash() < -ch.cashDelta) {
        return {}; // Caller shows insufficient funds banner
    }

    if (ch.cashDelta != 0)
        applyVmClubCashDelta(ch.cashDelta);

    switch (offer.type) {
    case DressingRoomFlashpointType::kRedCardDiscipline:
        if (choiceIdx == 0) {
            applyCharismaChange(+8);
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Fined " + offer.playerName + " 2 weeks' wages (+8 CP)";
            s_pendingNextMatch.targetShirtNumber = offer.playerShirtNumber;
            s_pendingNextMatch.targetPlayerName = offer.playerName;
            s_pendingNextMatch.playerPassingDelta = -1;
            s_pendingNextMatch.playerControlDelta = -1;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "FINED " + toUpperAscii(offer.playerName) + ": +" + formatPoundsShort(ch.cashDelta) +
                   " added to bank & +8 Charisma! (" + offer.playerName + " -1 PAS/CTL next match).";
        } else if (choiceIdx == 1) {
            applyCharismaChange(-6);
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Backed " + offer.playerName + " publicly after red card";
            s_pendingNextMatch.targetShirtNumber = offer.playerShirtNumber;
            s_pendingNextMatch.targetPlayerName = offer.playerName;
            s_pendingNextMatch.playerFinishingDelta = +2;
            s_pendingNextMatch.playerTacklingDelta = +2;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "BACKED " + toUpperAscii(offer.playerName) + ": Fired up (+2 FIN/TKL next match), press takes -6 Charisma.";
        } else {
            PlayerFile *pf = playerFileAtSlot(offer.playerCareerSlot);
            if (pf && (seed % 100u) < 55u) {
                std::string sk = upgradeOneSkillNibble(pf);
                if (!sk.empty()) {
                    s_lastDecisionSummary = offer.playerName + " trained hard (+1 permanent " + sk + ")";
                    return "EXTRA DRILLS PAID OFF: " + toUpperAscii(offer.playerName) +
                           " gained a PERMANENT +1 " + sk + "!";
                }
            }
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = offer.playerName + " put in extra training";
            s_pendingNextMatch.targetShirtNumber = offer.playerShirtNumber;
            s_pendingNextMatch.targetPlayerName = offer.playerName;
            s_pendingNextMatch.playerPassingDelta = +1;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "EXTRA TRAINING: " + toUpperAscii(offer.playerName) + " looks sharper (+1 Passing next match).";
        }

    case DressingRoomFlashpointType::kSlumpCrisis:
        if (choiceIdx == 0) {
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Fergie Hairdryer Treatment (+1 SPD/TKL XI; MUST win!)";
            s_pendingNextMatch.squadSpeedDelta = +1;
            s_pendingNextMatch.squadTacklingDelta = +1;
            s_pendingNextMatch.hairdryerWinRequired = true;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "HAIRDRYER UNLEASHED: Whole XI gets +1 Speed & +1 Tackling next match! Win next or lose -8 CP.";
        } else if (choiceIdx == 1) {
            applyCharismaChange(+6);
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Team Bonding Night (+1 PAS/CTL XI)";
            s_pendingNextMatch.squadPassingDelta = +1;
            s_pendingNextMatch.squadControlDelta = +1;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "TEAM BONDING NIGHT: Whole XI gets +1 Passing & +1 Ball Control next match, plus +6 Charisma!";
        } else {
            // Find cheapest starter for permanent +1 skill & +1 market value
            int bestSlot = offer.playerCareerSlot;
            std::string youthName = offer.playerName;
            uint8_t minPrice = 255;
            for (const auto& e : squad) {
                if (e.isStarter && e.pos != "GK" && e.price < minPrice) {
                    minPrice = e.price;
                    bestSlot = e.slot;
                    youthName = e.name;
                }
            }
            std::string sk = "SKILL";
            if (PlayerFile *pf = playerFileAtSlot(bestSlot)) {
                std::string up = upgradeOneSkillNibble(pf);
                if (!up.empty()) sk = up;
                if (pf->price < 48) ++pf->price;
            }
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Youth Shake-Up boosted " + youthName;
            s_pendingNextMatch.veteranPenaltyDelta = -1;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "YOUTH SHAKE-UP: " + toUpperAscii(youthName) + " gained PERMANENT +1 " + sk +
                   " & +1 MARKET VALUE TIER! (Veterans -1 PAS next match).";
        }

    case DressingRoomFlashpointType::kWinStreakBonus:
        if (choiceIdx == 0) {
            applyCharismaChange(+10);
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Paid Win-Streak Squad Bonus (+1 FIN/SPD/PAS XI)";
            s_pendingNextMatch.squadFinishingDelta = +1;
            s_pendingNextMatch.squadSpeedDelta = +1;
            s_pendingNextMatch.squadPassingDelta = +1;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "BONUS PAID: Whole XI flies into next match with +1 FIN/SPD/PAS & +10 Charisma!";
        } else if (choiceIdx == 1) {
            int32_t promised = computeDressingRoomCashScale(readVmDivisionTier(), 10000);
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Promised Double Win Bonus to " + offer.playerName;
            s_pendingNextMatch.doubleBonusWinPromise = true;
            s_pendingNextMatch.promisedBonusCost = promised;
            s_pendingNextMatch.targetCareerSlot = offer.playerCareerSlot;
            s_pendingNextMatch.targetPlayerName = offer.playerName;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "PROMISE MADE: Win the next match to unlock +15 Charisma & +1 Value on " +
                   toUpperAscii(offer.playerName) + "!";
        } else {
            applyCharismaChange(-5);
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Refused win-streak bonus (-1 CTL XI)";
            s_pendingNextMatch.squadControlDelta = -1;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "BONUS REFUSED: Saved club cash, but squad takes -1 Ball Control next match & -5 Charisma.";
        }

    case DressingRoomFlashpointType::kPlayerDoorKnock:
        if (choiceIdx == 0) {
            std::string sk = "SKILL";
            if (PlayerFile *pf = playerFileAtSlot(offer.playerCareerSlot)) {
                std::string up = upgradeOneSkillNibble(pf);
                if (!up.empty()) sk = up;
                if (pf->price < 48) ++pf->price;
            }
            s_lastDecisionSummary = "1-on-1 coaching upgraded " + offer.playerName + " (+1 " + sk + ")";
            return "1-ON-1 MASTERCLASS: " + toUpperAscii(offer.playerName) +
                   " gained PERMANENT +1 " + sk + " & +1 MARKET VALUE TIER!";
        } else if (choiceIdx == 1) {
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Challenged " + offer.playerName + " to prove it (+2 FIN/SPD)";
            s_pendingNextMatch.targetShirtNumber = offer.playerShirtNumber;
            s_pendingNextMatch.targetPlayerName = offer.playerName;
            s_pendingNextMatch.targetCareerSlot = offer.playerCareerSlot;
            s_pendingNextMatch.playerFinishingDelta = +2;
            s_pendingNextMatch.playerSpeedDelta = +2;
            s_pendingNextMatch.proveItChallenge = true;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "GAUNTLET THROWN: " + toUpperAscii(offer.playerName) +
                   " gets +2 FIN & +2 SPD next match (score/assist -> permanent +1 Value!).";
        } else {
            applyCharismaChange(+5);
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Disciplined " + offer.playerName + " (+5 CP)";
            s_pendingNextMatch.targetShirtNumber = offer.playerShirtNumber;
            s_pendingNextMatch.targetPlayerName = offer.playerName;
            s_pendingNextMatch.playerPassingDelta = -1;
            s_pendingNextMatch.playerControlDelta = -1;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "AUTHORITY STAMPED: +5 Charisma! (" + toUpperAscii(offer.playerName) + " -1 PAS/CTL next match).";
        }

    case DressingRoomFlashpointType::kSquadRecovery:
        if (choiceIdx == 0) {
            int healed = 0;
            for (const auto& e : squad) {
                if (e.injMatches > 0) {
                    if (PlayerFile *pf = playerFileAtSlot(e.slot)) {
                        uint8_t nextInj = static_cast<uint8_t>(e.injMatches - 1);
                        pf->cardsInjuries = static_cast<uint8_t>((pf->cardsInjuries & 0x0Fu) | ((nextInj & 0x0Fu) << 4));
                        ++healed;
                    }
                }
            }
            s_lastDecisionSummary = "Rest & Recovery Day shortened " + std::to_string(healed) + " injuries";
            return "RECOVERY DAY: Cut -1 match off " + std::to_string(healed) +
                   " injured squad player" + (healed == 1 ? "" : "s") + " right now!";
        } else if (choiceIdx == 1) {
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Hard Fitness Drill (+1 SPD XI)";
            s_pendingNextMatch.squadSpeedDelta = +1;
            std::string knockNote;
            if ((seed % 100u) < 15u && !squad.empty()) {
                for (const auto& e : squad) {
                    if (e.injMatches == 0 && e.pos != "GK") {
                        if (PlayerFile *pf = playerFileAtSlot(e.slot)) {
                            pf->cardsInjuries = static_cast<uint8_t>((pf->cardsInjuries & 0x0Fu) | (1u << 4));
                            knockNote = " (Setback: " + toUpperAscii(e.name) + " tweaked a hamstring - 1 match).";
                            break;
                        }
                    }
                }
            }
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "HARD FITNESS DRILL: Whole XI gets +1 Speed next match!" + knockNote;
        } else {
            applyCharismaChange(+3);
            s_pendingNextMatch.active = true;
            s_pendingNextMatch.label = "Light Tactical Walkthrough (+1 PAS XI)";
            s_pendingNextMatch.squadPassingDelta = +1;
            s_lastDecisionSummary = s_pendingNextMatch.label;
            return "SMART PREPARATION: Whole XI gets +1 Passing next match & +3 Charisma!";
        }

    default:
        return {};
    }
}

void onCareerMatchFinishedDressingRoomDilemma(int topGoals, int botGoals)
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame || swos.gameCanceled ||
        HotseatInternal::isVmNationalSelector())
        return;

    // 1. Always resolve any pending dressing-room promise from the previous dilemma first!
    resolvePendingDressingRoomPromises(topGoals, botGoals);

    const auto& cfg = getLlmConfig();
    if (!cfg.features.dressingRoomVoices)
        return;

    ++s_matchesSinceLastDilemma;

    bool humanIsTeam1 = true;
    if (!resolveHumanCareerSide(&swos.topTeamInGame, &swos.bottomTeamInGame, humanIsTeam1))
        return;

    const TeamGame *humanTeam = humanIsTeam1 ? &swos.topTeamInGame : &swos.bottomTeamInGame;
    const auto stats = getStats();
    const auto& hStats = humanIsTeam1 ? stats.team1 : stats.team2;

    auto squad = scanCareerSquad(humanTeam);
    if (squad.empty())
        return;

    int injCnt = 0;
    int maxInj = 0;
    for (const auto& e : squad) {
        if (e.injMatches > 0) {
            ++injCnt;
            maxInj = std::max<int>(maxInj, e.injMatches);
        }
    }

    DressingRoomFlashpointContext ctx;
    ctx.clubGoals = humanIsTeam1 ? topGoals : botGoals;
    ctx.opponentGoals = humanIsTeam1 ? botGoals : topGoals;
    ctx.clubRedCards = hStats.sendingsOff;
    ctx.clubYellowCards = hStats.bookings;
    ctx.recentForm = getCurrentCareerStadium().recentForm;
    ctx.injuredCount = injCnt;
    ctx.maxInjuryMatches = maxInj;
    ctx.matchesSinceLastDilemma = s_matchesSinceLastDilemma;
    ctx.divisionTier = readVmDivisionTier();
    ctx.seed = static_cast<uint32_t>(SDL_GetTicks() ^ (s_matchesSinceLastDilemma * 131u + ctx.clubGoals * 17u));

    DressingRoomFlashpointType fp = detectDressingRoomFlashpoint(ctx);
    if (fp == DressingRoomFlashpointType::kNone)
        return;

    // Pick the most authentic squad speaker for this exact flashpoint
    const CareerSquadEntry *speaker = &squad.front();
    if (fp == DressingRoomFlashpointType::kRedCardDiscipline) {
        for (const auto& e : squad) {
            if (e.isRedCarded) { speaker = &e; break; }
        }
    } else if (fp == DressingRoomFlashpointType::kPlayerDoorKnock) {
        // Prefer an outfield fringe/bench player or rising starter
        for (const auto& e : squad) {
            if (!e.isStarter && e.pos != "GK" && e.injMatches == 0) { speaker = &e; break; }
        }
        if (speaker == &squad.front() && squad.size() > 2)
            speaker = &squad[(ctx.seed % (squad.size() - 1)) + 1];
    } else {
        // Captain = highest-priced fit outfield starter
        uint8_t bestP = 0;
        for (const auto& e : squad) {
            if (e.isStarter && e.pos != "GK" && e.price >= bestP) {
                bestP = e.price;
                speaker = &e;
            }
        }
    }

    std::string clubName = readCurrentCareerClubName();
    const TeamGame *oppTeam = humanIsTeam1 ? &swos.bottomTeamInGame : &swos.topTeamInGame;
    std::string oppName = (oppTeam && oppTeam->teamName[0]) ? std::string(oppTeam->teamName) : std::string("the opposition");
    DressingRoomDilemmaOffer offer = buildDressingRoomDilemmaOffer(
        fp, clubName, speaker->name, speaker->pos, speaker->slot, speaker->shirtNumber, ctx.divisionTier, injCnt);

    // Tailor banner & offline quote strictly to the actual match result so big wins feel celebratory!
    bool wonMatch = (ctx.clubGoals > ctx.opponentGoals);
    bool lostMatch = (ctx.clubGoals < ctx.opponentGoals);
    std::string scoreStr = std::to_string(ctx.clubGoals) + "-" + std::to_string(ctx.opponentGoals);
    if (fp == DressingRoomFlashpointType::kWinStreakBonus && (ctx.clubGoals - ctx.opponentGoals) >= 4) {
        offer.bannerTitle = scoreStr + " DEMOLITION — BONUS BUZZ!";
        offer.offlineSpeech = offer.playerName + ": \"What a " + scoreStr + " thrashing of " + oppName +
                              ", Boss! The lads are buzzing — any chance of a squad bonus for that show?\"";
    } else if (fp == DressingRoomFlashpointType::kPlayerDoorKnock && wonMatch) {
        offer.offlineSpeech = offer.playerName + ": \"Brilliant " + scoreStr + " win over " + oppName +
                              ", Gaffer! I'm buzzing — give me extra 1-on-1 drills or a pitch challenge to keep flying!\"";
    }

    // Optional async LLM dialogue upgrade (strictly grounded in WIN/DRAW/LOSS outcome and mood!)
    uint64_t llmReqId = 0;
    if (cfg.enabled) {
        std::string resultVerb = wonMatch ? "WON" : (lostMatch ? "LOST" : "DREW");
        bool tenseMood = (fp == DressingRoomFlashpointType::kRedCardDiscipline ||
                          fp == DressingRoomFlashpointType::kSlumpCrisis);
        std::string moodRule = wonMatch
            ? ("Mood is UPBEAT and CELEBRATORY because " + clubName + " just WON " + scoreStr + " against " + oppName +
               "! NEVER complain or say 'we should do better' after a win!")
            : (tenseMood
                ? "Mood is tense and urgent because of the crisis/red card."
                : "Mood is constructive and focused on improving for the next fixture.");

        std::string prompt = "Club: " + clubName + ". Opponent just played: " + oppName + ".\n" +
                             "EXACT MATCH RESULT: " + clubName + " " + resultVerb + " " + scoreStr + " vs " + oppName + "!\n" +
                             "Last-5 Form (oldest->newest): " + (ctx.recentForm.empty() ? "N/A" : ctx.recentForm) + ".\n" +
                             "Speaker: " + offer.playerName + " (" + offer.playerPos + ").\n" +
                             "Dressing Room Topic: " + offer.bannerTitle + ".\n" +
                             "STRICT MOOD RULE: " + moodRule + "\n" +
                             "Write ONE authentic 1997 footballer quote (max 145 chars) speaking to the manager about this exact topic and exact result. " +
                             "Start with '" + offer.playerName + ": '. Plain text only.";
        std::string sys = tenseMood
            ? "You are a passionate 1997 footballer speaking candidly to your manager in the dressing room after a tough moment. Stay 100% faithful to the exact WIN/LOSS/DRAW score. Plain text only."
            : "You are an enthusiastic 1997 footballer chatting with your manager in the dressing room. Stay 100% faithful to the exact WIN/LOSS/DRAW score — never complain after a victory! Plain text only.";
        llmReqId = enqueueLlmRequest(sys, prompt, 180);
    }

    s_matchesSinceLastDilemma = 0;

#ifndef SWOS_TEST
    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = r ? getCompactFontAtlas(r) : nullptr;
    if (!r || !atlas)
        return;

    setStandardMenuBackgroundImage();
    menuFadeIn();
    flushKeyBuffer();
    waitForKeyboardAndMouseIdle();

    std::string liveSpeech = offer.offlineSpeech;
    std::string outcomeMsg;
    std::string errorToast;
    Uint32 errorTick = 0;
    bool resolved = false;
    bool prevKeys[5] = {};
    bool prevMouseDown = false;
    Uint32 openTick = SDL_GetTicks();

    const SDL_Color whiteCol{ 244, 246, 248, 255 };
    const SDL_Color mutedCol{ 166, 178, 188, 255 };
    const SDL_Color yellowCol{ 255, 224, 78, 255 };
    const SDL_Color cyanCol{ 118, 232, 246, 255 };
    const SDL_Color greenCol{ 96, 238, 134, 255 };
    const SDL_Color redCol{ 248, 114, 114, 255 };
    const SDL_Color orangeCol{ 250, 178, 84, 255 };

    auto fitPx = [](const std::string& s, float basePx, float maxW) -> float {
        int u = measureCompactTextUnits(s);
        if (u <= 0 || maxW <= 1.0f) return basePx;
        float needed = static_cast<float>(u) * basePx;
        return (needed <= maxW) ? basePx : std::max(0.75f, basePx * (maxW / needed));
    };

    while (true) {
        processControlEvents();

        if (llmReqId != 0 && !resolved) {
            std::string reply;
            if (tryTakeLlmResponse(llmReqId, reply)) {
                llmReqId = 0;
                std::string cleaned = sanitizeCompactMixedCase(reply);
                if (!cleaned.empty()) {
                    if (cleaned.size() > 175) cleaned.resize(175);
                    liveSpeech = std::move(cleaned);
                }
            }
        }

        drawMenuBackground();

        drawDarkRectangle({ 8.0f, 4.0f, 304.0f, 14.0f }, true);   // Header
        drawDarkRectangle({ 8.0f, 21.0f, 304.0f, 38.0f }, true);  // Player Quote Box
        drawDarkRectangle({ 8.0f, 62.0f, 304.0f, 112.0f }, true); // 3 Choices or Outcome
        drawDarkRectangle({ 8.0f, 177.0f, 304.0f, 18.0f }, true); // Footer

        float gameScale = getGameScale();
        float offX = getGameScreenOffsetX();
        float offY = getGameScreenOffsetY();
        float px = computeCompactPixelScaleForVga(gameScale, 0.42f);

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

        auto toRect = [&](float vx, float vy, float vw, float vh) -> SDL_FRect {
            return { offX + vx * gameScale, offY + vy * gameScale, vw * gameScale, vh * gameScale };
        };

        int32_t bankCash = readVmClubCash();
        std::string hdr = "DRESSING ROOM  -  " + offer.bannerTitle;
        drawCompactText(r, atlas, offX + 14.0f * gameScale, offY + 7.5f * gameScale,
            fitPx(hdr, px * 0.94f, 198.0f * gameScale), hdr, yellowCol, 255, true);
        std::string bankTxt = "BANK: " + formatPoundsShort(bankCash);
        drawCompactTextRightAligned(r, atlas, offX + 306.0f * gameScale, offY + 7.5f * gameScale,
            fitPx(bankTxt, px * 0.86f, 92.0f * gameScale), bankTxt, greenCol, 255, true);

        // Player Speech Card
        std::string spTag = toUpperAscii(offer.playerName) + " [" + offer.playerPos + "]  SPEAKS UP:";
        drawCompactText(r, atlas, offX + 14.0f * gameScale, offY + 24.5f * gameScale,
            fitPx(spTag, px * 0.86f, 290.0f * gameScale), spTag, cyanCol, 255, true);

        int qUnits = std::max(60, static_cast<int>((288.0f * gameScale) / (px * 0.88f)));
        auto qLines = wrapCompactTextUnits(liveSpeech, qUnits);
        float qy = offY + 34.0f * gameScale;
        for (size_t i = 0; i < qLines.size() && i < 3; ++i) {
            drawCompactText(r, atlas, offX + 14.0f * gameScale, qy, px * 0.88f, qLines[i], whiteCol, 255, false);
            qy += 9.0f * px * 0.88f;
        }

        SDL_FRect choiceRects[3];
        if (!resolved) {
            for (int i = 0; i < 3; ++i) {
                const auto& ch = offer.choices[i];
                bool canAfford = (ch.cashDelta >= 0 || bankCash >= -ch.cashDelta);
                choiceRects[i] = toRect(14.0f, 67.0f + static_cast<float>(i) * 34.5f, 292.0f, 31.5f);

                SDL_SetRenderDrawColor(r, canAfford ? 20 : 28, canAfford ? 32 : 20, canAfford ? 38 : 24, 215);
                SDL_RenderFillRectF(r, &choiceRects[i]);
                SDL_SetRenderDrawColor(r, canAfford ? 120 : 180, canAfford ? 210 : 90, canAfford ? 230 : 90, 200);
                SDL_RenderDrawRectF(r, &choiceRects[i]);

                std::string cTitle = "[" + std::to_string(i + 1) + "] " + ch.title;
                drawCompactText(r, atlas, choiceRects[i].x + 5.0f, choiceRects[i].y + 3.0f,
                    fitPx(cTitle, px * 0.88f, choiceRects[i].w - 10.0f), cTitle, canAfford ? yellowCol : redCol, 255, true);

                std::string rew = "GAIN: " + ch.rewardDesc;
                drawCompactText(r, atlas, choiceRects[i].x + 5.0f, choiceRects[i].y + 3.0f + 9.5f * px * 0.86f,
                    fitPx(rew, px * 0.80f, choiceRects[i].w - 10.0f), rew, greenCol, 250, false);

                std::string risk = (canAfford ? "COST/RISK: " : "CANNOT AFFORD: ") + ch.riskDesc;
                drawCompactText(r, atlas, choiceRects[i].x + 5.0f, choiceRects[i].y + 3.0f + 18.5f * px * 0.84f,
                    fitPx(risk, px * 0.78f, choiceRects[i].w - 10.0f), risk, canAfford ? orangeCol : redCol, 240, false);
            }

            bool showErr = !errorToast.empty() && (SDL_GetTicks() - errorTick < 2500);
            std::string foot = showErr
                ? errorToast
                : "[1 / 2 / 3] OR CLICK DECISION   |   [ESC] BRUSH OFF & CONTINUE";
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 183.0f * gameScale,
                fitPx(foot, px * 0.82f, 294.0f * gameScale), foot, showErr ? redCol : mutedCol, 255, true);
        } else {
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 76.0f * gameScale,
                px * 1.05f, "MANAGERIAL DECISION LOCKED IN", greenCol, 255, true);

            int oUnits = std::max(55, static_cast<int>((282.0f * gameScale) / (px * 0.92f)));
            auto oLines = wrapCompactTextUnits(outcomeMsg, oUnits);
            float oy = offY + 98.0f * gameScale;
            for (const auto& ol : oLines) {
                drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, oy, px * 0.92f, ol, whiteCol, 255, false);
                oy += 10.5f * px * 0.92f;
            }
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 183.0f * gameScale,
                px * 0.86f, "[ENTER / SPACE / CLICK] CONTINUE", yellowCol, 255, true);
        }

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_RenderSetScale(r, origSX, origSY);
        updateScreen(false);

        bool ready = (SDL_GetTicks() - openTick) >= 160;
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        bool curKeys[5] = {
            keys && (keys[SDL_SCANCODE_1] || keys[SDL_SCANCODE_KP_1]),
            keys && (keys[SDL_SCANCODE_2] || keys[SDL_SCANCODE_KP_2]),
            keys && (keys[SDL_SCANCODE_3] || keys[SDL_SCANCODE_KP_3]),
            keys && (keys[SDL_SCANCODE_ESCAPE] != 0),
            keys && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER] || keys[SDL_SCANCODE_SPACE]),
        };

        int mx = 0, my = 0;
        Uint32 mb = SDL_GetMouseState(&mx, &my);
        bool mouseDown = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        bool clicked = ready && mouseDown && !prevMouseDown;

        bool exitNow = false;
        if (ready) {
            if (!resolved) {
                int picked = -2;
                for (int i = 0; i < 3; ++i) {
                    bool hitClick = clicked && mx >= choiceRects[i].x && mx <= choiceRects[i].x + choiceRects[i].w &&
                                    my >= choiceRects[i].y && my <= choiceRects[i].y + choiceRects[i].h;
                    if ((curKeys[i] && !prevKeys[i]) || hitClick) {
                        picked = i;
                        break;
                    }
                }
                if (curKeys[3] && !prevKeys[3])
                    picked = -1; // ESC brush off

                if (picked >= -1) {
                    std::string res = resolveDilemmaChoice(offer, picked, squad, ctx.seed);
                    if (res.empty()) {
                        errorToast = "INSUFFICIENT CLUB FUNDS FOR THIS OPTION!";
                        errorTick = SDL_GetTicks();
                    } else if (picked == -1) {
                        exitNow = true; // ESC exits instantly!
                    } else {
                        outcomeMsg = std::move(res);
                        resolved = true;
                        openTick = SDL_GetTicks();
                    }
                }
            } else {
                if ((curKeys[4] && !prevKeys[4]) || (curKeys[3] && !prevKeys[3]) || clicked || isAnyPlayerFiring())
                    exitNow = true;
            }
        }

        for (int i = 0; i < 5; ++i) prevKeys[i] = curKeys[i];
        prevMouseDown = mouseDown;

        if (exitNow)
            break;
        SDL_Delay(16);
    }

    menuFadeOut();
    flushKeyBuffer();
    waitForKeyboardAndMouseIdle();
#endif
}

std::string formatDressingRoomContextForTelemetry()
{
    if (s_activeThisMatch.active && !s_activeThisMatch.label.empty())
        return "Dressing Room Storyline: " + s_activeThisMatch.label;
    if (!s_lastDecisionSummary.empty())
        return "Recent Dressing Room Call: " + s_lastDecisionSummary;
    return {};
}
