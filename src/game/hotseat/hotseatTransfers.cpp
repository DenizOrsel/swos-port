#include "hotseatInternal.h"
#include "continueAbortMenu.h"

using namespace HotseatInternal;

namespace {

struct CopiedTransferPlayer {
    PlayerFile player{};
    uint32_t wage = 0;
};

struct PendingOfferRemap {
    bool removed = false;
    int oldOrd = -1;
    int newOrd = -1;
};

void getBuyerAndSellerIndices(const HotseatCareerManager::TransferOffer& offer, int& buyerIdx, int& sellerIdx)
{
    if (offer.isCounterOffer) {
        buyerIdx = offer.toPlayerIndex;
        sellerIdx = offer.fromPlayerIndex;
    } else {
        buyerIdx = offer.fromPlayerIndex;
        sellerIdx = offer.toPlayerIndex;
    }
}

void collectUniqueValidOrdinals(int primary, int secondary, std::vector<int>& out)
{
    out.clear();
    if (primary >= 0)
        out.push_back(primary);
    if (secondary >= 0 && secondary != primary)
        out.push_back(secondary);
}

bool isOrdinalActiveInClub(const HotseatPlayerContext& ctx, int ord)
{
    if (ord < 0 || ord >= kMaxCareerSquadPlayers)
        return false;
    uint16_t count = ctx.careerNumPlayers;
    if (count == 0)
        return false;
    uint16_t limit = std::min<uint16_t>(count, static_cast<uint16_t>(kCurrentMatchPlayersSize));
    if (hasInitializedMatchPlayers(ctx.currentMatchPlayers, limit)) {
        for (uint16_t i = 0; i < limit; ++i) {
            if (ctx.currentMatchPlayers[i] == static_cast<uint8_t>(ord))
                return true;
        }
        return false;
    }
    return ord < static_cast<int>(count);
}

bool appendPlayerToClub(HotseatPlayerContext& club, const CopiedTransferPlayer& incoming)
{
    uint16_t count = std::max<uint16_t>(club.careerNumPlayers, static_cast<uint16_t>(kMinCareerSquadPlayers));
    if (count >= kMaxCareerSquadPlayers)
        return false;

    bool hasMatchPlayers = hasInitializedMatchPlayers(club.currentMatchPlayers, count);
    size_t slot = count;
    if (hasMatchPlayers) {
        for (size_t cand = 0; cand < kMaxCareerSquadPlayers; ++cand) {
            bool used = false;
            for (uint16_t k = 0; k < count && k < kCurrentMatchPlayersSize; ++k) {
                if (club.currentMatchPlayers[k] == cand) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                slot = cand;
                break;
            }
        }
    }

    size_t dstOffset = kTeamFileHeaderSize + slot * kPlayerFileSize;
    if (dstOffset + kPlayerFileSize > kCareerTeamBufferSize)
        return false;

    std::memcpy(&club.careerTeam[dstOffset], &incoming.player, kPlayerFileSize);
    auto *newPlayer = reinterpret_cast<PlayerFile *>(&club.careerTeam[dstOffset]);
    newPlayer->positionAndFace = static_cast<byte>(newPlayer->positionAndFace & ~0x02u);

    if (newPlayer->shirtNumber != 0) {
        bool usedNumbers[33] = { false };
        for (uint16_t k = 0; k < count; ++k) {
            size_t idx = hasMatchPlayers ? club.currentMatchPlayers[k] : k;
            if (kTeamFileHeaderSize + idx * kPlayerFileSize + kPlayerFileSize <= kCareerTeamBufferSize) {
                uint8_t num = reinterpret_cast<const PlayerFile *>(&club.careerTeam[kTeamFileHeaderSize + idx * kPlayerFileSize])->shirtNumber;
                if (num < 33)
                    usedNumbers[num] = true;
            }
        }
        if (newPlayer->shirtNumber < 33 && usedNumbers[newPlayer->shirtNumber]) {
            for (uint8_t sn = 1; sn < 33; ++sn) {
                if (!usedNumbers[sn]) {
                    newPlayer->shirtNumber = sn;
                    break;
                }
            }
        }
    }

    if (hasMatchPlayers && count < kCurrentMatchPlayersSize) {
        club.currentMatchPlayers[count] = static_cast<uint8_t>(slot);
    }
    if (slot < kPlayerWagesCount) {
        club.playerWages[slot] = incoming.wage;
        club.playerTrialWeeks[slot] = 0;
        club.dsegD8B8C[slot] = newPlayer->shirtNumber;
    }
    club.careerNumPlayers = count + 1;
    return true;
}

PendingOfferRemap removeOrdinalFromClub(HotseatPlayerContext& club, int soldOrd, std::vector<int>& pendingRemovals)
{
    PendingOfferRemap result;
    uint16_t count = club.careerNumPlayers;
    if (count == 0 || soldOrd < 0 || soldOrd >= kMaxCareerSquadPlayers)
        return result;

    result.removed = true;
    result.oldOrd = soldOrd;

    size_t lastOrdinal = count - 1;
    bool denseCompact = (static_cast<size_t>(soldOrd) != lastOrdinal) &&
                        isOrdinalActiveInClub(club, static_cast<int>(lastOrdinal));

    size_t soldOffset = kTeamFileHeaderSize + static_cast<size_t>(soldOrd) * kPlayerFileSize;
    size_t clearOffset = kTeamFileHeaderSize + (denseCompact ? lastOrdinal : static_cast<size_t>(soldOrd)) * kPlayerFileSize;

    if (denseCompact && soldOffset + kPlayerFileSize <= kCareerTeamBufferSize &&
        clearOffset + kPlayerFileSize <= kCareerTeamBufferSize) {
        std::memcpy(&club.careerTeam[soldOffset], &club.careerTeam[clearOffset], kPlayerFileSize);
        if (soldOrd < kPlayerWagesCount && lastOrdinal < kPlayerWagesCount) {
            club.playerWages[soldOrd] = club.playerWages[lastOrdinal];
            club.playerTrialWeeks[soldOrd] = club.playerTrialWeeks[lastOrdinal];
            club.dsegD8B8C[soldOrd] = club.dsegD8B8C[lastOrdinal];
        }
        result.newOrd = static_cast<int>(lastOrdinal);
    }

    if (clearOffset + kPlayerFileSize <= kCareerTeamBufferSize) {
        std::memset(&club.careerTeam[clearOffset], 0, kPlayerFileSize);
    }
    size_t clearedOrd = denseCompact ? lastOrdinal : static_cast<size_t>(soldOrd);
    if (clearedOrd < kPlayerWagesCount) {
        club.playerWages[clearedOrd] = 0;
        club.playerTrialWeeks[clearedOrd] = 0;
        club.dsegD8B8C[clearedOrd] = 0;
    }

    {
        uint16_t listedWords[5] = {};
        int kept = 0;
        int numListed = std::min<int>(club.numPlayersOnTransferMarket, 5);
        const auto *listedPtr = reinterpret_cast<const uint16_t *>(club.playersOnTransferMarket);
        for (int k = 0; k < numListed; ++k) {
            uint16_t ord = listedPtr[k];
            if (ord == static_cast<uint16_t>(soldOrd))
                continue;
            if (denseCompact && ord == static_cast<uint16_t>(lastOrdinal))
                ord = static_cast<uint16_t>(soldOrd);
            listedWords[kept++] = ord;
        }
        std::memset(club.playersOnTransferMarket, 0, sizeof(club.playersOnTransferMarket));
        std::memcpy(club.playersOnTransferMarket, listedWords, kept * sizeof(uint16_t));
        club.numPlayersOnTransferMarket = static_cast<uint16_t>(kept);
    }

    if (hasInitializedMatchPlayers(club.currentMatchPlayers, count)) {
        uint16_t limit = std::min<uint16_t>(count, static_cast<uint16_t>(kCurrentMatchPlayersSize));
        int removePos = -1;
        for (uint16_t k = 0; k < limit; ++k) {
            if (club.currentMatchPlayers[k] == static_cast<uint8_t>(soldOrd)) {
                removePos = static_cast<int>(k);
            } else if (denseCompact && club.currentMatchPlayers[k] == static_cast<uint8_t>(lastOrdinal)) {
                club.currentMatchPlayers[k] = static_cast<uint8_t>(soldOrd);
            }
        }
        if (removePos >= 0) {
            if (removePos < 16 && limit > 16) {
                int repPos = 16;
                for (uint16_t k = 16; k < limit; ++k) {
                    if (denseCompact && club.currentMatchPlayers[k] == static_cast<uint8_t>(soldOrd)) {
                        repPos = static_cast<int>(k);
                        break;
                    }
                }
                club.currentMatchPlayers[removePos] = club.currentMatchPlayers[repPos];
                removePos = repPos;
            }
            for (int k = removePos; k + 1 < limit; ++k) {
                club.currentMatchPlayers[k] = club.currentMatchPlayers[k + 1];
            }
            club.currentMatchPlayers[limit - 1] = 0;
        }
    }

    if (denseCompact) {
        for (int& pendingOrd : pendingRemovals) {
            if (pendingOrd == static_cast<int>(lastOrdinal))
                pendingOrd = soldOrd;
        }
    }

    club.careerNumPlayers = count - 1;
    return result;
}

void applyClubTransferChanges(HotseatPlayerContext& club,
                              std::vector<int>& outOrdinals,
                              const std::vector<CopiedTransferPlayer>& inPlayers,
                              std::vector<PendingOfferRemap>& remapsOut)
{
    size_t outIdx = 0;
    while (outIdx < outOrdinals.size() && club.careerNumPlayers > kMinCareerSquadPlayers) {
        int ord = outOrdinals[outIdx++];
        remapsOut.push_back(removeOrdinalFromClub(club, ord, outOrdinals));
    }

    for (const auto& incoming : inPlayers) {
        appendPlayerToClub(club, incoming);
    }

    while (outIdx < outOrdinals.size() && club.careerNumPlayers > 0) {
        int ord = outOrdinals[outIdx++];
        remapsOut.push_back(removeOrdinalFromClub(club, ord, outOrdinals));
    }

    syncTrainingMatchSavedPlayers(club.trainingMatchSavedPlayers, club.dsegD8B8C, club.careerTeam,
                                  club.careerNumPlayers, club.currentMatchPlayers);
}

void updatePendingOffersForClubChange(std::vector<HotseatCareerManager::TransferOffer>& offers,
                                      int acceptedOfferId,
                                      int changedPlayerIdx,
                                      const std::vector<PendingOfferRemap>& remaps)
{
    auto updateField = [&](int& fieldOrd, bool& cancel) {
        if (fieldOrd < 0)
            return;
        for (const auto& remap : remaps) {
            if (!remap.removed)
                continue;
            if (fieldOrd == remap.oldOrd) {
                cancel = true;
                return;
            }
            if (remap.newOrd >= 0 && fieldOrd == remap.newOrd) {
                fieldOrd = remap.oldOrd;
            }
        }
    };

    for (auto& other : offers) {
        if (other.id == acceptedOfferId || !other.pending)
            continue;
        int ob = -1, os = -1;
        getBuyerAndSellerIndices(other, ob, os);
        bool cancel = false;
        if (os == changedPlayerIdx) {
            updateField(other.playerOrdinal, cancel);
            updateField(other.extraWantedOrdinal, cancel);
        }
        if (!cancel && ob == changedPlayerIdx) {
            updateField(other.exchangeOrdinal1, cancel);
            updateField(other.exchangeOrdinal2, cancel);
        }
        if (cancel) {
            other.pending = false;
            other.accepted = false;
            other.rejected = true;
        }
    }
}

void formatExchangeSummary(const HotseatCareerManager::TransferOffer& offer,
                           const char *amtStr, char *out, size_t outSize)
{
    bool hasEx1 = offer.exchangeOrdinal1 >= 0 && offer.exchangeName1[0] != '\0';
    bool hasEx2 = offer.exchangeOrdinal2 >= 0 && offer.exchangeName2[0] != '\0';
    if (hasEx1 && hasEx2) {
        snprintf(out, outSize, "%s + %s + %s", amtStr, offer.exchangeName1, offer.exchangeName2);
    } else if (hasEx1) {
        snprintf(out, outSize, "%s + %s", amtStr, offer.exchangeName1);
    } else if (hasEx2) {
        snprintf(out, outSize, "%s + %s", amtStr, offer.exchangeName2);
    } else {
        snprintf(out, outSize, "%s", amtStr);
    }
}

bool marketPoolContains(const MarketEntry *entries, uint16_t count, uint16_t teamId, uint8_t ord)
{
    for (uint16_t i = 0; i < count; ++i) {
        if (entries[i].teamId == teamId && entries[i].playerOrdinal == ord)
            return true;
    }
    return false;
}

bool isHotseatListedPlayerValid(const HotseatPlayerContext& owner, uint8_t ord)
{
    if (ord >= 30)
        return false;

    int count = std::min<int>(owner.careerNumPlayers, kCurrentMatchPlayersSize);
    bool inActiveSquad = false;
    for (int i = 0; i < count; ++i) {
        if (owner.currentMatchPlayers[i] == ord) {
            inActiveSquad = true;
            break;
        }
    }
    if (!inActiveSquad)
        return false;

    const auto *pf = reinterpret_cast<const PlayerFile *>(
        owner.careerTeam + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize);
    return isValidPlayerName(pf->playerName) && (pf->positionAndFace & 0x02) == 0;
}

bool isOrdinalInHotseatTransferList(const HotseatPlayerContext& owner, uint8_t ord)
{
    int listedCount = std::min<int>(owner.numPlayersOnTransferMarket, 5);
    const auto *listed = reinterpret_cast<const uint16_t *>(owner.playersOnTransferMarket);
    for (int i = 0; i < listedCount; ++i) {
        if (listed[i] == ord && isHotseatListedPlayerValid(owner, ord))
            return true;
    }
    return false;
}

void sanitizeActivePlayerTransferList()
{
    uint16_t cnt = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]);
    int n = std::min<int>(cnt, 5);
    auto *listed = reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket]);
    uint16_t careerCount = std::min<uint16_t>(
        *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]),
        static_cast<uint16_t>(kCurrentMatchPlayersSize));
    const uint8_t *cmp = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];

    uint16_t kept[5] = {};
    int outCount = 0;
    for (int i = 0; i < n; ++i) {
        uint16_t ord = listed[i];
        if (ord >= 30)
            continue;
        bool inSquad = false;
        for (uint16_t s = 0; s < careerCount; ++s) {
            if (cmp[s] == ord) {
                inSquad = true;
                break;
            }
        }
        if (!inSquad)
            continue;
        const auto *pf = reinterpret_cast<const PlayerFile *>(
            &SwosVM::g_memByte[CareerOffsets::kCareerTeam + kTeamFileHeaderSize + ord * kPlayerFileSize]);
        if (!isValidPlayerName(pf->playerName) || (pf->positionAndFace & 0x02) != 0)
            continue;
        bool dup = false;
        for (int j = 0; j < outCount; ++j) {
            if (kept[j] == ord) {
                dup = true;
                break;
            }
        }
        if (!dup)
            kept[outCount++] = ord;
    }

    std::memset(listed, 0, kListedPlayersSize);
    std::memcpy(listed, kept, outCount * sizeof(uint16_t));
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]) =
        static_cast<uint16_t>(outCount);
}

