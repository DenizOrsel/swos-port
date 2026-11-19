#include "CareerTest.h"
#include "unitTest.h"
#include "menus.h"
#include "unpackMenu.h"
#include "sdlProcs.h"
#include "swossym.h"
#include "gameLoop.h"
#include "game.h"
#include "options.h"
#include "hotseatCareer.h"
#include "init.h"
#include "file.h"
#include "menuAlloc.h"
#include "continueAbortMenu.h"
#include "mockLog.h"
#include "mockSdlMixer.h"
#include <cstdio>
#include <fstream>

static CareerTest t;

const char *CareerTest::name() const
{
    return "career";
}

const char *CareerTest::displayName() const
{
    return "career mode test";
}

void CareerTest::init()
{
    takeOverInput();
}

void CareerTest::finish()
{
    SWOS_UnitTest::setMenuCallback();
    setGameLoopStartHook();
}

auto CareerTest::getCases() -> CaseList
{
    return {
        { "test career Liverpool", "career-liverpool", {}, bind(&CareerTest::testCareerLiverpool), 1, false },
        { "test career Germany Bayern", "career-germany-bayern", {}, bind(&CareerTest::testCareerGermany), 1, false },
        { "test foreign market player lookup", "career-foreign-market-lookup", {}, bind(&CareerTest::testForeignMarketPlayerLookup), 1, false },
        { "test hotseat initialization and swapping", "career-hotseat-swapping", {}, bind(&CareerTest::testHotseatInitializationAndSwapping), 1, false },
        { "test hotseat inter-player transfer offer", "career-hotseat-transfers", {}, bind(&CareerTest::testHotseatInterPlayerTransferOffer), 1, false },
        { "test hotseat head-to-head fixture", "career-hotseat-h2h", {}, bind(&CareerTest::testHotseatHeadToHeadFixture), 1, false },
        { "test hotseat multi-team selection", "career-hotseat-multi-team", {}, bind(&CareerTest::testHotseatMultiTeamSelection), 1, false },
        { "test hotseat wizard player setup", "career-hotseat-wizard-setup", {}, bind(&CareerTest::testHotseatWizardPlayerSetup), 1, false },
        { "test hotseat team slot assignment", "career-hotseat-slot-assignment", {}, bind(&CareerTest::testHotseatTeamSlotAssignment), 1, false },
        { "test single player career hotseat", "career-hotseat-single-player", {}, bind(&CareerTest::testSinglePlayerCareerHotseat), 1, false },
        { "test hotseat same league validation", "career-hotseat-same-league", {}, bind(&CareerTest::testHotseatSameLeagueValidation), 1, false },
        { "test hotseat per-club starting balance", "career-hotseat-per-club-balance", {}, bind(&CareerTest::testHotseatPerClubStartingBalance), 1, false },
        { "test hotseat head-to-head controls", "career-hotseat-h2h-controls", {}, bind(&CareerTest::testHotseatHeadToHeadControls), 1, false },
        { "test hotseat turn-based transfer queue and counter", "career-hotseat-transfers-queue-counter", {}, bind(&CareerTest::testHotseatTurnBasedTransferQueueAndCounter), 1, false },
        { "test hotseat save and load state", "career-hotseat-save-load", {}, bind(&CareerTest::testHotseatSaveAndLoadState), 1, false },
        { "test hotseat extended context offsets and persistence validation", "career-hotseat-context-persistence-validation", {}, bind(&CareerTest::testHotseatContextExtendedOffsetsAndPersistenceValidation), 1, false },
        { "test hotseat transfer guards and match players sync", "career-hotseat-transfer-guards", {}, bind(&CareerTest::testHotseatTransferGuardsAndMatchPlayers), 1, false },
        { "test hotseat VM fixtures and competition pointer relinking", "career-hotseat-vm-fixtures-relinking", {}, bind(&CareerTest::testHotseatVmFixturesAndPointerRelinking), 1, false },
        { "test hotseat weekly tick calendar turn advance and H2H stats", "career-hotseat-weekly-calendar-h2h", {}, bind(&CareerTest::testHotseatWeeklyTickCalendarTurnAndH2H), 1, false },
        { "test hotseat lifecycle reset and VM hooks", "career-hotseat-lifecycle-hooks", {}, bind(&CareerTest::testHotseatLifecycleResetAndHooks), 1, false },
        { "test hotseat 2P Turkey contest menu and turn switch", "career-hotseat-turkey-contest-menu", {}, bind(&CareerTest::testHotseatTurkeyTwoPlayerContestMenu), 1, false },
        { "test hotseat highlight menu context and match transitions", "career-hotseat-highlight-sync", {}, bind(&CareerTest::testHotseatHighlightMenuContextAndResultTransition), 1, false },
        { "test hotseat special cups labels and concurrent euro cups", "career-hotseat-special-cups-euro", {}, bind(&CareerTest::testHotseatSpecialCupsCompetitionLabelsAndConcurrentEuroCups), 1, false },
        { "test hotseat domestic cup inclusion across divisions", "career-hotseat-domestic-cups-divisions", {}, bind(&CareerTest::testHotseatDomesticCupInclusionPremAndDiv3), 1, false },
        { "test hotseat play match euro cup and transfers active", "career-hotseat-playmatch-euro-transfers", {}, bind(&CareerTest::testHotseatPlayMatchEuroCupAndTransfersActive), 1, false },
        { "test hotseat confirm offer prompt and multi offer", "career-hotseat-confirm-offer-prompt", {}, bind(&CareerTest::testHotseatConfirmOfferPromptAndMultiOffer), 1, false },
        { "test hotseat departure replacement and trialist names", "career-hotseat-departure-trial-replacement", {}, bind(&CareerTest::testHotseatTransferDepartureAndTrialReplacement), 1, false },
        { "test hotseat market exclusion and explicit listing", "career-hotseat-market-exclusion", {}, bind(&CareerTest::testHotseatMarketExclusionAndExplicitListing), 1, false },
        { "test hotseat exchange transfer human and cpu", "career-hotseat-exchange-transfers", {}, bind(&CareerTest::testHotseatExchangeTransferHumanAndCpu), 1, false },
        { "test hotseat trial departure after opponent match", "career-hotseat-trial-departure-2p", {}, bind(&CareerTest::testHotseatTrialDepartureAfterOpponentMatch), 1, false },
        { "test hotseat emergency loan for injured squad", "career-hotseat-emergency-loan", {}, bind(&CareerTest::testHotseatEmergencyLoanForInjuredSquad), 1, false },
        { "test hotseat .CAR and .hot file save and load across euro cups", "career-hotseat-car-load", {}, bind(&CareerTest::testHotseatSaveAndLoadCareerFile), 1, false },
        { "test hotseat end-of-season menu sequence and shared job offer dedup", "career-hotseat-end-season-job-offers", {}, bind(&CareerTest::testHotseatEndOfSeasonMenuSequenceAndSharedJobOfferDedup), 1, false },
        { "test hotseat club switch marks old team CPU red and CPU fixtures", "career-hotseat-club-switch-old-cpu", {}, bind(&CareerTest::testHotseatClubSwitchOldTeamCpuRedAndFixtures), 1, false },
        { "test hotseat 8 players isolated mirrors and V8 save load", "career-hotseat-8players", {}, bind(&CareerTest::testHotseatEightPlayersMirrorsAndSaveV8), 1, false },
        { "test hotseat multi-world country/league multiplexer", "career-hotseat-multi-world-mux", {}, bind(&CareerTest::testHotseatMultiWorldMultiplexer), 1, false },
        { "test hotseat multi-world lockstep and cpu round stepping", "career-hotseat-multi-world-lockstep", {}, bind(&CareerTest::testHotseatMultiWorldLockstep), 1, false },
        { "test hotseat Div 3 to Div 2 split and cross-league job offer", "career-hotseat-div3-div2-split", {}, bind(&CareerTest::testHotseatDiv3ToDiv2SplitAndCrossLeagueJobOffer), 1, false },
        { "test hotseat multi-country career setup, cross-country transfers, V9 save, and season transition", "career-hotseat-multi-country", {}, bind(&CareerTest::testHotseatMultiCountryCareer), 1, false },
    };
}

void CareerTest::testCareerLiverpool()
{
    setPreMatchMenus(false);
    int menuCount = 0;
    SWOS_UnitTest::setMenuCallback([&] {
        menuCount++;
        if (menuCount == 1) {
            // First menu after LoadLiverpool is Chairman memo -> CONTINUE
            selectEntry(6);
            return false;
        }
        if (menuCount == 2) {
            // Contest menu -> PLAY MATCH
            selectEntry(7);
            return false;
        }
        if (menuCount == 3) {
            // Division fixtures screen -> - MATCH -
            selectEntry(3);
            return false;
        }
        if (menuCount == 4) {
            // Play Match screen -> PLAY/COACH MATCH
            swos.pl1Fire = 1;
            selectEntry(9);
            return false;
        }
        SetExitMenuFlag();
        return true;
    });

    int gameLoopFrames = 0;
    setGameLoopStartHook([&] {
        gameLoopFrames++;
        if (gameLoopFrames == 1) {
            // Initial kickoff countdown
            assertTrue(swos.gameState == GameState::kStartingGame);
        }
        if (gameLoopFrames >= 540) {
            assertTrue(swos.gameStatePl == GameState::kWaitingOnPlayer);
            swos.playGame = 0;
        }
    });

    LoadLiverpool();
    StartContest();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testCareerGermany()
{
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    swos.coachOrPlayer = 0;
    D0 = 0x030E;
    InitCareer();
    SWOS_UnitTest::setMenuCallback();
}

extern void cseg_5A666();
extern void LoadPoolplyrDat();

void CareerTest::testForeignMarketPlayerLookup()
{
    LoadPoolplyrDat();
    // Test player from crash report: team_id = 0x0732 (Green Cross, country 50, team 7), player_ord = 14
    D0 = 0x0732;
    D1 = 14;
    cseg_5A666();

    auto ptr = A0.asPtr();
    auto teamId = *reinterpret_cast<const uint16_t *>(ptr);
    auto playerOrd = *reinterpret_cast<const uint8_t *>(ptr + 2);
    const char *teamName = ptr + 3;

    assertEqual(teamId, static_cast<uint16_t>(0x0732));
    assertEqual(playerOrd, static_cast<uint8_t>(14));
    assertEqual(std::string(teamName), std::string("GREEN CROSS"));
}

void CareerTest::testHotseatInitializationAndSwapping()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();
    assertFalse(mgr.isActive());

    // Initialize 2 teams: Liverpool (0x0811) and Arsenal (0x0801)
    std::vector<int16_t> teamIds = { 0x0811, 0x0801 };
    mgr.initializeHotseat(teamIds);

    assertTrue(mgr.isActive());
    assertEqual(mgr.numPlayers(), 2);
    assertEqual(mgr.activePlayerIndex(), 0);

    // Set custom data for Player 0 (Liverpool)
    auto& p0 = mgr.player(0);
    p0.managerName = "ALEX";
    p0.managerSurname = "FERGIE";
    p0.newBalance = 12'500'000;
    mgr.loadSwosContext(p0);

    // Verify VM has Player 0's data
    int32_t vmBalance = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    assertEqual(vmBalance, 12'500'000);
    char vmName[10] = {0};
    std::memcpy(vmName, &SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 8);
    assertEqual(std::string(vmName), std::string("ALEX"));

    // Check turn end constraint: cannot end turn before match is played!
    assertFalse(mgr.canEndTurn());
    assertFalse(mgr.endTurn());

    // Mark match played
    mgr.onMatchCompleted();
    assertTrue(mgr.canEndTurn());

    // End Player 0's turn -> switches to Player 1!
    bool newRound = mgr.endTurn();
    assertFalse(newRound); // Round not finished yet, Player 1 still needs to play!
    assertEqual(mgr.activePlayerIndex(), 1);

    // Set custom data for Player 1 (Arsenal)
    auto& p1 = mgr.player(1);
    p1.managerName = "ARSENE";
    p1.managerSurname = "WENGER";
    p1.newBalance = 8'200'000;
    mgr.loadSwosContext(p1);

    // Verify VM has Player 1's data
    vmBalance = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    assertEqual(vmBalance, 8'200'000);
    std::memset(vmName, 0, sizeof(vmName));
    std::memcpy(vmName, &SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 8);
    assertEqual(std::string(vmName), std::string("ARSENE"));

    // Player 1 plays match and ends turn
    mgr.onMatchCompleted();
    newRound = mgr.endTurn();
    assertTrue(newRound); // Both players finished -> new round begins!
    assertEqual(mgr.activePlayerIndex(), 0);

    // Verify VM context faithfully swapped back to Player 0!
    vmBalance = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    assertEqual(vmBalance, 12'500'000);
    std::memset(vmName, 0, sizeof(vmName));
    std::memcpy(vmName, &SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 8);
    assertEqual(std::string(vmName), std::string("ALEX"));
}

void CareerTest::testHotseatInterPlayerTransferOffer()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();
    std::vector<int16_t> teamIds = { 0x0811, 0x0801 };
    mgr.initializeHotseat(teamIds);

    mgr.player(0).newBalance = 10'000'000;
    mgr.player(0).careerNumPlayers = 16;
    mgr.player(1).newBalance = 5'000'000;
    mgr.player(1).careerNumPlayers = 17;

    // Player 0 offers Â£2,000,000 to Player 1 for player #5
    HotseatCareerManager::TransferOffer offer;
    offer.fromPlayerIndex = 0;
    offer.toPlayerIndex = 1;
    offer.playerOrdinal = 5;
    offer.offerAmount = 2'000'000;
    int offerId1 = mgr.queueTransferOffer(offer);

    auto pendingForP1 = mgr.getPendingOffersForPlayer(1);
    assertEqual(static_cast<int>(pendingForP1.size()), 1);
    assertEqual(pendingForP1[0].offerAmount, 2'000'000);

    // No index fallback: id 0 must not resolve offer 1
    assertTrue(mgr.getTransferOfferById(0) == nullptr);

    // Player 1 accepts offer
    assertTrue(mgr.resolveTransferOffer(offerId1, true));

    assertEqual(mgr.player(0).newBalance, 8'000'000);
    assertEqual(mgr.player(1).newBalance, 7'000'000);
    assertEqual(mgr.player(0).careerNumPlayers, 17);
    assertEqual(mgr.player(1).careerNumPlayers, 16);

    // Player 0 offers Â£1,000,000 to Player 1 for player #6 (explicitly rejected)
    HotseatCareerManager::TransferOffer offer2;
    offer2.fromPlayerIndex = 0;
    offer2.toPlayerIndex = 1;
    offer2.playerOrdinal = 6;
    offer2.offerAmount = 1'000'000;
    int offerId2 = mgr.queueTransferOffer(offer2);
    assertTrue(mgr.resolveTransferOffer(offerId2, false));

    assertEqual(mgr.player(0).newBalance, 8'000'000);
    assertEqual(mgr.player(1).newBalance, 7'000'000);
    assertEqual(static_cast<int>(mgr.getPendingOffersForPlayer(1).size()), 0);
}

void CareerTest::testHotseatHeadToHeadFixture()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();
    std::vector<int16_t> teamIds = { 0x0811, 0x0801, 0x0802 }; // Liverpool, Arsenal, Chelsea
    mgr.initializeHotseat(teamIds);

    mgr.player(0).teamName = "LIVERPOOL";
    mgr.player(1).teamName = "ARSENAL";
    mgr.player(2).teamName = "CHELSEA";

    // Team name lookup
    assertEqual(mgr.findHotseatPlayerByTeamName("LIVERPOOL"), 0);
    assertEqual(mgr.findHotseatPlayerByTeamName("ARSENAL*"), 1);
    assertEqual(mgr.findHotseatPlayerByTeamName("CHELSEA  "), 2);
    assertEqual(mgr.findHotseatPlayerByTeamName("MANCHESTER UTD"), -1);

    // Team ID lookup
    assertEqual(mgr.findHotseatPlayerByTeamId(0x0811), 0);
    assertEqual(mgr.findHotseatPlayerByTeamId(0x0801), 1);
    assertEqual(mgr.findHotseatPlayerByTeamId(0x0802), 2);
    assertEqual(mgr.findHotseatPlayerByTeamId(0x0999), -1);

    // Head-to-Head match completion marking
    assertFalse(mgr.player(0).matchPlayedThisTurn);
    assertFalse(mgr.player(1).matchPlayedThisTurn);
    assertFalse(mgr.player(2).matchPlayedThisTurn);

    // Simulate Liverpool vs Arsenal head-to-head match completed
    mgr.markPlayerMatchCompleted(0);
    mgr.markPlayerMatchCompleted(1);

    assertTrue(mgr.player(0).matchPlayedThisTurn);
    assertTrue(mgr.player(1).matchPlayedThisTurn);
    assertFalse(mgr.player(2).matchPlayedThisTurn);
}

void CareerTest::testHotseatMultiTeamSelection()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    // Simulate 3 teams selected from the same division
    std::vector<int16_t> teamIds = { 0x0811, 0x0801, 0x0805 }; // Liverpool, Arsenal, Everton
    mgr.initializeHotseat(teamIds);

    assertEqual(mgr.numPlayers(), 3);
    assertTrue(mgr.isActive());
    assertEqual(mgr.activePlayerIndex(), 0);

    // Give each player custom balances & team names
    mgr.player(0).teamName = "LIVERPOOL";
    mgr.player(0).newBalance = 15'000'000;
    mgr.player(1).teamName = "ARSENAL";
    mgr.player(1).newBalance = 10'000'000;
    mgr.player(2).teamName = "EVERTON";
    mgr.player(2).newBalance = 7'000'000;

    // Player 0 turn
    assertFalse(mgr.canEndTurn());
    mgr.onMatchCompleted();
    assertTrue(mgr.canEndTurn());

    bool newRound = mgr.endTurn();
    assertFalse(newRound); // Round 1 not over yet
    assertEqual(mgr.activePlayerIndex(), 1);

    // Player 1 turn
    assertFalse(mgr.canEndTurn());
    mgr.onMatchCompleted();
    assertTrue(mgr.canEndTurn());

    newRound = mgr.endTurn();
    assertFalse(newRound); // Round 1 not over yet
    assertEqual(mgr.activePlayerIndex(), 2);

    // Player 2 turn
    assertFalse(mgr.canEndTurn());
    mgr.onMatchCompleted();
    assertTrue(mgr.canEndTurn());

    newRound = mgr.endTurn();
    assertTrue(newRound); // All 3 players completed! New round begins!
    assertEqual(mgr.activePlayerIndex(), 0);

    // Verify all players' turn tracking reset for the new round
    assertFalse(mgr.player(0).matchPlayedThisTurn);
    assertFalse(mgr.player(1).matchPlayedThisTurn);
    assertFalse(mgr.player(2).matchPlayedThisTurn);
}

void CareerTest::testHotseatWizardPlayerSetup()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    // Setup wizard for 3 players
    mgr.resetWizard(3);
    assertTrue(mgr.isWizardActive());
    assertEqual(mgr.wizardTotalPlayers(), 3);
    assertEqual(mgr.wizardCurrentPlayer(), 0);

    // Player 1 configuration
    auto& p1 = mgr.wizardPlayer(0);
    p1.name = "ALEX";
    p1.surname = "FERGUSON";
    p1.nationality = 8;
    p1.coachOrPlayer = 1;
    mgr.setWizardAdvanced(true);
    assertTrue(mgr.wizardAdvanced());

    // Advance to Player 2
    mgr.setWizardCurrentPlayer(1);
    mgr.setWizardAdvanced(false);
    auto& p2 = mgr.wizardPlayer(1);
    p2.name = "ARSENE";
    p2.surname = "WENGER";
    p2.nationality = 3;
    p2.coachOrPlayer = 1;

    // Advance to Player 3
    mgr.setWizardCurrentPlayer(2);
    auto& p3 = mgr.wizardPlayer(2);
    p3.name = "JOSE";
    p3.surname = "MOURINHO";
    p3.nationality = 14;
    p3.coachOrPlayer = 1;

    // Verify stored wizard data
    assertEqual(mgr.wizardPlayer(0).name, "ALEX");
    assertEqual(mgr.wizardPlayer(0).surname, "FERGUSON");
    assertEqual(mgr.wizardPlayer(0).nationality, 8);
    assertEqual(mgr.wizardPlayer(0).coachOrPlayer, 1);

    assertEqual(mgr.wizardPlayer(1).name, "ARSENE");
    assertEqual(mgr.wizardPlayer(1).surname, "WENGER");
    assertEqual(mgr.wizardPlayer(1).nationality, 3);

    assertEqual(mgr.wizardPlayer(2).name, "JOSE");
    assertEqual(mgr.wizardPlayer(2).surname, "MOURINHO");
    assertEqual(mgr.wizardPlayer(2).nationality, 14);
}

void CareerTest::testHotseatTeamSlotAssignment()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    mgr.setSelectingCareerTeams(true);
    mgr.setCareerNumPlayers(3);
    mgr.unassignAllTeamSlots();

    assertEqual(mgr.getAssignedSlotCount(), 0);
    assertEqual(mgr.getNextAvailableSlot(), 0);

    // Player 1 picks Liverpool (0x0811) -> Slot 0 (P1)
    mgr.assignTeamSlot(0x0811, 0);
    assertEqual(mgr.getTeamSlot(0x0811), 0);
    assertEqual(mgr.getTeamForSlot(0), 0x0811);
    assertEqual(mgr.getAssignedSlotCount(), 1);
    assertEqual(mgr.getNextAvailableSlot(), 1); // Slot 1 (P2) is next

    // Player 2 picks Arsenal (0x0801) -> Slot 1 (P2)
    mgr.assignTeamSlot(0x0801, 1);
    assertEqual(mgr.getTeamSlot(0x0801), 1);
    assertEqual(mgr.getTeamForSlot(1), 0x0801);
    assertEqual(mgr.getAssignedSlotCount(), 2);
    assertEqual(mgr.getNextAvailableSlot(), 2); // Slot 2 (P3) is next

    // Player 3 picks Chelsea (0x0802) -> Slot 2 (P3)
    mgr.assignTeamSlot(0x0802, 2);
    assertEqual(mgr.getTeamSlot(0x0802), 2);
    assertEqual(mgr.getTeamForSlot(2), 0x0802);
    assertEqual(mgr.getAssignedSlotCount(), 3);
    assertEqual(mgr.getNextAvailableSlot(), -1); // All 3 slots assigned!

    // Player 2 clicks Arsenal again -> unassigns slot 1
    mgr.unassignTeamSlot(0x0801);
    assertEqual(mgr.getTeamSlot(0x0801), -1);
    assertEqual(mgr.getAssignedSlotCount(), 2);
    assertEqual(mgr.getNextAvailableSlot(), 1); // Slot 1 is now available again!

    // Player 2 picks Everton (0x0805) -> Slot 1 (P2)
    mgr.assignTeamSlot(0x0805, 1);
    assertEqual(mgr.getTeamSlot(0x0805), 1);
    assertEqual(mgr.getAssignedSlotCount(), 3);
    assertEqual(mgr.getNextAvailableSlot(), -1);

    // Verify final ordering: P1 = Liverpool, P2 = Everton, P3 = Chelsea
    assertEqual(mgr.getTeamForSlot(0), 0x0811);
    assertEqual(mgr.getTeamForSlot(1), 0x0805);
    assertEqual(mgr.getTeamForSlot(2), 0x0802);
}

void CareerTest::testSinglePlayerCareerHotseat()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    std::vector<int16_t> teamIds = { 0x0811 }; // 1 player: Liverpool
    mgr.initializeHotseat(teamIds);

    assertEqual(mgr.numPlayers(), 1);
    assertTrue(mgr.isActive());
    assertEqual(mgr.activePlayerIndex(), 0);

    assertFalse(mgr.canEndTurn());
    mgr.onMatchCompleted();
    assertTrue(mgr.canEndTurn());

    bool newRound = mgr.endTurn();
    assertTrue(newRound); // Single player immediately finishes round
    assertEqual(mgr.activePlayerIndex(), 0);
    assertFalse(mgr.player(0).matchPlayedThisTurn);
}

void CareerTest::testHotseatSameLeagueValidation()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();
    mgr.setSelectingCareerTeams(true);
    mgr.setCareerNumPlayers(3);
    mgr.unassignAllTeamSlots();

    // 0 assigned: returns true
    assertTrue(mgr.areAllAssignedTeamsInSameLeague());

    // Create mock TeamFile objects
    TeamFile tf1 = {0};
    tf1.teamFileNo = 8; // England
    tf1.league = 0;     // Premier League
    mgr.assignTeamSlot(0x0811, 0, &tf1); // Liverpool

    // 1 assigned: returns true
    assertTrue(mgr.areAllAssignedTeamsInSameLeague());

    TeamFile tf2 = {0};
    tf2.teamFileNo = 8; // England
    tf2.league = 0;     // Premier League
    mgr.assignTeamSlot(0x0801, 1, &tf2); // Arsenal

    // 2 assigned, same country & league: returns true
    assertTrue(mgr.areAllAssignedTeamsInSameLeague());

    // 3rd team from different league in same country (e.g. Division 1)
    TeamFile tf3 = {0};
    tf3.teamFileNo = 8; // England
    tf3.league = 1;     // Division 1
    mgr.assignTeamSlot(0x0805, 2, &tf3);

    // Should fail validation!
    assertFalse(mgr.areAllAssignedTeamsInSameLeague());

    // 3rd team from different country (e.g. Italy Serie A)
    tf3.teamFileNo = 12; // Italy
    tf3.league = 0;      // Serie A
    mgr.assignTeamSlot(0x0805, 2, &tf3);

    // Should fail validation!
    assertFalse(mgr.areAllAssignedTeamsInSameLeague());

    // Fix 3rd team to same country and league
    tf3.teamFileNo = 8;
    tf3.league = 0;
    mgr.assignTeamSlot(0x0805, 2, &tf3);

    // Now all 3 are in the same league
    assertTrue(mgr.areAllAssignedTeamsInSameLeague());
}

void CareerTest::testHotseatPerClubStartingBalance()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    std::vector<int16_t> teamIds = { 0x0811, 0x0801 }; // Liverpool & Arsenal
    mgr.initializeHotseat(teamIds);

    // Verify context separation and buffer sizes
    assertEqual(mgr.numPlayers(), 2);
    auto& p1 = mgr.player(0);
    auto& p2 = mgr.player(1);

    // Set starting balances
    p1.startingBalance = 15'000'000;
    p1.newBalance = 15'000'000;
    p1.oldBalance = 15'000'000;
    p1.careerNumPlayers = 18; // 16 main + 2 reserves

    p2.startingBalance = 10'000'000;
    p2.newBalance = 10'000'000;
    p2.oldBalance = 10'000'000;
    p2.careerNumPlayers = 16; // 16 main + 0 reserves

    assertEqual(p1.startingBalance, 15'000'000);
    assertEqual(p1.newBalance, 15'000'000);
    assertEqual(p1.careerNumPlayers, 18);

    assertEqual(p2.startingBalance, 10'000'000);
    assertEqual(p2.newBalance, 10'000'000);
    assertEqual(p2.careerNumPlayers, 16);

    // Verify careerTeam buffer size constant
    assertEqual(sizeof(p1.careerTeam), 1292);
    assertEqual(sizeof(p2.careerTeam), 1292);

    // Write dummy player data into squad reserve slot
    p1.careerTeam[76] = 0xAA;
    p2.careerTeam[76] = 0xBB;
    assertEqual(p1.careerTeam[76], 0xAA);
    assertEqual(p2.careerTeam[76], 0xBB);
}

