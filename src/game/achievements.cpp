#include "achievements.h"
#include "compactFont.h"
#include "controls.h"
#include "darkRectangle.h"
#include "drawMenu.h"
#include "file.h"
#include "gameFieldMapping.h"
#include "log.h"
#include "menuBackground.h"
#include "menus.h"
#include "render.h"
#include "sfx.h"
#include "swos.h"
#include "achievementsMenu.mnu.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace AchievementsMenu;

namespace {

constexpr uint32_t kProfileMagic = 0x53574F53u; // 'SWOS'
constexpr uint16_t kProfileVersion = 2;         // V2 adds transfer purchase price map + consecutive promotion streak
constexpr const char *kDefaultProfileFilename = "swos_achievements.dat";
constexpr size_t kToastQueueMax = 8;
constexpr Uint32 kToastDurationMs = 3500;

static const std::array<AchievementDef, kAchievementCount> kCatalog = {{
    // Category A: First Steps & Match Basics (01..08)
    { AchievementId::kFirstKickoff,       "ACH_FIRST_KICKOFF",       "Jumpers for Goalposts",  "Complete your very first match in any game mode.",                    AchievementCategory::kBasics, 10, 1, false },
    { AchievementId::kFriendlyWin,        "ACH_FRIENDLY_WIN",        "Warm-Up Act",            "Win a Friendly match.",                                               AchievementCategory::kBasics, 10, 1, false },
    { AchievementId::kFirstGoal,          "ACH_FIRST_GOAL",          "Off the Mark",           "Score your first goal in any match.",                                 AchievementCategory::kBasics, 10, 1, false },
    { AchievementId::kCleanSheet,         "ACH_CLEAN_SHEET",         "Shut the Door",          "Win a match without conceding a single goal.",                        AchievementCategory::kBasics, 10, 1, false },
    { AchievementId::kPenaltyShootoutWin, "ACH_PENALTY_SHOOTOUT_WIN","Nerves of Steel",        "Win a match via a Penalty Shootout.",                                 AchievementCategory::kBasics, 20, 1, false },
    { AchievementId::kExtraTimeWinner,    "ACH_EXTRA_TIME_WINNER",   "After Hours",            "Score a winning goal in Extra Time.",                                 AchievementCategory::kBasics, 20, 1, false },
    { AchievementId::kVeteran50Matches,   "ACH_VETERAN_50_MATCHES",  "Seasoned Gaffer",        "Play 50 matches across any game mode.",                               AchievementCategory::kBasics, 20, 50, false },
    { AchievementId::kCenturion100Goals,  "ACH_CENTURION_100_GOALS", "Century Club",           "Score 100 cumulative goals across all matches.",                      AchievementCategory::kBasics, 20, 100, false },

    // Category B: Pitch Craft & Goal Types (09..21)
    { AchievementId::kPenaltyGoal,        "ACH_PENALTY_GOAL",        "Spot On",                "Score from a penalty kick during regular or extra time.",             AchievementCategory::kPitchCraft, 10, 1, false },
    { AchievementId::kHeaderGoal,         "ACH_HEADER_GOAL",         "Bullet Header",          "Score a goal with a header from a cross or corner.",                  AchievementCategory::kPitchCraft, 20, 1, false },
    { AchievementId::kOutsideBoxGoal,     "ACH_OUTSIDE_BOX_GOAL",    "Screamer!",              "Score a goal from outside the 18-yard penalty box.",                  AchievementCategory::kPitchCraft, 20, 1, false },
    { AchievementId::kFreeKickGoal,       "ACH_FREE_KICK_GOAL",      "Dead-Ball Specialist",   "Score directly from a free kick.",                                    AchievementCategory::kPitchCraft, 30, 1, false },
    { AchievementId::kVolleyOrOverhead,   "ACH_VOLLEY_OR_OVERHEAD",  "Worldie",                "Score with a volley or bicycle/overhead kick.",                       AchievementCategory::kPitchCraft, 30, 1, false },
    { AchievementId::kCornerDirectOrQuick,"ACH_CORNER_DIRECT_OR_QUICK","Training Ground Routine","Score within 3 seconds of taking a corner kick.",                    AchievementCategory::kPitchCraft, 20, 1, false },
    { AchievementId::kHatTrick,           "ACH_HAT_TRICK",           "Match Ball",             "Score a hat-trick (3+ goals) with a single player in one match.",     AchievementCategory::kPitchCraft, 30, 1, false },
    { AchievementId::kDoubleHatTrick,     "ACH_DOUBLE_HAT_TRICK",    "Unstoppable Force",      "Score 6 goals with a single player in a single match.",               AchievementCategory::kPitchCraft, 50, 1, false },
    { AchievementId::kGoalkeeperGoal,     "ACH_GOALKEEPER_GOAL",     "Up for the Corner",      "Score a goal with your goalkeeper in open play or a set piece.",      AchievementCategory::kPitchCraft, 50, 1, false },
    { AchievementId::kEarlyBlitz,         "ACH_EARLY_BLITZ",         "Straight Out the Tunnel","Score a goal within the first 2 in-game minutes.",                     AchievementCategory::kPitchCraft, 20, 1, false },
    { AchievementId::kFergieTime,         "ACH_FERGIE_TIME",         "90th-Minute Drama",      "Score a winning goal in the 89th minute or later.",                   AchievementCategory::kPitchCraft, 30, 1, false },
    { AchievementId::kNoCardsWin,         "ACH_NO_CARDS_WIN",        "Fair Play Award",        "Win a competitive match with 0 yellow and 0 red cards.",              AchievementCategory::kPitchCraft, 10, 1, false },
    { AchievementId::kDownToTenWin,       "ACH_DOWN_TO_TEN_WIN",     "Against All Odds",       "Win a match after receiving a red card before the 60th minute.",      AchievementCategory::kPitchCraft, 30, 1, false },

    // Category C: Comebacks & Dominance (22..28)
    { AchievementId::kHalftimeComeback,   "ACH_HALFTIME_COMEBACK",   "Hairdryer Treatment",    "Win a match after trailing at half-time.",                            AchievementCategory::kComebacks, 20, 1, false },
    { AchievementId::kTwoGoalComeback,    "ACH_TWO_GOAL_COMEBACK",   "Great Escape",           "Win a match after trailing by 2 or more goals.",                      AchievementCategory::kComebacks, 30, 1, false },
    { AchievementId::kThreeGoalComeback,  "ACH_THREE_GOAL_COMEBACK", "Miracle of Istanbul",    "Win a match after trailing by 3 or more goals.",                      AchievementCategory::kComebacks, 50, 1, false },
    { AchievementId::kFiveGoalRout,       "ACH_FIVE_GOAL_ROUT",      "Five-Star Performance",  "Win a competitive match by a margin of 5 or more goals.",             AchievementCategory::kComebacks, 20, 1, false },
    { AchievementId::kDoubleDigits,       "ACH_DOUBLE_DIGITS",       "Total Football",         "Score 10 or more goals in a single competitive match.",               AchievementCategory::kComebacks, 50, 1, false },
    { AchievementId::kPossessionMaster,   "ACH_POSSESSION_MASTER",   "Tiki-Taka",              "Win a match while holding 65% or more ball possession.",              AchievementCategory::kComebacks, 20, 1, false },
    { AchievementId::kClinicalFinishing,  "ACH_CLINICAL_FINISHING",  "One Shot, One Kill",     "Win while scoring on 75%+ of shots on target (min. 3 goals).",        AchievementCategory::kComebacks, 30, 1, false },

    // Category D: Cup Runs & Giant Killing (29..34)
    { AchievementId::kGiantKiller1Tier,   "ACH_GIANT_KILLER_1_TIER", "Cup Upset",              "Eliminate a higher-division club in a domestic cup match.",           AchievementCategory::kCupGlory, 20, 1, false },
    { AchievementId::kGiantKiller2Tiers,  "ACH_GIANT_KILLER_2_TIERS","David vs. Goliath",      "Eliminate a club at least 2 divisions higher in a cup match.",        AchievementCategory::kCupGlory, 30, 1, false },
    { AchievementId::kGiantKiller3Tiers,  "ACH_GIANT_KILLER_3_TIERS","Non-League Legend",      "As a 4th-tier club, defeat a Top-Flight club in a cup tie.",          AchievementCategory::kCupGlory, 50, 1, false },
    { AchievementId::kDomesticCupWin,     "ACH_DOMESTIC_CUP_WIN",    "Wembley Glory",          "Win your nation's primary Domestic Cup competition.",                 AchievementCategory::kCupGlory, 50, 1, false },
    { AchievementId::kEuropeanCupWin,     "ACH_EUROPEAN_CUP_WIN",    "Kings of Europe",        "Win the European Champions Cup / continental top-tier trophy.",       AchievementCategory::kCupGlory, 100, 1, false },
    { AchievementId::kWorldCupWin,        "ACH_WORLD_CUP_WIN",       "World Champions",        "Win the World Cup managing a national team.",                         AchievementCategory::kCupGlory, 100, 1, false },

    // Category E: Career Mode Progression & Glory (35..42)
    { AchievementId::kCareerFirstWin,     "ACH_CAREER_FIRST_WIN",    "Three Points on Board",  "Win your first league match in Career Mode.",                         AchievementCategory::kCareer, 10, 1, false },
    { AchievementId::kPromotion,          "ACH_PROMOTION",           "Going Up!",              "Achieve promotion to a higher division in Career Mode.",              AchievementCategory::kCareer, 30, 1, false },
    { AchievementId::kBackToBackPromotion,"ACH_BACK_TO_BACK_PROMOTION","Unstoppable Rise",     "Achieve consecutive promotions in back-to-back Career seasons.",      AchievementCategory::kCareer, 50, 1, false },
    { AchievementId::kLeagueChampion,     "ACH_LEAGUE_CHAMPION",     "Champions!",             "Win the top-flight league title in Career Mode.",                     AchievementCategory::kCareer, 50, 1, false },
    { AchievementId::kUnbeatenSeason,     "ACH_UNBEATEN_SEASON",     "The Invincibles",        "Complete an entire Career Mode league season with 0 losses.",         AchievementCategory::kCareer, 100, 1, false },
    { AchievementId::kContinentalTreble,  "ACH_CONTINENTAL_TREBLE",  "The Treble",             "Win Top-Flight League, Domestic Cup, and Champions Cup in 1 season.", AchievementCategory::kCareer, 100, 1, false },
    { AchievementId::kInternationalCallup,"ACH_INTERNATIONAL_CALLUP","National Duty",          "Receive and accept an offer to manage a National Team in Career.",    AchievementCategory::kCareer, 50, 1, false },
    { AchievementId::kCareerTenSeasons,   "ACH_CAREER_TEN_SEASONS",  "Dynasty Builder",        "Complete 10 full seasons in a single Career Mode save.",              AchievementCategory::kCareer, 50, 10, false },

    // Category F: Transfers, Stadium, Press & Challenges (43..50)
    { AchievementId::kFirstSigning,       "ACH_FIRST_SIGNING",       "Welcome to the Club",    "Sign a player from the transfer market in Career Mode.",              AchievementCategory::kManagement, 10, 1, false },
    { AchievementId::kMillionPoundSigning,"ACH_MILLION_POUND_SIGNING","Galactico",             "Sign a single player for GBP 1,000,000 or more.",                     AchievementCategory::kManagement, 20, 1, false },
    { AchievementId::kProfitableFlip,     "ACH_PROFITABLE_FLIP",     "Wheeler Dealer",         "Sell a player for at least GBP 1,000,000 more than you bought him.",  AchievementCategory::kManagement, 30, 1, false },
    { AchievementId::kStadiumFirstUpgrade,"ACH_STADIUM_FIRST_UPGRADE","Breaking Ground",       "Complete your first Stadium or Facility upgrade.",                    AchievementCategory::kManagement, 10, 1, false },
    { AchievementId::kFortress85k,        "ACH_FORTRESS_85K",        "Theatre of Dreams",      "Expand Stadium Capacity to Level 10 (85,000 seats) and sell out.",    AchievementCategory::kManagement, 50, 1, false },
    { AchievementId::kMaxFacilities,      "ACH_MAX_FACILITIES",      "Five-Star Complex",      "Upgrade Pitch, Training, Medical, and Commercial all to Level 5.",    AchievementCategory::kManagement, 50, 1, false },
    { AchievementId::kIconicCharisma,     "ACH_ICONIC_CHARISMA",     "Box Office Boss",        "Reach 90+ Coach Charisma Points (Iconic tier) in Career Mode.",       AchievementCategory::kManagement, 30, 1, false },
    { AchievementId::kSponsorStreak,      "ACH_SPONSOR_STREAK",      "Sponsor's Darling",      "Complete 10 Pre-Match Sponsor Challenges in Career Mode.",            AchievementCategory::kManagement, 30, 10, false },
}};

struct ToastItem {
    AchievementId id;
    Uint32 startTick = 0;
    bool active = false;
};

std::mutex s_achMutex;
GlobalAchievementProfile s_profile;
bool s_loaded = false;
uint32_t s_consecutivePromotions = 0;
std::unordered_map<std::string, int32_t> s_playerBuyFees; // upper player name -> purchase fee in GBP
std::deque<AchievementId> s_toastQueue;
ToastItem s_activeToast;

// Browser menu UI state
int s_browserCategory = -1; // -1 = ALL, 0..5 = AchievementCategory
int s_browserScroll = 0;
constexpr int kRowsPerPage = 5;
bool s_prevNavKeys[5] = { false, false, false, false, false }; // Left, Right, Up, Down, Esc

std::string normalizePlayerKey(const std::string& name)
{
    std::string out = sanitizeCompactMixedCase(name);
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string resolveProfilePath(const std::string& customPath)
{
    if (!customPath.empty())
        return customPath;
    return pathInRootDir(kDefaultProfileFilename);
}

void recomputeProfileTotalsLocked()
{
    uint32_t pts = 0;
    uint32_t cnt = 0;
    for (uint16_t i = 0; i < kAchievementCount; ++i) {
        if (s_profile.records[i].unlocked) {
            pts += kCatalog[i].points;
            ++cnt;
        }
    }
    s_profile.totalPoints = pts;
    s_profile.unlockedCount = cnt;
}

void playUnlockChime()
{
#ifndef SWOS_TEST
    // Subtle crisp celebration cue using existing SWOS audio sample
    playSfx(kWhistle, 72);
#endif
}

template <typename T>
bool writePod(std::ofstream& out, const T& v)
{
    out.write(reinterpret_cast<const char *>(&v), sizeof(T));
    return out.good();
}

template <typename T>
bool readPod(std::ifstream& in, T& v)
{
    in.read(reinterpret_cast<char *>(&v), sizeof(T));
    return in.good();
}

bool saveAchievementsLocked(const std::string& customPath)
{
#ifdef SWOS_TEST
    if (customPath.empty())
        return true;
#endif
    std::string dstPath = resolveProfilePath(customPath);
    std::string tmpPath = dstPath + ".tmp";

    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        logWarn("Achievements: failed to open %s for writing", tmpPath.c_str());
        return false;
    }

    s_profile.magic = kProfileMagic;
    s_profile.version = kProfileVersion;
    recomputeProfileTotalsLocked();

    if (!writePod(out, s_profile.magic) ||
        !writePod(out, s_profile.version) ||
        !writePod(out, s_profile.totalPoints) ||
        !writePod(out, s_profile.unlockedCount) ||
        !writePod(out, s_profile.totalMatchesPlayed) ||
        !writePod(out, s_profile.totalGoalsScored) ||
        !writePod(out, s_profile.sponsorChallengesCompleted)) {
        return false;
    }

    for (uint16_t i = 0; i < kAchievementCount; ++i) {
        uint8_t u = s_profile.records[i].unlocked ? 1 : 0;
        if (!writePod(out, u) ||
            !writePod(out, s_profile.records[i].currentProgress) ||
            !writePod(out, s_profile.records[i].unlockTimestamp)) {
            return false;
        }
    }

    // V2 extra tracking: consecutive promotions + player purchase fees (capped at 128)
    if (!writePod(out, s_consecutivePromotions))
        return false;
    uint32_t feeCount = static_cast<uint32_t>(std::min<size_t>(s_playerBuyFees.size(), 128));
    if (!writePod(out, feeCount))
        return false;
    uint32_t written = 0;
    for (const auto& kv : s_playerBuyFees) {
        if (written++ >= feeCount)
            break;
        uint16_t nLen = static_cast<uint16_t>(std::min<size_t>(kv.first.size(), 48));
        if (!writePod(out, nLen))
            return false;
        if (nLen > 0)
            out.write(kv.first.data(), nLen);
        if (!writePod(out, kv.second))
            return false;
    }

    out.flush();
    bool ok = out.good();
    out.close();
    if (!ok) {
        std::remove(tmpPath.c_str());
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExA(tmpPath.c_str(), dstPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::remove(dstPath.c_str());
        if (std::rename(tmpPath.c_str(), dstPath.c_str()) != 0) {
            std::remove(tmpPath.c_str());
            return false;
        }
    }
#else
    if (std::rename(tmpPath.c_str(), dstPath.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        return false;
    }
#endif
    return true;
}

bool unlockAchievementLocked(AchievementId id, bool autoSave, const std::string& customPath = {})
{
    uint16_t idx = static_cast<uint16_t>(id);
    if (idx >= kAchievementCount)
        return false;

    auto& rec = s_profile.records[idx];
    if (rec.unlocked)
        return false;

    rec.unlocked = true;
    rec.currentProgress = std::max(rec.currentProgress, kCatalog[idx].targetProgress);
    rec.unlockTimestamp = static_cast<uint64_t>(std::time(nullptr));
    recomputeProfileTotalsLocked();

    if (s_toastQueue.size() < kToastQueueMax)
        s_toastQueue.push_back(id);

    logInfo("ACHIEVEMENT UNLOCKED: [%s] %s (+%u PTS) [Total: %u/%u, %u PTS]",
        kCatalog[idx].enumName, kCatalog[idx].title,
        static_cast<unsigned>(kCatalog[idx].points),
        static_cast<unsigned>(s_profile.unlockedCount),
        static_cast<unsigned>(kAchievementCount),
        static_cast<unsigned>(s_profile.totalPoints));

    if (autoSave)
        saveAchievementsLocked(customPath);
    return true;
}

SDL_Color pointsTierColor(uint16_t pts)
{
    if (pts >= 100) return { 112, 240, 255, 255 }; // Legend cyan-diamond
    if (pts >= 50)  return { 216, 172, 255, 255 }; // Platinum violet
    if (pts >= 30)  return { 255, 218, 76, 255 };  // Gold
    if (pts >= 20)  return { 212, 224, 236, 255 }; // Silver
    return { 226, 158, 102, 255 };                 // Bronze
}

void drawPixelTrophyIcon(SDL_Renderer *r, float x, float y, float px, Uint8 alpha)
{
    auto pxRect = [&](float ux, float uy, float uw, float uh, Uint8 cr, Uint8 cg, Uint8 cb) {
        SDL_SetRenderDrawColor(r, cr, cg, cb, alpha);
        SDL_FRect rc{ x + ux * px, y + uy * px, uw * px, uh * px };
        SDL_RenderFillRectF(r, &rc);
    };
    // Classic 8x8 gold chalice trophy
    pxRect(1, 0, 6, 4, 255, 216, 64);  // cup body
    pxRect(2, 1, 2, 2, 255, 245, 170); // shine
    pxRect(0, 1, 1, 2, 230, 178, 36);  // left handle
    pxRect(7, 1, 1, 2, 230, 178, 36);  // right handle
    pxRect(2, 4, 4, 1, 230, 178, 36);  // cup taper
    pxRect(3, 5, 2, 2, 210, 156, 28);  // stem
    pxRect(1, 7, 6, 1, 245, 198, 48);  // base
}

std::vector<uint16_t> getFilteredAchievementIndices()
{
    std::vector<uint16_t> out;
    out.reserve(kAchievementCount);
    for (uint16_t i = 0; i < kAchievementCount; ++i) {
        if (s_browserCategory < 0 || static_cast<int>(kCatalog[i].category) == s_browserCategory)
            out.push_back(i);
    }
    return out;
}

void clampBrowserScroll()
{
    auto list = getFilteredAchievementIndices();
    int maxScroll = std::max(0, static_cast<int>(list.size()) - kRowsPerPage);
    s_browserScroll = std::clamp(s_browserScroll, 0, maxScroll);
}

} // namespace

// --- Native Main Menu Achievements Browser Callbacks ---

static void initAchievementsMenu()
{
    s_browserCategory = -1;
    s_browserScroll = 0;
    std::memset(s_prevNavKeys, 0, sizeof(s_prevNavKeys));
    setCurrentEntry(backBtn);
}

static void achievementsPrevCategory()
{
    s_browserCategory = (s_browserCategory <= -1)
        ? (static_cast<int>(AchievementCategory::kCount) - 1)
        : (s_browserCategory - 1);
    s_browserScroll = 0;
}

static void achievementsNextCategory()
{
    s_browserCategory = (s_browserCategory >= static_cast<int>(AchievementCategory::kCount) - 1)
        ? -1
        : (s_browserCategory + 1);
    s_browserScroll = 0;
}

static void achievementsPageUp()
{
    s_browserScroll = std::max(0, s_browserScroll - kRowsPerPage);
}

static void achievementsPageDown()
{
    s_browserScroll += kRowsPerPage;
    clampBrowserScroll();
}

static void achievementsCloseMenu()
{
    SetExitMenuFlag();
}

static void drawAchievementsMenu()
{
    // Wide dark transparent panels spanning almost the full 320 VGA width:
    // Summary bar (y=16..31), Category bar (y=34..47), Main list panel (y=50..179)
    drawDarkRectangle({ 4.0f, 16.0f, 312.0f, 15.0f }, true);
    drawDarkRectangle({ 58.0f, 34.0f, 204.0f, 13.0f }, true);
    drawDarkRectangle({ 4.0f, 50.0f, 312.0f, 129.0f }, true);

    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = getCompactFontAtlas(r);
    if (r && atlas) {
        std::lock_guard<std::mutex> lock(s_achMutex);
        auto filtered = getFilteredAchievementIndices();
        int maxScroll = std::max(0, static_cast<int>(filtered.size()) - kRowsPerPage);
        s_browserScroll = std::clamp(s_browserScroll, 0, maxScroll);

        float gameScale = getGameScale();
        float px = computeCompactPixelScaleForVga(gameScale, 0.44f);
        float offX = getGameScreenOffsetX();
        float offY = getGameScreenOffsetY();

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_RenderSetScale(r, 1.0f, 1.0f);

        SDL_Color whiteCol{ 244, 246, 248, 255 };
        SDL_Color mutedCol{ 156, 166, 174, 255 };
        SDL_Color yellowCol{ 255, 226, 78, 255 };
        SDL_Color greenCol{ 96, 236, 134, 255 };
        SDL_Color cyanCol{ 118, 236, 246, 255 };

        auto fitScaleForWidth = [](const std::string& s, float basePx, float maxPixelW) -> float {
            int units = measureCompactTextUnits(s);
            if (units <= 0 || maxPixelW <= 1.0f)
                return basePx;
            float needed = static_cast<float>(units) * basePx;
            if (needed <= maxPixelW)
                return basePx;
            return std::max(0.75f, basePx * (maxPixelW / needed));
        };

        // Top summary strip
        uint32_t maxPts = getTotalPossibleAchievementPoints();
        char leftSum[64];
        std::snprintf(leftSum, sizeof(leftSum), "UNLOCKED: %u / %u",
            static_cast<unsigned>(s_profile.unlockedCount), static_cast<unsigned>(kAchievementCount));
        drawCompactText(r, atlas, offX + 10.0f * gameScale, offY + 20.0f * gameScale, px,
            leftSum, greenCol, 255, true);

        char rightSum[64];
        std::snprintf(rightSum, sizeof(rightSum), "GAMERSCORE: %u / %u PTS",
            static_cast<unsigned>(s_profile.totalPoints), static_cast<unsigned>(maxPts));
        drawCompactTextRightAligned(r, atlas, offX + 310.0f * gameScale, offY + 20.0f * gameScale, px,
            rightSum, yellowCol, 255, true);

        // Category filter label
        std::string catLabel = (s_browserCategory < 0)
            ? "CATEGORY: ALL (50)"
            : (std::string("CATEGORY: ") + getAchievementCategoryName(static_cast<AchievementCategory>(s_browserCategory)) +
               " (" + std::to_string(filtered.size()) + ")");
        float catPx = fitScaleForWidth(catLabel, px, 196.0f * gameScale);
        drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 37.5f * gameScale, catPx,
            catLabel, cyanCol, 255, true);

        // Render visible achievement rows (5 per page, 24 VGA units each)
        float rowStartY = 53.0f;
        float rowStride = 24.5f;
        float maxDescPixelW = 290.0f * gameScale;
        for (int row = 0; row < kRowsPerPage; ++row) {
            size_t listIdx = static_cast<size_t>(s_browserScroll + row);
            if (listIdx >= filtered.size())
                break;

            uint16_t achIdx = filtered[listIdx];
            const auto& def = kCatalog[achIdx];
            const auto& rec = s_profile.records[achIdx];

            float ry = offY + (rowStartY + row * rowStride) * gameScale;
            float rx = offX + 10.0f * gameScale;

            // Row separator line
            if (row > 0) {
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(r, 140, 152, 160, 65);
                SDL_RenderDrawLineF(r, offX + 8.0f * gameScale, ry - 2.0f * gameScale,
                    offX + 312.0f * gameScale, ry - 2.0f * gameScale);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            }

            // Status badge + Title
            const char *badge = rec.unlocked ? "[OK] " : ((def.targetProgress > 1 && rec.currentProgress > 0) ? "[..] " : "[--] ");
            SDL_Color badgeCol = rec.unlocked ? greenCol : ((def.targetProgress > 1 && rec.currentProgress > 0) ? cyanCol : mutedCol);
            SDL_Color titleCol = rec.unlocked ? yellowCol : whiteCol;

            float afterBadge = drawCompactText(r, atlas, rx, ry, px, badge, badgeCol, 255, true);
            std::string titleStr = def.title;
            if (!rec.unlocked && def.targetProgress > 1) {
                titleStr += " (" + std::to_string(std::min(rec.currentProgress, def.targetProgress)) +
                            "/" + std::to_string(def.targetProgress) + ")";
            }
            drawCompactText(r, atlas, afterBadge, ry, px, titleStr, titleCol, rec.unlocked ? 255 : 225, rec.unlocked);

            // Points badge right-aligned
            std::string ptsStr = "[" + std::to_string(def.points) + " PTS]";
            drawCompactTextRightAligned(r, atlas, offX + 310.0f * gameScale, ry, px,
                ptsStr, pointsTierColor(def.points), rec.unlocked ? 255 : 180, rec.unlocked);

            // Description line below (auto-fitted so it never overflows the dark panel)
            float descPx = fitScaleForWidth(def.description, px, maxDescPixelW);
            drawCompactText(r, atlas, rx + 8.0f * gameScale, ry + 10.5f * gameScale, descPx,
                def.description, rec.unlocked ? whiteCol : mutedCol, rec.unlocked ? 235 : 200, false);
        }

        // Scroll indicator in bottom of list box
        if (!filtered.empty()) {
            int firstShown = s_browserScroll + 1;
            int lastShown = std::min<int>(static_cast<int>(filtered.size()), s_browserScroll + kRowsPerPage);
            char rangeBuf[64];
            std::snprintf(rangeBuf, sizeof(rangeBuf), "Showing %d-%d of %zu  |  Arrows: Browse  ESC: Back",
                firstShown, lastShown, filtered.size());
            float footPx = fitScaleForWidth(rangeBuf, px * 0.88f, 300.0f * gameScale);
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 172.0f * gameScale, footPx,
                rangeBuf, mutedCol, 220, false);
        }

        SDL_RenderSetScale(r, origSX, origSY);
    }