uint32_t nextHotseatMarketRand(uint32_t salt)
{
    static uint32_t state = 0x6D2B79F5u;
    state = state * 1664525u + 1013904223u + salt;
    return state >> 16;
}

void tryAddAiDomesticMarketReplacement(HotseatCareerManager& mgr)
{
    if (!swos.selTeamsPtr || swos.g_numSelectedTeams <= 0)
        return;

    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return;

    int teamCount = std::min<int>(swos.g_numSelectedTeams, 68400 / static_cast<int>(sizeof(TeamFile)));
    uint16_t activeSelTeam = getVmSelTeamNumber();
    uint8_t activeCountry = static_cast<uint8_t>(activeSelTeam & 0xFF);

    std::vector<int> eligibleAiTeams;
    for (int t = 0; t < teamCount; ++t) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[t]);
        if (static_cast<uint8_t>(selTeams[t].teamFileNo) != activeCountry)
            continue;
        if (tid == activeSelTeam || mgr.findHotseatPlayerByTeamId(static_cast<int16_t>(tid)) >= 0)
            continue;
        eligibleAiTeams.push_back(t);
    }
    if (eligibleAiTeams.empty())
        return;

    uint16_t count = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]);
    if (count >= kMaxHomeMarketEntries)
        return;

    auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[CareerOffsets::kHomeMarketPlayers]);

    for (int attempt = 0; attempt < 128; ++attempt) {
        uint32_t r = nextHotseatMarketRand(activeSelTeam ^ count ^ static_cast<uint32_t>(attempt));

        int tIdx = eligibleAiTeams[(r + attempt) % eligibleAiTeams.size()];
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[tIdx]);
        uint8_t pOrd = static_cast<uint8_t>(((r >> 8) + attempt) & 0x0F);
        if (!isValidPlayerName(selTeams[tIdx].players[pOrd].playerName))
            continue;
        if (marketPoolContains(entries, count, tid, pOrd))
            continue;

        save68kRegisters();
        D0 = tid;
        D1 = pOrd;
        ::AddPlayerToCareerContest();
        restore68kRegisters();
        return;
    }
}

