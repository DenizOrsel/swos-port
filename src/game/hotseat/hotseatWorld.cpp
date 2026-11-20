#include "hotseatInternal.h"

using namespace HotseatInternal;

HotseatCareerManager& HotseatCareerManager::get()
{
    static HotseatCareerManager instance;
    return instance;
}

void HotseatCareerManager::ensurePlayerSlots(int count)
{
    if (count < 0)
        count = 0;
    m_players.resize(static_cast<size_t>(count));
    m_endSeasonEliminated.resize(static_cast<size_t>(count), 0);
    m_vmTeamMirrorOfs.resize(static_cast<size_t>(count), 0);
    m_h2hPackMaps.resize(static_cast<size_t>(count));
}

void HotseatCareerManager::reset()
{
    if (m_vmPoolPlayerScratchOfs)
        std::memset(&SwosVM::g_memByte[m_vmPoolPlayerScratchOfs], 0, 60);
    for (uint32_t ofs : m_vmTeamMirrorOfs) {
        if (ofs)
            std::memset(&SwosVM::g_memByte[ofs], 0, kCareerTeamBufferSize);
    }

    m_active = false;
    m_numPlayers = 0;
    m_activePlayerIndex = 0;
    m_nextOfferId = 1;
    m_roundCalendarIndex = 0;
    m_pendingPostMatchCalOfs = -1;
    m_pendingPostMatchSlotIdx = -1;
    m_pendingOfferPrompt = false;
    m_inPlayMatchFlow = false;
    m_initializingSeason = false;
    m_preserveEuroCupsOnSeasonInit = false;
    m_inEndSeasonCompute = false;
    m_endSeasonPendingInit = false;
    m_endSeasonActive = false;
    m_endSeasonPlayerIndex = 0;
    m_endSeasonEliminated.clear();
    m_claimedJobOfferTeams.clear();
    m_pendingSeasonTransitions.clear();
    m_transferOffers.clear();
    m_calendarSlots.clear();
    resetEuroCups();
    m_players.clear();
    m_vmTeamMirrorOfs.clear();
    m_h2hPackMaps.clear();
    m_isWizardActive = false;
    m_wizardCurrentPlayer = 0;
    m_wizardTotalPlayers = 1;
    m_wizardAdvanced = false;
    m_wizardPlayers.clear();
    m_isSelectingCareerTeams = false;
    m_careerNumPlayers = 1;
    m_assignedTeamSlots.clear();
    m_assignedSlotInfos.clear();
    m_prevHomeMarketCount = 0;
    m_prevForeignMarketCount = 0;
    m_emergencyLoanDepth = 0;
    m_playMatchSetupStack.clear();
    m_hasActiveWorld = false;
    m_activeWorldKey = {};
    m_hasLeagueOverlay = false;
    m_leagueOverlayKey = {};
    m_globalWeek = 0;
    m_countryWorlds.clear();
    m_leagueWorlds.clear();
    m_clubTeamCache.clear();
}

void HotseatCareerManager::initializeHotseat(const std::vector<int16_t>& teamIds)
{
    reset();
    m_numPlayers = std::min(static_cast<int>(teamIds.size()), kMaxHotseatPlayers);
    if (m_numPlayers < 1) {
        m_active = false;
        return;
    }

    ensurePlayerSlots(m_numPlayers);
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

    // Clear stale purple highlight pointers from any previous session before calendar initialization
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174576]) = -1;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174578]) = 0;
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457A]) = 0;
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457E]) = 0;
}

