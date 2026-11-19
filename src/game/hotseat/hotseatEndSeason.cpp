#include "hotseatInternal.h"
#include "menus.h"

using namespace HotseatInternal;

bool HotseatCareerManager::isValidHotseatJobOffer(uint16_t teamId, int forPlayerIdx /* = -1 */) const
{
    if (!isActive() || m_numPlayers <= 1)
        return true;

    if (teamId == 0 || teamId == 0xFFFF)
        return false;

    uint8_t country = static_cast<uint8_t>(teamId & 0xFFu);
    if (country >= 80) // national teams incompatible with multi-club hotseat career
        return false;

    if (forPlayerIdx < 0)
        forPlayerIdx = m_activePlayerIndex;

    for (int i = 0; i < m_numPlayers; ++i) {
        if (static_cast<uint16_t>(m_players[i].teamId) == teamId)
            return false;
        if (i != forPlayerIdx && m_players[i].teamNoOfferAccepted != -1 &&
            static_cast<uint16_t>(m_players[i].teamNoOfferAccepted) == teamId) {
            return false;
        }
    }

    int16_t forAccepted = (forPlayerIdx >= 0 && forPlayerIdx < m_numPlayers)
        ? m_players[forPlayerIdx].teamNoOfferAccepted : -1;
    if (forAccepted == -1 || static_cast<uint16_t>(forAccepted) != teamId) {
        for (uint16_t claimedId : m_claimedJobOfferTeams) {
            if (claimedId == teamId)
                return false;
        }
    }

    return true;
}

void HotseatCareerManager::removeJobOfferForAllOtherPlayers(uint16_t teamId, int acceptingPlayerIdx)
{
    if (!isActive() || teamId == 0 || teamId == 0xFFFF)
        return;

    if (std::find(m_claimedJobOfferTeams.begin(), m_claimedJobOfferTeams.end(), teamId) == m_claimedJobOfferTeams.end()) {
        m_claimedJobOfferTeams.push_back(teamId);
    }

    for (int i = 0; i < m_numPlayers; ++i) {
        if (i == acceptingPlayerIdx)
            continue;
        removeTeamFromJobOfferBuffer(m_players[i].newJobOffers, m_players[i].numNewJobOffers, teamId);
        removeTeamFromJobOfferBuffer(m_players[i].jobOffers, m_players[i].numJobOffers, teamId);
        if (i == m_activePlayerIndex) {
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kNewJobOffers], m_players[i].newJobOffers, kJobOffersBufferSize);
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kJobOffers], m_players[i].jobOffers, kJobOffersBufferSize);
            *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]) = m_players[i].numNewJobOffers;
            *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]) = m_players[i].numJobOffers;
        }
    }
}

void HotseatCareerManager::sanitizeJobOffersForContext(HotseatPlayerContext& ctx, int playerIdx)
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    auto sanitizeBuf = [&](uint8_t *buf, int16_t& count) {
        int n = std::clamp<int>(count, 0, kMaxJobOffersPerQueue);
        int writeIdx = 0;
        for (int i = 0; i < n; ++i) {
            const uint8_t *src = buf + static_cast<size_t>(i) * kJobOfferEntrySize;
            uint16_t tid = *reinterpret_cast<const uint16_t *>(src);
            if (!isValidHotseatJobOffer(tid, playerIdx))
                continue;
            if (writeIdx != i) {
                std::memmove(buf + static_cast<size_t>(writeIdx) * kJobOfferEntrySize, src, kJobOfferEntrySize);
            }
            ++writeIdx;
        }
        if (writeIdx < kMaxJobOffersPerQueue) {
            std::memset(buf + static_cast<size_t>(writeIdx) * kJobOfferEntrySize, 0,
                        static_cast<size_t>(kMaxJobOffersPerQueue - writeIdx) * kJobOfferEntrySize);
        }
        count = static_cast<int16_t>(writeIdx);
    };

    sanitizeBuf(ctx.newJobOffers, ctx.numNewJobOffers);
    sanitizeBuf(ctx.jobOffers, ctx.numJobOffers);

    if (ctx.teamNoOfferAccepted != -1) {
        uint16_t accTid = static_cast<uint16_t>(ctx.teamNoOfferAccepted);
        bool conflict = false;
        for (int j = 0; j < m_numPlayers; ++j) {
            if (j != playerIdx && (static_cast<uint16_t>(m_players[j].teamId) == accTid ||
                (m_players[j].teamNoOfferAccepted != -1 && static_cast<uint16_t>(m_players[j].teamNoOfferAccepted) == accTid))) {
                conflict = true;
                break;
            }
        }
        if (conflict) {
            ctx.teamNoOfferAccepted = -1;
            ctx.teamFundsAccepted = 0;
        }
    }

    if (playerIdx == m_activePlayerIndex) {
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kNewJobOffers], ctx.newJobOffers, kJobOffersBufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kJobOffers], ctx.jobOffers, kJobOffersBufferSize);
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]) = ctx.numNewJobOffers;
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]) = ctx.numJobOffers;
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = ctx.teamNoOfferAccepted;
        *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = ctx.teamFundsAccepted;
    }
}

void HotseatCareerManager::sanitizeJobOffersForAllPlayers()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    m_claimedJobOfferTeams.clear();
    for (int i = 0; i < m_numPlayers; ++i) {
        if (m_players[i].teamNoOfferAccepted != -1) {
            m_claimedJobOfferTeams.push_back(static_cast<uint16_t>(m_players[i].teamNoOfferAccepted));
        }
    }
    for (int i = 0; i < m_numPlayers; ++i) {
        sanitizeJobOffersForContext(m_players[i], i);
    }
}

void HotseatCareerManager::resetCareerOverMenuState() const
{
    auto currentMenu = getCurrentMenu();
    auto packed = getCurrentPackedMenu();
    if (!currentMenu || !packed || !SwosVM::isSwosPtr(packed) ||
        SwosVM::ptrToOffset(packed) != CareerOffsets::kCareerOverMenu) {
        return;
    }

    auto *entries = currentMenu->entries();
    if (currentMenu->numEntries > 3) {
        entries[3].invisible = 1; // "YOUR CAREER IS OVER"
    }
    if (currentMenu->numEntries > 4) {
        entries[4].disabled = 0;
        entries[4].bg.entryColor = (entries[4].bg.entryColor & 0xFFF0u) | 14u;
    }
    if (currentMenu->numEntries > 5) {
        entries[5].invisible = 0;
        entries[5].disabled = 0;
        entries[5].bg.entryColor = (entries[5].bg.entryColor & 0xFFF0u) | 14u;
        entries[5].stringFlags &= static_cast<uint16_t>(~0x2000u);
        entries[5].fg.string.setRaw(CareerOffsets::kAJobOffers);
    }
    if (currentMenu->numEntries > 6) {
        entries[6].disabled = 0;
        entries[6].bg.entryColor = (entries[6].bg.entryColor & 0xFFF0u) | 14u;
        entries[6].fg.string.setRaw(CareerOffsets::kAContinueCareer);
    }
    if (currentMenu->numEntries > 7) {
        entries[7].invisible = 0;
    }
}

