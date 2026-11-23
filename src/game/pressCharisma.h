#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TeamGame;

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
    kToxic = 0,       // < 20 CP
    kUninspiring = 1, // 20..44 CP
    kRespectable = 2, // 45..69 CP
    kCharismatic = 3, // 70..89 CP
    kIconic = 4,      // 90..100 CP
};

struct PressOption {
    std::string text;
    ManagerialStance stance = ManagerialStance::kHumble;
    int8_t charismaDelta = 0; // clamped -8..+8
    std::string mediaReaction;
};

struct PressConferencePrompt {
    PressPhase phase = PressPhase::kPreMatch;
    std::string journalistName;
    std::string question;
    PressOption options[4];
    bool isReady = false;
};

struct CareerPressState {
    int16_t charismaPoints = 50; // 0..100, starts at 50
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
    int clubRedCards = 0;
    int opponentRedCards = 0;
    int16_t currentCharisma = 50;
};

// Pure deterministic logic (also directly unit tested)
CharismaTier getCharismaTier(int16_t charismaPoints);
const char *getCharismaTierLabel(CharismaTier tier);
const char *getCharismaReputationTitle(CharismaTier tier);
const char *getManagerialStanceName(ManagerialStance stance);
double getCharismaTvMultiplier(int16_t charismaPoints);
double getPressAttendanceMultiplier(int16_t charismaPoints);
int64_t computeSeasonCharismaBonus(int64_t tvRightsBase, int16_t charismaPoints);
int16_t applyCharismaDelta(int16_t currentCp, int8_t rawDelta);
bool detectClassicDerby(const std::string& clubA, const std::string& clubB, int posA, int posB);
PressConferencePrompt generateOfflinePressPrompt(const PressMatchContext& ctx, uint32_t seed);
PressConferencePrompt parseLlmPressPrompt(const std::string& rawReply, const PressMatchContext& ctx, uint32_t seed);

// Career lifecycle & native UI hooks
void resetPressCharismaState();
void beginPreMatchPressConference(const TeamGame *topTeam, const TeamGame *bottomTeam);
void showPreMatchPressConferenceIfReady();
void beginPostMatchPressConference(int topGoals, int botGoals);
void showPostMatchPressConferenceIfReady();
void applyPendingMatchdayGateReceiptsCharismaBonus();
void onViewResultMatchPressConference();
void onEndOfSeasonPressCharismaBonus();

// Live telemetry & newspaper synergy
std::string formatPressContextForTelemetry();
std::string formatPressContextForNewspaper();

// Persistence (single-player .prs sidecar + Hotseat PRSS trailer sync)
bool savePressCharismaSidecar(const std::string& path);
bool loadPressCharismaSidecar(const std::string& path);
void getPressCareerTotals(int16_t& outCharisma, uint16_t& outInterviews, int64_t& outLastSeasonBonus,
                          uint8_t& outLastStance, std::string& outLastQuote);
void setPressCareerTotals(int16_t charisma, uint16_t interviews, int64_t lastSeasonBonus,
                          uint8_t lastStance, const std::string& lastQuote);
