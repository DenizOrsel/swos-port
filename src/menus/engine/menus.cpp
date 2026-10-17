#include "menus.h"
#include "unpackMenu.h"
#include "menuProc.h"
#include "menuAlloc.h"
#include "menuMouse.h"
#include "menuItemRenderer.h"
#include "timer.h"
#include "menuControls.h"
#include "mainMenu.h"
#include "game.h"
#include "init.h"
#include "hotseatCareer.h"

#ifdef SWOS_TEST
# include "unitTest.h"
#endif

void setEntriesVisibility(const std::vector<int>& entryIndices, bool visible)
{
    auto currentMenu = getCurrentMenu();

    for (auto entryIndex : entryIndices) {
        assert(entryIndex >= 0 && entryIndex < currentMenu->numEntries);
        currentMenu->entries()[entryIndex].setVisible(visible);
    }
}

void selectEntry(MenuEntry *entry, int controlMask /* = kShortFireMask */)
{
    assert(entry && entry->onSelect);
    if (entry && entry->onSelect) {
        // Entry 10 in careerMenu (offset 483353) is "CHOOSE TEAM"
        if (SwosVM::ptrToOffset(getCurrentPackedMenu()) == 483353 && entry->ordinal == 10) {
            extern void cseg_4C6EE();
            extern void InitCareer();

            cseg_4C6EE();

            uint16_t result = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[330146]); // chooseTeamsResult
            if (result == 0) {
                logInfo("Career team selection canceled");
                return;
            }

            uint16_t numSelected = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[131757]); // g_numSelectedTeams
            auto selectedTeams = reinterpret_cast<const TeamFile *>(&SwosVM::g_memByte[131759]);

            if (numSelected <= 1) {
                // Standard single player career
                HotseatCareerManager::get().reset();
                uint16_t teamId = (numSelected == 1) ? selectedTeams[0].globalTeamNumber : *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[330144]);
                *(uint16_t *)&D0 = teamId;
                InitCareer();
                A6 = 482233;
                SWOS::ActivateMenu();
                return;
            }

            // 2 to 4 teams selected: Initialize Hotseat Turn-Based Career!
            int numHotseatTeams = std::min(static_cast<int>(numSelected), 4);
            std::vector<int16_t> teamIds;
            for (int i = 0; i < numHotseatTeams; ++i) {
                teamIds.push_back(selectedTeams[i].globalTeamNumber);
            }

            auto& mgr = HotseatCareerManager::get();
            mgr.initializeHotseat(teamIds);

            // Copy manager name from careerMenu input buffer for Player 0
            char mgrName[9] = {0};
            char mgrSurname[9] = {0};
            std::memcpy(mgrName, &SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 8);
            std::memcpy(mgrSurname, &SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 8);
            if (mgrName[0] != 0 && mgrName[0] != ' ') {
                mgr.player(0).managerName = mgrName;
                mgr.player(0).managerSurname = mgrSurname;
            }

            for (int i = 0; i < numHotseatTeams; ++i) {
                char teamNameBuf[18] = {0};
                std::memcpy(teamNameBuf, selectedTeams[i].teamName, 17);
                mgr.player(i).teamName = teamNameBuf;
                if (i > 0) {
                    mgr.player(i).managerName = "PLAYER";
                    mgr.player(i).managerSurname = std::to_string(i + 1);
                }
            }

            // Initialize division career with Player 0's team
            *(uint16_t *)&D0 = teamIds[0];
            InitCareer();

            // Save Player 0's complete initialized context
            mgr.saveCurrentSwosContext(mgr.player(0));

            // Set up remaining players with their squads and starting balances
            for (int i = 1; i < numHotseatTeams; ++i) {
                auto& p = mgr.player(i);
                p.teamId = teamIds[i];
                p.coachOrPlayer = 0;
                p.newBalance = mgr.player(0).newBalance;
                p.oldBalance = mgr.player(0).oldBalance;
                p.playerWagesBill = mgr.player(0).playerWagesBill;
                std::memcpy(p.careerTeam, &selectedTeams[i], sizeof(TeamFile));
                std::memcpy(p.careerHistory, mgr.player(0).careerHistory, sizeof(p.careerHistory));
            }

            // Load Player 0 and launch contestMenu!
            mgr.loadSwosContext(mgr.player(0));
            logInfo("Hotseat Career initialized with %d players!", numHotseatTeams);
            A6 = 482233;
            SWOS::ActivateMenu();
            return;
        }

        if (HotseatCareerManager::get().isActive() && SwosVM::ptrToOffset(getCurrentPackedMenu()) == 482233) {
            // Entry 10 in contestMenu is NEXT MATCH / END TURN
            if (entry->ordinal == 10) {
                if (!HotseatCareerManager::get().canEndTurn()) {
                    logInfo("Hotseat: Match must be played before ending turn");
                    return;
                }

                bool newRound = HotseatCareerManager::get().endTurn();
                logInfo("Hotseat: Turn ended. Active player is now %d (newRound=%d)",
                        HotseatCareerManager::get().activePlayerIndex(), newRound);
                if (newRound) {
                    // All players completed their matches! Advance calendar round.
                    save68kRegisters();
                    swos.controlMask = controlMask;
                    A5 = entry;
                    entry->onSelect();
                    restore68kRegisters();
                    return;
                } else {
                    // Intermediate turn: re-activate contestMenu for next player
                    A6 = 482233;
                    SWOS::ActivateMenu();
                    return;
                }
            }

            // Entry 7 in contestMenu is PLAY MATCH
            if (entry->ordinal == 7) {
                if (HotseatCareerManager::get().activePlayer().matchPlayedThisTurn) {
                    logInfo("Hotseat: Match already completed this turn. Please press End Turn");
                    return;
                }
            }
        }

        save68kRegisters();

        swos.controlMask = controlMask;

        A5 = entry;
        entry->onSelect();

        restore68kRegisters();
    }
}