void CareerTest::testHotseatHeadToHeadControls()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    std::vector<int16_t> teamIds = { 0x0811, 0x0801, 0x0802 }; // Liverpool (P1), Arsenal (P2), Chelsea (P3)
    mgr.initializeHotseat(teamIds);

    mgr.player(0).teamName = "LIVERPOOL";
    mgr.player(0).coachOrPlayer = 1; // player-coach
    mgr.player(1).teamName = "ARSENAL";
    mgr.player(1).coachOrPlayer = 0; // coach only
    mgr.player(2).teamName = "CHELSEA";
    mgr.player(2).coachOrPlayer = 1;

    // Verify lookup of both teams in a fixture
    int p1Idx = mgr.findHotseatPlayerByTeamName("LIVERPOOL");
    int p2Idx = mgr.findHotseatPlayerByTeamName("ARSENAL");
    int cpuIdx = mgr.findHotseatPlayerByTeamName("MANCHESTER UTD");

    assertEqual(p1Idx, 0);
    assertEqual(p2Idx, 1);
    assertEqual(cpuIdx, -1);

    // In a human vs human fixture:
    assertTrue(p1Idx >= 0 && p2Idx >= 0);

    // Verify match completion logic:
    // Before match played:
    assertFalse(mgr.player(0).matchPlayedThisTurn);
    assertFalse(mgr.player(1).matchPlayedThisTurn);
    assertFalse(mgr.player(2).matchPlayedThisTurn);

    // Both players should be marked completed once match is played:
    mgr.markPlayerMatchCompleted(p1Idx);
    mgr.markPlayerMatchCompleted(p2Idx);

    assertTrue(mgr.player(0).matchPlayedThisTurn);
    assertTrue(mgr.player(1).matchPlayedThisTurn);
    assertFalse(mgr.player(2).matchPlayedThisTurn); // Chelsea hasn't played yet
}

void CareerTest::testHotseatTurnBasedTransferQueueAndCounter()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    std::vector<int16_t> teamIds = { 0x0811, 0x0801 }; // Liverpool (P0), Arsenal (P1)
    mgr.initializeHotseat(teamIds);

    mgr.player(0).teamName = "LIVERPOOL";
    mgr.player(0).newBalance = 10'000'000;
    mgr.player(0).careerNumPlayers = 16;

    mgr.player(1).teamName = "ARSENAL";
    mgr.player(1).newBalance = 5'000'000;
    mgr.player(1).careerNumPlayers = 18;

    // Populate seller's squad with dummy player data
    constexpr size_t kHeaderSize = 76;
    constexpr size_t kPlayerSize = 38;
    mgr.player(1).careerTeam[kHeaderSize + 5 * kPlayerSize] = 0x55; // P1 player 5
    mgr.player(1).careerTeam[kHeaderSize + 17 * kPlayerSize] = 0x77; // P1 reserve player (17th, i.e. sellerCount - 1)

    // Player 0 submits bid for Player 1's player
    HotseatCareerManager::TransferOffer offer;
    offer.fromPlayerIndex = 0;
    offer.toPlayerIndex = 1;
    offer.playerOrdinal = 5;
    snprintf(offer.playerName, sizeof(offer.playerName), "WRIGHT");
    offer.offerAmount = 2'000'000;

    int offerId = mgr.queueTransferOffer(offer);
    assertTrue(offerId > 0);

    // Offer should be pending for Player 1
    auto pendingP1 = mgr.getPendingOffersForPlayer(1);
    assertEqual(static_cast<int>(pendingP1.size()), 1);
    assertEqual(pendingP1[0].offerAmount, 2'000'000);
    assertEqual(std::string(pendingP1[0].playerName), "WRIGHT");
    assertFalse(pendingP1[0].isCounterOffer);

    // Player 0 should have 0 pending offers
    auto pendingP0 = mgr.getPendingOffersForPlayer(0);
    assertEqual(static_cast<int>(pendingP0.size()), 0);

    // Player 1 counters with Â£2.5M (+25%)
    mgr.counterTransferOffer(offerId, 2'500'000);

    // Now offer is inverted: toPlayerIndex is 0 (Player 0), isCounterOffer is true
    pendingP1 = mgr.getPendingOffersForPlayer(1);
    assertEqual(static_cast<int>(pendingP1.size()), 0);

    pendingP0 = mgr.getPendingOffersForPlayer(0);
    assertEqual(static_cast<int>(pendingP0.size()), 1);
    assertEqual(pendingP0[0].offerAmount, 2'500'000);
    assertTrue(pendingP0[0].isCounterOffer);
    assertEqual(pendingP0[0].fromPlayerIndex, 1);
    assertEqual(pendingP0[0].toPlayerIndex, 0);

    // Player 0 accepts counter offer
    mgr.resolveTransferOffer(offerId, true);

    // Balances should be updated:
    // P0 was buyer: 10M - 2.5M = 7.5M
    // P1 was seller: 5M + 2.5M = 7.5M
    assertEqual(mgr.player(0).newBalance, 7'500'000);
    assertEqual(mgr.player(1).newBalance, 7'500'000);

    // P0 squad count increased to 17
    assertEqual(mgr.player(0).careerNumPlayers, 17);
    // P1 squad count decreased to 17
    assertEqual(mgr.player(1).careerNumPlayers, 17);

    // Bought player transferred to P0
    assertEqual(mgr.player(0).careerTeam[kHeaderSize + 16 * kPlayerSize], 0x55);
    // Seller's reserve moved to player 5 slot
    assertEqual(mgr.player(1).careerTeam[kHeaderSize + 5 * kPlayerSize], 0x77);

    // No pending offers remaining
    assertEqual(static_cast<int>(mgr.getPendingOffersForPlayer(0).size()), 0);
    assertEqual(static_cast<int>(mgr.getPendingOffersForPlayer(1).size()), 0);
}

void CareerTest::testHotseatSaveAndLoadState()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    std::vector<int16_t> teamIds = { 0x0811, 0x0801, 0x0802 };
    mgr.initializeHotseat(teamIds);

    mgr.player(0).teamName = "LIVERPOOL";
    mgr.player(0).managerName = "KENNY";
    mgr.player(0).managerSurname = "DALGLISH";
    mgr.player(0).coachOrPlayer = 1;
    mgr.player(0).nationality = 9; // Scottish
    mgr.player(0).newBalance = 12'345'678;
    mgr.player(0).startingBalance = 8'000'000;
    mgr.player(0).careerNumPlayers = 19;
    mgr.player(0).careerTeam[100] = 0x42;

    mgr.player(1).teamName = "ARSENAL";
    mgr.player(1).managerName = "GEORGE";
    mgr.player(1).managerSurname = "GRAHAM";
    mgr.player(1).coachOrPlayer = 0;
    mgr.player(1).nationality = 9;
    mgr.player(1).newBalance = 9'876'543;
    mgr.player(1).careerNumPlayers = 16;
    mgr.player(1).careerTeam[100] = 0x84;

    mgr.player(2).teamName = "CHELSEA";
    mgr.player(2).managerName = "GLENN";
    mgr.player(2).managerSurname = "HODDLE";
    mgr.player(2).coachOrPlayer = 1;
    mgr.player(2).nationality = 8;
    mgr.player(2).newBalance = 4'567'890;
    mgr.player(2).careerNumPlayers = 17;

    // Queue a transfer offer
    HotseatCareerManager::TransferOffer offer;
    offer.fromPlayerIndex = 0;
    offer.toPlayerIndex = 1;
    offer.playerOrdinal = 3;
    snprintf(offer.playerName, sizeof(offer.playerName), "ADAMS");
    offer.offerAmount = 3'500'000;
    mgr.queueTransferOffer(offer);

    const std::string testSavePath = "test_hotseat_save.hot";
    mgr.loadSwosContext(mgr.player(0));
    bool saved = mgr.saveToFile(testSavePath);
    assertTrue(saved);

    // Reset manager completely
    mgr.reset();
    assertFalse(mgr.isActive());
    assertEqual(mgr.numPlayers(), 0);

    // Load state back
    bool loaded = mgr.loadFromFile(testSavePath);
    assertTrue(loaded);
    assertTrue(mgr.isActive());
    assertEqual(mgr.numPlayers(), 3);
    assertEqual(mgr.activePlayerIndex(), 0);

    // Verify Player 0 restored
    assertEqual(mgr.player(0).teamName, "LIVERPOOL");
    assertEqual(mgr.player(0).managerName, "KENNY");
    assertEqual(mgr.player(0).managerSurname, "DALGLISH");
    assertEqual(mgr.player(0).coachOrPlayer, 1);
    assertEqual(mgr.player(0).nationality, 9);
    assertEqual(mgr.player(0).newBalance, 12'345'678);
    assertEqual(mgr.player(0).startingBalance, 8'000'000);
    assertEqual(mgr.player(0).careerNumPlayers, 19);
    assertEqual(mgr.player(0).careerTeam[100], 0x42);

    // Verify Player 1 restored
    assertEqual(mgr.player(1).teamName, "ARSENAL");
    assertEqual(mgr.player(1).managerName, "GEORGE");
    assertEqual(mgr.player(1).managerSurname, "GRAHAM");
    assertEqual(mgr.player(1).coachOrPlayer, 0);
    assertEqual(mgr.player(1).nationality, 9);
    assertEqual(mgr.player(1).newBalance, 9'876'543);
    assertEqual(mgr.player(1).careerNumPlayers, 16);
    assertEqual(mgr.player(1).careerTeam[100], 0x84);

    // Verify Player 2 restored
    assertEqual(mgr.player(2).teamName, "CHELSEA");
    assertEqual(mgr.player(2).managerName, "GLENN");
    assertEqual(mgr.player(2).managerSurname, "HODDLE");
    assertEqual(mgr.player(2).coachOrPlayer, 1);
    assertEqual(mgr.player(2).nationality, 8);
    assertEqual(mgr.player(2).newBalance, 4'567'890);
    assertEqual(mgr.player(2).careerNumPlayers, 17);

    // Verify restored transfer offer
    auto pending = mgr.getPendingOffersForPlayer(1);
    assertEqual(static_cast<int>(pending.size()), 1);
    assertEqual(pending[0].fromPlayerIndex, 0);
    assertEqual(pending[0].toPlayerIndex, 1);
    assertEqual(pending[0].playerOrdinal, 3);
    assertEqual(std::string(pending[0].playerName), "ADAMS");
    assertEqual(pending[0].offerAmount, 3'500'000);

    // Clean up temporary file
    std::remove(testSavePath.c_str());
}

void CareerTest::testHotseatContextExtendedOffsetsAndPersistenceValidation()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    std::vector<int16_t> teamIds = { 0x0811, 0x0801 };
    mgr.initializeHotseat(teamIds);

    auto& p0 = mgr.player(0);
    p0.mrOrMs = 1;
    p0.careerTeamLeague = 2;
    p0.currentSeasonShowing = 3;
    p0.seasonPlaying = 97;
    p0.stayingInOldClub = 0;
    p0.retired = 0;
    p0.clubBusinessActive = 1;
    p0.gamesPlayed = 42;
    p0.numWins = 25;
    p0.numDrawn = 10;
    p0.numLost = 7;
    p0.goalsScored = 70;
    p0.goalsTaken = 33;
    p0.isNationalSelector = 1;
    p0.timeToNegotiate = 2;
    for (int i = 0; i < 16; ++i)
        p0.currentMatchPlayers[i] = static_cast<uint8_t>(i);

    mgr.loadSwosContext(p0);
    assertEqual(std::string(reinterpret_cast<const char *>(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer])), std::string("MS"));
    assertEqual(*reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]), 2);
    assertEqual(*reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]), 3);
    assertEqual(SwosVM::g_memByte[CareerOffsets::kSeasonPlaying], 97);
    assertEqual(*reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kClubBusinessActive]), 1);
    assertEqual(*reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGamesPlayed]), 42);
    assertEqual(SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 15], 15);

    const std::string path = "test_hotseat_ext.hot";
    assertTrue(mgr.saveToFile(path));

    mgr.reset();
    assertTrue(mgr.loadFromFile(path));
    assertEqual(mgr.player(0).careerTeamLeague, 2);
    assertEqual(mgr.player(0).currentSeasonShowing, 3);
    assertEqual(mgr.player(0).seasonPlaying, 97);
    assertEqual(mgr.player(0).gamesPlayed, 42);
    assertEqual(mgr.player(0).numWins, 25);
    assertEqual(mgr.player(0).currentMatchPlayers[15], 15);

    // Corrupt header magic must fail cleanly and leave manager reset
    {
        LogSilencer silencer;
        {
            std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
            const char badData[] = "NOT_A_HOTSEAT_FILE";
            corrupt.write(badData, sizeof(badData));
        }
        assertFalse(mgr.loadFromFile(path));
        assertFalse(mgr.isActive());

        // Truncated valid V3 file must fail and reset manager
        {
            std::ofstream trunc(path, std::ios::binary | std::ios::trunc);
            char partial[36] = {0};
            std::memcpy(partial, "SWOS_HOTSEAT_V3", 15);
            int32_t two = 2;
            std::memcpy(partial + 16, &two, sizeof(two));
            trunc.write(partial, sizeof(partial));
        }
        assertFalse(mgr.loadFromFile(path));
        assertFalse(mgr.isActive());
    }

    std::remove(path.c_str());
}

void CareerTest::testHotseatTransferGuardsAndMatchPlayers()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    std::vector<int16_t> teamIds = { 0x0811, 0x0801 };
    mgr.initializeHotseat(teamIds);

    auto& buyer = mgr.player(0);
    auto& seller = mgr.player(1);
    buyer.newBalance = 5'000'000;
    buyer.careerNumPlayers = 16;
    for (int i = 0; i < 16; ++i)
        buyer.currentMatchPlayers[i] = static_cast<uint8_t>(i);

    // 1. Seller at minimum 16 players must NOT lose or clone a player
    seller.careerNumPlayers = 16;
    for (int i = 0; i < 16; ++i)
        seller.currentMatchPlayers[i] = static_cast<uint8_t>(i);
    seller.careerTeam[sizeof(TeamFileHeader) + 3 * sizeof(PlayerFile)] = 0x33;

    HotseatCareerManager::TransferOffer minSquadOffer;
    minSquadOffer.fromPlayerIndex = 0;
    minSquadOffer.toPlayerIndex = 1;
    minSquadOffer.playerOrdinal = 3;
    minSquadOffer.offerAmount = 1'000'000;
    int minId = mgr.queueTransferOffer(minSquadOffer);
    assertFalse(mgr.canAcceptTransferOffer(minId));
    assertFalse(mgr.resolveTransferOffer(minId, true));
    assertEqual(buyer.careerNumPlayers, 16);
    assertEqual(seller.careerNumPlayers, 16);
    assertEqual(buyer.newBalance, 5'000'000);

    // 2. Counter-offer exceeding buyer funds must fail safely
    seller.careerNumPlayers = 18;
    for (int i = 0; i < 18; ++i)
        seller.currentMatchPlayers[i] = static_cast<uint8_t>(i);
    seller.careerTeam[sizeof(TeamFileHeader) + 3 * sizeof(PlayerFile)] = 0x44;
    seller.careerTeam[sizeof(TeamFileHeader) + 17 * sizeof(PlayerFile)] = 0x88;

    HotseatCareerManager::TransferOffer counterTooHigh;
    counterTooHigh.fromPlayerIndex = 0;
    counterTooHigh.toPlayerIndex = 1;
    counterTooHigh.playerOrdinal = 3;
    counterTooHigh.offerAmount = 4'000'000;
    int highId = mgr.queueTransferOffer(counterTooHigh);
    mgr.counterTransferOffer(highId, 6'000'000); // Buyer (P0) only has 5M
    assertFalse(mgr.canAcceptTransferOffer(highId));
    assertFalse(mgr.resolveTransferOffer(highId, true));
    assertEqual(buyer.newBalance, 5'000'000);
    assertEqual(seller.careerNumPlayers, 18);

    // 3. Valid transfer updates buyer/seller squads and currentMatchPlayers mapping
    HotseatCareerManager::TransferOffer validOffer;
    validOffer.fromPlayerIndex = 0;
    validOffer.toPlayerIndex = 1;
    validOffer.playerOrdinal = 3;
    validOffer.offerAmount = 2'000'000;
    int validId = mgr.queueTransferOffer(validOffer);
    assertTrue(mgr.canAcceptTransferOffer(validId));
    assertTrue(mgr.resolveTransferOffer(validId, true));

    assertEqual(buyer.careerNumPlayers, 17);
    assertEqual(buyer.currentMatchPlayers[16], 16);
    assertEqual(buyer.careerTeam[sizeof(TeamFileHeader) + 16 * sizeof(PlayerFile)], 0x44);

    assertEqual(seller.careerNumPlayers, 17);
    assertEqual(seller.careerTeam[sizeof(TeamFileHeader) + 3 * sizeof(PlayerFile)], 0x88);
    // Seller currentMatchPlayers must have removed 3 and remapped moved reserve 17 -> 3
    bool has3 = false;
    bool has17 = false;
    for (int i = 0; i < 17; ++i) {
        if (seller.currentMatchPlayers[i] == 3) has3 = true;
        if (seller.currentMatchPlayers[i] == 17) has17 = true;
    }
    assertTrue(has3);
    assertFalse(has17);
}

void CareerTest::testHotseatVmFixturesAndPointerRelinking()
{
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    // First discover a second real league team alongside Bayern Munich (0x030E)
    swos.coachOrPlayer = 0;
    D0 = 0x030E;
    InitCareer();

    int16_t team0 = 0x030E;
    int16_t team1 = 0;
    TeamFile team0File = {};
    TeamFile team1File = {};

    uint32_t compBufOfs = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers]);
    assertTrue(compBufOfs != 0);
    int32_t leagueTeamsCount = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[compBufOfs + 49]);
    for (int k = 0; k < leagueTeamsCount && k < 64; ++k) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[compBufOfs + 301 + k * 2]);
        if (tid != static_cast<uint16_t>(team0)) {
            team1 = static_cast<int16_t>(tid);
            break;
        }
    }
    assertTrue(team1 != 0);

    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    assertTrue(selTeams != nullptr);
    for (int tIdx = 0; tIdx < swos.g_numSelectedTeams; ++tIdx) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[tIdx]);
        if (tid == static_cast<uint16_t>(team0))
            team0File = selTeams[tIdx];
        if (tid == static_cast<uint16_t>(team1))
            team1File = selTeams[tIdx];
    }

    // Re-initialize as 2-player hotseat career using both clubs
    mgr.initializeHotseat({ team0, team1 });
    std::memcpy(mgr.player(0).careerTeam, &team0File, sizeof(TeamFile));
    std::memcpy(mgr.player(1).careerTeam, &team1File, sizeof(TeamFile));
    mgr.player(0).coachOrPlayer = 1; // Player-Coach
    mgr.player(1).coachOrPlayer = 0; // Coach
    mgr.player(0).teamName = team0File.teamName;
    mgr.player(1).teamName = team1File.teamName;
    mgr.setInitializingSeason(true);

    swos.coachOrPlayer = 1;
    swos.g_gameType = kGameTypeCareer;
    D0 = team0;
    InitCareer();
    swos.g_gameType = kGameTypeCareer;
    mgr.saveCurrentSwosContext(mgr.player(0));
    mgr.setInitializingSeason(false);

    // 1. Verify cseg_8B8B8_OnEnter flagged BOTH clubs human-controlled in g_selectedTeams
    selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    int foundT0Controls = -1;
    int foundT1Controls = -1;
    for (int tIdx = 0; tIdx < swos.g_numSelectedTeams; ++tIdx) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[tIdx]);
        if (tid == static_cast<uint16_t>(team0))
            foundT0Controls = selTeams[tIdx].teamControls;
        if (tid == static_cast<uint16_t>(team1))
            foundT1Controls = selTeams[tIdx].teamControls;
    }
    assertEqual(foundT0Controls, static_cast<int>(kPlayerCoach));
    assertEqual(foundT1Controls, static_cast<int>(kCoach));

    // 2. Verify dseg_DB3E6 preserves fixed 22-byte stride without overflow and mgr.calendarSlots()
    // captures human fixtures for BOTH team0 and team1
    uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    assertTrue(totalEntries > 0);
    int team0Slots = 0;
    int team1Slots = 0;
    size_t scanOfs = CareerOffsets::kDsegDB3E6;
    const auto& calSlots = mgr.calendarSlots();
    for (uint16_t e = 0; e < totalEntries && scanOfs < CareerOffsets::kDsegDB3E6 + 1870; ++e) {
        uint16_t len = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[scanOfs + 2]);
        assertEqual(len, 22);
        uint8_t numHuman = SwosVM::g_memByte[scanOfs + 5];
        uint16_t calOfs = static_cast<uint16_t>(scanOfs - CareerOffsets::kDsegDB3E6);
        auto it = calSlots.find(calOfs);
        if (numHuman > 0 && it != calSlots.end()) {
            assertEqual(static_cast<int>(it->second.size()), static_cast<int>(numHuman));
            for (const auto& slotBytes : it->second) {
                uint16_t a = *reinterpret_cast<const uint16_t *>(&slotBytes[2]);
                uint16_t b = *reinterpret_cast<const uint16_t *>(&slotBytes[4]);
                if (a == static_cast<uint16_t>(team0) || b == static_cast<uint16_t>(team0)) team0Slots++;
                if (a == static_cast<uint16_t>(team1) || b == static_cast<uint16_t>(team1)) team1Slots++;
            }
        }
        scanOfs += len;
    }
    assertTrue(team0Slots > 0);
    assertTrue(team1Slots > 0);

    // 3. Verify league cached team pointers relink dynamically on switchToPlayer(1)
    compBufOfs = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers]);
    leagueTeamsCount = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[compBufOfs + 49]);
    int t0IdxInLeague = -1, t1IdxInLeague = -1;
    for (int k = 0; k < leagueTeamsCount && k < 64; ++k) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[compBufOfs + 301 + k * 2]);
        if (tid == static_cast<uint16_t>(team0)) t0IdxInLeague = k;
        if (tid == static_cast<uint16_t>(team1)) t1IdxInLeague = k;
    }
    assertTrue(t0IdxInLeague >= 0 && t1IdxInLeague >= 0);

    int32_t careerTeamDelta = static_cast<int32_t>(CareerOffsets::kCareerTeam) - static_cast<int32_t>(swos.selTeamsPtr);
    mgr.switchToPlayer(1, true);
    int32_t delta0After = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[compBufOfs + 691 + t0IdxInLeague * 18]);
    int32_t delta1After = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[compBufOfs + 691 + t1IdxInLeague * 18]);
    assertEqual(delta1After, careerTeamDelta);
    assertTrue(delta0After >= 0 && delta0After < 68400);

    // 4. Regression test for ContestMenu + CareerGameListCommon rendering across all 14 rows
    // with 2-player hotseat clubs (including Turkey Premier Division: Fenerbahce 0x0926 / Galatasaray 0x0a26)
    mgr.switchToPlayer(0, true);
    int menuFrames = 0;
    SWOS_UnitTest::setMenuCallback([&] {
        menuFrames++;
        if (menuFrames >= 2) {
            SetExitMenuFlag();
            return true;
        }
        return false;
    });
    swos.g_exitMenu = 0;
    A6 = 482233; // contestMenu
    SWOS::ShowMenu();
    assertTrue(menuFrames >= 2);

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatWeeklyTickCalendarTurnAndH2H()
{
    extern void cseg_94DB1();

    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    std::vector<int16_t> teamIds = { 0x0811, 0x0801 };
    mgr.initializeHotseat(teamIds);
    mgr.player(0).newBalance = 1'000'000;
    mgr.player(0).dsegD6B9E = 10'000;
    mgr.player(0).dsegD6B9A = 2'000;
    mgr.player(0).dsegD7E66 = 0;

    mgr.player(1).newBalance = 1'000'000;
    mgr.player(1).dsegD6B9E = 25'000;
    mgr.player(1).dsegD6B9A = 5'000;
    mgr.player(1).dsegD7E66 = 0;

    // Set up synthetic 2-round calendar in dseg_DB3E6:
    // Entry 0 (week=3): 2 human slots (P0 vs 0x0899, P1 vs 0x0898)
    // Entry 1 (week=4): 1 human slot  (P1 vs 0x0897) -> tests calendar slot owner P1 goes first on Week 4!
    std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E6], 0, 128);
    uint8_t *e0 = &SwosVM::g_memByte[CareerOffsets::kDsegDB3E6];
    e0[1] = 3; // week 3
    *reinterpret_cast<uint16_t *>(&e0[2]) = 36;
    e0[5] = 2; // totalSlots
    e0[6] = 2; // remainingSlots
    *reinterpret_cast<int16_t *>(&e0[12]) = 0x0811; // P0
    *reinterpret_cast<int16_t *>(&e0[14]) = 0x0899;
    *reinterpret_cast<int16_t *>(&e0[24]) = 0x0801; // P1
    *reinterpret_cast<int16_t *>(&e0[26]) = 0x0898;

    uint8_t *e1 = &SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + 36];
    e1[1] = 4; // week 4
    *reinterpret_cast<uint16_t *>(&e1[2]) = 24;
    e1[5] = 1;
    e1[6] = 1;
    *reinterpret_cast<int16_t *>(&e1[12]) = 0x0801; // P1 owns first slot of Week 4
    *reinterpret_cast<int16_t *>(&e1[14]) = 0x0897;

    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]) = 0;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]) = 0;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]) = 2;
    mgr.setRoundCalendarIndex(0);

    // 1. Test weekly tick bills BOTH players for 3 elapsed weeks (P1 via OnEnter hook + P0 via cseg_94DB1)
    swos.g_gameType = kGameTypeCareer;
    mgr.loadSwosContext(mgr.player(0), false);
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0E]) += 1;
    save68kRegisters();
    cseg_94DB1();
    restore68kRegisters();
    mgr.saveCurrentSwosContext(mgr.player(0));

    assertEqual(mgr.player(0).newBalance, 1'000'000 - 3 * 12'000);
    assertEqual(mgr.player(1).newBalance, 1'000'000 - 3 * 30'000);
    assertEqual(mgr.player(0).dsegD7E66, 3);
    assertEqual(mgr.player(1).dsegD7E66, 3);
    assertEqual(mgr.player(1).dsegD6D0E, 1);

    // 2. Test calendar-driven turn progression within round 0 and across to round 1 (without NextMatch skip!)
    mgr.switchToPlayer(0, false);
    assertEqual(mgr.findNextPlayerForCurrentCalendarSlot(), 0);
    e0[6] = 1; // Slot 0 played by P0
    mgr.onMatchCompleted();
    bool newRound = mgr.advanceTurnAndResolveRound();
    assertFalse(newRound);
    assertEqual(mgr.activePlayerIndex(), 1);
    assertEqual(*reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]), 0);

    // Slot 1 played by P1 -> VM advances dseg_DB3DE from 0 to 1
    e0[6] = 0;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]) = 1;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]) = 36;
    mgr.onMatchCompleted();
    newRound = mgr.advanceTurnAndResolveRound();
    assertTrue(newRound);
    // Must stay at Entry 1 (not skipped to Entry 2) and pick P1 as owner of Entry 1 Slot 0
    assertEqual(*reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]), 1);
    assertEqual(mgr.activePlayerIndex(), 1);
    assertEqual(mgr.roundCalendarIndex(), 1);

    // 3. Test Head-to-Head stat sync updates only non-active side (active side left to SWOS cseg_2A71E)
    mgr.switchToPlayer(0, false); // active = 0
    mgr.player(0).gamesPlayed = 0;
    mgr.player(1).gamesPlayed = 0;
    mgr.recordHeadToHeadResult(0, 1, 1, 1, 3, 5); // Draw 1-1, P1 wins 5-3 on penalties
    assertEqual(mgr.player(0).gamesPlayed, 0);
    assertEqual(mgr.player(1).gamesPlayed, 1);
    assertEqual(mgr.player(1).numWins, 1);
    assertEqual(mgr.player(1).goalsScored, 1);
    assertEqual(mgr.player(1).goalsTaken, 1);
    assertEqual(mgr.player(1).seasonStatsBlock[0], 1);
    assertEqual(mgr.player(1).seasonStatsBlock[1], 1);

    mgr.reset();
}

