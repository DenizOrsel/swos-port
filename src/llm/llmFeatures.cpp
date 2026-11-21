#include "llmFeatures.h"
#include "llmClient.h"
#include "continueAbortMenu.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "pitch.h"
#include "result.h"
#include "stats.h"
#include "swos.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace {

uint64_t s_postMatchRequestId = 0;
bool s_postMatchTriggered = false;
int s_weeklyTickCounter = 0;

const char *pitchConditionName(int pitchType)
{
    switch (pitchType) {
    case 0: return "Frozen";
    case 1: return "Muddy";
    case 2: return "Wet";
    case 3: return "Soft";
    case 4: return "Normal";
    case 5: return "Dry";
    case 6: return "Hard";
    default: return "Normal";
    }
}

const char *gameModeName()
{
    if (HotseatCareerManager::get().isActive())
        return "Hotseat Career";
    switch (swos.g_gameType) {
    case kGameTypeCareer: return "Career";
    case kGameTypeSeason: return "Season";
    case kGameTypePresetCompetition: return "Cup / League";
    case kGameTypeDiyCompetition: return "Competition";
    case kGameTypeNoGame:
    default:
        return "Friendly";
    }
}

std::string sanitizeAsciiUpper(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    bool lastSpace = false;
    for (unsigned char ch : in) {
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            if (!lastSpace && !out.empty()) {
                out.push_back(' ');
                lastSpace = true;
            }
            continue;
        }
        if (ch >= 0x20 && ch <= 0x7E) {
            char c = static_cast<char>(std::toupper(ch));
            if (c == ' ') {
                if (!lastSpace && !out.empty()) {
                    out.push_back(' ');
                    lastSpace = true;
                }
            } else {
                out.push_back(c);
                lastSpace = false;
            }
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string pickCareerSquadVoice(std::string& outClubName, std::string& outPlayerName, std::string& outRole)
{
    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        outClubName = mgr.activePlayer().teamName;
    } else {
        const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
        outClubName = hdr->teamName;
    }
    if (outClubName.empty())
        outClubName = "THE CLUB";

    uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    if (numPlayers == 0 || numPlayers > kMaxCareerSquadPlayers)
        return {};

    std::vector<std::pair<std::string, std::string>> candidates;
    for (uint16_t i = 0; i < numPlayers; ++i) {
        size_t ofs = CareerOffsets::kCareerTeam +
                     HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(i) * HotseatInternal::kPlayerFileSize;
        const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
        if (pf->playerName[0] == '\0')
            continue;
        int pos = (pf->positionAndFace >> 5) & 0x07;
        const char *role = "MIDFIELDER";
        if (pos == 0) role = "GOALKEEPER";
        else if (pos == 1 || pos == 2) role = "DEFENDER";
        else if (pos == 6 || pos == 7) role = "STRIKER";
        else if (pos == 3 || pos == 4) role = "WINGER";
        candidates.emplace_back(pf->playerName, role);
    }
    if (candidates.empty())
        return {};

    size_t idx = static_cast<size_t>(s_weeklyTickCounter + numPlayers) % candidates.size();
    outPlayerName = candidates[idx].first;
    outRole = candidates[idx].second;
    return outPlayerName;
}

void showWrappedSwosPrompt(const char *header, const std::string& body)
{
    auto wrapped = wrapTextForSwosPrompt(body, 40, 6);
    if (wrapped.empty())
        return;

    std::vector<const char *> ptrs;
    ptrs.reserve(wrapped.size());
    for (const auto& line : wrapped)
        ptrs.push_back(line.c_str());

    showContinueAbortPrompt(header, "CONTINUE", nullptr, ptrs, false);
}

} // namespace

std::vector<std::string> wrapTextForSwosPrompt(const std::string& text, size_t maxLineLen, size_t maxLines)
{
    std::vector<std::string> lines;
    if (maxLines == 0 || maxLineLen == 0)
        return lines;

    // Split by explicit newlines first so structured sections stay neat
    std::istringstream stream(text);
    std::string rawLine;
    while (std::getline(stream, rawLine) && lines.size() < maxLines) {
        std::string clean = sanitizeAsciiUpper(rawLine);
        if (clean.empty())
            continue;

        size_t pos = 0;
        while (pos < clean.size() && lines.size() < maxLines) {
            if (clean.size() - pos <= maxLineLen) {
                lines.push_back(clean.substr(pos));
                break;
            }
            size_t split = clean.rfind(' ', pos + maxLineLen);
            if (split == std::string::npos || split <= pos)
                split = pos + maxLineLen;
            lines.push_back(clean.substr(pos, split - pos));
            pos = split;
            while (pos < clean.size() && clean[pos] == ' ')
                ++pos;
        }
    }
    return lines;
}

void resetPostMatchNewspaper()
{
    s_postMatchRequestId = 0;
    s_postMatchTriggered = false;
}

