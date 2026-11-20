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
#include "drawMenu.h"
#include "continueAbortMenu.h"

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

static void executeEndTurn(bool fromSubMenu)
{
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isActive() || mgr.numPlayers() <= 1 || !mgr.canEndTurn())
        return;

    int oldIdx = mgr.activePlayerIndex();
    bool newRound = mgr.advanceTurnAndResolveRound();
    logInfo("Hotseat: Turn ended for Player %d. Active player is now Player %d (%s, newRound=%d)",
            oldIdx + 1, mgr.activePlayerIndex() + 1, mgr.activePlayer().teamName.c_str(), newRound);

    if (fromSubMenu) {
        // Wait until contestMenu restores before prompting pending transfer offers
        mgr.setPendingOfferPrompt(true);
        SetExitMenuFlag();
    } else {
        mgr.setPendingOfferPrompt(false);
        A6 = 482233;
        SWOS::ActivateMenu();
        mgr.processPendingOffersForActivePlayer();
    }
}

void selectEntry(MenuEntry *entry, int controlMask /* = kShortFireMask */)
{
    assert(entry && entry->onSelect);
    if (entry && entry->onSelect) {
        auto currentPacked = getCurrentPackedMenu();
        uint32_t currentPackedOfs = (currentPacked && SwosVM::isSwosPtr(currentPacked)) ? SwosVM::ptrToOffset(currentPacked) : 0;

        // Career Wizard: Handle NEXT PLAYER / CHOOSE CLUB in careerMenu (offset 483353)
        if (currentPackedOfs == 483353) {
            if (HotseatCareerManager::get().isWizardActive()) {
                if (entry->ordinal == 10) {
                    int p = HotseatCareerManager::get().wizardCurrentPlayer();
                    char mgrName[9] = {0};
                    char mgrSurname[13] = {0};
                    std::memcpy(mgrName, &SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 8);
                    std::memcpy(mgrSurname, &SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 12);

                    trimTrailingSwosPadding(mgrName);
                    trimTrailingSwosPadding(mgrSurname);
                    const char *nameStart = mgrName;
                    while (*nameStart == ' ') ++nameStart;
                    const char *surnameStart = mgrSurname;
                    while (*surnameStart == ' ') ++surnameStart;

                    auto& wp = HotseatCareerManager::get().wizardPlayer(p);
                    wp.name = (*nameStart != '\0') ? nameStart : "PLAYER";
                    wp.surname = (*surnameStart != '\0') ? surnameStart : std::to_string(p + 1);
                    wp.nationality = SwosVM::g_memByte[CareerOffsets::kPlNationality];
                    wp.coachOrPlayer = swos.coachOrPlayer;
                    wp.mrOrMs = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]);

                    logInfo("Career Wizard: Player %d configured as '%s %s', nat=%d, role=%d",
                            p + 1, wp.name.c_str(), wp.surname.c_str(), wp.nationality, wp.coachOrPlayer);

                    HotseatCareerManager::get().setWizardAdvanced(true);
                    SetExitMenuFlag();
                    return;
                }
                if (entry->ordinal == 11) {
                    HotseatCareerManager::get().setWizardAdvanced(false);
                    SetExitMenuFlag();
                    return;
                }
            }
        }

        // Career Club Selection: assign P1..P4 slots using each player's chosen role
        if (HotseatCareerManager::get().isSelectingCareerTeams() &&
            HotseatCareerManager::get().isTeamSelectionMenuOffset(currentPackedOfs)) {
            if (HotseatCareerManager::get().handleTeamSelectionMenuSelect(currentPackedOfs, entry))
                return;
        }

        if (HotseatCareerManager::get().isActive()) {
            uint32_t offset = currentPackedOfs;

            if (offset == 482233) {
                // contestMenu (Career Hub)
                auto& mgr = HotseatCareerManager::get();
                if (mgr.numPlayers() > 1) {
                    if ((entry->ordinal == 7 || entry->ordinal == 10) && mgr.canEndTurn()) {
                        executeEndTurn(false);
                        return;
                    }
                    mgr.syncActivePlayerToHighlightedSlot(true);
                }
            } else if (offset == 496147) {
                // DIY_leagueTournamentMenu (League Table / Fixtures screen)
                if (entry->ordinal == 1) {
                    if (HotseatCareerManager::get().numPlayers() > 1 &&
                        HotseatCareerManager::get().canEndTurn()) {
                        if (HotseatCareerManager::get().inPlayMatchFlow()) {
                            // Inside PlayMatchMenu/NextMatch flow, exiting returns to cseg_8E539 ->
                            // cseg_8E3DA -> Hotseat_OnAfterPlayMatch, avoiding mid-flow player/EuroCup swaps.
                            SetExitMenuFlag();
                        } else {
                            executeEndTurn(true);
                        }
                        return;
                    }
                } else if (entry->ordinal == 10) {
                    if (HotseatCareerManager::get().numPlayers() > 1 &&
                        HotseatCareerManager::get().canEndTurn()) {
                        SetExitMenuFlag();
                        return;
                    }
                }
            }

            // Transfer Confirmation (Entry 12 in playerOffersMenu)
            bool isConfirmOffer = false;
            if (entry->ordinal == 12) {
                auto text = entry->string();
                if (text && strstr(text, "CONFIRM OFFER")) {
                    isConfirmOffer = true;
                }
            }

            if (isConfirmOffer && HotseatCareerManager::get().numPlayers() > 1) {
                auto& mgr = HotseatCareerManager::get();
                uint16_t teamOffering = swos.teamOffering;
                int sellerIdx = mgr.findHotseatPlayerByTeamId(teamOffering);

                extern void GetTeamPointer();
                save68kRegisters();
                *(uint16_t *)&D0 = teamOffering;
                GetTeamPointer();
                auto sellerTeamFile = A0.as<TeamFile*>();
                restore68kRegisters();

                if (sellerIdx < 0 && sellerTeamFile) {
                    sellerIdx = mgr.findHotseatPlayerByTeamName(sellerTeamFile->teamName);
                }

                int buyerIdx = mgr.activePlayerIndex();
                if (sellerIdx >= 0 && sellerIdx != buyerIdx) {
                    mgr.saveCurrentSwosContext(mgr.player(buyerIdx));
                    const auto& buyer = mgr.player(buyerIdx);
                    const auto& seller = mgr.player(sellerIdx);

                    int16_t wanted1 = static_cast<int16_t>(swos.dseg_17A6CE);
                    int16_t wanted2 = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumExtraPlayersWanted]);
                    int16_t exch1 = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17A6D2]);
                    int16_t exch2 = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17A6D4]);
                    if (wanted2 < 0 || wanted2 == wanted1) wanted2 = -1;
                    if (exch1 < 0) exch1 = -1;
                    if (exch2 < 0 || exch2 == exch1) exch2 = -1;

                    uint16_t offerAmountK = swos.offerAmount;
                    uint32_t cashAmount = static_cast<uint32_t>(offerAmountK) * 1000;

                    int numWanted = (wanted1 >= 0 ? 1 : 0) + (wanted2 >= 0 ? 1 : 0);
                    int numExchange = (exch1 >= 0 ? 1 : 0) + (exch2 >= 0 ? 1 : 0);
                    int sellerAfter = static_cast<int>(seller.careerNumPlayers) - numWanted + numExchange;
                    int buyerAfter = static_cast<int>(buyer.careerNumPlayers) + numWanted - numExchange;

                    if (sellerAfter < kMinCareerSquadPlayers) {
                        const char *msg2 = (seller.careerNumPlayers <= kMinCareerSquadPlayers && numExchange == 0)
                            ? "16 PLAYERS LEFT AND"
                            : "AT LEAST 16 PLAYERS";
                        const char *msg3 = (seller.careerNumPlayers <= kMinCareerSquadPlayers && numExchange == 0)
                            ? "CANNOT SELL WITHOUT EXCHANGE!"
                            : "FOR THIS TRANSFER!";
                        showContinueAbortPrompt("BID BLOCKED", "OK", nullptr,
                            { "THAT CLUB MUST KEEP", msg2, msg3 }, false);
                        SetExitMenuFlag();
                        return;
                    }
                    if (sellerAfter > kMaxCareerSquadPlayers) {
                        showContinueAbortPrompt("BID BLOCKED", "OK", nullptr,
                            { "TARGET CLUB SQUAD", "WOULD EXCEED THE", "30 PLAYER LIMIT!" }, false);
                        SetExitMenuFlag();
                        return;
                    }
                    if (buyerAfter > kMaxCareerSquadPlayers) {
                        showContinueAbortPrompt("BID BLOCKED", "OK", nullptr,
                            { "YOUR SQUAD WOULD", "EXCEED THE 30", "PLAYER LIMIT!" }, false);
                        SetExitMenuFlag();
                        return;
                    }
                    if (buyerAfter < kMinCareerSquadPlayers) {
                        showContinueAbortPrompt("BID BLOCKED", "OK", nullptr,
                            { "YOUR CLUB MUST KEEP", "AT LEAST 16 PLAYERS!" }, false);
                        SetExitMenuFlag();
                        return;
                    }

                    if (buyer.newBalance < static_cast<int32_t>(cashAmount)) {
                        showContinueAbortPrompt("INSUFFICIENT FUNDS", "OK", nullptr,
                            { "YOU DO NOT HAVE", "ENOUGH FUNDS FOR THIS", "TRANSFER BID!" }, false);
                        SetExitMenuFlag();
                        return;
                    }

                    auto extractPlayerName = [](const uint8_t *teamBuf, int ord, const TeamFile *fallbackTf,
                                                char *out, size_t outSize) {
                        snprintf(out, outSize, "PLAYER");
                        if (ord < 0 || ord >= kMaxCareerSquadPlayers)
                            return;
                        if (teamBuf) {
                            const auto *players = reinterpret_cast<const PlayerFile *>(teamBuf + sizeof(TeamFileHeader));
                            snprintf(out, outSize, "%.22s", players[ord].playerName);
                            trimTrailingSwosPadding(out);
                            if (out[0] != '\0')
                                return;
                        }
                        if (fallbackTf && ord < kNumPlayersInTeam) {
                            snprintf(out, outSize, "%.22s", fallbackTf->players[ord].playerName);
                            trimTrailingSwosPadding(out);
                            if (out[0] != '\0')
                                return;
                        }
                        snprintf(out, outSize, "PLAYER");
                    };

                    HotseatCareerManager::TransferOffer offer;
                    offer.fromPlayerIndex = buyerIdx;
                    offer.toPlayerIndex = sellerIdx;
                    offer.playerOrdinal = wanted1;
                    offer.extraWantedOrdinal = wanted2;
                    offer.exchangeOrdinal1 = exch1;
                    offer.exchangeOrdinal2 = exch2;
                    offer.offerAmount = cashAmount;
                    offer.pending = true;
                    offer.accepted = false;
                    offer.rejected = false;
                    offer.isCounterOffer = false;
                    extractPlayerName(seller.careerTeam, wanted1, sellerTeamFile, offer.playerName, sizeof(offer.playerName));
                    if (wanted2 >= 0) {
                        extractPlayerName(seller.careerTeam, wanted2, sellerTeamFile, offer.extraWantedName, sizeof(offer.extraWantedName));
                    }
                    if (exch1 >= 0) {
                        extractPlayerName(buyer.careerTeam, exch1, nullptr, offer.exchangeName1, sizeof(offer.exchangeName1));
                    }
                    if (exch2 >= 0) {
                        extractPlayerName(buyer.careerTeam, exch2, nullptr, offer.exchangeName2, sizeof(offer.exchangeName2));
                    }

                    int offerId = mgr.queueTransferOffer(offer);

                    // Prevent SWOS CPU negotiation logic from running
                    swos.dseg_17A6DE = 0;

                    char line1[96], line2[64], line3[96];
                    if (offer.exchangeOrdinal1 >= 0 && offer.exchangeOrdinal2 >= 0) {
                        snprintf(line1, sizeof(line1), "OFFER: \x9c%uK + %s + %s",
                                 cashAmount / 1000, offer.exchangeName1, offer.exchangeName2);
                    } else if (offer.exchangeOrdinal1 >= 0) {
                        snprintf(line1, sizeof(line1), "OFFER: \x9c%uK + %s",
                                 cashAmount / 1000, offer.exchangeName1);
                    } else {
                        snprintf(line1, sizeof(line1), "OFFER OF \x9c%uK SENT TO", cashAmount / 1000);
                    }
                    snprintf(line2, sizeof(line2), "TO PLAYER %d (%s)", sellerIdx + 1,
                             mgr.player(sellerIdx).managerSurname.c_str());
                    if (offer.extraWantedOrdinal >= 0) {
                        snprintf(line3, sizeof(line3), "FOR %s + %s", offer.playerName, offer.extraWantedName);
                    } else {
                        snprintf(line3, sizeof(line3), "FOR %s", offer.playerName);
                    }
                    std::vector<const char*> msgLines = { line1, line2, line3, "QUEUED FOR THEIR TURN!" };
                    showContinueAbortPrompt("BID TRANSMITTED", "CONTINUE", nullptr, msgLines, false);

                    logInfo("Hotseat: Player %d queued transfer offer #%d (£%u, exch=%d,%d) for %s (extra=%d) to Player %d",
                            buyerIdx + 1, offerId, cashAmount, offer.exchangeOrdinal1, offer.exchangeOrdinal2,
                            offer.playerName, offer.extraWantedOrdinal, sellerIdx + 1);

                    SetExitMenuFlag();
                    return;
                }
            }
            if (isConfirmOffer) {
                HotseatCareerManager::get().ensureVmTrainingMatchSavedPlayersSynced();
            }
        }

        save68kRegisters();

        swos.controlMask = controlMask;

        struct PlayMatchFlowGuard {
            HotseatCareerManager& mgr;
            bool active;
            PlayMatchFlowGuard(HotseatCareerManager& m, bool enable) : mgr(m), active(enable) {
                if (active)
                    mgr.setInPlayMatchFlow(true);
            }
            ~PlayMatchFlowGuard() {
                if (active)
                    mgr.setInPlayMatchFlow(false);
            }
        };

        auto& flowMgr = HotseatCareerManager::get();
        bool isMatchFlow = currentPackedOfs == 482233 && flowMgr.isActive() && flowMgr.numPlayers() > 1 &&
                           (entry->ordinal == 7 || entry->ordinal == 10);
        {
            PlayMatchFlowGuard guard(flowMgr, isMatchFlow);
            A5 = entry;
            entry->onSelect();
        }

        restore68kRegisters();

        if (currentPackedOfs == 482233) {
            auto& mgr = HotseatCareerManager::get();
            if (mgr.isActive() && mgr.pendingOfferPrompt()) {
                mgr.setPendingOfferPrompt(false);
                if (mgr.processPendingOffersForActivePlayer()) {
                    mgr.refreshContestMenuUi();
                }
            }
        }
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
        auto& mgr = HotseatCareerManager::get();
        int topP = mgr.findHotseatPlayerByTeamName(swos.topTeamInGame.teamName);
        int botP = mgr.findHotseatPlayerByTeamName(swos.bottomTeamInGame.teamName);
        if (topP >= 0 && botP >= 0) {
            logInfo("Hotseat: Head-to-Head fixture detected between %s (Player %d) and %s (Player %d)",
                    swos.topTeamInGame.teamName, topP + 1,
                    swos.bottomTeamInGame.teamName, botP + 1);
            // Ensure both sides retain human teamControls so SetupPlayers / ShowPlayMatchMenu choices remain intact
            if (swos.playMatchTeam1Ptr && swos.playMatchTeam1Ptr->teamControls == kComputerTeam) {
                swos.playMatchTeam1Ptr->teamControls =
                    (mgr.player(topP).coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            }
            if (swos.playMatchTeam2Ptr && swos.playMatchTeam2Ptr->teamControls == kComputerTeam) {
                swos.playMatchTeam2Ptr->teamControls =
                    (mgr.player(botP).coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            }
        }
    }

    saveCurrentMenu();
    startMainGameLoop();
    restorePreviousMenu();

    if (HotseatCareerManager::get().isActive() && !swos.gameCanceled) {
        auto& mgr = HotseatCareerManager::get();
        int topP = mgr.findHotseatPlayerByTeamName(swos.topTeamInGame.teamName);
        int botP = mgr.findHotseatPlayerByTeamName(swos.bottomTeamInGame.teamName);
        uint16_t topPen = swos.penaltiesState ? swos.team1PenaltyGoals : 0;
        uint16_t botPen = swos.penaltiesState ? swos.team2PenaltyGoals : 0;

        mgr.syncPlayersWithSelectedTeams(false);
        mgr.recordHeadToHeadResult(topP, botP, swos.statsTeam1Goals, swos.statsTeam2Goals, topPen, botPen);

        if (topP >= 0) {
            mgr.markPlayerMatchCompleted(topP);
            logInfo("Hotseat: Player %d match marked completed (top team %s)", topP + 1, swos.topTeamInGame.teamName);
        }
        if (botP >= 0) {
            mgr.markPlayerMatchCompleted(botP);
            logInfo("Hotseat: Player %d match marked completed (bottom team %s)", botP + 1, swos.bottomTeamInGame.teamName);
        }
        mgr.onMatchCompleted();
        logInfo("Hotseat: Match finished! Active player %d (%s), canEndTurn=%d",
                mgr.activePlayerIndex() + 1,
                mgr.activePlayer().teamName.c_str(),
                mgr.canEndTurn());
    }
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
