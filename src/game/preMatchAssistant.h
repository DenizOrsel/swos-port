#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TeamGame;
struct PlayerInfo;

enum class TacticalFocusType : uint8_t {
    kBalanced = 0,
    kAllOutAttack = 1,
    kIronDefence = 2,
    kMidfieldControl = 3,
    kSetPieceLongShot = 4,
    kCount = 5,
};

struct PreMatchScoutIntel {
    std::string myClub;
    std::string oppClub;
    std::string dangerManName;
    std::string dangerManPos;
    std::string dangerManStats;
    int dangerManIndex = -1; // 0..10 in opponent TeamGame

    std::string weakLinkLabel;
    std::string weakLinkAdvice;
};

struct PreMatchInjuredCandidate {
    bool valid = false;
    int careerSlot = -1;
    std::string playerName;
    uint8_t injuryMatches = 0;
    uint8_t priceTier = 0;
    int32_t treatmentCost = 0;
    bool isPainkiller = false; // true for 1-2 matches (clears now w/ aggravation risk), false for 3+ (guaranteed -2m rehab)
    int aggravationRiskPct = 0;
    bool applied = false;
};

struct PreMatchSpotlightCandidate {
    int careerSlot = -1;
    int shirtNumber = 0;
    std::string shortName;
    std::string pos;
    uint8_t priceTier = 0;
    std::string marketValueStr;
};

struct PreMatchPrepPlan {
    TacticalFocusType focus = TacticalFocusType::kBalanced;
    bool manMarkDangerMan = false;
    int spotlightCandidateIndex = -1; // -1 = OFF, 0..N-1 into candidates
    bool painkillerUsed = false;
    int painkillerCareerSlot = -1;
    std::string painkillerPlayerName;
    int painkillerAggravationPct = 0;
    int trainingLevel = 1;
    int medicalLevel = 1;
};

struct SpotlightMatchEvaluation {
    bool active = false;
    bool succeeded = false;
    bool masterclassSkillBoost = false;
    bool flopped = false;
    std::string playerName;
    int goals = 0;
    int assists = 0;
    double rating = 6.0;
    std::string summaryLine;
};

// --- Pure Deterministic Helpers (directly unit tested) ---
const char *getTacticalFocusName(TacticalFocusType focus);
const char *getTacticalFocusBoostDesc(TacticalFocusType focus);
const char *getTacticalFocusTradeoffDesc(TacticalFocusType focus, int trainingLevel);
int32_t computeEmergencyPhysioCost(int divisionTier, int medicalLevel, bool isPainkiller);
int computePainkillerAggravationPct(int medicalLevel);

PreMatchScoutIntel analyzeOpponentScouting(const TeamGame& myTeam, const TeamGame& oppTeam);
void applyTacticalFocusToTeams(
    TeamGame& myTeam,
    TeamGame& oppTeam,
    TacticalFocusType focus,
    int trainingLevel,
    bool manMarkDangerMan,
    int dangerManIndex);

SpotlightMatchEvaluation evaluateSpotlightPerformance(
    const std::string& playerName,
    int goals,
    int assists,
    int redCards,
    double rating);

// --- Career & Match Lifecycle Hooks ---
void resetPreMatchAssistantState();
void beginPreMatchAssistantBriefing(const TeamGame *topTeam, const TeamGame *bottomTeam);
void showPreMatchAssistantMenuIfReady();
void onMatchFinishedPreMatchAssistant();
void showPostMatchAssistantPayoffIfNeeded();

// --- Telemetry & Live Stream Context ---
std::string formatPreMatchAssistantContextForTelemetry();