void HotseatCareerManager::saveCurrentSwosContext(HotseatPlayerContext& ctx)
{
    int16_t prevTeamId = ctx.teamId;
    int16_t vmTeamId = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    if (vmTeamId != 0 || ctx.teamId == 0)
        ctx.teamId = vmTeamId;
    if (vmTeamId != 0 && (ctx.teamName.empty() || (prevTeamId != 0 && vmTeamId != prevTeamId))) {
        const auto *tf = reinterpret_cast<const TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
        if (*reinterpret_cast<const int16_t *>(tf) == vmTeamId && tf->teamName[0] != '\0') {
            char nameBuf[18] = {0};
            std::memcpy(nameBuf, tf->teamName, sizeof(tf->teamName));
            std::string tname(nameBuf);
            trimTrailingSwosPadding(tname);
            if (!tname.empty())
                ctx.teamName = tname;
        }
    }
    ctx.coachOrPlayer = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kCoachOrPlayer]);
    ctx.careerTeamLeague = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]);
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

    ctx.stayingInOldClub = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kStayingInOldClub]);
    ctx.dsegD6B54 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B54]);
    ctx.dsegD6B56 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B56]);
    ctx.dsegD6B5A = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B5A]);
    ctx.dsegD6B5C = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B5C]);
    ctx.dsegD6B62 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B62]);
    ctx.dsegD6B64 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B64]);
    ctx.dsegD6B94 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B94]);
    ctx.dsegD6B88 = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B88]);
    ctx.dsegD6B9A = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B9A]);
    ctx.dsegD6B9E = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B9E]);
    ctx.dsegD6BA2 = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6BA2]);
    std::memcpy(ctx.playerWages, &SwosVM::g_memByte[CareerOffsets::kDsegD6BA6], sizeof(ctx.playerWages));
    std::memcpy(ctx.playerTrialWeeks, &SwosVM::g_memByte[CareerOffsets::kDsegD6C26], sizeof(ctx.playerTrialWeeks));
    ctx.dsegD6C46 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C46]);
    ctx.additionalInvestments = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kAdditionalInvestments]);
    ctx.dsegD6C4E = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C4E]);
    ctx.dsegD6C50 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C50]);

    ctx.youreSacked = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kYoureSacked]);
    ctx.retired = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]);
    ctx.clubBusinessActive = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kClubBusinessActive]);
    ctx.numNewJobOffers = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]);
    ctx.numJobOffers = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]);
    ctx.teamNoOfferAccepted = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]);
    ctx.teamFundsAccepted = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]);
    ctx.isNationalSelector = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kIsNationalSelector]);
    bool preserveCrossWorldIdentity =
        (m_hasActiveWorld && m_leagueWorlds.size() > 1 && worldKeyForPlayer(ctx) != m_activeWorldKey);
    if (!preserveCrossWorldIdentity) {
        ctx.dsegD6CDC = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]);
    }
    ctx.numOffers = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumOffers]);
    ctx.timeToNegotiate = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTimeToNegotiate]);
    ctx.dsegD6D0A = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0A]);
    ctx.dsegD6D0C = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0C]);
    ctx.dsegD6D0E = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0E]);
    ctx.dsegD6D10 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D10]);
    ctx.dsegD6D12 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D12]);

    ctx.gamesPlayed = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGamesPlayed]);
    ctx.numWins = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumWins]);
    ctx.numDrawn = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumDrawn]);
    ctx.numLost = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumLost]);
    ctx.goalsScored = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGoalsScored]);
    ctx.goalsTaken = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGoalsTaken]);
    std::memcpy(ctx.seasonStatsBlock, &SwosVM::g_memByte[CareerOffsets::kDsegD6C88], sizeof(ctx.seasonStatsBlock));
    ctx.dsegD6CA0 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA0]);
    ctx.dsegD6CA2 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]);
    ctx.dsegD7E66 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD7E66]);

    ctx.currentSeasonShowing = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
    ctx.currentSeasonNumber = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]);
    ctx.seasonPlaying = SwosVM::g_memByte[CareerOffsets::kSeasonPlaying];
    if (!preserveCrossWorldIdentity) {
        ctx.dsegE0048 = SwosVM::g_memByte[CareerOffsets::kDsegE0048];
    }
    ctx.playMatchBlink = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDF4F4]);
    ctx.careerNumPlayers = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    std::memcpy(ctx.seasonFinancesBlock, &SwosVM::g_memByte[CareerOffsets::kSeasonFinancesBlock], kSeasonFinancesBlockSize);
    ctx.numPlayersOnTransferMarket = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]);
    std::memcpy(ctx.playersOnTransferMarket, &SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket], kListedPlayersSize);
    std::memcpy(ctx.dsegD6F26Block, &SwosVM::g_memByte[CareerOffsets::kDsegD6F26Block], kListedPlayersSize);
    std::memcpy(ctx.dsegD6DBA, &SwosVM::g_memByte[CareerOffsets::kDsegD6DBA], kTransferAiOffersSize);
    std::memcpy(ctx.newJobOffers, &SwosVM::g_memByte[CareerOffsets::kNewJobOffers], kJobOffersBufferSize);
    std::memcpy(ctx.jobOffers, &SwosVM::g_memByte[CareerOffsets::kJobOffers], kJobOffersBufferSize);

    std::memcpy(ctx.currentMatchPlayers, &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers], kCurrentMatchPlayersSize);
    std::memcpy(ctx.careerTeam, &SwosVM::g_memByte[CareerOffsets::kCareerTeam], kCareerTeamBufferSize);
    std::memcpy(ctx.careerHistory, &SwosVM::g_memByte[CareerOffsets::kCareerHistory], kCareerHistoryBufferSize);
    std::memcpy(ctx.userTactics, &SwosVM::g_memByte[CareerOffsets::kUserTacticsStart], 6 * kUserTacticsSize);

    ctx.dsegD761A = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761A]);
    ctx.dsegD761C = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761C]);
    ctx.dsegD8B6A = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8B6A]);
    std::memcpy(ctx.trainingMatchSavedPlayers, &SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers], kCurrentMatchPlayersSize);
    std::memcpy(ctx.dsegD8B8C, &SwosVM::g_memByte[CareerOffsets::kDsegD8B8C], kCurrentMatchPlayersSize);
    ctx.dsegD8BAC = SwosVM::g_memByte[CareerOffsets::kDsegD8BAC];
    ctx.dsegD8BAE = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8BAE]);
    ctx.dsegD8BB0 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8BB0]);
    std::memcpy(ctx.lastMatchPlayers, &SwosVM::g_memByte[CareerOffsets::kLastMatchPlayers], kCurrentMatchPlayersSize);
    std::memcpy(ctx.dsegD8BD2, &SwosVM::g_memByte[CareerOffsets::kDsegD8BD2], kCurrentMatchPlayersSize);
    ctx.currentTactics = SwosVM::g_memByte[CareerOffsets::kCurrentTactics];
    ctx.dsegD8BF3 = SwosVM::g_memByte[CareerOffsets::kDsegD8BF3];

    syncTrainingMatchSavedPlayers(ctx.trainingMatchSavedPlayers, ctx.dsegD8B8C, ctx.careerTeam,
                                  ctx.careerNumPlayers, ctx.currentMatchPlayers);
}

