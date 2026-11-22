#pragma once

#include <string>
#include <vector>

enum class SupporterSide {
    kHome,
    kAway,
    kNeutral,
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
void updateSupporterStream();
void drawSupporterStreamOverlay();
void toggleSupporterStreamVisible();
bool isSupporterStreamVisible();

// Exposed for unit testing
std::vector<SupporterTweet> parseSupporterTweetBatch(const std::string& rawResponse);
