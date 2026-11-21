#include "SwosDataLayoutTest.h"
#include "unitTest.h"
#include "goalNet.h"
#include "pitchEffects.h"
#include "freeKickAim.h"
#include "llmClient.h"
#include "llmFeatures.h"

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
    std::string ollamaPayload = buildLlmChatPayload(ollamaCfg, "Sys \"role\"", "Line1\nLine2");
    assertTrue(ollamaPayload.find("\"stream\":false") != std::string::npos);
    assertTrue(ollamaPayload.find("\"num_predict\":120") != std::string::npos);
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

    setLlmTransportForTest(nullptr);
    shutdownLlmClient();
    setLlmConfigForTest(prevCfg);
}
