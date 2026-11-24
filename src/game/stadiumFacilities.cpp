#include "stadiumFacilities.h"
#include "achievements.h"
#include "compactFont.h"
#include "controls.h"
#include "darkRectangle.h"
#include "drawMenu.h"
#include "gameFieldMapping.h"
#include "hotseatCareer.h"
#include "hotseat/hotseatInternal.h"
#include "llmReportMenu.h"
#include "log.h"
#include "menuBackground.h"
#include "menus.h"
#include "pressCharisma.h"
#include "render.h"
#include "swos.h"
#include "text.h"
#include "stadiumFacilities.mnu.h"

#include <SimpleIni.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace StadiumFacilitiesMenu;

namespace {

constexpr uint8_t kMaxCapacityLevel = 10;
constexpr uint8_t kMaxFacilityLevel = 5;

constexpr std::array<uint32_t, 10> kCapacityByLevel = {
    5000, 8000, 12500, 18500, 26000, 36000, 48000, 60000, 72000, 85000
};

// Target level 2..10 upgrade cost in GBP (tuned for meaningful Division 4 -> Top Flight progression)
constexpr std::array<int32_t, 9> kCapacityUpgradeCosts = {
    200000, 450000, 900000, 1600000, 2600000, 4000000, 6000000, 8500000, 12000000
};

// Target level 2..5 upgrade cost in GBP for Pitch, Training, Medical, Commercial
constexpr std::array<int32_t, 4> kFacilityUpgradeCosts = {
    125000, 275000, 550000, 1100000
};

// Realistic build duration (matches): Capacity L2..L10 (10..28 matches), Facilities L2..L5 (8..18 matches)
constexpr std::array<uint8_t, 9> kCapacityBuildMatchesByTarget = {
    10, 12, 14, 16, 18, 20, 22, 25, 28
};
constexpr std::array<uint8_t, 4> kFacilityBuildMatchesByTarget = {
    8, 10, 14, 18
};

// Stand matchday upkeep by capacity level 1..10
constexpr std::array<int32_t, 10> kStandUpkeepByLevel = {
    2000, 4500, 9500, 18000, 30000, 48000, 68000, 90000, 115000, 140000
};

// Guaranteed flat VIP corporate box baseline per home match (Div 4 base; scaled by division tier)
constexpr std::array<int32_t, 5> kVipBaseBoxIncome = {
    1500, 4000, 8000, 14000, 22000
};

// Commercial/VIP bonus % applied on top of Career Sponsor Challenge & Contract payouts (L1..L5)
constexpr std::array<int, 5> kCommercialSponsorBonusPct = {
    0, 8, 15, 22, 30
};

// Medical Center injury prevention % (new knocks turned into 0-match minor knocks) & weekly rehab boost % (L1..L5)
constexpr std::array<int, 5> kMedicalPreventionPct = {
    0, 15, 28, 42, 55
};
constexpr std::array<int, 5> kMedicalRecoveryBoostPct = {
    0, 0, 20, 35, 50
};

// 1996/97 SWOS-balanced division ticket prices (GBP): [divisionTier 0..3][TicketPolicy 0..2]
constexpr int32_t kTicketPricesByDivision[4][3] = {
    { 7, 10, 15 }, // Division 1 / Top Flight
    { 5,  8, 12 }, // Division 2
    { 4,  6,  9 }, // Division 3
    { 3,  5,  7 }, // Division 4
};

StadiumData s_stadium;
StadiumMatchdayReport s_lastReport;
StadiumMatchdayReport s_liveMatchPreview;
bool s_pendingNotableReportPopup = false;

// Pre-match snapshot state for home gate delta & medical injury reduction
bool s_matchTrackingActive = false;
bool s_matchHumanIsHome = false;
int32_t s_preMatchGateReceipts = 0;
std::string s_matchOpponentName;
bool s_matchIsDerby = false;
int s_matchClubDivision = 0;
int s_matchClubRank = 0;
int s_matchOppRank = 0;
std::array<uint8_t, kCurrentMatchPlayersSize> s_preMatchInjuryBytes = {};
bool s_preMatchInjuryCaptured = false;

// Management menu UI state
bool s_confirmingUpgrade = false;
StadiumModuleType s_confirmModule = StadiumModuleType::kCapacity;
std::string s_statusFeedback;
Uint32 s_statusFeedbackTick = 0;
bool s_prevMenuKeys[8] = {}; // 1..5, T, Left/Right, Esc
bool s_prevHubF6Down = false;
bool s_prevHubMouseDown = false;

std::string toUpperAscii(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in)
        out.push_back(static_cast<char>(std::toupper(c)));
    return out;
}

std::string formatPoundsShort(int64_t amount)
{
    bool neg = (amount < 0);
    int64_t a = neg ? -amount : amount;
    char buf[48];
    if (a >= 1000000) {
        double m = static_cast<double>(a) / 1000000.0;
        std::snprintf(buf, sizeof(buf), "%sGBP %.2fM", neg ? "-" : "", m);
    } else if (a >= 1000) {
        std::snprintf(buf, sizeof(buf), "%sGBP %lldK", neg ? "-" : "", static_cast<long long>(a / 1000));
    } else {
        std::snprintf(buf, sizeof(buf), "%sGBP %lld", neg ? "-" : "", static_cast<long long>(a));
    }
    return buf;
}

std::string formatPoundsExact(int64_t amount)
{
    bool neg = (amount < 0);
    int64_t a = neg ? -amount : amount;
    std::string digits = std::to_string(a);
    std::string withCommas;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && ((digits.size() - i) % 3) == 0)
            withCommas.push_back(',');
        withCommas.push_back(digits[i]);
    }
    return std::string(neg ? "-GBP " : "GBP ") + withCommas;
}

std::string formatSignedPoundsExact(int64_t amount)
{
    if (amount > 0)
        return "+" + formatPoundsExact(amount);
    return formatPoundsExact(amount);
}

std::string formatNumberCommas(uint32_t val)
{
    std::string digits = std::to_string(val);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && ((digits.size() - i) % 3) == 0)
            out.push_back(',');
        out.push_back(digits[i]);
    }
    return out;
}

std::string resolveAuthenticStadiumName(const std::string& clubName)
{
    std::string upper = toUpperAscii(clubName);
    static const std::pair<const char *, const char *> kFamousGrounds[] = {
        { "FENERBAHCE", "SUKRU SARACOGLU" },
        { "GALATASARAY", "ALI SAMI YEN" },
        { "BESIKTAS", "INONU STADIUM" },
        { "TRABZONSPOR", "HUSEYIN AVNI AKER" },
        { "MANCHESTER UTD", "OLD TRAFFORD" },
        { "MANCHESTER CITY", "MAINE ROAD" },
        { "LIVERPOOL", "ANFIELD" },
        { "EVERTON", "GOODISON PARK" },
        { "ARSENAL", "HIGHBURY" },
        { "TOTTENHAM", "WHITE HART LANE" },
        { "CHELSEA", "STAMFORD BRIDGE" },
        { "NEWCASTLE", "ST JAMES PARK" },
        { "ASTON VILLA", "VILLA PARK" },
        { "LEEDS", "ELLAND ROAD" },
        { "WEST HAM", "UPTON PARK" },
        { "CELTIC", "CELTIC PARK" },
        { "RANGERS", "IBROX STADIUM" },
        { "BARCELONA", "CAMP NOU" },
        { "REAL MADRID", "SANTIAGO BERNABEU" },
        { "ATLETICO", "VICENTE CALDERON" },
        { "VALENCIA", "MESTALLA" },
        { "AC MILAN", "SAN SIRO" },
        { "MILAN", "SAN SIRO" },
        { "INTER", "GIUSEPPE MEAZZA" },
        { "JUVENTUS", "DELLE ALPI" },
        { "ROMA", "STADIO OLIMPICO" },
        { "LAZIO", "STADIO OLIMPICO" },
        { "NAPOLI", "SAN PAOLO" },
        { "PARMA", "ENNIO TARDINI" },
        { "FIORENTINA", "ARTEMIO FRANCHI" },
        { "AJAX", "AMSTERDAM ARENA" },
        { "FEYENOORD", "DE KUIP" },
        { "PSV", "PHILIPS STADION" },
        { "BAYERN", "OLYMPIASTADION" },
        { "DORTMUND", "WESTFALENSTADION" },
        { "SCHALKE", "PARKSTADION" },
        { "HAMBURG", "VOLKSPARKSTADION" },
        { "BENFICA", "ESTADIO DA LUZ" },
        { "PORTO", "DAS ANTAS" },
        { "SPORTING", "JOSE ALVALADE" },
        { "PARIS", "PARC DES PRINCES" },
        { "MARSEILLE", "STADE VELODROME" },
        { "BOCA", "LA BOMBONERA" },
        { "RIVER", "EL MONUMENTAL" },
    };

    for (const auto& kv : kFamousGrounds) {
        if (upper.find(kv.first) != std::string::npos)
            return kv.second;
    }

    // Procedural fallback: strip trailing suffixes like FC / UTD and append STADIUM / PARK / ARENA
    std::string base = upper;
    while (!base.empty() && base.back() == ' ')
        base.pop_back();
    if (base.empty())
        return "MUNICIPAL STADIUM";

    uint32_t hash = 0;
    for (char c : base)
        hash = hash * 31u + static_cast<unsigned char>(c);
    static const char *const kSuffixes[3] = { " STADIUM", " PARK", " ARENA" };
    std::string suffix = kSuffixes[hash % 3u];
    if (base.size() + suffix.size() >= 31)
        base.resize(31 - suffix.size());
    while (!base.empty() && base.back() == ' ')
        base.pop_back();
    return base + suffix;
}

uint8_t getModuleLevel(const StadiumData& s, StadiumModuleType mod)
{
    switch (mod) {
    case StadiumModuleType::kCapacity:   return s.capacityLevel;
    case StadiumModuleType::kPitch:      return s.pitchLevel;
    case StadiumModuleType::kTraining:   return s.trainingLevel;
    case StadiumModuleType::kMedical:    return s.medicalLevel;
    case StadiumModuleType::kCommercial: return s.commercialLevel;
    default:                             return 1;
    }
}

void setModuleLevel(StadiumData& s, StadiumModuleType mod, uint8_t lvl)
{
    switch (mod) {
    case StadiumModuleType::kCapacity:
        s.capacityLevel = std::clamp<uint8_t>(lvl, 1, kMaxCapacityLevel);
        s.maxCapacity = getStadiumCapacityForLevel(s.capacityLevel);
        break;
    case StadiumModuleType::kPitch:
        s.pitchLevel = std::clamp<uint8_t>(lvl, 1, kMaxFacilityLevel);
        break;
    case StadiumModuleType::kTraining:
        s.trainingLevel = std::clamp<uint8_t>(lvl, 1, kMaxFacilityLevel);
        break;
    case StadiumModuleType::kMedical:
        s.medicalLevel = std::clamp<uint8_t>(lvl, 1, kMaxFacilityLevel);
        break;
    case StadiumModuleType::kCommercial:
        s.commercialLevel = std::clamp<uint8_t>(lvl, 1, kMaxFacilityLevel);
        break;
    default:
        break;
    }
}

int16_t readCurrentCharismaPoints()
{
    int16_t cp = 50;
    uint16_t ints = 0;
    int64_t bonus = 0;
    uint8_t stance = 0;
    std::string quote;
    getPressCareerTotals(cp, ints, bonus, stance, quote);
    return cp;
}

int readVmDivisionTier()
{
    uint16_t raw = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]);
    return std::clamp<int>(static_cast<int>(raw), 0, 3);
}

int64_t readVmClubCash()
{
    return static_cast<int64_t>(*reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]));
}

void applyVmCashDelta(int64_t delta)
{
    if (delta == 0)
        return;
    auto *vmBal = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    int64_t next = static_cast<int64_t>(*vmBal) + delta;
    next = std::clamp<int64_t>(next, -2000000000LL, 2000000000LL);
    *vmBal = static_cast<int32_t>(next);

    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        mgr.activePlayer().newBalance = *vmBal;
    }
}

void syncStadiumFromHotseatIfActive()
{
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        const auto& p = mgr.activePlayer();
        if (p.stadium.initialized)
            s_stadium = p.stadium;
        else
            s_stadium = StadiumData{};
    }
}

void syncStadiumToHotseatIfActive()
{
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        mgr.activePlayer().stadium = s_stadium;
    }
}

