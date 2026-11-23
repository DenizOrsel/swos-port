#pragma once

#include <cstdint>
#include <string>
#include <vector>

constexpr uint16_t kAchievementCount = 50;

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
    uint16_t version = 1;
    uint32_t totalPoints = 0;
    uint32_t unlockedCount = 0;

    // Persistent lifetime counters
    uint32_t totalMatchesPlayed = 0;
    uint32_t totalGoalsScored = 0;
    uint32_t sponsorChallengesCompleted = 0;

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

// Stadium & Facilities hooks (implemented & ready for Stadium feature integration)
void achievementsOnStadiumUpgradeCompleted();
void achievementsOnStadiumMatchAttendance(int capacityLevel, int attendance, int maxCapacity);
void achievementsOnFacilitiesUpdated(int pitchLevel, int trainingLevel, int medicalLevel, int commercialLevel);

// Global retro toast overlay (called from render.cpp updateScreen) & Main Menu browser
void drawAchievementToastOverlay();
void showAchievementsMenu();