void HotseatCareerManager::loadSwosContext(const HotseatPlayerContext& ctx, bool relink /* = true */)
{
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]) = ctx.teamId;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kCoachOrPlayer]) = ctx.coachOrPlayer;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]) = ctx.careerTeamLeague;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]) = ctx.mrOrMs;
    SwosVM::g_memByte[CareerOffsets::kPlNationality] = ctx.nationality;

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], 0, 5);
    const char *sexStr = (ctx.mrOrMs != 0) ? "MS" : "MR";
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], sexStr, 2);

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

    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kStayingInOldClub]) = ctx.stayingInOldClub;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B54]) = ctx.dsegD6B54;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B56]) = ctx.dsegD6B56;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B5A]) = ctx.dsegD6B5A;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B5C]) = ctx.dsegD6B5C;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B62]) = ctx.dsegD6B62;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B64]) = ctx.dsegD6B64;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B94]) = ctx.dsegD6B94;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B88]) = ctx.dsegD6B88;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B9A]) = ctx.dsegD6B9A;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B9E]) = ctx.dsegD6B9E;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6BA2]) = ctx.dsegD6BA2;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6BA6], ctx.playerWages, sizeof(ctx.playerWages));
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6C26], ctx.playerTrialWeeks, sizeof(ctx.playerTrialWeeks));
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C46]) = ctx.dsegD6C46;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kAdditionalInvestments]) = ctx.additionalInvestments;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C4E]) = ctx.dsegD6C4E;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C50]) = ctx.dsegD6C50;

    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kYoureSacked]) = ctx.youreSacked;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]) = ctx.retired;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kClubBusinessActive]) = ctx.clubBusinessActive;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]) = ctx.numNewJobOffers;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]) = ctx.numJobOffers;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = ctx.teamNoOfferAccepted;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = ctx.teamFundsAccepted;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kIsNationalSelector]) = ctx.isNationalSelector;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]) = ctx.dsegD6CDC;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumOffers]) = ctx.numOffers;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTimeToNegotiate]) = ctx.timeToNegotiate;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0A]) = ctx.dsegD6D0A;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0C]) = ctx.dsegD6D0C;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0E]) = ctx.dsegD6D0E;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D10]) = ctx.dsegD6D10;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D12]) = ctx.dsegD6D12;

    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGamesPlayed]) = ctx.gamesPlayed;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumWins]) = ctx.numWins;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumDrawn]) = ctx.numDrawn;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumLost]) = ctx.numLost;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGoalsScored]) = ctx.goalsScored;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGoalsTaken]) = ctx.goalsTaken;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6C88], ctx.seasonStatsBlock, sizeof(ctx.seasonStatsBlock));
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA0]) = ctx.dsegD6CA0;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]) = ctx.dsegD6CA2;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD7E66]) = ctx.dsegD7E66;

    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]) = ctx.currentSeasonShowing;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]) = ctx.currentSeasonNumber;
    SwosVM::g_memByte[CareerOffsets::kSeasonPlaying] = ctx.seasonPlaying;
    SwosVM::g_memByte[CareerOffsets::kDsegE0048] = ctx.dsegE0048;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDF4F4]) = ctx.playMatchBlink;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]) = ctx.careerNumPlayers;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kSeasonFinancesBlock], ctx.seasonFinancesBlock, kSeasonFinancesBlockSize);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]) = ctx.numPlayersOnTransferMarket;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket], ctx.playersOnTransferMarket, kListedPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6F26Block], ctx.dsegD6F26Block, kListedPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6DBA], ctx.dsegD6DBA, kTransferAiOffersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kNewJobOffers], ctx.newJobOffers, kJobOffersBufferSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kJobOffers], ctx.jobOffers, kJobOffersBufferSize);

    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers], ctx.currentMatchPlayers, kCurrentMatchPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], ctx.careerTeam, kCareerTeamBufferSize);
    if (m_active && ctx.teamId != 0) {
        reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam])->teamControls =
            (ctx.coachOrPlayer == 1) ? kPlayerCoach : kCoach;
    }
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCareerHistory], ctx.careerHistory, kCareerHistoryBufferSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kUserTacticsStart], ctx.userTactics, 6 * kUserTacticsSize);

    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761A]) = ctx.dsegD761A;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761C]) = ctx.dsegD761C;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8B6A]) = ctx.dsegD8B6A;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers], ctx.trainingMatchSavedPlayers, kCurrentMatchPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD8B8C], ctx.dsegD8B8C, kCurrentMatchPlayersSize);
    SwosVM::g_memByte[CareerOffsets::kDsegD8BAC] = ctx.dsegD8BAC;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8BAE]) = ctx.dsegD8BAE;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8BB0]) = ctx.dsegD8BB0;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kLastMatchPlayers], ctx.lastMatchPlayers, kCurrentMatchPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD8BD2], ctx.dsegD8BD2, kCurrentMatchPlayersSize);
    SwosVM::g_memByte[CareerOffsets::kCurrentTactics] = ctx.currentTactics;
    SwosVM::g_memByte[CareerOffsets::kDsegD8BF3] = ctx.dsegD8BF3;

    syncTrainingMatchSavedPlayers(&SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers],
                                  &SwosVM::g_memByte[CareerOffsets::kDsegD8B8C],
                                  &SwosVM::g_memByte[CareerOffsets::kCareerTeam],
                                  ctx.careerNumPlayers,
                                  &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers]);

    if (m_numPlayers > 1 && anyEuroCupActive() && ctx.teamId != 0) {
        activateEuroCup(ctx.euroCupIndex, false);
    }

    if (relink) {
        relinkCompetitionTeamPointers();
    }
}

void HotseatCareerManager::ensureVmTrainingMatchSavedPlayersSynced()
{
    uint16_t count = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    syncTrainingMatchSavedPlayers(&SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers],
                                  &SwosVM::g_memByte[CareerOffsets::kDsegD8B8C],
                                  &SwosVM::g_memByte[CareerOffsets::kCareerTeam],
                                  count,
                                  &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers]);
}

void HotseatCareerManager::syncPlayersWithSelectedTeams(bool writeToSelected)
{
    if (!isActive() || !swos.selTeamsPtr || swos.g_numSelectedTeams <= 0)
        return;

    if (writeToSelected) {
        unpackAllH2hPackedTeams();
    }

    auto *selTeams = reinterpret_cast<TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return;

    int count = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
    for (int i = 0; i < m_numPlayers; ++i) {
        uint16_t targetId = static_cast<uint16_t>(m_players[i].teamId);
        if (targetId == 0)
            continue;

        for (int t = 0; t < count; ++t) {
            uint16_t slotTeamId = *reinterpret_cast<const uint16_t *>(&selTeams[t]);
            if (slotTeamId != targetId)
                continue;

            TeamControls desiredControls = (m_players[i].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            if (writeToSelected) {
                reinterpret_cast<TeamFile *>(m_players[i].careerTeam)->teamControls = desiredControls;
                std::memcpy(&selTeams[t], m_players[i].careerTeam, sizeof(TeamFile));
            } else {
                if (m_hasActiveWorld && worldKeyForPlayer(m_players[i]) != m_activeWorldKey) {
                    selTeams[t].teamControls = desiredControls;
                    break;
                }
                if (i != m_activePlayerIndex) {
                    std::memcpy(m_players[i].careerTeam, &selTeams[t], sizeof(TeamFile));
                    m_players[i].dsegD6CDC = selTeams[t].league;
                    if (m_leagueWorlds.size() <= 1) {
                        m_players[i].careerTeamLeague = selTeams[t].league;
                    }
                    reinterpret_cast<TeamFile *>(m_players[i].careerTeam)->teamControls = desiredControls;
                } else {
                    selTeams[t].teamControls = desiredControls;
                }
            }
            break;
        }
    }
}

void HotseatCareerManager::ensureHotseatTeamControlsInSelectedTeams()
{
    if (!isActive() || !swos.selTeamsPtr || swos.g_numSelectedTeams <= 0)
        return;

    auto *selTeams = reinterpret_cast<TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return;

    int count = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
    for (int t = 0; t < count; ++t) {
        int16_t tid = *reinterpret_cast<const int16_t *>(&selTeams[t]);
        int pIdx = findHotseatPlayerByTeamId(tid);
        if (pIdx >= 0) {
            selTeams[t].teamControls = (m_players[pIdx].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
        } else if (selTeams[t].teamControls == kCoach || selTeams[t].teamControls == kPlayerCoach) {
            selTeams[t].teamControls = kComputerTeam;
        }
    }
}

void HotseatCareerManager::resetNonHotseatTeamControlsInTmdFile()
{
    if (!isActive())
        return;

    int maxTmdTeams = (static_cast<int>(CareerOffsets::kDiyFileBufferCopy) -
                       static_cast<int>(CareerOffsets::kTmdFileBuffer)) /
                      static_cast<int>(sizeof(TeamFile));
    int tmdCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTmdCareerCount]);
    tmdCount = std::clamp(tmdCount, 0, maxTmdTeams);

    auto *tmdTeams = reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kTmdFileBuffer]);
    for (int i = 0; i < tmdCount; ++i) {
        int16_t tid = *reinterpret_cast<const int16_t *>(&tmdTeams[i]);
        int pIdx = findHotseatPlayerByTeamId(tid);
        if (pIdx >= 0) {
            tmdTeams[i].teamControls = (m_players[pIdx].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
        } else if (tmdTeams[i].teamControls == kCoach || tmdTeams[i].teamControls == kPlayerCoach) {
            tmdTeams[i].teamControls = kComputerTeam;
        }
    }
}

