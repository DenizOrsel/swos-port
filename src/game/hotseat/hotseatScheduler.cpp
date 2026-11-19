#include "hotseatInternal.h"
#include "menus.h"

using namespace HotseatInternal;

namespace {

void captureSlotForHeader(std::map<uint16_t, std::vector<HotseatCareerManager::CalendarSlotBytes>>& slots,
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

int inspectNextPendingCalendarEntry(const uint8_t *calendar, uint16_t startIdx, uint16_t startOfs,
                                    uint16_t totalEntries, uint8_t& outCompSlot, int& outRemSlots)
{
    if (!calendar || totalEntries == 0)
        return -1;

    uint16_t idx = startIdx;
    uint16_t ofs = startOfs;
    while (idx < totalEntries && ofs + 22 <= kCalendarBufferSize) {
        const uint8_t *entry = &calendar[ofs];
        uint16_t len = *reinterpret_cast<const uint16_t *>(entry + 2);
        if (len < 22)
            break;
        if (entry[4] == 0) {
            outCompSlot = entry[0];
            outRemSlots = static_cast<int8_t>(entry[6]);
            return static_cast<int>(entry[1]);
        }
        ofs += len;
        ++idx;
    }
    return -1;
}

} // namespace

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

    if (slotIdx == 0) {
        std::memcpy(&SwosVM::g_memByte[calEntryVm + 10], it->second[0].data(), 12);
        if (calEntryVm == activeHdr) {
            *reinterpret_cast<uint32_t *>(&SwosVM::g_memByte[CareerOffsets::kDseg17457E]) =
                (activeSlotIdx == 0) ? calEntryVm : (calEntryVm + 1);
        }
        A4 = calEntryVm;
        return;
    }

    uint32_t scratchBase = getGameListScratchBase();
    uint32_t scratchLimit = getHotseatExtraMemoryOffset() + static_cast<uint32_t>(getHotseatExtraMemorySize());
    uint32_t row = (D0.asWord() >> 3) & 0x0F;
    uint32_t scratchVm = scratchBase + row * 22;
    if (scratchVm + 22 <= scratchLimit && scratchVm + 22 <= sizeof(SwosVM::g_memByte)) {
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
    if (!m_active || m_activePlayerIndex < 0 || m_activePlayerIndex >= m_numPlayers)
        return false;
    return m_players[m_activePlayerIndex].matchPlayedThisTurn;
}

void HotseatCareerManager::onMatchCompleted()
{
    if (m_active && m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) {
        m_players[m_activePlayerIndex].matchPlayedThisTurn = true;
    }
}

void HotseatCareerManager::markPlayerMatchCompleted(int playerIndex)
{
    if (playerIndex >= 0 && playerIndex < m_numPlayers) {
        m_players[playerIndex].matchPlayedThisTurn = true;
    }
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

bool HotseatCareerManager::areAllWorldsSeasonFinished() const
{
    if (!m_hasActiveWorld || m_leagueWorlds.empty()) {
        uint16_t sFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
        return sFin != 0;
    }

    for (const auto& kv : m_leagueWorlds) {
        if (!kv.second.initialized)
            continue;
        if (kv.first == m_activeWorldKey) {
            uint16_t sFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
            if (sFin == 0)
                return false;
        } else if (kv.second.seasonFinished == 0) {
            return false;
        }
    }
    return true;
}

bool HotseatCareerManager::stepActiveWorldOneCpuEntrySilently()
{
    uint16_t sFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
    uint16_t totalEntries = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E2]);
    uint16_t curCalIdx = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);
    uint16_t curCalOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);

    if (sFin != 0 || totalEntries == 0 || curCalIdx >= totalEntries ||
        curCalOfs + 22 > kCalendarBufferSize) {
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]) = 1;
        *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]) = 1;
        saveActiveWorldFromVm();
        return false;
    }

    syncPlayersWithSelectedTeams(true);
    ensureHotseatTeamControlsInSelectedTeams();
    relinkCompetitionTeamPointers();

    uint8_t *entry = &SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + curCalOfs];
    if (entry[4] == 0 && static_cast<int8_t>(entry[6]) > 0) {
        entry[6] = 0;
    }

    *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E4]) = 0;

    extern void cseg_8E841();
    extern void cseg_8EA82();
    extern void cseg_8EC2B();

    save68kRegisters();
    if (entry[4] == 0) {
        cseg_8E841();
    }
    cseg_8EA82();
    cseg_8EC2B();
    restore68kRegisters();

    sFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
    if (sFin == 0 && totalEntries > 0) {
        uint16_t newOfs = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3E0]);
        if (newOfs + 22 <= kCalendarBufferSize) {
            SwosVM::g_memByte[CareerOffsets::kDsegE0048] =
                SwosVM::g_memByte[CareerOffsets::kDsegDB3E6 + newOfs + 1];
        }
    }

    if (m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) {
        saveCurrentSwosContext(m_players[m_activePlayerIndex]);
    }
    saveCurrentEuroCup();
    syncPlayersWithSelectedTeams(true);
    saveActiveWorldFromVm();
    return true;
}

