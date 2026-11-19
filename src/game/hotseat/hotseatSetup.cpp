#include "hotseatInternal.h"
#include "menus.h"
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

int HotseatCareerManager::getTeamSlotFromEntry(const MenuEntry *entry) const
{
    if (!entry || !entry->fg.string || entry->invisible)
        return -1;
    auto packed = getCurrentPackedMenu();
    if (!packed || !SwosVM::isSwosPtr(packed) || SwosVM::ptrToOffset(packed) != 485019)
        return -1;
    auto tf = reinterpret_cast<const TeamFile *>(entry->fg.string - 5);
    return getTeamSlot(tf->globalTeamNumber);
}

void startCareerMode()
{
    extern void cseg_4C6EE();
    extern void InitCareer();
    extern void CommonMenuExit();

    logInfo("Starting Career Mode setup flow");

    s_chosenPlayerCount = 0;
    s_playerCountConfirmed = false;
    showMenu(careerPlayerCountMenu);
    if (!s_playerCountConfirmed || s_chosenPlayerCount < 1 || s_chosenPlayerCount > kMaxHotseatPlayers) {
        logInfo("Career player count selection canceled or exited");
        return;
    }

    int numPlayers = s_chosenPlayerCount;
    logInfo("Career Mode: %d player(s) selected", numPlayers);

    auto& mgr = HotseatCareerManager::get();
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
            mgr.setWizardActive(false);
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

    const auto& wp0 = savedWizardPlayers[0];
    swos.coachOrPlayer = wp0.coachOrPlayer;
    swos.plNationality = wp0.nationality;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]) = wp0.mrOrMs;
    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], 0, 5);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], wp0.mrOrMs != 0 ? "MS" : "MR", 2);

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 0, 9);
    size_t nlen = std::min(wp0.name.size(), size_t{8});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], wp0.name.data(), nlen);

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 0, 13);
    size_t slen = std::min(wp0.surname.size(), size_t{12});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], wp0.surname.data(), slen);

    logInfo("Calling InitCareer with D0=0x%04x (countryFile=%d, ordinal=%d)",
            static_cast<uint16_t>(orderedTeamIds[0]),
            orderedTeams[0].teamFileNo, orderedTeams[0].teamOrdinal);
    swos.g_gameType = kGameTypeCareer;
    *(uint16_t *)&D0 = orderedTeamIds[0];
    InitCareer();
    swos.g_gameType = kGameTypeCareer;

    mgr.saveCurrentSwosContext(mgr.player(0));
    reinterpret_cast<TeamFile *>(mgr.player(0).careerTeam)->teamControls =
        (mgr.player(0).coachOrPlayer == 1 ? kPlayerCoach : kCoach);

    extern void cseg_5DEB9();
    extern void cseg_3D204();
    extern void cseg_64A65();

    const auto& p0 = mgr.player(0);
    for (int i = 1; i < numPlayers; ++i) {
        auto& p = mgr.player(i);
        p.currentSeasonShowing = p0.currentSeasonShowing;
        p.currentSeasonNumber = p0.currentSeasonNumber;
        p.seasonPlaying = p0.seasonPlaying;
        p.dsegD6CA2 = p0.dsegD6CA2;
        p.dsegD7E66 = p0.dsegD7E66;
        p.dsegE0048 = p0.dsegE0048;
        p.dsegD6B62 = p0.dsegD6B62;
        p.dsegD6B64 = p0.dsegD6B64;
        p.dsegD6B94 = p0.dsegD6B94;
        p.dsegD6C46 = p0.dsegD6C46;
        p.additionalInvestments = p0.additionalInvestments;
        p.dsegD6CDC = p0.dsegD6CDC;
        p.teamNoOfferAccepted = p0.teamNoOfferAccepted;
        p.teamFundsAccepted = p0.teamFundsAccepted;
        p.dsegD6D0C = p0.dsegD6D0C;
        p.timeToNegotiate = p0.timeToNegotiate;
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
        std::memcpy(p.currentMatchPlayers, orderedTeams[i].playerNumbers, kNumPlayersInTeam);
        std::memset(p.trainingMatchSavedPlayers, 0, kCurrentMatchPlayersSize);
        std::memcpy(p.trainingMatchSavedPlayers, orderedTeams[i].playerNumbers, kNumPlayersInTeam);
        std::memset(p.dsegD8B8C, 0, kCurrentMatchPlayersSize);
        p.dsegD761A = 0xFFFF;
        p.dsegD761C = 0;
        p.dsegD8B6A = 0xFFFF;
        std::memset(&p.careerTeam[sizeof(TeamFile)], 0, kCareerTeamBufferSize - sizeof(TeamFile));
        writeTeamCoachName(p.careerTeam, p.managerName, p.managerSurname);
        std::memcpy(p.userTactics, p0.userTactics, 6 * kUserTacticsSize);
        std::memcpy(p.careerHistory, p0.careerHistory, sizeof(p.careerHistory));

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

    if (*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]) == 0) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], 0, kCup2BufferSize);
    }
    if (!mgr.anyEuroCupActive() && *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) == 0) {
        uint8_t schedTpl[6];
        std::memcpy(schedTpl, &SwosVM::g_memByte[CareerOffsets::kDsegDA11F], sizeof(schedTpl));
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA11F], schedTpl, sizeof(schedTpl));
    }

    mgr.syncPlayersWithSelectedTeams(true);
    mgr.setRoundCalendarIndex(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]));

    int firstTurnPlayer = mgr.findNextPlayerForCurrentCalendarSlot();
    if (firstTurnPlayer < 0)
        firstTurnPlayer = 0;

    mgr.switchToPlayer(firstTurnPlayer, true);
    logInfo("Hotseat Career successfully initialized with %d players! Starting P%d's turn.",
            numPlayers, firstTurnPlayer + 1);

    swos.g_exitMenu = 0;
    A6 = 482233; // contestMenu
    SWOS::ShowMenu();
    CommonMenuExit();
}
