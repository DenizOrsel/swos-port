#include "hotseatInternal.h"
#include "llmFeatures.h"
#include "pressCharisma.h"
#include "achievements.h"
#include "stadiumFacilities.h"

namespace SWOS {

namespace {

int16_t s_prevNationalSelector = 0;
int s_lastSeasonEndLeague = -1;

void activateEuroCupForVmSelectedTeam()
{
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isActive() || mgr.numPlayers() <= 1 || !mgr.anyEuroCupActive())
        return;

    int16_t selTid = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    int ownerIdx = mgr.findHotseatPlayerByTeamId(selTid);
    if (ownerIdx < 0)
        ownerIdx = mgr.activePlayerIndex();
    if (ownerIdx >= 0 && ownerIdx < mgr.numPlayers())
        mgr.activateEuroCup(mgr.player(ownerIdx).euroCupIndex, true);
}

} // namespace

void InitCareerVariables_OnEnter()
{
    resetCareerLlmSnapshots();
    resetPressCharismaState();
    resetStadiumFacilitiesState();
    s_prevNationalSelector = 0;
    s_lastSeasonEndLeague = -1;
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isInitializingSeason()) {
        mgr.reset();
    }
}

void CareerInitVars_OnEnter()
{
    resetCareerLlmSnapshots();
    resetPressCharismaState();
    resetStadiumFacilitiesState();
    s_prevNationalSelector = 0;
    s_lastSeasonEndLeague = -1;
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && !mgr.isInitializingSeason()) {
        mgr.reset();
    }
}

void StartContest_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isActive())
        return;

    if (swos.g_gameType != kGameTypeCareer) {
        mgr.reset();
    } else {
        if (mgr.hasActiveWorld() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
            int16_t tid = mgr.activePlayer().teamId;
            auto *selTeams = reinterpret_cast<const TeamFile *>(swos.g_selectedTeams);
            int count = std::clamp<int>(swos.g_numSelectedTeams, 0, kMaxSelectedTeamsCount);
            bool foundActive = false;
            for (int i = 0; i < count; ++i) {
                if (*reinterpret_cast<const int16_t *>(&selTeams[i]) == tid) {
                    foundActive = true;
                    break;
                }
            }
            if (!foundActive) {
                mgr.loadWorldToVm(mgr.activeWorldKey(), false);
            }
        }
        mgr.syncPlayersWithSelectedTeams(true);
        mgr.relinkCompetitionTeamPointers();
        mgr.syncTransferMarketPoolsForCurrentPlayer();
    }
}

void cseg_8B8B8_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isActive() || mgr.numPlayers() <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    mgr.ensureHotseatTeamControlsInSelectedTeams();
}

void cseg_94DB1_OnEnter()
{
    HotseatCareerManager::get().onWeeklyTickForAllPlayers();
    applyPendingMatchdayGateReceiptsCharismaBonus();
    checkCareerSquadTransfersLlm();
    onCareerWeeklyTickLlm();

    if (swos.g_gameType == kGameTypeCareer) {
        int16_t natSel = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kIsNationalSelector]);
        if (natSel != 0 && s_prevNationalSelector == 0)
            achievementsOnNationalTeamCallup();
        s_prevNationalSelector = natSel;
    }
}

void cseg_3C07C_OnEnter()
{
    onEndOfSeasonPressCharismaBonus();
    onEndOfSeasonStadiumProgression();
    if (swos.g_gameType == kGameTypeCareer) {
        uint16_t seasonIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
        uint16_t oldLeague = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]);
        const auto *statsWords = reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C88]);
        uint16_t played = statsWords[0];
        uint16_t losses = statsWords[3];
        uint16_t trophyMask = 0;
        size_t maxEntries = kCareerHistoryBufferSize / kCareerHistoryEntrySize;
        if (seasonIdx < maxEntries) {
            const uint8_t *histBuf = &SwosVM::g_memByte[CareerOffsets::kCareerHistory];
            trophyMask = *reinterpret_cast<const uint16_t *>(&histBuf[seasonIdx * kCareerHistoryEntrySize + 20]);
        }

        AchievementSeasonSummary season;
        season.oldDivisionTier = static_cast<int>(oldLeague);
        season.wonTopFlightLeague = (oldLeague == 0 && (trophyMask & (32u | 64u)) != 0);
        season.wonEuropeanCup = ((trophyMask & (1u | 2u | 4u)) != 0);
        season.wonDomesticCup = ((trophyMask & (8u | 16u | 128u | 256u)) != 0);
        season.unbeatenLeagueSeason = (played >= 10 && losses == 0);
        season.completedCareerSeasons = static_cast<int>(seasonIdx) + 1;
        achievementsOnCareerSeasonEnd(season);
        s_lastSeasonEndLeague = static_cast<int>(oldLeague);
    }
    HotseatCareerManager::get().onSeasonFinishedForAllPlayers();
}