bool HotseatCareerManager::resolveLockstepAndSelectPlayer(bool refreshUi /* = true */)
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer)
        return false;

    if (m_leagueWorlds.size() <= 1)
        return syncActivePlayerToHighlightedSlot(refreshUi);

    if (m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) {
        saveCurrentSwosContext(m_players[m_activePlayerIndex]);
    }
    saveActiveWorldFromVm();
    saveCurrentEuroCup();

    int prevActiveIdx = m_activePlayerIndex;
    auto findRepPlayerInWorld = [&](const LeagueWorldKey& k) -> int {
        for (int p = 0; p < m_numPlayers; ++p) {
            if (worldKeyForPlayer(m_players[p]) == k)
                return p;
        }
        return (m_activePlayerIndex >= 0 && m_activePlayerIndex < m_numPlayers) ? m_activePlayerIndex : 0;
    };

    auto updateUiFixtures = [&]() {
        stageActiveCalendarSlotToVm();
        extern void cseg_34DA3();
        extern void cseg_3563E();
        save68kRegisters();
        cseg_34DA3();
        cseg_3563E();
        restore68kRegisters();
        if (refreshUi) {
            refreshContestMenuUi();
        }
    };

    for (int step = 0; step < 512; ++step) {
        int minWeek = -1;
        for (const auto& kv : m_leagueWorlds) {
            const auto& lw = kv.second;
            if (!lw.initialized || lw.seasonFinished != 0)
                continue;
            uint8_t dummyComp = 0;
            int dummyRem = 0;
            int w = inspectNextPendingCalendarEntry(lw.calendar, lw.calStepIdx, lw.calByteOfs,
                                                    lw.calTotalEntries, dummyComp, dummyRem);
            if (w >= 0 && (minWeek < 0 || w < minWeek))
                minWeek = w;
        }

        if (minWeek < 0) {
            for (const auto& kv : m_leagueWorlds) {
                if (!kv.second.initialized)
                    continue;
                if (kv.second.seasonFinished == 0) {
                    int rep = findRepPlayerInWorld(kv.first);
                    if (kv.first != m_activeWorldKey) {
                        loadWorldToVm(kv.first, false);
                    }
                    m_activePlayerIndex = rep;
                    loadSwosContext(m_players[rep], false);
                    stepActiveWorldOneCpuEntrySilently();
                    minWeek = 0;
                    break;
                }
            }
            if (minWeek < 0) {
                updateUiFixtures();
                return m_activePlayerIndex != prevActiveIdx;
            }
            continue;
        }

        m_globalWeek = static_cast<uint16_t>(minWeek);

        // 1) Step any world whose next entry at minWeek has no human matches ([entry+6] <= 0)
        bool steppedCpu = false;
        for (const auto& kv : m_leagueWorlds) {
            const auto& lw = kv.second;
            if (!lw.initialized || lw.seasonFinished != 0)
                continue;
            uint8_t compSlot = 0;
            int remSlots = 0;
            int w = inspectNextPendingCalendarEntry(lw.calendar, lw.calStepIdx, lw.calByteOfs,
                                                    lw.calTotalEntries, compSlot, remSlots);
            if (w == minWeek && remSlots <= 0) {
                int rep = findRepPlayerInWorld(kv.first);
                if (kv.first != m_activeWorldKey) {
                    loadWorldToVm(kv.first, false);
                }
                m_activePlayerIndex = rep;
                loadSwosContext(m_players[rep], false);
                stepActiveWorldOneCpuEntrySilently();
                steppedCpu = true;
                break;
            }
        }
        if (steppedCpu)
            continue;

        // 2) Pick a world with an active human match at minWeek (preferring current active world)
        LeagueWorldKey chosenWorld{};
        bool foundWorld = false;
        uint8_t chosenCompSlot = 0;
        {
            auto curIt = m_leagueWorlds.find(m_activeWorldKey);
            if (curIt != m_leagueWorlds.end() && curIt->second.initialized &&
                curIt->second.seasonFinished == 0) {
                int remSlots = 0;
                int w = inspectNextPendingCalendarEntry(curIt->second.calendar, curIt->second.calStepIdx,
                                                        curIt->second.calByteOfs, curIt->second.calTotalEntries,
                                                        chosenCompSlot, remSlots);
                if (w == minWeek && remSlots > 0) {
                    chosenWorld = curIt->first;
                    foundWorld = true;
                }
            }
            if (!foundWorld) {
                for (const auto& kv : m_leagueWorlds) {
                    const auto& lw = kv.second;
                    if (!lw.initialized || lw.seasonFinished != 0)
                        continue;
                    int remSlots = 0;
                    int w = inspectNextPendingCalendarEntry(lw.calendar, lw.calStepIdx, lw.calByteOfs,
                                                            lw.calTotalEntries, chosenCompSlot, remSlots);
                    if (w == minWeek && remSlots > 0) {
                        chosenWorld = kv.first;
                        foundWorld = true;
                        break;
                    }
                }
            }
        }

        if (!foundWorld) {
            updateUiFixtures();
            return m_activePlayerIndex != prevActiveIdx;
        }

        if (chosenWorld != m_activeWorldKey) {
            loadWorldToVm(chosenWorld, false);
        }

        stageActiveCalendarSlotToVm();
        extern void cseg_34DA3();
        save68kRegisters();
        cseg_34DA3();
        restore68kRegisters();

        int slotOwner = findNextPlayerForCurrentCalendarSlot();
        if (slotOwner < 0) {
            // Defensive guard: entry reported remaining human slots, yet no active human team found in slot.
            int rep = findRepPlayerInWorld(chosenWorld);
            m_activePlayerIndex = rep;
            loadSwosContext(m_players[rep], false);
            stepActiveWorldOneCpuEntrySilently();
            continue;
        }

        m_activePlayerIndex = slotOwner;
        loadSwosContext(m_players[slotOwner], false);
        if (worldKeyForPlayer(m_players[slotOwner]) != chosenWorld) {
            const auto& hostLw = m_leagueWorlds[chosenWorld];
            if (chosenWorld.countryFileNo != 0xFF) {
                SwosVM::g_memByte[CareerOffsets::kCompCountryNumber] = chosenWorld.countryFileNo;
                SwosVM::g_memByte[CareerOffsets::kDsegD6CB8] = chosenWorld.countryFileNo;
            }
            *reinterpret_cast<uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6CDC]) = chosenWorld.divisionId;
            SwosVM::g_memByte[CareerOffsets::kDsegE0048] = hostLw.dsegE0048;
        }
        syncPlayersWithSelectedTeams(true);
        ensureHotseatTeamControlsInSelectedTeams();
        relinkCompetitionTeamPointers();
        syncTransferMarketPoolsForCurrentPlayer();
        updateUiFixtures();
        return m_activePlayerIndex != prevActiveIdx;
    }

    updateUiFixtures();
    return m_activePlayerIndex != prevActiveIdx;
}

