#include "voiceInput.h"
#include "llmClient.h"
#include "log.h"

#include <SDL.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace {

constexpr uint32_t kCaptureSampleRate = 16000;
constexpr size_t kMaxCaptureSeconds = 60;
constexpr size_t kMaxCaptureSamples = kCaptureSampleRate * kMaxCaptureSeconds;
constexpr size_t kMinCaptureSamples = kCaptureSampleRate / 4; // 0.25s minimum

std::string trimCopy(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string toLowerAscii(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

#if defined(_WIN32) && !defined(SWOS_TEST)

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

bool fileExistsW(const std::wstring& path)
{
    if (path.empty())
        return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

std::wstring parentDirW(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? std::wstring() : path.substr(0, pos);
}

std::wstring getExeDirectory()
{
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return L".";
    return parentDirW(std::wstring(buf, len));
}

std::wstring resolveWhisperFilePath(const std::string& configuredPath)
{
    if (configuredPath.empty())
        return {};
    std::wstring p = utf8ToWide(configuredPath);
    if (p.size() >= 2 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\'))) {
        return fileExistsW(p) ? p : std::wstring();
    }
    std::wstring nearExe = getExeDirectory() + L"\\" + p;
    if (fileExistsW(nearExe))
        return nearExe;
    if (fileExistsW(p)) {
        wchar_t fullBuf[MAX_PATH] = {};
        DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH, fullBuf, nullptr);
        return (n > 0 && n < MAX_PATH) ? std::wstring(fullBuf, n) : p;
    }
    return {};
}

std::string resolveWhisperLangCode(const LlmConfig& cfg)
{
    std::string wl = toLowerAscii(trimCopy(cfg.voice.whisperLanguage));
    if (!wl.empty() && wl != "auto")
        return wl;
    std::string ll = toLowerAscii(trimCopy(cfg.language));
    if (ll.find("turk") != std::string::npos)
        return "tr";
    return "auto";
}

bool transcribeViaWhisperCli(
    const std::wstring& cliPath,
    const std::wstring& modelPath,
    const std::vector<uint8_t>& wavBytes,
    const std::string& langCode,
    std::string& outText,
    std::string& outError)
{
    wchar_t tmpBuf[MAX_PATH] = {};
    DWORD tmpLen = GetTempPathW(MAX_PATH, tmpBuf);
    std::wstring tempDir = (tmpLen > 0 && tmpLen < MAX_PATH) ? std::wstring(tmpBuf, tmpLen) : (getExeDirectory() + L"\\");
    if (!tempDir.empty() && tempDir.back() != L'\\' && tempDir.back() != L'/')
        tempDir.push_back(L'\\');

    DWORD pid = GetCurrentProcessId();
    DWORD tick = GetTickCount();
    std::wstring baseName = L"swos_ptt_" + std::to_wstring(pid) + L"_" + std::to_wstring(tick);
    std::wstring wavPath = tempDir + baseName + L".wav";
    std::wstring outPrefix = tempDir + baseName + L"_out";
    std::wstring txtPath = outPrefix + L".txt";

    {
        FILE *fp = nullptr;
        if (_wfopen_s(&fp, wavPath.c_str(), L"wb") != 0 || !fp) {
            outError = "Could not write temporary WAV file.";
            return false;
        }
        size_t written = std::fwrite(wavBytes.data(), 1, wavBytes.size(), fp);
        std::fclose(fp);
        if (written != wavBytes.size()) {
            DeleteFileW(wavPath.c_str());
            outError = "Could not write temporary WAV file.";
            return false;
        }
    }

    std::wstring cmd = L"\"" + cliPath + L"\" -m \"" + modelPath +
                       L"\" -f \"" + wavPath + L"\" -l " + utf8ToWide(langCode) +
                       L" -nt --no-prints -otxt -of \"" + outPrefix + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    std::wstring workDir = parentDirW(cliPath);
    BOOL ok = CreateProcessW(
        cliPath.c_str(),
        cmdBuf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workDir.empty() ? nullptr : workDir.c_str(),
        &si, &pi);

    if (!ok) {
        DeleteFileW(wavPath.c_str());
        outError = "Failed to launch whisper-cli.exe.";
        return false;
    }

    DWORD waitRes = WaitForSingleObject(pi.hProcess, 45000);
    DWORD exitCode = 1;
    if (waitRes == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        DeleteFileW(wavPath.c_str());
        DeleteFileW(txtPath.c_str());
        outError = "Whisper CLI timed out.";
        return false;
    }
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::string rawTxt;
    if (fileExistsW(txtPath)) {
        FILE *fp = nullptr;
        if (_wfopen_s(&fp, txtPath.c_str(), L"rb") == 0 && fp) {
            std::fseek(fp, 0, SEEK_END);
            long sz = std::ftell(fp);
            std::fseek(fp, 0, SEEK_SET);
            if (sz > 0 && sz < 1024 * 1024) {
                rawTxt.resize(static_cast<size_t>(sz));
                size_t rd = std::fread(rawTxt.data(), 1, static_cast<size_t>(sz), fp);
                rawTxt.resize(rd);
            }
            std::fclose(fp);
        }
    }

    DeleteFileW(wavPath.c_str());
    DeleteFileW(txtPath.c_str());

    if (exitCode != 0 && rawTxt.empty()) {
        outError = "whisper-cli.exe exited with error (" + std::to_string(exitCode) + ").";
        return false;
    }

    outText = extractWhisperTextFromJsonOrPlain(rawTxt);
    if (outText.empty()) {
        outError = "No speech detected in recording.";
        return false;
    }
    return true;
}

bool transcribeViaWhisperHttp(
    const std::string& endpoint,
    const std::vector<uint8_t>& wavBytes,
    const std::string& langCode,
    std::string& outText,
    std::string& outError)
{
    if (endpoint.empty())
        return false;

    std::wstring wUrl = utf8ToWide(endpoint);
    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostBuf[256] = {};
    wchar_t pathBuf[512] = {};
    urlComp.lpszHostName = hostBuf;
    urlComp.dwHostNameLength = static_cast<DWORD>(std::size(hostBuf));
    urlComp.lpszUrlPath = pathBuf;
    urlComp.dwUrlPathLength = static_cast<DWORD>(std::size(pathBuf));

    if (!WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.size()), 0, &urlComp)) {
        outError = "Invalid whisperEndpoint URL.";
        return false;
    }

    const std::string boundary = "----SwosPortWhisperBoundary9697";
    std::string body;
    body.reserve(wavBytes.size() + 512);
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n";
    body += "Content-Type: audio/wav\r\n\r\n";
    body.append(reinterpret_cast<const char *>(wavBytes.data()), wavBytes.size());
    body += "\r\n--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n";
    if (!langCode.empty() && langCode != "auto") {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"language\"\r\n\r\n" + langCode + "\r\n";
    }
    body += "--" + boundary + "--\r\n";

    HINTERNET hSession = WinHttpOpen(L"SWOS-Port-Voice/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
        return false;

    WinHttpSetTimeouts(hSession, 2500, 3000, 15000, 30000);
    HINTERNET hConnect = WinHttpConnect(hSession, hostBuf, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"POST", pathBuf, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring hdr = L"Content-Type: multipart/form-data; boundary=" + utf8ToWide(boundary);
    BOOL sent = WinHttpSendRequest(hReq, hdr.c_str(), static_cast<DWORD>(hdr.size()),
        const_cast<char *>(body.data()), static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    std::string resp;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        std::string chunk(avail, '\0');
        DWORD readBytes = 0;
        if (WinHttpReadData(hReq, chunk.data(), avail, &readBytes) && readBytes > 0) {
            chunk.resize(readBytes);
            resp += chunk;
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (statusCode < 200 || statusCode >= 300) {
        outError = "Whisper server returned HTTP " + std::to_string(statusCode) + ".";
        return false;
    }

    outText = extractWhisperTextFromJsonOrPlain(resp);
    if (outText.empty()) {
        outError = "No speech detected in recording.";
        return false;
    }
    return true;
}

#endif

struct VoiceSession {
    std::mutex audioMutex;
    std::vector<int16_t> samples;
    SDL_AudioDeviceID captureDev = 0;
    std::atomic<VoiceCaptureState> state{ VoiceCaptureState::kIdle };
    std::atomic<float> level{ 0.0f };
    std::atomic<uint32_t> recordedSamples{ 0 };

    std::mutex resultMutex;
    bool hasResult = false;
    bool resultOk = false;
    std::string resultTextOrError;

    std::thread worker;
} s_voice;

void SDLCALL voiceCaptureCallback(void * /*userdata*/, Uint8 *stream, int len)
{
    if (!stream || len <= 0)
        return;
    const auto *pcm = reinterpret_cast<const int16_t *>(stream);
    size_t count = static_cast<size_t>(len) / sizeof(int16_t);
    if (count == 0)
        return;

    double sumSq = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double v = static_cast<double>(pcm[i]) / 32768.0;
        sumSq += v * v;
    }
    float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(count)));
    float boosted = std::clamp(rms * 4.5f, 0.0f, 1.0f);
    s_voice.level.store(boosted, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(s_voice.audioMutex);
    if (s_voice.samples.size() < kMaxCaptureSamples) {
        size_t take = std::min(count, kMaxCaptureSamples - s_voice.samples.size());
        s_voice.samples.insert(s_voice.samples.end(), pcm, pcm + take);
        s_voice.recordedSamples.store(static_cast<uint32_t>(s_voice.samples.size()), std::memory_order_relaxed);
    }
}

void joinWorkerIfNeeded()
{
    if (s_voice.worker.joinable())
        s_voice.worker.join();
}

} // namespace

std::vector<uint8_t> buildPcm16MonoWav(const int16_t *samples, size_t sampleCount, uint32_t sampleRate)
{
    const uint16_t numChannels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    const uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    const uint32_t dataSize = static_cast<uint32_t>(sampleCount * sizeof(int16_t));
    const uint32_t riffSize = 36u + dataSize;

    std::vector<uint8_t> wav;
    wav.reserve(44u + dataSize);
    auto pushU16 = [&](uint16_t v) {
        wav.push_back(static_cast<uint8_t>(v & 0xFFu));
        wav.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    };
    auto pushU32 = [&](uint32_t v) {
        wav.push_back(static_cast<uint8_t>(v & 0xFFu));
        wav.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        wav.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        wav.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    };
    auto pushTag = [&](const char *tag) {
        wav.insert(wav.end(), tag, tag + 4);
    };

    pushTag("RIFF");
    pushU32(riffSize);
    pushTag("WAVE");
    pushTag("fmt ");
    pushU32(16u);
    pushU16(1u); // PCM
    pushU16(numChannels);
    pushU32(sampleRate);
    pushU32(byteRate);
    pushU16(blockAlign);
    pushU16(bitsPerSample);
    pushTag("data");
    pushU32(dataSize);

    if (samples && sampleCount > 0) {
        const auto *raw = reinterpret_cast<const uint8_t *>(samples);
        wav.insert(wav.end(), raw, raw + dataSize);
    }
    return wav;
}

std::string extractWhisperTextFromJsonOrPlain(const std::string& raw)
{
    std::string s = trimCopy(raw);
    if (s.empty())
        return {};

    // If JSON response containing "text": "..."
    if (!s.empty() && s.front() == '{') {
        size_t keyPos = s.find("\"text\"");
        if (keyPos != std::string::npos) {
            size_t colon = s.find(':', keyPos + 6);
            if (colon != std::string::npos) {
                size_t q1 = s.find('"', colon + 1);
                if (q1 != std::string::npos) {
                    std::string decoded;
                    bool esc = false;
                    for (size_t i = q1 + 1; i < s.size(); ++i) {
                        char c = s[i];
                        if (esc) {
                            if (c == 'n' || c == 'r' || c == 't')
                                decoded.push_back(' ');
                            else
                                decoded.push_back(c);
                            esc = false;
                        } else if (c == '\\') {
                            esc = true;
                        } else if (c == '"') {
                            break;
                        } else {
                            decoded.push_back(c);
                        }
                    }
                    s = std::move(decoded);
                }
            }
        }
    }

    // Strip Whisper bracketed markers like [BLANK_AUDIO], [00:00:00.000 --> 00:00:03.000], (music)
    std::string clean;
    clean.reserve(s.size());
    bool inBracket = false;
    bool prevSpace = false;
    for (char c : s) {
        if (c == '[') {
            inBracket = true;
            continue;
        }
        if (c == ']') {
            inBracket = false;
            continue;
        }
        if (inBracket)
            continue;
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
        if (static_cast<unsigned char>(c) < 32)
            continue;
        if (c == ' ') {
            if (!prevSpace && !clean.empty()) {
                clean.push_back(' ');
                prevSpace = true;
            }
        } else {
            clean.push_back(c);
            prevSpace = false;
        }
    }
    return trimCopy(clean);
}

bool startVoiceRecording(std::string& outError)
{
#ifdef SWOS_TEST
    outError = "Voice capture unavailable in test builds.";
    return false;
#else
    if (s_voice.state.load() != VoiceCaptureState::kIdle) {
        outError = "Voice capture is already busy.";
        return false;
    }

    joinWorkerIfNeeded();

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            outError = std::string("Audio init failed: ") + SDL_GetError();
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_voice.audioMutex);
        s_voice.samples.clear();
        s_voice.samples.reserve(kCaptureSampleRate * 10);
        s_voice.recordedSamples.store(0, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lock(s_voice.resultMutex);
        s_voice.hasResult = false;
        s_voice.resultOk = false;
        s_voice.resultTextOrError.clear();
    }

    SDL_AudioSpec want{};
    want.freq = static_cast<int>(kCaptureSampleRate);
    want.format = AUDIO_S16LSB;
    want.channels = 1;
    want.samples = 1024;
    want.callback = voiceCaptureCallback;

    SDL_AudioSpec have{};
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 1 /* iscapture */, &want, &have, 0);
    if (dev == 0) {
        outError = std::string("No microphone found (") + SDL_GetError() + ")";
        logWarn("Voice capture failed to open microphone: %s", SDL_GetError());
        return false;
    }

    s_voice.captureDev = dev;
    s_voice.level.store(0.0f, std::memory_order_relaxed);
    s_voice.state.store(VoiceCaptureState::kRecording);
    SDL_PauseAudioDevice(dev, 0);
    logInfo("Voice push-to-talk recording started (16kHz mono PCM).");
    return true;
