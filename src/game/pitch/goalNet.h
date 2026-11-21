#pragma once

#include "FixedPoint.h"

struct ReplayNetActor
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

void initGoalNets();
void resetGoalNets();
void updateGoalNets();
void updateGoalNetsFromReplay(bool hasBall, float ballX, float ballY, float ballZ,
    const ReplayNetActor *players, int numPlayers);
void notifyGoalNetBallImpact(int goalIndex, float hitX, float hitY, float hitZ,
    float velX, float velY, float velZ);

void setGoalNetRenderCamera(FixedPoint cameraX, FixedPoint cameraY);
void clearGoalNetRenderCamera();

void drawTopGoalBackNet(float xOffset, float yOffset);
void drawTopGoalOverlayNet(float xOffset, float yOffset);
void drawBottomGoalBackNet(float xOffset, float yOffset);
void drawBottomGoalOverlayNet(float xOffset, float yOffset);

bool getPhysicsGoalNets();
void setPhysicsGoalNets(bool enabled);

struct GoalNetTestState
{
    int totalNodes = 0;
    int pinnedNodes = 0;
    float maxDisplacement = 0.0f;
    float totalKineticEnergy = 0.0f;
};

GoalNetTestState getGoalNetTestState(int goalIndex);
