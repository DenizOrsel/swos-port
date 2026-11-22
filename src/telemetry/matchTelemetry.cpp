#include "matchTelemetry.h"
#include "file.h"
#include "gameTime.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "llmClient.h"
#include "log.h"
#include "pitch.h"
#include "pitchConstants.h"
#include "result.h"
#include "stats.h"
#include "swos.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>

namespace {

struct TouchRecord {
    int teamNum = 0;       // 1 = team1, 2 = team2
    int playerOrd = 0;     // 1-based ordinal
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    uint32_t frameTick = 0;
};

uint32_t s_frameCounter = 0;
uint32_t s_zoneSamplesTeam1Attack = 0;
uint32_t s_zoneSamplesMidfield = 0;
uint32_t s_zoneSamplesTeam2Attack = 0;

TouchRecord s_lastTouch;
TouchRecord s_prevTouch;
std::deque<MatchTelemetryEvent> s_recentEvents;
std::vector<MatchTelemetryEvent> s_allMatchEvents;

const char *telemetryPitchName(int pitchType)
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

const char *telemetryCompetitionName()
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

void resolveCanonicalTeams(const TeamGeneralInfo*& outTeam1, const TeamGeneralInfo*& outTeam2)
{
    if (swos.topTeamData.inGameTeamPtr.asAligned() == &swos.topTeamInGame) {
        outTeam1 = &swos.topTeamData;
        outTeam2 = &swos.bottomTeamData;
    } else {
        outTeam1 = &swos.bottomTeamData;
        outTeam2 = &swos.topTeamData;
    }
}

std::string cleanCoachName(const TeamFile *tf)
{
    if (!tf || tf->coachName[0] == '\0')
        return {};
    std::string out;
    for (size_t i = 0; i < sizeof(tf->coachName) && tf->coachName[i] != '\0'; ++i) {
        unsigned char ch = tf->coachName[i];
        if (ch >= 0x20 && ch <= 0x7E)
            out.push_back(static_cast<char>(ch));
    }
    return out;
}

std::string collectKeyPlayers(const TeamGame& team)
{
    std::string out;
    for (int i = 1; i < 11; ++i) {
        if (team.players[i].shortName[0] == '\0')
            continue;
        if (!out.empty())
            out += ", ";
        out += team.players[i].shortName;
        if (out.size() > 54)
            break;
    }
    return out;
}

std::string extractCareerRecordSummary()
{
    if (swos.g_gameType != kGameTypeCareer)
        return {};

    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        const auto& p = mgr.activePlayer();
        if (p.gamesPlayed > 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s season record: %uP %uW-%uD-%uL (%u GF, %u GA)",
                p.teamName.c_str(), p.gamesPlayed, p.numWins, p.numDrawn, p.numLost, p.goalsScored, p.goalsTaken);
            return buf;
        }
        return {};
    }

    // Single-player career stats block in VM memory (CareerOffsets::kDsegD6C88: [0]=P, [1]=W, [2]=D, [3]=L, [4]=GF, [5]=GA)
    const auto *statsWords = reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C88]);
    uint16_t played = statsWords[0];
    if (played > 0 && played < 200) {
        const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
        const char *club = hdr->teamName[0] ? hdr->teamName : "Club";
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s season record: %uP %uW-%uD-%uL (%u GF, %u GA)",
            club, played, statsWords[1], statsWords[2], statsWords[3], statsWords[4], statsWords[5]);
        return buf;
    }
    return {};
}

void sampleBallTouch()
{
    const TeamGeneralInfo *team = swos.lastTeamPlayed.asAligned();
    const Sprite *carrier = team ? team->controlledPlayer.asAligned() : nullptr;
    if (!carrier || carrier->playerOrdinal < 1 || carrier->playerOrdinal > kNumPlayersInTeam)
        return;

    int teamNum = carrier->teamNumber;
    if (teamNum != 1 && teamNum != 2)
        return;

    // Only register a touch when the ball is close to the player sprite
    float dx = swos.ballSprite.x.asFloat() - carrier->x.asFloat();
    float dy = swos.ballSprite.y.asFloat() - carrier->y.asFloat();
    if (dx * dx + dy * dy > 26.0f * 26.0f)
        return;

    const TeamGame& tg = (teamNum == 1) ? swos.topTeamInGame : swos.bottomTeamInGame;
    const char *pName = tg.players[carrier->playerOrdinal - 1].shortName;
    if (!pName || pName[0] == '\0')
        return;

    if (s_lastTouch.teamNum != teamNum || s_lastTouch.playerOrd != carrier->playerOrdinal) {
        s_prevTouch = s_lastTouch;
    }
    s_lastTouch.teamNum = teamNum;
    s_lastTouch.playerOrd = carrier->playerOrdinal;
    s_lastTouch.name = pName;
    s_lastTouch.x = swos.ballSprite.x.asFloat();
    s_lastTouch.y = swos.ballSprite.y.asFloat();
    s_lastTouch.frameTick = s_frameCounter;
}