void tryAddAiForeignMarketReplacement(HotseatCareerManager& mgr)
{
    if (*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kPoolplyrLoaded]) == 0)
        return;

    uint16_t count = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]);
    if (count >= kMaxForeignMarketEntries)
        return;

    const auto *pool = reinterpret_cast<const PoolPlayerEntry *>(&SwosVM::g_memByte[CareerOffsets::kPoolplyrDatBuffer]);
    auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[CareerOffsets::kForeignMarketPlayers]);
    uint8_t activeCountry = static_cast<uint8_t>(getVmSelTeamNumber() & 0xFF);

    uint32_t seed = nextHotseatMarketRand(activeCountry ^ count);

    for (int offset = 0; offset < kPoolPlyrCount; ++offset) {
        const auto& pe = pool[(seed + offset * 17) % kPoolPlyrCount];
        if (pe.teamId == 0 || static_cast<uint8_t>(pe.teamId & 0xFF) == activeCountry)
            continue;
        if (mgr.findHotseatPlayerByTeamId(static_cast<int16_t>(pe.teamId)) >= 0)
            continue;
        if (!isValidPlayerName(pe.player.playerName))
            continue;
        if (marketPoolContains(entries, count, pe.teamId, pe.playerOrdinal))
            continue;

        save68kRegisters();
        D0 = pe.teamId;
        D1 = pe.playerOrdinal;
        ::AddForeignMarketPlayer();
        restore68kRegisters();
        return;
    }
}

} // namespace

