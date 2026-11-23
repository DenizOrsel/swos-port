#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include <map>
#include <iosfwd>
#include "stadiumFacilities.h"

constexpr int kMaxHotseatPlayers = 32;
constexpr int kLegacyV7MaxHotseatPlayers = 4;
constexpr int kCareerTeamBufferSize = 1292;
constexpr int kCareerHistoryBufferSize = 2120;
constexpr int kUserTacticsSize = 370;
constexpr int kNumEuroCups = 3;
constexpr size_t kCup1BufferSize = 2898;     // dseg_D8D0A..dseg_D985C (99964..102862)
constexpr size_t kCup2BufferSize = 1091;     // dseg_D985C..dseg_D9C9F (102862..103953)
constexpr size_t kCup4BufferSize = 1091;     // dseg_D9C9F..dseg_DA0E2 (103953..105044)
constexpr size_t kEuroCupBufferSize = 3026;  // dseg_DA0E2..euroCupCopy (105044..108070)
constexpr size_t kCalendarBufferSize = 1870; // dseg_DB3E6 (109912..111782)
constexpr size_t kCompetitionBufferSize = 2898; // competitionFileBuffer..dseg_E0048 (126568..129466)
constexpr int kMaxSelectedTeamsCount = 100;
constexpr size_t kSelectedTeamsBufferSize = 68400; // 100 * sizeof(TeamFile)
constexpr int kTransferAiOffersSize = 70;    // dseg_D6DBA
constexpr int kJobOffersBufferSize = 130;    // newJobOffers / jobOffers
constexpr int kListedPlayersSize = 10;       // playersOnTransferMarket / dseg_D6F26..E