int computeAverageCareerSquadPrice()
{
    uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    if (numPlayers == 0 || numPlayers > kCurrentMatchPlayersSize)
        return 12;

    const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];
    int sum = 0;
    int cnt = 0;
    for (uint16_t i = 0; i < numPlayers; ++i) {
        uint8_t slot = matchPlayers[i];
        if (slot >= kCurrentMatchPlayersSize)
            continue;
        size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
        if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
            continue;
        const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
        if (pf->playerName[0] == '\0' || std::strncmp(pf->playerName, "*ERR*", 5) == 0)
            continue;
        sum += static_cast<int>(pf->price);
        ++cnt;
    }
    return (cnt > 0) ? (sum / cnt) : 12;
}

std::string readCurrentCareerClubName()
{
    const auto *hdr = reinterpret_cast<const TeamFileHeader *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]);
    if (hdr->teamName[0] != '\0')
        return std::string(hdr->teamName);
    return "CLUB";
}

// Estimate approximate league standing (1..20) from career W-D-L record if league table unavailable
int estimateClubRankFromSeasonRecord()
{
    const auto *statsWords = reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C88]);
    uint16_t played = statsWords[0];
    if (played < 2 || played > 120)
        return 8; // mid-table default early in season
    uint16_t wins = statsWords[1];
    uint16_t draws = statsWords[2];
    double ppg = static_cast<double>(wins * 3 + draws) / static_cast<double>(played);
    // Map ppg [0.4 .. 2.4] -> rank [20 .. 1]
    double norm = std::clamp((ppg - 0.4) / 2.0, 0.0, 1.0);
    return std::clamp<int>(static_cast<int>(std::lround(20.0 - norm * 19.0)), 1, 20);
}

void setStatusFeedback(const std::string& msg)
{
    s_statusFeedback = msg;
    s_statusFeedbackTick = SDL_GetTicks();
}

void updateConfirmButtonsVisibility()
{
    if (s_confirmingUpgrade) {
        confirmYesBtnEntry.show();
        confirmYesBtnEntry.enable();
        confirmNoBtnEntry.show();
        confirmNoBtnEntry.enable();
        backBtnEntry.hide();
        backBtnEntry.disable();
        setCurrentEntry(confirmYesBtn);
    } else {
        confirmYesBtnEntry.hide();
        confirmYesBtnEntry.disable();
        confirmNoBtnEntry.hide();
        confirmNoBtnEntry.disable();
        backBtnEntry.show();
        backBtnEntry.enable();
    }
}

void beginUpgradeConfirmation(StadiumModuleType mod)
{
    ensureCurrentCareerStadiumInitialized();
    int64_t cash = readVmClubCash();
    StadiumUpgradeQuote q = stadiumGetUpgradeQuote(s_stadium, mod, cash);
    if (q.status == StadiumUpgradeStatus::kMaxLevel) {
        setStatusFeedback(std::string(getStadiumModuleName(mod)) + " is already at MAXIMUM level!");
        return;
    }
    if (q.status == StadiumUpgradeStatus::kConstructionInProgress) {
        setStatusFeedback("Construction already underway! Finish current project first.");
        return;
    }
    if (q.status == StadiumUpgradeStatus::kInsufficientFunds) {
        setStatusFeedback("Need " + formatPoundsExact(q.cost) + " (insufficient club funds).");
        return;
    }

    s_confirmingUpgrade = true;
    s_confirmModule = mod;
    setStatusFeedback("Confirm: Upgrade " + std::string(getStadiumModuleName(mod)) +
                      " to Lvl " + std::to_string(q.targetLevel) + " for " +
                      formatPoundsExact(q.cost) + " (" + std::to_string(q.buildMatches) + " matches)?");
    updateConfirmButtonsVisibility();
}

} // namespace

// --- Pure Deterministic Economy & Formula Helpers ---

uint32_t getStadiumCapacityForLevel(uint8_t level)
{
    int idx = std::clamp<int>(static_cast<int>(level) - 1, 0, 9);
    return kCapacityByLevel[static_cast<size_t>(idx)];
}

int32_t getStadiumTicketPriceForDivision(int divisionTier, TicketPolicy policy)
{
    int divIdx = std::clamp(divisionTier, 0, 3);
    int polIdx = std::clamp(static_cast<int>(policy), 0, 2);
    return kTicketPricesByDivision[divIdx][polIdx];
}

int getStadiumFormScore(const StadiumData& stadium)
{
    int score = 0;
    for (int i = 0; i < 5 && stadium.recentForm[i] != '\0'; ++i) {
        char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(stadium.recentForm[i])));
        if (ch == 'W') ++score;
        else if (ch == 'L') --score;
    }
    return score;
}

int32_t getStadiumFormTicketAdjustment(const StadiumData& stadium)
{
    int score = getStadiumFormScore(stadium);
    if (score >= 4) return 2;
    if (score == 3) return 1;
    if (score <= -4) return -2;
    if (score == -3) return -1;
    return 0;
}

int32_t getEffectiveStadiumTicketPrice(const StadiumData& stadium, int divisionTier)
{
    int32_t base = getStadiumTicketPriceForDivision(divisionTier, stadium.ticketPolicy);
    int32_t transferAdj = (stadium.transferBuzzHomeMatches > 0) ? static_cast<int32_t>(stadium.transferTicketDelta) : 0;
    return std::max<int32_t>(2, base + getStadiumFormTicketAdjustment(stadium) + transferAdj);
}

double getStadiumFormDemandMultiplier(const StadiumData& stadium)
{
    int score = getStadiumFormScore(stadium);
    if (score >= 4) return 1.12;
    if (score == 3) return 1.06;
    if (score <= -4) return 0.88;
    if (score == -3) return 0.94;
    return 1.00;
}

void stadiumRecordMatchForm(StadiumData& stadium, int clubGoals, int opponentGoals)
{
    char tag = (clubGoals > opponentGoals) ? 'W' : ((clubGoals < opponentGoals) ? 'L' : 'D');
    size_t len = 0;
    while (len < 5 && stadium.recentForm[len] != '\0')
        ++len;
    if (len < 5) {
        stadium.recentForm[len] = tag;
        stadium.recentForm[len + 1] = '\0';
    } else {
        for (int i = 0; i < 4; ++i)
            stadium.recentForm[i] = stadium.recentForm[i + 1];
        stadium.recentForm[4] = tag;
        stadium.recentForm[5] = '\0';
    }
}

int32_t getStadiumVipBoxIncome(uint8_t commercialLevel, int divisionTier)
{
    int idx = std::clamp<int>(static_cast<int>(commercialLevel) - 1, 0, 4);
    int div = std::clamp(divisionTier, 0, 3);
    static constexpr double kDivScale[4] = { 3.2, 2.0, 1.3, 1.0 };
    double raw = static_cast<double>(kVipBaseBoxIncome[static_cast<size_t>(idx)]) * kDivScale[div];
    int32_t rounded = static_cast<int32_t>(std::llround(raw / 500.0) * 500LL);
    return std::max<int32_t>(500, rounded);
}

int getStadiumCommercialSponsorBonusPct(uint8_t commercialLevel)
{
    int idx = std::clamp<int>(static_cast<int>(commercialLevel) - 1, 0, 4);
    return kCommercialSponsorBonusPct[static_cast<size_t>(idx)];
}

int getCurrentCareerCommercialSponsorBonusPct()
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame || HotseatInternal::isVmNationalSelector())
        return 0;
    ensureCurrentCareerStadiumInitialized();
    if (!s_stadium.initialized)
        return 0;
    return getStadiumCommercialSponsorBonusPct(s_stadium.commercialLevel);
}

int getMedicalInjuryPreventionPct(uint8_t medicalLevel)
{
    int idx = std::clamp<int>(static_cast<int>(medicalLevel) - 1, 0, 4);
    return kMedicalPreventionPct[static_cast<size_t>(idx)];
}

int getMedicalRecoveryBoostPct(uint8_t medicalLevel)
{
    int idx = std::clamp<int>(static_cast<int>(medicalLevel) - 1, 0, 4);
    return kMedicalRecoveryBoostPct[static_cast<size_t>(idx)];
}

int32_t getStadiumMatchdayUpkeep(const StadiumData& stadium)
{
    int capIdx = std::clamp<int>(static_cast<int>(stadium.capacityLevel) - 1, 0, 9);
    int32_t standUpkeep = kStandUpkeepByLevel[static_cast<size_t>(capIdx)];
    int extraFacilityLevels =
        std::max(0, static_cast<int>(stadium.pitchLevel) - 1) +
        std::max(0, static_cast<int>(stadium.trainingLevel) - 1) +
        std::max(0, static_cast<int>(stadium.medicalLevel) - 1) +
        std::max(0, static_cast<int>(stadium.commercialLevel) - 1);
    return standUpkeep + extraFacilityLevels * 1500;
}

const char *getStadiumModuleName(StadiumModuleType module)
{
    switch (module) {
    case StadiumModuleType::kCapacity:   return "STAND CAPACITY";
    case StadiumModuleType::kPitch:      return "PITCH QUALITY";
    case StadiumModuleType::kTraining:   return "TRAINING COMPLEX";
    case StadiumModuleType::kMedical:    return "MEDICAL CENTER";
    case StadiumModuleType::kCommercial: return "COMMERCIAL & VIP";
    default:                             return "FACILITY";
    }
}

const char *getTicketPolicyName(TicketPolicy policy)
{
    switch (policy) {
    case TicketPolicy::kCheap:     return "CHEAP";
    case TicketPolicy::kStandard:  return "STANDARD";
    case TicketPolicy::kExpensive: return "EXPENSIVE";
    default:                       return "STANDARD";
    }
}

const char *getPitchQualityLabel(uint8_t pitchLevel)
{
    switch (std::clamp<int>(pitchLevel, 1, 5)) {
    case 1: return "MUDDY TURF";
    case 2: return "BASIC GRASS";
    case 3: return "GOOD TURF";
    case 4: return "DRAINED PRO";
    case 5: return "PRISTINE TURF";
    default: return "GOOD TURF";
    }
}

StadiumData stadiumInitForClub(const std::string& clubName, int divisionTier, int avgSquadPrice, int16_t teamId)
{
    StadiumData s;
    s.initialized = true;
    s.teamId = teamId;

    std::string name = resolveAuthenticStadiumName(clubName);
    std::strncpy(s.stadiumName, name.c_str(), sizeof(s.stadiumName) - 1);
    s.stadiumName[sizeof(s.stadiumName) - 1] = '\0';

    int div = std::clamp(divisionTier, 0, 3);
    bool eliteSquad = (avgSquadPrice >= 26);
    bool strongSquad = (avgSquadPrice >= 18);

    if (div == 0) {
        s.capacityLevel = eliteSquad ? 5 : (strongSquad ? 4 : 3);
        s.pitchLevel = eliteSquad ? 3 : 2;
        s.trainingLevel = eliteSquad ? 3 : 2;
        s.medicalLevel = strongSquad ? 2 : 1;
        s.commercialLevel = eliteSquad ? 3 : 2;
    } else if (div == 1) {
        s.capacityLevel = strongSquad ? 3 : 2;
        s.pitchLevel = 2;
        s.trainingLevel = 2;
        s.medicalLevel = 1;
        s.commercialLevel = 1;
    } else if (div == 2) {
        s.capacityLevel = 2;
        s.pitchLevel = 1;
        s.trainingLevel = 1;
        s.medicalLevel = 1;
        s.commercialLevel = 1;
    } else {
        s.capacityLevel = 1;
        s.pitchLevel = 1;
        s.trainingLevel = 1;
        s.medicalLevel = 1;
        s.commercialLevel = 1;
    }

    s.maxCapacity = getStadiumCapacityForLevel(s.capacityLevel);
    s.ticketPolicy = TicketPolicy::kStandard;
    s.ticketPrice = getStadiumTicketPriceForDivision(div, s.ticketPolicy);
    s.activeProject = ConstructionProject{};
    return s;
}