bool HotseatCareerManager::isPlayerCareerOver(const HotseatPlayerContext& ctx) const
{
    return ctx.youreSacked != 0 &&
           ctx.teamNoOfferAccepted == -1 &&
           ctx.numNewJobOffers == 0 &&
           ctx.numJobOffers == 0;
}

bool HotseatCareerManager::isVmCareerOver() const
{
    int16_t sacked = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kYoureSacked]);
    int16_t accepted = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]);
    int16_t newCnt = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]);
    int16_t oldCnt = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]);
    return sacked != 0 && accepted == -1 && newCnt == 0 && oldCnt == 0;
}

bool HotseatCareerManager::hasSurvivingPlayerAfterEndSeason(int excludePlayerIdx /* = -1 */) const
{
    for (int i = 0; i < m_numPlayers; ++i) {
        bool elim = (i < static_cast<int>(m_endSeasonEliminated.size())) ? (m_endSeasonEliminated[i] != 0) : false;
        if (i == excludePlayerIdx || elim)
            continue;
        const auto& p = m_players[i];
        bool curOver = (i == m_activePlayerIndex) ? isVmCareerOver() : isPlayerCareerOver(p);
        if (!curOver)
            return true;
    }
    return false;
}

void HotseatCareerManager::advanceEndSeasonToNextPlayer(int nextIdx)
{
    m_endSeasonPlayerIndex = nextIdx;
    switchToPlayer(nextIdx, true);
    sanitizeJobOffersForContext(m_players[nextIdx], nextIdx);
    resetCareerOverMenuState();
    save68kRegisters();
    A6 = CareerOffsets::kCareerOverMenu;
    SWOS::ActivateMenu();
    restore68kRegisters();
}

void HotseatCareerManager::packDepartedTeamIntoTmdFile(const HotseatPlayerContext& ctx)
{
    if (ctx.isNationalSelector != 0 || ctx.teamId == 0)
        return;

    loadSwosContext(ctx, false);

    uint8_t savedSel0PlayerNumbers[kNumPlayersInTeam] = {};
    TeamFile *selTeams = swos.selTeamsPtr
        ? reinterpret_cast<TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr))
        : nullptr;
    if (selTeams) {
        std::memcpy(savedSel0PlayerNumbers, selTeams[0].playerNumbers, kNumPlayersInTeam);
        const auto *ctxTf = reinterpret_cast<const TeamFile *>(ctx.careerTeam);
        std::memcpy(selTeams[0].playerNumbers, ctxTf->playerNumbers, kNumPlayersInTeam);
    }

    extern void cseg_5A6A1();
    save68kRegisters();
    cseg_5A6A1();
    restore68kRegisters();

    if (selTeams) {
        std::memcpy(selTeams[0].playerNumbers, savedSel0PlayerNumbers, kNumPlayersInTeam);
    }

    uint16_t tid = static_cast<uint16_t>(ctx.teamId);
    auto *vmCareerTf = reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
    vmCareerTf->teamControls = kComputerTeam;
    updateExistingTmdFileEntry(tid, &SwosVM::g_memByte[CareerOffsets::kCareerTeam], kComputerTeam);

    if (selTeams && swos.g_numSelectedTeams > 0) {
        int count = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
        for (int t = 0; t < count; ++t) {
            if (*reinterpret_cast<const uint16_t *>(&selTeams[t]) == tid) {
                std::memcpy(&selTeams[t], &SwosVM::g_memByte[CareerOffsets::kCareerTeam], sizeof(TeamFile));
                selTeams[t].teamControls = kComputerTeam;
                break;
            }
        }
    }
}

void HotseatCareerManager::onSeasonFinishedForAllPlayers()
{
    static bool s_inSeasonEnd = false;
    if (s_inSeasonEnd || !isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    struct Guard {
        bool& flag;
        explicit Guard(bool& f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(s_inSeasonEnd);

    extern void cseg_3C07C();

    int activeIdx = m_activePlayerIndex;
    saveCurrentSwosContext(m_players[activeIdx]);
    syncPlayersWithSelectedTeams(true);
    if (m_hasActiveWorld) {
        saveActiveWorldFromVm();
    }

    m_endSeasonPendingInit = true;
    m_endSeasonActive = false;
    m_endSeasonPlayerIndex = 0;
    m_endSeasonEliminated.assign(static_cast<size_t>(m_numPlayers), 0);
    m_claimedJobOfferTeams.clear();
    for (int i = 0; i < m_numPlayers; ++i) {
        if (m_players[i].teamNoOfferAccepted != -1) {
            m_claimedJobOfferTeams.push_back(static_cast<uint16_t>(m_players[i].teamNoOfferAccepted));
        }
    }

    for (int i = 0; i < m_numPlayers; ++i) {
        if (i == activeIdx)
            continue;

        switchToPlayer(i, true);
        save68kRegisters();
        cseg_3C07C();
        restore68kRegisters();
        saveCurrentSwosContext(m_players[i]);
        sanitizeJobOffersForContext(m_players[i], i);
        if (m_hasActiveWorld) {
            saveActiveWorldFromVm();
        }
    }

    switchToPlayer(activeIdx, true);
}

void HotseatCareerManager::onCareerOverInit()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    resetCareerOverMenuState();

    if (m_endSeasonPendingInit) {
        m_endSeasonPendingInit = false;
        saveCurrentSwosContext(m_players[m_activePlayerIndex]);
        sanitizeJobOffersForAllPlayers();
        m_endSeasonActive = true;
        m_endSeasonPlayerIndex = 0;
        m_endSeasonEliminated.assign(static_cast<size_t>(m_numPlayers), 0);

        if (m_activePlayerIndex != 0 ||
            (m_hasActiveWorld && m_activeWorldKey != worldKeyForPlayer(m_players[0]))) {
            switchToPlayer(0, true);
        } else {
            sanitizeJobOffersForContext(m_players[0], 0);
        }
    } else if (m_endSeasonActive) {
        if (m_endSeasonPlayerIndex >= 0 && m_endSeasonPlayerIndex < m_numPlayers &&
            m_activePlayerIndex != m_endSeasonPlayerIndex) {
            switchToPlayer(m_endSeasonPlayerIndex, true);
        }
        sanitizeJobOffersForContext(m_players[m_activePlayerIndex], m_activePlayerIndex);
    }
}

void HotseatCareerManager::onAfterJobOfferRoll()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    if (D0.asWord() < 21) {
        D0 = 21;
    }
}

void HotseatCareerManager::onFilterNewJobOffer()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    if (!SwosVM::flags.zero && !isValidHotseatJobOffer(D0.asWord(), m_activePlayerIndex)) {
        SwosVM::flags.zero = true;
    }
}

