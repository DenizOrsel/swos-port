#pragma once

#include <cstdint>
#include <string>

struct GoalToasterEvent {
    int minute = 0;
    std::string scorerName;
    std::string teamName;
    std::string assisterName;
    int team1Goals = 0;
    int team2Goals = 0;
    int scorerMatchGoals = 1;
    uint8_t teamShirtColorIdx = 5;
    bool isHumanTeam = false;
    bool isHeader = false;
    bool isOutsideBox = false;
    bool isVolley = false;
    bool isPenalty = false;
    bool isFreeKick = false;
    bool isOwnGoal = false;
    bool isGoAheadGoal = false;
};

// Pure deterministic helper (also unit tested)
std::string buildOfflineScorerComment(const GoalToasterEvent& ev, uint32_t seed);

// Match lifecycle & rendering
void resetGoalToaster();
void triggerGoalToaster(const GoalToasterEvent& ev);
void updateAndDrawGoalToaster(float cameraXOffset, float cameraYOffset);
bool isGoalToasterActive();
