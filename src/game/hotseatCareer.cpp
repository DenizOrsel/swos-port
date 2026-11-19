#include "hotseatCareer.h"
#include "stdinc.h"
#include "menus.h"
#include "unpackMenu.h"
#include "careerPlayerCount.mnu.h"
#include "continueAbortMenu.h"
#include <cstring>
#include <algorithm>
#include <fstream>

namespace {

constexpr char kHotseatFileMagicV3[16] = "SWOS_HOTSEAT_V3";
constexpr char kHotseatFileMagicV4[16] = "SWOS_HOTSEAT_V4";
constexpr char kHotseatFileMagicV5[16] = "SWOS_HOTSEAT_V5";
constexpr char kHotseatFileMagicV6[16] = "SWOS_HOTSEAT_V6";
constexpr int32_t kMaxSerializedOffers = 1024;
constexpr size_t kTeamFileHeaderSize = sizeof(TeamFileHeader);
constexpr size_t kPlayerFileSize = sizeof(PlayerFile);
constexpr int kMaxHomeMarketEntries = 25;
constexpr int kMaxForeignMarketEntries = 15;
constexpr int kPoolPlyrCount = 900;

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

bool isValidPlayerName(const char *name)
{
    if (!name || name[0] == '\0' || std::strncmp(name, "*ERR*", 5) == 0)
        return false;

    for (size_t i = 0; i < 23 && name[i] != '\0'; ++i) {
        if (static_cast<unsigned char>(name[i]) > ' ')
            return true;
    }
    return false;
}

uint16_t getVmSelTeamNumber()
{
    return *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
}

bool isVmNationalSelector()
{
    return *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kIsNationalSelector]) != 0;
}

bool hasCountryInSelectedTeams(uint8_t country)
{
    if (!swos.selTeamsPtr || swos.g_numSelectedTeams <= 0)
        return false;

    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return false;

    int count = std::min<int>(swos.g_numSelectedTeams, 68400 / static_cast<int>(sizeof(TeamFile)));
    for (int i = 0; i < count; ++i) {
        if (static_cast<uint8_t>(selTeams[i].teamFileNo) == country)
            return true;
    }
    return false;
}

void copyBoundedPlayerName(char *outName, size_t outSize, const char *src, bool trimPadding = false)
{
    if (!outName || outSize == 0)
        return;

    std::memset(outName, 0, outSize);
    if (!src)
        return;

    size_t len = 0;
    while (len + 1 < outSize && src[len] != '\0')
        ++len;
    std::memcpy(outName, src, len);
    outName[len] = '\0';
    if (trimPadding)
        trimTrailingSwosPadding(outName);
}

void generateNonEmptyPlayerName(uint8_t country, uint16_t excludeTeamId,
                                char *outName, size_t outSize,
                                uint8_t& outNat, uint8_t& outFaceBits)
{
    std::memset(outName, 0, outSize);
    bool gotName = false;

    uint16_t tmdCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTmdCareerCount]);
    if (tmdCount > 0 || (swos.selTeamsPtr && swos.g_numSelectedTeams > 0)) {
        save68kRegisters();
        D0 = country;
        D3 = excludeTeamId;
        ::cseg_52B6A();
        const char *gen = reinterpret_cast<const char *>(&SwosVM::g_memByte[CareerOffsets::kDseg17A3D0]);
        if (isValidPlayerName(gen)) {
            copyBoundedPlayerName(outName, outSize, gen, true);
            outNat = static_cast<uint8_t>(D0);
            outFaceBits = static_cast<uint8_t>(D3) & 0x18;
            gotName = isValidPlayerName(outName);
        }
        restore68kRegisters();
    }

    if (!gotName && *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kPoolplyrLoaded]) != 0) {
        const auto *pool = reinterpret_cast<const PoolPlayerEntry *>(&SwosVM::g_memByte[CareerOffsets::kPoolplyrDatBuffer]);
        uint32_t seed = getVmSelTeamNumber() + *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        for (int pass = 0; pass < 2 && !gotName; ++pass) {
            for (int k = 0; k < kPoolPlyrCount; ++k) {
                const auto& pe = pool[(seed + k) % kPoolPlyrCount];
                if (pass == 0 && static_cast<uint8_t>(pe.teamId & 0xFF) != country)
                    continue;
                if (isValidPlayerName(pe.player.playerName)) {
                    copyBoundedPlayerName(outName, outSize, pe.player.playerName);
                    outNat = pe.player.nationality;
                    outFaceBits = pe.player.positionAndFace & 0x18;
                    gotName = true;
                    break;
                }
            }
        }
    }

    if (!gotName && swos.selTeamsPtr && swos.g_numSelectedTeams > 0) {
        const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
        int count = std::min<int>(swos.g_numSelectedTeams, 68400 / static_cast<int>(sizeof(TeamFile)));
        for (int t = 0; t < count && !gotName; ++t) {
            uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[t]);
            if (tid == excludeTeamId)
                continue;
            for (int pIdx = 0; pIdx < 16; ++pIdx) {
                const auto& pf = selTeams[t].players[pIdx];
                if (isValidPlayerName(pf.playerName)) {
                    copyBoundedPlayerName(outName, outSize, pf.playerName);
                    outNat = pf.nationality;
                    outFaceBits = pf.positionAndFace & 0x18;
                    gotName = true;
                    break;
                }
            }
        }
    }

    if (!gotName) {
        copyBoundedPlayerName(outName, outSize, "TRIAL PLAYER");
        outNat = country;
        outFaceBits = 0;
    }
}

void populateReplacementPlayerSlot(uint8_t *teamBuf, uint8_t *trialWeeksBuf,
                                   int ordinal, uint16_t teamId, bool runSwosSkillGen, bool isEmergencyLoan)
{
    if (!teamBuf || ordinal < 0 || ordinal >= 30)
        return;

    auto *pf = reinterpret_cast<PlayerFile *>(teamBuf + kTeamFileHeaderSize + ordinal * kPlayerFileSize);
    uint8_t posBits = pf->positionAndFace & 0xE0;
    uint8_t faceBits = pf->positionAndFace & 0x18;
    uint8_t nat = pf->nationality;
    uint8_t savedShirt = pf->shirtNumber;
    bool hasValidName = isValidPlayerName(pf->playerName);
    char preservedName[sizeof(pf->playerName)]{};
    if (hasValidName)
        std::memcpy(preservedName, pf->playerName, sizeof(preservedName));

    // Generate skills/price first because cseg_567B5 overwrites the 38-byte PlayerFile from its template buffer.
    if (runSwosSkillGen && hasCountryInSelectedTeams(static_cast<uint8_t>(teamId & 0xFF))) {
        save68kRegisters();
        A3 = static_cast<uint32_t>(CareerOffsets::kCareerTeam + ordinal * kPlayerFileSize);
        D5 = teamId;
        ::cseg_567B5();
        restore68kRegisters();
    }

    if (hasValidName) {
        std::memcpy(pf->playerName, preservedName, sizeof(pf->playerName));
        pf->nationality = nat;
    } else {
        generateNonEmptyPlayerName(static_cast<uint8_t>(teamId & 0xFF), teamId,
                                   pf->playerName, sizeof(pf->playerName), nat, faceBits);
        pf->nationality = nat;
    }

    if (pf->shirtNumber == 0)
        pf->shirtNumber = savedShirt ? savedShirt : 16;

    // Emergency loan players preserve LOAN bit (0x02) and someFlag=0 so SWOS cleans them up when fit depth returns;
    // transfer-departure replacements become permanent TRIAL players (someFlag=0x9B) with a contract timer.
    pf->positionAndFace = static_cast<byte>(posBits | faceBits | (isEmergencyLoan ? 0x02 : 0x00));
    pf->cardsInjuries = 0;
    pf->someFlag = isEmergencyLoan ? 0 : 0x9B;

    if (trialWeeksBuf) {
        if (isEmergencyLoan) {
            trialWeeksBuf[ordinal] = 0;
        } else {
            uint8_t weeks = 45;
            save68kRegisters();
            SWOS::Rand();
            uint16_t r = static_cast<uint16_t>(D0) & 0xFF;
            weeks = static_cast<uint8_t>(40 + ((r * 50) >> 8));
            restore68kRegisters();
            trialWeeksBuf[ordinal] = static_cast<uint8_t>(static_cast<int8_t>(-static_cast<int>(weeks)));
        }
    }

    if (posBits != 0 && pf->passing == 0 && pf->shootingHeading == 0 &&
        pf->tacklingBallControl == 0 && pf->speedFinishing == 0) {
        pf->passing = 0x33;
        pf->shootingHeading = 0x33;
        pf->tacklingBallControl = 0x33;
        pf->speedFinishing = 0x33;
    }
    if (pf->price == 0) {
        pf->price = 2;
    }
}

void populateNamedTrialPlayerSlot(uint8_t *teamBuf, uint8_t *trialWeeksBuf,
                                  int ordinal, uint16_t teamId, bool runSwosSkillGen)
{
    populateReplacementPlayerSlot(teamBuf, trialWeeksBuf, ordinal, teamId, runSwosSkillGen, false);
}

void writeTeamCoachName(uint8_t *teamBuf, const std::string& firstName, const std::string& surname)
{
    if (!teamBuf)
        return;

    std::string combined;
    if (!firstName.empty() && !surname.empty())
        combined = firstName + " " + surname;
    else if (!firstName.empty())
        combined = firstName;
    else if (!surname.empty())
        combined = surname;
    else
        return;

    auto *tf = reinterpret_cast<TeamFile *>(teamBuf);
    std::memset(tf->coachName, 0, sizeof(tf->coachName));
    std::memcpy(tf->coachName, combined.data(), std::min(combined.size(), sizeof(tf->coachName) - 1));
}
constexpr size_t kTourSubCompOffset = CareerOffsets::kDiyCompetitionStart - CareerOffsets::kDiyFileBufferCopy; // 1055 bytes

bool isTeamInEuroCupDef(uint32_t cupCopyOfs, uint16_t teamId)
{
    if (cupCopyOfs == 0 || cupCopyOfs + 32 >= sizeof(SwosVM::g_memByte) || teamId == 0)
        return false;

    uint8_t nameLen = SwosVM::g_memByte[cupCopyOfs + 7];
    uint32_t teamListOfs = cupCopyOfs + 7 + nameLen;
    uint8_t type = SwosVM::g_memByte[cupCopyOfs + 1];
    uint16_t count = 0;
    if (type == 1)
        count = SwosVM::g_memByte[cupCopyOfs + 0x0A];
    else if (type == 2)
        count = SwosVM::g_memByte[cupCopyOfs + 0x0F];
    else
        count = SwosVM::g_memByte[cupCopyOfs + 0x0D];

    for (uint16_t k = 0; k < count; ++k) {
        uint32_t ofs = teamListOfs + static_cast<uint32_t>(k) * 2;
        if (ofs + 2 > sizeof(SwosVM::g_memByte))
            break;
        if (*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[ofs]) == teamId)
            return true;
    }
    return false;
}

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
bool writeBinary(std::ofstream& out, const T& val)
{
    return static_cast<bool>(out.write(reinterpret_cast<const char *>(&val), sizeof(T)));
}

bool writeString(std::ofstream& out, const std::string& str, size_t maxLen)
{
    uint32_t len = static_cast<uint32_t>(std::min(str.size(), maxLen));
    if (!writeBinary(out, len))
        return false;
    if (len > 0 && !out.write(str.data(), len))
        return false;
    return true;
}

template <typename T>
bool readBinary(std::ifstream& in, T& val)
{
    return static_cast<bool>(in.read(reinterpret_cast<char *>(&val), sizeof(T)));
}

bool readString(std::ifstream& in, std::string& str, size_t maxLen)
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

bool hasInitializedMatchPlayers(const uint8_t *matchPlayers, uint16_t count)
{
    uint16_t n = std::min<uint16_t>(count, kCurrentMatchPlayersSize);
    for (uint16_t i = 0; i < n; ++i) {
        if (matchPlayers[i] != 0)
            return true;
    }
    return false;
}

void syncTrainingMatchSavedPlayers(uint8_t *savedPlayers, uint8_t *shirtSnapshot, uint8_t *teamBuf,
                                   uint16_t count, const uint8_t *matchPlayers)
{
    uint16_t n = std::min<uint16_t>(count, static_cast<uint16_t>(kCurrentMatchPlayersSize));
    if (n == 0 || !matchPlayers || !savedPlayers)
        return;

    if (hasInitializedMatchPlayers(matchPlayers, n)) {
        // Check if savedPlayers[0..n-1] already contains every ordinal in matchPlayers[0..n-1].
        bool inMatch[kCurrentMatchPlayersSize] = {false};
        for (uint16_t i = 0; i < n; ++i) {
            if (matchPlayers[i] < kCurrentMatchPlayersSize)
                inMatch[matchPlayers[i]] = true;
        }

        bool seenSaved[kCurrentMatchPlayersSize] = {false};
        for (uint16_t i = 0; i < n; ++i) {
            uint8_t ord = savedPlayers[i];
            if (ord < kCurrentMatchPlayersSize && inMatch[ord])
                seenSaved[ord] = true;
        }

        bool allPresent = true;
        for (uint16_t i = 0; i < n; ++i) {
            uint8_t ord = matchPlayers[i];
            if (ord < kCurrentMatchPlayersSize && !seenSaved[ord]) {
                allPresent = false;
                break;
            }
        }

        if (!allPresent) {
            std::memset(savedPlayers, 0, kCurrentMatchPlayersSize);
            std::memcpy(savedPlayers, matchPlayers, n);
        }

        if (teamBuf && n >= kNumPlayersInTeam) {
            std::memcpy(reinterpret_cast<TeamFile *>(teamBuf)->playerNumbers, matchPlayers, kNumPlayersInTeam);
        }
    }

    if (shirtSnapshot && teamBuf) {
        for (uint16_t i = 0; i < n; ++i) {
            uint8_t ord = hasInitializedMatchPlayers(matchPlayers, n) ? matchPlayers[i] : static_cast<uint8_t>(i);
            if (ord < kMaxCareerSquadPlayers && shirtSnapshot[ord] == 0) {
                const auto *pf = reinterpret_cast<const PlayerFile *>(
                    teamBuf + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize);
                if (pf->shirtNumber != 0)
                    shirtSnapshot[ord] = pf->shirtNumber;
            }
        }
    }
}

void formatCurrencyAmount(char *buf, size_t bufSize, uint32_t amount)
{
    if (amount >= 1000000) {
        snprintf(buf, bufSize, "\x9c%u.%03u,000", amount / 1000000, (amount % 1000000) / 1000);
    } else {
        snprintf(buf, bufSize, "\x9c%u,000", amount / 1000);
    }
}

} // namespace

void trimTrailingSwosPadding(std::string& s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '*'))
        s.pop_back();
}

void trimTrailingSwosPadding(char *s)
{
    if (!s)
        return;
    for (int len = static_cast<int>(strlen(s)) - 1; len >= 0 && (s[len] == ' ' || s[len] == '*'); --len)
        s[len] = '\0';
}

HotseatCareerManager& HotseatCareerManager::get()
{
    static HotseatCareerManager instance;
    return instance;
}

void HotseatCareerManager::reset()
{
    m_active = false;
    m_numPlayers = 0;
    m_activePlayerIndex = 0;
    m_nextOfferId = 1;
    m_roundCalendarIndex = 0;
    m_pendingPostMatchCalOfs = -1;
    m_pendingPostMatchSlotIdx = -1;
    m_pendingOfferPrompt = false;
    m_inPlayMatchFlow = false;
    m_initializingSeason = false;
    m_transferOffers.clear();
    m_calendarSlots.clear();
    resetEuroCups();
    for (auto& p : m_players) {
        p = HotseatPlayerContext();
    }
    m_isWizardActive = false;
    m_wizardCurrentPlayer = 0;
    m_wizardTotalPlayers = 1;
    m_wizardAdvanced = false;
    m_wizardPlayers.clear();
    m_isSelectingCareerTeams = false;
    m_careerNumPlayers = 1;
    m_assignedTeamSlots.clear();
    m_assignedSlotInfos.clear();
    m_prevHomeMarketCount = 0;
    m_prevForeignMarketCount = 0;
    m_emergencyLoanDepth = 0;
    m_playMatchSetupStack.clear();
    for (auto& pm : m_h2hPackMaps) {
        pm = H2hPackedTeamMap{};
    }
    if (m_vmPoolPlayerScratchOfs)
        std::memset(&SwosVM::g_memByte[m_vmPoolPlayerScratchOfs], 0, 60);
    for (uint32_t ofs : m_vmTeamMirrorOfs) {
        if (ofs)
            std::memset(&SwosVM::g_memByte[ofs], 0, kCareerTeamBufferSize);
    }
}

void HotseatCareerManager::resetEuroCups()
{
    m_activeEuroCupIndex = -1;
    m_euroCupCopyOfs[0] = CareerOffsets::kEuroCupCopy;
    m_euroCupCopyOfs[1] = CareerOffsets::kCupWinnersCupCopy;
    m_euroCupCopyOfs[2] = CareerOffsets::kUefaCupCopy;
    for (int e = 0; e < kNumEuroCups; ++e) {
        m_euroCupActive[e] = false;
        m_euroCupSeasonStrOfs[e] = 0;
        m_euroCupBuffers[e].fill(0);
    }
    m_euroCupByCalOfs.clear();
    static const uint8_t kDefaultTemplate[6] = { 5, 0, 2, 0, 8, 0 };
    std::memcpy(m_euroCupScheduleTemplate, kDefaultTemplate, sizeof(kDefaultTemplate));
}

bool HotseatCareerManager::anyEuroCupActive() const
{
    return m_euroCupActive[0] || m_euroCupActive[1] || m_euroCupActive[2];
}

void HotseatCareerManager::initializeHotseat(const std::vector<int16_t>& teamIds)
{
    reset();
    m_numPlayers = std::min(static_cast<int>(teamIds.size()), kMaxHotseatPlayers);
    if (m_numPlayers < 1) {
        m_active = false;
        return;
    }

    m_active = true;
    m_activePlayerIndex = 0;

    for (int i = 0; i < m_numPlayers; ++i) {
        m_players[i].teamId = teamIds[i];
        m_players[i].managerName = "PLAYER";
        m_players[i].managerSurname = std::to_string(i + 1);
        m_players[i].coachOrPlayer = 0;
        m_players[i].nationality = 8; // English default
        m_players[i].matchPlayedThisTurn = false;
        m_players[i].turnCompleted = false;
    }

    saveCurrentSwosContext(m_players[0]);
    m_players[0].teamId = teamIds[0];

    // Clear stale purple highlight pointers from any previous session before calendar initialization
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174576]) = -1;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174578]) = 0;
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457A]) = 0;
    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457E]) = 0;
}

void HotseatCareerManager::saveCurrentSwosContext(HotseatPlayerContext& ctx)
{
    int16_t vmTeamId = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    if (vmTeamId != 0 || ctx.teamId == 0)
        ctx.teamId = vmTeamId;
    ctx.coachOrPlayer = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kCoachOrPlayer]);
    ctx.careerTeamLeague = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]);
    ctx.mrOrMs = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]);
    ctx.nationality = SwosVM::g_memByte[CareerOffsets::kPlNationality];

    ctx.newBalance = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]);
    ctx.oldBalance = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kOldBalance]);
    ctx.startingBalance = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kStartingBalance]);
    ctx.playerPurchases = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerPurchases]);
    ctx.playerSales = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerSales]);
    ctx.generalRunningCosts = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGeneralRunningCosts]);
    ctx.playerWagesBill = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerWagesBill]);
    ctx.gateReceipts = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGateReceipts]);
    ctx.competitionBonuses = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionBonuses]);
    ctx.sensisoftPlcSponsorship = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kSensisoftPlcSponsorship]);

    ctx.stayingInOldClub = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kStayingInOldClub]);
    ctx.dsegD6B54 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B54]);
    ctx.dsegD6B56 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B56]);
    ctx.dsegD6B5A = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B5A]);
    ctx.dsegD6B5C = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B5C]);
    ctx.dsegD6B62 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B62]);
    ctx.dsegD6B64 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B64]);
    ctx.dsegD6B94 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B94]);
    ctx.dsegD6B88 = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B88]);
    ctx.dsegD6B9A = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B9A]);
    ctx.dsegD6B9E = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B9E]);
    ctx.dsegD6BA2 = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6BA2]);
    std::memcpy(ctx.playerWages, &SwosVM::g_memByte[CareerOffsets::kDsegD6BA6], sizeof(ctx.playerWages));
    std::memcpy(ctx.playerTrialWeeks, &SwosVM::g_memByte[CareerOffsets::kDsegD6C26], sizeof(ctx.playerTrialWeeks));
    ctx.dsegD6C46 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C46]);
    ctx.additionalInvestments = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kAdditionalInvestments]);
    ctx.dsegD6C4E = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C4E]);
    ctx.dsegD6C50 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C50]);

    ctx.youreSacked = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kYoureSacked]);
    ctx.retired = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]);
    ctx.clubBusinessActive = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kClubBusinessActive]);
    ctx.numNewJobOffers = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]);
    ctx.numJobOffers = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]);
    ctx.teamNoOfferAccepted = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]);
    ctx.teamFundsAccepted = *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]);
    ctx.isNationalSelector = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kIsNationalSelector]);
    ctx.dsegD6CDC = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]);
    ctx.numOffers = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumOffers]);
    ctx.timeToNegotiate = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTimeToNegotiate]);
    ctx.dsegD6D0A = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0A]);
    ctx.dsegD6D0C = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0C]);
    ctx.dsegD6D0E = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0E]);
    ctx.dsegD6D10 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D10]);
    ctx.dsegD6D12 = *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D12]);

    ctx.gamesPlayed = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGamesPlayed]);
    ctx.numWins = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumWins]);
    ctx.numDrawn = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumDrawn]);
    ctx.numLost = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumLost]);
    ctx.goalsScored = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGoalsScored]);
    ctx.goalsTaken = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGoalsTaken]);
    std::memcpy(ctx.seasonStatsBlock, &SwosVM::g_memByte[CareerOffsets::kDsegD6C88], sizeof(ctx.seasonStatsBlock));
    ctx.dsegD6CA0 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA0]);
    ctx.dsegD6CA2 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]);
    ctx.dsegD7E66 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD7E66]);

    ctx.currentSeasonShowing = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
    ctx.currentSeasonNumber = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]);
    ctx.seasonPlaying = SwosVM::g_memByte[CareerOffsets::kSeasonPlaying];
    ctx.dsegE0048 = SwosVM::g_memByte[CareerOffsets::kDsegE0048];
    ctx.playMatchBlink = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDF4F4]);
    ctx.careerNumPlayers = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    std::memcpy(ctx.seasonFinancesBlock, &SwosVM::g_memByte[CareerOffsets::kSeasonFinancesBlock], kSeasonFinancesBlockSize);
    ctx.numPlayersOnTransferMarket = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]);
    std::memcpy(ctx.playersOnTransferMarket, &SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket], kListedPlayersSize);
    std::memcpy(ctx.dsegD6F26Block, &SwosVM::g_memByte[CareerOffsets::kDsegD6F26Block], kListedPlayersSize);
    std::memcpy(ctx.dsegD6DBA, &SwosVM::g_memByte[CareerOffsets::kDsegD6DBA], kTransferAiOffersSize);
    std::memcpy(ctx.newJobOffers, &SwosVM::g_memByte[CareerOffsets::kNewJobOffers], kJobOffersBufferSize);
    std::memcpy(ctx.jobOffers, &SwosVM::g_memByte[CareerOffsets::kJobOffers], kJobOffersBufferSize);

    std::memcpy(ctx.currentMatchPlayers, &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers], kCurrentMatchPlayersSize);
    std::memcpy(ctx.careerTeam, &SwosVM::g_memByte[CareerOffsets::kCareerTeam], kCareerTeamBufferSize);
    std::memcpy(ctx.careerHistory, &SwosVM::g_memByte[CareerOffsets::kCareerHistory], kCareerHistoryBufferSize);
    std::memcpy(ctx.userTactics, &SwosVM::g_memByte[CareerOffsets::kUserTacticsStart], 6 * kUserTacticsSize);

    ctx.dsegD761A = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761A]);
    ctx.dsegD761C = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761C]);
    ctx.dsegD8B6A = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8B6A]);
    std::memcpy(ctx.trainingMatchSavedPlayers, &SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers], kCurrentMatchPlayersSize);
    std::memcpy(ctx.dsegD8B8C, &SwosVM::g_memByte[CareerOffsets::kDsegD8B8C], kCurrentMatchPlayersSize);
    ctx.dsegD8BAC = SwosVM::g_memByte[CareerOffsets::kDsegD8BAC];
    ctx.dsegD8BAE = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8BAE]);
    ctx.dsegD8BB0 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8BB0]);
    std::memcpy(ctx.lastMatchPlayers, &SwosVM::g_memByte[CareerOffsets::kLastMatchPlayers], kCurrentMatchPlayersSize);
    std::memcpy(ctx.dsegD8BD2, &SwosVM::g_memByte[CareerOffsets::kDsegD8BD2], kCurrentMatchPlayersSize);
    ctx.currentTactics = SwosVM::g_memByte[CareerOffsets::kCurrentTactics];
    ctx.dsegD8BF3 = SwosVM::g_memByte[CareerOffsets::kDsegD8BF3];

    syncTrainingMatchSavedPlayers(ctx.trainingMatchSavedPlayers, ctx.dsegD8B8C, ctx.careerTeam,
                                  ctx.careerNumPlayers, ctx.currentMatchPlayers);
}