    // Keyboard shortcuts: Left/Right switch category, Up/Down scroll 1 row, ESC closes
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        bool down[5] = {
            keys[SDL_SCANCODE_LEFT] != 0,
            keys[SDL_SCANCODE_RIGHT] != 0,
            keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_PAGEUP],
            keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_PAGEDOWN],
            keys[SDL_SCANCODE_ESCAPE] != 0,
        };
        if (down[0] && !s_prevNavKeys[0]) achievementsPrevCategory();
        if (down[1] && !s_prevNavKeys[1]) achievementsNextCategory();
        if (down[2] && !s_prevNavKeys[2]) { s_browserScroll = std::max(0, s_browserScroll - 1); }
        if (down[3] && !s_prevNavKeys[3]) { ++s_browserScroll; clampBrowserScroll(); }
        if (down[4] && !s_prevNavKeys[4]) SetExitMenuFlag();
        for (int i = 0; i < 5; ++i)
            s_prevNavKeys[i] = down[i];
    }
}

// --- Public API ---

const AchievementDef& getAchievementDef(AchievementId id)
{
    uint16_t idx = static_cast<uint16_t>(id);
    if (idx >= kAchievementCount)
        idx = 0;
    return kCatalog[idx];
}

uint32_t getTotalPossibleAchievementPoints()
{
    uint32_t sum = 0;
    for (const auto& def : kCatalog)
        sum += def.points;
    return sum;
}

