#include "hotseatCareer.h"
#include "stdinc.h"
#include "menus.h"
#include "unpackMenu.h"
#include "careerPlayerCount.mnu.h"
#include <cstring>
#include <algorithm>

HotseatCareerManager& HotseatCareerManager::get()
{
    static HotseatCareerManager instance;
    return instance;
}

void HotseatCareerManager::reset()
{
    m_active = false;
    m_numPlayers = 0;
    m_activePlayerIndex = 0;
    m_transferOffers.clear();
    for (auto& p : m_players) {
        p = HotseatPlayerContext();
    }
    m_isWizardActive = false;
    m_wizardCurrentPlayer = 0;
    m_wizardTotalPlayers = 1;
    m_wizardAdvanced = false;
    m_wizardPlayers.clear();
    m_isSelectingCareerTeams = false;
    m_careerNumPlayers = 1;
    m_assignedTeamSlots.clear();
}

void HotseatCareerManager::initializeHotseat(const std::vector<int16_t>& teamIds)
{
    reset();
    m_numPlayers = std::min(static_cast<int>(teamIds.size()), kMaxHotseatPlayers);
    if (m_numPlayers < 1) {
        m_active = false;
        return;
    }

    m_active = true;
    m_activePlayerIndex = 0;

    for (int i = 0; i < m_numPlayers; ++i) {
        m_players[i].teamId = teamIds[i];
        m_players[i].managerName = "PLAYER";
        m_players[i].managerSurname = std::to_string(i + 1);
        m_players[i].coachOrPlayer = 0;
        m_players[i].nationality = 8; // English default
        m_players[i].matchPlayedThisTurn = false;
        m_players[i].turnCompleted = false;
    }

    saveCurrentSwosContext(m_players[0]);
    m_players[0].teamId = teamIds[0];
}

void HotseatCareerManager::saveCurrentSwosContext(HotseatPlayerContext& ctx)
{
    int16_t vmTeamId = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    if (vmTeamId != 0 || ctx.teamId == 0)
        ctx.teamId = vmTeamId;
    ctx.coachOrPlayer = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kCoachOrPlayer]);
    ctx.mrOrMs = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]);
    ctx.nationality = SwosVM::g_memByte[CareerOffsets::kPlNationality];

    ctx.newBalance = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    ctx.oldBalance = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kOldBalance]);
    ctx.startingBalance = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kStartingBalance]);
    ctx.playerPurchases = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerPurchases]);
    ctx.playerSales = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerSales]);
    ctx.generalRunningCosts = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGeneralRunningCosts]);
    ctx.playerWagesBill = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerWagesBill]);
    ctx.gateReceipts = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGateReceipts]);
    ctx.competitionBonuses = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionBonuses]);
    ctx.sensisoftPlcSponsorship = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kSensisoftPlcSponsorship]);

    ctx.youreSacked = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kYoureSacked]);
    ctx.numNewJobOffers = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]);
    ctx.numJobOffers = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]);
    ctx.currentSeasonNumber = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]);

    std::memcpy(ctx.careerTeam, &SwosVM::g_memByte[CareerOffsets::kCareerTeam], kCareerTeamBufferSize);
    std::memcpy(ctx.careerHistory, &SwosVM::g_memByte[CareerOffsets::kCareerHistory], kCareerHistoryBufferSize);
    std::memcpy(ctx.userTactics, &SwosVM::g_memByte[CareerOffsets::kUserTacticsStart], 6 * kUserTacticsSize);
}