void HotseatCareerManager::loadSwosContext(const HotseatPlayerContext& ctx, bool relink /* = true */)
{
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]) = ctx.teamId;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kCoachOrPlayer]) = ctx.coachOrPlayer;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeamLeague]) = ctx.careerTeamLeague;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]) = ctx.mrOrMs;
    SwosVM::g_memByte[CareerOffsets::kPlNationality] = ctx.nationality;

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], 0, 5);
    const char *sexStr = (ctx.mrOrMs != 0) ? "MS" : "MR";
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], sexStr, 2);

    // Manager name buffers (null-terminated)
    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 0, 9);
    size_t nameLen = std::min(ctx.managerName.size(), size_t{8});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], ctx.managerName.data(), nameLen);

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 0, 13);
    size_t surnameLen = std::min(ctx.managerSurname.size(), size_t{12});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], ctx.managerSurname.data(), surnameLen);

    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kNewBalance]) = ctx.newBalance;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kOldBalance]) = ctx.oldBalance;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kStartingBalance]) = ctx.startingBalance;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerPurchases]) = ctx.playerPurchases;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerSales]) = ctx.playerSales;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGeneralRunningCosts]) = ctx.generalRunningCosts;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayerWagesBill]) = ctx.playerWagesBill;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kGateReceipts]) = ctx.gateReceipts;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kCompetitionBonuses]) = ctx.competitionBonuses;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kSensisoftPlcSponsorship]) = ctx.sensisoftPlcSponsorship;

    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kStayingInOldClub]) = ctx.stayingInOldClub;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B54]) = ctx.dsegD6B54;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B56]) = ctx.dsegD6B56;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B5A]) = ctx.dsegD6B5A;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B5C]) = ctx.dsegD6B5C;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B62]) = ctx.dsegD6B62;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B64]) = ctx.dsegD6B64;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B94]) = ctx.dsegD6B94;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B88]) = ctx.dsegD6B88;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B9A]) = ctx.dsegD6B9A;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6B9E]) = ctx.dsegD6B9E;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6BA2]) = ctx.dsegD6BA2;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6BA6], ctx.playerWages, sizeof(ctx.playerWages));
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6C26], ctx.playerTrialWeeks, sizeof(ctx.playerTrialWeeks));
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C46]) = ctx.dsegD6C46;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kAdditionalInvestments]) = ctx.additionalInvestments;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C4E]) = ctx.dsegD6C4E;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6C50]) = ctx.dsegD6C50;

    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kYoureSacked]) = ctx.youreSacked;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kRetired]) = ctx.retired;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kClubBusinessActive]) = ctx.clubBusinessActive;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumNewJobOffers]) = ctx.numNewJobOffers;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumJobOffers]) = ctx.numJobOffers;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamNoOfferAccepted]) = ctx.teamNoOfferAccepted;
    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[CareerOffsets::kTeamFundsAccepted]) = ctx.teamFundsAccepted;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kIsNationalSelector]) = ctx.isNationalSelector;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]) = ctx.dsegD6CDC;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumOffers]) = ctx.numOffers;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kTimeToNegotiate]) = ctx.timeToNegotiate;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0A]) = ctx.dsegD6D0A;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0C]) = ctx.dsegD6D0C;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0E]) = ctx.dsegD6D0E;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D10]) = ctx.dsegD6D10;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D12]) = ctx.dsegD6D12;

    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGamesPlayed]) = ctx.gamesPlayed;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumWins]) = ctx.numWins;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumDrawn]) = ctx.numDrawn;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumLost]) = ctx.numLost;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGoalsScored]) = ctx.goalsScored;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kGoalsTaken]) = ctx.goalsTaken;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6C88], ctx.seasonStatsBlock, sizeof(ctx.seasonStatsBlock));
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA0]) = ctx.dsegD6CA0;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]) = ctx.dsegD6CA2;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD7E66]) = ctx.dsegD7E66;

    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]) = ctx.currentSeasonShowing;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]) = ctx.currentSeasonNumber;
    SwosVM::g_memByte[CareerOffsets::kSeasonPlaying] = ctx.seasonPlaying;
    SwosVM::g_memByte[CareerOffsets::kDsegE0048] = ctx.dsegE0048;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDF4F4]) = ctx.playMatchBlink;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]) = ctx.careerNumPlayers;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kSeasonFinancesBlock], ctx.seasonFinancesBlock, kSeasonFinancesBlockSize);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]) = ctx.numPlayersOnTransferMarket;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket], ctx.playersOnTransferMarket, kListedPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6F26Block], ctx.dsegD6F26Block, kListedPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD6DBA], ctx.dsegD6DBA, kTransferAiOffersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kNewJobOffers], ctx.newJobOffers, kJobOffersBufferSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kJobOffers], ctx.jobOffers, kJobOffersBufferSize);

    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers], ctx.currentMatchPlayers, kCurrentMatchPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCareerTeam], ctx.careerTeam, kCareerTeamBufferSize);
    if (m_active && ctx.teamId != 0) {
        reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam])->teamControls =
            (ctx.coachOrPlayer == 1) ? kPlayerCoach : kCoach;
    }
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kCareerHistory], ctx.careerHistory, kCareerHistoryBufferSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kUserTacticsStart], ctx.userTactics, 6 * kUserTacticsSize);

    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761A]) = ctx.dsegD761A;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD761C]) = ctx.dsegD761C;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8B6A]) = ctx.dsegD8B6A;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers], ctx.trainingMatchSavedPlayers, kCurrentMatchPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD8B8C], ctx.dsegD8B8C, kCurrentMatchPlayersSize);
    SwosVM::g_memByte[CareerOffsets::kDsegD8BAC] = ctx.dsegD8BAC;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8BAE]) = ctx.dsegD8BAE;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8BB0]) = ctx.dsegD8BB0;
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kLastMatchPlayers], ctx.lastMatchPlayers, kCurrentMatchPlayersSize);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegD8BD2], ctx.dsegD8BD2, kCurrentMatchPlayersSize);
    SwosVM::g_memByte[CareerOffsets::kCurrentTactics] = ctx.currentTactics;
    SwosVM::g_memByte[CareerOffsets::kDsegD8BF3] = ctx.dsegD8BF3;

    syncTrainingMatchSavedPlayers(&SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers],
                                  &SwosVM::g_memByte[CareerOffsets::kDsegD8B8C],
                                  &SwosVM::g_memByte[CareerOffsets::kCareerTeam],
                                  ctx.careerNumPlayers,
                                  &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers]);

    if (m_numPlayers > 1 && anyEuroCupActive() && ctx.teamId != 0) {
        activateEuroCup(ctx.euroCupIndex, false);
    }

    if (relink) {
        relinkCompetitionTeamPointers();
    }
}

void HotseatCareerManager::ensureVmTrainingMatchSavedPlayersSynced()
{
    uint16_t count = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
    syncTrainingMatchSavedPlayers(&SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers],
                                  &SwosVM::g_memByte[CareerOffsets::kDsegD8B8C],
                                  &SwosVM::g_memByte[CareerOffsets::kCareerTeam],
                                  count,
                                  &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers]);
}

void HotseatCareerManager::syncPlayersWithSelectedTeams(bool writeToSelected)
{
    if (!isActive() || !swos.selTeamsPtr || swos.g_numSelectedTeams <= 0)
        return;

    if (writeToSelected) {
        unpackAllH2hPackedTeams();
    }

    auto *selTeams = reinterpret_cast<TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return;

    int count = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
    for (int i = 0; i < m_numPlayers; ++i) {
        uint16_t targetId = static_cast<uint16_t>(m_players[i].teamId);
        if (targetId == 0)
            continue;

        for (int t = 0; t < count; ++t) {
            uint16_t slotTeamId = *reinterpret_cast<const uint16_t *>(&selTeams[t]);
            if (slotTeamId != targetId)
                continue;

            TeamControls desiredControls = (m_players[i].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            if (writeToSelected) {
                reinterpret_cast<TeamFile *>(m_players[i].careerTeam)->teamControls = desiredControls;
                std::memcpy(&selTeams[t], m_players[i].careerTeam, sizeof(TeamFile));
            } else {
                // After matches (e.g. H2H), sync non-active hotseat teams back from g_selectedTeams
                if (i != m_activePlayerIndex) {
                    std::memcpy(m_players[i].careerTeam, &selTeams[t], sizeof(TeamFile));
                    m_players[i].careerTeamLeague = selTeams[t].league;
                    reinterpret_cast<TeamFile *>(m_players[i].careerTeam)->teamControls = desiredControls;
                } else {
                    selTeams[t].teamControls = desiredControls;
                }
            }
            break;
        }
    }
}

void HotseatCareerManager::ensureHotseatTeamControlsInSelectedTeams()
{
    if (!isActive() || !swos.selTeamsPtr || swos.g_numSelectedTeams <= 0)
        return;

    auto *selTeams = reinterpret_cast<TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return;

    int count = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
    for (int t = 0; t < count; ++t) {
        int16_t tid = *reinterpret_cast<const int16_t *>(&selTeams[t]);
        int pIdx = findHotseatPlayerByTeamId(tid);
        if (pIdx >= 0) {
            selTeams[t].teamControls = (m_players[pIdx].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
        }
    }
}

static bool computeTeamCacheDelta(uint16_t teamWord, uint16_t activeTeamWord, int32_t selTeamsPtrVal, int32_t& outDelta)
{
    if (teamWord == activeTeamWord) {
        outDelta = static_cast<int32_t>(CareerOffsets::kCareerTeam) - selTeamsPtrVal;
        return true;
    }

    if (!selTeamsPtrVal || swos.g_numSelectedTeams <= 0)
        return false;

    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(selTeamsPtrVal));
    if (!selTeams)
        return false;

    int count = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
    for (int t = 0; t < count; ++t) {
        uint16_t slotTeamId = *reinterpret_cast<const uint16_t *>(&selTeams[t]);
        if (slotTeamId == teamWord) {
            outDelta = static_cast<int32_t>(t * sizeof(TeamFile));
            return true;
        }
    }
    return false;
}

static void relinkSingleCompetitionBuffer(uint32_t compOffset, uint16_t activeTeamWord, int32_t selTeamsPtrVal)
{
    if (compOffset == 0 || compOffset + 100 >= sizeof(SwosVM::g_memByte))
        return;

    uint16_t compType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[compOffset]);
    auto relinkBlock = [&](uint32_t base, uint16_t blockType) {
        if (base + 840 >= sizeof(SwosVM::g_memByte))
            return;

        int16_t numTeams = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[base + 49]);
        if (numTeams <= 0 || numTeams > 64)
            return;

        if (blockType == 0) {
            // League: team words at +301 (2B), cached deltas at +691 (18B stride)
            if (base + 691 + static_cast<uint32_t>(numTeams) * 18 > sizeof(SwosVM::g_memByte))
                return;
            for (int k = 0; k < numTeams; ++k) {
                uint16_t teamWord = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[base + 301 + k * 2]);
                int32_t delta = 0;
                if (computeTeamCacheDelta(teamWord, activeTeamWord, selTeamsPtrVal, delta)) {
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[base + 691 + k * 18]) = delta;
                }
            }
        } else if (blockType == 1 || blockType == 2) {
            // Cup / Tournament: team words at +589 (2B), cached deltas at +835 (+0x343, 4B stride)
            if (base + 835 + static_cast<uint32_t>(numTeams) * 4 > sizeof(SwosVM::g_memByte))
                return;
            for (int k = 0; k < numTeams; ++k) {
                uint16_t teamWord = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[base + 589 + k * 2]);
                int32_t delta = 0;
                if (computeTeamCacheDelta(teamWord, activeTeamWord, selTeamsPtrVal, delta)) {
                    *reinterpret_cast<int32_t *>(&SwosVM::g_memByte[base + 835 + k * 4]) = delta;
                }
            }
        }
    };

    if (compType == 0 || compType == 1) {
        relinkBlock(compOffset, compType);
    } else if (compType == 2) {
        // Tournaments store the live DIY_competitionStart block at +1055 (+0x41F) inside the tournament buffer
        uint32_t sub = compOffset + static_cast<uint32_t>(kTourSubCompOffset);
        if (sub + 52 < sizeof(SwosVM::g_memByte)) {
            uint16_t subType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[sub]);
            if (subType <= 2) {
                relinkBlock(sub, subType);
            }
        }
    }
}

void HotseatCareerManager::relinkCompetitionTeamPointers()
{
    if (!isActive() || !swos.selTeamsPtr)
        return;

    uint16_t activeTeamWord = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]);
    int32_t selTeamsPtrVal = static_cast<int32_t>(swos.selTeamsPtr);
    if (activeTeamWord == 0 || activeTeamWord == 0xFFFF || selTeamsPtrVal == 0)
        return;

    for (int c = 0; c < 5; ++c) {
        uint32_t activeContestPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + c * 4]);
        uint32_t compBufPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kArrayOfPointers + c * 4]);
        if (activeContestPtr != 0 && compBufPtr >= 5000 && compBufPtr < sizeof(SwosVM::g_memByte)) {
            relinkSingleCompetitionBuffer(compBufPtr, activeTeamWord, selTeamsPtrVal);
        }
    }

    // Relink active work buffer if currently populated with a valid league/cup
    uint16_t diyType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDiyCompetitionStart]);
    if (diyType <= 2) {
        relinkSingleCompetitionBuffer(static_cast<uint32_t>(CareerOffsets::kDiyCompetitionStart), activeTeamWord, selTeamsPtrVal);
    }
    uint16_t tourWorkType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDiyFileBufferCopy]);
    if (tourWorkType == 2) {
        relinkSingleCompetitionBuffer(static_cast<uint32_t>(CareerOffsets::kDiyFileBufferCopy), activeTeamWord, selTeamsPtrVal);
    }
}

void HotseatCareerManager::switchToPlayer(int index, bool relink /* = true */)
{
    if (index < 0 || index >= m_numPlayers)
        return;

    if (relink) {
        syncPlayersWithSelectedTeams(true);
    }

    m_activePlayerIndex = index;
    loadSwosContext(m_players[m_activePlayerIndex], relink);
    syncTransferMarketPoolsForCurrentPlayer();
}

int HotseatCareerManager::findNextPlayerForCurrentCalendarSlot() const
{
    if (!isActive() || m_numPlayers <= 1)
        return m_activePlayerIndex;

    uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    if (totalEntries == 0)
        return -1;

    uint16_t calOfs = 0;
    const uint8_t *calEntry = nullptr;
    int slotIdx = -1;

    // Prefer the active purple-highlighted fixture (dseg_174576 / dseg_17457A / dseg_174578)
    uint16_t seasonFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
    uint16_t careerFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]);
    int16_t hlIdx = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174576]);
    uint32_t hlHdr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457A]);
    uint16_t hlSlotIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174578]);

    if (seasonFin == 0 && careerFin == 0 && hlIdx >= 0 && hlIdx < totalEntries &&
        hlHdr >= CareerOffsets::kDsegDB3E6 && hlHdr + 22 <= CareerOffsets::kDsegDB3E6 + 1870) {
        const uint8_t *candEntry = &SwosVM::g_memByte[hlHdr];
        int totalSlots = candEntry[5];
        int remainingSlots = static_cast<int8_t>(candEntry[6]);
        if (totalSlots > 0 && remainingSlots > 0 && remainingSlots <= totalSlots) {
            calOfs = static_cast<uint16_t>(hlHdr - CareerOffsets::kDsegDB3E6);
            calEntry = candEntry;
            slotIdx = (hlSlotIdx < totalSlots) ? static_cast<int>(hlSlotIdx) : (totalSlots - remainingSlots);
        }
    }

    if (!calEntry) {
        uint16_t curCalIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);
        uint16_t curCalOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
        if (curCalIdx >= totalEntries || curCalOfs >= 1870)
            return -1;

        calEntry = &SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + curCalOfs];
        int totalSlots = calEntry[5];
        int remainingSlots = static_cast<int8_t>(calEntry[6]);
        if (remainingSlots <= 0 || totalSlots <= 0 || remainingSlots > totalSlots)
            return -1;

        calOfs = curCalOfs;
        slotIdx = totalSlots - remainingSlots;
    }

    int16_t team1Id = 0;
    int16_t team2Id = 0;

    auto calIt = m_calendarSlots.find(calOfs);
    if (calIt != m_calendarSlots.end() && slotIdx >= 0 && slotIdx < static_cast<int>(calIt->second.size())) {
        team1Id = *reinterpret_cast<const int16_t *>(&calIt->second[slotIdx][2]);
        team2Id = *reinterpret_cast<const int16_t *>(&calIt->second[slotIdx][4]);
    } else {
        size_t slotByteOfs = CareerOffsets::kDsegDB3E6 + calOfs + static_cast<size_t>(slotIdx) * 12;
        if (slotByteOfs + 16 > sizeof(SwosVM::g_memByte))
            return -1;
        team1Id = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[slotByteOfs + 12]);
        team2Id = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[slotByteOfs + 14]);
    }
    int p1 = findHotseatPlayerByTeamId(team1Id);
    int p2 = findHotseatPlayerByTeamId(team2Id);

    if (p1 >= 0 && !m_players[p1].matchPlayedThisTurn && !m_players[p1].turnCompleted)
        return p1;
    if (p2 >= 0 && !m_players[p2].matchPlayedThisTurn && !m_players[p2].turnCompleted)
        return p2;
    if (p1 >= 0 && !m_players[p1].turnCompleted)
        return p1;
    if (p2 >= 0 && !m_players[p2].turnCompleted)
        return p2;
    if (p1 >= 0)
        return p1;
    if (p2 >= 0)
        return p2;
    return -1;
}

void HotseatCareerManager::clearAllCalendarSlots()
{
    m_calendarSlots.clear();
    m_euroCupByCalOfs.clear();
    m_pendingPostMatchCalOfs = -1;
    m_pendingPostMatchSlotIdx = -1;
}

void HotseatCareerManager::resetCalendarEntrySlots(uint32_t entryVmOffset)
{
    if (!isActive() || m_numPlayers <= 1)
        return;
    if (entryVmOffset < CareerOffsets::kDsegDB3E6 || entryVmOffset >= CareerOffsets::kDsegDB3E6 + 1870)
        return;

    uint16_t calOfs = static_cast<uint16_t>(entryVmOffset - CareerOffsets::kDsegDB3E6);
    m_calendarSlots.erase(calOfs);
}

void HotseatCareerManager::shiftCalendarSlots(uint32_t insertVmOffset, uint16_t shiftBytes)
{
    if (!isActive() || m_numPlayers <= 1 || shiftBytes == 0)
        return;
    if (insertVmOffset < CareerOffsets::kDsegDB3E6 || insertVmOffset >= CareerOffsets::kDsegDB3E6 + 1870)
        return;

    uint16_t insertOfs = static_cast<uint16_t>(insertVmOffset - CareerOffsets::kDsegDB3E6);
    if (!m_calendarSlots.empty()) {
        std::map<uint16_t, std::vector<CalendarSlotBytes>> shifted;
        for (auto& kv : m_calendarSlots) {
            uint16_t newKey = (kv.first >= insertOfs) ? static_cast<uint16_t>(kv.first + shiftBytes) : kv.first;
            if (newKey < 1870) {
                shifted[newKey] = std::move(kv.second);
            }
        }
        m_calendarSlots = std::move(shifted);
    }
    if (!m_euroCupByCalOfs.empty()) {
        std::map<uint16_t, int> shiftedEuro;
        for (const auto& kv : m_euroCupByCalOfs) {
            uint16_t newKey = (kv.first >= insertOfs) ? static_cast<uint16_t>(kv.first + shiftBytes) : kv.first;
            if (newKey < 1870) {
                shiftedEuro[newKey] = kv.second;
            }
        }
        m_euroCupByCalOfs = std::move(shiftedEuro);
    }
}

static void captureSlotForHeader(std::map<uint16_t, std::vector<HotseatCareerManager::CalendarSlotBytes>>& slots,
                                 uint32_t entryVmOffset)
{
    if (entryVmOffset < CareerOffsets::kDsegDB3E6 || entryVmOffset + 22 > CareerOffsets::kDsegDB3E6 + 1870)
        return;

    uint16_t calOfs = static_cast<uint16_t>(entryVmOffset - CareerOffsets::kDsegDB3E6);
    uint8_t totalSlots = SwosVM::g_memByte[entryVmOffset + 5];
    auto& vec = slots[calOfs];
    if (totalSlots == 1 && !vec.empty()) {
        // Re-populated cup/replay entry cleared [entry+5]=0->1; reset stale slots if fixture changed or shrank
        if (vec.size() > 1 || std::memcmp(vec[0].data(), &SwosVM::g_memByte[entryVmOffset + 10], 12) != 0) {
            vec.clear();
        }
    }
    if (totalSlots > vec.size()) {
        HotseatCareerManager::CalendarSlotBytes bytes;
        std::memcpy(bytes.data(), &SwosVM::g_memByte[entryVmOffset + 10], 12);
        vec.push_back(bytes);
    }
}

void HotseatCareerManager::captureLeagueCalendarSlot()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    uint32_t entryVmOffset = A4.asDword();
    captureSlotForHeader(m_calendarSlots, entryVmOffset);
    A3 = entryVmOffset;
}

void HotseatCareerManager::captureCupCalendarSlot()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    uint32_t entryVmOffset = A6.asDword();
    captureSlotForHeader(m_calendarSlots, entryVmOffset);
    A5 = entryVmOffset;
}

void HotseatCareerManager::restoreCalendarEntryFirstSlots()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    for (const auto& kv : m_calendarSlots) {
        if (!kv.second.empty() && kv.first + 22 <= 1870) {
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + kv.first + 10],
                        kv.second[0].data(), 12);
        }
    }
}

void HotseatCareerManager::stageActiveCalendarSlotToVm()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    for (const auto& kv : m_calendarSlots) {
        uint16_t calOfs = kv.first;
        if (kv.second.empty() || calOfs + 22 > 1870)
            continue;

        const uint8_t *calEntry = &SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + calOfs];
        int totalSlots = calEntry[5];
        int remSlots = static_cast<int8_t>(calEntry[6]);
        if (remSlots > 0 && totalSlots > 0 && remSlots <= totalSlots) {
            int slotIdx = totalSlots - remSlots;
            if (slotIdx >= 0 && slotIdx < static_cast<int>(kv.second.size())) {
                std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + calOfs + 10],
                            kv.second[slotIdx].data(), 12);
            }
        }
    }
}

void HotseatCareerManager::resolvePostMatchCalendarSlotPtr()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    uint16_t curCalOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
    if (curCalOfs + 22 > 1870)
        return;

    auto it = m_calendarSlots.find(curCalOfs);
    if (it == m_calendarSlots.end() || it->second.empty())
        return;

    uint32_t entryVmOffset = static_cast<uint32_t>(CareerOffsets::kDsegDB3E6 + curCalOfs);
    int totalSlots = SwosVM::g_memByte[entryVmOffset + 5];
    int remSlots = static_cast<int8_t>(SwosVM::g_memByte[entryVmOffset + 6]);
    if (remSlots > 0 && totalSlots > 0 && remSlots <= totalSlots) {
        int slotIdx = totalSlots - remSlots;
        if (slotIdx >= 0 && slotIdx < static_cast<int>(it->second.size())) {
            std::memcpy(&SwosVM::g_memByte[entryVmOffset + 10], it->second[slotIdx].data(), 12);
            m_pendingPostMatchCalOfs = curCalOfs;
            m_pendingPostMatchSlotIdx = slotIdx;
        }
    }

    A0 = entryVmOffset;
    A1 = entryVmOffset;
}

