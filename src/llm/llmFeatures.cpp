#include "llmFeatures.h"
#include "compactFont.h"
#include "llmClient.h"
#include "llmReportMenu.h"
#include "matchTelemetry.h"
#include "sponsorChallenge.h"
#include "pressCharisma.h"
#include "supporterStream.h"
#include "tabloidNewspaper.h"
#include "achievements.h"
#include "dressingRoomDilemma.h"
#include "stadiumFacilities.h"
#include "transferImpact.h"
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
#include <ctime>
#include <random>
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
int s_urundulAmbientFrames = 1500;
uint32_t s_punditCallCount = 0;
bool s_pendingPunditIsUrundul = false;
std::vector<std::string> s_punditLines;

bool isTurkishLlmLanguage(const std::string& lang)
{
    std::string up = sanitizeCompactMixedCase(lang);
    for (char& c : up)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return up == "TURKISH" || up == "TR" || up == "TURKCE" || up.find("TURK") != std::string::npos;
}
struct SquadPlayerSnapshot {
    std::string name;
    std::string position;
    std::string marketValue;
    int32_t valueGbp = 0;
    uint8_t priceTier = 0;
    int totalSkills = 0;
    int maxSingleSkill = 0;
    int shirtNumber = 0;
    int careerSlot = -1;
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

int32_t computeExactCareerPlayerMarketValueGbp(uint16_t playerIdx, const PlayerFile *pf, std::string& outFormatted)
{
    if (!pf) {
        outFormatted = "GBP 0";
        return 0;
    }
    if (pf->someFlag == 0x9A) {
        outFormatted = "Reserve";
        return 0;
    }
    if ((pf->positionAndFace & 2u) != 0) {
        outFormatted = "On Loan";
        return 0;
    }

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
    outFormatted = formatThousandsPounds(thousands);
    return static_cast<int32_t>(thousands) * 1000;
}

std::string formatExactCareerPlayerMarketValue(uint16_t playerIdx, const PlayerFile *pf)
{
    std::string formatted;
    computeExactCareerPlayerMarketValueGbp(playerIdx, pf, formatted);
    return formatted;
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
    std::string mixed = sanitizeCompactMixedCase(in);
    std::string out;
    out.reserve(mixed.size());
    for (char c : mixed) {
        if (c == '#')
            continue;
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
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
    if (numPlayers == 0 || numPlayers > kCurrentMatchPlayersSize)
        return {};

    const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];
    bool seenSlot[kCurrentMatchPlayersSize] = {};
    std::vector<std::pair<std::string, std::string>> candidates;
    // Pick from the active Playing XI (first 11 slots in currentMatchPlayers)
    uint16_t xiLimit = std::min<uint16_t>(numPlayers, 11);
    for (uint16_t i = 0; i < xiLimit; ++i) {
        uint8_t slot = matchPlayers[i];
        if (slot >= kCurrentMatchPlayersSize || seenSlot[slot])
            continue;
        seenSlot[slot] = true;
        size_t ofs = CareerOffsets::kCareerTeam +
                     HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
        if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
            continue;
        const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
        std::string pName = cleanSwosPlayerName(pf->playerName, sizeof(pf->playerName));
        if (pName.empty() || pName.rfind("*ERR*", 0) == 0)
            continue;
        candidates.emplace_back(std::move(pName), playerPositionName(pf->positionAndFace));
    }
    if (candidates.empty())
        return {};

    static std::mt19937 s_voiceRng(
        static_cast<uint32_t>(std::random_device{}() ^ static_cast<uint32_t>(std::time(nullptr)) ^ SDL_GetTicks()));
    static std::string s_lastVoicePlayer;

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    size_t idx = dist(s_voiceRng);
    if (candidates.size() > 1 && candidates[idx].first == s_lastVoicePlayer) {
        idx = (idx + 1 + (dist(s_voiceRng) % (candidates.size() - 1))) % candidates.size();
    }
    outPlayerName = candidates[idx].first;
    outRole = candidates[idx].second;
    s_lastVoicePlayer = outPlayerName;
    return outPlayerName;
}

void showWrappedSwosPrompt(const char *header, const std::string& body)
{
#ifdef SWOS_TEST
    (void)header;
    (void)body;
    return;
#else
    showLlmReportDialog(header, body);
#endif
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
    resetTabloidMatchPhoto();
}

static TabloidKitColors extractKitFromTeamGame(const TeamGame& tg)
{
    TabloidKitColors k;
    k.shirtCol = static_cast<uint8_t>(tg.prShirtCol & 0x0Fu);
    k.stripesCol = static_cast<uint8_t>(tg.prStripesCol & 0x0Fu);
    k.shortsCol = static_cast<uint8_t>(tg.prShortsCol & 0x0Fu);
    k.socksCol = static_cast<uint8_t>(tg.prSocksCol & 0x0Fu);
    return k;
}

static TabloidKitColors extractKitFromTeamFile(const TeamFile *tf)
{
    TabloidKitColors k;
    if (!tf)
        return k;
    k.shirtCol = static_cast<uint8_t>(tf->prBasicColor & 0x0Fu);
    k.stripesCol = static_cast<uint8_t>(tf->prStripesColor & 0x0Fu);
    k.shortsCol = static_cast<uint8_t>(tf->prShortsColor & 0x0Fu);
    k.socksCol = static_cast<uint8_t>(tf->prSocksColor & 0x0Fu);
    return k;
}

static TabloidNewspaperEdition buildCurrentPlayedMatchTabloidMeta()
{
    LlmMatchContext ctx = buildLlmMatchContext();
    const auto stats = getStats();
    int totalPoss = stats.team1.ballPossession + stats.team2.ballPossession;
    int poss1Pct = totalPoss > 0 ? (100 * stats.team1.ballPossession) / totalPoss : 50;
    int poss2Pct = 100 - poss1Pct;

    TabloidNewspaperEdition meta;
    meta.isPlayedMatch = true;
    meta.competition = ctx.competition;
    meta.team1 = ctx.team1;
    meta.team2 = ctx.team2;
    meta.goals1 = swos.statsTeam1Goals;
    meta.goals2 = swos.statsTeam2Goals;
    meta.playingPenalties = (swos.playingPenalties != 0);
    meta.penGoals1 = swos.team1PenaltyGoals;
    meta.penGoals2 = swos.team2PenaltyGoals;
    meta.team1Human = ctx.team1Human;
    meta.team2Human = ctx.team2Human;
    meta.scorers1 = getMatchScorersSummary(1);
    meta.scorers2 = getMatchScorersSummary(2);
    meta.statsLine = "SHOTS " + std::to_string(stats.team1.goalAttempts) + "-" + std::to_string(stats.team2.goalAttempts) +
                     " | POSS " + std::to_string(poss1Pct) + "%-" + std::to_string(poss2Pct) + "%";
    meta.kit1 = extractKitFromTeamGame(swos.topTeamInGame);
    meta.kit2 = extractKitFromTeamGame(swos.bottomTeamInGame);

    std::string cap = getTabloidMatchPhotoCaption();
    if (!cap.empty())
        meta.photoCaption = cap;

    MatchMotmSummary m = getMatchManOfTheMatch();
    if (m.valid) {
        meta.motm.valid = true;
        meta.motm.name = m.name;
        meta.motm.team = m.team;
        meta.motm.pos = m.pos;
        meta.motm.goals = m.goals;
        meta.motm.assists = m.assists;
        meta.motm.gkSaves = m.gkSaves;
        meta.motm.rating = m.rating;
        meta.motm.kit = (m.teamNum == 1) ? meta.kit1 : meta.kit2;
    }

    // Back-Page Briefing bullets pulling live Sponsor, Stadium, & Scorer/Press notes
    std::string sponsorNote = formatLastSponsorChallengeOutcomeForNewspaper();
    if (!sponsorNote.empty()) {
        if (sponsorNote.size() > 86) sponsorNote.resize(86);
        meta.briefingBullets.push_back(sponsorNote);
    }
    std::string stadiumNote = formatLastStadiumOutcomeForNewspaper();
    if (!stadiumNote.empty()) {
        if (stadiumNote.size() > 86) stadiumNote.resize(86);
        meta.briefingBullets.push_back(stadiumNote);
    }
    if (!meta.scorers1.empty() || !meta.scorers2.empty()) {
        std::string sc = "Goals: ";
        if (!meta.scorers1.empty()) sc += meta.team1 + " (" + meta.scorers1 + ")";
        if (!meta.scorers1.empty() && !meta.scorers2.empty()) sc += "; ";
        if (!meta.scorers2.empty()) sc += meta.team2 + " (" + meta.scorers2 + ")";
        if (sc.size() > 86) sc.resize(86);
        meta.briefingBullets.push_back(sc);
    }
    std::string pressNote = formatPressContextForNewspaper();
    if (!pressNote.empty() && meta.briefingBullets.size() < 3) {
        if (pressNote.size() > 86) pressNote.resize(86);
        meta.briefingBullets.push_back(pressNote);
    }
    return meta;
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
    int totalPoss = stats.team1.ballPossession + stats.team2.ballPossession;
    int poss1Pct = totalPoss > 0 ? (100 * stats.team1.ballPossession) / totalPoss : 50;
    int poss2Pct = 100 - poss1Pct;
    prompt += ", Possession " + std::to_string(poss1Pct) + "%-" + std::to_string(poss2Pct) + "%.";
    MatchMotmSummary motm = getMatchManOfTheMatch();
    if (motm.valid) {
        char rBuf[16];
        std::snprintf(rBuf, sizeof(rBuf), "%.1f", motm.rating);
        prompt += "\nMan of the Match: " + motm.name + " (" + motm.team + ", rating " + std::string(rBuf) + "/10).";
    }
    std::string photoCap = getTabloidMatchPhotoCaption();
    if (!photoCap.empty())
        prompt += "\nAction photograph moment: " + photoCap;
    std::string sponsorNote = formatLastSponsorChallengeOutcomeForNewspaper();
    if (!sponsorNote.empty())
        prompt += "\n" + sponsorNote;
    std::string stadiumNote = formatLastStadiumOutcomeForNewspaper();
    if (!stadiumNote.empty())
        prompt += "\nStadium/Form: " + stadiumNote;
    std::string pressNote = formatPressContextForNewspaper();
    if (!pressNote.empty())
        prompt += "\n" + pressNote;
    prompt += "\nWrite a punchy 1996/97 British tabloid sports back page strictly in these 5 tagged lines:\n"
              "HEADLINE: <screaming 3-7 word tabloid banner headline referencing the actual result/hero>\n"
              "SUBHEAD: <1 crisp sentence sub-headline under 90 chars>\n"
              "CAPTION: <1 short 8-14 word newspaper caption describing the action photo moment above>\n"
              "STORY: <3-4 vivid sentences summarizing the match using real scorers, shots, possession, and MOTM. Do NOT invent players, goals, or weather.>\n"
              "CHAIRMAN: <1-2 expressive sentences in character from the human club's chairman reacting strictly to the actual WIN/LOSS/DRAW.>\n"
              "Plain text only, no markdown, no bullet points.";

    const std::string sys = "You are a witty, atmospheric 1996/97 British football back-page tabloid editor and expressive club chairman. "
                            "Write punchy, vivid sports tabloid copy grounded strictly in the provided match facts. "
                            "Never confuse who won or lost. Never invent weather or scorers not given. "
                            "Follow the HEADLINE / SUBHEAD / CAPTION / STORY / CHAIRMAN tag format strictly.";
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
    waitForLlmWithIndicator(s_postMatchRequestId, cfg.timeoutMs, "PRINTING DAILY ECHO", reply);
    s_postMatchRequestId = 0;

    TabloidNewspaperEdition meta = buildCurrentPlayedMatchTabloidMeta();
    TabloidNewspaperEdition edition = parseTabloidEditionReply(reply, meta);
    showTabloidNewspaperModal(edition);
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
    onCareerMatchFinishedDressingRoomDilemma(topGoals, botGoals);
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

    // Award Global Achievements for Career "View Result" fast-sim matches even when LLM is disabled
    if (topHuman || botHuman) {
        bool evalTeam1 = (topHuman && botHuman) ? !(g2 > g1) : topHuman;
        int hGoals = evalTeam1 ? g1 : g2;
        int oGoals = evalTeam1 ? g2 : g1;
        int div1 = (t1File->league <= 3) ? static_cast<int>(t1File->league) : -1;
        int div2 = (t2File->league <= 3) ? static_cast<int>(t2File->league) : -1;

        AchievementMatchSummary ms;
        ms.humanParticipated = true;
        ms.humanWon = (hGoals > oGoals);
        ms.isFriendly = (swos.g_gameType == kGameTypeNoGame);
        ms.isCareer = (swos.g_gameType == kGameTypeCareer);
        ms.isCompetitive = !ms.isFriendly;
        ms.isCupMatch = (swos.g_gameType == kGameTypePresetCompetition ||
                         swos.g_gameType == kGameTypeDiyCompetition ||
                         (ms.isCareer && div1 >= 0 && div2 >= 0 && div1 != div2));
        ms.humanGoals = hGoals;
        ms.opponentGoals = oGoals;
        ms.humanDivisionTier = evalTeam1 ? div1 : div2;
        ms.opponentDivisionTier = evalTeam1 ? div2 : div1;
        achievementsOnMatchFinished(ms);
        onPostMatchStadiumUpdate(false);
        onViewResultSponsorContractUpdate(hGoals, oGoals);
    }

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled)
        return;

