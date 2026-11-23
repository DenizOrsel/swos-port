#include "matchTelemetry.h"
#include "achievements.h"
#include "file.h"
#include "gameTime.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "llmClient.h"
#include "log.h"
#include "pitch.h"
#include "pitchConstants.h"
#include "result.h"
#include "sponsorChallenge.h"
#include "pressCharisma.h"
#include "stadiumFacilities.h"
#include "stats.h"
#include "swos.h"
#include "updatePlayers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxTimelineBuckets = 24; // 0..120' in 5-minute windows

struct TouchRecord {
    int teamNum = 0;       // 1 = team1, 2 = team2
    int playerOrd = 0;     // 1-based ordinal
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float ballZ = 0.0f;
    bool wasHeader = false;
    uint32_t frameTick = 0;
};

struct ShotRecord {
    int minute = 0;
    int half = 1;
    int teamNum = 1;
    std::string team;
    int playerOrd = 0;
    int shirtNumber = 0;
    std::string player;
    std::string assistedBy;
    float rawX = 0.0f;
    float rawY = 0.0f;
    float rawZ = 0.0f;
    float normX = 0.5f;   // 0..1 left-to-right width
    float normY = 0.5f;   // 0..1 own goal -> opponent goal (always attacking toward 1.0)
    float distYards = 0.0f;
    float angleDeg = 0.0f;
    std::string bodyPart = "foot";
    std::string situation = "open_play";
    std::string outcome = "wide";
    bool outsideBox = false;
    double xg = 0.0;
    uint32_t frameTick = 0;
    int timelineBucket = 0;
};

struct PlayerMatchAnalytics {
    int goals = 0;
    int assists = 0;
    int shots = 0;
    int shotsOnTarget = 0;
    double xg = 0.0;
    int touches = 0;
    int passesCompleted = 0;
    int turnovers = 0;
    int tacklesWon = 0;
    int yellowCards = 0;
    int redCards = 0;
    int gkSaves = 0;
    double sumNormX = 0.0;
    double sumNormY = 0.0;
    uint32_t posSamples = 0;
    std::array<uint32_t, kTelemetryHeatmapCols * kTelemetryHeatmapRows> heatmap{};
};

struct TimelineBucket {
    uint32_t possTicks1 = 0;
    uint32_t possTicks2 = 0;
    int shots1 = 0;
    int shots2 = 0;
    double xg1 = 0.0;
    double xg2 = 0.0;
    int goals1 = 0;
    int goals2 = 0;
    int finalThirdTouches1 = 0;
    int finalThirdTouches2 = 0;
};

uint32_t s_frameCounter = 0;
uint32_t s_lastCornerFrame = 0;
uint32_t s_lastFreeKickFrame = 0;
uint32_t s_zoneSamplesTeam1Attack = 0;
uint32_t s_zoneSamplesMidfield = 0;
uint32_t s_zoneSamplesTeam2Attack = 0;
int s_htGoals1 = 0;
int s_htGoals2 = 0;
int s_longRangeGoals1 = 0;
int s_longRangeGoals2 = 0;
int s_headerGoals1 = 0;
int s_headerGoals2 = 0;
int s_maxDeficit1 = 0;
int s_maxDeficit2 = 0;
bool s_redBefore60_1 = false;
bool s_redBefore60_2 = false;

TouchRecord s_lastTouch;
TouchRecord s_prevTouch;
TouchRecord s_lastTouchByTeam[2];
TouchRecord s_lastStrikeByTeam[2];
std::array<std::array<TouchRecord, kNumPlayersInTeam>, 2> s_lastTouchByPlayer{};
const Sprite *s_prevLastPlayerPlayed = nullptr;
std::array<word, 2> s_prevPassKickTimer = { 0, 0 };
std::array<word, 2> s_prevPlayerHasBall = { 0, 0 };
std::deque<MatchTelemetryEvent> s_recentEvents;
std::vector<MatchTelemetryEvent> s_allMatchEvents;
std::map<std::string, int> s_matchAssists;

// Telemetry V2 spatial, shot, player, pass-network & momentum stores
std::vector<ShotRecord> s_shots;
std::array<uint32_t, kTelemetryHeatmapCols * kTelemetryHeatmapRows> s_ballHeatmap{};
std::array<std::array<uint32_t, kTelemetryHeatmapCols * kTelemetryHeatmapRows>, 2> s_teamHeatmaps{};
std::array<std::array<PlayerMatchAnalytics, kNumPlayersInTeam>, 2> s_playerAnalytics{};
std::array<std::map<std::pair<int, int>, int>, 2> s_passLinks{}; // (fromOrd, toOrd) -> count
std::array<TimelineBucket, kMaxTimelineBuckets> s_timeline{};
uint32_t s_lastShotDedupFrame = 0;

bool isTeam1AttackingTopGoal()
{
    // topTeamData defends top goal (Y=129) and attacks bottom goal (Y=769).
    // When team1 is topTeamData, team1 attacks bottom; when swapped at halftime, team1 attacks top.
    return swos.topTeamData.inGameTeamPtr.asAligned() != &swos.topTeamInGame;
}

void normalizeToTeamAttackCoords(float rawX, float rawY, int teamNum, float& outNormX, float& outNormY)
{
    float nx = std::clamp((rawX - static_cast<float>(kLeftThrowInLine)) /
                          static_cast<float>(kRightThrowInLine - kLeftThrowInLine), 0.0f, 1.0f);
    float nyTopToBot = std::clamp((rawY - static_cast<float>(kTopPitchLine)) /
                                  static_cast<float>(kBottomPitchLine - kTopPitchLine), 0.0f, 1.0f);
    bool t1AtkTop = isTeam1AttackingTopGoal();
    bool thisTeamAtkTop = (teamNum == 1) ? t1AtkTop : !t1AtkTop;
    // Canonical: normY = 0.0 at own goal line, 1.0 at opponent goal line; normX flips when attacking top so left/right stays consistent
    if (thisTeamAtkTop) {
        outNormX = 1.0f - nx;
        outNormY = 1.0f - nyTopToBot;
    } else {
        outNormX = nx;
        outNormY = nyTopToBot;
    }
}

void addHeatmapSample(std::array<uint32_t, kTelemetryHeatmapCols * kTelemetryHeatmapRows>& grid, float normX, float normY, uint32_t weight = 1)
{
    int col = std::clamp(static_cast<int>(normX * static_cast<float>(kTelemetryHeatmapCols)), 0, kTelemetryHeatmapCols - 1);
    int row = std::clamp(static_cast<int>(normY * static_cast<float>(kTelemetryHeatmapRows)), 0, kTelemetryHeatmapRows - 1);
    grid[static_cast<size_t>(row * kTelemetryHeatmapCols + col)] += weight;
}

int currentTimelineBucket()
{
    int m = static_cast<int>(gameTimeInMinutes());
    return std::clamp(m / 5, 0, kMaxTimelineBuckets - 1);
}

std::string fmtFloat(double v, int prec = 3)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << v;
    return oss.str();
}

void writeHeatmapJson(std::ofstream& f, const std::array<uint32_t, kTelemetryHeatmapCols * kTelemetryHeatmapRows>& grid)
{
    f << "[";
    for (int r = 0; r < kTelemetryHeatmapRows; ++r) {
        if (r > 0) f << ",";
        f << "[";
        for (int c = 0; c < kTelemetryHeatmapCols; ++c) {
            if (c > 0) f << ",";
            f << grid[static_cast<size_t>(r * kTelemetryHeatmapCols + c)];
        }
        f << "]";
    }
    f << "]";
}

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

const char *shortPosTag(PlayerPosition pos)
{
    switch (pos) {
    case PlayerPosition::kGoalkeeper: return "GK";
    case PlayerPosition::kRightBack: return "RB";
    case PlayerPosition::kLeftBack: return "LB";
    case PlayerPosition::kDefender: return "DEF";
    case PlayerPosition::kRightWing: return "RW";
    case PlayerPosition::kLeftWing: return "LW";
    case PlayerPosition::kMidfielder: return "MID";
    case PlayerPosition::kAttacker: return "ATT";
    default: return "SUB";
    }
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

std::string collectBenchPlayers(const TeamGame& team)
{
    std::string out;
    for (int i = kNumPlayersInLineup; i < kNumPlayersInTeam; ++i) {
        const auto& pl = team.players[i];
        if (pl.shortName[0] == '\0' || pl.wasSubstituted())
            continue;
        if (!out.empty())
            out += ", ";
        out += std::string(pl.shortName) + " (" + shortPosTag(pl.position) + ")";
    }
    return out;
}

std::string readBoundedAscii(const uint8_t *buf, size_t maxLen)
{
    std::string out;
    for (size_t i = 0; i < maxLen && buf[i] != '\0'; ++i) {
        if (buf[i] >= 0x20 && buf[i] <= 0x7E)
            out.push_back(static_cast<char>(buf[i]));
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string extractManagerCareerSummary()
{
    if (swos.g_gameType != kGameTypeCareer)
        return {};

    std::string mgrName;
    uint16_t seasonsIdx = 0;
    uint16_t played = 0, wins = 0, drawn = 0, lost = 0;
    const uint8_t *histBuf = nullptr;

    const auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        const auto& p = mgr.activePlayer();
        mgrName = p.managerName;
        if (!p.managerSurname.empty()) {
            if (!mgrName.empty()) mgrName += " ";
            mgrName += p.managerSurname;
        }
        seasonsIdx = p.currentSeasonShowing;
        played = p.gamesPlayed;
        wins = p.numWins;
        drawn = p.numDrawn;
        lost = p.numLost;
        histBuf = p.careerHistory;
    } else {
        std::string first = readBoundedAscii(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 9);
        std::string last = readBoundedAscii(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 13);
        mgrName = first;
        if (!last.empty()) {
            if (!mgrName.empty()) mgrName += " ";
            mgrName += last;
        }
        seasonsIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
        played = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGamesPlayed]);
        wins = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumWins]);
        drawn = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumDrawn]);
        lost = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumLost]);
        histBuf = &SwosVM::g_memByte[CareerOffsets::kCareerHistory];
    }

    int leagueTitles = 0;
    int cupTitles = 0;
    if (histBuf) {
        size_t maxSeasons = std::min<size_t>(static_cast<size_t>(seasonsIdx) + 1, kCareerHistoryBufferSize / kCareerHistoryEntrySize);
        for (size_t s = 0; s < maxSeasons; ++s) {
            uint16_t trophyMask = *reinterpret_cast<const uint16_t *>(&histBuf[s * kCareerHistoryEntrySize + 20]);
            if (trophyMask & (32u | 64u))
                ++leagueTitles;
            uint16_t cupBits = trophyMask & static_cast<uint16_t>(~(32u | 64u)) & 0x03FFu;
            while (cupBits) {
                cupTitles += (cupBits & 1u);
                cupBits >>= 1;
            }
        }
    }

    int totalTrophies = leagueTitles + cupTitles;
    std::string mood;
    if (played < 5 && seasonsIdx == 0) {
        mood = "brand-new boss (fans still making up their minds)";
    } else if (totalTrophies >= 3 || (played >= 10 && wins * 100 / std::max<uint16_t>(1, played) >= 62)) {
        mood = "beloved club hero (fans adore the manager)";
    } else if (played >= 8 && lost * 100 / std::max<uint16_t>(1, played) >= 48) {
        mood = "under heavy pressure (angry fans questioning tactics/job)";
    } else {
        mood = "mixed fan approval";
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "Human Manager %s: Season %u, Trophies won: %d (%d league, %d cup), Career W-D-L: %u-%u-%u (%u games) - %s",
        mgrName.empty() ? "Boss" : mgrName.c_str(),
        static_cast<unsigned>(seasonsIdx + 1),
        totalTrophies, leagueTitles, cupTitles,
        wins, drawn, lost, played, mood.c_str());
    return buf;
}