LeagueWorldKey HotseatCareerManager::worldKeyForPlayer(const HotseatPlayerContext& ctx)
{
    LeagueWorldKey key;
    key.countryFileNo = (ctx.isNationalSelector != 0)
        ? uint8_t{0xFF}
        : static_cast<uint8_t>(static_cast<uint16_t>(ctx.teamId) & 0xFFu);
    key.divisionId = static_cast<uint8_t>(ctx.dsegD6CDC);
    return key;
}

void HotseatCareerManager::cacheTeamFile(const TeamFile& tf)
{
    uint16_t tid = *reinterpret_cast<const uint16_t *>(&tf);
    if (tid == 0 || tid == 0xFFFF)
        return;
    TeamFile copy = tf;
    if (copy.teamControls == kCoach || copy.teamControls == kPlayerCoach) {
        copy.teamControls = kComputerTeam;
    }
    m_clubTeamCache[tid] = copy;
}

void HotseatCareerManager::cacheTeamsFromSelectedTeams()
{
    if (!swos.selTeamsPtr || swos.g_numSelectedTeams <= 0)
        return;
    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return;
    int count = std::min<int>(swos.g_numSelectedTeams, kMaxSelectedTeamsCount);
    for (int t = 0; t < count; ++t) {
        cacheTeamFile(selTeams[t]);
    }
}

bool HotseatCareerManager::lookupCachedTeamFile(uint16_t teamId, TeamFile& outTf) const
{
    if (teamId == 0 || teamId == 0xFFFF)
        return false;

    int pIdx = findHotseatPlayerByTeamId(static_cast<int16_t>(teamId));
    if (pIdx >= 0 && pIdx < m_numPlayers) {
        std::memcpy(&outTf, m_players[pIdx].careerTeam, sizeof(TeamFile));
        return true;
    }

    if (swos.selTeamsPtr && swos.g_numSelectedTeams > 0) {
        const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
        if (selTeams) {
            int count = std::min<int>(swos.g_numSelectedTeams, kMaxSelectedTeamsCount);
            for (int t = 0; t < count; ++t) {
                if (*reinterpret_cast<const uint16_t *>(&selTeams[t]) == teamId) {
                    outTf = selTeams[t];
                    return true;
                }
            }
        }
    }

    int maxTmd = (static_cast<int>(CareerOffsets::kDiyFileBufferCopy) -
                  static_cast<int>(CareerOffsets::kTmdFileBuffer)) /
                 static_cast<int>(sizeof(TeamFile));
    int tmdCount = std::clamp<int>(
        *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTmdCareerCount]), 0, maxTmd);
    const auto *tmdTeams = reinterpret_cast<const TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kTmdFileBuffer]);
    for (int t = 0; t < tmdCount; ++t) {
        if (*reinterpret_cast<const uint16_t *>(&tmdTeams[t]) == teamId) {
            outTf = tmdTeams[t];
            return true;
        }
    }

    for (const auto& kv : m_leagueWorlds) {
        const auto& lw = kv.second;
        int count = std::min<int>(lw.numSelectedTeams, static_cast<int>(lw.selectedTeams.size() / sizeof(TeamFile)));
        const auto *teams = reinterpret_cast<const TeamFile *>(lw.selectedTeams.data());
        for (int t = 0; t < count; ++t) {
            if (*reinterpret_cast<const uint16_t *>(&teams[t]) == teamId) {
                outTf = teams[t];
                return true;
            }
        }
    }

    auto cacheIt = m_clubTeamCache.find(teamId);
    if (cacheIt != m_clubTeamCache.end()) {
        outTf = cacheIt->second;
        return true;
    }
    return false;
}

void HotseatCareerManager::ensureCupTeamsInSelectedTeams()
{
    if (!isActive() || m_leagueWorlds.size() <= 1 || !swos.selTeamsPtr)
        return;

    auto *selTeams = reinterpret_cast<TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return;

    uint16_t activeTeamWord = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    auto hasTeamInSel = [&](uint16_t tid) -> bool {
        int count = std::clamp<int>(swos.g_numSelectedTeams, 0, kMaxSelectedTeamsCount);
        for (int t = 0; t < count; ++t) {
            if (*reinterpret_cast<const uint16_t *>(&selTeams[t]) == tid)
                return true;
        }
        return false;
    };

    auto addTeamIfMissing = [&](uint16_t tid) {
        if (swos.g_numSelectedTeams >= kMaxSelectedTeamsCount)
            return;
        if (tid == 0 || tid == 0xFFFF || tid == activeTeamWord)
            return;
        if (findHotseatPlayerByTeamId(static_cast<int16_t>(tid)) >= 0)
            return;
        if (hasTeamInSel(tid))
            return;
        TeamFile tf{};
        if (lookupCachedTeamFile(tid, tf)) {
            tf.teamControls = kComputerTeam;
            selTeams[swos.g_numSelectedTeams++] = tf;
        }
    };

    auto scanComp = [&](uint32_t compOffset) {
        if (compOffset < 5000 || compOffset + 100 >= sizeof(SwosVM::g_memByte))
            return;
        uint16_t compType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[compOffset]);
        uint32_t base = compOffset;
        uint16_t blockType = compType;
        if (compType == 2) {
            int16_t tourTeams = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[compOffset + 49]);
            uint32_t tourTableOfs = compOffset + 153;
            if (tourTeams > 0 && tourTeams <= 64 &&
                tourTableOfs + static_cast<uint32_t>(tourTeams) * 2 <= sizeof(SwosVM::g_memByte)) {
                for (int k = 0; k < tourTeams; ++k) {
                    uint16_t tid = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[tourTableOfs + k * 2]);
                    addTeamIfMissing(tid);
                }
            }
            base = compOffset + static_cast<uint32_t>(kTourSubCompOffset);
            if (base + 52 >= sizeof(SwosVM::g_memByte))
                return;
            blockType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[base]);
        }
        if (blockType > 2 || base + 840 >= sizeof(SwosVM::g_memByte))
            return;

        int16_t numTeams = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[base + 49]);
        if (numTeams <= 0 || numTeams > 64)
            return;

        uint32_t tableOfs = (blockType == 0) ? (base + 301) : (base + 589);
        if (tableOfs + static_cast<uint32_t>(numTeams) * 2 > sizeof(SwosVM::g_memByte))
            return;

        for (int k = 0; k < numTeams; ++k) {
            uint16_t tid = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[tableOfs + k * 2]);
            addTeamIfMissing(tid);
        }
    };

    int startComp = m_hasLeagueOverlay ? 0 : 1;
    for (int c = startComp; c < 5; ++c) {
        uint32_t activePtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + c * 4]);
        uint32_t bufPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + c * 4]);
        if (activePtr != 0 && bufPtr >= 5000 && bufPtr < sizeof(SwosVM::g_memByte)) {
            scanComp(bufPtr);
        }
    }
}