void HotseatCareerManager::commitAndStageNextCalendarSlot()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    if (m_pendingPostMatchCalOfs < 0)
        return;

    uint16_t calOfs = static_cast<uint16_t>(m_pendingPostMatchCalOfs);
    int playedSlot = m_pendingPostMatchSlotIdx;
    m_pendingPostMatchCalOfs = -1;
    m_pendingPostMatchSlotIdx = -1;

    auto it = m_calendarSlots.find(calOfs);
    if (it == m_calendarSlots.end() || calOfs + 22 > 1870)
        return;

    uint32_t entryVmOffset = static_cast<uint32_t>(CareerOffsets::kDsegDB3E6 + calOfs);
    if (playedSlot >= 0 && playedSlot < static_cast<int>(it->second.size())) {
        std::memcpy(it->second[playedSlot].data(), &SwosVM::g_memByte[entryVmOffset + 10], 12);
    }

    int totalSlots = SwosVM::g_memByte[entryVmOffset + 5];
    int remSlots = static_cast<int8_t>(SwosVM::g_memByte[entryVmOffset + 6]);
    if (remSlots > 0 && totalSlots > 0 && remSlots <= totalSlots) {
        int nextSlot = totalSlots - remSlots;
        if (nextSlot >= 0 && nextSlot < static_cast<int>(it->second.size())) {
            std::memcpy(&SwosVM::g_memByte[entryVmOffset + 10], it->second[nextSlot].data(), 12);
            *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457E]) = entryVmOffset;
        }
    }
}

void HotseatCareerManager::clampCalcNextMatchSlotPtr()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    uint32_t hdr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457A]);
    if (hdr >= CareerOffsets::kDsegDB3E6 && hdr < CareerOffsets::kDsegDB3E6 + 1870) {
        *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457E]) = hdr;
    }
}

void HotseatCareerManager::resolveGameListSlotPtr()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    uint32_t calEntryVm = A6.asDword();
    uint32_t origSlotVm = A4.asDword();
    if (calEntryVm < CareerOffsets::kDsegDB3E6 || calEntryVm + 22 > CareerOffsets::kDsegDB3E6 + 1870 || origSlotVm < calEntryVm)
        return;

    uint16_t calOfs = static_cast<uint16_t>(calEntryVm - CareerOffsets::kDsegDB3E6);
    uint32_t slotIdx = (origSlotVm - calEntryVm) / 12;

    if (SwosVM::g_memByte[calEntryVm] == 3 && anyEuroCupActive()) {
        int euroIdx = findEuroCupForCalOfs(calOfs);
        if (euroIdx >= 0) {
            activateEuroCup(euroIdx, false);
        }
    }

    auto it = m_calendarSlots.find(calOfs);
    if (it == m_calendarSlots.end() || slotIdx >= it->second.size())
        return;

    uint32_t activeHdr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457A]);
    uint16_t activeSlotIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg174578]);

    // Always render slot 0 through calEntryVm (A4 == A6) so CareerGameListCommon draws
    // the competition title (#38) and round label (#39) for every league/cup block.
    if (slotIdx == 0) {
        std::memcpy(&SwosVM::g_memByte[calEntryVm + 10], it->second[0].data(), 12);
        if (calEntryVm == activeHdr) {
            *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457E]) =
                (activeSlotIdx == 0) ? calEntryVm : (calEntryVm + 1);
        }
        A4 = calEntryVm;
        return;
    }

    // Populate a safe scratch 22-byte calendar slot inside the VM extra memory tail (--extra-memory-size=60000)
    constexpr size_t kScratchBase = sizeof(SwosVM::g_memByte) - 1024;
    uint32_t row = (D0.asWord() >> 3) & 0x0F;
    uint32_t scratchVm = static_cast<uint32_t>(kScratchBase + row * 22);
    if (scratchVm + 22 <= sizeof(SwosVM::g_memByte)) {
        std::memset(&SwosVM::g_memByte[scratchVm], 0, 22);
        std::memcpy(&SwosVM::g_memByte[scratchVm], &SwosVM::g_memByte[calEntryVm], 10);
        std::memcpy(&SwosVM::g_memByte[scratchVm + 10], it->second[slotIdx].data(), 12);
        if (calEntryVm == activeHdr && slotIdx == activeSlotIdx) {
            *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457E]) = scratchVm;
        }
        A4 = scratchVm;
    }
}

bool HotseatCareerManager::ensureActivePlayerMatchesCurrentSlot()
{
    if (!isActive() || m_numPlayers <= 1)
        return true;

    uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    if (totalEntries == 0)
        return false;

    extern void cseg_34DA3();
    save68kRegisters();
    cseg_34DA3();
    restore68kRegisters();

    return syncActivePlayerToHighlightedSlot(true);
}

void HotseatCareerManager::refreshContestMenuUi() const
{
    if (!isActive() || swos.g_gameType != kGameTypeCareer)
        return;

    auto currentMenu = getCurrentMenu();
    auto packed = getCurrentPackedMenu();
    if (!currentMenu || !packed || !SwosVM::isSwosPtr(packed) || SwosVM::ptrToOffset(packed) != 482233)
        return;

    // Reset Entry 5 default string to aJobOffers (17282) because CheckJobOffers does not restore
    // it when returning early for numJobOffers > 0 after a previous player saw "NO JOB OFFERS".
    if (currentMenu->numEntries > 5) {
        currentMenu->entries()[5].fg.string.setRaw(17282);
    }

    extern void PrintNameAndSeason();
    extern void ContestSetButtons();
    extern void CheckTransfers();
    extern void CheckClubBusiness();
    extern void CheckJobOffers();
    extern void SetNationalSelectorButtons();

    save68kRegisters();
    A6 = swos.g_currentMenu;
    PrintNameAndSeason();
    A6 = swos.g_currentMenu;
    ContestSetButtons();
    A6 = swos.g_currentMenu;
    CheckTransfers();
    A6 = swos.g_currentMenu;
    CheckClubBusiness();
    A6 = swos.g_currentMenu;
    CheckJobOffers();
    A6 = swos.g_currentMenu;
    SetNationalSelectorButtons();
    restore68kRegisters();
}

bool HotseatCareerManager::syncActivePlayerToHighlightedSlot(bool refreshUi /* = true */)
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return false;

    uint16_t seasonFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
    uint16_t careerFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]);
    if (seasonFin != 0 || careerFin != 0) {
        if (anyEuroCupActive() && m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) {
            activateEuroCup(m_players[m_activePlayerIndex].euroCupIndex, true);
        }
        if (refreshUi) {
            refreshContestMenuUi();
        }
        return false;
    }

    stageActiveCalendarSlotToVm();

    int slotOwner = findNextPlayerForCurrentCalendarSlot();
    bool switched = false;
    if (slotOwner >= 0 && slotOwner != m_activePlayerIndex) {
        saveCurrentSwosContext(m_players[m_activePlayerIndex]);
        switchToPlayer(slotOwner, true);
        switched = true;
    } else if (anyEuroCupActive() && m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) {
        int wantEuro = m_players[m_activePlayerIndex].euroCupIndex;
        if (m_activeEuroCupIndex != wantEuro) {
            activateEuroCup(wantEuro, true);
        }
    }

    if (refreshUi || switched) {
        refreshContestMenuUi();
    }
    return switched;
}

bool HotseatCareerManager::canEndTurn() const
{
    if (!m_active)
        return false;
    return m_players[m_activePlayerIndex].matchPlayedThisTurn;
}

void HotseatCareerManager::onMatchCompleted()
{
    if (m_active && m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) {
        m_players[m_activePlayerIndex].matchPlayedThisTurn = true;
    }
}

bool HotseatCareerManager::endTurn()
{
    if (!canEndTurn())
        return false;

    saveCurrentSwosContext(m_players[m_activePlayerIndex]);
    m_players[m_activePlayerIndex].turnCompleted = true;

    int nextIndex = (m_activePlayerIndex + 1) % m_numPlayers;
    bool newRound = (nextIndex == 0);

    if (newRound) {
        for (int i = 0; i < m_numPlayers; ++i) {
            m_players[i].matchPlayedThisTurn = false;
            m_players[i].turnCompleted = false;
        }
    }

    switchToPlayer(nextIndex);
    return newRound;
}

bool HotseatCareerManager::advanceTurnAndResolveRound()
{
    if (!canEndTurn())
        return false;

    saveCurrentSwosContext(m_players[m_activePlayerIndex]);
    m_players[m_activePlayerIndex].turnCompleted = true;

    const int curCalIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);
    const bool calendarAdvancedByMatch = (curCalIdx != m_roundCalendarIndex);

    // If the calendar already moved past this round (e.g. last human slot finished, or Cup/Bye week),
    // remaining players get a clean post-round management turn without starting the next round's fixture.
    if (calendarAdvancedByMatch) {
        for (int i = 0; i < m_numPlayers; ++i) {
            if (!m_players[i].turnCompleted) {
                m_players[i].matchPlayedThisTurn = true;
            }
        }
    }

    int nextIdx = -1;
    if (!calendarAdvancedByMatch) {
        int slotOwner = findNextPlayerForCurrentCalendarSlot();
        if (slotOwner >= 0 && !m_players[slotOwner].turnCompleted && !m_players[slotOwner].matchPlayedThisTurn) {
            nextIdx = slotOwner;
        }
    }

    if (nextIdx < 0) {
        for (int step = 1; step <= m_numPlayers; ++step) {
            int cand = (m_activePlayerIndex + step) % m_numPlayers;
            if (!m_players[cand].turnCompleted) {
                nextIdx = cand;
                break;
            }
        }
    }

    if (nextIdx >= 0 && nextIdx != m_activePlayerIndex) {
        switchToPlayer(nextIdx, true);
        return false;
    }

    // Defensive check: if any human slot was unexpectedly left unplayed in this calendar step, advance once.
    const uint16_t curCalOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
    const uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    if (!calendarAdvancedByMatch && totalEntries > 0 && curCalIdx < totalEntries && curCalOfs < 1870) {
        int8_t rem = static_cast<int8_t>(SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + curCalOfs + 6]);
        if (rem > 0) {
            extern void NextMatch();
            save68kRegisters();
            NextMatch();
            restore68kRegisters();
        }
    }

    for (int i = 0; i < m_numPlayers; ++i) {
        m_players[i].matchPlayedThisTurn = false;
        m_players[i].turnCompleted = false;
    }

    m_roundCalendarIndex = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);

    int startPlayer = findNextPlayerForCurrentCalendarSlot();
    if (startPlayer < 0)
        startPlayer = 0;

    switchToPlayer(startPlayer, true);
    return true;
}

static void applySeasonOutcomeToPlayerContext(HotseatPlayerContext& p, uint16_t goalsFor, uint16_t goalsAgainst,
                                              bool wonByPenalties, bool lostByPenalties)
{
    p.gamesPlayed += 1;
    p.goalsScored += goalsFor;
    p.goalsTaken += goalsAgainst;

    // dseg_D6C88..D6C9E words: [0]=games, [1]=wins, [2]=draws, [3]=losses, [4]=gf, [5]=ga
    p.seasonStatsBlock[0] += 1;
    p.seasonStatsBlock[4] += goalsFor;
    p.seasonStatsBlock[5] += goalsAgainst;

    if (goalsFor > goalsAgainst || wonByPenalties) {
        p.numWins += 1;
        p.seasonStatsBlock[1] += 1;
    } else if (goalsFor < goalsAgainst || lostByPenalties) {
        p.numLost += 1;
        p.seasonStatsBlock[3] += 1;
    } else {
        p.numDrawn += 1;
        p.seasonStatsBlock[2] += 1;
    }
}

void HotseatCareerManager::recordHeadToHeadResult(int topPlayerIdx, int botPlayerIdx, uint16_t topGoals, uint16_t botGoals,
                                                  uint16_t topPenGoals /* = 0 */, uint16_t botPenGoals /* = 0 */)
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    bool hasPenResult = (topGoals == botGoals) && (topPenGoals != botPenGoals);
    bool topPenWin = hasPenResult && (topPenGoals > botPenGoals);
    bool botPenWin = hasPenResult && (botPenGoals > topPenGoals);

    // SWOS cseg_2A71E natively updates whichever side points to offset careerTeam (m_activePlayerIndex).
    // Update the other human participant(s) directly so H2H wins/losses/goals are never lost.
    if (topPlayerIdx >= 0 && topPlayerIdx < m_numPlayers && topPlayerIdx != m_activePlayerIndex) {
        applySeasonOutcomeToPlayerContext(m_players[topPlayerIdx], topGoals, botGoals, topPenWin, botPenWin);
    }
    if (botPlayerIdx >= 0 && botPlayerIdx < m_numPlayers && botPlayerIdx != m_activePlayerIndex) {
        applySeasonOutcomeToPlayerContext(m_players[botPlayerIdx], botGoals, topGoals, botPenWin, topPenWin);
    }
}

void HotseatCareerManager::onAfterPlayMatch()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    unpackAllH2hPackedTeams();

    if (m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) {
        saveCurrentSwosContext(m_players[m_activePlayerIndex]);
        syncPlayersWithSelectedTeams(true);
    }

    // Clear turn-lock flags so control follows the newly purple-highlighted fixture immediately
    for (int i = 0; i < m_numPlayers; ++i) {
        m_players[i].matchPlayedThisTurn = false;
        m_players[i].turnCompleted = false;
    }
    m_roundCalendarIndex = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);

    if (syncActivePlayerToHighlightedSlot(true)) {
        setPendingOfferPrompt(true);
    }
}

void HotseatCareerManager::onWeeklyTickForAllPlayers()
{
    static bool s_inWeeklyTick = false;
    if (s_inWeeklyTick || !isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    unpackAllH2hPackedTeams();

    struct Guard {
        bool& flag;
        explicit Guard(bool& f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(s_inWeeklyTick);

    extern void cseg_94DB1();

    int activeIdx = m_activePlayerIndex;
    saveCurrentSwosContext(m_players[activeIdx]);

    for (int i = 0; i < m_numPlayers; ++i) {
        if (i == activeIdx)
            continue;

        loadSwosContext(m_players[i], false);

        // Mirror cseg_8EA82's weekly timer increments before entering cseg_94DB1
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0E]) += 1;
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D10]) += 1;
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D12]) += 1;

        save68kRegisters();
        cseg_94DB1();
        restore68kRegisters();

        saveCurrentSwosContext(m_players[i]);
    }

    // cseg_8EA82 clears dseg_DF4F4 only for the VM's currently loaded player after returning
    // from cseg_94DB1; reset playMatchBlink for every hotseat player so all players begin the
    // new week with TRANSFERS enabled.
    for (int i = 0; i < m_numPlayers; ++i) {
        m_players[i].playMatchBlink = 0;
    }

    loadSwosContext(m_players[activeIdx], false);
}

void HotseatCareerManager::onSeasonFinishedForAllPlayers()
{
    static bool s_inSeasonEnd = false;
    if (s_inSeasonEnd || !isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    struct Guard {
        bool& flag;
        explicit Guard(bool& f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(s_inSeasonEnd);

    extern void cseg_3C07C();

    int activeIdx = m_activePlayerIndex;
    saveCurrentSwosContext(m_players[activeIdx]);
    syncPlayersWithSelectedTeams(true);

    for (int i = 0; i < m_numPlayers; ++i) {
        if (i == activeIdx)
            continue;

        loadSwosContext(m_players[i], true);
        save68kRegisters();
        cseg_3C07C();
        restore68kRegisters();
        saveCurrentSwosContext(m_players[i]);
    }

    loadSwosContext(m_players[activeIdx], true);
}

void HotseatCareerManager::onNewSeasonInitialized()
{
    if (m_initializingSeason || !isActive() || m_numPlayers <= 1)
        return;

    ScopedInitializingSeason seasonGuard(*this);
    extern void cseg_5DEB9();
    extern void cseg_3D204();

    int activeIdx = m_activePlayerIndex;
    saveCurrentSwosContext(m_players[activeIdx]);

    uint16_t snapSeasonShowing = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonShowing]);
    uint16_t snapSeasonNum = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentSeasonNumber]);
    uint8_t snapSeasonPlaying = SwosVM::g_memByte[CareerOffsets::kSeasonPlaying];
    uint16_t snapD6CA2 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CA2]);
    uint16_t snapD7E66 = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD7E66]);
    uint8_t snapE0048 = SwosVM::g_memByte[CareerOffsets::kDsegE0048];
    uint16_t snapCalIdx = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);

    // Refresh non-active squads & divisions updated during inter-season promotion/relegation
    syncPlayersWithSelectedTeams(false);

    const auto& activeCtx = m_players[activeIdx];
    for (int i = 0; i < m_numPlayers; ++i) {
        if (i == activeIdx)
            continue;

        auto& p = m_players[i];
        p.currentSeasonShowing = snapSeasonShowing;
        p.currentSeasonNumber = snapSeasonNum;
        p.seasonPlaying = snapSeasonPlaying;
        p.dsegD6CA2 = snapD6CA2;
        p.dsegD7E66 = snapD7E66;
        p.dsegE0048 = snapE0048;
        p.dsegD6B62 = activeCtx.dsegD6B62;
        p.dsegD6B64 = activeCtx.dsegD6B64;
        p.dsegD6B94 = activeCtx.dsegD6B94;
        p.dsegD6C46 = activeCtx.dsegD6C46;
        p.additionalInvestments = activeCtx.additionalInvestments;
        p.dsegD6CDC = activeCtx.dsegD6CDC;
        p.teamNoOfferAccepted = -1;
        p.teamFundsAccepted = 0;
        p.dsegD6D0C = 0;
        p.playMatchBlink = 0;
        p.retired = 0;
        p.youreSacked = 0;
        p.stayingInOldClub = (snapSeasonShowing == 0) ? 0 : static_cast<int16_t>(p.stayingInOldClub + 1);
        p.numJobOffers = 0;
        p.numNewJobOffers = 0;
        p.dsegD6CA0 = 0;
        p.timeToNegotiate = 6;
        p.numOffers = 0;
        p.dsegD6D0E = 0;
        p.dsegD6D10 = 0;
        p.dsegD6D12 = 0;
        p.dsegD6D0A = 0;
        p.numPlayersOnTransferMarket = 0;
        std::memset(p.playersOnTransferMarket, 0, sizeof(p.playersOnTransferMarket));
        std::memset(p.dsegD6F26Block, 0, sizeof(p.dsegD6F26Block));
        std::memset(p.dsegD6DBA, 0, sizeof(p.dsegD6DBA));
        std::memset(p.newJobOffers, 0, sizeof(p.newJobOffers));
        std::memset(p.jobOffers, 0, sizeof(p.jobOffers));
        p.gamesPlayed = 0;
        p.numWins = 0;
        p.numDrawn = 0;
        p.numLost = 0;
        p.goalsScored = 0;
        p.goalsTaken = 0;
        std::memset(p.seasonStatsBlock, 0, sizeof(p.seasonStatsBlock));

        size_t histOfs = static_cast<size_t>(snapSeasonShowing) * kCareerHistoryEntrySize;
        if (histOfs + kCareerHistoryEntrySize <= kCareerHistoryBufferSize) {
            std::memcpy(&p.careerHistory[histOfs], activeCtx.careerHistory + histOfs, kCareerHistoryEntrySize);
            *reinterpret_cast<uint16_t *>(&p.careerHistory[histOfs]) = p.teamId;
            std::memset(&p.careerHistory[histOfs + 2], 0, 18);
            std::memcpy(&p.careerHistory[histOfs + 2], p.teamName.c_str(), std::min(p.teamName.size(), size_t{17}));
            uint32_t euroStrOfs = (p.euroCupIndex >= 0 && p.euroCupIndex < kNumEuroCups && m_euroCupActive[p.euroCupIndex])
                ? m_euroCupSeasonStrOfs[p.euroCupIndex]
                : 0;
            *reinterpret_cast<uint32_t *>(&p.careerHistory[histOfs + 34]) = euroStrOfs;
        }

        loadSwosContext(p, true);
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 0;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 0;

        save68kRegisters();
        cseg_5DEB9();
        cseg_3D204();
        restore68kRegisters();

        saveCurrentSwosContext(p);
    }

    // Clear stale Cup 2 / Euro Cup VM buffers when unused by any active competition
    if (*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]) == 0) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], 0, kCup2BufferSize);
    }
    if (!anyEuroCupActive() && *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) == 0) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA11F], m_euroCupScheduleTemplate, sizeof(m_euroCupScheduleTemplate));
    }

    for (int i = 0; i < m_numPlayers; ++i) {
        m_players[i].matchPlayedThisTurn = false;
        m_players[i].turnCompleted = false;
    }
    m_roundCalendarIndex = snapCalIdx;

    syncPlayersWithSelectedTeams(true);
    syncTransferMarketPoolsForCurrentPlayer();

    int firstPlayer = findNextPlayerForCurrentCalendarSlot();
    if (firstPlayer < 0)
        firstPlayer = activeIdx;
    switchToPlayer(firstPlayer, true);
}

void HotseatCareerManager::saveCurrentEuroCup()
{
    if (m_activeEuroCupIndex >= 0 && m_activeEuroCupIndex < kNumEuroCups && m_euroCupActive[m_activeEuroCupIndex]) {
        std::memcpy(m_euroCupBuffers[m_activeEuroCupIndex].data(),
                    &SwosVM::g_memByte[CareerOffsets::kDsegDA0E2],
                    kEuroCupBufferSize);
    }
}

void HotseatCareerManager::activateEuroCup(int euroIdx, bool relink /* = true */)
{
    if (!anyEuroCupActive())
        return;

    if (euroIdx < 0 || euroIdx >= kNumEuroCups || !m_euroCupActive[euroIdx]) {
        saveCurrentEuroCup();
        m_activeEuroCupIndex = -1;
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA11F], m_euroCupScheduleTemplate, sizeof(m_euroCupScheduleTemplate));
        *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) = 0;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CBE]) = 0;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegE092F]) = 0xFFFF;
        return;
    }

    if (m_activeEuroCupIndex != euroIdx) {
        saveCurrentEuroCup();
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2],
                    m_euroCupBuffers[euroIdx].data(),
                    kEuroCupBufferSize);
        m_activeEuroCupIndex = euroIdx;
    }

    *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) = m_euroCupCopyOfs[euroIdx];
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CBE]) = static_cast<uint16_t>(euroIdx);
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegE092F]) = static_cast<uint16_t>(euroIdx);

    if (relink) {
        relinkCompetitionTeamPointers();
    }
}

int HotseatCareerManager::findEuroCupForCalOfs(uint16_t calOfs) const
{
    auto it = m_euroCupByCalOfs.find(calOfs);
    if (it != m_euroCupByCalOfs.end())
        return it->second;
    if (m_activeEuroCupIndex >= 0 && m_activeEuroCupIndex < kNumEuroCups && m_euroCupActive[m_activeEuroCupIndex])
        return m_activeEuroCupIndex;
    for (int e = 0; e < kNumEuroCups; ++e) {
        if (m_euroCupActive[e])
            return e;
    }
    return -1;
}

void HotseatCareerManager::maskOtherEuroCupCalendarEntries()
{
    if (!isActive() || m_numPlayers <= 1 || !anyEuroCupActive() || m_euroCupByCalOfs.empty())
        return;

    uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    size_t scanOfs = CareerOffsets::kDsegDB3E6;
    for (uint16_t i = 0; i < totalEntries && scanOfs + 22 <= CareerOffsets::kDsegDB3E6 + 1870; ++i) {
        if (SwosVM::g_memByte[scanOfs] == 3) {
            uint16_t calOfs = static_cast<uint16_t>(scanOfs - CareerOffsets::kDsegDB3E6);
            auto it = m_euroCupByCalOfs.find(calOfs);
            if (it != m_euroCupByCalOfs.end() && it->second != m_activeEuroCupIndex) {
                SwosVM::g_memByte[scanOfs] = 0xFE;
            }
        }
        uint16_t len = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[scanOfs + 2]);
        if (len < 22) break;
        scanOfs += len;
    }
}