    if (cfg.features.postMatchNewspaper) {
        std::string prompt = buildMatchPerspectiveContext(t1, t2, g1, g2, topHuman, botHuman);
        prompt += "\nScoreline: " + std::string(t1) + " " + std::to_string(g1) + "-" + std::to_string(g2) + " " + t2;
        prompt += "\nCompetition: " + std::string(gameModeName()) + ".";
        std::string stadiumNote = formatLastStadiumOutcomeForNewspaper();
        if (!stadiumNote.empty())
            prompt += "\nStadium/Form: " + stadiumNote;
        prompt += "\nWrite a punchy 1996/97 British tabloid back page strictly in these 5 tagged lines:\n"
                  "HEADLINE: <screaming 3-7 word tabloid banner headline referencing the real result>\n"
                  "SUBHEAD: <1 crisp sub-headline sentence under 90 chars>\n"
                  "CAPTION: <short 8-12 word caption summarizing the clash>\n"
                  "STORY: <2-3 vivid sentences summarizing the outcome without inventing scorers or weather.>\n"
                  "CHAIRMAN: <1-2 expressive sentences from the human club's chairman reacting strictly to the actual WIN/LOSS/DRAW.>\n"
                  "Plain text only.";

        uint64_t reqId = enqueueLlmRequest(
            "You are a witty 1996/97 British football back-page tabloid editor and expressive club chairman. "
            "Never confuse who won or lost. Never invent scorers or weather. Follow the HEADLINE / SUBHEAD / CAPTION / STORY / CHAIRMAN tag format strictly.",
            prompt);
        std::string reply;
        waitForLlmWithIndicator(reqId, cfg.timeoutMs, "PRINTING DAILY ECHO", reply);

        TabloidNewspaperEdition meta;
        meta.isPlayedMatch = false;
        meta.competition = gameModeName();
        meta.team1 = t1;
        meta.team2 = t2;
        meta.goals1 = g1;
        meta.goals2 = g2;
        meta.team1Human = topHuman;
        meta.team2Human = botHuman;
        meta.kit1 = extractKitFromTeamFile(t1File);
        meta.kit2 = extractKitFromTeamFile(t2File);
        if (!stadiumNote.empty())
            meta.briefingBullets.push_back(stadiumNote);
        std::string sponsorNote = formatLastSponsorChallengeOutcomeForNewspaper();
        if (!sponsorNote.empty())
            meta.briefingBullets.push_back(sponsorNote);

        TabloidNewspaperEdition edition = parseTabloidEditionReply(reply, meta);
        showTabloidNewspaperModal(edition);
    }