const char *getAchievementCategoryName(AchievementCategory cat)
{
    switch (cat) {
    case AchievementCategory::kBasics:     return "BASICS";
    case AchievementCategory::kPitchCraft: return "PITCH CRAFT";
    case AchievementCategory::kComebacks:  return "COMEBACKS";
    case AchievementCategory::kCupGlory:   return "CUP GLORY";
    case AchievementCategory::kCareer:     return "CAREER";
    case AchievementCategory::kManagement: return "MANAGEMENT";
    default:                               return "ALL";
    }
}

const GlobalAchievementProfile& getAchievementProfile()
{
    return s_profile;
}

void initAchievements()
{
    loadAchievements();
}

void resetAchievementsProfileForTest()
{
    std::lock_guard<std::mutex> lock(s_achMutex);
    s_profile = GlobalAchievementProfile{};
    s_consecutivePromotions = 0;
    s_playerBuyFees.clear();
    s_toastQueue.clear();
    s_activeToast = ToastItem{};
    s_loaded = true;
}

bool loadAchievements(const std::string& customPath)
{
    std::lock_guard<std::mutex> lock(s_achMutex);
    s_profile = GlobalAchievementProfile{};
    s_consecutivePromotions = 0;
    s_playerBuyFees.clear();
    s_loaded = true;

    std::string path = resolveProfilePath(customPath);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    uint32_t magic = 0;
    uint16_t version = 0;
    if (!readPod(in, magic) || magic != kProfileMagic ||
        !readPod(in, version) || version == 0 || version > kProfileVersion) {
        logWarn("Achievements: invalid or unsupported profile header in %s", path.c_str());
        s_profile = GlobalAchievementProfile{};
        return false;
    }

    s_profile.magic = magic;
    s_profile.version = version;

    if (!readPod(in, s_profile.totalPoints) ||
        !readPod(in, s_profile.unlockedCount) ||
        !readPod(in, s_profile.totalMatchesPlayed) ||
        !readPod(in, s_profile.totalGoalsScored) ||
        !readPod(in, s_profile.sponsorChallengesCompleted)) {
        s_profile = GlobalAchievementProfile{};
        return false;
    }

    for (uint16_t i = 0; i < kAchievementCount; ++i) {
        uint8_t u = 0;
        if (!readPod(in, u) ||
            !readPod(in, s_profile.records[i].currentProgress) ||
            !readPod(in, s_profile.records[i].unlockTimestamp)) {
            s_profile = GlobalAchievementProfile{};
            return false;
        }
        s_profile.records[i].unlocked = (u != 0);
    }

    if (version >= 2) {
        uint32_t feeCount = 0;
        if (readPod(in, s_consecutivePromotions) && readPod(in, feeCount) && feeCount <= 128) {
            for (uint32_t i = 0; i < feeCount; ++i) {
                uint16_t nLen = 0;
                if (!readPod(in, nLen) || nLen > 48)
                    break;
                std::string name(nLen, '\0');
                if (nLen > 0 && !in.read(name.data(), nLen))
                    break;
                int32_t fee = 0;
                if (!readPod(in, fee))
                    break;
                if (!name.empty())
                    s_playerBuyFees[std::move(name)] = fee;
            }
        }
    }

    recomputeProfileTotalsLocked();
    logInfo("Loaded global achievements from %s (%u/%u unlocked, %u PTS)",
        path.c_str(), static_cast<unsigned>(s_profile.unlockedCount),
        static_cast<unsigned>(kAchievementCount), static_cast<unsigned>(s_profile.totalPoints));
    return true;
}