void CareerTest::testHotseatLifecycleResetAndHooks()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    mgr.initializeHotseat({ 0x0811, 0x0801 });
    assertTrue(mgr.isActive());

    // CareerInitVars while initializing season must preserve hotseat state
    mgr.setInitializingSeason(true);
    SWOS::CareerInitVars_OnEnter();
    assertTrue(mgr.isActive());

    // StartContest resuming Career mode preserves hotseat state
    swos.g_gameType = kGameTypeCareer;
    SWOS::StartContest_OnEnter();
    assertTrue(mgr.isActive());

    // StartContest entering Preset Competition resets stale hotseat state
    swos.g_gameType = kGameTypePresetCompetition;
    SWOS::StartContest_OnEnter();
    assertFalse(mgr.isActive());

    // CareerInitVars outside season init resets hotseat state
    mgr.initializeHotseat({ 0x0811, 0x0801 });
    mgr.setInitializingSeason(false);
    SWOS::CareerInitVars_OnEnter();
    assertFalse(mgr.isActive());

    // InitCareerVariables unconditionally resets hotseat state
    mgr.initializeHotseat({ 0x0811, 0x0801 });
    SWOS::InitCareerVariables_OnEnter();
    assertFalse(mgr.isActive());
}

void CareerTest::testHotseatTurkeyTwoPlayerContestMenu()
{
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    // Exact user crash scenario: Turkey Premier Division (Fenerbahce 0x0926 + Galatasaray 0x0a26)
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;

    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    // Load Turkey squads cleanly first
    swos.coachOrPlayer = 1;
    D0 = kFenerbahce;
    InitCareer();

    TeamFile team0File = {};
    TeamFile team1File = {};
    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    assertTrue(selTeams != nullptr);
    for (int tIdx = 0; tIdx < swos.g_numSelectedTeams; ++tIdx) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[tIdx]);
        if (tid == static_cast<uint16_t>(kFenerbahce))
            team0File = selTeams[tIdx];
        if (tid == static_cast<uint16_t>(kGalatasaray))
            team1File = selTeams[tIdx];
    }
    assertTrue(*reinterpret_cast<const uint16_t *>(&team0File) == static_cast<uint16_t>(kFenerbahce));
    assertTrue(*reinterpret_cast<const uint16_t *>(&team1File) == static_cast<uint16_t>(kGalatasaray));

    mgr.initializeHotseat({ kFenerbahce, kGalatasaray });
    mgr.setInitializingSeason(true);
    std::memcpy(mgr.player(0).careerTeam, &team0File, sizeof(TeamFile));
    std::memcpy(mgr.player(1).careerTeam, &team1File, sizeof(TeamFile));
    mgr.player(0).coachOrPlayer = 1;
    mgr.player(1).coachOrPlayer = 1;
    mgr.player(0).teamName = team0File.teamName;
    mgr.player(1).teamName = team1File.teamName;

    D0 = kFenerbahce;
    InitCareer();
    swos.g_gameType = kGameTypeCareer;
    mgr.saveCurrentSwosContext(mgr.player(0));
    mgr.setInitializingSeason(false);

    mgr.syncPlayersWithSelectedTeams(true);
    mgr.setRoundCalendarIndex(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]));
    int firstPlayer = mgr.findNextPlayerForCurrentCalendarSlot();
    if (firstPlayer < 0)
        firstPlayer = 0;
    mgr.switchToPlayer(firstPlayer, true);

    // Render ContestMenu (482233) for first player across multiple draw frames (previously crashed in CareerGameListCommon)
    int menuFrames = 0;
    SWOS_UnitTest::setMenuCallback([&] {
        menuFrames++;
        if (menuFrames >= 2) {
            SetExitMenuFlag();
            return true;
        }
        return false;
    });
    swos.g_exitMenu = 0;
    A6 = 482233;
    SWOS::ShowMenu();
    assertTrue(menuFrames >= 2);

    // Simulate first match slot completion and turn advance to Player 2, then render Player 2's ContestMenu
    uint16_t curOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
    uint8_t totalSlots = SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + curOfs + 5];
    mgr.onMatchCompleted();
    if (totalSlots > 1) {
        SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + curOfs + 6] = totalSlots - 1;
        bool newRound = mgr.advanceTurnAndResolveRound();
        assertFalse(newRound);
        assertTrue(mgr.activePlayerIndex() != firstPlayer);

        menuFrames = 0;
        swos.g_exitMenu = 0;
        A6 = 482233;
        SWOS::ShowMenu();
        assertTrue(menuFrames >= 2);
    }


    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

static void initTwoPlayerHotseatFromTeams(int16_t team0, int16_t team1)
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    swos.coachOrPlayer = 1;
    D0 = team0;
    InitCareer();

    TeamFile team0File = {};
    TeamFile team1File = {};
    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    assertTrue(selTeams != nullptr);
    for (int tIdx = 0; tIdx < swos.g_numSelectedTeams; ++tIdx) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[tIdx]);
        if (tid == static_cast<uint16_t>(team0))
            team0File = selTeams[tIdx];
        if (tid == static_cast<uint16_t>(team1))
            team1File = selTeams[tIdx];
    }

    // Fallback to teamFileBuffer if second club is in another division of the same country
    if (*reinterpret_cast<const uint16_t *>(&team1File) != static_cast<uint16_t>(team1)) {
        extern void LoadTeamFile();
        save68kRegisters();
        D0 = static_cast<uint16_t>(team1) & 0xFF;
        LoadTeamFile();
        restore68kRegisters();
        uint16_t fileCount = (static_cast<uint16_t>(SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer]) << 8) |
                             SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer + 1];
        for (uint16_t t = 0; t < fileCount; ++t) {
            const auto *tf = reinterpret_cast<const TeamFile *>(
                &SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer + 2 + static_cast<size_t>(t) * sizeof(TeamFile)]);
            if (*reinterpret_cast<const uint16_t *>(tf) == static_cast<uint16_t>(team1)) {
                team1File = *tf;
                break;
            }
        }
    }

    assertTrue(*reinterpret_cast<const uint16_t *>(&team0File) == static_cast<uint16_t>(team0));
    assertTrue(*reinterpret_cast<const uint16_t *>(&team1File) == static_cast<uint16_t>(team1));

    mgr.initializeHotseat({ team0, team1 });
    mgr.setInitializingSeason(true);
    std::memcpy(mgr.player(0).careerTeam, &team0File, sizeof(TeamFile));
    std::memcpy(mgr.player(1).careerTeam, &team1File, sizeof(TeamFile));
    mgr.player(0).coachOrPlayer = 1;
    mgr.player(1).coachOrPlayer = 1;
    mgr.player(0).teamName = team0File.teamName;
    mgr.player(1).teamName = team1File.teamName;
    mgr.player(0).startingBalance = 2'500'000;
    mgr.player(0).newBalance = 2'500'000;
    mgr.player(0).oldBalance = 2'500'000;
    mgr.player(1).startingBalance = 1'750'000;
    mgr.player(1).newBalance = 1'750'000;
    mgr.player(1).oldBalance = 1'750'000;

    swos.g_gameType = kGameTypeCareer;
    D0 = team0;
    InitCareer();
    swos.g_gameType = kGameTypeCareer;
    mgr.saveCurrentSwosContext(mgr.player(0));

    // Mirror startCareerMode() per-player season initialization so player 1 gets full financials & dseg_D6CA2
    extern void cseg_5DEB9();
    extern void cseg_3D204();
    const auto& p0 = mgr.player(0);
    auto& p1 = mgr.player(1);
    p1.currentSeasonShowing = p0.currentSeasonShowing;
    p1.currentSeasonNumber = p0.currentSeasonNumber;
    p1.seasonPlaying = p0.seasonPlaying;
    p1.dsegD6CA2 = p0.dsegD6CA2;
    p1.dsegD7E66 = p0.dsegD7E66;
    p1.dsegE0048 = p0.dsegE0048;
    p1.dsegD6B62 = p0.dsegD6B62;
    p1.dsegD6B64 = p0.dsegD6B64;
    p1.dsegD6B94 = p0.dsegD6B94;
    p1.dsegD6C46 = p0.dsegD6C46;
    p1.additionalInvestments = p0.additionalInvestments;
    p1.dsegD6CDC = p0.dsegD6CDC;
    p1.teamNoOfferAccepted = p0.teamNoOfferAccepted;
    p1.teamFundsAccepted = p0.teamFundsAccepted;
    p1.dsegD6D0C = p0.dsegD6D0C;
    p1.timeToNegotiate = p0.timeToNegotiate;
    p1.stayingInOldClub = 0;
    p1.careerNumPlayers = p0.careerNumPlayers;
    p1.careerTeamLeague = reinterpret_cast<const TeamFile *>(p1.careerTeam)->league;
    std::memcpy(p1.currentMatchPlayers, p0.currentMatchPlayers, kCurrentMatchPlayersSize);
    std::memcpy(p1.userTactics, p0.userTactics, 6 * kUserTacticsSize);
    std::memcpy(p1.careerHistory, p0.careerHistory, sizeof(p1.careerHistory));

    size_t histOfs = static_cast<size_t>(p1.currentSeasonShowing) * kCareerHistoryEntrySize;
    if (histOfs + kCareerHistoryEntrySize <= kCareerHistoryBufferSize) {
        *reinterpret_cast<uint16_t *>(&p1.careerHistory[histOfs]) = p1.teamId;
        std::memset(&p1.careerHistory[histOfs + 2], 0, 18);
        std::memcpy(&p1.careerHistory[histOfs + 2], p1.teamName.c_str(), std::min(p1.teamName.size(), size_t{17}));
        uint32_t euroStrOfs = 0;
        if (p1.euroCupIndex >= 0 && p1.euroCupIndex < kNumEuroCups && mgr.isEuroCupActive(p1.euroCupIndex)) {
            mgr.activateEuroCup(p1.euroCupIndex, false);
            uint32_t strPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA109]);
            euroStrOfs = (strPtr >= CareerOffsets::kChairmanScenes) ? (strPtr - CareerOffsets::kChairmanScenes) : 0;
        }
        *reinterpret_cast<uint32_t *>(&p1.careerHistory[histOfs + 34]) = euroStrOfs;
    }

    mgr.loadSwosContext(p1, true);
    save68kRegisters();
    cseg_5DEB9();
    cseg_3D204();
    restore68kRegisters();
    mgr.saveCurrentSwosContext(p1);
    mgr.setInitializingSeason(false);

    mgr.syncPlayersWithSelectedTeams(true);
    mgr.setRoundCalendarIndex(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]));
    int firstPlayer = mgr.findNextPlayerForCurrentCalendarSlot();
    if (firstPlayer < 0)
        firstPlayer = 0;
    mgr.switchToPlayer(firstPlayer, true);
}

void CareerTest::testHotseatHighlightMenuContextAndResultTransition()
{
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;

    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    auto& mgr = HotseatCareerManager::get();

    // Verify transfer market isolation state before playing
    mgr.player(0).numPlayersOnTransferMarket = 1;
    mgr.player(0).playersOnTransferMarket[0] = 3;
    mgr.player(1).numPlayersOnTransferMarket = 0;
    mgr.player(1).playersOnTransferMarket[0] = 0;
    mgr.loadSwosContext(mgr.player(mgr.activePlayerIndex()), true);

    setPreMatchMenus(false);
    int playedMatches = 0;
    bool inMatchFlow = false;
    int loopTicks = 0;
    int seenOwnerTransitions = 0;
    int lastOwner = -1;
    bool verifiedClubBusiness = false;
    bool verifiedSquad = false;
    bool verifiedTransfers = false;
    int menuSteps = 0;

    setGameLoopStartHook([&] {
        resetMockSdlMixer();
        if (swos.gameStatePl == GameState::kWaitingOnPlayer || ++loopTicks > 560) {
            loopTicks = 0;
            swos.playGame = 0;
        }
    });

    SWOS_UnitTest::setMenuCallback([&] {
        resetMockSdlMixer();
        if (++menuSteps > 80) {
            exitCurrentMenu();
            SetExitMenuFlag();
            return true;
        }

        auto packed = getCurrentPackedMenu();
        uint32_t packedOfs = (packed && SwosVM::isSwosPtr(packed)) ? SwosVM::ptrToOffset(packed) : 0;

        if (packedOfs == 482233) {
            inMatchFlow = false;
            // On contestMenu, active player + Header team name + selTeamNumber + transfer list MUST always match purple highlight!
            assertFalse(mgr.canEndTurn());
            int expectedOwner = mgr.findNextPlayerForCurrentCalendarSlot();
            assertTrue(expectedOwner >= 0 && expectedOwner < mgr.numPlayers());
            assertEqual(mgr.activePlayerIndex(), expectedOwner);

            uint16_t vmTeamId = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
            assertEqual(vmTeamId, static_cast<uint16_t>(mgr.player(expectedOwner).teamId));

            // Entry 1 is club name in the contestMenu top bar; button 7 must say PLAY MATCH (never stale END TURN)
            auto menu = getCurrentMenu();
            assertTrue(menu != nullptr && menu->numEntries > 7);
            const char *headerTeam = menu->entries()[1].string();
            assertTrue(headerTeam != nullptr);
            std::string actualHeader(headerTeam);
            std::string expectedTeam = mgr.player(expectedOwner).teamName;
            trimTrailingSwosPadding(actualHeader);
            trimTrailingSwosPadding(expectedTeam);
            assertEqual(actualHeader, expectedTeam);

            uint16_t vmListed = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]);
            assertEqual(vmListed, mgr.player(expectedOwner).numPlayersOnTransferMarket);

            if (lastOwner >= 0 && expectedOwner != lastOwner) {
                seenOwnerTransitions++;
            }
            lastOwner = expectedOwner;

            if (!verifiedClubBusiness) {
                verifiedClubBusiness = true;
                selectEntry(6); // CLUB BUSINESS
                return false;
            }
            if (!verifiedSquad) {
                verifiedSquad = true;
                selectEntry(8); // SQUAD
                return false;
            }
            if (!verifiedTransfers) {
                verifiedTransfers = true;
                selectEntry(4); // TRANSFERS
                return false;
            }

            if (playedMatches < 2) {
                playedMatches++;
                inMatchFlow = true;
                loopTicks = 0;
                selectEntry(7); // PLAY MATCH
                return false;
            }

            SetExitMenuFlag();
            return false;
        }

        // PlayMatchMenu / pre-match / result screens
        if (packedOfs == 496147) {
            auto m = getCurrentMenu();
            if (inMatchFlow && m && m->numEntries > 3 && !m->entries()[3].invisible) {
                // Division fixtures "- MATCH -"
                selectEntry(3);
                return false;
            }
            SetExitMenuFlag();
            return false;
        }

        // Submenus entered cleanly (Club Business 495055 / Squad / Transfers / Result): unwind cleanly back to contestMenu
        if (inMatchFlow &&
            getCurrentMenu() && getCurrentMenu()->onReturn.index() == static_cast<int>(SwosVM::Procs::PlayMatchMenuReinit)) {
            inMatchFlow = false; // only press PLAY inside playMatchMenu once per match
            swos.pl1Fire = 1;
            selectEntry(9);
            return false;
        }

        // Exit submenus cleanly back to contestMenu
        SetExitMenuFlag();
        return false;
    });

    swos.g_exitMenu = 0;
    A6 = 482233;
    SWOS::ShowMenu();

    assertEqual(playedMatches, 2);
    assertTrue(verifiedClubBusiness && verifiedSquad && verifiedTransfers);
    assertTrue(seenOwnerTransitions >= 1);

    setGameLoopStartHook();
    SWOS_UnitTest::setMenuCallback();
    mgr.reset();
}

void CareerTest::testHotseatSpecialCupsCompetitionLabelsAndConcurrentEuroCups()
{
    constexpr int16_t kFenerbahce = 0x0926;  // Champions Cup (euroCupCopy)
    constexpr int16_t kGalatasaray = 0x0a26; // Cup Winners Cup (cupWinnersCupCopy)

    // 1. P1=Fenerbahce (Champions Cup) + P2=Galatasaray (Cup Winners Cup)
    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    auto& mgr = HotseatCareerManager::get();

    assertTrue(mgr.isEuroCupActive(0));
    assertTrue(mgr.isEuroCupActive(1));
    assertEqual(mgr.findHotseatPlayerByTeamId(kFenerbahce), 0);
    assertEqual(mgr.findHotseatPlayerByTeamId(kGalatasaray), 1);
    assertEqual(static_cast<int>(mgr.player(0).euroCupIndex), 0);
    assertEqual(static_cast<int>(mgr.player(1).euroCupIndex), 1);

    // Count type=3 slots in calendar for BOTH P1 (Champions Cup) and P2 (Cup Winners Cup)
    int fenerEuroSlots = 0;
    int gsEuroSlots = 0;
    int euroCupMapCount = 0;
    uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    size_t scanOfs = CareerOffsets::kDsegDB3E6;
    for (uint16_t i = 0; i < totalEntries && scanOfs + 22 <= CareerOffsets::kDsegDB3E6 + 1870; ++i) {
        uint16_t calOfs = static_cast<uint16_t>(scanOfs - CareerOffsets::kDsegDB3E6);
        if (SwosVM::g_memByte[scanOfs] == 3) {
            euroCupMapCount++;
            auto it = mgr.calendarSlots().find(calOfs);
            if (it != mgr.calendarSlots().end()) {
                for (const auto& s : it->second) {
                    uint16_t a = *reinterpret_cast<const uint16_t *>(&s[2]);
                    uint16_t b = *reinterpret_cast<const uint16_t *>(&s[4]);
                    if (a == static_cast<uint16_t>(kFenerbahce) || b == static_cast<uint16_t>(kFenerbahce))
                        fenerEuroSlots++;
                    if (a == static_cast<uint16_t>(kGalatasaray) || b == static_cast<uint16_t>(kGalatasaray))
                        gsEuroSlots++;
                }
            }
        }
        uint16_t len = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[scanOfs + 2]);
        if (len < 22) break;
        scanOfs += len;
    }
    assertTrue(euroCupMapCount >= 2);
    assertTrue(fenerEuroSlots > 0);
    assertTrue(gsEuroSlots > 0);

    // Verify slotIdx=0 keeps A4 == A6 so competition label (#38) & round name (#39) are rendered on special cups!
    auto checkSlot0CompLabel = [&](uint16_t calOfs, int expectedEuroIdx) {
        uint32_t calEntryVm = CareerOffsets::kDsegDB3E6 + calOfs;
        save68kRegisters();
        A6 = calEntryVm;
        A4 = calEntryVm;
        D0 = 0;
        SWOS::Hotseat_ResolveGameListSlotPtr();
        uint32_t outA4 = A4.asDword();
        restore68kRegisters();
        assertEqual(outA4, calEntryVm);
        uint32_t euroDefPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]);
        if (expectedEuroIdx == 0)
            assertEqual(euroDefPtr, static_cast<uint32_t>(CareerOffsets::kEuroCupCopy));
        else if (expectedEuroIdx == 1)
            assertEqual(euroDefPtr, static_cast<uint32_t>(CareerOffsets::kCupWinnersCupCopy));
    };

    bool checked0 = false, checked1 = false;
    for (const auto& kv : mgr.euroCupByCalOfs()) {
        auto calIt = mgr.calendarSlots().find(kv.first);
        if (calIt == mgr.calendarSlots().end() || calIt->second.empty())
            continue;
        if (kv.second == 0 && !checked0) {
            checkSlot0CompLabel(kv.first, 0);
            checked0 = true;
        }
        if (kv.second == 1 && !checked1) {
            checkSlot0CompLabel(kv.first, 1);
            checked1 = true;
        }
    }
    assertTrue(checked0 && checked1);

    // Verify ViewCompetitions switches European Cup name cleanly between P0 (Champions) and P1 (Cup-Winners)
    auto testViewCompEuroText = [&](int pIdx, const char *expectedSubstr) {
        mgr.switchToPlayer(pIdx, true);
        int checks = 0;
        SWOS_UnitTest::setMenuCallback([&] {
            checks++;
            auto m = getCurrentMenu();
            assertTrue(m != nullptr && m->numEntries > 4);
            const char *btn4 = m->entries()[4].string();
            assertTrue(btn4 != nullptr && strstr(btn4, expectedSubstr) != nullptr);
            SetExitMenuFlag();
            return false;
        });
        swos.g_exitMenu = 0;
        A6 = 484087; // viewCompetitionsMenu
        SWOS::ShowMenu();
        assertTrue(checks >= 1);
    };
    testViewCompEuroText(0, "CHAMPIONS");
    testViewCompEuroText(1, "WINNERS");

    // Advance via real NEXT MATCH + "- RESULT -" (entry 5) inside League/Cup screens until BOTH Euro cups advance!
    int nextMatchSteps = 0;
    int euro0Played = 0;
    int euro1Played = 0;
    int simSteps = 0;
    SWOS_UnitTest::setMenuCallback([&] {
        resetMockSdlMixer();
        if (++simSteps > 500) {
            exitCurrentMenu();
            SetExitMenuFlag();
            return false;
        }
        auto packed = getCurrentPackedMenu();
        uint32_t packedOfs = (packed && SwosVM::isSwosPtr(packed)) ? SwosVM::ptrToOffset(packed) : 0;
        if (packedOfs == 482233) {
            uint16_t seasonFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
            if (seasonFin != 0 || (euro0Played >= 2 && euro1Played >= 2) || nextMatchSteps >= 80) {
                SetExitMenuFlag();
                return false;
            }
            int expectedOwner = mgr.findNextPlayerForCurrentCalendarSlot();
            assertTrue(expectedOwner >= 0);
            assertEqual(mgr.activePlayerIndex(), expectedOwner);

            uint32_t hlHdr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457A]);
            if (hlHdr >= CareerOffsets::kDsegDB3E6 && hlHdr < CareerOffsets::kDsegDB3E6 + 1870 &&
                SwosVM::g_memByte[hlHdr] == 3) {
                uint16_t calOfs = static_cast<uint16_t>(hlHdr - CareerOffsets::kDsegDB3E6);
                int eIdx = mgr.findEuroCupForCalOfs(calOfs);
                if (eIdx == 0) euro0Played++;
                if (eIdx == 1) euro1Played++;
            }

            nextMatchSteps++;
            selectEntry(10); // NEXT MATCH
            return false;
        }
        if (packedOfs == 496147 || packedOfs == 493825) {
            auto m = getCurrentMenu();
            if (m && m->numEntries > 5 && !m->entries()[5].invisible && m->entries()[5].onSelect) {
                selectEntry(5); // - RESULT -
                return false;
            }
            SetExitMenuFlag();
            return false;
        }
        SetExitMenuFlag();
        return false;
    });
    swos.g_exitMenu = 0;
    A6 = 482233;
    SWOS::ShowMenu();
    assertTrue(nextMatchSteps >= 2);
    assertTrue(euro0Played > 0);
    assertTrue(euro1Played > 0);
    assertTrue(mgr.isEuroCupActive(0) && mgr.isEuroCupActive(1));

    // Verify V4 persistence saves & restores concurrent European cup buffers cleanly
    const std::string savePath = "test_hotseat_euro_v4.hot";
    assertTrue(mgr.saveToFile(savePath));
    mgr.reset();
    assertTrue(mgr.loadFromFile(savePath));
    assertTrue(mgr.isEuroCupActive(0));
    assertTrue(mgr.isEuroCupActive(1));
    assertEqual(static_cast<int>(mgr.player(0).euroCupIndex), 0);
    assertEqual(static_cast<int>(mgr.player(1).euroCupIndex), 1);
    assertTrue(!mgr.euroCupByCalOfs().empty());
    std::remove(savePath.c_str());

    // 2. Reversed order: P1=Galatasaray (CWC) + P2=Fenerbahce (Champions Cup)
    initTwoPlayerHotseatFromTeams(kGalatasaray, kFenerbahce);
    assertTrue(mgr.isEuroCupActive(0));
    assertTrue(mgr.isEuroCupActive(1));
    assertEqual(static_cast<int>(mgr.player(0).euroCupIndex), 1);
    assertEqual(static_cast<int>(mgr.player(1).euroCupIndex), 0);

    // Verify season-end hooks cleanly swap active VM Euro cup for cseg_92D55/9307A/9339D
    SWOS::cseg_92D55_OnEnter();
    assertEqual(*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]),
                static_cast<uint32_t>(CareerOffsets::kEuroCupCopy));
    SWOS::cseg_9307A_OnEnter();
    assertEqual(*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]),
                static_cast<uint32_t>(CareerOffsets::kCupWinnersCupCopy));
    SWOS::cseg_9339D_OnEnter();
    assertEqual(*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]), 0u);

    SWOS_UnitTest::setMenuCallback();
    mgr.reset();
}