    if (swos.g_gameType == kGameTypeCareer) {
        onViewResultMatchPressConference();
    }
}

static std::unordered_map<int16_t, int32_t> s_prevTransferPurchases;
static std::unordered_map<int16_t, int32_t> s_prevTransferSales;

void resetCareerLlmSnapshots()
{
    s_squadSnapshots.clear();
    s_prevTransferPurchases.clear();
    s_prevTransferSales.clear();
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

    uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    if (numPlayers == 0 || numPlayers > kCurrentMatchPlayersSize)
        return;

    const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
    int16_t teamId = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    std::string clubName = hdr->teamName[0] ? std::string(hdr->teamName) : std::string("THE CLUB");

    int32_t curPurchases = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerPurchases]);
    int32_t curSales = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerSales]);

    // SWOS keeps active career squad slot indices in currentMatchPlayers[0..careerNumPlayers-1].
    // Removed/exchanged slots stay in careerTeam overwritten with "*ERR* ", while newly signed players
    // can occupy higher slot indices >= careerNumPlayers, so we MUST index via currentMatchPlayers.
    const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];
    bool seenSlot[kCurrentMatchPlayersSize] = {};
    std::vector<SquadPlayerSnapshot> currentSquad;
    currentSquad.reserve(numPlayers);
    for (uint16_t i = 0; i < numPlayers; ++i) {
        uint8_t slot = matchPlayers[i];
        if (slot >= kCurrentMatchPlayersSize || seenSlot[slot])
            continue;
        seenSlot[slot] = true;
        size_t ofs = CareerOffsets::kCareerTeam +
                     HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
        if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
            continue;
        const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
        std::string pName = cleanSwosPlayerName(pf->playerName, sizeof(pf->playerName));
        if (!pName.empty() && pName.rfind("*ERR*", 0) != 0) {
            std::string valFmt;
            int32_t valGbp = computeExactCareerPlayerMarketValueGbp(slot, pf, valFmt);
            int sPas = pf->passing & 0x07;
            int sSht = (pf->shootingHeading >> 4) & 0x07;
            int sHed = pf->shootingHeading & 0x07;
            int sTkl = (pf->tacklingBallControl >> 4) & 0x07;
            int sCtl = pf->tacklingBallControl & 0x07;
            int sSpd = (pf->speedFinishing >> 4) & 0x07;
            int sFin = pf->speedFinishing & 0x07;
            int totSk = sPas + sSht + sHed + sTkl + sCtl + sSpd + sFin;
            int maxSk = std::max({ sPas, sSht, sHed, sTkl, sCtl, sSpd, sFin });
            currentSquad.push_back({
                std::move(pName),
                playerPositionName(pf->positionAndFace),
                std::move(valFmt),
                valGbp,
                pf->price,
                totSk,
                maxSk,
                static_cast<int>(pf->shirtNumber),
                static_cast<int>(slot)
            });
        }
    }
    if (currentSquad.empty())
        return;

    auto it = s_squadSnapshots.find(teamId);
    if (it == s_squadSnapshots.end()) {
        s_squadSnapshots[teamId] = std::move(currentSquad);
        s_prevTransferPurchases[teamId] = curPurchases;
        s_prevTransferSales[teamId] = curSales;
        return;
    }

    int32_t prevPurchases = s_prevTransferPurchases[teamId];
    int32_t prevSales = s_prevTransferSales[teamId];
    s_prevTransferPurchases[teamId] = curPurchases;
    s_prevTransferSales[teamId] = curSales;

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

    // Capture previous squad copy before overwriting snapshot so Transfer Impact knows top-2 stars
    std::vector<SquadPlayerSnapshot> prevSquadCopy = prevSquad;
    s_squadSnapshots[teamId] = std::move(currentSquad);

    // Ignore bulk resets / season-init squad rebuilds
    if (added.empty() && removed.empty())
        return;
    if (added.size() > 2 || removed.size() > 2)
        return;

    // Fire Global Achievement transfer hooks (works even when LLM Transfer Desk is disabled)
    if (!added.empty()) {
        int32_t buyDelta = std::max<int32_t>(0, curPurchases - prevPurchases);
        int32_t perBuy = buyDelta / static_cast<int32_t>(added.size());
        for (const auto& p : added)
            achievementsOnCareerTransfer(true, perBuy, p.name);
    }
    if (!removed.empty()) {
        int32_t sellDelta = std::max<int32_t>(0, curSales - prevSales);
        int32_t perSell = sellDelta / static_cast<int32_t>(removed.size());
        for (const auto& p : removed)
            achievementsOnCareerTransfer(false, perSell, p.name);
    }

    const auto& cfg = getLlmConfig();
    if (!cfg.enabled || !cfg.features.transferNegotiations)
        return;

    // Apply Transfer Market Impact V2 (Marquee 5-home-game surge + shirt sales, Hidden Gem, Star Sale backlash, Swap verdict)
    auto toImpactVec = [](const std::vector<SquadPlayerSnapshot>& src) {
        std::vector<TransferPlayerRecord> out;
        out.reserve(src.size());
        for (const auto& s : src) {
            out.push_back({ s.name, s.position, s.marketValue, s.valueGbp, s.priceTier,
                            s.totalSkills, s.maxSingleSkill, s.shirtNumber, s.careerSlot });
        }
        return out;
    };
    bool modalAlreadyShown = false;
    std::string impactSummary = handleCareerTransferImpact(
        clubName, toImpactVec(added), toImpactVec(removed), toImpactVec(prevSquadCopy), modalAlreadyShown);

    if (modalAlreadyShown)
        return;

    if (!cfg.enabled) {
        if (!impactSummary.empty())
            showWrappedSwosPrompt("TRANSFER MARKET IMPACT", impactSummary);
        return;
    }

    auto formatPlayerList = [](const std::vector<SquadPlayerSnapshot>& list) {
        std::string s;
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) s += "; ";
            s += "\"" + list[i].name + "\" (Position: " + list[i].position +
                 ", Exact In-Game Market Value: " + list[i].marketValue + ")";
        }
        return s;
    };
    auto formatNamesOnly = [](const std::vector<SquadPlayerSnapshot>& list) {
        std::string s;
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) s += ", ";
            s += "\"" + list[i].name + "\"";
        }
        return s;
    };

    const std::string addedSummary = formatPlayerList(added);
    const std::string removedSummary = formatPlayerList(removed);
    logInfo("LLM Transfer Desk detected move for %s | IN: [%s] | OUT: [%s]",
        clubName.c_str(), addedSummary.c_str(), removedSummary.c_str());

    std::string prompt;
    if (!added.empty() && !removed.empty()) {
        prompt = "PLAYER EXCHANGE TRANSFER DEAL FOR " + clubName + ":\n"
                 "- NEW SIGNING(S) JOINING " + clubName + " (INCOMING): " + addedSummary + "\n"
                 "- DEPARTING PLAYER(S) LEAVING " + clubName + " IN EXCHANGE (OUTGOING): " + removedSummary + "\n"
                 "STRICT RULES:\n"
                 "1) " + formatNamesOnly(added) + " JOINED / SIGNED FOR " + clubName + ". " +
                 formatNamesOnly(removed) + " LEFT / DEPARTED " + clubName + " as part of the swap deal. NEVER reverse who joined and who left!\n"
                 "2) Copy every player's name EXACTLY as written - NEVER guess or alter spelling.\n"
                 "3) Quote each player's EXACT position and EXACT GBP market value without converting currency or changing numbers.\n"
                 "Write a lively 2-3 sentence 1997 transfer desk bulletin covering this swap deal, with a brief quote from the new signing or manager. Plain text only.";
    } else if (!added.empty()) {
        prompt = "INCOMING TRANSFER SIGNING FOR " + clubName + ":\n"
                 "- NEW SIGNING(S) JOINING " + clubName + ": " + addedSummary + "\n"
                 "STRICT RULES:\n"
                 "1) " + formatNamesOnly(added) + " JOINED / SIGNED FOR " + clubName + ".\n"
                 "2) Copy every player's name EXACTLY as written - NEVER guess or invent a different first name or alter spelling.\n"
                 "3) Quote each player's EXACT position and EXACT GBP market value without converting currency or changing numbers.\n"
                 "Write a lively 2-3 sentence 1997 transfer desk bulletin with a brief quote from the new signing or rival manager. Plain text only.";
    } else {
        prompt = "OUTGOING TRANSFER DEPARTURE FROM " + clubName + ":\n"
                 "- DEPARTING PLAYER(S) LEAVING " + clubName + ": " + removedSummary + "\n"
                 "STRICT RULES:\n"
                 "1) " + formatNamesOnly(removed) + " LEFT / DEPARTED " + clubName + " (they did NOT sign in!).\n"
                 "2) Copy every player's name EXACTLY as written - NEVER alter spelling or invent another name.\n"
                 "3) Quote each player's EXACT position and EXACT GBP market value without converting currency or changing numbers.\n"
                 "Write a lively 2-3 sentence 1997 transfer desk bulletin with a brief reaction from the fans or dressing room. Plain text only.";
    }

    uint64_t reqId = enqueueLlmRequest(
        "You are a sharp 1997 football transfer desk journalist covering deadline moves and squad shake-ups. "
        "CRITICAL: Never confuse incoming signings with outgoing departures. Copy every player name letter-for-letter as given (do not expand initials or change spelling), and always quote the exact position and exact GBP market value provided without converting currency. Plain text only.",
        prompt);
    if (reqId == 0)
        return;

    std::string reply;
    if (waitForLlmWithIndicator(reqId, cfg.timeoutMs, "CHECKING TRANSFER DESK", reply)) {
        std::string combined = impactSummary.empty() ? reply : (impactSummary + "\n" + reply);
        showWrappedSwosPrompt("TRANSFER DESK", combined);
    } else if (!impactSummary.empty()) {
        showWrappedSwosPrompt("TRANSFER MARKET IMPACT", impactSummary);
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
    s_urundulAmbientFrames = 1350; // ~22.5s before first ambient Turkish Ömer Üründül drop-in
    s_punditCallCount = 0;
    s_pendingPunditIsUrundul = false;
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
    if (!snap.recentEvents.empty() && std::strcmp(eventLabel, "OMER URUNDUL AMBIENT") != 0) {
        const auto& latest = snap.recentEvents.back();
        if (!latest.team.empty())
            prompt += " (" + latest.team + ")";
        if (!latest.player.empty())
            prompt += " by " + latest.player;
        if (!latest.detail.empty())
            prompt += " [" + latest.detail + "]";
    }

    ++s_punditCallCount;
    bool forceUrundul = (std::strcmp(eventLabel, "OMER URUNDUL AMBIENT") == 0);
    bool useUrundul = isTurkishLlmLanguage(cfg.language) && (forceUrundul || (s_punditCallCount % 2u) == 0u);
    s_pendingPunditIsUrundul = useUrundul;

    // Gentle, non-intrusive broadcast angles for active Sponsor Challenges, Last-5 form streaks, & Stadium sellouts/construction
    std::string broadcastAngle;
    {
        const std::string sponsorNote = getLiveSponsorChallengeStreamNote();
        const std::string stadiumFormNote = getLiveStadiumAndFormStreamNote();
        bool isSponsorRelevantEvent = forceUrundul ||
            (std::strstr(eventLabel, "GOAL") != nullptr) ||
            (std::strstr(eventLabel, "CARD") != nullptr) ||
            (std::strstr(eventLabel, "HALF TIME") != nullptr) ||
            (std::strstr(eventLabel, "FULL TIME") != nullptr) ||
            (std::strstr(eventLabel, "SAVE") != nullptr);
        if (!sponsorNote.empty() && isSponsorRelevantEvent &&
            (sponsorNote.find("ALMOST THERE") != std::string::npos ||
             sponsorNote.find("LOCKED IN") != std::string::npos ||
             sponsorNote.find("BROKEN") != std::string::npos ||
             sponsorNote.find("JACKPOT") != std::string::npos ||
             (s_punditCallCount % 3u) == 0u)) {
            broadcastAngle = "Sponsor Bonus Context: " + sponsorNote;
        }
        if (!stadiumFormNote.empty() && (forceUrundul || (s_punditCallCount % 3u) == 1u ||
            std::strstr(eventLabel, "HALF TIME") != nullptr || std::strstr(eventLabel, "FULL TIME") != nullptr)) {
            if (!broadcastAngle.empty()) broadcastAngle += " | ";
            broadcastAngle += "Stadium/Form Context: " + stadiumFormNote;
        }
    }
    if (!broadcastAngle.empty()) {
        prompt += "\nOptional broadcast angle (weave in ONLY if it fits naturally alongside calling the play — never neglect the actual match event): " + broadcastAngle;
    }

    if (useUrundul) {
        prompt += "\nYou are legendary Turkish football TV commentator Omer Urundul chiming in live on the broadcast! "
                  "React to this exact match situation in 1-2 short, punchy Turkish sentences grounded strictly in the real clubs, players, score, and event above.\n"
                  "Naturally weave in ONE fitting Omer Urundul signature catchphrase or style that matches what just happened:\n"
                  "- Tactical mantras: \"Kollektif futbol\", \"Defans ve orta saha bloklari arasindaki mesafe...\", "
                  "\"Sirti donuk oynayabilen santrfor / kule forvet\", \"Gunumuz futbolunda duran toplar cok onemli\", "
                  "\"Arkadan sarkiyorlar, dikkat etmemiz lazim\", \"Iyi orta gol getirir\".\n"
                  "- Captain Obvious classics: \"Gol atan kazanir, futbol ilginc bir oyun\", \"Kazanmalari icin gol atmalari lazim\", "
                  "\"Iki takim da gol atamazsa berabere biter\", \"Eger kacirmasaydi goldu!\", "
                  "\"Kaleci olmasa gol olacakti, cok enteresan\", \"Futbol topla oynanan bir oyundur\", "
                  "\"Top dogrudan auta gitti, rakibe carpsaydi korner olurdu\", \"[Team A] kazanirsa [Team B] kaybedecek\".\n"
                  "- Iconic reactions: on red/yellow cards \"Ya niye kirmizi kart cikariyorsun ki simdi adama? Ne olacak yani, zaten mac bitecek\", "
                  "on offsides \"Ama bu cok acik ofsayit, hakemlerin bunu gormesi lazim artik\", "
                  "on big wins \"Bugun cok buyuk futbol oynadik, bundan daha buyugu oynanmaz\", "
                  "on thrilling goals \"Ooohleeeeeeyyyyy! Sahane bir gol!\".\n"
                  "Plain text only, no quotes, no speaker label.";

        s_punditRequestId = enqueueLlmRequest(
            "You are Omer Urundul, the iconic Turkish live football TV commentator famous for tactical mantras (kollektif futbol, bloklar arasi mesafe, kule forvet, duran toplar) and hilariously matter-of-fact Captain Obvious observations. "
            "Stay 100% faithful to the real match telemetry (exact teams, players, score, goalkeeper, event). "
            "Reply in natural, witty Turkish in 1-2 short sentences (max 160 chars). Plain text only, no speaker prefix.",
            prompt);
    } else {
        prompt += "\nReact with ONE spontaneous, excited 1997 TV co-commentator sentence grounded strictly in the telemetry facts above "
                  "(exact goalkeeper, exact team, exact scorer, exact finish type, exact minute). Plain text only, no quotes, no speaker prefix.";

        s_punditRequestId = enqueueLlmRequest(
            "You are an excited 1997 live football TV co-commentator like Barry Davies or Andy Gray. "
            "Always credit the exact goalkeeper, shooter, scorer, finish type, and team from the telemetry accurately. "
            "One vivid, punchy broadcast sentence. Plain text only, no speaker label.",
            prompt);
    }

    s_punditCooldownFrames = 360; // ~6 seconds cooldown between non-goal events
    s_urundulAmbientFrames = 1620; // reset ~27s ambient drop-in timer after any pundit line
}