void HotseatCareerManager::applyActivePlayerLeagueOverlayIfNeeded()
{
    if (!m_hasActiveWorld || m_leagueWorlds.size() <= 1 ||
        m_activePlayerIndex < 0 || m_activePlayerIndex >= m_numPlayers) {
        return;
    }

    LeagueWorldKey ownerKey = worldKeyForPlayer(m_players[m_activePlayerIndex]);
    if (ownerKey == m_activeWorldKey)
        return;

    auto ownerIt = m_leagueWorlds.find(ownerKey);
    if (ownerIt == m_leagueWorlds.end() || !ownerIt->second.initialized)
        return;

    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer],
                ownerIt->second.competitionFileBuffer,
                kCompetitionBufferSize);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]) = ownerIt->second.dsegD6CA2;
    m_hasLeagueOverlay = true;
    m_leagueOverlayKey = ownerKey;
}

void HotseatCareerManager::saveActiveWorldFromVm()
{
    if (!m_hasActiveWorld)
        return;

    auto& cw = m_countryWorlds[m_activeWorldKey.countryFileNo];
    cw.countryFileNo = m_activeWorldKey.countryFileNo;
    cw.initialized = true;
    cw.dsegD8CAE = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 4]);
    cw.dsegD8CB2 = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]);
    cw.dsegD6CDE = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDE]);
    std::memcpy(cw.cup1, &SwosVM::g_memByte[CareerOffsets::kDsegD8D0A], kCup1BufferSize);
    std::memcpy(cw.cup2, &SwosVM::g_memByte[CareerOffsets::kDsegD985C], kCup2BufferSize);

    uint8_t marketCountry = (m_hasLeagueOverlay && m_countryWorlds.count(m_leagueOverlayKey.countryFileNo) != 0)
        ? m_leagueOverlayKey.countryFileNo
        : m_activeWorldKey.countryFileNo;
    auto& mcw = m_countryWorlds[marketCountry];
    mcw.countryFileNo = marketCountry;
    mcw.initialized = true;
    mcw.numHomeMarketPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]);
    std::memcpy(mcw.homeMarketPlayers, &SwosVM::g_memByte[CareerOffsets::kHomeMarketPlayers], sizeof(mcw.homeMarketPlayers));
    mcw.numForeignMarketPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]);
    std::memcpy(mcw.foreignMarketPlayers, &SwosVM::g_memByte[CareerOffsets::kForeignMarketPlayers], sizeof(mcw.foreignMarketPlayers));

    auto& lw = m_leagueWorlds[m_activeWorldKey];
    lw.key = m_activeWorldKey;
    lw.initialized = true;
    if (m_hasLeagueOverlay) {
        std::memcpy(&lw.dsegD8CAA[1], &SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 4], 4 * sizeof(uint32_t));
        auto ovIt = m_leagueWorlds.find(m_leagueOverlayKey);
        if (ovIt != m_leagueWorlds.end() && ovIt->second.initialized) {
            std::memcpy(ovIt->second.competitionFileBuffer,
                        &SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer],
                        kCompetitionBufferSize);
            ovIt->second.dsegD6CA2 = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]);
        }
    } else {
        std::memcpy(lw.dsegD8CAA, &SwosVM::g_memByte[CareerOffsets::kDsegD8CAA], sizeof(lw.dsegD8CAA));
        std::memcpy(lw.competitionFileBuffer, &SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer], kCompetitionBufferSize);
        lw.dsegD6CA2 = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]);
    }
    lw.dsegD8CC0 = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CC0]);
    std::memcpy(lw.dsegD9C9F, &SwosVM::g_memByte[CareerOffsets::kDsegD9C9F], kCup4BufferSize);
    std::memcpy(lw.calendar, &SwosVM::g_memByte[CareerOffsets::kDsegDB3E6], kCalendarBufferSize);
    lw.calStepIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);
    lw.calByteOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
    lw.calTotalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    lw.dsegDB3E4 = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E4]);
    lw.playMatchBlink = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDF4F4]);
    lw.dsegE0048 = SwosVM::g_memByte[CareerOffsets::kDsegE0048];
    lw.seasonPlaying = SwosVM::g_memByte[CareerOffsets::kSeasonPlaying];
    lw.currentSeasonShowing = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
    lw.currentSeasonNumber = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]);
    lw.seasonFinished = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
    lw.careerFinished = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]);
    lw.hlIdx = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174576]);
    lw.hlSlotIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174578]);
    lw.hlHdr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457A]);
    lw.hlMatchPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457E]);
    lw.roundCalendarIndex = m_roundCalendarIndex;
    lw.pendingPostMatchCalOfs = m_pendingPostMatchCalOfs;
    lw.pendingPostMatchSlotIdx = m_pendingPostMatchSlotIdx;
    lw.calendarSlots = m_calendarSlots;
    lw.euroCupByCalOfs = m_euroCupByCalOfs;

    if (!m_hasLeagueOverlay) {
        int16_t count = static_cast<int16_t>(std::clamp<int>(swos.g_numSelectedTeams, 0, kMaxSelectedTeamsCount));
        lw.numSelectedTeams = count;
        size_t selBytes = static_cast<size_t>(count) * sizeof(TeamFile);
        lw.selectedTeams.resize(selBytes);
        if (selBytes > 0) {
            std::memcpy(lw.selectedTeams.data(), swos.g_selectedTeams, selBytes);
        }
    }
}