bool saveAchievements(const std::string& customPath)
{
    std::lock_guard<std::mutex> lock(s_achMutex);
    return saveAchievementsLocked(customPath);
}

bool unlockAchievement(AchievementId id, bool autoSave)
{
    std::lock_guard<std::mutex> lock(s_achMutex);
    if (!s_loaded)
        s_loaded = true;
    return unlockAchievementLocked(id, autoSave);
}

bool addAchievementProgress(AchievementId id, uint32_t amount, bool autoSave)
{
    if (amount == 0)
        return false;
    std::lock_guard<std::mutex> lock(s_achMutex);
    uint16_t idx = static_cast<uint16_t>(id);
    if (idx >= kAchievementCount)
        return false;

    auto& rec = s_profile.records[idx];
    uint32_t target = std::max<uint32_t>(1, kCatalog[idx].targetProgress);
    rec.currentProgress = std::min<uint32_t>(target, rec.currentProgress + amount);
    if (!rec.unlocked && rec.currentProgress >= target) {
        return unlockAchievementLocked(id, autoSave);
    }
    if (autoSave)
        saveAchievementsLocked({});
    return false;
}

bool setAchievementProgress(AchievementId id, uint32_t value, bool autoSave)
{
    std::lock_guard<std::mutex> lock(s_achMutex);
    uint16_t idx = static_cast<uint16_t>(id);
    if (idx >= kAchievementCount)
        return false;

    auto& rec = s_profile.records[idx];
    if (value <= rec.currentProgress)
        return false;

    uint32_t target = std::max<uint32_t>(1, kCatalog[idx].targetProgress);
    rec.currentProgress = std::min<uint32_t>(target, value);
    if (!rec.unlocked && rec.currentProgress >= target) {
        return unlockAchievementLocked(id, autoSave);
    }
    if (autoSave)
        saveAchievementsLocked({});
    return false;
}

