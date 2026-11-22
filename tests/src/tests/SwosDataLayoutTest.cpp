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
#include "supporterStream.h"

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

    setLlmTransportForTest(nullptr);
    shutdownLlmClient();
    setLlmConfigForTest(prevCfg);
}
