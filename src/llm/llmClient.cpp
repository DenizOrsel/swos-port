#include "llmClient.h"
#include "file.h"
#include "log.h"
#include "SimpleIni.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {

constexpr char kAiIniFilename[] = "ai.ini";

struct PendingRequest {
    uint64_t id = 0;
    LlmConfig config;
    std::string systemPrompt;
    std::string userPrompt;
};

struct CompletedResponse {
    bool ok = false;
    std::string text;
};

LlmConfig s_config;
std::mutex s_configMutex;

std::mutex s_queueMutex;
std::condition_variable s_queueCv;
std::condition_variable s_doneCv;
std::deque<PendingRequest> s_requestQueue;
std::unordered_map<uint64_t, CompletedResponse> s_completedResponses;
std::atomic<uint64_t> s_nextRequestId{ 1 };
bool s_workerRunning = false;
bool s_stopWorker = false;
std::thread s_workerThread;

#ifdef SWOS_TEST
LlmTransportFn s_testTransport;
#endif

std::string trimCopy(const std::string& s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

std::string toLowerCopy(std::string s)
{
    for (char& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

LlmProvider parseProvider(const char *val)
{
    if (!val)
        return LlmProvider::kOllama;
    std::string lower = toLowerCopy(trimCopy(val));
    if (lower == "openai")
        return LlmProvider::kOpenAi;
    if (lower == "custom")
        return LlmProvider::kCustom;
    return LlmProvider::kOllama;
}

const char *providerToString(LlmProvider p)
{
    switch (p) {
    case LlmProvider::kOpenAi:
        return "openai";
    case LlmProvider::kCustom:
        return "custom";
    case LlmProvider::kOllama:
    default:
        return "ollama";
    }
}

void writeDefaultAiIni(const std::string& path)
{
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.SetBoolValue("llm", "enabled", s_config.enabled);
    ini.SetValue("llm", "provider", providerToString(s_config.provider));
    ini.SetValue("llm", "endpoint", s_config.endpoint.c_str());
    ini.SetValue("llm", "model", s_config.model.c_str());
    ini.SetValue("llm", "apiKey", s_config.apiKey.c_str());
    ini.SetLongValue("llm", "timeoutMs", s_config.timeoutMs);
    ini.SetLongValue("llm", "maxTokens", s_config.maxTokens);
    ini.SetDoubleValue("llm", "temperature", s_config.temperature);

    ini.SetBoolValue("features", "postMatchNewspaper", s_config.features.postMatchNewspaper);
    ini.SetBoolValue("features", "preMatchScouting", s_config.features.preMatchScouting);
    ini.SetBoolValue("features", "transferNegotiations", s_config.features.transferNegotiations);
    ini.SetBoolValue("features", "dressingRoomVoices", s_config.features.dressingRoomVoices);
    ini.SetBoolValue("features", "livePunditTicker", s_config.features.livePunditTicker);
    ini.SetBoolValue("features", "cpuTacticalManager", s_config.features.cpuTacticalManager);

    SI_Error rc = ini.SaveFile(path.c_str());
    if (rc < 0)
        logWarn("Failed to write default %s", path.c_str());
    else
        logInfo("Created default LLM configuration at %s", path.c_str());
}

std::wstring utf8ToWide(const std::string& str)
{
    if (str.empty())
        return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (needed <= 0)
        return {};
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), wide.data(), needed);
    return wide;
}

LlmHttpResult performWinHttpPost(const LlmConfig& cfg, const std::string& payload)
{
    LlmHttpResult result;

    std::wstring wUrl = utf8ToWide(cfg.endpoint);
    if (wUrl.empty())
        return result;

    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostBuf[256] = {};
    wchar_t pathBuf[1024] = {};
    urlComp.lpszHostName = hostBuf;
    urlComp.dwHostNameLength = static_cast<DWORD>(std::size(hostBuf));
    urlComp.lpszUrlPath = pathBuf;
    urlComp.dwUrlPathLength = static_cast<DWORD>(std::size(pathBuf));

    if (!WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.size()), 0, &urlComp))
        return result;

    HINTERNET hSession = WinHttpOpen(L"swos-port-llm/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
        return result;

    int timeout = std::clamp(cfg.timeoutMs, 250, 30000);
    WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

    HINTERNET hConnect = WinHttpConnect(hSession, hostBuf, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD reqFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST",
        pathBuf[0] ? pathBuf : L"/", nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!cfg.apiKey.empty()) {
        headers += L"Authorization: Bearer ";
        headers += utf8ToWide(cfg.apiKey);
        headers += L"\r\n";
    }

    BOOL sent = WinHttpSendRequest(hRequest,
        headers.c_str(), static_cast<DWORD>(headers.size()),
        const_cast<char *>(payload.data()), static_cast<DWORD>(payload.size()),
        static_cast<DWORD>(payload.size()), 0);

    if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
        result.statusCode = static_cast<int>(statusCode);

        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            std::string chunk(bytesAvailable, '\0');
            DWORD bytesRead = 0;
            if (!WinHttpReadData(hRequest, chunk.data(), bytesAvailable, &bytesRead) || bytesRead == 0)
                break;
            chunk.resize(bytesRead);
            result.body += chunk;
            if (result.body.size() > 262144)
                break;
        }
        result.ok = (result.statusCode >= 200 && result.statusCode < 300);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// Extracts a JSON string value starting at the opening quote `pos` (returns false if invalid).