// VM memory offsets for Career mode single-instance global variables
namespace CareerOffsets {
    constexpr size_t kChairmanScenes = 4201;            // base for season strings
    constexpr size_t kCurrentSeasonShowing = 91330;     // uint16_t
    constexpr size_t kStayingInOldClub = 91332;         // int16_t
    constexpr size_t kDsegD6B54 = 91334;                // int16_t (overdraft week streak)
    constexpr size_t kDsegD6B56 = 91336;                // int16_t
    constexpr size_t kDsegD6B5A = 91340;                // int16_t
    constexpr size_t kDsegD6B5C = 91342;                // int16_t
    constexpr size_t kYoureSacked = 91344;              // int16_t
    constexpr size_t kRetired = 91346;                  // int16_t
    constexpr size_t kDsegD6B62 = 91348;                // int16_t
    constexpr size_t kDsegD6B64 = 91350;                // int16_t
    constexpr size_t kClubBusinessActive = 91352;       // int16_t
    constexpr size_t kNewBalance = 91354;               // int32_t
    constexpr size_t kOldBalance = 91358;               // int32_t
    constexpr size_t kPlayerPurchases = 91362;          // int32_t
    constexpr size_t kPlayerSales = 91366;              // int32_t
    constexpr size_t kGeneralRunningCosts = 91370;      // int32_t
    constexpr size_t kPlayerWagesBill = 91374;          // int32_t
    constexpr size_t kGateReceipts = 91378;             // int32_t
    constexpr size_t kStartingBalance = 91382;          // int32_t
    constexpr size_t kDsegD6B88 = 91386;                // int32_t
    constexpr size_t kCompetitionBonuses = 91390;       // int32_t
    constexpr size_t kSensisoftPlcSponsorship = 91394;  // int32_t
    constexpr size_t kDsegD6B94 = 91398;                // int16_t
    constexpr size_t kDsegD6B9A = 91404;                // int32_t (weekly running costs)
    constexpr size_t kDsegD6B9E = 91408;                // int32_t (weekly player wages)
    constexpr size_t kDsegD6BA2 = 91412;                // int32_t (annual wage pool)
    constexpr size_t kDsegD6BA6 = 91416;                // uint32_t[32] (per-player wages)
    constexpr size_t kDsegD6C26 = 91544;                // uint8_t[32] (trial/contract countdown weeks)
    constexpr size_t kDsegD6C46 = 91576;                // int16_t
    constexpr size_t kAdditionalInvestments = 91578;    // int16_t
    constexpr size_t kDsegD6C4E = 91584;                // uint16_t
    constexpr size_t kDsegD6C50 = 91586;                // uint16_t
    constexpr size_t kGamesPlayed = 91630;              // uint16_t
    constexpr size_t kNumWins = 91632;                  // uint16_t
    constexpr size_t kNumDrawn = 91634;                 // uint16_t
    constexpr size_t kNumLost = 91636;                  // uint16_t
    constexpr size_t kGoalsScored = 91638;              // uint16_t
    constexpr size_t kGoalsTaken = 91640;               // uint16_t
    constexpr size_t kDsegD6C88 = 91642;                // uint16_t[12] (dseg_D6C88..D6C9E)
    constexpr size_t kDsegD6CA0 = 91666;                // uint16_t
    constexpr size_t kDsegD6CA2 = 91668;                // uint16_t
    constexpr size_t kTeamNoOfferAccepted = 91670;      // int16_t
    constexpr size_t kTeamFundsAccepted = 91672;        // int32_t
    constexpr size_t kIsNationalSelector = 91676;       // int16_t
    constexpr size_t kDsegD6CB8 = 91690;                // uint8_t
    constexpr size_t kSelTeamNumber = 91720;            // int16_t
    constexpr size_t kCoachOrPlayer = 91722;            // int16_t
    constexpr size_t kCareerTeamLeague = 91724;         // uint16_t
    constexpr size_t kDsegD6CDC = 91726;                // uint16_t
    constexpr size_t kDsegD6CDE = 91728;                // uint16_t (division count)
    constexpr size_t kMrOrMs = 91734;                   // int16_t
    constexpr size_t kManagerSexBuffer = 91736;         // 5 bytes
    constexpr size_t kManagerNameBuffer = 91741;        // 9 bytes
    constexpr size_t kManagerSurnameBuffer = 91750;     // 13 bytes
    constexpr size_t kPlNationality = 91763;            // uint8_t
    constexpr size_t kDsegD6D0A = 91772;                // int16_t
    constexpr size_t kDsegD6D0C = 91774;                // int16_t
    constexpr size_t kDsegD6D0E = 91776;                // int16_t
    constexpr size_t kDsegD6D10 = 91778;                // int16_t
    constexpr size_t kDsegD6D12 = 91780;                // int16_t
    constexpr size_t kNumForeignMarketPlayers = 91782;  // uint16_t
    constexpr size_t kNumHomeMarketPlayers = 91784;     // uint16_t
    constexpr size_t kNumOffers = 91786;                // int16_t
    constexpr size_t kForeignMarketPlayers = 91788;     // 15 * 4 bytes (teamId u16, ordinal u8, pad u8)
    constexpr size_t kHomeMarketPlayers = 91848;        // 25 * 4 bytes (teamId u16, ordinal u8, pad u8)
    constexpr size_t kDsegD6DBA = 91948;                // uint8_t[70] (incoming AI offers)
    constexpr size_t kNumNewJobOffers = 92034;          // int16_t
    constexpr size_t kNewJobOffers = 92036;             // uint8_t[130]
    constexpr size_t kNumJobOffers = 92166;             // int16_t
    constexpr size_t kJobOffers = 92168;                // uint8_t[130]
    constexpr size_t kTimeToNegotiate = 92298;          // int16_t
    constexpr size_t kNumPlayersOnTransferMarket = 92300; // uint16_t
    constexpr size_t kPlayersOnTransferMarket = 92302;  // uint8_t[10]
    constexpr size_t kDsegD6F26Block = 92312;           // uint8_t[10] (dseg_D6F26..E)
    constexpr size_t kCurrentMatchPlayers = 92766;      // 32 bytes
    constexpr size_t kCareerTeam = 92798;               // 1292 bytes
    constexpr size_t kCareerNumPlayers = 94090;         // uint16_t
    constexpr size_t kDsegD761A = 94092;                // uint16_t
    constexpr size_t kDsegD761C = 94094;                // uint16_t
    constexpr size_t kTmdCareerCount = 36606;           // uint16_t (careerFileBuffer)
    constexpr size_t kTmdFileBuffer = 36608;            // loaded TMD teams buffer
    constexpr size_t kPoolplyrDatBuffer = 200176;       // 900 * 58 bytes (POOLPLYR.DAT)
    constexpr size_t kPoolplyrLoaded = 323694;          // uint16_t
    constexpr size_t kDseg17A3D0 = 503556;              // char[23] generated player name buffer
    constexpr size_t kCareerHistory = 94096;            // 2120 bytes
    constexpr size_t kDsegD7E66 = 96216;                // uint16_t (last billed week)
    constexpr size_t kDsegD8B6A = 99548;                // uint16_t (saved dseg_D761A)
    constexpr size_t kTrainingMatchSavedPlayers = 99550;// uint8_t[32] (canonical squad order)
    constexpr size_t kDsegD8B8C = 99582;                // uint8_t[32] (saved shirt numbers)
    constexpr size_t kCurrentTeamNumber = 99544;        // uint16_t (FindTeamInCache team id)
    constexpr size_t kDsegD8BAC = 99614;                // uint8_t (saved tactics)
    constexpr size_t kDsegD8BAE = 99616;                // uint16_t
    constexpr size_t kDsegD8BB0 = 99618;                // uint16_t
    constexpr size_t kLastMatchPlayers = 99620;         // uint8_t[32]
    constexpr size_t kDsegD8BD2 = 99652;                // uint8_t[32]
    constexpr size_t kCurrentTactics = 99684;           // uint8_t
    constexpr size_t kDsegD8BF3 = 99685;                // uint8_t
    constexpr size_t kSeasonFinished = 99864;           // uint16_t
    constexpr size_t kCareerFinished = 99866;           // uint16_t
    constexpr size_t kDsegD8CAA = 99868;                // uint32_t[5] (active competition ptrs)
    constexpr size_t kDsegD8CB6 = 99880;                // uint32_t (active euro cup ptr)
    constexpr size_t kDsegD8CBA = 99884;                // uint32_t (playoff/supercup ptr)
    constexpr size_t kDsegD8CBE = 99888;                // uint16_t (active euro cup idx)
    constexpr size_t kDsegD8CC0 = 99890;                // uint32_t (cup4 init ptr)
    constexpr size_t kSeasonNumberOfSquads = 99894;     // uint16_t
    constexpr size_t kArrayOfPointers = 99944;          // uint32_t[5] (competition buffer ptrs)
    constexpr size_t kDsegD8D0A = 99964;                // 2898 bytes (cup 1 buffer)
    constexpr size_t kDsegD985C = 102862;               // 1091 bytes (cup 2 buffer)
    constexpr size_t kDsegD9C9F = 103953;               // 1091 bytes (cup 4 buffer)
    constexpr size_t kDsegDA0E2 = 105044;               // 3026 bytes (euro cup buffer)
    constexpr size_t kDsegDA109 = 105083;               // uint32_t (euro cup season string ptr)
    constexpr size_t kDsegDA11F = 105105;               // 6 bytes (dseg_DA11F..23)
    constexpr size_t kEuroCupCopy = 108070;             // 79 bytes
    constexpr size_t kCupWinnersCupCopy = 108149;       // 92 bytes
    constexpr size_t kUefaCupCopy = 108241;             // 92 bytes
    constexpr size_t kDsegDB3DE = 109904;               // uint16_t (calendar step index)
    constexpr size_t kDsegDB3E0 = 109906;               // uint16_t (calendar step byte offset)
    constexpr size_t kDsegDB3E2 = 109908;               // uint16_t (total calendar entries)
    constexpr size_t kDsegDB3E4 = 109910;               // uint16_t
    constexpr size_t kDsegDB3E6 = 109912;               // calendar entries array
    constexpr size_t kDsegDF4F4 = 126566;               // uint16_t (play match indicator)
    constexpr size_t kCompetitionFileBuffer = 126568;   // 2898 bytes (league buffer)
    constexpr size_t kDsegE0048 = 129466;               // uint8_t (calendar week byte)
    constexpr size_t kSeasonPlaying = 129467;           // uint8_t
    constexpr size_t kDsegE0052 = 129476;               // uint16_t (0=career cup select)
    constexpr size_t kDsegE092F = 131745;               // uint16_t (euro cup index / -1)
    constexpr size_t kCompCountryNumber = 131744;       // uint8_t (active country number)
    constexpr size_t kSomeLeaguesTable = 96218;         // int8_t[2000] active league adjustments
    constexpr size_t kLeaguesTableCopy = 353240;        // int8_t[12944] offseason league deltas
    constexpr size_t kDsegD6E00 = 92018;                // uint16_t career counter
    constexpr size_t kTeamFileBuffer = 252466;          // 64014 bytes
    constexpr size_t kDseg11F9CC = 379316;              // uint16_t (FindTeamInCache careerTeam active flag)
    constexpr size_t kDiyFileBufferCopy = 317386;       // tournament work buffer
    constexpr size_t kDseg10EB23 = 317539;              // tournament team word list
    constexpr size_t kDiyCompetitionStart = 318441;     // league/cup work buffer
    constexpr size_t kDseg10F0F6 = 319030;              // cup team word list
    constexpr size_t kChooseTeamsResult = 330146;       // uint16_t
    constexpr size_t kUserTacticsStart = 129478;        // 6 * 370 bytes
    constexpr size_t kDseg174570 = 482083;              // uint16_t (top calendar entry idx in game list)
    constexpr size_t kCurrentGame = 482085;             // uint16_t (top calendar slot idx in game list)
    constexpr size_t kDseg174574 = 482087;              // uint16_t (total calendar entries in game list)
    constexpr size_t kDseg174576 = 482089;              // int16_t (highlighted cal row idx)
    constexpr size_t kDseg174578 = 482091;              // uint16_t (current calendar slot idx)
    constexpr size_t kDseg17457A = 482093;              // uint32_t (current calendar header ptr)
    constexpr size_t kDseg17457E = 482097;              // uint32_t (current match slot ptr)
    constexpr size_t kGameListArray = 482119;           // uint32_t[28] (14 x {calEntryVm, slotVm})
    constexpr size_t kContestMenu = 482233;             // PackedMenu contestMenu
    constexpr size_t kCareerOverMenu = 483813;          // PackedMenu careerOverMenu
    constexpr size_t kChooseTeamsFriendly = 485019;     // PackedMenu chooseTeamsFriendly
    constexpr size_t kChooseTeamsFriendlyMenu = 499317; // PackedMenu chooseTeamsFriendlyMenu
    constexpr size_t kSelectTeamsMenu = 497495;         // PackedMenu selectTeamsMenu
    constexpr size_t kTopOfStackMark = 329978;          // uint16_t selectedTeams stack mark
    constexpr size_t kCurrentSeasonNumber = 484815;     // uint16_t (display copy)
    constexpr size_t kSeasonFinancesBlock = 484991;     // uint8_t[24] (dseg_1750CC..E0)
    constexpr size_t kDseg17A3CE = 503554;              // uint16_t national selector flag
    constexpr size_t kNumExtraPlayersWanted = 504324;   // int16_t (2nd wanted player ordinal)
    constexpr size_t kDseg17A6D2 = 504326;              // int16_t (1st exchange player ordinal)
    constexpr size_t kDseg17A6D4 = 504328;              // int16_t (2nd exchange player ordinal)
    constexpr size_t kDseg180782 = 518659;              // uint8_t (active country in cseg_9153F)
    constexpr size_t kJobOffersMenu = 509636;           // PackedMenu jobOffersMenu
    constexpr size_t kGotJobOfferMenu = 510954;         // PackedMenu gotJobOfferMenu
    constexpr size_t kDseg17C228 = 511324;              // uint16_t job offers UI mode
    constexpr size_t kAContinueCareer = 8403;           // "CONTINUE CAREER*******"
    constexpr size_t kAJobOffers = 17282;               // "JOB OFFERS******"
}

