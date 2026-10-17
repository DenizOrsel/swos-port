#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include <map>

constexpr int kMaxHotseatPlayers = 4;
constexpr int kCareerTeamBufferSize = 1292;
constexpr int kCareerHistoryBufferSize = 2120;
constexpr int kUserTacticsSize = 370;

// VM memory offsets for Career mode single-instance global variables
namespace CareerOffsets {
    constexpr size_t kSelTeamNumber = 91720;            // uint16_t
    constexpr size_t kCoachOrPlayer = 91722;            // uint16_t
    constexpr size_t kCareerTeamLeague = 91724;         // uint16_t
    constexpr size_t kMrOrMs = 91734;                   // uint16_t
    constexpr size_t kManagerSexBuffer = 91736;         // 5 bytes
    constexpr size_t kManagerNameBuffer = 91741;        // 9 bytes
    constexpr size_t kManagerSurnameBuffer = 91750;     // 13 bytes
    constexpr size_t kPlNationality = 91763;            // uint8_t
    constexpr size_t kNewBalance = 91354;               // int32_t
    constexpr size_t kOldBalance = 91358;               // int32_t
    constexpr size_t kPlayerPurchases = 91362;          // int32_t
    constexpr size_t kPlayerSales = 91366;              // int32_t
    constexpr size_t kGeneralRunningCosts = 91370;      // int32_t
    constexpr size_t kPlayerWagesBill = 91374;          // int32_t
    constexpr size_t kGateReceipts = 91378;             // int32_t
    constexpr size_t kStartingBalance = 91382;          // int32_t
    constexpr size_t kCompetitionBonuses = 91390;       // int32_t
    constexpr size_t kSensisoftPlcSponsorship = 91394;  // int32_t
    constexpr size_t kYoureSacked = 91344;              // int16_t
    constexpr size_t kNumNewJobOffers = 92034;          // int16_t
    constexpr size_t kNumJobOffers = 92166;             // int16_t
    constexpr size_t kCareerTeam = 92798;               // 1292 bytes
    constexpr size_t kCareerHistory = 94096;            // 2120 bytes
    constexpr size_t kCurrentSeasonNumber = 484815;     // uint16_t
    constexpr size_t kSeasonNumberOfSquads = 99894;     // uint16_t
    constexpr size_t kUserTacticsStart = 129478;        // 6 * 370 bytes
}

struct HotseatPlayerContext {
    int16_t teamId = 0;
    std::string teamName;
    std::string managerName = "MANAGER";
    std::string managerSurname = "PLAYER";
    int16_t coachOrPlayer = 0;       // 0 = coach, 1 = player-coach
    uint8_t nationality = 8;         // default English
    int16_t mrOrMs = 0;              // 0 = Mr, 1 = Ms

    // Financials
    int32_t newBalance = 0;
    int32_t oldBalance = 0;
    int32_t startingBalance = 0;
    int32_t gateReceipts = 0;
    int32_t playerWagesBill = 0;
    int32_t generalRunningCosts = 0;
    int32_t playerSales = 0;
    int32_t playerPurchases = 0;
    int32_t competitionBonuses = 0;
    int32_t sensisoftPlcSponsorship = 0;

    // Board & Career
    int16_t youreSacked = 0;
    int16_t numNewJobOffers = 0;
    int16_t numJobOffers = 0;
    uint16_t currentSeasonNumber = 0;

    // Team and History buffers
    uint8_t careerTeam[kCareerTeamBufferSize] = {0};
    uint8_t careerHistory[kCareerHistoryBufferSize] = {0};

    // User custom tactics (USER A to F)
    uint8_t userTactics[6][kUserTacticsSize] = {{0}};

    // Turn tracking
    bool matchPlayedThisTurn = false;
    bool turnCompleted = false;
};

struct WizardPlayerInfo {
    std::string name = "MANAGER";
    std::string surname = "PLAYER";
    uint8_t nationality = 8;
    int16_t coachOrPlayer = 1;       // 1 = player-coach
    int16_t mrOrMs = 0;
};

class HotseatCareerManager {
public:
    static HotseatCareerManager& get();