void CareerTest::testHotseatDomesticCupInclusionPremAndDiv3()
{
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    // P1: Manchester Utd (0x1408, England Prem Div) + P2: first England Div 3 club
    constexpr int16_t kManUtd = 0x1408;
    int16_t div3TeamId = 0;

    extern void LoadTeamFile();
    save68kRegisters();
    D0 = 0x08; // England
    LoadTeamFile();
    restore68kRegisters();

    uint16_t fileCount = (static_cast<uint16_t>(SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer]) << 8) |
                         SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer + 1];
    for (uint16_t t = 0; t < fileCount; ++t) {
        const auto *tf = reinterpret_cast<const TeamFile *>(
            &SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer + 2 + static_cast<size_t>(t) * sizeof(TeamFile)]);
        if (tf->league == 3) {
            div3TeamId = static_cast<int16_t>(*reinterpret_cast<const uint16_t *>(tf));
            break;
        }
    }
    assertTrue(div3TeamId != 0);

    initTwoPlayerHotseatFromTeams(kManUtd, div3TeamId);
    auto& mgr = HotseatCareerManager::get();

    int faP1 = 0, faP2 = 0;
    int lcP1 = 0, lcP2 = 0;
    uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    size_t scanOfs = CareerOffsets::kDsegDB3E6;
    for (uint16_t i = 0; i < totalEntries && scanOfs + 22 <= CareerOffsets::kDsegDB3E6 + 1870; ++i) {
        uint8_t type = SwosVM::g_memByte[scanOfs];
        uint16_t calOfs = static_cast<uint16_t>(scanOfs - CareerOffsets::kDsegDB3E6);
        auto it = mgr.calendarSlots().find(calOfs);
        if (it != mgr.calendarSlots().end()) {
            for (const auto& s : it->second) {
                uint16_t a = *reinterpret_cast<const uint16_t *>(&s[2]);
                uint16_t b = *reinterpret_cast<const uint16_t *>(&s[4]);
                if (type == 1) {
                    if (a == static_cast<uint16_t>(kManUtd) || b == static_cast<uint16_t>(kManUtd)) faP1++;
                    if (a == static_cast<uint16_t>(div3TeamId) || b == static_cast<uint16_t>(div3TeamId)) faP2++;
                } else if (type == 2) {
                    if (a == static_cast<uint16_t>(kManUtd) || b == static_cast<uint16_t>(kManUtd)) lcP1++;
                    if (a == static_cast<uint16_t>(div3TeamId) || b == static_cast<uint16_t>(div3TeamId)) lcP2++;
                }
            }
        }
        uint16_t len = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[scanOfs + 2]);
        if (len < 22) break;
        scanOfs += len;
    }

    assertTrue(faP1 > 0 && faP2 > 0);
    assertTrue(lcP1 > 0 && lcP2 > 0);

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatPlayMatchEuroCupAndTransfersActive()
{
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;

    auto runPlayMatchCycle = [](int16_t team0, int16_t team1, int expectedEuro0, int expectedEuro1,
                                int minHubSteps, int minEuro0SubVisits, int minEuro1SubVisits) {
        initTwoPlayerHotseatFromTeams(team0, team1);
        auto& mgr = HotseatCareerManager::get();
        assertTrue(mgr.isEuroCupActive(0) && mgr.isEuroCupActive(1));
        assertEqual(static_cast<int>(mgr.player(0).euroCupIndex), expectedEuro0);
        assertEqual(static_cast<int>(mgr.player(1).euroCupIndex), expectedEuro1);

        int steps = 0;
        int hubTurns[2] = { 0, 0 };
        int euroSubVisits[2] = { 0, 0 };
        bool finishedTarget = false;

        SWOS_UnitTest::setMenuCallback([&] {
            resetMockSdlMixer();
            if (++steps > 160) {
                SetExitMenuFlag();
                return false;
            }
            auto packed = getCurrentPackedMenu();
            uint32_t packedOfs = (packed && SwosVM::isSwosPtr(packed)) ? SwosVM::ptrToOffset(packed) : 0;
            auto m = getCurrentMenu();
            auto entryStr = [](const MenuEntry& e) -> const char * {
                return (e.type == kEntryString && e.string()) ? e.string() : "";
            };

            if (packedOfs == 482233) {
                SWOS::DrawMenu();
                assertTrue(m != nullptr && m->numEntries > 7);

                // Issue #1: Whenever control is on contestMenu at the start of any player's turn,
                // dseg_DF4F4 and activePlayer.playMatchBlink must be 0 and TRANSFERS must be enabled.
                uint16_t blink = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDF4F4]);
                assertEqual(static_cast<int>(blink), 0);
                assertEqual(static_cast<int>(m->entries()[4].disabled), 0);
                assertEqual(static_cast<int>(mgr.activePlayer().playMatchBlink), 0);
                assertFalse(mgr.inPlayMatchFlow());

                int p = mgr.activePlayerIndex();
                hubTurns[p]++;

                // Issue #2: After drawing all fixtures on contestMenu, VM's active EuroCup buffer must
                // remain restored to the active player's European competition (not the last row drawn).
                int activeEuro = mgr.player(p).euroCupIndex;
                uint32_t expectedPtr = (activeEuro == 0) ? CareerOffsets::kEuroCupCopy : CareerOffsets::kCupWinnersCupCopy;
                uint32_t vmEuroPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]);
                assertEqual(vmEuroPtr, expectedPtr);

                const char *da0e2Name = reinterpret_cast<const char *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2 + 4]);
                if (activeEuro == 0) {
                    assertTrue(strstr(da0e2Name, "CHAMPIONS") != nullptr);
                } else {
                    assertTrue(strstr(da0e2Name, "WINNERS") != nullptr);
                }

                if (steps >= minHubSteps &&
                    hubTurns[0] >= 4 && hubTurns[1] >= 4 &&
                    euroSubVisits[0] >= minEuro0SubVisits &&
                    euroSubVisits[1] >= minEuro1SubVisits) {
                    finishedTarget = true;
                    SetExitMenuFlag();
                    return false;
                }

                selectEntry(7); // PLAY MATCH
                return false;
            }

            if (packedOfs == 496147 || packedOfs == 493825) {
                const char *title = (m && m->numEntries > 0) ? entryStr(m->entries()[0]) : "";
                const char *s5 = (m && m->numEntries > 5) ? entryStr(m->entries()[5]) : "";
                const char *s17 = (m && m->numEntries > 17) ? entryStr(m->entries()[17]) : "";
                uint32_t euroPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]);

                uint16_t curOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
                if (curOfs < 1870 && SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + curOfs] == 3) {
                    int p = mgr.activePlayerIndex();
                    int expectedEuro = mgr.player(p).euroCupIndex;
                    if (expectedEuro == 0) {
                        assertEqual(euroPtr, CareerOffsets::kEuroCupCopy);
                        assertTrue(strstr(title, "CHAMPIONS") != nullptr);
                        assertTrue(strstr(title, "WINNERS") == nullptr);
                    } else {
                        assertEqual(packedOfs, uint32_t{493825});
                        assertEqual(euroPtr, CareerOffsets::kCupWinnersCupCopy);
                        assertTrue(strstr(title, "WINNERS") != nullptr);
                        assertTrue(strstr(title, "CHAMPIONS") == nullptr);
                    }
                    if (*s5 && strstr(s5, "RESULT") && !m->entries()[5].invisible) {
                        euroSubVisits[p]++;
                    }
                }

                if (*s5 && strstr(s5, "RESULT") && !m->entries()[5].invisible) {
                    selectEntry(5);
                    return false;
                }
                if (*s17 && strstr(s17, "RESULT") && !m->entries()[17].invisible) {
                    selectEntry(17);
                    return false;
                }
            }

            SetExitMenuFlag();
            return false;
        });

        swos.g_exitMenu = 0;
        A6 = 482233;
        SWOS::ShowMenu();

        assertTrue(finishedTarget);
        assertFalse(mgr.inPlayMatchFlow());
        assertTrue(hubTurns[0] >= 4 && hubTurns[1] >= 4);
        assertTrue(euroSubVisits[0] >= minEuro0SubVisits);
        assertTrue(euroSubVisits[1] >= minEuro1SubVisits);

        // Concurrent EuroCup buffers must remain intact with their true competition types & titles.
        mgr.activateEuroCup(0, false);
        assertEqual(static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2])), 2);
        assertTrue(strstr(reinterpret_cast<const char *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2 + 4]), "CHAMPIONS") != nullptr);
        mgr.activateEuroCup(1, false);
        assertEqual(static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2])), 1);
        assertTrue(strstr(reinterpret_cast<const char *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2 + 4]), "WINNERS") != nullptr);

        mgr.reset();
        SWOS_UnitTest::setMenuCallback();
    };

    // Fenerbahce (P0: Champions Cup e=0) + Galatasaray (P1: Cup-Winners Cup e=1) through 2 CWC rounds + shared league turns
    runPlayMatchCycle(kFenerbahce, kGalatasaray, 0, 1, 75, 1, 2);

    // Reversed order: Galatasaray (P0: e=1) + Fenerbahce (P1: e=0)
    runPlayMatchCycle(kGalatasaray, kFenerbahce, 1, 0, 50, 1, 1);
}