StadiumUpgradeQuote stadiumGetUpgradeQuote(const StadiumData& stadium, StadiumModuleType module, int64_t clubCash)
{
    StadiumUpgradeQuote q;
    q.module = module;
    q.currentLevel = getModuleLevel(stadium, module);
    uint8_t maxLvl = (module == StadiumModuleType::kCapacity) ? kMaxCapacityLevel : kMaxFacilityLevel;

    if (q.currentLevel >= maxLvl) {
        q.status = StadiumUpgradeStatus::kMaxLevel;
        q.targetLevel = maxLvl;
        q.previewText = "MAX LEVEL REACHED";
        return q;
    }

    q.targetLevel = static_cast<uint8_t>(q.currentLevel + 1);
    int costIdx = static_cast<int>(q.targetLevel) - 2;
    if (module == StadiumModuleType::kCapacity) {
        q.cost = kCapacityUpgradeCosts[static_cast<size_t>(std::clamp(costIdx, 0, 8))];
        q.buildMatches = kCapacityBuildMatchesByTarget[static_cast<size_t>(std::clamp(costIdx, 0, 8))];
        q.previewText = formatNumberCommas(getStadiumCapacityForLevel(q.targetLevel)) + " seats";
    } else {
        q.cost = kFacilityUpgradeCosts[static_cast<size_t>(std::clamp(costIdx, 0, 3))];
        q.buildMatches = kFacilityBuildMatchesByTarget[static_cast<size_t>(std::clamp(costIdx, 0, 3))];
        switch (module) {
        case StadiumModuleType::kPitch:
            q.previewText = std::string(getPitchQualityLabel(q.targetLevel)) +
                            (q.targetLevel >= 4 ? " (No Mud/Wet)" : "");
            break;
        case StadiumModuleType::kTraining:
            q.previewText = "+" + std::to_string(q.targetLevel * 5) + "% annual skill growth";
            break;
        case StadiumModuleType::kMedical: {
            int prevPct = getMedicalInjuryPreventionPct(q.targetLevel);
            int rehabPct = getMedicalRecoveryBoostPct(q.targetLevel);
            q.previewText = std::to_string(prevPct) + "% prevent, -" + std::to_string(q.targetLevel * 10) + "% time" +
                            (rehabPct > 0 ? (", " + std::to_string(rehabPct) + "% rehab") : "");
            break;
        }
        case StadiumModuleType::kCommercial: {
            int div = readVmDivisionTier();
            int32_t vip = getStadiumVipBoxIncome(q.targetLevel, div);
            int sponPct = getStadiumCommercialSponsorBonusPct(q.targetLevel);
            q.previewText = formatPoundsShort(vip) + " VIP boxes + " + std::to_string(q.targetLevel * 10) +
                            "% merch + +" + std::to_string(sponPct) + "% Sponsors";
            break;
        }
        default:
            break;
        }
    }

    StadiumData sim = stadium;
    setModuleLevel(sim, module, q.targetLevel);
    q.newMatchdayUpkeep = getStadiumMatchdayUpkeep(sim);

    if (stadium.activeProject.isActive) {
        q.status = StadiumUpgradeStatus::kConstructionInProgress;
    } else if (clubCash < q.cost) {
        q.status = StadiumUpgradeStatus::kInsufficientFunds;
    } else {
        q.status = StadiumUpgradeStatus::kOk;
    }
    return q;
}

bool stadiumStartUpgrade(StadiumData& stadium, int64_t& clubCash, StadiumModuleType module)
{
    StadiumUpgradeQuote q = stadiumGetUpgradeQuote(stadium, module, clubCash);
    if (q.status != StadiumUpgradeStatus::kOk)
        return false;

    clubCash -= q.cost;
    stadium.activeProject.isActive = true;
    stadium.activeProject.module = module;
    stadium.activeProject.targetLevel = q.targetLevel;
    stadium.activeProject.matchesRemaining = q.buildMatches;
    stadium.activeProject.costPaid = q.cost;
    return true;
}

bool stadiumAdvanceConstruction(StadiumData& stadium, StadiumModuleType& outCompletedModule, uint8_t& outNewLevel)
{
    if (!stadium.activeProject.isActive)
        return false;

    if (stadium.activeProject.matchesRemaining > 1) {
        --stadium.activeProject.matchesRemaining;
        return false;
    }

    outCompletedModule = stadium.activeProject.module;
    outNewLevel = stadium.activeProject.targetLevel;
    setModuleLevel(stadium, outCompletedModule, outNewLevel);
    stadium.activeProject = ConstructionProject{};
    return true;
}

StadiumMatchdayReport stadiumCalculateMatchday(
    const StadiumData& stadium,
    int divisionTier,
    int clubRank,
    int oppRank,
    bool isDerby,
    int16_t charismaPoints,
    const std::string& opponentName)
{
    StadiumMatchdayReport rep;
    rep.valid = true;
    rep.isHomeMatch = true;
    rep.isDerby = isDerby;
    rep.stadiumName = stadium.stadiumName[0] ? stadium.stadiumName : "CLUB STADIUM";
    rep.opponentName = opponentName;
    rep.maxCapacity = std::max<uint32_t>(5000, stadium.maxCapacity);

    // Base fill rate F_base: 0.55 (20th) .. 0.95 (1st)
    double fBase = 0.78;
    if (clubRank >= 1 && clubRank <= 20) {
        double rankFrac = static_cast<double>(20 - clubRank) / 19.0;
        fBase = 0.55 + rankFrac * 0.40;
    }

    // Opponent prestige M_opp
    double mOpp = 1.00;
    if (isDerby || (oppRank >= 1 && oppRank <= 4))
        mOpp = 1.25;
    else if (oppRank >= 14)
        mOpp = 0.90;

    // Ticket price elasticity M_price
    double mPrice = 1.00;
    if (stadium.ticketPolicy == TicketPolicy::kCheap)
        mPrice = 1.15;
    else if (stadium.ticketPolicy == TicketPolicy::kExpensive)
        mPrice = 0.78;

    // Coach Charisma multiplier M_charisma (0.90 .. 1.15), Last-5 Form (0.88 .. 1.12), & Transfer Buzz
    double mCharisma = getPressAttendanceMultiplier(charismaPoints);
    double mForm = getStadiumFormDemandMultiplier(stadium);
    double mTransfer = (stadium.transferBuzzHomeMatches > 0)
        ? std::clamp(1.0 + static_cast<double>(stadium.transferDemandPctDelta) / 100.0, 0.75, 1.30)
        : 1.0;

    double rawAtt = std::floor(static_cast<double>(rep.maxCapacity) * fBase * mOpp * mPrice * mCharisma * mForm * mTransfer);
    rep.attendance = static_cast<uint32_t>(std::clamp<double>(rawAtt, 1000.0, static_cast<double>(rep.maxCapacity)));
    rep.fillPct = static_cast<int>(std::lround((static_cast<double>(rep.attendance) * 100.0) /
                                               static_cast<double>(rep.maxCapacity)));
    rep.isSellout = (rep.fillPct >= 95);

    rep.baseTicketPrice = getStadiumTicketPriceForDivision(divisionTier, stadium.ticketPolicy);
    rep.formTicketDelta = getStadiumFormTicketAdjustment(stadium);
    rep.transferTicketDelta = (stadium.transferBuzzHomeMatches > 0) ? static_cast<int32_t>(stadium.transferTicketDelta) : 0;
    if (stadium.transferBuzzHomeMatches > 0 && stadium.transferBuzzLabel[0] != '\0')
        rep.transferBuzzLabel = stadium.transferBuzzLabel;
    rep.ticketPrice = getEffectiveStadiumTicketPrice(stadium, divisionTier);
    rep.ticketRevenue = static_cast<int64_t>(rep.attendance) * static_cast<int64_t>(rep.ticketPrice);
    rep.commercialBonusPct = std::clamp<int>(static_cast<int>(stadium.commercialLevel), 1, 5) * 10;
    rep.vipBoxRevenue = static_cast<int64_t>(getStadiumVipBoxIncome(stadium.commercialLevel, divisionTier));
    rep.merchRevenue = static_cast<int64_t>(std::llround(
        static_cast<double>(rep.ticketRevenue) * (static_cast<double>(rep.commercialBonusPct) / 100.0)));
    rep.commercialRevenue = rep.vipBoxRevenue + rep.merchRevenue;
    rep.upkeepCost = static_cast<int64_t>(getStadiumMatchdayUpkeep(stadium));
    rep.netProfit = rep.ticketRevenue + rep.commercialRevenue - rep.upkeepCost;

    if (rep.isSellout && rep.isDerby)
        rep.notableReason = "PACKED DERBY SELLOUT";
    else if (rep.isSellout)
        rep.notableReason = "HOME SELLOUT";
    else if (rep.isDerby)
        rep.notableReason = "RIVALRY DERBY GATE";
    return rep;
}

int applyStadiumPitchQualityOverrideForLevel(int rawPitchType, uint8_t pitchLevel)
{
    // SWOS pitch types: 0=Frozen, 1=Muddy, 2=Wet, 3=Soft, 4=Normal, 5=Dry, 6=Hard
    if (pitchLevel < 4)
        return rawPitchType;
    if (rawPitchType == 0 || rawPitchType == 1 || rawPitchType == 2) {
        return (pitchLevel >= 5) ? 5 : 4; // Lvl 5 -> Dry pristine turf, Lvl 4 -> Normal drained pitch
    }
    return rawPitchType;
}

uint8_t computeMedicalReducedInjuryDays(uint8_t rawInjuryDays, uint8_t medicalLevel)
{
    if (rawInjuryDays <= 1)
        return rawInjuryDays;
    int lvl = std::clamp<int>(medicalLevel, 1, 5);
    double factor = 1.0 - (static_cast<double>(lvl) * 0.10);
    int reduced = static_cast<int>(std::floor(static_cast<double>(rawInjuryDays) * factor + 0.0001));
    return static_cast<uint8_t>(std::clamp(reduced, 1, static_cast<int>(rawInjuryDays)));
}

double getTrainingGrowthBonusFraction(uint8_t trainingLevel)
{
    return static_cast<double>(std::clamp<int>(trainingLevel, 1, 5)) * 0.05;
}

// --- Career Lifecycle, Match Engine & Persistence Hooks ---

void resetStadiumFacilitiesState()
{
    s_stadium = StadiumData{};
    s_lastReport = StadiumMatchdayReport{};
    s_pendingNotableReportPopup = false;
    s_matchTrackingActive = false;
    s_liveMatchPreview = StadiumMatchdayReport{};
    s_matchHumanIsHome = false;
    s_preMatchInjuryCaptured = false;
    s_liveMatchPreview = StadiumMatchdayReport{};
    s_confirmingUpgrade = false;
    s_statusFeedback.clear();
}

void ensureCurrentCareerStadiumInitialized()
{
    if (swos.g_gameType != kGameTypeCareer)
        return;

    syncStadiumFromHotseatIfActive();

    int16_t curTeamId = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    int div = readVmDivisionTier();

    if (!s_stadium.initialized || (curTeamId >= 0 && s_stadium.teamId >= 0 && s_stadium.teamId != curTeamId)) {
        std::string club = readCurrentCareerClubName();
        int avgPrice = computeAverageCareerSquadPrice();
        s_stadium = stadiumInitForClub(club, div, avgPrice, curTeamId);
        syncStadiumToHotseatIfActive();
        logInfo("Stadium initialized for %s (teamId=%d, div=%d): %s (Cap L%u=%u, Pitch L%u, Train L%u, Med L%u, Comm L%u)",
            club.c_str(), static_cast<int>(curTeamId), div, s_stadium.stadiumName,
            static_cast<unsigned>(s_stadium.capacityLevel), static_cast<unsigned>(s_stadium.maxCapacity),
            static_cast<unsigned>(s_stadium.pitchLevel), static_cast<unsigned>(s_stadium.trainingLevel),
            static_cast<unsigned>(s_stadium.medicalLevel), static_cast<unsigned>(s_stadium.commercialLevel));
    } else {
        s_stadium.ticketPrice = getEffectiveStadiumTicketPrice(s_stadium, div);
    }
}

const StadiumData& getCurrentCareerStadium()
{
    ensureCurrentCareerStadiumInitialized();
    return s_stadium;
}

void setCurrentCareerStadium(const StadiumData& data)
{
    s_stadium = data;
    syncStadiumToHotseatIfActive();
}

void applyStadiumTransferBuzz(int8_t ticketDelta, int8_t demandPctDelta, uint8_t homeMatches, const std::string& label)
{
    if (swos.g_gameType != kGameTypeCareer || HotseatInternal::isVmNationalSelector())
        return;
    ensureCurrentCareerStadiumInitialized();
    if (!s_stadium.initialized)
        return;
    s_stadium.transferTicketDelta = std::clamp<int8_t>(ticketDelta, -4, 5);
    s_stadium.transferDemandPctDelta = std::clamp<int8_t>(demandPctDelta, -20, 25);
    s_stadium.transferBuzzHomeMatches = std::clamp<uint8_t>(homeMatches, 0, 10);
    std::memset(s_stadium.transferBuzzLabel, 0, sizeof(s_stadium.transferBuzzLabel));
    if (!label.empty()) {
        std::strncpy(s_stadium.transferBuzzLabel, label.c_str(), sizeof(s_stadium.transferBuzzLabel) - 1);
    }
    s_stadium.ticketPrice = getEffectiveStadiumTicketPrice(s_stadium, readVmDivisionTier());
    syncStadiumToHotseatIfActive();
}

