#include "hotseatInternal.h"
#include "menus.h"
#include "continueAbortMenu.h"
#include "drawMenu.h"
#include "mainMenu.h"
#include "careerPlayerCount.mnu.h"

using namespace HotseatInternal;

namespace {

int s_chosenPlayerCount = 0;
bool s_playerCountConfirmed = false;
int s_customPlayerCount = 5;

void selectPlayerCount(int count)
{
    s_chosenPlayerCount = count;
    s_playerCountConfirmed = true;
    SetExitMenuFlag();
}

void refreshCustomPlayerCountEntry()
{
    char buf[20];
    snprintf(buf, sizeof(buf), "%d %s", s_customPlayerCount, (s_customPlayerCount == 1) ? "PLAYER" : "PLAYERS");
    CareerPlayerCountMenu::customPlayersEntry.setString(copyStringToMenuBuffer(buf));
}

} // namespace

void onInitPlayerCountMenu()
{
    s_customPlayerCount = std::clamp(s_customPlayerCount, 1, kMaxHotseatPlayers);
    refreshCustomPlayerCountEntry();
}

void onSelect1Player() { selectPlayerCount(1); }
void onSelect2Players() { selectPlayerCount(2); }
void onSelect3Players() { selectPlayerCount(3); }
void onSelect4Players() { selectPlayerCount(4); }

void onDecreaseCustomPlayers()
{
    s_customPlayerCount = (s_customPlayerCount > 1) ? (s_customPlayerCount - 1) : kMaxHotseatPlayers;
    refreshCustomPlayerCountEntry();
}

void onIncreaseCustomPlayers()
{
    s_customPlayerCount = (s_customPlayerCount < kMaxHotseatPlayers) ? (s_customPlayerCount + 1) : 1;
    refreshCustomPlayerCountEntry();
}

void onSelectCustomPlayers()
{
    selectPlayerCount(s_customPlayerCount);
}

void onSelectExitPlayerCount()
{
    s_playerCountConfirmed = false;
    SetExitMenuFlag();
}

void HotseatCareerManager::resetWizard(int totalPlayers)
{
    m_wizardTotalPlayers = std::clamp(totalPlayers, 1, kMaxHotseatPlayers);
    m_wizardCurrentPlayer = 0;
    m_wizardAdvanced = false;
    m_isWizardActive = true;
    m_wizardPlayers.clear();
    m_wizardPlayers.resize(m_wizardTotalPlayers);
}

void HotseatCareerManager::applyWizardDefaultsToVm(int playerIndex) const
{
    extern void SetDefaultNameAndSurname();

    swos.coachOrPlayer = 1;
    swos.plNationality = kEng;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]) = 0;
    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], 0, 5);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], "MR", 2);

    if (playerIndex == 0) {
        SetDefaultNameAndSurname();
    } else {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 0, 9);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], "PLAYER", 6);

        std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 0, 13);
        std::string defSurname = std::to_string(playerIndex + 1);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], defSurname.data(),
                    std::min(defSurname.size(), size_t{12}));
    }
}

void HotseatCareerManager::assignTeamSlot(int16_t teamId, int slot, const TeamFile *tf)
{
    m_assignedTeamSlots[teamId] = slot;
    AssignedSlotInfo info;
    info.slot = slot;
    info.globalTeamNumber = teamId;
    if (tf) {
        info.teamWord = *reinterpret_cast<const uint16_t *>(tf);
        info.teamFileNo = tf->teamFileNo;
        info.league = tf->league;
        info.teamFile = *tf;
    }
    m_assignedSlotInfos[slot] = info;
}

bool HotseatCareerManager::areAllAssignedTeamsInSameLeague() const
{
    if (m_assignedSlotInfos.empty())
        return true;

    auto firstIt = m_assignedSlotInfos.begin();
    uint8_t targetCountry = firstIt->second.teamFileNo;
    uint8_t targetLeague = firstIt->second.league;

    for (const auto& pair : m_assignedSlotInfos) {
        if (pair.second.teamFileNo != targetCountry || pair.second.league != targetLeague) {
            return false;
        }
    }
    return true;
}

void HotseatCareerManager::unassignTeamSlot(int16_t teamId)
{
    auto it = m_assignedTeamSlots.find(teamId);
    if (it != m_assignedTeamSlots.end()) {
        int slot = it->second;
        m_assignedSlotInfos.erase(slot);
        m_assignedTeamSlots.erase(it);
    }
}

void HotseatCareerManager::unassignAllTeamSlots()
{
    m_assignedTeamSlots.clear();
    m_assignedSlotInfos.clear();
}

