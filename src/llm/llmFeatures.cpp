#include "llmFeatures.h"
#include "compactFont.h"
#include "llmClient.h"
#include "llmReportMenu.h"
#include "matchTelemetry.h"
#include "sponsorChallenge.h"
#include "supporterStream.h"
#include "continueAbortMenu.h"
#include "controls.h"
#include "darkRectangle.h"
#include "drawMenu.h"
#include "gameFieldMapping.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "menuBackground.h"
#include "menus.h"
#include "pitch.h"
#include "render.h"
#include "result.h"
#include "stats.h"
#include "swos.h"
#include "text.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

extern void cseg_2484B();

namespace {

uint64_t s_postMatchRequestId = 0;
bool s_postMatchTriggered = false;
bool s_handledMatchInGameLoop = false;
uint64_t s_preMatchRequestId = 0;
uint64_t s_punditRequestId = 0;
int s_punditCooldownFrames = 0;
int s_punditDisplayFrames = 0;
std::vector<std::string> s_punditLines;
struct SquadPlayerSnapshot {
    std::string name;
    std::string position;
    std::string marketValue;
};

int s_weeklyTickCounter = 0;
std::unordered_map<int16_t, std::vector<SquadPlayerSnapshot>> s_squadSnapshots;

const char *playerPositionName(byte positionAndFace)
{
    int pos = (positionAndFace >> 5) & 0x07;
    switch (pos) {
    case 0: return "GOALKEEPER";
    case 1: return "RIGHT BACK";
    case 2: return "LEFT BACK";
    case 3: return "DEFENDER";
    case 4: return "RIGHT WINGER";
    case 5: return "LEFT WINGER";
    case 6: return "MIDFIELDER";
    case 7: return "ATTACKER";
    default: return "OUTFIELD PLAYER";
    }
}

std::string cleanSwosPlayerName(const char *raw, size_t maxBytes)
{
    std::string out;
    if (!raw)
        return out;
    for (size_t i = 0; i < maxBytes && raw[i] != '\0'; ++i) {
        unsigned char ch = static_cast<unsigned char>(raw[i]);
        if (ch >= 0x20 && ch <= 0x7E) {
            out.push_back(static_cast<char>(ch));
        } else if (ch == static_cast<unsigned char>(kAUmlautChar)) {
            out.push_back('A');
        } else if (ch == static_cast<unsigned char>(kOUmlautChar)) {
            out.push_back('O');
        } else if (ch == static_cast<unsigned char>(kUUmlautChar)) {
            out.push_back('U');
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string formatThousandsPounds(uint16_t thousands)
{
    char buf[32];
    if (thousands >= 1000) {
        if (thousands % 1000 == 0)
            std::snprintf(buf, sizeof(buf), "GBP %uM", thousands / 1000);
        else if (thousands % 100 == 0)
            std::snprintf(buf, sizeof(buf), "GBP %u.%uM", thousands / 1000, (thousands % 1000) / 100);
        else
            std::snprintf(buf, sizeof(buf), "GBP %u.%02uM", thousands / 1000, (thousands % 1000) / 10);
    } else {
        std::snprintf(buf, sizeof(buf), "GBP %uK", thousands);
    }
    return buf;
}

std::string formatExactCareerPlayerMarketValue(uint16_t playerIdx, const PlayerFile *pf)
{
    if (!pf)
        return "GBP 0";
    if (pf->someFlag == 0x9A)
        return "Reserve";
    if ((pf->positionAndFace & 2u) != 0)
        return "On Loan";

    // Invoke SWOS's exact native market-value index function (cseg_2484B) which applies
    // avgTeamValues[pf->price] + live career form/performance modifiers (cseg_94C7E),
    // then look up the exact pound figure in VM table avgTeamValueBases (offset 331612).
    uint16_t valIdx = 1;
    uint32_t playerHeaderVmOfs = static_cast<uint32_t>(
        CareerOffsets::kCareerTeam + static_cast<size_t>(playerIdx) * HotseatInternal::kPlayerFileSize);
    invokeWithSaved68kRegisters([&]() {
        A0 = playerHeaderVmOfs;
        cseg_2484B();
        valIdx = D0.asWord();
    });
    valIdx = std::clamp<uint16_t>(valIdx, 1, 80);
    constexpr size_t kAvgTeamValueBasesOfs = 331612;
    uint16_t thousands = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[kAvgTeamValueBasesOfs + static_cast<size_t>(valIdx) * 2]);
    return formatThousandsPounds(thousands);
}

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
        if (ch == '*' || ch == '#' || ch == '`')
            continue;
        if (ch >= 0x20 && ch <= 0x7E) {
            pushChar(static_cast<char>(std::toupper(ch)));
            continue;
        }
        // Transliterate common 2-byte UTF-8 Latin/Turkish/European letters so non-English replies stay readable
        if ((ch & 0xE0) == 0xC0 && i + 1 < in.size()) {
            unsigned char b2 = static_cast<unsigned char>(in[++i]);
            unsigned int cp = ((ch & 0x1Fu) << 6) | (b2 & 0x3Fu);
            switch (cp) {
            case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
            case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
                pushChar('A'); break;
            case 0x00C7: case 0x00E7: case 0x0106: case 0x0107: case 0x010C: case 0x010D:
                pushChar('C'); break;
            case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
            case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
                pushChar('E'); break;
            case 0x011E: case 0x011F:
                pushChar('G'); break;
            case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
            case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
            case 0x0130: case 0x0131:
                pushChar('I'); break;
            case 0x00D1: case 0x00F1:
                pushChar('N'); break;
            case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
            case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
                pushChar('O'); break;
            case 0x015E: case 0x015F: case 0x0160: case 0x0161:
                pushChar('S'); break;
            case 0x00DF:
                pushChar('S'); pushChar('S'); break;
            case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
            case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
                pushChar('U'); break;
            case 0x00DD: case 0x00FD: case 0x00FF:
                pushChar('Y'); break;
            case 0x017D: case 0x017E:
                pushChar('Z'); break;
            case 0x0110: case 0x0111:
                pushChar('D'); break;
            default:
                break;
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
        candidates.emplace_back(pf->playerName, playerPositionName(pf->positionAndFace));
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
    showLlmReportDialog(header, body);
}

bool waitForLlmWithIndicator(uint64_t requestId, int maxWaitMs, const char *statusLabel, std::string& outText)
{
    if (requestId == 0)
        return false;
    if (tryTakeLlmResponse(requestId, outText))
        return true;

#ifdef SWOS_TEST
    return waitForLlmResponse(requestId, maxWaitMs, outText);
#else
    static const char kSpinnerChars[4] = { '|', '/', '-', '\\' };
    Uint32 startTicks = SDL_GetTicks();
    Uint32 deadline = startTicks + static_cast<Uint32>(std::max(0, maxWaitMs));
    int frame = 0;

    setStandardMenuBackgroundImage();
    while (SDL_GetTicks() < deadline) {
        if (tryTakeLlmResponse(requestId, outText))
            return true;

        drawMenuBackground();
        drawDarkRectangle({ 30.0f, 76.0f, 260.0f, 48.0f }, true);

        int spinIdx = (frame / 3) & 3;
        int dotsCount = (frame / 6) & 3;
        std::string dots(static_cast<size_t>(dotsCount), '.');
        std::string line1 = std::string("[") + kSpinnerChars[spinIdx] + "] " + statusLabel + dots;

        SDL_Renderer *r = getRenderer();
        SDL_Texture *atlas = getCompactFontAtlas(r);
        if (r && atlas) {
            float gameScale = getGameScale();
            float px = computeCompactPixelScaleForVga(gameScale, 0.48f);
            float centerX = getGameScreenOffsetX() + (static_cast<float>(kVgaWidth) * 0.5f) * gameScale;
            float baseY = getGameScreenOffsetY() + 76.0f * gameScale;
            float origSX = 1.0f, origSY = 1.0f;
            SDL_RenderGetScale(r, &origSX, &origSY);
            SDL_RenderSetScale(r, 1.0f, 1.0f);

            drawCompactTextCentered(r, atlas, centerX, baseY + 10.0f * gameScale, px,
                line1, { 255, 226, 78, 255 }, 255, true);
            drawCompactTextCentered(r, atlas, centerX, baseY + 22.0f * gameScale, px,
                "AI thinking (warmup may take a moment)", { 244, 246, 248, 255 }, 245, false);
            drawCompactTextCentered(r, atlas, centerX, baseY + 34.0f * gameScale, px,
                "Press ESC to skip", { 176, 210, 238, 255 }, 230, false);

            SDL_RenderSetScale(r, origSX, origSY);
        }

        updateScreen(false);
        SDL_Delay(33);
        processControlEvents();

        if (SDL_GetTicks() - startTicks > 250) {
            const Uint8 *keys = SDL_GetKeyboardState(nullptr);
            if (keys && keys[SDL_SCANCODE_ESCAPE])
                return false;
        }
        ++frame;
    }
    return tryTakeLlmResponse(requestId, outText);
#endif
}

void resolveTeam1AndTeam2(const TeamGeneralInfo*& outTeam1, const TeamGeneralInfo*& outTeam2)
{
    if (swos.topTeamData.inGameTeamPtr.asAligned() == &swos.topTeamInGame) {
        outTeam1 = &swos.topTeamData;
        outTeam2 = &swos.bottomTeamData;
    } else {
        outTeam1 = &swos.bottomTeamData;
        outTeam2 = &swos.topTeamData;
    }
}

std::string buildMatchPerspectiveContext(const char *t1, const char *t2, int g1, int g2,
    bool topHuman, bool botHuman)
{
    if (topHuman && botHuman) {
        return "TWO-PLAYER HEAD-TO-HEAD MATCH: " + std::string(t1) + " (PLAYER 1) " +
            std::to_string(g1) + "-" + std::to_string(g2) + " " + t2 + " (PLAYER 2). " +
            "Comment on both human players fairly.";
    }

    if (topHuman || botHuman) {
        const char *humanTeam = topHuman ? t1 : t2;
        const char *cpuTeam = topHuman ? t2 : t1;
        int humanGoals = topHuman ? g1 : g2;
        int cpuGoals = topHuman ? g2 : g1;
        int humanPen = topHuman ? swos.team1PenaltyGoals : swos.team2PenaltyGoals;
        int cpuPen = topHuman ? swos.team2PenaltyGoals : swos.team1PenaltyGoals;

        std::string outcome = "DREW";
        if (humanGoals > cpuGoals || (humanGoals == cpuGoals && swos.playingPenalties && humanPen > cpuPen))
            outcome = "WON";
        else if (humanGoals < cpuGoals || (humanGoals == cpuGoals && swos.playingPenalties && humanPen < cpuPen))
            outcome = "LOST";

        return "HUMAN PLAYER'S CLUB: " + std::string(humanTeam) + " " + outcome + " " +
            std::to_string(humanGoals) + "-" + std::to_string(cpuGoals) + " against CPU club " + cpuTeam + ". " +
            "IMPORTANT: Write strictly from " + humanTeam + "'s perspective reacting to this " + outcome + ".";
    }

    return "Match: " + std::string(t1) + " " + std::to_string(g1) + "-" + std::to_string(g2) + " " + t2 + ".";
}

struct LlmMatchContext {
    std::string team1;
    std::string team2;
    std::string coach1;
    std::string coach2;
    std::string players1;
    std::string players2;
    std::string competition;
    std::string pitchCondition;
    bool team1Human = false;
    bool team2Human = false;
};

std::string extractCoachName(const TeamFile *tf)
{
    if (!tf || tf->coachName[0] == '\0')
        return {};
    std::string name;
    for (size_t i = 0; i < sizeof(tf->coachName) && tf->coachName[i] != '\0'; ++i) {
        unsigned char ch = tf->coachName[i];
        if (ch >= 0x20 && ch <= 0x7E)
            name.push_back(static_cast<char>(ch));
    }
    return name;
}

std::string collectOutfieldNames(const TeamGame *team)
{
    if (!team)
        return {};
    std::string names;
    for (int i = 1; i < 11; ++i) {
        if (team->players[i].shortName[0] == '\0')
            continue;
        if (!names.empty())
            names += ", ";
        names += team->players[i].shortName;
        if (names.size() > 56)
            break;
    }
    return names;
}

LlmMatchContext buildLlmMatchContext(const TeamGame *preMatchTop = nullptr, const TeamGame *preMatchBot = nullptr)
{
    LlmMatchContext ctx;
    if (preMatchTop && preMatchBot) {
        ctx.team1 = preMatchTop->teamName[0] ? preMatchTop->teamName : "HOME";
        ctx.team2 = preMatchBot->teamName[0] ? preMatchBot->teamName : "AWAY";
        ctx.players1 = collectOutfieldNames(preMatchTop);
        ctx.players2 = collectOutfieldNames(preMatchBot);
    } else {
        ctx.team1 = swos.topTeamInGame.teamName[0] ? swos.topTeamInGame.teamName : "HOME";
        ctx.team2 = swos.bottomTeamInGame.teamName[0] ? swos.bottomTeamInGame.teamName : "AWAY";
        ctx.players1 = collectOutfieldNames(&swos.topTeamInGame);
        ctx.players2 = collectOutfieldNames(&swos.bottomTeamInGame);
    }

    const TeamGeneralInfo *team1Info = nullptr;
    const TeamGeneralInfo *team2Info = nullptr;
    resolveTeam1AndTeam2(team1Info, team2Info);
    ctx.team1Human = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0 || team1Info->isPlCoach != 0);
    ctx.team2Human = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0 || team2Info->isPlCoach != 0);

    const TeamFile *tf1 = swos.gameTeam1.asAligned();
    const TeamFile *tf2 = swos.gameTeam2.asAligned();
    if (!tf1) tf1 = swos.playMatchTeam1Ptr.asAligned();
    if (!tf2) tf2 = swos.playMatchTeam2Ptr.asAligned();
    ctx.coach1 = extractCoachName(tf1);
    ctx.coach2 = extractCoachName(tf2);
    if (!ctx.team1Human && !ctx.team2Human) {
        if (tf1)
            ctx.team1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
        if (tf2)
            ctx.team2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);
    }

    // Career fallback: match active career club name directly so away matches always identify the human club accurately
    if (ctx.team1Human == ctx.team2Human && swos.g_gameType == kGameTypeCareer) {
        std::string careerClub;
        const auto& mgr = HotseatCareerManager::get();
        if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
            careerClub = sanitizeAsciiUpper(mgr.activePlayer().teamName);
        } else {
            const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
            if (hdr && hdr->teamName[0] != '\0')
                careerClub = sanitizeAsciiUpper(hdr->teamName);
        }
        if (!careerClub.empty()) {
            bool m1 = (sanitizeAsciiUpper(ctx.team1) == careerClub);
            bool m2 = (sanitizeAsciiUpper(ctx.team2) == careerClub);
            if (m1 != m2) {
                ctx.team1Human = m1;
                ctx.team2Human = m2;
            }
        }
    }

