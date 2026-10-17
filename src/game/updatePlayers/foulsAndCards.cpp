// Implementation fragment included by updatePlayers.cpp; do not compile separately.

// Tests whether a tackling player has fouled the opponent's controlled player.
//
// SWOS only considers the tackle when the two players are almost touching. A
// non-goalkeeper victim is knocked down immediately; whether play is stopped then
// depends on proximity to the ball and the tackle direction/state. Card selection
// is a separate step after the foul has been recorded.
static void playerTacklingTestFoul(TeamGeneralInfo& tacklingTeam, Sprite& tacklingPlayer)
{
    constexpr int kMaximumSquaredCollisionDistance = 32;
    constexpr uint32_t kMaximumVictimBallDistance = 800;
    constexpr int kDirectCardRandomThreshold = 32;

    assert(tacklingTeam.opponentTeam);
    auto& opponentTeam = *tacklingTeam.opponentTeam;
    if (!opponentTeam.controlledPlayer)
        return;

    auto& fouledPlayer = *opponentTeam.controlledPlayer;
    auto squaredDistance = [](int x1, int y1, int x2, int y2) {
        int deltaX = x1 - x2;
        int deltaY = y1 - y2;
        return deltaX * deltaX + deltaY * deltaY;
    };

    int playersSquaredDistance = squaredDistance(tacklingPlayer.x.whole(), tacklingPlayer.y.whole(),
        fouledPlayer.x.whole(), fouledPlayer.y.whole());
    if (playersSquaredDistance > kMaximumSquaredCollisionDistance)
        return;

    // Goalkeepers cannot be fouled through this path. The collision only drains
    // most of the tackler's remaining speed, as in the original game.
    if (fouledPlayer.isGoalkeeper()) {
        tacklingPlayer.speed = static_cast<uint16_t>(tacklingPlayer.speed) / 4 | 1;
        return;
    }

    // Ignore players already beyond the playable pitch. This check happens before
    // the victim is knocked down and before the tackler is slowed.
    int foulX = fouledPlayer.x.whole();
    int foulY = fouledPlayer.y.whole();
    if (foulX < kLeftThrowInLine || foulX > kRightThrowInLine ||
        foulY < kTopPitchLine || foulY > kBottomPitchLine)
        return;

    tacklingPlayer.speed = static_cast<uint16_t>(tacklingPlayer.speed) / 4 | 1;
    playerTackled(opponentTeam, fouledPlayer);

    // A collision away from the ball is not called as a foul. A good tackle near
    // the ball gets the dangerous-play comment, but play continues.
    if (fouledPlayer.ballDistance > kMaximumVictimBallDistance)
        return;

    if (tacklingPlayer.tackleState == TackleState::kGoodTackle) {
        playDangerousPlayComment();
        return;
    }

    if (tacklingPlayer.tackleState != TackleState::kStarted) {
        int directionDifference =
            static_cast<int>(tacklingPlayer.direction) - static_cast<int>(fouledPlayer.direction);
        if (directionDifference < -1 || directionDifference > 1)
            return;
    }

    assert(tacklingTeam.teamStatsPtr);
    tacklingTeam.teamStatsPtr->foulsConceded++;

    enum class CardDecision
    {
        kNone,
        kYellow,
        kRed,
    };
    CardDecision cardDecision = CardDecision::kNone;

    if (!swos.g_trainingGame) {
        bool topTeamCommittedFoul = &tacklingTeam == &swos.topTeamData;
        bool inPenaltyArea = foulX >= kPenaltyAreaLeft && foulX <= kPenaltyAreaRight &&
            (topTeamCommittedFoul ? foulY <= kTopPenaltyAreaBottom : foulY >= kBottomPenaltyAreaTop);

        bool victimClosestToGoal = false;
        if (!inPenaltyArea) {
            // Outside the penalty area SWOS checks for a last-defender situation.
            // The victim starts as the closest candidate to the fouling team's own
            // goal. If no active outfield teammate (apart from the tackler) is at
            // least as close, the tackle is treated as denying a clear goal chance.
            int goalY = topTeamCommittedFoul ? kTopPitchLine : kBottomPitchLine;
            Sprite *closestPlayer = &fouledPlayer;
            int closestSquaredDistance = squaredDistance(
                foulX, foulY, kPitchCenterX, goalY);

            for (int i = 0; i < kNumPlayersInLineup; i++) {
                auto candidate = tacklingTeam.players[i];
                assert(candidate);
                if (!candidate->sentAway && !candidate->isGoalkeeper() && candidate != &tacklingPlayer) {
                    int candidateSquaredDistance = squaredDistance(candidate->x.whole(), candidate->y.whole(),
                        kPitchCenterX, goalY);
                    if (candidateSquaredDistance <= closestSquaredDistance) {
                        closestSquaredDistance = candidateSquaredDistance;
                        closestPlayer = candidate;
                    }
                }
            }

            victimClosestToGoal = closestPlayer == &fouledPlayer;
        }

        if (victimClosestToGoal) {
            // Last-defender fouls strongly favor a direct red. This branch bypasses
            // the ordinary card-frequency gate, matching the original routine.
            cardDecision = SWOS::rand() < kDirectCardRandomThreshold ?
                CardDecision::kYellow : CardDecision::kRed;
        } else {
            int cardChanceRoll = (swos.currentGameTick & 0x1e) >> 1;
            if (cardChanceRoll < swos.playerCardChance) {
                cardDecision = SWOS::rand() < kDirectCardRandomThreshold ?
                    CardDecision::kRed : CardDecision::kYellow;
            }
        }
    }

    bool cardGiven = false;
    if (cardDecision == CardDecision::kYellow)
        cardGiven = tryBookingThePlayer(tacklingTeam, tacklingPlayer);
    else if (cardDecision == CardDecision::kRed)
        cardGiven = trySendingOffThePlayer(tacklingTeam, tacklingPlayer);

    resolveFoulAsPenaltyFreeKickOrOrdinaryFoul(tacklingTeam, fouledPlayer);
    if (cardGiven)
        activateReferee();
}