bool isStadiumManagingHomeGateReceipts()
{
    return swos.g_gameType == kGameTypeCareer && !swos.g_trainingGame &&
           !HotseatInternal::isVmNationalSelector() && s_stadium.initialized;
}

void applyStadiumPitchQualityOverride(int& pitchType)
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame || HotseatInternal::isVmNationalSelector())
        return;

    const TeamFile *tf1 = swos.gameTeam1.asAligned();
    if (!tf1)
        tf1 = swos.playMatchTeam1Ptr.asAligned();
    bool homeIsHuman = tf1 && (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
    if (!homeIsHuman)
        return;

    ensureCurrentCareerStadiumInitialized();
    int updated = applyStadiumPitchQualityOverrideForLevel(pitchType, s_stadium.pitchLevel);
    if (updated != pitchType) {
        logInfo("Stadium Pitch Quality Lvl %u (%s) upgraded home pitch condition %d -> %d",
            static_cast<unsigned>(s_stadium.pitchLevel), getPitchQualityLabel(s_stadium.pitchLevel),
            pitchType, updated);
        pitchType = updated;
    }
}

void onPreMatchStadiumSnapshot(const TeamGame *topTeam, const TeamGame *bottomTeam)
{
    s_matchTrackingActive = false;
    s_liveMatchPreview = StadiumMatchdayReport{};
    s_matchHumanIsHome = false;
    s_preMatchInjuryCaptured = false;
    s_pendingNotableReportPopup = false;
    s_liveMatchPreview = StadiumMatchdayReport{};

    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame || HotseatInternal::isVmNationalSelector())
        return;

    ensureCurrentCareerStadiumInitialized();

    bool t1Human = false;
    bool t2Human = false;
    const TeamFile *tf1 = swos.gameTeam1.asAligned();
    const TeamFile *tf2 = swos.gameTeam2.asAligned();
    if (!tf1) tf1 = swos.playMatchTeam1Ptr.asAligned();
    if (!tf2) tf2 = swos.playMatchTeam2Ptr.asAligned();
    if (tf1) t1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
    if (tf2) t2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);

    if (!t1Human && !t2Human)
        return;

    s_matchTrackingActive = true;
    s_matchHumanIsHome = t1Human;
    s_preMatchGateReceipts = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGateReceipts]);

    std::string homeName = (topTeam && topTeam->teamName[0]) ? topTeam->teamName : (tf1 ? tf1->teamName : "HOME");
    std::string awayName = (bottomTeam && bottomTeam->teamName[0]) ? bottomTeam->teamName : (tf2 ? tf2->teamName : "AWAY");
    s_matchOpponentName = s_matchHumanIsHome ? awayName : homeName;
    s_matchClubDivision = readVmDivisionTier();
    s_matchClubRank = estimateClubRankFromSeasonRecord();
    s_matchOppRank = 8;
    s_matchIsDerby = detectClassicDerby(s_matchHumanIsHome ? homeName : awayName, s_matchOpponentName, s_matchClubRank, s_matchOppRank);

    if (s_matchHumanIsHome) {
        s_liveMatchPreview = stadiumCalculateMatchday(
            s_stadium, s_matchClubDivision, s_matchClubRank, s_matchOppRank,
            s_matchIsDerby, readCurrentCharismaPoints(), s_matchOpponentName);
    }

    // Snapshot pre-match squad injury bytes so Medical Center can reduce newly acquired injuries
    s_preMatchInjuryBytes.fill(0);
    const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];
    uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    uint16_t count = std::min<uint16_t>(numPlayers, kCurrentMatchPlayersSize);
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t slot = matchPlayers[i];
        if (slot >= kCurrentMatchPlayersSize)
            continue;
        size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
        if (ofs + HotseatInternal::kPlayerFileSize <= CareerOffsets::kCareerTeam + kCareerTeamBufferSize) {
            const auto *pf = reinterpret_cast<const PlayerFile *>(&SwosVM::g_memByte[ofs]);
            s_preMatchInjuryBytes[slot] = pf->cardsInjuries;
        }
    }
    s_preMatchInjuryCaptured = true;
}

void onPostMatchStadiumUpdate(bool isPlayedMatch)
{
    if (swos.g_gameType != kGameTypeCareer || swos.g_trainingGame || swos.gameCanceled ||
        HotseatInternal::isVmNationalSelector()) {
        s_matchTrackingActive = false;
    s_liveMatchPreview = StadiumMatchdayReport{};
        return;
    }

    ensureCurrentCareerStadiumInitialized();

    // If this was a View Result fast-sim without a prior pre-match hook, infer home/away from playMatchTeam pointers
    if (!s_matchTrackingActive) {
        const TeamFile *tf1 = swos.playMatchTeam1Ptr.asAligned();
        const TeamFile *tf2 = swos.playMatchTeam2Ptr.asAligned();
        if (!tf1 || !tf2)
            return;
        bool t1Human = (tf1->teamControls == kPlayerCoach || tf1->teamControls == kCoach);
        bool t2Human = (tf2->teamControls == kPlayerCoach || tf2->teamControls == kCoach);
        if (!t1Human && !t2Human)
            return;
        s_matchHumanIsHome = t1Human;
        s_matchOpponentName = t1Human ? tf2->teamName : tf1->teamName;
        s_matchClubDivision = readVmDivisionTier();
        s_matchClubRank = estimateClubRankFromSeasonRecord();
        s_matchOppRank = 8;
        s_matchIsDerby = detectClassicDerby(t1Human ? tf1->teamName : tf2->teamName, s_matchOpponentName, s_matchClubRank, s_matchOppRank);
        s_preMatchGateReceipts = *reinterpret_cast<const int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGateReceipts]);
    }
    s_matchTrackingActive = false;
    s_liveMatchPreview = StadiumMatchdayReport{};

    // 0. Record last-5 match form & update dynamic effective ticket price
    {
        int g1 = isPlayedMatch
            ? static_cast<int>(swos.statsTeam1Goals)
            : static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[379094]));
        int g2 = isPlayedMatch
            ? static_cast<int>(swos.statsTeam2Goals)
            : static_cast<int>(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[379096]));
        int clubG = s_matchHumanIsHome ? g1 : g2;
        int oppG = s_matchHumanIsHome ? g2 : g1;
        stadiumRecordMatchForm(s_stadium, clubG, oppG);
        s_stadium.ticketPrice = getEffectiveStadiumTicketPrice(s_stadium, s_matchClubDivision);
    }

    // 1. Medical Center: prevent knocks (L2-L5), shorten new injuries (L1-L5), & boost weekly rehab (L3-L5)
    std::vector<std::string> medicalNotes;
    if (s_preMatchInjuryCaptured) {
        const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];
        uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        uint16_t count = std::min<uint16_t>(numPlayers, kCurrentMatchPlayersSize);
        int preventPct = getMedicalInjuryPreventionPct(s_stadium.medicalLevel);
        int rehabPct = getMedicalRecoveryBoostPct(s_stadium.medicalLevel);
        uint32_t medSeed = static_cast<uint32_t>(SDL_GetTicks() ^ (s_stadium.seasonHomeMatches * 131u + 17u));

        for (uint16_t i = 0; i < count; ++i) {
            uint8_t slot = matchPlayers[i];
            if (slot >= kCurrentMatchPlayersSize)
                continue;
            size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                         static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
            if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
                continue;
            auto *pf = reinterpret_cast<PlayerFile *>(&SwosVM::g_memByte[ofs]);
            uint8_t prevInj = (s_preMatchInjuryBytes[slot] >> 4) & 0x0Fu;
            uint8_t curInj = (pf->cardsInjuries >> 4) & 0x0Fu;
            uint32_t roll = (medSeed + static_cast<uint32_t>(slot + 3) * 2654435761u) % 100u;

            if (curInj > prevInj) {
                if (preventPct > 0 && static_cast<int>(roll) < preventPct) {
                    pf->cardsInjuries = static_cast<uint8_t>(pf->cardsInjuries & 0x0Fu);
                    std::string note = std::string("KNOCK CLEARED: ") + pf->playerName +
                                       " cleared fit by Medical Lab (0 matches missed!)";
                    medicalNotes.push_back(note);
                    logInfo("Medical Center (Lvl %u) %s", static_cast<unsigned>(s_stadium.medicalLevel), note.c_str());
                } else if (curInj > 1) {
                    uint8_t reduced = computeMedicalReducedInjuryDays(curInj, s_stadium.medicalLevel);
                    if (reduced < curInj) {
                        pf->cardsInjuries = static_cast<uint8_t>((pf->cardsInjuries & 0x0Fu) | ((reduced & 0x0Fu) << 4));
                        std::string note = std::string("INJURY SHORTENED: ") + pf->playerName +
                                           " recovery cut from " + std::to_string(curInj) +
                                           " to " + std::to_string(reduced) + " matches";
                        medicalNotes.push_back(note);
                        logInfo("Medical Center (Lvl %u) %s", static_cast<unsigned>(s_stadium.medicalLevel), note.c_str());
                    }
                }
            } else if (prevInj > 1 && curInj > 1 && rehabPct > 0 && static_cast<int>(roll) < rehabPct) {
                uint8_t faster = static_cast<uint8_t>(curInj - 1);
                pf->cardsInjuries = static_cast<uint8_t>((pf->cardsInjuries & 0x0Fu) | ((faster & 0x0Fu) << 4));
                std::string note = std::string("REHAB BOOST: ") + pf->playerName +
                                   " ahead of schedule (-1 match -> " + std::to_string(faster) + " left)";
                medicalNotes.push_back(note);
                logInfo("Medical Center (Lvl %u) %s", static_cast<unsigned>(s_stadium.medicalLevel), note.c_str());
            }
        }
        if (!medicalNotes.empty())
            achievementsOnMedicalIntervention();
        s_preMatchInjuryCaptured = false;
    }

    // 2. Construction Project Countdown (every competitive match)
    bool completedBuild = false;
    StadiumModuleType completedMod = StadiumModuleType::kCapacity;
    uint8_t completedLvl = 0;
    if (stadiumAdvanceConstruction(s_stadium, completedMod, completedLvl)) {
        completedBuild = true;
        achievementsOnStadiumUpgradeCompleted();
        achievementsOnFacilitiesUpdated(
            s_stadium.pitchLevel, s_stadium.trainingLevel,
            s_stadium.medicalLevel, s_stadium.commercialLevel);
        logInfo("STADIUM UPGRADE COMPLETE: %s reached Level %u!",
            getStadiumModuleName(completedMod), static_cast<unsigned>(completedLvl));
    }

    // 3. Home Matchday Gate Report & Financial Integration
    if (s_matchHumanIsHome) {
        int16_t cp = readCurrentCharismaPoints();
        StadiumMatchdayReport rep = stadiumCalculateMatchday(
            s_stadium, s_matchClubDivision, s_matchClubRank, s_matchOppRank,
            s_matchIsDerby, cp, s_matchOpponentName);
        rep.constructionCompleted = completedBuild;
        if (completedBuild) {
            rep.completedModuleName = getStadiumModuleName(completedMod);
            rep.completedNewLevel = completedLvl;
            if (!rep.notableReason.empty())
                rep.notableReason += " + ";
            rep.notableReason += "UPGRADE COMPLETED";
        }

        // Replace SWOS's legacy home gate receipt delta cleanly with our Stadium net profit
        auto *vmGate = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGateReceipts]);
        auto *vmRun = reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGeneralRunningCosts]);
        int32_t legacyGateDelta = std::max<int32_t>(0, *vmGate - s_preMatchGateReceipts);
        int64_t grossRevenue = rep.ticketRevenue + rep.commercialRevenue;
        int64_t gateDiff = grossRevenue - static_cast<int64_t>(legacyGateDelta);

        *vmGate = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(*vmGate) + gateDiff, 0, 2000000000LL));
        *vmRun = static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(*vmRun) + rep.upkeepCost, 0, 2000000000LL));
        applyVmCashDelta(rep.netProfit - static_cast<int64_t>(legacyGateDelta));

        auto& mgr = HotseatCareerManager::get();
        if (mgr.isActive() && mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
            auto& p = mgr.activePlayer();
            p.gateReceipts = *vmGate;
            p.generalRunningCosts = *vmRun;
        }

        s_stadium.seasonTotalAttendance += rep.attendance;
        s_stadium.seasonGateRevenue += grossRevenue;
        s_stadium.seasonUpkeepPaid += rep.upkeepCost;
        ++s_stadium.seasonHomeMatches;

        // Tick down transfer buzz/backlash home-match modifier after this home game
        if (s_stadium.transferBuzzHomeMatches > 0) {
            --s_stadium.transferBuzzHomeMatches;
            if (s_stadium.transferBuzzHomeMatches == 0) {
                s_stadium.transferTicketDelta = 0;
                s_stadium.transferDemandPctDelta = 0;
                std::memset(s_stadium.transferBuzzLabel, 0, sizeof(s_stadium.transferBuzzLabel));
            }
            s_stadium.ticketPrice = getEffectiveStadiumTicketPrice(s_stadium, s_matchClubDivision);
        }

        // Fire Theatre of Dreams (85k sellout) achievement hook
        achievementsOnStadiumMatchAttendance(
            static_cast<int>(s_stadium.capacityLevel),
            static_cast<int>(rep.attendance),
            static_cast<int>(rep.maxCapacity));

        rep.medicalNotes = medicalNotes;
        if (!medicalNotes.empty()) {
            if (!rep.notableReason.empty())
                rep.notableReason += " + ";
            rep.notableReason += "MEDICAL LAB REPORT";
        }
        s_lastReport = rep;
        // Show popup on notable home matches (sellout, derby, upgrade completed, or visible Medical Center save!)
        s_pendingNotableReportPopup = isPlayedMatch &&
            (rep.isSellout || rep.isDerby || rep.constructionCompleted || !medicalNotes.empty());

        logInfo("Stadium home gate (%s vs %s): %u/%u (%d%%), Ticket=%s, VIP=%s+Merch(+%d%%)=%s, Upkeep=-%s, Net=%s",
            rep.stadiumName.c_str(), rep.opponentName.c_str(),
            static_cast<unsigned>(rep.attendance), static_cast<unsigned>(rep.maxCapacity), rep.fillPct,
            formatPoundsExact(rep.ticketRevenue).c_str(),
            formatPoundsExact(rep.vipBoxRevenue).c_str(), rep.commercialBonusPct,
            formatPoundsExact(rep.merchRevenue).c_str(), formatPoundsExact(rep.upkeepCost).c_str(),
            formatSignedPoundsExact(rep.netProfit).c_str());
    } else if (completedBuild || !medicalNotes.empty()) {
        // Away match finished a construction project or Medical Center intervened — show concise report!
        StadiumMatchdayReport rep;
        rep.valid = true;
        rep.isHomeMatch = false;
        rep.stadiumName = s_stadium.stadiumName;
        rep.constructionCompleted = completedBuild;
        rep.completedModuleName = completedBuild ? getStadiumModuleName(completedMod) : "";
        rep.completedNewLevel = completedLvl;
        rep.medicalNotes = medicalNotes;
        rep.notableReason = completedBuild ? "UPGRADE COMPLETED" : "MEDICAL LAB REPORT";
        s_lastReport = rep;
        s_pendingNotableReportPopup = isPlayedMatch;
    }

    syncStadiumToHotseatIfActive();
}