void CareerTest::testHotseatConfirmOfferPromptAndMultiOffer()
{
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;

    // 1. Direct null-safety regression check for menuAllocString(nullptr) and showContinueAbortPrompt(..., nullptr, ...)
    assertTrue(menuAllocString(nullptr) == nullptr);

    bool promptChecked = false;
    SWOS_UnitTest::setMenuCallback([&] {
        auto m = getCurrentMenu();
        if (m && m->numEntries >= 4 && m->entries()[0].type == kEntryString &&
            m->entries()[0].string() && strcmp(m->entries()[0].string(), "BID TRANSMITTED") == 0) {
            promptChecked = true;
            assertEqual(static_cast<int>(m->entries()[3].invisible), 1);
            assertEqual(strcmp(m->entries()[2].string(), "CONTINUE"), 0);
            assertTrue(m->selectedEntry == &m->entries()[2]);
            selectEntry(2);
            return false;
        }
        SetExitMenuFlag();
        return false;
    });
    bool promptRes = showContinueAbortPrompt("BID TRANSMITTED", "CONTINUE", nullptr,
        { "OFFER OF \x9c" "250K SENT TO", "PLAYER 2", "QUEUED FOR THEIR TURN!" }, true);
    assertTrue(promptChecked);
    assertTrue(promptRes);
    SWOS_UnitTest::setMenuCallback();

    // 2. Full playerOffersMenu Entry 12 ("CONFIRM OFFER") UI flow between P0 (Fenerbahce) and P1 (Galatasaray)
    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    auto& mgr = HotseatCareerManager::get();
    mgr.switchToPlayer(0, true);
    mgr.saveCurrentSwosContext(mgr.player(0));

    extern void cseg_54C1E();
    bool sawOfferMenu = false;
    bool sawBidPrompt = false;
    SWOS_UnitTest::setMenuCallback([&] {
        resetMockSdlMixer();
        auto m = getCurrentMenu();
        if (!m) {
            SetExitMenuFlag();
            return false;
        }
        if (m->numEntries > 0 && m->entries()[0].type == kEntryString &&
            m->entries()[0].string() && strcmp(m->entries()[0].string(), "BID TRANSMITTED") == 0) {
            sawBidPrompt = true;
            assertEqual(static_cast<int>(m->entries()[3].invisible), 1);
            assertFalse(m->entries()[1].fg.multilineText.isNull());
            const char *line1 = m->entries()[1].fg.multilineText.as<const char *>() + 1;
            assertTrue(strchr(line1, static_cast<char>(156)) != nullptr);   // SWOS CP437 pound byte \x9c
            assertTrue(strstr(line1, "\xc2\xa3") == nullptr);               // must not emit UTF-8 pound sequence
            selectEntry(2);
            return false;
        }
        if (!sawOfferMenu && m->numEntries > 12 && m->entries()[12].type == kEntryString &&
            m->entries()[12].string() && strstr(m->entries()[12].string(), "CONFIRM OFFER") != nullptr) {
            sawOfferMenu = true;
            selectEntry(12);
            return false;
        }
        SetExitMenuFlag();
        return false;
    });
    save68kRegisters();
    D0 = kGalatasaray;
    D1 = 2; // bid on P1's ordinal 2
    cseg_54C1E();
    restore68kRegisters();
    SWOS_UnitTest::setMenuCallback();

    assertTrue(sawOfferMenu);
    assertTrue(sawBidPrompt);

    auto pendingAfterConfirm = mgr.getPendingOffersForPlayer(1);
    assertEqual(static_cast<int>(pendingAfterConfirm.size()), 1);
    const int confirmOfferId = pendingAfterConfirm[0].id;
    assertEqual(pendingAfterConfirm[0].playerOrdinal, 2);

    // 3. Multi-offer compaction & duplicate invalidation on seller's squad when resolving offers
    const uint16_t initialSellerCount = mgr.player(1).careerNumPlayers;
    const uint16_t initialBuyerCount = mgr.player(0).careerNumPlayers;
    assertTrue(initialSellerCount > kMinCareerSquadPlayers + 2);
    const int lastOrd = static_cast<int>(initialSellerCount - 1);
    const auto *sellerPlayers = reinterpret_cast<const PlayerFile *>(mgr.player(1).careerTeam + sizeof(TeamFileHeader));
    char lastPlayerName[32] = {0};
    snprintf(lastPlayerName, sizeof(lastPlayerName), "%.22s", sellerPlayers[lastOrd].playerName);
    trimTrailingSwosPadding(lastPlayerName);

    HotseatCareerManager::TransferOffer dupOffer;
    dupOffer.fromPlayerIndex = 0;
    dupOffer.toPlayerIndex = 1;
    dupOffer.playerOrdinal = 2;
    dupOffer.offerAmount = 300'000;
    std::snprintf(dupOffer.playerName, sizeof(dupOffer.playerName), "%s", pendingAfterConfirm[0].playerName);
    int dupOfferId = mgr.queueTransferOffer(dupOffer);

    HotseatCareerManager::TransferOffer lastSlotOffer;
    lastSlotOffer.fromPlayerIndex = 0;
    lastSlotOffer.toPlayerIndex = 1;
    lastSlotOffer.playerOrdinal = lastOrd;
    lastSlotOffer.offerAmount = 250'000;
    std::snprintf(lastSlotOffer.playerName, sizeof(lastSlotOffer.playerName), "%s", lastPlayerName);
    int lastOfferId = mgr.queueTransferOffer(lastSlotOffer);

    // Accept the offer on ordinal 2 -> lastOrd moves into ordinal 2, duplicate offer is invalidated
    assertTrue(mgr.resolveTransferOffer(confirmOfferId, true));
    assertEqual(mgr.player(1).careerNumPlayers, static_cast<uint16_t>(initialSellerCount - 1));
    assertEqual(mgr.player(0).careerNumPlayers, static_cast<uint16_t>(initialBuyerCount + 1));

    const auto *dupPtr = mgr.getTransferOfferById(dupOfferId);
    assertTrue(dupPtr != nullptr && !dupPtr->pending && dupPtr->rejected && !dupPtr->accepted);

    const auto *lastPtr = mgr.getTransferOfferById(lastOfferId);
    assertTrue(lastPtr != nullptr && lastPtr->pending);
    assertEqual(lastPtr->playerOrdinal, 2);

    // Accept the remapped offer -> transfers the exact player originally at lastOrd
    assertTrue(mgr.resolveTransferOffer(lastOfferId, true));
    assertEqual(mgr.player(1).careerNumPlayers, static_cast<uint16_t>(initialSellerCount - 2));
    assertEqual(mgr.player(0).careerNumPlayers, static_cast<uint16_t>(initialBuyerCount + 2));

    const auto *buyerPlayers = reinterpret_cast<const PlayerFile *>(mgr.player(0).careerTeam + sizeof(TeamFileHeader));
    char boughtLastName[32] = {0};
    snprintf(boughtLastName, sizeof(boughtLastName), "%.22s", buyerPlayers[initialBuyerCount + 1].playerName);
    trimTrailingSwosPadding(boughtLastName);
    assertEqual(strcmp(boughtLastName, lastPlayerName), 0);

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

static bool hasValidPlayerNameForTest(const PlayerFile *pf)
{
    if (!pf || pf->playerName[0] == '\0' || std::strncmp(pf->playerName, "*ERR*", 5) == 0)
        return false;
    for (size_t i = 0; i < sizeof(pf->playerName) && pf->playerName[i] != '\0'; ++i) {
        if (static_cast<unsigned char>(pf->playerName[i]) > ' ')
            return true;
    }
    return false;
}

static const PlayerFile *getPlayerSlot(const uint8_t *teamBuf, uint8_t ordinal)
{
    return reinterpret_cast<const PlayerFile *>(teamBuf + sizeof(TeamFileHeader) + static_cast<size_t>(ordinal) * sizeof(PlayerFile));
}

static void initHotseatPlayerReservesViaSwos(HotseatCareerManager& mgr, int playerIdx)
{
    extern void cseg_64A65();

    auto& p = mgr.player(playerIdx);
    std::memset(p.careerTeam + sizeof(TeamFile), 0, kCareerTeamBufferSize - sizeof(TeamFile));
    std::memset(p.playerTrialWeeks, 0, sizeof(p.playerTrialWeeks));
    std::memset(p.currentMatchPlayers, 0, kCurrentMatchPlayersSize);
    p.careerNumPlayers = 16;
    const auto *hdr = reinterpret_cast<const TeamFileHeader *>(p.careerTeam);
    for (int s = 0; s < 16; ++s)
        p.currentMatchPlayers[s] = hdr->playerNumbers[s];
    mgr.loadSwosContext(p, true);
    save68kRegisters();
    cseg_64A65();
    restore68kRegisters();
    mgr.saveCurrentSwosContext(p);
    mgr.syncPlayersWithSelectedTeams(true);
}

void CareerTest::testHotseatTransferDepartureAndTrialReplacement()
{
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    extern void cseg_52EA6();
    extern void cseg_52A3E();
    extern void RemovePlayerFromTeam();

    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;

    // 1. Initialize P1+P2 with cseg_64A65() (as startCareerMode does) and verify all 26 players & trialists
    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    auto& mgr = HotseatCareerManager::get();
    initHotseatPlayerReservesViaSwos(mgr, 1);

    for (int pi = 0; pi < 2; ++pi) {
        mgr.switchToPlayer(pi, true);
        uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        assertEqual(numPlayers, static_cast<uint16_t>(26));
        const uint8_t *ct = &SwosVM::g_memByte[CareerOffsets::kCareerTeam];
        for (int s = 0; s < 26; ++s) {
            uint8_t ord = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + s];
            assertTrue(ord < 30);
            const auto *pf = getPlayerSlot(ct, ord);
            assertTrue(hasValidPlayerNameForTest(pf));
        }
        int8_t reserveTrialWeek = static_cast<int8_t>(SwosVM::g_memByte[CareerOffsets::kDsegD6C26 + 20]);
        assertTrue(reserveTrialWeek < 0);
    }

    // 2. Depart via cseg_52EA6() when careerNumPlayers > 16 (compacts squad without blank or *ERR* slots)
    mgr.switchToPlayer(1, true);
    uint16_t beforeHi = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    assertEqual(beforeHi, static_cast<uint16_t>(26));
    D1 = 13;
    cseg_52EA6();
    uint16_t afterHi = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    assertEqual(afterHi, static_cast<uint16_t>(25));
    for (int s = 0; s < 25; ++s) {
        uint8_t ord = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + s];
        const auto *pf = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], ord);
        assertTrue(hasValidPlayerNameForTest(pf));
    }

    // 3. Depart via cseg_52EA6() at careerNumPlayers == 16 (replacement arrives on TRIAL with non-empty name)
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]) = 16;
    for (uint8_t s = 0; s < 16; ++s)
        SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + s] = s;
    D1 = 13;
    cseg_52EA6();
    uint16_t after16 = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    assertEqual(after16, static_cast<uint16_t>(16));

    uint8_t repOrd = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 13];
    const auto *repPf = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], repOrd);
    assertTrue(hasValidPlayerNameForTest(repPf));
    assertEqual(repPf->someFlag, static_cast<uint8_t>(0x9B)); // ON TRIAL, not blank/0
    assertEqual(static_cast<uint8_t>(repPf->positionAndFace & 0x07), static_cast<uint8_t>(0)); // not LOAN
    assertTrue(repPf->price > 0);
    assertTrue(static_cast<int8_t>(SwosVM::g_memByte[CareerOffsets::kDsegD6C26 + repOrd]) < 0);

    // Also verify RemovePlayerFromTeam() on starting XI slot D1=5 at careerNumPlayers == 16 swaps a sub into XI
    // and puts the new named TRIAL player into departed ordinal 5 on the bench
    D1 = 5;
    RemovePlayerFromTeam();
    const auto *promotedSub = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam],
                                            SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 5]);
    assertTrue(hasValidPlayerNameForTest(promotedSub));
    const auto *repPf2 = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], 5);
    assertTrue(hasValidPlayerNameForTest(repPf2));
    assertEqual(repPf2->someFlag, static_cast<uint8_t>(0x9B));
    assertEqual(static_cast<uint8_t>(repPf2->positionAndFace & 0x07), static_cast<uint8_t>(0));
    assertTrue(static_cast<int8_t>(SwosVM::g_memByte[CareerOffsets::kDsegD6C26 + 5]) < 0);

    uint16_t savedTmdCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTmdCareerCount]);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTmdCareerCount]) = 0;
    D0 = static_cast<uint16_t>(kGalatasaray);
    D1 = 6;
    cseg_52A3E();
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTmdCareerCount]) = savedTmdCount;
    const auto *repPf3 = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], 6);
    assertTrue(hasValidPlayerNameForTest(repPf3));
    assertEqual(repPf3->someFlag, static_cast<uint8_t>(0x9B));
    assertEqual(static_cast<uint8_t>(repPf3->positionAndFace & 0x07), static_cast<uint8_t>(0));
    assertTrue(repPf3->price > 0);

    // 4. Verify inter-player transfer preserves buyer/seller trial weeks, wages, and named reserves
    mgr.switchToPlayer(0, true);
    mgr.player(1).careerNumPlayers = 20;
    for (uint8_t s = 0; s < 20; ++s)
        mgr.player(1).currentMatchPlayers[s] = s;
    mgr.player(1).playerWages[19] = 420;
    mgr.player(1).playerTrialWeeks[19] = static_cast<uint8_t>(static_cast<int8_t>(-44));
    mgr.player(1).playerWages[13] = 900;
    mgr.player(1).numPlayersOnTransferMarket = 1;
    mgr.player(1).playersOnTransferMarket[0] = 19;
    mgr.player(0).newBalance = 5'000'000;

    HotseatCareerManager::TransferOffer offer;
    offer.fromPlayerIndex = 0;
    offer.toPlayerIndex = 1;
    offer.playerOrdinal = 13;
    offer.offerAmount = 300'000;
    std::snprintf(offer.playerName, sizeof(offer.playerName), "TRANSFER PLAYER");
    int oid = mgr.queueTransferOffer(offer);
    assertTrue(mgr.resolveTransferOffer(oid, true));

    assertEqual(mgr.player(1).careerNumPlayers, static_cast<uint16_t>(19));
    assertEqual(mgr.player(1).playerWages[13], static_cast<uint32_t>(420));
    assertEqual(static_cast<int8_t>(mgr.player(1).playerTrialWeeks[13]), static_cast<int8_t>(-44));
    assertEqual(mgr.player(1).playerWages[19], static_cast<uint32_t>(0));
    assertEqual(mgr.player(1).playerTrialWeeks[19], static_cast<uint8_t>(0));
    // Market listing of swapped lastOrdinal=19 was remapped to soldOrdinal=13
    assertEqual(mgr.player(1).numPlayersOnTransferMarket, static_cast<uint16_t>(1));
    assertEqual(mgr.player(1).playersOnTransferMarket[0], static_cast<uint16_t>(13));

    // 5. Verify SWOS_HOTSEAT_V5 round-trip preserves playerTrialWeeks and repairs blank active V4 reserves
    const std::string v5Path = "test_hotseat_v5_trials.hot";
    mgr.player(0).playerTrialWeeks[18] = static_cast<uint8_t>(static_cast<int8_t>(-55));
    mgr.loadSwosContext(mgr.player(0), true);
    assertTrue(mgr.saveToFile(v5Path));

    mgr.reset();
    assertTrue(mgr.loadFromFile(v5Path));
    assertEqual(static_cast<int8_t>(mgr.player(0).playerTrialWeeks[18]), static_cast<int8_t>(-55));
    assertEqual(static_cast<int8_t>(mgr.player(1).playerTrialWeeks[13]), static_cast<int8_t>(-44));

    // Build a genuine SWOS_HOTSEAT_V4 stream (without V5 playerTrialWeeks) and verify blank-slot repair on load
    const std::string v4Path = "test_hotseat_v4_repair.hot";
    uint8_t sentinel0[32], sentinel1[32];
    for (int k = 0; k < 32; ++k) {
        sentinel0[k] = static_cast<uint8_t>(0xA1u + (k & 7));
        sentinel1[k] = static_cast<uint8_t>(0xB1u + (k & 7));
    }
    std::memcpy(mgr.player(0).playerTrialWeeks, sentinel0, 32);
    std::memcpy(mgr.player(1).playerTrialWeeks, sentinel1, 32);
    mgr.player(1).currentMatchPlayers[17] = 22;
    auto *blankSlot = reinterpret_cast<PlayerFile *>(
        mgr.player(1).careerTeam + sizeof(TeamFileHeader) + 22 * sizeof(PlayerFile));
    std::memset(blankSlot, 0, sizeof(PlayerFile));
    mgr.loadSwosContext(mgr.player(0), true);
    assertTrue(mgr.saveToFile(v5Path));
    {
        std::vector<uint8_t> bytes;
        std::ifstream in(v5Path, std::ios::binary | std::ios::ate);
        assertTrue(in.is_open());
        bytes.resize(static_cast<size_t>(in.tellg()));
        in.seekg(0);
        in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        assertTrue(bytes.size() > 128);
        std::memcpy(bytes.data(), "SWOS_HOTSEAT_V4\0", 16);
        auto it1 = std::search(bytes.begin(), bytes.end(), sentinel1, sentinel1 + 32);
        assertTrue(it1 != bytes.end());
        bytes.erase(it1, it1 + 32);
        auto it0 = std::search(bytes.begin(), bytes.end(), sentinel0, sentinel0 + 32);
        assertTrue(it0 != bytes.end());
        bytes.erase(it0, it0 + 32);
        std::ofstream out(v4Path, std::ios::binary);
        assertTrue(out.is_open());
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    mgr.reset();
    assertTrue(mgr.loadFromFile(v4Path));
    const auto *repaired = getPlayerSlot(mgr.player(1).careerTeam, 22);
    assertTrue(hasValidPlayerNameForTest(repaired));
    assertEqual(repaired->someFlag, static_cast<uint8_t>(0x9B));
    assertTrue(repaired->price > 0);
    assertTrue(static_cast<int8_t>(mgr.player(1).playerTrialWeeks[22]) < 0);

    std::remove(v5Path.c_str());
    std::remove(v4Path.c_str());
    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatMarketExclusionAndExplicitListing()
{
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    extern void FillCareerTeamPlayers();
    extern void AddCareerTeamPlayer();
    extern void FillForeignMarketPlayers();
    extern void AddRandomForeignMarketPlayer();
    extern void cseg_54F7A();
    extern void cseg_5509D();
    extern void cseg_540A0();
    extern void cseg_5400D();
    extern void cseg_59B41();
    extern void cseg_5A227();
    extern void GetPlayer();
    extern void cseg_5A666();

#pragma pack(push, 1)
    struct VmMarketEntry {
        uint16_t teamId;
        uint8_t playerOrdinal;
        uint8_t padding;
    };
    struct VmPoolPlayerEntry {
        uint16_t teamId;
        uint8_t playerOrdinal;
        char clubName[17];
        PlayerFile player;
    };
#pragma pack(pop)

    auto marketContains = [](size_t countOfs, size_t listOfs, int maxCount, uint16_t tid, uint8_t ord) {
        int n = std::min<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[countOfs]), maxCount);
        const auto *entries = reinterpret_cast<const VmMarketEntry *>(&SwosVM::g_memByte[listOfs]);
        for (int i = 0; i < n; ++i) {
            if (entries[i].teamId == tid && entries[i].playerOrdinal == ord)
                return true;
        }
        return false;
    };

    auto marketHasTeam = [](size_t countOfs, size_t listOfs, int maxCount, uint16_t tid) {
        int n = std::min<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[countOfs]), maxCount);
        const auto *entries = reinterpret_cast<const VmMarketEntry *>(&SwosVM::g_memByte[listOfs]);
        for (int i = 0; i < n; ++i) {
            if (entries[i].teamId == tid)
                return true;
        }
        return false;
    };

    // 1. Domestic pool exclusion (Fenerbahce 0x0926 & Galatasaray 0x0a26) + explicit listing sync
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;
    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    auto& mgr = HotseatCareerManager::get();
    initHotseatPlayerReservesViaSwos(mgr, 1);

    mgr.switchToPlayer(0, true);
    FillCareerTeamPlayers();
    for (int i = 0; i < 40; ++i)
        AddCareerTeamPlayer();
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0E]) = 5;
    cseg_5509D();

    uint16_t homeCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]);
    assertTrue(homeCount > 0);
    assertFalse(marketHasTeam(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kFenerbahce)));
    assertFalse(marketHasTeam(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kGalatasaray)));

    // Also verify from P1's perspective
    mgr.switchToPlayer(1, true);
    FillCareerTeamPlayers();
    assertFalse(marketHasTeam(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kFenerbahce)));
    assertFalse(marketHasTeam(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kGalatasaray)));

    // P1 lists starter #2 and reserve #18; P0 lists starter #1 -> P0 sees ONLY P1's #2 and #18, never P0's own #1
    D0 = 2;
    cseg_540A0();
    D0 = 18;
    cseg_540A0();
    // P1 must not see P1's own listed players in domestic market
    assertFalse(marketContains(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kGalatasaray), 2));
    assertFalse(marketContains(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kGalatasaray), 18));

    constexpr size_t kShowDomesticMarketOfs = 504336;
    mgr.switchToPlayer(0, true);
    D0 = 1;
    cseg_540A0();
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[kShowDomesticMarketOfs]) = 1;
    cseg_59B41();
    cseg_5A227();

    assertTrue(marketContains(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kGalatasaray), 2));
    assertTrue(marketContains(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kGalatasaray), 18));
    assertFalse(marketContains(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kFenerbahce), 1));

    // GetPlayer() on P0's turn must resolve P1's listed reserve ordinal #18 cleanly via team mirror
    D0 = static_cast<uint16_t>(kGalatasaray);
    D1 = 18;
    GetPlayer();
    const auto *resolvedRes = reinterpret_cast<const PlayerFile *>(A0.asPtr() + sizeof(TeamFileHeader));
    const auto *expectedRes = getPlayerSlot(mgr.player(1).careerTeam, 18);
    assertTrue(hasValidPlayerNameForTest(resolvedRes));
    assertEqual(std::strncmp(resolvedRes->playerName, expectedRes->playerName, sizeof(expectedRes->playerName)), 0);

    // P1 unlists #2 -> immediately removed from P0's market while #18 remains
    mgr.switchToPlayer(1, true);
    D1 = 2;
    cseg_5400D();
    mgr.switchToPlayer(0, true);
    assertFalse(marketContains(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kGalatasaray), 2));
    assertTrue(marketContains(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers, 25, static_cast<uint16_t>(kGalatasaray), 18));

    // 2. Foreign market exclusion (POOLPLYR.DAT club + English club) & safe non-pool lookup
    mgr.reset();
    LoadPoolplyrDat();
    const auto *pool = reinterpret_cast<const VmPoolPlayerEntry *>(&SwosVM::g_memByte[CareerOffsets::kPoolplyrDatBuffer]);
    uint16_t poolTeamId = 0;
    uint8_t poolOrd = 0;
    PlayerFile poolPlayerCopy{};
    for (int i = 0; i < 900; ++i) {
        if (pool[i].teamId != 0 && static_cast<uint8_t>(pool[i].teamId & 0xFF) != 0x11) {
            poolTeamId = pool[i].teamId;
            poolOrd = pool[i].playerOrdinal;
            poolPlayerCopy = pool[i].player;
            break;
        }
    }
    assertTrue(poolTeamId != 0);

    // Setup cross-country hotseat: P0 = Liverpool (0x0811), P1 = POOLPLYR.DAT foreign club
    mgr.initializeHotseat({ static_cast<int16_t>(0x0811), static_cast<int16_t>(poolTeamId) });
    swos.g_gameType = kGameTypeCareer;
    mgr.player(0).coachOrPlayer = 1;
    mgr.player(1).coachOrPlayer = 1;
    mgr.player(0).careerNumPlayers = 20;
    mgr.player(1).careerNumPlayers = 20;
    for (uint8_t s = 0; s < 20; ++s) {
        mgr.player(0).currentMatchPlayers[s] = s;
        mgr.player(1).currentMatchPlayers[s] = s;
    }
    *reinterpret_cast<uint16_t *>(mgr.player(0).careerTeam) = 0x0811;
    *reinterpret_cast<uint16_t *>(mgr.player(1).careerTeam) = poolTeamId;
    std::snprintf(reinterpret_cast<TeamFile *>(mgr.player(1).careerTeam)->teamName, 17, "FOREIGN HOTSEAT");
    auto *p1PoolPf = reinterpret_cast<PlayerFile *>(
        mgr.player(1).careerTeam + sizeof(TeamFileHeader) + static_cast<size_t>(poolOrd) * sizeof(PlayerFile));
    *p1PoolPf = poolPlayerCopy;
    // Also create a reserve at ordinal 18 (guaranteed absent from POOLPLYR.DAT)
    auto *p1ReservePf = reinterpret_cast<PlayerFile *>(
        mgr.player(1).careerTeam + sizeof(TeamFileHeader) + 18 * sizeof(PlayerFile));
    std::memset(p1ReservePf, 0, sizeof(PlayerFile));
    std::snprintf(p1ReservePf->playerName, sizeof(p1ReservePf->playerName), "FOREIGN RESERVE");
    p1ReservePf->price = 5;
    p1ReservePf->someFlag = 0x9B;

    mgr.switchToPlayer(0, false);
    FillForeignMarketPlayers();
    for (int i = 0; i < 40; ++i)
        AddRandomForeignMarketPlayer();
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D10]) = 5;
    cseg_54F7A();

    uint16_t foreignCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]);
    assertTrue(foreignCount > 0);
    assertFalse(marketHasTeam(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers, 15, static_cast<uint16_t>(0x0811)));
    assertFalse(marketHasTeam(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers, 15, poolTeamId));

    // P1 lists both the POOLPLYR.DAT player and the non-pool reserve ordinal 18
    mgr.switchToPlayer(1, false);
    D0 = poolOrd;
    cseg_540A0();
    D0 = 18;
    cseg_540A0();

    mgr.switchToPlayer(0, false);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[kShowDomesticMarketOfs]) = 0;
    cseg_59B41();
    cseg_5A227();
    assertTrue(marketContains(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers, 15, poolTeamId, poolOrd));
    assertTrue(marketContains(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers, 15, poolTeamId, 18));

    // cseg_5A666 must resolve listed non-pool reserve #18 safely without hanging
    D0 = poolTeamId;
    D1 = 18;
    cseg_5A666();
    const auto *resolvedForeign = reinterpret_cast<const VmPoolPlayerEntry *>(A0.asPtr());
    assertEqual(resolvedForeign->teamId, poolTeamId);
    assertEqual(resolvedForeign->playerOrdinal, static_cast<uint8_t>(18));
    assertEqual(std::string(resolvedForeign->player.playerName), std::string("FOREIGN RESERVE"));

    // Bounded fallback check for unknown foreign pair (must return immediately without infinite loop)
    D0 = 0xFFFF;
    D1 = 99;
    cseg_5A666();
    assertEqual(*reinterpret_cast<const uint16_t *>(A0.asPtr()), static_cast<uint16_t>(0xFFFF));

    // Unlist on P1's turn removes cleanly on P0's turn
    mgr.switchToPlayer(1, false);
    D1 = 18;
    cseg_5400D();
    mgr.switchToPlayer(0, false);
    assertFalse(marketContains(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers, 15, poolTeamId, 18));
    assertTrue(marketContains(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers, 15, poolTeamId, poolOrd));

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatExchangeTransferHumanAndCpu()
{
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;

    // 1. Human-to-Human MAKE OFFER menu using green '+' exchange button (Entry 10 -> Entry 12)
    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    auto& mgr = HotseatCareerManager::get();
    mgr.switchToPlayer(0, true);
    mgr.player(0).newBalance = 10'000'000;
    mgr.loadSwosContext(mgr.player(0), true);

    extern void cseg_54C1E();
    bool clickedPlusExchange = false;
    bool pickedExchangePlayer = false;
    bool clickedConfirm = false;
    bool sawBidPrompt = false;

    SWOS_UnitTest::setMenuCallback([&] {
        resetMockSdlMixer();
        auto m = getCurrentMenu();
        if (!m) {
            SetExitMenuFlag();
            return false;
        }
        if (m->numEntries > 0 && m->entries()[0].type == kEntryString && m->entries()[0].string()) {
            const char *title = m->entries()[0].string();
            if (strcmp(title, "BID TRANSMITTED") == 0) {
                sawBidPrompt = true;
                assertFalse(m->entries()[1].fg.multilineText.isNull());
                const char *line1 = m->entries()[1].fg.multilineText.as<const char *>() + 1;
                assertTrue(strchr(line1, static_cast<char>(156)) != nullptr);
                assertTrue(strstr(line1, "\xc2\xa3") == nullptr);
                selectEntry(2);
                return false;
            }
            if (strstr(title, "SELECT PLAYER TO EXCHANGE") != nullptr && !pickedExchangePlayer) {
                pickedExchangePlayer = true;
                // Entry 7 corresponds to player slot 3 on buyer squad
                selectEntry(7);
                return false;
            }
        }
        if (m->numEntries > 12 && m->entries()[12].type == kEntryString &&
            m->entries()[12].string() && strstr(m->entries()[12].string(), "CONFIRM OFFER") != nullptr) {
            if (!clickedPlusExchange) {
                clickedPlusExchange = true;
                selectEntry(10); // Green '+' exchange button
                return false;
            }
            if (!clickedConfirm) {
                clickedConfirm = true;
                selectEntry(12); // Confirm offer
                return false;
            }
        }
        SetExitMenuFlag();
        return false;
    });

    save68kRegisters();
    D0 = kGalatasaray;
    D1 = 5;
    cseg_54C1E();
    restore68kRegisters();
    SWOS_UnitTest::setMenuCallback();

    assertTrue(clickedPlusExchange);
    assertTrue(pickedExchangePlayer);
    assertTrue(clickedConfirm);
    assertTrue(sawBidPrompt);

    auto pending = mgr.getPendingOffersForPlayer(1);
    assertEqual(static_cast<int>(pending.size()), 1);
    assertEqual(pending[0].playerOrdinal, 5);
    assertTrue(pending[0].exchangeOrdinal1 >= 0);
    assertTrue(pending[0].exchangeName1[0] != '\0');
    int exchOrd = pending[0].exchangeOrdinal1;

    // 2. Even when seller only has 16 players, 1-for-1 exchange keeps net count at 16 and must be allowed!
    mgr.player(1).careerNumPlayers = 16;
    std::string reason;
    assertTrue(mgr.canAcceptTransferOffer(pending[0].id, &reason));

    char expectedWantedName[24] = {};
    char expectedExchName[24] = {};
    snprintf(expectedWantedName, sizeof(expectedWantedName), "%.22s",
             getPlayerSlot(mgr.player(1).careerTeam, 5)->playerName);
    snprintf(expectedExchName, sizeof(expectedExchName), "%.22s",
             getPlayerSlot(mgr.player(0).careerTeam, exchOrd)->playerName);

    int32_t p0CountBefore = mgr.player(0).careerNumPlayers;
    int32_t p1BalanceBefore = mgr.player(1).newBalance;
    int32_t p0BalanceBefore = mgr.player(0).newBalance;
    uint32_t bidCash = pending[0].offerAmount;

    assertTrue(mgr.resolveTransferOffer(pending[0].id, true));
    assertEqual(static_cast<int>(mgr.player(1).careerNumPlayers), 16);
    assertEqual(static_cast<int>(mgr.player(0).careerNumPlayers), p0CountBefore);
    assertEqual(mgr.player(0).newBalance, p0BalanceBefore - static_cast<int32_t>(bidCash));
    assertEqual(mgr.player(1).newBalance, p1BalanceBefore + static_cast<int32_t>(bidCash));

    auto squadContainsName = [](const HotseatPlayerContext& club, const char *name) {
        int limit = std::min<int>(club.careerNumPlayers, kCurrentMatchPlayersSize);
        for (int s = 0; s < limit; ++s) {
            uint8_t ord = club.currentMatchPlayers[s];
            const auto *pf = getPlayerSlot(club.careerTeam, ord);
            if (pf && strncmp(pf->playerName, name, 22) == 0)
                return true;
        }
        return false;
    };
    assertTrue(squadContainsName(mgr.player(0), expectedWantedName));
    assertTrue(squadContainsName(mgr.player(1), expectedExchName));

    // Verify trainingMatchSavedPlayers and TeamFile::playerNumbers stay synced on both clubs
    assertEqual(memcmp(reinterpret_cast<const TeamFile *>(mgr.player(0).careerTeam)->playerNumbers,
                       mgr.player(0).currentMatchPlayers, 16), 0);
    assertEqual(memcmp(reinterpret_cast<const TeamFile *>(mgr.player(1).careerTeam)->playerNumbers,
                       mgr.player(1).currentMatchPlayers, 16), 0);

    // 3. Verify 2-wanted + 2-exchange persistence across V6 save/load and resolution at 30 players
    mgr.player(0).careerNumPlayers = 30;
    mgr.player(1).careerNumPlayers = 20;
    for (uint8_t k = 0; k < 30; ++k) {
        mgr.player(0).currentMatchPlayers[k] = k;
        mgr.player(0).trainingMatchSavedPlayers[k] = k;
        auto *pf = const_cast<PlayerFile *>(getPlayerSlot(mgr.player(0).careerTeam, k));
        snprintf(pf->playerName, sizeof(pf->playerName), "P0 SLOT %d", k);
    }
    for (uint8_t k = 0; k < 20; ++k) {
        mgr.player(1).currentMatchPlayers[k] = k;
        mgr.player(1).trainingMatchSavedPlayers[k] = k;
        auto *pf = const_cast<PlayerFile *>(getPlayerSlot(mgr.player(1).careerTeam, k));
        snprintf(pf->playerName, sizeof(pf->playerName), "P1 SLOT %d", k);
    }
    mgr.loadSwosContext(mgr.player(0), false);

    HotseatCareerManager::TransferOffer multiExch;
    multiExch.fromPlayerIndex = 0;
    multiExch.toPlayerIndex = 1;
    multiExch.playerOrdinal = 4;
    multiExch.extraWantedOrdinal = 7;
    multiExch.exchangeOrdinal1 = 27;
    multiExch.exchangeOrdinal2 = 28;
    multiExch.offerAmount = 400'000;
    snprintf(multiExch.playerName, sizeof(multiExch.playerName), "P1 SLOT 4");
    snprintf(multiExch.extraWantedName, sizeof(multiExch.extraWantedName), "P1 SLOT 7");
    snprintf(multiExch.exchangeName1, sizeof(multiExch.exchangeName1), "P0 SLOT 27");
    snprintf(multiExch.exchangeName2, sizeof(multiExch.exchangeName2), "P0 SLOT 28");
    int multiId = mgr.queueTransferOffer(multiExch);

    const std::string v6Path = "test_hotseat_v6_exchange.hot";
    assertTrue(mgr.saveToFile(v6Path));
    mgr.reset();
    assertTrue(mgr.loadFromFile(v6Path));
    std::remove(v6Path.c_str());

    auto loadedPending = mgr.getPendingOffersForPlayer(1);
    assertEqual(static_cast<int>(loadedPending.size()), 1);
    assertEqual(loadedPending[0].id, multiId);
    assertEqual(loadedPending[0].extraWantedOrdinal, 7);
    assertEqual(loadedPending[0].exchangeOrdinal1, 27);
    assertEqual(loadedPending[0].exchangeOrdinal2, 28);
    assertTrue(mgr.resolveTransferOffer(multiId, true));
    assertEqual(static_cast<int>(mgr.player(0).careerNumPlayers), 30);
    assertEqual(static_cast<int>(mgr.player(1).careerNumPlayers), 20);
    assertTrue(squadContainsName(mgr.player(0), "P1 SLOT 4"));
    assertTrue(squadContainsName(mgr.player(0), "P1 SLOT 7"));
    assertTrue(squadContainsName(mgr.player(1), "P0 SLOT 27"));
    assertTrue(squadContainsName(mgr.player(1), "P0 SLOT 28"));

    // 4. Regression for Crash #1 (1ee997cd / cseg_639F9): Player-to-CPU exchange after opponent plays
    // First simulate opponent P1 playing a match via cseg_63F6F() (short 16-player squad overwriting global VM)
    extern void cseg_63F6F();
    extern void cseg_592D5();
    mgr.player(1).careerNumPlayers = 16;
    for (uint8_t k = 0; k < 16; ++k) {
        mgr.player(1).currentMatchPlayers[k] = k;
        mgr.player(1).trainingMatchSavedPlayers[k] = k;
    }
    mgr.switchToPlayer(1, true);
    save68kRegisters();
    cseg_63F6F();
    restore68kRegisters();
    mgr.saveCurrentSwosContext(mgr.player(1));

    // Switch back to P0 and exchange P0's high-ordinal player (27) with a CPU club (team 0x0b26 = Gaziantepspor)
    mgr.switchToPlayer(0, true);
    assertEqual(static_cast<int>(SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers + 27]), 27);

    // Find another domestic Turkish CPU club in g_selectedTeams (not P0 or P1)
    uint16_t cpuTeamId = 0;
    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    int selCount = std::min<int>(swos.g_numSelectedTeams, 68400 / static_cast<int>(sizeof(TeamFile)));
    for (int t = 0; t < selCount; ++t) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[t]);
        if (tid != 0 && mgr.findHotseatPlayerByTeamId(static_cast<int16_t>(tid)) < 0) {
            cpuTeamId = tid;
            break;
        }
    }
    assertTrue(cpuTeamId != 0);

    swos.teamOffering = cpuTeamId;
    swos.dseg_17A6CE = 2;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumExtraPlayersWanted]) = -1;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17A6D2]) = 27;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17A6D4]) = -1;
    swos.offerAmount = 50;

    save68kRegisters();
    cseg_592D5(); // Must execute cseg_56379 + cseg_56129 -> cseg_52EA6 -> cseg_639F9 without trapping!
    restore68kRegisters();

    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]), static_cast<uint16_t>(30));

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatTrialDepartureAfterOpponentMatch()
{
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;

    // Regression for Crash #2 (2fd4c1ce): P1 removes ordinal 13 / plays match, then P0 loses trialist at ordinal 13
    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    auto& mgr = HotseatCareerManager::get();

    extern void cseg_52EA6();
    extern void cseg_52A3E();
    extern void cseg_63F6F();

    // Ensure P0 has > 16 players and slot 13 is a valid player
    mgr.switchToPlayer(0, true);
    uint16_t p0Count = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    assertTrue(p0Count > 16);
    uint8_t p0Ord13 = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 13];
    mgr.saveCurrentSwosContext(mgr.player(0));

    // Switch to P1, play/save trainingMatchSavedPlayers, and depart player at ordinal 13 from P1!
    mgr.switchToPlayer(1, true);
    uint16_t p1CountBefore = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    assertTrue(p1CountBefore > 16);
    uint8_t p1Ord13 = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 13];

    save68kRegisters();
    cseg_63F6F();
    D1 = p1Ord13;
    cseg_52EA6();
    restore68kRegisters();
    mgr.saveCurrentSwosContext(mgr.player(1));

    // Verify P1 no longer has p1Ord13 in P1's trainingMatchSavedPlayers
    uint16_t p1CountAfter = mgr.player(1).careerNumPlayers;
    assertEqual(p1CountAfter, static_cast<uint16_t>(p1CountBefore - 1));
    bool p1StillHas = false;
    for (uint16_t k = 0; k < p1CountAfter; ++k) {
        if (mgr.player(1).trainingMatchSavedPlayers[k] == p1Ord13)
            p1StillHas = true;
    }
    assertFalse(p1StillHas);

    // Switch back to P0: P0's canonical trainingMatchSavedPlayers MUST be restored and still contain p0Ord13!
    mgr.switchToPlayer(0, true);
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]), p0Count);

    auto *trialPf = const_cast<PlayerFile *>(getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], p0Ord13));
    trialPf->positionAndFace = static_cast<byte>(trialPf->positionAndFace | 0x02u);
    SwosVM::g_memByte[CareerOffsets::kDsegD6C26 + p0Ord13] = 1;

    // Trigger trialist departure on P0's ordinal 13 -> must NOT crash inside cseg_639F9!
    save68kRegisters();
    D1 = p0Ord13;
    cseg_52EA6();
    restore68kRegisters();

    uint16_t p0CountAfter = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    assertEqual(p0CountAfter, static_cast<uint16_t>(p0Count - 1));

    // Also drop down to 16 and verify in-place replacement creates a named trialist while keeping trainingMatchSavedPlayers valid
    while (*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]) > 16) {
        uint8_t tailOrd = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 16];
        save68kRegisters();
        D1 = tailOrd;
        cseg_52EA6();
        restore68kRegisters();
    }
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]), static_cast<uint16_t>(16));

    save68kRegisters();
    D1 = 5;
    cseg_52EA6();
    restore68kRegisters();

    uint8_t replacementOrd = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 5];
    const auto *repPf = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], replacementOrd);
    assertTrue(hasValidPlayerNameForTest(repPf));

    // Switch back to P1 and then back to P0 to confirm neither player's state leaked
    mgr.saveCurrentSwosContext(mgr.player(0));
    mgr.switchToPlayer(1, true);
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]), p1CountAfter);
    mgr.switchToPlayer(0, true);
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]), static_cast<uint16_t>(16));

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatEmergencyLoanForInjuredSquad()
{
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;

    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    auto& mgr = HotseatCareerManager::get();

    extern void ShowPlayMatchMenu();
    extern void cseg_52EA6();
    extern void GetPlayer();

    // Helper: ensure a 16-player matchday layout with specified fit GKs + outfielders and an injured player at slot 4
    auto configure16PlayerSquadWithInjury = [&](int playerIdx, bool injureSecondGkInsteadOfSlot4) {
        mgr.switchToPlayer(playerIdx, true);
        auto& ctx = mgr.player(playerIdx);
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]) = 16;
        uint8_t *teamBuf = &SwosVM::g_memByte[CareerOffsets::kCareerTeam];
        for (int i = 0; i < 16; ++i) {
            uint8_t ord = static_cast<uint8_t>(i);
            SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + i] = ord;
            SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers + i] = ord;
            SwosVM::g_memByte[CareerOffsets::kDsegD6C26 + ord] = 0;
            auto *pf = const_cast<PlayerFile *>(getPlayerSlot(teamBuf, ord));
            pf->cardsInjuries = 0;
            pf->positionAndFace = static_cast<byte>(pf->positionAndFace & ~0x07u);
            pf->someFlag = (i == 13 || i == 15) ? 0x9A : 0; // mirror RES labels from screenshot
            // Slot 0 & 11 = GK (pos 0); others = outfielder (pos != 0)
            uint8_t pos = (i == 0 || i == 11) ? 0u : static_cast<uint8_t>(((pf->positionAndFace >> 5) != 0) ? (pf->positionAndFace >> 5) : 3u);
            pf->positionAndFace = static_cast<byte>((pos << 5) | (pf->positionAndFace & 0x18u));
        }
        int injOrd = injureSecondGkInsteadOfSlot4 ? 11 : 4;
        const_cast<PlayerFile *>(getPlayerSlot(teamBuf, injOrd))->cardsInjuries = 0xC0; // '?' injured (unknown duration)
        mgr.saveCurrentSwosContext(ctx);
        mgr.syncPlayersWithSelectedTeams(true);
    };

    auto findVmTeamOfs = [&](int16_t teamId) -> uint32_t {
        uint32_t base = static_cast<uint32_t>(swos.selTeamsPtr);
        const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
        int count = std::min<int>(swos.g_numSelectedTeams, 68400 / static_cast<int>(sizeof(TeamFile)));
        for (int t = 0; t < count; ++t) {
            if (*reinterpret_cast<const int16_t *>(&selTeams[t]) == teamId)
                return base + static_cast<uint32_t>(t * sizeof(TeamFile));
        }
        return 0;
    };

    // 1. Active team (P0 Fenerbahce): 16 players, 2 fit GKs, 13 fit outfielders, slot 4 injured ('?'), maxSubs=5
    configure16PlayerSquadWithInjury(0, false);
    mgr.switchToPlayer(0, true);
    uint32_t p1VmOfs = findVmTeamOfs(kGalatasaray);
    assertTrue(p1VmOfs != 0);

    int step1 = 0;
    bool playMatchEnabledAfterSwap1 = false;
    SWOS_UnitTest::setMenuCallback([&] {
        auto m = getCurrentMenu();
        assertTrue(m && m->numEntries > 29);
        ++step1;
        if (step1 == 1) {
            // An emergency LOAN player (17th row, entry 29) must have been added and made visible; PLAY MATCH still hidden while slot 4 is injured
            assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]), static_cast<uint16_t>(17));
            assertEqual(static_cast<int>(m->entries()[9].invisible), 1);
            assertEqual(static_cast<int>(m->entries()[29].invisible), 0);

            uint8_t loanOrd = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 16];
            const auto *loanPf = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], loanOrd);
            assertTrue(hasValidPlayerNameForTest(loanPf));
            assertTrue((loanPf->positionAndFace & 0x02u) != 0); // LOAN flag bit preserved (not overwritten as TRIAL)
            assertEqual(static_cast<int>(loanPf->someFlag), 0);
            assertEqual(static_cast<int>(SwosVM::g_memByte[CareerOffsets::kDsegD6C26 + loanOrd]), 0);
            assertTrue((loanPf->positionAndFace >> 5) != 0);
            assertTrue(loanPf->price >= 2);

            swos.longFireFlag = 0;
            selectEntry(17); // select slot 4 injured player
            return false;
        }
        if (step1 == 2) {
            swos.longFireFlag = 0;
            selectEntry(29); // swap with 17th LOAN player
            return false;
        }
        playMatchEnabledAfterSwap1 = (m->entries()[9].invisible == 0);
        assertTrue(playMatchEnabledAfterSwap1);
        swos.pl1Fire = 1;
        swos.longFireFlag = 0;
        selectEntry(9); // PLAY MATCH
        return false;
    });

    swos.g_gameType = kGameTypeCareer;
    swos.g_trainingGame = 0;
    swos.g_exitGameFlag = 0;
    D0 = 3;
    D1 = 5;
    D2 = 0;
    D5 = 1;
    D6 = 1;
    A1 = static_cast<uint32_t>(CareerOffsets::kCareerTeam);
    A2 = p1VmOfs;
    ShowPlayMatchMenu();
    assertTrue(playMatchEnabledAfterSwap1);
    assertEqual(static_cast<int>(swos.g_exitGameFlag), 0);
    mgr.saveCurrentSwosContext(mgr.player(0));

    // 2. Non-active hotseat team (P0 Fenerbahce in g_selectedTeams) while P1 (Galatasaray) is loaded in VM careerTeam!
    configure16PlayerSquadWithInjury(0, false);
    mgr.switchToPlayer(1, true);
    uint16_t p1InitialCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    uint32_t p0VmOfs = findVmTeamOfs(kFenerbahce);
    assertTrue(p0VmOfs != 0);

    int step2 = 0;
    bool sawFenerbahceOwnLoan = false;
    bool playMatchEnabledAfterSwap2 = false;
    SWOS_UnitTest::setMenuCallback([&] {
        auto m = getCurrentMenu();
        assertTrue(m && m->numEntries > 29);
        ++step2;
        if (step2 == 1) {
            assertEqual(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]), kFenerbahce);
            assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]), static_cast<uint16_t>(17));
            assertEqual(static_cast<int>(m->entries()[29].invisible), 0);
            uint8_t loanOrd = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 16];
            const auto *loanPf = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], loanOrd);
            assertTrue(hasValidPlayerNameForTest(loanPf));
            assertTrue((loanPf->positionAndFace & 0x02u) != 0);
            assertEqual(static_cast<int>(loanPf->someFlag), 0);
            sawFenerbahceOwnLoan = true;

            swos.longFireFlag = 0;
            selectEntry(17); // select slot 4 injured player
            return false;
        }
        if (step2 == 2) {
            swos.longFireFlag = 0;
            selectEntry(29); // swap with 17th LOAN player
            return false;
        }
        playMatchEnabledAfterSwap2 = (m->entries()[9].invisible == 0);
        assertTrue(playMatchEnabledAfterSwap2);
        swos.pl1Fire = 1;
        swos.longFireFlag = 0;
        selectEntry(9);
        return false;
    });

    swos.g_gameType = kGameTypeCareer;
    swos.g_trainingGame = 0;
    swos.g_exitGameFlag = 0;
    D0 = 3;
    D1 = 5;
    D2 = 0;
    D5 = 1;
    D6 = 1;
    A1 = p0VmOfs; // non-active Fenerbahce!
    A2 = static_cast<uint32_t>(CareerOffsets::kCareerTeam); // Galatasaray opponent!
    ShowPlayMatchMenu();

    assertTrue(sawFenerbahceOwnLoan);
    assertTrue(playMatchEnabledAfterSwap2);
    // VM active player must be restored to P1 (Galatasaray) with zero squad-count leakage!
    assertEqual(mgr.activePlayerIndex(), 1);
    assertEqual(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]), kGalatasaray);
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]), p1InitialCount);

    // P0's chosen 16 matchday players (with the LOAN player in slot 4) must be packed into p0VmOfs, and GetPlayer(kFenerbahce, 4) must see the fit LOAN player
    assertEqual(mgr.player(0).careerNumPlayers, static_cast<uint16_t>(17));
    uint8_t packedLoanOrd = mgr.player(0).currentMatchPlayers[4];
    assertEqual(static_cast<int>(packedLoanOrd), 16);
    assertEqual(static_cast<int>(mgr.player(0).currentMatchPlayers[16]), 4);

    save68kRegisters();
    D0 = kFenerbahce;
    D1 = 4;
    GetPlayer();
    const auto *resolvedMatchdaySlot4 = reinterpret_cast<const PlayerFile *>(
        &SwosVM::g_memByte[A0.asDword() + sizeof(TeamFileHeader)]);
    assertEqual(static_cast<int>(resolvedMatchdaySlot4->cardsInjuries), 0);
    assertTrue((resolvedMatchdaySlot4->positionAndFace & 0x02u) != 0);
    restore68kRegisters();

    // Simulate a post-match card written to packed slot 4 during H2H match and unpack via Hotseat_OnAfterPlayMatch()
    auto *packedTeam = reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[p0VmOfs]);
    packedTeam->players[4].cardsInjuries = 0x01; // 1 yellow card
    SWOS::Hotseat_OnAfterPlayMatch();
    const auto *unpackedLoanPf = getPlayerSlot(mgr.player(0).careerTeam, packedLoanOrd);
    const auto *unpackedInjPf = getPlayerSlot(mgr.player(0).careerTeam, 4);
    assertEqual(static_cast<int>(unpackedLoanPf->cardsInjuries), 0x01);
    assertEqual(static_cast<int>(unpackedInjPf->cardsInjuries), 0xC0);

    // 3. Single fit GK (2nd GK injured) + 14 fit outfielders with maxSubs=5 -> emergency LOAN GK added;
    //    meanwhile cseg_52EA6 transfer departure at 16 players STILL creates a named TRIAL player (0x9B).
    configure16PlayerSquadWithInjury(0, true);
    mgr.switchToPlayer(0, true);
    int step3 = 0;
    SWOS_UnitTest::setMenuCallback([&] {
        auto m = getCurrentMenu();
        assertTrue(m && m->numEntries > 29);
        ++step3;
        if (step3 == 1) {
            assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]), static_cast<uint16_t>(17));
            uint8_t gkLoanOrd = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 16];
            const auto *gkLoanPf = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], gkLoanOrd);
            assertTrue(hasValidPlayerNameForTest(gkLoanPf));
            assertEqual(static_cast<int>(gkLoanPf->positionAndFace >> 5), 0); // GK
            assertTrue((gkLoanPf->positionAndFace & 0x02u) != 0);
            assertEqual(static_cast<int>(gkLoanPf->someFlag), 0);
        }
        swos.longFireFlag = 0;
        selectEntry(8); // EXIT
        return false;
    });

    swos.g_gameType = kGameTypeCareer;
    swos.g_trainingGame = 0;
    swos.g_exitGameFlag = 0;
    D0 = 3;
    D1 = 5;
    D2 = 0;
    D5 = 1;
    D6 = 1;
    A1 = static_cast<uint32_t>(CareerOffsets::kCareerTeam);
    A2 = p1VmOfs;
    ShowPlayMatchMenu();

    // Transfer departure at 16 players must STILL create a named TRIAL player (someFlag=0x9B, not LOAN=0x02)
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]) = 16;
    uint8_t depOrd = SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + 5];
    save68kRegisters();
    D1 = depOrd;
    cseg_52EA6();
    restore68kRegisters();
    const auto *depTrialPf = getPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], depOrd);
    assertTrue(hasValidPlayerNameForTest(depTrialPf));
    assertEqual(static_cast<int>(depTrialPf->someFlag), 0x9B);
    assertEqual(static_cast<int>(depTrialPf->positionAndFace & 0x02u), 0);
    assertTrue(static_cast<int8_t>(SwosVM::g_memByte[CareerOffsets::kDsegD6C26 + depOrd]) < 0);

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