std::string extractTopScorersAndAssistsSummary()
{
    std::string out;

    // 1. In-match scorers & assists
    std::string matchPart;
    auto appendTeamScorers = [&](const TeamGame& tg) {
        for (int i = 0; i < kNumPlayersInTeam; ++i) {
            if (tg.players[i].goalsScored > 0 && tg.players[i].shortName[0] != '\0') {
                if (!matchPart.empty()) matchPart += ", ";
                matchPart += std::string(tg.players[i].shortName) + " (" + std::to_string(tg.players[i].goalsScored) + "G)";
            }
        }
    };
    appendTeamScorers(swos.topTeamInGame);
    appendTeamScorers(swos.bottomTeamInGame);
    for (const auto& kv : s_matchAssists) {
        if (!matchPart.empty()) matchPart += ", ";
        matchPart += kv.first + " (" + std::to_string(kv.second) + "A)";
    }
    if (!matchPart.empty())
        out = "Match scorers/assists: " + matchPart;

    // 2. Career season top scorers from PlayerFile bytes unk_2..unk_5 (league/cup/euro/other goals)
    if (swos.g_gameType == kGameTypeCareer) {
        uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        if (numPlayers > 0 && numPlayers <= kCurrentMatchPlayersSize) {
            const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];
            bool seenSlot[kCurrentMatchPlayersSize] = {};
            std::vector<std::pair<int, std::string>> scorers;
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
                if (pf->playerName[0] == '\0' || std::strncmp(pf->playerName, "*ERR*", 5) == 0)
                    continue;
                int seasonGoals = static_cast<int>(pf->unk_2) + static_cast<int>(pf->unk_3) +
                                  static_cast<int>(pf->unk_4) + static_cast<int>(pf->unk_5);
                if (seasonGoals > 0)
                    scorers.emplace_back(seasonGoals, std::string(pf->playerName));
            }
            std::sort(scorers.begin(), scorers.end(), [](const auto& a, const auto& b) {
                return a.first > b.first;
            });
            if (!scorers.empty()) {
                std::string seasonPart;
                size_t limit = std::min<size_t>(3, scorers.size());
                for (size_t i = 0; i < limit; ++i) {
                    if (i > 0) seasonPart += ", ";
                    seasonPart += scorers[i].second + " (" + std::to_string(scorers[i].first) + " goals)";
                }
                if (!out.empty()) out += " | ";
                out += "Season top scorers: " + seasonPart;
            }
        }
    }
    return out;
}

std::string extractUpcomingFixturesSummary()
{
    if (swos.g_gameType != kGameTypeCareer)
        return {};

    uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    uint16_t curIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);
    uint16_t curOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
    if (totalEntries == 0 || curIdx >= totalEntries || curOfs >= kCalendarBufferSize)
        return {};

    const uint8_t *cal = &SwosVM::g_memByte[CareerOffsets::kDsegDB3E6];
    uint16_t idx = curIdx;
    uint16_t ofs = curOfs;
    std::string upcoming;
    int found = 0;
    bool skippedCurrent = false;

    while (idx < totalEntries && ofs + 22 <= kCalendarBufferSize && found < 3) {
        const uint8_t *entry = &cal[ofs];
        uint16_t len = *reinterpret_cast<const uint16_t *>(entry + 2);
        if (len < 22)
            break;
        if (entry[4] == 0) {
            if (!skippedCurrent) {
                skippedCurrent = true;
            } else {
                const char *compName = nullptr;
                switch (entry[0]) {
                case 0: compName = "League match"; break;
                case 1: compName = "Big Domestic Cup tie"; break;
                case 2: compName = "League Cup tie"; break;
                case 3: compName = "Huge European Cup night"; break;
                case 4: compName = "Cup / Playoff tie"; break;
                default: break;
                }
                if (compName) {
                    if (!upcoming.empty()) upcoming += ", then ";
                    upcoming += compName;
                    ++found;
                }
            }
        }
        ofs += len;
        ++idx;
    }

    if (upcoming.empty())
        return {};
    return "Upcoming schedule: " + upcoming;
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

void recordPlayerTouchOrStrike(const Sprite *sp, bool isStrikeRelease, bool isHeading)
{
    if (!sp || sp->playerOrdinal < 1 || sp->playerOrdinal > kNumPlayersInTeam)
        return;

    int teamNum = sp->teamNumber;
    if (teamNum != 1 && teamNum != 2)
        return;

    const TeamGame& tg = (teamNum == 1) ? swos.topTeamInGame : swos.bottomTeamInGame;
    const char *pName = tg.players[sp->playerOrdinal - 1].shortName;
    if (!pName || pName[0] == '\0')
        return;

    // On a kick/shot release, prefer the striker's exact pitch coordinates (since the ball may already
    // have jumped 15-25px forward in the kick frame, pushing an edge-of-box strike inside the box!).
    float bx = swos.ballSprite.x.asFloat();
    float by = swos.ballSprite.y.asFloat();
    float sx = sp->x.asFloat();
    float sy = sp->y.asFloat();
    float distSq = (bx - sx) * (bx - sx) + (by - sy) * (by - sy);
    float touchX = (isStrikeRelease || distSq > 16.0f * 16.0f) ? sx : bx;
    float touchY = (isStrikeRelease || distSq > 16.0f * 16.0f) ? sy : by;

    bool samePlayer = (s_lastTouch.teamNum == teamNum && s_lastTouch.playerOrd == sp->playerOrdinal);
    if (!samePlayer) {
        // Detect pass completion or turnover/tackle between previous touch and new touch
        if (s_lastTouch.teamNum != 0 && s_lastTouch.playerOrd >= 1 && s_lastTouch.playerOrd <= kNumPlayersInTeam &&
            s_frameCounter >= s_lastTouch.frameTick) {
            uint32_t dt = s_frameCounter - s_lastTouch.frameTick;
            if (s_lastTouch.teamNum == teamNum && dt <= 240) {
                ++s_playerAnalytics[teamNum - 1][s_lastTouch.playerOrd - 1].passesCompleted;
                ++s_passLinks[teamNum - 1][{ s_lastTouch.playerOrd, sp->playerOrdinal }];
            } else if (s_lastTouch.teamNum != teamNum && dt <= 120) {
                ++s_playerAnalytics[s_lastTouch.teamNum - 1][s_lastTouch.playerOrd - 1].turnovers;
                ++s_playerAnalytics[teamNum - 1][sp->playerOrdinal - 1].tacklesWon;
            }
        }

        // Count discrete touch & spatial touch sample
        auto& pa = s_playerAnalytics[teamNum - 1][sp->playerOrdinal - 1];
        ++pa.touches;
        float nx = 0.5f, ny = 0.5f;
        normalizeToTeamAttackCoords(touchX, touchY, teamNum, nx, ny);
        addHeatmapSample(pa.heatmap, nx, ny, 2);
        addHeatmapSample(s_teamHeatmaps[teamNum - 1], nx, ny, 2);
        if (ny >= 0.67f) {
            int b = currentTimelineBucket();
            if (teamNum == 1) ++s_timeline[b].finalThirdTouches1;
            else ++s_timeline[b].finalThirdTouches2;
        }

        s_prevTouch = s_lastTouch;
    }

    bool prevHeader = samePlayer && s_lastTouch.wasHeader && (s_frameCounter - s_lastTouch.frameTick) <= 18;
    TouchRecord rec;
    rec.teamNum = teamNum;
    rec.playerOrd = sp->playerOrdinal;
    rec.name = pName;
    rec.x = touchX;
    rec.y = touchY;
    rec.ballZ = swos.ballSprite.z.asFloat();
    rec.wasHeader = isHeading || prevHeader;
    rec.frameTick = s_frameCounter;

    s_lastTouch = rec;
    s_lastTouchByTeam[teamNum - 1] = rec;
    s_lastTouchByPlayer[teamNum - 1][sp->playerOrdinal - 1] = rec;
    if (isStrikeRelease)
        s_lastStrikeByTeam[teamNum - 1] = rec;
}

