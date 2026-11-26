#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class LlmProvider {
    kOllama,
    kOpenAi,
    kCustom,
};

struct FanPersonality {
    std::string name;
    std::string instruction;
};

std::vector<FanPersonality> defaultFanPersonalities();

struct LlmFeaturesConfig {
    bool postMatchNewspaper = true;
    bool preMatchScouting = true;
    bool transferNegotiations = true;
    bool dressingRoomVoices = true;
    bool livePunditTicker = true;
    bool supporterStream = true;
    bool sponsorChallenges = true;
    bool pressConferences = true;
    bool cpuTacticalManager = false;
};

struct LlmTelemetryConfig {
    bool enabled = true;
    bool exportMatchJson = true;
    int streamUpdateSeconds = 18;
    int maxVisibleTweets = 5;
    int streamBoxHeightPct = 62;
};

struct LlmVoiceConfig {
    bool enabled = true;
    std::string whisperCliPath = "whisper\\whisper-cli.exe";
    std::string whisperModelPath = "whisper\\ggml-base.bin";
    std::string whisperEndpoint = "http://127.0.0.1:8178/inference";
    std::string whisperLanguage = "auto";
};

struct LlmConfig {
    bool enabled = false;
    LlmProvider provider = LlmProvider::kOllama;
    std::string endpoint = "http://localhost:11434/api/chat";
    std::string model = "gemma4:latest";
    std::string apiKey;
    std::string language = "English";
    std::string systemInstruction = "The year is 1997 and you are generating immersive in-game football world content for Sensible World of Soccer 96/97. Only describe facts explicitly provided in the prompt. Never invent or guess unstated details such as player positions, headers, volleys, free kicks, assists, injuries, or weather.";
    int timeoutMs = 10000;
    int maxTokens = 1200;
    float temperature = 0.85f;
    bool think = false;
    bool useKnowledgeBase = true;
    std::string knowledgeBasePath = "docs\\features\\swos_1996_97_european_football_knowledge_base.md";
    LlmFeaturesConfig features;
    LlmTelemetryConfig telemetry;
    LlmVoiceConfig voice;
    std::vector<FanPersonality> personalities = defaultFanPersonalities();
};

struct LlmHttpResult {
    bool ok = false;
    int statusCode = 0;
    std::string body;
};

// Lifecycle & config
void initLlmClient();
void shutdownLlmClient();
bool loadLlmConfig(const char *iniPath = nullptr);
bool saveLlmConfig(const char *iniPath = nullptr);
bool reloadLlmKnowledgeBase(const char *overridePath = nullptr);
const std::string& getLoadedLlmKnowledgeBase();
void updateLlmConfig(const LlmConfig& cfg);
const LlmConfig& getLlmConfig();
void setLlmConfigForTest(const LlmConfig& cfg);

// Non-blocking async inference queue
uint64_t enqueueLlmRequest(const std::string& systemPrompt, const std::string& userPrompt, int maxTokensOverride = 0);
bool tryTakeLlmResponse(uint64_t requestId, std::string& outText);
bool waitForLlmResponse(uint64_t requestId, int maxWaitMs, std::string& outText);
void clearPendingLlmQueue();

// JSON helpers (exposed for unit testing)
std::string escapeLlmJson(const std::string& input);
std::string buildLlmChatPayload(const LlmConfig& cfg, const std::string& systemPrompt, const std::string& userPrompt);
std::string extractLlmAssistantContent(LlmProvider provider, const std::string& jsonResponse);

#ifdef SWOS_TEST
using LlmTransportFn = std::function<LlmHttpResult(const LlmConfig&, const std::string& payload)>;
void setLlmTransportForTest(LlmTransportFn fn);
#endif
