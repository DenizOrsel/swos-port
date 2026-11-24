#pragma once

#include <cstdint>
#include <string>
#include <vector>

constexpr uint16_t kLegacyV2AchievementCount = 50;
constexpr uint16_t kAchievementCount = 100;

enum class AchievementCategory : uint8_t {
    kBasics = 0,
    kPitchCraft = 1,
    kComebacks = 2,
    kCupGlory = 3,
    kCareer = 4,
    kManagement = 5,
    kCount = 6,
};

enum class AchievementId : uint16_t {
    // --- Original 50 Achievements (0..49 — indices frozen for save compatibility) ---
    // Category A: First Steps & Match Basics (01..08)
    kFirstKickoff = 0,
    kFriendlyWin = 1,
    kFirstGoal = 2,
    kCleanSheet = 3,
    kPenaltyShootoutWin = 4,
    kExtraTimeWinner = 5,
    kVeteran50Matches = 6,
    kCenturion100Goals = 7,

    // Category B: Pitch Craft & Goal Types (09..21)
    kPenaltyGoal = 8,
    kHeaderGoal = 9,
    kOutsideBoxGoal = 10,
    kFreeKickGoal = 11,
    kVolleyOrOverhead = 12,
    kCornerDirectOrQuick = 13,
    kHatTrick = 14,
    kDoubleHatTrick = 15,
    kGoalkeeperGoal = 16,
    kEarlyBlitz = 17,
    kFergieTime = 18,
    kNoCardsWin = 19,
    kDownToTenWin = 20,

    // Category C: Comebacks & Dominance (22..28)
    kHalftimeComeback = 21,
    kTwoGoalComeback = 22,
    kThreeGoalComeback = 23,
    kFiveGoalRout = 24,
    kDoubleDigits = 25,
    kPossessionMaster = 26,
    kClinicalFinishing = 27,

    // Category D: Cup Runs & Giant Killing (29..34)
    kGiantKiller1Tier = 28,
    kGiantKiller2Tiers = 29,
    kGiantKiller3Tiers = 30,
    kDomesticCupWin = 31,
    kEuropeanCupWin = 32,
    kWorldCupWin = 33,

    // Category E: Career Mode Progression & Glory (35..42)
    kCareerFirstWin = 34,
    kPromotion = 35,
    kBackToBackPromotion = 36,
    kLeagueChampion = 37,
    kUnbeatenSeason = 38,
    kContinentalTreble = 39,
    kInternationalCallup = 40,
    kCareerTenSeasons = 41,

    // Category F: Transfers, Stadium, Press & Challenges (43..50)
    kFirstSigning = 42,
    kMillionPoundSigning = 43,
    kProfitableFlip = 44,
    kStadiumFirstUpgrade = 45,
    kFortress85k = 46,
    kMaxFacilities = 47,
    kIconicCharisma = 48,
    kSponsorStreak = 49,

    // --- V2 Expansion: 50 New Achievements (50..99) ---
    // Category A+: Match, Win, Goal & Clean Sheet Ladders (51..60)
    kTenMatches = 50,
    kCenturyMatches = 51,
    kLegend250Matches = 52,
    kTenWins = 53,
    kFiftyWins = 54,
    kHundredWins = 55,
    kGoals250 = 56,
    kGoals500 = 57,
    kGoals1000 = 58,
    kCleanSheets15 = 59,

    // Category B+: Pitch Craft & Telemetry Specials (61..71)
    kTwoLongRangeInMatch = 60,
    kTenLongRangeCareer = 61,
    kTenHeadersCareer = 62,
    kFiveFreeKicksCareer = 63,
    kDefenderGoal = 64,
    kFourGoalHaul = 65,
    kFiveHatTricks = 66,
    kScoreBothHalves = 67,
    kThreeDifferentScorers = 68,
    kFiveDifferentScorers = 69,
    kColdRainyNight = 70,

    // Category C+: Streaks, Shutdowns & Drama (72..80)
    kWinStreak3 = 71,
    kWinStreak5 = 72,
    kWinStreak10 = 73,
    kUnbeatenRun10 = 74,
    kSmashAndGrab = 75,
    kShotBarrage = 76,
    kBrickWallGk = 77,
    kZeroShotsAllowed = 78,
    kOwnGoalAndWin = 79,

    // Category D+: Cup & Thriller Specials (81..83)
    kCupCleanSheetWin = 80,
    kShootoutCleanSheet = 81,
    kHighScoringThriller = 82,

    // Category E+: Career Climb Milestones (84..89)
    kCareerThreeSeasons = 83,
    kCareerFiveSeasons = 84,
    kReachDiv3 = 85,
    kReachDiv2 = 86,
    kReachTopFlight = 87,
    kCleanSheets30 = 88,

    // Category F+: Sponsor, Contract, Stadium, Medical & Transfer Ladders (90..100)
    kFirstSponsorChallenge = 89,
    kSponsor25 = 90,
    kSponsor50 = 91,
    kFirstCleanSweep = 92,
    kThreeCleanSweeps = 93,
    kFirstMultiContract = 94,
    kSponsorMillionaire = 95,
    kStadiumBuilder5 = 96,
    kHomeSelloutStreak3 = 97,
    kFirstMedicalSave = 98,
    kFiveSignings = 99,
};

struct AchievementDef {
    AchievementId id;
    const char *enumName;
    const char *title;
    const char *description;
    AchievementCategory category;
    uint16_t points;         // 10, 20, 30, 50, or 100
    uint32_t targetProgress; // 1 for single-event, >1 for cumulative
    bool isSecret;
};

struct AchievementRecord {
    bool unlocked = false;
    uint32_t currentProgress = 0;
    uint64_t unlockTimestamp = 0;
};