int HotseatCareerManager::getTeamSlot(int16_t teamId) const
{
    auto it = m_assignedTeamSlots.find(teamId);
    if (it != m_assignedTeamSlots.end())
        return it->second;
    return -1;
}

int HotseatCareerManager::getTeamForSlot(int slot) const
{
    for (const auto& pair : m_assignedTeamSlots) {
        if (pair.second == slot)
            return pair.first;
    }
    return 0;
}

const HotseatCareerManager::AssignedSlotInfo *HotseatCareerManager::getAssignedSlotInfo(int slot) const
{
    auto it = m_assignedSlotInfos.find(slot);
    if (it != m_assignedSlotInfos.end())
        return &it->second;
    return nullptr;
}

int HotseatCareerManager::getNextAvailableSlot() const
{
    for (int i = 0; i < m_careerNumPlayers; ++i) {
        bool used = false;
        for (const auto& pair : m_assignedTeamSlots) {
            if (pair.second == i) {
                used = true;
                break;
            }
        }
        if (!used)
            return i;
    }
    return -1;
}

int HotseatCareerManager::getAssignedSlotCount() const
{
    return static_cast<int>(m_assignedTeamSlots.size());
}

bool HotseatCareerManager::isTeamSelectionMenuOffset(uint32_t menuOffset) const
{
    return menuOffset == CareerOffsets::kChooseTeamsFriendly ||
           menuOffset == CareerOffsets::kChooseTeamsFriendlyMenu;
}

int HotseatCareerManager::getTeamSlotFromEntry(const MenuEntry *entry) const
{
    if (!entry || entry->invisible || !entry->isString())
        return -1;
    auto packed = getCurrentPackedMenu();
    if (!packed || !SwosVM::isSwosPtr(packed))
        return -1;
    uint32_t offset = SwosVM::ptrToOffset(packed);
    if (offset == CareerOffsets::kChooseTeamsFriendly) {
        if (entry->ordinal < 8 || entry->ordinal > 73)
            return -1;
    } else if (offset == CareerOffsets::kChooseTeamsFriendlyMenu) {
        if (entry->ordinal < 7 || entry->ordinal > 54)
            return -1;
    } else {
        return -1;
    }
    if (!entry->string() || !SwosVM::isSwosPtr(entry->fg.string) || SwosVM::ptrToOffset(entry->fg.string) < 5)
        return -1;
    auto tf = reinterpret_cast<const TeamFile *>(entry->fg.string - 5);
    return getTeamSlot(tf->globalTeamNumber);
}

void HotseatCareerManager::syncSelectedTeamsFromAssignedSlots()
{
    if (m_assignedSlotInfos.empty()) {
        swos.g_numSelectedTeams = 0;
        return;
    }

    bool hasAnyTeamFile = false;
    for (const auto& kv : m_assignedSlotInfos) {
        if (kv.second.teamFile.teamName[0] != '\0') {
            hasAnyTeamFile = true;
            break;
        }
    }
    if (!hasAnyTeamFile)
        return;

    if (!swos.selTeamsPtr) {
        swos.selTeamsPtr = SwosVM::ptrToOffset(swos.g_selectedTeams);
    }
    auto *dst = reinterpret_cast<TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!dst) {
        dst = reinterpret_cast<TeamFile *>(swos.g_selectedTeams);
        swos.selTeamsPtr = SwosVM::ptrToOffset(swos.g_selectedTeams);
    }

    int written = 0;
    for (int slot = 0; slot < m_careerNumPlayers && written < kMaxSelectedTeamsCount; ++slot) {
        auto it = m_assignedSlotInfos.find(slot);
        if (it == m_assignedSlotInfos.end() || it->second.teamFile.teamName[0] == '\0')
            continue;

        dst[written] = it->second.teamFile;
        int16_t role = (slot < wizardTotalPlayers()) ? wizardPlayer(slot).coachOrPlayer : 1;
        dst[written].teamControls = (role == 1) ? kPlayerCoach : kCoach;
        ++written;
    }
    swos.g_numSelectedTeams = static_cast<int16_t>(written);
}