int HotseatCareerManager::queueTransferOffer(const TransferOffer& offer)
{
    TransferOffer o = offer;
    o.id = m_nextOfferId++;
    o.pending = true;
    o.accepted = false;
    o.rejected = false;
    m_transferOffers.push_back(o);
    return o.id;
}

std::vector<HotseatCareerManager::TransferOffer> HotseatCareerManager::getPendingOffersForPlayer(int playerIndex) const
{
    std::vector<TransferOffer> result;
    for (const auto& offer : m_transferOffers) {
        if (offer.toPlayerIndex == playerIndex && offer.pending) {
            result.push_back(offer);
        }
    }
    return result;
}

HotseatCareerManager::TransferOffer* HotseatCareerManager::getTransferOfferById(int offerId)
{
    for (auto& offer : m_transferOffers) {
        if (offer.id == offerId)
            return &offer;
    }
    return nullptr;
}

const HotseatCareerManager::TransferOffer* HotseatCareerManager::getTransferOfferById(int offerId) const
{
    for (const auto& offer : m_transferOffers) {
        if (offer.id == offerId)
            return &offer;
    }
    return nullptr;
}

bool HotseatCareerManager::canAcceptTransferOffer(int offerId, std::string *reasonOut /* = nullptr */) const
{
    const auto offerPtr = getTransferOfferById(offerId);
    if (!offerPtr || !offerPtr->pending) {
        if (reasonOut) *reasonOut = "OFFER NO LONGER PENDING.";
        return false;
    }

    int buyerIdx = -1, sellerIdx = -1;
    getBuyerAndSellerIndices(*offerPtr, buyerIdx, sellerIdx);

    if (buyerIdx < 0 || buyerIdx >= m_numPlayers || sellerIdx < 0 || sellerIdx >= m_numPlayers || buyerIdx == sellerIdx) {
        if (reasonOut) *reasonOut = "INVALID CLUB PARTICIPANTS.";
        return false;
    }

    const auto& buyer = m_players[buyerIdx];
    const auto& seller = m_players[sellerIdx];

    if (buyer.newBalance < static_cast<int32_t>(offerPtr->offerAmount)) {
        if (reasonOut) *reasonOut = "BUYER LACKS SUFFICIENT FUNDS.";
        return false;
    }

    std::vector<int> wantedOrds, exchangeOrds;
    collectUniqueValidOrdinals(offerPtr->playerOrdinal, offerPtr->extraWantedOrdinal, wantedOrds);
    collectUniqueValidOrdinals(offerPtr->exchangeOrdinal1, offerPtr->exchangeOrdinal2, exchangeOrds);

    if (wantedOrds.empty()) {
        if (reasonOut) *reasonOut = "PLAYER NO LONGER IN SELLER SQUAD.";
        return false;
    }
    for (int ord : wantedOrds) {
        if (!isOrdinalActiveInClub(seller, ord)) {
            if (reasonOut) *reasonOut = "PLAYER NO LONGER IN SELLER SQUAD.";
            return false;
        }
    }
    for (int ord : exchangeOrds) {
        if (!isOrdinalActiveInClub(buyer, ord)) {
            if (reasonOut) *reasonOut = "EXCHANGE PLAYER NO LONGER IN BUYER SQUAD.";
            return false;
        }
    }

    int buyerAfter = static_cast<int>(buyer.careerNumPlayers) + static_cast<int>(wantedOrds.size()) - static_cast<int>(exchangeOrds.size());
    int sellerAfter = static_cast<int>(seller.careerNumPlayers) - static_cast<int>(wantedOrds.size()) + static_cast<int>(exchangeOrds.size());

    if (buyerAfter > kMaxCareerSquadPlayers) {
        if (reasonOut) *reasonOut = "BUYER SQUAD IS FULL (MAX 30).";
        return false;
    }
    if (buyerAfter < kMinCareerSquadPlayers) {
        if (reasonOut) *reasonOut = "BUYER MUST KEEP AT LEAST 16 PLAYERS.";
        return false;
    }
    if (sellerAfter < kMinCareerSquadPlayers) {
        if (reasonOut) *reasonOut = "SELLER MUST KEEP AT LEAST 16 PLAYERS.";
        return false;
    }
    if (sellerAfter > kMaxCareerSquadPlayers) {
        if (reasonOut) *reasonOut = "SELLER SQUAD WOULD EXCEED 30 PLAYERS.";
        return false;
    }
    return true;
}