struct GlobalAchievementProfile {
    uint32_t magic = 0x53574F53u; // 'SWOS'
    uint16_t version = 3;
    uint32_t totalPoints = 0;
    uint32_t unlockedCount = 0;

    // Persistent lifetime counters (V1/V2 + V3)
    uint32_t totalMatchesPlayed = 0;
    uint32_t totalGoalsScored = 0;
    uint32_t sponsorChallengesCompleted = 0;
    uint32_t totalWins = 0;
    uint32_t totalCleanSheets = 0;
    uint32_t currentWinStreak = 0;
    uint32_t currentUnbeatenStreak = 0;
    uint32_t totalLongRangeGoals = 0;
    uint32_t totalHeaderGoals = 0;
    uint32_t totalFreeKickGoals = 0;
    uint32_t totalHatTricks = 0;
    uint32_t totalCleanSweeps = 0;
    uint32_t totalMultiContractsCompleted = 0;
    uint64_t totalSponsorCashEarned = 0;
    uint32_t totalStadiumUpgrades = 0;
    uint32_t consecutiveHomeSellouts = 0;
    uint32_t totalMedicalSaves = 0;
    uint32_t totalTransfersSigned = 0;

    AchievementRecord records[kAchievementCount];
};

struct AchievementGoalEvent {
    bool humanScored = false;
    bool isOwnGoal = false;
    bool isPenalty = false;
    bool isHeader = false;
    bool isOutsideBox = false;
    bool isFreeKick = false;
    bool isVolley = false;
    bool withinCornerWindow = false; // <= 3 seconds after a corner kick
    bool scorerIsGoalkeeper = false;
    bool scorerIsDefender = false;
    int matchMinute = 0;
    bool isExtraTime = false;
    bool isGoAheadGoal = false;      // puts the scoring team ahead
    int scorerMatchGoals = 1;
};

struct AchievementMatchSummary {
    bool humanParticipated = false;
    bool humanWon = false;
    bool isFriendly = false;
    bool isCompetitive = false;
    bool isCareer = false;
    bool isCupMatch = false;
    bool isWorldCupFinalWin = false;
    bool isDomesticCupFinalWin = false;
    bool isEuropeanCupFinalWin = false;
    bool wonOnPenalties = false;
    bool wonInExtraTime = false;
    int humanGoals = 0;
    int opponentGoals = 0;
    int humanHtGoals = 0;
    int opponentHtGoals = 0;
    int maxGoalDeficit = 0;          // largest deficit human trailed by at any point
    int humanPossessionPct = 50;
    int humanShotsOnTarget = 0;
    int opponentShotsOnTarget = -1;  // -1 when unknown (e.g. fast-sim View Result)
    int humanGkSaves = 0;
    int humanLongRangeGoals = 0;
    int humanDifferentScorers = 0;
    bool humanConcededOwnGoal = false;
    int pitchType = -1;              // 0=Frozen, 1=Muddy, 2=Wet, 3=Soft, 4=Normal, 5=Dry, 6=Hard
    int humanYellowCards = 0;
    int humanRedCards = 0;
    bool hadRedCardBefore60 = false;
    int humanDivisionTier = -1;      // 0 = top flight, 1 = div 2, 2 = div 3, 3 = div 4
    int opponentDivisionTier = -1;
};

struct AchievementSeasonSummary {
    int oldDivisionTier = -1;
    int newDivisionTier = -1;
    bool promoted = false;
    bool wonTopFlightLeague = false;
    bool wonDomesticCup = false;
    bool wonEuropeanCup = false;
    bool unbeatenLeagueSeason = false;
    int completedCareerSeasons = 0;
};

// Catalog & profile accessors (also directly unit tested)
const AchievementDef& getAchievementDef(AchievementId id);
uint32_t getTotalPossibleAchievementPoints();
const char *getAchievementCategoryName(AchievementCategory cat);
const GlobalAchievementProfile& getAchievementProfile();

// Lifecycle & atomic global persistence (`swos_achievements.dat`)
void initAchievements();
void resetAchievementsProfileForTest();
bool loadAchievements(const std::string& customPath = {});
bool saveAchievements(const std::string& customPath = {});

// Unlock & progress API
bool unlockAchievement(AchievementId id, bool autoSave = true);
bool addAchievementProgress(AchievementId id, uint32_t amount, bool autoSave = true);
bool setAchievementProgress(AchievementId id, uint32_t value, bool autoSave = true);
size_t getPendingAchievementToastCount();

// Match, Career & Subsystem event hooks
void achievementsOnGoalScored(const AchievementGoalEvent& ev);
void achievementsOnMatchFinished(const AchievementMatchSummary& summary);
void achievementsOnCareerTransfer(bool isSigning, int32_t feePounds, const std::string& playerName);
void achievementsOnCareerSeasonEnd(const AchievementSeasonSummary& season);
void achievementsOnNationalTeamCallup();
void achievementsOnCharismaChanged(int16_t charismaPoints);
void achievementsOnSponsorChallengesCompleted(uint32_t newlyCompletedCount);
void achievementsOnSponsorChallengesResolved(
    uint32_t newlyCompletedCount,
    bool cleanSweepTriggered,
    bool multiContractCompleted,
    int64_t positiveNetProfit);

// Stadium & Facilities hooks
void achievementsOnStadiumUpgradeCompleted();
void achievementsOnStadiumMatchAttendance(int capacityLevel, int attendance, int maxCapacity);
void achievementsOnFacilitiesUpdated(int pitchLevel, int trainingLevel, int medicalLevel, int commercialLevel);
void achievementsOnMedicalIntervention();

// Global retro toast overlay (called from render.cpp updateScreen) & Main Menu browser
void drawAchievementToastOverlay();
void showAchievementsMenu();
