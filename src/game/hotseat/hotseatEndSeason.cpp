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
    if (country >= 80) // national teams incompatible with shared-country hotseat club career
        return false;

    uint8_t compCountry = SwosVM::g_memByte[CareerOffsets::kCompCountryNumber];
    if (compCountry != 0 && country != compCountry)
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

    if (swos.selTeamsPtr && swos.g_numSelectedTeams > 0) {
        const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
        if (selTeams) {
            uint16_t activeLeague = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]);
            int count = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
            for (int t = 0; t < count; ++t) {
                if (*reinterpret_cast<const uint16_t *>(&selTeams[t]) == teamId) {
                    if (selTeams[t].league != static_cast<uint8_t>(activeLeague))
                        return false;
                    break;
                }
            }
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

        loadSwosContext(m_players[i], true);
        save68kRegisters();
        cseg_3C07C();
        restore68kRegisters();
        saveCurrentSwosContext(m_players[i]);
        sanitizeJobOffersForContext(m_players[i], i);
    }

    loadSwosContext(m_players[activeIdx], true);
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

        if (m_activePlayerIndex != 0) {
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

    syncPlayersWithSelectedTeams(true);
    for (int i = 0; i < m_numPlayers; ++i) {
        if (m_players[i].teamId != 0) {
            TeamControls desiredControls = (m_players[i].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            updateExistingTmdFileEntry(static_cast<uint16_t>(m_players[i].teamId),
                                       m_players[i].careerTeam, desiredControls);
        }
    }

    {
        loadSwosContext(survivorPreCtx[0], true);
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = -1;
        save68kRegisters();
        ::cseg_91428();
        restore68kRegisters();
    }

    for (const auto& dep : departedContexts) {
        packDepartedTeamIntoTmdFile(dep);
    }

    int survivorCount = static_cast<int>(survivorPreCtx.size());
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
    ensureHotseatTeamControlsInSelectedTeams();

    m_pendingSeasonTransitions = survivorPlans;

    loadSwosContext(survivorPreCtx[0], false);
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = -1;
    if (survivorPlans[0].switched) {
        int32_t startBal = computeJobOfferStartingBalance(survivorPlans[0].acceptedFunds);
        uint16_t numTmdCounter = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6E00]);
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6E00]) = static_cast<uint16_t>(numTmdCounter + 1);
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]) = 0;
        loadSwitchedClubVia5DB56(survivorPlans[0].newTeamId, startBal);
        reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam])->teamControls =
            (survivorPreCtx[0].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
    } else {
        save68kRegisters();
        ::cseg_943FA();
        restore68kRegisters();
    }

    resetNonHotseatTeamControlsInTmdFile();

    save68kRegisters();
    ::InitializeNewSeason();
    restore68kRegisters();

    if (m_numPlayers == 1) {
        m_pendingSeasonTransitions.clear();
        m_active = false;
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
}