void HotseatCareerManager::onAfterAcceptJobOffer()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    saveCurrentSwosContext(m_players[m_activePlayerIndex]);
    int16_t acceptedTid = m_players[m_activePlayerIndex].teamNoOfferAccepted;
    if (acceptedTid != -1) {
        removeJobOfferForAllOtherPlayers(static_cast<uint16_t>(acceptedTid), m_activePlayerIndex);
    }
}

bool HotseatCareerManager::onJobOffersMenuRetiredSackedEmpty()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return false;

    saveCurrentSwosContext(m_players[m_activePlayerIndex]);
    if (hasSurvivingPlayerAfterEndSeason(m_activePlayerIndex)) {
        return true;
    }
    return false;
}

void HotseatCareerManager::onExitCareerMenu()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer) {
        if (isVmCareerOver()) {
            D0 = 0;
            ::CareerInitVars();
        }
        SetExitMenuFlag();
        return;
    }

    int k = m_activePlayerIndex;
    saveCurrentSwosContext(m_players[k]);

    if (m_endSeasonActive && isPlayerCareerOver(m_players[k])) {
        if (k >= 0 && k < static_cast<int>(m_endSeasonEliminated.size()))
            m_endSeasonEliminated[k] = 1;
        if (k + 1 < m_numPlayers) {
            advanceEndSeasonToNextPlayer(k + 1);
            return;
        }
        if (hasSurvivingPlayerAfterEndSeason(-1)) {
            executeHotseatSeasonTransition();
            save68kRegisters();
            A6 = CareerOffsets::kContestMenu;
            SWOS::ActivateMenu();
            restore68kRegisters();
            return;
        }
        m_endSeasonActive = false;
        D0 = 0;
        ::CareerInitVars();
        SetExitMenuFlag();
        return;
    }

    m_endSeasonActive = false;
    m_endSeasonPendingInit = false;
    SetExitMenuFlag();
}

void HotseatCareerManager::onContinueCareer()
{
    uint16_t seasonShowing = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);

    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer) {
        if (seasonShowing == 19) {
            ::ShowRetirementMessage();
            D0 = 0;
            ::CareerInitVars();
            SetExitMenuFlag();
        } else {
            ::ContinueCareerNextSeason();
            A6 = CareerOffsets::kContestMenu;
            SWOS::ActivateMenu();
        }
        return;
    }

    int k = m_activePlayerIndex;
    saveCurrentSwosContext(m_players[k]);

    if (seasonShowing == 19) {
        if (k + 1 < m_numPlayers) {
            advanceEndSeasonToNextPlayer(k + 1);
            return;
        }
        m_endSeasonActive = false;
        m_endSeasonPendingInit = false;
        ::ShowRetirementMessage();
        D0 = 0;
        ::CareerInitVars();
        SetExitMenuFlag();
        return;
    }

    if (m_players[k].teamNoOfferAccepted != -1) {
        removeJobOfferForAllOtherPlayers(static_cast<uint16_t>(m_players[k].teamNoOfferAccepted), k);
        if (m_players[k].youreSacked == 0) {
            save68kRegisters();
            ::ShowLeavingMessage();
            restore68kRegisters();
        }
    } else {
        save68kRegisters();
        ::ShowChairmanOpinionAboutTheSeason();
        restore68kRegisters();
    }

    if (k + 1 < m_numPlayers) {
        advanceEndSeasonToNextPlayer(k + 1);
        return;
    }

    executeHotseatSeasonTransition();
    save68kRegisters();
    A6 = CareerOffsets::kContestMenu;
    SWOS::ActivateMenu();
    restore68kRegisters();
}

static void relinkInjectedDiyCompetitionBuffer(HotseatCareerManager& mgr, size_t compOfs)
{
    int16_t numTeams = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[compOfs + 49]);
    uint16_t compType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[compOfs]);
    if (numTeams <= 0 || numTeams > 64 || !swos.selTeamsPtr)
        return;

    int32_t selPtrVal = static_cast<int32_t>(swos.selTeamsPtr);
    uint16_t activeTid = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    auto *selTeams = reinterpret_cast<TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return;

    uint32_t idsOfs = static_cast<uint32_t>((compType == 0) ? (compOfs + 301) : (compOfs + 589));
    auto findOrAppendTeam = [&](uint16_t tid) -> int {
        int count = std::clamp<int>(swos.g_numSelectedTeams, 0, kMaxSelectedTeamsCount);
        for (int t = 0; t < count; ++t) {
            if (*reinterpret_cast<const uint16_t *>(&selTeams[t]) == tid)
                return t;
        }
        if (count < kMaxSelectedTeamsCount) {
            TeamFile tf{};
            if (mgr.lookupCachedTeamFile(tid, tf)) {
                selTeams[count] = tf;
                swos.g_numSelectedTeams = static_cast<int16_t>(count + 1);
                return count;
            }
        }
        return -1;
    };

    for (int k = 0; k < numTeams; ++k) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[idsOfs + k * 2]);
        int32_t delta = 0;
        if (tid == activeTid && activeTid != 0 && activeTid != 0xFFFF) {
            delta = static_cast<int32_t>(CareerOffsets::kCareerTeam) - selPtrVal;
        } else {
            int idx = findOrAppendTeam(tid);
            if (idx < 0)
                continue;
            delta = static_cast<int32_t>(idx * sizeof(TeamFile));
        }
        if (compType == 0) {
            *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[compOfs + 691 + k * 18]) = delta;
        } else {
            *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[compOfs + 835 + k * 4]) = delta;
        }
    }
}

void HotseatCareerManager::injectEndSeasonLeagueResult()
{
    if (!isActive() || !m_inEndSeasonCompute || m_leagueWorlds.empty())
        return;

    uint8_t country = SwosVM::g_memByte[CareerOffsets::kDseg180782];
    uint8_t div = static_cast<uint8_t>(D7.asWord() & 0xFFu);
    auto it = m_leagueWorlds.find({ country, div });
    if (it == m_leagueWorlds.end() || !it->second.initialized)
        return;

    const auto& lw = it->second;
    int16_t count = static_cast<int16_t>(std::clamp<int>(lw.numSelectedTeams, 0, kMaxSelectedTeamsCount));
    swos.g_numSelectedTeams = count;
    std::memset(swos.g_selectedTeams, 0, kSelectedTeamsBufferSize);
    size_t copyBytes = std::min(lw.selectedTeams.size(), kSelectedTeamsBufferSize);
    if (copyBytes > 0) {
        std::memcpy(swos.g_selectedTeams, lw.selectedTeams.data(), copyBytes);
    }

    constexpr size_t kDiyLeagueStructBytes = 1843;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDiyCompetitionStart],
                lw.competitionFileBuffer, kDiyLeagueStructBytes);
    relinkInjectedDiyCompetitionBuffer(*this, CareerOffsets::kDiyCompetitionStart);
}