bool HotseatCareerManager::resolveTransferOffer(int offerId, bool accepted)
{
    auto offerPtr = getTransferOfferById(offerId);
    if (!offerPtr || !offerPtr->pending)
        return false;

    if (!accepted) {
        offerPtr->pending = false;
        offerPtr->accepted = false;
        offerPtr->rejected = true;
        return true;
    }

    if (!canAcceptTransferOffer(offerId)) {
        offerPtr->pending = false;
        offerPtr->accepted = false;
        offerPtr->rejected = true;
        return false;
    }

    int buyerIdx = -1, sellerIdx = -1;
    getBuyerAndSellerIndices(*offerPtr, buyerIdx, sellerIdx);

    auto& buyer = m_players[buyerIdx];
    auto& seller = m_players[sellerIdx];

    offerPtr->pending = false;
    offerPtr->accepted = true;
    offerPtr->rejected = false;

    buyer.newBalance -= static_cast<int32_t>(offerPtr->offerAmount);
    buyer.playerPurchases += static_cast<int32_t>(offerPtr->offerAmount);
    seller.newBalance += static_cast<int32_t>(offerPtr->offerAmount);
    seller.playerSales += static_cast<int32_t>(offerPtr->offerAmount);

    std::vector<int> wantedOrds, exchangeOrds;
    collectUniqueValidOrdinals(offerPtr->playerOrdinal, offerPtr->extraWantedOrdinal, wantedOrds);
    collectUniqueValidOrdinals(offerPtr->exchangeOrdinal1, offerPtr->exchangeOrdinal2, exchangeOrds);

    auto snapshotPlayers = [](const HotseatPlayerContext& src, const std::vector<int>& ords) {
        std::vector<CopiedTransferPlayer> out;
        out.reserve(ords.size());
        for (int ord : ords) {
            CopiedTransferPlayer c;
            size_t ofs = kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize;
            if (ofs + kPlayerFileSize <= kCareerTeamBufferSize) {
                std::memcpy(&c.player, &src.careerTeam[ofs], kPlayerFileSize);
            }
            c.wage = (ord >= 0 && ord < kPlayerWagesCount) ? src.playerWages[ord] : 0;
            out.push_back(c);
        }
        return out;
    };

    std::vector<CopiedTransferPlayer> wantedCopies = snapshotPlayers(seller, wantedOrds);
    std::vector<CopiedTransferPlayer> exchangeCopies = snapshotPlayers(buyer, exchangeOrds);

    std::vector<PendingOfferRemap> sellerRemaps, buyerRemaps;
    applyClubTransferChanges(seller, wantedOrds, exchangeCopies, sellerRemaps);
    applyClubTransferChanges(buyer, exchangeOrds, wantedCopies, buyerRemaps);

    updatePendingOffersForClubChange(m_transferOffers, offerId, sellerIdx, sellerRemaps);
    updatePendingOffersForClubChange(m_transferOffers, offerId, buyerIdx, buyerRemaps);

    if (m_active && m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers &&
        (buyerIdx == m_activePlayerIndex || sellerIdx == m_activePlayerIndex)) {
        loadSwosContext(m_players[m_activePlayerIndex], false);
    }
    if (swos.selTeamsPtr && swos.g_numSelectedTeams > 0) {
        syncPlayersWithSelectedTeams(true);
    }
    syncTransferMarketPoolsForCurrentPlayer();
    return true;
}

void HotseatCareerManager::counterTransferOffer(int offerId, uint32_t counterAmount)
{
    auto offerPtr = getTransferOfferById(offerId);
    if (!offerPtr || !offerPtr->pending)
        return;

    std::swap(offerPtr->fromPlayerIndex, offerPtr->toPlayerIndex);
    offerPtr->offerAmount = counterAmount;
    offerPtr->isCounterOffer = true;
}