constexpr int kCurrentMatchPlayersSize = 32;
constexpr int kMinCareerSquadPlayers = 16;
constexpr int kMaxCareerSquadPlayers = 30;
constexpr int kPlayerWagesCount = 32;
constexpr int kSeasonStatsWordsCount = 12;
constexpr int kSeasonFinancesBlockSize = 24;
constexpr int kCareerHistoryEntrySize = 106;

struct HotseatPlayerContext {
    int16_t teamId = 0;
    std::string teamName;
    std::string managerName = "MANAGER";
    std::string managerSurname = "PLAYER";
    int16_t coachOrPlayer = 0;       // 0 = coach, 1 = player-coach
    uint16_t careerTeamLeague = 0;
    uint8_t nationality = 8;         // default English
    int16_t mrOrMs = 0;              // 0 = Mr, 1 = Ms

    // Financials
    int32_t newBalance = 0;
    int32_t oldBalance = 0;
    int32_t startingBalance = 0;
    int32_t gateReceipts = 0;
    int32_t playerWagesBill = 0;
    int32_t generalRunningCosts = 0;
    int32_t playerSales = 0;
    int32_t playerPurchases = 0;
    int32_t competitionBonuses = 0;
    int32_t sensisoftPlcSponsorship = 0;

    // Board, stats & season state
    int16_t stayingInOldClub = 0;
    int16_t dsegD6B54 = 0;
    int16_t dsegD6B56 = 0;
    int16_t dsegD6B5A = 0;
    int16_t dsegD6B5C = 0;
    int16_t dsegD6B62 = 0;
    int16_t dsegD6B64 = 0;
    int16_t dsegD6B94 = 0;
    int32_t dsegD6B88 = 0;
    int32_t dsegD6B9A = 0;
    int32_t dsegD6B9E = 0;
    int32_t dsegD6BA2 = 0;
    uint32_t playerWages[kPlayerWagesCount] = {0};
    uint8_t playerTrialWeeks[kPlayerWagesCount] = {0};
    int16_t dsegD6C46 = 0;
    int16_t additionalInvestments = 0;
    uint16_t dsegD6C4E = 0;
    uint16_t dsegD6C50 = 0;
    int16_t youreSacked = 0;
    int16_t retired = 0;
    int16_t clubBusinessActive = 0;
    int16_t numNewJobOffers = 0;
    int16_t numJobOffers = 0;
    int16_t teamNoOfferAccepted = -1;
    int32_t teamFundsAccepted = 0;
    int16_t isNationalSelector = 0;
    uint16_t dsegD6CDC = 0;
    int16_t numOffers = 0;
    int16_t timeToNegotiate = 0;
    int16_t dsegD6D0A = 0;
    int16_t dsegD6D0C = 0;
    int16_t dsegD6D0E = 0;
    int16_t dsegD6D10 = 0;
    int16_t dsegD6D12 = 0;
    uint16_t gamesPlayed = 0;
    uint16_t numWins = 0;
    uint16_t numDrawn = 0;
    uint16_t numLost = 0;
    uint16_t goalsScored = 0;
    uint16_t goalsTaken = 0;
    uint16_t seasonStatsBlock[kSeasonStatsWordsCount] = {0};
    uint16_t dsegD6CA0 = 0;
    uint16_t dsegD6CA2 = 0;
    uint16_t dsegD7E66 = 0;
    uint16_t currentSeasonShowing = 0;
    uint16_t currentSeasonNumber = 0;
    uint8_t seasonPlaying = 94;
    uint8_t dsegE0048 = 0;
    uint16_t playMatchBlink = 0;
    uint16_t careerNumPlayers = 16;
    uint8_t seasonFinancesBlock[kSeasonFinancesBlockSize] = {0};
    uint16_t numPlayersOnTransferMarket = 0;
    uint8_t playersOnTransferMarket[kListedPlayersSize] = {0};
    uint8_t dsegD6F26Block[kListedPlayersSize] = {0};
    uint8_t dsegD6DBA[kTransferAiOffersSize] = {0};
    uint8_t newJobOffers[kJobOffersBufferSize] = {0};
    uint8_t jobOffers[kJobOffersBufferSize] = {0};
    int8_t euroCupIndex = -1;