void sampleTerritoryThirds()
{
    if (swos.gameStatePl != GameState::kInProgress)
        return;

    float ballY = swos.ballSprite.y.asFloat();
    // Pitch playable Y spans roughly [129 .. 769]
    float normY = std::clamp((ballY - 129.0f) / (769.0f - 129.0f), 0.0f, 1.0f);

    // Determine which side Team 1 is attacking (top goal = normY near 0, bottom goal = normY near 1)
    bool team1AttackingTop = (swos.topTeamData.inGameTeamPtr.asAligned() == &swos.topTeamInGame)
        ? false : true; // topTeamData defends top goal and attacks bottom goal

    if (normY < 0.33f) {
        if (team1AttackingTop) ++s_zoneSamplesTeam1Attack;
        else ++s_zoneSamplesTeam2Attack;
    } else if (normY > 0.67f) {
        if (team1AttackingTop) ++s_zoneSamplesTeam2Attack;
        else ++s_zoneSamplesTeam1Attack;
    } else {
        ++s_zoneSamplesMidfield;
    }
}

std::string describeGoalMechanics(const Sprite *scorerSprite, const char *eventType)
{
    std::string detail;
    if (swos.goalTypeScored == static_cast<int>(GoalType::kOwnGoal)) {
        detail = "own goal";
    } else if (swos.penalty || (eventType && std::strstr(eventType, "PENALTY") != nullptr)) {
        detail = "penalty kick";
    } else if (scorerSprite && (scorerSprite->state == PlayerState::kStaticHeader ||
                                scorerSprite->state == PlayerState::kJumpHeader)) {
        detail = "header";
    } else {
        detail = "footed shot (NOT a header)";
    }

    // Distance & height classification from ball/touch position
    float bx = swos.ballSprite.x.asFloat();
    float by = swos.ballSprite.y.asFloat();
    float bz = swos.ballSprite.z.asFloat();
    float distToTopGoal = std::hypot(bx - 336.0f, by - 129.0f);
    float distToBotGoal = std::hypot(bx - 336.0f, by - 769.0f);
    float shotDist = (s_lastTouch.frameTick > 0 && s_frameCounter - s_lastTouch.frameTick < 120)
        ? std::min(std::hypot(s_lastTouch.x - 336.0f, s_lastTouch.y - 129.0f),
                   std::hypot(s_lastTouch.x - 336.0f, s_lastTouch.y - 769.0f))
        : std::min(distToTopGoal, distToBotGoal);

    if (detail.find("penalty") == std::string::npos && detail.find("own") == std::string::npos) {
        if (shotDist < 55.0f)
            detail += ", close-range finish";
        else if (shotDist < 125.0f)
            detail += ", inside the box";
        else
            detail += ", long-range strike from outside the box";

        if (bz > 11.0f)
            detail += ", high finish";
    }

    // Assist attribution from prior teammate touch within ~5 seconds (300 frames)
    if (scorerSprite && swos.goalTypeScored != static_cast<int>(GoalType::kOwnGoal) && !swos.penalty) {
        if (s_prevTouch.teamNum == scorerSprite->teamNumber &&
            s_prevTouch.playerOrd != scorerSprite->playerOrdinal &&
            !s_prevTouch.name.empty() &&
            s_frameCounter >= s_prevTouch.frameTick &&
            (s_frameCounter - s_prevTouch.frameTick) <= 300) {
            detail += ", assisted by " + s_prevTouch.name;
        }
    }
    return detail;
}

} // namespace