void showPostMatchStadiumReportIfNotable()
{
    if (!s_pendingNotableReportPopup || !s_lastReport.valid || swos.gameCanceled || swos.g_trainingGame)
        return;
    s_pendingNotableReportPopup = false;

#ifndef SWOS_TEST
    const auto& r = s_lastReport;
    std::string title = r.isHomeMatch
        ? (r.stadiumName + " - MATCHDAY REPORT")
        : (r.constructionCompleted ? "STADIUM UPGRADE COMPLETE!" : "MEDICAL CENTER REPORT");
    std::string body;

    if (r.constructionCompleted) {
        body += "CONSTRUCTION COMPLETE!\n";
        body += r.completedModuleName + " has been upgraded to LEVEL " +
                std::to_string(r.completedNewLevel) + "!\n\n";
    }

    if (!r.medicalNotes.empty()) {
        body += "MEDICAL CENTER (LVL " + std::to_string(s_stadium.medicalLevel) + "):\n";
        for (const auto& note : r.medicalNotes)
            body += "  - " + note + "\n";
        body += "\n";
    }

    if (r.isHomeMatch) {
        if (!r.notableReason.empty())
            body += "EVENT: " + r.notableReason + "\n";
        body += "MATCH: HOME vs " + r.opponentName + "\n";
        body += "ATTENDANCE: " + formatNumberCommas(r.attendance) + " / " +
                formatNumberCommas(r.maxCapacity) + " (" + std::to_string(r.fillPct) + "% FULL)\n\n";
        std::string formTag;
        if (r.formTicketDelta != 0)
            formTag = (r.formTicketDelta > 0 ? " [+" : " [") + std::to_string(r.formTicketDelta) + " FORM]";
        body += "TICKET REVENUE (" + formatNumberCommas(r.attendance) + " x GBP " +
                std::to_string(r.ticketPrice) + formTag + "): " + formatPoundsExact(r.ticketRevenue) + "\n";
        body += "VIP BOXES (FLAT): +" + formatPoundsExact(r.vipBoxRevenue) +
                "  |  MERCH (+" + std::to_string(r.commercialBonusPct) + "%): +" +
                formatPoundsExact(r.merchRevenue) + "\n";
        body += "STADIUM MAINTENANCE UPKEEP: -" + formatPoundsExact(r.upkeepCost) + "\n\n";
        body += "NET MATCHDAY STADIUM PROFIT: " + formatSignedPoundsExact(r.netProfit) + "\n";
        body += "UPDATED BANK BALANCE: " + formatPoundsExact(readVmClubCash());
    } else if (r.constructionCompleted) {
        body += "Your club's new " + r.completedModuleName + " (Level " +
                std::to_string(r.completedNewLevel) + ") is now fully operational!\n\n";
        body += "Press F6 in the Career Hub to inspect all Stadium & Facilities upgrades.";
    }

    setStandardMenuBackgroundImage();
    menuFadeIn();
    showLlmReportDialog(title.c_str(), body);
    menuFadeOut();
#endif
}

void onEndOfSeasonStadiumProgression()
{
    if (swos.g_gameType != kGameTypeCareer || HotseatInternal::isVmNationalSelector())
        return;

    ensureCurrentCareerStadiumInitialized();

    // Training Complex: +5% to +25% chance per player to gain +1 in a skill nibble (0..7) + proportional value bump
    double bonusChance = getTrainingGrowthBonusFraction(s_stadium.trainingLevel);
    uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    uint16_t count = std::min<uint16_t>(numPlayers, kCurrentMatchPlayersSize);
    const uint8_t *matchPlayers = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];
    uint16_t seasonIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);

    int improvedPlayers = 0;
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t slot = matchPlayers[i];
        if (slot >= kCurrentMatchPlayersSize)
            continue;
        size_t ofs = CareerOffsets::kCareerTeam + HotseatInternal::kTeamFileHeaderSize +
                     static_cast<size_t>(slot) * HotseatInternal::kPlayerFileSize;
        if (ofs + HotseatInternal::kPlayerFileSize > CareerOffsets::kCareerTeam + kCareerTeamBufferSize)
            continue;
        auto *pf = reinterpret_cast<PlayerFile *>(&SwosVM::g_memByte[ofs]);
        if (pf->playerName[0] == '\0' || std::strncmp(pf->playerName, "*ERR*", 5) == 0)
            continue;

        // Deterministic roll seeded by season, teamId, and slot
        uint32_t seed = static_cast<uint32_t>((seasonIdx + 1) * 1103515245u +
                        static_cast<uint32_t>(slot + 7) * 2654435761u +
                        static_cast<uint32_t>(s_stadium.trainingLevel * 97));
        double roll = static_cast<double>(seed % 1000u) / 1000.0;
        if (roll < bonusChance) {
            // Pick one of the 7 skill nibbles that is below 7 and bump by +1
            uint8_t *skillBytes[4] = { &pf->passing, &pf->shootingHeading, &pf->tacklingBallControl, &pf->speedFinishing };
            bool upgraded = false;
            for (int attempt = 0; attempt < 7 && !upgraded; ++attempt) {
                int skillIdx = static_cast<int>((seed / 13u + attempt) % 7u);
                if (skillIdx == 0) {
                    uint8_t v = pf->passing & 0x07u;
                    if (v < 7) {
                        pf->passing = static_cast<uint8_t>((pf->passing & 0xF8u) | (v + 1));
                        upgraded = true;
                    }
                } else {
                    int bIdx = 1 + ((skillIdx - 1) / 2);
                    bool highNib = ((skillIdx - 1) % 2) == 0;
                    uint8_t b = *skillBytes[bIdx];
                    uint8_t v = highNib ? ((b >> 4) & 0x07u) : (b & 0x07u);
                    if (v < 7) {
                        if (highNib)
                            *skillBytes[bIdx] = static_cast<uint8_t>((b & 0x8Fu) | ((v + 1) << 4));
                        else
                            *skillBytes[bIdx] = static_cast<uint8_t>((b & 0xF8u) | (v + 1));
                        upgraded = true;
                    }
                }
            }
            if (upgraded) {
                if (pf->price < 48)
                    ++pf->price;
                ++improvedPlayers;
            }
        }
    }

    if (improvedPlayers > 0) {
        logInfo("Training Complex (Lvl %u, +%.0f%%) boosted %d squad players at end of season!",
            static_cast<unsigned>(s_stadium.trainingLevel), bonusChance * 100.0, improvedPlayers);
    }

    // Reset seasonal counters for the new season
    s_stadium.seasonTotalAttendance = 0;
    s_stadium.seasonGateRevenue = 0;
    s_stadium.seasonUpkeepPaid = 0;
    s_stadium.seasonHomeMatches = 0;
    syncStadiumToHotseatIfActive();
}

// --- Single-player sidecar (.std) persistence ---

bool saveStadiumSidecar(const std::string& path)
{
    if (!s_stadium.initialized)
        return false;

    CSimpleIniA ini;
    ini.SetUnicode();
    ini.SetValue("stadium", "name", s_stadium.stadiumName);
    ini.SetLongValue("stadium", "teamId", s_stadium.teamId);
    ini.SetLongValue("stadium", "capacityLevel", s_stadium.capacityLevel);
    ini.SetLongValue("stadium", "pitchLevel", s_stadium.pitchLevel);
    ini.SetLongValue("stadium", "trainingLevel", s_stadium.trainingLevel);
    ini.SetLongValue("stadium", "medicalLevel", s_stadium.medicalLevel);
    ini.SetLongValue("stadium", "commercialLevel", s_stadium.commercialLevel);
    ini.SetLongValue("stadium", "ticketPolicy", static_cast<long>(s_stadium.ticketPolicy));
    ini.SetLongValue("stadium", "ticketPrice", s_stadium.ticketPrice);
    ini.SetValue("stadium", "recentForm", s_stadium.recentForm);
    ini.SetLongValue("stadium", "transferTicketDelta", s_stadium.transferTicketDelta);
    ini.SetLongValue("stadium", "transferDemandPctDelta", s_stadium.transferDemandPctDelta);
    ini.SetLongValue("stadium", "transferBuzzHomeMatches", s_stadium.transferBuzzHomeMatches);
    ini.SetValue("stadium", "transferBuzzLabel", s_stadium.transferBuzzLabel);

    ini.SetBoolValue("project", "isActive", s_stadium.activeProject.isActive);
    ini.SetLongValue("project", "module", static_cast<long>(s_stadium.activeProject.module));
    ini.SetLongValue("project", "targetLevel", s_stadium.activeProject.targetLevel);
    ini.SetLongValue("project", "matchesRemaining", s_stadium.activeProject.matchesRemaining);
    ini.SetLongValue("project", "costPaid", s_stadium.activeProject.costPaid);

    ini.SetValue("season", "totalAttendance", std::to_string(s_stadium.seasonTotalAttendance).c_str());
    ini.SetValue("season", "gateRevenue", std::to_string(s_stadium.seasonGateRevenue).c_str());
    ini.SetValue("season", "upkeepPaid", std::to_string(s_stadium.seasonUpkeepPaid).c_str());
    ini.SetLongValue("season", "homeMatches", s_stadium.seasonHomeMatches);

    return ini.SaveFile(path.c_str()) >= 0;
}