    ctx.competition = gameModeName();
    if (isPitchTypeResolved()) {
        ctx.pitchCondition = pitchConditionName(getPitchType());
    }
    return ctx;
}

std::string formatLlmMatchInfoBase(const LlmMatchContext& ctx)
{
    std::string info = "Competition: " + ctx.competition;
    if (!ctx.pitchCondition.empty())
        info += ", Football pitch grass surface: " + ctx.pitchCondition;
    info += "\n" + ctx.team1 + (ctx.team1Human ? " [HUMAN]" : " [CPU]");
    if (!ctx.coach1.empty()) info += " (Coach: " + ctx.coach1 + ")";
    if (!ctx.players1.empty()) info += " | Players: " + ctx.players1;
    info += "\n" + ctx.team2 + (ctx.team2Human ? " [HUMAN]" : " [CPU]");
    if (!ctx.coach2.empty()) info += " (Coach: " + ctx.coach2 + ")";
    if (!ctx.players2.empty()) info += " | Players: " + ctx.players2;
    return info;
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

    LlmMatchContext ctx = buildLlmMatchContext();
    int g1 = swos.statsTeam1Goals;
    int g2 = swos.statsTeam2Goals;
    const auto stats = getStats();
    std::string s1 = getMatchScorersSummary(1);
    std::string s2 = getMatchScorersSummary(2);

    std::string prompt = buildMatchPerspectiveContext(ctx.team1.c_str(), ctx.team2.c_str(), g1, g2, ctx.team1Human, ctx.team2Human);
    prompt += "\n" + formatLlmMatchInfoBase(ctx);
    prompt += "\nScoreline: " + ctx.team1 + " " + std::to_string(g1) + "-" + std::to_string(g2) + " " + ctx.team2;
    if (swos.playingPenalties) {
        prompt += " (PENS " + std::to_string(swos.team1PenaltyGoals) + "-" + std::to_string(swos.team2PenaltyGoals) + ")";
    }
    if (!s1.empty()) prompt += "\n" + ctx.team1 + " scorers: " + s1;
    if (!s2.empty()) prompt += "\n" + ctx.team2 + " scorers: " + s2;
    prompt += "\nStats: Shots " + std::to_string(stats.team1.goalAttempts) + "-" + std::to_string(stats.team2.goalAttempts);
    prompt += ", Possession " + std::to_string(stats.team1.ballPossession) + "%-" + std::to_string(stats.team2.ballPossession) + "%.";
    std::string sponsorNote = formatLastSponsorChallengeOutcomeForNewspaper();
    if (!sponsorNote.empty())
        prompt += "\n" + sponsorNote;
    prompt += "\nWrite an authentic, lively 1997 football back-page newspaper report + board reaction:\n"
              "Line 1: Punchy tabloid headline (all caps style, witty, referencing the actual result)\n"
              "Paragraph 2: 3 vivid sentences summarizing how the match unfolded using the real scorers, shots, possession, and pitch condition (only if listed above). Do NOT invent players, goals, or weather.\n"
              "Final paragraph starting with 'CHAIRMAN: ': 2 expressive sentences in character from the human club's chairman reacting strictly to the actual WIN/LOSS/DRAW.\n"
              "Plain text only, no markdown, no bullet points.";

    const std::string sys = "You are a witty, atmospheric 1997 British football back-page journalist and expressive club chairman. "
                            "Write vivid, natural sports prose grounded strictly in the provided match facts. "
                            "Never confuse who won or lost. Never invent weather or scorers not given. Plain text only.";
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
    if (waitForLlmWithIndicator(s_postMatchRequestId, cfg.timeoutMs, "PRINTING DAILY ECHO", reply)) {
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
    userPrompt += " Reply with ONE sharp, authentic 1997 football manager negotiation line (plain text, no quotation marks).";

    uint64_t reqId = enqueueLlmRequest(
        "You are a shrewd 1997 football manager negotiating a transfer on the phone. Speak naturally with character in one short sentence. Plain text only.",
        userPrompt);
    if (reqId == 0)
        return {};

    std::string reply;
    if (!waitForLlmWithIndicator(reqId, cfg.timeoutMs, "CONTACTING AGENT", reply))
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

    const TeamGeneralInfo *team1Info = nullptr;
    const TeamGeneralInfo *team2Info = nullptr;
    resolveTeam1AndTeam2(team1Info, team2Info);

    const char *t1 = swos.topTeamInGame.teamName[0] ? swos.topTeamInGame.teamName : "HOME";
    const char *t2 = swos.bottomTeamInGame.teamName[0] ? swos.bottomTeamInGame.teamName : "AWAY";
    bool topHuman = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0);
    bool botHuman = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0);

    std::string prompt = buildMatchPerspectiveContext(t1, t2, topGoals, botGoals, topHuman, botHuman) +
        "\nSpeaker's club: " + club + ". Player speaking: " + player + " (" + role + "). "
        "Write an expressive 2-3 sentence 1997 post-match dressing room quote from " + player +
        " reacting honestly to this exact result and looking ahead to the next fixture. Start with '" + player + ": '. Plain text only.";

    uint64_t reqId = enqueueLlmRequest(
        "You are a passionate 1997 footballer speaking candidly in the dressing room right after full-time. "
        "Sound like a real player, never generic or robotic, and strictly match the actual win/loss/draw. Plain text only.",
        prompt);
    if (reqId == 0)
        return;

    std::string reply;
    if (waitForLlmWithIndicator(reqId, cfg.timeoutMs, "DRESSING ROOM", reply)) {
        showWrappedSwosPrompt("DRESSING ROOM VOICE", reply);
    }
}