std::string computeTimePressureTag(int minute, int goals1, int goals2, bool playingPenalties)
{
    if (playingPenalties)
        return "PENALTY SHOOTOUT - EXTREME TENSION";
    int diff = std::abs(goals1 - goals2);
    if (minute >= 85) {
        if (diff == 0)
            return "FINAL MINUTES (TIED - FANS BEGGING FOR A LATE WINNER)";
        if (diff == 1)
            return "FINAL MINUTES (1-GOAL MARGIN - LEADING FANS BEGGING REF TO BLOW THE WHISTLE, TRAILING FANS DESPERATE)";
        return "FINAL MINUTES (COMFORTABLE LEAD - CELEBRATING OR HEADING FOR THE EXITS)";
    }
    if (minute >= 75) {
        return (diff <= 1) ? "CLOSING STAGES (TENSE FINISH APPROACHING)" : "CLOSING STAGES";
    }
    if (minute >= 40 && minute <= 45)
        return "APPROACHING HALFTIME";
    if (minute <= 12)
        return "EARLY EXCHANGES";
    return "OPEN PLAY";
}

void resetMatchTelemetry()
{
    s_frameCounter = 0;
    s_zoneSamplesTeam1Attack = 0;
    s_zoneSamplesMidfield = 0;
    s_zoneSamplesTeam2Attack = 0;
    s_lastTouch = {};
    s_prevTouch = {};
    s_recentEvents.clear();
    s_allMatchEvents.clear();
}

void updateMatchTelemetry()
{
    ++s_frameCounter;
    if (swos.g_trainingGame)
        return;

    sampleBallTouch();
    if ((s_frameCounter % 30) == 0)
        sampleTerritoryThirds();
}

void recordTelemetryMatchEvent(const char *eventType, bool isGoalEvent /* = false */)
{
    if (!eventType || swos.g_trainingGame)
        return;

    const TeamGeneralInfo *team1Info = nullptr;
    const TeamGeneralInfo *team2Info = nullptr;
    resolveCanonicalTeams(team1Info, team2Info);

    const char *t1 = swos.topTeamInGame.teamName[0] ? swos.topTeamInGame.teamName : "HOME";
    const char *t2 = swos.bottomTeamInGame.teamName[0] ? swos.bottomTeamInGame.teamName : "AWAY";

    const TeamGeneralInfo *actingPtr = isGoalEvent
        ? static_cast<const TeamGeneralInfo *>(swos.lastTeamScored.asAligned())
        : static_cast<const TeamGeneralInfo *>(swos.lastTeamPlayedBeforeBreak.asAligned());

    std::string teamName;
    if ((isGoalEvent && swos.lastTeamScoredNumber == 1) || actingPtr == team1Info)
        teamName = t1;
    else if ((isGoalEvent && swos.lastTeamScoredNumber == 2) || actingPtr == team2Info)
        teamName = t2;

    std::string playerName;
    std::string detail;
    if (isGoalEvent) {
        const Sprite *scorerSprite = swos.currentScorer.asAligned();
        if (scorerSprite && scorerSprite->playerOrdinal >= 1 && scorerSprite->playerOrdinal <= kNumPlayersInTeam) {
            const TeamGame& tg = (scorerSprite->teamNumber == 1) ? swos.topTeamInGame : swos.bottomTeamInGame;
            if (tg.players[scorerSprite->playerOrdinal - 1].shortName[0] != '\0')
                playerName = tg.players[scorerSprite->playerOrdinal - 1].shortName;
        }
        detail = describeGoalMechanics(scorerSprite, eventType);
    } else if (!s_lastTouch.name.empty() && (s_frameCounter - s_lastTouch.frameTick) < 90) {
        playerName = s_lastTouch.name;
    }

    MatchTelemetryEvent ev;
    ev.minute = static_cast<int>(gameTimeInMinutes());
    ev.type = eventType;
    ev.team = std::move(teamName);
    ev.player = std::move(playerName);
    ev.detail = std::move(detail);

    s_recentEvents.push_back(ev);
    if (s_recentEvents.size() > 5)
        s_recentEvents.pop_front();
    s_allMatchEvents.push_back(std::move(ev));
}