void Hotseat_OnNewSeasonInitialized()
{
    resetCareerLlmSnapshots();
    auto& mgr = HotseatCareerManager::get();
    mgr.restoreCalendarEntryFirstSlots();
    mgr.onNewSeasonInitialized();

    if (swos.g_gameType == kGameTypeCareer && s_lastSeasonEndLeague >= 0) {
        uint16_t newLeague = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]);
        AchievementSeasonSummary promo;
        promo.oldDivisionTier = s_lastSeasonEndLeague;
        promo.newDivisionTier = static_cast<int>(newLeague);
        promo.promoted = (s_lastSeasonEndLeague > 0 && static_cast<int>(newLeague) < s_lastSeasonEndLeague);
        achievementsOnCareerSeasonEnd(promo);
        s_lastSeasonEndLeague = -1;
    }
}

void Hotseat_OnAfterProcessCareerFile()
{
    HotseatCareerManager::get().onAfterProcessCareerFile();
}

void Hotseat_OnAfterPlayMatch()
{
    HotseatCareerManager::get().onAfterPlayMatch();
    applyPendingMatchdayGateReceiptsCharismaBonus();
    onViewResultMatchCompletedLlm();
}

void InitializeNewSeason_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.numPlayers() > 1) {
        mgr.clearAllCalendarSlots();
    }
}

void cseg_34DA3_OnEnter()
{
    HotseatCareerManager::get().stageActiveCalendarSlotToVm();
}

void Hotseat_OnLeagueCalendarSlotAdded()
{
    HotseatCareerManager::get().captureLeagueCalendarSlot();
}

void Hotseat_ResetCupCalendarEntrySlots()
{
    HotseatCareerManager::get().resetCalendarEntrySlots(A6.asDword());
}

void Hotseat_OnCupCalendarSlotAdded()
{
    HotseatCareerManager::get().captureCupCalendarSlot();
}

void Hotseat_OnCalendarEntryFinished()
{
    HotseatCareerManager::get().restoreCalendarEntryFirstSlots();
}

void Hotseat_ResolvePostMatchCalendarSlotPtr()
{
    HotseatCareerManager::get().resolvePostMatchCalendarSlotPtr();
}

void Hotseat_CommitAndStageNextCalendarSlot()
{
    HotseatCareerManager::get().commitAndStageNextCalendarSlot();
}

void Hotseat_OnAfterCalcNextMatch()
{
    HotseatCareerManager::get().clampCalcNextMatchSlotPtr();
}

void Hotseat_OnCalendarInsertSpace()
{
    HotseatCareerManager::get().shiftCalendarSlots(A4.asDword(), D5.asWord());
}

void cseg_34D5A_OnEnter()
{
    HotseatCareerManager::get().onGameListCenter();
}

void Hotseat_OnGameListScrollUp()
{
    HotseatCareerManager::get().onGameListScrollUp();
}

void Hotseat_OnGameListScrollDown()
{
    HotseatCareerManager::get().onGameListScrollDown();
}

void Hotseat_OnAfterBuildGameList()
{
    HotseatCareerManager::get().onAfterBuildGameList();
}

void Hotseat_ResolveGameListSlotPtr()
{
    HotseatCareerManager::get().resolveGameListSlotPtr();
}

void Hotseat_RestoreGameListContext()
{
    HotseatCareerManager::get().restoreGameListContext();
}

void cseg_8DAC3_OnEnter()
{
    HotseatCareerManager::get().initAdditionalEuroCups();
}

void cseg_8B7EA_OnEnter()
{
    HotseatCareerManager::get().unmaskEuroCupCalendarEntries();
}

void Hotseat_OnDomesticCupTeamSelect()
{
    HotseatCareerManager::get().addHumanTeamsToCupSelection(CareerOffsets::kDseg10F0F6);
}

void Hotseat_OnTournamentTeamSelect()
{
    HotseatCareerManager::get().addHumanTeamsToCupSelection(CareerOffsets::kDseg10EB23);
}

void cseg_8E539_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForCurrentCalendarEntry();
}

void cseg_8E841_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForCurrentCalendarEntry();
}

void cseg_8E796_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForCurrentCalendarEntry();
}

void cseg_8E469_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForCurrentCalendarEntry();
}

void cseg_29B4A_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isActive() || mgr.numPlayers() <= 1 || !mgr.anyEuroCupActive())
        return;

    uint16_t calType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDiyCompetitionStart + 0x2B]);
    if (calType == 3) {
        mgr.maskOtherEuroCupCalendarEntries();
    }
}

void cseg_375C2_OnEnter()
{
    activateEuroCupForVmSelectedTeam();
}

void cseg_39392_OnEnter()
{
    activateEuroCupForVmSelectedTeam();
}

void cseg_92D55_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForSeasonEnd(0);
}

void cseg_9307A_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForSeasonEnd(1);
}

void cseg_9339D_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForSeasonEnd(2);
}

void cseg_927A2_OnEnter()
{
    HotseatCareerManager::get().injectEndSeasonLeagueResult();
}