void HotseatCareerManager::unmaskEuroCupCalendarEntries()
{
    if (m_maskingForEuroCupInit)
        return;

    uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    size_t scanOfs = CareerOffsets::kDsegDB3E6;
    for (uint16_t i = 0; i < totalEntries && scanOfs + 22 <= CareerOffsets::kDsegDB3E6 + 1870; ++i) {
        if (SwosVM::g_memByte[scanOfs] == 0xFE) {
            SwosVM::g_memByte[scanOfs] = 3;
        }
        uint16_t len = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[scanOfs + 2]);
        if (len < 22) break;
        scanOfs += len;
    }
}

void HotseatCareerManager::initAdditionalEuroCups()
{
    if (!isActive() || swos.g_gameType != kGameTypeCareer)
        return;

    resetEuroCups();
    for (int p = 0; p < m_numPlayers; ++p)
        m_players[p].euroCupIndex = -1;

    // Preserve static European cup week schedule template (dseg_DA11F..23)
    std::memcpy(m_euroCupScheduleTemplate, &SwosVM::g_memByte[CareerOffsets::kDsegDA11F], sizeof(m_euroCupScheduleTemplate));

    int16_t firstEuroIdx = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegE092F]);
    uint32_t activeEuroPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]);
    if (firstEuroIdx >= 0 && firstEuroIdx < kNumEuroCups && activeEuroPtr == m_euroCupCopyOfs[firstEuroIdx]) {
        m_euroCupActive[firstEuroIdx] = true;
        m_activeEuroCupIndex = firstEuroIdx;
        std::memcpy(m_euroCupBuffers[firstEuroIdx].data(),
                    &SwosVM::g_memByte[CareerOffsets::kDsegDA0E2],
                    kEuroCupBufferSize);
        uint32_t strPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA109]);
        m_euroCupSeasonStrOfs[firstEuroIdx] = (strPtr >= CareerOffsets::kChairmanScenes)
            ? (strPtr - CareerOffsets::kChairmanScenes)
            : 0;

        for (int p = 0; p < m_numPlayers; ++p) {
            if (isTeamInEuroCupDef(m_euroCupCopyOfs[firstEuroIdx], static_cast<uint16_t>(m_players[p].teamId)))
                m_players[p].euroCupIndex = static_cast<int8_t>(firstEuroIdx);
        }

        uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
        size_t scanOfs = CareerOffsets::kDsegDB3E6;
        for (uint16_t c = 0; c < totalEntries && scanOfs + 22 <= CareerOffsets::kDsegDB3E6 + 1870; ++c) {
            if (SwosVM::g_memByte[scanOfs] == 3) {
                m_euroCupByCalOfs[static_cast<uint16_t>(scanOfs - CareerOffsets::kDsegDB3E6)] = firstEuroIdx;
            }
            uint16_t len = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[scanOfs + 2]);
            if (len < 22) break;
            scanOfs += len;
        }
    }

    if (m_numPlayers <= 1)
        return;

    extern void cseg_8B2D3();
    extern void GetCurrentSeasonPointer();

    for (int e = 0; e < kNumEuroCups; ++e) {
        if (m_euroCupActive[e])
            continue;

        bool anyHumanInCup = false;
        for (int p = 0; p < m_numPlayers; ++p) {
            if (m_players[p].euroCupIndex < 0 &&
                isTeamInEuroCupDef(m_euroCupCopyOfs[e], static_cast<uint16_t>(m_players[p].teamId))) {
                m_players[p].euroCupIndex = static_cast<int8_t>(e);
                anyHumanInCup = true;
            }
        }
        if (!anyHumanInCup)
            continue;

        saveCurrentEuroCup();

        // Temporarily mask type=3 entries of already-initialized European cups so cseg_8B2D3/cseg_8BC06 only see cup e
        m_maskingForEuroCupInit = true;
        uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
        size_t scanOfs = CareerOffsets::kDsegDB3E6;
        for (uint16_t c = 0; c < totalEntries && scanOfs + 22 <= CareerOffsets::kDsegDB3E6 + 1870; ++c) {
            if (SwosVM::g_memByte[scanOfs] == 3) {
                SwosVM::g_memByte[scanOfs] = 0xFE;
            }
            uint16_t len = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[scanOfs + 2]);
            if (len < 22) break;
            scanOfs += len;
        }

        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA11F], m_euroCupScheduleTemplate, sizeof(m_euroCupScheduleTemplate));

        save68kRegisters();
        A0 = m_euroCupCopyOfs[e];
        D0 = 3;
        D2 = 0;
        cseg_8B2D3();
        restore68kRegisters();

        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA11F], m_euroCupScheduleTemplate, sizeof(m_euroCupScheduleTemplate));

        m_euroCupActive[e] = true;
        m_activeEuroCupIndex = e;
        std::memcpy(m_euroCupBuffers[e].data(),
                    &SwosVM::g_memByte[CareerOffsets::kDsegDA0E2],
                    kEuroCupBufferSize);
        uint32_t strPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA109]);
        m_euroCupSeasonStrOfs[e] = (strPtr >= CareerOffsets::kChairmanScenes)
            ? (strPtr - CareerOffsets::kChairmanScenes)
            : 0;

        totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
        scanOfs = CareerOffsets::kDsegDB3E6;
        for (uint16_t c = 0; c < totalEntries && scanOfs + 22 <= CareerOffsets::kDsegDB3E6 + 1870; ++c) {
            if (SwosVM::g_memByte[scanOfs] == 3) {
                m_euroCupByCalOfs[static_cast<uint16_t>(scanOfs - CareerOffsets::kDsegDB3E6)] = e;
            } else if (SwosVM::g_memByte[scanOfs] == 0xFE) {
                SwosVM::g_memByte[scanOfs] = 3;
            }
            uint16_t len = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[scanOfs + 2]);
            if (len < 22) break;
            scanOfs += len;
        }
        m_maskingForEuroCupInit = false;
    }

    // Restore active player's European cup into VM and update their season history entry string
    int activeIdx = (m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) ? m_activePlayerIndex : 0;
    int activeEuro = m_players[activeIdx].euroCupIndex;
    if (anyEuroCupActive()) {
        activateEuroCup(activeEuro, false);
        save68kRegisters();
        GetCurrentSeasonPointer();
        uint32_t seasonPtr = A0.asDword();
        restore68kRegisters();
        if (seasonPtr >= CareerOffsets::kCareerHistory && seasonPtr + kCareerHistoryEntrySize <= CareerOffsets::kCareerHistory + kCareerHistoryBufferSize) {
            uint32_t strOfs = (activeEuro >= 0 && activeEuro < kNumEuroCups && m_euroCupActive[activeEuro])
                ? m_euroCupSeasonStrOfs[activeEuro]
                : 0;
            *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[seasonPtr + 34]) = strOfs;
        }
    }
}

void HotseatCareerManager::activateEuroCupForCurrentCalendarEntry()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer || !anyEuroCupActive())
        return;

    uint16_t curCalOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
    if (curCalOfs + 22 > 1870)
        return;

    if (SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + curCalOfs] == 3) {
        int euroIdx = findEuroCupForCalOfs(curCalOfs);
        if (euroIdx >= 0) {
            activateEuroCup(euroIdx, true);
        }
    }
}

void HotseatCareerManager::activateEuroCupForSeasonEnd(int euroIdx)
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer || !anyEuroCupActive())
        return;

    if (euroIdx >= 0 && euroIdx < kNumEuroCups && m_euroCupActive[euroIdx]) {
        activateEuroCup(euroIdx, false);
    } else {
        saveCurrentEuroCup();
        m_activeEuroCupIndex = -1;
        *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) = 0;
    }
}

void HotseatCareerManager::addHumanTeamsToCupSelection(uint32_t listBaseVm)
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    uint16_t dsegE0052 = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegE0052]);
    if (dsegE0052 != 0)
        return;

    uint16_t remSlots = D6.asWord();
    uint32_t listCurVm = A2.asDword();
    uint32_t selEndVm = A4.asDword();
    if (remSlots == 0 || listCurVm < listBaseVm || selEndVm < swos.selTeamsPtr)
        return;

    uint16_t fileTeamCount = (static_cast<uint16_t>(SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer]) << 8) |
                             SwosVM::g_memByte[CareerOffsets::kTeamFileBuffer + 1];
    size_t maxFileTeams = (64014 - 2) / sizeof(TeamFile);
    if (fileTeamCount > maxFileTeams)
        fileTeamCount = static_cast<uint16_t>(maxFileTeams);

    for (int i = 0; i < m_numPlayers && remSlots > 0; ++i) {
        uint16_t tid = static_cast<uint16_t>(m_players[i].teamId);
        if (tid == 0)
            continue;

        bool alreadyInCup = false;
        for (uint32_t ptr = listBaseVm; ptr + 2 <= listCurVm; ptr += 2) {
            if (*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[ptr]) == tid) {
                alreadyInCup = true;
                break;
            }
        }
        if (alreadyInCup)
            continue;

        // Verify this club exists in the currently loaded domestic team file before adding it
        const TeamFile *fileEntry = nullptr;
        for (uint16_t t = 0; t < fileTeamCount; ++t) {
            size_t ofs = CareerOffsets::kTeamFileBuffer + 2 + static_cast<size_t>(t) * sizeof(TeamFile);
            const auto *tf = reinterpret_cast<const TeamFile *>(&SwosVM::g_memByte[ofs]);
            uint16_t entryId = *reinterpret_cast<const uint16_t *>(tf);
            if (entryId == tid) {
                fileEntry = tf;
                break;
            }
        }
        if (!fileEntry)
            continue;

        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[listCurVm]) = tid;
        listCurVm += 2;
        remSlots -= 1;

        // Ensure lower-division human clubs are also cached in g_selectedTeams with human controls
        int selCount = std::min<int>(swos.g_numSelectedTeams, 68400 / sizeof(TeamFile));
        bool inSelected = false;
        for (int s = 0; s < selCount; ++s) {
            size_t selOfs = swos.selTeamsPtr + static_cast<size_t>(s) * sizeof(TeamFile);
            if (*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[selOfs]) == tid) {
                inSelected = true;
                break;
            }
        }
        if (!inSelected && selEndVm + sizeof(TeamFile) <= swos.selTeamsPtr + 68400) {
            std::memcpy(&SwosVM::g_memByte[selEndVm], fileEntry, sizeof(TeamFile));
            reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[selEndVm])->teamControls =
                (m_players[i].coachOrPlayer == 1) ? kPlayerCoach : kCoach;
            selEndVm += sizeof(TeamFile);
            swos.g_numSelectedTeams += 1;
        }
    }

    D6 = remSlots;
    A2 = listCurVm;
    A4 = selEndVm;
}

void HotseatCareerManager::markPlayerMatchCompleted(int playerIndex)
{
    if (playerIndex >= 0 && playerIndex < m_numPlayers) {
        m_players[playerIndex].matchPlayedThisTurn = true;
    }
}

static bool teamNamesMatch(const std::string& a, const char *b)
{
    if (!b || a.empty())
        return false;

    std::string lhs(a);
    std::string rhs(b);
    trimTrailingSwosPadding(lhs);
    trimTrailingSwosPadding(rhs);

    return !lhs.empty() && lhs == rhs;
}

int HotseatCareerManager::findHotseatPlayerByTeamName(const char *teamName) const
{
    if (!teamName || !m_active)
        return -1;

    for (int i = 0; i < m_numPlayers; ++i) {
        if (teamNamesMatch(m_players[i].teamName, teamName))
            return i;
    }
    return -1;
}

int HotseatCareerManager::findHotseatPlayerByTeamId(int16_t teamId) const
{
    if (!m_active)
        return -1;

    for (int i = 0; i < m_numPlayers; ++i) {
        if (m_players[i].teamId == teamId)
            return i;
    }
    return -1;
}

int HotseatCareerManager::queueTransferOffer(const TransferOffer& offer)
{
    TransferOffer o = offer;
    o.id = m_nextOfferId++;
    o.pending = true;
    o.accepted = false;
    o.rejected = false;
    m_transferOffers.push_back(o);
    return o.id;
}

std::vector<HotseatCareerManager::TransferOffer> HotseatCareerManager::getPendingOffersForPlayer(int playerIndex) const
{
    std::vector<TransferOffer> result;
    for (const auto& offer : m_transferOffers) {
        if (offer.toPlayerIndex == playerIndex && offer.pending) {
            result.push_back(offer);
        }
    }
    return result;
}

HotseatCareerManager::TransferOffer* HotseatCareerManager::getTransferOfferById(int offerId)
{
    for (auto& offer : m_transferOffers) {
        if (offer.id == offerId)
            return &offer;
    }
    return nullptr;
}

const HotseatCareerManager::TransferOffer* HotseatCareerManager::getTransferOfferById(int offerId) const
{
    for (const auto& offer : m_transferOffers) {
        if (offer.id == offerId)
            return &offer;
    }
    return nullptr;
}

static void getBuyerAndSellerIndices(const HotseatCareerManager::TransferOffer& offer, int& buyerIdx, int& sellerIdx)
{
    if (offer.isCounterOffer) {
        buyerIdx = offer.toPlayerIndex;
        sellerIdx = offer.fromPlayerIndex;
    } else {
        buyerIdx = offer.fromPlayerIndex;
        sellerIdx = offer.toPlayerIndex;
    }
}

namespace {

struct CopiedTransferPlayer {
    PlayerFile player{};
    uint32_t wage = 0;
};

struct PendingOfferRemap {
    bool removed = false;
    int oldOrd = -1;
    int newOrd = -1;
};

void collectUniqueValidOrdinals(int primary, int secondary, std::vector<int>& out)
{
    out.clear();
    if (primary >= 0)
        out.push_back(primary);
    if (secondary >= 0 && secondary != primary)
        out.push_back(secondary);
}

bool isOrdinalActiveInClub(const HotseatPlayerContext& ctx, int ord)
{
    if (ord < 0 || ord >= kMaxCareerSquadPlayers)
        return false;
    uint16_t count = ctx.careerNumPlayers;
    if (count == 0)
        return false;
    uint16_t limit = std::min<uint16_t>(count, static_cast<uint16_t>(kCurrentMatchPlayersSize));
    if (hasInitializedMatchPlayers(ctx.currentMatchPlayers, limit)) {
        for (uint16_t i = 0; i < limit; ++i) {
            if (ctx.currentMatchPlayers[i] == static_cast<uint8_t>(ord))
                return true;
        }
        return false;
    }
    return ord < static_cast<int>(count);
}

bool appendPlayerToClub(HotseatPlayerContext& club, const CopiedTransferPlayer& incoming)
{
    uint16_t count = std::max<uint16_t>(club.careerNumPlayers, static_cast<uint16_t>(kMinCareerSquadPlayers));
    if (count >= kMaxCareerSquadPlayers)
        return false;

    bool hasMatchPlayers = hasInitializedMatchPlayers(club.currentMatchPlayers, count);
    size_t slot = count;
    if (hasMatchPlayers) {
        for (size_t cand = 0; cand < kMaxCareerSquadPlayers; ++cand) {
            bool used = false;
            for (uint16_t k = 0; k < count && k < kCurrentMatchPlayersSize; ++k) {
                if (club.currentMatchPlayers[k] == cand) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                slot = cand;
                break;
            }
        }
    }

    size_t dstOffset = kTeamFileHeaderSize + slot * kPlayerFileSize;
    if (dstOffset + kPlayerFileSize > kCareerTeamBufferSize)
        return false;

    std::memcpy(&club.careerTeam[dstOffset], &incoming.player, kPlayerFileSize);
    auto *newPlayer = reinterpret_cast<PlayerFile *>(&club.careerTeam[dstOffset]);
    newPlayer->positionAndFace = static_cast<byte>(newPlayer->positionAndFace & ~0x02u);

    if (newPlayer->shirtNumber != 0) {
        bool usedNumbers[33] = { false };
        for (uint16_t k = 0; k < count; ++k) {
            size_t idx = hasMatchPlayers ? club.currentMatchPlayers[k] : k;
            if (kTeamFileHeaderSize + idx * kPlayerFileSize + kPlayerFileSize <= kCareerTeamBufferSize) {
                uint8_t num = reinterpret_cast<const PlayerFile *>(&club.careerTeam[kTeamFileHeaderSize + idx * kPlayerFileSize])->shirtNumber;
                if (num < 33)
                    usedNumbers[num] = true;
            }
        }
        if (newPlayer->shirtNumber < 33 && usedNumbers[newPlayer->shirtNumber]) {
            for (uint8_t sn = 1; sn < 33; ++sn) {
                if (!usedNumbers[sn]) {
                    newPlayer->shirtNumber = sn;
                    break;
                }
            }
        }
    }

    if (hasMatchPlayers && count < kCurrentMatchPlayersSize) {
        club.currentMatchPlayers[count] = static_cast<uint8_t>(slot);
    }
    if (slot < kPlayerWagesCount) {
        club.playerWages[slot] = incoming.wage;
        club.playerTrialWeeks[slot] = 0;
        club.dsegD8B8C[slot] = newPlayer->shirtNumber;
    }
    club.careerNumPlayers = count + 1;
    return true;
}

PendingOfferRemap removeOrdinalFromClub(HotseatPlayerContext& club, int soldOrd, std::vector<int>& pendingRemovals)
{
    PendingOfferRemap result;
    uint16_t count = club.careerNumPlayers;
    if (count == 0 || soldOrd < 0 || soldOrd >= kMaxCareerSquadPlayers)
        return result;

    result.removed = true;
    result.oldOrd = soldOrd;

    size_t lastOrdinal = count - 1;
    bool denseCompact = (static_cast<size_t>(soldOrd) != lastOrdinal) &&
                        isOrdinalActiveInClub(club, static_cast<int>(lastOrdinal));

    size_t soldOffset = kTeamFileHeaderSize + static_cast<size_t>(soldOrd) * kPlayerFileSize;
    size_t clearOffset = kTeamFileHeaderSize + (denseCompact ? lastOrdinal : static_cast<size_t>(soldOrd)) * kPlayerFileSize;

    if (denseCompact && soldOffset + kPlayerFileSize <= kCareerTeamBufferSize &&
        clearOffset + kPlayerFileSize <= kCareerTeamBufferSize) {
        std::memcpy(&club.careerTeam[soldOffset], &club.careerTeam[clearOffset], kPlayerFileSize);
        if (soldOrd < kPlayerWagesCount && lastOrdinal < kPlayerWagesCount) {
            club.playerWages[soldOrd] = club.playerWages[lastOrdinal];
            club.playerTrialWeeks[soldOrd] = club.playerTrialWeeks[lastOrdinal];
            club.dsegD8B8C[soldOrd] = club.dsegD8B8C[lastOrdinal];
        }
        result.newOrd = static_cast<int>(lastOrdinal);
    }

    if (clearOffset + kPlayerFileSize <= kCareerTeamBufferSize) {
        std::memset(&club.careerTeam[clearOffset], 0, kPlayerFileSize);
    }
    size_t clearedOrd = denseCompact ? lastOrdinal : static_cast<size_t>(soldOrd);
    if (clearedOrd < kPlayerWagesCount) {
        club.playerWages[clearedOrd] = 0;
        club.playerTrialWeeks[clearedOrd] = 0;
        club.dsegD8B8C[clearedOrd] = 0;
    }

    // Update transfer-market listing ordinals
    {
        uint16_t listedWords[5] = {};
        int kept = 0;
        int numListed = std::min<int>(club.numPlayersOnTransferMarket, 5);
        const auto *listedPtr = reinterpret_cast<const uint16_t *>(club.playersOnTransferMarket);
        for (int k = 0; k < numListed; ++k) {
            uint16_t ord = listedPtr[k];
            if (ord == static_cast<uint16_t>(soldOrd))
                continue;
            if (denseCompact && ord == static_cast<uint16_t>(lastOrdinal))
                ord = static_cast<uint16_t>(soldOrd);
            listedWords[kept++] = ord;
        }
        std::memset(club.playersOnTransferMarket, 0, sizeof(club.playersOnTransferMarket));
        std::memcpy(club.playersOnTransferMarket, listedWords, kept * sizeof(uint16_t));
        club.numPlayersOnTransferMarket = static_cast<uint16_t>(kept);
    }

    if (hasInitializedMatchPlayers(club.currentMatchPlayers, count)) {
        uint16_t limit = std::min<uint16_t>(count, static_cast<uint16_t>(kCurrentMatchPlayersSize));
        int removePos = -1;
        for (uint16_t k = 0; k < limit; ++k) {
            if (club.currentMatchPlayers[k] == static_cast<uint8_t>(soldOrd)) {
                removePos = static_cast<int>(k);
            } else if (denseCompact && club.currentMatchPlayers[k] == static_cast<uint8_t>(lastOrdinal)) {
                club.currentMatchPlayers[k] = static_cast<uint8_t>(soldOrd);
            }
        }
        if (removePos >= 0) {
            if (removePos < 16 && limit > 16) {
                int repPos = 16;
                for (uint16_t k = 16; k < limit; ++k) {
                    if (denseCompact && club.currentMatchPlayers[k] == static_cast<uint8_t>(soldOrd)) {
                        repPos = static_cast<int>(k);
                        break;
                    }
                }
                club.currentMatchPlayers[removePos] = club.currentMatchPlayers[repPos];
                removePos = repPos;
            }
            for (int k = removePos; k + 1 < limit; ++k) {
                club.currentMatchPlayers[k] = club.currentMatchPlayers[k + 1];
            }
            club.currentMatchPlayers[limit - 1] = 0;
        }
    }

    if (denseCompact) {
        for (int& pendingOrd : pendingRemovals) {
            if (pendingOrd == static_cast<int>(lastOrdinal))
                pendingOrd = soldOrd;
        }
    }

    club.careerNumPlayers = count - 1;
    return result;
}

void applyClubTransferChanges(HotseatPlayerContext& club,
                              std::vector<int>& outOrdinals,
                              const std::vector<CopiedTransferPlayer>& inPlayers,
                              std::vector<PendingOfferRemap>& remapsOut)
{
    size_t outIdx = 0;
    // Remove excess players down to 16 first so adding never temporarily exceeds 30
    while (outIdx < outOrdinals.size() && club.careerNumPlayers > kMinCareerSquadPlayers) {
        int ord = outOrdinals[outIdx++];
        remapsOut.push_back(removeOrdinalFromClub(club, ord, outOrdinals));
    }

    for (const auto& incoming : inPlayers) {
        appendPlayerToClub(club, incoming);
    }

    while (outIdx < outOrdinals.size() && club.careerNumPlayers > 0) {
        int ord = outOrdinals[outIdx++];
        remapsOut.push_back(removeOrdinalFromClub(club, ord, outOrdinals));
    }

    syncTrainingMatchSavedPlayers(club.trainingMatchSavedPlayers, club.dsegD8B8C, club.careerTeam,
                                  club.careerNumPlayers, club.currentMatchPlayers);
}

void updatePendingOffersForClubChange(std::vector<HotseatCareerManager::TransferOffer>& offers,
                                      int acceptedOfferId,
                                      int changedPlayerIdx,
                                      const std::vector<PendingOfferRemap>& remaps)
{
    auto updateField = [&](int& fieldOrd, bool& cancel) {
        if (fieldOrd < 0)
            return;
        for (const auto& remap : remaps) {
            if (!remap.removed)
                continue;
            if (fieldOrd == remap.oldOrd) {
                cancel = true;
                return;
            }
            if (remap.newOrd >= 0 && fieldOrd == remap.newOrd) {
                fieldOrd = remap.oldOrd;
            }
        }
    };

    for (auto& other : offers) {
        if (other.id == acceptedOfferId || !other.pending)
            continue;
        int ob = -1, os = -1;
        getBuyerAndSellerIndices(other, ob, os);
        bool cancel = false;
        if (os == changedPlayerIdx) {
            updateField(other.playerOrdinal, cancel);
            updateField(other.extraWantedOrdinal, cancel);
        }
        if (!cancel && ob == changedPlayerIdx) {
            updateField(other.exchangeOrdinal1, cancel);
            updateField(other.exchangeOrdinal2, cancel);
        }
        if (cancel) {
            other.pending = false;
            other.accepted = false;
            other.rejected = true;
        }
    }
}

void formatExchangeSummary(const HotseatCareerManager::TransferOffer& offer,
                           const char *amtStr, char *out, size_t outSize)
{
    bool hasEx1 = offer.exchangeOrdinal1 >= 0 && offer.exchangeName1[0] != '\0';
    bool hasEx2 = offer.exchangeOrdinal2 >= 0 && offer.exchangeName2[0] != '\0';
    if (hasEx1 && hasEx2) {
        snprintf(out, outSize, "%s + %s + %s", amtStr, offer.exchangeName1, offer.exchangeName2);
    } else if (hasEx1) {
        snprintf(out, outSize, "%s + %s", amtStr, offer.exchangeName1);
    } else if (hasEx2) {
        snprintf(out, outSize, "%s + %s", amtStr, offer.exchangeName2);
    } else {
        snprintf(out, outSize, "%s", amtStr);
    }
}

} // namespace

