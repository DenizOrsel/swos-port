#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TeamGame;

enum class DressingRoomFlashpointType : uint8_t {
    kNone = 0,
    kRedCardDiscipline = 1,
    kSlumpCrisis = 2,
    kWinStreakBonus = 3,
    kPlayerDoorKnock = 4,
    kSquadRecovery = 5,
};

struct DressingRoomFlashpointContext {
    int clubGoals = 0;
    int opponentGoals = 0;
    int clubRedCards = 0;
    int clubYellowCards = 0;
    std::string recentForm; // oldest -> newest (e.g. "WWLLL" or "WWWWW")
    int injuredCount = 0;
    int maxInjuryMatches = 0;
    int matchesSinceLastDilemma = 0;
    int divisionTier = 0;
    uint32_t seed = 0;
};

struct DressingRoomChoiceSpec {
    std::string title;
    std::string rewardDesc;
    std::string riskDesc;
    int32_t cashDelta = 0; // positive = income/fine, negative = cost
};

struct DressingRoomDilemmaOffer {
    DressingRoomFlashpointType type = DressingRoomFlashpointType::kNone;
    std::string bannerTitle;
    std::string playerName;
    std::string playerPos;
    int playerCareerSlot = -1;
    int playerShirtNumber = 0;
    std::string offlineSpeech;
    DressingRoomChoiceSpec choices[3];
};

struct PendingDressingRoomEffect {
    bool active = false;
    std::string label;
    // Squad-wide next-match deltas (clamped to [0..7])
    int8_t squadSpeedDelta = 0;
    int8_t squadPassingDelta = 0;
    int8_t squadControlDelta = 0;
    int8_t squadTacklingDelta = 0;
    int8_t squadFinishingDelta = 0;
    int8_t veteranPenaltyDelta = 0; // applied to above-average price starters

    // Single-player next-match deltas
    int targetShirtNumber = 0;
    std::string targetPlayerName;
    int targetCareerSlot = -1;
    int8_t playerFinishingDelta = 0;
    int8_t playerTacklingDelta = 0;
    int8_t playerSpeedDelta = 0;
    int8_t playerPassingDelta = 0;
    int8_t playerControlDelta = 0;

    // Post-match promise hooks for the NEXT played match
    bool hairdryerWinRequired = false;     // -8 CP if next match is NOT won
    bool doubleBonusWinPromise = false;    // If won: pay bonusCash, +15 CP & +1 captain price; else -5 CP
    int32_t promisedBonusCost = 0;
    bool proveItChallenge = false;         // If targetPlayer scores/assists: +1 permanent price tier
};

// --- Pure Deterministic Logic (unit tested) ---
DressingRoomFlashpointType detectDressingRoomFlashpoint(const DressingRoomFlashpointContext& ctx);
int32_t computeDressingRoomCashScale(int divisionTier, int baseDiv4Amount);
DressingRoomDilemmaOffer buildDressingRoomDilemmaOffer(
    DressingRoomFlashpointType type,
    const std::string& clubName,
    const std::string& playerName,
    const std::string& playerPos,
    int playerCareerSlot,
    int playerShirtNumber,
    int divisionTier,
    int injuredCount);
void applyPendingDressingRoomEffectToTeam(TeamGame& myTeam, const PendingDressingRoomEffect& eff);

// --- Career & Match Lifecycle Hooks ---
void resetDressingRoomDilemmaState();
void applyPendingDressingRoomEffectsForMatch(TeamGame *topTeam, TeamGame *bottomTeam);
void onCareerMatchFinishedDressingRoomDilemma(int topGoals, int botGoals);

// --- Live Telemetry, Stream & Newspaper Context ---
std::string formatDressingRoomContextForTelemetry();