bool HotseatCareerManager::processPendingOffersForActivePlayer()
{
    if (!m_active)
        return false;

    auto pending = getPendingOffersForPlayer(m_activePlayerIndex);
    if (pending.empty())
        return false;

    saveCurrentSwosContext(m_players[m_activePlayerIndex]);

    for (const auto& offer : pending) {
        char line1[64], line2[96], line3[96], line4[64];
        char amtStr[32], termStr[96];
        formatCurrencyAmount(amtStr, sizeof(amtStr), offer.offerAmount);
        formatExchangeSummary(offer, amtStr, termStr, sizeof(termStr));

        char targetStr[80];
        if (offer.extraWantedOrdinal >= 0 && offer.extraWantedName[0] != '\0') {
            snprintf(targetStr, sizeof(targetStr), "%s + %s", offer.playerName, offer.extraWantedName);
        } else {
            snprintf(targetStr, sizeof(targetStr), "%s", offer.playerName);
        }

        if (!offer.isCounterOffer) {
            snprintf(line1, sizeof(line1), "PLAYER %d (%s) BIDS",
                     offer.fromPlayerIndex + 1, m_players[offer.fromPlayerIndex].teamName.c_str());
            snprintf(line2, sizeof(line2), "FOR: %s", targetStr);
            snprintf(line3, sizeof(line3), "OFFER: %s", termStr);
            snprintf(line4, sizeof(line4), "ACCEPT THIS OFFER?");

            std::vector<const char*> lines = { line1, line2, line3, line4 };
            bool accept = showContinueAbortPrompt("TRANSFER OFFER", "ACCEPT", "DECLINE / COUNTER", lines, false);
            if (accept) {
                std::string reason;
                if (resolveTransferOffer(offer.id, true)) {
                    showContinueAbortPrompt("DEAL AGREED", "CONTINUE", nullptr,
                        { "OFFER ACCEPTED!", "FUNDS AND SQUAD UPDATED." }, false);
                } else {
                    canAcceptTransferOffer(offer.id, &reason);
                    if (reason.empty()) reason = "CANNOT COMPLETE TRANSFER.";
                    showContinueAbortPrompt("TRANSFER FAILED", "CONTINUE", nullptr, { reason.c_str() }, false);
                }
            } else {
                snprintf(line1, sizeof(line1), "BID: %s FOR %s", termStr, targetStr);
                snprintf(line2, sizeof(line2), "DO YOU WANT TO COUNTER");
                snprintf(line3, sizeof(line3), "OR REJECT THE BID?");
                std::vector<const char*> optLines = { line1, line2, line3 };
                bool counter = showContinueAbortPrompt("COUNTER OR REJECT", "COUNTER", "REJECT", optLines, false);
                if (counter) {
                    uint32_t c25 = offer.offerAmount + (offer.offerAmount / 4);
                    uint32_t c50 = offer.offerAmount + (offer.offerAmount / 2);
                    char c25Str[32], c50Str[32];
                    snprintf(c25Str, sizeof(c25Str), "1. +25%% (approx \x9c%uK)", c25 / 1000);
                    snprintf(c50Str, sizeof(c50Str), "2. +50%% (approx \x9c%uK)", c50 / 1000);
                    std::vector<const char*> cLines = { "SELECT COUNTER-OFFER:", c25Str, c50Str };
                    bool pick50 = showContinueAbortPrompt("COUNTER AMOUNT", "+50%", "+25%", cLines, false);
                    uint32_t chosenAmount = pick50 ? c50 : c25;
                    counterTransferOffer(offer.id, chosenAmount);
                    showContinueAbortPrompt("COUNTER SENT", "CONTINUE", nullptr,
                        { "COUNTER-OFFER TRANSMITTED!", "AWAITING BUYER TURN." }, false);
                } else {
                    resolveTransferOffer(offer.id, false);
                    showContinueAbortPrompt("OFFER REJECTED", "CONTINUE", nullptr,
                        { "OFFER HAS BEEN REJECTED." }, false);
                }
            }
        } else {
            snprintf(line1, sizeof(line1), "PLAYER %d (%s) COUNTERED",
                     offer.fromPlayerIndex + 1, m_players[offer.fromPlayerIndex].teamName.c_str());
            snprintf(line2, sizeof(line2), "FOR: %s", targetStr);
            snprintf(line3, sizeof(line3), "NEW TERMS: %s", termStr);
            snprintf(line4, sizeof(line4), "ACCEPT COUNTER-OFFER?");

            std::vector<const char*> lines = { line1, line2, line3, line4 };
            bool accept = showContinueAbortPrompt("COUNTER-OFFER", "ACCEPT", "REJECT", lines, false);
            if (accept) {
                std::string reason;
                if (resolveTransferOffer(offer.id, true)) {
                    showContinueAbortPrompt("DEAL AGREED", "CONTINUE", nullptr,
                        { "COUNTER-OFFER ACCEPTED!", "FUNDS AND SQUAD UPDATED." }, false);
                } else {
                    canAcceptTransferOffer(offer.id, &reason);
                    if (reason.empty()) reason = "CANNOT COMPLETE TRANSFER.";
                    showContinueAbortPrompt("TRANSFER FAILED", "CONTINUE", nullptr, { reason.c_str() }, false);
                }
            } else {
                resolveTransferOffer(offer.id, false);
                showContinueAbortPrompt("OFFER REJECTED", "CONTINUE", nullptr,
                    { "COUNTER-OFFER REJECTED." }, false);
            }
        }
    }
    return true;
}

void HotseatCareerManager::onAfterCreateReplacementPlayer()
{
    save68kRegisters();
    const bool isLoan = (m_emergencyLoanDepth > 0);
    int ord = static_cast<int>(static_cast<uint32_t>(A3) - static_cast<uint32_t>(CareerOffsets::kCareerTeam)) /
              static_cast<int>(kPlayerFileSize);
    if (ord < 0 || ord >= 30) {
        int alt = static_cast<int>(static_cast<int16_t>(static_cast<uint16_t>(D6)));
        if (alt >= 0 && alt < 30)
            ord = alt;
    }
    if (ord >= 0 && ord < 30) {
        auto *pf = reinterpret_cast<PlayerFile *>(
            &SwosVM::g_memByte[CareerOffsets::kCareerTeam + kTeamFileHeaderSize + ord * kPlayerFileSize]);
        pf->playerName[0] = '\0';
        populateReplacementPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam],
                                      &SwosVM::g_memByte[CareerOffsets::kDsegD6C26],
                                      ord, getVmSelTeamNumber(), true, isLoan);
    }
    restore68kRegisters();
}

void HotseatCareerManager::onBeforeEmergencyLoan()
{
    ++m_emergencyLoanDepth;
}

void HotseatCareerManager::onAfterEmergencyLoan()
{
    if (m_emergencyLoanDepth > 0)
        --m_emergencyLoanDepth;
}