bool HotseatCareerManager::canAcceptTransferOffer(int offerId, std::string *reasonOut /* = nullptr */) const
{
    const auto offerPtr = getTransferOfferById(offerId);
    if (!offerPtr || !offerPtr->pending) {
        if (reasonOut) *reasonOut = "OFFER NO LONGER PENDING.";
        return false;
    }

    int buyerIdx = -1, sellerIdx = -1;
    getBuyerAndSellerIndices(*offerPtr, buyerIdx, sellerIdx);

    if (buyerIdx < 0 || buyerIdx >= m_numPlayers || sellerIdx < 0 || sellerIdx >= m_numPlayers || buyerIdx == sellerIdx) {
        if (reasonOut) *reasonOut = "INVALID CLUB PARTICIPANTS.";
        return false;
    }

    const auto& buyer = m_players[buyerIdx];
    const auto& seller = m_players[sellerIdx];

    if (buyer.newBalance < static_cast<int32_t>(offerPtr->offerAmount)) {
        if (reasonOut) *reasonOut = "BUYER LACKS SUFFICIENT FUNDS.";
        return false;
    }

    std::vector<int> wantedOrds, exchangeOrds;
    collectUniqueValidOrdinals(offerPtr->playerOrdinal, offerPtr->extraWantedOrdinal, wantedOrds);
    collectUniqueValidOrdinals(offerPtr->exchangeOrdinal1, offerPtr->exchangeOrdinal2, exchangeOrds);

    if (wantedOrds.empty()) {
        if (reasonOut) *reasonOut = "PLAYER NO LONGER IN SELLER SQUAD.";
        return false;
    }
    for (int ord : wantedOrds) {
        if (!isOrdinalActiveInClub(seller, ord)) {
            if (reasonOut) *reasonOut = "PLAYER NO LONGER IN SELLER SQUAD.";
            return false;
        }
    }
    for (int ord : exchangeOrds) {
        if (!isOrdinalActiveInClub(buyer, ord)) {
            if (reasonOut) *reasonOut = "EXCHANGE PLAYER NO LONGER IN BUYER SQUAD.";
            return false;
        }
    }

    int buyerAfter = static_cast<int>(buyer.careerNumPlayers) + static_cast<int>(wantedOrds.size()) - static_cast<int>(exchangeOrds.size());
    int sellerAfter = static_cast<int>(seller.careerNumPlayers) - static_cast<int>(wantedOrds.size()) + static_cast<int>(exchangeOrds.size());

    if (buyerAfter > kMaxCareerSquadPlayers) {
        if (reasonOut) *reasonOut = "BUYER SQUAD IS FULL (MAX 30).";
        return false;
    }
    if (buyerAfter < kMinCareerSquadPlayers) {
        if (reasonOut) *reasonOut = "BUYER MUST KEEP AT LEAST 16 PLAYERS.";
        return false;
    }
    if (sellerAfter < kMinCareerSquadPlayers) {
        if (reasonOut) *reasonOut = "SELLER MUST KEEP AT LEAST 16 PLAYERS.";
        return false;
    }
    if (sellerAfter > kMaxCareerSquadPlayers) {
        if (reasonOut) *reasonOut = "SELLER SQUAD WOULD EXCEED 30 PLAYERS.";
        return false;
    }
    return true;
}

bool HotseatCareerManager::resolveTransferOffer(int offerId, bool accepted)
{
    auto offerPtr = getTransferOfferById(offerId);
    if (!offerPtr || !offerPtr->pending)
        return false;

    if (!accepted) {
        offerPtr->pending = false;
        offerPtr->accepted = false;
        offerPtr->rejected = true;
        return true;
    }

    if (!canAcceptTransferOffer(offerId)) {
        offerPtr->pending = false;
        offerPtr->accepted = false;
        offerPtr->rejected = true;
        return false;
    }

    int buyerIdx = -1, sellerIdx = -1;
    getBuyerAndSellerIndices(*offerPtr, buyerIdx, sellerIdx);

    auto& buyer = m_players[buyerIdx];
    auto& seller = m_players[sellerIdx];

    offerPtr->pending = false;
    offerPtr->accepted = true;
    offerPtr->rejected = false;

    buyer.newBalance -= static_cast<int32_t>(offerPtr->offerAmount);
    buyer.playerPurchases += static_cast<int32_t>(offerPtr->offerAmount);
    seller.newBalance += static_cast<int32_t>(offerPtr->offerAmount);
    seller.playerSales += static_cast<int32_t>(offerPtr->offerAmount);

    std::vector<int> wantedOrds, exchangeOrds;
    collectUniqueValidOrdinals(offerPtr->playerOrdinal, offerPtr->extraWantedOrdinal, wantedOrds);
    collectUniqueValidOrdinals(offerPtr->exchangeOrdinal1, offerPtr->exchangeOrdinal2, exchangeOrds);

    auto snapshotPlayers = [](const HotseatPlayerContext& src, const std::vector<int>& ords) {
        std::vector<CopiedTransferPlayer> out;
        out.reserve(ords.size());
        for (int ord : ords) {
            CopiedTransferPlayer c;
            size_t ofs = kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize;
            if (ofs + kPlayerFileSize <= kCareerTeamBufferSize) {
                std::memcpy(&c.player, &src.careerTeam[ofs], kPlayerFileSize);
            }
            c.wage = (ord >= 0 && ord < kPlayerWagesCount) ? src.playerWages[ord] : 0;
            out.push_back(c);
        }
        return out;
    };

    std::vector<CopiedTransferPlayer> wantedCopies = snapshotPlayers(seller, wantedOrds);
    std::vector<CopiedTransferPlayer> exchangeCopies = snapshotPlayers(buyer, exchangeOrds);

    std::vector<PendingOfferRemap> sellerRemaps, buyerRemaps;
    applyClubTransferChanges(seller, wantedOrds, exchangeCopies, sellerRemaps);
    applyClubTransferChanges(buyer, exchangeOrds, wantedCopies, buyerRemaps);

    updatePendingOffersForClubChange(m_transferOffers, offerId, sellerIdx, sellerRemaps);
    updatePendingOffersForClubChange(m_transferOffers, offerId, buyerIdx, buyerRemaps);

    if (m_active && m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers &&
        (buyerIdx == m_activePlayerIndex || sellerIdx == m_activePlayerIndex)) {
        loadSwosContext(m_players[m_activePlayerIndex], false);
    }
    if (swos.selTeamsPtr && swos.g_numSelectedTeams > 0) {
        syncPlayersWithSelectedTeams(true);
    }
    syncTransferMarketPoolsForCurrentPlayer();
    return true;
}

void HotseatCareerManager::counterTransferOffer(int offerId, uint32_t counterAmount)
{
    auto offerPtr = getTransferOfferById(offerId);
    if (!offerPtr || !offerPtr->pending)
        return;

    std::swap(offerPtr->fromPlayerIndex, offerPtr->toPlayerIndex);
    offerPtr->offerAmount = counterAmount;
    offerPtr->isCounterOffer = true;
}

bool HotseatCareerManager::processPendingOffersForActivePlayer()
{
    if (!m_active)
        return false;

    auto pending = getPendingOffersForPlayer(m_activePlayerIndex);
    if (pending.empty())
        return false;

    saveCurrentSwosContext(m_players[m_activePlayerIndex]);

    for (const auto& offer : pending) {
        char line1[64], line2[96], line3[96], line4[64];
        char amtStr[32], termStr[96];
        formatCurrencyAmount(amtStr, sizeof(amtStr), offer.offerAmount);
        formatExchangeSummary(offer, amtStr, termStr, sizeof(termStr));

        char targetStr[80];
        if (offer.extraWantedOrdinal >= 0 && offer.extraWantedName[0] != '\0') {
            snprintf(targetStr, sizeof(targetStr), "%s + %s", offer.playerName, offer.extraWantedName);
        } else {
            snprintf(targetStr, sizeof(targetStr), "%s", offer.playerName);
        }

        if (!offer.isCounterOffer) {
            snprintf(line1, sizeof(line1), "PLAYER %d (%s) BIDS",
                     offer.fromPlayerIndex + 1, m_players[offer.fromPlayerIndex].teamName.c_str());
            snprintf(line2, sizeof(line2), "FOR: %s", targetStr);
            snprintf(line3, sizeof(line3), "OFFER: %s", termStr);
            snprintf(line4, sizeof(line4), "ACCEPT THIS OFFER?");

            std::vector<const char*> lines = { line1, line2, line3, line4 };
            bool accept = showContinueAbortPrompt("TRANSFER OFFER", "ACCEPT", "DECLINE / COUNTER", lines, false);
            if (accept) {
                std::string reason;
                if (resolveTransferOffer(offer.id, true)) {
                    showContinueAbortPrompt("DEAL AGREED", "CONTINUE", nullptr,
                        { "OFFER ACCEPTED!", "FUNDS AND SQUAD UPDATED." }, false);
                } else {
                    canAcceptTransferOffer(offer.id, &reason);
                    if (reason.empty()) reason = "CANNOT COMPLETE TRANSFER.";
                    showContinueAbortPrompt("TRANSFER FAILED", "CONTINUE", nullptr, { reason.c_str() }, false);
                }
            } else {
                snprintf(line1, sizeof(line1), "BID: %s FOR %s", termStr, targetStr);
                snprintf(line2, sizeof(line2), "DO YOU WANT TO COUNTER");
                snprintf(line3, sizeof(line3), "OR REJECT THE BID?");
                std::vector<const char*> optLines = { line1, line2, line3 };
                bool counter = showContinueAbortPrompt("COUNTER OR REJECT", "COUNTER", "REJECT", optLines, false);
                if (counter) {
                    uint32_t c25 = offer.offerAmount + (offer.offerAmount / 4);
                    uint32_t c50 = offer.offerAmount + (offer.offerAmount / 2);
                    char c25Str[32], c50Str[32];
                    snprintf(c25Str, sizeof(c25Str), "1. +25%% (approx \x9c%uK)", c25 / 1000);
                    snprintf(c50Str, sizeof(c50Str), "2. +50%% (approx \x9c%uK)", c50 / 1000);
                    std::vector<const char*> cLines = { "SELECT COUNTER-OFFER:", c25Str, c50Str };
                    bool pick50 = showContinueAbortPrompt("COUNTER AMOUNT", "+50%", "+25%", cLines, false);
                    uint32_t chosenAmount = pick50 ? c50 : c25;
                    counterTransferOffer(offer.id, chosenAmount);
                    showContinueAbortPrompt("COUNTER SENT", "CONTINUE", nullptr,
                        { "COUNTER-OFFER TRANSMITTED!", "AWAITING BUYER TURN." }, false);
                } else {
                    resolveTransferOffer(offer.id, false);
                    showContinueAbortPrompt("OFFER REJECTED", "CONTINUE", nullptr,
                        { "OFFER HAS BEEN REJECTED." }, false);
                }
            }
        } else {
            snprintf(line1, sizeof(line1), "PLAYER %d (%s) COUNTERED",
                     offer.fromPlayerIndex + 1, m_players[offer.fromPlayerIndex].teamName.c_str());
            snprintf(line2, sizeof(line2), "FOR: %s", targetStr);
            snprintf(line3, sizeof(line3), "NEW TERMS: %s", termStr);
            snprintf(line4, sizeof(line4), "ACCEPT COUNTER-OFFER?");

            std::vector<const char*> lines = { line1, line2, line3, line4 };
            bool accept = showContinueAbortPrompt("COUNTER-OFFER", "ACCEPT", "REJECT", lines, false);
            if (accept) {
                std::string reason;
                if (resolveTransferOffer(offer.id, true)) {
                    showContinueAbortPrompt("DEAL AGREED", "CONTINUE", nullptr,
                        { "COUNTER-OFFER ACCEPTED!", "FUNDS AND SQUAD UPDATED." }, false);
                } else {
                    canAcceptTransferOffer(offer.id, &reason);
                    if (reason.empty()) reason = "CANNOT COMPLETE TRANSFER.";
                    showContinueAbortPrompt("TRANSFER FAILED", "CONTINUE", nullptr, { reason.c_str() }, false);
                }
            } else {
                resolveTransferOffer(offer.id, false);
                showContinueAbortPrompt("OFFER REJECTED", "CONTINUE", nullptr,
                    { "COUNTER-OFFER REJECTED." }, false);
            }
        }
    }
    return true;
}

#pragma pack(push, 1)
struct HotseatFileHeader {
    char magic[16]; // "SWOS_HOTSEAT_V3"
    int32_t numPlayers;
    int32_t activePlayerIndex;
    int32_t nextOfferId;
    int32_t numOffers;
    int32_t roundCalendarIndex;
};
#pragma pack(pop)

static bool writePlayerContext(std::ofstream& out, const HotseatPlayerContext& p)
{
    bool ok = writeBinary(out, p.teamId) &&
              writeString(out, p.teamName, 127) &&
              writeString(out, p.managerName, 63) &&
              writeString(out, p.managerSurname, 63) &&
              writeBinary(out, p.coachOrPlayer) &&
              writeBinary(out, p.careerTeamLeague) &&
              writeBinary(out, p.nationality) &&
              writeBinary(out, p.mrOrMs) &&
              writeBinary(out, p.newBalance) &&
              writeBinary(out, p.oldBalance) &&
              writeBinary(out, p.startingBalance) &&
              writeBinary(out, p.gateReceipts) &&
              writeBinary(out, p.playerWagesBill) &&
              writeBinary(out, p.generalRunningCosts) &&
              writeBinary(out, p.playerSales) &&
              writeBinary(out, p.playerPurchases) &&
              writeBinary(out, p.competitionBonuses) &&
              writeBinary(out, p.sensisoftPlcSponsorship) &&
              writeBinary(out, p.stayingInOldClub) &&
              writeBinary(out, p.dsegD6B54) &&
              writeBinary(out, p.dsegD6B56) &&
              writeBinary(out, p.dsegD6B5A) &&
              writeBinary(out, p.dsegD6B5C) &&
              writeBinary(out, p.dsegD6B88) &&
              writeBinary(out, p.dsegD6B9A) &&
              writeBinary(out, p.dsegD6B9E) &&
              writeBinary(out, p.youreSacked) &&
              writeBinary(out, p.retired) &&
              writeBinary(out, p.clubBusinessActive) &&
              writeBinary(out, p.numNewJobOffers) &&
              writeBinary(out, p.numJobOffers) &&
              writeBinary(out, p.isNationalSelector) &&
              writeBinary(out, p.numOffers) &&
              writeBinary(out, p.timeToNegotiate) &&
              writeBinary(out, p.dsegD6D0A) &&
              writeBinary(out, p.dsegD6D0E) &&
              writeBinary(out, p.dsegD6D10) &&
              writeBinary(out, p.dsegD6D12) &&
              writeBinary(out, p.gamesPlayed) &&
              writeBinary(out, p.numWins) &&
              writeBinary(out, p.numDrawn) &&
              writeBinary(out, p.numLost) &&
              writeBinary(out, p.goalsScored) &&
              writeBinary(out, p.goalsTaken) &&
              writeBinary(out, p.dsegD6CA0) &&
              writeBinary(out, p.dsegD6CA2) &&
              writeBinary(out, p.dsegD7E66) &&
              writeBinary(out, p.currentSeasonShowing) &&
              writeBinary(out, p.currentSeasonNumber) &&
              writeBinary(out, p.seasonPlaying) &&
              writeBinary(out, p.dsegE0048) &&
              writeBinary(out, p.playMatchBlink) &&
              writeBinary(out, p.careerNumPlayers);
    if (!ok)
        return false;

    out.write(reinterpret_cast<const char *>(p.playerWages), sizeof(p.playerWages));
    out.write(reinterpret_cast<const char *>(p.playerTrialWeeks), sizeof(p.playerTrialWeeks));
    out.write(reinterpret_cast<const char *>(p.seasonStatsBlock), sizeof(p.seasonStatsBlock));
    out.write(reinterpret_cast<const char *>(p.seasonFinancesBlock), kSeasonFinancesBlockSize);
    out.write(reinterpret_cast<const char *>(p.currentMatchPlayers), kCurrentMatchPlayersSize);
    out.write(reinterpret_cast<const char *>(p.careerTeam), kCareerTeamBufferSize);
    out.write(reinterpret_cast<const char *>(p.careerHistory), kCareerHistoryBufferSize);
    out.write(reinterpret_cast<const char *>(p.userTactics), 6 * kUserTacticsSize);

    uint8_t matchPlayed = p.matchPlayedThisTurn ? 1 : 0;
    uint8_t turnComp = p.turnCompleted ? 1 : 0;
    if (!writeBinary(out, matchPlayed) || !writeBinary(out, turnComp))
        return false;

    // V4 extended per-club fields
    ok = writeBinary(out, p.dsegD6B62) &&
         writeBinary(out, p.dsegD6B64) &&
         writeBinary(out, p.dsegD6B94) &&
         writeBinary(out, p.dsegD6BA2) &&
         writeBinary(out, p.dsegD6C46) &&
         writeBinary(out, p.additionalInvestments) &&
         writeBinary(out, p.dsegD6C4E) &&
         writeBinary(out, p.dsegD6C50) &&
         writeBinary(out, p.teamNoOfferAccepted) &&
         writeBinary(out, p.teamFundsAccepted) &&
         writeBinary(out, p.dsegD6CDC) &&
         writeBinary(out, p.dsegD6D0C) &&
         writeBinary(out, p.numPlayersOnTransferMarket) &&
         writeBinary(out, p.euroCupIndex);
    if (!ok)
        return false;

    out.write(reinterpret_cast<const char *>(p.playersOnTransferMarket), kListedPlayersSize);
    out.write(reinterpret_cast<const char *>(p.dsegD6F26Block), kListedPlayersSize);
    out.write(reinterpret_cast<const char *>(p.dsegD6DBA), kTransferAiOffersSize);
    out.write(reinterpret_cast<const char *>(p.newJobOffers), kJobOffersBufferSize);
    out.write(reinterpret_cast<const char *>(p.jobOffers), kJobOffersBufferSize);
    return out.good();
}

static bool readPlayerContext(std::ifstream& in, HotseatPlayerContext& p, int fileVersion)
{
    bool ok = readBinary(in, p.teamId) &&
              readString(in, p.teamName, 127) &&
              readString(in, p.managerName, 63) &&
              readString(in, p.managerSurname, 63) &&
              readBinary(in, p.coachOrPlayer) &&
              readBinary(in, p.careerTeamLeague) &&
              readBinary(in, p.nationality) &&
              readBinary(in, p.mrOrMs) &&
              readBinary(in, p.newBalance) &&
              readBinary(in, p.oldBalance) &&
              readBinary(in, p.startingBalance) &&
              readBinary(in, p.gateReceipts) &&
              readBinary(in, p.playerWagesBill) &&
              readBinary(in, p.generalRunningCosts) &&
              readBinary(in, p.playerSales) &&
              readBinary(in, p.playerPurchases) &&
              readBinary(in, p.competitionBonuses) &&
              readBinary(in, p.sensisoftPlcSponsorship) &&
              readBinary(in, p.stayingInOldClub) &&
              readBinary(in, p.dsegD6B54) &&
              readBinary(in, p.dsegD6B56) &&
              readBinary(in, p.dsegD6B5A) &&
              readBinary(in, p.dsegD6B5C) &&
              readBinary(in, p.dsegD6B88) &&
              readBinary(in, p.dsegD6B9A) &&
              readBinary(in, p.dsegD6B9E) &&
              readBinary(in, p.youreSacked) &&
              readBinary(in, p.retired) &&
              readBinary(in, p.clubBusinessActive) &&
              readBinary(in, p.numNewJobOffers) &&
              readBinary(in, p.numJobOffers) &&
              readBinary(in, p.isNationalSelector) &&
              readBinary(in, p.numOffers) &&
              readBinary(in, p.timeToNegotiate) &&
              readBinary(in, p.dsegD6D0A) &&
              readBinary(in, p.dsegD6D0E) &&
              readBinary(in, p.dsegD6D10) &&
              readBinary(in, p.dsegD6D12) &&
              readBinary(in, p.gamesPlayed) &&
              readBinary(in, p.numWins) &&
              readBinary(in, p.numDrawn) &&
              readBinary(in, p.numLost) &&
              readBinary(in, p.goalsScored) &&
              readBinary(in, p.goalsTaken) &&
              readBinary(in, p.dsegD6CA0) &&
              readBinary(in, p.dsegD6CA2) &&
              readBinary(in, p.dsegD7E66) &&
              readBinary(in, p.currentSeasonShowing) &&
              readBinary(in, p.currentSeasonNumber) &&
              readBinary(in, p.seasonPlaying) &&
              readBinary(in, p.dsegE0048) &&
              readBinary(in, p.playMatchBlink) &&
              readBinary(in, p.careerNumPlayers);
    if (!ok)
        return false;

    in.read(reinterpret_cast<char *>(p.playerWages), sizeof(p.playerWages));
    if (fileVersion >= 5) {
        in.read(reinterpret_cast<char *>(p.playerTrialWeeks), sizeof(p.playerTrialWeeks));
    } else {
        std::memset(p.playerTrialWeeks, 0, sizeof(p.playerTrialWeeks));
    }
    in.read(reinterpret_cast<char *>(p.seasonStatsBlock), sizeof(p.seasonStatsBlock));
    in.read(reinterpret_cast<char *>(p.seasonFinancesBlock), kSeasonFinancesBlockSize);
    in.read(reinterpret_cast<char *>(p.currentMatchPlayers), kCurrentMatchPlayersSize);
    in.read(reinterpret_cast<char *>(p.careerTeam), kCareerTeamBufferSize);
    in.read(reinterpret_cast<char *>(p.careerHistory), kCareerHistoryBufferSize);
    in.read(reinterpret_cast<char *>(p.userTactics), 6 * kUserTacticsSize);

    uint8_t matchPlayed = 0, turnComp = 0;
    if (!readBinary(in, matchPlayed) || !readBinary(in, turnComp) || !in.good())
        return false;
    p.matchPlayedThisTurn = (matchPlayed != 0);
    p.turnCompleted = (turnComp != 0);

    if (fileVersion >= 4) {
        ok = readBinary(in, p.dsegD6B62) &&
             readBinary(in, p.dsegD6B64) &&
             readBinary(in, p.dsegD6B94) &&
             readBinary(in, p.dsegD6BA2) &&
             readBinary(in, p.dsegD6C46) &&
             readBinary(in, p.additionalInvestments) &&
             readBinary(in, p.dsegD6C4E) &&
             readBinary(in, p.dsegD6C50) &&
             readBinary(in, p.teamNoOfferAccepted) &&
             readBinary(in, p.teamFundsAccepted) &&
             readBinary(in, p.dsegD6CDC) &&
             readBinary(in, p.dsegD6D0C) &&
             readBinary(in, p.numPlayersOnTransferMarket) &&
             readBinary(in, p.euroCupIndex);
        if (!ok)
            return false;

        in.read(reinterpret_cast<char *>(p.playersOnTransferMarket), kListedPlayersSize);
        in.read(reinterpret_cast<char *>(p.dsegD6F26Block), kListedPlayersSize);
        in.read(reinterpret_cast<char *>(p.dsegD6DBA), kTransferAiOffersSize);
        in.read(reinterpret_cast<char *>(p.newJobOffers), kJobOffersBufferSize);
        in.read(reinterpret_cast<char *>(p.jobOffers), kJobOffersBufferSize);
        if (!in.good())
            return false;
    }

    syncTrainingMatchSavedPlayers(p.trainingMatchSavedPlayers, p.dsegD8B8C, p.careerTeam,
                                  p.careerNumPlayers, p.currentMatchPlayers);
    return true;
}

