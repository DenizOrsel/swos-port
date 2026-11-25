#pragma once

#include <string>

void updateCurrentPlayerName();
void drawPlayerName();
bool getCurrentBallCarrierInfo(bool& outIsTopTeam, int& outShirtNumber, std::string& outPlayerName);
#ifdef SWOS_TEST
std::pair<bool, int> getDisplayedPlayerNumberAndTeam();
#endif