    // Team, Matchday Mapping, and History buffers
    uint8_t currentMatchPlayers[kCurrentMatchPlayersSize] = {0};
    uint8_t careerTeam[kCareerTeamBufferSize] = {0};
    uint8_t careerHistory[kCareerHistoryBufferSize] = {0};

    // Canonical SWOS match-save/restore & team-change buffers (must stay per-player!)
    uint16_t dsegD761A = 0xFFFF;
    uint16_t dsegD761C = 0;
    uint16_t dsegD8B6A = 0xFFFF;
    uint8_t trainingMatchSavedPlayers[kCurrentMatchPlayersSize] = {0};
    uint8_t dsegD8B8C[kCurrentMatchPlayersSize] = {0};
    uint8_t dsegD8BAC = 0;
    uint16_t dsegD8BAE = 0;
    uint16_t dsegD8BB0 = 0;
    uint8_t lastMatchPlayers[kCurrentMatchPlayersSize] = {0};
    uint8_t dsegD8BD2[kCurrentMatchPlayersSize] = {0};
    uint8_t currentTactics = 0;
    uint8_t dsegD8BF3 = 0;

    // User custom tactics (USER A to F)
    uint8_t userTactics[6][kUserTacticsSize] = {{0}};

    // Turn tracking
    bool matchPlayedThisTurn = false;
    bool turnCompleted = false;

    // Pre-Match Sponsor Challenge career statistics (serialized in V10+)
    uint16_t sponsorChallengesCompleted = 0;
    uint16_t sponsorChallengesFailed = 0;
    int64_t sponsorTotalBonusEarned = 0;

    // LLM Press Conference & Coach Charisma career state (serialized in PRSS trailer)
    int16_t pressCharismaPoints = 50;
    uint16_t pressInterviewsCompleted = 0;
    int64_t pressLastSeasonBonus = 0;
    uint8_t pressLastPreMatchStance = 0;
    std::string pressLastPreMatchQuote;

    // Stadium & Facilities state (serialized in STAD trailer)
    StadiumData stadium;
};

struct WizardPlayerInfo {
    std::string name = "MANAGER";
    std::string surname = "PLAYER";
    uint8_t nationality = 8;
    int16_t coachOrPlayer = 1;       // 1 = player-coach
    int16_t mrOrMs = 0;
};

