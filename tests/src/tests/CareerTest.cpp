#include "CareerTest.h"
#include "unitTest.h"
#include "menus.h"
#include "unpackMenu.h"
#include "sdlProcs.h"
#include "swossym.h"
#include "gameLoop.h"
#include "game.h"
#include "options.h"

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
