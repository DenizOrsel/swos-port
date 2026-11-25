#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TeamGame;

enum class TicketPolicy : uint8_t {
    kCheap = 0,
    kStandard = 1,
    kExpensive = 2,
};

enum class StadiumModuleType : uint8_t {
    kCapacity = 0,
    kPitch = 1,
    kTraining = 2,
    kMedical = 3,
    kCommercial = 4,
    kCount = 5,
};

enum class StadiumUpgradeStatus : uint8_t {
    kOk = 0,
    kInsufficientFunds = 1,
    kMaxLevel = 2,
    kConstructionInProgress = 3,
};

struct ConstructionProject {
    bool isActive = false;
    StadiumModuleType module = StadiumModuleType::kCapacity;
    uint8_t targetLevel = 0;
    uint8_t matchesRemaining = 0;
    int32_t costPaid = 0;
};

struct StadiumData {
    bool initialized = false;
    int16_t teamId = -1;
    char stadiumName[32] = {};
    uint8_t capacityLevel = 1;       // 1..10
    uint32_t maxCapacity = 5000;     // 5,000..85,000
    uint8_t pitchLevel = 1;          // 1..10
    uint8_t trainingLevel = 1;       // 1..5
    uint8_t medicalLevel = 1;        // 1..10
    uint8_t commercialLevel = 1;     // 1..10
    TicketPolicy ticketPolicy = TicketPolicy::kStandard;
    int32_t ticketPrice = 10;
    ConstructionProject activeProject;

    // Last-5 match results ('W', 'D', 'L', oldest -> newest) for dynamic form-based ticket pricing
    char recentForm[6] = {};

    // Transfer market home-match buzz/backlash modifier (auto-decrements after each home match)
    int8_t transferTicketDelta = 0;
    int8_t transferDemandPctDelta = 0;
    uint8_t transferBuzzHomeMatches = 0;
    char transferBuzzLabel[32] = {};

    // Seasonal tracking
    uint32_t seasonTotalAttendance = 0;
    int64_t seasonGateRevenue = 0;
    int64_t seasonUpkeepPaid = 0;
    uint16_t seasonHomeMatches = 0;
};

struct StadiumUpgradeQuote {
    StadiumUpgradeStatus status = StadiumUpgradeStatus::kOk;
    StadiumModuleType module = StadiumModuleType::kCapacity;
    uint8_t currentLevel = 1;
    uint8_t targetLevel = 2;
    int32_t cost = 0;
    uint8_t buildMatches = 0;
    int32_t newMatchdayUpkeep = 0;
    std::string previewText;
};

struct StadiumMatchdayReport {
    bool valid = false;
    bool isHomeMatch = false;
    bool isDerby = false;
    bool isSellout = false;
    bool constructionCompleted = false;
    std::string stadiumName;
    std::string opponentName;
    uint32_t attendance = 0;
    uint32_t maxCapacity = 0;
    int fillPct = 0;
    int32_t baseTicketPrice = 0;
    int32_t formTicketDelta = 0;
    int32_t transferTicketDelta = 0;
    std::string transferBuzzLabel;
    int32_t ticketPrice = 0;
    int64_t ticketRevenue = 0;
    int commercialBonusPct = 0;
    int64_t vipBoxRevenue = 0;
    int64_t merchRevenue = 0;
    int64_t commercialRevenue = 0; // vipBoxRevenue + merchRevenue
    int64_t upkeepCost = 0;
    int64_t netProfit = 0;
    std::string completedModuleName;
    uint8_t completedNewLevel = 0;
    std::string notableReason;
    std::vector<std::string> medicalNotes;
};

// --- Pure Deterministic Economy & Formula Helpers (unit tested) ---
uint32_t getStadiumCapacityForLevel(uint8_t level);
int32_t getStadiumTicketPriceForDivision(int divisionTier, TicketPolicy policy);
int getStadiumFormScore(const StadiumData& stadium);
int32_t getStadiumFormTicketAdjustment(const StadiumData& stadium);
int32_t getEffectiveStadiumTicketPrice(const StadiumData& stadium, int divisionTier);
double getStadiumFormDemandMultiplier(const StadiumData& stadium);
void stadiumRecordMatchForm(StadiumData& stadium, int clubGoals, int opponentGoals);

int32_t getStadiumVipBoxIncome(uint8_t commercialLevel, int divisionTier);
int getStadiumCommercialSponsorBonusPct(uint8_t commercialLevel);
int getCurrentCareerCommercialSponsorBonusPct();

int getMedicalInjuryPreventionPct(uint8_t medicalLevel);
int getMedicalRecoveryBoostPct(uint8_t medicalLevel);
uint8_t computeMedicalReducedInjuryDays(uint8_t rawInjuryDays, uint8_t medicalLevel);
double getTrainingGrowthBonusFraction(uint8_t trainingLevel);
int applyStadiumPitchQualityOverrideForLevel(int rawPitchType, uint8_t pitchLevel);

int32_t getStadiumMatchdayUpkeep(const StadiumData& stadium);
const char *getStadiumModuleName(StadiumModuleType module);
const char *getTicketPolicyName(TicketPolicy policy);
const char *getPitchQualityLabel(uint8_t pitchLevel);

StadiumData stadiumInitForClub(const std::string& clubName, int divisionTier, int avgSquadPrice, int16_t teamId = -1);
StadiumUpgradeQuote stadiumGetUpgradeQuote(const StadiumData& stadium, StadiumModuleType module, int64_t clubCash);
bool stadiumStartUpgrade(StadiumData& stadium, int64_t& clubCash, StadiumModuleType module);
bool stadiumAdvanceConstruction(StadiumData& stadium, StadiumModuleType& outCompletedModule, uint8_t& outNewLevel);

StadiumMatchdayReport stadiumCalculateMatchday(
    const StadiumData& stadium,
    int divisionTier,
    int clubRank,
    int oppRank,
    bool isDerby,
    int16_t charismaPoints,
    const std::string& opponentName = {});

// --- Career Lifecycle, Match Engine & Persistence Hooks ---
void resetStadiumFacilitiesState();
const StadiumData& getCurrentCareerStadium();
void ensureCurrentCareerStadiumInitialized();
void setCurrentCareerStadium(const StadiumData& data);
void applyStadiumTransferBuzz(int8_t ticketDelta, int8_t demandPctDelta, uint8_t homeMatches, const std::string& label);

bool isStadiumManagingHomeGateReceipts();
void applyStadiumPitchQualityOverride(int& pitchType);
void onPreMatchStadiumSnapshot(const TeamGame *topTeam, const TeamGame *bottomTeam);
void onPostMatchStadiumUpdate(bool isPlayedMatch);
void showPostMatchStadiumReportIfNotable();
void onEndOfSeasonStadiumProgression();

// --- Single-player sidecar (.std) persistence ---
bool saveStadiumSidecar(const std::string& path);
bool loadStadiumSidecar(const std::string& path);

// --- Career Hub UI button / hotkey & Management Menu ---
void drawCareerHubStadiumButton();
bool handleCareerHubStadiumInput();
void showStadiumFacilitiesMenu();

// --- LLM Telemetry Context (Supporter Stream, Newspaper, Press) ---
std::string describeRecentFormStreak(const char *recentForm);
std::string formatStadiumContextForTelemetry();
std::string getLiveStadiumAndFormStreamNote();
std::string formatLastStadiumOutcomeForNewspaper();
