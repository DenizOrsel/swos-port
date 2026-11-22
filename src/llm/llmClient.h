#pragma once

#include <cstdint>
#include <functional>
#include <string>

enum class LlmProvider {
    kOllama,
    kOpenAi,
    kCustom,
};

struct LlmFeaturesConfig {
    bool postMatchNewspaper = true;
    bool preMatchScouting = true;
    bool transferNegotiations = true;
    bool dressingRoomVoices = true;
    bool livePunditTicker = true;
    bool cpuTacticalManager = false;
};

struct LlmConfig {
    bool enabled = false;
    LlmProvider provider = LlmProvider::kOllama;
    std::string endpoint = "http://localhost:11434/api/chat";
    std::string model = "gemma4:latest";
    std::string apiKey;
    int timeoutMs = 10000;
    int maxTokens = 1200;
    float temperature = 0.7f;
    LlmFeaturesConfig features;
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
void updateLlmConfig(const LlmConfig& cfg);
const LlmConfig& getLlmConfig();
void setLlmConfigForTest(const LlmConfig& cfg);

// Non-blocking async inference queue
uint64_t enqueueLlmRequest(const std::string& systemPrompt, const std::string& userPrompt);
bool tryTakeLlmResponse(uint64_t requestId, std::string& outText);
bool waitForLlmResponse(uint64_t requestId, int maxWaitMs, std::string& outText);

// JSON helpers (exposed for unit testing)
std::string escapeLlmJson(const std::string& input);
std::string buildLlmChatPayload(const LlmConfig& cfg, const std::string& systemPrompt, const std::string& userPrompt);
std::string extractLlmAssistantContent(LlmProvider provider, const std::string& jsonResponse);

#ifdef SWOS_TEST
using LlmTransportFn = std::function<LlmHttpResult(const LlmConfig&, const std::string& payload)>;
void setLlmTransportForTest(LlmTransportFn fn);
#endif
