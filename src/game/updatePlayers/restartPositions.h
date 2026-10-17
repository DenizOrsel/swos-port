#pragma once

struct RestartPosition
{
    int16_t x;
    int16_t y;
};

using RestartPositions = std::array<RestartPosition, 11>;

// The underlying bottom/top lookup tables are indexed directly by GameState.
// A missing entry returns nullptr so normal tactical positioning can be used.
const RestartPositions *getRestartPositions(bool useBottomPositions, GameState gameState);

void patchPlayersLeavingPitchDestinations();