bool loadStadiumSidecar(const std::string& path)
{
    CSimpleIniA ini;
    ini.SetUnicode();
    if (ini.LoadFile(path.c_str()) < 0)
        return false;

    StadiumData d;
    d.initialized = true;
    const char *nm = ini.GetValue("stadium", "name", "CLUB STADIUM");
    std::strncpy(d.stadiumName, nm ? nm : "CLUB STADIUM", sizeof(d.stadiumName) - 1);
    d.stadiumName[sizeof(d.stadiumName) - 1] = '\0';
    d.teamId = static_cast<int16_t>(ini.GetLongValue("stadium", "teamId", -1));
    d.capacityLevel = static_cast<uint8_t>(std::clamp(ini.GetLongValue("stadium", "capacityLevel", 1), 1L, 10L));
    d.maxCapacity = getStadiumCapacityForLevel(d.capacityLevel);
    d.pitchLevel = static_cast<uint8_t>(std::clamp(ini.GetLongValue("stadium", "pitchLevel", 1), 1L, 5L));
    d.trainingLevel = static_cast<uint8_t>(std::clamp(ini.GetLongValue("stadium", "trainingLevel", 1), 1L, 5L));
    d.medicalLevel = static_cast<uint8_t>(std::clamp(ini.GetLongValue("stadium", "medicalLevel", 1), 1L, 5L));
    d.commercialLevel = static_cast<uint8_t>(std::clamp(ini.GetLongValue("stadium", "commercialLevel", 1), 1L, 5L));
    d.ticketPolicy = static_cast<TicketPolicy>(std::clamp(ini.GetLongValue("stadium", "ticketPolicy", 1), 0L, 2L));
    d.ticketPrice = static_cast<int32_t>(std::clamp(ini.GetLongValue("stadium", "ticketPrice", 10), 1L, 100L));
    const char *formStr = ini.GetValue("stadium", "recentForm", "");
    std::memset(d.recentForm, 0, sizeof(d.recentForm));
    if (formStr) {
        int outIdx = 0;
        for (int i = 0; formStr[i] != '\0' && outIdx < 5; ++i) {
            char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(formStr[i])));
            if (ch == 'W' || ch == 'D' || ch == 'L')
                d.recentForm[outIdx++] = ch;
        }
    }
    d.transferTicketDelta = static_cast<int8_t>(std::clamp(ini.GetLongValue("stadium", "transferTicketDelta", 0), -4L, 5L));
    d.transferDemandPctDelta = static_cast<int8_t>(std::clamp(ini.GetLongValue("stadium", "transferDemandPctDelta", 0), -20L, 25L));
    d.transferBuzzHomeMatches = static_cast<uint8_t>(std::clamp(ini.GetLongValue("stadium", "transferBuzzHomeMatches", 0), 0L, 10L));
    const char *tbLbl = ini.GetValue("stadium", "transferBuzzLabel", "");
    std::memset(d.transferBuzzLabel, 0, sizeof(d.transferBuzzLabel));
    if (tbLbl && tbLbl[0] != '\0')
        std::strncpy(d.transferBuzzLabel, tbLbl, sizeof(d.transferBuzzLabel) - 1);

    d.activeProject.isActive = ini.GetBoolValue("project", "isActive", false);
    d.activeProject.module = static_cast<StadiumModuleType>(std::clamp(ini.GetLongValue("project", "module", 0), 0L, 4L));
    d.activeProject.targetLevel = static_cast<uint8_t>(std::clamp(ini.GetLongValue("project", "targetLevel", 0), 0L, 10L));
    d.activeProject.matchesRemaining = static_cast<uint8_t>(std::clamp(ini.GetLongValue("project", "matchesRemaining", 0), 0L, 40L));
    d.activeProject.costPaid = static_cast<int32_t>(std::max(0L, ini.GetLongValue("project", "costPaid", 0)));

    const char *attStr = ini.GetValue("season", "totalAttendance", "0");
    const char *revStr = ini.GetValue("season", "gateRevenue", "0");
    const char *upkStr = ini.GetValue("season", "upkeepPaid", "0");
    d.seasonTotalAttendance = attStr ? static_cast<uint32_t>(std::strtoul(attStr, nullptr, 10)) : 0;
    d.seasonGateRevenue = revStr ? std::strtoll(revStr, nullptr, 10) : 0;
    d.seasonUpkeepPaid = upkStr ? std::strtoll(upkStr, nullptr, 10) : 0;
    d.seasonHomeMatches = static_cast<uint16_t>(std::max(0L, ini.GetLongValue("season", "homeMatches", 0)));

    s_stadium = d;
    return true;
}

// --- Career Hub Clickable Pill Button + F6 Hotkey ---

namespace {

bool isCareerHubStadiumAccessible()
{
    if (swos.g_gameType != kGameTypeCareer || HotseatInternal::isVmNationalSelector())
        return false;
    auto packed = getCurrentPackedMenu();
    if (!packed || !SwosVM::isSwosPtr(packed))
        return false;
    return SwosVM::ptrToOffset(packed) == 482233;
}

SDL_FRect getCareerHubStadiumButtonScreenRect()
{
    float gameScale = getGameScale();
    float offX = getGameScreenOffsetX();
    float offY = getGameScreenOffsetY();
    // Placed in the clean top-right open margin of contestMenu (VGA x=232..316, y=1..14)
    return { offX + 232.0f * gameScale, offY + 1.0f * gameScale, 84.0f * gameScale, 13.0f * gameScale };
}

} // namespace

void drawCareerHubStadiumButton()
{
#ifdef SWOS_TEST
    return;
#else
    if (!isCareerHubStadiumAccessible())
        return;

    ensureCurrentCareerStadiumInitialized();
    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = getCompactFontAtlas(r);
    if (!r || !atlas)
        return;

    SDL_FRect rc = getCareerHubStadiumButtonScreenRect();
    float gameScale = getGameScale();
    float px = computeCompactPixelScaleForVga(gameScale, 0.36f);

    float origSX = 1.0f, origSY = 1.0f;
    SDL_RenderGetScale(r, &origSX, &origSY);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    bool building = s_stadium.activeProject.isActive;
    SDL_SetRenderDrawColor(r, building ? 24 : 18, building ? 42 : 34, building ? 58 : 26, 220);
    SDL_RenderFillRectF(r, &rc);
    SDL_SetRenderDrawColor(r, building ? 112 : 255, building ? 228 : 214, building ? 248 : 74, 225);
    SDL_RenderDrawRectF(r, &rc);

    std::string label = building
        ? ("[F6] STADIUM (" + std::to_string(s_stadium.activeProject.matchesRemaining) + "M)")
        : "[F6] STADIUM";
    float needed = static_cast<float>(measureCompactTextUnits(label)) * px;
    float fitPx = (needed > rc.w - 6.0f) ? std::max(0.75f, px * ((rc.w - 6.0f) / needed)) : px;
    SDL_Color col = building ? SDL_Color{ 118, 238, 250, 255 } : SDL_Color{ 255, 226, 86, 255 };
    drawCompactTextCentered(r, atlas, rc.x + rc.w * 0.5f, rc.y + (rc.h - 8.0f * fitPx) * 0.5f,
        fitPx, label, col, 255, true);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_RenderSetScale(r, origSX, origSY);
#endif
}

