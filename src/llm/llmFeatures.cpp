#include "llmFeatures.h"
#include "llmClient.h"
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

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

uint64_t s_postMatchRequestId = 0;
bool s_postMatchTriggered = false;
bool s_handledMatchInGameLoop = false;
uint64_t s_preMatchRequestId = 0;
uint64_t s_punditRequestId = 0;
int s_punditCooldownFrames = 0;
int s_punditDisplayFrames = 0;
std::vector<std::string> s_punditLines;
int s_weeklyTickCounter = 0;
std::unordered_map<int16_t, std::vector<std::string>> s_squadSnapshots;

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

        drawTextCentered(kVgaWidth / 2, 84, line1.c_str(), 248, kWhiteText2, false);
        drawTextCentered(kVgaWidth / 2, 97, "AI THINKING (WARMUP MAY TAKE A MOMENT)", 248, kWhiteText2, false);
        drawTextCentered(kVgaWidth / 2, 110, "PRESS ESC TO SKIP", 248, kWhiteText, false);

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
    ctx.team1Human = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0);
    ctx.team2Human = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0);

    const TeamFile *tf1 = swos.playMatchTeam1Ptr.asAligned();
    const TeamFile *tf2 = swos.playMatchTeam2Ptr.asAligned();
    ctx.coach1 = extractCoachName(tf1);
    ctx.coach2 = extractCoachName(tf2);
    if (tf1 && !ctx.team1Human)
        ctx.team1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
    if (tf2 && !ctx.team2Human)
        ctx.team2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);

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
        info += ", Pitch: " + ctx.pitchCondition;
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
    prompt += "\nWrite a punchy 1990s British sports newspaper blurb:\n"
              "Line 1: Tabloid headline (max 38 chars)\n"
              "Line 2-3: 1-2 short match sentences (max 75 chars total)\n"
              "Line 4: CHAIRMAN: short board reaction matching the exact WIN/LOSS/DRAW outcome (max 70 chars). No markdown.";

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
    userPrompt += " Reply with ONE short in-character 1990s football manager quote (max 72 chars), plain ASCII, no quotes.";

    uint64_t reqId = enqueueLlmRequest(
        "You are a 1995 football manager negotiating a transfer. One punchy sentence, max 72 chars, plain ASCII.",
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
        " Speaker's club: " + club + ". Player speaking: " + player + " (" + role + "). "
        "Give a 1-2 sentence 1990s dressing room reaction matching that exact result (max 110 chars). Prefix with player name.";

    uint64_t reqId = enqueueLlmRequest(
        "You are a 1995 football player in the dressing room after a match. Plain ASCII, max 110 chars, no markdown.",
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
        prompt += "\nCompetition: " + std::string(gameModeName()) + " (VIEW RESULT).";
        prompt += "\nWrite a punchy 1990s British sports newspaper blurb:\n"
                  "Line 1: Tabloid headline (max 38 chars)\n"
                  "Line 2-3: 1-2 short match sentences (max 75 chars total)\n"
                  "Line 4: CHAIRMAN: short board reaction matching the exact WIN/LOSS/DRAW outcome (max 70 chars). No markdown.";

        uint64_t reqId = enqueueLlmRequest(
            "You are a 1995 British football tabloid editor and club chairman. Be concise, punchy, plain ASCII text, no markdown, no bullets.",
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
                " Speaker's club: " + club + ". Player speaking: " + player + " (" + role + "). "
                "Give a 1-2 sentence 1990s dressing room reaction matching that exact result (max 110 chars). Prefix with player name.";
            uint64_t reqId = enqueueLlmRequest(
                "You are a 1995 football player in the dressing room after a match. Plain ASCII, max 110 chars, no markdown.",
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

    std::vector<std::string> currentNames;
    currentNames.reserve(numPlayers);
    for (uint16_t i = 0; i < numPlayers; ++i) {
        size_t ofs = CareerOffsets::kCareerTeam +
                     HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(i) * HotseatInternal::kPlayerFileSize;
        const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
        if (pf->playerName[0] != '\0')
            currentNames.emplace_back(pf->playerName);
    }
    if (currentNames.empty())
        return;

    auto it = s_squadSnapshots.find(teamId);
    if (it == s_squadSnapshots.end()) {
        s_squadSnapshots[teamId] = std::move(currentNames);
        return;
    }

    const auto& prevNames = it->second;
    std::unordered_set<std::string> prevSet(prevNames.begin(), prevNames.end());
    std::unordered_set<std::string> currSet(currentNames.begin(), currentNames.end());

    std::vector<std::string> added;
    std::vector<std::string> removed;
    for (const auto& name : currentNames) {
        if (prevSet.find(name) == prevSet.end())
            added.push_back(name);
    }
    for (const auto& name : prevNames) {
        if (currSet.find(name) == currSet.end())
            removed.push_back(name);
    }

    s_squadSnapshots[teamId] = std::move(currentNames);

    // Ignore bulk resets / season-init squad rebuilds
    if (added.empty() && removed.empty())
        return;
    if (added.size() > 2 || removed.size() > 2)
        return;

    std::string prompt;
    if (!added.empty()) {
        prompt = clubName + " just signed " + added.front() + ".";
        if (!removed.empty())
            prompt += " " + removed.front() + " departed.";
        prompt += " Give a punchy 1-2 sentence 1995 transfer desk reaction from the player or rival manager (max 115 chars).";
    } else {
        prompt = removed.front() + " just left " + clubName + ". "
                 "Give a punchy 1-2 sentence 1995 transfer desk reaction from the manager or dressing room (max 115 chars).";
    }

    uint64_t reqId = enqueueLlmRequest(
        "You are a 1995 football transfer reporter and dressing room insider. Plain ASCII, max 115 chars, no markdown.",
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
    std::string prompt = "Pre-match briefing:\n" + formatLlmMatchInfoBase(ctx);
    prompt += "\nGive a 2-sentence assistant manager scout & pitch briefing (max 150 chars total). Plain ASCII, no markdown.";

    s_preMatchRequestId = enqueueLlmRequest(
        "You are a 1995 football assistant manager giving a concise pre-match tactical & pitch briefing. Plain ASCII, no markdown.",
        prompt);
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

    const TeamGeneralInfo *team1Info = nullptr;
    const TeamGeneralInfo *team2Info = nullptr;
    resolveTeam1AndTeam2(team1Info, team2Info);

    const char *t1 = swos.topTeamInGame.teamName[0] ? swos.topTeamInGame.teamName : "HOME";
    const char *t2 = swos.bottomTeamInGame.teamName[0] ? swos.bottomTeamInGame.teamName : "AWAY";
    int g1 = swos.statsTeam1Goals;
    int g2 = swos.statsTeam2Goals;
    bool topHuman = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0);
    bool botHuman = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0);

    bool isGoalEvent = (std::strstr(eventLabel, "GOAL") != nullptr);
    const TeamGeneralInfo *actingPtr = isGoalEvent
        ? static_cast<const TeamGeneralInfo *>(swos.lastTeamScored.asAligned())
        : static_cast<const TeamGeneralInfo *>(swos.lastTeamPlayedBeforeBreak.asAligned());

    const char *actingTeam = nullptr;
    const char *otherTeam = nullptr;
    if ((isGoalEvent && swos.lastTeamScoredNumber == 1) || actingPtr == team1Info) {
        actingTeam = t1;
        otherTeam = t2;
    } else if ((isGoalEvent && swos.lastTeamScoredNumber == 2) || actingPtr == team2Info) {
        actingTeam = t2;
        otherTeam = t1;
    }

    std::string prompt = buildMatchPerspectiveContext(t1, t2, g1, g2, topHuman, botHuman);
    prompt += "\nCurrent score: " + std::string(t1) + " " + std::to_string(g1) + "-" + std::to_string(g2) + " " + t2 + ".";
    prompt += "\nEvent: " + std::string(eventLabel) + ".";
    if (actingTeam) {
        prompt += " Team that just acted/scored: " + std::string(actingTeam) +
                  " (NOT " + otherTeam + "). Credit " + actingTeam + " accurately.";
    }
    prompt += " Give ONE punchy 1995 TV pundit line (max 76 chars, plain ASCII, no quotes).";

    s_punditRequestId = enqueueLlmRequest(
        "You are a 1995 live football TV pundit. Always credit the exact scoring/acting team accurately. One short sentence, max 76 chars, plain ASCII.",
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
        s_punditLines = wrapTextForSwosPrompt(reply, 48, 2);
        if (!s_punditLines.empty())
            s_punditDisplayFrames = 270; // ~4.5 seconds on screen
    }
}

void drawLivePunditTicker()
{
    if (s_punditDisplayFrames <= 0 || s_punditLines.empty())
        return;
    if (showingPostGameStats() || showingUserRequestedStats())
        return;

    constexpr int kLineH = 8;
    int numLines = static_cast<int>(s_punditLines.size());
    float barH = static_cast<float>(numLines * kLineH + 6);
    float barY = static_cast<float>(kVgaHeight) - barH - 2.0f;
    float barW = static_cast<float>(kVgaWidth + 2 * getGameScreenOffsetX());

    drawDarkRectangle({ 0.0f, barY, barW, barH }, false);
    for (int i = 0; i < numLines; ++i) {
        int y = static_cast<int>(barY) + 3 + i * kLineH;
        drawTextCentered(kVgaWidth / 2, y, s_punditLines[i].c_str(), kVgaWidth - 12, kWhiteText2, false);
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
    if (waitForLlmWithIndicator(reqId, cfg.timeoutMs, isMarket ? "TRANSFER RUMOURS" : "DRESSING ROOM", reply)) {
        showWrappedSwosPrompt(isMarket ? "TRANSFER WHISPER" : "DRESSING ROOM", reply);
    }
}