void HotseatCareerManager::onAfterEnsureCareerPlayers()
{
    if (swos.g_gameType != kGameTypeCareer || isVmNationalSelector())
        return;

    uint32_t teamOfs = A6.asDword();
    if (teamOfs != CareerOffsets::kCareerTeam)
        return;

    uint16_t excludeFlags = D4.asWord();
    uint16_t maxSubs = D5.asWord();
    int reqBase = swos.g_trainingGame ? 22 : 11;
    int reqTotal = reqBase + static_cast<int>(maxSubs);
    int reqOutfieldMin = std::max(0, reqTotal - 2);
    int reqGkMin = (maxSubs > 5) ? 2 : 1;

    extern void cseg_3A110();
    extern void cseg_3A2CE();
    extern void cseg_52956();

    save68kRegisters();
    bool addedAny = false;
    for (int iter = 0; iter < 30; ++iter) {
        uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        if (numPlayers >= 30)
            break;

        A0 = CareerOffsets::kCareerTeam;
        D0 = excludeFlags;
        D1 = 0;
        cseg_3A110();
        int fitGk = static_cast<int>(static_cast<int16_t>(D7.asWord()));

        A0 = CareerOffsets::kCareerTeam;
        D0 = excludeFlags;
        cseg_3A2CE();
        int fitOut = static_cast<int>(static_cast<int16_t>(D7.asWord()));

        bool needGk = (fitGk < reqGkMin) || (fitGk < 2 && fitOut >= reqOutfieldMin && (fitGk + fitOut) < reqTotal);
        bool needOut = !needGk && (fitOut < reqOutfieldMin || (fitGk + fitOut) < reqTotal);
        if (!needGk && !needOut)
            break;

        if (needGk) {
            D0 = 0;
        } else {
            SWOS::Rand2();
            D0 = 1 + (((D0.asWord() & 0xFFFFu) * 7u) >> 8);
        }
        cseg_52956();
        addedAny = true;
    }
    if (addedAny) {
        ensureVmTrainingMatchSavedPlayersSynced();
    }
    restore68kRegisters();
}

void HotseatCareerManager::onBeforeAddDomesticMarket()
{
    m_prevHomeMarketCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]);
}

void HotseatCareerManager::onAfterAddDomesticMarket()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    uint16_t curCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]);
    if (curCount > m_prevHomeMarketCount && curCount <= kMaxHomeMarketEntries) {
        auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[CareerOffsets::kHomeMarketPlayers]);
        const auto& last = entries[curCount - 1];
        if (findHotseatPlayerByTeamId(static_cast<int16_t>(last.teamId)) >= 0) {
            std::memset(&entries[curCount - 1], 0, sizeof(MarketEntry));
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]) = curCount - 1;
            tryAddAiDomesticMarketReplacement(*this);
        }
    }
}

void HotseatCareerManager::onBeforeAddForeignMarket()
{
    m_prevForeignMarketCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]);
}

void HotseatCareerManager::onAfterAddForeignMarket()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    uint16_t curCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]);
    if (curCount > m_prevForeignMarketCount && curCount <= kMaxForeignMarketEntries) {
        auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[CareerOffsets::kForeignMarketPlayers]);
        const auto& last = entries[curCount - 1];
        if (findHotseatPlayerByTeamId(static_cast<int16_t>(last.teamId)) >= 0) {
            std::memset(&entries[curCount - 1], 0, sizeof(MarketEntry));
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]) = curCount - 1;
            tryAddAiForeignMarketReplacement(*this);
        }
    }
}

void HotseatCareerManager::syncTransferMarketPoolsForCurrentPlayer()
{
    static bool s_syncingMarkets = false;
    if (s_syncingMarkets || !isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;
    if (isVmNationalSelector())
        return;
    if (m_activePlayerIndex < 0 || m_activePlayerIndex >= m_numPlayers)
        return;

    struct Guard {
        bool& f;
        explicit Guard(bool& r) : f(r) { f = true; }
        ~Guard() { f = false; }
    } guard(s_syncingMarkets);

    if (getVmSelTeamNumber() == static_cast<uint16_t>(m_players[m_activePlayerIndex].teamId)) {
        sanitizeActivePlayerTransferList();
        auto& active = m_players[m_activePlayerIndex];
        active.careerNumPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        std::memcpy(active.currentMatchPlayers, &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers], kCurrentMatchPlayersSize);
        std::memcpy(active.trainingMatchSavedPlayers, &SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers], kCurrentMatchPlayersSize);
        std::memcpy(active.dsegD8B8C, &SwosVM::g_memByte[CareerOffsets::kDsegD8B8C], kCurrentMatchPlayersSize);
        std::memcpy(active.careerTeam, &SwosVM::g_memByte[CareerOffsets::kCareerTeam], kCareerTeamBufferSize);
        syncTrainingMatchSavedPlayers(active.trainingMatchSavedPlayers, active.dsegD8B8C, active.careerTeam,
                                      active.careerNumPlayers, active.currentMatchPlayers);
        std::memcpy(active.playerTrialWeeks, &SwosVM::g_memByte[CareerOffsets::kDsegD6C26], sizeof(active.playerTrialWeeks));
        active.numPlayersOnTransferMarket =
            *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]);
        std::memcpy(active.playersOnTransferMarket,
                    &SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket], kListedPlayersSize);
    }

    uint8_t activeCountry = static_cast<uint8_t>(m_players[m_activePlayerIndex].teamId & 0xFF);

    auto syncSinglePool = [&](size_t countOfs, size_t entriesOfs, int maxEntries, bool isDomesticPool) {
        uint16_t rawCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[countOfs]);
        int n = std::min<int>(rawCount, maxEntries);
        auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[entriesOfs]);

        std::vector<MarketEntry> kept;
        kept.reserve(maxEntries);

        for (int i = 0; i < n; ++i) {
            MarketEntry e = entries[i];
            if (e.teamId == 0)
                continue;

            int ownerIdx = findHotseatPlayerByTeamId(static_cast<int16_t>(e.teamId));
            if (ownerIdx >= 0) {
                bool sameCountry = (static_cast<uint8_t>(m_players[ownerIdx].teamId & 0xFF) == activeCountry);
                if (ownerIdx == m_activePlayerIndex || sameCountry != isDomesticPool)
                    continue;
                if (!isOrdinalInHotseatTransferList(m_players[ownerIdx], e.playerOrdinal))
                    continue;
            } else {
                bool sameCountry = (static_cast<uint8_t>(e.teamId & 0xFF) == activeCountry);
                if (sameCountry != isDomesticPool)
                    continue;
            }

            if (!marketPoolContains(kept.data(), static_cast<uint16_t>(kept.size()), e.teamId, e.playerOrdinal)) {
                e.padding = 0;
                kept.push_back(e);
            }
        }

        for (int ownerIdx = 0; ownerIdx < m_numPlayers; ++ownerIdx) {
            if (ownerIdx == m_activePlayerIndex)
                continue;
            const auto& owner = m_players[ownerIdx];
            bool sameCountry = (static_cast<uint8_t>(owner.teamId & 0xFF) == activeCountry);
            if (sameCountry != isDomesticPool)
                continue;

            int listedCount = std::min<int>(owner.numPlayersOnTransferMarket, 5);
            const auto *listed = reinterpret_cast<const uint16_t *>(owner.playersOnTransferMarket);
            for (int k = 0; k < listedCount; ++k) {
                uint16_t ord = listed[k];
                if (ord >= 30 || !isHotseatListedPlayerValid(owner, static_cast<uint8_t>(ord)))
                    continue;
                uint16_t tid = static_cast<uint16_t>(owner.teamId);
                uint8_t ordByte = static_cast<uint8_t>(ord);
                if (marketPoolContains(kept.data(), static_cast<uint16_t>(kept.size()), tid, ordByte))
                    continue;

                MarketEntry addEntry{ tid, ordByte, 0 };
                if (static_cast<int>(kept.size()) < maxEntries) {
                    kept.push_back(addEntry);
                } else {
                    for (int j = static_cast<int>(kept.size()) - 1; j >= 0; --j) {
                        if (findHotseatPlayerByTeamId(static_cast<int16_t>(kept[j].teamId)) < 0) {
                            kept[j] = addEntry;
                            break;
                        }
                    }
                }
            }
        }

        std::memset(entries, 0, static_cast<size_t>(maxEntries) * sizeof(MarketEntry));
        if (!kept.empty()) {
            std::memcpy(entries, kept.data(), kept.size() * sizeof(MarketEntry));
        }
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[countOfs]) = static_cast<uint16_t>(kept.size());
    };

    syncSinglePool(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers,
                   kMaxHomeMarketEntries, true);
    syncSinglePool(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers,
                   kMaxForeignMarketEntries, false);
}