void HotseatCareerManager::loadSwosContext(const HotseatPlayerContext& ctx)
{
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]) = ctx.teamId;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kCoachOrPlayer]) = ctx.coachOrPlayer;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]) = ctx.mrOrMs;
    SwosVM::g_memByte[CareerOffsets::kPlNationality] = ctx.nationality;

    // Manager name buffers (null-terminated)
    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 0, 9);
    size_t nameLen = std::min(ctx.managerName.size(), size_t{8});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], ctx.managerName.data(), nameLen);

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 0, 13);
    size_t surnameLen = std::min(ctx.managerSurname.size(), size_t{12});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], ctx.managerSurname.data(), surnameLen);

    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]) = ctx.newBalance;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kOldBalance]) = ctx.oldBalance;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kStartingBalance]) = ctx.startingBalance;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerPurchases]) = ctx.playerPurchases;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerSales]) = ctx.playerSales;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGeneralRunningCosts]) = ctx.generalRunningCosts;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerWagesBill]) = ctx.playerWagesBill;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGateReceipts]) = ctx.gateReceipts;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionBonuses]) = ctx.competitionBonuses;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kSensisoftPlcSponsorship]) = ctx.sensisoftPlcSponsorship;

    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kYoureSacked]) = ctx.youreSacked;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]) = ctx.numNewJobOffers;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]) = ctx.numJobOffers;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]) = ctx.currentSeasonNumber;

    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], ctx.careerTeam, kCareerTeamBufferSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCareerHistory], ctx.careerHistory, kCareerHistoryBufferSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kUserTacticsStart], ctx.userTactics, 6 * kUserTacticsSize);
}

void HotseatCareerManager::switchToPlayer(int index)
{
    if (index < 0 || index >= m_numPlayers)
        return;

    m_activePlayerIndex = index;
    loadSwosContext(m_players[m_activePlayerIndex]);
}

bool HotseatCareerManager::canEndTurn() const
{
    if (!m_active)
        return false;
    return m_players[m_activePlayerIndex].matchPlayedThisTurn;
}

void HotseatCareerManager::onMatchCompleted()
{
    if (m_active && m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) {
        m_players[m_activePlayerIndex].matchPlayedThisTurn = true;
    }
}

bool HotseatCareerManager::endTurn()
{
    if (!canEndTurn())
        return false;

    // Save active player's latest state
    saveCurrentSwosContext(m_players[m_activePlayerIndex]);
    m_players[m_activePlayerIndex].turnCompleted = true;

    // Advance to next player
    int nextIndex = (m_activePlayerIndex + 1) % m_numPlayers;
    bool newRound = (nextIndex == 0);

    if (newRound) {
        // Reset match flags for the new fixture round
        for (int i = 0; i < m_numPlayers; ++i) {
            m_players[i].matchPlayedThisTurn = false;
            m_players[i].turnCompleted = false;
        }
    }

    switchToPlayer(nextIndex);
    return newRound;
}

void HotseatCareerManager::markPlayerMatchCompleted(int playerIndex)
{
    if (playerIndex >= 0 && playerIndex < m_numPlayers) {
        m_players[playerIndex].matchPlayedThisTurn = true;
    }
}

static bool teamNamesMatch(const std::string& a, const char *b)
{
    if (!b || a.empty())
        return false;

    size_t aLen = a.size();
    while (aLen > 0 && (a[aLen - 1] == ' ' || a[aLen - 1] == '*'))
        --aLen;

    size_t bLen = strlen(b);
    while (bLen > 0 && (b[bLen - 1] == ' ' || b[bLen - 1] == '*'))
        --bLen;

    if (aLen != bLen)
        return false;

    return strncmp(a.c_str(), b, aLen) == 0;
}

int HotseatCareerManager::findHotseatPlayerByTeamName(const char *teamName) const
{
    if (!teamName || !m_active)
        return -1;

    for (int i = 0; i < m_numPlayers; ++i) {
        if (teamNamesMatch(m_players[i].teamName, teamName))
            return i;
    }
    return -1;
}

int HotseatCareerManager::findHotseatPlayerByTeamId(int16_t teamId) const
{
    if (!m_active)
        return -1;

    for (int i = 0; i < m_numPlayers; ++i) {
        if (m_players[i].teamId == teamId)
            return i;
    }
    return -1;
}

void HotseatCareerManager::queueTransferOffer(const TransferOffer& offer)
{
    m_transferOffers.push_back(offer);
}

std::vector<HotseatCareerManager::TransferOffer> HotseatCareerManager::getPendingOffersForPlayer(int playerIndex)
{
    std::vector<TransferOffer> result;
    for (const auto& offer : m_transferOffers) {
        if (offer.toPlayerIndex == playerIndex && offer.pending) {
            result.push_back(offer);
        }
    }
    return result;
}