bool handleCareerHubStadiumInput()
{
#ifdef SWOS_TEST
    return false;
#else
    if (!isCareerHubStadiumAccessible()) {
        s_prevHubF6Down = false;
        s_prevHubMouseDown = false;
        return false;
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    bool f6Now = keys && keys[SDL_SCANCODE_F6] != 0;
    bool triggered = (f6Now && !s_prevHubF6Down);
    s_prevHubF6Down = f6Now;

    int mx = 0, my = 0;
    Uint32 buttons = SDL_GetMouseState(&mx, &my);
    bool lmbNow = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    if (lmbNow && !s_prevHubMouseDown) {
        SDL_FRect rc = getCareerHubStadiumButtonScreenRect();
        if (mx >= rc.x && mx <= rc.x + rc.w && my >= rc.y && my <= rc.y + rc.h)
            triggered = true;
    }
    s_prevHubMouseDown = lmbNow;

    if (triggered) {
        showStadiumFacilitiesMenu();
        return true;
    }
    return false;
#endif
}

// --- Native Stadium & Facilities Management Menu Callbacks ---

static void initStadiumFacilitiesMenu()
{
    ensureCurrentCareerStadiumInitialized();
    s_confirmingUpgrade = false;
    s_statusFeedback.clear();
    std::memset(s_prevMenuKeys, 0, sizeof(s_prevMenuKeys));
    updateConfirmButtonsVisibility();
    setCurrentEntry(modCapBtn);
}

static void selectStadiumModuleCap()   { if (!s_confirmingUpgrade) beginUpgradeConfirmation(StadiumModuleType::kCapacity); }
static void selectStadiumModulePitch() { if (!s_confirmingUpgrade) beginUpgradeConfirmation(StadiumModuleType::kPitch); }
static void selectStadiumModuleTrain() { if (!s_confirmingUpgrade) beginUpgradeConfirmation(StadiumModuleType::kTraining); }
static void selectStadiumModuleMed()   { if (!s_confirmingUpgrade) beginUpgradeConfirmation(StadiumModuleType::kMedical); }
static void selectStadiumModuleComm()  { if (!s_confirmingUpgrade) beginUpgradeConfirmation(StadiumModuleType::kCommercial); }

static void cycleStadiumTicketPolicyDir(int dir)
{
    if (s_confirmingUpgrade)
        return;
    ensureCurrentCareerStadiumInitialized();
    int cur = static_cast<int>(s_stadium.ticketPolicy);
    cur = (cur + dir + 3) % 3;
    s_stadium.ticketPolicy = static_cast<TicketPolicy>(cur);
    int div = readVmDivisionTier();
    s_stadium.ticketPrice = getEffectiveStadiumTicketPrice(s_stadium, div);
    syncStadiumToHotseatIfActive();
    int32_t formAdj = getStadiumFormTicketAdjustment(s_stadium);
    std::string adjStr = (formAdj > 0) ? (" [+" + std::to_string(formAdj) + " FORM]") :
                         (formAdj < 0) ? (" [" + std::to_string(formAdj) + " FORM]") : "";
    setStatusFeedback("Ticket policy: " + std::string(getTicketPolicyName(s_stadium.ticketPolicy)) +
                      " -> GBP " + std::to_string(s_stadium.ticketPrice) + adjStr);
}

static void cycleStadiumTicketPolicy()
{
    cycleStadiumTicketPolicyDir(1);
}

static void confirmStadiumUpgradeYes()
{
    if (!s_confirmingUpgrade)
        return;
    int64_t cash = readVmClubCash();
    int64_t beforeCash = cash;
    if (stadiumStartUpgrade(s_stadium, cash, s_confirmModule)) {
        applyVmCashDelta(cash - beforeCash);
        syncStadiumToHotseatIfActive();
        setStatusFeedback("Started upgrading " + std::string(getStadiumModuleName(s_confirmModule)) +
                          " to Lvl " + std::to_string(s_stadium.activeProject.targetLevel) +
                          " (" + std::to_string(s_stadium.activeProject.matchesRemaining) + " matches)!");
    } else {
        setStatusFeedback("Upgrade could not be started.");
    }
    s_confirmingUpgrade = false;
    updateConfirmButtonsVisibility();
    setCurrentEntry(modCapBtn);
}

static void confirmStadiumUpgradeNo()
{
    s_confirmingUpgrade = false;
    s_statusFeedback.clear();
    updateConfirmButtonsVisibility();
    setCurrentEntry(modCapBtn);
}

static void closeStadiumFacilitiesMenu()
{
    SetExitMenuFlag();
}

static void drawStadiumFacilitiesMenu()
{
    // Wide dark transparent panels (x=4..316, width=312)
    drawDarkRectangle({ 4.0f, 16.0f, 312.0f, 16.0f }, true);  // Stadium name + bank bar
    static constexpr float kModYs[5] = { 35.0f, 56.0f, 77.0f, 98.0f, 119.0f };
    for (int i = 0; i < 5; ++i)
        drawDarkRectangle({ 4.0f, kModYs[i], 312.0f, 19.0f }, true);
    drawDarkRectangle({ 4.0f, 141.0f, 312.0f, 17.0f }, true); // Ticket policy bar
    drawDarkRectangle({ 4.0f, 160.0f, 312.0f, 19.0f }, true); // Construction / Season / Feedback bar

    SDL_Renderer *r = getRenderer();
    SDL_Texture *atlas = getCompactFontAtlas(r);
    if (r && atlas) {
        float gameScale = getGameScale();
        float px = computeCompactPixelScaleForVga(gameScale, 0.42f);
        float offX = getGameScreenOffsetX();
        float offY = getGameScreenOffsetY();

        float origSX = 1.0f, origSY = 1.0f;
        SDL_RenderGetScale(r, &origSX, &origSY);
        SDL_RenderSetScale(r, 1.0f, 1.0f);

        SDL_Color whiteCol{ 244, 246, 248, 255 };
        SDL_Color mutedCol{ 164, 174, 182, 255 };
        SDL_Color yellowCol{ 255, 226, 78, 255 };
        SDL_Color greenCol{ 96, 236, 134, 255 };
        SDL_Color cyanCol{ 118, 238, 248, 255 };
        SDL_Color redCol{ 248, 110, 110, 255 };

        auto fitPx = [](const std::string& s, float basePx, float maxW) -> float {
            int u = measureCompactTextUnits(s);
            if (u <= 0 || maxW <= 1.0f) return basePx;
            float needed = static_cast<float>(u) * basePx;
            return (needed <= maxW) ? basePx : std::max(0.75f, basePx * (maxW / needed));
        };

        int div = readVmDivisionTier();
        int64_t cash = readVmClubCash();

        // Top stadium & bank bar
        std::string stLabel = std::string("GROUND: ") + s_stadium.stadiumName;
        drawCompactText(r, atlas, offX + 10.0f * gameScale, offY + 20.5f * gameScale,
            fitPx(stLabel, px, 175.0f * gameScale), stLabel, cyanCol, 255, true);

        std::string bankLabel = "BANK: " + formatPoundsExact(cash);
        drawCompactTextRightAligned(r, atlas, offX + 310.0f * gameScale, offY + 20.5f * gameScale,
            fitPx(bankLabel, px, 125.0f * gameScale), bankLabel, (cash >= 0 ? yellowCol : redCol), 255, true);

        // 5 Module rows
        for (int i = 0; i < 5; ++i) {
            auto mod = static_cast<StadiumModuleType>(i);
            uint8_t curLvl = getModuleLevel(s_stadium, mod);
            uint8_t maxLvl = (mod == StadiumModuleType::kCapacity) ? kMaxCapacityLevel : kMaxFacilityLevel;
            StadiumUpgradeQuote q = stadiumGetUpgradeQuote(s_stadium, mod, cash);
            bool isBuildingThis = (s_stadium.activeProject.isActive && s_stadium.activeProject.module == mod);

            float rowY = offY + (kModYs[i] + 2.5f) * gameScale;
            float leftX = offX + 10.0f * gameScale;

            std::string leftHead = "[" + std::to_string(i + 1) + "] " + getStadiumModuleName(mod);
            std::string curStat;
            switch (mod) {
            case StadiumModuleType::kCapacity:
                curStat = "LVL " + std::to_string(curLvl) + "/10 (" + formatNumberCommas(s_stadium.maxCapacity) + " SEATS)";
                break;
            case StadiumModuleType::kPitch:
                curStat = "LVL " + std::to_string(curLvl) + "/5 (" + getPitchQualityLabel(curLvl) + ")";
                break;
            case StadiumModuleType::kTraining:
                curStat = "LVL " + std::to_string(curLvl) + "/5 (+" + std::to_string(curLvl * 5) + "% GROWTH)";
                break;
            case StadiumModuleType::kMedical:
                curStat = "LVL " + std::to_string(curLvl) + "/5 (" +
                          std::to_string(getMedicalInjuryPreventionPct(curLvl)) + "% PREV, -" +
                          std::to_string(curLvl * 10) + "% INJ)";
                break;
            case StadiumModuleType::kCommercial:
                curStat = "LVL " + std::to_string(curLvl) + "/5 (" +
                          formatPoundsShort(getStadiumVipBoxIncome(curLvl, div)) + " VIP, +" +
                          std::to_string(getStadiumCommercialSponsorBonusPct(curLvl)) + "% SPON)";
                break;
            default:
                break;
            }

            drawCompactText(r, atlas, leftX, rowY, px, leftHead, yellowCol, 255, true);
            drawCompactText(r, atlas, offX + 105.0f * gameScale, rowY,
                fitPx(curStat, px, 110.0f * gameScale), curStat, whiteCol, 240, false);

            std::string rightTag;
            SDL_Color rightCol = greenCol;
            if (isBuildingThis) {
                rightTag = "BUILDING L" + std::to_string(s_stadium.activeProject.targetLevel) +
                           " (" + std::to_string(s_stadium.activeProject.matchesRemaining) + "M)";
                rightCol = cyanCol;
            } else if (q.status == StadiumUpgradeStatus::kMaxLevel) {
                rightTag = "MAX LEVEL";
                rightCol = cyanCol;
            } else if (q.status == StadiumUpgradeStatus::kInsufficientFunds) {
                rightTag = "UPG: " + formatPoundsShort(q.cost);
                rightCol = redCol;
            } else if (q.status == StadiumUpgradeStatus::kConstructionInProgress) {
                rightTag = "UPG: " + formatPoundsShort(q.cost);
                rightCol = mutedCol;
            } else {
                rightTag = "UPG: " + formatPoundsShort(q.cost) + " (" + std::to_string(q.buildMatches) + "M)";
                rightCol = greenCol;
            }
            drawCompactTextRightAligned(r, atlas, offX + 310.0f * gameScale, rowY,
                fitPx(rightTag, px, 96.0f * gameScale), rightTag, rightCol, 255, true);

            // Second sub-line showing next-level effect or upkeep
            std::string subLine;
            if (q.status == StadiumUpgradeStatus::kMaxLevel) {
                subLine = "Fully upgraded! Matchday upkeep: " + formatPoundsExact(getStadiumMatchdayUpkeep(s_stadium));
            } else {
                subLine = "Next Lvl " + std::to_string(q.targetLevel) + ": " + q.previewText +
                          "  |  Upkeep: " + formatPoundsExact(q.newMatchdayUpkeep) + "/match";
            }
            drawCompactText(r, atlas, leftX + 6.0f * gameScale, rowY + 9.2f * gameScale,
                fitPx(subLine, px * 0.88f, 294.0f * gameScale), subLine, mutedCol, 220, false);
        }

        // Ticket pricing + Last-5 Form + Live Home Matchday P&L Breakdown bar (y=141..158)
        StadiumMatchdayReport est = stadiumCalculateMatchday(
            s_stadium, div, estimateClubRankFromSeasonRecord(), 8, false, readCurrentCharismaPoints());
        std::string formTag;
        if (est.formTicketDelta > 0)
            formTag += "+" + std::to_string(est.formTicketDelta) + "F";
        else if (est.formTicketDelta < 0)
            formTag += std::to_string(est.formTicketDelta) + "F";
        if (est.transferTicketDelta > 0)
            formTag += "+" + std::to_string(est.transferTicketDelta) + "H";
        else if (est.transferTicketDelta < 0)
            formTag += std::to_string(est.transferTicketDelta) + "H";
        std::string tickLeft = "[T] TICKETS: < " + std::string(getTicketPolicyName(s_stadium.ticketPolicy)) +
                               " (GBP " + std::to_string(est.baseTicketPrice) +
                               (formTag.empty() ? "" : (" " + formTag + "=GBP " + std::to_string(est.ticketPrice))) + ") >";
        int fScore = getStadiumFormScore(s_stadium);
        std::string formDisp = s_stadium.recentForm[0]
            ? ("FORM(5): " + std::string(s_stadium.recentForm) + " (" + (fScore >= 0 ? "+" : "") + std::to_string(fScore) + ")")
            : "FORM(5): ----- (0)";
        SDL_Color formCol = (fScore >= 3) ? greenCol : ((fScore <= -3) ? redCol : cyanCol);

        drawCompactText(r, atlas, offX + 10.0f * gameScale, offY + 142.5f * gameScale,
            fitPx(tickLeft, px * 0.92f, 195.0f * gameScale), tickLeft, yellowCol, 255, true);
        drawCompactTextRightAligned(r, atlas, offX + 310.0f * gameScale, offY + 142.5f * gameScale,
            fitPx(formDisp, px * 0.90f, 102.0f * gameScale), formDisp, formCol, 255, true);

        std::string pnlLine = "EST HOME P&L (" + std::to_string(est.fillPct) + "%): Gate " +
                              formatPoundsShort(est.ticketRevenue) + " + VIP/Merch " +
                              formatPoundsShort(est.commercialRevenue) + " - Upkeep " +
                              formatPoundsShort(est.upkeepCost) + " = " +
                              formatSignedPoundsExact(est.netProfit);
        drawCompactText(r, atlas, offX + 10.0f * gameScale, offY + 150.5f * gameScale,
            fitPx(pnlLine, px * 0.84f, 300.0f * gameScale), pnlLine, (est.netProfit >= 0 ? greenCol : redCol), 240, false);

        // Status / Construction / Season summary bar (y=160..179)
        bool showFeedback = (!s_statusFeedback.empty() && (s_confirmingUpgrade || (SDL_GetTicks() - s_statusFeedbackTick) < 5000));
        if (showFeedback) {
            SDL_Color fCol = s_confirmingUpgrade ? yellowCol : cyanCol;
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 166.0f * gameScale,
                fitPx(s_statusFeedback, px, 300.0f * gameScale), s_statusFeedback, fCol, 255, true);
        } else if (s_stadium.activeProject.isActive) {
            std::string bLine = "PROJECT: " + std::string(getStadiumModuleName(s_stadium.activeProject.module)) +
                                " -> LVL " + std::to_string(s_stadium.activeProject.targetLevel) +
                                "  (" + std::to_string(s_stadium.activeProject.matchesRemaining) + " MATCHES REMAINING)";
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 163.0f * gameScale,
                fitPx(bLine, px, 300.0f * gameScale), bLine, cyanCol, 255, true);
            std::string hint = "[1-5] Upgrade  |  [T / Left-Right] Ticket Price  |  [ESC] Back";
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 171.5f * gameScale,
                fitPx(hint, px * 0.85f, 300.0f * gameScale), hint, mutedCol, 220, false);
        } else if (s_stadium.transferBuzzHomeMatches > 0) {
            std::string buzzLine = "TRANSFER BUZZ: " +
                std::string(s_stadium.transferBuzzLabel[0] ? s_stadium.transferBuzzLabel : "ACTIVE") +
                " (" + (s_stadium.transferTicketDelta >= 0 ? "+GBP " : "-GBP ") +
                std::to_string(std::abs(static_cast<int>(s_stadium.transferTicketDelta))) + ", " +
                (s_stadium.transferDemandPctDelta >= 0 ? "+" : "") +
                std::to_string(static_cast<int>(s_stadium.transferDemandPctDelta)) + "% CROWD, " +
                std::to_string(s_stadium.transferBuzzHomeMatches) + " HOME LEFT)";
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 163.0f * gameScale,
                fitPx(buzzLine, px * 0.90f, 300.0f * gameScale), buzzLine,
                (s_stadium.transferDemandPctDelta >= 0 ? greenCol : redCol), 255, true);
            std::string hint = "[1-5] Upgrade Module  |  [T / Left-Right] Ticket Price  |  [ESC] Back";
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 171.5f * gameScale,
                fitPx(hint, px * 0.85f, 300.0f * gameScale), hint, mutedCol, 220, false);
        } else {
            std::string seasonLine = "SEASON HOME GAMES: " + std::to_string(s_stadium.seasonHomeMatches) +
                                     "  |  ATTENDANCE: " + formatNumberCommas(s_stadium.seasonTotalAttendance) +
                                     "  |  REV: " + formatPoundsShort(s_stadium.seasonGateRevenue);
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 163.0f * gameScale,
                fitPx(seasonLine, px * 0.92f, 300.0f * gameScale), seasonLine, whiteCol, 235, false);
            std::string hint = "[1-5] Upgrade Module  |  [T / Left-Right] Ticket Price  |  [ESC] Back";
            drawCompactTextCentered(r, atlas, offX + 160.0f * gameScale, offY + 171.5f * gameScale,
                fitPx(hint, px * 0.85f, 300.0f * gameScale), hint, mutedCol, 220, false);
        }

        SDL_RenderSetScale(r, origSX, origSY);
    }

    // Keyboard shortcuts: 1-5 upgrade module, T/Left/Right cycle ticket policy, Y/N confirm, ESC cancel/back
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        bool down[8] = {
            keys[SDL_SCANCODE_1] || keys[SDL_SCANCODE_KP_1],
            keys[SDL_SCANCODE_2] || keys[SDL_SCANCODE_KP_2],
            keys[SDL_SCANCODE_3] || keys[SDL_SCANCODE_KP_3],
            keys[SDL_SCANCODE_4] || keys[SDL_SCANCODE_KP_4],
            keys[SDL_SCANCODE_5] || keys[SDL_SCANCODE_KP_5],
            keys[SDL_SCANCODE_T] != 0,
            keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT],
            keys[SDL_SCANCODE_ESCAPE] != 0,
        };
        if (!s_confirmingUpgrade) {
            for (int i = 0; i < 5; ++i) {
                if (down[i] && !s_prevMenuKeys[i])
                    beginUpgradeConfirmation(static_cast<StadiumModuleType>(i));
            }
            if (down[5] && !s_prevMenuKeys[5])
                cycleStadiumTicketPolicyDir(1);
            if (down[6] && !s_prevMenuKeys[6])
                cycleStadiumTicketPolicyDir(keys[SDL_SCANCODE_LEFT] ? -1 : 1);
            if (down[7] && !s_prevMenuKeys[7])
                SetExitMenuFlag();
        } else {
            if (down[7] && !s_prevMenuKeys[7])
                confirmStadiumUpgradeNo();
        }
        for (int i = 0; i < 8; ++i)
            s_prevMenuKeys[i] = down[i];
    }
}