MatchTelemetrySnapshot captureMatchTelemetrySnapshot()
{
    MatchTelemetrySnapshot snap;
    snap.minute = static_cast<int>(gameTimeInMinutes());
    snap.half = swos.halfNumber ? static_cast<int>(swos.halfNumber) : 1;
    snap.playingPenalties = (swos.playingPenalties != 0);

    snap.competition = telemetryCompetitionName();
    if (isPitchTypeResolved())
        snap.pitchCondition = telemetryPitchName(getPitchType());

    const TeamGeneralInfo *team1Info = nullptr;
    const TeamGeneralInfo *team2Info = nullptr;
    resolveCanonicalTeams(team1Info, team2Info);

    snap.team1 = swos.topTeamInGame.teamName[0] ? swos.topTeamInGame.teamName : "HOME";
    snap.team2 = swos.bottomTeamInGame.teamName[0] ? swos.bottomTeamInGame.teamName : "AWAY";
    snap.team1Human = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0);
    snap.team2Human = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0);

    const TeamFile *tf1 = swos.playMatchTeam1Ptr.asAligned();
    const TeamFile *tf2 = swos.playMatchTeam2Ptr.asAligned();
    snap.coach1 = cleanCoachName(tf1);
    snap.coach2 = cleanCoachName(tf2);
    if (tf1 && !snap.team1Human)
        snap.team1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
    if (tf2 && !snap.team2Human)
        snap.team2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);

    snap.players1 = collectKeyPlayers(swos.topTeamInGame);
    snap.players2 = collectKeyPlayers(swos.bottomTeamInGame);

    snap.goals1 = swos.statsTeam1Goals;
    snap.goals2 = swos.statsTeam2Goals;
    snap.penGoals1 = swos.team1PenaltyGoals;
    snap.penGoals2 = swos.team2PenaltyGoals;
    snap.timePressureTag = computeTimePressureTag(snap.minute, snap.goals1, snap.goals2, snap.playingPenalties);

    const auto stats = getStats();
    snap.shots1 = stats.team1.goalAttempts;
    snap.shots2 = stats.team2.goalAttempts;
    snap.onTarget1 = stats.team1.onTarget;
    snap.onTarget2 = stats.team2.onTarget;
    snap.possession1 = stats.team1.ballPossession;
    snap.possession2 = stats.team2.ballPossession;
    snap.corners1 = stats.team1.cornersWon;
    snap.corners2 = stats.team2.cornersWon;
    snap.fouls1 = stats.team1.foulsConceded;
    snap.fouls2 = stats.team2.foulsConceded;

    uint32_t totalZones = s_zoneSamplesTeam1Attack + s_zoneSamplesMidfield + s_zoneSamplesTeam2Attack;
    if (totalZones > 0) {
        snap.team1AttackZonePct = static_cast<int>((s_zoneSamplesTeam1Attack * 100u) / totalZones);
        snap.team2AttackZonePct = static_cast<int>((s_zoneSamplesTeam2Attack * 100u) / totalZones);
        snap.midfieldZonePct = std::max(0, 100 - snap.team1AttackZonePct - snap.team2AttackZonePct);
    }

    snap.careerRecordSummary = extractCareerRecordSummary();
    snap.recentEvents.assign(s_recentEvents.begin(), s_recentEvents.end());
    return snap;
}