void cseg_93FD8_OnEnter()
{
    HotseatCareerManager::get().injectEndSeasonLeagueResult();
}

void cseg_928CC_OnEnter()
{
    HotseatCareerManager::get().injectEndSeasonCup1Result();
}

void cseg_92A63_OnEnter()
{
    HotseatCareerManager::get().injectEndSeasonCup2Result();
}

void cseg_925C9_OnEnter()
{
    HotseatCareerManager::get().injectEndSeasonPlayoffResult();
}

void Hotseat_OnAfterReplacementPlayer()
{
    HotseatCareerManager::get().onAfterCreateReplacementPlayer();
}

void Hotseat_OnBeforeAddDomesticMarket()
{
    HotseatCareerManager::get().onBeforeAddDomesticMarket();
}

void Hotseat_OnAfterAddDomesticMarket()
{
    HotseatCareerManager::get().onAfterAddDomesticMarket();
}

void Hotseat_OnBeforeAddForeignMarket()
{
    HotseatCareerManager::get().onBeforeAddForeignMarket();
}

void Hotseat_OnAfterAddForeignMarket()
{
    HotseatCareerManager::get().onAfterAddForeignMarket();
}

void Hotseat_SyncTransferMarkets()
{
    HotseatCareerManager::get().syncTransferMarketPoolsForCurrentPlayer();
}

void Hotseat_ResolveMarketEntryTeamPtr()
{
    HotseatCareerManager::get().resolveMarketTeamPtrForEntry(A6);
}

void Hotseat_ResolveMarketListTeamPtr()
{
    HotseatCareerManager::get().resolveMarketTeamPtrForEntry(A1);
}

void Hotseat_LookupForeignPoolPlayer()
{
    HotseatCareerManager::get().lookupForeignPoolPlayer();
}

void Hotseat_ResolveGetPlayerTeamPtr()
{
    HotseatCareerManager::get().resolveGetPlayerTeamPtr();
}

void cseg_52956_OnEnter()
{
    HotseatCareerManager::get().onBeforeEmergencyLoan();
}

void Hotseat_OnAfterEmergencyLoan()
{
    HotseatCareerManager::get().onAfterEmergencyLoan();
}

void Hotseat_OnAfterEnsureCareerPlayers()
{
    HotseatCareerManager::get().onAfterEnsureCareerPlayers();
}

void ShowPlayMatchMenu_OnEnter()
{
    HotseatCareerManager::get().onBeforeShowPlayMatchMenu();
    checkCareerSquadTransfersLlm();
}

void PlayMatchMenu_OnEnter()
{
    HotseatCareerManager::get().onBeforePlayMatchMenu();
}

void Hotseat_OnAfterShowPlayMatchMenu()
{
    HotseatCareerManager::get().onAfterShowPlayMatchMenu();
    checkCareerSquadTransfersLlm();
}

void CareerOverInit_OnEnter()
{
    HotseatCareerManager::get().onCareerOverInit();
}

void Hotseat_FilterNewJobOffer()
{
    HotseatCareerManager::get().onFilterNewJobOffer();
    if (!SwosVM::flags.zero && !shouldAllowJobOfferTeamIdByCharisma(D0.asWord())) {
        SwosVM::flags.zero = true;
    }
}

void Hotseat_OnAfterAcceptJobOffer()
{
    HotseatCareerManager::get().onAfterAcceptJobOffer();
}

void Hotseat_OnAfterJobOfferRoll()
{
    HotseatCareerManager::get().onAfterJobOfferRoll();
}

void ContinueCareer()
{
    HotseatCareerManager::get().onContinueCareer();
}

void ExitCareerMenu()
{
    HotseatCareerManager::get().onExitCareerMenu();
}

void JobOffersMenuRetired()
{
    int16_t accepted = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]);
    int16_t newOffers = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]);
    int16_t oldOffers = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]);

    if (accepted != -1) {
        A6 = CareerOffsets::kGotJobOfferMenu;
        SWOS::ShowMenu();
    } else if (newOffers != 0 || oldOffers != 0) {
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17C228]) = 1;
        A6 = CareerOffsets::kJobOffersMenu;
        SWOS::ShowMenu();

        accepted = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]);
        int16_t sacked = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kYoureSacked]);
        newOffers = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]);
        oldOffers = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]);
        if (accepted == -1 && sacked != 0 && newOffers == 0 && oldOffers == 0) {
            if (!HotseatCareerManager::get().onJobOffersMenuRetiredSackedEmpty()) {
                D0 = 0;
                ::CareerInitVars();
                SetExitMenuFlag();
                return;
            }
        }
    } else {
        return;
    }

    D0 = 5;
    ::CalcMenuEntryAddress();
    if (SwosVM::readMemory(A0.asDword() + 6, 2) != 0) {
        D0 = 8;
        SWOS::SetCurrentEntry();
    }
}

} // namespace SWOS