void HotseatCareerManager::loadWorldToVm(const LeagueWorldKey& targetKey, bool relink /* = true */)
{
    m_hasLeagueOverlay = false;
    m_leagueOverlayKey = {};
    auto lwIt = m_leagueWorlds.find(targetKey);
    if (lwIt == m_leagueWorlds.end() || !lwIt->second.initialized) {
        m_activeWorldKey = targetKey;
        m_hasActiveWorld = true;
        return;
    }

    const auto& lw = lwIt->second;

    // Ensure standard arrayOfPointers & selTeamsPtr are valid in VM
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + 0]) =
        static_cast<uint32_t>(CareerOffsets::kCompetitionFileBuffer);
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + 4]) =
        static_cast<uint32_t>(CareerOffsets::kDsegD8D0A);
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + 8]) =
        static_cast<uint32_t>(CareerOffsets::kDsegD985C);
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + 12]) =
        static_cast<uint32_t>(CareerOffsets::kDsegDA0E2);
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + 16]) =
        static_cast<uint32_t>(CareerOffsets::kDsegD9C9F);
    swos.selTeamsPtr = 131759;

    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA], lw.dsegD8CAA, sizeof(lw.dsegD8CAA));
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CC0]) = lw.dsegD8CC0;

    auto cwIt = m_countryWorlds.find(targetKey.countryFileNo);
    if (cwIt != m_countryWorlds.end() && cwIt->second.initialized) {
        const auto& cw = cwIt->second;
        *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 4]) = cw.dsegD8CAE;
        *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]) = cw.dsegD8CB2;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDE]) = cw.dsegD6CDE;
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD8D0A], cw.cup1, kCup1BufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], cw.cup2, kCup2BufferSize);
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]) = cw.numHomeMarketPlayers;
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kHomeMarketPlayers], cw.homeMarketPlayers, sizeof(cw.homeMarketPlayers));
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]) = cw.numForeignMarketPlayers;
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kForeignMarketPlayers], cw.foreignMarketPlayers, sizeof(cw.foreignMarketPlayers));
    }

    if (targetKey.countryFileNo != 0xFF) {
        SwosVM::g_memByte[CareerOffsets::kCompCountryNumber] = targetKey.countryFileNo;
        SwosVM::g_memByte[CareerOffsets::kDsegD6CB8] = targetKey.countryFileNo;
    }
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]) = targetKey.divisionId;

    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCompetitionFileBuffer], lw.competitionFileBuffer, kCompetitionBufferSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD9C9F], lw.dsegD9C9F, kCup4BufferSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E6], lw.calendar, kCalendarBufferSize);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]) = lw.calStepIdx;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]) = lw.calByteOfs;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]) = lw.calTotalEntries;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E4]) = lw.dsegDB3E4;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDF4F4]) = lw.playMatchBlink;
    SwosVM::g_memByte[CareerOffsets::kDsegE0048] = lw.dsegE0048;
    SwosVM::g_memByte[CareerOffsets::kSeasonPlaying] = lw.seasonPlaying;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]) = lw.currentSeasonShowing;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]) = lw.currentSeasonNumber;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]) = lw.dsegD6CA2;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = lw.seasonFinished;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = lw.careerFinished;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174576]) = lw.hlIdx;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174578]) = lw.hlSlotIdx;
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457A]) = lw.hlHdr;
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457E]) = lw.hlMatchPtr;

    m_roundCalendarIndex = lw.roundCalendarIndex;
    m_pendingPostMatchCalOfs = lw.pendingPostMatchCalOfs;
    m_pendingPostMatchSlotIdx = lw.pendingPostMatchSlotIdx;
    m_calendarSlots = lw.calendarSlots;
    m_euroCupByCalOfs = lw.euroCupByCalOfs;

    int16_t count = static_cast<int16_t>(std::clamp<int>(lw.numSelectedTeams, 0, kMaxSelectedTeamsCount));
    swos.g_numSelectedTeams = count;
    std::memset(swos.g_selectedTeams, 0, kSelectedTeamsBufferSize);
    size_t copyBytes = std::min(lw.selectedTeams.size(), kSelectedTeamsBufferSize);
    if (copyBytes > 0) {
        std::memcpy(swos.g_selectedTeams, lw.selectedTeams.data(), copyBytes);
    }

    m_activeWorldKey = targetKey;
    m_hasActiveWorld = true;

    if (m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers &&
        worldKeyForPlayer(m_players[m_activePlayerIndex]) == targetKey) {
        auto& active = m_players[m_activePlayerIndex];
        active.dsegE0048 = lw.dsegE0048;
        active.seasonPlaying = lw.seasonPlaying;
        active.currentSeasonShowing = lw.currentSeasonShowing;
        active.currentSeasonNumber = lw.currentSeasonNumber;
        active.dsegD6CA2 = lw.dsegD6CA2;
        active.playMatchBlink = lw.playMatchBlink;
        active.dsegD6CDC = targetKey.divisionId;
    }

    if (relink) {
        syncPlayersWithSelectedTeams(true);
        ensureHotseatTeamControlsInSelectedTeams();
        relinkCompetitionTeamPointers();
    }
}

void HotseatCareerManager::captureActiveWorldAsInitial(const LeagueWorldKey& key)
{
    m_activeWorldKey = key;
    m_hasActiveWorld = true;
    cacheTeamsFromSelectedTeams();
    saveActiveWorldFromVm();
}

int32_t HotseatCareerManager::resolveTeamDeltaForCompetition(uint16_t teamWord, uint16_t activeTeamWord, int32_t selTeamsPtrVal, bool& found)
{
    found = false;
    if (teamWord == activeTeamWord) {
        found = true;
        return static_cast<int32_t>(CareerOffsets::kCareerTeam) - selTeamsPtrVal;
    }

    if (selTeamsPtrVal && swos.g_numSelectedTeams > 0) {
        const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(selTeamsPtrVal));
        if (selTeams) {
            int count = std::min<int>(swos.g_numSelectedTeams, kMaxSelectedTeamsCount);
            for (int t = 0; t < count; ++t) {
                uint16_t slotTeamId = *reinterpret_cast<const uint16_t *>(&selTeams[t]);
                if (slotTeamId == teamWord) {
                    found = true;
                    return static_cast<int32_t>(t * sizeof(TeamFile));
                }
            }
        }
    }

    int pIdx = findHotseatPlayerByTeamId(static_cast<int16_t>(teamWord));
    if (pIdx >= 0 && pIdx < m_numPlayers) {
        uint32_t mirrorOfs = ensureHotseatTeamMirror(pIdx);
        if (mirrorOfs != 0) {
            found = true;
            return static_cast<int32_t>(mirrorOfs) - selTeamsPtrVal;
        }
    }

    return 0;
}