size_t getPendingAchievementToastCount()
{
    std::lock_guard<std::mutex> lock(s_achMutex);
    return s_toastQueue.size() + (s_activeToast.active ? 1 : 0);
}

// --- Domain Event Evaluators ---

void achievementsOnGoalScored(const AchievementGoalEvent& ev)
{
    if (!ev.humanScored || ev.isOwnGoal)
        return;

    std::lock_guard<std::mutex> lock(s_achMutex);
    bool dirty = false;

    dirty |= unlockAchievementLocked(AchievementId::kFirstGoal, false);
    if (ev.isPenalty)
        dirty |= unlockAchievementLocked(AchievementId::kPenaltyGoal, false);
    if (ev.isHeader)
        dirty |= unlockAchievementLocked(AchievementId::kHeaderGoal, false);
    if (ev.isOutsideBox && !ev.isPenalty)
        dirty |= unlockAchievementLocked(AchievementId::kOutsideBoxGoal, false);
    if (ev.isFreeKick && !ev.isPenalty)
        dirty |= unlockAchievementLocked(AchievementId::kFreeKickGoal, false);
    if (ev.isVolley && !ev.isPenalty && !ev.isHeader)
        dirty |= unlockAchievementLocked(AchievementId::kVolleyOrOverhead, false);
    if (ev.withinCornerWindow)
        dirty |= unlockAchievementLocked(AchievementId::kCornerDirectOrQuick, false);
    if (ev.scorerIsGoalkeeper)
        dirty |= unlockAchievementLocked(AchievementId::kGoalkeeperGoal, false);
    if (ev.matchMinute > 0 && ev.matchMinute <= 2)
        dirty |= unlockAchievementLocked(AchievementId::kEarlyBlitz, false);
    if (ev.matchMinute >= 89 && ev.isGoAheadGoal)
        dirty |= unlockAchievementLocked(AchievementId::kFergieTime, false);
    if (ev.isExtraTime && ev.isGoAheadGoal)
        dirty |= unlockAchievementLocked(AchievementId::kExtraTimeWinner, false);
    if (ev.scorerMatchGoals >= 3)
        dirty |= unlockAchievementLocked(AchievementId::kHatTrick, false);
    if (ev.scorerMatchGoals >= 6)
        dirty |= unlockAchievementLocked(AchievementId::kDoubleHatTrick, false);

    if (dirty)
        saveAchievementsLocked({});
}