static bool writePlayerV6Extension(std::ofstream& out, const HotseatPlayerContext& p)
{
    bool ok = writeBinary(out, p.dsegD761A) &&
              writeBinary(out, p.dsegD761C) &&
              writeBinary(out, p.dsegD8B6A) &&
              writeBinary(out, p.dsegD8BAC) &&
              writeBinary(out, p.dsegD8BAE) &&
              writeBinary(out, p.dsegD8BB0) &&
              writeBinary(out, p.currentTactics) &&
              writeBinary(out, p.dsegD8BF3);
    if (!ok)
        return false;

    out.write(reinterpret_cast<const char *>(p.trainingMatchSavedPlayers), kCurrentMatchPlayersSize);
    out.write(reinterpret_cast<const char *>(p.dsegD8B8C), kCurrentMatchPlayersSize);
    out.write(reinterpret_cast<const char *>(p.lastMatchPlayers), kCurrentMatchPlayersSize);
    out.write(reinterpret_cast<const char *>(p.dsegD8BD2), kCurrentMatchPlayersSize);
    return out.good();
}

static bool readPlayerV6Extension(std::ifstream& in, HotseatPlayerContext& p)
{
    bool ok = readBinary(in, p.dsegD761A) &&
              readBinary(in, p.dsegD761C) &&
              readBinary(in, p.dsegD8B6A) &&
              readBinary(in, p.dsegD8BAC) &&
              readBinary(in, p.dsegD8BAE) &&
              readBinary(in, p.dsegD8BB0) &&
              readBinary(in, p.currentTactics) &&
              readBinary(in, p.dsegD8BF3);
    if (!ok)
        return false;

    in.read(reinterpret_cast<char *>(p.trainingMatchSavedPlayers), kCurrentMatchPlayersSize);
    in.read(reinterpret_cast<char *>(p.dsegD8B8C), kCurrentMatchPlayersSize);
    in.read(reinterpret_cast<char *>(p.lastMatchPlayers), kCurrentMatchPlayersSize);
    in.read(reinterpret_cast<char *>(p.dsegD8BD2), kCurrentMatchPlayersSize);
    if (!in.good())
        return false;

    syncTrainingMatchSavedPlayers(p.trainingMatchSavedPlayers, p.dsegD8B8C, p.careerTeam,
                                  p.careerNumPlayers, p.currentMatchPlayers);
    return true;
}

static bool writeTransferOffer(std::ofstream& out, const HotseatCareerManager::TransferOffer& offer)
{
    uint8_t flags = (offer.pending ? 1 : 0) |
                    (offer.accepted ? 2 : 0) |
                    (offer.rejected ? 4 : 0) |
                    (offer.isCounterOffer ? 8 : 0);
    return writeBinary(out, offer.id) &&
           writeBinary(out, offer.fromPlayerIndex) &&
           writeBinary(out, offer.toPlayerIndex) &&
           writeBinary(out, offer.playerOrdinal) &&
           static_cast<bool>(out.write(offer.playerName, sizeof(offer.playerName))) &&
           writeBinary(out, offer.offerAmount) &&
           writeBinary(out, flags) &&
           out.good();
}

static bool readTransferOffer(std::ifstream& in, HotseatCareerManager::TransferOffer& offer)
{
    uint8_t flags = 0;
    bool ok = readBinary(in, offer.id) &&
              readBinary(in, offer.fromPlayerIndex) &&
              readBinary(in, offer.toPlayerIndex) &&
              readBinary(in, offer.playerOrdinal) &&
              static_cast<bool>(in.read(offer.playerName, sizeof(offer.playerName))) &&
              readBinary(in, offer.offerAmount) &&
              readBinary(in, flags) &&
              in.good();
    if (!ok)
        return false;
    offer.playerName[sizeof(offer.playerName) - 1] = '\0';
    offer.extraWantedOrdinal = -1;
    std::memset(offer.extraWantedName, 0, sizeof(offer.extraWantedName));
    offer.exchangeOrdinal1 = -1;
    std::memset(offer.exchangeName1, 0, sizeof(offer.exchangeName1));
    offer.exchangeOrdinal2 = -1;
    std::memset(offer.exchangeName2, 0, sizeof(offer.exchangeName2));
    offer.pending = (flags & 1) != 0;
    offer.accepted = (flags & 2) != 0;
    offer.rejected = (flags & 4) != 0;
    offer.isCounterOffer = (flags & 8) != 0;
    return true;
}

static bool writeTransferOfferV6Extension(std::ofstream& out, const HotseatCareerManager::TransferOffer& offer)
{
    return writeBinary(out, offer.extraWantedOrdinal) &&
           static_cast<bool>(out.write(offer.extraWantedName, sizeof(offer.extraWantedName))) &&
           writeBinary(out, offer.exchangeOrdinal1) &&
           static_cast<bool>(out.write(offer.exchangeName1, sizeof(offer.exchangeName1))) &&
           writeBinary(out, offer.exchangeOrdinal2) &&
           static_cast<bool>(out.write(offer.exchangeName2, sizeof(offer.exchangeName2))) &&
           out.good();
}

static bool readTransferOfferV6Extension(std::ifstream& in, HotseatCareerManager::TransferOffer& offer)
{
    bool ok = readBinary(in, offer.extraWantedOrdinal) &&
              static_cast<bool>(in.read(offer.extraWantedName, sizeof(offer.extraWantedName))) &&
              readBinary(in, offer.exchangeOrdinal1) &&
              static_cast<bool>(in.read(offer.exchangeName1, sizeof(offer.exchangeName1))) &&
              readBinary(in, offer.exchangeOrdinal2) &&
              static_cast<bool>(in.read(offer.exchangeName2, sizeof(offer.exchangeName2))) &&
              in.good();
    if (!ok)
        return false;
    offer.extraWantedName[sizeof(offer.extraWantedName) - 1] = '\0';
    offer.exchangeName1[sizeof(offer.exchangeName1) - 1] = '\0';
    offer.exchangeName2[sizeof(offer.exchangeName2) - 1] = '\0';
    return true;
}

bool HotseatCareerManager::saveToFile(const std::string& path)
{
    if (!m_active || m_numPlayers < 1)
        return false;

    saveCurrentEuroCup();
    saveCurrentSwosContext(m_players[m_activePlayerIndex]);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    HotseatFileHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::memcpy(hdr.magic, kHotseatFileMagicV6, 15);
    hdr.numPlayers = m_numPlayers;
    hdr.activePlayerIndex = m_activePlayerIndex;
    hdr.nextOfferId = m_nextOfferId;
    hdr.numOffers = static_cast<int32_t>(std::min<size_t>(m_transferOffers.size(), kMaxSerializedOffers));
    hdr.roundCalendarIndex = m_roundCalendarIndex;

    if (!writeBinary(out, hdr))
        return false;

    for (int i = 0; i < m_numPlayers; ++i) {
        if (!writePlayerContext(out, m_players[i]))
            return false;
    }

    for (int32_t i = 0; i < hdr.numOffers; ++i) {
        if (!writeTransferOffer(out, m_transferOffers[i]))
            return false;
    }

    int32_t numCalEntries = static_cast<int32_t>(m_calendarSlots.size());
    if (!writeBinary(out, numCalEntries))
        return false;
    for (const auto& kv : m_calendarSlots) {
        uint16_t calOfs = kv.first;
        uint16_t count = static_cast<uint16_t>(kv.second.size());
        if (!writeBinary(out, calOfs) || !writeBinary(out, count))
            return false;
        for (uint16_t s = 0; s < count; ++s) {
            out.write(reinterpret_cast<const char *>(kv.second[s].data()), 12);
        }
    }

    if (!writeBinary(out, m_activeEuroCupIndex))
        return false;
    if (!out.write(reinterpret_cast<const char *>(m_euroCupScheduleTemplate), sizeof(m_euroCupScheduleTemplate)))
        return false;
    for (int e = 0; e < kNumEuroCups; ++e) {
        uint8_t active = m_euroCupActive[e] ? 1 : 0;
        if (!writeBinary(out, active) || !writeBinary(out, m_euroCupSeasonStrOfs[e]))
            return false;
        if (active) {
            out.write(reinterpret_cast<const char *>(m_euroCupBuffers[e].data()), kEuroCupBufferSize);
        }
    }
    int32_t numEuroMapEntries = static_cast<int32_t>(m_euroCupByCalOfs.size());
    if (!writeBinary(out, numEuroMapEntries))
        return false;
    for (const auto& kv : m_euroCupByCalOfs) {
        uint16_t calOfs = kv.first;
        int32_t eIdx = kv.second;
        if (!writeBinary(out, calOfs) || !writeBinary(out, eIdx))
            return false;
    }

    for (int i = 0; i < m_numPlayers; ++i) {
        if (!writePlayerV6Extension(out, m_players[i]))
            return false;
    }
    for (int32_t i = 0; i < hdr.numOffers; ++i) {
        if (!writeTransferOfferV6Extension(out, m_transferOffers[i]))
            return false;
    }

    out.flush();
    if (!out.good())
        return false;

    logInfo("HotseatCareerManager: Successfully saved hotseat state to %s", path.c_str());
    return true;
}

// Re-synchronize hotseat VM pointers and active player squads after ProcessCareerFile unpacks a .CAR buffer.
void HotseatCareerManager::onAfterProcessCareerFile()
{
    if (!isActive() || swos.g_gameType != kGameTypeCareer)
        return;

    stageActiveCalendarSlotToVm();
    syncPlayersWithSelectedTeams(true);
    relinkCompetitionTeamPointers();
    syncTransferMarketPoolsForCurrentPlayer();
}

bool HotseatCareerManager::loadFromFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        reset();
        return false;
    }

    HotseatFileHeader hdr;
    if (!readBinary(in, hdr)) {
        reset();
        return false;
    }

    int fileVersion = 0;
    if (std::memcmp(hdr.magic, kHotseatFileMagicV6, 15) == 0) {
        fileVersion = 6;
    } else if (std::memcmp(hdr.magic, kHotseatFileMagicV5, 15) == 0) {
        fileVersion = 5;
    } else if (std::memcmp(hdr.magic, kHotseatFileMagicV4, 15) == 0) {
        fileVersion = 4;
    } else if (std::memcmp(hdr.magic, kHotseatFileMagicV3, 15) == 0) {
        fileVersion = 3;
    } else {
        reset();
        logWarn("HotseatCareerManager: Invalid or unsupported magic in %s", path.c_str());
        return false;
    }

    if (hdr.numPlayers < 1 || hdr.numPlayers > kMaxHotseatPlayers ||
        hdr.activePlayerIndex < 0 || hdr.activePlayerIndex >= hdr.numPlayers ||
        hdr.numOffers < 0 || hdr.numOffers > kMaxSerializedOffers ||
        hdr.roundCalendarIndex < 0) {
        reset();
        logWarn("HotseatCareerManager: Invalid header values in %s", path.c_str());
        return false;
    }

    reset();
    m_numPlayers = hdr.numPlayers;
    m_activePlayerIndex = hdr.activePlayerIndex;
    m_nextOfferId = hdr.nextOfferId;
    m_roundCalendarIndex = hdr.roundCalendarIndex;
    m_active = true;

    for (int i = 0; i < m_numPlayers; ++i) {
        if (!readPlayerContext(in, m_players[i], fileVersion)) {
            reset();
            logWarn("HotseatCareerManager: Corrupted player context %d in %s", i, path.c_str());
            return false;
        }
    }

    m_transferOffers.clear();
    m_transferOffers.reserve(static_cast<size_t>(hdr.numOffers));
    for (int32_t i = 0; i < hdr.numOffers; ++i) {
        TransferOffer offer;
        if (!readTransferOffer(in, offer)) {
            reset();
            logWarn("HotseatCareerManager: Corrupted transfer offer %d in %s", i, path.c_str());
            return false;
        }
        m_transferOffers.push_back(offer);
    }

    int32_t numCalEntries = 0;
    if (!readBinary(in, numCalEntries) || numCalEntries < 0 || numCalEntries > 256) {
        reset();
        logWarn("HotseatCareerManager: Invalid calendar slots count in %s", path.c_str());
        return false;
    }
    m_calendarSlots.clear();
    for (int32_t c = 0; c < numCalEntries; ++c) {
        uint16_t calOfs = 0, count = 0;
        if (!readBinary(in, calOfs) || !readBinary(in, count) || calOfs >= 1870 || count > 32) {
            reset();
            logWarn("HotseatCareerManager: Corrupted calendar slot entry %d in %s", c, path.c_str());
            return false;
        }
        std::vector<CalendarSlotBytes> vec(count);
        for (uint16_t s = 0; s < count; ++s) {
            if (!in.read(reinterpret_cast<char *>(vec[s].data()), 12)) {
                reset();
                logWarn("HotseatCareerManager: Truncated calendar slot data in %s", path.c_str());
                return false;
            }
        }
        m_calendarSlots[calOfs] = std::move(vec);
    }

    if (fileVersion >= 4) {
        if (!readBinary(in, m_activeEuroCupIndex) ||
            !in.read(reinterpret_cast<char *>(m_euroCupScheduleTemplate), sizeof(m_euroCupScheduleTemplate))) {
            reset();
            logWarn("HotseatCareerManager: Corrupted euro cup header in %s", path.c_str());
            return false;
        }
        for (int e = 0; e < kNumEuroCups; ++e) {
            uint8_t active = 0;
            if (!readBinary(in, active) || !readBinary(in, m_euroCupSeasonStrOfs[e])) {
                reset();
                return false;
            }
            m_euroCupActive[e] = (active != 0);
            if (m_euroCupActive[e]) {
                if (!in.read(reinterpret_cast<char *>(m_euroCupBuffers[e].data()), kEuroCupBufferSize)) {
                    reset();
                    return false;
                }
            }
        }
        int32_t numEuroMapEntries = 0;
        if (!readBinary(in, numEuroMapEntries) || numEuroMapEntries < 0 || numEuroMapEntries > 256) {
            reset();
            return false;
        }
        for (int32_t i = 0; i < numEuroMapEntries; ++i) {
            uint16_t calOfs = 0;
            int32_t eIdx = -1;
            if (!readBinary(in, calOfs) || !readBinary(in, eIdx) || calOfs >= 1870 || eIdx < 0 || eIdx >= kNumEuroCups) {
                reset();
                return false;
            }
            m_euroCupByCalOfs[calOfs] = eIdx;
        }
        if (m_activeEuroCupIndex >= 0 && m_activeEuroCupIndex < kNumEuroCups && m_euroCupActive[m_activeEuroCupIndex]) {
            std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2],
                        m_euroCupBuffers[m_activeEuroCupIndex].data(),
                        kEuroCupBufferSize);
        }
    }

    if (fileVersion >= 6) {
        for (int i = 0; i < m_numPlayers; ++i) {
            if (!readPlayerV6Extension(in, m_players[i])) {
                reset();
                logWarn("HotseatCareerManager: Corrupted V6 player extension %d in %s", i, path.c_str());
                return false;
            }
        }
        for (int32_t i = 0; i < hdr.numOffers; ++i) {
            if (!readTransferOfferV6Extension(in, m_transferOffers[i])) {
                reset();
                logWarn("HotseatCareerManager: Corrupted V6 offer extension %d in %s", i, path.c_str());
                return false;
            }
        }
    }

    if (fileVersion <= 4 && m_numPlayers > 1) {
        // Repair blank-named reserve slots left by pre-V5 startCareerMode copies
        for (int i = 0; i < m_numPlayers; ++i) {
            auto& ctx = m_players[i];
            int count = std::min<int>(ctx.careerNumPlayers, kCurrentMatchPlayersSize);
            for (int slot = 0; slot < count; ++slot) {
                uint8_t ord = ctx.currentMatchPlayers[slot];
                if (ord >= 30)
                    continue;
                auto *pf = reinterpret_cast<PlayerFile *>(
                    ctx.careerTeam + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize);
                if (!isValidPlayerName(pf->playerName)) {
                    populateNamedTrialPlayerSlot(ctx.careerTeam, ctx.playerTrialWeeks, ord,
                                                 static_cast<uint16_t>(ctx.teamId), false);
                }
            }
        }
    }

    m_emergencyLoanDepth = 0;
    m_playMatchSetupStack.clear();
    for (auto& pm : m_h2hPackMaps) {
        pm = H2hPackedTeamMap{};
    }

    stageActiveCalendarSlotToVm();
    loadSwosContext(m_players[m_activePlayerIndex]);
    if (swos.selTeamsPtr && swos.g_numSelectedTeams > 0) {
        syncPlayersWithSelectedTeams(true);
    }
    syncTransferMarketPoolsForCurrentPlayer();
    logInfo("HotseatCareerManager: Successfully loaded hotseat state (%d players, active P%d) from %s",
            m_numPlayers, m_activePlayerIndex + 1, path.c_str());
    return true;
}

static int s_chosenPlayerCount = 0;
static bool s_playerCountConfirmed = false;

static void selectPlayerCount(int count)
{
    s_chosenPlayerCount = count;
    s_playerCountConfirmed = true;
    SetExitMenuFlag();
}

void onSelect1Player() { selectPlayerCount(1); }
void onSelect2Players() { selectPlayerCount(2); }
void onSelect3Players() { selectPlayerCount(3); }
void onSelect4Players() { selectPlayerCount(4); }

void onSelectExitPlayerCount()
{
    s_playerCountConfirmed = false;
    SetExitMenuFlag();
}

void HotseatCareerManager::resetWizard(int totalPlayers)
{
    m_wizardTotalPlayers = std::clamp(totalPlayers, 1, kMaxHotseatPlayers);
    m_wizardCurrentPlayer = 0;
    m_wizardAdvanced = false;
    m_isWizardActive = true;
    m_wizardPlayers.clear();
    m_wizardPlayers.resize(m_wizardTotalPlayers);
}

void HotseatCareerManager::applyWizardDefaultsToVm(int playerIndex) const
{
    extern void SetDefaultNameAndSurname();

    swos.coachOrPlayer = 1;
    swos.plNationality = kEng;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]) = 0;
    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], 0, 5);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], "MR", 2);

    if (playerIndex == 0) {
        SetDefaultNameAndSurname();
    } else {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 0, 9);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], "PLAYER", 6);

        std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 0, 13);
        std::string defSurname = std::to_string(playerIndex + 1);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], defSurname.data(),
                    std::min(defSurname.size(), size_t{12}));
    }
}

void HotseatCareerManager::assignTeamSlot(int16_t teamId, int slot, const TeamFile *tf)
{
    m_assignedTeamSlots[teamId] = slot;
    AssignedSlotInfo info;
    info.slot = slot;
    info.globalTeamNumber = teamId;
    if (tf) {
        info.teamWord = *reinterpret_cast<const uint16_t *>(tf);
        info.teamFileNo = tf->teamFileNo;
        info.league = tf->league;
        info.teamFile = *tf;
    }
    m_assignedSlotInfos[slot] = info;
}

bool HotseatCareerManager::areAllAssignedTeamsInSameLeague() const
{
    if (m_assignedSlotInfos.empty())
        return true;

    auto firstIt = m_assignedSlotInfos.begin();
    uint8_t targetCountry = firstIt->second.teamFileNo;
    uint8_t targetLeague = firstIt->second.league;

    for (const auto& pair : m_assignedSlotInfos) {
        if (pair.second.teamFileNo != targetCountry || pair.second.league != targetLeague) {
            return false;
        }
    }
    return true;
}

void HotseatCareerManager::unassignTeamSlot(int16_t teamId)
{
    auto it = m_assignedTeamSlots.find(teamId);
    if (it != m_assignedTeamSlots.end()) {
        int slot = it->second;
        m_assignedSlotInfos.erase(slot);
        m_assignedTeamSlots.erase(it);
    }
}

void HotseatCareerManager::unassignAllTeamSlots()
{
    m_assignedTeamSlots.clear();
    m_assignedSlotInfos.clear();
}

int HotseatCareerManager::getTeamSlot(int16_t teamId) const
{
    auto it = m_assignedTeamSlots.find(teamId);
    if (it != m_assignedTeamSlots.end())
        return it->second;
    return -1;
}

int HotseatCareerManager::getTeamForSlot(int slot) const
{
    for (const auto& pair : m_assignedTeamSlots) {
        if (pair.second == slot)
            return pair.first;
    }
    return 0;
}

const HotseatCareerManager::AssignedSlotInfo *HotseatCareerManager::getAssignedSlotInfo(int slot) const
{
    auto it = m_assignedSlotInfos.find(slot);
    if (it != m_assignedSlotInfos.end())
        return &it->second;
    return nullptr;
}

int HotseatCareerManager::getNextAvailableSlot() const
{
    for (int i = 0; i < m_careerNumPlayers; ++i) {
        bool used = false;
        for (const auto& pair : m_assignedTeamSlots) {
            if (pair.second == i) {
                used = true;
                break;
            }
        }
        if (!used)
            return i;
    }
    return -1;
}

int HotseatCareerManager::getAssignedSlotCount() const
{
    return static_cast<int>(m_assignedTeamSlots.size());
}

int HotseatCareerManager::getTeamSlotFromEntry(const MenuEntry *entry) const
{
    if (!entry || !entry->fg.string || entry->invisible)
        return -1;
    auto packed = getCurrentPackedMenu();
    if (!packed || !SwosVM::isSwosPtr(packed) || SwosVM::ptrToOffset(packed) != 485019)
        return -1;
    auto tf = reinterpret_cast<const TeamFile *>(entry->fg.string - 5);
    return getTeamSlot(tf->globalTeamNumber);
}