void sampleBallTouch()
{
    // Never sample bogus touches during goal celebrations / stoppages
    if (swos.gameStatePl != GameState::kInProgress) {
        s_prevPassKickTimer = { 0, 0 };
        s_prevPlayerHasBall = { 0, 0 };
        return;
    }

    const TeamGeneralInfo *teams[2] = { &swos.topTeamData, &swos.bottomTeamData };
    bool recordedThisFrame = false;

    // 1. Check authoritative engine kick/shot release & active ball carry on BOTH teams
    for (int t = 0; t < 2; ++t) {
        const TeamGeneralInfo *ti = teams[t];
        word curKick = ti->passKickTimer;
        word curHas = ti->playerHasBall;
        bool kickJustFired = (curKick > s_prevPassKickTimer[t]) || (ti->shooting != 0);
        bool ballJustReleased = (s_prevPlayerHasBall[t] != 0 && curHas == 0);

        if (kickJustFired || ballJustReleased) {
            const Sprite *striker = ti->passingKickingPlayer.asAligned();
            if (!striker || striker->playerOrdinal < 1 || striker->playerOrdinal > kNumPlayersInTeam)
                striker = ti->controlledPlayer.asAligned();
            if (striker && striker->playerOrdinal >= 1 && striker->playerOrdinal <= kNumPlayersInTeam) {
                bool isHdr = (striker->state == PlayerState::kStaticHeader ||
                              striker->state == PlayerState::kJumpHeader);
                recordPlayerTouchOrStrike(striker, true, isHdr);
                recordedThisFrame = true;
            }
        } else if (curHas != 0) {
            const Sprite *carrier = ti->controlledPlayer.asAligned();
            if (carrier && carrier->playerOrdinal >= 1 && carrier->playerOrdinal <= kNumPlayersInTeam) {
                bool isHdr = (carrier->state == PlayerState::kStaticHeader ||
                              carrier->state == PlayerState::kJumpHeader);
                recordPlayerTouchOrStrike(carrier, false, isHdr);
                recordedThisFrame = true;
            }
        }

        s_prevPassKickTimer[t] = curKick;
        s_prevPlayerHasBall[t] = curHas;
    }

    // 2. Check SWOS's authoritative lastPlayerPlayed (catches one-touch volleys, headers, & deflections)
    const Sprite *lastPl = getLastPlayerPlayed();
    if (!swos.lastKeeperPlayed.isNull() &&
        (lastPl == &swos.goalie1Sprite || lastPl == &swos.goalie2Sprite)) {
        lastPl = swos.lastKeeperPlayed.asAligned();
    }

    if (lastPl && lastPl->playerOrdinal >= 1 && lastPl->playerOrdinal <= kNumPlayersInTeam) {
        bool pointerChanged = (lastPl != s_prevLastPlayerPlayed);
        float dx = swos.ballSprite.x.asFloat() - lastPl->x.asFloat();
        float dy = swos.ballSprite.y.asFloat() - lastPl->y.asFloat();
        bool veryClose = (dx * dx + dy * dy <= 20.0f * 20.0f);
        bool isHdr = (lastPl->state == PlayerState::kStaticHeader ||
                      lastPl->state == PlayerState::kJumpHeader);
        if (!recordedThisFrame && (pointerChanged || (veryClose && isHdr))) {
            // Only treat as a real touch/strike if the player actually just made contact
            if (pointerChanged || veryClose) {
                recordPlayerTouchOrStrike(lastPl, pointerChanged, isHdr);
            }
        }
    }
    s_prevLastPlayerPlayed = lastPl;
}

void sampleSpatialPositionsAndTimeline()
{
    if (swos.gameStatePl != GameState::kInProgress)
        return;

    // 1. Ball heatmap (always oriented to Team 1 attacking toward 1.0)
    float bnx = 0.5f, bny = 0.5f;
    normalizeToTeamAttackCoords(swos.ballSprite.x.asFloat(), swos.ballSprite.y.asFloat(), 1, bnx, bny);
    addHeatmapSample(s_ballHeatmap, bnx, bny, 1);

    // 2. Timeline 5-min possession tick
    int b = currentTimelineBucket();
    const TeamGeneralInfo *team1Info = nullptr;
    const TeamGeneralInfo *team2Info = nullptr;
    resolveCanonicalTeams(team1Info, team2Info);
    const TeamGeneralInfo *lastTeam = swos.lastTeamPlayed.asAligned();
    if (lastTeam == team1Info)
        ++s_timeline[b].possTicks1;
    else if (lastTeam == team2Info)
        ++s_timeline[b].possTicks2;

    // 3. All 22 on-pitch player positions for average positions & heatmaps
    auto sampleTeamSprites = [](const TeamGeneralInfo *info, int teamNum) {
        if (!info)
            return;
        const auto *plArr = info->players.asAligned();
        if (!plArr)
            return;
        for (int i = 0; i < kNumPlayersInLineup; ++i) {
            const Sprite *sp = plArr[i].asAligned();
            if (!sp || sp->playerOrdinal < 1 || sp->playerOrdinal > kNumPlayersInTeam)
                continue;
            float nx = 0.5f, ny = 0.5f;
            normalizeToTeamAttackCoords(sp->x.asFloat(), sp->y.asFloat(), teamNum, nx, ny);
            auto& pa = s_playerAnalytics[teamNum - 1][sp->playerOrdinal - 1];
            pa.sumNormX += static_cast<double>(nx);
            pa.sumNormY += static_cast<double>(ny);
            ++pa.posSamples;
            addHeatmapSample(pa.heatmap, nx, ny, 1);
            addHeatmapSample(s_teamHeatmaps[teamNum - 1], nx, ny, 1);
        }
    };
    sampleTeamSprites(team1Info, 1);
    sampleTeamSprites(team2Info, 2);
}

bool isInsideSwosPenaltyArea(float x, float y, bool topGoal);

void recordShotAnalytics(
    int teamNum, const std::string& teamName, int playerOrd, const std::string& playerName,
    const std::string& assistedBy, float rawX, float rawY, float rawZ,
    bool isHeader, bool isPenalty, bool isFreeKick, bool isVolley, bool withinCorner,
    const std::string& outcome)
{
    if (teamNum != 1 && teamNum != 2)
        return;

    // Deduplicate back-to-back shot comments within 25 frames for non-goal shots
    if (outcome != "goal" && outcome != "own_goal" &&
        s_lastShotDedupFrame > 0 && s_frameCounter >= s_lastShotDedupFrame &&
        (s_frameCounter - s_lastShotDedupFrame) < 25) {
        return;
    }

    // If a keeper fingertip "saved" event fired just moments ago (< 60 frames) for the SAME attacking team
    // and the ball still went in (or hit the post), replace that false save in-place so the Shot Map
    // doesn't draw two overlapping circles or double-count xG / keeper saves!
    bool mergedFromDeflectionSave = false;
    if ((outcome == "goal" || outcome == "post") && !s_shots.empty()) {
        const auto& prev = s_shots.back();
        if (prev.outcome == "saved" && prev.teamNum == teamNum &&
            s_frameCounter >= prev.frameTick && (s_frameCounter - prev.frameTick) <= 60) {
            // Roll back the false save's player/timeline/GK credits
            int defTeam = (teamNum == 1) ? 2 : 1;
            if (s_playerAnalytics[defTeam - 1][0].gkSaves > 0)
                --s_playerAnalytics[defTeam - 1][0].gkSaves;
            int prevB = std::clamp(prev.timelineBucket, 0, kMaxTimelineBuckets - 1);
            if (teamNum == 1) {
                s_timeline[prevB].shots1 = std::max(0, s_timeline[prevB].shots1 - 1);
                s_timeline[prevB].xg1 = std::max(0.0, s_timeline[prevB].xg1 - prev.xg);
            } else {
                s_timeline[prevB].shots2 = std::max(0, s_timeline[prevB].shots2 - 1);
                s_timeline[prevB].xg2 = std::max(0.0, s_timeline[prevB].xg2 - prev.xg);
            }
            if (prev.playerOrd >= 1 && prev.playerOrd <= kNumPlayersInTeam) {
                auto& prevPa = s_playerAnalytics[teamNum - 1][prev.playerOrd - 1];
                prevPa.shots = std::max(0, prevPa.shots - 1);
                prevPa.shotsOnTarget = std::max(0, prevPa.shotsOnTarget - 1);
                prevPa.xg = std::max(0.0, prevPa.xg - prev.xg);
            }
            s_shots.pop_back();
            mergedFromDeflectionSave = true;
        }
    }
    (void)mergedFromDeflectionSave;
    s_lastShotDedupFrame = s_frameCounter;

    float normX = 0.5f, normY = 0.5f;
    normalizeToTeamAttackCoords(rawX, rawY, teamNum, normX, normY);
    if (isPenalty) {
        normX = 0.5f;
        normY = 1.0f - (static_cast<float>(kTopPenaltySpotY - kTopPitchLine) /
                        static_cast<float>(kBottomPitchLine - kTopPitchLine));
    }

    float distYards = 0.0f;
    float angleDeg = 0.0f;
    double xg = (outcome == "own_goal")
        ? 0.0
        : computeSwosShotXg(normX, normY, rawZ, isHeader, isPenalty, isFreeKick, isVolley, distYards, angleDeg);

    bool attackingTop = (teamNum == 1) ? isTeam1AttackingTopGoal() : !isTeam1AttackingTopGoal();
    bool shotOutsideBox = (!isPenalty && outcome != "own_goal") &&
                          !isInsideSwosPenaltyArea(rawX, rawY, attackingTop);

    int b = currentTimelineBucket();
    ShotRecord sr;
    sr.minute = static_cast<int>(gameTimeInMinutes());
    sr.half = swos.halfNumber ? static_cast<int>(swos.halfNumber) : 1;
    sr.teamNum = teamNum;
    sr.team = teamName;
    sr.playerOrd = playerOrd;
    if (playerOrd >= 1 && playerOrd <= kNumPlayersInTeam) {
        const TeamGame& tg = (teamNum == 1) ? swos.topTeamInGame : swos.bottomTeamInGame;
        sr.shirtNumber = static_cast<int>(tg.players[playerOrd - 1].shirtNumber);
    }
    sr.player = playerName;
    sr.assistedBy = assistedBy;
    sr.rawX = rawX;
    sr.rawY = rawY;
    sr.rawZ = rawZ;
    sr.normX = normX;
    sr.normY = normY;
    sr.distYards = distYards;
    sr.angleDeg = angleDeg;
    sr.bodyPart = isHeader ? "header" : (isVolley ? "volley" : "foot");
    sr.situation = isPenalty ? "penalty" : (isFreeKick ? "free_kick" : (withinCorner ? "corner" : "open_play"));
    sr.outcome = outcome;
    sr.outsideBox = shotOutsideBox;
    sr.xg = xg;
    sr.frameTick = s_frameCounter;
    sr.timelineBucket = b;
    s_shots.push_back(sr);

    // Update player & timeline aggregates
    if (outcome != "own_goal") {
        if (teamNum == 1) {
            ++s_timeline[b].shots1;
            s_timeline[b].xg1 += xg;
            if (outcome == "goal") ++s_timeline[b].goals1;
        } else {
            ++s_timeline[b].shots2;
            s_timeline[b].xg2 += xg;
            if (outcome == "goal") ++s_timeline[b].goals2;
        }
        if (playerOrd >= 1 && playerOrd <= kNumPlayersInTeam) {
            auto& pa = s_playerAnalytics[teamNum - 1][playerOrd - 1];
            ++pa.shots;
            pa.xg += xg;
            if (outcome == "goal" || outcome == "saved")
                ++pa.shotsOnTarget;
            if (outcome == "goal")
                ++pa.goals;
        }
    } else {
        if (teamNum == 1) ++s_timeline[b].goals1;
        else ++s_timeline[b].goals2;
    }
}

