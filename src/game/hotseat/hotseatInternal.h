#pragma once

#include "hotseatCareer.h"
#include "stdinc.h"
#include "init.h"
#include "menus.h"
#include "unpackMenu.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

extern void CareerInitVars();
extern void CalcMenuEntryAddress();
extern void ShowRetirementMessage();
extern void ShowLeavingMessage();
extern void ShowChairmanOpinionAboutTheSeason();
extern void ContinueCareerNextSeason();
extern void cseg_91428();
extern void cseg_5DB56();
extern void cseg_943FA();
extern void InitializeNewSeason();

namespace HotseatInternal {

constexpr char kHotseatFileMagicV3[16] = "SWOS_HOTSEAT_V3";
constexpr char kHotseatFileMagicV4[16] = "SWOS_HOTSEAT_V4";
constexpr char kHotseatFileMagicV5[16] = "SWOS_HOTSEAT_V5";
constexpr char kHotseatFileMagicV6[16] = "SWOS_HOTSEAT_V6";
constexpr char kHotseatFileMagicV7[16] = "SWOS_HOTSEAT_V7";
constexpr char kHotseatFileMagicV8[16] = "SWOS_HOTSEAT_V8";
constexpr char kHotseatFileMagicV9[16] = "SWOS_HOTSEAT_V9"; // V8 + multi-world (country/league) universe

constexpr int32_t kMaxSerializedOffers = 1024;
constexpr size_t kTeamFileHeaderSize = sizeof(TeamFileHeader);
constexpr size_t kPlayerFileSize = sizeof(PlayerFile);
constexpr int kMaxHomeMarketEntries = 25;
constexpr int kMaxForeignMarketEntries = 15;
constexpr int kPoolPlyrCount = 900;
constexpr size_t kJobOfferEntrySize = 26;
constexpr int kMaxJobOffersPerQueue = 5;
constexpr size_t kTourSubCompOffset = CareerOffsets::kDiyCompetitionStart - CareerOffsets::kDiyFileBufferCopy; // 1055 bytes

// Isolated layout inside the dedicated hotseat extra VM memory region (from src/init.cpp):
// [0 .. 256): negative-offset safety padding (e.g. PoolPlayer - 56)
// [256 .. 320): PoolPlayerEntry scratch buffer
// [320 .. 1024): 16 x 36-byte calendar row scratch slots for resolveGameListSlotPtr()
// [1024 .. kHotseatExtraMemSize): per-player 1292-byte team mirrors
constexpr uint32_t kPoolScratchSubOffset = 256;
constexpr uint32_t kGameListScratchSubOffset = 320;
constexpr uint32_t kMirrorBaseSubOffset = 1024;

#pragma pack(push, 1)
struct MarketEntry {
    uint16_t teamId;
    uint8_t playerOrdinal;
    uint8_t padding;
};

struct PoolPlayerEntry {
    uint16_t teamId;
    uint8_t playerOrdinal;
    char clubName[17];
    PlayerFile player;
};
#pragma pack(pop)

static_assert(sizeof(MarketEntry) == 4, "MarketEntry size mismatch");
static_assert(sizeof(PoolPlayerEntry) == 58, "PoolPlayerEntry size mismatch");

struct ScopedInitializingSeason {
    HotseatCareerManager& mgr;
    bool prev;
    explicit ScopedInitializingSeason(HotseatCareerManager& m) : mgr(m), prev(m.isInitializingSeason()) {
        mgr.setInitializingSeason(true);
    }
    ~ScopedInitializingSeason() {
        mgr.setInitializingSeason(prev);
    }
};

template <typename T>
inline bool writeBinary(std::ofstream& out, const T& val)
{
    return static_cast<bool>(out.write(reinterpret_cast<const char *>(&val), sizeof(T)));
}

inline bool writeString(std::ofstream& out, const std::string& str, size_t maxLen)
{
    uint32_t len = static_cast<uint32_t>(std::min(str.size(), maxLen));
    if (!writeBinary(out, len))
        return false;
    if (len > 0 && !out.write(str.data(), len))
        return false;
    return true;
}

template <typename T>
inline bool readBinary(std::ifstream& in, T& val)
{
    return static_cast<bool>(in.read(reinterpret_cast<char *>(&val), sizeof(T)));
}

inline bool readString(std::ifstream& in, std::string& str, size_t maxLen)
{
    uint32_t len = 0;
    if (!readBinary(in, len) || len > maxLen)
        return false;
    str.clear();
    if (len > 0) {
        str.resize(len);
        if (!in.read(&str[0], len))
            return false;
    }
    return true;
}

bool isValidPlayerName(const char *name);
uint16_t getVmSelTeamNumber();
bool isVmNationalSelector();
bool hasCountryInSelectedTeams(uint8_t country);
void copyBoundedPlayerName(char *outName, size_t outSize, const char *src, bool trimPadding = false);
void generateNonEmptyPlayerName(uint8_t country, uint16_t excludeTeamId,
                                char *outName, size_t outSize,
                                uint8_t& outNat, uint8_t& outFaceBits);
void populateReplacementPlayerSlot(uint8_t *teamBuf, uint8_t *trialWeeksBuf,
                                   int ordinal, uint16_t teamId, bool runSwosSkillGen, bool isEmergencyLoan);
void populateNamedTrialPlayerSlot(uint8_t *teamBuf, uint8_t *trialWeeksBuf,
                                  int ordinal, uint16_t teamId, bool runSwosSkillGen);
void writeTeamCoachName(uint8_t *teamBuf, const std::string& firstName, const std::string& surname);
bool isTeamInEuroCupDef(uint32_t cupCopyOfs, uint16_t teamId);
bool hasInitializedMatchPlayers(const uint8_t *matchPlayers, uint16_t count);
void syncTrainingMatchSavedPlayers(uint8_t *savedPlayers, uint8_t *shirtSnapshot, uint8_t *teamBuf,
                                   uint16_t count, const uint8_t *matchPlayers);
void formatCurrencyAmount(char *buf, size_t bufSize, uint32_t amount);
void applySeasonOutcomeToPlayerContext(HotseatPlayerContext& p, uint16_t goalsFor, uint16_t goalsAgainst,
                                       bool wonByPenalties, bool lostByPenalties);
void removeTeamFromJobOfferBuffer(uint8_t *buf, int16_t& count, uint16_t teamId);
int32_t computeJobOfferStartingBalance(int32_t amountOffered);
void applyCareerTeamResetInit(uint8_t *teamBuf, bool newClub);
void updateExistingTmdFileEntry(uint16_t teamId, const uint8_t *teamFileBytes, TeamControls controls);
void loadSwitchedClubVia5DB56(int16_t targetNewTid, int32_t startBal);
bool teamNamesMatch(const std::string& a, const char *b);
uint32_t getGameListScratchBase();

} // namespace HotseatInternal