void HotseatCareerManager::refreshTeamSelectionMenu(uint32_t menuOffset)
{
    if (!m_isSelectingCareerTeams || !isTeamSelectionMenuOffset(menuOffset))
        return;

    // Allow exiting across divisions, countries, and continents freely during multi-club career setup
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTopOfStackMark]) = 0;

    auto *menu = getCurrentMenu();
    if (!menu)
        return;

    bool isFriendly = (menuOffset == CareerOffsets::kChooseTeamsFriendly);
    int minTeamOrd = isFriendly ? 8 : 7;
    int maxTeamOrd = isFriendly ? 73 : 54;
    int playOrd = isFriendly ? 76 : 55;
    int statusOrd = isFriendly ? 77 : 58;

    if (!isFriendly) {
        uint32_t selLeagueText = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[329642]); // selectedTeamText
        if (selLeagueText != 0) {
            *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[329654]) = selLeagueText; // gameTypeStr
        }
        uint8_t loadedCountry = SwosVM::g_memByte[316568]; // lastLoadedExtNumber
        if (loadedCountry < 0xFE) {
            extern void GetCountryName();
            extern void ChooseTeamsSetHeader();
            save68kRegisters();
            D0 = loadedCountry;
            GetCountryName();
            ChooseTeamsSetHeader();
            restore68kRegisters();
        }
    }

    for (int ord = minTeamOrd; ord <= maxTeamOrd && ord < menu->numEntries; ++ord) {
        auto *e = getMenuEntry(ord);
        if (!e || e->invisible || !e->isString())
            continue;
        int slot = getTeamSlotFromEntry(e);
        if (slot >= 0) {
            int16_t role = (slot < wizardTotalPlayers()) ? wizardPlayer(slot).coachOrPlayer : 1;
            if (SwosVM::isSwosPtr(e->fg.string) && SwosVM::ptrToOffset(e->fg.string) >= 5) {
                auto *team = reinterpret_cast<TeamFile *>(e->fg.string - 5);
                team->teamControls = (role == 1) ? kPlayerCoach : kCoach;
            }
            e->bg.entryColor = (role == 1) ? 11 : 13;
        } else {
            if (!isFriendly) {
                e->bg.entryColor = 10;
            } else if (e->bg.entryColor == 11 || e->bg.entryColor == 13) {
                if (SwosVM::isSwosPtr(e->fg.string) && SwosVM::ptrToOffset(e->fg.string) >= 5) {
                    auto *team = reinterpret_cast<TeamFile *>(e->fg.string - 5);
                    team->teamControls = kComputerTeam;
                }
                e->bg.entryColor = 10;
            }
        }
    }

    int assigned = getAssignedSlotCount();
    int needed = m_careerNumPlayers;
    bool ready = (assigned >= needed);

    if (playOrd < menu->numEntries) {
        auto *playBtn = getMenuEntry(playOrd);
        if (ready) {
            playBtn->show();
            playBtn->enable();
        } else {
            playBtn->hide();
            playBtn->disable();
        }
    }

    if (statusOrd < menu->numEntries) {
        auto *statusLbl = getMenuEntry(statusOrd);
        if (ready) {
            statusLbl->hide();
        } else {
            statusLbl->show();
        }
        statusLbl->disable();
    }

    if (!isFriendly) {
        if (56 < menu->numEntries) {
            auto *viewBtn = getMenuEntry(56);
            if (assigned > 0) {
                viewBtn->show();
                viewBtn->enable();
            } else {
                viewBtn->hide();
                viewBtn->disable();
            }
        }
        if (57 < menu->numEntries) {
            auto *exitBtn = getMenuEntry(57);
            exitBtn->show();
            exitBtn->enable();
        }
    }

    auto selected = menu->selectedEntry;
    if (!selected || selected->invisible || (selected->ordinal == playOrd && !ready)) {
        if (isFriendly) {
            setCurrentEntry(ready ? 76 : 74);
        } else {
            setCurrentEntry(ready ? 55 : (assigned > 0 ? 56 : 57));
        }
    }
}