void achievementsOnMatchFinished(const AchievementMatchSummary& s)
{
    if (!s.humanParticipated)
        return;

    std::lock_guard<std::mutex> lock(s_achMutex);

    s_profile.totalMatchesPlayed += 1;
    if (s.humanGoals > 0)
        s_profile.totalGoalsScored += static_cast<uint32_t>(s.humanGoals);

    unlockAchievementLocked(AchievementId::kFirstKickoff, false);
    if (s.humanGoals > 0)
        unlockAchievementLocked(AchievementId::kFirstGoal, false);

    // Cumulative match & goal milestones
    {
        auto& mRec = s_profile.records[static_cast<uint16_t>(AchievementId::kVeteran50Matches)];
        mRec.currentProgress = std::min<uint32_t>(50, s_profile.totalMatchesPlayed);
        if (!mRec.unlocked && mRec.currentProgress >= 50)
            unlockAchievementLocked(AchievementId::kVeteran50Matches, false);

        auto& gRec = s_profile.records[static_cast<uint16_t>(AchievementId::kCenturion100Goals)];
        gRec.currentProgress = std::min<uint32_t>(100, s_profile.totalGoalsScored);
        if (!gRec.unlocked && gRec.currentProgress >= 100)
            unlockAchievementLocked(AchievementId::kCenturion100Goals, false);
    }

    if (s.humanWon) {
        if (s.isFriendly)
            unlockAchievementLocked(AchievementId::kFriendlyWin, false);
        if (s.isCareer)
            unlockAchievementLocked(AchievementId::kCareerFirstWin, false);
        if (s.opponentGoals == 0)
            unlockAchievementLocked(AchievementId::kCleanSheet, false);
        if (s.wonOnPenalties)
            unlockAchievementLocked(AchievementId::kPenaltyShootoutWin, false);
        if (s.wonInExtraTime)
            unlockAchievementLocked(AchievementId::kExtraTimeWinner, false);
        if (s.isCompetitive && s.humanYellowCards == 0 && s.humanRedCards == 0)
            unlockAchievementLocked(AchievementId::kNoCardsWin, false);
        if (s.hadRedCardBefore60)
            unlockAchievementLocked(AchievementId::kDownToTenWin, false);
        if (s.humanHtGoals < s.opponentHtGoals)
            unlockAchievementLocked(AchievementId::kHalftimeComeback, false);
        if (s.maxGoalDeficit >= 2)
            unlockAchievementLocked(AchievementId::kTwoGoalComeback, false);
        if (s.maxGoalDeficit >= 3)
            unlockAchievementLocked(AchievementId::kThreeGoalComeback, false);
        if (s.isCompetitive && (s.humanGoals - s.opponentGoals) >= 5)
            unlockAchievementLocked(AchievementId::kFiveGoalRout, false);
        if (s.humanPossessionPct >= 65)
            unlockAchievementLocked(AchievementId::kPossessionMaster, false);
        if (s.humanGoals >= 3 && s.humanShotsOnTarget > 0 && (s.humanGoals * 4) >= (s.humanShotsOnTarget * 3))
            unlockAchievementLocked(AchievementId::kClinicalFinishing, false);

        // Cup Giant Killing (lower division number = higher tier; e.g., 0 = top flight, 3 = tier 4)
        if (s.isCupMatch && s.humanDivisionTier >= 0 && s.opponentDivisionTier >= 0 &&
            s.humanDivisionTier > s.opponentDivisionTier) {
            int tierDiff = s.humanDivisionTier - s.opponentDivisionTier;
            if (tierDiff >= 1)
                unlockAchievementLocked(AchievementId::kGiantKiller1Tier, false);
            if (tierDiff >= 2)
                unlockAchievementLocked(AchievementId::kGiantKiller2Tiers, false);
            if (tierDiff >= 3 && s.opponentDivisionTier == 0)
                unlockAchievementLocked(AchievementId::kGiantKiller3Tiers, false);
        }

        if (s.isDomesticCupFinalWin)
            unlockAchievementLocked(AchievementId::kDomesticCupWin, false);
        if (s.isEuropeanCupFinalWin)
            unlockAchievementLocked(AchievementId::kEuropeanCupWin, false);
        if (s.isWorldCupFinalWin)
            unlockAchievementLocked(AchievementId::kWorldCupWin, false);
    }

    if (s.isCompetitive && s.humanGoals >= 10)
        unlockAchievementLocked(AchievementId::kDoubleDigits, false);

    saveAchievementsLocked({});
}