// Convert a detected foul into the appropriate restart. Penalty-area fouls
// become penalties; central attacking areas select one of seven free-kick
// lanes; the remainder use the ordinary foul state and original ball position.
static void resolveFoulAsPenaltyFreeKickOrOrdinaryFoul(const TeamGeneralInfo& foulingTeam, const Sprite& fouledPlayer)
{
    constexpr int kUpperFreeKickAreaBottom = 331;
    constexpr int kLowerFreeKickAreaTop = 567;

    constexpr int kFreeKickBoundaries[] = { 153, 261, 309, 362, 410, 518 };
    constexpr GameState kTopTeamFreeKickStates[] = {
        GameState::kFreeKickOuterLeft,
        GameState::kFreeKickMiddleLeft,
        GameState::kFreeKickInnerLeft,
        GameState::kFreeKickCenter,
        GameState::kFreeKickInnerRight,
        GameState::kFreeKickMiddleRight,
        GameState::kFreeKickOuterRight,
    };
    constexpr GameState kBottomTeamFreeKickStates[] = {
        GameState::kFreeKickOuterRight,
        GameState::kFreeKickMiddleRight,
        GameState::kFreeKickInnerRight,
        GameState::kFreeKickCenter,
        GameState::kFreeKickInnerLeft,
        GameState::kFreeKickMiddleLeft,
        GameState::kFreeKickOuterLeft,
    };

    if (swos.gameStatePl == GameState::kStopped)
        return;

    playFoulWhistleSample();

    int foulX = fouledPlayer.x.whole();
    int foulY = fouledPlayer.y.whole();
    bool topTeamCommittedFoul = &foulingTeam == &swos.topTeamData;
    swos.cameraDirection = topTeamCommittedFoul ? Direction::kTop : Direction::kBottom;

    bool insidePenaltyAreaHorizontally = foulX >= kPenaltyAreaLeft && foulX <= kPenaltyAreaRight;
    bool penalty = insidePenaltyAreaHorizontally &&
        (topTeamCommittedFoul ? foulY <= kTopPenaltyAreaBottom : foulY >= kBottomPenaltyAreaTop);

    if (penalty) {
        swos.gameState = GameState::kPenalty;
        swos.breakCameraMode = CameraBreakMode::kInactive;
        swos.foulXCoordinate = kPitchCenterX;
        swos.foulYCoordinate = topTeamCommittedFoul ? kTopPenaltySpotY : kBottomPenaltySpotY;
        swos.playerTurnFlags = topTeamCommittedFoul ?
            makeDirectionMask(Direction::kTop, Direction::kTopRight, Direction::kTopLeft) :
            makeDirectionMask(Direction::kBottomRight, Direction::kBottom, Direction::kBottomLeft);
        playPenaltyComment();
    } else {
        playFoulComment();

        bool inFreeKickArea = topTeamCommittedFoul ?
            foulY >= kTopPenaltyAreaBottom && foulY < kUpperFreeKickAreaBottom :
            foulY >= kLowerFreeKickAreaTop && foulY <= kBottomPenaltyAreaTop;

        if (inFreeKickArea) {
            size_t freeKickZone = 0;
            while (freeKickZone < std::size(kFreeKickBoundaries) &&
                foulX >= kFreeKickBoundaries[freeKickZone])
                freeKickZone++;

            const auto& freeKickStates = topTeamCommittedFoul ?
                kTopTeamFreeKickStates : kBottomTeamFreeKickStates;
            swos.gameState = freeKickStates[freeKickZone];
        } else {
            swos.gameState = GameState::kFoul;
        }

        swos.breakCameraMode = CameraBreakMode::kInactive;
        swos.playerTurnFlags = makeDirectionMask(Direction::kTop, Direction::kTopRight, Direction::kRight,
            Direction::kBottomRight, Direction::kBottom, Direction::kBottomLeft, Direction::kLeft,
            Direction::kTopLeft);
        swos.foulXCoordinate = foulX;
        swos.foulYCoordinate = foulY;
    }

    swos.gameStatePl = GameState::kStopped;
    swos.lastTeamPlayedBeforeBreak = foulingTeam.opponentTeam;
    swos.stoppageTimerTotal = 0;
    swos.stoppageTimerActive = 0;
    stopAllPlayers();
    swos.cameraXVelocity = 0;
    swos.cameraYVelocity = 0;
}