bool HotseatCareerManager::handleTeamSelectionMenuSelect(uint32_t menuOffset, MenuEntry *entry)
{
    if (!m_isSelectingCareerTeams || !isTeamSelectionMenuOffset(menuOffset) || !entry)
        return false;

    extern void ChooseTeamsFriendlyInit();
    extern void ChooseTeamsFriendlyInitFinish();

    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTopOfStackMark]) = 0;

    bool isFriendly = (menuOffset == CareerOffsets::kChooseTeamsFriendly);
    int minTeamOrd = isFriendly ? 8 : 7;
    int maxTeamOrd = isFriendly ? 73 : 54;
    int playOrd = isFriendly ? 76 : 55;

    auto reinitAfterModal = [&]() {
        save68kRegisters();
        if (isFriendly)
            ChooseTeamsFriendlyInit();
        else
            ChooseTeamsFriendlyInitFinish();
        restore68kRegisters();
        refreshTeamSelectionMenu(menuOffset);
    };

    if (entry->ordinal >= minTeamOrd && entry->ordinal <= maxTeamOrd &&
        !entry->invisible && entry->isString() &&
        SwosVM::isSwosPtr(entry->fg.string) && SwosVM::ptrToOffset(entry->fg.string) >= 5) {
        auto *team = reinterpret_cast<TeamFile *>(entry->fg.string - 5);
        int curSlot = getTeamSlot(team->globalTeamNumber);

        if (curSlot >= 0) {
            unassignTeamSlot(team->globalTeamNumber);
            team->teamControls = isFriendly ? kComputerTeam : kTeamNotSelected;
            entry->bg.entryColor = isFriendly ? 10 : 9;
            if (!isFriendly)
                syncSelectedTeamsFromAssignedSlots();
            refreshTeamSelectionMenu(menuOffset);
            entry->drawn = 0;
            drawMenu();
            return true;
        }

        int nextSlot = getNextAvailableSlot();
        if (nextSlot >= 0 && nextSlot < m_careerNumPlayers) {
            assignTeamSlot(team->globalTeamNumber, nextSlot, team);
            int16_t role = (nextSlot < wizardTotalPlayers()) ? wizardPlayer(nextSlot).coachOrPlayer : 1;
            team->teamControls = (role == 1) ? kPlayerCoach : kCoach;
            entry->bg.entryColor = (role == 1) ? 11 : 13;
            if (!isFriendly)
                syncSelectedTeamsFromAssignedSlots();
            refreshTeamSelectionMenu(menuOffset);
            entry->drawn = 0;
            drawMenu();
            return true;
        }

        char msg[64];
        snprintf(msg, sizeof(msg), "ALL %d CLUBS SELECTED! DESELECT ONE FIRST.", m_careerNumPlayers);
        showContinueAbortPrompt("CLUB SELECTION", "OK", nullptr, { msg }, false);
        reinitAfterModal();
        return true;
    }

    if (entry->ordinal == playOrd) {
        int assigned = getAssignedSlotCount();
        int needed = m_careerNumPlayers;
        if (assigned < needed) {
            char msg[48];
            snprintf(msg, sizeof(msg), "SELECT %d MORE CLUB%s!", needed - assigned, (needed - assigned == 1) ? "" : "S");
            showContinueAbortPrompt("CLUB SELECTION", "OK", nullptr, { msg }, false);
            reinitAfterModal();
            return true;
        }

        syncSelectedTeamsFromAssignedSlots();
        save68kRegisters();
        swos.controlMask = 0;
        A5 = entry;
        entry->onSelect();
        restore68kRegisters();
        return true;
    }

    if (isFriendly && entry->ordinal == 74) {
        // CHANGE / OTHER LEAGUES button: run SWOS ChangeTeams then keep all picked hotseat clubs
        save68kRegisters();
        swos.controlMask = 0;
        A5 = entry;
        entry->onSelect();
        restore68kRegisters();
        syncSelectedTeamsFromAssignedSlots();
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTopOfStackMark]) = 0;
        return true;
    }

    if (!isFriendly && entry->ordinal == 56) {
        // VIEW SELECTED button on chooseTeamsFriendlyMenu (499317)
        if (getAssignedSlotCount() <= 0) {
            refreshTeamSelectionMenu(menuOffset);
            return true;
        }
        syncSelectedTeamsFromAssignedSlots();
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTopOfStackMark]) = 0;
        save68kRegisters();
        swos.controlMask = 0;
        A5 = entry;
        entry->onSelect();
        restore68kRegisters();
        return true;
    }

    if (!isFriendly && entry->ordinal == 57) {
        // EXIT button on chooseTeamsFriendlyMenu (499317): navigate back to league/country list
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTopOfStackMark]) = 0;
        save68kRegisters();
        swos.controlMask = 0;
        A5 = entry;
        entry->onSelect();
        restore68kRegisters();
        return true;
    }

    return false;
}

void cseg_5DEB9();
void cseg_3D204();
void cseg_64A65();

namespace {

void applyPlayerIdentityToVm(const HotseatPlayerContext& p)
{
    swos.coachOrPlayer = p.coachOrPlayer;
    swos.plNationality = p.nationality;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]) = p.mrOrMs;
    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], 0, 5);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], p.mrOrMs != 0 ? "MS" : "MR", 2);

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 0, 9);
    size_t nlen = std::min(p.managerName.size(), size_t{8});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], p.managerName.data(), nlen);

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 0, 13);
    size_t slen = std::min(p.managerSurname.size(), size_t{12});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], p.managerSurname.data(), slen);
}

