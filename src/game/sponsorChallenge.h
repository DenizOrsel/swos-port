#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct TeamGame;

constexpr int kSponsorOfferedCount = 5;

enum class SponsorChallengeType : uint8_t {
    kNone = 0,
    kCleanSheetWin = 1,     // Win and concede 0 goals
    kPlayerGoals = 2,       // Named starting pure Attacker scores >= thresholdValue (2..3)
    kFirstHalfGoals = 3,    // Club scores >= thresholdValue (1..3) in 1st half
    kPossessionWin = 4,     // Win and possession >= thresholdValue (60..70)
    kShotsOnTarget = 5,     // Club records >= thresholdValue (6..12) shots on target
    kWinByMargin = 6,       // Club wins by >= thresholdValue (2..3) goals
    kTeamGoals = 7,         // Club scores >= thresholdValue (2..4) total goals
    kLongRangeGoal = 8,     // Club scores >= thresholdValue (1..2) outside-box goal(s)
    kHeaderGoal = 9,        // Club scores >= thresholdValue (1..2) header goal(s)
    kNoCardsWin = 10,       // Win with 0 yellow and 0 red cards
    kEarlyGoal = 11,        // Score >= 1 goal within first 15 in-game minutes
    kBothHalvesGoal = 12,   // Score >= 1 goal in 1st half AND >= 1 in 2nd half
    kSecondHalfGoals = 13,  // Score >= thresholdValue (2..3) goals in 2nd half
    kLateGoal = 14,         // Score >= 1 goal in the 75th minute or later
    kLimitOppShots = 15,    // Win while holding opponent to <= thresholdValue (2) shots on target
    kClinicalFinishing = 16,// Score >= 2 goals with >= thresholdValue (50%) conversion on shots on target
    kPlayerAssist = 17,     // Named starting outfield player records >= 1 assist
    kNonAttackerGoal = 18,  // Any defender, midfielder, or GK (non-attacker) scores a goal
    kDifferentScorers = 19, // At least thresholdValue (3) different players score for your club
    kHatTrickHero = 20,     // Any single player on your team scores >= 3 goals
    kSetPieceGoal = 21,     // Score from a corner kick window or directly from a free kick
};

enum class MultiMatchContractType : uint8_t {
    kNone = 0,
    kWinStreak = 1,             // M1: Win 5 of your next 5 matches
    kUnbeatenRun = 2,           // M2: Go unbeaten (W/D) for your next 3 matches
    kGoalsInMatches = 3,        // M3: Score 10+ total goals across your next 3 matches
    kCleanSheetsInMatches = 4,  // M4: Keep 2 clean sheets across your next 3 matches
    kMinGoalsEachMatch = 5,     // M5: Score at least 2 goals in each of your next 5 matches
};

struct MultiMatchContract {
    bool isActive = false;
    MultiMatchContractType type = MultiMatchContractType::kNone;
    std::string title;
    std::string description;
    int32_t rewardAmount = 0;      // Paid on completion
    int32_t penaltyAmount = 0;     // Deducted on failure (50% breach fee)
    uint8_t matchesTotal = 0;      // e.g. 3 or 5
    uint8_t matchesPlayed = 0;     // 0..matchesTotal
    uint16_t targetValue = 0;      // e.g. 5 wins, 3 unbeaten, 10 goals, 2 CS, 5 games w/ 2+ goals
    uint16_t currentProgress = 0;  // live progress counter
    uint8_t perMatchMin = 0;       // e.g. 2 for M5
};

enum class ContractStepStatus : uint8_t {
    kNone = 0,
    kInProgress = 1,
    kCompleted = 2,
    kFailed = 3,
};

struct MultiMatchContractUpdate {
    ContractStepStatus status = ContractStepStatus::kNone;
    std::string title;
    std::string description;
    std::string progressText;
    int32_t deltaCash = 0;
};

struct SponsorChallenge {
    bool isActive = false;
    SponsorChallengeType type = SponsorChallengeType::kNone;
    std::string title;
    std::string description;
    int32_t rewardAmount = 0;

    // Deterministic verification parameters
    int16_t targetShirtNumber = -1;
    std::string targetPlayerName;
    int16_t thresholdValue = 0;
};

struct SponsorChallengeOutcome {
    std::string title;
    std::string description;
    int32_t rewardAmount = 0;
    bool completed = false;
    int32_t deltaCash = 0; // +rewardAmount if completed, -rewardAmount if failed
};

struct SponsorBatchEvaluation {
    std::vector<SponsorChallengeOutcome> outcomes;
    int completedCount = 0;
    int failedCount = 0;
    bool allInJackpotTriggered = false; // true when combo/clean-sweep bonus (>0%) triggered
    int jackpotPct = 0;                 // 0, 25 (3-4 combo), 50 (4/4 or 3/3 sweep), or 75 (5/5 All-In sweep)
    int32_t baseNetDelta = 0;
    int32_t jackpotBonus = 0;
    int32_t totalNetDelta = 0;          // baseNetDelta + jackpotBonus
};