void HotseatCareerManager::onBeforePlayMatchMenu()
{
    if (!isActive() || m_numPlayers <= 1 || swos.g_gameType != kGameTypeCareer || m_leagueWorlds.size() <= 1)
        return;
    if (isVmNationalSelector())
        return;

    uint16_t sFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kSeasonFinished]);
    uint16_t cFin = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kCareerFinished]);
    if ((sFin != 0 || cFin != 0) && !areAllWorldsSeasonFinished()) {
        resolveLockstepAndSelectPlayer(true);
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

    for (int i = 0; i < m_numPlayers; ++i) {
        m_players[i].matchPlayedThisTurn = false;
        m_players[i].turnCompleted = false;
    }
    m_roundCalendarIndex = *reinterpret_cast<const uint16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegDB3DE]);

    bool switched = (m_leagueWorlds.size() > 1)
        ? resolveLockstepAndSelectPlayer(true)
        : syncActivePlayerToHighlightedSlot(true);
    if (switched) {
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
        if (m_hasActiveWorld && m_leagueWorlds.size() > 1 &&
            worldKeyForPlayer(m_players[i]) != m_activeWorldKey) {
            continue;
        }

        loadSwosContext(m_players[i], false);

        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D0E]) += 1;
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D10]) += 1;
        *reinterpret_cast<int16_t *>(&SwosVM::g_memByte[CareerOffsets::kDsegD6D12]) += 1;

        save68kRegisters();
        cseg_94DB1();
        restore68kRegisters();

        saveCurrentSwosContext(m_players[i]);
    }

    for (int i = 0; i < m_numPlayers; ++i) {
        if (!m_hasActiveWorld || m_leagueWorlds.size() <= 1 ||
            worldKeyForPlayer(m_players[i]) == m_activeWorldKey) {
            m_players[i].playMatchBlink = 0;
        }
    }

    loadSwosContext(m_players[activeIdx], false);
}