bool parseJsonStringAt(const std::string& json, size_t quotePos, std::string& out, size_t& outEndPos)
{
    if (quotePos >= json.size() || json[quotePos] != '"')
        return false;

    out.clear();
    size_t i = quotePos + 1;
    while (i < json.size()) {
        char ch = json[i++];
        if (ch == '"') {
            outEndPos = i;
            return true;
        }
        if (ch == '\\' && i < json.size()) {
            char esc = json[i++];
            switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
                if (i + 4 <= json.size()) {
                    unsigned int cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = json[i + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned int>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned int>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned int>(h - 'A' + 10);
                    }
                    i += 4;
                    if (cp >= 0x20 && cp <= 0x7E)
                        out.push_back(static_cast<char>(cp));
                    else if (cp == 0x2019 || cp == 0x2018)
                        out.push_back('\'');
                    else if (cp == 0x201C || cp == 0x201D)
                        out.push_back('"');
                    else if (cp == 0x2013 || cp == 0x2014)
                        out.push_back('-');
                    else
                        out.push_back(' ');
                }
                break;
            default:
                out.push_back(esc);
                break;
            }
        } else {
            out.push_back(ch);
        }
    }
    return false;
}

bool findJsonStringField(const std::string& json, size_t searchFrom, const std::string& key, std::string& outValue)
{
    std::string pattern = "\"" + key + "\"";
    size_t pos = searchFrom;
    while (pos < json.size()) {
        size_t keyPos = json.find(pattern, pos);
        if (keyPos == std::string::npos)
            return false;
        size_t afterKey = keyPos + pattern.size();
        while (afterKey < json.size() && std::isspace(static_cast<unsigned char>(json[afterKey])))
            ++afterKey;
        if (afterKey < json.size() && json[afterKey] == ':') {
            ++afterKey;
            while (afterKey < json.size() && std::isspace(static_cast<unsigned char>(json[afterKey])))
                ++afterKey;
            if (afterKey < json.size() && json[afterKey] == '"') {
                size_t endPos = 0;
                if (parseJsonStringAt(json, afterKey, outValue, endPos))
                    return true;
            }
        }
        pos = afterKey;
    }
    return false;
}