void markMatchHandledInGameLoop()
{
    s_handledMatchInGameLoop = true;
}

void onViewResultMatchCompletedLlm()
{
    if (s_handledMatchInGameLoop) {
        s_handledMatchInGameLoop = false;
        return;
    }
    if (swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled)
        return;

    const auto *t1File = swos.playMatchTeam1Ptr.asAligned();
    const auto *t2File = swos.playMatchTeam2Ptr.asAligned();
    if (!t1File || !t2File)
        return;

    const char *t1 = t1File->teamName[0] ? t1File->teamName : "HOME";
    const char *t2 = t2File->teamName[0] ? t2File->teamName : "AWAY";
    int g1 = static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[379094]));
    int g2 = static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[379096]));
    bool topHuman = (t1File->teamControls == kPlayerCoach || t1File->teamControls == kCoach);
    bool botHuman = (t2File->teamControls == kPlayerCoach || t2File->teamControls == kCoach);

    if (cfg.features.postMatchNewspaper) {
        std::string prompt = buildMatchPerspectiveContext(t1, t2, g1, g2, topHuman, botHuman);
        prompt += "\nScoreline: " + std::string(t1) + " " + std::to_string(g1) + "-" + std::to_string(g2) + " " + t2;
        prompt += "\nCompetition: " + std::string(gameModeName()) + ".";
        prompt += "\nWrite an authentic, lively 1997 football back-page newspaper report + board reaction:\n"
                  "Line 1: Punchy tabloid headline referencing the real result\n"
                  "Paragraph 2: 2-3 vivid sentences summarizing the outcome without inventing scorers or weather.\n"
                  "Final paragraph starting with 'CHAIRMAN: ': 2 expressive sentences from the human club's chairman reacting strictly to the actual WIN/LOSS/DRAW. Plain text only.";

        uint64_t reqId = enqueueLlmRequest(
            "You are a witty 1997 British football back-page journalist and expressive club chairman. "
            "Never confuse who won or lost. Never invent scorers or weather. Plain text only.",
            prompt);
        std::string reply;
        if (waitForLlmWithIndicator(reqId, cfg.timeoutMs, "PRINTING DAILY ECHO", reply)) {
            showWrappedSwosPrompt("DAILY SOCCER ECHO", reply);
        }
    }

    if (swos.g_gameType == kGameTypeCareer && cfg.features.dressingRoomVoices) {
        std::string club, player, role;
        if (!pickCareerSquadVoice(club, player, role).empty()) {
            std::string prompt = buildMatchPerspectiveContext(t1, t2, g1, g2, topHuman, botHuman) +
                "\nSpeaker's club: " + club + ". Player speaking: " + player + " (" + role + "). "
                "Write an expressive 2-3 sentence 1997 dressing room quote from " + player +
                " reacting honestly to this exact result. Start with '" + player + ": '. Plain text only.";
            uint64_t reqId = enqueueLlmRequest(
                "You are a passionate 1997 footballer speaking in the dressing room after full-time. Sound authentic and match the exact result. Plain text only.",
                prompt);
            std::string reply;
            if (waitForLlmWithIndicator(reqId, cfg.timeoutMs, "DRESSING ROOM", reply)) {
                showWrappedSwosPrompt("DRESSING ROOM VOICE", reply);
            }
        }
    }
}

