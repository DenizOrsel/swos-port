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

    // End Player 0's turn -> should switch to Player 1
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
}

