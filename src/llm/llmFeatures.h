#pragma once

#include <string>
#include <vector>

// Step 3A: Post-Match Sports Newspaper & Chairman Reactions
void resetPostMatchNewspaper();
void beginPostMatchNewspaperRequest();
void showPostMatchNewspaperIfReady();

// Step 3B: Pre-Match Assistant Manager / Scout Briefing
void beginPreMatchScoutingRequest(const TeamGame *topTeam, const TeamGame *bottomTeam);
void showPreMatchScoutingIfReady();

// Step 3C: Living Transfer Market & Dressing Room Voices (Career + Hotseat compatible)
std::string fetchTransferNegotiationVoice(const char *buyerClub, const char *sellerClub,
    const char *playerName, const char *offerTerms, bool isCounterOffer);
void onCareerMatchCompletedLlm(int topGoals, int botGoals);
void markMatchHandledInGameLoop();
void onViewResultMatchCompletedLlm();
void onCareerWeeklyTickLlm();
void checkCareerSquadTransfersLlm();
void resetCareerLlmSnapshots();

// Step 3D: Async Live Match Pundit / Commentary Ticker
void resetLivePunditTicker();
void triggerLivePunditEvent(const char *eventLabel, bool highPriority = false);
void updateLivePunditTicker();
void drawLivePunditTicker();

// Text wrapping helper for SWOS modal prompts (max 6 lines)
std::vector<std::string> wrapTextForSwosPrompt(const std::string& text, size_t maxLineLen = 40, size_t maxLines = 6);
