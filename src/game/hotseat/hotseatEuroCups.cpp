#include "hotseatInternal.h"
#include "menus.h"

using namespace HotseatInternal;

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
