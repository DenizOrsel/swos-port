#pragma once

#include "gameControlEvents.h"

struct TeamGeneralInfo;

void initFreeKickAim();
bool isFreeKickAimingActive();
bool isFreeKickAimingActiveForTeam(const TeamGeneralInfo *team);
void updateFreeKickAimInput(TeamGeneralInfo *team, GameControlEvents events);
void onFreeKickLaunched();
void applyActiveFreeKickCurl();
void drawFreeKickAimTrajectory(float xOffset, float yOffset);

bool getFreeKickAiming();
void setFreeKickAiming(bool enabled);

struct FreeKickAimTestState
{
    bool active = false;
    float aimAngle = 0.0f;
    float elevation = 0.0f;
    float curl = 0.0f;
    bool flightCurlActive = false;
};

FreeKickAimTestState getFreeKickAimTestState();