void resetCareerLlmSnapshots()
{
    s_squadSnapshots.clear();
}

void checkCareerSquadTransfersLlm()
{
    static bool s_inTransferCheck = false;
    if (s_inTransferCheck)
        return;

    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame ||
        HotseatCareerManager::get().isInitializingSeason())
        return;

    struct Guard {
        bool& flag;
        explicit Guard(bool& f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(s_inTransferCheck);

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.transferNegotiations)
        return;

    uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    if (numPlayers == 0 || numPlayers > kMaxCareerSquadPlayers)
        return;

    const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
    int16_t teamId = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    std::string clubName = hdr->teamName[0] ? std::string(hdr->teamName) : std::string("THE CLUB");

    std::vector<SquadPlayerSnapshot> currentSquad;
    currentSquad.reserve(numPlayers);
    for (uint16_t i = 0; i < numPlayers; ++i) {
        size_t ofs = CareerOffsets::kCareerTeam +
                     HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(i) * HotseatInternal::kPlayerFileSize;
        const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
        std::string pName = cleanSwosPlayerName(pf->playerName, sizeof(pf->playerName));
        if (!pName.empty()) {
            currentSquad.push_back({
                std::move(pName),
                playerPositionName(pf->positionAndFace),
                formatExactCareerPlayerMarketValue(i, pf)
            });
        }
    }
    if (currentSquad.empty())
        return;

    auto it = s_squadSnapshots.find(teamId);
    if (it == s_squadSnapshots.end()) {
        s_squadSnapshots[teamId] = std::move(currentSquad);
        return;
    }

    const auto& prevSquad = it->second;
    std::unordered_set<std::string> prevSet;
    for (const auto& p : prevSquad) prevSet.insert(p.name);
    std::unordered_set<std::string> currSet;
    for (const auto& p : currentSquad) currSet.insert(p.name);

    std::vector<SquadPlayerSnapshot> added;
    std::vector<SquadPlayerSnapshot> removed;
    for (const auto& p : currentSquad) {
        if (prevSet.find(p.name) == prevSet.end())
            added.push_back(p);
    }
    for (const auto& p : prevSquad) {
        if (currSet.find(p.name) == currSet.end())
            removed.push_back(p);
    }

    s_squadSnapshots[teamId] = std::move(currentSquad);

    // Ignore bulk resets / season-init squad rebuilds
    if (added.empty() && removed.empty())
        return;
    if (added.size() > 2 || removed.size() > 2)
        return;

    std::string prompt;
    if (!added.empty()) {
        const auto& signedP = added.front();
        prompt = clubName + " just completed the signing of \"" + signedP.name + "\"" +
                 " (Position: " + signedP.position + ", Exact In-Game Market Value: " + signedP.marketValue + ").";
        if (!removed.empty()) {
            const auto& leftP = removed.front();
            prompt += " \"" + leftP.name + "\" (Position: " + leftP.position + ", Exact In-Game Market Value: " + leftP.marketValue + ") has departed the club.";
        }
        prompt += "\nSTRICT RULES: Keep the player's name EXACTLY as written (\"" + signedP.name + "\") - NEVER guess or invent a different first name or alter spelling. "
                  "State the EXACT market value (" + signedP.marketValue + ") and position (" + signedP.position + ") without converting currency or changing the number.\n"
                  "Write a lively 2-3 sentence 1997 transfer desk bulletin with a brief quote from the new signing or rival manager. Plain text only.";
    } else {
        const auto& leftP = removed.front();
        prompt = "\"" + leftP.name + "\" (Position: " + leftP.position + ", Exact In-Game Market Value: " + leftP.marketValue + ") has just left " + clubName + ".\n"
                 "STRICT RULES: Keep the player's name EXACTLY as written (\"" + leftP.name + "\") - NEVER alter spelling or invent another name. "
                 "State the EXACT market value (" + leftP.marketValue + ") and position (" + leftP.position + ") without converting currency or changing the number.\n"
                 "Write a lively 2-3 sentence 1997 transfer desk bulletin with a brief reaction from the fans or dressing room. Plain text only.";
    }

    uint64_t reqId = enqueueLlmRequest(
        "You are a sharp 1997 football transfer desk journalist covering deadline moves and squad shake-ups. "
        "CRITICAL: Copy every player name letter-for-letter as given (do not expand initials or change spelling), and always quote the exact position and exact GBP market value provided without converting currency. Plain text only.",
        prompt);
    if (reqId == 0)
        return;

    std::string reply;
    if (waitForLlmWithIndicator(reqId, cfg.timeoutMs, "CHECKING TRANSFER DESK", reply)) {
        showWrappedSwosPrompt("TRANSFER DESK", reply);
    }
}