struct LeagueWorldKey {
    uint8_t countryFileNo = 0;
    uint8_t divisionId = 0;

    bool operator<(const LeagueWorldKey& o) const {
        return countryFileNo < o.countryFileNo ||
               (countryFileNo == o.countryFileNo && divisionId < o.divisionId);
    }
    bool operator==(const LeagueWorldKey& o) const {
        return countryFileNo == o.countryFileNo && divisionId == o.divisionId;
    }
    bool operator!=(const LeagueWorldKey& o) const {
        return !(*this == o);
    }
};

struct CountryWorldState {
    uint8_t countryFileNo = 0;
    bool initialized = false;
    uint32_t dsegD8CAE = 0;
    uint32_t dsegD8CB2 = 0;
    uint16_t dsegD6CDE = 1;
    uint8_t cup1[kCup1BufferSize] = {0};
    uint8_t cup2[kCup2BufferSize] = {0};
    uint16_t numHomeMarketPlayers = 0;
    uint8_t homeMarketPlayers[25 * 4] = {0};
    uint16_t numForeignMarketPlayers = 0;
    uint8_t foreignMarketPlayers[15 * 4] = {0};
};

struct LeagueWorldState {
    LeagueWorldKey key{};
    bool initialized = false;
    uint32_t dsegD8CAA[5] = {0};
    uint32_t dsegD8CC0 = 0;
    uint8_t competitionFileBuffer[kCompetitionBufferSize] = {0};
    uint8_t dsegD9C9F[kCup4BufferSize] = {0};
    uint8_t calendar[kCalendarBufferSize] = {0};
    uint16_t calStepIdx = 0;
    uint16_t calByteOfs = 0;
    uint16_t calTotalEntries = 0;
    uint16_t dsegDB3E4 = 0;
    uint16_t playMatchBlink = 0;
    uint8_t dsegE0048 = 0;
    uint8_t seasonPlaying = 94;
    uint16_t currentSeasonShowing = 0;
    uint16_t currentSeasonNumber = 0;
    uint16_t dsegD6CA2 = 0;
    uint16_t seasonFinished = 0;
    uint16_t careerFinished = 0;
    int16_t hlIdx = -1;
    uint16_t hlSlotIdx = 0;
    uint32_t hlHdr = 0;
    uint32_t hlMatchPtr = 0;
    int roundCalendarIndex = 0;
    int pendingPostMatchCalOfs = -1;
    int pendingPostMatchSlotIdx = -1;
    std::map<uint16_t, std::vector<std::array<uint8_t, 12>>> calendarSlots;
    std::map<uint16_t, int> euroCupByCalOfs;
    int16_t numSelectedTeams = 0;
    std::vector<uint8_t> selectedTeams;
};

class HotseatCareerManager {
public:
    static HotseatCareerManager& get();

    void reset();
    bool isActive() const { return m_active && m_numPlayers >= 1; }
    void setActive(bool active) { m_active = active; }
    int numPlayers() const { return m_numPlayers; }
    int activePlayerIndex() const { return m_activePlayerIndex; }
    HotseatPlayerContext& activePlayer() { return m_players[m_activePlayerIndex]; }
    const HotseatPlayerContext& activePlayer() const { return m_players[m_activePlayerIndex]; }
    HotseatPlayerContext& player(int index) { return m_players[index]; }
    const HotseatPlayerContext& player(int index) const { return m_players[index]; }

    // Setup
    void initializeHotseat(const std::vector<int16_t>& teamIds);

    // Wizard
    bool isWizardActive() const { return m_isWizardActive; }
    void setWizardActive(bool active) { m_isWizardActive = active; }
    int wizardCurrentPlayer() const { return m_wizardCurrentPlayer; }
    void setWizardCurrentPlayer(int p) { m_wizardCurrentPlayer = p; }
    int wizardTotalPlayers() const { return m_wizardTotalPlayers; }
    void setWizardTotalPlayers(int total) { m_wizardTotalPlayers = total; }
    bool wizardAdvanced() const { return m_wizardAdvanced; }
    void setWizardAdvanced(bool advanced) { m_wizardAdvanced = advanced; }
    WizardPlayerInfo& wizardPlayer(int idx) { return m_wizardPlayers[idx]; }
    const WizardPlayerInfo& wizardPlayer(int idx) const { return m_wizardPlayers[idx]; }
    void resetWizard(int totalPlayers);
    void applyWizardDefaultsToVm(int playerIndex) const;

    // Club Selection State
    bool isSelectingCareerTeams() const { return m_isSelectingCareerTeams; }
    void setSelectingCareerTeams(bool selecting) { m_isSelectingCareerTeams = selecting; }
    int careerNumPlayers() const { return m_careerNumPlayers; }
    void setCareerNumPlayers(int count) { m_careerNumPlayers = count; }

    struct AssignedSlotInfo {
        int slot = -1;
        int16_t globalTeamNumber = 0;
        uint16_t teamWord = 0;
        uint8_t teamFileNo = 0;
        uint8_t league = 0;
        TeamFile teamFile;
    };

    void assignTeamSlot(int16_t teamId, int slot, const TeamFile *tf = nullptr);
    void unassignTeamSlot(int16_t teamId);
    void unassignAllTeamSlots();
    int getTeamSlot(int16_t teamId) const;
    int getTeamForSlot(int slot) const;
    const AssignedSlotInfo *getAssignedSlotInfo(int slot) const;
    int getNextAvailableSlot() const;
    int getAssignedSlotCount() const;
    int getTeamSlotFromEntry(const struct MenuEntry *entry) const;
    bool areAllAssignedTeamsInSameLeague() const;
    bool isTeamSelectionMenuOffset(uint32_t menuOffset) const;
    void syncSelectedTeamsFromAssignedSlots();
    void refreshTeamSelectionMenu(uint32_t menuOffset);
    bool handleTeamSelectionMenuSelect(uint32_t menuOffset, struct MenuEntry *entry);