void achievementsOnCareerTransfer(bool isSigning, int32_t feePounds, const std::string& playerName)
{
    std::lock_guard<std::mutex> lock(s_achMutex);
    std::string key = normalizePlayerKey(playerName);

    if (isSigning) {
        unlockAchievementLocked(AchievementId::kFirstSigning, false);
        if (feePounds >= 1000000)
            unlockAchievementLocked(AchievementId::kMillionPoundSigning, false);
        if (!key.empty() && feePounds >= 0) {
            if (s_playerBuyFees.size() >= 128 && s_playerBuyFees.find(key) == s_playerBuyFees.end())
                s_playerBuyFees.erase(s_playerBuyFees.begin());
            s_playerBuyFees[key] = feePounds;
        }
    } else if (!key.empty()) {
        auto it = s_playerBuyFees.find(key);
        if (it != s_playerBuyFees.end()) {
            if (feePounds - it->second >= 1000000)
                unlockAchievementLocked(AchievementId::kProfitableFlip, false);
            s_playerBuyFees.erase(it);
        }
    }

    saveAchievementsLocked({});
}

void achievementsOnCareerSeasonEnd(const AchievementSeasonSummary& season)
{
    std::lock_guard<std::mutex> lock(s_achMutex);

    bool promoted = season.promoted ||
                    (season.oldDivisionTier > 0 && season.newDivisionTier >= 0 &&
                     season.newDivisionTier < season.oldDivisionTier);
    if (promoted) {
        ++s_consecutivePromotions;
        unlockAchievementLocked(AchievementId::kPromotion, false);
        if (s_consecutivePromotions >= 2)
            unlockAchievementLocked(AchievementId::kBackToBackPromotion, false);
    } else if (season.newDivisionTier >= 0) {
        s_consecutivePromotions = 0;
    }

    if (season.wonTopFlightLeague)
        unlockAchievementLocked(AchievementId::kLeagueChampion, false);
    if (season.unbeatenLeagueSeason)
        unlockAchievementLocked(AchievementId::kUnbeatenSeason, false);
    if (season.wonDomesticCup)
        unlockAchievementLocked(AchievementId::kDomesticCupWin, false);
    if (season.wonEuropeanCup)
        unlockAchievementLocked(AchievementId::kEuropeanCupWin, false);
    if (season.wonTopFlightLeague && season.wonDomesticCup && season.wonEuropeanCup)
        unlockAchievementLocked(AchievementId::kContinentalTreble, false);

    if (season.completedCareerSeasons > 0) {
        auto& sRec = s_profile.records[static_cast<uint16_t>(AchievementId::kCareerTenSeasons)];
        uint32_t val = static_cast<uint32_t>(season.completedCareerSeasons);
        if (val > sRec.currentProgress)
            sRec.currentProgress = std::min<uint32_t>(10, val);
        if (!sRec.unlocked && sRec.currentProgress >= 10)
            unlockAchievementLocked(AchievementId::kCareerTenSeasons, false);
    }

    saveAchievementsLocked({});
}

void achievementsOnNationalTeamCallup()
{
    unlockAchievement(AchievementId::kInternationalCallup, true);
}

void achievementsOnCharismaChanged(int16_t charismaPoints)
{
    if (charismaPoints >= 90)
        unlockAchievement(AchievementId::kIconicCharisma, true);
}

void achievementsOnSponsorChallengesCompleted(uint32_t newlyCompletedCount)
{
    if (newlyCompletedCount == 0)
        return;
    std::lock_guard<std::mutex> lock(s_achMutex);
    s_profile.sponsorChallengesCompleted += newlyCompletedCount;
    auto& rec = s_profile.records[static_cast<uint16_t>(AchievementId::kSponsorStreak)];
    rec.currentProgress = std::min<uint32_t>(10, s_profile.sponsorChallengesCompleted);
    if (!rec.unlocked && rec.currentProgress >= 10)
        unlockAchievementLocked(AchievementId::kSponsorStreak, false);
    saveAchievementsLocked({});
}

