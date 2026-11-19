#include "hotseatInternal.h"

using namespace HotseatInternal;

namespace {

#pragma pack(push, 1)
struct HotseatFileHeader {
    char magic[16];
    int32_t numPlayers;
    int32_t activePlayerIndex;
    int32_t nextOfferId;
    int32_t numOffers;
    int32_t roundCalendarIndex;
};
#pragma pack(pop)

bool writePlayerContext(std::ofstream& out, const HotseatPlayerContext& p)
{
    bool ok = writeBinary(out, p.teamId) &&
              writeString(out, p.teamName, 127) &&
              writeString(out, p.managerName, 63) &&
              writeString(out, p.managerSurname, 63) &&
              writeBinary(out, p.coachOrPlayer) &&
              writeBinary(out, p.careerTeamLeague) &&
              writeBinary(out, p.nationality) &&
              writeBinary(out, p.mrOrMs) &&
              writeBinary(out, p.newBalance) &&
              writeBinary(out, p.oldBalance) &&
              writeBinary(out, p.startingBalance) &&
              writeBinary(out, p.gateReceipts) &&
              writeBinary(out, p.playerWagesBill) &&
              writeBinary(out, p.generalRunningCosts) &&
              writeBinary(out, p.playerSales) &&
              writeBinary(out, p.playerPurchases) &&
              writeBinary(out, p.competitionBonuses) &&
              writeBinary(out, p.sensisoftPlcSponsorship) &&
              writeBinary(out, p.stayingInOldClub) &&
              writeBinary(out, p.dsegD6B54) &&
              writeBinary(out, p.dsegD6B56) &&
              writeBinary(out, p.dsegD6B5A) &&
              writeBinary(out, p.dsegD6B5C) &&
              writeBinary(out, p.dsegD6B88) &&
              writeBinary(out, p.dsegD6B9A) &&
              writeBinary(out, p.dsegD6B9E) &&
              writeBinary(out, p.youreSacked) &&
              writeBinary(out, p.retired) &&
              writeBinary(out, p.clubBusinessActive) &&
              writeBinary(out, p.numNewJobOffers) &&
              writeBinary(out, p.numJobOffers) &&
              writeBinary(out, p.isNationalSelector) &&
              writeBinary(out, p.numOffers) &&
              writeBinary(out, p.timeToNegotiate) &&
              writeBinary(out, p.dsegD6D0A) &&
              writeBinary(out, p.dsegD6D0E) &&
              writeBinary(out, p.dsegD6D10) &&
              writeBinary(out, p.dsegD6D12) &&
              writeBinary(out, p.gamesPlayed) &&
              writeBinary(out, p.numWins) &&
              writeBinary(out, p.numDrawn) &&
              writeBinary(out, p.numLost) &&
              writeBinary(out, p.goalsScored) &&
              writeBinary(out, p.goalsTaken) &&
              writeBinary(out, p.dsegD6CA0) &&
              writeBinary(out, p.dsegD6CA2) &&
              writeBinary(out, p.dsegD7E66) &&
              writeBinary(out, p.currentSeasonShowing) &&
              writeBinary(out, p.currentSeasonNumber) &&
              writeBinary(out, p.seasonPlaying) &&
              writeBinary(out, p.dsegE0048) &&
              writeBinary(out, p.playMatchBlink) &&
              writeBinary(out, p.careerNumPlayers);
    if (!ok)
        return false;

    out.write(reinterpret_cast<const char *>(p.playerWages), sizeof(p.playerWages));
    out.write(reinterpret_cast<const char *>(p.playerTrialWeeks), sizeof(p.playerTrialWeeks));
    out.write(reinterpret_cast<const char *>(p.seasonStatsBlock), sizeof(p.seasonStatsBlock));
    out.write(reinterpret_cast<const char *>(p.seasonFinancesBlock), kSeasonFinancesBlockSize);
    out.write(reinterpret_cast<const char *>(p.currentMatchPlayers), kCurrentMatchPlayersSize);
    out.write(reinterpret_cast<const char *>(p.careerTeam), kCareerTeamBufferSize);
    out.write(reinterpret_cast<const char *>(p.careerHistory), kCareerHistoryBufferSize);
    out.write(reinterpret_cast<const char *>(p.userTactics), 6 * kUserTacticsSize);

    uint8_t matchPlayed = p.matchPlayedThisTurn ? 1 : 0;
    uint8_t turnComp = p.turnCompleted ? 1 : 0;
    if (!writeBinary(out, matchPlayed) || !writeBinary(out, turnComp))
        return false;

    ok = writeBinary(out, p.dsegD6B62) &&
         writeBinary(out, p.dsegD6B64) &&
         writeBinary(out, p.dsegD6B94) &&
         writeBinary(out, p.dsegD6BA2) &&
         writeBinary(out, p.dsegD6C46) &&
         writeBinary(out, p.additionalInvestments) &&
         writeBinary(out, p.dsegD6C4E) &&
         writeBinary(out, p.dsegD6C50) &&
         writeBinary(out, p.teamNoOfferAccepted) &&
         writeBinary(out, p.teamFundsAccepted) &&
         writeBinary(out, p.dsegD6CDC) &&
         writeBinary(out, p.dsegD6D0C) &&
         writeBinary(out, p.numPlayersOnTransferMarket) &&
         writeBinary(out, p.euroCupIndex);
    if (!ok)
        return false;

    out.write(reinterpret_cast<const char *>(p.playersOnTransferMarket), kListedPlayersSize);
    out.write(reinterpret_cast<const char *>(p.dsegD6F26Block), kListedPlayersSize);
    out.write(reinterpret_cast<const char *>(p.dsegD6DBA), kTransferAiOffersSize);
    out.write(reinterpret_cast<const char *>(p.newJobOffers), kJobOffersBufferSize);
    out.write(reinterpret_cast<const char *>(p.jobOffers), kJobOffersBufferSize);
    return out.good();
}

bool readPlayerContext(std::ifstream& in, HotseatPlayerContext& p, int fileVersion)
{
    bool ok = readBinary(in, p.teamId) &&
              readString(in, p.teamName, 127) &&
              readString(in, p.managerName, 63) &&
              readString(in, p.managerSurname, 63) &&
              readBinary(in, p.coachOrPlayer) &&
              readBinary(in, p.careerTeamLeague) &&
              readBinary(in, p.nationality) &&
              readBinary(in, p.mrOrMs) &&
              readBinary(in, p.newBalance) &&
              readBinary(in, p.oldBalance) &&
              readBinary(in, p.startingBalance) &&
              readBinary(in, p.gateReceipts) &&
              readBinary(in, p.playerWagesBill) &&
              readBinary(in, p.generalRunningCosts) &&
              readBinary(in, p.playerSales) &&
              readBinary(in, p.playerPurchases) &&
              readBinary(in, p.competitionBonuses) &&
              readBinary(in, p.sensisoftPlcSponsorship) &&
              readBinary(in, p.stayingInOldClub) &&
              readBinary(in, p.dsegD6B54) &&
              readBinary(in, p.dsegD6B56) &&
              readBinary(in, p.dsegD6B5A) &&
              readBinary(in, p.dsegD6B5C) &&
              readBinary(in, p.dsegD6B88) &&
              readBinary(in, p.dsegD6B9A) &&
              readBinary(in, p.dsegD6B9E) &&
              readBinary(in, p.youreSacked) &&
              readBinary(in, p.retired) &&
              readBinary(in, p.clubBusinessActive) &&
              readBinary(in, p.numNewJobOffers) &&
              readBinary(in, p.numJobOffers) &&
              readBinary(in, p.isNationalSelector) &&
              readBinary(in, p.numOffers) &&
              readBinary(in, p.timeToNegotiate) &&
              readBinary(in, p.dsegD6D0A) &&
              readBinary(in, p.dsegD6D0E) &&
              readBinary(in, p.dsegD6D10) &&
              readBinary(in, p.dsegD6D12) &&
              readBinary(in, p.gamesPlayed) &&
              readBinary(in, p.numWins) &&
              readBinary(in, p.numDrawn) &&
              readBinary(in, p.numLost) &&
              readBinary(in, p.goalsScored) &&
              readBinary(in, p.goalsTaken) &&
              readBinary(in, p.dsegD6CA0) &&
              readBinary(in, p.dsegD6CA2) &&
              readBinary(in, p.dsegD7E66) &&
              readBinary(in, p.currentSeasonShowing) &&
              readBinary(in, p.currentSeasonNumber) &&
              readBinary(in, p.seasonPlaying) &&
              readBinary(in, p.dsegE0048) &&
              readBinary(in, p.playMatchBlink) &&
              readBinary(in, p.careerNumPlayers);
    if (!ok)
        return false;

    in.read(reinterpret_cast<char *>(p.playerWages), sizeof(p.playerWages));
    if (fileVersion >= 5) {
        in.read(reinterpret_cast<char *>(p.playerTrialWeeks), sizeof(p.playerTrialWeeks));
    } else {
        std::memset(p.playerTrialWeeks, 0, sizeof(p.playerTrialWeeks));
    }
    in.read(reinterpret_cast<char *>(p.seasonStatsBlock), sizeof(p.seasonStatsBlock));
    in.read(reinterpret_cast<char *>(p.seasonFinancesBlock), kSeasonFinancesBlockSize);
    in.read(reinterpret_cast<char *>(p.currentMatchPlayers), kCurrentMatchPlayersSize);
    in.read(reinterpret_cast<char *>(p.careerTeam), kCareerTeamBufferSize);
    in.read(reinterpret_cast<char *>(p.careerHistory), kCareerHistoryBufferSize);
    in.read(reinterpret_cast<char *>(p.userTactics), 6 * kUserTacticsSize);

    uint8_t matchPlayed = 0, turnComp = 0;
    if (!readBinary(in, matchPlayed) || !readBinary(in, turnComp) || !in.good())
        return false;
    p.matchPlayedThisTurn = (matchPlayed != 0);
    p.turnCompleted = (turnComp != 0);

    if (fileVersion >= 4) {
        ok = readBinary(in, p.dsegD6B62) &&
             readBinary(in, p.dsegD6B64) &&
             readBinary(in, p.dsegD6B94) &&
             readBinary(in, p.dsegD6BA2) &&
             readBinary(in, p.dsegD6C46) &&
             readBinary(in, p.additionalInvestments) &&
             readBinary(in, p.dsegD6C4E) &&
             readBinary(in, p.dsegD6C50) &&
             readBinary(in, p.teamNoOfferAccepted) &&
             readBinary(in, p.teamFundsAccepted) &&
             readBinary(in, p.dsegD6CDC) &&
             readBinary(in, p.dsegD6D0C) &&
             readBinary(in, p.numPlayersOnTransferMarket) &&
             readBinary(in, p.euroCupIndex);
        if (!ok)
            return false;

        in.read(reinterpret_cast<char *>(p.playersOnTransferMarket), kListedPlayersSize);
        in.read(reinterpret_cast<char *>(p.dsegD6F26Block), kListedPlayersSize);
        in.read(reinterpret_cast<char *>(p.dsegD6DBA), kTransferAiOffersSize);
        in.read(reinterpret_cast<char *>(p.newJobOffers), kJobOffersBufferSize);
        in.read(reinterpret_cast<char *>(p.jobOffers), kJobOffersBufferSize);
        if (!in.good())
            return false;
    }

    syncTrainingMatchSavedPlayers(p.trainingMatchSavedPlayers, p.dsegD8B8C, p.careerTeam,
                                  p.careerNumPlayers, p.currentMatchPlayers);
    return true;
}

bool writePlayerV6Extension(std::ofstream& out, const HotseatPlayerContext& p)
{
    bool ok = writeBinary(out, p.dsegD761A) &&
              writeBinary(out, p.dsegD761C) &&
              writeBinary(out, p.dsegD8B6A) &&
              writeBinary(out, p.dsegD8BAC) &&
              writeBinary(out, p.dsegD8BAE) &&
              writeBinary(out, p.dsegD8BB0) &&
              writeBinary(out, p.currentTactics) &&
              writeBinary(out, p.dsegD8BF3);
    if (!ok)
        return false;

    out.write(reinterpret_cast<const char *>(p.trainingMatchSavedPlayers), kCurrentMatchPlayersSize);
    out.write(reinterpret_cast<const char *>(p.dsegD8B8C), kCurrentMatchPlayersSize);
    out.write(reinterpret_cast<const char *>(p.lastMatchPlayers), kCurrentMatchPlayersSize);
    out.write(reinterpret_cast<const char *>(p.dsegD8BD2), kCurrentMatchPlayersSize);
    return out.good();
}

bool readPlayerV6Extension(std::ifstream& in, HotseatPlayerContext& p)
{
    bool ok = readBinary(in, p.dsegD761A) &&
              readBinary(in, p.dsegD761C) &&
              readBinary(in, p.dsegD8B6A) &&
              readBinary(in, p.dsegD8BAC) &&
              readBinary(in, p.dsegD8BAE) &&
              readBinary(in, p.dsegD8BB0) &&
              readBinary(in, p.currentTactics) &&
              readBinary(in, p.dsegD8BF3);
    if (!ok)
        return false;

    in.read(reinterpret_cast<char *>(p.trainingMatchSavedPlayers), kCurrentMatchPlayersSize);
    in.read(reinterpret_cast<char *>(p.dsegD8B8C), kCurrentMatchPlayersSize);
    in.read(reinterpret_cast<char *>(p.lastMatchPlayers), kCurrentMatchPlayersSize);
    in.read(reinterpret_cast<char *>(p.dsegD8BD2), kCurrentMatchPlayersSize);
    if (!in.good())
        return false;

    syncTrainingMatchSavedPlayers(p.trainingMatchSavedPlayers, p.dsegD8B8C, p.careerTeam,
                                  p.careerNumPlayers, p.currentMatchPlayers);
    return true;
}

bool writeTransferOffer(std::ofstream& out, const HotseatCareerManager::TransferOffer& offer)
{
    uint8_t flags = (offer.pending ? 1 : 0) |
                    (offer.accepted ? 2 : 0) |
                    (offer.rejected ? 4 : 0) |
                    (offer.isCounterOffer ? 8 : 0);
    return writeBinary(out, offer.id) &&
           writeBinary(out, offer.fromPlayerIndex) &&
           writeBinary(out, offer.toPlayerIndex) &&
           writeBinary(out, offer.playerOrdinal) &&
           static_cast<bool>(out.write(offer.playerName, sizeof(offer.playerName))) &&
           writeBinary(out, offer.offerAmount) &&
           writeBinary(out, flags) &&
           out.good();
}

bool readTransferOffer(std::ifstream& in, HotseatCareerManager::TransferOffer& offer)
{
    uint8_t flags = 0;
    bool ok = readBinary(in, offer.id) &&
              readBinary(in, offer.fromPlayerIndex) &&
              readBinary(in, offer.toPlayerIndex) &&
              readBinary(in, offer.playerOrdinal) &&
              static_cast<bool>(in.read(offer.playerName, sizeof(offer.playerName))) &&
              readBinary(in, offer.offerAmount) &&
              readBinary(in, flags) &&
              in.good();
    if (!ok)
        return false;
    offer.playerName[sizeof(offer.playerName) - 1] = '\0';
    offer.extraWantedOrdinal = -1;
    std::memset(offer.extraWantedName, 0, sizeof(offer.extraWantedName));
    offer.exchangeOrdinal1 = -1;
    std::memset(offer.exchangeName1, 0, sizeof(offer.exchangeName1));
    offer.exchangeOrdinal2 = -1;
    std::memset(offer.exchangeName2, 0, sizeof(offer.exchangeName2));
    offer.pending = (flags & 1) != 0;
    offer.accepted = (flags & 2) != 0;
    offer.rejected = (flags & 4) != 0;
    offer.isCounterOffer = (flags & 8) != 0;
    return true;
}

bool writeTransferOfferV6Extension(std::ofstream& out, const HotseatCareerManager::TransferOffer& offer)
{
    return writeBinary(out, offer.extraWantedOrdinal) &&
           static_cast<bool>(out.write(offer.extraWantedName, sizeof(offer.extraWantedName))) &&
           writeBinary(out, offer.exchangeOrdinal1) &&
           static_cast<bool>(out.write(offer.exchangeName1, sizeof(offer.exchangeName1))) &&
           writeBinary(out, offer.exchangeOrdinal2) &&
           static_cast<bool>(out.write(offer.exchangeName2, sizeof(offer.exchangeName2))) &&
           out.good();
}

bool readTransferOfferV6Extension(std::ifstream& in, HotseatCareerManager::TransferOffer& offer)
{
    bool ok = readBinary(in, offer.extraWantedOrdinal) &&
              static_cast<bool>(in.read(offer.extraWantedName, sizeof(offer.extraWantedName))) &&
              readBinary(in, offer.exchangeOrdinal1) &&
              static_cast<bool>(in.read(offer.exchangeName1, sizeof(offer.exchangeName1))) &&
              readBinary(in, offer.exchangeOrdinal2) &&
              static_cast<bool>(in.read(offer.exchangeName2, sizeof(offer.exchangeName2))) &&
              in.good();
    if (!ok)
        return false;
    offer.extraWantedName[sizeof(offer.extraWantedName) - 1] = '\0';
    offer.exchangeName1[sizeof(offer.exchangeName1) - 1] = '\0';
    offer.exchangeName2[sizeof(offer.exchangeName2) - 1] = '\0';
    return true;
}

} // namespace

