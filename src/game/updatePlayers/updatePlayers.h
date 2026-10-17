#pragma once

void updatePlayers(TeamGeneralInfo *team);
int getBallDefensivePositionX();
bool isBallInGoalkeeperArea();
int getStrikeDestinationX();
int getBallQuadrantIndex();
void setBallQuadrantIndex(int quadrant);
Sprite *getLastPlayerPlayed();
void resetLastPlayerPlayed();
Sprite *getLastPlayerBeforeGoalkeeper();
void setLastPlayerBeforeGoalkeeper(Sprite *player);
void resetLastPlayerBeforeGoalkeeper();
void setPlayerDownTacklingInterval(int interval);
void setPlayerDownHeadingInterval(int interval);