void HotseatCareerManager::injectEndSeasonCup1Result()
{
    if (!isActive() || !m_inEndSeasonCompute || m_countryWorlds.empty())
        return;

    uint8_t country = SwosVM::g_memByte[CareerOffsets::kDseg180782];
    auto it = m_countryWorlds.find(country);
    if (it == m_countryWorlds.end() || !it->second.initialized)
        return;

    constexpr size_t kDiyCupStructBytes = 1091;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDiyCompetitionStart],
                it->second.cup1, kDiyCupStructBytes);
    relinkInjectedDiyCompetitionBuffer(*this, CareerOffsets::kDiyCompetitionStart);
}

void HotseatCareerManager::injectEndSeasonCup2Result()
{
    if (!isActive() || !m_inEndSeasonCompute || m_countryWorlds.empty())
        return;

    uint8_t country = SwosVM::g_memByte[CareerOffsets::kDseg180782];
    auto it = m_countryWorlds.find(country);
    if (it == m_countryWorlds.end() || !it->second.initialized)
        return;

    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDiyCompetitionStart],
                it->second.cup2, kCup2BufferSize);
    relinkInjectedDiyCompetitionBuffer(*this, CareerOffsets::kDiyCompetitionStart);
}

void HotseatCareerManager::injectEndSeasonPlayoffResult()
{
    if (!isActive() || !m_inEndSeasonCompute || m_leagueWorlds.empty())
        return;

    uint8_t country = SwosVM::g_memByte[CareerOffsets::kDseg180782];
    uint8_t div = static_cast<uint8_t>(D7.asWord() & 0xFFu);
    auto it = m_leagueWorlds.find({ country, div });
    if (it == m_leagueWorlds.end() || !it->second.initialized)
        return;

    const auto& lw = it->second;
    uint16_t cupType = *reinterpret_cast<const uint16_t *>(lw.dsegD9C9F);
    int16_t numTeams = *reinterpret_cast<const int16_t *>(lw.dsegD9C9F + 49);
    uint16_t roundIdx = *reinterpret_cast<const uint16_t *>(lw.dsegD9C9F + 0x53);
    if (cupType != 1 || numTeams <= 0 || roundIdx == 0)
        return;

    int16_t count = static_cast<int16_t>(std::clamp<int>(lw.numSelectedTeams, 0, kMaxSelectedTeamsCount));
    swos.g_numSelectedTeams = count;
    std::memset(swos.g_selectedTeams, 0, kSelectedTeamsBufferSize);
    size_t copyBytes = std::min(lw.selectedTeams.size(), kSelectedTeamsBufferSize);
    if (copyBytes > 0) {
        std::memcpy(swos.g_selectedTeams, lw.selectedTeams.data(), copyBytes);
    }

    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDiyCompetitionStart],
                lw.dsegD9C9F, kCup4BufferSize);
    relinkInjectedDiyCompetitionBuffer(*this, CareerOffsets::kDiyCompetitionStart);
}