    // Context Swapping & Competition Pointer Linking
    void saveCurrentSwosContext(HotseatPlayerContext& ctx);
    void loadSwosContext(const HotseatPlayerContext& ctx, bool relink = true);
    void ensureVmTrainingMatchSavedPlayersSynced();
    void switchToPlayer(int index, bool relink = true);
    void relinkCompetitionTeamPointers();
    void syncPlayersWithSelectedTeams(bool writeToSelected);
    void ensureHotseatTeamControlsInSelectedTeams();

    // Multi-League / Multi-Country World Multiplexer
    static LeagueWorldKey worldKeyForPlayer(const HotseatPlayerContext& ctx);
    LeagueWorldKey activeWorldKey() const { return m_activeWorldKey; }
    bool hasActiveWorld() const { return m_hasActiveWorld; }
    int worldCount() const { return static_cast<int>(m_leagueWorlds.size()); }
    int countryWorldCount() const { return static_cast<int>(m_countryWorlds.size()); }
    const std::map<LeagueWorldKey, LeagueWorldState>& leagueWorlds() const { return m_leagueWorlds; }
    std::map<LeagueWorldKey, LeagueWorldState>& leagueWorlds() { return m_leagueWorlds; }
    const std::map<uint8_t, CountryWorldState>& countryWorlds() const { return m_countryWorlds; }
    std::map<uint8_t, CountryWorldState>& countryWorlds() { return m_countryWorlds; }
    void saveActiveWorldFromVm();
    void loadWorldToVm(const LeagueWorldKey& targetKey, bool relink = true);
    void applyActivePlayerLeagueOverlayIfNeeded();
    bool hasLeagueOverlay() const { return m_hasLeagueOverlay; }
    LeagueWorldKey leagueOverlayKey() const { return m_leagueOverlayKey; }
    void captureActiveWorldAsInitial(const LeagueWorldKey& key);
    void cacheTeamsFromSelectedTeams();
    void cacheTeamFile(const TeamFile& tf);
    bool lookupCachedTeamFile(uint16_t teamId, TeamFile& outTf) const;
    void ensureCupTeamsInSelectedTeams();
    int32_t resolveTeamDeltaForCompetition(uint16_t teamWord, uint16_t activeTeamWord, int32_t selTeamsPtrVal, bool& found);

    // Turn Management & Calendar Synchronization
    bool canEndTurn() const;
    void onMatchCompleted();
    void markPlayerMatchCompleted(int playerIndex);
    bool endTurn(); // Legacy round-robin step (kept for unit compatibility)
    int roundCalendarIndex() const { return m_roundCalendarIndex; }
    void setRoundCalendarIndex(int idx) { m_roundCalendarIndex = idx; }
    uint16_t globalWeek() const { return m_globalWeek; }
    void setGlobalWeek(uint16_t week) { m_globalWeek = week; }
    // Builds every starting (country, division) world for the already-configured players (setup wizard / tests).
    // Returns the index of the player whose turn comes first.
    int initializeStartingWorlds(const std::vector<TeamFile>& orderedTeams);
    // .hot V9 multi-world persistence (country worlds, league worlds, lockstep cursor, EuroCup hosts, club cache)
    bool writeWorldsV9(std::ofstream& out) const;
    bool readWorldsV9(std::ifstream& in);
    bool areAllWorldsSeasonFinished() const;
    bool stepActiveWorldOneCpuEntrySilently();
    bool resolveLockstepAndSelectPlayer(bool refreshUi = true);
    void onBeforePlayMatchMenu();
    void synchronizeSharedCupCalendarEntries();
    int findNextPlayerForCurrentCalendarSlot() const;
    bool ensureActivePlayerMatchesCurrentSlot();
    bool syncActivePlayerToHighlightedSlot(bool refreshUi = true);
    void refreshContestMenuUi() const;
    bool advanceTurnAndResolveRound();

    // Concurrent European Cup & Domestic Cup Support
    void resetEuroCups();
    bool anyEuroCupActive() const;
    void saveCurrentEuroCup();
    void activateEuroCup(int euroIdx, bool relink = true);
    int findEuroCupForCalOfs(uint16_t calOfs) const;
    void initAdditionalEuroCups();
    void activateEuroCupForCurrentCalendarEntry();
    void maskOtherEuroCupCalendarEntries();
    void unmaskEuroCupCalendarEntries();
    void activateEuroCupForSeasonEnd(int euroIdx);
    void addHumanTeamsToCupSelection(uint32_t listBaseVm);
    const std::map<uint16_t, int>& euroCupByCalOfs() const { return m_euroCupByCalOfs; }
    bool isEuroCupActive(int euroIdx) const { return euroIdx >= 0 && euroIdx < kNumEuroCups && m_euroCupActive[euroIdx]; }

    // Multi-player Calendar Shadow Slot Management (prevents 1870B dseg_DB3E6 overflow)
    using CalendarSlotBytes = std::array<uint8_t, 12>;
    void clearAllCalendarSlots();
    void resetCalendarEntrySlots(uint32_t entryVmOffset);
    void shiftCalendarSlots(uint32_t insertVmOffset, uint16_t shiftBytes);
    void captureLeagueCalendarSlot();
    void captureCupCalendarSlot();
    void restoreCalendarEntryFirstSlots();
    void stageActiveCalendarSlotToVm();
    void resolvePostMatchCalendarSlotPtr();
    void commitAndStageNextCalendarSlot();
    void clampCalcNextMatchSlotPtr();
    void onGameListCenter();
    void onGameListScrollUp();
    void onGameListScrollDown();
    void onAfterBuildGameList();
    void resolveGameListSlotPtr();
    void restoreGameListContext();
    const std::map<uint16_t, std::vector<CalendarSlotBytes>>& calendarSlots() const { return m_calendarSlots; }