void HotseatCareerManager::resolveTransferOffer(size_t offerIndex, bool accepted)
{
    if (offerIndex >= m_transferOffers.size())
        return;

    auto& offer = m_transferOffers[offerIndex];
    offer.pending = false;
    offer.accepted = accepted;

    if (accepted) {
        // Transfer money from buyer (fromPlayerIndex) to seller (toPlayerIndex)
        if (offer.fromPlayerIndex >= 0 && offer.fromPlayerIndex < m_numPlayers &&
            offer.toPlayerIndex >= 0 && offer.toPlayerIndex < m_numPlayers) {
            m_players[offer.fromPlayerIndex].newBalance -= offer.offerAmount;
            m_players[offer.toPlayerIndex].newBalance += offer.offerAmount;

            // Transfer squad member if valid ordinal
            if (offer.playerOrdinal >= 0 && offer.playerOrdinal < 16) {
                constexpr size_t kHeaderSize = 76;
                constexpr size_t kPlayerSize = 38;
                size_t srcOffset = kHeaderSize + offer.playerOrdinal * kPlayerSize;
                size_t dstOffset = kHeaderSize + 15 * kPlayerSize; // transfer to reserve slot 15

                auto& sellerTeam = m_players[offer.toPlayerIndex].careerTeam;
                auto& buyerTeam = m_players[offer.fromPlayerIndex].careerTeam;
                std::memcpy(&buyerTeam[dstOffset], &sellerTeam[srcOffset], kPlayerSize);
            }

            // Sync with current VM memory if active player is involved
            if (offer.fromPlayerIndex == m_activePlayerIndex || offer.toPlayerIndex == m_activePlayerIndex) {
                *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]) =
                    m_players[m_activePlayerIndex].newBalance;
            }
        }
    }
}

static int s_chosenPlayerCount = 0;
static bool s_playerCountConfirmed = false;

void onSelect1Player()
{
    s_chosenPlayerCount = 1;
    s_playerCountConfirmed = true;
    SetExitMenuFlag();
}

void onSelect2Players()
{
    s_chosenPlayerCount = 2;
    s_playerCountConfirmed = true;
    SetExitMenuFlag();
}

void onSelect3Players()
{
    s_chosenPlayerCount = 3;
    s_playerCountConfirmed = true;
    SetExitMenuFlag();
}

