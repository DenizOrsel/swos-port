#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct MatchTelemetryEvent {
    int minute = 0;
    std::string type;
    std::string team;
    std::string player;
    std::string detail;
};

struct MatchTelemetrySnapshot {
    int minute = 0;
    int half = 1;
    bool playingPenalties = false;
    std::string timePressureTag;

    std::string competition;
    std::string pitchCondition;

    std::string team1;
    std::string team2;
    std::string coach1;
    std::string coach2;
    std::string keeper1;
    std::string keeper2;
    std::string players1;
    std::string players2;
    std::string bench1;
    std::string bench2;
    bool team1Human = false;
    bool team2Human = false;

    int goals1 = 0;
    int goals2 = 0;
    int htGoals1 = 0;
    int htGoals2 = 0;
    int penGoals1 = 0;
    int penGoals2 = 0;
    int longRangeGoals1 = 0;
    int longRangeGoals2 = 0;
    int headerGoals1 = 0;
    int headerGoals2 = 0;

    int shots1 = 0;
    int shots2 = 0;
    int onTarget1 = 0;
    int onTarget2 = 0;
    int possession1 = 50;
    int possession2 = 50;
    int corners1 = 0;
    int corners2 = 0;
    int fouls1 = 0;
    int fouls2 = 0;

    int team1AttackZonePct = 33;
    int midfieldZonePct = 34;
    int team2AttackZonePct = 33;

    std::string careerRecordSummary;
    std::string managerCareerSummary;
    std::string topScorersSummary;
    std::string upcomingFixturesSummary;
    std::string activeSponsorChallengeSummary;
    std::string pressCharismaSummary;
    std::string stadiumFacilitiesSummary;
    std::vector<MatchTelemetryEvent> recentEvents;
};

void resetMatchTelemetry();
void updateMatchTelemetry();
void recordTelemetryMatchEvent(const char *eventType, bool isGoalEvent = false);
void finalizeMatchTelemetry();

MatchTelemetrySnapshot captureMatchTelemetrySnapshot();
std::pair<int, int> getHalfTimeTelemetryGoals();
std::pair<int, int> getLongRangeTelemetryGoals();
std::pair<int, int> getHeaderTelemetryGoals();
std::pair<int, int> getMatchMaxGoalDeficits();
std::pair<bool, bool> getMatchEarlyRedCards();
std::string formatTelemetryPromptBlock(const MatchTelemetrySnapshot& snap);
std::string computeTimePressureTag(int minute, int goals1, int goals2, bool playingPenalties);