bool HotseatCareerManager::saveToFile(const std::string& path)
{
    if (!m_active || m_numPlayers < 1)
        return false;

    saveCurrentEuroCup();
    saveCurrentSwosContext(m_players[m_activePlayerIndex]);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    HotseatFileHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::memcpy(hdr.magic, kHotseatFileMagicV8, 15);
    hdr.numPlayers = m_numPlayers;
    hdr.activePlayerIndex = m_activePlayerIndex;
    hdr.nextOfferId = m_nextOfferId;
    hdr.numOffers = static_cast<int32_t>(std::min<size_t>(m_transferOffers.size(), kMaxSerializedOffers));
    hdr.roundCalendarIndex = m_roundCalendarIndex;

    if (!writeBinary(out, hdr))
        return false;

    for (int i = 0; i < m_numPlayers; ++i) {
        if (!writePlayerContext(out, m_players[i]))
            return false;
    }

    for (int32_t i = 0; i < hdr.numOffers; ++i) {
        if (!writeTransferOffer(out, m_transferOffers[i]))
            return false;
    }

    int32_t numCalEntries = static_cast<int32_t>(m_calendarSlots.size());
    if (!writeBinary(out, numCalEntries))
        return false;
    for (const auto& kv : m_calendarSlots) {
        uint16_t calOfs = kv.first;
        uint16_t count = static_cast<uint16_t>(kv.second.size());
        if (!writeBinary(out, calOfs) || !writeBinary(out, count))
            return false;
        for (uint16_t s = 0; s < count; ++s) {
            out.write(reinterpret_cast<const char *>(kv.second[s].data()), 12);
            if (!out.good())
                return false;
        }
    }

    if (!writeBinary(out, m_activeEuroCupIndex))
        return false;
    out.write(reinterpret_cast<const char *>(m_euroCupScheduleTemplate), sizeof(m_euroCupScheduleTemplate));
    if (!out.good())
        return false;
    for (int e = 0; e < kNumEuroCups; ++e) {
        uint8_t active = m_euroCupActive[e] ? 1 : 0;
        if (!writeBinary(out, active) || !writeBinary(out, m_euroCupSeasonStrOfs[e]))
            return false;
        if (m_euroCupActive[e]) {
            out.write(reinterpret_cast<const char *>(m_euroCupBuffers[e].data()), kEuroCupBufferSize);
            if (!out.good())
                return false;
        }
    }
    int32_t numEuroMapEntries = static_cast<int32_t>(m_euroCupByCalOfs.size());
    if (!writeBinary(out, numEuroMapEntries))
        return false;
    for (const auto& kv : m_euroCupByCalOfs) {
        uint16_t calOfs = kv.first;
        int32_t eIdx = kv.second;
        if (!writeBinary(out, calOfs) || !writeBinary(out, eIdx))
            return false;
    }

    for (int i = 0; i < m_numPlayers; ++i) {
        if (!writePlayerV6Extension(out, m_players[i]))
            return false;
    }
    for (int32_t i = 0; i < hdr.numOffers; ++i) {
        if (!writeTransferOfferV6Extension(out, m_transferOffers[i]))
            return false;
    }

    uint8_t endSeasonActiveByte = m_endSeasonActive ? 1 : 0;
    int32_t endSeasonIdx = m_endSeasonPlayerIndex;
    if (!writeBinary(out, endSeasonActiveByte) || !writeBinary(out, endSeasonIdx))
        return false;
    for (int i = 0; i < m_numPlayers; ++i) {
        uint8_t elim = (i < static_cast<int>(m_endSeasonEliminated.size()) && m_endSeasonEliminated[i] != 0) ? 1 : 0;
        if (!writeBinary(out, elim))
            return false;
    }
    int32_t numClaimed = static_cast<int32_t>(m_claimedJobOfferTeams.size());
    if (!writeBinary(out, numClaimed))
        return false;
    for (int32_t i = 0; i < numClaimed; ++i) {
        if (!writeBinary(out, m_claimedJobOfferTeams[i]))
            return false;
    }

    out.flush();
    if (!out.good())
        return false;

    logInfo("HotseatCareerManager: Saved hotseat state (%d players, %d offers) to %s",
            m_numPlayers, hdr.numOffers, path.c_str());
    return true;
}

