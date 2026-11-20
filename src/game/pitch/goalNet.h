#pragma once

void initGoalNets();
void updateGoalNets();
void notifyGoalNetBallImpact(int goalIndex, float hitX, float hitY, float hitZ,
    float velX, float velY, float velZ);

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
