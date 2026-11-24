#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TeamGame;

constexpr int16_t kMaxCharismaPoints = 1000;
constexpr int16_t kCharismaPointsPerLevel = 100;
constexpr int kMaxCharismaLevel = 10;

enum class PressPhase : uint8_t {
    kPreMatch = 0,
    kPostMatch = 1,
};

enum class ManagerialStance : uint8_t {
    kNone = 0,
    kHumble = 1,
    kConfident = 2,
    kTactical = 3,
    kHostile = 4,
};

enum class CharismaTier : uint8_t {
    kToxic = 0,       // Level 1 (0..99 CP)
    kUninspiring = 1, // Levels 2-3 (100..299 CP)
    kRespectable = 2, // Levels 4-5 (300..499 CP)
    kCharismatic = 3, // Levels 6-8 (500..799 CP)
    kIconic = 4,      // Levels 9-10 (800..1000 CP)
};

struct PressOption {
    std::string text;
    ManagerialStance stance = ManagerialStance::kHumble;
    int8_t charismaDelta = 0; // clamped -12..+10
    std::string mediaReaction;
};

struct PressConferencePrompt {
    PressPhase phase = PressPhase::kPreMatch;
    std::string journalistName;
    std::string question;
    PressOption options[4]; // kept for offline fallback/compatibility
    bool isReady = false;
};

struct PressFreeTextEvaluation {
    ManagerialStance detectedStance = ManagerialStance::kHumble;
    int8_t charismaDelta = 0; // clamped -12..+10
    std::string verdictTag;   // e.g. "CLASSY", "DELUSIONAL", "WITTY", "GENERIC", "FIERY"
    std::string mediaReaction;
};

struct CareerPressState {
    int16_t charismaPoints = 0; // 0..1000, starts at 0 (Level 1: 0/100)
    ManagerialStance lastPreMatchStance = ManagerialStance::kNone;
    std::string lastPreMatchQuote;
    uint16_t interviewsCompleted = 0;
    int64_t lastSeasonCharismaBonus = 0;
    std::vector<std::string> recentForm; // last 5 results e.g. "W 2-0"
};

struct PressMatchContext {
    PressPhase phase = PressPhase::kPreMatch;
    std::string managerName;
    std::string clubName;
    std::string opponentName;
    std::string competition;
    bool isDerby = false;
    int leaguePosition = 0;
    int opponentPosition = 0;
    std::vector<std::string> recentForm;
    ManagerialStance preMatchStance = ManagerialStance::kNone;
    std::string preMatchQuote;
    int clubGoals = 0;
    int opponentGoals = 0;
    int clubPossessionPct = 50;
    int clubShotsOnTarget = 0;
    int oppShotsOnTarget = 0;
    int clubLongRangeGoals = 0;
    int clubRedCards = 0;
    int opponentRedCards = 0;
    std::string matchScorersSummary;
    std::string matchAssistsSummary;
    std::string seasonTopScorersSummary;
    std::string sponsorSummary;
    int16_t currentCharisma = 0;
};

// --- 10-Level Charisma System & Economy Helpers (unit tested) ---
int getCharismaLevel(int16_t charismaPoints);            // 1..10 (100 CP per level; 0 -> L1, 100 -> L2, ..., 900..1000 -> L10)
int getCharismaLevelProgress(int16_t charismaPoints);    // 0..99 (100 at max 1000)
const char *getCharismaLevelTitle(int level);            // "UNKNOWN ROOKIE" .. "LIVING LEGEND"
const char *getCharismaLevelTitleForPoints(int16_t charismaPoints);
CharismaTier getCharismaTier(int16_t charismaPoints);
const char *getCharismaTierLabel(CharismaTier tier);
const char *getCharismaReputationTitle(CharismaTier tier);
const char *getManagerialStanceName(ManagerialStance stance);
double getCharismaTvMultiplier(int16_t charismaPoints);
double getPressAttendanceMultiplier(int16_t charismaPoints);
int getCharismaSponsorBonusPct(int16_t charismaPoints);
int getCurrentCareerCharismaSponsorBonusPct();
int64_t computeSeasonCharismaBonus(int64_t tvRightsBase, int16_t charismaPoints);
int16_t applyCharismaDelta(int16_t currentCp, int8_t rawDelta);
bool detectClassicDerby(const std::string& clubA, const std::string& clubB, int posA, int posB);

// --- Job Offer Reputation Gating (Level 1-10 vs Club Prestige) ---
int getRequiredCharismaLevelForJobOffer(
    int clubDivisionTier, int avgSquadPrice, const std::string& clubName, bool isNationalTeam);
bool isJobOfferAllowedForCharisma(
    int16_t coachCharismaPoints, int clubDivisionTier, int avgSquadPrice,
    const std::string& clubName, bool isNationalTeam, int currentClubDivisionTier = 3);
bool shouldAllowJobOfferTeamIdByCharisma(uint16_t teamId);

// --- Press Question Generation & Free-Text Answer Evaluation ---
PressConferencePrompt generateOfflinePressPrompt(const PressMatchContext& ctx, uint32_t seed);
PressConferencePrompt parseLlmPressPrompt(const std::string& rawReply, const PressMatchContext& ctx, uint32_t seed);
PressFreeTextEvaluation evaluateOfflineFreeTextReply(const std::string& replyText, const PressMatchContext& ctx);
PressFreeTextEvaluation parseLlmFreeTextEvaluation(const std::string& rawLlmReply, const std::string& userAnswer, const PressMatchContext& ctx);

// --- Career Lifecycle & Native UI Hooks ---
void resetPressCharismaState();
void beginPreMatchPressConference(const TeamGame *topTeam, const TeamGame *bottomTeam);
void showPreMatchPressConferenceIfReady();
void beginPostMatchPressConference(int topGoals, int botGoals);
void showPostMatchPressConferenceIfReady();
void applyPendingMatchdayGateReceiptsCharismaBonus();
void onViewResultMatchPressConference();
void onEndOfSeasonPressCharismaBonus();

// --- Live Telemetry & Newspaper Synergy ---
std::string formatPressContextForTelemetry();
std::string formatPressContextForNewspaper();

// --- Persistence (single-player .prs sidecar + Hotseat PRSS trailer sync) ---
bool savePressCharismaSidecar(const std::string& path);
bool loadPressCharismaSidecar(const std::string& path);
void getPressCareerTotals(int16_t& outCharisma, uint16_t& outInterviews, int64_t& outLastSeasonBonus,
                          uint8_t& outLastStance, std::string& outLastQuote);
void setPressCareerTotals(int16_t charisma, uint16_t interviews, int64_t lastSeasonBonus,
                          uint8_t lastStance, const std::string& lastQuote);