#endif
}

bool stopVoiceRecordingAndTranscribeAsync(std::string& outError)
{
#ifdef SWOS_TEST
    outError = "Voice capture unavailable in test builds.";
    return false;
#else
    if (s_voice.state.load() != VoiceCaptureState::kRecording)
        return false;

    if (s_voice.captureDev != 0) {
        SDL_PauseAudioDevice(s_voice.captureDev, 1);
        SDL_CloseAudioDevice(s_voice.captureDev);
        s_voice.captureDev = 0;
    }
    s_voice.level.store(0.0f, std::memory_order_relaxed);

    std::vector<int16_t> captured;
    {
        std::lock_guard<std::mutex> lock(s_voice.audioMutex);
        captured.swap(s_voice.samples);
    }

    if (captured.size() < kMinCaptureSamples) {
        s_voice.state.store(VoiceCaptureState::kIdle);
        outError = "Recording too short - hold Ctrl+M (or F2) while speaking.";
        return false;
    }

    std::vector<uint8_t> wavBytes = buildPcm16MonoWav(captured.data(), captured.size(), kCaptureSampleRate);
    LlmConfig cfgCopy = getLlmConfig();
    s_voice.state.store(VoiceCaptureState::kTranscribing);

    joinWorkerIfNeeded();
    s_voice.worker = std::thread([wav = std::move(wavBytes), cfg = std::move(cfgCopy)]() {
        std::string text;
        std::string err;
        bool ok = false;

#ifdef _WIN32
        std::string langCode = resolveWhisperLangCode(cfg);
        auto cliPath = resolveWhisperFilePath(cfg.voice.whisperCliPath);
        auto modelPath = resolveWhisperFilePath(cfg.voice.whisperModelPath);

        if (!cliPath.empty() && !modelPath.empty()) {
            ok = transcribeViaWhisperCli(cliPath, modelPath, wav, langCode, text, err);
        } else {
            ok = transcribeViaWhisperHttp(cfg.voice.whisperEndpoint, wav, langCode, text, err);
            if (!ok && err.empty()) {
                err = "Local Whisper not found: put whisper-cli.exe + ggml-base.bin in whisper\\ folder (or run whisper-server).";
            }
        }
#else
        err = "Local Whisper transcription is currently supported on Windows builds.";
#endif

        {
            std::lock_guard<std::mutex> lock(s_voice.resultMutex);
            s_voice.hasResult = true;
            s_voice.resultOk = ok;
            s_voice.resultTextOrError = ok ? text : err;
        }
        s_voice.state.store(VoiceCaptureState::kIdle);
        if (ok)
            logInfo("Voice transcription succeeded (%zu chars).", text.size());
        else
            logWarn("Voice transcription failed: %s", err.c_str());
    });
    return true;
#endif
}

