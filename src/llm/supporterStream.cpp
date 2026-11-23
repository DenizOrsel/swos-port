#include "supporterStream.h"
#include "compactFont.h"
#include "gameTime.h"
#include "llmClient.h"
#include "matchTelemetry.h"
#include "render.h"
#include "sponsorChallenge.h"
#include "stats.h"
#include "swos.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <sstream>

namespace {

uint64_t s_streamRequestId = 0;
int s_cooldownFrames = 0;
int s_ambientTimerFrames = 0;
int s_staggerTimerFrames = 0;
bool s_kickoffTriggered = false;
bool s_userHidden = false;
int s_avatarSeq = 0;
size_t s_personalityCursor = 0;
size_t s_banterTopicCursor = 0;

std::deque<SupporterTweet> s_pendingBatch;
std::deque<SupporterTweet> s_visibleTweets;

SDL_Texture *getMiniFontAtlas(SDL_Renderer *r) { return getCompactFontAtlas(r); }
int measureMiniTextUnits(const std::string& text) { return measureCompactTextUnits(text); }
float drawMiniText(SDL_Renderer *r, SDL_Texture *atlas, float x, float y, float px,
    const std::string& text, SDL_Color color, Uint8 alpha, bool bold = false)
{
    return drawCompactText(r, atlas, x, y, px, text, color, alpha, bold);
}
void drawMiniTextWithMentions(SDL_Renderer *r, SDL_Texture *atlas, float x, float y, float px,
    const std::string& text, SDL_Color baseColor, SDL_Color mentionColor, Uint8 alpha)
{
    drawCompactTextWithMentions(r, atlas, x, y, px, text, baseColor, mentionColor, alpha, false);
}

std::string trimStream(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string sanitizeStreamText(const std::string& in)
{
    return sanitizeCompactMixedCase(in);
}

std::string toUpperAscii(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Wrap message text inline right after "Handle: " on line 1, and full width on lines 2..maxLines
std::vector<std::string> wrapInlineMessage(const std::string& msg, int firstLineMaxUnits, int nextLineMaxUnits, int maxLines)
{
    std::vector<std::string> words;
    std::istringstream iss(msg);
    std::string w;
    while (iss >> w)
        words.push_back(std::move(w));

    std::vector<std::string> lines;
    std::string cur;
    int curLimit = firstLineMaxUnits;

    for (const auto& word : words) {
        if (cur.empty()) {
            int wUnits = measureMiniTextUnits(word);
            if (wUnits <= curLimit || !lines.empty()) {
                cur = word;
            } else {
                // First word doesn't fit after handle; push empty line 1 so message starts cleanly on line 2
                lines.push_back("");
                if (static_cast<int>(lines.size()) >= maxLines)
                    return lines;
                curLimit = nextLineMaxUnits;
                cur = word;
            }
        } else {
            std::string cand = cur + " " + word;
            if (measureMiniTextUnits(cand) <= curLimit) {
                cur = std::move(cand);
            } else {
                lines.push_back(std::move(cur));
                if (static_cast<int>(lines.size()) >= maxLines)
                    return lines;
                curLimit = nextLineMaxUnits;
                cur = word;
            }
        }
    }

    if (!cur.empty() && static_cast<int>(lines.size()) < maxLines)
        lines.push_back(std::move(cur));
    if (lines.empty())
        lines.push_back("");
    return lines;
}

SDL_Color swosKitColorToRgb(word colorIdx)
{
    switch (colorIdx & 0x0F) {
    case 0: return { 170, 175, 185, 255 }; // gray
    case 1: return { 238, 240, 245, 255 }; // white
    case 2: return { 42, 46, 54, 255 };    // black
    case 3: return { 235, 140, 42, 255 };  // orange
    case 4: return { 215, 48, 52, 255 };   // red
    case 5: return { 44, 98, 210, 255 };   // blue
    case 6: return { 135, 86, 52, 255 };   // brown
    case 7: return { 105, 195, 245, 255 }; // light blue
    case 8: return { 54, 178, 76, 255 };   // green
    case 9: return { 242, 206, 54, 255 };  // yellow
    default: return { 110, 180, 235, 255 };
    }
}

void drawCompactAvatar(SDL_Renderer *r, float ax, float ay, float px, SupporterSide side, int variant, Uint8 alpha)
{
    auto fillUnit = [&](float ux, float uy, float uw, float uh, Uint8 red, Uint8 g, Uint8 b, Uint8 a) {
        SDL_SetRenderDrawColor(r, red, g, b, static_cast<Uint8>((static_cast<int>(a) * alpha) / 255));
        SDL_FRect fr{ ax + ux * px, ay + uy * px, uw * px, uh * px };
        SDL_RenderFillRectF(r, &fr);
    };

    if (side == SupporterSide::kPundit) {
        // Mini 8x8 broadcast microphone icon (gold/yellow accent)
        fillUnit(2, 0, 4, 4, 255, 224, 72, 250);      // mic capsule
        fillUnit(3, 1, 2, 2, 180, 145, 36, 250);      // mic grille
        fillUnit(1, 3, 1, 2, 220, 224, 232, 240);     // left cradle
        fillUnit(6, 3, 1, 2, 220, 224, 232, 240);     // right cradle
        fillUnit(2, 5, 4, 1, 220, 224, 232, 240);     // cradle bottom
        fillUnit(3, 6, 2, 1, 220, 224, 232, 240);     // stand stem
        fillUnit(2, 7, 4, 1, 255, 224, 72, 240);      // stand base
        return;
    }

    if (side == SupporterSide::kNeutral && (variant % 2 == 0)) {
        // Mini 8x8 pixel football
        fillUnit(1, 0, 6, 8, 235, 238, 242, 245);
        fillUnit(0, 1, 8, 6, 235, 238, 242, 245);
        fillUnit(3, 3, 2, 2, 30, 34, 42, 245);
        fillUnit(1, 1, 1, 1, 30, 34, 42, 245);
        fillUnit(6, 1, 1, 1, 30, 34, 42, 245);
        fillUnit(1, 6, 1, 1, 30, 34, 42, 245);
        fillUnit(6, 6, 1, 1, 30, 34, 42, 245);
        return;
    }

    if (side == SupporterSide::kHome || side == SupporterSide::kAway) {
        const TeamGame& team = (side == SupporterSide::kHome) ? swos.topTeamInGame : swos.bottomTeamInGame;
        SDL_Color primary = swosKitColorToRgb(team.prShirtCol);
        SDL_Color accent = swosKitColorToRgb(team.prStripesCol);
        // Mini 8x8 club crest / shield
        fillUnit(0, 0, 8, 6, 224, 228, 236, 235);
        fillUnit(1, 6, 6, 1, 224, 228, 236, 235);
        fillUnit(2, 7, 4, 1, 224, 228, 236, 235);
        fillUnit(1, 1, 6, 5, primary.r, primary.g, primary.b, 245);
        fillUnit(2, 6, 4, 1, primary.r, primary.g, primary.b, 245);
        fillUnit(3, 1, 2, 5, accent.r, accent.g, accent.b, 245);
        return;
    }

    // Mini 8x8 retro pixel supporter face
    fillUnit(1, 0, 6, 2, 98, 64, 38, 245);         // hair
    fillUnit(1, 2, 6, 4, 238, 188, 150, 245);      // face
    fillUnit(2, 3, 1, 1, 24, 26, 32, 245);         // left eye
    fillUnit(5, 3, 1, 1, 24, 26, 32, 245);         // right eye
    fillUnit(1, 6, 6, 2, 70, 132, 214, 245);       // collar
}

void enqueueSupporterBatchRequest(const char *triggerContext)
{
    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;

    MatchTelemetrySnapshot snap = captureMatchTelemetrySnapshot();
    std::string prompt = formatTelemetryPromptBlock(snap);
    prompt += "\nTrigger: " + std::string(triggerContext ? triggerContext : "LIVE FLOW");
    if (!snap.recentEvents.empty() && triggerContext &&
        std::strcmp(triggerContext, "KICKOFF") != 0 &&
        std::strcmp(triggerContext, "LIVE MATCH FLOW") != 0) {
        const auto& latest = snap.recentEvents.back();
        if (!latest.detail.empty())
            prompt += " (" + latest.detail + ")";
    }

    bool hasPriorFanChat = false;
    if (!s_visibleTweets.empty()) {
        prompt += "\nRecent chat stream (oldest to newest - output ONLY new messages):";
        size_t histCount = std::min<size_t>(5, s_visibleTweets.size());
        for (size_t i = histCount; i > 0; --i) {
            const auto& prev = s_visibleTweets[i - 1];
            if (prev.side == SupporterSide::kPundit) {
                prompt += "\n- [TV Commentator]: " + prev.message;
            } else {
                hasPriorFanChat = true;
                const char *sideLabel = (prev.side == SupporterSide::kHome) ? "HOME" :
                                        (prev.side == SupporterSide::kAway) ? "AWAY" : "NEUTRAL";
                prompt += "\n- @" + prev.handle + " (" + sideLabel + "): " + prev.message;
            }
        }
    }

    const auto& personas = cfg.personalities.empty() ? defaultFanPersonalities() : cfg.personalities;
    size_t count = std::min<size_t>(3, personas.size());
    prompt += "\nGenerate " + std::to_string(count) + " NEW short live supporter chat messages.";
    if (hasPriorFanChat) {
        prompt += "\n- INTERACTION RULE: At least ONE message MUST directly reply to or tease an earlier fan from the Recent chat stream by placing @TheirExactHandle in front of their nickname (e.g. @archy_brown ...).";
    }

    bool isAmbientFlow = (!triggerContext ||
                          std::strcmp(triggerContext, "LIVE MATCH FLOW") == 0 ||
                          std::strcmp(triggerContext, "KICKOFF") == 0);
    if (isAmbientFlow) {
        size_t topic = (s_banterTopicCursor++) % 4;
        if (topic == 0) {
            prompt += "\n- BANTER TOPIC: Include at least 1 message discussing squad/bench subs (calling for a real available bench player listed above to come on), the manager's record/trophies (showing love for a winning boss or impatience if struggling/new), or the team's season top scorers/assists.";
        } else if (topic == 1) {
            prompt += "\n- BANTER TOPIC: Include at least 1 message dropping casual 1996/97 global football season banter (talking about the best players or top clubs of 96/97 like Ronaldo at Barcelona, Shearer at Newcastle, Cantona at Man Utd, Juventus/Del Piero/Zidane, Dortmund in Europe, Bergkamp, Weah, or Roberto Carlos) tied naturally into watching this match.";
        } else if (topic == 2) {
            prompt += "\n- BANTER TOPIC: Include at least 1 message chatting about upcoming fixtures (e.g. an upcoming cup tie, European night, or big league fixture from the schedule above, or worrying about resting players for the next big cup game).";
        } else {
            prompt += "\n- BANTER TOPIC: Mix live reactions to possession, shots, and territory with lively terrace banter and rivalry between fans.";
        }
    } else {
        prompt += "\n- React directly to the live Trigger event above (exact goalkeeper, scorer, finish type, minute), and fans may also debate subs, the manager, or banter with earlier chatters.";
    }

    if (!personas.empty()) {
        prompt += "\nGive each message its own distinct fan personality from this list:";
        for (size_t i = 0; i < count; ++i) {
            const auto& p = personas[(s_personalityCursor + i) % personas.size()];
            prompt += "\n- Message " + std::to_string(i + 1) + " [" + p.name + "]: " + p.instruction;
        }
        s_personalityCursor = (s_personalityCursor + count) % personas.size();
    }

    prompt += "\nFormat STRICTLY one message per line as: HANDLE|SIDE|MESSAGE";
    prompt += "\n- HANDLE: realistic 1997 fan nickname (no spaces, max 13 chars, mixed case e.g. archy_brown, Fan_atic, Terrace97)";
    prompt += "\n- SIDE: HOME (supports " + snap.team1 + "), AWAY (supports " + snap.team2 + "), or NEUTRAL";
    prompt += "\n- MESSAGE: short punchy fan reaction in normal sentence case (4 to 13 words). Always write percentages as whole integers (e.g. 57% or %57, never decimals). Use classic ASCII-only text emoticons naturally where fitting (like :), :(, :D, ;), :P, >:), O_O, T_T, <3, ^_^, xD, uWu) - NEVER use Unicode emojis. If minute >= 85 and margin <= 1, leading fans beg the ref to blow the whistle!";
    prompt += "\nOutput ONLY the HANDLE|SIDE|MESSAGE lines. No intro, no numbering, no markdown.";

    const std::string sys = "You simulate a lively 1996/97 football fan chat stream watching the match live. "
                            "Be passionate, varied in personality (calm, fanatic, tactical, pessimist, joker, troll), "
                            "banter with each other using @Handle mentions, discuss real bench subs, managers/trophies, season scorers/assists, upcoming fixtures, and occasional 1996/97 world football talk. "
                            "Use classic ASCII text emoticons only (like :), :D, >:), O_O, uWu - strictly NO Unicode emojis). "
                            "For anything happening in THIS match, stay 100% faithful to the telemetry facts (never invent fake goals, headers, cards, or weather). "
                            "Use natural sentence case. Output strictly HANDLE|SIDE|MESSAGE lines only.";

    s_streamRequestId = enqueueLlmRequest(sys, prompt);
    s_cooldownFrames = 240; // ~4s minimum spacing between requests
    s_ambientTimerFrames = std::max(5, cfg.telemetry.streamUpdateSeconds) * 60;
}

} // namespace

std::vector<SupporterTweet> parseSupporterTweetBatch(const std::string& rawResponse)
{
    std::vector<SupporterTweet> parsed;
    std::istringstream stream(rawResponse);
    std::string line;
    while (std::getline(stream, line)) {
        line = trimStream(line);
        if (line.empty())
            continue;
        size_t p1 = line.find('|');
        if (p1 == std::string::npos)
            continue;
        size_t p2 = line.find('|', p1 + 1);
        if (p2 == std::string::npos)
            continue;

        std::string handle = sanitizeStreamText(trimStream(line.substr(0, p1)));
        std::string sideStr = toUpperAscii(sanitizeStreamText(trimStream(line.substr(p1 + 1, p2 - p1 - 1))));
        std::string msg = sanitizeStreamText(trimStream(line.substr(p2 + 1)));

        if (!handle.empty() && handle.front() == '@')
            handle.erase(handle.begin());
        if (handle.size() > 14)
            handle.resize(14);
        if (handle.empty() || msg.empty())
            continue;
        if (msg.size() > 220)
            msg.resize(220);

        SupporterTweet tw;
        tw.handle = std::move(handle);
        if (sideStr.find("HOME") != std::string::npos)
            tw.side = SupporterSide::kHome;
        else if (sideStr.find("AWAY") != std::string::npos)
            tw.side = SupporterSide::kAway;
        else
            tw.side = SupporterSide::kNeutral;
        tw.message = std::move(msg);
        tw.avatarVariant = s_avatarSeq++;
        parsed.push_back(std::move(tw));
        if (parsed.size() >= 3)
            break;
    }
    return parsed;
}

void resetSupporterStream()
{
    s_streamRequestId = 0;
    s_cooldownFrames = 0;
    s_ambientTimerFrames = 90; // ~1.5s after kickoff
    s_staggerTimerFrames = 0;
    s_kickoffTriggered = false;
    s_banterTopicCursor = 0;
    s_pendingBatch.clear();
    s_visibleTweets.clear();
}

void toggleSupporterStreamVisible()
{
    s_userHidden = !s_userHidden;
}

void pushPunditMessageToStream(const std::string& rawMessage)
{
    std::string msg = sanitizeStreamText(trimStream(rawMessage));
    if (msg.empty())
        return;

    // Strip any accidental "Pundit:" or "Commentator:" prefix if the model emitted one
    std::string upper = toUpperAscii(msg);
    if (upper.rfind("PUNDIT:", 0) == 0)
        msg = trimStream(msg.substr(7));
    else if (upper.rfind("COMMENTATOR:", 0) == 0)
        msg = trimStream(msg.substr(12));

    if (msg.empty())
        return;
    if (msg.size() > 260)
        msg.resize(260);

    SupporterTweet tw;
    tw.side = SupporterSide::kPundit;
    tw.message = std::move(msg);
    tw.avatarVariant = s_avatarSeq++;
    s_visibleTweets.push_front(std::move(tw));

    const auto& cfg = getLlmConfig();
    int maxTweets = std::clamp(cfg.telemetry.maxVisibleTweets, 3, 6);
    while (static_cast<int>(s_visibleTweets.size()) > maxTweets)
        s_visibleTweets.pop_back();
}

bool isSupporterStreamVisible()
{
    const auto& cfg = getLlmConfig();
    bool anyStreamFeature = (cfg.features.supporterStream && cfg.telemetry.enabled) || cfg.features.livePunditTicker;
    return cfg.enabled && anyStreamFeature && !s_userHidden;
}

void triggerSupporterStreamEvent(const char *eventLabel, bool highPriority /* = false */)
{
    if (!eventLabel || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;

    if (!highPriority && (s_cooldownFrames > 0 || s_streamRequestId != 0))
        return;

    enqueueSupporterBatchRequest(eventLabel);
}

void updateSupporterStream()
{
    if (swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled)
        return;

    for (auto& tw : s_visibleTweets)
        ++tw.ageFrames;

    if (!cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;

    if (s_cooldownFrames > 0)
        --s_cooldownFrames;
    if (s_staggerTimerFrames > 0)
        --s_staggerTimerFrames;

    // Poll async LLM response without blocking
    if (s_streamRequestId != 0) {
        std::string reply;
        if (tryTakeLlmResponse(s_streamRequestId, reply)) {
            s_streamRequestId = 0;
            auto batch = parseSupporterTweetBatch(reply);
            for (auto& tw : batch)
                s_pendingBatch.push_back(std::move(tw));
            s_staggerTimerFrames = 0;
        }
    }

    // Release one tweet at a time with a natural ~2.2s stagger
    if (!s_pendingBatch.empty() && s_staggerTimerFrames <= 0) {
        s_visibleTweets.push_front(std::move(s_pendingBatch.front()));
        s_pendingBatch.pop_front();
        int maxTweets = std::clamp(cfg.telemetry.maxVisibleTweets, 3, 6);
        while (static_cast<int>(s_visibleTweets.size()) > maxTweets)
            s_visibleTweets.pop_back();
        s_staggerTimerFrames = 130; // ~2.2 seconds between staggered fan messages
    }

    // Trigger kickoff and periodic ambient match-flow reactions while match is active
    if (swos.gameStatePl == GameState::kInProgress && s_streamRequestId == 0 && s_pendingBatch.empty()) {
        if (s_ambientTimerFrames > 0) {
            --s_ambientTimerFrames;
        } else {
            if (!s_kickoffTriggered) {
                s_kickoffTriggered = true;
                enqueueSupporterBatchRequest("KICKOFF");
            } else {
                enqueueSupporterBatchRequest("LIVE MATCH FLOW");
            }
        }
    }
}

void drawSupporterStreamOverlay()
{
    if (swos.g_trainingGame || s_userHidden)
        return;
    if (showingPostGameStats() || showingUserRequestedStats())
        return;

    const auto& cfg = getLlmConfig();
    bool anyStreamFeature = (cfg.features.supporterStream && cfg.telemetry.enabled) || cfg.features.livePunditTicker;
    bool showSponsorHud = hasActiveSponsorChallenge();
    if (!showSponsorHud && (!cfg.enabled || !anyStreamFeature || s_visibleTweets.empty()))
        return;

    SDL_Renderer *renderer = getRenderer();
    if (!renderer)
        return;

    SDL_Texture *atlas = getMiniFontAtlas(renderer);
    if (!atlas)
        return;

    SDL_Rect vp = getViewport();
    if (vp.w <= 0 || vp.h <= 0)
        return;

    // Compact native-pixel scale: ~14px tall crisp text at 1080p, ~10.5px at 720p, ~21px at 1440p
    float px = 1.0f;
    if (vp.h >= 900)
        px = std::clamp(std::floor(static_cast<float>(vp.h) / 520.0f + 0.5f), 2.0f, 4.0f);
    else if (vp.h >= 600)
        px = 1.5f;

    // Panel geometry in logical compact font units (~156 units wide -> ~312px on 1080p = ~16% of width)
    constexpr int kPanelUnitsW = 156;
    constexpr int kHeaderUnitsH = 15;
    constexpr int kPadX = 5;
    constexpr int kAvatarSize = 8;
    constexpr int kAvatarGap = 4;
    constexpr int kTextStartUnits = kPadX + kAvatarSize + kAvatarGap;
    constexpr int kTextMaxUnits = kPanelUnitsW - kTextStartUnits - kPadX;
    constexpr int kLineStepUnits = 10;
    constexpr int kTweetGapUnits = 4;

    float panelW = kPanelUnitsW * px;
    float margin = 6.0f * px;
    float panelX = static_cast<float>(vp.w) - panelW - margin;
    float panelY = margin;

    float origScaleX = 1.0f, origScaleY = 1.0f;
    SDL_RenderGetScale(renderer, &origScaleX, &origScaleY);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Optional compact active Sponsor Challenge status card tucked cleanly at top-right (supports 1-3 challenges + all-in jackpot line)
    if (showSponsorHud) {
        auto hudLines = formatActiveSponsorChallengeHudLines();
        if (!hudLines.empty()) {
            float lineH = 10.0f * px;
            float pillH = (4.0f * px) + static_cast<float>(hudLines.size()) * lineH;
            SDL_SetRenderDrawColor(renderer, 14, 18, 16, 168);
            SDL_FRect pillRect{ panelX, panelY, panelW, pillH };
            SDL_RenderFillRectF(renderer, &pillRect);

            int overallMode = 0;
            bool allAchieved = true;
            for (const auto& hl : hudLines) {
                if (hl.second == 2) overallMode = 2;
                if (hl.second != 1) allAchieved = false;
            }
            if (overallMode != 2 && allAchieved)
                overallMode = 1;

            SDL_Color borderCol = (overallMode == 1) ? SDL_Color{ 92, 232, 128, 255 } :
                                  (overallMode == 2) ? SDL_Color{ 240, 92, 92, 255 } :
                                                       SDL_Color{ 255, 214, 74, 255 };
            SDL_SetRenderDrawColor(renderer, borderCol.r, borderCol.g, borderCol.b, 165);
            SDL_RenderDrawRectF(renderer, &pillRect);

            float ly = panelY + 2.5f * px;
            for (const auto& hl : hudLines) {
                SDL_Color col = (hl.second == 1) ? SDL_Color{ 92, 232, 128, 255 } :
                                (hl.second == 2) ? SDL_Color{ 240, 92, 92, 255 } :
                                                   SDL_Color{ 255, 214, 74, 255 };
                drawMiniText(renderer, atlas, panelX + 4.0f * px, ly, px, hl.first, col, 255, true);
                ly += lineH;
            }

            panelY += pillH + 3.0f * px;
        }
    }

    if (!cfg.enabled || !anyStreamFeature || s_visibleTweets.empty()) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderSetScale(renderer, origScaleX, origScaleY);
        return;
    }

    float heightFrac = static_cast<float>(std::clamp(cfg.telemetry.streamBoxHeightPct, 30, 85)) / 100.0f;
    int maxPanelUnitsH = static_cast<int>((static_cast<float>(vp.h) * heightFrac) / px);
    int maxBodyBottom = maxPanelUnitsH - 4;

    struct LaidOutTweet {
        int yUnits;
        int alpha;
        const SupporterTweet *tw;
        std::string handleLabel;
        int handleUnits;
        std::vector<std::string> lines;
    };

    std::vector<LaidOutTweet> layout;
    int curY = kHeaderUnitsH + 4;

    for (size_t i = 0; i < s_visibleTweets.size(); ++i) {
        const auto& tw = s_visibleTweets[i];
        bool isPundit = (tw.side == SupporterSide::kPundit);
        std::string handleLabel = isPundit ? std::string() : (tw.handle + ": ");
        int handleW = isPundit ? 0 : (measureMiniTextUnits(handleLabel) + 2);
        int firstLineAvail = std::max(0, kTextMaxUnits - handleW);
        auto lines = wrapInlineMessage(tw.message, firstLineAvail, kTextMaxUnits, 7);
        int blockH = std::max(kAvatarSize + 1, static_cast<int>(lines.size()) * kLineStepUnits) + kTweetGapUnits;

        if (curY + blockH > maxBodyBottom)
            break;

        int fadeIn = std::min(255, 65 + tw.ageFrames * 15);
        int posFade = (i >= 4) ? 150 : (i == 3 ? 200 : 255);
        int alpha = std::min(fadeIn, posFade);

        layout.push_back({ curY, alpha, &tw, std::move(handleLabel), handleW, std::move(lines) });
        curY += blockH;
    }

    if (layout.empty()) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderSetScale(renderer, origScaleX, origScaleY);
        return;
    }

    int panelUnitsH = curY + 2;
    float panelH = panelUnitsH * px;

    // 1. Sleek semi-transparent dark glass card (leaves pitch clearly visible behind/around it)
    SDL_SetRenderDrawColor(renderer, 14, 18, 16, 152);
    SDL_FRect panelRect{ panelX, panelY, panelW, panelH };
    SDL_RenderFillRectF(renderer, &panelRect);

    // Subtle darker header strip
    SDL_SetRenderDrawColor(renderer, 10, 14, 12, 182);
    SDL_FRect hdrBand{ panelX + px * 0.5f, panelY + px * 0.5f, panelW - px, kHeaderUnitsH * px };
    SDL_RenderFillRectF(renderer, &hdrBand);

    // Crisp hairline border + header separator
    SDL_SetRenderDrawColor(renderer, 160, 174, 162, 115);
    SDL_RenderDrawRectF(renderer, &panelRect);
    SDL_SetRenderDrawColor(renderer, 150, 164, 152, 85);
    SDL_RenderDrawLineF(renderer, panelX + 2.0f * px, panelY + kHeaderUnitsH * px,
        panelX + panelW - 2.0f * px, panelY + kHeaderUnitsH * px);

    // Compact red "• LIVE" badge pill
    SDL_SetRenderDrawColor(renderer, 220, 44, 44, 232);
    SDL_FRect livePill{ panelX + 5.0f * px, panelY + 3.0f * px, 29.0f * px, 9.0f * px };
    SDL_RenderFillRectF(renderer, &livePill);

    // Pulsing white live dot
    bool dotBright = ((SDL_GetTicks() / 450) & 1) == 0;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, dotBright ? 255 : 125);
    SDL_FRect liveDot{ panelX + 7.5f * px, panelY + 6.5f * px, 2.0f * px, 2.0f * px };
    SDL_RenderFillRectF(renderer, &liveDot);

    // Header text
    drawMiniText(renderer, atlas, panelX + 12.0f * px, panelY + 4.0f * px, px,
        "LIVE", { 255, 255, 255, 255 }, 255, true);
    drawMiniText(renderer, atlas, panelX + 38.0f * px, panelY + 4.0f * px, px,
        "CHAT STREAM", { 238, 244, 240, 255 }, 245, true);

    // 2. Render each compact supporter message (or yellow live commentator message)
    for (const auto& item : layout) {
        float rowY = panelY + item.yUnits * px;
        drawCompactAvatar(renderer, panelX + kPadX * px, rowY, px,
            item.tw->side, item.tw->avatarVariant, static_cast<Uint8>(item.alpha));

        bool isPundit = (item.tw->side == SupporterSide::kPundit);
        float textX = panelX + kTextStartUnits * px;
        float firstLineX = textX;

        if (!isPundit && !item.handleLabel.empty()) {
            SDL_Color handleColor = (item.tw->side == SupporterSide::kHome) ? SDL_Color{ 122, 198, 255, 255 } :
                                    (item.tw->side == SupporterSide::kAway) ? SDL_Color{ 255, 214, 96, 255 } :
                                                                              SDL_Color{ 176, 232, 196, 255 };
            firstLineX = drawMiniText(renderer, atlas, textX, rowY, px,
                item.handleLabel, handleColor, static_cast<Uint8>(item.alpha), true);
        }

        SDL_Color bodyColor = isPundit ? SDL_Color{ 255, 226, 78, 255 } : SDL_Color{ 244, 246, 248, 255 };
        SDL_Color mentionColor = SDL_Color{ 118, 244, 232, 255 };
        for (size_t li = 0; li < item.lines.size(); ++li) {
            if (item.lines[li].empty())
                continue;
            float lineX = (li == 0) ? firstLineX : textX;
            float lineY = rowY + static_cast<float>(li * kLineStepUnits) * px;
            drawMiniTextWithMentions(renderer, atlas, lineX, lineY, px,
                item.lines[li], bodyColor, mentionColor, static_cast<Uint8>(item.alpha));
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_RenderSetScale(renderer, origScaleX, origScaleY);
}