namespace SWOS {
    int WriteFile_REAL();
}

void CareerTest::testHotseatSaveAndLoadCareerFile()
{
    constexpr int16_t kFenerbahce = 0x0926;    // ECC (0)
    constexpr int16_t kGalatasaray = 0x0a26;   // Cup Winners Cup (1)
    constexpr int16_t kBesiktas = 0x0526;      // UEFA Cup (2)
    constexpr int16_t kGaziantepspor = 0x0b26; // None (0xFFFF)
    constexpr size_t kCompCountryNumberOfs = 131744; // db

    extern void LoadCareerFile();
    // WriteFile is unconditionally intercepted by mockFile.cpp unless WriteFile_REAL is called directly
    auto saveRealCareerFiles = [&](const char *filename) {
        uint32_t saveEndOfs = 131757u + 2u + static_cast<uint32_t>(swos.g_numSelectedTeams) * 684u;
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDseg17A3D0], filename, std::strlen(filename) + 1);
        save68kRegisters();
        A0 = static_cast<uint32_t>(CareerOffsets::kDseg17A3D0);
        A1 = static_cast<uint32_t>(CareerOffsets::kTmdCareerCount); // careerFileBuffer
        D1 = saveEndOfs - static_cast<uint32_t>(CareerOffsets::kTmdCareerCount);
        int res = SWOS::WriteFile_REAL();
        assertEqual(res, 0);
        restore68kRegisters();
    };

    const char *kSaveName = "THSLOAD.CAR";
    const std::string carPath = pathInRootDir("THSLOAD.CAR");
    const std::string hotPath = pathInRootDir("THSLOAD.hot");
    const std::string noExtPath = pathInRootDir("THSLOAD");
    auto cleanupFiles = [&]() {
        std::remove(carPath.c_str());
        std::remove(hotPath.c_str());
        std::remove(noExtPath.c_str());
    };
    cleanupFiles();

    struct Scenario {
        int16_t p0Team;
        int16_t p1Team;
        int activeIdx;
        uint16_t expectedEuroVal;
    };
    const Scenario scenarios[] = {
        { kFenerbahce, kGalatasaray, 1, 1 },      // Cup Winners Cup
        { kGalatasaray, kBesiktas, 1, 2 },        // UEFA Cup
        { kFenerbahce, kGaziantepspor, 1, 0xFFFF }, // Not in Europe
        { kFenerbahce, kGalatasaray, 0, 0 },      // Champions Cup
    };

    for (const auto& sc : scenarios) {
        SWOS_UnitTest::setMenuCallback([&] {
            SetExitMenuFlag();
            return true;
        });

        initTwoPlayerHotseatFromTeams(sc.p0Team, sc.p1Team);
        auto& mgr = HotseatCareerManager::get();

        // Turkey must have compCountryNumber = 38 (non-zero byte followed immediately by dseg_E092F word)
        assertEqual(static_cast<int>(SwosVM::g_memByte[kCompCountryNumberOfs]), 38);

        mgr.switchToPlayer(sc.activeIdx, true);
        int16_t expectedActiveTeam = sc.activeIdx == 0 ? sc.p0Team : sc.p1Team;
        int otherIdx = sc.activeIdx == 0 ? 1 : 0;
        int16_t expectedOtherTeam = otherIdx == 0 ? sc.p0Team : sc.p1Team;
        assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegE092F]), sc.expectedEuroVal);

        saveRealCareerFiles(kSaveName);

        assertTrue(fileExists(carPath.c_str()));
        assertTrue(fileExists(hotPath.c_str()));

        // Cold-boot state wipe before LoadCareerFile() -> ProcessCareerFile() -> StartContest()
        mgr.reset();
        assertFalse(mgr.isActive());
        swos.selTeamsPtr = 0;
        swos.g_numSelectedTeams = 0;
        swos.g_gameType = 0;
        SwosVM::g_memByte[kCompCountryNumberOfs] = 0;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegE092F]) = 0;
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]) = 0;
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA], 0, 24);
        std::memset(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers], 0, 20);
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);

        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDseg17A3D0], kSaveName, std::strlen(kSaveName) + 1);
        save68kRegisters();
        A0 = static_cast<uint32_t>(CareerOffsets::kDseg17A3D0);
        LoadCareerFile();
        bool loadOk = SwosVM::flags.zero && D0.asWord() == 0;
        restore68kRegisters();

        assertTrue(loadOk);
        assertEqual(static_cast<int>(SwosVM::g_memByte[kCompCountryNumberOfs]), 38);
        assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegE092F]), sc.expectedEuroVal);
        assertEqual(static_cast<int>(swos.g_gameType), static_cast<int>(kGameTypeCareer));
        assertTrue(swos.selTeamsPtr != 0);
        assertTrue(swos.g_numSelectedTeams > 0);
        assertTrue(*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA]) != 0);
        if (sc.expectedEuroVal != 0xFFFF) {
            assertTrue(*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) != 0);
        }

        assertTrue(mgr.isActive());
        assertEqual(mgr.numPlayers(), 2);
        assertEqual(mgr.activePlayerIndex(), sc.activeIdx);
        assertEqual(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]), expectedActiveTeam);

        // Render StartContest -> ContestMenu for loaded active player, then switch turn and render other player
        int contestFrames = 0;
        SWOS_UnitTest::setMenuCallback([&] {
            ++contestFrames;
            if (contestFrames >= 2) {
                SetExitMenuFlag();
                return true;
            }
            return false;
        });
        swos.g_exitMenu = 0;
        StartContest();
        assertTrue(contestFrames >= 2);

        mgr.switchToPlayer(otherIdx, true);
        assertEqual(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]), expectedOtherTeam);
        contestFrames = 0;
        swos.g_exitMenu = 0;
        A6 = 482233;
        SWOS::ShowMenu();
        assertTrue(contestFrames >= 2);

        // Overwrite existing THSLOAD.CAR + THSLOAD.hot via SelectFileToSaveDialog clicking SAVE (saveLabel)
        int savePlayerIdx = mgr.activePlayerIndex();
        int16_t saveTeamId = savePlayerIdx == 0 ? sc.p0Team : sc.p1Team;
        constexpr int32_t kUpdatedBalance = 9'876'543;
        *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]) = kUpdatedBalance;
        mgr.player(savePlayerIdx).newBalance = kUpdatedBalance;
        constexpr size_t kCompetitionFilenameOfs = 316642;
        std::memcpy(&SwosVM::g_memByte[kCompetitionFilenameOfs], kSaveName, std::strlen(kSaveName) + 1);

        SWOS_UnitTest::setMenuCallback([&] {
            auto m = getCurrentMenu();
            if (m) {
                for (int i = 0; i < m->numEntries; ++i) {
                    const auto& e = m->entries()[i];
                    if (e.invisible == 0 && e.type == kEntryString && e.string() &&
                        std::strcmp(e.string(), "SAVE") == 0) {
                        selectEntry(i);
                        return false;
                    }
                }
            }
            SetExitMenuFlag();
            return false;
        });

        save68kRegisters();
        A0 = static_cast<uint32_t>(kCompetitionFilenameOfs);
        D0 = ('.' << 24) | ('C' << 16) | ('A' << 8) | 'R';
        A1 = static_cast<uint32_t>(CareerOffsets::kDseg17A3D0);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDseg17A3D0], "SAVE CAREER", 12);
        SWOS::SelectFileToSaveDialog();
        std::string chosenSaveName = A0.asPtr();
        bool dialogOk = SwosVM::flags.zero && D0.asWord() == 0;
        restore68kRegisters();
        SWOS_UnitTest::setMenuCallback();

        assertTrue(dialogOk);
        assertEqual(chosenSaveName, std::string(kSaveName));
        saveRealCareerFiles(chosenSaveName.c_str());
        assertFalse(fileExists(noExtPath.c_str()));

        // Reload overwritten save after wiping VM and confirm active player + updated balance persisted
        mgr.reset();
        swos.selTeamsPtr = 0;
        swos.g_numSelectedTeams = 0;
        swos.g_gameType = 0;
        *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]) = 0;
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDseg17A3D0], kSaveName, std::strlen(kSaveName) + 1);
        save68kRegisters();
        A0 = static_cast<uint32_t>(CareerOffsets::kDseg17A3D0);
        LoadCareerFile();
        loadOk = SwosVM::flags.zero && D0.asWord() == 0;
        restore68kRegisters();

        assertTrue(loadOk);
        assertEqual(mgr.activePlayerIndex(), savePlayerIdx);
        assertEqual(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]), saveTeamId);
        assertEqual(*reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]), kUpdatedBalance);
        assertEqual(mgr.player(savePlayerIdx).newBalance, kUpdatedBalance);

        mgr.reset();
        SWOS_UnitTest::setMenuCallback();
        cleanupFiles();
    }
}

namespace {

void seedJobOfferInBuffer(uint8_t *buf, int slot, uint16_t teamId, const char *teamName, int32_t funds)
{
    uint8_t *entry = buf + static_cast<size_t>(slot) * 26;
    std::memset(entry, 0, 26);
    *reinterpret_cast<uint16_t *>(entry) = teamId;
    size_t len = std::min(std::strlen(teamName), size_t{17});
    std::memcpy(entry + 2, teamName, len);
    *reinterpret_cast<int32_t *>(entry + 20) = funds;
}

std::vector<uint16_t> collectJobOfferTeamIds(const uint8_t *buf, int16_t count)
{
    std::vector<uint16_t> ids;
    int n = std::clamp<int>(count, 0, 5);
    for (int i = 0; i < n; ++i) {
        ids.push_back(*reinterpret_cast<const uint16_t *>(buf + static_cast<size_t>(i) * 26));
    }
    return ids;
}

const TeamFile *findSelectedTeamById(int16_t tid)
{
    if (!swos.selTeamsPtr || swos.g_numSelectedTeams <= 0)
        return nullptr;
    const auto *teams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    int count = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
    for (int i = 0; i < count; ++i) {
        if (*reinterpret_cast<const int16_t *>(&teams[i]) == tid)
            return &teams[i];
    }
    return nullptr;
}

void advanceHotseatSeasonToFinish()
{
    int steps = 0;
    bool finished = false;
    SWOS_UnitTest::setMenuCallback([&] {
        resetMockSdlMixer();
        if (++steps > 3000) {
            SetExitMenuFlag();
            return false;
        }
        auto m = getCurrentMenu();
        auto packed = getCurrentPackedMenu();
        uint32_t packedOfs = (packed && SwosVM::isSwosPtr(packed)) ? SwosVM::ptrToOffset(packed) : 0;
        if (packedOfs == CareerOffsets::kContestMenu) {
            uint16_t seasonFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
            if (seasonFin != 0) {
                finished = true;
                SetExitMenuFlag();
                return false;
            }
            selectEntry(10); // NEXT MATCH
            return false;
        }
        if (m) {
            for (int i = 0; i < m->numEntries; ++i) {
                const auto& e = m->entries()[i];
                if (!e.invisible && !e.disabled && e.onSelect && e.type == kEntryString && e.string()) {
                    const char *s = e.string();
                    if (strstr(s, "RESULT")) {
                        selectEntry(i);
                        return false;
                    }
                }
            }
        }
        SetExitMenuFlag();
        return false;
    });

    swos.g_exitMenu = 0;
    A6 = CareerOffsets::kContestMenu;
    SWOS::ShowMenu();

    assertTrue(finished);
    assertTrue(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) != 0);
}

} // namespace