void HotseatCareerManager::executeHotseatSeasonTransition()
{
    m_endSeasonActive = false;
    m_endSeasonPendingInit = false;

    std::vector<HotseatPlayerContext> prePlayers;
    std::vector<PendingSeasonPlayerState> survivorPlans;
    std::vector<HotseatPlayerContext> survivorPreCtx;
    std::vector<HotseatPlayerContext> departedContexts;

    for (int i = 0; i < m_numPlayers; ++i) {
        prePlayers.push_back(m_players[i]);
        bool elim = (i < static_cast<int>(m_endSeasonEliminated.size())) && (m_endSeasonEliminated[i] != 0);
        int16_t accTid = (!elim) ? m_players[i].teamNoOfferAccepted : -1;
        bool switched = (!elim && accTid != -1);
        if (elim || switched) {
            departedContexts.push_back(m_players[i]);
        }
        if (!elim) {
            PendingSeasonPlayerState plan;
            plan.oldTeamId = m_players[i].teamId;
            plan.newTeamId = switched ? accTid : m_players[i].teamId;
            plan.acceptedFunds = m_players[i].teamFundsAccepted;
            plan.switched = switched;
            survivorPlans.push_back(plan);
            survivorPreCtx.push_back(m_players[i]);
        }
    }

    if (survivorPreCtx.empty()) {
        m_pendingSeasonTransitions.clear();
        return;
    }

    // Save current VM world & push freshest player rosters into every world + tmdFileBuffer
    saveActiveWorldFromVm();
    for (int i = 0; i < m_numPlayers; ++i) {
        if (m_players[i].teamId != 0) {
            TeamControls desiredControls = (m_players[i].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            updateExistingTmdFileEntry(static_cast<uint16_t>(m_players[i].teamId),
                                       m_players[i].careerTeam, desiredControls);
            cacheTeamFile(*reinterpret_cast<const TeamFile *>(m_players[i].careerTeam));
        }
    }
    if (!m_leagueWorlds.empty()) {
        for (auto& wkv : m_leagueWorlds) {
            auto& lw = wkv.second;
            if (!lw.initialized || lw.numSelectedTeams <= 0)
                continue;
            auto *teams = reinterpret_cast<TeamFile *>(lw.selectedTeams.data());
            int count = std::min<int>(lw.numSelectedTeams, static_cast<int>(lw.selectedTeams.size() / sizeof(TeamFile)));
            for (int t = 0; t < count; ++t) {
                uint16_t tid = *reinterpret_cast<const uint16_t *>(&teams[t]);
                int pIdx = findHotseatPlayerByTeamId(static_cast<int16_t>(tid));
                if (pIdx >= 0) {
                    std::memcpy(&teams[t], m_players[pIdx].careerTeam, sizeof(TeamFile));
                    teams[t].teamControls = (m_players[pIdx].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
                }
            }
        }
    } else {
        syncPlayersWithSelectedTeams(true);
    }

    // Load survivor 0's Season S world into VM, compute per-staying-player budgets while Season S tables are live,
    // and execute cseg_91428 ONCE with live world/cup injection hooks enabled.
    LeagueWorldKey repOldKey = worldKeyForPlayer(survivorPreCtx[0]);
    if (m_hasActiveWorld && m_leagueWorlds.count(repOldKey) != 0) {
        loadWorldToVm(repOldKey, false);
    }
    loadSwosContext(survivorPreCtx[0], !m_leagueWorlds.empty());
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = -1;

    // Pre-compute next season's budget (cseg_943FA) for every staying player against their OWN Season S table
    int survivorCount = static_cast<int>(survivorPreCtx.size());
    std::vector<int32_t> computedStartingBalances(survivorCount, 0);
    std::vector<int16_t> computedAddInvest(survivorCount, 0);
    for (int i = 0; i < survivorCount; ++i) {
        if (!survivorPlans[i].switched) {
            LeagueWorldKey oldK = worldKeyForPlayer(survivorPreCtx[i]);
            if (m_hasActiveWorld && m_leagueWorlds.count(oldK) != 0 && oldK != m_activeWorldKey) {
                loadWorldToVm(oldK, false);
            }
            loadSwosContext(survivorPreCtx[i], false);
            *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = -1;
            save68kRegisters();
            ::cseg_943FA();
            restore68kRegisters();
            computedStartingBalances[i] = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kStartingBalance]);
            computedAddInvest[i] = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kAdditionalInvestments]);
        }
    }

    if (m_hasActiveWorld && m_leagueWorlds.count(repOldKey) != 0 && repOldKey != m_activeWorldKey) {
        loadWorldToVm(repOldKey, false);
    }
    loadSwosContext(survivorPreCtx[0], true);
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = -1;

    m_inEndSeasonCompute = true;
    save68kRegisters();
    ::cseg_91428();
    restore68kRegisters();
    m_inEndSeasonCompute = false;

    for (const auto& dep : departedContexts) {
        packDepartedTeamIntoTmdFile(dep);
    }

    ensurePlayerSlots(survivorCount);
    for (int i = 0; i < survivorCount; ++i) {
        m_players[i] = survivorPreCtx[i];
        m_players[i].teamId = survivorPlans[i].newTeamId;
        if (survivorPlans[i].switched)
            m_players[i].teamName.clear();
        m_players[i].teamNoOfferAccepted = -1;
        m_players[i].teamFundsAccepted = 0;
    }
    m_numPlayers = survivorCount;
    m_activePlayerIndex = 0;
    m_claimedJobOfferTeams.clear();
    std::fill(m_endSeasonEliminated.begin(), m_endSeasonEliminated.end(), 0);

    resetNonHotseatTeamControlsInTmdFile();

    // Determine each survivor's target Season S+1 (countryFileNo, divisionId) using updated leaguesTableCopy
    const int8_t *leaguesDeltas = reinterpret_cast<const int8_t *>(&SwosVM::g_memByte[CareerOffsets::kLeaguesTableCopy]);
    std::vector<LeagueWorldKey> survivorTargetKeys(survivorCount);

    for (int i = 0; i < survivorCount; ++i) {
        auto& p = m_players[i];
        if (survivorPlans[i].switched) {
            loadSwosContext(survivorPreCtx[i], false);
            int32_t startBal = computeJobOfferStartingBalance(survivorPlans[i].acceptedFunds);
            uint16_t numTmdCounter = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6E00]);
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6E00]) = static_cast<uint16_t>(numTmdCounter + 1);
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]) = 0;
            loadSwitchedClubVia5DB56(survivorPlans[i].newTeamId, startBal);

            auto *tf = reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
            uint16_t baseLeague = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]);
            int delta = (tf->globalTeamNumber < 12944) ? static_cast<int>(leaguesDeltas[tf->globalTeamNumber]) : 0;
            uint8_t newDiv = static_cast<uint8_t>(std::max(0, static_cast<int>(baseLeague) + delta));

            tf->league = newDiv;
            tf->teamControls = (p.coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            writeTeamCoachName(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], p.managerName, p.managerSurname);
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]) = baseLeague;
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]) = newDiv;

            saveCurrentSwosContext(p);
            p.careerTeamLeague = baseLeague;
            p.dsegD6CDC = newDiv;
            p.startingBalance = startBal;
            p.newBalance = startBal;
            p.oldBalance = startBal;
            p.stayingInOldClub = -1; // InitializeNewSeason increments -1 -> 0 so cseg_64A65 expands squad
            p.teamNoOfferAccepted = -1;
            p.teamFundsAccepted = 0;
            survivorTargetKeys[i] = worldKeyForPlayer(p);
        } else {
            auto *tf = reinterpret_cast<TeamFile *>(p.careerTeam);
            int delta = (tf->globalTeamNumber < 12944) ? static_cast<int>(leaguesDeltas[tf->globalTeamNumber]) : 0;
            uint8_t newDiv = static_cast<uint8_t>(std::max(0, static_cast<int>(p.careerTeamLeague) + delta));
            p.dsegD6CDC = newDiv;
            tf->league = newDiv;
            tf->teamControls = (p.coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            p.startingBalance = computedStartingBalances[i];
            p.additionalInvestments = computedAddInvest[i];
            p.teamNoOfferAccepted = -1;
            p.teamFundsAccepted = 0;
            updateExistingTmdFileEntry(static_cast<uint16_t>(p.teamId), p.careerTeam, tf->teamControls);
            survivorTargetKeys[i] = worldKeyForPlayer(p);
        }
    }

    resetNonHotseatTeamControlsInTmdFile();

    // Group survivors by Season S+1 LeagueWorldKey (naturally handles Div3->Div2 splits, merges, & country moves!)
    std::map<LeagueWorldKey, std::vector<int>> targetWorldGroups;
    for (int i = 0; i < survivorCount; ++i) {
        targetWorldGroups[survivorTargetKeys[i]].push_back(i);
    }

    m_pendingSeasonTransitions = survivorPlans;

    // Preserve pre-transition season counter so EVERY newly created world advances from S to exact S+1
    uint16_t preSeasonShowing = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
    uint8_t preSeasonPlaying = SwosVM::g_memByte[CareerOffsets::kSeasonPlaying];
    std::vector<uint8_t> savedLeaguesTableCopy(2000);
    std::memcpy(savedLeaguesTableCopy.data(), &SwosVM::g_memByte[CareerOffsets::kLeaguesTableCopy], 2000);

    m_leagueWorlds.clear();
    m_countryWorlds.clear();
    m_hasActiveWorld = false;
    m_clubTeamCache.clear();

    extern void cseg_5DEB9();
    extern void cseg_3D204();
    extern void cseg_64A65();

    std::vector<uint8_t> savedCountryCup1(kCup1BufferSize);
    std::vector<uint8_t> savedCountryCup2(kCup2BufferSize);
    std::map<uint8_t, std::vector<uint8_t>> countryCup1Map;
    std::map<uint8_t, std::vector<uint8_t>> countryCup2Map;
    std::map<uint8_t, std::pair<uint32_t, uint32_t>> countryCupPtrMap;

    ScopedInitializingSeason seasonGuard(*this);
    bool isFirstWorld = true;

    for (const auto& wg : targetWorldGroups) {
        const LeagueWorldKey& wKey = wg.first;
        const std::vector<int>& groupPlayers = wg.second;
        int repIdx = groupPlayers[0];

        // Restore shared pre-season counters & updated leaguesTableCopy cleanly for each world init
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]) = preSeasonShowing;
        SwosVM::g_memByte[CareerOffsets::kSeasonPlaying] = preSeasonPlaying;
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kLeaguesTableCopy], savedLeaguesTableCopy.data(), 2000);
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 0;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 0;

        m_activePlayerIndex = repIdx;
        loadSwosContext(m_players[repIdx], false);
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = -1;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]) = wKey.divisionId;
        if (wKey.countryFileNo != 0xFF) {
            SwosVM::g_memByte[CareerOffsets::kCompCountryNumber] = wKey.countryFileNo;
            SwosVM::g_memByte[CareerOffsets::kDsegD6CB8] = wKey.countryFileNo;
        }

        // First world builds shared EuroCups for all players; subsequent worlds preserve them
        setPreserveEuroCupsOnSeasonInit(!isFirstWorld);
        m_calendarSlots.clear();
        m_euroCupByCalOfs.clear();

        save68kRegisters();
        ::InitializeNewSeason();
        restore68kRegisters();

        // Sync repIdx back (keeping base careerTeamLeague untouched!)
        uint16_t repBaseLeague = m_players[repIdx].careerTeamLeague;
        m_players[repIdx].dsegD6CDC = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]);
        saveCurrentSwosContext(m_players[repIdx]);
        m_players[repIdx].careerTeamLeague = repBaseLeague;
        m_players[repIdx].matchPlayedThisTurn = false;
        m_players[repIdx].turnCompleted = false;

        // Share Domestic Cups across divisions of the same country (e.g., Eng Div 2 & Eng Div 3)
        if (countryCup1Map.count(wKey.countryFileNo) == 0) {
            countryCup1Map[wKey.countryFileNo].assign(
                &SwosVM::g_memByte[CareerOffsets::kDsegD8D0A],
                &SwosVM::g_memByte[CareerOffsets::kDsegD8D0A] + kCup1BufferSize);
            countryCup2Map[wKey.countryFileNo].assign(
                &SwosVM::g_memByte[CareerOffsets::kDsegD985C],
                &SwosVM::g_memByte[CareerOffsets::kDsegD985C] + kCup2BufferSize);
            countryCupPtrMap[wKey.countryFileNo] = {
                *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 4]),
                *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8])
            };
        } else {
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A], countryCup1Map[wKey.countryFileNo].data(), kCup1BufferSize);
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], countryCup2Map[wKey.countryFileNo].data(), kCup2BufferSize);
            *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 4]) = countryCupPtrMap[wKey.countryFileNo].first;
            *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]) = countryCupPtrMap[wKey.countryFileNo].second;
        }

        // Ensure every human player belonging to this world (especially repIdx, whom SWOS cseg_8B2D3 skips
        // because repIdx sits in careerTeam) has an up-to-date slot in g_selectedTeams before snapshotting.
        for (int gIdx : groupPlayers) {
            const auto& gp = m_players[gIdx];
            uint16_t gpTid = static_cast<uint16_t>(gp.teamId);
            if (gpTid == 0 || gpTid == 0xFFFF)
                continue;
            auto *selArr = reinterpret_cast<TeamFile *>(swos.g_selectedTeams);
            int curCnt = std::clamp<int>(swos.g_numSelectedTeams, 0, kMaxSelectedTeamsCount);
            bool foundSlot = false;
            for (int t = 0; t < curCnt; ++t) {
                if (*reinterpret_cast<const uint16_t *>(&selArr[t]) == gpTid) {
                    std::memcpy(&selArr[t], gp.careerTeam, sizeof(TeamFile));
                    selArr[t].teamControls = (gp.coachOrPlayer == 1) ? kPlayerCoach : kCoach;
                    foundSlot = true;
                    break;
                }
            }
            if (!foundSlot && curCnt < kMaxSelectedTeamsCount) {
                std::memcpy(&selArr[curCnt], gp.careerTeam, sizeof(TeamFile));
                selArr[curCnt].teamControls = (gp.coachOrPlayer == 1) ? kPlayerCoach : kCoach;
                swos.g_numSelectedTeams = static_cast<int16_t>(curCnt + 1);
            }
        }

        // Snapshot world's freshly initialized g_selectedTeams before initializing additional siblings in same world
        uint16_t snapSeasonShowing = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
        uint16_t snapSeasonNum = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]);
        uint8_t snapSeasonPlaying = SwosVM::g_memByte[CareerOffsets::kSeasonPlaying];
        uint16_t snapD6CA2 = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]);
        uint16_t snapD7E66 = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD7E66]);
        uint8_t snapE0048 = SwosVM::g_memByte[CareerOffsets::kDsegE0048];
        uint16_t snapCalIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);

        std::vector<uint8_t> worldSelTeams(kSelectedTeamsBufferSize);
        std::memcpy(worldSelTeams.data(), swos.g_selectedTeams, kSelectedTeamsBufferSize);
        int16_t worldNumSelTeams = swos.g_numSelectedTeams;
        const auto& repCtx = m_players[repIdx];

        for (size_t gi = 1; gi < groupPlayers.size(); ++gi) {
            int pIdx = groupPlayers[gi];
            auto& p = m_players[pIdx];
            bool pSwitched = survivorPlans[pIdx].switched;

            p.currentSeasonShowing = snapSeasonShowing;
            p.currentSeasonNumber = snapSeasonNum;
            p.seasonPlaying = snapSeasonPlaying;
            p.dsegD6CA2 = snapD6CA2;
            p.dsegD7E66 = snapD7E66;
            p.dsegE0048 = snapE0048;
            p.dsegD6B62 = 0;
            p.dsegD6B64 = repCtx.dsegD6B64;
            p.dsegD6B94 = repCtx.dsegD6B94;
            p.dsegD6C46 = pSwitched ? 0 : repCtx.dsegD6C46;
            if (pSwitched)
                p.additionalInvestments = 0;
            p.teamNoOfferAccepted = -1;
            p.teamFundsAccepted = 0;
            p.dsegD6D0C = 0;
            p.playMatchBlink = 0;
            p.retired = 0;
            p.youreSacked = 0;
            p.numJobOffers = 0;
            p.numNewJobOffers = 0;
            p.dsegD6CA0 = 0;
            p.timeToNegotiate = 6;
            p.numOffers = 0;
            p.dsegD6D0E = 0;
            p.dsegD6D10 = 0;
            p.dsegD6D12 = 0;
            p.dsegD6D0A = 0;
            p.numPlayersOnTransferMarket = 0;
            std::memset(p.playersOnTransferMarket, 0, sizeof(p.playersOnTransferMarket));
            std::memset(p.dsegD6F26Block, 0, sizeof(p.dsegD6F26Block));
            std::memset(p.dsegD6DBA, 0, sizeof(p.dsegD6DBA));
            std::memset(p.newJobOffers, 0, sizeof(p.newJobOffers));
            std::memset(p.jobOffers, 0, sizeof(p.jobOffers));
            p.gamesPlayed = 0;
            p.numWins = 0;
            p.numDrawn = 0;
            p.numLost = 0;
            p.goalsScored = 0;
            p.goalsTaken = 0;
            std::memset(p.seasonStatsBlock, 0, sizeof(p.seasonStatsBlock));

            if (!pSwitched) {
                p.stayingInOldClub = (preSeasonShowing == 0) ? 0 : static_cast<int16_t>(p.stayingInOldClub + 1);
            } else {
                p.stayingInOldClub = 0;
                *reinterpret_cast<uint16_t *>(&p.dsegD761A) = 0xFFFF;
                p.dsegD761C = 0;
                p.dsegD8B6A = 0xFFFF;
                std::memset(p.playerWages, 0, sizeof(p.playerWages));
                std::memset(p.playerTrialWeeks, 0, sizeof(p.playerTrialWeeks));
                std::memset(p.trainingMatchSavedPlayers, 0, kCurrentMatchPlayersSize);
                std::memcpy(p.trainingMatchSavedPlayers, p.currentMatchPlayers, kNumPlayersInTeam);
                std::memset(p.lastMatchPlayers, 0, kCurrentMatchPlayersSize);
                std::memcpy(p.lastMatchPlayers, p.currentMatchPlayers, kNumPlayersInTeam);
            }

            applyCareerTeamResetInit(p.careerTeam, pSwitched);

            uint16_t pBaseLeague = p.careerTeamLeague;
            m_activePlayerIndex = pIdx;
            loadSwosContext(p, false);
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 0;
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 0;
            if (pSwitched) {
                save68kRegisters();
                cseg_64A65();
                restore68kRegisters();
            }
            std::memcpy(swos.g_selectedTeams, worldSelTeams.data(), kSelectedTeamsBufferSize);
            swos.g_numSelectedTeams = worldNumSelTeams;

            save68kRegisters();
            cseg_5DEB9();
            cseg_3D204();
            restore68kRegisters();

            saveCurrentSwosContext(p);
            p.careerTeamLeague = pBaseLeague;
            p.dsegD6CDC = wKey.divisionId;
            p.matchPlayedThisTurn = false;
            p.turnCompleted = false;

            size_t histOfs = static_cast<size_t>(snapSeasonShowing) * kCareerHistoryEntrySize;
            if (histOfs + kCareerHistoryEntrySize <= kCareerHistoryBufferSize) {
                std::memcpy(&p.careerHistory[histOfs], repCtx.careerHistory + histOfs, kCareerHistoryEntrySize);
                *reinterpret_cast<uint16_t *>(&p.careerHistory[histOfs]) = p.teamId;
                std::memset(&p.careerHistory[histOfs + 2], 0, 18);
                std::memcpy(&p.careerHistory[histOfs + 2], p.teamName.c_str(), std::min(p.teamName.size(), size_t{17}));
                uint32_t euroStrOfs = (p.euroCupIndex >= 0 && p.euroCupIndex < kNumEuroCups && m_euroCupActive[p.euroCupIndex])
                    ? m_euroCupSeasonStrOfs[p.euroCupIndex]
                    : 0;
                *reinterpret_cast<uint32_t *>(&p.careerHistory[histOfs + 34]) = euroStrOfs;
            }
        }

        // Restore world context & capture complete initialized LeagueWorldState + CountryWorldState
        std::memcpy(swos.g_selectedTeams, worldSelTeams.data(), kSelectedTeamsBufferSize);
        swos.g_numSelectedTeams = worldNumSelTeams;
        m_activePlayerIndex = repIdx;
        loadSwosContext(m_players[repIdx], false);
        restoreCalendarEntryFirstSlots();
        m_roundCalendarIndex = snapCalIdx;
        syncPlayersWithSelectedTeams(true);
        ensureHotseatTeamControlsInSelectedTeams();
        resetNonHotseatTeamControlsInTmdFile();
        relinkCompetitionTeamPointers();
        captureActiveWorldAsInitial(wKey);

        isFirstWorld = false;
    }

    setPreserveEuroCupsOnSeasonInit(false);
    m_pendingSeasonTransitions.clear();

    if (*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]) == 0) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], 0, kCup2BufferSize);
    }
    if (!anyEuroCupActive() && *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) == 0) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA11F], m_euroCupScheduleTemplate, sizeof(m_euroCupScheduleTemplate));
    }

    if (m_numPlayers == 1) {
        m_active = false;
        m_leagueWorlds.clear();
        m_countryWorlds.clear();
        m_hasActiveWorld = false;
        return;
    }

    synchronizeSharedCupCalendarEntries();
    m_globalWeek = 0;
    if (m_leagueWorlds.size() > 1) {
        resolveLockstepAndSelectPlayer(false);
    } else {
        int firstPlayer = findNextPlayerForCurrentCalendarSlot();
        if (firstPlayer < 0)
            firstPlayer = 0;
        switchToPlayer(firstPlayer, true);
    }
}