    void reset();
    bool isActive() const { return m_active && m_numPlayers >= 1; }
    void setActive(bool active) { m_active = active; }
    int numPlayers() const { return m_numPlayers; }
    int activePlayerIndex() const { return m_activePlayerIndex; }
    HotseatPlayerContext& activePlayer() { return m_players[m_activePlayerIndex]; }
    const HotseatPlayerContext& activePlayer() const { return m_players[m_activePlayerIndex]; }
    HotseatPlayerContext& player(int index) { return m_players[index]; }
    const HotseatPlayerContext& player(int index) const { return m_players[index]; }

    // Setup
    void initializeHotseat(const std::vector<int16_t>& teamIds);

    // Wizard
    bool isWizardActive() const { return m_isWizardActive; }
    void setWizardActive(bool active) { m_isWizardActive = active; }
    int wizardCurrentPlayer() const { return m_wizardCurrentPlayer; }
    void setWizardCurrentPlayer(int p) { m_wizardCurrentPlayer = p; }
    int wizardTotalPlayers() const { return m_wizardTotalPlayers; }
    void setWizardTotalPlayers(int total) { m_wizardTotalPlayers = total; }
    bool wizardAdvanced() const { return m_wizardAdvanced; }
    void setWizardAdvanced(bool advanced) { m_wizardAdvanced = advanced; }
    WizardPlayerInfo& wizardPlayer(int idx) { return m_wizardPlayers[idx]; }
    const WizardPlayerInfo& wizardPlayer(int idx) const { return m_wizardPlayers[idx]; }
    void resetWizard(int totalPlayers);

    // Club Selection State
    bool isSelectingCareerTeams() const { return m_isSelectingCareerTeams; }
    void setSelectingCareerTeams(bool selecting) { m_isSelectingCareerTeams = selecting; }
    int careerNumPlayers() const { return m_careerNumPlayers; }
    void setCareerNumPlayers(int count) { m_careerNumPlayers = count; }

    struct AssignedSlotInfo {
        int slot = -1;
        int16_t globalTeamNumber = 0;
        uint16_t teamWord = 0;
        TeamFile teamFile;
    };

    void assignTeamSlot(int16_t teamId, int slot, const TeamFile *tf = nullptr);
    void unassignTeamSlot(int16_t teamId);
    void unassignAllTeamSlots();
    int getTeamSlot(int16_t teamId) const;
    int getTeamForSlot(int slot) const;
    const AssignedSlotInfo *getAssignedSlotInfo(int slot) const;
    int getNextAvailableSlot() const;
    int getAssignedSlotCount() const;
    int getTeamSlotFromEntry(const struct MenuEntry *entry) const;

    // Context Swapping
    void saveCurrentSwosContext(HotseatPlayerContext& ctx);
    void loadSwosContext(const HotseatPlayerContext& ctx);
    void switchToPlayer(int index);

    // Turn Management
    bool canEndTurn() const;
    void onMatchCompleted();
    void markPlayerMatchCompleted(int playerIndex);
    bool endTurn(); // Returns true if a new fixture round was started

    // Team and Player lookups
    int findHotseatPlayerByTeamName(const char *teamName) const;
    int findHotseatPlayerByTeamId(int16_t teamId) const;

    // Inter-Player Transfer queue
    struct TransferOffer {
        int fromPlayerIndex;
        int toPlayerIndex;
        int playerOrdinal;
        int32_t offerAmount;
        bool pending = true;
        bool accepted = false;
    };
    void queueTransferOffer(const TransferOffer& offer);
    std::vector<TransferOffer> getPendingOffersForPlayer(int playerIndex);
    void resolveTransferOffer(size_t offerIndex, bool accepted);

private:
    HotseatCareerManager() = default;

    bool m_active = false;
    int m_numPlayers = 0;
    int m_activePlayerIndex = 0;
    std::array<HotseatPlayerContext, kMaxHotseatPlayers> m_players;
    std::vector<TransferOffer> m_transferOffers;

    // Hotseat Setup Wizard & Selection
    bool m_isWizardActive = false;
    int m_wizardCurrentPlayer = 0;
    int m_wizardTotalPlayers = 1;
    bool m_wizardAdvanced = false;
    std::vector<WizardPlayerInfo> m_wizardPlayers;

    bool m_isSelectingCareerTeams = false;
    int m_careerNumPlayers = 1;
    std::map<int16_t, int> m_assignedTeamSlots;
    std::map<int, AssignedSlotInfo> m_assignedSlotInfos;
};

void startCareerMode();
void onSelect1Player();
void onSelect2Players();
void onSelect3Players();
void onSelect4Players();
void onSelectExitPlayerCount();