// Attempts to book a player. A second yellow is also a sending-off and consumes
// one of the team's remaining removable-player slots. Original SWOS normally
// suppresses bookings for fully CPU-controlled teams; the special PLG mode
// permits their first yellow but not a second one.
static bool tryBookingThePlayer(TeamGeneralInfo& team, Sprite& player)
{
    bool controlledByPlayer = team.playerNumber || team.playerCoachNumber;
    if (!controlledByPlayer && (!swos.useCompetitionPlayerStatusRules || player.cards))
        return false;

    auto& playersAvailable = team.teamNumber == 1 ?
        swos.team1NumAllowedInjuries : swos.team2NumAllowedInjuries;
    if (player.cards && !playersAvailable)
        return false;

    assert(team.inGameTeamPtr);
    assert(team.teamStatsPtr);
    assert(player.playerOrdinal >= 1 && player.playerOrdinal <= std::size(team.inGameTeamPtr->players));
    assert(player.cards <= 1);

    auto& playerInfo = team.inGameTeamPtr->players[player.playerOrdinal - 1];
    if (swos.useCompetitionPlayerStatusRules) {
        playerInfo.cards = playerInfo.cards ? 3 : 1;
    } else {
        size_t cardIndex = player.cards + 1;
        const auto& cardValues = playerInfo.previousCards ?
            kPersistentCardValuesWithPreviousCards : kPersistentCardValues;
        playerInfo.cards = cardValues[cardIndex];
    }

    player.cards++;
    swos.lastTeamBooked = &team;
    swos.bookedPlayer = &player;
    swos.refTimer = 0;

    auto& statistics = *team.teamStatsPtr;
    if (player.cards == 1) {
        swos.whichCard = kYellowCard;
        statistics.bookings++;
    } else {
        swos.whichCard = kSecondYellowCard;
        statistics.bookings--;
        statistics.sendingsOff++;
        playersAvailable--;
    }

    return true;
}

// Attempts to issue a direct red card. Original SWOS suppresses direct reds for
// fully CPU-controlled teams and when the team cannot lose another player.
static bool trySendingOffThePlayer(TeamGeneralInfo& team, Sprite& player)
{
    bool controlledByPlayer = team.playerNumber || team.playerCoachNumber;
    auto& playersAvailable = team.teamNumber == 1 ?
        swos.team1NumAllowedInjuries : swos.team2NumAllowedInjuries;
    if (!controlledByPlayer || !playersAvailable)
        return false;

    assert(team.inGameTeamPtr);
    assert(team.teamStatsPtr);
    assert(player.playerOrdinal >= 1 && player.playerOrdinal <= std::size(team.inGameTeamPtr->players));
    assert(player.cards <= 1);

    auto& playerInfo = team.inGameTeamPtr->players[player.playerOrdinal - 1];
    if (swos.useCompetitionPlayerStatusRules) {
        playerInfo.cards = 3;
    } else {
        size_t cardIndex = (player.cards ^ 1) + 3;
        const auto& cardValues = playerInfo.previousCards ?
            kPersistentCardValuesWithPreviousCards : kPersistentCardValues;
        playerInfo.cards = cardValues[cardIndex];
    }

    swos.lastTeamBooked = &team;
    swos.bookedPlayer = &player;
    swos.refTimer = 0;
    swos.whichCard = kRedCard;

    auto& statistics = *team.teamStatsPtr;
    if (player.cards == 1)
        statistics.bookings--;

    statistics.sendingsOff++;
    playersAvailable--;

    return true;
}

