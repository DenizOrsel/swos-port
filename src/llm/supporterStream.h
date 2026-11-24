#pragma once

#include <string>
#include <vector>

enum class SupporterSide {
    kHome,
    kAway,
    kNeutral,
    kPundit,
};

struct SupporterTweet {
    std::string handle;
    SupporterSide side = SupporterSide::kNeutral;
    std::string message;
    int avatarVariant = 0;
    int ageFrames = 0;
};

void resetSupporterStream();
void triggerSupporterStreamEvent(const char *eventLabel, bool highPriority = false);
void pushPunditMessageToStream(const std::string& rawMessage);
void updateSupporterStream();
void drawSupporterStreamOverlay(float cameraXOffset = 0.0f, float cameraYOffset = 0.0f);
void toggleSupporterStreamVisible();
bool isSupporterStreamVisible();

// Exposed for unit testing
std::vector<SupporterTweet> parseSupporterTweetBatch(const std::string& rawResponse);
float computeSupporterOverlayProximityAlpha(
    float pointX, float pointY,
    float boxX, float boxY, float boxW, float boxH,
    float fadeMarginPx);