void updateLivePunditTicker()
{
    if (s_punditCooldownFrames > 0)
        --s_punditCooldownFrames;
    if (s_punditDisplayFrames > 0)
        --s_punditDisplayFrames;

    if (s_punditRequestId != 0) {
        std::string reply;
        if (tryTakeLlmResponse(s_punditRequestId, reply)) {
            s_punditRequestId = 0;
            bool wasUrundul = s_pendingPunditIsUrundul;
            s_pendingPunditIsUrundul = false;
            if (!reply.empty()) {
                if (wasUrundul) {
                    std::string normUp = sanitizeAsciiUpper(reply);
                    if (normUp.rfind("OMER URUNDUL", 0) != 0)
                        reply = "Omer Urundul: " + reply;
                }
                pushPunditMessageToStream(reply);
            }
        }
        return;
    }

    // Periodic ambient Omer Urundul commentary drop-in (strictly when Turkish + live pundit are both enabled)
    if (swos.gameStatePl == GameState::kInProgress && !swos.g_trainingGame) {
        const auto& cfg = getLlmConfig();
        if (cfg.enabled && cfg.features.livePunditTicker && isTurkishLlmLanguage(cfg.language)) {
            if (s_urundulAmbientFrames > 0) {
                --s_urundulAmbientFrames;
            } else if (s_punditCooldownFrames <= 0) {
                triggerLivePunditEvent("OMER URUNDUL AMBIENT", false);
            }
        }
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
    if ((s_weeklyTickCounter % 8) != 0)
        return;

    onCareerWeeklyRivalTapUpEvent();
}