struct CareerChallengeState {
    SponsorChallenge offered[kSponsorOfferedCount];
    bool selectedOffer[kSponsorOfferedCount] = { false, false, false, false, false };
    bool slot5IsContractOffer = false;
    MultiMatchContract offeredContract;
    bool offersReady = false;

    std::vector<SponsorChallenge> activeChallenges;
    int offeredSingleMatchCount = kSponsorOfferedCount; // 4 when slot5 is contract, 5 otherwise
    MultiMatchContract activeContract;

    // Last match outcome for Post-Match Report rendering
    bool lastMatchHadChallenge = false;
    SponsorBatchEvaluation lastMatchEval;
    MultiMatchContractUpdate lastContractUpdate;
    int32_t lastMatchNewBalance = 0;

    // Persistent career totals
    uint16_t challengesCompleted = 0;
    uint16_t challengesFailed = 0;
    int64_t totalBonusEarned = 0; // cumulative net sponsor profit/loss
};

struct SponsorMatchResult {
    int clubGoals = 0;
    int opponentGoals = 0;
    int clubHtGoals = 0;
    int opponentHtGoals = 0;
    int clubPossessionPct = 50;
    int clubShotsOnTarget = 0;
    int opponentShotsOnTarget = 0;
    int clubYellowCards = 0;
    int clubRedCards = 0;
    int clubLongRangeGoals = 0;
    int clubHeaderGoals = 0;
    int clubEarlyGoals = 0;    // minute <= 15
    int clubLateGoals = 0;     // minute >= 75
    int clubSetPieceGoals = 0; // corner window or direct free kick
    int clubNonAttackerGoals = 0;
    // (shirtNumber, shortName) for each goal scored by the human club (excluding own goals)
    std::vector<std::pair<int, std::string>> scorerEntries;
    // (shirtNumber, shortName) for each assist recorded by the human club
    std::vector<std::pair<int, std::string>> assistEntries;
};

struct SponsorStarterInfo {
    int shirtNumber = 0;
    std::string shortName;
    bool isAttacker = false;     // Pure ATT (or wing fallback if isPureAttacker not set)
    bool isMidfielder = false;
    int seasonGoals = 0;
    bool isPureAttacker = false; // Strict PlayerPosition::kAttacker
};

// Pure deterministic logic (also directly unit tested)
int32_t clampSponsorRewardForDivision(int32_t rawReward, int divisionTier);
int32_t clampMultiMatchRewardForDivision(int32_t rawReward, int divisionTier);
int16_t clampSponsorThreshold(SponsorChallengeType type, int16_t rawThreshold);
bool evaluateSponsorChallenge(const SponsorChallenge& challenge, const SponsorMatchResult& result);
SponsorBatchEvaluation evaluateSponsorChallengeBatch(
    const std::vector<SponsorChallenge>& activeChallenges,
    int totalOfferedCount,
    const SponsorMatchResult& result);
MultiMatchContract generateOfflineMultiMatchContract(int divisionTier, uint32_t seed);
MultiMatchContractUpdate advanceMultiMatchContract(MultiMatchContract& contract, int clubGoals, int opponentGoals);
std::vector<SponsorChallenge> generateOfflineSponsorChallenges(
    const std::string& clubName, const std::string& opponentName,
    int divisionTier, const std::vector<SponsorStarterInfo>& starters, uint32_t seed,
    int desiredCount = kSponsorOfferedCount);
std::vector<SponsorChallenge> parseLlmSponsorChallenges(
    const std::string& rawReply, const std::string& clubName, const std::string& opponentName,
    int divisionTier, const std::vector<SponsorStarterInfo>& starters, uint32_t seed,
    int desiredCount = kSponsorOfferedCount);
std::string formatSponsorPounds(int64_t amount);

// Career lifecycle & UI hooks
void resetSponsorChallengeState();
void beginSponsorChallengeGeneration(const TeamGame *topTeam, const TeamGame *bottomTeam);
void showPreMatchSponsorChallengeMenuIfReady();
void onMatchFinishedSponsorChallenge();
void onViewResultSponsorContractUpdate(int clubGoals, int opponentGoals);
void showPostMatchSponsorPayoutIfNeeded();

// Live telemetry, supporter stream & post-match newspaper integration
bool hasActiveSponsorChallenge();
std::string formatActiveSponsorChallengeForTelemetry();
std::vector<std::pair<std::string, int>> formatActiveSponsorChallengeHudLines();
std::string formatLastSponsorChallengeOutcomeForNewspaper();

// Persistence (Single-player sidecar .sch + Hotseat career sync)
bool saveSponsorChallengeSidecar(const std::string& path);
bool loadSponsorChallengeSidecar(const std::string& path);
void getSponsorCareerTotals(uint16_t& outCompleted, uint16_t& outFailed, int64_t& outTotalBonus);
void setSponsorCareerTotals(uint16_t completed, uint16_t failed, int64_t totalBonus);
MultiMatchContract getActiveSponsorContract();
void setActiveSponsorContract(const MultiMatchContract& contract);
