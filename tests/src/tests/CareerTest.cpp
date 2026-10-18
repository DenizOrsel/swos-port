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
    // The Bayern smoke test exits during career initialization and leaves its
    // partial career state in memory, so keep it after the end-to-end case.
    return {
        { "test career Liverpool", "career-liverpool", {}, bind(&CareerTest::testCareerLiverpool), 1, false },
        { "test career Germany Bayern", "career-germany-bayern", {}, bind(&CareerTest::testCareerGermany), 1, false },
    };
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