// Initializes a non-representative human club inside the world (league/division) that was just built by
// InitCareer() for the representative player `rep`, mirroring what SWOS does for the career club.
void initSiblingPlayerInWorld(HotseatCareerManager& mgr, HotseatPlayerContext& p, const HotseatPlayerContext& rep,
                              const TeamFile& team)
{
    p.currentSeasonShowing = rep.currentSeasonShowing;
    p.currentSeasonNumber = rep.currentSeasonNumber;
    p.seasonPlaying = rep.seasonPlaying;
    p.dsegD6CA2 = rep.dsegD6CA2;
    p.dsegD7E66 = rep.dsegD7E66;
    p.dsegE0048 = rep.dsegE0048;
    p.dsegD6B62 = rep.dsegD6B62;
    p.dsegD6B64 = rep.dsegD6B64;
    p.dsegD6B94 = rep.dsegD6B94;
    p.dsegD6C46 = rep.dsegD6C46;
    p.additionalInvestments = rep.additionalInvestments;
    p.dsegD6CDC = rep.dsegD6CDC;
    p.teamNoOfferAccepted = rep.teamNoOfferAccepted;
    p.teamFundsAccepted = rep.teamFundsAccepted;
    p.dsegD6D0C = rep.dsegD6D0C;
    p.timeToNegotiate = rep.timeToNegotiate;
    p.stayingInOldClub = 0;
    p.careerNumPlayers = 16;
    p.careerTeamLeague = reinterpret_cast<const TeamFile *>(p.careerTeam)->league;
    p.numPlayersOnTransferMarket = 0;
    std::memset(p.playersOnTransferMarket, 0, sizeof(p.playersOnTransferMarket));
    std::memset(p.dsegD6F26Block, 0, sizeof(p.dsegD6F26Block));
    std::memset(p.dsegD6DBA, 0, sizeof(p.dsegD6DBA));
    std::memset(p.newJobOffers, 0, sizeof(p.newJobOffers));
    std::memset(p.jobOffers, 0, sizeof(p.jobOffers));
    std::memset(p.playerTrialWeeks, 0, sizeof(p.playerTrialWeeks));
    std::memset(p.currentMatchPlayers, 0, kCurrentMatchPlayersSize);
    std::memcpy(p.currentMatchPlayers, team.playerNumbers, kNumPlayersInTeam);
    std::memset(p.trainingMatchSavedPlayers, 0, kCurrentMatchPlayersSize);
    std::memcpy(p.trainingMatchSavedPlayers, team.playerNumbers, kNumPlayersInTeam);
    std::memset(p.dsegD8B8C, 0, kCurrentMatchPlayersSize);
    p.dsegD761A = 0xFFFF;
    p.dsegD761C = 0;
    p.dsegD8B6A = 0xFFFF;
    std::memset(&p.careerTeam[sizeof(TeamFile)], 0, kCareerTeamBufferSize - sizeof(TeamFile));
    writeTeamCoachName(p.careerTeam, p.managerName, p.managerSurname);
    std::memcpy(p.userTactics, rep.userTactics, 6 * kUserTacticsSize);
    std::memcpy(p.careerHistory, rep.careerHistory, sizeof(p.careerHistory));

    size_t histOfs = static_cast<size_t>(p.currentSeasonShowing) * kCareerHistoryEntrySize;
    if (histOfs + kCareerHistoryEntrySize <= kCareerHistoryBufferSize) {
        *reinterpret_cast<uint16_t *>(&p.careerHistory[histOfs]) = p.teamId;
        std::memset(&p.careerHistory[histOfs + 2], 0, 18);
        std::memcpy(&p.careerHistory[histOfs + 2], p.teamName.c_str(), std::min(p.teamName.size(), size_t{17}));
        uint32_t euroStrOfs = 0;
        if (p.euroCupIndex >= 0 && p.euroCupIndex < kNumEuroCups && mgr.isEuroCupActive(p.euroCupIndex)) {
            mgr.activateEuroCup(p.euroCupIndex, false);
            uint32_t strPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA109]);
            euroStrOfs = (strPtr >= CareerOffsets::kChairmanScenes) ? (strPtr - CareerOffsets::kChairmanScenes) : 0;
        }
        *reinterpret_cast<uint32_t *>(&p.careerHistory[histOfs + 34]) = euroStrOfs;
    }

    mgr.loadSwosContext(p, true);
    save68kRegisters();
    if (hasCountryInSelectedTeams(static_cast<uint8_t>(p.teamId & 0xFF))) {
        cseg_64A65();
    } else if (!isVmNationalSelector()) {
        static const uint8_t kReservePositions[10] = { 2, 1, 5, 4, 3, 3, 6, 6, 7, 7 };
        for (int r = 0; r < 10; ++r) {
            int ord = 16 + r;
            auto *pf = reinterpret_cast<PlayerFile *>(
                &SwosVM::g_memByte[CareerOffsets::kCareerTeam + kTeamFileHeaderSize + ord * kPlayerFileSize]);
            std::memset(pf, 0, kPlayerFileSize);
            pf->shirtNumber = 16;
            pf->positionAndFace = static_cast<byte>(kReservePositions[r] << 5);
            populateNamedTrialPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam],
                                         &SwosVM::g_memByte[CareerOffsets::kDsegD6C26],
                                         ord, static_cast<uint16_t>(p.teamId), false);
            SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + ord] = static_cast<uint8_t>(ord);
            SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers + ord] = static_cast<uint8_t>(ord);
            SwosVM::g_memByte[CareerOffsets::kDsegD8B8C + ord] = pf->shirtNumber;
        }
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]) = 26;
    }
    cseg_5DEB9();
    cseg_3D204();
    restore68kRegisters();
    mgr.saveCurrentSwosContext(p);
}

