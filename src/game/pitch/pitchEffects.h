#pragma once

#include "FixedPoint.h"

struct ReplayWeatherActor
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool slidingOrDiving = false;
};

void initPitchEffects();
void resetPitchEffects();
void updatePitchEffects();
void updatePitchEffectsFromReplay(bool hasBall, float ballX, float ballY, float ballZ,
    const ReplayWeatherActor *players, int numPlayers);

void setPitchEffectsRenderCamera(FixedPoint cameraX, FixedPoint cameraY);
void clearPitchEffectsRenderCamera();

void drawPitchEffectsGround(float xOffset, float yOffset);
void drawPitchEffectsAir(float xOffset, float yOffset);

bool getPitchWeatherEffects();
void setPitchWeatherEffects(bool enabled);

struct PitchEffectsTestState
{
    int skidMarkCount = 0;
    int particleCount = 0;
    int weatherDropCount = 0;
    int puddleRippleCount = 0;
};

PitchEffectsTestState getPitchEffectsTestState();