void workerLoop()
{
    while (true) {
        PendingRequest req;
        {
            std::unique_lock<std::mutex> lock(s_queueMutex);
            s_queueCv.wait(lock, [] { return s_stopWorker || !s_requestQueue.empty(); });
            if (s_stopWorker && s_requestQueue.empty())
                break;
            req = std::move(s_requestQueue.front());
            s_requestQueue.pop_front();
        }

        CompletedResponse resp;
        if (req.config.enabled) {
            std::string payload = buildLlmChatPayload(req.config, req.systemPrompt, req.userPrompt);
            LlmHttpResult http;
#ifdef SWOS_TEST
            if (s_testTransport)
                http = s_testTransport(req.config, payload);
            else
                http = performWinHttpPost(req.config, payload);
#else
            http = performWinHttpPost(req.config, payload);
#endif
            if (http.ok) {
                std::string extracted = trimCopy(extractLlmAssistantContent(req.config.provider, http.body));
                if (!extracted.empty()) {
                    resp.ok = true;
                    resp.text = std::move(extracted);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(s_queueMutex);
            if (s_completedResponses.size() > 64)
                s_completedResponses.clear();
            s_completedResponses[req.id] = std::move(resp);
        }
        s_doneCv.notify_all();
    }
}

} // namespace

void initLlmClient()
{
    loadLlmConfig();

    std::lock_guard<std::mutex> lock(s_queueMutex);
    if (!s_workerRunning) {
        s_stopWorker = false;
        s_workerRunning = true;
        s_workerThread = std::thread(workerLoop);
    }
}

void shutdownLlmClient()
{
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        if (!s_workerRunning)
            return;
        s_stopWorker = true;
    }
    s_queueCv.notify_all();
    if (s_workerThread.joinable())
        s_workerThread.join();

    std::lock_guard<std::mutex> lock(s_queueMutex);
    s_workerRunning = false;
    s_requestQueue.clear();
    s_completedResponses.clear();
}

bool loadLlmConfig(const char *iniPath /* = nullptr */)
{
    std::string resolvedPath = iniPath ? std::string(iniPath) : pathInRootDir(kAiIniFilename);

    std::lock_guard<std::mutex> lock(s_configMutex);
    s_config = LlmConfig{};

    if (!fileExists(resolvedPath.c_str())) {
        if (!iniPath)
            writeDefaultAiIni(resolvedPath);
        return false;
    }

    CSimpleIniA ini;
    ini.SetUnicode();
    if (ini.LoadFile(resolvedPath.c_str()) < 0) {
        logWarn("Failed to parse %s, using defaults", resolvedPath.c_str());
        return false;
    }

    s_config.enabled = ini.GetBoolValue("llm", "enabled", s_config.enabled);
    s_config.provider = parseProvider(ini.GetValue("llm", "provider", "ollama"));
    s_config.endpoint = trimCopy(ini.GetValue("llm", "endpoint", s_config.endpoint.c_str()));
    s_config.model = trimCopy(ini.GetValue("llm", "model", s_config.model.c_str()));
    s_config.apiKey = trimCopy(ini.GetValue("llm", "apiKey", ""));
    s_config.timeoutMs = std::clamp(static_cast<int>(ini.GetLongValue("llm", "timeoutMs", s_config.timeoutMs)), 250, 30000);
    s_config.maxTokens = std::clamp(static_cast<int>(ini.GetLongValue("llm", "maxTokens", s_config.maxTokens)), 16, 8192);
    s_config.temperature = static_cast<float>(std::clamp(ini.GetDoubleValue("llm", "temperature", s_config.temperature), 0.0, 2.0));

    s_config.features.postMatchNewspaper = ini.GetBoolValue("features", "postMatchNewspaper", s_config.features.postMatchNewspaper);
    s_config.features.preMatchScouting = ini.GetBoolValue("features", "preMatchScouting", s_config.features.preMatchScouting);
    s_config.features.transferNegotiations = ini.GetBoolValue("features", "transferNegotiations", s_config.features.transferNegotiations);
    s_config.features.dressingRoomVoices = ini.GetBoolValue("features", "dressingRoomVoices", s_config.features.dressingRoomVoices);
    s_config.features.livePunditTicker = ini.GetBoolValue("features", "livePunditTicker", s_config.features.livePunditTicker);
    s_config.features.cpuTacticalManager = ini.GetBoolValue("features", "cpuTacticalManager", s_config.features.cpuTacticalManager);

    logInfo("Loaded LLM config (%s, provider=%s, model=%s, enabled=%s)",
        resolvedPath.c_str(), providerToString(s_config.provider), s_config.model.c_str(),
        s_config.enabled ? "true" : "false");
    return true;
}

const LlmConfig& getLlmConfig()
{
    std::lock_guard<std::mutex> lock(s_configMutex);
    return s_config;
}

void setLlmConfigForTest(const LlmConfig& cfg)
{
    std::lock_guard<std::mutex> lock(s_configMutex);
    s_config = cfg;
}