void onSelect4Players()
{
    s_chosenPlayerCount = 4;
    s_playerCountConfirmed = true;
    SetExitMenuFlag();
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

void HotseatCareerManager::assignTeamSlot(int16_t teamId, int slot)
{
    m_assignedTeamSlots[teamId] = slot;
}

void HotseatCareerManager::unassignTeamSlot(int16_t teamId)
{
    m_assignedTeamSlots.erase(teamId);
}

void HotseatCareerManager::unassignAllTeamSlots()
{
    m_assignedTeamSlots.clear();
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
    if (!entry || !entry->fg.string)
        return -1;
    auto tf = reinterpret_cast<const TeamFile *>(entry->fg.string - 5);
    return getTeamSlot(tf->globalTeamNumber);
}

void startCareerMode()
{
    extern void SetDefaultNameAndSurname();
    extern void cseg_4C6EE();
    extern void InitCareer();
    extern void CommonMenuExit();

    logInfo("Starting Career Mode setup flow");

    // Step 1: Prompt for Number of Players (1, 2, 3, 4)
    s_chosenPlayerCount = 0;
    s_playerCountConfirmed = false;
    showMenu(careerPlayerCountMenu);
    if (!s_playerCountConfirmed || s_chosenPlayerCount < 1 || s_chosenPlayerCount > 4) {
        logInfo("Career player count selection canceled or exited");
        return;
    }

    int numPlayers = s_chosenPlayerCount;
    logInfo("Career Mode: %d player(s) selected", numPlayers);

    auto& mgr = HotseatCareerManager::get();
    mgr.resetWizard(numPlayers);

    // Step 2: Sequential Player Setup Wizard (Name, Surname, Nationality, Role)
    for (int p = 0; p < numPlayers; ++p) {
        mgr.setWizardCurrentPlayer(p);
        mgr.setWizardAdvanced(false);

        if (p == 0) {
            SetDefaultNameAndSurname();
            swos.coachOrPlayer = 1; // Default to Player-Coach
            swos.plNationality = kEng;
        } else {
            std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], ' ', 8);
            std::string defName = "PLAYER";
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], defName.c_str(), defName.size());

            std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], ' ', 12);
            std::string defSurname = std::to_string(p + 1);
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], defSurname.c_str(), defSurname.size());

            swos.coachOrPlayer = 1;
            swos.plNationality = kEng;
        }

        A6 = 483353; // careerMenu
        SWOS::ShowMenu();

        if (!mgr.wizardAdvanced()) {
            logInfo("Player %d setup aborted by user", p + 1);
            mgr.setWizardActive(false);
            return;
        }
    }
    mgr.setWizardActive(false);

    // Step 3: Club Selection
    mgr.setSelectingCareerTeams(true);
    mgr.setCareerNumPlayers(numPlayers);
    mgr.unassignAllTeamSlots();

    cseg_4C6EE();

    mgr.setSelectingCareerTeams(false);

    uint16_t result = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[330146]); // chooseTeamsResult
    if (result == 0 || mgr.getAssignedSlotCount() < numPlayers) {
        logInfo("Career club selection canceled or incomplete (assigned=%d, needed=%d)",
                mgr.getAssignedSlotCount(), numPlayers);
        return;
    }

    // Step 4: Gather teams ordered by player slots (P1 -> index 0, P2 -> index 1, etc.)
    std::vector<int16_t> orderedTeamIds(numPlayers);
    std::vector<TeamFile> orderedTeams(numPlayers);

    for (int i = 0; i < numPlayers; ++i) {
        int16_t tid = mgr.getTeamForSlot(i);
        orderedTeamIds[i] = tid;

        bool found = false;
        int count = swos.g_numSelectedTeams;
        if (count <= 0 || count > 100) count = 100;

        for (int t = 0; t < count; ++t) {
            auto tf = reinterpret_cast<const TeamFile *>(&swos.g_selectedTeams[t * sizeof(TeamFile)]);
            if (tf->globalTeamNumber == tid) {
                orderedTeams[i] = *tf;
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
                        found = true;
                        break;
                    }
                }
            }
        }
    }

    mgr.initializeHotseat(orderedTeamIds);

    for (int i = 0; i < numPlayers; ++i) {
        auto& p = mgr.player(i);
        const auto& wp = mgr.wizardPlayer(i);
        p.managerName = wp.name;
        p.managerSurname = wp.surname;
        p.nationality = wp.nationality;
        p.coachOrPlayer = wp.coachOrPlayer;
        p.mrOrMs = wp.mrOrMs;

        char tbuf[18] = {0};
        std::memcpy(tbuf, orderedTeams[i].teamName, 17);
        p.teamName = tbuf;
        std::memcpy(p.careerTeam, &orderedTeams[i], sizeof(TeamFile));
    }

    // Initialize division career with Player 0's team ID
    *(uint16_t *)&D0 = orderedTeamIds[0];
    InitCareer();
    swos.g_gameType = kGameTypeCareer;

    // Save Player 0's complete context
    mgr.saveCurrentSwosContext(mgr.player(0));

    // Set up remaining players with squads and starting balances
    for (int i = 1; i < numPlayers; ++i) {
        auto& p = mgr.player(i);
        p.newBalance = mgr.player(0).newBalance;
        p.oldBalance = mgr.player(0).oldBalance;
        p.playerWagesBill = mgr.player(0).playerWagesBill;
        std::memcpy(p.careerHistory, mgr.player(0).careerHistory, sizeof(p.careerHistory));
    }

    // Load Player 0's context to begin P1's turn
    mgr.loadSwosContext(mgr.player(0));
    logInfo("Hotseat Career successfully initialized with %d players! Starting P1's turn.", numPlayers);

    swos.g_exitMenu = 0;

    // Launch contestMenu cleanly in its own ShowMenu loop!
    A6 = 482233; // contestMenu
    SWOS::ShowMenu();
    CommonMenuExit();
}