void clearUnusedCupBuffersInVm(const HotseatCareerManager& mgr)
{
    if (*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]) == 0) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], 0, kCup2BufferSize);
    }
    if (!mgr.anyEuroCupActive() && *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) == 0) {
        uint8_t schedTpl[6];
        std::memcpy(schedTpl, &SwosVM::g_memByte[CareerOffsets::kDsegDA11F], sizeof(schedTpl));
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA11F], schedTpl, sizeof(schedTpl));
    }
}

} // namespace

int HotseatCareerManager::initializeStartingWorlds(const std::vector<TeamFile>& orderedTeams)
{
    extern void InitCareer();

    int numPlayers = std::min<int>(m_numPlayers, static_cast<int>(orderedTeams.size()));
    if (numPlayers < 1)
        return 0;

    ScopedInitializingSeason initSeasonGuard(*this);

    // Group human clubs by their starting (country, division) world, keeping first-appearance order so the
    // world of player 1 is always built first (and owns the shared European cup draw).
    std::vector<LeagueWorldKey> worldOrder;
    std::map<LeagueWorldKey, std::vector<int>> worldPlayers;
    for (int i = 0; i < numPlayers; ++i) {
        LeagueWorldKey key;
        key.countryFileNo = static_cast<uint8_t>(static_cast<uint16_t>(m_players[i].teamId) & 0xFFu);
        key.divisionId = orderedTeams[i].league;
        if (worldPlayers.count(key) == 0)
            worldOrder.push_back(key);
        worldPlayers[key].push_back(i);
    }

    struct CountryCups {
        std::vector<uint8_t> cup1, cup2;
        uint32_t ptr1 = 0, ptr2 = 0;
    };
    std::map<uint8_t, CountryCups> countryCups;
    bool multiWorld = worldOrder.size() > 1;

    for (size_t w = 0; w < worldOrder.size(); ++w) {
        const LeagueWorldKey& key = worldOrder[w];
        const auto& members = worldPlayers[key];
        int rep = members[0];

        if (multiWorld) {
            setPreserveEuroCupsOnSeasonInit(w > 0);
            m_calendarSlots.clear();
            m_euroCupByCalOfs.clear();
        }

        m_activePlayerIndex = rep;
        applyPlayerIdentityToVm(m_players[rep]);
        logInfo("Initializing world (country=%d, division=%d) via InitCareer with D0=0x%04x (P%d)",
                key.countryFileNo, key.divisionId, static_cast<uint16_t>(m_players[rep].teamId), rep + 1);
        swos.g_gameType = kGameTypeCareer;
        *(uint16_t *)&D0 = static_cast<uint16_t>(m_players[rep].teamId);
        InitCareer();
        swos.g_gameType = kGameTypeCareer;

        m_activePlayerIndex = rep;
        saveCurrentSwosContext(m_players[rep]);
        reinterpret_cast<TeamFile *>(m_players[rep].careerTeam)->teamControls =
            (m_players[rep].coachOrPlayer == 1 ? kPlayerCoach : kCoach);
        cacheTeamsFromSelectedTeams();
        cacheTeamFile(*reinterpret_cast<const TeamFile *>(m_players[rep].careerTeam));

        // Divisions of the same country share their domestic cups (e.g. Eng Div 2 & Eng Div 3 in one FA Cup)
        if (multiWorld) {
            auto cupIt = countryCups.find(key.countryFileNo);
            if (cupIt == countryCups.end()) {
                CountryCups cc;
                cc.cup1.assign(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A],
                               &SwosVM::g_memByte[CareerOffsets::kDsegD8D0A] + kCup1BufferSize);
                cc.cup2.assign(&SwosVM::g_memByte[CareerOffsets::kDsegD985C],
                               &SwosVM::g_memByte[CareerOffsets::kDsegD985C] + kCup2BufferSize);
                cc.ptr1 = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 4]);
                cc.ptr2 = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]);
                countryCups[key.countryFileNo] = std::move(cc);
            } else {
                std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A], cupIt->second.cup1.data(), kCup1BufferSize);
                std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], cupIt->second.cup2.data(), kCup2BufferSize);
                *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 4]) = cupIt->second.ptr1;
                *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]) = cupIt->second.ptr2;
                ensureCupTeamsInSelectedTeams();
            }
        }

        std::vector<uint8_t> worldSelTeams(kSelectedTeamsBufferSize);
        std::memcpy(worldSelTeams.data(), swos.g_selectedTeams, kSelectedTeamsBufferSize);
        int16_t worldNumSelTeams = swos.g_numSelectedTeams;

        for (size_t mi = 1; mi < members.size(); ++mi) {
            int idx = members[mi];
            m_activePlayerIndex = idx;
            initSiblingPlayerInWorld(*this, m_players[idx], m_players[rep], orderedTeams[idx]);
            std::memcpy(swos.g_selectedTeams, worldSelTeams.data(), kSelectedTeamsBufferSize);
            swos.g_numSelectedTeams = worldNumSelTeams;
        }

        clearUnusedCupBuffersInVm(*this);

        if (!multiWorld)
            break;

        // Snapshot this world into its LeagueWorld/CountryWorld state before building the next one
        m_activePlayerIndex = rep;
        loadSwosContext(m_players[rep], false);
        syncPlayersWithSelectedTeams(true);
        ensureHotseatTeamControlsInSelectedTeams();
        relinkCompetitionTeamPointers();
        m_roundCalendarIndex = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);
        captureActiveWorldAsInitial(key);
    }

    if (!multiWorld) {
        syncPlayersWithSelectedTeams(true);
        setRoundCalendarIndex(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]));

        int firstTurnPlayer = findNextPlayerForCurrentCalendarSlot();
        if (firstTurnPlayer < 0)
            firstTurnPlayer = 0;

        switchToPlayer(firstTurnPlayer, true);
        captureActiveWorldAsInitial(worldKeyForPlayer(m_players[firstTurnPlayer]));
        return firstTurnPlayer;
    }

    setPreserveEuroCupsOnSeasonInit(false);
    synchronizeSharedCupCalendarEntries();
    m_globalWeek = 0;
    resolveLockstepAndSelectPlayer(false);
    return m_activePlayerIndex;
}