void startCareerMode()
{
    extern void cseg_4C6EE();
    extern void InitCareer();
    extern void CommonMenuExit();

    logInfo("Starting Career Mode setup flow");

    // Step 1: Prompt for Number of Players (1, 2, 3, 4)
    s_chosenPlayerCount = 0;
    s_playerCountConfirmed = false;
    showMenu(careerPlayerCountMenu);
    if (!s_playerCountConfirmed || s_chosenPlayerCount < 1 || s_chosenPlayerCount > 4) {
        logInfo("Career player count selection canceled or exited");
        return;
    }

    int numPlayers = s_chosenPlayerCount;
    logInfo("Career Mode: %d player(s) selected", numPlayers);

    auto& mgr = HotseatCareerManager::get();
    if (numPlayers == 1) {
        // Native 1-player Career Mode without hotseat overrides
        extern void CareerMenu();
        mgr.reset();
        CareerMenu();
        return;
    }

    mgr.resetWizard(numPlayers);

    // Step 2: Sequential Player Setup Wizard (Name, Surname, Nationality, Role)
    for (int p = 0; p < numPlayers; ++p) {
        mgr.setWizardCurrentPlayer(p);
        mgr.setWizardAdvanced(false);
        mgr.applyWizardDefaultsToVm(p);

        A6 = 483353; // careerMenu
        SWOS::ShowMenu();

        if (!mgr.wizardAdvanced()) {
            logInfo("Player %d setup aborted by user", p + 1);
            mgr.setWizardActive(false);
            return;
        }
    }
    mgr.setWizardActive(false);

    // Step 3: Club Selection
    mgr.setSelectingCareerTeams(true);
    mgr.setCareerNumPlayers(numPlayers);
    mgr.unassignAllTeamSlots();

    cseg_4C6EE();

    mgr.setSelectingCareerTeams(false);

    uint16_t result = *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kChooseTeamsResult]);
    if (result == 0 || mgr.getAssignedSlotCount() < numPlayers) {
        logInfo("Career club selection canceled or incomplete (assigned=%d, needed=%d)",
                mgr.getAssignedSlotCount(), numPlayers);
        return;
    }

    // Step 4: Gather teams ordered by player slots (P1 -> index 0, P2 -> index 1, etc.)
    std::vector<int16_t> orderedTeamIds(numPlayers);
    std::vector<TeamFile> orderedTeams(numPlayers);

    for (int i = 0; i < numPlayers; ++i) {
        auto slotInfo = mgr.getAssignedSlotInfo(i);
        if (slotInfo && slotInfo->teamWord != 0) {
            orderedTeamIds[i] = static_cast<int16_t>(slotInfo->teamWord);
            orderedTeams[i] = slotInfo->teamFile;
        } else {
            int16_t tid = mgr.getTeamForSlot(i);
            bool found = false;
            int count = swos.g_numSelectedTeams;
            if (count <= 0 || count > 100) count = 100;

            for (int t = 0; t < count; ++t) {
                auto tf = reinterpret_cast<const TeamFile *>(&swos.g_selectedTeams[t * sizeof(TeamFile)]);
                if (tf->globalTeamNumber == tid) {
                    orderedTeams[i] = *tf;
                    orderedTeamIds[i] = *reinterpret_cast<const int16_t *>(tf);
                    found = true;
                    break;
                }
            }
            if (!found && swos.selTeamsPtr) {
                auto selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
                if (selTeams) {
                    for (int t = 0; t < count; ++t) {
                        if (selTeams[t].globalTeamNumber == tid) {
                            orderedTeams[i] = selTeams[t];
                            orderedTeamIds[i] = *reinterpret_cast<const int16_t *>(&selTeams[t]);
                            found = true;
                            break;
                        }
                    }
                }
            }
        }

        logInfo("Player %d assigned: %.*s (teamWord=0x%04x, countryFile=%d, ordinal=%d, globalId=%d)",
                i + 1, 17, orderedTeams[i].teamName,
                static_cast<uint16_t>(orderedTeamIds[i]),
                orderedTeams[i].teamFileNo, orderedTeams[i].teamOrdinal,
                orderedTeams[i].globalTeamNumber);
    }

    // Preserve wizard data before initializeHotseat() calls reset()
    std::vector<WizardPlayerInfo> savedWizardPlayers;
    savedWizardPlayers.reserve(numPlayers);
    for (int i = 0; i < numPlayers; ++i)
        savedWizardPlayers.push_back(mgr.wizardPlayer(i));

    mgr.initializeHotseat(orderedTeamIds);
    ScopedInitializingSeason initSeasonGuard(mgr);

    extern void CalculateStartingBalance();

    for (int i = 0; i < numPlayers; ++i) {
        auto& p = mgr.player(i);
        const auto& wp = savedWizardPlayers[i];
        p.managerName = wp.name;
        p.managerSurname = wp.surname;
        p.nationality = wp.nationality;
        p.coachOrPlayer = wp.coachOrPlayer;
        p.mrOrMs = wp.mrOrMs;
        p.careerNumPlayers = 16;

        char tbuf[18] = {0};
        std::memcpy(tbuf, orderedTeams[i].teamName, 17);
        p.teamName = tbuf;
        std::memcpy(p.careerTeam, &orderedTeams[i], sizeof(TeamFile));
        auto tf = reinterpret_cast<TeamFile *>(p.careerTeam);
        tf->teamControls = (p.coachOrPlayer == 1 ? kPlayerCoach : kCoach);
        p.careerTeamLeague = tf->league;

        // Compute individual starting balance for this club
        save68kRegisters();
        *(uint16_t *)&D0 = orderedTeamIds[i];
        CalculateStartingBalance();
        int32_t startingBal = static_cast<int32_t>(D1);
        restore68kRegisters();

        if (startingBal <= 0) startingBal = 1'000'000;
        p.startingBalance = startingBal;
        p.newBalance = startingBal;
        p.oldBalance = startingBal;
        p.playerWagesBill = 0;
        p.playerPurchases = 0;
        p.playerSales = 0;
        p.generalRunningCosts = 0;
        p.gateReceipts = 0;
        p.competitionBonuses = 0;
        p.sensisoftPlcSponsorship = 0;
    }

    // Restore Player 0's manager details into SWOS memory before InitCareer()
    const auto& wp0 = savedWizardPlayers[0];
    swos.coachOrPlayer = wp0.coachOrPlayer;
    swos.plNationality = wp0.nationality;
    *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kMrOrMs]) = wp0.mrOrMs;
    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], 0, 5);
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSexBuffer], wp0.mrOrMs != 0 ? "MS" : "MR", 2);

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], 0, 9);
    size_t nlen = std::min(wp0.name.size(), size_t{8});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerNameBuffer], wp0.name.data(), nlen);

    std::memset(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], 0, 13);
    size_t slen = std::min(wp0.surname.size(), size_t{12});
    std::memcpy(&SwosVM::g_memByte[CareerOffsets::kManagerSurnameBuffer], wp0.surname.data(), slen);

    // Initialize division career with Player 0's team ID (teamWord: (ordinal << 8) | countryFile)
    // cseg_8B8B8_OnEnter ensures all N hotseat clubs are flagged human-controlled in g_selectedTeams.
    logInfo("Calling InitCareer with D0=0x%04x (countryFile=%d, ordinal=%d)",
            static_cast<uint16_t>(orderedTeamIds[0]),
            orderedTeams[0].teamFileNo, orderedTeams[0].teamOrdinal);
    swos.g_gameType = kGameTypeCareer;
    *(uint16_t *)&D0 = orderedTeamIds[0];
    InitCareer();
    swos.g_gameType = kGameTypeCareer;

    // Save Player 0's complete initialized context
    mgr.saveCurrentSwosContext(mgr.player(0));
    reinterpret_cast<TeamFile *>(mgr.player(0).careerTeam)->teamControls =
        (mgr.player(0).coachOrPlayer == 1 ? kPlayerCoach : kCoach);

    extern void cseg_5DEB9();
    extern void cseg_3D204();
    extern void cseg_64A65();

    // Set up remaining players' own lineups, reserves/trialists, career history, and finances
    const auto& p0 = mgr.player(0);
    for (int i = 1; i < numPlayers; ++i) {
        auto& p = mgr.player(i);
        p.currentSeasonShowing = p0.currentSeasonShowing;
        p.currentSeasonNumber = p0.currentSeasonNumber;
        p.seasonPlaying = p0.seasonPlaying;
        p.dsegD6CA2 = p0.dsegD6CA2;
        p.dsegD7E66 = p0.dsegD7E66;
        p.dsegE0048 = p0.dsegE0048;
        p.dsegD6B62 = p0.dsegD6B62;
        p.dsegD6B64 = p0.dsegD6B64;
        p.dsegD6B94 = p0.dsegD6B94;
        p.dsegD6C46 = p0.dsegD6C46;
        p.additionalInvestments = p0.additionalInvestments;
        p.dsegD6CDC = p0.dsegD6CDC;
        p.teamNoOfferAccepted = p0.teamNoOfferAccepted;
        p.teamFundsAccepted = p0.teamFundsAccepted;
        p.dsegD6D0C = p0.dsegD6D0C;
        p.timeToNegotiate = p0.timeToNegotiate;
        p.stayingInOldClub = 0;
        p.careerNumPlayers = 16;
        p.careerTeamLeague = reinterpret_cast<const TeamFile *>(p.careerTeam)->league;
        p.numPlayersOnTransferMarket = 0;
        std::memset(p.playersOnTransferMarket, 0, sizeof(p.playersOnTransferMarket));
        std::memset(p.dsegD6F26Block, 0, sizeof(p.dsegD6F26Block));
        std::memset(p.dsegD6DBA, 0, sizeof(p.dsegD6DBA));
        std::memset(p.newJobOffers, 0, sizeof(p.newJobOffers));
        std::memset(p.jobOffers, 0, sizeof(p.jobOffers));
        std::memset(p.playerTrialWeeks, 0, sizeof(p.playerTrialWeeks));
        std::memset(p.currentMatchPlayers, 0, kCurrentMatchPlayersSize);
        std::memcpy(p.currentMatchPlayers, orderedTeams[i].playerNumbers, kNumPlayersInTeam);
        std::memset(p.trainingMatchSavedPlayers, 0, kCurrentMatchPlayersSize);
        std::memcpy(p.trainingMatchSavedPlayers, orderedTeams[i].playerNumbers, kNumPlayersInTeam);
        std::memset(p.dsegD8B8C, 0, kCurrentMatchPlayersSize);
        p.dsegD761A = 0xFFFF;
        p.dsegD761C = 0;
        p.dsegD8B6A = 0xFFFF;
        std::memset(&p.careerTeam[sizeof(TeamFile)], 0, kCareerTeamBufferSize - sizeof(TeamFile));
        writeTeamCoachName(p.careerTeam, p.managerName, p.managerSurname);
        std::memcpy(p.userTactics, p0.userTactics, 6 * kUserTacticsSize);
        std::memcpy(p.careerHistory, p0.careerHistory, sizeof(p.careerHistory));

        size_t histOfs = static_cast<size_t>(p.currentSeasonShowing) * kCareerHistoryEntrySize;
        if (histOfs + kCareerHistoryEntrySize <= kCareerHistoryBufferSize) {
            *reinterpret_cast<uint16_t *>(&p.careerHistory[histOfs]) = p.teamId;
            std::memset(&p.careerHistory[histOfs + 2], 0, 18);
            std::memcpy(&p.careerHistory[histOfs + 2], p.teamName.c_str(), std::min(p.teamName.size(), size_t{17}));
            uint32_t euroStrOfs = 0;
            if (p.euroCupIndex >= 0 && p.euroCupIndex < kNumEuroCups && mgr.isEuroCupActive(p.euroCupIndex)) {
                mgr.activateEuroCup(p.euroCupIndex, false);
                uint32_t strPtr = *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDA109]);
                euroStrOfs = (strPtr >= CareerOffsets::kChairmanScenes) ? (strPtr - CareerOffsets::kChairmanScenes) : 0;
            }
            *reinterpret_cast<uint32_t *>(&p.careerHistory[histOfs + 34]) = euroStrOfs;
        }

        mgr.loadSwosContext(p, true);
        save68kRegisters();
        if (hasCountryInSelectedTeams(static_cast<uint8_t>(p.teamId & 0xFF))) {
            cseg_64A65();
        } else if (!isVmNationalSelector()) {
            static const uint8_t kReservePositions[10] = { 2, 1, 5, 4, 3, 3, 6, 6, 7, 7 };
            for (int r = 0; r < 10; ++r) {
                int ord = 16 + r;
                auto *pf = reinterpret_cast<PlayerFile *>(
                    &SwosVM::g_memByte[CareerOffsets::kCareerTeam + kTeamFileHeaderSize + ord * kPlayerFileSize]);
                std::memset(pf, 0, kPlayerFileSize);
                pf->shirtNumber = 16;
                pf->positionAndFace = static_cast<byte>(kReservePositions[r] << 5);
                populateNamedTrialPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam],
                                             &SwosVM::g_memByte[CareerOffsets::kDsegD6C26],
                                             ord, static_cast<uint16_t>(p.teamId), false);
                SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers + ord] = static_cast<uint8_t>(ord);
                SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers + ord] = static_cast<uint8_t>(ord);
                SwosVM::g_memByte[CareerOffsets::kDsegD8B8C + ord] = pf->shirtNumber;
            }
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]) = 26;
        }
        cseg_5DEB9();
        cseg_3D204();
        restore68kRegisters();
        mgr.saveCurrentSwosContext(p);
    }

    // Clear stale Cup 2 / Euro Cup VM buffers if unused by any human player
    if (*reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CAA + 8]) == 0) {
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegD985C], 0, kCup2BufferSize);
    }
    if (!mgr.anyEuroCupActive() && *reinterpret_cast<const uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD8CB6]) == 0) {
        uint8_t schedTpl[6];
        std::memcpy(schedTpl, &SwosVM::g_memByte[CareerOffsets::kDsegDA11F], sizeof(schedTpl));
        std::memset(&SwosVM::g_memByte[CareerOffsets::kDsegDA0E2], 0, kEuroCupBufferSize);
        std::memcpy(&SwosVM::g_memByte[CareerOffsets::kDsegDA11F], schedTpl, sizeof(schedTpl));
    }

    mgr.syncPlayersWithSelectedTeams(true);
    mgr.setRoundCalendarIndex(*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]));

    int firstTurnPlayer = mgr.findNextPlayerForCurrentCalendarSlot();
    if (firstTurnPlayer < 0)
        firstTurnPlayer = 0;

    // Load whichever hotseat club owns the first calendar fixture slot
    mgr.switchToPlayer(firstTurnPlayer, true);
    logInfo("Hotseat Career successfully initialized with %d players! Starting P%d's turn.",
            numPlayers, firstTurnPlayer + 1);

    swos.g_exitMenu = 0;
    A6 = 482233; // contestMenu
    SWOS::ShowMenu();
    CommonMenuExit();
}

namespace SWOS {

void InitCareerVariables_OnEnter()
{
    HotseatCareerManager::get().reset();
}

void CareerInitVars_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && !mgr.isInitializingSeason()) {
        mgr.reset();
    }
}

void StartContest_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isActive())
        return;

    if (swos.g_gameType != kGameTypeCareer) {
        mgr.reset();
    } else {
        mgr.syncPlayersWithSelectedTeams(true);
        mgr.relinkCompetitionTeamPointers();
        mgr.syncTransferMarketPoolsForCurrentPlayer();
    }
}

void cseg_8B8B8_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isActive() || mgr.numPlayers() <= 1 || swos.g_gameType != kGameTypeCareer)
        return;

    mgr.ensureHotseatTeamControlsInSelectedTeams();
}

void cseg_94DB1_OnEnter()
{
    HotseatCareerManager::get().onWeeklyTickForAllPlayers();
}

void cseg_3C07C_OnEnter()
{
    HotseatCareerManager::get().onSeasonFinishedForAllPlayers();
}

void Hotseat_OnNewSeasonInitialized()
{
    auto& mgr = HotseatCareerManager::get();
    mgr.restoreCalendarEntryFirstSlots();
    mgr.onNewSeasonInitialized();
}

void Hotseat_OnAfterProcessCareerFile()
{
    HotseatCareerManager::get().onAfterProcessCareerFile();
}

void Hotseat_OnAfterPlayMatch()
{
    HotseatCareerManager::get().onAfterPlayMatch();
}

void InitializeNewSeason_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.numPlayers() > 1) {
        mgr.clearAllCalendarSlots();
    }
}

void cseg_34DA3_OnEnter()
{
    HotseatCareerManager::get().stageActiveCalendarSlotToVm();
}

void Hotseat_OnLeagueCalendarSlotAdded()
{
    HotseatCareerManager::get().captureLeagueCalendarSlot();
}

void Hotseat_ResetCupCalendarEntrySlots()
{
    HotseatCareerManager::get().resetCalendarEntrySlots(A6.asDword());
}

void Hotseat_OnCupCalendarSlotAdded()
{
    HotseatCareerManager::get().captureCupCalendarSlot();
}

void Hotseat_OnCalendarEntryFinished()
{
    HotseatCareerManager::get().restoreCalendarEntryFirstSlots();
}

void Hotseat_ResolvePostMatchCalendarSlotPtr()
{
    HotseatCareerManager::get().resolvePostMatchCalendarSlotPtr();
}

void Hotseat_CommitAndStageNextCalendarSlot()
{
    HotseatCareerManager::get().commitAndStageNextCalendarSlot();
}

void Hotseat_OnAfterCalcNextMatch()
{
    HotseatCareerManager::get().clampCalcNextMatchSlotPtr();
}

void Hotseat_OnCalendarInsertSpace()
{
    HotseatCareerManager::get().shiftCalendarSlots(A4.asDword(), D5.asWord());
}

void Hotseat_ResolveGameListSlotPtr()
{
    HotseatCareerManager::get().resolveGameListSlotPtr();
}

void cseg_8DAC3_OnEnter()
{
    HotseatCareerManager::get().initAdditionalEuroCups();
}

void cseg_8B7EA_OnEnter()
{
    HotseatCareerManager::get().unmaskEuroCupCalendarEntries();
}

void Hotseat_OnDomesticCupTeamSelect()
{
    HotseatCareerManager::get().addHumanTeamsToCupSelection(CareerOffsets::kDseg10F0F6);
}

void Hotseat_OnTournamentTeamSelect()
{
    HotseatCareerManager::get().addHumanTeamsToCupSelection(CareerOffsets::kDseg10EB23);
}

void cseg_8E539_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForCurrentCalendarEntry();
}

void cseg_8E841_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForCurrentCalendarEntry();
}

void cseg_8E796_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForCurrentCalendarEntry();
}

void cseg_8E469_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForCurrentCalendarEntry();
}

void cseg_29B4A_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (!mgr.isActive() || mgr.numPlayers() <= 1 || !mgr.anyEuroCupActive())
        return;

    uint16_t calType = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDiyCompetitionStart + 0x2B]);
    if (calType == 3) {
        mgr.maskOtherEuroCupCalendarEntries();
    }
}

void cseg_375C2_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.numPlayers() > 1 && mgr.anyEuroCupActive() &&
        mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        mgr.activateEuroCup(mgr.activePlayer().euroCupIndex, true);
    }
}

void cseg_39392_OnEnter()
{
    auto& mgr = HotseatCareerManager::get();
    if (mgr.isActive() && mgr.numPlayers() > 1 && mgr.anyEuroCupActive() &&
        mgr.activePlayerIndex() >= 0 && mgr.activePlayerIndex() < mgr.numPlayers()) {
        mgr.activateEuroCup(mgr.activePlayer().euroCupIndex, true);
    }
}

void cseg_92D55_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForSeasonEnd(0);
}

void cseg_9307A_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForSeasonEnd(1);
}

} // namespace SWOS

namespace {

bool marketPoolContains(const MarketEntry *entries, uint16_t count, uint16_t teamId, uint8_t ord)
{
    for (uint16_t i = 0; i < count; ++i) {
        if (entries[i].teamId == teamId && entries[i].playerOrdinal == ord)
            return true;
    }
    return false;
}

bool isHotseatListedPlayerValid(const HotseatPlayerContext& owner, uint8_t ord)
{
    if (ord >= 30)
        return false;

    int count = std::min<int>(owner.careerNumPlayers, kCurrentMatchPlayersSize);
    bool inActiveSquad = false;
    for (int i = 0; i < count; ++i) {
        if (owner.currentMatchPlayers[i] == ord) {
            inActiveSquad = true;
            break;
        }
    }
    if (!inActiveSquad)
        return false;

    const auto *pf = reinterpret_cast<const PlayerFile *>(
        owner.careerTeam + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize);
    return isValidPlayerName(pf->playerName) && (pf->positionAndFace & 0x02) == 0;
}

bool isOrdinalInHotseatTransferList(const HotseatPlayerContext& owner, uint8_t ord)
{
    int listedCount = std::min<int>(owner.numPlayersOnTransferMarket, 5);
    const auto *listed = reinterpret_cast<const uint16_t *>(owner.playersOnTransferMarket);
    for (int i = 0; i < listedCount; ++i) {
        if (listed[i] == ord && isHotseatListedPlayerValid(owner, ord))
            return true;
    }
    return false;
}

void sanitizeActivePlayerTransferList()
{
    uint16_t cnt = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]);
    int n = std::min<int>(cnt, 5);
    auto *listed = reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket]);
    uint16_t careerCount = std::min<uint16_t>(
        *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]),
        static_cast<uint16_t>(kCurrentMatchPlayersSize));
    const uint8_t *cmp = &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers];

    uint16_t kept[5] = {};
    int outCount = 0;
    for (int i = 0; i < n; ++i) {
        uint16_t ord = listed[i];
        if (ord >= 30)
            continue;
        bool inSquad = false;
        for (uint16_t s = 0; s < careerCount; ++s) {
            if (cmp[s] == ord) {
                inSquad = true;
                break;
            }
        }
        if (!inSquad)
            continue;
        const auto *pf = reinterpret_cast<const PlayerFile *>(
            &SwosVM::g_memByte[CareerOffsets::kCareerTeam + kTeamFileHeaderSize + ord * kPlayerFileSize]);
        if (!isValidPlayerName(pf->playerName) || (pf->positionAndFace & 0x02) != 0)
            continue;
        bool dup = false;
        for (int j = 0; j < outCount; ++j) {
            if (kept[j] == ord) {
                dup = true;
                break;
            }
        }
        if (!dup)
            kept[outCount++] = ord;
    }

    std::memset(listed, 0, kListedPlayersSize);
    std::memcpy(listed, kept, outCount * sizeof(uint16_t));
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]) =
        static_cast<uint16_t>(outCount);
}

uint32_t nextHotseatMarketRand(uint32_t salt)
{
    static uint32_t state = 0x6D2B79F5u;
    state = state * 1664525u + 1013904223u + salt;
    return state >> 16;
}

void tryAddAiDomesticMarketReplacement(HotseatCareerManager& mgr)
{
    if (!swos.selTeamsPtr || swos.g_numSelectedTeams <= 0)
        return;

    const auto *selTeams = reinterpret_cast<const TeamFile *>(SwosVM::offsetToPtr(swos.selTeamsPtr));
    if (!selTeams)
        return;

    int teamCount = std::min<int>(swos.g_numSelectedTeams, 68400 / static_cast<int>(sizeof(TeamFile)));
    uint16_t activeSelTeam = getVmSelTeamNumber();
    uint8_t activeCountry = static_cast<uint8_t>(activeSelTeam & 0xFF);

    std::vector<int> eligibleAiTeams;
    for (int t = 0; t < teamCount; ++t) {
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[t]);
        if (static_cast<uint8_t>(selTeams[t].teamFileNo) != activeCountry)
            continue;
        if (tid == activeSelTeam || mgr.findHotseatPlayerByTeamId(static_cast<int16_t>(tid)) >= 0)
            continue;
        eligibleAiTeams.push_back(t);
    }
    if (eligibleAiTeams.empty())
        return;

    uint16_t count = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]);
    if (count >= kMaxHomeMarketEntries)
        return;

    auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[CareerOffsets::kHomeMarketPlayers]);

    for (int attempt = 0; attempt < 128; ++attempt) {
        uint32_t r = nextHotseatMarketRand(activeSelTeam ^ count ^ static_cast<uint32_t>(attempt));

        int tIdx = eligibleAiTeams[(r + attempt) % eligibleAiTeams.size()];
        uint16_t tid = *reinterpret_cast<const uint16_t *>(&selTeams[tIdx]);
        uint8_t pOrd = static_cast<uint8_t>(((r >> 8) + attempt) & 0x0F);
        if (!isValidPlayerName(selTeams[tIdx].players[pOrd].playerName))
            continue;
        if (marketPoolContains(entries, count, tid, pOrd))
            continue;

        save68kRegisters();
        D0 = tid;
        D1 = pOrd;
        ::AddPlayerToCareerContest();
        restore68kRegisters();
        return;
    }
}

void tryAddAiForeignMarketReplacement(HotseatCareerManager& mgr)
{
    if (*reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kPoolplyrLoaded]) == 0)
        return;

    uint16_t count = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]);
    if (count >= kMaxForeignMarketEntries)
        return;

    const auto *pool = reinterpret_cast<const PoolPlayerEntry *>(&SwosVM::g_memByte[CareerOffsets::kPoolplyrDatBuffer]);
    auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[CareerOffsets::kForeignMarketPlayers]);
    uint8_t activeCountry = static_cast<uint8_t>(getVmSelTeamNumber() & 0xFF);

    uint32_t seed = nextHotseatMarketRand(activeCountry ^ count);

    for (int offset = 0; offset < kPoolPlyrCount; ++offset) {
        const auto& pe = pool[(seed + offset * 17) % kPoolPlyrCount];
        if (pe.teamId == 0 || static_cast<uint8_t>(pe.teamId & 0xFF) == activeCountry)
            continue;
        if (mgr.findHotseatPlayerByTeamId(static_cast<int16_t>(pe.teamId)) >= 0)
            continue;
        if (!isValidPlayerName(pe.player.playerName))
            continue;
        if (marketPoolContains(entries, count, pe.teamId, pe.playerOrdinal))
            continue;

        save68kRegisters();
        D0 = pe.teamId;
        D1 = pe.playerOrdinal;
        ::AddForeignMarketPlayer();
        restore68kRegisters();
        return;
    }
}

} // namespace