void beginPreMatchScoutingRequest(const TeamGame *topTeam, const TeamGame *bottomTeam)
{
    s_preMatchRequestId = 0;
    if (!topTeam || !bottomTeam || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.preMatchScouting)
        return;

    LlmMatchContext ctx = buildLlmMatchContext(topTeam, bottomTeam);
    std::string prompt;
    std::string sys;

    if (ctx.team1Human != ctx.team2Human) {
        const std::string& myClub = ctx.team1Human ? ctx.team1 : ctx.team2;
        const std::string& myPlayers = ctx.team1Human ? ctx.players1 : ctx.players2;
        const std::string& oppClub = ctx.team1Human ? ctx.team2 : ctx.team1;
        const std::string& oppCoach = ctx.team1Human ? ctx.coach2 : ctx.coach1;
        const std::string& oppPlayers = ctx.team1Human ? ctx.players2 : ctx.players1;

        prompt = "Pre-match tunnel briefing:\n";
        prompt += "Competition: " + ctx.competition;
        if (!ctx.pitchCondition.empty())
            prompt += ", Football pitch grass surface: " + ctx.pitchCondition;
        prompt += "\nYOUR CLUB (the manager's own team that YOU work for): " + myClub;
        if (!myPlayers.empty())
            prompt += " | Our own players (DO NOT warn against them as opponents): " + myPlayers;
        prompt += "\nTODAY'S OPPONENT (the rival club we are playing against): " + oppClub;
        if (!oppCoach.empty())
            prompt += " (Coach: " + oppCoach + ")";
        if (!oppPlayers.empty())
            prompt += " | OPPONENT THREATS to be cautious about: " + oppPlayers;
        prompt += "\nGive a sharp, authentic 3-sentence assistant manager pre-match briefing to the " + myClub + " boss.\n"
                  "CRITICAL RULES:\n"
                  "- You are the assistant manager of " + myClub + ". Warn ONLY about " + oppClub + "'s players (" + oppPlayers + ") as opposition threats.\n"
                  "- NEVER describe " + myClub + " or " + myClub + "'s players as opponents or threats to watch out for.\n"
                  "- Only mention the football pitch surface if explicitly listed above; never invent weather. Plain text only.";

        sys = "You are the loyal 1997 assistant manager of " + myClub + " speaking to your boss in the tunnel before facing " + oppClub + ". "
              "Be specific, tactical, and atmospheric. NEVER confuse your own club (" + myClub + ") with the opponent (" + oppClub + "): "
              "only scout and warn against " + oppClub + "'s players. Plain text only.";
    } else {
        prompt = "Pre-match tunnel briefing:\n" + formatLlmMatchInfoBase(ctx);
        prompt += "\nGive a sharp, authentic 3-sentence tactical pre-match preview naming key players on both sides. "
                  "Only mention the football pitch surface if explicitly listed above; never invent weather. Plain text only.";
        sys = "You are an experienced 1997 football tactical analyst previewing a head-to-head match before kickoff. "
              "Be specific, tactical, and atmospheric using only the real club/player/pitch facts given. Plain text only.";
    }

    s_preMatchRequestId = enqueueLlmRequest(sys, prompt);
}

