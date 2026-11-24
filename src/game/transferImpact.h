#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TeamGame;

enum class IncomingTransferTier : uint8_t {
    kSquadAddition = 0,
    kHiddenGem = 1,
    kMarqueeStar = 2,
};

struct TransferPlayerRecord {
    std::string name;
    std::string position;
    std::string marketValueStr;
    int32_t valueGbp = 0;
    uint8_t priceTier = 0;
    int totalSkills = 0;
    int maxSingleSkill = 0;
    int shirtNumber = 0;
    int careerSlot = -1;
};

struct PendingTransferMatchEffect {
    bool active = false;
    std::string label;
    int targetShirtNumber = 0;
    std::string targetPlayerName;
    int8_t speedDelta = 0;
    int8_t finishingDelta = 0;
    int8_t passingDelta = 0;
    int8_t controlDelta = 0;
};

// --- Pure Deterministic Helpers (directly unit tested) ---
int32_t getMarqueeSigningThresholdGbp(int divisionTier);
IncomingTransferTier classifyIncomingTransfer(
    int32_t marketValueGbp, int divisionTier, int totalSkills, int maxSingleSkill, int32_t avgSquadValueGbp);
int32_t computeMarqueeShirtSalesWindfall(int32_t marketValueGbp, int commercialLevel);
void applyPendingTransferEffectToTeam(TeamGame& myTeam, const PendingTransferMatchEffect& eff);

// --- Career & Match Lifecycle Hooks ---
void resetTransferImpactState();

// Called whenever checkCareerSquadTransfersLlm detects real incoming/outgoing/swap moves.
// Returns a concise impact summary block (empty if Star Sale modal already handled display).
std::string handleCareerTransferImpact(
    const std::string& clubName,
    const std::vector<TransferPlayerRecord>& added,
    const std::vector<TransferPlayerRecord>& removed,
    const std::vector<TransferPlayerRecord>& prevSquad,
    bool& outHandledModalAlready);

// Replaces random passive weekly whispers with rare actionable Rival Tap-Up / Agent Alert
void onCareerWeeklyRivalTapUpEvent();

// Match hooks: apply Debut Adrenaline / tap-up sulk & tick down Reinvest Promise
void applyPendingTransferEffectsForMatch(TeamGame *topTeam, TeamGame *bottomTeam);
void onCareerMatchFinishedTransferImpact();

// Live Stream & Newspaper context
std::string formatTransferImpactContextForTelemetry();