static void relinkSingleCompetitionBuffer(HotseatCareerManager& mgr, uint32_t compOffset, uint16_t activeTeamWord, int32_t selTeamsPtrVal)
{
    if (compOffset == 0 || compOffset + 100 >= sizeof(SwosVM::g_memByte))
        return;

    uint16_t compType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[compOffset]);
    auto relinkBlock = [&](uint32_t base, uint16_t blockType) {
        if (base + 840 >= sizeof(SwosVM::g_memByte))
            return;

        int16_t numTeams = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[base + 49]);
        if (numTeams <= 0 || numTeams > 64)
            return;

        if (blockType == 0) {
            if (base + 691 + static_cast<uint32_t>(numTeams) * 18 > sizeof(SwosVM::g_memByte))
                return;
            for (int k = 0; k < numTeams; ++k) {
                uint16_t teamWord = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[base + 301 + k * 2]);
                bool found = false;
                int32_t delta = mgr.resolveTeamDeltaForCompetition(teamWord, activeTeamWord, selTeamsPtrVal, found);
                if (found) {
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[base + 691 + k * 18]) = delta;
                }
            }
        } else if (blockType == 1 || blockType == 2) {
            if (base + 835 + static_cast<uint32_t>(numTeams) * 4 > sizeof(SwosVM::g_memByte))
                return;
            for (int k = 0; k < numTeams; ++k) {
                uint16_t teamWord = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[base + 589 + k * 2]);
                bool found = false;
                int32_t delta = mgr.resolveTeamDeltaForCompetition(teamWord, activeTeamWord, selTeamsPtrVal, found);
                if (found) {
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[base + 835 + k * 4]) = delta;
                }
            }
        }
    };

    if (compType == 0 || compType == 1) {
        relinkBlock(compOffset, compType);
    } else if (compType == 2) {
        uint32_t sub = compOffset + static_cast<uint32_t>(kTourSubCompOffset);
        if (sub + 52 < sizeof(SwosVM::g_memByte)) {
            uint16_t subType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[sub]);
            if (subType <= 2) {
                relinkBlock(sub, subType);
            }
        }
    }
}

void HotseatCareerManager::relinkCompetitionTeamPointers()
{
    if (!isActive() || !swos.selTeamsPtr)
        return;

    if (m_leagueWorlds.size() > 1) {
        ensureCupTeamsInSelectedTeams();
    }

    uint16_t activeTeamWord = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    int32_t selTeamsPtrVal = static_cast<int32_t>(swos.selTeamsPtr);
    if (activeTeamWord == 0 || activeTeamWord == 0xFFFF || selTeamsPtrVal == 0)
        return;

    for (int c = 0; c < 5; ++c) {
        uint32_t activeContestPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + c * 4]);
        uint32_t compBufPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + c * 4]);
        if (activeContestPtr != 0 && compBufPtr >= 5000 && compBufPtr < sizeof(SwosVM::g_memByte)) {
            relinkSingleCompetitionBuffer(*this, compBufPtr, activeTeamWord, selTeamsPtrVal);
        }
    }

    uint16_t diyType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDiyCompetitionStart]);
    if (diyType <= 2) {
        relinkSingleCompetitionBuffer(*this, static_cast<uint32_t>(CareerOffsets::kDiyCompetitionStart), activeTeamWord, selTeamsPtrVal);
    }
    uint16_t tourWorkType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDiyFileBufferCopy]);
    if (tourWorkType == 2) {
        relinkSingleCompetitionBuffer(*this, static_cast<uint32_t>(CareerOffsets::kDiyFileBufferCopy), activeTeamWord, selTeamsPtrVal);
    }
}

void HotseatCareerManager::switchToPlayer(int index, bool relink /* = true */)
{
    if (index < 0 || index >= m_numPlayers)
        return;

    LeagueWorldKey targetKey = worldKeyForPlayer(m_players[index]);
    if (m_hasActiveWorld && (targetKey != m_activeWorldKey || m_hasLeagueOverlay)) {
        if (relink) {
            syncPlayersWithSelectedTeams(true);
        }
        saveActiveWorldFromVm();
        m_activePlayerIndex = index;
        loadWorldToVm(targetKey, false);
        loadSwosContext(m_players[m_activePlayerIndex], false);
        syncPlayersWithSelectedTeams(true);
        if (relink) {
            ensureHotseatTeamControlsInSelectedTeams();
            relinkCompetitionTeamPointers();
        }
        syncTransferMarketPoolsForCurrentPlayer();
        return;
    }

    if (relink) {
        syncPlayersWithSelectedTeams(true);
    }

    m_activePlayerIndex = index;
    loadSwosContext(m_players[m_activePlayerIndex], relink);
    syncTransferMarketPoolsForCurrentPlayer();
}

uint32_t HotseatCareerManager::ensureHotseatTeamMirror(int playerIdx)
{
    if (playerIdx < 0 || playerIdx >= m_numPlayers || playerIdx >= kMaxHotseatPlayers)
        return 0;

    if (playerIdx >= static_cast<int>(m_vmTeamMirrorOfs.size()))
        m_vmTeamMirrorOfs.resize(static_cast<size_t>(m_numPlayers), 0);

    if (m_vmTeamMirrorOfs[playerIdx] == 0) {
        uint32_t extraBase = getHotseatExtraMemoryOffset();
        uint32_t subOfs = kMirrorBaseSubOffset + static_cast<uint32_t>(playerIdx * kCareerTeamBufferSize);
        if (subOfs + kCareerTeamBufferSize > getHotseatExtraMemorySize())
            return 0;
        m_vmTeamMirrorOfs[playerIdx] = extraBase + subOfs;
    }

    uint32_t ofs = m_vmTeamMirrorOfs[playerIdx];
    const uint8_t *src = (playerIdx == m_activePlayerIndex &&
                          getVmSelTeamNumber() == static_cast<uint16_t>(m_players[playerIdx].teamId))
        ? &SwosVM::g_memByte[CareerOffsets::kCareerTeam]
        : m_players[playerIdx].careerTeam;
    std::memcpy(&SwosVM::g_memByte[ofs], src, kCareerTeamBufferSize);
    return ofs;
}

uint32_t HotseatCareerManager::ensurePoolPlayerScratch()
{
    if (m_vmPoolPlayerScratchOfs == 0) {
        m_vmPoolPlayerScratchOfs = getHotseatExtraMemoryOffset() + kPoolScratchSubOffset;
    }
    return m_vmPoolPlayerScratchOfs;
}