void HotseatCareerManager::resolveMarketTeamPtrForEntry(uint32_t entryOffset)
{
    if (!isActive() || entryOffset + sizeof(MarketEntry) > sizeof(SwosVM::g_memByte))
        return;

    uint16_t teamId = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[entryOffset]);
    int idx = findHotseatPlayerByTeamId(static_cast<int16_t>(teamId));
    if (idx >= 0) {
        uint16_t ord = static_cast<uint16_t>(static_cast<uint32_t>(D1) & 0xFFFFu);
        if (idx < static_cast<int>(m_h2hPackMaps.size()) &&
            m_h2hPackMaps[idx].active && m_h2hPackMaps[idx].vmTeamOfs != 0 && ord < kNumPlayersInTeam)
            return;
        A0 = ensureHotseatTeamMirror(idx);
    }
}

void HotseatCareerManager::lookupForeignPoolPlayer()
{
    uint16_t teamId = static_cast<uint16_t>(static_cast<uint32_t>(D0) & 0xFFFFu);
    uint8_t ord = static_cast<uint8_t>(static_cast<uint32_t>(D1) & 0xFFu);
    uint32_t scratchOfs = ensurePoolPlayerScratch();

    if (isActive()) {
        int idx = findHotseatPlayerByTeamId(static_cast<int16_t>(teamId));
        if (idx >= 0) {
            uint32_t mirrorOfs = ensureHotseatTeamMirror(idx);
            const auto *tf = reinterpret_cast<const TeamFile *>(&SwosVM::g_memByte[mirrorOfs]);
            auto *out = reinterpret_cast<PoolPlayerEntry *>(&SwosVM::g_memByte[scratchOfs]);
            std::memset(out, 0, sizeof(PoolPlayerEntry));
            out->teamId = teamId;
            out->playerOrdinal = ord;
            std::memcpy(out->clubName, tf->teamName, sizeof(out->clubName));
            if (ord < 30) {
                std::memcpy(&out->player,
                            &SwosVM::g_memByte[mirrorOfs + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize],
                            kPlayerFileSize);
            }
            A0 = scratchOfs;
            return;
        }
    }

    const auto *pool = reinterpret_cast<const PoolPlayerEntry *>(&SwosVM::g_memByte[CareerOffsets::kPoolplyrDatBuffer]);
    for (int i = 0; i < kPoolPlyrCount; ++i) {
        if (pool[i].teamId == teamId && pool[i].playerOrdinal == ord) {
            A0 = static_cast<uint32_t>(CareerOffsets::kPoolplyrDatBuffer + static_cast<size_t>(i) * sizeof(PoolPlayerEntry));
            return;
        }
    }

    auto *out = reinterpret_cast<PoolPlayerEntry *>(&SwosVM::g_memByte[scratchOfs]);
    std::memset(out, 0, sizeof(PoolPlayerEntry));
    out->teamId = teamId;
    out->playerOrdinal = ord;
    A0 = scratchOfs;
}

void HotseatCareerManager::resolveGetPlayerTeamPtr()
{
    if (!isActive())
        return;

    uint16_t teamId = static_cast<uint16_t>(static_cast<uint32_t>(D0) & 0xFFFFu);
    if (teamId == static_cast<uint16_t>(swos.currentTeamNumber) || teamId == getVmSelTeamNumber())
        return;

    int idx = findHotseatPlayerByTeamId(static_cast<int16_t>(teamId));
    if (idx >= 0) {
        uint16_t ord = static_cast<uint16_t>(static_cast<uint32_t>(D1) & 0xFFFFu);
        if (idx < static_cast<int>(m_h2hPackMaps.size()) &&
            m_h2hPackMaps[idx].active && m_h2hPackMaps[idx].vmTeamOfs != 0 && ord < kNumPlayersInTeam)
            return;
        A0 = ensureHotseatTeamMirror(idx);
    }
}
