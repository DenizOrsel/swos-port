#include "llmOptionsMenu.h"
#include "llmClient.h"
#include "menus.h"
#include "textInput.h"
#include "llmOptions.mnu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

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
    languageEntry.copyString(elideDisplay(cfg.language.empty() ? "English" : cfg.language, 20).c_str());

    char numBuf[32];
    std::snprintf(numBuf, sizeof(numBuf), "%d", cfg.timeoutMs);
    timeoutEntry.copyString(numBuf);
    std::snprintf(numBuf, sizeof(numBuf), "%d", cfg.maxTokens);
    maxTokensEntry.copyString(numBuf);
    std::snprintf(numBuf, sizeof(numBuf), "%.2f", cfg.temperature);
    temperatureEntry.copyString(numBuf);

    newspaperEntry.copyString(cfg.features.postMatchNewspaper ? "ON" : "OFF");
    preMatchEntry.copyString(cfg.features.preMatchScouting ? "ON" : "OFF");
    transfersEntry.copyString(cfg.features.transferNegotiations ? "ON" : "OFF");
    dressingRoomEntry.copyString(cfg.features.dressingRoomVoices ? "ON" : "OFF");
    punditEntry.copyString(cfg.features.livePunditTicker ? "ON" : "OFF");
    cpuTacticsEntry.copyString("OFF");
}

static void toggleLlmEnabled()
{
    LlmConfig cfg = getLlmConfig();
    cfg.enabled = !cfg.enabled;
    updateLlmConfig(cfg);
    refreshLlmOptionsMenu();
}

static LlmProvider parseProviderText(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (text == "openai")
        return LlmProvider::kOpenAi;
    if (text == "custom")
        return LlmProvider::kCustom;
    return LlmProvider::kOllama;
}

static void editLlmProvider()
{
    LlmConfig cfg = getLlmConfig();
    providerEntry.copyString(providerLabel(cfg.provider));
    if (inputText(providerEntry, 24, kFullCharset)) {
        cfg.provider = parseProviderText(providerEntry.fg.string.asPtr());
        updateLlmConfig(cfg);
    }
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

static void editLlmLanguage()
{
    LlmConfig cfg = getLlmConfig();
    std::string cur = cfg.language.empty() ? "English" : cfg.language;
    languageEntry.copyString(cur.c_str());
    if (inputText(languageEntry, 32, kFullCharset)) {
        std::string entered = languageEntry.fg.string.asPtr();
        size_t first = entered.find_first_not_of(" \t");
        if (first == std::string::npos) {
            cfg.language = "English";
        } else {
            size_t last = entered.find_last_not_of(" \t");
            cfg.language = entered.substr(first, last - first + 1);
        }
        updateLlmConfig(cfg);
    }
    refreshLlmOptionsMenu();
}

static void editLlmTimeout()
{
    LlmConfig cfg = getLlmConfig();
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", cfg.timeoutMs);
    timeoutEntry.copyString(buf);
    if (inputText(timeoutEntry, 8, kFullCharset)) {
        int val = std::atoi(timeoutEntry.fg.string);
        if (val > 0) {
            cfg.timeoutMs = std::clamp(val, 250, 30000);
            updateLlmConfig(cfg);
        }
    }
    refreshLlmOptionsMenu();
}

static void editLlmMaxTokens()
{
    LlmConfig cfg = getLlmConfig();
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", cfg.maxTokens);
    maxTokensEntry.copyString(buf);
    if (inputText(maxTokensEntry, 8, kFullCharset)) {
        int val = std::atoi(maxTokensEntry.fg.string);
        if (val > 0) {
            cfg.maxTokens = std::clamp(val, 16, 8192);
            updateLlmConfig(cfg);
        }
    }
    refreshLlmOptionsMenu();
}

static void editLlmTemperature()
{
    LlmConfig cfg = getLlmConfig();
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", cfg.temperature);
    temperatureEntry.copyString(buf);
    if (inputText(temperatureEntry, 8, kFullCharset)) {
        float val = static_cast<float>(std::atof(temperatureEntry.fg.string));
        cfg.temperature = std::clamp(val, 0.0f, 2.0f);
        updateLlmConfig(cfg);
    }
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
    // Disabled feature: keep cpuTacticalManager off
    LlmConfig cfg = getLlmConfig();
    if (cfg.features.cpuTacticalManager) {
        cfg.features.cpuTacticalManager = false;
        updateLlmConfig(cfg);
    }
    refreshLlmOptionsMenu();
}

static void exitLlmOptions()
{
    SetExitMenuFlag();
}