    // Match, Weekly, & Season Lifecycle Hooks
    void recordHeadToHeadResult(int topPlayerIdx, int botPlayerIdx, uint16_t topGoals, uint16_t botGoals,
                                uint16_t topPenGoals = 0, uint16_t botPenGoals = 0);
    void onAfterPlayMatch();
    void onWeeklyTickForAllPlayers();
    void onSeasonFinishedForAllPlayers();
    void onNewSeasonInitialized();
    bool isInitializingSeason() const { return m_initializingSeason; }
    void setInitializingSeason(bool initializing) { m_initializingSeason = initializing; }

    // Team and Player lookups
    int findHotseatPlayerByTeamName(const char *teamName) const;
    int findHotseatPlayerByTeamId(int16_t teamId) const;

    // Inter-Player Transfer queue
    struct TransferOffer {
        int id = 0;
        int fromPlayerIndex = -1;
        int toPlayerIndex = -1;
        int playerOrdinal = -1;
        char playerName[32] = {0};
        int extraWantedOrdinal = -1;
        char extraWantedName[32] = {0};
        int exchangeOrdinal1 = -1;
        char exchangeName1[32] = {0};
        int exchangeOrdinal2 = -1;
        char exchangeName2[32] = {0};
        uint32_t offerAmount = 0;
        bool pending = true;
        bool accepted = false;
        bool rejected = false;
        bool isCounterOffer = false;
    };
    int queueTransferOffer(const TransferOffer& offer);
    std::vector<TransferOffer> getPendingOffersForPlayer(int playerIndex) const;
    TransferOffer* getTransferOfferById(int offerId);
    const TransferOffer* getTransferOfferById(int offerId) const;
    bool canAcceptTransferOffer(int offerId, std::string *reasonOut = nullptr) const;
    bool resolveTransferOffer(int offerId, bool accepted);
    void counterTransferOffer(int offerId, uint32_t counterAmount);
    bool processPendingOffersForActivePlayer();
    bool pendingOfferPrompt() const { return m_pendingOfferPrompt; }
    void setPendingOfferPrompt(bool pending) { m_pendingOfferPrompt = pending; }
    bool inPlayMatchFlow() const { return m_inPlayMatchFlow; }
    void setInPlayMatchFlow(bool inFlow) { m_inPlayMatchFlow = inFlow; }

    // Persistence
    bool saveToFile(const std::string& path);
    bool loadFromFile(const std::string& path);
    void onAfterProcessCareerFile();

    // Transfer departure trialist replacement, emergency loan, & market exclusion/sync helpers
    void onAfterCreateReplacementPlayer();
    void onBeforeEmergencyLoan();
    void onAfterEmergencyLoan();
    void onAfterEnsureCareerPlayers();
    void onBeforeShowPlayMatchMenu();
    void onAfterShowPlayMatchMenu();
    void onBeforeAddDomesticMarket();
    void onAfterAddDomesticMarket();
    void onBeforeAddForeignMarket();
    void onAfterAddForeignMarket();
    void syncTransferMarketPoolsForCurrentPlayer();
    void resolveMarketTeamPtrForEntry(uint32_t entryOffset);
    void lookupForeignPoolPlayer();
    void resolveGetPlayerTeamPtr();
    uint32_t ensureHotseatTeamMirror(int playerIdx);
    uint32_t ensurePoolPlayerScratch();

    // End-of-Season Multi-Player Review, Job Offer Deduplication, & Club Transition
    bool isEndSeasonActive() const { return m_endSeasonActive; }
    int endSeasonPlayerIndex() const { return m_endSeasonPlayerIndex; }
    bool isValidHotseatJobOffer(uint16_t teamId, int forPlayerIdx = -1) const;
    void removeJobOfferForAllOtherPlayers(uint16_t teamId, int acceptingPlayerIdx);
    void sanitizeJobOffersForContext(HotseatPlayerContext& ctx, int playerIdx);
    void sanitizeJobOffersForAllPlayers();
    void resetCareerOverMenuState() const;
    void resetNonHotseatTeamControlsInTmdFile();
    void ensurePlayerSlots(int count);
    void onCareerOverInit();
    void onAfterAcceptJobOffer();
    void onAfterJobOfferRoll();
    void onFilterNewJobOffer();
    void onContinueCareer();
    void onExitCareerMenu();
    void onJobOffersMenuRetired();
    bool onJobOffersMenuRetiredSackedEmpty();
    void injectEndSeasonLeagueResult();
    void injectEndSeasonCup1Result();
    void injectEndSeasonCup2Result();
    void injectEndSeasonPlayoffResult();
    void setPreserveEuroCupsOnSeasonInit(bool preserve) { m_preserveEuroCupsOnSeasonInit = preserve; }

private:
    HotseatCareerManager() = default;

    bool isPlayerCareerOver(const HotseatPlayerContext& ctx) const;
    bool isVmCareerOver() const;
    bool hasSurvivingPlayerAfterEndSeason(int excludePlayerIdx = -1) const;
    void advanceEndSeasonToNextPlayer(int nextIdx);
    void packDepartedTeamIntoTmdFile(const HotseatPlayerContext& ctx);
    void executeHotseatSeasonTransition();