void selectEntry(int ordinal)
{
    assert(ordinal >= 0 && ordinal < 255 && ordinal < getCurrentMenu()->numEntries);

    if (ordinal >= 0 && ordinal < 255) {
        auto entry = getMenuEntry(ordinal);
        selectEntry(entry);
    }
}

static void menuDelay()
{
#ifdef DEBUG
# ifdef _WIN32
    assert(_CrtCheckMemory());
# endif
    checkMemory();
#endif
}

static void menuProcCycle()
{
    menuDelay();
    menuProc();
}

void SWOS::WaitRetrace()
{
    menuDelay();
}

// Tries to discover menu title (or something characteristic) using some simple heuristics.
static const char *discoverMenuTitle()
{
    static char buf[40];

    auto entryText = [](const MenuEntry *entry) -> const char * {
        if (entry->type == kEntryString)
            return entry->string();
        else if (entry->type == kEntryMultilineText)
            return entry->fg.multilineText.asCharPtr() + 1;
        else
            return nullptr;
    };

    auto hasText = [entryText](const MenuEntry *entry) {
        const char *str = entryText(entry);
        return str && *str;
    };

    if (mainMenuActive())
        return "MAIN MENU";

    auto currentMenu = getCurrentMenu();

    const MenuEntry *colorMatch = nullptr;
    const MenuEntry *dimensionsMatch = nullptr;
    const MenuEntry *firstText = nullptr;
    const MenuEntry *firstSprite = nullptr;
    const MenuEntry *firstNumeric = nullptr;

    for (auto entry = currentMenu->entries(); entry < currentMenu->sentinelEntry(); entry++) {
        if (!colorMatch && entry->bg.entryColor == (kGray | kGrayFrame) && hasText(entry)) // title color
            colorMatch = entry;
        else if (!dimensionsMatch && entry->width > 250 && entry->height > 12 && hasText(entry)) // very wide entry
            dimensionsMatch = entry;
        else if (!firstText && hasText(entry))
            firstText = entry;
        else if (!firstSprite && entry->type == kEntrySprite2)
            firstSprite = entry;
        else if (!firstNumeric && entry->type == kEntryNumber)
            firstNumeric = entry;
    }

    if (currentMenu->onReturn.index() == static_cast<int>(SwosVM::Procs::PlayMatchMenuReinit)) {
        sprintf(buf, "PLAY MATCH MENU (%s)", entryText(firstText));
        return buf;
    }

    if (colorMatch) {
        return entryText(colorMatch);
    } else if (dimensionsMatch) {
        return entryText(dimensionsMatch);
    } else if (firstText) {
        return entryText(firstText);
    } else if (firstSprite || firstNumeric) {
        memcpy(buf, firstSprite ? "SPRITE " : "NUMBER ", 7);
        auto entry = firstSprite ? firstSprite : firstNumeric;
        SDL_itoa(entry->fg.number, buf + 7, 10);
        return buf;
    }

    sprintf(buf, "%d-[%d,%d,%d]-%d", currentMenu->numEntries, currentMenu->onInit.index(), currentMenu->onReturn.index(),
        currentMenu->onDraw.index(), currentMenu->selectedEntry ? currentMenu->selectedEntry->ordinal : -1);

    return buf;
}

// These are macros instead of functions because we need to save the variables for each level of nesting;
// being functions they can't create local variables on callers stack, and then we'd need explicit stacks.
#define saveCurrentMenu() \
    auto currentMenu = getCurrentMenu(); \
    auto savedEntry = currentMenu->selectedEntry ? currentMenu->selectedEntry->ordinal : 0; \
    auto savedMenu = getCurrentPackedMenu(); \

