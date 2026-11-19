#include "hotseatInternal.h"

namespace SWOS {

namespace {

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
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isInitializingSeason()) {
        mgr.reset();
    }
}

void CareerInitVars_OnEnter()
{
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
}

void cseg_3C07C_OnEnter()
{
    HotseatCareerManager::get().onSeasonFinishedForAllPlayers();
}

void Hotseat_OnNewSeasonInitialized()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.restoreCalendarEntryFirstSlots();
    mgr.onNewSeasonInitialized();
}

void Hotseat_OnAfterProcessCareerFile()
{
    HotseatCareerManager::get().onAfterProcessCareerFile();
}

void Hotseat_OnAfterPlayMatch()
{
    HotseatCareerManager::get().onAfterPlayMatch();
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

void Hotseat_ResolveGameListSlotPtr()
{
    HotseatCareerManager::get().resolveGameListSlotPtr();
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
}

void PlayMatchMenu_OnEnter()
{
    HotseatCareerManager::get().onBeforePlayMatchMenu();
}

void Hotseat_OnAfterShowPlayMatchMenu()
{
    HotseatCareerManager::get().onAfterShowPlayMatchMenu();
}

void CareerOverInit_OnEnter()
{
    HotseatCareerManager::get().onCareerOverInit();
}

void Hotseat_FilterNewJobOffer()
{
    HotseatCareerManager::get().onFilterNewJobOffer();
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
