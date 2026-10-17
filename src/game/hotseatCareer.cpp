#include "hotseatCareer.h"
#include "stdinc.h"
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
}

void HotseatCareerManager::initializeHotseat(const std::vector<int16_t>& teamIds)
{
    reset();
    m_numPlayers = std::min(static_cast<int>(teamIds.size()), kMaxHotseatPlayers);
    if (m_numPlayers < 2) {
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
    std::strncpy(reinterpret_cast<char *>(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer]), ctx.managerName.c_str(), 8);

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 0, 13);
    std::strncpy(reinterpret_cast<char *>(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer]), ctx.managerSurname.c_str(), 12);

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