uint32_t HotseatCareerManager::ensureHotseatTeamMirror(int playerIdx)
{
    if (playerIdx < 0 || playerIdx >= m_numPlayers || playerIdx >= kMaxHotseatPlayers)
        return 0;

    if (m_vmTeamMirrorOfs[playerIdx] == 0) {
        uint32_t extraBase = static_cast<uint32_t>(
            reinterpret_cast<const uint8_t *>(SwosVM::getExtraMemoryArea()) - SwosVM::g_memByte);
        // Leave 256 bytes of headroom for negative SWOS struct adjustments (e.g. PoolPlayer - 56), plus 64 bytes for scratch
        m_vmTeamMirrorOfs[playerIdx] = extraBase + 320 + static_cast<uint32_t>(playerIdx * kCareerTeamBufferSize);
    }

    uint32_t ofs = m_vmTeamMirrorOfs[playerIdx];
    const uint8_t *src = (playerIdx == m_activePlayerIndex &&
                          getVmSelTeamNumber() == static_cast<uint16_t>(m_players[playerIdx].teamId))
        ? &SwosVM::g_memByte[CareerOffsets::kCareerTeam]
        : m_players[playerIdx].careerTeam;
    std::memcpy(&SwosVM::g_memByte[ofs], src, kCareerTeamBufferSize);
    return ofs;
}

uint32_t HotseatCareerManager::ensurePoolPlayerScratch()
{
    if (m_vmPoolPlayerScratchOfs == 0) {
        uint32_t extraBase = static_cast<uint32_t>(
            reinterpret_cast<const uint8_t *>(SwosVM::getExtraMemoryArea()) - SwosVM::g_memByte);
        m_vmPoolPlayerScratchOfs = extraBase + 256;
    }
    return m_vmPoolPlayerScratchOfs;
}

void HotseatCareerManager::onAfterCreateReplacementPlayer()
{
    save68kRegisters();
    const bool isLoan = (m_emergencyLoanDepth > 0);
    int ord = static_cast<int>(static_cast<uint32_t>(A3) - static_cast<uint32_t>(CareerOffsets::kCareerTeam)) /
              static_cast<int>(kPlayerFileSize);
    if (ord < 0 || ord >= 30) {
        int alt = static_cast<int>(static_cast<int16_t>(static_cast<uint16_t>(D6)));
        if (alt >= 0 && alt < 30)
            ord = alt;
    }
    if (ord >= 0 && ord < 30) {
        auto *pf = reinterpret_cast<PlayerFile *>(
            &SwosVM::g_memByte[CareerOffsets::kCareerTeam + kTeamFileHeaderSize + ord * kPlayerFileSize]);
        // Clear any uninitialized-D3 name from cseg_52A3E so it is cleanly generated & guaranteed non-empty
        pf->playerName[0] = '\0';
        populateReplacementPlayerSlot(&SwosVM::g_memByte[CareerOffsets::kCareerTeam],
                                      &SwosVM::g_memByte[CareerOffsets::kDsegD6C26],
                                      ord, getVmSelTeamNumber(), true, isLoan);
    }
    restore68kRegisters();
}

void HotseatCareerManager::onBeforeEmergencyLoan()
{
    ++m_emergencyLoanDepth;
}

void HotseatCareerManager::onAfterEmergencyLoan()
{
    if (m_emergencyLoanDepth > 0)
        --m_emergencyLoanDepth;
}

void HotseatCareerManager::onAfterEnsureCareerPlayers()
{
    if (swos.g_gameType != kGameTypeCareer || isVmNationalSelector())
        return;

    uint32_t teamOfs = A6.asDword();
    if (teamOfs != CareerOffsets::kCareerTeam)
        return;

    uint16_t excludeFlags = D4.asWord();
    uint16_t maxSubs = D5.asWord();
    int reqBase = swos.g_trainingGame ? 22 : 11;
    int reqTotal = reqBase + static_cast<int>(maxSubs);
    int reqOutfieldMin = std::max(0, reqTotal - 2);
    int reqGkMin = (maxSubs > 5) ? 2 : 1;

    extern void cseg_3A110();
    extern void cseg_3A2CE();
    extern void cseg_52956();

    save68kRegisters();
    bool addedAny = false;
    for (int iter = 0; iter < 30; ++iter) {
        uint16_t numPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        if (numPlayers >= 30)
            break;

        A0 = CareerOffsets::kCareerTeam;
        D0 = excludeFlags;
        D1 = 0;
        cseg_3A110();
        int fitGk = static_cast<int>(static_cast<int16_t>(D7.asWord()));

        A0 = CareerOffsets::kCareerTeam;
        D0 = excludeFlags;
        cseg_3A2CE();
        int fitOut = static_cast<int>(static_cast<int16_t>(D7.asWord()));

        bool needGk = (fitGk < reqGkMin) || (fitGk < 2 && fitOut >= reqOutfieldMin && (fitGk + fitOut) < reqTotal);
        bool needOut = !needGk && (fitOut < reqOutfieldMin || (fitGk + fitOut) < reqTotal);
        if (!needGk && !needOut)
            break;

        if (needGk) {
            D0 = 0;
        } else {
            SWOS::Rand2();
            D0 = 1 + (((D0.asWord() & 0xFFFFu) * 7u) >> 8);
        }
        cseg_52956();
        addedAny = true;
    }
    if (addedAny) {
        ensureVmTrainingMatchSavedPlayersSynced();
    }
    restore68kRegisters();
}

void HotseatCareerManager::unpackH2hPackedTeam(int playerIdx)
{
    if (playerIdx < 0 || playerIdx >= kMaxHotseatPlayers)
        return;

    auto& pm = m_h2hPackMaps[playerIdx];
    if (!pm.active || pm.vmTeamOfs < 1000 || pm.vmTeamOfs + sizeof(TeamFile) > sizeof(SwosVM::g_memByte)) {
        pm.active = false;
        return;
    }

    auto& ctx = m_players[playerIdx];
    const auto *packedTf = reinterpret_cast<const TeamFile *>(&SwosVM::g_memByte[pm.vmTeamOfs]);
    auto *dstTf = reinterpret_cast<TeamFile *>(ctx.careerTeam);

    if (*reinterpret_cast<const uint16_t *>(packedTf) == static_cast<uint16_t>(ctx.teamId)) {
        dstTf->writesZeroHere = packedTf->writesZeroHere;
        dstTf->andWith0xFE = packedTf->andWith0xFE;
        dstTf->tactics = packedTf->tactics;
        dstTf->teamControls = (ctx.coachOrPlayer == 1) ? kPlayerCoach : kCoach;

        for (int slot = 0; slot < kNumPlayersInTeam; ++slot) {
            uint8_t ord = pm.slotToOrd[slot];
            if (ord < 30) {
                std::memcpy(ctx.careerTeam + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize,
                            &packedTf->players[slot],
                            kPlayerFileSize);
            }
        }
    }

    pm = H2hPackedTeamMap{};
}

void HotseatCareerManager::unpackAllH2hPackedTeams()
{
    for (int i = 0; i < kMaxHotseatPlayers; ++i) {
        if (m_h2hPackMaps[i].active) {
            unpackH2hPackedTeam(i);
        }
    }
}

void HotseatCareerManager::onBeforeShowPlayMatchMenu()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer || isVmNationalSelector())
        return;

    auto resolveTeamIdx = [&](uint32_t vmOfs) -> int {
        if (vmOfs == CareerOffsets::kCareerTeam)
            return m_activePlayerIndex;
        if (vmOfs >= 1000 && vmOfs + sizeof(TeamFile) <= sizeof(SwosVM::g_memByte)) {
            int16_t tid = *reinterpret_cast<const int16_t *>(&SwosVM::g_memByte[vmOfs]);
            return findHotseatPlayerByTeamId(tid);
        }
        return -1;
    };

    uint32_t origA1 = A1.asDword();
    uint32_t origA2 = A2.asDword();
    int idx1 = resolveTeamIdx(origA1);
    int idx2 = resolveTeamIdx(origA2);

    PlayMatchSetupState st;
    st.savedActiveIdx = m_activePlayerIndex;
    st.team1HotseatIdx = idx1;
    st.origA1 = origA1;
    st.origA2 = origA2;
    st.didSwitch = false;

    if (idx1 >= 0 && idx1 < m_numPlayers && idx1 != m_activePlayerIndex) {
        unpackH2hPackedTeam(idx1);
        saveCurrentSwosContext(m_players[m_activePlayerIndex]);

        uint32_t newA2 = origA2;
        if (idx2 >= 0 && idx2 < m_numPlayers && idx2 != idx1 && origA2 == CareerOffsets::kCareerTeam) {
            newA2 = ensureHotseatTeamMirror(idx2);
        }

        m_activePlayerIndex = idx1;
        loadSwosContext(m_players[idx1], false);
        A1 = static_cast<uint32_t>(CareerOffsets::kCareerTeam);
        A2 = newA2;
        st.didSwitch = true;
    } else if (idx1 == m_activePlayerIndex && idx1 >= 0 && origA1 != CareerOffsets::kCareerTeam) {
        A1 = static_cast<uint32_t>(CareerOffsets::kCareerTeam);
    }

    m_playMatchSetupStack.push_back(st);
}

void HotseatCareerManager::onAfterShowPlayMatchMenu()
{
    if (m_playMatchSetupStack.empty())
        return;

    uint32_t savedRetD0 = D0;
    save68kRegisters();

    PlayMatchSetupState st = m_playMatchSetupStack.back();
    m_playMatchSetupStack.pop_back();

    if (st.team1HotseatIdx >= 0 && st.team1HotseatIdx < m_numPlayers && m_activePlayerIndex == st.team1HotseatIdx) {
        saveCurrentSwosContext(m_players[st.team1HotseatIdx]);

        if (st.origA1 != CareerOffsets::kCareerTeam &&
            st.origA1 >= 1000 && st.origA1 + sizeof(TeamFile) <= sizeof(SwosVM::g_memByte)) {
            const auto& ctx = m_players[st.team1HotseatIdx];
            auto *packedTf = reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[st.origA1]);
            std::memcpy(packedTf, ctx.careerTeam, kTeamFileHeaderSize);
            packedTf->teamControls = (ctx.coachOrPlayer == 1) ? kPlayerCoach : kCoach;

            auto& pm = m_h2hPackMaps[st.team1HotseatIdx];
            pm = H2hPackedTeamMap{};
            pm.active = true;
            pm.vmTeamOfs = st.origA1;
            for (int slot = 0; slot < kNumPlayersInTeam; ++slot) {
                uint8_t ord = ctx.currentMatchPlayers[slot];
                if (ord >= 30)
                    ord = static_cast<uint8_t>(slot);
                std::memcpy(&packedTf->players[slot],
                            ctx.careerTeam + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize,
                            kPlayerFileSize);
                packedTf->playerNumbers[slot] = static_cast<byte>(slot);
                pm.slotToOrd[slot] = ord;
            }
        }
    }

    if (st.didSwitch && st.savedActiveIdx >= 0 && st.savedActiveIdx < m_numPlayers) {
        m_activePlayerIndex = st.savedActiveIdx;
        loadSwosContext(m_players[st.savedActiveIdx], false);
    }

    swos.playMatchTeam1Ptr.setRaw(st.origA1);
    swos.playMatchTeam2Ptr.setRaw(st.origA2);

    if (swos.g_exitGameFlag != 0) {
        unpackAllH2hPackedTeams();
        syncPlayersWithSelectedTeams(true);
    }

    restore68kRegisters();
    D0 = savedRetD0;
}

void HotseatCareerManager::onBeforeAddDomesticMarket()
{
    m_prevHomeMarketCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]);
}

void HotseatCareerManager::onAfterAddDomesticMarket()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    uint16_t curCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]);
    if (curCount > m_prevHomeMarketCount && curCount <= kMaxHomeMarketEntries) {
        auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[CareerOffsets::kHomeMarketPlayers]);
        const auto& last = entries[curCount - 1];
        if (findHotseatPlayerByTeamId(static_cast<int16_t>(last.teamId)) >= 0) {
            std::memset(&entries[curCount - 1], 0, sizeof(MarketEntry));
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumHomeMarketPlayers]) = curCount - 1;
            tryAddAiDomesticMarketReplacement(*this);
        }
    }
}

void HotseatCareerManager::onBeforeAddForeignMarket()
{
    m_prevForeignMarketCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]);
}

void HotseatCareerManager::onAfterAddForeignMarket()
{
    if (!isActive() || m_numPlayers <= 1)
        return;

    uint16_t curCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]);
    if (curCount > m_prevForeignMarketCount && curCount <= kMaxForeignMarketEntries) {
        auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[CareerOffsets::kForeignMarketPlayers]);
        const auto& last = entries[curCount - 1];
        if (findHotseatPlayerByTeamId(static_cast<int16_t>(last.teamId)) >= 0) {
            std::memset(&entries[curCount - 1], 0, sizeof(MarketEntry));
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumForeignMarketPlayers]) = curCount - 1;
            tryAddAiForeignMarketReplacement(*this);
        }
    }
}

void HotseatCareerManager::syncTransferMarketPoolsForCurrentPlayer()
{
    static bool s_syncingMarkets = false;
    if (s_syncingMarkets || !isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return;
    if (isVmNationalSelector())
        return;
    if (m_activePlayerIndex < 0 || m_activePlayerIndex >= m_numPlayers)
        return;

    struct Guard {
        bool& f;
        explicit Guard(bool& r) : f(r) { f = true; }
        ~Guard() { f = false; }
    } guard(s_syncingMarkets);

    // Pull latest active player's squad/transfer list from VM first
    if (getVmSelTeamNumber() == static_cast<uint16_t>(m_players[m_activePlayerIndex].teamId)) {
        sanitizeActivePlayerTransferList();
        auto& active = m_players[m_activePlayerIndex];
        active.careerNumPlayers = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerNumPlayers]);
        std::memcpy(active.currentMatchPlayers, &SwosVM::g_memByte[CareerOffsets::kCurrentMatchPlayers], kCurrentMatchPlayersSize);
        std::memcpy(active.trainingMatchSavedPlayers, &SwosVM::g_memByte[CareerOffsets::kTrainingMatchSavedPlayers], kCurrentMatchPlayersSize);
        std::memcpy(active.dsegD8B8C, &SwosVM::g_memByte[CareerOffsets::kDsegD8B8C], kCurrentMatchPlayersSize);
        std::memcpy(active.careerTeam, &SwosVM::g_memByte[CareerOffsets::kCareerTeam], kCareerTeamBufferSize);
        syncTrainingMatchSavedPlayers(active.trainingMatchSavedPlayers, active.dsegD8B8C, active.careerTeam,
                                      active.careerNumPlayers, active.currentMatchPlayers);
        std::memcpy(active.playerTrialWeeks, &SwosVM::g_memByte[CareerOffsets::kDsegD6C26], sizeof(active.playerTrialWeeks));
        active.numPlayersOnTransferMarket =
            *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kNumPlayersOnTransferMarket]);
        std::memcpy(active.playersOnTransferMarket,
                    &SwosVM::g_memByte[CareerOffsets::kPlayersOnTransferMarket], kListedPlayersSize);
    }

    uint8_t activeCountry = static_cast<uint8_t>(m_players[m_activePlayerIndex].teamId & 0xFF);

    auto syncSinglePool = [&](size_t countOfs, size_t entriesOfs, int maxEntries, bool isDomesticPool) {
        uint16_t rawCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[countOfs]);
        int n = std::min<int>(rawCount, maxEntries);
        auto *entries = reinterpret_cast<MarketEntry *>(&SwosVM::g_memByte[entriesOfs]);

        std::vector<MarketEntry> kept;
        kept.reserve(maxEntries);

        for (int i = 0; i < n; ++i) {
            MarketEntry e = entries[i];
            if (e.teamId == 0)
                continue;

            int ownerIdx = findHotseatPlayerByTeamId(static_cast<int16_t>(e.teamId));
            if (ownerIdx >= 0) {
                bool sameCountry = (static_cast<uint8_t>(m_players[ownerIdx].teamId & 0xFF) == activeCountry);
                if (ownerIdx == m_activePlayerIndex || sameCountry != isDomesticPool)
                    continue;
                if (!isOrdinalInHotseatTransferList(m_players[ownerIdx], e.playerOrdinal))
                    continue;
            } else {
                bool sameCountry = (static_cast<uint8_t>(e.teamId & 0xFF) == activeCountry);
                if (sameCountry != isDomesticPool)
                    continue;
            }

            if (!marketPoolContains(kept.data(), static_cast<uint16_t>(kept.size()), e.teamId, e.playerOrdinal)) {
                e.padding = 0;
                kept.push_back(e);
            }
        }

        // Insert every explicitly listed player of OTHER hotseat clubs matching this pool's scope
        for (int ownerIdx = 0; ownerIdx < m_numPlayers; ++ownerIdx) {
            if (ownerIdx == m_activePlayerIndex)
                continue;
            const auto& owner = m_players[ownerIdx];
            bool sameCountry = (static_cast<uint8_t>(owner.teamId & 0xFF) == activeCountry);
            if (sameCountry != isDomesticPool)
                continue;

            int listedCount = std::min<int>(owner.numPlayersOnTransferMarket, 5);
            const auto *listed = reinterpret_cast<const uint16_t *>(owner.playersOnTransferMarket);
            for (int k = 0; k < listedCount; ++k) {
                uint16_t ord = listed[k];
                if (ord >= 30 || !isHotseatListedPlayerValid(owner, static_cast<uint8_t>(ord)))
                    continue;
                uint16_t tid = static_cast<uint16_t>(owner.teamId);
                uint8_t ordByte = static_cast<uint8_t>(ord);
                if (marketPoolContains(kept.data(), static_cast<uint16_t>(kept.size()), tid, ordByte))
                    continue;

                MarketEntry addEntry{ tid, ordByte, 0 };
                if (static_cast<int>(kept.size()) < maxEntries) {
                    kept.push_back(addEntry);
                } else {
                    for (int j = static_cast<int>(kept.size()) - 1; j >= 0; --j) {
                        if (findHotseatPlayerByTeamId(static_cast<int16_t>(kept[j].teamId)) < 0) {
                            kept[j] = addEntry;
                            break;
                        }
                    }
                }
            }
        }

        std::memset(entries, 0, static_cast<size_t>(maxEntries) * sizeof(MarketEntry));
        if (!kept.empty()) {
            std::memcpy(entries, kept.data(), kept.size() * sizeof(MarketEntry));
        }
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[countOfs]) = static_cast<uint16_t>(kept.size());
    };

    syncSinglePool(CareerOffsets::kNumHomeMarketPlayers, CareerOffsets::kHomeMarketPlayers,
                   kMaxHomeMarketEntries, true);
    syncSinglePool(CareerOffsets::kNumForeignMarketPlayers, CareerOffsets::kForeignMarketPlayers,
                   kMaxForeignMarketEntries, false);
}

void HotseatCareerManager::resolveMarketTeamPtrForEntry(uint32_t entryOffset)
{
    if (!isActive() || entryOffset + sizeof(MarketEntry) > sizeof(SwosVM::g_memByte))
        return;

    uint16_t teamId = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[entryOffset]);
    int idx = findHotseatPlayerByTeamId(static_cast<int16_t>(teamId));
    if (idx >= 0) {
        uint16_t ord = static_cast<uint16_t>(static_cast<uint32_t>(D1) & 0xFFFFu);
        if (m_h2hPackMaps[idx].active && m_h2hPackMaps[idx].vmTeamOfs != 0 && ord < kNumPlayersInTeam)
            return;
        A0 = ensureHotseatTeamMirror(idx);
    }
}

void HotseatCareerManager::lookupForeignPoolPlayer()
{
    uint16_t teamId = static_cast<uint16_t>(static_cast<uint32_t>(D0) & 0xFFFFu);
    uint8_t ord = static_cast<uint8_t>(static_cast<uint32_t>(D1) & 0xFFu);
    uint32_t scratchOfs = ensurePoolPlayerScratch();

    if (isActive()) {
        int idx = findHotseatPlayerByTeamId(static_cast<int16_t>(teamId));
        if (idx >= 0) {
            uint32_t mirrorOfs = ensureHotseatTeamMirror(idx);
            const auto *tf = reinterpret_cast<const TeamFile *>(&SwosVM::g_memByte[mirrorOfs]);
            auto *out = reinterpret_cast<PoolPlayerEntry *>(&SwosVM::g_memByte[scratchOfs]);
            std::memset(out, 0, sizeof(PoolPlayerEntry));
            out->teamId = teamId;
            out->playerOrdinal = ord;
            std::memcpy(out->clubName, tf->teamName, sizeof(out->clubName));
            if (ord < 30) {
                std::memcpy(&out->player,
                            &SwosVM::g_memByte[mirrorOfs + kTeamFileHeaderSize + static_cast<size_t>(ord) * kPlayerFileSize],
                            kPlayerFileSize);
            }
            A0 = scratchOfs;
            return;
        }
    }

    const auto *pool = reinterpret_cast<const PoolPlayerEntry *>(&SwosVM::g_memByte[CareerOffsets::kPoolplyrDatBuffer]);
    for (int i = 0; i < kPoolPlyrCount; ++i) {
        if (pool[i].teamId == teamId && pool[i].playerOrdinal == ord) {
            A0 = static_cast<uint32_t>(CareerOffsets::kPoolplyrDatBuffer + static_cast<size_t>(i) * sizeof(PoolPlayerEntry));
            return;
        }
    }

    auto *out = reinterpret_cast<PoolPlayerEntry *>(&SwosVM::g_memByte[scratchOfs]);
    std::memset(out, 0, sizeof(PoolPlayerEntry));
    out->teamId = teamId;
    out->playerOrdinal = ord;
    A0 = scratchOfs;
}

void HotseatCareerManager::resolveGetPlayerTeamPtr()
{
    if (!isActive())
        return;

    uint16_t teamId = static_cast<uint16_t>(static_cast<uint32_t>(D0) & 0xFFFFu);
    if (teamId == static_cast<uint16_t>(swos.currentTeamNumber) || teamId == getVmSelTeamNumber())
        return;

    int idx = findHotseatPlayerByTeamId(static_cast<int16_t>(teamId));
    if (idx >= 0) {
        uint16_t ord = static_cast<uint16_t>(static_cast<uint32_t>(D1) & 0xFFFFu);
        if (m_h2hPackMaps[idx].active && m_h2hPackMaps[idx].vmTeamOfs != 0 && ord < kNumPlayersInTeam)
            return;
        A0 = ensureHotseatTeamMirror(idx);
    }
}

namespace SWOS {

void cseg_9339D_OnEnter()
{
    HotseatCareerManager::get().activateEuroCupForSeasonEnd(2);
}

void Hotseat_OnAfterReplacementPlayer()
{
    HotseatCareerManager::get().onAfterCreateReplacementPlayer();
}

void Hotseat_OnBeforeAddDomesticMarket()
{
    HotseatCareerManager::get().onBeforeAddDomesticMarket();
}

void Hotseat_OnAfterAddDomesticMarket()
{
    HotseatCareerManager::get().onAfterAddDomesticMarket();
}

void Hotseat_OnBeforeAddForeignMarket()
{
    HotseatCareerManager::get().onBeforeAddForeignMarket();
}

void Hotseat_OnAfterAddForeignMarket()
{
    HotseatCareerManager::get().onAfterAddForeignMarket();
}

void Hotseat_SyncTransferMarkets()
{
    HotseatCareerManager::get().syncTransferMarketPoolsForCurrentPlayer();
}

void Hotseat_ResolveMarketEntryTeamPtr()
{
    HotseatCareerManager::get().resolveMarketTeamPtrForEntry(A6);
}

void Hotseat_ResolveMarketListTeamPtr()
{
    HotseatCareerManager::get().resolveMarketTeamPtrForEntry(A1);
}

void Hotseat_LookupForeignPoolPlayer()
{
    HotseatCareerManager::get().lookupForeignPoolPlayer();
}

void Hotseat_ResolveGetPlayerTeamPtr()
{
    HotseatCareerManager::get().resolveGetPlayerTeamPtr();
}

void cseg_52956_OnEnter()
{
    HotseatCareerManager::get().onBeforeEmergencyLoan();
}

void Hotseat_OnAfterEmergencyLoan()
{
    HotseatCareerManager::get().onAfterEmergencyLoan();
}

void Hotseat_OnAfterEnsureCareerPlayers()
{
    HotseatCareerManager::get().onAfterEnsureCareerPlayers();
}

void ShowPlayMatchMenu_OnEnter()
{
    HotseatCareerManager::get().onBeforeShowPlayMatchMenu();
}

void Hotseat_OnAfterShowPlayMatchMenu()
{
    HotseatCareerManager::get().onAfterShowPlayMatchMenu();
}

} // namespace SWOS

