#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct TeamGame;

enum class SponsorChallengeType : uint8_t {
    kNone = 0,
    kCleanSheetWin = 1,    // Win and concede <= thresholdValue (0)
    kPlayerGoals = 2,      // Target starting XI player scores >= thresholdValue (1..2)
    kFirstHalfGoals = 3,   // Club scores >= thresholdValue (1..3) in 1st half
    kPossessionWin = 4,    // Win and possession >= thresholdValue (55..65)
    kShotsOnTarget = 5,    // Club records >= thresholdValue (6..12) shots on target
    kWinByMargin = 6,      // Club wins by >= thresholdValue (2..3) goals
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

struct CareerChallengeState {
    SponsorChallenge offered[3];
    bool offersReady = false;
    SponsorChallenge activeChallenge;

    // Last match outcome for Post-Match Report rendering
    bool lastMatchHadChallenge = false;
    bool lastMatchCompleted = false;
    std::string lastMatchTitle;
    std::string lastMatchDescription;
    int32_t lastMatchPayout = 0;
    int32_t lastMatchNewBalance = 0;

    // Persistent career totals
    uint16_t challengesCompleted = 0;
    uint16_t challengesFailed = 0;
    int64_t totalBonusEarned = 0;
};

struct SponsorMatchResult {
    int clubGoals = 0;
    int opponentGoals = 0;
    int clubHtGoals = 0;
    int opponentHtGoals = 0;
    int clubPossessionPct = 50;
    int clubShotsOnTarget = 0;
    // (shirtNumber, shortName) for each goal scored by the human club (excluding own goals)
    std::vector<std::pair<int, std::string>> scorerEntries;
};

struct SponsorStarterInfo {
    int shirtNumber = 0;
    std::string shortName;
    bool isAttacker = false;
    bool isMidfielder = false;
    int seasonGoals = 0;
};

// Pure deterministic logic (also directly unit tested)
int32_t clampSponsorRewardForDivision(int32_t rawReward, int divisionTier);
int16_t clampSponsorThreshold(SponsorChallengeType type, int16_t rawThreshold);
bool evaluateSponsorChallenge(const SponsorChallenge& challenge, const SponsorMatchResult& result);
std::vector<SponsorChallenge> generateOfflineSponsorChallenges(
    const std::string& clubName, const std::string& opponentName,
    int divisionTier, const std::vector<SponsorStarterInfo>& starters, uint32_t seed);
std::vector<SponsorChallenge> parseLlmSponsorChallenges(
    const std::string& rawReply, const std::string& clubName, const std::string& opponentName,
    int divisionTier, const std::vector<SponsorStarterInfo>& starters, uint32_t seed);
std::string formatSponsorPounds(int64_t amount);

// Career lifecycle & UI hooks
void resetSponsorChallengeState();
void beginSponsorChallengeGeneration(const TeamGame *topTeam, const TeamGame *bottomTeam);
void showPreMatchSponsorChallengeMenuIfReady();
void onMatchFinishedSponsorChallenge();
void showPostMatchSponsorPayoutIfNeeded();

// Live telemetry, supporter stream & post-match newspaper integration
bool hasActiveSponsorChallenge();
std::string formatActiveSponsorChallengeForTelemetry();
std::string formatActiveSponsorChallengeHudLine(int& outStatusColorMode);
std::string formatLastSponsorChallengeOutcomeForNewspaper();

// Persistence (single-player sidecar .sch + Hotseat sync)
bool saveSponsorChallengeSidecar(const std::string& path);
bool loadSponsorChallengeSidecar(const std::string& path);
void getSponsorCareerTotals(uint16_t& outCompleted, uint16_t& outFailed, int64_t& outTotalBonus);
void setSponsorCareerTotals(uint16_t completed, uint16_t failed, int64_t totalBonus);
