#pragma once

#include "BaseTest.h"

class CareerTest : public BaseTest
{
public:
    void init() override;
    void finish() override;
    void defaultCaseInit() override {}
    const char *name() const override;
    const char *displayName() const override;
    CaseList getCases() override;

private:
    void testCareerLiverpool();
    void testCareerGermany();
    void testForeignMarketPlayerLookup();
    void testHotseatInitializationAndSwapping();
    void testHotseatInterPlayerTransferOffer();
    void testHotseatHeadToHeadFixture();
    void testHotseatMultiTeamSelection();
    void testHotseatWizardPlayerSetup();
    void testHotseatTeamSlotAssignment();
    void testSinglePlayerCareerHotseat();
    void testHotseatSameLeagueValidation();
    void testHotseatPerClubStartingBalance();
    void testHotseatHeadToHeadControls();
    void testHotseatTurnBasedTransferQueueAndCounter();
    void testHotseatSaveAndLoadState();
    void testHotseatContextExtendedOffsetsAndPersistenceValidation();
    void testHotseatTransferGuardsAndMatchPlayers();
    void testHotseatVmFixturesAndPointerRelinking();
    void testHotseatWeeklyTickCalendarTurnAndH2H();
    void testHotseatLifecycleResetAndHooks();
    void testHotseatTurkeyTwoPlayerContestMenu();
    void testHotseatHighlightMenuContextAndResultTransition();
    void testHotseatSpecialCupsCompetitionLabelsAndConcurrentEuroCups();
    void testHotseatDomesticCupInclusionPremAndDiv3();
    void testHotseatPlayMatchEuroCupAndTransfersActive();
    void testHotseatConfirmOfferPromptAndMultiOffer();
    void testHotseatTransferDepartureAndTrialReplacement();
    void testHotseatMarketExclusionAndExplicitListing();
    void testHotseatExchangeTransferHumanAndCpu();
    void testHotseatTrialDepartureAfterOpponentMatch();
    void testHotseatEmergencyLoanForInjuredSquad();
    void testHotseatSaveAndLoadCareerFile();
    void testHotseatEndOfSeasonMenuSequenceAndSharedJobOfferDedup();
    void testHotseatClubSwitchOldTeamCpuRedAndFixtures();
    void testHotseatEightPlayersMirrorsAndSaveV8();
    void testHotseatMultiWorldMultiplexer();
    void testHotseatMultiWorldLockstep();
    void testHotseatDiv3ToDiv2SplitAndCrossLeagueJobOffer();
};