// Exact SWOS penalty area lines (swos.asm): X in [193..478], upper box Y <= 216, lower box Y >= 682
bool isInsideSwosPenaltyArea(float x, float y, bool topGoal)
{
    if (x < 193.0f || x > 478.0f)
        return false;
    return topGoal ? (y <= 216.0f) : (y >= 682.0f);
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

struct GoalMechanicsInfo {
    std::string detail;
    bool isHeader = false;
    bool isOutsidePenaltyArea = false;
    float shotX = 0.0f;
    float shotY = 0.0f;
    float shotZ = 0.0f;
    bool hasExactStrike = false;
};

// Resolve the shooter's true strike/touch origin, prioritizing the actual scorer's last strike/touch
// so opponent deflections inside the penalty box NEVER corrupt a long-range shot's coordinates!
const TouchRecord *resolveTeamShotTouch(int teamNum, const Sprite *scorerSprite, uint32_t maxAgeFrames = 360)
{
    if (teamNum != 1 && teamNum != 2)
        return nullptr;

    auto isFresh = [&](const TouchRecord& r) {
        return r.frameTick > 0 && s_frameCounter >= r.frameTick && (s_frameCounter - r.frameTick) <= maxAgeFrames;
    };

    // 1. Exact scorer's own recorded strike/touch (immune to teammates & opponent deflections!)
    if (scorerSprite && scorerSprite->playerOrdinal >= 1 && scorerSprite->playerOrdinal <= kNumPlayersInTeam) {
        const auto& pRec = s_lastTouchByPlayer[teamNum - 1][scorerSprite->playerOrdinal - 1];
        if (isFresh(pRec))
            return &pRec;
    }

    // 2. Attacking team's last kick/shot release
    const auto& strikeRec = s_lastStrikeByTeam[teamNum - 1];
    if (isFresh(strikeRec))
        return &strikeRec;

    // 3. Attacking team's last touch
    const auto& teamRec = s_lastTouchByTeam[teamNum - 1];
    if (isFresh(teamRec))
        return &teamRec;

    return nullptr;
}

GoalMechanicsInfo describeGoalMechanics(const Sprite *scorerSprite, const char *eventType, int scoringTeamNum)
{
    GoalMechanicsInfo info;
    const TouchRecord *teamTouch = resolveTeamShotTouch(scoringTeamNum, scorerSprite, 360);
    bool recentTouch = (teamTouch != nullptr);

    bool isOwnGoal = (swos.goalTypeScored == static_cast<int>(GoalType::kOwnGoal));
    bool isPenalty = (swos.penalty != 0) || (eventType && std::strstr(eventType, "PENALTY") != nullptr);

    if (isOwnGoal) {
        info.detail = "own goal";
    } else if (isPenalty) {
        info.detail = "penalty kick";
    } else if ((scorerSprite && (scorerSprite->state == PlayerState::kStaticHeader ||
                                 scorerSprite->state == PlayerState::kJumpHeader)) ||
               (recentTouch && teamTouch->wasHeader)) {
        info.detail = "header";
        info.isHeader = true;
    } else {
        info.detail = "footed shot (NOT a header)";
    }

    // Exact SWOS penalty-box classification from shooter's true strike position
    float bx = swos.ballSprite.x.asFloat();
    float by = swos.ballSprite.y.asFloat();
    float bz = swos.ballSprite.z.asFloat();
    bool scoredAtTopGoal = (by < static_cast<float>(kPitchCenterY));

    float shotX = bx;
    float shotY = by;
    float shotZ = bz;
    bool haveOrigin = false;
    if (recentTouch) {
        shotX = teamTouch->x;
        shotY = teamTouch->y;
        shotZ = teamTouch->ballZ;
        haveOrigin = true;
    } else if (scorerSprite && !isOwnGoal) {
        // Fallback to scorer sprite coordinates (far better than goal-line ballSprite!)
        shotX = scorerSprite->x.asFloat();
        shotY = scorerSprite->y.asFloat();
        haveOrigin = true;
    }

    info.shotX = shotX;
    info.shotY = shotY;
    info.shotZ = shotZ;
    info.hasExactStrike = haveOrigin;

    float goalY = scoredAtTopGoal ? static_cast<float>(kTopPitchLine) : static_cast<float>(kBottomPitchLine);
    float shotDist = std::hypot(shotX - static_cast<float>(kPitchCenterX), shotY - goalY);

    if (!isPenalty && !isOwnGoal) {
        bool outsideBox = haveOrigin && !isInsideSwosPenaltyArea(shotX, shotY, scoredAtTopGoal);
        if (outsideBox) {
            info.detail += ", long-range strike from outside the penalty area";
            info.isOutsidePenaltyArea = true;
        } else if (shotDist < 55.0f) {
            info.detail += ", close-range finish";
        } else {
            info.detail += ", inside the box";
        }

        if (bz > 11.0f)
            info.detail += ", high finish";
    }

    // Assist attribution from prior teammate touch within ~5 seconds (300 frames)
    if (scorerSprite && !isOwnGoal && !isPenalty) {
        if (s_prevTouch.teamNum == scorerSprite->teamNumber &&
            s_prevTouch.playerOrd != scorerSprite->playerOrdinal &&
            !s_prevTouch.name.empty() &&
            s_frameCounter >= s_prevTouch.frameTick &&
            (s_frameCounter - s_prevTouch.frameTick) <= 300) {
            info.detail += ", assisted by " + s_prevTouch.name;
            ++s_matchAssists[s_prevTouch.name];
        }
    }
    return info;
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
    s_lastCornerFrame = 0;
    s_lastFreeKickFrame = 0;
    s_zoneSamplesTeam1Attack = 0;
    s_zoneSamplesMidfield = 0;
    s_zoneSamplesTeam2Attack = 0;
    s_htGoals1 = 0;
    s_htGoals2 = 0;
    s_longRangeGoals1 = 0;
    s_longRangeGoals2 = 0;
    s_headerGoals1 = 0;
    s_headerGoals2 = 0;
    s_maxDeficit1 = 0;
    s_maxDeficit2 = 0;
    s_redBefore60_1 = false;
    s_redBefore60_2 = false;
    s_lastTouch = {};
    s_prevTouch = {};
    s_lastTouchByTeam[0] = {};
    s_lastTouchByTeam[1] = {};
    s_lastStrikeByTeam[0] = {};
    s_lastStrikeByTeam[1] = {};
    for (auto& arr : s_lastTouchByPlayer)
        for (auto& tr : arr)
            tr = TouchRecord{};
    s_prevLastPlayerPlayed = nullptr;
    s_prevPassKickTimer = { 0, 0 };
    s_prevPlayerHasBall = { 0, 0 };
    s_recentEvents.clear();
    s_allMatchEvents.clear();
    s_matchAssists.clear();
    s_shots.clear();
    s_ballHeatmap.fill(0);
    s_teamHeatmaps[0].fill(0);
    s_teamHeatmaps[1].fill(0);
    for (auto& teamArr : s_playerAnalytics)
        for (auto& pa : teamArr)
            pa = PlayerMatchAnalytics{};
    s_passLinks[0].clear();
    s_passLinks[1].clear();
    for (auto& tb : s_timeline)
        tb = TimelineBucket{};
    s_lastShotDedupFrame = 0;
}

void updateMatchTelemetry()
{
    ++s_frameCounter;
    if (swos.g_trainingGame)
        return;

    if (swos.halfNumber <= 1) {
        s_htGoals1 = swos.statsTeam1Goals;
        s_htGoals2 = swos.statsTeam2Goals;
    }

    if (swos.gameState == GameState::kCornerLeft || swos.gameState == GameState::kCornerRight)
        s_lastCornerFrame = s_frameCounter;
    if (swos.gameState >= GameState::kFreeKickOuterLeft && swos.gameState <= GameState::kFreeKickOuterRight)
        s_lastFreeKickFrame = s_frameCounter;

    int deficit1 = swos.statsTeam2Goals - swos.statsTeam1Goals;
    if (deficit1 > s_maxDeficit1)
        s_maxDeficit1 = deficit1;
    int deficit2 = swos.statsTeam1Goals - swos.statsTeam2Goals;
    if (deficit2 > s_maxDeficit2)
        s_maxDeficit2 = deficit2;

    if (static_cast<int>(gameTimeInMinutes()) < 60 && (s_frameCounter % 15) == 0) {
        const auto st = getStats();
        if (st.team1.sendingsOff > 0)
            s_redBefore60_1 = true;
        if (st.team2.sendingsOff > 0)
            s_redBefore60_2 = true;
    }

    sampleBallTouch();
    if ((s_frameCounter % 30) == 0) {
        sampleTerritoryThirds();
        sampleSpatialPositionsAndTimeline();
    }
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

    auto teamNameForInfo = [&](const TeamGeneralInfo *info) -> std::string {
        if (!info)
            return {};
        const TeamGame *tg = info->inGameTeamPtr.asAligned();
        if (tg == &swos.topTeamInGame)
            return t1;
        if (tg == &swos.bottomTeamInGame)
            return t2;
        if (info == team1Info)
            return t1;
        if (info == team2Info)
            return t2;
        return {};
    };

    auto keeperNameForInfo = [&](const TeamGeneralInfo *info) -> std::string {
        if (!info)
            return {};
        const TeamGame *tg = info->inGameTeamPtr.asAligned();
        if (!tg)
            tg = (info == team1Info) ? &swos.topTeamInGame : &swos.bottomTeamInGame;
        return (tg->players[0].shortName[0] != '\0') ? std::string(tg->players[0].shortName) : std::string();
    };

    // Pitch Y geometry: topTeamData defends top goal (y <= kPitchCenterY) and attacks bottom goal (y > kPitchCenterY).
    // bottomTeamData defends bottom goal (y > kPitchCenterY) and attacks top goal (y <= kPitchCenterY).
    float ballY = swos.ballSprite.y.asFloat();
    const TeamGeneralInfo *attackingInfo = (ballY > static_cast<float>(kPitchCenterY)) ? &swos.topTeamData : &swos.bottomTeamData;
    const TeamGeneralInfo *defendingInfo = (ballY > static_cast<float>(kPitchCenterY)) ? &swos.bottomTeamData : &swos.topTeamData;

    bool isKeeperSave = (std::strstr(eventType, "SAVE") != nullptr);
    bool isAttackGoalMouth = (std::strstr(eventType, "POST") != nullptr ||
                              std::strstr(eventType, "CROSSBAR") != nullptr ||
                              std::strstr(eventType, "WIDE") != nullptr ||
                              std::strstr(eventType, "PENALTY MISSED") != nullptr ||
                              std::strstr(eventType, "PENALTY AWARDED") != nullptr);
    bool isCardEvent = (std::strstr(eventType, "CARD") != nullptr);

    std::string teamName;
    std::string playerName;
    std::string detail;

    if (isGoalEvent) {
        const TeamGeneralInfo *scoringPtr = swos.lastTeamScored.asAligned();
        int scoringTeamNum = 0;
        if (swos.lastTeamScoredNumber == 1 || scoringPtr == team1Info) {
            teamName = t1;
            scoringTeamNum = 1;
        } else if (swos.lastTeamScoredNumber == 2 || scoringPtr == team2Info) {
            teamName = t2;
            scoringTeamNum = 2;
        }

        const Sprite *scorerSprite = swos.currentScorer.asAligned();
        if (scorerSprite && scorerSprite->playerOrdinal >= 1 && scorerSprite->playerOrdinal <= kNumPlayersInTeam) {
            const TeamGame& tg = (scorerSprite->teamNumber == 1) ? swos.topTeamInGame : swos.bottomTeamInGame;
            if (tg.players[scorerSprite->playerOrdinal - 1].shortName[0] != '\0')
                playerName = tg.players[scorerSprite->playerOrdinal - 1].shortName;
        }
        GoalMechanicsInfo mech = describeGoalMechanics(scorerSprite, eventType, scoringTeamNum);
        detail = mech.detail;
        if (scoringTeamNum == 1) {
            if (mech.isOutsidePenaltyArea) ++s_longRangeGoals1;
            if (mech.isHeader) ++s_headerGoals1;
        } else if (scoringTeamNum == 2) {
            if (mech.isOutsidePenaltyArea) ++s_longRangeGoals2;
            if (mech.isHeader) ++s_headerGoals2;
        }
        const char *concedingTeam = (teamName == t1) ? t2 : t1;
        const TeamGame& concedingTg = (teamName == t1) ? swos.bottomTeamInGame : swos.topTeamInGame;
        if (concedingTg.players[0].shortName[0] != '\0')
            detail += ", past " + std::string(concedingTeam) + " goalkeeper " + concedingTg.players[0].shortName;

        // Fire Global Achievements goal hook
        if (!swos.playingPenalties && (scoringTeamNum == 1 || scoringTeamNum == 2)) {
            bool t1Human = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0 || team1Info->isPlCoach != 0);
            bool t2Human = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0 || team2Info->isPlCoach != 0);
            if (!t1Human && !t2Human) {
                const TeamFile *tf1 = swos.gameTeam1.asAligned();
                const TeamFile *tf2 = swos.gameTeam2.asAligned();
                if (tf1) t1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
                if (tf2) t2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);
            }
            bool scoringSideHuman = (scoringTeamNum == 1) ? t1Human : t2Human;
            const TouchRecord *goalTouch = resolveTeamShotTouch(scoringTeamNum, scorerSprite, 360);

            AchievementGoalEvent achGoal;
            achGoal.humanScored = scoringSideHuman;
            achGoal.isOwnGoal = (swos.goalTypeScored == static_cast<int>(GoalType::kOwnGoal));
            achGoal.isPenalty = (swos.penalty != 0) || (std::strstr(eventType, "PENALTY") != nullptr);
            achGoal.isHeader = mech.isHeader;
            achGoal.isOutsideBox = mech.isOutsidePenaltyArea;
            achGoal.isFreeKick = (!achGoal.isOwnGoal && !achGoal.isPenalty &&
                s_lastFreeKickFrame > 0 && s_frameCounter >= s_lastFreeKickFrame &&
                (s_frameCounter - s_lastFreeKickFrame) <= 240);
            achGoal.withinCornerWindow = (!achGoal.isOwnGoal && !achGoal.isPenalty &&
                s_lastCornerFrame > 0 && s_frameCounter >= s_lastCornerFrame &&
                (s_frameCounter - s_lastCornerFrame) <= 180);
            achGoal.isVolley = (!achGoal.isOwnGoal && !achGoal.isPenalty && !achGoal.isHeader &&
                goalTouch && goalTouch->ballZ >= 6.0f);
            achGoal.scorerIsGoalkeeper = (scorerSprite && scorerSprite->playerOrdinal == 1);
            achGoal.matchMinute = static_cast<int>(gameTimeInMinutes());
            achGoal.isExtraTime = (swos.halfNumber > 2 || achGoal.matchMinute > 90);
            int g1 = swos.statsTeam1Goals;
            int g2 = swos.statsTeam2Goals;
            achGoal.isGoAheadGoal = (scoringTeamNum == 1) ? (g1 > g2) : (g2 > g1);
            if (scorerSprite && scorerSprite->playerOrdinal >= 1 && scorerSprite->playerOrdinal <= kNumPlayersInTeam) {
                const TeamGame& tg = (scorerSprite->teamNumber == 1) ? swos.topTeamInGame : swos.bottomTeamInGame;
                achGoal.scorerMatchGoals = std::max<int>(1, static_cast<int>(tg.players[scorerSprite->playerOrdinal - 1].goalsScored));
            }
            achievementsOnGoalScored(achGoal);

            // Record Telemetry V2 shot + xG + assister credit using the exact scorer strike coordinates!
            int scorerOrd = (scorerSprite && scorerSprite->playerOrdinal >= 1 && scorerSprite->playerOrdinal <= kNumPlayersInTeam)
                ? static_cast<int>(scorerSprite->playerOrdinal)
                : (goalTouch ? goalTouch->playerOrd : 0);
            std::string assister;
            if (!achGoal.isOwnGoal && !achGoal.isPenalty &&
                s_prevTouch.teamNum == scoringTeamNum &&
                s_prevTouch.playerOrd >= 1 && s_prevTouch.playerOrd <= kNumPlayersInTeam &&
                s_prevTouch.playerOrd != scorerOrd &&
                s_frameCounter >= s_prevTouch.frameTick && (s_frameCounter - s_prevTouch.frameTick) <= 300) {
                assister = s_prevTouch.name;
                ++s_playerAnalytics[scoringTeamNum - 1][s_prevTouch.playerOrd - 1].assists;
            }
            recordShotAnalytics(
                scoringTeamNum, teamName, scorerOrd, playerName, assister,
                mech.shotX, mech.shotY, mech.shotZ, achGoal.isHeader, achGoal.isPenalty, achGoal.isFreeKick,
                achGoal.isVolley, achGoal.withinCornerWindow,
                achGoal.isOwnGoal ? "own_goal" : "goal");
        }
    } else if (isKeeperSave) {
        // Defending team's goalkeeper made the save against the attacking team's shot!
        teamName = teamNameForInfo(defendingInfo);
        std::string oppTeam = teamNameForInfo(attackingInfo);
        playerName = keeperNameForInfo(defendingInfo);

        int atkTeamNum = (attackingInfo == team1Info) ? 1 : 2;
        int defTeamNum = (defendingInfo == team1Info) ? 1 : 2;
        ++s_playerAnalytics[defTeamNum - 1][0].gkSaves;

        std::string shooter;
        int shooterOrd = 0;
        const TouchRecord *atkTouch = resolveTeamShotTouch(atkTeamNum, nullptr, 300);
        if (atkTouch && !atkTouch->name.empty()) {
            shooter = atkTouch->name;
            shooterOrd = atkTouch->playerOrd;
        }
        if (!swos.playingPenalties) {
            bool isPen = (swos.penalty != 0) || (std::strstr(eventType, "PENALTY") != nullptr);
            bool isFk = !isPen && s_lastFreeKickFrame > 0 && s_frameCounter >= s_lastFreeKickFrame && (s_frameCounter - s_lastFreeKickFrame) <= 240;
            bool isCor = !isPen && s_lastCornerFrame > 0 && s_frameCounter >= s_lastCornerFrame && (s_frameCounter - s_lastCornerFrame) <= 180;
            bool isHdr = atkTouch && atkTouch->wasHeader;
            bool isVol = !isPen && !isHdr && atkTouch && atkTouch->ballZ >= 6.0f;
            float sx = atkTouch ? atkTouch->x : swos.ballSprite.x.asFloat();
            float sy = atkTouch ? atkTouch->y : swos.ballSprite.y.asFloat();
            float sz = atkTouch ? atkTouch->ballZ : swos.ballSprite.z.asFloat();
            recordShotAnalytics(atkTeamNum, oppTeam, shooterOrd, shooter, {}, sx, sy, sz, isHdr, isPen, isFk, isVol, isCor, "saved");
        }
        detail = teamName + " goalkeeper" + (playerName.empty() ? "" : (" " + playerName)) +
                 " saved shot from " + oppTeam + (shooter.empty() ? "" : (" (" + shooter + ")")) +
                 " (NOT " + oppTeam + "'s goalkeeper)";
    } else if (isAttackGoalMouth) {
        // Attacking team was shooting at defending team's goal
        teamName = teamNameForInfo(attackingInfo);
        std::string defTeam = teamNameForInfo(defendingInfo);
        std::string defGk = keeperNameForInfo(defendingInfo);

        int atkTeamNum = (attackingInfo == team1Info) ? 1 : 2;
        int shooterOrd = 0;
        const TouchRecord *atkTouch = resolveTeamShotTouch(atkTeamNum, nullptr, 300);
        if (atkTouch && !atkTouch->name.empty()) {
            playerName = atkTouch->name;
            shooterOrd = atkTouch->playerOrd;
        }
        bool isPenAwarded = (std::strstr(eventType, "PENALTY AWARDED") != nullptr);
        if (!swos.playingPenalties && !isPenAwarded) {
            bool isPost = (std::strstr(eventType, "POST") != nullptr || std::strstr(eventType, "CROSSBAR") != nullptr);
            bool isPen = (std::strstr(eventType, "PENALTY MISSED") != nullptr);
            bool isFk = !isPen && s_lastFreeKickFrame > 0 && s_frameCounter >= s_lastFreeKickFrame && (s_frameCounter - s_lastFreeKickFrame) <= 240;
            bool isCor = !isPen && s_lastCornerFrame > 0 && s_frameCounter >= s_lastCornerFrame && (s_frameCounter - s_lastCornerFrame) <= 180;
            bool isHdr = atkTouch && atkTouch->wasHeader;
            bool isVol = !isPen && !isHdr && atkTouch && atkTouch->ballZ >= 6.0f;
            float sx = atkTouch ? atkTouch->x : swos.ballSprite.x.asFloat();
            float sy = atkTouch ? atkTouch->y : swos.ballSprite.y.asFloat();
            float sz = atkTouch ? atkTouch->ballZ : swos.ballSprite.z.asFloat();
            recordShotAnalytics(atkTeamNum, teamName, shooterOrd, playerName, {}, sx, sy, sz, isHdr, isPen, isFk, isVol, isCor, isPost ? "post" : "wide");
        }
        detail = teamName + " attacking " + defTeam + "'s goal" +
                 (defGk.empty() ? "" : (" (defending GK: " + defGk + ")"));
    } else if (isCardEvent) {
        const TeamGeneralInfo *bookedTeam = swos.lastTeamBooked.asAligned();
        teamName = teamNameForInfo(bookedTeam);
        const Sprite *bookedSp = swos.bookedPlayer.asAligned();
        if (bookedSp && bookedSp->playerOrdinal >= 1 && bookedSp->playerOrdinal <= kNumPlayersInTeam) {
            const TeamGame& tg = (bookedSp->teamNumber == 1) ? swos.topTeamInGame : swos.bottomTeamInGame;
            if (tg.players[bookedSp->playerOrdinal - 1].shortName[0] != '\0')
                playerName = tg.players[bookedSp->playerOrdinal - 1].shortName;
            int bTeam = (bookedSp->teamNumber == 1 || bookedTeam == team1Info) ? 1 : 2;
            if (std::strstr(eventType, "RED") != nullptr)
                ++s_playerAnalytics[bTeam - 1][bookedSp->playerOrdinal - 1].redCards;
            else
                ++s_playerAnalytics[bTeam - 1][bookedSp->playerOrdinal - 1].yellowCards;
        }
    } else {
        const TeamGeneralInfo *activePtr = swos.lastTeamPlayed.asAligned();
        if (!activePtr)
            activePtr = swos.lastTeamPlayedBeforeBreak.asAligned();
        teamName = teamNameForInfo(activePtr);
        if (!s_lastTouch.name.empty() && (s_frameCounter - s_lastTouch.frameTick) < 90)
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

std::pair<int, int> getHalfTimeTelemetryGoals()
{
    return { s_htGoals1, s_htGoals2 };
}

std::pair<int, int> getLongRangeTelemetryGoals()
{
    return { s_longRangeGoals1, s_longRangeGoals2 };
}

std::pair<int, int> getHeaderTelemetryGoals()
{
    return { s_headerGoals1, s_headerGoals2 };
}

std::pair<int, int> getMatchMaxGoalDeficits()
{
    return { s_maxDeficit1, s_maxDeficit2 };
}

std::pair<bool, bool> getMatchEarlyRedCards()
{
    return { s_redBefore60_1, s_redBefore60_2 };
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
    snap.team1Human = (team1Info->playerNumber != 0 || team1Info->playerCoachNumber != 0 || team1Info->isPlCoach != 0);
    snap.team2Human = (team2Info->playerNumber != 0 || team2Info->playerCoachNumber != 0 || team2Info->isPlCoach != 0);

    const TeamFile *tf1 = swos.gameTeam1.asAligned();
    const TeamFile *tf2 = swos.gameTeam2.asAligned();
    if (!tf1) tf1 = swos.playMatchTeam1Ptr.asAligned();
    if (!tf2) tf2 = swos.playMatchTeam2Ptr.asAligned();
    snap.coach1 = cleanCoachName(tf1);
    snap.coach2 = cleanCoachName(tf2);
    if (!snap.team1Human && !snap.team2Human) {
        if (tf1)
            snap.team1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
        if (tf2)
            snap.team2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);
    }

    snap.keeper1 = swos.topTeamInGame.players[0].shortName[0] ? swos.topTeamInGame.players[0].shortName : "";
    snap.keeper2 = swos.bottomTeamInGame.players[0].shortName[0] ? swos.bottomTeamInGame.players[0].shortName : "";
    snap.players1 = collectKeyPlayers(swos.topTeamInGame);
    snap.players2 = collectKeyPlayers(swos.bottomTeamInGame);
    snap.bench1 = collectBenchPlayers(swos.topTeamInGame);
    snap.bench2 = collectBenchPlayers(swos.bottomTeamInGame);

    snap.goals1 = swos.statsTeam1Goals;
    snap.goals2 = swos.statsTeam2Goals;
    snap.htGoals1 = s_htGoals1;
    snap.htGoals2 = s_htGoals2;
    snap.penGoals1 = swos.team1PenaltyGoals;
    snap.penGoals2 = swos.team2PenaltyGoals;
    snap.longRangeGoals1 = s_longRangeGoals1;
    snap.longRangeGoals2 = s_longRangeGoals2;
    snap.headerGoals1 = s_headerGoals1;
    snap.headerGoals2 = s_headerGoals2;
    snap.timePressureTag = computeTimePressureTag(snap.minute, snap.goals1, snap.goals2, snap.playingPenalties);

    const auto stats = getStats();
    snap.shots1 = stats.team1.goalAttempts;
    snap.shots2 = stats.team2.goalAttempts;
    snap.onTarget1 = stats.team1.onTarget;
    snap.onTarget2 = stats.team2.onTarget;
    int totalPossession = stats.team1.ballPossession + stats.team2.ballPossession;
    if (totalPossession > 0) {
        snap.possession1 = (100 * stats.team1.ballPossession) / totalPossession;
        snap.possession2 = 100 - snap.possession1;
    } else {
        snap.possession1 = 50;
        snap.possession2 = 50;
    }
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
    snap.managerCareerSummary = extractManagerCareerSummary();
    snap.topScorersSummary = extractTopScorersAndAssistsSummary();
    snap.upcomingFixturesSummary = extractUpcomingFixturesSummary();
    snap.activeSponsorChallengeSummary = formatActiveSponsorChallengeForTelemetry();
    snap.pressCharismaSummary = formatPressContextForTelemetry();
    snap.stadiumFacilitiesSummary = formatStadiumContextForTelemetry();
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
    if (!snap.keeper1.empty() || !snap.keeper2.empty()) {
        out += "\nGoalkeepers: " + snap.team1 + " GK = " + (snap.keeper1.empty() ? "Unknown" : snap.keeper1) +
               " | " + snap.team2 + " GK = " + (snap.keeper2.empty() ? "Unknown" : snap.keeper2) + ".";
    }
    if (!snap.players1.empty())
        out += "\n" + snap.team1 + " XI: " + snap.players1;
    if (!snap.bench1.empty())
        out += "\n" + snap.team1 + " Bench (available subs): " + snap.bench1;
    if (!snap.players2.empty())
        out += "\n" + snap.team2 + " XI: " + snap.players2;
    if (!snap.bench2.empty())
        out += "\n" + snap.team2 + " Bench (available subs): " + snap.bench2;
    if (!snap.careerRecordSummary.empty())
        out += "\n" + snap.careerRecordSummary;
    if (!snap.managerCareerSummary.empty())
        out += "\n" + snap.managerCareerSummary;
    if (!snap.topScorersSummary.empty())
        out += "\n" + snap.topScorersSummary;
    if (!snap.upcomingFixturesSummary.empty())
        out += "\n" + snap.upcomingFixturesSummary;
    if (!snap.activeSponsorChallengeSummary.empty())
        out += "\n" + snap.activeSponsorChallengeSummary;
    if (!snap.pressCharismaSummary.empty())
        out += "\n" + snap.pressCharismaSummary;
    if (!snap.stadiumFacilitiesSummary.empty())
        out += "\n" + snap.stadiumFacilitiesSummary +
               " (Fans, pundits, and journalists may organically praise or criticize the stadium atmosphere, pitch surface, facilities, construction, or ticket prices.)";

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

double computeSwosShotXg(
    float normX, float normY, float rawZ,
    bool isHeader, bool isPenalty, bool isFreeKick, bool isVolley,
    float& outDistYards, float& outAngleDeg)
{
    if (isPenalty) {
        outDistYards = 12.0f;
        outAngleDeg = 33.4f;
        return 0.76;
    }

    // Map normalized pitch [0..1]x[0..1] to realistic yards (75yd wide x 115yd long, goal width = 8yd centered at x=37.5)
    constexpr double kPitchWidthYds = 75.0;
    constexpr double kPitchLengthYds = 115.0;
    constexpr double kGoalHalfWidthYds = 4.0;

    double xYds = (static_cast<double>(std::clamp(normX, 0.0f, 1.0f)) - 0.5) * kPitchWidthYds;
    double yYds = std::max(0.5, (1.0 - static_cast<double>(std::clamp(normY, 0.0f, 1.0f))) * kPitchLengthYds);

    double dist = std::hypot(xYds, yYds);
    outDistYards = static_cast<float>(dist);

    // Exact visible angle subtended by the two goalposts from (xYds, yYds)
    double a1 = std::atan2(kGoalHalfWidthYds - xYds, yYds);
    double a2 = std::atan2(-kGoalHalfWidthYds - xYds, yYds);
    double angleRad = std::max(0.01, std::abs(a1 - a2));
    outAngleDeg = static_cast<float>(angleRad * 180.0 / kPi);

    double logit = -0.35 - 0.135 * dist + 2.35 * angleRad;
    if (isHeader)
        logit -= 0.55;
    if (isVolley)
        logit -= 0.45;
    if (isFreeKick)
        logit -= 0.25;
    if (rawZ > 12.0f)
        logit -= 0.20;

    double xg = 1.0 / (1.0 + std::exp(-logit));
    return std::clamp(xg, 0.01, 0.92);
}

double computePlayerMatchRating(
    int goals, int assists, int shotsOnTarget, int shotsOffTarget,
    int passesCompleted, int turnovers, int tacklesWon,
    int gkSaves, int goalsConcededByGk, bool isGk,
    int yellowCards, int redCards, bool wonMatch, bool lostMatch)
{
    double r = 6.0;
    r += static_cast<double>(goals) * 1.15;
    r += static_cast<double>(assists) * 0.65;
    r += static_cast<double>(shotsOnTarget) * 0.18;
    r -= static_cast<double>(shotsOffTarget) * 0.05;
    r += std::min(1.0, static_cast<double>(passesCompleted) * 0.03);
    r += std::min(1.1, static_cast<double>(tacklesWon) * 0.22);
    r -= std::min(1.2, static_cast<double>(turnovers) * 0.08);
    if (isGk) {
        r += std::min(2.2, static_cast<double>(gkSaves) * 0.35);
        r -= static_cast<double>(goalsConcededByGk) * 0.30;
        if (goalsConcededByGk == 0)
            r += 0.45;
    }
    r -= static_cast<double>(yellowCards) * 0.35;
    r -= static_cast<double>(redCards) * 1.25;
    if (wonMatch) r += 0.25;
    else if (lostMatch) r -= 0.20;

    return std::round(std::clamp(r, 3.0, 10.0) * 10.0) / 10.0;
}

bool exportMatchTelemetryJsonToPath(const std::string& fullPath)
{
    MatchTelemetrySnapshot snap = captureMatchTelemetrySnapshot();
    const auto stats = getStats();

    std::ofstream f(fullPath, std::ios::out | std::ios::trunc);
    if (!f)
        return false;

    std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &now);
    char isoBuf[32];
    std::snprintf(isoBuf, sizeof(isoBuf), "%04d-%02d-%02dT%02d:%02d:%02d",
        tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
        tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    char matchIdBuf[48];
    std::snprintf(matchIdBuf, sizeof(matchIdBuf), "match-%04d%02d%02d-%02d%02d%02d",
        tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
        tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);

    double totalXg1 = 0.0, totalXg2 = 0.0;
    for (const auto& sr : s_shots) {
        if (sr.teamNum == 1) totalXg1 += sr.xg;
        else if (sr.teamNum == 2) totalXg2 += sr.xg;
    }

    int passes1 = 0, passes2 = 0, tackles1 = 0, tackles2 = 0;
    for (int i = 0; i < kNumPlayersInTeam; ++i) {
        passes1 += s_playerAnalytics[0][i].passesCompleted;
        passes2 += s_playerAnalytics[1][i].passesCompleted;
        tackles1 += s_playerAnalytics[0][i].tacklesWon;
        tackles2 += s_playerAnalytics[1][i].tacklesWon;
    }

    bool t1Won = snap.playingPenalties ? (snap.penGoals1 > snap.penGoals2) : (snap.goals1 > snap.goals2);
    bool t2Won = snap.playingPenalties ? (snap.penGoals2 > snap.penGoals1) : (snap.goals2 > snap.goals1);

    // Pre-build player rows & find MOTM
    struct BuiltPlayerRow {
        int teamNum = 1;
        int ordinal = 1;
        int shirtNumber = 1;
        std::string team;
        std::string name;
        std::string pos;
        bool isStarter = true;
        int goals = 0;
        int assists = 0;
        int shots = 0;
        int shotsOnTarget = 0;
        double xg = 0.0;
        int touches = 0;
        int passesCompleted = 0;
        int turnovers = 0;
        int tacklesWon = 0;
        int yellowCards = 0;
        int redCards = 0;
        int gkSaves = 0;
        double avgX = 0.5;
        double avgY = 0.5;
        double rating = 6.0;
        const std::array<uint32_t, kTelemetryHeatmapCols * kTelemetryHeatmapRows> *heatmap = nullptr;
    };

    std::vector<BuiltPlayerRow> builtPlayers;
    auto collectTeamPlayers = [&](int teamNum, const TeamGame& tg, const std::string& teamName, int oppGoals, bool won, bool lost) {
        for (int i = 0; i < kNumPlayersInTeam; ++i) {
            const auto& pl = tg.players[i];
            const auto& pa = s_playerAnalytics[teamNum - 1][i];
            if (pl.shortName[0] == '\0')
                continue;
            bool participated = (i < kNumPlayersInLineup) || (pa.touches > 0) || (pa.posSamples > 0) ||
                                (pl.goalsScored > 0) || (pa.goals > 0);
            if (!participated)
                continue;

            BuiltPlayerRow row;
            row.teamNum = teamNum;
            row.ordinal = i + 1;
            row.shirtNumber = static_cast<int>(pl.shirtNumber);
            row.team = teamName;
            row.name = pl.shortName;
            row.pos = shortPosTag(pl.position);
            row.isStarter = (i < kNumPlayersInLineup);
            row.goals = std::max(pa.goals, static_cast<int>(pl.goalsScored));
            row.assists = pa.assists;
            row.shots = std::max(pa.shots, row.goals);
            row.shotsOnTarget = std::max(pa.shotsOnTarget, row.goals);
            row.xg = pa.xg;
            row.touches = pa.touches;
            row.passesCompleted = pa.passesCompleted;
            row.turnovers = pa.turnovers;
            row.tacklesWon = pa.tacklesWon;
            row.yellowCards = pa.yellowCards;
            row.redCards = pa.redCards;
            row.gkSaves = pa.gkSaves;
            row.avgX = (pa.posSamples > 0) ? (pa.sumNormX / static_cast<double>(pa.posSamples)) : 0.5;
            row.avgY = (pa.posSamples > 0) ? (pa.sumNormY / static_cast<double>(pa.posSamples)) : (i == 0 ? 0.06 : 0.5);
            row.heatmap = &pa.heatmap;
            bool isGk = (i == 0 || pl.position == PlayerPosition::kGoalkeeper);
            row.rating = computePlayerMatchRating(
                row.goals, row.assists, row.shotsOnTarget, std::max(0, row.shots - row.shotsOnTarget),
                row.passesCompleted, row.turnovers, row.tacklesWon,
                row.gkSaves, oppGoals, isGk, row.yellowCards, row.redCards, won, lost);
            builtPlayers.push_back(std::move(row));
        }
    };
    collectTeamPlayers(1, swos.topTeamInGame, snap.team1, snap.goals2, t1Won, t2Won);
    collectTeamPlayers(2, swos.bottomTeamInGame, snap.team2, snap.goals1, t2Won, t1Won);

    const BuiltPlayerRow *motm = nullptr;
    for (const auto& pr : builtPlayers) {
        if (!motm || pr.rating > motm->rating ||
            (pr.rating == motm->rating && (pr.goals + pr.assists) > (motm->goals + motm->assists))) {
            motm = &pr;
        }
    }

    f << "{\n";
    f << "  \"schemaVersion\": 2,\n";
    f << "  \"matchId\": \"" << matchIdBuf << "\",\n";
    f << "  \"timestamp\": \"" << isoBuf << "\",\n";
    f << "  \"competition\": \"" << escapeLlmJson(snap.competition) << "\",\n";
    f << "  \"pitch\": \"" << escapeLlmJson(snap.pitchCondition) << "\",\n";
    f << "  \"minute\": " << snap.minute << ",\n";
    f << "  \"half\": " << snap.half << ",\n";
    f << "  \"playingPenalties\": " << (snap.playingPenalties ? "true" : "false") << ",\n";
    f << "  \"pitchDimensions\": { \"leftX\": " << kLeftThrowInLine
      << ", \"rightX\": " << kRightThrowInLine
      << ", \"topY\": " << kTopPitchLine
      << ", \"bottomY\": " << kBottomPitchLine
      << ", \"goalLeftX\": " << kGoalLeft
      << ", \"goalRightX\": " << kGoalRight
      << ", \"penaltyBoxDepthNorm\": 0.136"
      << ", \"penaltyBoxLeftNorm\": 0.220"
      << ", \"penaltyBoxRightNorm\": 0.780"
      << ", \"heatmapCols\": " << kTelemetryHeatmapCols
      << ", \"heatmapRows\": " << kTelemetryHeatmapRows << " },\n";

    // Backward-compatible top-level summary fields + rich team objects
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

    f << "  \"teams\": {\n";
    f << "    \"team1\": { \"name\": \"" << escapeLlmJson(snap.team1)
      << "\", \"coach\": \"" << escapeLlmJson(snap.coach1)
      << "\", \"isHuman\": " << (snap.team1Human ? "true" : "false")
      << ", \"goals\": " << snap.goals1 << ", \"htGoals\": " << snap.htGoals1
      << ", \"penGoals\": " << snap.penGoals1 << ", \"xg\": " << fmtFloat(totalXg1, 2)
      << ", \"shots\": " << snap.shots1 << ", \"shotsOnTarget\": " << snap.onTarget1
      << ", \"possessionPct\": " << snap.possession1 << ", \"corners\": " << snap.corners1
      << ", \"fouls\": " << snap.fouls1 << ", \"yellowCards\": " << stats.team1.bookings
      << ", \"redCards\": " << stats.team1.sendingsOff << ", \"passesCompleted\": " << passes1
      << ", \"tacklesWon\": " << tackles1 << ", \"longRangeGoals\": " << snap.longRangeGoals1
      << ", \"headerGoals\": " << snap.headerGoals1 << ", \"attackZonePct\": " << snap.team1AttackZonePct << " },\n";
    f << "    \"team2\": { \"name\": \"" << escapeLlmJson(snap.team2)
      << "\", \"coach\": \"" << escapeLlmJson(snap.coach2)
      << "\", \"isHuman\": " << (snap.team2Human ? "true" : "false")
      << ", \"goals\": " << snap.goals2 << ", \"htGoals\": " << snap.htGoals2
      << ", \"penGoals\": " << snap.penGoals2 << ", \"xg\": " << fmtFloat(totalXg2, 2)
      << ", \"shots\": " << snap.shots2 << ", \"shotsOnTarget\": " << snap.onTarget2
      << ", \"possessionPct\": " << snap.possession2 << ", \"corners\": " << snap.corners2
      << ", \"fouls\": " << snap.fouls2 << ", \"yellowCards\": " << stats.team2.bookings
      << ", \"redCards\": " << stats.team2.sendingsOff << ", \"passesCompleted\": " << passes2
      << ", \"tacklesWon\": " << tackles2 << ", \"longRangeGoals\": " << snap.longRangeGoals2
      << ", \"headerGoals\": " << snap.headerGoals2 << ", \"attackZonePct\": " << snap.team2AttackZonePct << " }\n";
    f << "  },\n";

    // Optional Career Stadium & Charisma context
    if (swos.g_gameType == kGameTypeCareer) {
        const auto& st = getCurrentCareerStadium();
        int16_t cp = 50; uint16_t ints = 0; int64_t sb = 0; uint8_t lst = 0; std::string lq;
        getPressCareerTotals(cp, ints, sb, lst, lq);
        f << "  \"stadium\": { \"name\": \"" << escapeLlmJson(st.stadiumName)
          << "\", \"capacityLevel\": " << static_cast<int>(st.capacityLevel)
          << ", \"maxCapacity\": " << st.maxCapacity
          << ", \"pitchLevel\": " << static_cast<int>(st.pitchLevel)
          << ", \"pitchQuality\": \"" << getPitchQualityLabel(st.pitchLevel)
          << "\", \"trainingLevel\": " << static_cast<int>(st.trainingLevel)
          << ", \"medicalLevel\": " << static_cast<int>(st.medicalLevel)
          << ", \"commercialLevel\": " << static_cast<int>(st.commercialLevel)
          << ", \"ticketPolicy\": \"" << getTicketPolicyName(st.ticketPolicy)
          << "\", \"ticketPrice\": " << st.ticketPrice
          << ", \"coachCharisma\": " << static_cast<int>(cp) << " },\n";
    } else {
        f << "  \"stadium\": null,\n";
    }

    if (motm) {
        f << "  \"motm\": { \"teamNum\": " << motm->teamNum
          << ", \"team\": \"" << escapeLlmJson(motm->team)
          << "\", \"player\": \"" << escapeLlmJson(motm->name)
          << "\", \"shirtNumber\": " << motm->shirtNumber
          << ", \"pos\": \"" << motm->pos
          << "\", \"rating\": " << fmtFloat(motm->rating, 1)
          << ", \"goals\": " << motm->goals
          << ", \"assists\": " << motm->assists
          << ", \"xg\": " << fmtFloat(motm->xg, 2) << " },\n";
    } else {
        f << "  \"motm\": null,\n";
    }

    // Spatial heatmaps (16 rows x 12 cols)
    f << "  \"heatmaps\": {\n";
    f << "    \"ball\": "; writeHeatmapJson(f, s_ballHeatmap); f << ",\n";
    f << "    \"team1\": "; writeHeatmapJson(f, s_teamHeatmaps[0]); f << ",\n";
    f << "    \"team2\": "; writeHeatmapJson(f, s_teamHeatmaps[1]); f << "\n";
    f << "  },\n";

    // Shots with exact normalized & raw coordinates + xG
    f << "  \"shots\": [\n";
    for (size_t i = 0; i < s_shots.size(); ++i) {
        const auto& sr = s_shots[i];
        f << "    { \"minute\": " << sr.minute << ", \"half\": " << sr.half
          << ", \"teamNum\": " << sr.teamNum << ", \"team\": \"" << escapeLlmJson(sr.team)
          << "\", \"player\": \"" << escapeLlmJson(sr.player)
          << "\", \"shirtNumber\": " << sr.shirtNumber
          << ", \"assistedBy\": \"" << escapeLlmJson(sr.assistedBy)
          << "\", \"normX\": " << fmtFloat(sr.normX, 3) << ", \"normY\": " << fmtFloat(sr.normY, 3)
          << ", \"rawX\": " << fmtFloat(sr.rawX, 1) << ", \"rawY\": " << fmtFloat(sr.rawY, 1)
          << ", \"rawZ\": " << fmtFloat(sr.rawZ, 1)
          << ", \"distYards\": " << fmtFloat(sr.distYards, 1) << ", \"angleDeg\": " << fmtFloat(sr.angleDeg, 1)
          << ", \"bodyPart\": \"" << sr.bodyPart << "\", \"situation\": \"" << sr.situation
          << "\", \"outcome\": \"" << sr.outcome
          << "\", \"outsideBox\": " << (sr.outsideBox ? "true" : "false")
          << ", \"xg\": " << fmtFloat(sr.xg, 3) << " }"
          << (i + 1 < s_shots.size() ? "," : "") << "\n";
    }
    f << "  ],\n";

    // 5-minute attack momentum & xG race timeline
    int activeBuckets = std::clamp((std::max(snap.minute, 90) + 4) / 5, 18, kMaxTimelineBuckets);
    f << "  \"timeline\": [\n";
    for (int b = 0; b < activeBuckets; ++b) {
        const auto& tb = s_timeline[b];
        uint32_t totPoss = tb.possTicks1 + tb.possTicks2;
        int poss1 = (totPoss > 0) ? static_cast<int>((tb.possTicks1 * 100u) / totPoss) : 50;
        double rawMom = static_cast<double>(poss1 - 50) * 0.9 +
                        static_cast<double>(tb.finalThirdTouches1 - tb.finalThirdTouches2) * 6.0 +
                        (tb.xg1 - tb.xg2) * 55.0 +
                        static_cast<double>(tb.goals1 - tb.goals2) * 30.0;
        int momentum = std::clamp(static_cast<int>(std::lround(rawMom)), -100, 100);
        f << "    { \"fromMin\": " << (b * 5) << ", \"toMin\": " << ((b + 1) * 5)
          << ", \"poss1Pct\": " << poss1
          << ", \"shots1\": " << tb.shots1 << ", \"shots2\": " << tb.shots2
          << ", \"xg1\": " << fmtFloat(tb.xg1, 3) << ", \"xg2\": " << fmtFloat(tb.xg2, 3)
          << ", \"goals1\": " << tb.goals1 << ", \"goals2\": " << tb.goals2
          << ", \"finalThird1\": " << tb.finalThirdTouches1 << ", \"finalThird2\": " << tb.finalThirdTouches2
          << ", \"momentum\": " << momentum << " }"
          << (b + 1 < activeBuckets ? "," : "") << "\n";
    }
    f << "  ],\n";

    // Pass networks per team
    auto writePassNetwork = [&](int teamNum, const TeamGame& tg) {
        f << "[";
        bool first = true;
        for (const auto& kv : s_passLinks[teamNum - 1]) {
            int fromOrd = kv.first.first;
            int toOrd = kv.first.second;
            if (fromOrd < 1 || fromOrd > kNumPlayersInTeam || toOrd < 1 || toOrd > kNumPlayersInTeam)
                continue;
            const char *fromName = tg.players[fromOrd - 1].shortName;
            const char *toName = tg.players[toOrd - 1].shortName;
            if (!fromName[0] || !toName[0])
                continue;
            if (!first) f << ",";
            first = false;
            f << "{\"from\":\"" << escapeLlmJson(fromName) << "\",\"fromShirt\":" << static_cast<int>(tg.players[fromOrd - 1].shirtNumber)
              << ",\"to\":\"" << escapeLlmJson(toName) << "\",\"toShirt\":" << static_cast<int>(tg.players[toOrd - 1].shirtNumber)
              << ",\"count\":" << kv.second << "}";
        }
        f << "]";
    };
    f << "  \"passNetworks\": {\n";
    f << "    \"team1\": "; writePassNetwork(1, swos.topTeamInGame); f << ",\n";
    f << "    \"team2\": "; writePassNetwork(2, swos.bottomTeamInGame); f << "\n";
    f << "  },\n";

    // Individual player stats, ratings, average position & personal heatmaps
    f << "  \"players\": [\n";
    for (size_t i = 0; i < builtPlayers.size(); ++i) {
        const auto& pr = builtPlayers[i];
        f << "    { \"teamNum\": " << pr.teamNum << ", \"team\": \"" << escapeLlmJson(pr.team)
          << "\", \"name\": \"" << escapeLlmJson(pr.name) << "\", \"shirtNumber\": " << pr.shirtNumber
          << ", \"pos\": \"" << pr.pos << "\", \"isStarter\": " << (pr.isStarter ? "true" : "false")
          << ", \"goals\": " << pr.goals << ", \"assists\": " << pr.assists
          << ", \"shots\": " << pr.shots << ", \"shotsOnTarget\": " << pr.shotsOnTarget
          << ", \"xg\": " << fmtFloat(pr.xg, 3) << ", \"touches\": " << pr.touches
          << ", \"passesCompleted\": " << pr.passesCompleted << ", \"turnovers\": " << pr.turnovers
          << ", \"tacklesWon\": " << pr.tacklesWon << ", \"yellowCards\": " << pr.yellowCards
          << ", \"redCards\": " << pr.redCards << ", \"gkSaves\": " << pr.gkSaves
          << ", \"avgX\": " << fmtFloat(pr.avgX, 3) << ", \"avgY\": " << fmtFloat(pr.avgY, 3)
          << ", \"rating\": " << fmtFloat(pr.rating, 1) << ", \"heatmap\": ";
        if (pr.heatmap)
            writeHeatmapJson(f, *pr.heatmap);
        else
            f << "[]";
        f << " }" << (i + 1 < builtPlayers.size() ? "," : "") << "\n";
    }
    f << "  ],\n";

    // Full timestamped match event feed
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
    return f.good();
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
    if (exportMatchTelemetryJsonToPath(fullPath))
        logInfo("Exported Telemetry V2 JSON to %s", fullPath.c_str());
}