void showStadiumFacilitiesMenu()
{
#ifndef SWOS_TEST
    ensureCurrentCareerStadiumInitialized();
    showMenu(stadiumFacilitiesMenu);
#endif
}

// --- LLM Telemetry Synergy (Live Stream, Daily Echo Newspaper, Press Conferences) ---

std::string describeRecentFormStreak(const char *recentForm)
{
    if (!recentForm || recentForm[0] == '\0')
        return {};

    std::string cleaned;
    for (int i = 0; recentForm[i] != '\0' && cleaned.size() < 5; ++i) {
        char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(recentForm[i])));
        if (ch == 'W' || ch == 'D' || ch == 'L')
            cleaned.push_back(ch);
    }
    if (cleaned.empty())
        return {};

    int wins = 0, draws = 0, losses = 0;
    for (char ch : cleaned) {
        if (ch == 'W') ++wins;
        else if (ch == 'D') ++draws;
        else if (ch == 'L') ++losses;
    }

    int n = static_cast<int>(cleaned.size());
    int winStreak = 0;
    for (int i = n - 1; i >= 0 && cleaned[i] == 'W'; --i) ++winStreak;
    int lossStreak = 0;
    for (int i = n - 1; i >= 0 && cleaned[i] == 'L'; --i) ++lossStreak;
    int unbeatenStreak = 0;
    for (int i = n - 1; i >= 0 && cleaned[i] != 'L'; --i) ++unbeatenStreak;
    int winlessStreak = 0;
    for (int i = n - 1; i >= 0 && cleaned[i] != 'W'; --i) ++winlessStreak;

    std::string hyphenated;
    for (size_t i = 0; i < cleaned.size(); ++i) {
        if (i > 0) hyphenated.push_back('-');
        hyphenated.push_back(cleaned[i]);
    }

    std::string note;
    if (winStreak >= 4)
        note = std::to_string(winStreak) + " wins in a row — red-hot winning streak!";
    else if (winStreak == 3)
        note = (unbeatenStreak >= 4)
            ? ("3 straight wins, " + std::to_string(unbeatenStreak) + " unbeaten!")
            : "3 wins in a row!";
    else if (unbeatenStreak >= 4)
        note = std::to_string(unbeatenStreak) + " matches unbeaten";
    else if (lossStreak >= 3)
        note = std::to_string(lossStreak) + " defeats in a row — slump under pressure";
    else if (winlessStreak >= 4)
        note = std::to_string(winlessStreak) + " matches without a win — fans getting restless";
    else
        note = std::to_string(wins) + "W " + std::to_string(draws) + "D " + std::to_string(losses) + "L in last " + std::to_string(n);

    return hyphenated + " (" + note + ")";
}

std::string formatStadiumContextForTelemetry()
{
    if (swos.g_gameType != kGameTypeCareer || HotseatInternal::isVmNationalSelector())
        return {};

    ensureCurrentCareerStadiumInitialized();
    if (!s_stadium.initialized)
        return {};

    std::string out = "Club Stadium & Facilities: " + std::string(s_stadium.stadiumName) +
                      " (Capacity Lvl " + std::to_string(s_stadium.capacityLevel) + "/10, " +
                      formatNumberCommas(s_stadium.maxCapacity) + " seats; Pitch Lvl " +
                      std::to_string(s_stadium.pitchLevel) + "/5 [" + getPitchQualityLabel(s_stadium.pitchLevel) + "]; " +
                      "Training Lvl " + std::to_string(s_stadium.trainingLevel) + "/5, Medical Lvl " +
                      std::to_string(s_stadium.medicalLevel) + "/5, Commercial VIP Lvl " +
                      std::to_string(s_stadium.commercialLevel) + "/5). ";

    out += "Ticket Policy: " + std::string(getTicketPolicyName(s_stadium.ticketPolicy)) +
           " (effective GBP " + std::to_string(s_stadium.ticketPrice) + ")";
    std::string formDesc = describeRecentFormStreak(s_stadium.recentForm);
    if (!formDesc.empty())
        out += "; Last-5 Form (oldest->newest): " + formDesc;
    if (s_stadium.ticketPolicy == TicketPolicy::kCheap)
        out += " [supporters appreciate the fair ticket prices]";
    else if (s_stadium.ticketPolicy == TicketPolicy::kExpensive)
        out += " [supporters and journalists may criticize the steep ticket prices]";

    if (s_stadium.pitchLevel <= 1)
        out += "; poor muddy pitch quality draws criticism";
    else if (s_stadium.pitchLevel >= 4)
        out += "; immaculate playing surface draws praise";

    if (s_stadium.activeProject.isActive) {
        out += ". ONGOING STADIUM CONSTRUCTION: upgrading " +
               std::string(getStadiumModuleName(s_stadium.activeProject.module)) + " to Level " +
               std::to_string(s_stadium.activeProject.targetLevel) + " (" +
               std::to_string(s_stadium.activeProject.matchesRemaining) + " matches remaining; cranes/work visible around the ground)";
    }

    if (s_matchTrackingActive) {
        if (s_matchHumanIsHome && s_liveMatchPreview.valid && s_liveMatchPreview.attendance > 0) {
            out += ". TODAY'S MATCH VENUE: AT HOME (" + std::string(s_stadium.stadiumName) + ") — Live Crowd: " +
                   formatNumberCommas(s_liveMatchPreview.attendance) + "/" +
                   formatNumberCommas(s_liveMatchPreview.maxCapacity) + " (" +
                   std::to_string(s_liveMatchPreview.fillPct) + "% full";
            if (s_liveMatchPreview.isSellout)
                out += ", ROARING SELLOUT!";
            else if (s_liveMatchPreview.fillPct >= 90)
                out += ", PACKED HOUSE";
            if (s_liveMatchPreview.isDerby)
                out += ", FIERCE DERBY ATMOSPHERE";
            out += ")";
        } else if (!s_matchHumanIsHome) {
            out += ". TODAY'S MATCH VENUE: PLAYING AWAY FROM HOME (do NOT describe today's crowd as a home sellout at " +
                   std::string(s_stadium.stadiumName) + ")";
        }
    } else if (s_lastReport.valid && s_lastReport.isHomeMatch && s_lastReport.attendance > 0) {
        out += ". Latest Home Attendance: " + formatNumberCommas(s_lastReport.attendance) + "/" +
               formatNumberCommas(s_lastReport.maxCapacity) + " (" + std::to_string(s_lastReport.fillPct) + "% full";
        if (s_lastReport.isSellout)
            out += ", ROARING SELLOUT";
        out += ")";
    }

    return out;
}

std::string getLiveStadiumAndFormStreamNote()
{
    if (swos.g_gameType != kGameTypeCareer || HotseatInternal::isVmNationalSelector())
        return {};

    ensureCurrentCareerStadiumInitialized();
    if (!s_stadium.initialized)
        return {};

    std::vector<std::string> hooks;

    // 1. Live home sellout / packed house (strictly when playing at home today)
    if (s_matchTrackingActive && s_matchHumanIsHome && s_liveMatchPreview.valid && s_liveMatchPreview.attendance > 0) {
        if (s_liveMatchPreview.isSellout) {
            hooks.push_back("ROARING HOME SELLOUT at " + std::string(s_stadium.stadiumName) + " (" +
                            formatNumberCommas(s_liveMatchPreview.attendance) + "/" +
                            formatNumberCommas(s_liveMatchPreview.maxCapacity) + ", " +
                            std::to_string(s_liveMatchPreview.fillPct) + "% full" +
                            (s_liveMatchPreview.isDerby ? ", fierce derby!" : "!)"));
        } else if (s_liveMatchPreview.fillPct >= 90) {
            hooks.push_back("Packed home crowd at " + std::string(s_stadium.stadiumName) + " (" +
                            formatNumberCommas(s_liveMatchPreview.attendance) + ", " +
                            std::to_string(s_liveMatchPreview.fillPct) + "% full)");
        }
    }

    // 2. Ongoing construction project
    if (s_stadium.activeProject.isActive) {
        hooks.push_back("Ongoing construction at " + std::string(s_stadium.stadiumName) + ": " +
                        std::string(getStadiumModuleName(s_stadium.activeProject.module)) + " -> Lvl " +
                        std::to_string(s_stadium.activeProject.targetLevel) + " (" +
                        std::to_string(s_stadium.activeProject.matchesRemaining) + " matches left)");
    }

    // 3. Notable last-5 form streak (3+ wins, 4+ unbeaten, 3+ losses, or 4+ winless)
    std::string cleaned = s_stadium.recentForm;
    int n = static_cast<int>(cleaned.size());
    if (n >= 3) {
        int winStreak = 0, lossStreak = 0, unbeatenStreak = 0, winlessStreak = 0;
        for (int i = n - 1; i >= 0 && cleaned[i] == 'W'; --i) ++winStreak;
        for (int i = n - 1; i >= 0 && cleaned[i] == 'L'; --i) ++lossStreak;
        for (int i = n - 1; i >= 0 && cleaned[i] != 'L'; --i) ++unbeatenStreak;
        for (int i = n - 1; i >= 0 && cleaned[i] != 'W'; --i) ++winlessStreak;
        if (winStreak >= 3 || unbeatenStreak >= 4 || lossStreak >= 3 || winlessStreak >= 4) {
            hooks.push_back("Last-5 Form: " + describeRecentFormStreak(s_stadium.recentForm));
        }
    }

    if (hooks.empty())
        return {};

    std::string out;
    for (size_t i = 0; i < hooks.size(); ++i) {
        if (i > 0) out += " | ";
        out += hooks[i];
    }
    return out;
}

std::string formatLastStadiumOutcomeForNewspaper()
{
    if (swos.g_gameType != kGameTypeCareer || HotseatInternal::isVmNationalSelector() || !s_stadium.initialized)
        return {};

    if (s_lastReport.valid && s_lastReport.isHomeMatch && s_lastReport.attendance > 0) {
        std::string out = "Crowd: " + formatNumberCommas(s_lastReport.attendance) + "/" +
                          formatNumberCommas(s_lastReport.maxCapacity) + " (" +
                          std::to_string(s_lastReport.fillPct) + "%" +
                          (s_lastReport.isSellout ? " SELLOUT!" : ")") +
                          (s_lastReport.isSellout ? ")" : "");
        if (s_stadium.recentForm[0] != '\0')
            out += " | Form: " + std::string(s_stadium.recentForm);
        return out;
    }

    if (s_stadium.activeProject.isActive) {
        return std::string("Works: ") + getStadiumModuleName(s_stadium.activeProject.module) +
               " -> Lvl " + std::to_string(s_stadium.activeProject.targetLevel) +
               " (" + std::to_string(s_stadium.activeProject.matchesRemaining) + "m left)";
    }

    std::string form = describeRecentFormStreak(s_stadium.recentForm);
    if (!form.empty())
        return "Form: " + form;
    return {};
}