void showPreMatchScoutingIfReady()
{
    if (s_preMatchRequestId == 0 || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.preMatchScouting)
        return;

    std::string reply;
    setStandardMenuBackgroundImage();
    menuFadeIn();
    if (waitForLlmWithIndicator(s_preMatchRequestId, cfg.timeoutMs, "SCOUTING REPORT", reply)) {
        showWrappedSwosPrompt("ASSISTANT MANAGER", reply);
    }
    menuFadeOut();
    s_preMatchRequestId = 0;
}

void resetLivePunditTicker()
{
    s_punditRequestId = 0;
    s_punditCooldownFrames = 0;
    s_punditDisplayFrames = 0;
    s_punditLines.clear();
}

void triggerLivePunditEvent(const char *eventLabel, bool highPriority)
{
    if (!eventLabel || swos.g_trainingGame)
        return;

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.livePunditTicker)
        return;

    if (!highPriority && s_punditCooldownFrames > 0)
        return;

    MatchTelemetrySnapshot snap = captureMatchTelemetrySnapshot();
    std::string prompt = buildMatchPerspectiveContext(
        snap.team1.c_str(), snap.team2.c_str(), snap.goals1, snap.goals2, snap.team1Human, snap.team2Human);
    prompt += "\n" + formatTelemetryPromptBlock(snap);
    prompt += "\nLatest event to react to right now: " + std::string(eventLabel);
    if (!snap.recentEvents.empty()) {
        const auto& latest = snap.recentEvents.back();
        if (!latest.team.empty())
            prompt += " (" + latest.team + ")";
        if (!latest.player.empty())
            prompt += " by " + latest.player;
        if (!latest.detail.empty())
            prompt += " [" + latest.detail + "]";
    }
    prompt += "\nReact with ONE spontaneous, excited 1997 TV co-commentator sentence grounded strictly in the telemetry facts above "
              "(exact goalkeeper, exact team, exact scorer, exact finish type, exact minute). Plain text only, no quotes, no speaker prefix.";

    s_punditRequestId = enqueueLlmRequest(
        "You are an excited 1997 live football TV co-commentator like Barry Davies or Andy Gray. "
        "Always credit the exact goalkeeper, shooter, scorer, finish type, and team from the telemetry accurately. "
        "One vivid, punchy broadcast sentence. Plain text only, no speaker label.",
        prompt);
    s_punditCooldownFrames = 360; // ~6 seconds cooldown between non-goal events
}