void HotseatCareerManager::onAfterProcessCareerFile()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    syncPlayersWithSelectedTeams(true);
    ensureHotseatTeamControlsInSelectedTeams();
    relinkCompetitionTeamPointers();
    if (m_leagueWorlds.empty() && m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) {
        captureActiveWorldAsInitial(worldKeyForPlayer(m_players[m_activePlayerIndex]));
    }
}

bool HotseatCareerManager::loadFromFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        logWarn("HotseatCareerManager: Could not open %s", path.c_str());
        return false;
    }

    HotseatFileHeader hdr;
    if (!readBinary(in, hdr)) {
        reset();
        return false;
    }

    int fileVersion = 0;
    if (std::memcmp(hdr.magic, kHotseatFileMagicV8, 15) == 0) {
        fileVersion = 8;
    } else if (std::memcmp(hdr.magic, kHotseatFileMagicV7, 15) == 0) {
        fileVersion = 7;
    } else if (std::memcmp(hdr.magic, kHotseatFileMagicV6, 15) == 0) {
        fileVersion = 6;
    } else if (std::memcmp(hdr.magic, kHotseatFileMagicV5, 15) == 0) {
        fileVersion = 5;
    } else if (std::memcmp(hdr.magic, kHotseatFileMagicV4, 15) == 0) {
        fileVersion = 4;
    } else if (std::memcmp(hdr.magic, kHotseatFileMagicV3, 15) == 0) {
        fileVersion = 3;
    } else {
        reset();
        logWarn("HotseatCareerManager: Invalid or unsupported magic in %s", path.c_str());
        return false;
    }

    if (hdr.numPlayers < 1 || hdr.numPlayers > kMaxHotseatPlayers ||
        hdr.activePlayerIndex < 0 || hdr.activePlayerIndex >= hdr.numPlayers ||
        hdr.numOffers < 0 || hdr.numOffers > kMaxSerializedOffers ||
        hdr.roundCalendarIndex < 0) {
        reset();
        logWarn("HotseatCareerManager: Invalid header values in %s", path.c_str());
        return false;
    }

    reset();
    m_numPlayers = hdr.numPlayers;
    ensurePlayerSlots(m_numPlayers);
    m_activePlayerIndex = hdr.activePlayerIndex;
    m_nextOfferId = hdr.nextOfferId;
    m_roundCalendarIndex = hdr.roundCalendarIndex;
    m_active = true;

    for (int i = 0; i < m_numPlayers; ++i) {
        if (!readPlayerContext(in, m_players[i], fileVersion)) {
            reset();
            logWarn("HotseatCareerManager: Corrupted player context %d in %s", i, path.c_str());
            return false;
        }
    }

    m_transferOffers.clear();
    m_transferOffers.reserve(static_cast<size_t>(hdr.numOffers));
    for (int32_t i = 0; i < hdr.numOffers; ++i) {
        TransferOffer offer;
        if (!readTransferOffer(in, offer)) {
            reset();
            logWarn("HotseatCareerManager: Corrupted transfer offer %d in %s", i, path.c_str());
            return false;
        }
        m_transferOffers.push_back(offer);
    }

    int32_t numCalEntries = 0;
    if (!readBinary(in, numCalEntries) || numCalEntries < 0 || numCalEntries > 256) {
        reset();
        logWarn("HotseatCareerManager: Invalid calendar slots count in %s", path.c_str());
        return false;
    }
    m_calendarSlots.clear();
    for (int32_t c = 0; c < numCalEntries; ++c) {
        uint16_t calOfs = 0, count = 0;
        if (!readBinary(in, calOfs) || !readBinary(in, count) || calOfs >= 1870 || count > 32) {
            reset();
            logWarn("HotseatCareerManager: Corrupted calendar slot entry %d in %s", c, path.c_str());
            return false;
        }
        std::vector<CalendarSlotBytes> vec(count);
        for (uint16_t s = 0; s < count; ++s) {
            if (!in.read(reinterpret_cast<char *>(vec[s].data()), 12)) {
                reset();
                logWarn("HotseatCareerManager: Truncated calendar slot data in %s", path.c_str());
                return false;
            }
        }
        m_calendarSlots[calOfs] = std::move(vec);
    }

    if (fileVersion >= 4) {
        if (!readBinary(in, m_activeEuroCupIndex) ||
            !in.read(reinterpret_cast<char *>(m_euroCupScheduleTemplate), sizeof(m_euroCupScheduleTemplate))) {
            reset();
            logWarn("HotseatCareerManager: Corrupted euro cup header in %s", path.c_str());
            return false;
        }
        for (int e = 0; e < kNumEuroCups; ++e) {
            uint8_t active = 0;
            if (!readBinary(in, active) || !readBinary(in, m_euroCupSeasonStrOfs[e])) {
                reset();
                return false;
            }
            m_euroCupActive[e] = (active != 0);
            if (m_euroCupActive[e]) {
                if (!in.read(reinterpret_cast<char *>(m_euroCupBuffers[e].data()), kEuroCupBufferSize)) {
                    reset();
                    return false;
                }
            }
        }
        int32_t numEuroMapEntries = 0;
        if (!readBinary(in, numEuroMapEntries) || numEuroMapEntries < 0 || numEuroMapEntries > 256) {
            reset();
            return false;
        }
        for (int32_t i = 0; i < numEuroMapEntries; ++i) {
            uint16_t calOfs = 0;
            int32_t eIdx = -1;
            if (!readBinary(in, calOfs) || !readBinary(in, eIdx) || calOfs >= 1870 || eIdx < 0 || eIdx >= kNumEuroCups) {
                reset();
                return false;
            }
            m_euroCupByCalOfs[calOfs] = eIdx;
        }
        if (m_activeEuroCupIndex >= 0 && m_activeEuroCupIndex < kNumEuroCups && m_euroCupActive[m_activeEuroCupIndex]) {
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2],
                        m_euroCupBuffers[m_activeEuroCupIndex].data(),
                        kEuroCupBufferSize);
        }
    }

    if (fileVersion >= 6) {
        for (int i = 0; i < m_numPlayers; ++i) {
            if (!readPlayerV6Extension(in, m_players[i])) {
                reset();
                logWarn("HotseatCareerManager: Corrupted V6 player extension %d in %s", i, path.c_str());
                return false;
            }
        }
        for (int32_t i = 0; i < hdr.numOffers; ++i) {
            if (!readTransferOfferV6Extension(in, m_transferOffers[i])) {
                reset();
                logWarn("HotseatCareerManager: Corrupted V6 offer extension %d in %s", i, path.c_str());
                return false;
            }
        }
    }

    if (fileVersion <= 4 && m_numPlayers > 1) {
        for (int i = 0; i < m_numPlayers; ++i) {
            auto& ctx = m_players[i];
            int count = std::min<int>(ctx.careerNumPlayers, kCurrentMatchPlayersSize);
            for (int slot = 0; slot < count; ++slot) {
                uint8_t ord = ctx.currentMatchPlayers[slot];
                if (ord >= 30)
                    continue;
                auto *pf = reinterpret_cast<PlayerFile *>(
                    ctx.careerTeam + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize);
                if (!isValidPlayerName(pf->playerName)) {
                    populateNamedTrialPlayerSlot(ctx.careerTeam, ctx.playerTrialWeeks, ord,
                                                 static_cast<uint16_t>(ctx.teamId), false);
                }
            }
        }
    }

    if (fileVersion >= 7) {
        uint8_t endSeasonActiveByte = 0;
        int32_t endSeasonIdx = 0;
        if (!readBinary(in, endSeasonActiveByte) || !readBinary(in, endSeasonIdx)) {
            reset();
            logWarn("HotseatCareerManager: Corrupted end-of-season header in %s", path.c_str());
            return false;
        }
        m_endSeasonActive = (endSeasonActiveByte != 0);
        m_endSeasonPlayerIndex = std::clamp(endSeasonIdx, 0, std::max(0, m_numPlayers - 1));

        int elimCount = (fileVersion >= 8) ? m_numPlayers : kLegacyV7MaxHotseatPlayers;
        for (int i = 0; i < elimCount; ++i) {
            uint8_t elim = 0;
            if (!readBinary(in, elim)) {
                reset();
                return false;
            }
            if (i < static_cast<int>(m_endSeasonEliminated.size())) {
                m_endSeasonEliminated[i] = (elim != 0) ? 1 : 0;
            }
        }

        int32_t numClaimed = 0;
        if (!readBinary(in, numClaimed) || numClaimed < 0 || numClaimed > 64) {
            reset();
            return false;
        }
        m_claimedJobOfferTeams.clear();
        for (int32_t i = 0; i < numClaimed; ++i) {
            uint16_t tid = 0;
            if (!readBinary(in, tid)) {
                reset();
                return false;
            }
            m_claimedJobOfferTeams.push_back(tid);
        }
    } else if (m_numPlayers > 1 && m_players[m_activePlayerIndex].retired != 0) {
        m_endSeasonActive = true;
        m_endSeasonPlayerIndex = m_activePlayerIndex;
    }

    m_emergencyLoanDepth = 0;
    m_playMatchSetupStack.clear();
    m_h2hPackMaps.assign(static_cast<size_t>(m_numPlayers), H2hPackedTeamMap{});

    stageActiveCalendarSlotToVm();
    loadSwosContext(m_players[m_activePlayerIndex]);
    if (swos.selTeamsPtr && swos.g_numSelectedTeams > 0) {
        syncPlayersWithSelectedTeams(true);
    }
    syncTransferMarketPoolsForCurrentPlayer();
    logInfo("HotseatCareerManager: Successfully loaded hotseat state (%d players, active P%d) from %s",
            m_numPlayers, m_activePlayerIndex + 1, path.c_str());
    return true;
}