void CareerTest::testHotseatEndOfSeasonMenuSequenceAndSharedJobOfferDedup()
{
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;
    constexpr int16_t kBesiktas = 0x0526;
    constexpr int16_t kGaziantepspor = 0x0b26;
    constexpr int16_t kManUtdForeign = 0x0801;
    constexpr int16_t kEnglandNational = 0x0050;

    extern void AcceptJobOffer();
    extern void CareerOverInit();
    extern void CareerOverFinish();
    extern void ContinueCareer();
    extern void ExitCareerMenu();

    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    advanceHotseatSeasonToFinish();
    auto& mgr = HotseatCareerManager::get();
    mgr.switchToPlayer(0, true);

    // Start season-end while P1 (index 1) was active to verify CareerOver still starts at P0 (index 0)
    mgr.switchToPlayer(1, true);
    mgr.onSeasonFinishedForAllPlayers();
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]) = 1;

    // Also verify IsNewJobOffer filtering allows cross-country club offers while rejecting national and human clubs in hotseat
    assertTrue(mgr.isValidHotseatJobOffer(kManUtdForeign, 0));
    assertFalse(mgr.isValidHotseatJobOffer(kEnglandNational, 0));
    assertFalse(mgr.isValidHotseatJobOffer(kGalatasaray, 0));
    assertTrue(mgr.isValidHotseatJobOffer(kBesiktas, 0));

    // Seed P0 and P1 job offers directly (avoiding random RNG dependency) including shared Besiktas offer
    auto& p0 = mgr.player(0);
    p0.retired = 1;
    p0.youreSacked = 0;
    p0.numNewJobOffers = 3;
    std::memset(p0.newJobOffers, 0, kJobOffersBufferSize);
    seedJobOfferInBuffer(p0.newJobOffers, 0, kBesiktas, "BESIKTAS", 2'000'000);
    seedJobOfferInBuffer(p0.newJobOffers, 1, kEnglandNational, "ENGLAND", 5'000'000); // filtered (national team)
    seedJobOfferInBuffer(p0.newJobOffers, 2, kGalatasaray, "GALATASARAY", 2'000'000); // filtered (human club)
    p0.numJobOffers = 0;
    std::memset(p0.jobOffers, 0, kJobOffersBufferSize);

    auto& p1 = mgr.player(1);
    p1.retired = 1;
    p1.youreSacked = 0;
    p1.numNewJobOffers = 2;
    std::memset(p1.newJobOffers, 0, kJobOffersBufferSize);
    seedJobOfferInBuffer(p1.newJobOffers, 0, kBesiktas, "BESIKTAS", 2'000'000);
    seedJobOfferInBuffer(p1.newJobOffers, 1, kGaziantepspor, "GAZIANTEPSPOR", 1'500'000);
    p1.numJobOffers = 1;
    std::memset(p1.jobOffers, 0, kJobOffersBufferSize);
    seedJobOfferInBuffer(p1.jobOffers, 0, kBesiktas, "BESIKTAS", 2'000'000);

    // Sync active P1 VM state before triggering CareerOverMenu via ContestMenuInit -> CareerOverInit
    mgr.loadSwosContext(p1, true);

    std::vector<int> careerOverPlayersSeen;
    bool acceptedP0Besiktas = false;
    bool checkedP1AfterSwitch = false;
    bool transitionedToSeason2 = false;

    SWOS_UnitTest::setMenuCallback([&] {
        auto m = getCurrentMenu();
        auto packed = getCurrentPackedMenu();
        if (!m || !packed || !SwosVM::isSwosPtr(packed)) {
            SetExitMenuFlag();
            return false;
        }
        uint32_t ofs = SwosVM::ptrToOffset(packed);
        if (ofs == CareerOffsets::kCareerOverMenu) {
            int curP = mgr.activePlayerIndex();
            careerOverPlayersSeen.push_back(curP);

            if (curP == 0 && !acceptedP0Besiktas) {
                // Verify P0 has blinking JOB OFFERS (0x2000) and invalid offers were sanitized out
                assertEqual(static_cast<int>(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers])), 1);
                assertEqual(static_cast<int>(m->entries()[5].disabled), 0);
                assertTrue((m->entries()[5].stringFlags & 0x2000u) != 0);

                // Accept P0's Besiktas job offer
                p0.teamNoOfferAccepted = kBesiktas;
                p0.teamFundsAccepted = 2'000'000;
                *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = kBesiktas;
                *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = 2'000'000;
                *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]) = 0;
                *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]) = 0;
                mgr.onAfterAcceptJobOffer();
                acceptedP0Besiktas = true;

                // Besiktas must immediately be removed from P1's newJobOffers & jobOffers!
                auto p1New = collectJobOfferTeamIds(mgr.player(1).newJobOffers, mgr.player(1).numNewJobOffers);
                assertEqual(static_cast<int>(p1New.size()), 1);
                assertEqual(p1New[0], static_cast<uint16_t>(kGaziantepspor));
                assertEqual(static_cast<int>(mgr.player(1).numJobOffers), 0);

                // Click CONTINUE CAREER for P0 -> must advance to P1's careerOverMenu (NOT next season yet!)
                selectEntry(6);
                return false;
            }

            if (curP == 1 && !checkedP1AfterSwitch) {
                checkedP1AfterSwitch = true;
                // Verify P1's careerOverMenu opened cleanly with "JOB OFFERS" reset & blinking, Besiktas stripped
                assertEqual(static_cast<int>(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers])), 1);
                assertEqual(static_cast<int>(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers])), 0);
                assertEqual(static_cast<int>(m->entries()[5].disabled), 0);
                assertTrue((m->entries()[5].stringFlags & 0x2000u) != 0);
                assertEqual(static_cast<uint32_t>(m->entries()[5].fg.string.getRaw()), static_cast<uint32_t>(CareerOffsets::kAJobOffers));

                // Test V7 hotseat save & reload mid-end-of-season
                const std::string hotV7Path = pathInRootDir("THS_EOS_V7.hot");
                assertTrue(mgr.saveToFile(hotV7Path));
                int savedP0Accept = mgr.player(0).teamNoOfferAccepted;
                mgr.reset();
                assertTrue(mgr.loadFromFile(hotV7Path));
                std::remove(hotV7Path.c_str());
                assertTrue(mgr.isEndSeasonActive());
                assertEqual(mgr.endSeasonPlayerIndex(), 1);
                assertEqual(mgr.player(0).teamNoOfferAccepted, savedP0Accept);

                // P1 accepts Gaziantepspor and clicks CONTINUE CAREER -> transitions to Season 2
                *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = kGaziantepspor;
                *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = 1'500'000;
                mgr.onAfterAcceptJobOffer();
                selectEntry(6);
                transitionedToSeason2 = true;
                return false;
            }
        } else if (ofs == CareerOffsets::kContestMenu && transitionedToSeason2) {
            SetExitMenuFlag();
            return false;
        }
        SetExitMenuFlag();
        return false;
    });

    swos.g_exitMenu = 0;
    A6 = CareerOffsets::kContestMenu;
    SWOS::ShowMenu();

    assertTrue(acceptedP0Besiktas);
    assertTrue(checkedP1AfterSwitch);
    assertTrue(transitionedToSeason2);
    assertTrue(careerOverPlayersSeen.size() >= 2);
    assertEqual(careerOverPlayersSeen[0], 0); // P1 (index 0) MUST go first
    assertEqual(careerOverPlayersSeen.back(), 1); // P2 (index 1) MUST follow before Season 2
    assertEqual(mgr.player(0).teamId, kBesiktas);
    assertEqual(mgr.player(1).teamId, kGaziantepspor);

    // Part 2: Verify sacked player losing only shared offer sees "YOUR CAREER IS OVER" and surviving player continues
    mgr.reset();
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });
    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    advanceHotseatSeasonToFinish();
    mgr.onSeasonFinishedForAllPlayers();
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]) = 1;
    auto& sp0 = mgr.player(0);
    sp0.retired = 1;
    sp0.youreSacked = 0;
    sp0.numNewJobOffers = 1;
    sp0.numJobOffers = 0;
    std::memset(sp0.newJobOffers, 0, kJobOffersBufferSize);
    seedJobOfferInBuffer(sp0.newJobOffers, 0, kBesiktas, "BESIKTAS", 2'000'000);

    auto& sp1 = mgr.player(1);
    sp1.retired = 1;
    sp1.youreSacked = 1;
    sp1.numNewJobOffers = 1;
    sp1.numJobOffers = 0;
    std::memset(sp1.newJobOffers, 0, kJobOffersBufferSize);
    seedJobOfferInBuffer(sp1.newJobOffers, 0, kBesiktas, "BESIKTAS", 2'000'000); // same sole club!

    mgr.switchToPlayer(0, true);
    bool p1SawCareerOver = false;
    bool season2ReachedBySurvivor = false;
    int step = 0;
    SWOS_UnitTest::setMenuCallback([&] {
        auto m = getCurrentMenu();
        auto packed = getCurrentPackedMenu();
        if (!m || !packed || !SwosVM::isSwosPtr(packed)) {
            SetExitMenuFlag();
            return false;
        }
        uint32_t ofs = SwosVM::ptrToOffset(packed);
        if (ofs == CareerOffsets::kCareerOverMenu) {
            if (mgr.activePlayerIndex() == 0 && step == 0) {
                step = 1;
                *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = kBesiktas;
                *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = 2'000'000;
                mgr.onAfterAcceptJobOffer();
                selectEntry(6); // CONTINUE CAREER for P0
                return false;
            }
            if (mgr.activePlayerIndex() == 1 && step == 1) {
                step = 2;
                // Sacked P1 lost sole offer Besiktas -> "YOUR CAREER IS OVER" visible, Continue disabled
                assertEqual(static_cast<int>(m->entries()[3].invisible), 0);
                assertEqual(static_cast<int>(m->entries()[6].disabled), 1);
                p1SawCareerOver = true;
                selectEntry(8); // EXIT for sacked P1 -> P0 survives into Season 2
                season2ReachedBySurvivor = true;
                return false;
            }
        } else if (ofs == CareerOffsets::kContestMenu && season2ReachedBySurvivor) {
            SetExitMenuFlag();
            return false;
        }
        SetExitMenuFlag();
        return false;
    });
    swos.g_exitMenu = 0;
    A6 = CareerOffsets::kContestMenu;
    SWOS::ShowMenu();

    assertTrue(p1SawCareerOver);
    assertTrue(season2ReachedBySurvivor);
    assertEqual(static_cast<int>(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber])), static_cast<int>(kBesiktas));
    const TeamFile *oldFener = findSelectedTeamById(kFenerbahce);
    const TeamFile *oldGala = findSelectedTeamById(kGalatasaray);
    const TeamFile *newBesiktas = findSelectedTeamById(kBesiktas);
    assertTrue(oldFener != nullptr && oldGala != nullptr && newBesiktas != nullptr);
    assertEqual(static_cast<int>(oldFener->teamControls), static_cast<int>(kComputerTeam));
    assertEqual(static_cast<int>(oldGala->teamControls), static_cast<int>(kComputerTeam));
    assertTrue(newBesiktas->teamControls == kPlayerCoach || newBesiktas->teamControls == kCoach);

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatClubSwitchOldTeamCpuRedAndFixtures()
{
    constexpr int16_t kFenerbahce = 0x0926;
    constexpr int16_t kGalatasaray = 0x0a26;
    constexpr int16_t kBesiktas = 0x0526;
    constexpr int16_t kGaziantepspor = 0x0b26;

    // Subcase A: P0 accepts Besiktas, P1 stays at Galatasaray.
    // Put both clubs in tmdFileBuffer first (simulating European cups / saved career) to catch stale kCoach in tmdFileBuffer!
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });
    initTwoPlayerHotseatFromTeams(kFenerbahce, kGalatasaray);
    advanceHotseatSeasonToFinish();
    auto& mgr = HotseatCareerManager::get();
    mgr.switchToPlayer(0, true);

    // Seed Fenerbahce & Galatasaray into tmdFileBuffer with human controls (reproduces bug where cseg_5A6A1 kept kCoach)
    {
        auto *tmd = reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kTmdFileBuffer]);
        const TeamFile *fenerTf = findSelectedTeamById(kFenerbahce);
        const TeamFile *galaTf = findSelectedTeamById(kGalatasaray);
        assertTrue(fenerTf != nullptr && galaTf != nullptr);
        std::memcpy(&tmd[0], fenerTf, sizeof(TeamFile));
        tmd[0].teamControls = kCoach;
        std::memcpy(&tmd[1], galaTf, sizeof(TeamFile));
        tmd[1].teamControls = kCoach;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTmdCareerCount]) = 2;
    }

    // Tag P1's staying Galatasaray GK player name to confirm staying squad isn't reset to stock disk data
    {
        auto *p1Gk = reinterpret_cast<PlayerFile *>(mgr.player(1).careerTeam + sizeof(TeamFileHeader));
        std::snprintf(p1Gk->playerName, sizeof(p1Gk->playerName), "CUSTOM GALA GK");
    }

    mgr.onSeasonFinishedForAllPlayers();
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]) = 1;
    auto& p0 = mgr.player(0);
    p0.retired = 1;
    p0.youreSacked = 0;
    p0.numNewJobOffers = 1;
    std::memset(p0.newJobOffers, 0, kJobOffersBufferSize);
    seedJobOfferInBuffer(p0.newJobOffers, 0, kBesiktas, "BESIKTAS", 3'000'000);
    mgr.player(1).retired = 1;
    mgr.player(1).youreSacked = 0;
    mgr.player(1).numNewJobOffers = 0;
    mgr.player(1).numJobOffers = 0;
    mgr.switchToPlayer(0, true);

    bool p0Done = false;
    bool p1Done = false;
    bool enteredSeason2Contest = false;
    SWOS_UnitTest::setMenuCallback([&] {
        auto packed = getCurrentPackedMenu();
        if (packed && SwosVM::isSwosPtr(packed)) {
            uint32_t ofs = SwosVM::ptrToOffset(packed);
            if (ofs == CareerOffsets::kCareerOverMenu) {
                if (mgr.activePlayerIndex() == 0 && !p0Done) {
                    p0Done = true;
                    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = kBesiktas;
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = 3'000'000;
                    mgr.onAfterAcceptJobOffer();
                    selectEntry(6);
                    return false;
                }
                if (mgr.activePlayerIndex() == 1 && !p1Done) {
                    p1Done = true;
                    selectEntry(6); // P1 stays at Galatasaray
                    enteredSeason2Contest = true;
                    return false;
                }
            } else if (ofs == CareerOffsets::kContestMenu && enteredSeason2Contest) {
                SetExitMenuFlag();
                return false;
            }
        }
        SetExitMenuFlag();
        return false;
    });

    swos.g_exitMenu = 0;
    A6 = CareerOffsets::kContestMenu;
    SWOS::ShowMenu();

    assertTrue(p0Done && p1Done && enteredSeason2Contest);
    assertEqual(mgr.numPlayers(), 2);
    assertEqual(static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing])), 1);

    // P0 now coaches Besiktas (26 players, fresh starting funds)
    assertEqual(mgr.player(0).teamId, kBesiktas);
    assertEqual(mgr.player(0).teamName, "BESIKTAS");
    assertEqual(static_cast<int>(mgr.player(0).careerNumPlayers), 26);
    assertEqual(static_cast<int>(mgr.player(0).stayingInOldClub), 0);

    // P1 still coaches Galatasaray with Season 1 squad intact
    assertEqual(mgr.player(1).teamId, kGalatasaray);
    const auto *p1GkAfter = reinterpret_cast<const PlayerFile *>(mgr.player(1).careerTeam + sizeof(TeamFileHeader));
    assertEqual(std::string(p1GkAfter->playerName), "CUSTOM GALA GK");

    // Old club Fenerbahce MUST now be kComputerTeam (1 => rendered COLOR_RED) in BOTH g_selectedTeams and tmdFileBuffer!
    const TeamFile *oldFenerSel = findSelectedTeamById(kFenerbahce);
    const TeamFile *newBesikSel = findSelectedTeamById(kBesiktas);
    const TeamFile *stayGalaSel = findSelectedTeamById(kGalatasaray);
    assertTrue(oldFenerSel != nullptr && newBesikSel != nullptr && stayGalaSel != nullptr);
    assertEqual(static_cast<int>(oldFenerSel->teamControls), static_cast<int>(kComputerTeam));
    assertTrue(newBesikSel->teamControls == kPlayerCoach || newBesikSel->teamControls == kCoach);
    assertTrue(stayGalaSel->teamControls == kPlayerCoach || stayGalaSel->teamControls == kCoach);

    // Also verify Fenerbahce remains included in competitionFileBuffer league teams as a CPU team
    uint32_t compBufOfs = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers]);
    int16_t leagueTeamsCount = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[compBufOfs + 49]);
    bool fenerInLeagueTable = false;
    bool besiktasInLeagueTable = false;
    bool galaInLeagueTable = false;
    for (int i = 0; i < leagueTeamsCount && i < 64; ++i) {
        int16_t tid = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[compBufOfs + 301 + i * 2]);
        if (tid == kFenerbahce) fenerInLeagueTable = true;
        if (tid == kBesiktas) besiktasInLeagueTable = true;
        if (tid == kGalatasaray) galaInLeagueTable = true;
    }
    assertTrue(fenerInLeagueTable && besiktasInLeagueTable && galaInLeagueTable);

    uint16_t tmdCnt = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTmdCareerCount]);
    const auto *tmdAfter = reinterpret_cast<const TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kTmdFileBuffer]);
    for (uint16_t i = 0; i < tmdCnt && i < 64; ++i) {
        int16_t tid = *reinterpret_cast<const int16_t *>(&tmdAfter[i]);
        if (tid == kFenerbahce) {
            assertEqual(static_cast<int>(tmdAfter[i].teamControls), static_cast<int>(kComputerTeam));
        }
    }

    // Verify Fenerbahce is NOT scheduled as a human turn in Season 2 calendar slots, while Besiktas & Galatasaray ARE
    bool besiktasScheduled = false;
    bool galatasarayScheduled = false;
    bool fenerbahceAsCpuOpponentOnly = true;
    uint16_t totalCal = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    size_t scanOfs = CareerOffsets::kDsegDB3E6;
    for (uint16_t c = 0; c < totalCal && scanOfs + 22 <= CareerOffsets::kDsegDB3E6 + 1870; ++c) {
        if (SwosVM::g_memByte[scanOfs] == 0) { // league fixture entry
            uint16_t calOfs = static_cast<uint16_t>(scanOfs - CareerOffsets::kDsegDB3E6);
            auto checkSlot = [&](const uint8_t *slotBytes) {
                uint16_t t1 = *reinterpret_cast<const uint16_t *>(slotBytes + 2);
                uint16_t t2 = *reinterpret_cast<const uint16_t *>(slotBytes + 4);
                if (t1 == kBesiktas || t2 == kBesiktas) besiktasScheduled = true;
                if (t1 == kGalatasaray || t2 == kGalatasaray) galatasarayScheduled = true;
                // Fenerbahce should never appear in a CPU-vs-CPU slot inside human calendar entries
                if ((t1 == kFenerbahce && t2 != kBesiktas && t2 != kGalatasaray) ||
                    (t2 == kFenerbahce && t1 != kBesiktas && t1 != kGalatasaray)) {
                    fenerbahceAsCpuOpponentOnly = false;
                }
            };
            auto it = mgr.calendarSlots().find(calOfs);
            if (it != mgr.calendarSlots().end()) {
                for (const auto& s : it->second)
                    checkSlot(s.data());
            } else if (SwosVM::g_memByte[scanOfs + 5] > 0) {
                checkSlot(&SwosVM::g_memByte[scanOfs + 10]);
            }
        }
        uint16_t len = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[scanOfs + 2]);
        if (len < 22) break;
        scanOfs += len;
    }
    assertTrue(besiktasScheduled);
    assertTrue(galatasarayScheduled);
    assertTrue(fenerbahceAsCpuOpponentOnly);

    // Subcase B: BOTH players switch clubs cleanly in Season 2 -> Season 3 (Besiktas -> Fenerbahce, Galatasaray -> Gaziantepspor)
    advanceHotseatSeasonToFinish();
    mgr.onSeasonFinishedForAllPlayers();
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]) = 1;
    mgr.player(0).retired = 1;
    mgr.player(0).numNewJobOffers = 1;
    seedJobOfferInBuffer(mgr.player(0).newJobOffers, 0, kFenerbahce, "FENERBAHCE", 2'500'000);
    mgr.player(1).retired = 1;
    mgr.player(1).numNewJobOffers = 1;
    seedJobOfferInBuffer(mgr.player(1).newJobOffers, 0, kGaziantepspor, "GAZIANTEPSPOR", 1'500'000);
    mgr.switchToPlayer(0, true);

    bool s3P0Done = false, s3P1Done = false, enteredS3 = false;
    SWOS_UnitTest::setMenuCallback([&] {
        auto packed = getCurrentPackedMenu();
        if (packed && SwosVM::isSwosPtr(packed)) {
            uint32_t ofs = SwosVM::ptrToOffset(packed);
            if (ofs == CareerOffsets::kCareerOverMenu) {
                if (mgr.activePlayerIndex() == 0 && !s3P0Done) {
                    s3P0Done = true;
                    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = kFenerbahce;
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = 2'500'000;
                    mgr.onAfterAcceptJobOffer();
                    selectEntry(6);
                    return false;
                }
                if (mgr.activePlayerIndex() == 1 && !s3P1Done) {
                    s3P1Done = true;
                    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = kGaziantepspor;
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = 1'500'000;
                    mgr.onAfterAcceptJobOffer();
                    selectEntry(6);
                    enteredS3 = true;
                    return false;
                }
            } else if (ofs == CareerOffsets::kContestMenu && enteredS3) {
                SetExitMenuFlag();
                return false;
            }
        }
        SetExitMenuFlag();
        return false;
    });
    swos.g_exitMenu = 0;
    A6 = CareerOffsets::kContestMenu;
    SWOS::ShowMenu();

    assertTrue(s3P0Done && s3P1Done && enteredS3);
    assertEqual(mgr.player(0).teamId, kFenerbahce);
    assertEqual(mgr.player(1).teamId, kGaziantepspor);
    assertEqual(static_cast<int>(mgr.player(0).careerNumPlayers), 26);
    assertEqual(static_cast<int>(mgr.player(1).careerNumPlayers), 26);

    const TeamFile *oldBesikS3 = findSelectedTeamById(kBesiktas);
    const TeamFile *oldGalaS3 = findSelectedTeamById(kGalatasaray);
    const TeamFile *newFenerS3 = findSelectedTeamById(kFenerbahce);
    const TeamFile *newGaziS3 = findSelectedTeamById(kGaziantepspor);
    assertTrue(oldBesikS3 && oldGalaS3 && newFenerS3 && newGaziS3);
    assertEqual(static_cast<int>(oldBesikS3->teamControls), static_cast<int>(kComputerTeam));
    assertEqual(static_cast<int>(oldGalaS3->teamControls), static_cast<int>(kComputerTeam));
    assertTrue(newFenerS3->teamControls == kPlayerCoach || newFenerS3->teamControls == kCoach);
    assertTrue(newGaziS3->teamControls == kPlayerCoach || newGaziS3->teamControls == kCoach);

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatEightPlayersMirrorsAndSaveV8()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    std::vector<int16_t> eightTeams;
    for (int i = 0; i < 8; ++i) {
        eightTeams.push_back(static_cast<int16_t>(0x0100 * (i + 1) + 0x26));
    }
    mgr.initializeHotseat(eightTeams);
    assertEqual(mgr.numPlayers(), 8);

    const uint32_t dosMemOfs = swos.dosMemOfs60c00h.getRaw();
    const uint32_t hotBase = getHotseatExtraMemoryOffset();
    const uint32_t hotEnd = hotBase + static_cast<uint32_t>(getHotseatExtraMemorySize());
    assertTrue(hotBase >= dosMemOfs + 21890);

    for (int i = 0; i < 8; ++i) {
        auto& ctx = mgr.player(i);
        ctx.teamName = "CLUB_" + std::to_string(i + 1);
        ctx.newBalance = 2'000'000 + i * 250'000;
        ctx.careerNumPlayers = 18;
        for (uint8_t s = 0; s < 18; ++s) {
            ctx.currentMatchPlayers[s] = s;
            ctx.trainingMatchSavedPlayers[s] = s;
            auto *pf = reinterpret_cast<PlayerFile *>(
                ctx.careerTeam + sizeof(TeamFileHeader) + static_cast<size_t>(s) * sizeof(PlayerFile));
            std::memset(pf, 0, sizeof(PlayerFile));
            snprintf(pf->playerName, sizeof(pf->playerName), "P%d_%u", i + 1, s);
            pf->shirtNumber = static_cast<byte>(s + 1);
        }
        *reinterpret_cast<int16_t *>(ctx.careerTeam) = ctx.teamId;
        uint32_t mirrorOfs = mgr.ensureHotseatTeamMirror(i);
        assertTrue(mirrorOfs >= hotBase + 672);
        assertTrue(mirrorOfs + kCareerTeamBufferSize <= hotEnd);
    }

    // Overwriting dosMemOfs60c00h (as SaveSelectedTeams does) must not corrupt isolated hotseat mirrors
    std::vector<uint8_t> savedDosMem(21890);
    std::memcpy(savedDosMem.data(), swos.dosMemOfs60c00h.asCharPtr(), savedDosMem.size());
    std::memset(swos.dosMemOfs60c00h.asCharPtr(), 0xAA, 21890);
    for (int i = 0; i < 8; ++i) {
        uint32_t mirrorOfs = mgr.ensureHotseatTeamMirror(i);
        assertEqual(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[mirrorOfs]), eightTeams[i]);
    }
    std::memcpy(swos.dosMemOfs60c00h.asCharPtr(), savedDosMem.data(), savedDosMem.size());

    // Verify transfer queue between P1 and P8 across 8-player roster
    mgr.loadSwosContext(mgr.player(0), false);
    HotseatCareerManager::TransferOffer offer;
    offer.fromPlayerIndex = 0;
    offer.toPlayerIndex = 7;
    offer.playerOrdinal = 5;
    snprintf(offer.playerName, sizeof(offer.playerName), "P8_5");
    offer.offerAmount = 500'000;
    int offerId = mgr.queueTransferOffer(offer);
    assertTrue(mgr.resolveTransferOffer(offerId, true));
    assertEqual(static_cast<int>(mgr.player(0).careerNumPlayers), 19);
    assertEqual(static_cast<int>(mgr.player(7).careerNumPlayers), 17);

    // Verify V8 save/load round-trip with 8 players
    const std::string hotPath = "hotseat_8p_test.hot";
    std::remove(hotPath.c_str());
    assertTrue(mgr.saveToFile(hotPath));

    mgr.reset();
    assertEqual(mgr.numPlayers(), 0);
    assertTrue(mgr.loadFromFile(hotPath));
    std::remove(hotPath.c_str());

    assertEqual(mgr.numPlayers(), 8);
    assertEqual(mgr.player(7).teamId, eightTeams[7]);
    assertEqual(mgr.player(7).teamName, "CLUB_8");
    assertEqual(static_cast<int>(mgr.player(0).careerNumPlayers), 19);
    assertEqual(static_cast<int>(mgr.player(7).careerNumPlayers), 17);

    mgr.reset();
}

void CareerTest::testHotseatMultiWorldMultiplexer()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    const int16_t kEngPremTid = 0x0108; // country 8, ord 1, div 0
    const int16_t kEngDiv2Tid = 0x2508; // country 8, ord 37, div 2
    const int16_t kTurDiv0Tid = 0x0126; // country 38, ord 1, div 0

    mgr.initializeHotseat({ kEngPremTid, kEngDiv2Tid, kTurDiv0Tid });
    assertEqual(mgr.numPlayers(), 3);

    mgr.player(0).dsegD6CDC = 0;
    mgr.player(0).careerTeamLeague = 0;
    *reinterpret_cast<int16_t *>(mgr.player(0).careerTeam) = kEngPremTid;
    mgr.player(0).teamName = "ENG_PREM_0";

    mgr.player(1).dsegD6CDC = 2;
    mgr.player(1).careerTeamLeague = 2;
    *reinterpret_cast<int16_t *>(mgr.player(1).careerTeam) = kEngDiv2Tid;
    mgr.player(1).teamName = "ENG_DIV2_1";

    mgr.player(2).dsegD6CDC = 0;
    mgr.player(2).careerTeamLeague = 0;
    *reinterpret_cast<int16_t *>(mgr.player(2).careerTeam) = kTurDiv0Tid;
    mgr.player(2).teamName = "TUR_PREM_2";

    auto setupSyntheticWorld = [&](int pIdx, uint8_t country, uint8_t div, uint16_t cup1Tag,
                                   uint16_t leagueTag, uint16_t calTag, int16_t selCount, uint16_t marketSeed) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer], 0, kCompetitionBufferSize);
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A], 0, kCup1BufferSize);
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], 0, kCup2BufferSize);
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E6], 0, kCalendarBufferSize);
        std::memset(swos.g_selectedTeams, 0, kSelectedTeamsBufferSize);

        SwosVM::g_memByte[CareerOffsets::kCompCountryNumber] = country;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]) = div;
        *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 0]) = 50000;
        *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 4]) = 50001;
        *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + 0]) =
            static_cast<uint32_t>(CareerOffsets::kCompetitionFileBuffer);
        *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + 4]) =
            static_cast<uint32_t>(CareerOffsets::kDsegD8D0A);
        swos.selTeamsPtr = 131759;

        // League competition with P[pIdx] + 1 AI club
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer]) = 0;
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer + 49]) = 2;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer + 120]) = leagueTag;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer + 301]) =
            static_cast<uint16_t>(mgr.player(pIdx).teamId);
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer + 303]) =
            static_cast<uint16_t>(0x1000 + country);

        // Shared country cup with all hotseat teams across country + P2 in Euro Cup
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A]) = 1;
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 49]) = 3;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 120]) = cup1Tag;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 589]) =
            static_cast<uint16_t>(kEngPremTid);
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 591]) =
            static_cast<uint16_t>(kEngDiv2Tid);
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 593]) =
            static_cast<uint16_t>(kTurDiv0Tid);

        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]) = 1;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + 10]) = calTag;

        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]) = 1;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kHomeMarketPlayers]) = marketSeed;

        swos.g_numSelectedTeams = selCount;
        auto *sel = reinterpret_cast<TeamFile *>(swos.g_selectedTeams);
        std::memcpy(&sel[0], mgr.player(pIdx).careerTeam, sizeof(TeamFile));
        *reinterpret_cast<uint16_t *>(&sel[1]) = static_cast<uint16_t>(0x1000 + country);

        mgr.loadSwosContext(mgr.player(pIdx), false);
        mgr.captureActiveWorldAsInitial(mgr.worldKeyForPlayer(mgr.player(pIdx)));
    };

    setupSyntheticWorld(0, 8, 0, 0xAAAA, 0x1111, 0x2222, 2, 0x0508);
    setupSyntheticWorld(1, 8, 2, 0xAAAA, 0x3333, 0x4444, 2, 0x0508);
    setupSyntheticWorld(2, 38, 0, 0xBBBB, 0x5555, 0x6666, 2, 0x0526);

    assertEqual(mgr.worldCount(), 3);
    assertEqual(mgr.countryWorldCount(), 2);

    // Activate P0 (Eng Prem), mutate England's Cup 1 buffer
    mgr.switchToPlayer(0, true);
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer + 120]),
                static_cast<uint16_t>(0x1111));
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + 10]),
                static_cast<uint16_t>(0x2222));
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 122]) = 0xCAFE;

    // Cross-world cup opponents not in g_selectedTeams must relink to their live mirror offsets
    int32_t selPtrVal = static_cast<int32_t>(swos.selTeamsPtr);
    int32_t div2Delta = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 835 + 4]);
    int32_t turDelta = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 835 + 8]);
    uint32_t div2Ptr = static_cast<uint32_t>(selPtrVal + div2Delta);
    uint32_t turPtr = static_cast<uint32_t>(selPtrVal + turDelta);
    assertEqual(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[div2Ptr]), kEngDiv2Tid);
    assertEqual(*reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[turPtr]), kTurDiv0Tid);

    // Switching to P1 (Eng Div 2) must keep P0's CountryWorld Cup1 mutation while loading Div 2's league/calendar
    mgr.switchToPlayer(1, true);
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer + 120]),
                static_cast<uint16_t>(0x3333));
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + 10]),
                static_cast<uint16_t>(0x4444));
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 122]),
                static_cast<uint16_t>(0xCAFE));

    // Switching to P2 (Turkey Div 0) must keep Turkey's Cup1 & league completely isolated from England
    mgr.switchToPlayer(2, true);
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer + 120]),
                static_cast<uint16_t>(0x5555));
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + 10]),
                static_cast<uint16_t>(0x6666));
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 120]),
                static_cast<uint16_t>(0xBBBB));
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A + 122]),
                static_cast<uint16_t>(0));
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kHomeMarketPlayers]),
                static_cast<uint16_t>(0x0526));

    mgr.reset();
}

