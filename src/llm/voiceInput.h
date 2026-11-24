#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class VoiceCaptureState : uint8_t {
    kIdle = 0,
    kRecording = 1,
    kTranscribing = 2,
};

// Pure testable helpers
std::vector<uint8_t> buildPcm16MonoWav(const int16_t *samples, size_t sampleCount, uint32_t sampleRate = 16000);
std::string extractWhisperTextFromJsonOrPlain(const std::string& raw);

// Live SDL2 microphone capture + async local Whisper transcription
bool startVoiceRecording(std::string& outError);
bool stopVoiceRecordingAndTranscribeAsync(std::string& outError);
void cancelVoiceRecording();
VoiceCaptureState getVoiceCaptureState();
float getVoiceInputLevel();
float getVoiceRecordingSeconds();
bool pollVoiceTranscriptionResult(bool& outOk, std::string& outTextOrError);