std::string formatTelemetryPromptBlock(const MatchTelemetrySnapshot& snap)
{
    std::string out = "Clock: " + std::to_string(snap.minute) + "' (Half " + std::to_string(snap.half) +
                      ", Phase: " + snap.timePressureTag + ").";
    out += "\nScore: " + snap.team1 + (snap.team1Human ? " [HUMAN]" : " [CPU]") + " " +
           std::to_string(snap.goals1) + "-" + std::to_string(snap.goals2) + " " +
           snap.team2 + (snap.team2Human ? " [HUMAN]" : " [CPU]");
    if (snap.playingPenalties)
        out += " (PENS " + std::to_string(snap.penGoals1) + "-" + std::to_string(snap.penGoals2) + ")";
    out += "\nCompetition: " + snap.competition;
    if (!snap.pitchCondition.empty())
        out += ", Pitch: " + snap.pitchCondition;
    out += "\nStats: Shots " + std::to_string(snap.shots1) + "-" + std::to_string(snap.shots2) +
           " (On target " + std::to_string(snap.onTarget1) + "-" + std::to_string(snap.onTarget2) + ")" +
           ", Possession " + std::to_string(snap.possession1) + "%-" + std::to_string(snap.possession2) + "%" +
           ", Territory (" + snap.team1 + " atk " + std::to_string(snap.team1AttackZonePct) + "%, mid " +
           std::to_string(snap.midfieldZonePct) + "%, " + snap.team2 + " atk " +
           std::to_string(snap.team2AttackZonePct) + "%).";
    if (!snap.coach1.empty() || !snap.coach2.empty()) {
        out += "\nManagers: " + snap.team1 + " (" + (snap.coach1.empty() ? "Unknown" : snap.coach1) + ") vs " +
               snap.team2 + " (" + (snap.coach2.empty() ? "Unknown" : snap.coach2) + ").";
    }
    if (!snap.players1.empty())
        out += "\n" + snap.team1 + " XI: " + snap.players1;
    if (!snap.players2.empty())
        out += "\n" + snap.team2 + " XI: " + snap.players2;
    if (!snap.careerRecordSummary.empty())
        out += "\n" + snap.careerRecordSummary;

    if (!snap.recentEvents.empty()) {
        out += "\nRecent events:";
        for (const auto& ev : snap.recentEvents) {
            out += "\n- " + std::to_string(ev.minute) + "' " + ev.type;
            if (!ev.team.empty()) out += " (" + ev.team + ")";
            if (!ev.player.empty()) out += " by " + ev.player;
            if (!ev.detail.empty()) out += " [" + ev.detail + "]";
        }
    }
    return out;
}

void finalizeMatchTelemetry()
{
    const auto& cfg = getLlmConfig();
    if (!cfg.telemetry.enabled || !cfg.telemetry.exportMatchJson || swos.g_trainingGame)
        return;

    std::string dir = pathInRootDir("telemetry");
    if (!dirExists(dir.c_str()))
        createDir(dir.c_str());

    std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &now);
    char fname[64];
    std::snprintf(fname, sizeof(fname), "match-%04d%02d%02d-%02d%02d%02d.json",
        tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
        tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);

    std::string fullPath = joinPaths(dir.c_str(), fname);
    MatchTelemetrySnapshot snap = captureMatchTelemetrySnapshot();

    std::ofstream f(fullPath, std::ios::out | std::ios::trunc);
    if (!f)
        return;

    f << "{\n";
    f << "  \"competition\": \"" << escapeLlmJson(snap.competition) << "\",\n";
    f << "  \"pitch\": \"" << escapeLlmJson(snap.pitchCondition) << "\",\n";
    f << "  \"minute\": " << snap.minute << ",\n";
    f << "  \"team1\": \"" << escapeLlmJson(snap.team1) << "\",\n";
    f << "  \"team2\": \"" << escapeLlmJson(snap.team2) << "\",\n";
    f << "  \"goals1\": " << snap.goals1 << ",\n";
    f << "  \"goals2\": " << snap.goals2 << ",\n";
    f << "  \"shots1\": " << snap.shots1 << ",\n";
    f << "  \"shots2\": " << snap.shots2 << ",\n";
    f << "  \"possession1\": " << snap.possession1 << ",\n";
    f << "  \"possession2\": " << snap.possession2 << ",\n";
    f << "  \"territory\": { \"team1AtkPct\": " << snap.team1AttackZonePct
      << ", \"midPct\": " << snap.midfieldZonePct
      << ", \"team2AtkPct\": " << snap.team2AttackZonePct << " },\n";
    f << "  \"events\": [\n";
    for (size_t i = 0; i < s_allMatchEvents.size(); ++i) {
        const auto& ev = s_allMatchEvents[i];
        f << "    { \"minute\": " << ev.minute
          << ", \"type\": \"" << escapeLlmJson(ev.type)
          << "\", \"team\": \"" << escapeLlmJson(ev.team)
          << "\", \"player\": \"" << escapeLlmJson(ev.player)
          << "\", \"detail\": \"" << escapeLlmJson(ev.detail) << "\" }"
          << (i + 1 < s_allMatchEvents.size() ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
    logInfo("Exported match telemetry to %s", fullPath.c_str());
}