void startCareerMode()
{
    extern void cseg_4C6EE();
    extern void CommonMenuExit();
    extern void CheckContinueAbortPrompt();

    logInfo("Starting Career Mode setup flow");

    if (swos.g_gameType != kGameTypeNoGame) {
        save68kRegisters();
        A0 = 18550; // offset aCareer ("CAREER")
        CheckContinueAbortPrompt();
        uint16_t aborted = D0.asWord();
        restore68kRegisters();
        swos.g_exitMenu = 0;
        if (aborted != 0 || swos.g_gameType != kGameTypeNoGame) {
            if (mainMenuActive())
                CommonMenuExit();
            return;
        }
        if (mainMenuActive())
            CommonMenuExit();
    }

    auto& mgr = HotseatCareerManager::get();
    mgr.reset();

    auto abortCareerSetup = [&]() {
        mgr.setSelectingCareerTeams(false);
        mgr.unassignAllTeamSlots();
        mgr.setWizardActive(false);
        mgr.reset();
        save68kRegisters();
        D0 = kGameTypeNoGame;
        ::CareerInitVars();
        restore68kRegisters();
        swos.g_exitMenu = 0;
        if (mainMenuActive())
            CommonMenuExit();
    };

    s_chosenPlayerCount = 0;
    s_playerCountConfirmed = false;
    showMenu(careerPlayerCountMenu);
    if (!s_playerCountConfirmed || s_chosenPlayerCount < 1 || s_chosenPlayerCount > kMaxHotseatPlayers) {
        logInfo("Career player count selection canceled or exited");
        abortCareerSetup();
        return;
    }

    int numPlayers = s_chosenPlayerCount;
    logInfo("Career Mode: %d player(s) selected", numPlayers);

    if (numPlayers == 1) {
        extern void CareerMenu();
        mgr.reset();
        CareerMenu();
        return;
    }

    mgr.resetWizard(numPlayers);

    for (int p = 0; p < numPlayers; ++p) {
        mgr.setWizardCurrentPlayer(p);
        mgr.setWizardAdvanced(false);
        mgr.applyWizardDefaultsToVm(p);

        A6 = 483353; // careerMenu
        SWOS::ShowMenu();

        if (!mgr.wizardAdvanced()) {
            logInfo("Player %d setup aborted by user", p + 1);
            abortCareerSetup();
            return;
        }
    }
    mgr.setWizardActive(false);

    mgr.setSelectingCareerTeams(true);
    mgr.setCareerNumPlayers(numPlayers);
    mgr.unassignAllTeamSlots();

    cseg_4C6EE();

    mgr.setSelectingCareerTeams(false);

    uint16_t result = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kChooseTeamsResult]);
    if (result == 0 || mgr.getAssignedSlotCount() < numPlayers) {
        logInfo("Career club selection canceled or incomplete (assigned=%d, needed=%d)",
                mgr.getAssignedSlotCount(), numPlayers);
        abortCareerSetup();
        return;
    }

    std::vector<int16_t> orderedTeamIds(numPlayers);
    std::vector<TeamFile> orderedTeams(numPlayers);

    for (int i = 0; i < numPlayers; ++i) {
        auto slotInfo = mgr.getAssignedSlotInfo(i);
        if (slotInfo && slotInfo->teamWord != 0) {
            orderedTeamIds[i] = static_cast<int16_t>(slotInfo->teamWord);
            orderedTeams[i] = slotInfo->teamFile;
        } else {
            int16_t tid = mgr.getTeamForSlot(i);
            bool found = false;
            int count = swos.g_numSelectedTeams;
            if (count <= 0 || count > 100) count = 100;

            for (int t = 0; t < count; ++t) {
                auto tf = reinterpret_cast<const TeamFile *>(&swos.g_selectedTeams[t * sizeof(TeamFile)]);
                if (tf->globalTeamNumber == tid) {
                    orderedTeams[i] = *tf;
                    orderedTeamIds[i] = *reinterpret_cast<const int16_t *>(tf);
                    found = true;
                    break;
                }
            }
            if (!found && swos.selTeamsPtr) {
                auto selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
                if (selTeams) {
                    for (int t = 0; t < count; ++t) {
                        if (selTeams[t].globalTeamNumber == tid) {
                            orderedTeams[i] = selTeams[t];
                            orderedTeamIds[i] = *reinterpret_cast<const int16_t *>(&selTeams[t]);
                            found = true;
                            break;
                        }
                    }
                }
            }
        }

        logInfo("Player %d assigned: %.*s (teamWord=0x%04x, countryFile=%d, ordinal=%d, globalId=%d)",
                i + 1, 17, orderedTeams[i].teamName,
                static_cast<uint16_t>(orderedTeamIds[i]),
                orderedTeams[i].teamFileNo, orderedTeams[i].teamOrdinal,
                orderedTeams[i].globalTeamNumber);
    }

    std::vector<WizardPlayerInfo> savedWizardPlayers;
    savedWizardPlayers.reserve(numPlayers);
    for (int i = 0; i < numPlayers; ++i)
        savedWizardPlayers.push_back(mgr.wizardPlayer(i));

    mgr.initializeHotseat(orderedTeamIds);
    ScopedInitializingSeason initSeasonGuard(mgr);

    extern void CalculateStartingBalance();

    for (int i = 0; i < numPlayers; ++i) {
        auto& p = mgr.player(i);
        const auto& wp = savedWizardPlayers[i];
        p.managerName = wp.name;
        p.managerSurname = wp.surname;
        p.nationality = wp.nationality;
        p.coachOrPlayer = wp.coachOrPlayer;
        p.mrOrMs = wp.mrOrMs;
        p.careerNumPlayers = 16;

        char tbuf[18] = {0};
        std::memcpy(tbuf, orderedTeams[i].teamName, 17);
        p.teamName = tbuf;
        std::memcpy(p.careerTeam, &orderedTeams[i], sizeof(TeamFile));
        auto tf = reinterpret_cast<TeamFile *>(p.careerTeam);
        tf->teamControls = (p.coachOrPlayer == 1 ? kPlayerCoach : kCoach);
        p.careerTeamLeague = tf->league;

        save68kRegisters();
        *(uint16_t *)&D0 = orderedTeamIds[i];
        CalculateStartingBalance();
        int32_t startingBal = static_cast<int32_t>(D1);
        restore68kRegisters();

        if (startingBal <= 0) startingBal = 1'000'000;
        p.startingBalance = startingBal;
        p.newBalance = startingBal;
        p.oldBalance = startingBal;
        p.playerWagesBill = 0;
        p.playerPurchases = 0;
        p.playerSales = 0;
        p.generalRunningCosts = 0;
        p.gateReceipts = 0;
        p.competitionBonuses = 0;
        p.sensisoftPlcSponsorship = 0;
    }

    int firstTurnPlayer = mgr.initializeStartingWorlds(orderedTeams);
    logInfo("Hotseat Career successfully initialized with %d players in %d world(s)! Starting P%d's turn.",
            numPlayers, std::max(1, mgr.worldCount()), firstTurnPlayer + 1);

    swos.g_exitMenu = 0;
    A6 = 482233; // contestMenu
    SWOS::ShowMenu();
    CommonMenuExit();
}