// --- Stadium & Facilities Hooks (Ready for Stadium System Integration) ---

void achievementsOnStadiumUpgradeCompleted()
{
    unlockAchievement(AchievementId::kStadiumFirstUpgrade, true);
}

void achievementsOnStadiumMatchAttendance(int capacityLevel, int attendance, int maxCapacity)
{
    if (capacityLevel >= 10 && maxCapacity >= 85000 && attendance >= maxCapacity)
        unlockAchievement(AchievementId::kFortress85k, true);
}

void achievementsOnFacilitiesUpdated(int pitchLevel, int trainingLevel, int medicalLevel, int commercialLevel)
{
    if (pitchLevel >= 5 && trainingLevel >= 5 && medicalLevel >= 5 && commercialLevel >= 5)
        unlockAchievement(AchievementId::kMaxFacilities, true);
}

// --- Live Retro Unlock Toast Overlay (rendered from render.cpp updateScreen) ---

void drawAchievementToastOverlay()
{
#ifdef SWOS_TEST
    return;
#else
    AchievementId showId = AchievementId::kFirstKickoff;
    Uint32 elapsedMs = 0;
    bool shouldDraw = false;
    bool justStarted = false;

    {
        std::lock_guard<std::mutex> lock(s_achMutex);
        Uint32 now = SDL_GetTicks();
        if (s_activeToast.active) {
            elapsedMs = now - s_activeToast.startTick;
            if (elapsedMs >= kToastDurationMs) {
                s_activeToast.active = false;
            }
        }
        if (!s_activeToast.active && !s_toastQueue.empty()) {
            s_activeToast.id = s_toastQueue.front();
            s_toastQueue.pop_front();
            s_activeToast.startTick = now;
            s_activeToast.active = true;
            elapsedMs = 0;
            justStarted = true;
        }
        if (s_activeToast.active) {
            showId = s_activeToast.id;
            shouldDraw = true;
        }
    }

    if (justStarted)
        playUnlockChime();
    if (!shouldDraw)
        return;

    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = getCompactFontAtlas(r);
    if (!r || !atlas)
        return;

    SDL_Rect vp = getViewport();
    if (vp.w <= 0 || vp.h <= 0)
        return;

    const auto& def = getAchievementDef(showId);

    float px = 1.0f;
    if (vp.h >= 900)
        px = std::clamp(std::floor(static_cast<float>(vp.h) / 480.0f + 0.5f), 2.0f, 4.0f);
    else if (vp.h >= 540)
        px = 1.5f;

    // Smooth slide-in (0..260ms) and fade-out (3100..3500ms)
    float slideFrac = (elapsedMs < 260) ? (static_cast<float>(elapsedMs) / 260.0f) : 1.0f;
    float fadeFrac = (elapsedMs > 3100) ? std::max(0.0f, 1.0f - static_cast<float>(elapsedMs - 3100) / 400.0f) : 1.0f;
    Uint8 alpha = static_cast<Uint8>(std::clamp(fadeFrac * 255.0f, 0.0f, 255.0f));

    std::string headerStr = "ACHIEVEMENT UNLOCKED!";
    std::string ptsStr = "[+" + std::to_string(def.points) + " PTS]";
    std::string bodyStr = std::string(def.title) + " - " + def.description;

    constexpr int kLeftTextOffsetUnits = 20; // trophy icon + padding on the left
    constexpr int kRightPadUnits = 10;       // generous right padding behind text
    int maxViewportUnits = std::max(180, static_cast<int>((static_cast<float>(vp.w) * 0.92f) / px));
    int maxTextUnits = std::max(120, maxViewportUnits - kLeftTextOffsetUnits - kRightPadUnits);

    if (measureCompactTextUnits(bodyStr) > maxTextUnits) {
        while (bodyStr.size() > 16 && measureCompactTextUnits(bodyStr + "...") > maxTextUnits)
            bodyStr.pop_back();
        while (!bodyStr.empty() && bodyStr.back() == ' ')
            bodyStr.pop_back();
        bodyStr += "...";
    }

    int topUnits = measureCompactTextUnits(headerStr) + measureCompactTextUnits(ptsStr) + 18;
    int bodyUnits = measureCompactTextUnits(bodyStr);
    int contentUnits = std::max(topUnits, bodyUnits);
    int boxUnitsW = std::clamp(kLeftTextOffsetUnits + contentUnits + kRightPadUnits, 180, maxViewportUnits);
    int boxUnitsH = 26;

    float boxW = boxUnitsW * px;
    float boxH = boxUnitsH * px;
    float boxX = (static_cast<float>(vp.w) - boxW) * 0.5f;
    float targetY = 8.0f * px;
    float boxY = (-boxH) + (boxH + targetY) * slideFrac;

    float origSX = 1.0f, origSY = 1.0f;
    SDL_RenderGetScale(r, &origSX, &origSY);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    SDL_FRect bg{ boxX, boxY, boxW, boxH };
    SDL_SetRenderDrawColor(r, 14, 18, 22, static_cast<Uint8>((215 * alpha) / 255));
    SDL_RenderFillRectF(r, &bg);

    // Gold top accent & crisp border
    SDL_FRect accent{ boxX + px, boxY + px, boxW - 2.0f * px, 1.5f * px };
    SDL_SetRenderDrawColor(r, 255, 214, 68, alpha);
    SDL_RenderFillRectF(r, &accent);
    SDL_SetRenderDrawColor(r, 255, 214, 68, static_cast<Uint8>((210 * alpha) / 255));
    SDL_RenderDrawRectF(r, &bg);

    // Trophy icon on left
    drawPixelTrophyIcon(r, boxX + 6.0f * px, boxY + 9.0f * px, px * 1.25f, alpha);

    float tx = boxX + static_cast<float>(kLeftTextOffsetUnits) * px;
    drawCompactText(r, atlas, tx, boxY + 5.0f * px, px, headerStr, { 255, 224, 76, 255 }, alpha, true);
    drawCompactTextRightAligned(r, atlas, boxX + boxW - 8.0f * px, boxY + 5.0f * px, px,
        ptsStr, pointsTierColor(def.points), alpha, true);
    drawCompactText(r, atlas, tx, boxY + 15.0f * px, px, bodyStr, { 244, 246, 248, 255 }, alpha, false);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_RenderSetScale(r, origSX, origSY);
#endif
}

void showAchievementsMenu()
{
    showMenu(achievementsMenu);
    CommonMenuExit();
}
