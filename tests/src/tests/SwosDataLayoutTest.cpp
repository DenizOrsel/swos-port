#include "SwosDataLayoutTest.h"
#include "unitTest.h"
#include "goalNet.h"
#include "pitchEffects.h"
#include "freeKickAim.h"
#include "llmClient.h"
#include "llmFeatures.h"
#include "llmReportMenu.h"
#include "matchTelemetry.h"
#include "sponsorChallenge.h"
#include "pressCharisma.h"
#include "supporterStream.h"
#include "achievements.h"
#include "stadiumFacilities.h"

static SwosDataLayoutTest t;

const char *SwosDataLayoutTest::name() const
{
    return "swos-data-layout";
}

const char *SwosDataLayoutTest::displayName() const
{
    return "SWOS data layout";
}

auto SwosDataLayoutTest::getCases() -> CaseList
{
    return {
        { "chairman scenes string pool offsets", "chairman-scenes-string-pool", {},
            bind(&SwosDataLayoutTest::verifyChairmanScenesStringPool) },
        { "competition file buffer layout", "competition-file-buffer-layout", {},
            bind(&SwosDataLayoutTest::verifyCompetitionFileBuffer) },
        { "career file buffer layout", "career-file-buffer-layout", {},
            bind(&SwosDataLayoutTest::verifyCareerFileBuffer) },
        { "fixed file buffer sizes", "fixed-file-buffer-sizes", {},
            bind(&SwosDataLayoutTest::verifyFixedFileBuffers) },
        { "original frame table area", "original-frame-table-area", {},
            bind(&SwosDataLayoutTest::verifyFrameTableArea) },
        { "physics goal net simulation and toggle", "physics-goal-net-simulation", {},
            bind(&SwosDataLayoutTest::verifyPhysicsGoalNetSimulation) },
        { "llm client json, async queue, and prompt wrapping", "llm-client-and-features", {},
            bind(&SwosDataLayoutTest::verifyLlmClientAndFeatures) },
    };
}

void SwosDataLayoutTest::verifyChairmanScenesStringPool()
{
    struct ExpectedString {
        size_t offset;
        const char *text;
    };

    static const ExpectedString expectedStrings[] = {
        { 0x4dc1, "YUGOSLAVIA" },
        { 0x2e5, "THE PRESIDENT" },
        { 0x2f, "RETURN TO GAME" },
        { 0x30c, "EXCELLENT JOB" },
        { 0x58c3, "QUEENSLAND" },
        { 0x331, "APPRECIATE IT" },
        { 0x3d1c, "DISK FULL" },
    };

    for (const auto& expected : expectedStrings)
        assertTrue(!memcmp(swos.aChairmanScenes + expected.offset, expected.text, strlen(expected.text)));
}

