#include "hotseatInternal.h"

namespace HotseatInternal {

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

void copyBoundedPlayerName(char *outName, size_t outSize, const char *src, bool trimPadding /* = false */)
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

void applySeasonOutcomeToPlayerContext(HotseatPlayerContext& p, uint16_t goalsFor, uint16_t goalsAgainst,
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

void removeTeamFromJobOfferBuffer(uint8_t *buf, int16_t& count, uint16_t teamId)
{
    int n = std::clamp<int>(count, 0, kMaxJobOffersPerQueue);
    int writeIdx = 0;
    for (int i = 0; i < n; ++i) {
        const uint8_t *src = buf + static_cast<size_t>(i) * kJobOfferEntrySize;
        uint16_t entryTeamId = *reinterpret_cast<const uint16_t *>(src);
        if (entryTeamId == teamId)
            continue;
        if (writeIdx != i) {
            std::memmove(buf + static_cast<size_t>(writeIdx) * kJobOfferEntrySize, src, kJobOfferEntrySize);
        }
        ++writeIdx;
    }
    if (writeIdx < kMaxJobOffersPerQueue) {
        std::memset(buf + static_cast<size_t>(writeIdx) * kJobOfferEntrySize, 0,
                    static_cast<size_t>(kMaxJobOffersPerQueue - writeIdx) * kJobOfferEntrySize);
    }
    count = static_cast<int16_t>(writeIdx);
}

int32_t computeJobOfferStartingBalance(int32_t amountOffered)
{
    if (amountOffered == -1)
        return -1;

    save68kRegisters();
    SWOS::Rand2();
    uint16_t rnd = static_cast<uint16_t>(D0.asWord());
    restore68kRegisters();

    uint32_t thousands = static_cast<uint32_t>(amountOffered) / 1000u;
    uint32_t pct = ((static_cast<uint32_t>(rnd) * 25u) >> 8) + 80u;
    uint32_t scaledThousands = (thousands * pct) / 100u;
    return static_cast<int32_t>(scaledThousands * 1000u);
}

void applyCareerTeamResetInit(uint8_t *teamBuf, bool newClub)
{
    auto *hdr = reinterpret_cast<TeamFileHeader *>(teamBuf);
    hdr->someFlag = 1;
    hdr->writesZeroHere = 0;
    hdr->andWith0xFE &= 0xFE;

    for (int slot = 0; slot < 32; ++slot) {
        auto *pf = reinterpret_cast<PlayerFile *>(teamBuf + kTeamFileHeaderSize + static_cast<size_t>(slot) * kPlayerFileSize);
        pf->cardsInjuries = 0;
        if (newClub) {
            pf->positionAndFace &= 0xFD;
            pf->someFlag = 0;
        }
        pf->positionAndFace &= 0xFB;
        pf->unk_2 = 0;
        pf->unk_3 = 0;
        pf->unk_4 = 0;
        pf->unk_5 = 0;
    }
}

void updateExistingTmdFileEntry(uint16_t teamId, const uint8_t *teamFileBytes, TeamControls controls)
{
    int maxTmdTeams = (static_cast<int>(CareerOffsets::kDiyFileBufferCopy) -
                       static_cast<int>(CareerOffsets::kTmdFileBuffer)) /
                      static_cast<int>(sizeof(TeamFile));
    int tmdCount = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kTmdCareerCount]);
    tmdCount = std::clamp(tmdCount, 0, maxTmdTeams);

    auto *tmdTeams = reinterpret_cast<TeamFile *>(&SwosVM::g_memByte[CareerOffsets::kTmdFileBuffer]);
    for (int i = 0; i < tmdCount; ++i) {
        if (*reinterpret_cast<const uint16_t *>(&tmdTeams[i]) == teamId) {
            std::memcpy(&tmdTeams[i], teamFileBytes, sizeof(TeamFile));
            tmdTeams[i].teamControls = controls;
            break;
        }
    }
}

void loadSwitchedClubVia5DB56(int16_t targetNewTid, int32_t startBal)
{
    // Invalidate all FindTeamInCache shortcuts so cseg_5DB56 loads the destination club fresh rather than reusing stale careerTeam/selTeam cache
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg11F9CC]) = 0;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCurrentTeamNumber]) = 0xFFFF;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSelTeamNumber]) = 0xFFFF;
    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerTeam]) = 0;

    save68kRegisters();
    D0 = static_cast<uint16_t>(targetNewTid);
    D1 = static_cast<uint32_t>(startBal);
    ::cseg_5DB56();
    restore68kRegisters();
}

bool teamNamesMatch(const std::string& a, const char *b)
{
    if (!b || a.empty())
        return false;

    std::string lhs(a);
    std::string rhs(b);
    trimTrailingSwosPadding(lhs);
    trimTrailingSwosPadding(rhs);

    return !lhs.empty() && lhs == rhs;
}

uint32_t getGameListScratchBase()
{
    return getHotseatExtraMemoryOffset() + kGameListScratchSubOffset;
}

} // namespace HotseatInternal

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
