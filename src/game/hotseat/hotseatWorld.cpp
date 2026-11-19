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
    ctx.dsegD6CDC = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]);
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
    ctx.dsegE0048 = SwosVM::g_memByte[CareerOffsets::kDsegE0048];
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
                if (i != m_activePlayerIndex) {
                    std::memcpy(m_players[i].careerTeam, &selTeams[t], sizeof(TeamFile));
                    m_players[i].careerTeamLeague = selTeams[t].league;
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

static bool computeTeamCacheDelta(uint16_t teamWord, uint16_t activeTeamWord, int32_t selTeamsPtrVal, int32_t& outDelta)
{
    if (teamWord == activeTeamWord) {
        outDelta = static_cast<int32_t>(CareerOffsets::kCareerTeam) - selTeamsPtrVal;
        return true;
    }

    if (!selTeamsPtrVal || swos.g_numSelectedTeams <= 0)
        return false;

    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(selTeamsPtrVal));
    if (!selTeams)
        return false;

    int count = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
    for (int t = 0; t < count; ++t) {
        uint16_t slotTeamId = *reinterpret_cast<const uint16_t *>(&selTeams[t]);
        if (slotTeamId == teamWord) {
            outDelta = static_cast<int32_t>(t * sizeof(TeamFile));
            return true;
        }
    }
    return false;
}

static void relinkSingleCompetitionBuffer(uint32_t compOffset, uint16_t activeTeamWord, int32_t selTeamsPtrVal)
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
                int32_t delta = 0;
                if (computeTeamCacheDelta(teamWord, activeTeamWord, selTeamsPtrVal, delta)) {
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[base + 691 + k * 18]) = delta;
                }
            }
        } else if (blockType == 1 || blockType == 2) {
            if (base + 835 + static_cast<uint32_t>(numTeams) * 4 > sizeof(SwosVM::g_memByte))
                return;
            for (int k = 0; k < numTeams; ++k) {
                uint16_t teamWord = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[base + 589 + k * 2]);
                int32_t delta = 0;
                if (computeTeamCacheDelta(teamWord, activeTeamWord, selTeamsPtrVal, delta)) {
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

    uint16_t activeTeamWord = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    int32_t selTeamsPtrVal = static_cast<int32_t>(swos.selTeamsPtr);
    if (activeTeamWord == 0 || activeTeamWord == 0xFFFF || selTeamsPtrVal == 0)
        return;

    for (int c = 0; c < 5; ++c) {
        uint32_t activeContestPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + c * 4]);
        uint32_t compBufPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + c * 4]);
        if (activeContestPtr != 0 && compBufPtr >= 5000 && compBufPtr < sizeof(SwosVM::g_memByte)) {
            relinkSingleCompetitionBuffer(compBufPtr, activeTeamWord, selTeamsPtrVal);
        }
    }

    uint16_t diyType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDiyCompetitionStart]);
    if (diyType <= 2) {
        relinkSingleCompetitionBuffer(static_cast<uint32_t>(CareerOffsets::kDiyCompetitionStart), activeTeamWord, selTeamsPtrVal);
    }
    uint16_t tourWorkType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDiyFileBufferCopy]);
    if (tourWorkType == 2) {
        relinkSingleCompetitionBuffer(static_cast<uint32_t>(CareerOffsets::kDiyFileBufferCopy), activeTeamWord, selTeamsPtrVal);
    }
}

void HotseatCareerManager::switchToPlayer(int index, bool relink /* = true */)
{
    if (index < 0 || index >= m_numPlayers)
        return;

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