void HotseatCareerManager::onNewSeasonInitialized()
{
    if (m_initializingSeason || !isActive() || m_numPlayers < 1)
        return;

    ScopedInitializingSeason seasonGuard(*this);
    extern void cseg_5DEB9();
    extern void cseg_3D204();
    extern void cseg_64A65();

    if (m_numPlayers == 1) {
        m_pendingSeasonTransitions.clear();
        m_players[0].teamId = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
        resetNonHotseatTeamControlsInTmdFile();
        ensureHotseatTeamControlsInSelectedTeams();
        return;
    }

    int activeIdx = m_activePlayerIndex;
    saveCurrentSwosContext(m_players[activeIdx]);
    m_players[activeIdx].matchPlayedThisTurn = false;
    m_players[activeIdx].turnCompleted = false;

    uint16_t snapSeasonShowing = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
    uint16_t snapSeasonNum = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]);
    uint8_t snapSeasonPlaying = SwosVM::g_memByte[CareerOffsets::kSeasonPlaying];
    uint16_t snapD6CA2 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]);
    uint16_t snapD7E66 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD7E66]);
    uint8_t snapE0048 = SwosVM::g_memByte[CareerOffsets::kDsegE0048];
    uint16_t snapCalIdx = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);

    std::vector<uint8_t> savedSelTeams(68400);
    std::memcpy(savedSelTeams.data(), swos.g_selectedTeams, 68400);
    int16_t savedNumSelTeams = swos.g_numSelectedTeams;
    auto findReloadedTeamLeague = [&](int16_t targetId, uint16_t fallbackLeague) -> uint16_t {
        const auto *teams = reinterpret_cast<const TeamFile *>(savedSelTeams.data());
        int count = std::min<int>(savedNumSelTeams, 68400 / sizeof(TeamFile));
        for (int t = 0; t < count; ++t) {
            if (*reinterpret_cast<const int16_t *>(&teams[t]) == targetId)
                return teams[t].league;
        }
        return fallbackLeague;
    };

    const auto& activeCtx = m_players[activeIdx];
    for (int i = 0; i < m_numPlayers; ++i) {
        if (i == activeIdx)
            continue;

        bool switched = (i < static_cast<int>(m_pendingSeasonTransitions.size()))
            ? m_pendingSeasonTransitions[i].switched
            : (m_players[i].teamNoOfferAccepted != -1);
        int16_t targetNewTid = (i < static_cast<int>(m_pendingSeasonTransitions.size()))
            ? m_pendingSeasonTransitions[i].newTeamId
            : (m_players[i].teamNoOfferAccepted != -1 ? m_players[i].teamNoOfferAccepted : m_players[i].teamId);
        int32_t acceptedFunds = (i < static_cast<int>(m_pendingSeasonTransitions.size()))
            ? m_pendingSeasonTransitions[i].acceptedFunds
            : m_players[i].teamFundsAccepted;

        auto& p = m_players[i];
        p.currentSeasonShowing = snapSeasonShowing;
        p.currentSeasonNumber = snapSeasonNum;
        p.seasonPlaying = snapSeasonPlaying;
        p.dsegD6CA2 = snapD6CA2;
        p.dsegD7E66 = snapD7E66;
        p.dsegE0048 = snapE0048;
        p.dsegD6B62 = 0;
        p.dsegD6B64 = activeCtx.dsegD6B64;
        p.dsegD6B94 = activeCtx.dsegD6B94;
        p.dsegD6C46 = switched ? 0 : activeCtx.dsegD6C46;
        p.additionalInvestments = switched ? 0 : activeCtx.additionalInvestments;
        p.teamNoOfferAccepted = -1;
        p.teamFundsAccepted = 0;
        p.dsegD6D0C = 0;
        p.playMatchBlink = 0;
        p.retired = 0;
        p.youreSacked = 0;
        p.numJobOffers = 0;
        p.numNewJobOffers = 0;
        p.dsegD6CA0 = 0;
        p.timeToNegotiate = 6;
        p.numOffers = 0;
        p.dsegD6D0E = 0;
        p.dsegD6D10 = 0;
        p.dsegD6D12 = 0;
        p.dsegD6D0A = 0;
        p.numPlayersOnTransferMarket = 0;
        std::memset(p.playersOnTransferMarket, 0, sizeof(p.playersOnTransferMarket));
        std::memset(p.dsegD6F26Block, 0, sizeof(p.dsegD6F26Block));
        std::memset(p.dsegD6DBA, 0, sizeof(p.dsegD6DBA));
        std::memset(p.newJobOffers, 0, sizeof(p.newJobOffers));
        std::memset(p.jobOffers, 0, sizeof(p.jobOffers));
        p.gamesPlayed = 0;
        p.numWins = 0;
        p.numDrawn = 0;
        p.numLost = 0;
        p.goalsScored = 0;
        p.goalsTaken = 0;
        std::memset(p.seasonStatsBlock, 0, sizeof(p.seasonStatsBlock));

        if (switched && targetNewTid != 0 && targetNewTid != -1) {
            loadSwosContext(p, false);
            int32_t startBal = computeJobOfferStartingBalance(acceptedFunds);

            loadSwitchedClubVia5DB56(targetNewTid, startBal);

            std::memcpy(swos.g_selectedTeams, savedSelTeams.data(), 68400);
            swos.g_numSelectedTeams = savedNumSelTeams;

            uint16_t reloadedLeague = findReloadedTeamLeague(
                targetNewTid, *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]));
            reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam])->league =
                static_cast<uint8_t>(reloadedLeague);
            reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam])->teamControls =
                (p.coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            writeTeamCoachName(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], p.managerName, p.managerSurname);
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]) = reloadedLeague;
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]) = reloadedLeague;
            *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kStayingInOldClub]) = 0;
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761A]) = 0xFFFF;
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761C]) = 0;
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8B6A]) = 0xFFFF;
            std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD6BA6], 0, sizeof(p.playerWages));
            std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD6C26], 0, sizeof(p.playerTrialWeeks));
            std::memset(&SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers], 0, kCurrentMatchPlayersSize);
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers],
                        &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers], kNumPlayersInTeam);
            std::memset(&SwosVM::g_memByte[CareerOffsets::kLastMatchPlayers], 0, kCurrentMatchPlayersSize);
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kLastMatchPlayers],
                        &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers], kNumPlayersInTeam);

            applyCareerTeamResetInit(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], true);
            save68kRegisters();
            cseg_64A65();
            restore68kRegisters();

            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 0;
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 0;
            saveCurrentSwosContext(p);
            syncPlayersWithSelectedTeams(true);
            relinkCompetitionTeamPointers();

            save68kRegisters();
            cseg_5DEB9();
            cseg_3D204();
            restore68kRegisters();
            saveCurrentSwosContext(p);
        } else {
            p.stayingInOldClub = (snapSeasonShowing == 0) ? 0 : static_cast<int16_t>(p.stayingInOldClub + 1);
            uint16_t newLeague = findReloadedTeamLeague(p.teamId, p.careerTeamLeague);
            p.careerTeamLeague = newLeague;
            p.dsegD6CDC = newLeague;
            reinterpret_cast<TeamFile *>(p.careerTeam)->league = static_cast<uint8_t>(newLeague);
            reinterpret_cast<TeamFile *>(p.careerTeam)->teamControls = (p.coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            applyCareerTeamResetInit(p.careerTeam, false);

            loadSwosContext(p, false);
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 0;
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 0;
            syncPlayersWithSelectedTeams(true);
            relinkCompetitionTeamPointers();

            save68kRegisters();
            ::cseg_943FA();
            cseg_5DEB9();
            cseg_3D204();
            restore68kRegisters();
            saveCurrentSwosContext(p);
        }

        size_t histOfs = static_cast<size_t>(snapSeasonShowing) * kCareerHistoryEntrySize;
        if (histOfs + kCareerHistoryEntrySize <= kCareerHistoryBufferSize) {
            std::memcpy(&p.careerHistory[histOfs], activeCtx.careerHistory + histOfs, kCareerHistoryEntrySize);
            *reinterpret_cast<uint16_t *>(&p.careerHistory[histOfs]) = p.teamId;
            std::memset(&p.careerHistory[histOfs + 2], 0, 18);
            std::memcpy(&p.careerHistory[histOfs + 2], p.teamName.c_str(), std::min(p.teamName.size(), size_t{17}));
            uint32_t euroStrOfs = (p.euroCupIndex >= 0 && p.euroCupIndex < kNumEuroCups && m_euroCupActive[p.euroCupIndex])
                ? m_euroCupSeasonStrOfs[p.euroCupIndex]
                : 0;
            *reinterpret_cast<uint32_t *>(&p.careerHistory[histOfs + 34]) = euroStrOfs;
        }
    }

    m_pendingSeasonTransitions.clear();

    if (*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]) == 0) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], 0, kCup2BufferSize);
    }
    if (!anyEuroCupActive() && *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) == 0) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA11F], m_euroCupScheduleTemplate, sizeof(m_euroCupScheduleTemplate));
    }

    for (int i = 0; i < m_numPlayers; ++i) {
        m_players[i].matchPlayedThisTurn = false;
        m_players[i].turnCompleted = false;
    }
    m_roundCalendarIndex = snapCalIdx;

    syncPlayersWithSelectedTeams(true);
    ensureHotseatTeamControlsInSelectedTeams();
    resetNonHotseatTeamControlsInTmdFile();
    syncTransferMarketPoolsForCurrentPlayer();

    int firstPlayer = findNextPlayerForCurrentCalendarSlot();
    if (firstPlayer < 0)
        firstPlayer = 0;
    switchToPlayer(firstPlayer, true);
    captureActiveWorldAsInitial(worldKeyForPlayer(m_players[firstPlayer]));
}