uint64_t enqueueLlmRequest(const std::string& systemPrompt, const std::string& userPrompt)
{
    LlmConfig cfgCopy;
    {
        std::lock_guard<std::mutex> lock(s_configMutex);
        cfgCopy = s_config;
    }
    if (!cfgCopy.enabled || cfgCopy.endpoint.empty() || cfgCopy.model.empty())
        return 0;

    uint64_t reqId = s_nextRequestId.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        if (!s_workerRunning) {
            s_stopWorker = false;
            s_workerRunning = true;
            s_workerThread = std::thread(workerLoop);
        }
        // Keep queue bounded so rapid events never pile up
        if (s_requestQueue.size() >= 8)
            s_requestQueue.pop_front();
        s_requestQueue.push_back({ reqId, std::move(cfgCopy), systemPrompt, userPrompt });
    }
    s_queueCv.notify_one();
    return reqId;
}

bool tryTakeLlmResponse(uint64_t requestId, std::string& outText)
{
    if (requestId == 0)
        return false;

    std::lock_guard<std::mutex> lock(s_queueMutex);
    auto it = s_completedResponses.find(requestId);
    if (it == s_completedResponses.end())
        return false;
    bool ok = it->second.ok;
    if (ok)
        outText = std::move(it->second.text);
    s_completedResponses.erase(it);
    return ok;
}

bool waitForLlmResponse(uint64_t requestId, int maxWaitMs, std::string& outText)
{
    if (requestId == 0)
        return false;

    std::unique_lock<std::mutex> lock(s_queueMutex);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, maxWaitMs));
    s_doneCv.wait_until(lock, deadline, [requestId] {
        return s_completedResponses.find(requestId) != s_completedResponses.end() || s_stopWorker;
    });

    auto it = s_completedResponses.find(requestId);
    if (it == s_completedResponses.end())
        return false;
    bool ok = it->second.ok;
    if (ok)
        outText = std::move(it->second.text);
    s_completedResponses.erase(it);
    return ok;
}

std::string escapeLlmJson(const std::string& input)
{
    std::string out;
    out.reserve(input.size() + 16);
    for (unsigned char ch : input) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                out += buf;
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return out;
}

std::string buildLlmChatPayload(const LlmConfig& cfg, const std::string& systemPrompt, const std::string& userPrompt)
{
    char numBuf[32];
    std::snprintf(numBuf, sizeof(numBuf), "%.2f", cfg.temperature);

    std::string json = "{\"model\":\"" + escapeLlmJson(cfg.model) + "\",";
    if (cfg.provider == LlmProvider::kOllama) {
        json += "\"stream\":false,";
        json += "\"options\":{\"num_predict\":" + std::to_string(cfg.maxTokens) + ",\"temperature\":" + numBuf + "},";
    } else {
        json += "\"max_tokens\":" + std::to_string(cfg.maxTokens) + ",\"temperature\":" + numBuf + ",";
    }
    json += "\"messages\":[";
    json += "{\"role\":\"system\",\"content\":\"" + escapeLlmJson(systemPrompt) + "\"},";
    json += "{\"role\":\"user\",\"content\":\"" + escapeLlmJson(userPrompt) + "\"}";
    json += "]}";
    return json;
}

std::string extractLlmAssistantContent(LlmProvider provider, const std::string& jsonResponse)
{
    std::string out;
    if (provider == LlmProvider::kOllama) {
        size_t msgPos = jsonResponse.find("\"message\"");
        if (msgPos != std::string::npos && findJsonStringField(jsonResponse, msgPos, "content", out))
            return out;
        if (findJsonStringField(jsonResponse, 0, "response", out))
            return out;
        if (findJsonStringField(jsonResponse, 0, "content", out))
            return out;
        return {};
    }

    size_t choicesPos = jsonResponse.find("\"choices\"");
    if (choicesPos != std::string::npos && findJsonStringField(jsonResponse, choicesPos, "content", out))
        return out;
    if (findJsonStringField(jsonResponse, 0, "content", out))
        return out;
    return {};
}

#ifdef SWOS_TEST
void setLlmTransportForTest(LlmTransportFn fn)
{
    std::lock_guard<std::mutex> lock(s_queueMutex);
    s_testTransport = std::move(fn);
}
#endif
