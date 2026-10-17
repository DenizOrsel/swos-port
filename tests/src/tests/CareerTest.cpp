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
    mgr.player(1).newBalance = 5'000'000;

    // Player 0 offers £2,000,000 to Player 1 for player #5
    HotseatCareerManager::TransferOffer offer;
    offer.fromPlayerIndex = 0;
    offer.toPlayerIndex = 1;
    offer.playerOrdinal = 5;
    offer.offerAmount = 2'000'000;
    mgr.queueTransferOffer(offer);

    auto pendingForP1 = mgr.getPendingOffersForPlayer(1);
    assertEqual(static_cast<int>(pendingForP1.size()), 1);
    assertEqual(pendingForP1[0].offerAmount, 2'000'000);

    // Player 1 accepts offer
    mgr.resolveTransferOffer(0, true);

    assertEqual(mgr.player(0).newBalance, 8'000'000);
    assertEqual(mgr.player(1).newBalance, 7'000'000);

    // Player 0 offers £1,000,000 to Player 1 for player #6 (rejected)
    HotseatCareerManager::TransferOffer offer2;
    offer2.fromPlayerIndex = 0;
    offer2.toPlayerIndex = 1;
    offer2.playerOrdinal = 6;
    offer2.offerAmount = 1'000'000;
    mgr.queueTransferOffer(offer2);
    mgr.resolveTransferOffer(1, false);

    assertEqual(mgr.player(0).newBalance, 8'000'000);
    assertEqual(mgr.player(1).newBalance, 7'000'000);
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