void SwosDataLayoutTest::verifyCompetitionFileBuffer()
{
    using Variables = SwosVM::SwosVariables;

    constexpr auto bufferStart = offsetof(Variables, competitionFileBuffer);
    assertEqual(offsetof(Variables, selTeamsPtr) - bufferStart, size_t{ 5'170 });
    assertEqual(offsetof(Variables, trainingGameCopy) - bufferStart, size_t{ 5'187 });
    assertEqual(offsetof(Variables, g_numSelectedTeams) - bufferStart, size_t{ 5'189 });
    assertEqual(offsetof(Variables, g_selectedTeams) - bufferStart, size_t{ 5'191 });
}

void SwosDataLayoutTest::verifyCareerFileBuffer()
{
    using Variables = SwosVM::SwosVariables;

    constexpr auto bufferStart = offsetof(Variables, careerFileBuffer);
    assertEqual(offsetof(Variables, USER_A) - bufferStart, size_t{ 0x16ac8 });
    assertEqual(offsetof(Variables, g_gameLength) - bufferStart, size_t{ 0x17374 });
    assertEqual(offsetof(Variables, g_autoReplays) - bufferStart, size_t{ 0x17376 });
    assertEqual(offsetof(Variables, g_autoSaveHighlights) - bufferStart, size_t{ 0x1737a });
    assertEqual(offsetof(Variables, g_allPlayerTeamsEqual) - bufferStart, size_t{ 0x1737c });
    assertEqual(offsetof(Variables, g_pitchType) - bufferStart, size_t{ 0x1737e });
    assertEqual(offsetof(Variables, g_selectedTeams) - bufferStart, size_t{ 95'153 });
}

void SwosDataLayoutTest::verifyFixedFileBuffers()
{
    assertEqual(sizeof(swos.g_pitchDatBuffer), size_t{ 10'032 });
    assertEqual(sizeof(swos.importTacticsFilename), size_t{ 256 });
    assertEqual(sizeof(swos.g_selectedTeams), size_t{ 68'400 });
    assertEqual(sizeof(swos.kRandomTable), size_t{ 256 });
}

void SwosDataLayoutTest::verifyFrameTableArea()
{
    using Variables = SwosVM::SwosVariables;
    static const int16_t expectedFrameTable[] = { -20, 344, -65, 415, 415, -101 };

    constexpr auto frameTablesStart = offsetof(Variables, frameIndicesTablesStart);
    constexpr auto animationTablesStart = offsetof(Variables, animTablesStart);

    assertEqual(frameTablesStart % alignof(int16_t), size_t{ 0 });
    assertEqual(animationTablesStart % alignof(int16_t), size_t{ 0 });
    assertEqual(animationTablesStart - frameTablesStart, sizeof(expectedFrameTable));
    assertTrue(!memcmp(swos.frameIndicesTablesStart, expectedFrameTable, sizeof(expectedFrameTable)));
}

void SwosDataLayoutTest::verifyPhysicsGoalNetSimulation()
{
    bool prevOpt = getPhysicsGoalNets();
    setPhysicsGoalNets(true);
    assertTrue(getPhysicsGoalNets());

    initGoalNets();
    auto topRest = getGoalNetTestState(0);
    auto botRest = getGoalNetTestState(1);
    assertTrue(topRest.totalNodes > 200);
    assertTrue(topRest.pinnedNodes > 20);
    assertEqual(topRest.totalNodes, botRest.totalNodes);
    assertTrue(topRest.maxDisplacement < 0.001f);

    // Strike the top goal net with a fast shot into the upper-middle back netting
    notifyGoalNetBallImpact(0, 334.0f, 118.0f, 9.0f, 0.5f, -5.5f, 1.0f);
    auto topHit = getGoalNetTestState(0);
    assertTrue(topHit.maxDisplacement > 1.0f);
    assertTrue(topHit.totalKineticEnergy > 0.1f);

    // Run 60 frames of Verlet + spring solver and verify ripple damping returns toward rest
    for (int f = 0; f < 60; ++f) {
        updateGoalNets();
    }
    auto topSettled = getGoalNetTestState(0);
    assertTrue(topSettled.maxDisplacement < topHit.maxDisplacement);
    assertTrue(topSettled.totalKineticEnergy < topHit.totalKineticEnergy * 0.25f);

    // Verify action replay goal net simulation when a replay shot enters the bottom goal and hits the net
    resetGoalNets();
    auto botAfterReset = getGoalNetTestState(1);
    assertTrue(botAfterReset.maxDisplacement < 0.001f);

    updateGoalNetsFromReplay(true, 334.0f, 768.0f, 7.0f, nullptr, 0);
    updateGoalNetsFromReplay(true, 334.0f, 773.5f, 8.0f, nullptr, 0);
    updateGoalNetsFromReplay(true, 334.0f, 773.5f, 8.0f, nullptr, 0); // Clamped impact frame in replay
    auto botReplayHit = getGoalNetTestState(1);
    assertTrue(botReplayHit.maxDisplacement > 1.0f);
    assertTrue(botReplayHit.totalKineticEnergy > 0.05f);

    // Verify ball & goalframe feedback toggle (on by default) and post/crossbar impact vibration + net shockwave
    bool prevBallFx = getBallAndGoalframeEffects();
    assertTrue(prevBallFx);
    resetGoalNets();
    notifyGoalFrameImpact(0, 299.5f, 129.0f, 9.0f, -1.2f, -4.8f, 0.3f, false);
    auto topPostHit = getGoalNetTestState(0);
    assertTrue(topPostHit.frameVibrationTimer > 20.0f);
    assertTrue(topPostHit.maxDisplacement > 0.2f);
    assertTrue(topPostHit.totalKineticEnergy > 0.01f);

    for (int f = 0; f < 30; ++f) {
        updateGoalNets();
    }
    auto topPostSettled = getGoalNetTestState(0);
    assertTrue(topPostSettled.frameVibrationTimer < 0.001f);

    setBallAndGoalframeEffects(false);
    assertFalse(getBallAndGoalframeEffects());
    setBallAndGoalframeEffects(prevBallFx);

    // Verify pitch & weather effects toggle, slide/dive skid marks, surface particles, and replay tracking
    bool prevPitchFx = getPitchWeatherEffects();
    assertTrue(prevPitchFx);
    resetPitchEffects();
    ReplayWeatherActor slidingPlayer{ 310.0f, 400.0f, 0.0f, true };
    updatePitchEffectsFromReplay(false, 0.0f, 0.0f, 0.0f, &slidingPlayer, 1);
    slidingPlayer.x = 316.0f;
    slidingPlayer.y = 403.0f;
    updatePitchEffectsFromReplay(false, 0.0f, 0.0f, 0.0f, &slidingPlayer, 1);
    auto fxState = getPitchEffectsTestState();
    assertTrue(fxState.skidMarkCount >= 1);
    assertTrue(fxState.particleCount >= 1);

    setPitchWeatherEffects(false);
    assertFalse(getPitchWeatherEffects());
    assertEqual(getPitchEffectsTestState().skidMarkCount, 0);
    setPitchWeatherEffects(prevPitchFx);

    // Verify Striker-style free-kick trajectory aiming (elevation Up/Down, aim Left/Right, curl modifier, toggle)
    bool prevFkAim = getFreeKickAiming();
    assertTrue(prevFkAim);
    initFreeKickAim();
    auto prevGameState = swos.gameState;
    auto prevGameStatePl = swos.gameStatePl;
    auto prevLastTeam = swos.lastTeamPlayedBeforeBreak;
    auto prevPlNum = swos.topTeamData.playerNumber;
    swos.gameState = GameState::kFreeKickCenter;
    swos.gameStatePl = GameState::kWaitingOnPlayer;
    swos.lastTeamPlayedBeforeBreak = &swos.topTeamData;
    swos.topTeamData.playerNumber = 1;

    updateFreeKickAimInput(&swos.topTeamData, kNoGameEvents);
    auto fkInit = getFreeKickAimTestState();
    assertTrue(fkInit.active);

    updateFreeKickAimInput(&swos.topTeamData, kGameEventUp);
    auto fkLow = getFreeKickAimTestState();
    assertTrue(fkLow.elevation < fkInit.elevation);

    updateFreeKickAimInput(&swos.topTeamData, kGameEventDown);
    updateFreeKickAimInput(&swos.topTeamData, kGameEventDown);
    auto fkHigh = getFreeKickAimTestState();
    assertTrue(fkHigh.elevation > fkInit.elevation);

    updateFreeKickAimInput(&swos.topTeamData, kGameEventRight);
    auto fkTurned = getFreeKickAimTestState();
    assertTrue(fkTurned.aimAngle > fkInit.aimAngle);

    updateFreeKickAimInput(&swos.topTeamData, kGameEventFreeKickCurl | kGameEventLeft);
    auto fkCurled = getFreeKickAimTestState();
    assertTrue(fkCurled.curl < 0.0f);

    setFreeKickAiming(false);
    assertFalse(getFreeKickAiming());
    setFreeKickAiming(prevFkAim);
    swos.gameState = prevGameState;
    swos.gameStatePl = prevGameStatePl;
    swos.lastTeamPlayedBeforeBreak = prevLastTeam;
    swos.topTeamData.playerNumber = prevPlNum;

    // Verify toggle off pauses physics updates
    setPhysicsGoalNets(false);
    assertFalse(getPhysicsGoalNets());
    setPhysicsGoalNets(prevOpt);
}

void SwosDataLayoutTest::verifyLlmClientAndFeatures()
{
    // 1. JSON escaping & payload formatting for Ollama vs OpenAI-compatible
    LlmConfig ollamaCfg;
    ollamaCfg.enabled = true;
    ollamaCfg.provider = LlmProvider::kOllama;
    ollamaCfg.model = "gemma4:latest";
    ollamaCfg.language = "Turkish";
    std::string ollamaPayload = buildLlmChatPayload(ollamaCfg, "Sys \"role\"", "Line1\nLine2");
    assertTrue(ollamaPayload.find("\"stream\":false") != std::string::npos);
    assertTrue(ollamaPayload.find("\"think\":false") != std::string::npos);
    assertTrue(ollamaPayload.find("\"num_predict\":1200") != std::string::npos);
    assertTrue(ollamaPayload.find("The year is 1997") != std::string::npos);
    assertTrue(ollamaPayload.find("Only describe facts explicitly provided") != std::string::npos);
    assertTrue(ollamaPayload.find("Always reply strictly in Turkish.") != std::string::npos);
    assertTrue(ollamaPayload.find("Sys \\\"role\\\"") != std::string::npos);
    assertTrue(ollamaPayload.find("Line1\\nLine2") != std::string::npos);

    std::string ollamaResp = R"({"message":{"role":"assistant","content":"CLASSIC WIN!\nChairman: Delighted."}})";
    assertEqual(extractLlmAssistantContent(LlmProvider::kOllama, ollamaResp), std::string("CLASSIC WIN!\nChairman: Delighted."));

    std::string openaiResp = R"({"choices":[{"message":{"role":"assistant","content":"GREAT STRIKE!"}}]})";
    assertEqual(extractLlmAssistantContent(LlmProvider::kOpenAi, openaiResp), std::string("GREAT STRIKE!"));

    // 2. Prompt wrapping for SWOS modal dialogs (max 6 lines, <= 40 chars each)
    auto wrapped = wrapTextForSwosPrompt(
        "STUNNING VICTORY IN THE POURING RAIN!\nUnited crushed City 3-0 at home.\nCHAIRMAN: KEEP THIS UP!",
        38, 6);
    assertTrue(!wrapped.empty() && wrapped.size() <= 6);
    for (const auto& line : wrapped)
        assertTrue(line.size() <= 38);

    // Full-width multi-page report wrapping preserves long content without cropping
    auto reportLines = wrapLlmReportLines(
        "HEADLINE: AMAZING COMEBACK!\n"
        "A long match report with plenty of detail about goals, saves, tactics, and weather so that nothing gets cropped.\n"
        "CHAIRMAN: Fantastic spirit from the lads tonight!",
        180);
    assertTrue(reportLines.size() >= 3);

    // 3. Async worker queue with mock transport
    auto prevCfg = getLlmConfig();
    setLlmConfigForTest(ollamaCfg);
    setLlmTransportForTest([](const LlmConfig&, const std::string&) -> LlmHttpResult {
        return { true, 200, R"({"message":{"content":"WE WANT THREE POINTS NEXT WEEK!"}})" };
    });

    uint64_t reqId = enqueueLlmRequest("sys", "user");
    assertTrue(reqId != 0);
    std::string reply;
    assertTrue(waitForLlmResponse(reqId, 2000, reply));
    assertEqual(reply, std::string("WE WANT THREE POINTS NEXT WEEK!"));

    // 4. Live pundit ticker non-blocking event -> update flow
    resetLivePunditTicker();
    triggerLivePunditEvent("GOAL SCORED", true);
    for (int i = 0; i < 40; ++i) {
        updateLivePunditTicker();
        SDL_Delay(5);
    }
    resetLivePunditTicker();

    // 5. Match telemetry time-pressure cues & supporter chat tweet parsing
    auto lateClose = computeTimePressureTag(88, 2, 1, false);
    assertTrue(lateClose.find("WHISTLE") != std::string::npos);
    auto parsedTweets = parseSupporterTweetBatch(
        "@User1988|HOME|Ref blow the whistle already!\n"
        "Fan_atic|AWAY|One more corner, we can still equalize!\n"
        "PixelGuru|NEUTRAL|What a tense finish in the 88th minute!");
    assertEqual(parsedTweets.size(), size_t{ 3 });
    assertEqual(parsedTweets[0].handle, std::string("User1988"));
    assertEqual(parsedTweets[0].message, std::string("Ref blow the whistle already!"));
    assertTrue(parsedTweets[0].side == SupporterSide::kHome);
    assertTrue(parsedTweets[1].side == SupporterSide::kAway);
    assertTrue(parsedTweets[2].side == SupporterSide::kNeutral);

    // 6. Fan personalities ai.ini round-trip & pundit stream routing
    {
        LlmConfig pCfg = getLlmConfig();
        pCfg.personalities = {
            { "calm", "You are a righteous and peaceful football fan." },
            { "fanatic", "You are wicked and only think your team deserves to win." },
        };
        setLlmConfigForTest(pCfg);
        const char *tmpIni = "tmp_test_personalities_ai.ini";
        assertTrue(saveLlmConfig(tmpIni));
        assertTrue(loadLlmConfig(tmpIni));
        const auto& loaded = getLlmConfig();
        assertEqual(loaded.personalities.size(), size_t{ 2 });
        assertEqual(loaded.personalities[0].name, std::string("calm"));
        assertEqual(loaded.personalities[1].name, std::string("fanatic"));
        assertTrue(loaded.personalities[1].instruction.find("wicked") != std::string::npos);
        std::remove(tmpIni);
        pushPunditMessageToStream("Pundit: What a fingertip save by the keeper!");
        resetSupporterStream();
    }

    // 7. Sponsor Challenge deterministic evaluator, reward clamping, parser, and sidecar persistence
    {
        assertEqual(clampSponsorRewardForDivision(999999, 0), 50000);
        assertEqual(clampSponsorRewardForDivision(1000, 0), 15000);
        assertEqual(clampSponsorRewardForDivision(999999, 2), 18000);
        assertEqual(clampSponsorRewardForDivision(1000, 2), 5000);

        SponsorMatchResult res;
        res.clubGoals = 3;
        res.opponentGoals = 0;
        res.clubHtGoals = 2;
        res.opponentHtGoals = 0;
        res.clubPossessionPct = 62;
        res.clubShotsOnTarget = 9;
        res.scorerEntries = { { 9, "MARSCHALL" }, { 9, "MARSCHALL" }, { 10, "KUKA" } };

        SponsorChallenge c;
        c.isActive = true;

        c.type = SponsorChallengeType::kCleanSheetWin;
        c.thresholdValue = 0;
        assertTrue(evaluateSponsorChallenge(c, res));
        res.opponentGoals = 1;
        assertFalse(evaluateSponsorChallenge(c, res));
        res.opponentGoals = 0;

        c.type = SponsorChallengeType::kPlayerGoals;
        c.targetShirtNumber = 9;
        c.targetPlayerName = "MARSCHALL";
        c.thresholdValue = 2;
        assertTrue(evaluateSponsorChallenge(c, res));
        c.thresholdValue = 3;
        assertFalse(evaluateSponsorChallenge(c, res));

        c.type = SponsorChallengeType::kFirstHalfGoals;
        c.thresholdValue = 2;
        assertTrue(evaluateSponsorChallenge(c, res));
        c.thresholdValue = 3;
        assertFalse(evaluateSponsorChallenge(c, res));

        c.type = SponsorChallengeType::kPossessionWin;
        c.thresholdValue = 60;
        assertTrue(evaluateSponsorChallenge(c, res));
        c.thresholdValue = 65;
        assertFalse(evaluateSponsorChallenge(c, res));

        c.type = SponsorChallengeType::kShotsOnTarget;
        c.thresholdValue = 9;
        assertTrue(evaluateSponsorChallenge(c, res));
        c.thresholdValue = 10;
        assertFalse(evaluateSponsorChallenge(c, res));

        c.type = SponsorChallengeType::kWinByMargin;
        c.thresholdValue = 3;
        assertTrue(evaluateSponsorChallenge(c, res));
        res.opponentGoals = 1;
        assertFalse(evaluateSponsorChallenge(c, res));

        c.type = SponsorChallengeType::kTeamGoals;
        c.thresholdValue = 3;
        assertTrue(evaluateSponsorChallenge(c, res));
        c.thresholdValue = 4;
        assertFalse(evaluateSponsorChallenge(c, res));

        res.clubLongRangeGoals = 1;
        c.type = SponsorChallengeType::kLongRangeGoal;
        c.thresholdValue = 1;
        assertTrue(evaluateSponsorChallenge(c, res));
        c.thresholdValue = 2;
        assertFalse(evaluateSponsorChallenge(c, res));

        res.clubHeaderGoals = 1;
        c.type = SponsorChallengeType::kHeaderGoal;
        c.thresholdValue = 1;
        assertTrue(evaluateSponsorChallenge(c, res));
        c.thresholdValue = 2;
        assertFalse(evaluateSponsorChallenge(c, res));

        std::vector<SponsorStarterInfo> starters = {
            { 9, "MARSCHALL", true, false, 12 },
            { 10, "KUKA", true, false, 7 },
            { 8, "WAGNER", false, true, 3 },
        };
        auto offline = generateOfflineSponsorChallenges("KAISERSLAUTERN", "ARSENAL", 0, starters, 42);
        assertEqual(offline.size(), size_t{ 3 });
        assertTrue(offline[0].type != offline[1].type && offline[1].type != offline[2].type && offline[0].type != offline[2].type);

        auto parsed = parseLlmSponsorChallenges(
            "CHALLENGE_CLEAN_SHEET_WIN|IRON WALL|Win without conceding.|NONE|0|90000\n"
            "CHALLENGE_PLAYER_GOALS|HOT STREAK|Marschall nets twice!|MARSCHALL|2|25000\n"
            "CHALLENGE_POSSESSION_WIN|BOSS THE BALL|Win with 61% ball.|NONE|61|32000",
            "KAISERSLAUTERN", "ARSENAL", 0, starters, 7);
        assertEqual(parsed.size(), size_t{ 3 });
        assertEqual(parsed[0].rewardAmount, 50000); // clamped to top-flight max
        assertEqual(parsed[1].targetPlayerName, std::string("MARSCHALL"));
        assertEqual(parsed[1].targetShirtNumber, int16_t{ 9 });
        assertEqual(parsed[2].thresholdValue, int16_t{ 61 });

        auto parsedSpecial = parseLlmSponsorChallenges(
            "CHALLENGE_TEAM_GOALS|GOAL RUSH|Score at least 3 goals.|NONE|3|22000\n"
            "CHALLENGE_LONG_RANGE_GOAL|OUTSIDE BOX|Score from outside the box.|NONE|1|28000\n"
            "CHALLENGE_HEADER_GOAL|AIR RAID|Score with a header.|NONE|1|26000",
            "KAISERSLAUTERN", "ARSENAL", 0, starters, 9);
        assertEqual(parsedSpecial.size(), size_t{ 3 });
        assertTrue(parsedSpecial[0].type == SponsorChallengeType::kTeamGoals && parsedSpecial[0].thresholdValue == 3);
        assertTrue(parsedSpecial[1].type == SponsorChallengeType::kLongRangeGoal && parsedSpecial[1].thresholdValue == 1);
        assertTrue(parsedSpecial[2].type == SponsorChallengeType::kHeaderGoal && parsedSpecial[2].thresholdValue == 1);

        // Multi-select batch evaluation: symmetric fail penalty + All-In +50% Clean Sweep Jackpot
        SponsorChallenge b1{ true, SponsorChallengeType::kCleanSheetWin, "SHUTOUT", "", 15000, -1, "", 0 };
        SponsorChallenge b2{ true, SponsorChallengeType::kPlayerGoals, "STRIKER", "", 25000, 9, "MARSCHALL", 2 };
        SponsorChallenge b3{ true, SponsorChallengeType::kPossessionWin, "DOMINATE", "", 40000, -1, "", 60 };

        // Case A: All 3 taken & all 3 completed -> 80,000 base + 40,000 (50% jackpot) = +120,000
        res.opponentGoals = 0;
        auto evalAllInWin = evaluateSponsorChallengeBatch({ b1, b2, b3 }, 3, res);
        assertTrue(evalAllInWin.allInJackpotTriggered);
        assertEqual(evalAllInWin.completedCount, 3);
        assertEqual(evalAllInWin.failedCount, 0);
        assertEqual(evalAllInWin.baseNetDelta, 80000);
        assertEqual(evalAllInWin.jackpotBonus, 40000);
        assertEqual(evalAllInWin.totalNetDelta, 120000);

        // Case B: Only 2 of 3 taken & both completed -> +40,000, NO jackpot
        auto evalTwoWin = evaluateSponsorChallengeBatch({ b1, b2 }, 3, res);
        assertFalse(evalTwoWin.allInJackpotTriggered);
        assertEqual(evalTwoWin.jackpotBonus, 0);
        assertEqual(evalTwoWin.totalNetDelta, 40000);

        // Case C: All 3 taken, 1 failed (conceded 1 goal -> b1 fails: -15,000; b2+b3 pass: +65,000) -> +50,000, NO jackpot
        res.opponentGoals = 1;
        auto evalMixed = evaluateSponsorChallengeBatch({ b1, b2, b3 }, 3, res);
        assertFalse(evalMixed.allInJackpotTriggered);
        assertEqual(evalMixed.completedCount, 2);
        assertEqual(evalMixed.failedCount, 1);
        assertEqual(evalMixed.jackpotBonus, 0);
        assertEqual(evalMixed.totalNetDelta, 50000);

        // Case D: Both taken fail -> full negative penalty deduction (-40,000)
        SponsorMatchResult badRes;
        badRes.clubGoals = 0;
        badRes.opponentGoals = 2;
        auto evalAllFail = evaluateSponsorChallengeBatch({ b1, b2 }, 3, badRes);
        assertFalse(evalAllFail.allInJackpotTriggered);
        assertEqual(evalAllFail.completedCount, 0);
        assertEqual(evalAllFail.failedCount, 2);
        assertEqual(evalAllFail.totalNetDelta, -40000);

        resetSponsorChallengeState();
        setSponsorCareerTotals(4, 2, 115000);
        const char *tmpSch = "tmp_test_sponsor.sch";
        assertTrue(saveSponsorChallengeSidecar(tmpSch));
        resetSponsorChallengeState();
        assertTrue(loadSponsorChallengeSidecar(tmpSch));
        uint16_t comp = 0, fail = 0;
        int64_t bonus = 0;
        getSponsorCareerTotals(comp, fail, bonus);
        assertEqual(comp, uint16_t{ 4 });
        assertEqual(fail, uint16_t{ 2 });
        assertEqual(bonus, int64_t{ 115000 });
        std::remove(tmpSch);
        resetSponsorChallengeState();
    }

    // 8. LLM Press Conference & Coach Charisma tiers, formulas, parser, offline templates, and sidecar persistence
    {
        // Tier boundaries & multipliers
        assertTrue(getCharismaTier(0) == CharismaTier::kToxic);
        assertTrue(getCharismaTier(19) == CharismaTier::kToxic);
        assertTrue(getCharismaTier(20) == CharismaTier::kUninspiring);
        assertTrue(getCharismaTier(44) == CharismaTier::kUninspiring);
        assertTrue(getCharismaTier(45) == CharismaTier::kRespectable);
        assertTrue(getCharismaTier(69) == CharismaTier::kRespectable);
        assertTrue(getCharismaTier(70) == CharismaTier::kCharismatic);
        assertTrue(getCharismaTier(89) == CharismaTier::kCharismatic);
        assertTrue(getCharismaTier(90) == CharismaTier::kIconic);
        assertTrue(getCharismaTier(100) == CharismaTier::kIconic);

        assertEqual(computeSeasonCharismaBonus(2000000, 15), int64_t{ 0 });       // Toxic 0.00x
        assertEqual(computeSeasonCharismaBonus(2000000, 40), int64_t{ 200000 });  // Uninspiring 0.25x * 0.40
        assertEqual(computeSeasonCharismaBonus(2000000, 50), int64_t{ 500000 });  // Respectable 0.50x * 0.50
        assertEqual(computeSeasonCharismaBonus(2000000, 80), int64_t{ 1600000 }); // Charismatic 1.00x * 0.80
        assertEqual(computeSeasonCharismaBonus(2000000, 100), int64_t{ 3000000 });// Iconic 1.50x * 1.00

        assertTrue(std::abs(getPressAttendanceMultiplier(10) - 0.90) < 0.001);
        assertTrue(std::abs(getPressAttendanceMultiplier(30) - 0.95) < 0.001);
        assertTrue(std::abs(getPressAttendanceMultiplier(55) - 1.00) < 0.001);
        assertTrue(std::abs(getPressAttendanceMultiplier(80) - 1.08) < 0.001);
        assertTrue(std::abs(getPressAttendanceMultiplier(95) - 1.15) < 0.001);

        // Delta & CP clamping
        assertEqual(applyCharismaDelta(50, 25), int16_t{ 58 });
        assertEqual(applyCharismaDelta(50, -25), int16_t{ 42 });
        assertEqual(applyCharismaDelta(98, 8), int16_t{ 100 });
        assertEqual(applyCharismaDelta(3, -8), int16_t{ 0 });

        // Classic derby detection
        assertTrue(detectClassicDerby("FENERBAHCE", "GALATASARAY", 0, 0));
        assertTrue(detectClassicDerby("AJAX", "FEYENOORD", 0, 0));
        assertTrue(detectClassicDerby("TEAM A", "TEAM B", 1, 2));
        assertFalse(detectClassicDerby("FENERBAHCE", "SERVETTE", 5, 10));

        // Offline template generation (pre-match derby + post-match confident backfire)
        PressMatchContext ctx;
        ctx.phase = PressPhase::kPreMatch;
        ctx.clubName = "FENERBAHCE";
        ctx.opponentName = "GALATASARAY";
        ctx.isDerby = true;
        auto preOffline = generateOfflinePressPrompt(ctx, 3);
        assertTrue(preOffline.isReady);
        assertTrue(!preOffline.question.empty());
        assertTrue(preOffline.question.find("GALATASARAY") != std::string::npos);

        ctx.phase = PressPhase::kPostMatch;
        ctx.preMatchStance = ManagerialStance::kConfident;
        ctx.preMatchQuote = "Nobody can stop us!";
        ctx.clubGoals = 0;
        ctx.opponentGoals = 3;
        auto postOffline = generateOfflinePressPrompt(ctx, 1);
        assertTrue(postOffline.isReady);
        assertTrue(postOffline.question.find("0-3") != std::string::npos);

        // Pipe-delimited LLM parser with clamping + Turkish Latin transliteration
        auto parsedPress = parseLlmPressPrompt(
            "Murat Kaya (Spor)|Şampiyonluk yolunda büyük derbiye hazır mısınız?\n"
            "HUMBLE|Rakibe saygımız büyük, sahada savaşacağız.|3|Basın saygılı yaklaşımı beğendi.\n"
            "CONFIDENT|Bu maçı kesin kazanacağız!|15|Taraftarlar coştu!\n"
            "TACTICAL|Orta sahayı kontrol edip hızlı çıkacağız.|2|Analistler taktiği övdü.\n"
            "HOSTILE|Saçma sorulara vaktim yok!|-20|Basın odasında gerginlik çıktı.",
            ctx, 7);
        assertTrue(parsedPress.isReady);
        assertEqual(parsedPress.journalistName, std::string("Murat Kaya (Spor)"));
        assertTrue(parsedPress.question.find("Sampiyonluk") != std::string::npos);
        assertEqual(parsedPress.options[1].charismaDelta, int8_t{ 8 });  // clamped from +15
        assertEqual(parsedPress.options[3].charismaDelta, int8_t{ -8 }); // clamped from -20

        // Sidecar .prs save/load round-trip
        resetPressCharismaState();
        setPressCareerTotals(78, 9, 1250000, static_cast<uint8_t>(ManagerialStance::kConfident), "We will dominate!");
        const char *tmpPrs = "tmp_test_press.prs";
        assertTrue(savePressCharismaSidecar(tmpPrs));
        resetPressCharismaState();
        assertTrue(loadPressCharismaSidecar(tmpPrs));
        int16_t cp = 0;
        uint16_t ints = 0;
        int64_t sBonus = 0;
        uint8_t stance = 0;
        std::string quote;
        getPressCareerTotals(cp, ints, sBonus, stance, quote);
        assertEqual(cp, int16_t{ 78 });
        assertEqual(ints, uint16_t{ 9 });
        assertEqual(sBonus, int64_t{ 1250000 });
        assertEqual(stance, static_cast<uint8_t>(ManagerialStance::kConfident));
        assertEqual(quote, std::string("We will dominate!"));
        std::remove(tmpPrs);
        resetPressCharismaState();
    }

    // 9. Global Achievements System: catalog, idempotency, evaluators, stadium hooks, atomic persistence
    {
        resetAchievementsProfileForTest();

        // Catalog integrity: 50 achievements, 1,710 total points
        assertEqual(kAchievementCount, uint16_t{ 50 });
        assertEqual(getTotalPossibleAchievementPoints(), uint32_t{ 1710 });
        for (uint16_t i = 0; i < kAchievementCount; ++i) {
            const auto& def = getAchievementDef(static_cast<AchievementId>(i));
            assertEqual(static_cast<uint16_t>(def.id), i);
            assertTrue(def.title && def.title[0] != '\0');
            assertTrue(def.description && def.description[0] != '\0');
            assertTrue(def.points >= 10 && def.points <= 100);
            assertTrue(def.targetProgress >= 1);
        }

        // Idempotent unlocking & toast queue
        assertTrue(unlockAchievement(AchievementId::kFirstKickoff, false));
        assertFalse(unlockAchievement(AchievementId::kFirstKickoff, false));
        assertEqual(getAchievementProfile().unlockedCount, uint32_t{ 1 });
        assertEqual(getAchievementProfile().totalPoints, uint32_t{ 10 });
        assertEqual(getPendingAchievementToastCount(), size_t{ 1 });

        // Goal event triggers: header, outside box, free kick, volley, corner <=3s, GK goal, early blitz, fergie time, hat-trick, 6-goal
        resetAchievementsProfileForTest();
        AchievementGoalEvent g;
        g.humanScored = true;
        g.isHeader = true;
        g.isOutsideBox = true;
        g.isFreeKick = true;
        g.withinCornerWindow = true;
        g.scorerIsGoalkeeper = true;
        g.matchMinute = 1;
        g.scorerMatchGoals = 6;
        achievementsOnGoalScored(g);
        const auto& prof = getAchievementProfile();
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kFirstGoal)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kHeaderGoal)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kOutsideBoxGoal)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kFreeKickGoal)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kCornerDirectOrQuick)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kGoalkeeperGoal)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kEarlyBlitz)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kHatTrick)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kDoubleHatTrick)].unlocked);

        // Penalty, volley, and Fergie Time (90'+ go-ahead)
        AchievementGoalEvent g2;
        g2.humanScored = true;
        g2.isPenalty = true;
        g2.matchMinute = 90;
        g2.isGoAheadGoal = true;
        achievementsOnGoalScored(g2);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kPenaltyGoal)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kFergieTime)].unlocked);

        AchievementGoalEvent g3;
        g3.humanScored = true;
        g3.isVolley = true;
        g3.matchMinute = 50;
        achievementsOnGoalScored(g3);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kVolleyOrOverhead)].unlocked);

        // Match summary: comebacks, dominance, down-to-10, giant killing (3 tiers), shootout, ET
        AchievementMatchSummary ms;
        ms.humanParticipated = true;
        ms.humanWon = true;
        ms.isCompetitive = true;
        ms.isCareer = true;
        ms.isCupMatch = true;
        ms.wonOnPenalties = true;
        ms.wonInExtraTime = true;
        ms.humanGoals = 10;
        ms.opponentGoals = 3;
        ms.humanHtGoals = 0;
        ms.opponentHtGoals = 3;
        ms.maxGoalDeficit = 3;
        ms.humanPossessionPct = 68;
        ms.humanShotsOnTarget = 12;
        ms.humanYellowCards = 0;
        ms.humanRedCards = 1;
        ms.hadRedCardBefore60 = true;
        ms.humanDivisionTier = 3;
        ms.opponentDivisionTier = 0;
        achievementsOnMatchFinished(ms);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kFirstKickoff)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kCareerFirstWin)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kPenaltyShootoutWin)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kExtraTimeWinner)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kHalftimeComeback)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kTwoGoalComeback)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kThreeGoalComeback)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kFiveGoalRout)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kDoubleDigits)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kPossessionMaster)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kClinicalFinishing)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kDownToTenWin)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kGiantKiller1Tier)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kGiantKiller2Tiers)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kGiantKiller3Tiers)].unlocked);

        // Friendly win + clean sheet
        AchievementMatchSummary ms2;
        ms2.humanParticipated = true;
        ms2.humanWon = true;
        ms2.isFriendly = true;
        ms2.humanGoals = 2;
        ms2.opponentGoals = 0;
        achievementsOnMatchFinished(ms2);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kFriendlyWin)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kCleanSheet)].unlocked);
        assertFalse(prof.records[static_cast<uint16_t>(AchievementId::kNoCardsWin)].unlocked);

        // Competitive Fair Play win (0 yellow, 0 red)
        AchievementMatchSummary ms3;
        ms3.humanParticipated = true;
        ms3.humanWon = true;
        ms3.isCompetitive = true;
        ms3.humanGoals = 1;
        ms3.opponentGoals = 0;
        ms3.humanYellowCards = 0;
        ms3.humanRedCards = 0;
        achievementsOnMatchFinished(ms3);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kNoCardsWin)].unlocked);

        // Transfers: First Signing, Million-Pound Signing, Profitable Flip (+1M+)
        achievementsOnCareerTransfer(true, 1200000, "A. SHEARER");
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kFirstSigning)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kMillionPoundSigning)].unlocked);
        assertFalse(prof.records[static_cast<uint16_t>(AchievementId::kProfitableFlip)].unlocked);
        achievementsOnCareerTransfer(false, 2500000, "A. SHEARER");
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kProfitableFlip)].unlocked);

        // Career Seasons: Promotion + Back-to-Back Promotion + Treble + Unbeaten + 10 Seasons + Call-up
        AchievementSeasonSummary s1;
        s1.oldDivisionTier = 2;
        s1.newDivisionTier = 1;
        s1.promoted = true;
        s1.completedCareerSeasons = 1;
        achievementsOnCareerSeasonEnd(s1);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kPromotion)].unlocked);
        assertFalse(prof.records[static_cast<uint16_t>(AchievementId::kBackToBackPromotion)].unlocked);

        // End-of-season call before new-season division check must NOT wipe consecutive promotion streak
        AchievementSeasonSummary s2End;
        s2End.oldDivisionTier = 1;
        s2End.newDivisionTier = -1;
        s2End.completedCareerSeasons = 2;
        achievementsOnCareerSeasonEnd(s2End);

        AchievementSeasonSummary s2Promo;
        s2Promo.oldDivisionTier = 1;
        s2Promo.newDivisionTier = 0;
        s2Promo.promoted = true;
        achievementsOnCareerSeasonEnd(s2Promo);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kBackToBackPromotion)].unlocked);

        AchievementSeasonSummary sTreble;
        sTreble.oldDivisionTier = 0;
        sTreble.newDivisionTier = 0;
        sTreble.wonTopFlightLeague = true;
        sTreble.wonDomesticCup = true;
        sTreble.wonEuropeanCup = true;
        sTreble.unbeatenLeagueSeason = true;
        sTreble.completedCareerSeasons = 10;
        achievementsOnCareerSeasonEnd(sTreble);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kLeagueChampion)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kDomesticCupWin)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kEuropeanCupWin)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kContinentalTreble)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kUnbeatenSeason)].unlocked);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kCareerTenSeasons)].unlocked);

        achievementsOnNationalTeamCallup();
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kInternationalCallup)].unlocked);

        // Charisma 90+ & Sponsor 10-challenge cumulative streak
        achievementsOnCharismaChanged(89);
        assertFalse(prof.records[static_cast<uint16_t>(AchievementId::kIconicCharisma)].unlocked);
        achievementsOnCharismaChanged(90);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kIconicCharisma)].unlocked);

        achievementsOnSponsorChallengesCompleted(4);
        assertFalse(prof.records[static_cast<uint16_t>(AchievementId::kSponsorStreak)].unlocked);
        achievementsOnSponsorChallengesCompleted(6);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kSponsorStreak)].unlocked);

        // Stadium & Facilities hooks (ready for future Stadium feature)
        achievementsOnStadiumUpgradeCompleted();
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kStadiumFirstUpgrade)].unlocked);
        achievementsOnStadiumMatchAttendance(9, 75000, 75000);
        assertFalse(prof.records[static_cast<uint16_t>(AchievementId::kFortress85k)].unlocked);
        achievementsOnStadiumMatchAttendance(10, 85000, 85000);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kFortress85k)].unlocked);
        achievementsOnFacilitiesUpdated(5, 5, 4, 5);
        assertFalse(prof.records[static_cast<uint16_t>(AchievementId::kMaxFacilities)].unlocked);
        achievementsOnFacilitiesUpdated(5, 5, 5, 5);
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kMaxFacilities)].unlocked);

        // Cumulative progress helper (Veteran 50 matches & Centurion 100 goals)
        assertTrue(addAchievementProgress(AchievementId::kVeteran50Matches, 50, false));
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kVeteran50Matches)].unlocked);
        assertTrue(setAchievementProgress(AchievementId::kCenturion100Goals, 100, false));
        assertTrue(prof.records[static_cast<uint16_t>(AchievementId::kCenturion100Goals)].unlocked);

        // Atomic disk save/load round-trip
        uint32_t expectedUnlocked = prof.unlockedCount;
        uint32_t expectedPoints = prof.totalPoints;
        const char *tmpAch = "tmp_test_achievements.dat";
        assertTrue(saveAchievements(tmpAch));
        resetAchievementsProfileForTest();
        assertEqual(getAchievementProfile().unlockedCount, uint32_t{ 0 });
        assertTrue(loadAchievements(tmpAch));
        assertEqual(getAchievementProfile().unlockedCount, expectedUnlocked);
        assertEqual(getAchievementProfile().totalPoints, expectedPoints);
        assertTrue(getAchievementProfile().records[static_cast<uint16_t>(AchievementId::kMaxFacilities)].unlocked);
        assertTrue(getAchievementProfile().records[static_cast<uint16_t>(AchievementId::kBackToBackPromotion)].unlocked);
        std::remove(tmpAch);
        resetAchievementsProfileForTest();
    }

    // 10. Stadium & Facilities System: seeding, 96/97 ticket pricing, upgrades, attendance/revenue, pitch/medical/training, sidecar persistence
    {
        resetStadiumFacilitiesState();

        // Authentic stadium seeding across divisions
        StadiumData fb = stadiumInitForClub("FENERBAHCE", 0, 28, 101);
        assertTrue(fb.initialized);
        assertEqual(std::string(fb.stadiumName), std::string("SUKRU SARACOGLU"));
        assertEqual(fb.capacityLevel, uint8_t{ 5 });
        assertEqual(fb.maxCapacity, uint32_t{ 45000 });
        assertEqual(fb.ticketPrice, int32_t{ 10 }); // 1996/97 Div 0 Standard = GBP 10

        StadiumData div4 = stadiumInitForClub("SMALL TOWN FC", 3, 8, 202);
        assertEqual(div4.capacityLevel, uint8_t{ 1 });
        assertEqual(div4.maxCapacity, uint32_t{ 5000 });
        assertEqual(div4.pitchLevel, uint8_t{ 1 });
        assertEqual(div4.ticketPrice, int32_t{ 5 }); // 1996/97 Div 4 Standard = GBP 5

        // Division ticket price table (96/97 SWOS scale)
        assertEqual(getStadiumTicketPriceForDivision(0, TicketPolicy::kCheap), int32_t{ 7 });
        assertEqual(getStadiumTicketPriceForDivision(0, TicketPolicy::kStandard), int32_t{ 10 });
        assertEqual(getStadiumTicketPriceForDivision(0, TicketPolicy::kExpensive), int32_t{ 15 });
        assertEqual(getStadiumTicketPriceForDivision(3, TicketPolicy::kCheap), int32_t{ 3 });
        assertEqual(getStadiumTicketPriceForDivision(3, TicketPolicy::kExpensive), int32_t{ 7 });

        // Upgrade quotes & validation edge cases
        int64_t cash = 120000;
        auto qPoor = stadiumGetUpgradeQuote(div4, StadiumModuleType::kCapacity, cash);
        assertTrue(qPoor.status == StadiumUpgradeStatus::kInsufficientFunds);
        assertEqual(qPoor.cost, int32_t{ 150000 });
        assertFalse(stadiumStartUpgrade(div4, cash, StadiumModuleType::kCapacity));
        assertEqual(cash, int64_t{ 120000 });

        cash = 500000;
        auto qOk = stadiumGetUpgradeQuote(div4, StadiumModuleType::kCapacity, cash);
        assertTrue(qOk.status == StadiumUpgradeStatus::kOk);
        assertEqual(qOk.buildMatches, uint8_t{ 2 });
        assertTrue(stadiumStartUpgrade(div4, cash, StadiumModuleType::kCapacity));
        assertEqual(cash, int64_t{ 350000 });
        assertTrue(div4.activeProject.isActive);

        // Concurrent construction blocked
        auto qBusy = stadiumGetUpgradeQuote(div4, StadiumModuleType::kPitch, cash);
        assertTrue(qBusy.status == StadiumUpgradeStatus::kConstructionInProgress);
        assertFalse(stadiumStartUpgrade(div4, cash, StadiumModuleType::kPitch));

        // Construction countdown & completion
        StadiumModuleType doneMod = StadiumModuleType::kPitch;
        uint8_t doneLvl = 0;
        assertFalse(stadiumAdvanceConstruction(div4, doneMod, doneLvl));
        assertEqual(div4.activeProject.matchesRemaining, uint8_t{ 1 });
        assertTrue(stadiumAdvanceConstruction(div4, doneMod, doneLvl));
        assertTrue(doneMod == StadiumModuleType::kCapacity);
        assertEqual(doneLvl, uint8_t{ 2 });
        assertEqual(div4.capacityLevel, uint8_t{ 2 });
        assertEqual(div4.maxCapacity, uint32_t{ 12000 });
        assertFalse(div4.activeProject.isActive);

        // Max level check
        StadiumData maxed = fb;
        maxed.capacityLevel = 10;
        maxed.maxCapacity = getStadiumCapacityForLevel(10);
        maxed.pitchLevel = 5;
        assertEqual(maxed.maxCapacity, uint32_t{ 85000 });
        assertTrue(stadiumGetUpgradeQuote(maxed, StadiumModuleType::kCapacity, 99999999).status == StadiumUpgradeStatus::kMaxLevel);
        assertTrue(stadiumGetUpgradeQuote(maxed, StadiumModuleType::kPitch, 99999999).status == StadiumUpgradeStatus::kMaxLevel);

        // Dynamic attendance, derby boost, charisma multiplier, commercial bonus, upkeep
        StadiumMatchdayReport derbyRep = stadiumCalculateMatchday(fb, 0, 1, 2, true, 95, "GALATASARAY");
        assertEqual(derbyRep.attendance, uint32_t{ 45000 }); // capped at maxCapacity
        assertTrue(derbyRep.isSellout);
        assertTrue(derbyRep.isDerby);
        assertEqual(derbyRep.ticketRevenue, int64_t{ 450000 }); // 45,000 * GBP 10
        assertEqual(derbyRep.commercialBonusPct, 30);          // Level 3 -> +30%
        assertEqual(derbyRep.commercialRevenue, int64_t{ 135000 });
        assertEqual(derbyRep.netProfit, derbyRep.ticketRevenue + derbyRep.commercialRevenue - derbyRep.upkeepCost);

        // Ticket price elasticity: Expensive lowers attendance vs Cheap
        StadiumData cheapClub = fb;
        cheapClub.ticketPolicy = TicketPolicy::kCheap;
        StadiumData expClub = fb;
        expClub.ticketPolicy = TicketPolicy::kExpensive;
        auto repCheap = stadiumCalculateMatchday(cheapClub, 0, 10, 10, false, 50, "MIDCLUB");
        auto repExp = stadiumCalculateMatchday(expClub, 0, 10, 10, false, 50, "MIDCLUB");
        assertTrue(repCheap.attendance > repExp.attendance);

        // Pitch quality override, medical injury reduction, training growth bonus
        assertEqual(applyStadiumPitchQualityOverrideForLevel(1, 3), 1); // L3 keeps Muddy
        assertEqual(applyStadiumPitchQualityOverrideForLevel(1, 4), 4); // L4 Muddy -> Normal
        assertEqual(applyStadiumPitchQualityOverrideForLevel(2, 5), 5); // L5 Wet -> Dry
        assertEqual(computeMedicalReducedInjuryDays(10, 5), uint8_t{ 5 }); // L5 -> -50%
        assertEqual(computeMedicalReducedInjuryDays(2, 5), uint8_t{ 1 });  // min 1 match
        assertTrue(std::abs(getTrainingGrowthBonusFraction(5) - 0.25) < 0.001);

        // Sidecar .std save/load round-trip
        div4.ticketPolicy = TicketPolicy::kExpensive;
        div4.ticketPrice = 7;
        div4.seasonTotalAttendance = 42000;
        div4.seasonGateRevenue = 315000;
        div4.seasonUpkeepPaid = 20000;
        div4.seasonHomeMatches = 4;
        setCurrentCareerStadium(div4);
        const char *tmpStd = "tmp_test_stadium.std";
        assertTrue(saveStadiumSidecar(tmpStd));
        resetStadiumFacilitiesState();
        assertTrue(loadStadiumSidecar(tmpStd));
        const auto& loaded = getCurrentCareerStadium();
        assertTrue(loaded.initialized);
        assertEqual(loaded.capacityLevel, uint8_t{ 2 });
        assertEqual(loaded.maxCapacity, uint32_t{ 12000 });
        assertTrue(loaded.ticketPolicy == TicketPolicy::kExpensive);
        assertEqual(loaded.seasonTotalAttendance, uint32_t{ 42000 });
        assertEqual(loaded.seasonGateRevenue, int64_t{ 315000 });
        assertEqual(loaded.seasonHomeMatches, uint16_t{ 4 });
        std::remove(tmpStd);
        resetStadiumFacilitiesState();
    }

    setLlmTransportForTest(nullptr);
    shutdownLlmClient();
    setLlmConfigForTest(prevCfg);
}