void CareerTest::testHotseatMultiWorldLockstep()
{
    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    extern void InitCareer();
    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    // P0: England Div 3 (long 46-round season), P1: Turkey Div 0 (shorter 34-round season)
    const int16_t kTurTeam = 0x0526; // Fenerbahce (country 38)
    const int16_t kEngTeam = 0x4508; // England Div 3 club (country 8, ord 69)

    mgr.initializeHotseat({ kEngTeam, kTurTeam });
    assertEqual(mgr.numPlayers(), 2);

    // Initialize World(8, Div3) via real InitCareer under initializingSeason guard
    mgr.setInitializingSeason(true);
    swos.g_gameType = kGameTypeCareer;
    swos.coachOrPlayer = 1;
    D0 = static_cast<uint16_t>(kEngTeam);
    InitCareer();
    swos.g_gameType = kGameTypeCareer;
    uint16_t engDiv = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]);
    mgr.player(0).dsegD6CDC = engDiv;
    mgr.saveCurrentSwosContext(mgr.player(0));
    LeagueWorldKey engKey = mgr.worldKeyForPlayer(mgr.player(0));
    mgr.captureActiveWorldAsInitial(engKey);
    uint16_t engEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    assertTrue(engEntries > 10);

    // Initialize World(38, Div0) via real InitCareer cleanly for P1
    swos.g_gameType = kGameTypeCareer;
    swos.coachOrPlayer = 1;
    D0 = static_cast<uint16_t>(kTurTeam);
    InitCareer();
    swos.g_gameType = kGameTypeCareer;
    mgr.setInitializingSeason(false);

    // Snapshot P1 into World(38, Div0)
    uint16_t turDiv = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]);
    mgr.player(1).dsegD6CDC = turDiv;
    mgr.saveCurrentSwosContext(mgr.player(1));
    LeagueWorldKey turKey = mgr.worldKeyForPlayer(mgr.player(1));
    mgr.captureActiveWorldAsInitial(turKey);
    uint16_t turEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    assertTrue(turEntries > 10);

    assertEqual(mgr.worldCount(), 2);
    assertEqual(mgr.numPlayers(), 2);
    assertTrue(mgr.isActive());
    assertFalse(mgr.areAllWorldsSeasonFinished());

    // Verify actual SWOS start weeks (engWeek0 <= turWeek0): lockstep must hold Turkey until globalWeek >= turWeek0
    uint8_t engWeek0 = mgr.leagueWorlds().at(engKey).calendar[mgr.leagueWorlds().at(engKey).calByteOfs + 1];
    mgr.switchToPlayer(1, true);
    uint16_t curCalOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
    uint8_t turWeek0 = SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + curCalOfs + 1];
    uint16_t prevEngIdx = mgr.leagueWorlds().at(engKey).calStepIdx;
    SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + curCalOfs + 6] = 0; // make Turkey entry 0 CPU-only
    mgr.saveActiveWorldFromVm();

    mgr.resolveLockstepAndSelectPlayer(false);
    if (engWeek0 < turWeek0) {
        // England starts earlier than Turkey: Turkey waits while England leads!
        assertEqual(mgr.activeWorldKey().countryFileNo, engKey.countryFileNo);
        assertEqual(mgr.leagueWorlds().at(turKey).calStepIdx, static_cast<uint16_t>(0));
        int preGuard = 0;
        while (mgr.leagueWorlds().at(engKey).seasonFinished == 0 && preGuard < 50) {
            uint16_t eOfs = mgr.leagueWorlds().at(engKey).calByteOfs;
            uint8_t eW = mgr.leagueWorlds().at(engKey).calendar[eOfs + 1];
            if (eW >= turWeek0)
                break;
            mgr.switchToPlayer(0, true);
            mgr.stepActiveWorldOneCpuEntrySilently();
            ++preGuard;
        }
        mgr.resolveLockstepAndSelectPlayer(false);
    }

    assertTrue(mgr.leagueWorlds().at(turKey).calStepIdx > 0);
    if (engWeek0 >= turWeek0) {
        assertEqual(mgr.leagueWorlds().at(engKey).calStepIdx, prevEngIdx);
    }

    // Fast-forward Turkey to seasonFinished = 1 while England still has remaining rounds
    mgr.loadWorldToVm(turKey, false);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 1;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 1;
    mgr.saveActiveWorldFromVm();
    assertFalse(mgr.areAllWorldsSeasonFinished());

    // Calling onBeforePlayMatchMenu while in finished Turkey must hold SeasonFinished and route to England!
    mgr.onBeforePlayMatchMenu();
    assertEqual(mgr.activeWorldKey().countryFileNo, engKey.countryFileNo);
    assertEqual(mgr.activePlayerIndex(), 0);
    assertEqual(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]),
                static_cast<uint16_t>(0));

    // Step England silently through trailing entries until both worlds finish
    int guardSteps = 0;
    while (!mgr.areAllWorldsSeasonFinished() && guardSteps < 200) {
        mgr.stepActiveWorldOneCpuEntrySilently();
        ++guardSteps;
    }
    assertTrue(mgr.areAllWorldsSeasonFinished());
    assertTrue(guardSteps > 0 && guardSteps < 200);
    assertTrue(mgr.leagueWorlds().at(engKey).seasonFinished != 0);
    assertTrue(mgr.leagueWorlds().at(turKey).seasonFinished != 0);

    // Verify Turkey's calendar buffer stayed untouched while England simulated to season completion
    assertEqual(mgr.leagueWorlds().at(turKey).calTotalEntries, turEntries);
    assertTrue(turWeek0 <= 200);

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatDiv3ToDiv2SplitAndCrossLeagueJobOffer()
{
    constexpr int16_t kBarnetDiv3 = 0x2c08;        // England Div 3 (country 8, ord 44)
    constexpr int16_t kBrightonDiv3 = 0x3008;      // England Div 3 (country 8, ord 48)
    constexpr int16_t kLiverpoolDiv0 = 0x0811;     // England Div 0 (country 8)
    constexpr int16_t kFenerbahceTur = 0x0926;     // Turkey Div 0 (country 38)
    constexpr int16_t kEnglandNational = 0x0050;   // National team (>= 80)

    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return true;
    });

    initTwoPlayerHotseatFromTeams(kBarnetDiv3, kBrightonDiv3);
    auto& mgr = HotseatCareerManager::get();
    // Simulate Season 1 to completion (silent VM stepping keeps the test independent of menu/result options)
    // so league, domestic cups & playoffs all have real final results
    mgr.switchToPlayer(0, true);
    mgr.captureActiveWorldAsInitial(mgr.worldKeyForPlayer(mgr.player(0)));
    {
        int guard = 0;
        while (!mgr.areAllWorldsSeasonFinished() && guard < 1200) {
            mgr.stepActiveWorldOneCpuEntrySilently();
            mgr.saveActiveWorldFromVm();
            ++guard;
        }
        assertTrue(mgr.areAllWorldsSeasonFinished());
    }

    assertEqual(mgr.worldCount(), 1);
    assertEqual(static_cast<int>(mgr.player(0).careerTeamLeague), 3);
    assertEqual(static_cast<int>(mgr.player(0).dsegD6CDC), 3);
    assertEqual(static_cast<int>(mgr.player(1).careerTeamLeague), 3);
    assertEqual(static_cast<int>(mgr.player(1).dsegD6CDC), 3);

    // Helper: consistently relabel two clubs inside the active world's league table (competitionFileBuffer)
    // AND its playoff buffer (dseg_D9C9F) by swapping their team ids and team pointer deltas. This moves a club
    // into another club's final position without creating impossible tables (e.g. a club both champion and
    // playoff winner), so SWOS's native promotion/relegation logic stays self-consistent.
    auto swapTeamsInDiyBuffer = [&](size_t compOfs, uint16_t tidA, uint16_t tidB) {
        uint16_t compType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[compOfs]);
        int16_t numTeams = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[compOfs + 49]);
        if (numTeams <= 0 || numTeams > 64 || compType > 1)
            return;
        size_t idsOfs = (compType == 0) ? compOfs + 301 : compOfs + 589;
        auto ptrOfs = [&](int k) {
            return (compType == 0) ? compOfs + 691 + static_cast<size_t>(k) * 18 : compOfs + 835 + static_cast<size_t>(k) * 4;
        };
        int ka = -1, kb = -1;
        for (int k = 0; k < numTeams; ++k) {
            uint16_t tid = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[idsOfs + k * 2]);
            if (tid == tidA) ka = k;
            if (tid == tidB) kb = k;
        }
        if (ka >= 0)
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[idsOfs + ka * 2]) = tidB;
        if (kb >= 0)
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[idsOfs + kb * 2]) = tidA;
        if (ka >= 0 && kb >= 0) {
            std::swap(*reinterpret_cast<int32_t *>(&SwosVM::g_memByte[ptrOfs(ka)]),
                      *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[ptrOfs(kb)]));
        }
    };
    auto teamAtLeaguePosition = [&](int pos) -> uint16_t {
        size_t compOfs = CareerOffsets::kCompetitionFileBuffer;
        uint16_t rowOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[compOfs + 109 + pos * 2]);
        int k = rowOfs / 18;
        return *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[compOfs + 301 + k * 2]);
    };
    auto placeTeamAtLeaguePosition = [&](int16_t teamId, int pos) {
        uint16_t other = teamAtLeaguePosition(pos);
        if (other == static_cast<uint16_t>(teamId))
            return;
        swapTeamsInDiyBuffer(CareerOffsets::kCompetitionFileBuffer, static_cast<uint16_t>(teamId), other);
        swapTeamsInDiyBuffer(CareerOffsets::kDsegD9C9F, static_cast<uint16_t>(teamId), other);
        assertEqual(teamAtLeaguePosition(pos), static_cast<uint16_t>(teamId));
    };

    // Cross-division & cross-country job offer validation (Phase 4)
    assertTrue(mgr.isValidHotseatJobOffer(kLiverpoolDiv0, 0));
    assertTrue(mgr.isValidHotseatJobOffer(kFenerbahceTur, 0));
    assertFalse(mgr.isValidHotseatJobOffer(kEnglandNational, 0));
    assertFalse(mgr.isValidHotseatJobOffer(kBarnetDiv3, 0));
    assertFalse(mgr.isValidHotseatJobOffer(kBrightonDiv3, 0));

    // Finish Season 1 in World(8, 3): Barnet finishes 1st (promoted to Div 2), Brighton 13th (stays Div 3)
    const LeagueWorldKey engDiv3Key{ 8, 3 };
    mgr.loadWorldToVm(engDiv3Key, true);
    placeTeamAtLeaguePosition(kBarnetDiv3, 0);
    placeTeamAtLeaguePosition(kBrightonDiv3, 12);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 1;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 1;
    mgr.saveActiveWorldFromVm();

    mgr.switchToPlayer(0, true);
    mgr.onSeasonFinishedForAllPlayers();
    for (int i = 0; i < 2; ++i) {
        mgr.player(i).retired = 1;
        mgr.player(i).youreSacked = 0;
        mgr.player(i).numNewJobOffers = 0;
        mgr.player(i).numJobOffers = 0;
    }
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]) = 1;

    bool s1P0Done = false;
    bool s1P1Done = false;
    bool enteredSeason2 = false;
    SWOS_UnitTest::setMenuCallback([&] {
        auto packed = getCurrentPackedMenu();
        if (packed && SwosVM::isSwosPtr(packed)) {
            uint32_t ofs = SwosVM::ptrToOffset(packed);
            if (ofs == CareerOffsets::kCareerOverMenu) {
                if (mgr.activePlayerIndex() == 0 && !s1P0Done) {
                    s1P0Done = true;
                    selectEntry(6); // CONTINUE CAREER -> advance to P1
                    return false;
                }
                if (mgr.activePlayerIndex() == 1 && !s1P1Done) {
                    s1P1Done = true;
                    selectEntry(6); // CONTINUE CAREER -> executeHotseatSeasonTransition
                    enteredSeason2 = true;
                    return false;
                }
            } else if (ofs == CareerOffsets::kContestMenu && enteredSeason2) {
                SetExitMenuFlag();
                return false;
            }
        }
        SetExitMenuFlag();
        return false;
    });
    swos.g_exitMenu = 0;
    A6 = CareerOffsets::kContestMenu;
    SWOS::ShowMenu();

    assertTrue(s1P0Done && s1P1Done && enteredSeason2);
    assertEqual(mgr.numPlayers(), 2);
    // Season 2 MUST split into World(8, 2) for P0 and World(8, 3) for P1, sharing CountryWorld(8) cups!
    assertEqual(mgr.worldCount(), 2);
    const LeagueWorldKey engDiv2Key{ 8, 2 };
    assertTrue(mgr.leagueWorlds().count(engDiv2Key) != 0);
    assertTrue(mgr.leagueWorlds().count(engDiv3Key) != 0);
    assertEqual(static_cast<int>(mgr.countryWorlds().size()), 1);
    assertTrue(mgr.countryWorlds().count(8) != 0);
    assertTrue(mgr.worldKeyForPlayer(mgr.player(0)) == engDiv2Key);
    assertTrue(mgr.worldKeyForPlayer(mgr.player(1)) == engDiv3Key);
    assertEqual(static_cast<int>(mgr.player(0).dsegD6CDC), 2);
    assertEqual(static_cast<int>(mgr.player(1).dsegD6CDC), 3);
    // Base careerTeamLeague MUST remain 3 so Season 3 never double-promotes!
    assertEqual(static_cast<int>(mgr.player(0).careerTeamLeague), 3);
    assertEqual(static_cast<int>(mgr.player(1).careerTeamLeague), 3);
    assertEqual(static_cast<int>(reinterpret_cast<const TeamFile *>(mgr.player(0).careerTeam)->league), 2);
    assertEqual(static_cast<int>(reinterpret_cast<const TeamFile *>(mgr.player(1).careerTeam)->league), 3);

    // Season 2 -> Season 3: simulate every live world to completion first (real cups/playoffs results),
    // then pin final standings: P0 mid-table in Eng Div 2 (no double promotion!), P1 mid-table in Eng Div 3
    // who then accepts a cross-country job offer at Fenerbahce (Turkey Div 0)!
    {
        int guard = 0;
        while (!mgr.areAllWorldsSeasonFinished() && guard < 1200) {
            for (int pi = 0; pi < mgr.numPlayers(); ++pi) {
                mgr.switchToPlayer(pi, true);
                mgr.stepActiveWorldOneCpuEntrySilently();
                mgr.saveActiveWorldFromVm();
            }
            ++guard;
        }
        assertTrue(mgr.areAllWorldsSeasonFinished());
    }
    mgr.loadWorldToVm(engDiv2Key, true);
    placeTeamAtLeaguePosition(kBarnetDiv3, 12);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 1;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 1;
    mgr.saveActiveWorldFromVm();

    mgr.loadWorldToVm(engDiv3Key, true);
    placeTeamAtLeaguePosition(kBrightonDiv3, 12);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 1;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 1;
    mgr.saveActiveWorldFromVm();

    mgr.switchToPlayer(0, true);
    mgr.onSeasonFinishedForAllPlayers();
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]) = 1;
    mgr.player(0).retired = 1;
    mgr.player(0).youreSacked = 0;
    mgr.player(0).numNewJobOffers = 0;
    mgr.player(0).numJobOffers = 0;

    auto& p1 = mgr.player(1);
    p1.retired = 1;
    p1.youreSacked = 0;
    p1.numNewJobOffers = 1;
    p1.numJobOffers = 0;
    std::memset(p1.newJobOffers, 0, kJobOffersBufferSize);
    seedJobOfferInBuffer(p1.newJobOffers, 0, kFenerbahceTur, "FENERBAHCE", 3'000'000);

    bool s2P0Done = false;
    bool s2P1Done = false;
    bool enteredSeason3 = false;
    SWOS_UnitTest::setMenuCallback([&] {
        auto packed = getCurrentPackedMenu();
        if (packed && SwosVM::isSwosPtr(packed)) {
            uint32_t ofs = SwosVM::ptrToOffset(packed);
            if (ofs == CareerOffsets::kCareerOverMenu) {
                if (mgr.activePlayerIndex() == 0 && !s2P0Done) {
                    s2P0Done = true;
                    selectEntry(6);
                    return false;
                }
                if (mgr.activePlayerIndex() == 1 && !s2P1Done) {
                    s2P1Done = true;
                    // Verify cross-country offer survived sanitization
                    assertEqual(static_cast<int>(*reinterpret_cast<const int16_t *>(
                        &SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers])), 1);
                    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = kFenerbahceTur;
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = 3'000'000;
                    mgr.onAfterAcceptJobOffer();
                    selectEntry(6);
                    enteredSeason3 = true;
                    return false;
                }
            } else if (ofs == CareerOffsets::kContestMenu && enteredSeason3) {
                SetExitMenuFlag();
                return false;
            }
        }
        SetExitMenuFlag();
        return false;
    });
    swos.g_exitMenu = 0;
    A6 = CareerOffsets::kContestMenu;
    SWOS::ShowMenu();

    assertTrue(s2P0Done && s2P1Done && enteredSeason3);
    assertEqual(mgr.numPlayers(), 2);
    // P0 stays at Barnet in Eng Div 2 (never double-promoted!)
    assertEqual(mgr.player(0).teamId, kBarnetDiv3);
    assertEqual(static_cast<int>(mgr.player(0).dsegD6CDC), 2);
    assertEqual(static_cast<int>(mgr.player(0).careerTeamLeague), 3);
    // P1 moved cleanly into Fenerbahce's Turkey Div 0 world!
    assertEqual(mgr.player(1).teamId, kFenerbahceTur);
    assertEqual(static_cast<int>(mgr.player(1).dsegD6CDC), 0);
    assertEqual(static_cast<int>(mgr.worldKeyForPlayer(mgr.player(1)).countryFileNo), 38);
    assertEqual(mgr.worldCount(), 2);
    assertTrue(mgr.leagueWorlds().count(engDiv2Key) != 0);
    assertTrue(mgr.leagueWorlds().count({ 38, 0 }) != 0);

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}

void CareerTest::testHotseatMultiCountryCareer()
{
    constexpr int16_t kManUtd = 0x1408;     // England Div 0 (country 8, ord 20)
    constexpr int16_t kFenerbahce = 0x0926; // Turkey Div 0 (country 38, ord 9)

    extern void LoadTeamFile();
    extern void LoadCareerFile();

    auto loadRealTeamFile = [&](uint8_t countryFileNo, int16_t targetTeamId, TeamFile *firstOtherPremOut = nullptr) -> TeamFile {
        save68kRegisters();
        D0 = countryFileNo;
        LoadTeamFile();
        restore68kRegisters();

        uint16_t fileCount = (static_cast<uint16_t>(SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer]) << 8) |
                             SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer + 1];
        TeamFile found{};
        bool gotTarget = false;
        bool gotOther = (firstOtherPremOut == nullptr);
        for (uint16_t t = 0; t < fileCount; ++t) {
            const auto *tf = reinterpret_cast<const TeamFile *>(
                &SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer + 2 + static_cast<size_t>(t) * sizeof(TeamFile)]);
            int16_t tid = static_cast<int16_t>(*reinterpret_cast<const uint16_t *>(tf));
            if (tid == targetTeamId && !gotTarget) {
                found = *tf;
                gotTarget = true;
            } else if (!gotOther && tf->league == 0 && tid != targetTeamId) {
                *firstOtherPremOut = *tf;
                gotOther = true;
            }
        }
        assertTrue(gotTarget);
        assertTrue(gotOther);
        return found;
    };

    TeamFile otherEngPremTf{};
    TeamFile manUtdTf = loadRealTeamFile(0x08, kManUtd, &otherEngPremTf);
    TeamFile fenerTf = loadRealTeamFile(0x26, kFenerbahce);
    int16_t otherEngPremId = static_cast<int16_t>(*reinterpret_cast<const uint16_t *>(&otherEngPremTf));

    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    SWOS_UnitTest::setMenuCallback([&] {
        SetExitMenuFlag();
        return false;
    });

    // 1. Multi-country setup: check cross-country selection allowed & call initializeStartingWorlds()
    mgr.setCareerNumPlayers(2);
    mgr.setSelectingCareerTeams(true);
    mgr.assignTeamSlot(kManUtd, 0, &manUtdTf);
    mgr.assignTeamSlot(kFenerbahce, 1, &fenerTf);
    assertFalse(mgr.areAllAssignedTeamsInSameLeague());
    mgr.setSelectingCareerTeams(false);

    std::vector<TeamFile> orderedTeams = { manUtdTf, fenerTf };
    mgr.initializeHotseat({ kManUtd, kFenerbahce });
    mgr.setInitializingSeason(true);

    struct InitPlayerMeta {
        const char *name;
        const char *surname;
        uint8_t nat;
        int32_t startBal;
    };
    const InitPlayerMeta meta[2] = {
        { "ALEX", "FERGUSON", 8, 2'000'000 },
        { "FATIH", "TERIM", 23, 1'500'000 },
    };
    for (int i = 0; i < 2; ++i) {
        auto& p = mgr.player(i);
        p.managerName = meta[i].name;
        p.managerSurname = meta[i].surname;
        p.nationality = meta[i].nat;
        p.coachOrPlayer = 1;
        p.careerNumPlayers = 16;
        char tbuf[18] = {0};
        std::memcpy(tbuf, orderedTeams[i].teamName, 17);
        p.teamName = tbuf;
        std::memcpy(p.careerTeam, &orderedTeams[i], sizeof(TeamFile));
        auto *tf = reinterpret_cast<TeamFile *>(p.careerTeam);
        tf->teamControls = kPlayerCoach;
        p.careerTeamLeague = tf->league;
        p.startingBalance = meta[i].startBal;
        p.newBalance = meta[i].startBal;
        p.oldBalance = meta[i].startBal;
    }

    mgr.initializeStartingWorlds(orderedTeams);
    mgr.setInitializingSeason(false);

    assertTrue(mgr.isActive());
    assertFalse(mgr.isSelectingCareerTeams());
    assertEqual(mgr.numPlayers(), 2);
    assertEqual(mgr.player(0).teamId, kManUtd);
    assertEqual(mgr.player(1).teamId, kFenerbahce);
    assertEqual(mgr.player(0).managerSurname, std::string("FERGUSON"));
    assertEqual(mgr.player(1).managerSurname, std::string("TERIM"));
    assertEqual(mgr.countryWorlds().size(), size_t{2});
    assertEqual(mgr.leagueWorlds().size(), size_t{2});
    assertTrue(mgr.countryWorlds().count(8) != 0);
    assertTrue(mgr.countryWorlds().count(38) != 0);
    assertTrue(mgr.leagueWorlds().count({ 8, 0 }) != 0);
    assertTrue(mgr.leagueWorlds().count({ 38, 0 }) != 0);
    assertTrue(mgr.anyEuroCupActive());
    assertEqual(static_cast<int>(mgr.player(0).careerNumPlayers), 26);
    assertEqual(static_cast<int>(mgr.player(1).careerNumPlayers), 26);
    const uint8_t s1SeasonPlaying = mgr.player(0).seasonPlaying;

    // 2. Multi-country lockstep & mismatched schedule progression
    const uint16_t startWeek = mgr.globalWeek();
    const auto initialKey = mgr.activeWorldKey();
    assertEqual(initialKey.countryFileNo, mgr.worldKeyForPlayer(mgr.activePlayer()).countryFileNo);
    for (int step = 0; step < 12; ++step) {
        mgr.stepActiveWorldOneCpuEntrySilently();
        mgr.saveActiveWorldFromVm();
        mgr.resolveLockstepAndSelectPlayer(false);
    }
    assertTrue(mgr.globalWeek() >= startWeek);
    assertTrue(mgr.leagueWorlds().at({ 8, 0 }).calStepIdx > 0 || mgr.leagueWorlds().at({ 38, 0 }).calStepIdx > 0);

    // 3. Cross-country transfer market visibility, lookupForeignPoolPlayer, queue & resolveTransferOffer
    mgr.switchToPlayer(0, true);
    assertEqual(mgr.activeWorldKey().countryFileNo, uint8_t{8});
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]) = 1;
    std::memset(&SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket], 0, kListedPlayersSize);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket]) = 2;
    mgr.saveCurrentSwosContext(mgr.player(0));
    mgr.syncTransferMarketPoolsForCurrentPlayer();

    auto marketHasEntry = [](size_t countOfs, size_t entriesOfs, int maxEntries, uint16_t teamId, uint8_t ord) {
        uint16_t cnt = std::min<uint16_t>(
            *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[countOfs]),
            static_cast<uint16_t>(maxEntries));
        const uint8_t *buf = &SwosVM::g_memByte[entriesOfs];
        for (uint16_t i = 0; i < cnt; ++i) {
            uint16_t tid = *reinterpret_cast<const uint16_t *>(buf + i * 4);
            uint8_t pOrd = buf[i * 4 + 2];
            if (tid == teamId && pOrd == ord)
                return true;
        }
        return false;
    };

    // Active player (P0) must not see own listed player in England's domestic market
    assertFalse(marketHasEntry(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers,
                               25, static_cast<uint16_t>(kManUtd), 2));

    // Switch to P1 (Turkey): P0's listed player must appear in Turkey's Foreign Market & lookupForeignPoolPlayer()
    mgr.switchToPlayer(1, true);
    assertEqual(mgr.activeWorldKey().countryFileNo, uint8_t{38});
    assertTrue(marketHasEntry(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers,
                              15, static_cast<uint16_t>(kManUtd), 2));

    save68kRegisters();
    D0 = static_cast<uint16_t>(kManUtd);
    D1 = 2;
    mgr.lookupForeignPoolPlayer();
    const auto *poolEntry = reinterpret_cast<const uint8_t *>(&SwosVM::g_memByte[A0]);
    uint16_t poolTid = *reinterpret_cast<const uint16_t *>(poolEntry);
    uint8_t poolOrd = poolEntry[2];
    char poolPlayerName[24] = {};
    std::memcpy(poolPlayerName, poolEntry + 20 + 3, 23); // PoolPlayerEntry: 2+1+17+PlayerFile(nat+unk+shirt+name[23])
    restore68kRegisters();
    assertEqual(poolTid, static_cast<uint16_t>(kManUtd));
    assertEqual(static_cast<int>(poolOrd), 2);
    assertTrue(poolPlayerName[0] != '\0');

    // P1 (Turkey) submits a bid to P0 (England) for P0's listed player ord = 2
    char expectedTransferredName[24] = {};
    std::memcpy(expectedTransferredName,
                reinterpret_cast<const PlayerFile *>(mgr.player(0).careerTeam + sizeof(TeamFileHeader))[2].playerName,
                23);

    int32_t p0PreBal = mgr.player(0).newBalance;
    int32_t p1PreBal = mgr.player(1).newBalance;
    constexpr uint32_t kOfferCash = 250'000;

    HotseatCareerManager::TransferOffer offer;
    offer.fromPlayerIndex = 1;
    offer.toPlayerIndex = 0;
    offer.playerOrdinal = 2;
    offer.offerAmount = kOfferCash;
    snprintf(offer.playerName, sizeof(offer.playerName), "%.22s", expectedTransferredName);
    int offerId = mgr.queueTransferOffer(offer);

    mgr.switchToPlayer(0, true);
    assertEqual(mgr.getPendingOffersForPlayer(0).size(), size_t{1});
    assertTrue(mgr.resolveTransferOffer(offerId, true));

    assertEqual(mgr.player(0).newBalance, p0PreBal + static_cast<int32_t>(kOfferCash));
    assertEqual(mgr.player(1).newBalance, p1PreBal - static_cast<int32_t>(kOfferCash));
    assertEqual(static_cast<int>(mgr.player(0).careerNumPlayers), 25);
    assertEqual(static_cast<int>(mgr.player(0).numPlayersOnTransferMarket), 0);
    assertEqual(static_cast<int>(mgr.player(1).careerNumPlayers), 27);

    bool foundInP1 = false;
    const auto *p1Players = reinterpret_cast<const PlayerFile *>(mgr.player(1).careerTeam + sizeof(TeamFileHeader));
    for (int i = 0; i < mgr.player(1).careerNumPlayers && i < 30; ++i) {
        if (std::memcmp(p1Players[i].playerName, expectedTransferredName, 22) == 0) {
            foundInP1 = true;
            break;
        }
    }
    assertTrue(foundInP1);

    mgr.switchToPlayer(1, true);
    assertFalse(marketHasEntry(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers,
                               15, static_cast<uint16_t>(kManUtd), 2));

    // 4. Cross-country .hot V9 and .CAR round-trip persistence
    const char *kSaveName = "TMCCAR.CAR";
    const std::string carPath = pathInRootDir("TMCCAR.CAR");
    const std::string hotPath = pathInRootDir("TMCCAR.hot");
    const std::string noExtPath = pathInRootDir("TMCCAR");
    auto cleanupFiles = [&]() {
        std::remove(carPath.c_str());
        std::remove(hotPath.c_str());
        std::remove(noExtPath.c_str());
    };
    cleanupFiles();

    const uint16_t savedGlobalWeek = mgr.globalWeek();
    const int savedActiveIdx = mgr.activePlayerIndex();
    const auto savedActiveKey = mgr.activeWorldKey();
    const int32_t savedP0Bal = mgr.player(0).newBalance;
    const int32_t savedP1Bal = mgr.player(1).newBalance;

    {
        uint32_t saveEndOfs = 131757u + 2u + static_cast<uint32_t>(swos.g_numSelectedTeams) * 684u;
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDseg17A3D0], kSaveName, std::strlen(kSaveName) + 1);
        save68kRegisters();
        A0 = static_cast<uint32_t>(CareerOffsets::kDseg17A3D0);
        A1 = static_cast<uint32_t>(CareerOffsets::kTmdCareerCount);
        D1 = saveEndOfs - static_cast<uint32_t>(CareerOffsets::kTmdCareerCount);
        int res = SWOS::WriteFile_REAL();
        assertEqual(res, 0);
        restore68kRegisters();
    }

    assertTrue(fileExists(carPath.c_str()));
    assertTrue(fileExists(hotPath.c_str()));
    {
        std::ifstream fMagic(hotPath, std::ios::binary);
        char magic[16] = {};
        assertTrue(static_cast<bool>(fMagic.read(magic, sizeof(magic))));
        assertEqual(std::memcmp(magic, "SWOS_HOTSEAT_V9", 15), 0);
    }

    // Direct standalone .hot V9 reload
    mgr.reset();
    assertTrue(mgr.loadFromFile(hotPath));
    assertTrue(mgr.isActive());
    assertEqual(mgr.numPlayers(), 2);
    assertEqual(mgr.globalWeek(), savedGlobalWeek);
    assertEqual(mgr.activePlayerIndex(), savedActiveIdx);
    assertEqual(mgr.activeWorldKey().countryFileNo, savedActiveKey.countryFileNo);
    assertEqual(mgr.countryWorlds().size(), size_t{2});
    assertEqual(mgr.leagueWorlds().size(), size_t{2});
    assertEqual(mgr.player(0).newBalance, savedP0Bal);
    assertEqual(mgr.player(1).newBalance, savedP1Bal);
    assertEqual(static_cast<int>(mgr.player(0).careerNumPlayers), 25);
    assertEqual(static_cast<int>(mgr.player(1).careerNumPlayers), 27);

    // Cold-boot VM wipe + full LoadCareerFile() (.CAR + .hot V9)
    mgr.reset();
    swos.selTeamsPtr = 0;
    swos.g_numSelectedTeams = 0;
    swos.g_gameType = 0;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]) = 0;
    std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA], 0, 24);
    std::memset(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers], 0, 20);
    std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);

    {
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDseg17A3D0], kSaveName, std::strlen(kSaveName) + 1);
        save68kRegisters();
        A0 = static_cast<uint32_t>(CareerOffsets::kDseg17A3D0);
        LoadCareerFile();
        bool loadOk = SwosVM::flags.zero && D0.asWord() == 0;
        restore68kRegisters();
        assertTrue(loadOk);
    }

    assertTrue(mgr.isActive());
    assertEqual(mgr.countryWorlds().size(), size_t{2});
    assertEqual(mgr.leagueWorlds().size(), size_t{2});
    assertEqual(mgr.activePlayerIndex(), savedActiveIdx);
    assertEqual(mgr.activeWorldKey().countryFileNo, savedActiveKey.countryFileNo);
    assertEqual(mgr.globalWeek(), savedGlobalWeek);
    assertEqual(mgr.player(0).newBalance, savedP0Bal);
    assertEqual(mgr.player(1).newBalance, savedP1Bal);
    cleanupFiles();

    // 5. End-of-season transition across both countries + cross-country job offer acceptance (Fenerbahce -> England Prem)
    {
        int guard = 0;
        while (!mgr.areAllWorldsSeasonFinished() && guard < 1200) {
            for (int pi = 0; pi < mgr.numPlayers(); ++pi) {
                mgr.switchToPlayer(pi, true);
                mgr.stepActiveWorldOneCpuEntrySilently();
                mgr.saveActiveWorldFromVm();
            }
            ++guard;
        }
        assertTrue(mgr.areAllWorldsSeasonFinished());
    }

    mgr.switchToPlayer(0, true);
    mgr.onSeasonFinishedForAllPlayers();
    mgr.player(0).youreSacked = 0;
    mgr.player(0).retired = 1;
    mgr.player(0).stayingInOldClub = 0;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]) = 1;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 1;

    mgr.player(1).retired = 1;
    mgr.player(1).numNewJobOffers = 1;
    seedJobOfferInBuffer(mgr.player(1).newJobOffers, 0, otherEngPremId, otherEngPremTf.teamName, 3'000'000);
    mgr.switchToPlayer(0, true);

    bool eosP0Done = false, eosP1Done = false, enteredS2 = false;
    SWOS_UnitTest::setMenuCallback([&] {
        auto packed = getCurrentPackedMenu();
        if (packed && SwosVM::isSwosPtr(packed)) {
            uint32_t ofs = SwosVM::ptrToOffset(packed);
            if (ofs == CareerOffsets::kCareerOverMenu) {
                if (mgr.activePlayerIndex() == 0 && !eosP0Done) {
                    eosP0Done = true;
                    selectEntry(6); // P0 stays at Man Utd
                    return false;
                }
                if (mgr.activePlayerIndex() == 1 && !eosP1Done) {
                    eosP1Done = true;
                    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = otherEngPremId;
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = 3'000'000;
                    mgr.onAfterAcceptJobOffer();
                    selectEntry(6); // P1 accepts cross-country England Prem offer
                    enteredS2 = true;
                    return false;
                }
            } else if (ofs == CareerOffsets::kContestMenu && enteredS2) {
                SetExitMenuFlag();
                return false;
            }
        }
        SetExitMenuFlag();
        return false;
    });

    swos.g_exitMenu = 0;
    A6 = CareerOffsets::kContestMenu;
    SWOS::ShowMenu();

    assertTrue(eosP0Done);
    assertTrue(eosP1Done);
    assertTrue(enteredS2);
    assertEqual(mgr.player(0).teamId, kManUtd);
    assertEqual(mgr.player(1).teamId, otherEngPremId);
    assertEqual(static_cast<int>(mgr.player(0).seasonPlaying), static_cast<int>(s1SeasonPlaying) + 1);
    assertEqual(static_cast<int>(mgr.player(1).seasonPlaying), static_cast<int>(s1SeasonPlaying) + 1);
    assertEqual(mgr.worldKeyForPlayer(mgr.player(0)).countryFileNo, uint8_t{8});
    assertEqual(mgr.worldKeyForPlayer(mgr.player(1)).countryFileNo, uint8_t{8});
    assertEqual(mgr.leagueWorlds().count({ 8, 0 }), size_t{1});
    assertEqual(mgr.leagueWorlds().count({ 38, 0 }), size_t{0});

    const TeamFile *manUtdS2 = findSelectedTeamById(kManUtd);
    const TeamFile *otherEngS2 = findSelectedTeamById(otherEngPremId);
    assertTrue(manUtdS2 != nullptr);
    assertTrue(otherEngS2 != nullptr);
    assertTrue(manUtdS2->teamControls == kPlayerCoach || manUtdS2->teamControls == kCoach);
    assertTrue(otherEngS2->teamControls == kPlayerCoach || otherEngS2->teamControls == kCoach);

    mgr.reset();
    SWOS_UnitTest::setMenuCallback();
}