#define restorePreviousMenu() \
    restoreMenu(savedMenu, savedEntry); \

void showMenu(const BaseMenu& menu)
{
    resetControls();

    menuMouseOnAboutToShowNewMenu();

    saveCurrentMenu();
    logInfo("About to activate menu %#x", &menu);

    auto memoryMark = SwosVM::markAllMemory();

    swos.g_exitMenu = 0;
    activateMenu(&menu);

    logInfo("Showing menu: \"%s\", previous menu is %#x", discoverMenuTitle(), savedMenu);

    while (!swos.g_exitMenu) {
        menuProcCycle();
        markFrameStartTime();
#ifdef SWOS_TEST
        // ask unit tests how long they want to run menu proc
        if (SWOS_UnitTest::exitMenuProc())
            return;
#endif
    }

    swos.g_exitMenu = 0;
    SwosVM::releaseAllMemory(memoryMark);

    restorePreviousMenu();

    menuMouseOnOldMenuRestored();

    logInfo("Menu %#x finished, restoring menu %#x", &menu, savedMenu);
}

void saveCurrentMenuAndStartGameLoop()
{
    logInfo("Starting the game...");
    logInfo("Top team: %s, bottom team: %s", swos.topTeamInGame.teamName, swos.bottomTeamInGame.teamName);

    if (HotseatCareerManager::get().isActive()) {
        int topP = HotseatCareerManager::get().findHotseatPlayerByTeamName(swos.topTeamInGame.teamName);
        int botP = HotseatCareerManager::get().findHotseatPlayerByTeamName(swos.bottomTeamInGame.teamName);
        if (topP >= 0 && botP >= 0) {
            logInfo("Hotseat: Head-to-Head fixture detected between %s (Player %d) and %s (Player %d)",
                    swos.topTeamInGame.teamName, topP + 1,
                    swos.bottomTeamInGame.teamName, botP + 1);
            swos.topTeamData.playerNumber = 1;
            swos.bottomTeamData.playerNumber = 2;
            HotseatCareerManager::get().markPlayerMatchCompleted(topP);
            HotseatCareerManager::get().markPlayerMatchCompleted(botP);
        }
    }

    saveCurrentMenu();
    startMainGameLoop();
    restorePreviousMenu();
}

void exitCurrentMenu()
{
    swos.chosenTactics = -1;    // edit tactics menu
    swos.teamsType = -1;        // select files menu
    if (getCurrentPackedMenu() == &swos.diyCompetitionMenu)
        swos.diySelected = -1;
    ExitSelectTeams();
    if (swos.choosingPreset)    // preset competition must be aborted at any level
        swos.abortSelectTeams = -1;
    swos.g_exitGameFlag = -1;   // play match menu
    swos.gameCanceled = 1;
    if (mainMenuActive())
        activateExitGameButton();
}

char *copyStringToMenuBuffer(const char *str)
{
    auto buf = getCurrentMenu()->endOfMenuPtr.asAlignedCharPtr();
    strcpy(buf, str);
    assert(buf + strlen(str) <= swos.g_currentMenu + sizeof(swos.g_currentMenu));
    return buf;
}

char *getMenuTempBuffer()
{
    auto tempBuf = getCurrentMenu()->endOfMenuPtr.asAlignedCharPtr();
    assert(tempBuf + 256 < swos.g_currentMenu + sizeof(swos.g_currentMenu));
    return tempBuf;
}

// ShowMenu
//
// in:
//     A6 -> menu to show on screen (packed form)
//
// Shows this menu and blocks, returns only when it's exited.
// And then returns to the previous menu and it becomes the active menu.
//
void SWOS::ShowMenu()
{
    auto menu = A6.as<BaseMenu *>();
    showMenu(*menu);
}

// ActivateMenu
//
// in:
//     A6 -> menu to activate
// globals:
//     g_currentMenu - destination for unpacked menu.
//
// Unpacks given static menu into g_currentMenu. Executes menu initialization function, if present.
// Sometimes called directly by some menu functions, causing it to skip the menu stack system, and the
// newly shown menu will return to the callers caller.
//
void SWOS::ActivateMenu()
{
    activateMenu(A6.asPtr());
}

// SetCurrentEntry
//
//  in:
//      D0 - number of menu entry
//
//  Sets current entry in the current menu.
//
void SWOS::SetCurrentEntry()
{
    auto currentMenu = getCurrentMenu();
    assert(D0.asWord() < currentMenu->numEntries);

    auto newSelectedEntry = getMenuEntry(D0.asWord());
    currentMenu->selectedEntry = newSelectedEntry;;
}
