#include "supporterStream.h"
#include "gameFieldMapping.h"
#include "gameTime.h"
#include "llmClient.h"
#include "llmReportMenu.h"
#include "matchTelemetry.h"
#include "render.h"
#include "stats.h"
#include "swos.h"
#include "text.h"

#include <algorithm>
#include <cctype>
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

std::deque<SupporterTweet> s_pendingBatch;
std::deque<SupporterTweet> s_visibleTweets;

std::string trimStream(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string sanitizeStreamUpper(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    bool lastSpace = false;
    auto pushChar = [&](char c) {
        if (c == ' ') {
            if (!lastSpace && !out.empty()) {
                out.push_back(' ');
                lastSpace = true;
            }
        } else {
            out.push_back(c);
            lastSpace = false;
        }
    };

    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(in[i]);
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            pushChar(' ');
            continue;
        }
        if (ch == '*' || ch == '#' || ch == '`' || ch == '"')
            continue;
        if (ch >= 0x20 && ch <= 0x7E) {
            pushChar(static_cast<char>(std::toupper(ch)));
            continue;
        }
        if ((ch & 0xE0) == 0xC0 && i + 1 < in.size()) {
            unsigned char b2 = static_cast<unsigned char>(in[++i]);
            unsigned int cp = ((ch & 0x1Fu) << 6) | (b2 & 0x3Fu);
            switch (cp) {
            case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
            case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5: pushChar('A'); break;
            case 0x00C7: case 0x00E7: case 0x0106: case 0x0107: case 0x010C: case 0x010D: pushChar('C'); break;
            case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
            case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: pushChar('E'); break;
            case 0x011E: case 0x011F: pushChar('G'); break;
            case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
            case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
            case 0x0130: case 0x0131: pushChar('I'); break;
            case 0x00D1: case 0x00F1: pushChar('N'); break;
            case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
            case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8: pushChar('O'); break;
            case 0x015E: case 0x015F: case 0x0160: case 0x0161: pushChar('S'); break;
            case 0x00DF: pushChar('S'); pushChar('S'); break;
            case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
            case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: pushChar('U'); break;
            case 0x00DD: case 0x00FD: case 0x00FF: pushChar('Y'); break;
            case 0x017D: case 0x017E: pushChar('Z'); break;
            case 0x0110: case 0x0111: pushChar('D'); break;
            default: break;
            }
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
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

void drawPixelAvatar(SDL_Renderer *r, float ax, float ay, SupporterSide side, int variant, Uint8 alpha)
{
    auto fillRect = [&](float x, float y, float w, float h, Uint8 red, Uint8 g, Uint8 b, Uint8 a) {
        SDL_SetRenderDrawColor(r, red, g, b, static_cast<Uint8>((static_cast<int>(a) * alpha) / 255));
        SDL_FRect fr{ x, y, w, h };
        SDL_RenderFillRectF(r, &fr);
    };

    if (side == SupporterSide::kNeutral && (variant % 2 == 0)) {
        // Mini pixel football
        fillRect(ax + 1, ay, 6, 8, 235, 235, 240, 245);
        fillRect(ax, ay + 1, 8, 6, 235, 235, 240, 245);
        fillRect(ax + 3, ay + 3, 2, 2, 30, 34, 42, 245);
        fillRect(ax + 1, ay + 1, 1, 1, 30, 34, 42, 245);
        fillRect(ax + 6, ay + 1, 1, 1, 30, 34, 42, 245);
        fillRect(ax + 1, ay + 6, 1, 1, 30, 34, 42, 245);
        fillRect(ax + 6, ay + 6, 1, 1, 30, 34, 42, 245);
        return;
    }

    if (side == SupporterSide::kHome || side == SupporterSide::kAway) {
        const TeamGame& team = (side == SupporterSide::kHome) ? swos.topTeamInGame : swos.bottomTeamInGame;
        SDL_Color primary = swosKitColorToRgb(team.prShirtCol);
        SDL_Color accent = swosKitColorToRgb(team.prStripesCol);
        // Club crest / shield icon
        fillRect(ax, ay, 8, 6, 220, 224, 232, 240);
        fillRect(ax + 1, ay + 6, 6, 1, 220, 224, 232, 240);
        fillRect(ax + 2, ay + 7, 4, 1, 220, 224, 232, 240);
        fillRect(ax + 1, ay + 1, 6, 5, primary.r, primary.g, primary.b, 245);
        fillRect(ax + 2, ay + 6, 4, 1, primary.r, primary.g, primary.b, 245);
        fillRect(ax + 3, ay + 1, 2, 5, accent.r, accent.g, accent.b, 245);
        return;
    }

    // Retro pixel supporter face
    fillRect(ax + 1, ay, 6, 2, 96, 62, 36, 245);          // hair
    fillRect(ax + 1, ay + 2, 6, 4, 236, 186, 148, 245);  // face
    fillRect(ax + 2, ay + 3, 1, 1, 25, 25, 30, 245);     // left eye
    fillRect(ax + 5, ay + 3, 1, 1, 25, 25, 30, 245);     // right eye
    fillRect(ax + 1, ay + 6, 6, 2, 68, 128, 210, 245);   // jersey collar
}

void enqueueSupporterBatchRequest(const char *triggerContext)
{
    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;

    MatchTelemetrySnapshot snap = captureMatchTelemetrySnapshot();
    std::string prompt = formatTelemetryPromptBlock(snap);
    prompt += "\nTrigger: " + std::string(triggerContext ? triggerContext : "LIVE FLOW");
    prompt += "\nGenerate 2 to 3 short live supporter chat messages reacting to this exact moment (clock, score, territory, events).";
    prompt += "\nFormat STRICTLY one message per line as: HANDLE|SIDE|MESSAGE";
    prompt += "\n- HANDLE: realistic 1997 fan nickname (no spaces, max 13 chars, e.g. Fan_atic, Terrace97, GoalHunter)";
    prompt += "\n- SIDE: HOME (supports " + snap.team1 + "), AWAY (supports " + snap.team2 + "), or NEUTRAL";
    prompt += "\n- MESSAGE: short punchy fan reaction (4 to 14 words). If minute >= 85 and margin <= 1, leading fans beg the ref to blow the whistle!";
    prompt += "\nOutput ONLY the HANDLE|SIDE|MESSAGE lines. No intro, no numbering, no markdown.";

    const std::string sys = "You simulate a live 1997 football fan chat stream watching the match. "
                            "Be passionate, varied, and strictly faithful to the telemetry facts (score, minute, scorer, finish type, stats). "
                            "Output strictly HANDLE|SIDE|MESSAGE lines only.";

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

        std::string handle = sanitizeStreamUpper(trimStream(line.substr(0, p1)));
        std::string sideStr = sanitizeStreamUpper(trimStream(line.substr(p1 + 1, p2 - p1 - 1)));
        std::string msg = sanitizeStreamUpper(trimStream(line.substr(p2 + 1)));

        if (!handle.empty() && handle.front() == '@')
            handle.erase(handle.begin());
        if (handle.size() > 13)
            handle.resize(13);
        if (handle.empty() || msg.empty())
            continue;
        if (msg.size() > 96)
            msg.resize(96);

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
    s_pendingBatch.clear();
    s_visibleTweets.clear();
}

void toggleSupporterStreamVisible()
{
    s_userHidden = !s_userHidden;
}

bool isSupporterStreamVisible()
{
    const auto& cfg = getLlmConfig();
    return cfg.enabled && cfg.features.supporterStream && cfg.telemetry.enabled && !s_userHidden;
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
    if (!cfg.enabled || !cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;

    if (s_cooldownFrames > 0)
        --s_cooldownFrames;
    if (s_staggerTimerFrames > 0)
        --s_staggerTimerFrames;

    for (auto& tw : s_visibleTweets)
        ++tw.ageFrames;

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
    if (!cfg.enabled || !cfg.features.supporterStream || !cfg.telemetry.enabled)
        return;
    if (s_visibleTweets.empty() && s_streamRequestId == 0)
        return;

    SDL_Renderer *renderer = getRenderer();
    if (!renderer)
        return;

    float gameScale = getGameScale();
    float xOffset = getGameScreenOffsetX() / gameScale;
    float yOffset = getGameScreenOffsetY() / gameScale;

    constexpr float kPanelW = 104.0f;
    constexpr float kPanelH = 166.0f;
    constexpr float kPanelY = 16.0f;
    // Anchor cleanly along the right edge of the visible screen (using widescreen space when available)
    float panelXInVga = static_cast<float>(kVgaWidth) + xOffset - kPanelW - 4.0f;
    float screenX = panelXInVga + xOffset;
    float screenY = kPanelY + yOffset;

    float origScaleX = 1.0f, origScaleY = 1.0f;
    SDL_RenderGetScale(renderer, &origScaleX, &origScaleY);
    SDL_RenderSetScale(renderer, gameScale, gameScale);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 1. Semi-transparent dark card backdrop + subtle border
    SDL_SetRenderDrawColor(renderer, 18, 24, 18, 178);
    SDL_FRect panelRect{ screenX, screenY, kPanelW, kPanelH };
    SDL_RenderFillRectF(renderer, &panelRect);

    // Header band slightly darker
    SDL_SetRenderDrawColor(renderer, 12, 16, 12, 200);
    SDL_FRect hdrBand{ screenX + 1.0f, screenY + 1.0f, kPanelW - 2.0f, 15.0f };
    SDL_RenderFillRectF(renderer, &hdrBand);

    // Crisp outer border & header separator
    SDL_SetRenderDrawColor(renderer, 155, 168, 150, 140);
    SDL_RenderDrawRectF(renderer, &panelRect);
    SDL_SetRenderDrawColor(renderer, 140, 152, 136, 110);
    SDL_RenderDrawLineF(renderer, screenX + 2.0f, screenY + 16.0f, screenX + kPanelW - 2.0f, screenY + 16.0f);

    // Red "• LIVE" badge pill
    SDL_SetRenderDrawColor(renderer, 220, 42, 42, 235);
    SDL_FRect livePill{ screenX + 5.0f, screenY + 4.0f, 25.0f, 9.0f };
    SDL_RenderFillRectF(renderer, &livePill);

    // Pulsing white live dot
    bool dotBright = ((SDL_GetTicks() / 450) & 1) == 0;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, dotBright ? 255 : 130);
    SDL_FRect liveDot{ screenX + 7.0f, screenY + 7.0f, 2.0f, 2.0f };
    SDL_RenderFillRectF(renderer, &liveDot);

    // Draw avatars for visible tweets while scale is active
    constexpr float kAvatarXRel = 5.0f;
    constexpr int kTextXRel = 16;
    constexpr int kMaxTextPx = static_cast<int>(kPanelW) - kTextXRel - 4;

    struct LaidOutTweet {
        int yVga;
        int alpha;
        const SupporterTweet *tw;
        std::vector<std::string> lines;
    };
    std::vector<LaidOutTweet> layout;
    int curY = static_cast<int>(kPanelY) + 21;
    int maxBottomY = static_cast<int>(kPanelY + kPanelH) - 6;

    for (size_t i = 0; i < s_visibleTweets.size(); ++i) {
        const auto& tw = s_visibleTweets[i];
        auto lines = wrapLlmReportLines(tw.message, kMaxTextPx);
        if (lines.size() > 3)
            lines.resize(3);
        int blockH = 7 + static_cast<int>(lines.size()) * 7 + 5;
        if (curY + blockH > maxBottomY)
            break;

        int fadeIn = std::min(255, 60 + tw.ageFrames * 14);
        int posFade = (i >= 4) ? 145 : (i == 3 ? 195 : 255);
        int alpha = std::min(fadeIn, posFade);

        drawPixelAvatar(renderer, screenX + kAvatarXRel, static_cast<float>(curY) + yOffset + 1.0f,
            tw.side, tw.avatarVariant, static_cast<Uint8>(alpha));

        layout.push_back({ curY, alpha, &tw, std::move(lines) });
        curY += blockH;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_RenderSetScale(renderer, origScaleX, origScaleY);

    // 2. Crisp bitmap text header + tweets (using VGA coords relative to 320x200 origin)
    int hdrX = static_cast<int>(panelXInVga);
    int hdrY = static_cast<int>(kPanelY);
    drawText(hdrX + 11, hdrY + 6, "LIVE", 20, kWhiteText2, false, 255);
    drawText(hdrX + 33, hdrY + 6, "CHAT STREAM", 68, kWhiteText2, false, 255);

    for (const auto& item : layout) {
        int textX = hdrX + kTextXRel;
        int handleColor = (item.tw->side == SupporterSide::kHome) ? kYellowText :
                          (item.tw->side == SupporterSide::kAway) ? kWhiteText : kYellowText;
        std::string handleLabel = item.tw->handle + ":";
        drawText(textX, item.yVga, handleLabel.c_str(), kMaxTextPx, handleColor, false, item.alpha);

        int lineY = item.yVga + 7;
        for (const auto& ln : item.lines) {
            drawText(textX, lineY, ln.c_str(), kMaxTextPx, kWhiteText2, false, item.alpha);
            lineY += 7;
        }
    }
}