void updateLivePunditTicker()
{
    if (s_punditCooldownFrames > 0)
        --s_punditCooldownFrames;
    if (s_punditDisplayFrames > 0)
        --s_punditDisplayFrames;

    if (s_punditRequestId == 0)
        return;

    std::string reply;
    if (tryTakeLlmResponse(s_punditRequestId, reply)) {
        s_punditRequestId = 0;
        if (!reply.empty())
            pushPunditMessageToStream(reply);
    }
}

void drawLivePunditTicker()
{
    // Live pundit commentary now renders directly inside the Live Chat Stream in yellow.
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
        ? "You are a well-connected 1997 football transfer agent whispering inside gossip. Write naturally in 2 sentences. Plain text only."
        : "You are a candid 1997 squad footballer chatting at the training ground. Write naturally in 2 sentences. Plain text only.";
    std::string prompt = isMarket
        ? ("Club: " + club + ". Write a lively 2-sentence transfer market whisper mentioning " + player + " (" + role + ") and rival interest. Plain text only.")
        : ("Club: " + club + ". Write a candid 2-sentence training-ground quote from " + player + " (" + role + ") about squad morale, form, or fighting for a starting shirt. Start with '" + player + ": '. Plain text only.");

    uint64_t reqId = enqueueLlmRequest(sys, prompt);
    if (reqId == 0)
        return;

    std::string reply;
    if (waitForLlmWithIndicator(reqId, cfg.timeoutMs, isMarket ? "TRANSFER RUMOURS" : "DRESSING ROOM", reply)) {
        showWrappedSwosPrompt(isMarket ? "TRANSFER WHISPER" : "DRESSING ROOM", reply);
    }
}