void HotseatCareerManager::unpackH2hPackedTeam(int playerIdx)
{
    if (playerIdx < 0 || playerIdx >= static_cast<int>(m_h2hPackMaps.size()) || playerIdx >= m_numPlayers)
        return;

    auto& pm = m_h2hPackMaps[playerIdx];
    if (!pm.active || pm.vmTeamOfs < 1000 || pm.vmTeamOfs + sizeof(TeamFile) > sizeof(SwosVM::g_memByte)) {
        pm.active = false;
        return;
    }

    auto& ctx = m_players[playerIdx];
    const auto *packedTf = reinterpret_cast<const TeamFile *>(&SwosVM::g_memByte[pm.vmTeamOfs]);
    auto *dstTf = reinterpret_cast<TeamFile *>(ctx.careerTeam);

    if (*reinterpret_cast<const uint16_t *>(packedTf) == static_cast<uint16_t>(ctx.teamId)) {
        dstTf->writesZeroHere = packedTf->writesZeroHere;
        dstTf->andWith0xFE = packedTf->andWith0xFE;
        dstTf->tactics = packedTf->tactics;
        dstTf->teamControls = (ctx.coachOrPlayer == 1) ? kPlayerCoach : kCoach;

        for (int slot = 0; slot < kNumPlayersInTeam; ++slot) {
            uint8_t ord = pm.slotToOrd[slot];
            if (ord < 30) {
                std::memcpy(ctx.careerTeam + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize,
                            &packedTf->players[slot],
                            kPlayerFileSize);
            }
        }
    }

    pm = H2hPackedTeamMap{};
}

void HotseatCareerManager::unpackAllH2hPackedTeams()
{
    for (int i = 0; i < static_cast<int>(m_h2hPackMaps.size()); ++i) {
        if (m_h2hPackMaps[i].active) {
            unpackH2hPackedTeam(i);
        }
    }
}

void HotseatCareerManager::onBeforeShowPlayMatchMenu()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer || isVmNationalSelector())
        return;

    auto resolveTeamIdx = [&](uint32_t vmOfs) -> int {
        if (vmOfs == CareerOffsets::kCareerTeam)
            return m_activePlayerIndex;
        if (vmOfs >= 1000 && vmOfs + sizeof(TeamFile) <= sizeof(SwosVM::g_memByte)) {
            int16_t tid = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[vmOfs]);
            return findHotseatPlayerByTeamId(tid);
        }
        return -1;
    };

    uint32_t origA1 = A1.asDword();
    uint32_t origA2 = A2.asDword();
    int idx1 = resolveTeamIdx(origA1);
    int idx2 = resolveTeamIdx(origA2);

    PlayMatchSetupState st;
    st.savedActiveIdx = m_activePlayerIndex;
    st.team1HotseatIdx = idx1;
    st.origA1 = origA1;
    st.origA2 = origA2;
    st.didSwitch = false;

    if (idx1 >= 0 && idx1 < m_numPlayers && idx1 != m_activePlayerIndex) {
        unpackH2hPackedTeam(idx1);
        saveCurrentSwosContext(m_players[m_activePlayerIndex]);

        uint32_t newA2 = origA2;
        if (idx2 >= 0 && idx2 < m_numPlayers && idx2 != idx1 && origA2 == CareerOffsets::kCareerTeam) {
            newA2 = ensureHotseatTeamMirror(idx2);
        }

        m_activePlayerIndex = idx1;
        loadSwosContext(m_players[idx1], false);
        A1 = static_cast<uint32_t>(CareerOffsets::kCareerTeam);
        A2 = newA2;
        st.didSwitch = true;
    } else if (idx1 == m_activePlayerIndex && idx1 >= 0 && origA1 != CareerOffsets::kCareerTeam) {
        A1 = static_cast<uint32_t>(CareerOffsets::kCareerTeam);
    }

    if (idx2 >= 0 && idx2 < m_numPlayers && idx2 != idx1) {
        ensureHotseatTeamMirror(idx2);
    }

    m_playMatchSetupStack.push_back(st);
}

void HotseatCareerManager::onAfterShowPlayMatchMenu()
{
    if (m_playMatchSetupStack.empty())
        return;

    uint32_t savedRetD0 = D0;
    save68kRegisters();

    PlayMatchSetupState st = m_playMatchSetupStack.back();
    m_playMatchSetupStack.pop_back();

    if (st.team1HotseatIdx >= 0 && st.team1HotseatIdx < m_numPlayers && m_activePlayerIndex == st.team1HotseatIdx) {
        saveCurrentSwosContext(m_players[st.team1HotseatIdx]);

        if (st.origA1 != CareerOffsets::kCareerTeam &&
            st.origA1 >= 1000 && st.origA1 + sizeof(TeamFile) <= sizeof(SwosVM::g_memByte)) {
            const auto& ctx = m_players[st.team1HotseatIdx];
            auto *packedTf = reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[st.origA1]);
            std::memcpy(packedTf, ctx.careerTeam, kTeamFileHeaderSize);
            packedTf->teamControls = (ctx.coachOrPlayer == 1) ? kPlayerCoach : kCoach;

            if (st.team1HotseatIdx >= static_cast<int>(m_h2hPackMaps.size()))
                m_h2hPackMaps.resize(static_cast<size_t>(m_numPlayers));
            auto& pm = m_h2hPackMaps[st.team1HotseatIdx];
            pm = H2hPackedTeamMap{};
            pm.active = true;
            pm.vmTeamOfs = st.origA1;
            for (int slot = 0; slot < kNumPlayersInTeam; ++slot) {
                uint8_t ord = ctx.currentMatchPlayers[slot];
                if (ord >= 30)
                    ord = static_cast<uint8_t>(slot);
                std::memcpy(&packedTf->players[slot],
                            ctx.careerTeam + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize,
                            kPlayerFileSize);
                packedTf->playerNumbers[slot] = static_cast<byte>(slot);
                pm.slotToOrd[slot] = ord;
            }
        }
    }

    if (st.didSwitch && st.savedActiveIdx >= 0 && st.savedActiveIdx < m_numPlayers) {
        m_activePlayerIndex = st.savedActiveIdx;
        loadSwosContext(m_players[st.savedActiveIdx], false);
    }

    swos.playMatchTeam1Ptr.setRaw(st.origA1);
    swos.playMatchTeam2Ptr.setRaw(st.origA2);

    if (swos.g_exitGameFlag != 0) {
        unpackAllH2hPackedTeams();
        syncPlayersWithSelectedTeams(true);
    }

    restore68kRegisters();
    D0 = savedRetD0;
}