    bool m_active = false;
    int m_numPlayers = 0;
    int m_activePlayerIndex = 0;
    int m_nextOfferId = 1;
    int m_roundCalendarIndex = 0;
    int m_pendingPostMatchCalOfs = -1;
    int m_pendingPostMatchSlotIdx = -1;
    bool m_pendingOfferPrompt = false;
    bool m_inPlayMatchFlow = false;
    bool m_initializingSeason = false;
    bool m_preserveEuroCupsOnSeasonInit = false;
    bool m_inEndSeasonCompute = false;
    bool m_endSeasonPendingInit = false;
    bool m_endSeasonActive = false;
    int m_endSeasonPlayerIndex = 0;
    std::vector<uint8_t> m_endSeasonEliminated;
    std::vector<uint16_t> m_claimedJobOfferTeams;
    std::vector<HotseatPlayerContext> m_players;

    struct PendingSeasonPlayerState {
        int16_t oldTeamId = 0;
        int16_t newTeamId = 0;
        int32_t acceptedFunds = 0;
        bool switched = false;
    };
    std::vector<PendingSeasonPlayerState> m_pendingSeasonTransitions;
    std::vector<TransferOffer> m_transferOffers;

    // Hotseat Setup Wizard & Selection
    bool m_isWizardActive = false;
    int m_wizardCurrentPlayer = 0;
    int m_wizardTotalPlayers = 1;
    bool m_wizardAdvanced = false;
    std::vector<WizardPlayerInfo> m_wizardPlayers;

    bool m_isSelectingCareerTeams = false;
    int m_careerNumPlayers = 1;
    std::map<int16_t, int> m_assignedTeamSlots;
    std::map<int, AssignedSlotInfo> m_assignedSlotInfos;
    std::map<uint16_t, std::vector<CalendarSlotBytes>> m_calendarSlots;

    // Concurrent European Cups (0 = euroCup, 1 = cupWinnersCup, 2 = uefaCup)
    bool m_euroCupActive[kNumEuroCups] = {false, false, false};
    int m_activeEuroCupIndex = -1;
    bool m_maskingForEuroCupInit = false;
    uint32_t m_euroCupCopyOfs[kNumEuroCups] = {};
    uint32_t m_euroCupSeasonStrOfs[kNumEuroCups] = {0, 0, 0};
    uint8_t m_euroCupScheduleTemplate[6] = { 5, 0, 2, 0, 8, 0 };
    std::array<std::array<uint8_t, kEuroCupBufferSize>, kNumEuroCups> m_euroCupBuffers{};
    std::map<uint16_t, int> m_euroCupByCalOfs;

    uint16_t m_prevHomeMarketCount = 0;
    uint16_t m_prevForeignMarketCount = 0;
    uint32_t m_vmPoolPlayerScratchOfs = 0;
    std::vector<uint32_t> m_vmTeamMirrorOfs;

    struct PlayMatchSetupState {
        int savedActiveIdx = -1;
        int team1HotseatIdx = -1;
        uint32_t origA1 = 0;
        uint32_t origA2 = 0;
        bool didSwitch = false;
    };
    struct H2hPackedTeamMap {
        bool active = false;
        uint32_t vmTeamOfs = 0;
        uint8_t slotToOrd[16]{};
    };
    int m_emergencyLoanDepth = 0;
    std::vector<PlayMatchSetupState> m_playMatchSetupStack;
    std::vector<H2hPackedTeamMap> m_h2hPackMaps;

    // Multi-world state (country shared cups/markets + division calendars/tables)
    bool m_hasActiveWorld = false;
    LeagueWorldKey m_activeWorldKey{};
    bool m_hasLeagueOverlay = false;
    LeagueWorldKey m_leagueOverlayKey{};
    uint16_t m_globalWeek = 0;
    std::array<bool, kNumEuroCups> m_euroCupHostSet{};
    std::array<LeagueWorldKey, kNumEuroCups> m_euroCupHostKeys{};
    std::map<uint8_t, CountryWorldState> m_countryWorlds;
    std::map<LeagueWorldKey, LeagueWorldState> m_leagueWorlds;
    std::map<uint16_t, TeamFile> m_clubTeamCache;

    struct UnifiedGameListRow {
        LeagueWorldKey worldKey{};
        uint16_t calOfs = 0;
        uint16_t slotIdx = 0;
        uint8_t headerBytes[10]{};
        CalendarSlotBytes slotBytes{};
        bool isContinuationRow = false;
        bool isLastSlotOfEntry = true;
        bool isActiveHighlight = false;
        int euroCupIdx = -1;
    };
    struct SavedGameListVmContext {
        bool active = false;
        uint32_t dsegD8CAA[5]{};
        uint16_t dsegD6CDC = 0;
        uint16_t compType0 = 0;
        uint16_t compType1 = 0;
        uint16_t compType2 = 0;
        uint16_t compType3 = 0;
        uint16_t compType4 = 0;
        uint32_t hlHdr = 0;
        uint32_t hlMatchPtr = 0;
    };
    std::vector<UnifiedGameListRow> m_unifiedGameListRows;
    int m_unifiedGameListTopRow = 0;
    int m_unifiedGameListActiveRow = -1;
    SavedGameListVmContext m_savedGameListCtx{};
    void rebuildUnifiedGameListRows();

    void unpackH2hPackedTeam(int playerIdx);
    void unpackAllH2hPackedTeams();
};

void trimTrailingSwosPadding(std::string& s);
void trimTrailingSwosPadding(char *s);
void startCareerMode();
void onInitPlayerCountMenu();
void onSelect1Player();
void onSelect2Players();
void onSelect3Players();
void onSelect4Players();
void onDecreaseCustomPlayers();
void onIncreaseCustomPlayers();
void onSelectCustomPlayers();
void onSelectExitPlayerCount();