void cancelVoiceRecording()
{
#ifndef SWOS_TEST
    if (s_voice.captureDev != 0) {
        SDL_PauseAudioDevice(s_voice.captureDev, 1);
        SDL_CloseAudioDevice(s_voice.captureDev);
        s_voice.captureDev = 0;
    }
    if (s_voice.state.load() == VoiceCaptureState::kRecording)
        s_voice.state.store(VoiceCaptureState::kIdle);
    s_voice.level.store(0.0f, std::memory_order_relaxed);
    joinWorkerIfNeeded();
#endif
}

VoiceCaptureState getVoiceCaptureState()
{
    return s_voice.state.load();
}

float getVoiceInputLevel()
{
    return s_voice.level.load(std::memory_order_relaxed);
}

float getVoiceRecordingSeconds()
{
    return static_cast<float>(s_voice.recordedSamples.load(std::memory_order_relaxed)) /
           static_cast<float>(kCaptureSampleRate);
}

bool pollVoiceTranscriptionResult(bool& outOk, std::string& outTextOrError)
{
    std::lock_guard<std::mutex> lock(s_voice.resultMutex);
    if (!s_voice.hasResult)
        return false;
    outOk = s_voice.resultOk;
    outTextOrError = std::move(s_voice.resultTextOrError);
    s_voice.hasResult = false;
    return true;
}