void beginPostMatchNewspaperRequest()
{
    if (s_postMatchTriggered || swos.g_trainingGame || swos.gameCanceled)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.postMatchNewspaper)
        return;

    s_postMatchTriggered = true;

    const char *t1 = (swos.topTeamData.inGameTeamPtr && swos.topTeamData.inGameTeamPtr->teamName[0])
        ? swos.topTeamData.inGameTeamPtr->teamName : "HOME";
    const char *t2 = (swos.bottomTeamData.inGameTeamPtr && swos.bottomTeamData.inGameTeamPtr->teamName[0])
        ? swos.bottomTeamData.inGameTeamPtr->teamName : "AWAY";
    int g1 = swos.statsTeam1Goals;
    int g2 = swos.statsTeam2Goals;
    const auto stats = getStats();
    std::string s1 = getMatchScorersSummary(1);
    std::string s2 = getMatchScorersSummary(2);

    std::string prompt = "Match: " + std::string(t1) + " " + std::to_string(g1) + "-" + std::to_string(g2) + " " + t2;
    if (swos.playingPenalties) {
        prompt += " (PENS " + std::to_string(swos.team1PenaltyGoals) + "-" + std::to_string(swos.team2PenaltyGoals) + ")";
    }
    prompt += "\nCompetition: " + std::string(gameModeName());
    prompt += ", Pitch: " + std::string(pitchConditionName(getPitchType()));
    if (!s1.empty()) prompt += "\n" + std::string(t1) + " scorers: " + s1;
    if (!s2.empty()) prompt += "\n" + std::string(t2) + " scorers: " + s2;
    prompt += "\nStats: Shots " + std::to_string(stats.team1.goalAttempts) + "-" + std::to_string(stats.team2.goalAttempts);
    prompt += ", Possession " + std::to_string(stats.team1.ballPossession) + "%-" + std::to_string(stats.team2.ballPossession) + "%.";
    prompt += "\nWrite a punchy 1990s British sports newspaper blurb:\n"
              "Line 1: Tabloid headline (max 38 chars)\n"
              "Line 2-3: 1-2 short match sentences (max 75 chars total)\n"
              "Line 4: CHAIRMAN: short board reaction (max 70 chars). No markdown.";

    const std::string sys = "You are a 1995 British football tabloid editor and club chairman. "
                            "Be concise, punchy, plain ASCII text, no markdown, no bullets.";
    s_postMatchRequestId = enqueueLlmRequest(sys, prompt);
}

void showPostMatchNewspaperIfReady()
{
    if (s_postMatchRequestId == 0 || swos.gameCanceled || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.postMatchNewspaper)
        return;

    std::string reply;
    int waitBudget = std::min(cfg.timeoutMs, 1500);
    if (waitForLlmResponse(s_postMatchRequestId, waitBudget, reply)) {
        showWrappedSwosPrompt("DAILY SOCCER ECHO", reply);
    }
    s_postMatchRequestId = 0;
}

std::string fetchTransferNegotiationVoice(const char *buyerClub, const char *sellerClub,
    const char *playerName, const char *offerTerms, bool isCounterOffer)
{
    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.transferNegotiations)
        return {};

    std::string userPrompt = isCounterOffer
        ? (std::string(sellerClub) + " manager responds to " + buyerClub + " over " + playerName + " (" + offerTerms + ").")
        : (std::string(buyerClub) + " manager pitches a bid to " + sellerClub + " for " + playerName + " (" + offerTerms + ").");
    userPrompt += " Reply with ONE short in-character 1990s football manager quote (max 72 chars), plain ASCII, no quotes.";

    uint64_t reqId = enqueueLlmRequest(
        "You are a 1995 football manager negotiating a transfer. One punchy sentence, max 72 chars, plain ASCII.",
        userPrompt);
    if (reqId == 0)
        return {};

    std::string reply;
    if (!waitForLlmResponse(reqId, std::min(cfg.timeoutMs, 1200), reply))
        return {};

    std::string clean = sanitizeAsciiUpper(reply);
    if (clean.size() > 72)
        clean.resize(72);
    return clean;
}

void onCareerMatchCompletedLlm(int topGoals, int botGoals)
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame || swos.gameCanceled)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.dressingRoomVoices)
        return;

    std::string club, player, role;
    if (pickCareerSquadVoice(club, player, role).empty())
        return;

    std::string prompt = "Club: " + club + ". Score: " + std::to_string(topGoals) + "-" + std::to_string(botGoals) +
        ". Player speaking: " + player + " (" + role + "). "
        "Give a 1-2 sentence 1990s dressing room reaction (max 110 chars). Prefix with player name.";

    uint64_t reqId = enqueueLlmRequest(
        "You are a 1995 football player in the dressing room after a match. Plain ASCII, max 110 chars, no markdown.",
        prompt);
    if (reqId == 0)
        return;

    std::string reply;
    if (waitForLlmResponse(reqId, std::min(cfg.timeoutMs, 1200), reply)) {
        showWrappedSwosPrompt("DRESSING ROOM", reply);
    }
}

void onCareerWeeklyTickLlm()
{
    if (swos.g_gameType != kGameTypeCareer || HotseatCareerManager::get().isInitializingSeason())
        return;

    ++s_weeklyTickCounter;
    if ((s_weeklyTickCounter % 3) != 0)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || (!cfg.features.dressingRoomVoices && !cfg.features.transferNegotiations))
        return;

    std::string club, player, role;
    if (pickCareerSquadVoice(club, player, role).empty())
        return;

    bool isMarket = cfg.features.transferNegotiations && ((s_weeklyTickCounter % 6) == 0);
    std::string sys = isMarket
        ? "You are a 1995 football transfer agent whispering a short rumour. Plain ASCII, max 110 chars."
        : "You are a 1995 squad player speaking at training. Plain ASCII, max 110 chars.";
    std::string prompt = isMarket
        ? ("Club: " + club + ". Mention " + player + " (" + role + ") in a short transfer market whisper (max 110 chars).")
        : ("Club: " + club + ". " + player + " (" + role + ") gives a short training-ground quote about form or selection (max 110 chars).");

    uint64_t reqId = enqueueLlmRequest(sys, prompt);
    if (reqId == 0)
        return;

    std::string reply;
    if (waitForLlmResponse(reqId, std::min(cfg.timeoutMs, 1000), reply)) {
        showWrappedSwosPrompt(isMarket ? "TRANSFER WHISPER" : "DRESSING ROOM", reply);
    }
}
