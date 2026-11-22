#include "llmOptionsMenu.h"
#include "llmClient.h"
#include "menus.h"
#include "textInput.h"
#include "llmOptions.mnu.h"

#include <algorithm>
#include <cstdio>

using namespace LlmOptionsMenu;

static void refreshLlmOptionsMenu();

void showLlmOptionsMenu()
{
    showMenu(llmOptionsMenu);
}

static const char *providerLabel(LlmProvider p)
{
    switch (p) {
    case LlmProvider::kOpenAi: return "OPENAI";
    case LlmProvider::kCustom: return "CUSTOM";
    case LlmProvider::kOllama:
    default: return "OLLAMA";
    }
}

static std::string elideDisplay(const std::string& val, size_t maxChars)
{
    if (val.empty())
        return "(NONE)";
    if (val.size() <= maxChars)
        return val;
    return val.substr(0, maxChars - 3) + "...";
}

static void initLlmOptionsMenu()
{
    refreshLlmOptionsMenu();
}

static void refreshLlmOptionsMenu()
{
    const auto& cfg = getLlmConfig();

    enabledEntry.copyString(cfg.enabled ? "ON" : "OFF");
    providerEntry.copyString(providerLabel(cfg.provider));
    endpointEntry.copyString(elideDisplay(cfg.endpoint, 20).c_str());
    modelEntry.copyString(elideDisplay(cfg.model, 20).c_str());
    apiKeyEntry.copyString(cfg.apiKey.empty() ? "(NONE)" : "********");
    timeoutEntry.setNumber(cfg.timeoutMs);
    maxTokensEntry.setNumber(cfg.maxTokens);

    char tempBuf[16];
    std::snprintf(tempBuf, sizeof(tempBuf), "%.1f", cfg.temperature);
    temperatureEntry.copyString(tempBuf);

    newspaperEntry.copyString(cfg.features.postMatchNewspaper ? "ON" : "OFF");
    preMatchEntry.copyString(cfg.features.preMatchScouting ? "ON" : "OFF");
    transfersEntry.copyString(cfg.features.transferNegotiations ? "ON" : "OFF");
    dressingRoomEntry.copyString(cfg.features.dressingRoomVoices ? "ON" : "OFF");
    punditEntry.copyString(cfg.features.livePunditTicker ? "ON" : "OFF");
    cpuTacticsEntry.copyString(cfg.features.cpuTacticalManager ? "ON" : "OFF");
}

static void toggleLlmEnabled()
{
    LlmConfig cfg = getLlmConfig();
    cfg.enabled = !cfg.enabled;
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static void cycleLlmProvider()
{
    LlmConfig cfg = getLlmConfig();
    switch (cfg.provider) {
    case LlmProvider::kOllama: cfg.provider = LlmProvider::kOpenAi; break;
    case LlmProvider::kOpenAi: cfg.provider = LlmProvider::kCustom; break;
    case LlmProvider::kCustom:
    default: cfg.provider = LlmProvider::kOllama; break;
    }
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static void editLlmEndpoint()
{
    LlmConfig cfg = getLlmConfig();
    endpointEntry.copyString(cfg.endpoint.empty() ? "" : elideDisplay(cfg.endpoint, 60).c_str());
    if (inputText(endpointEntry, 60, kFullCharset)) {
        cfg.endpoint = endpointEntry.fg.string;
        updateLlmConfig(cfg);
    }
    refreshLlmOptionsMenu();
}

static void editLlmModel()
{
    LlmConfig cfg = getLlmConfig();
    modelEntry.copyString(cfg.model.empty() ? "" : elideDisplay(cfg.model, 60).c_str());
    if (inputText(modelEntry, 60, kFullCharset)) {
        cfg.model = modelEntry.fg.string;
        updateLlmConfig(cfg);
    }
    refreshLlmOptionsMenu();
}

static void editLlmApiKey()
{
    LlmConfig cfg = getLlmConfig();
    apiKeyEntry.copyString(cfg.apiKey.empty() ? "" : elideDisplay(cfg.apiKey, 60).c_str());
    if (inputText(apiKeyEntry, 60, kFullCharset)) {
        cfg.apiKey = apiKeyEntry.fg.string;
        updateLlmConfig(cfg);
    }
    refreshLlmOptionsMenu();
}

static void editLlmTimeout()
{
    auto entry = A5.as<MenuEntry *>();
    if (inputNumber(*entry, 5, 250, 30000)) {
        LlmConfig cfg = getLlmConfig();
        cfg.timeoutMs = std::clamp(static_cast<int>(entry->fg.number), 250, 30000);
        updateLlmConfig(cfg);
    }
    refreshLlmOptionsMenu();
}

static void editLlmMaxTokens()
{
    auto entry = A5.as<MenuEntry *>();
    if (inputNumber(*entry, 4, 16, 8192)) {
        LlmConfig cfg = getLlmConfig();
        cfg.maxTokens = std::clamp(static_cast<int>(entry->fg.number), 16, 8192);
        updateLlmConfig(cfg);
    }
    refreshLlmOptionsMenu();
}

static void cycleLlmTemperature()
{
    static constexpr float kTemps[] = { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f, 1.2f };
    LlmConfig cfg = getLlmConfig();
    int nextIdx = 0;
    for (int i = 0; i < static_cast<int>(std::size(kTemps)); ++i) {
        if (cfg.temperature < kTemps[i] - 0.05f) {
            nextIdx = i;
            break;
        }
        nextIdx = (i + 1) % static_cast<int>(std::size(kTemps));
    }
    cfg.temperature = kTemps[nextIdx];
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static void toggleFeatureNewspaper()
{
    LlmConfig cfg = getLlmConfig();
    cfg.features.postMatchNewspaper = !cfg.features.postMatchNewspaper;
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static void toggleFeaturePreMatch()
{
    LlmConfig cfg = getLlmConfig();
    cfg.features.preMatchScouting = !cfg.features.preMatchScouting;
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static void toggleFeatureTransfers()
{
    LlmConfig cfg = getLlmConfig();
    cfg.features.transferNegotiations = !cfg.features.transferNegotiations;
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static void toggleFeatureDressingRoom()
{
    LlmConfig cfg = getLlmConfig();
    cfg.features.dressingRoomVoices = !cfg.features.dressingRoomVoices;
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static void toggleFeatureLivePundit()
{
    LlmConfig cfg = getLlmConfig();
    cfg.features.livePunditTicker = !cfg.features.livePunditTicker;
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static void toggleFeatureCpuTactics()
{
    LlmConfig cfg = getLlmConfig();
    cfg.features.cpuTacticalManager = !cfg.features.cpuTacticalManager;
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static void exitLlmOptions()
{
    SetExitMenuFlag();
}
