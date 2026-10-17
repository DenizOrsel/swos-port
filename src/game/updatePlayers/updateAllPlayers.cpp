// Implementation fragment included by updatePlayers.cpp; do not compile separately.

// State handlers describe how the shared per-player tail should proceed. Most
// states are finalized normally; kStop first pins the destination to the
// current position, while the scheduling results select increasingly general
// off-ball setup paths. kSkip deliberately bypasses final movement processing.
enum class PlayerUpdateResult
{
    kFinalize,
    kStop,
    kSkip,
    kControlled,
    kPassReceiver,
    kSchedule,
    kCommonSchedule,
};

static PlayerUpdateResult updateGoalkeeper(TeamGeneralInfo& team, Sprite& player);

// Stop future movement without changing the current fixed-point position.
static void stopPlayer(Sprite& player)
{
    player.destX = player.x.whole();
    player.destY = player.y.whole();
}

// Move one step in the selected eight-way direction, unless that direction
// would take the player farther beyond the extended pitch boundary.
static void setControlledPlayerDestination(const TeamGeneralInfo& team, Sprite& player)
{
    constexpr auto kLeftwardDirections = makeDirectionMask(
        Direction::kBottomLeft, Direction::kLeft, Direction::kTopLeft);
    constexpr auto kRightwardDirections = makeDirectionMask(
        Direction::kTopRight, Direction::kRight, Direction::kBottomRight);
    constexpr auto kUpwardDirections = makeDirectionMask(
        Direction::kTopLeft, Direction::kTop, Direction::kTopRight);
    constexpr auto kDownwardDirections = makeDirectionMask(
        Direction::kBottomRight, Direction::kBottom, Direction::kBottomLeft);

    DirectionMask blockedDirections = 0;
    constexpr int kPlayerBoundaryMargin = 2;
    if (player.x.whole() < kLeftThrowInLine - kPlayerBoundaryMargin)
        blockedDirections |= kLeftwardDirections;
    if (player.x.whole() > kRightThrowInLine + kPlayerBoundaryMargin)
        blockedDirections |= kRightwardDirections;
    if (player.y.whole() < kTopPitchLine - kPlayerBoundaryMargin)
        blockedDirections |= kUpwardDirections;
    if (player.y.whole() > kBottomPitchLine + kPlayerBoundaryMargin)
        blockedDirections |= kDownwardDirections;

    int directionIndex = static_cast<int>(team.currentAllowedDirection);
    assert(directionIndex >= 0 && directionIndex < 8);
    if (blockedDirections & (1u << directionIndex)) {
        stopPlayer(player);
    } else {
        const auto& destination = getDefaultBallDestinations()[directionIndex];
        player.destX = player.x.whole() + destination.x;
        player.destY = player.y.whole() + destination.y;
    }
}

// Test the conditions shared by pass and shot input. Outfield players must be
// almost touching a ground ball; goalkeepers retain the original looser rule.
static bool canControlledPlayerKick(const TeamGeneralInfo& team, const Sprite& player)
{
    if (!hasDirection(team.currentAllowedDirection) || (!team.plVeryCloseToBall && !team.plCloseToBall))
        return false;

    if (swos.gameStatePl != GameState::kInProgress &&
        !isRestartTurnDirectionAllowed(swos.playerTurnFlags, team.currentAllowedDirection))
        return false;

    return player.isGoalkeeper() || team.ballLessEqual4;
}

// Commit a pass or shot and resume normal play. This also releases direct
// control so the kick animation and pass-target logic can take over.
static void beginControlledPlayerKick(TeamGeneralInfo& team, Sprite& player, bool pass)
{
    swos.lastTeamPlayed = &team;
    m_lastPlayerPlayed = &player;
    swos.penalty = false;
    swos.playerHadBall = false;

    if (pass) {
        swos.ballSprite.z.setWhole(0);
        doPass(team, player);
    } else {
        playerKickingBall(team, player);
        team.shooting = true;
    }

    team.lastHeadingTacklingPlayer = &player;
    if (swos.gameState == GameState::kPenalty)
        swos.penalty = true;

    swos.gameStatePl = GameState::kInProgress;
    swos.gameState = GameState::kInProgress;
    team.ballInPlay = true;
    team.ballOutOfPlay = true;
    team.opponentTeam->ballInPlay = true;
    team.opponentTeam->ballOutOfPlay = true;
    team.opponentTeam->spinTimer = static_cast<uint16_t>(-1);
    team.controlledPlayer.reset();
    team.passingKickingPlayer = &player;
    team.passKickTimer = kPassKickTimerDuration;
    team.ballCanBeControlled = false;
    team.opponentTeam->passingToPlayer = false;
    team.opponentTeam->passingKickingPlayer.reset();
}

// Process one frame of human or CPU controls for the selected player. The
// order is significant: restart handling, ball acquisition, kick input,
// tackles, headers, and finally ordinary directional movement.
static PlayerUpdateResult updateControlledPlayer(TeamGeneralInfo& team, Sprite& player)
{
    if (swos.playingPenalties && swos.gameState != GameState::kPenaltyShootout)
        return PlayerUpdateResult::kFinalize;

    if (!team.playerNumber)
        updateCpuPlayerControls(team);

    team.allowedDirections = static_cast<Direction>(team.playerHasBall);
    team.playerHasBall = false;
    if (team.shooting && !team.firePressed)
        team.shooting = false;

    if (swos.gameStatePl != GameState::kInProgress) {
        if (!prepareControlledPlayerForBreak(team, player))
            return PlayerUpdateResult::kFinalize;

        handleControlledPlayerBreakInput(team);
        if (!isRestartTurnDirectionAllowed(swos.playerTurnFlags, team.currentAllowedDirection)) {
            team.currentAllowedDirection = player.direction;
        } else {
            player.direction = team.currentAllowedDirection;
            updatePlayerWithBall(player);
        }
    }

    if (player.z.whole())
        return PlayerUpdateResult::kFinalize;

    if (!hasDirection(team.currentAllowedDirection))
        stopPlayer(player);

    bool testFire = swos.gameStatePl != GameState::kInProgress || team.quickFire ||
        team.normalFire || team.shooting;
    if (!testFire) {
        if (team.plVeryCloseToBall || team.plCloseToBall) {
            if (!static_cast<uint16_t>(team.allowedDirections))
                team.ballDirectionChangeTimer = 0;
            team.playerHasBall = true;
            team.opponentTeam->spinTimer = static_cast<uint16_t>(-1);
        }

        if (!hasDirection(team.currentAllowedDirection)) {
            tryControlledPlayerStaticHeader(team, player);
            return PlayerUpdateResult::kFinalize;
        }

        if (!team.opponentTeam->playerHasBall)
            team.wonTheBallTimer = 0;

        if (team.wonTheBallTimer) {
            setControlledPlayerDestination(team, player);
            return PlayerUpdateResult::kFinalize;
        }

        if (team.plVeryCloseToBall && !team.ballAbove17 && !team.ball12To17) {
            if (!team.prevPlVeryCloseToBall || team.currentAllowedDirection != team.ballControllingDirection)
                team.ballCanBeControlled = 0;

            team.ballCanBeControlled++;
            team.ballControllingDirection = team.currentAllowedDirection;
            swos.lastTeamPlayed = &team;
            m_lastPlayerPlayed = &player;
            swos.penalty = false;
            swos.playerHadBall = team.opponentTeam->playerHasBall;
            calculateIfPlayerWinsBall(team, player, team.currentAllowedDirection);
            team.opponentTeam->spinTimer = static_cast<uint16_t>(-1);
            team.opponentTeam->passingToPlayer = false;
            team.opponentTeam->passingKickingPlayer.reset();
            setControlledPlayerDestination(team, player);
            return PlayerUpdateResult::kFinalize;
        }
    }

    if (team.quickFire && canControlledPlayerKick(team, player)) {
        beginControlledPlayerKick(team, player, true);
        return PlayerUpdateResult::kStop;
    }

    if (team.normalFire && canControlledPlayerKick(team, player)) {
        beginControlledPlayerKick(team, player, false);
        return PlayerUpdateResult::kStop;
    }

    bool canTackle = !player.isGoalkeeper() && team.fireThisFrame && hasDirection(team.currentAllowedDirection) &&
        team.plNotFarFromBall && (team.ballLessEqual4 || team.ball4To8) &&
        (!team.passToPlayerPtr || team.passToPlayerPtr->ballDistance >= player.ballDistance);
    if (canTackle) {
        team.headerOrTackle = true;
        playerBeginTackling(team, player, team.currentAllowedDirection);
        team.ballCanBeControlled = false;
        return PlayerUpdateResult::kFinalize;
    }

    bool canHead = !player.isGoalkeeper() && team.fireThisFrame && hasDirection(team.currentAllowedDirection) &&
        (team.plVeryCloseToBall || team.plCloseToBall || team.plNotFarFromBall) &&
        (team.ball8To12 || team.ball12To17 || team.ballAbove17);
    if (canHead) {
        team.headerOrTackle = true;
        playerAttemptingJumpHeader(player, team.currentAllowedDirection);
        team.lastHeadingTacklingPlayer = &player;
        team.ballCanBeControlled = false;
        return PlayerUpdateResult::kFinalize;
    }

    if (swos.gameStatePl == GameState::kInProgress && hasDirection(team.currentAllowedDirection))
        setControlledPlayerDestination(team, player);

    return PlayerUpdateResult::kFinalize;
}

// Keep a thrower facing an allowed restart direction, advance the chosen short
// or long throw animation, and release the ball at its contact frame.
static PlayerUpdateResult updateThrowInPlayer(TeamGeneralInfo& team, Sprite& player)
{
    constexpr int kThrowInBallHeight = 12;
    constexpr int kThrowInReleaseTimer = 18;
    constexpr int kShortThrowInDuration = 20;
    constexpr int kLongThrowInDuration = 25;

    if (!team.playerNumber)
        updateCpuPlayerControls(team);

    if (!isRestartTurnDirectionAllowed(swos.playerTurnFlags, player.direction))
        player.direction = swos.cameraDirection;

    if (!isRestartTurnDirectionAllowed(swos.playerTurnFlags, player.direction)) {
        int directionIndex = static_cast<int>(Direction::kNumDirections) - 1;
        while (directionIndex >= 0 &&
            !(swos.playerTurnFlags & makeDirectionMask(static_cast<Direction>(directionIndex))))
            directionIndex--;
        if (directionIndex < 0)
            directionIndex = static_cast<int>(Direction::kLowestDirection);

        swos.cameraDirection = static_cast<Direction>(directionIndex);
        player.direction = swos.cameraDirection;
    }

    bool abortThrowIn = swos.g_substituteInProgress && swos.substitutedPlSprite == &player;
    abortThrowIn |= swos.gameState < GameState::kThrowInForwardRight || swos.gameState > GameState::kThrowInBackLeft;
    if (abortThrowIn) {
        swos.hideBall = false;
        player.state = PlayerState::kNormal;
        setPlayerAnimationTable(player, getPlayerNormalStandingAnimTable());
        return PlayerUpdateResult::kFinalize;
    }

    if (player.playerDownTimer) {
        if (!--player.playerDownTimer) {
            player.state = PlayerState::kNormal;
            setPlayerAnimationTable(player, getPlayerNormalStandingAnimTable());
            return PlayerUpdateResult::kFinalize;
        }
        if (player.playerDownTimer != kThrowInReleaseTimer)
            return PlayerUpdateResult::kFinalize;

        if (!swos.g_inSubstitutesMenu) {
            swos.ballSprite.z.setWhole(kThrowInBallHeight);
            swos.hideBall = false;
            takeThrowIn(team, player, throwInUsesPass());
            return PlayerUpdateResult::kFinalize;
        }

        setPlayerAnimationTable(player, getAboutToThrowInAnimTable());
        player.state = PlayerState::kThrowIn;
        player.playerDownTimer = 0;
    }

    handleControlledPlayerBreakInput(team);
    Direction direction = team.currentAllowedDirection;
    if (!isRestartTurnDirectionAllowed(swos.playerTurnFlags, direction)) {
        direction = player.direction;
        team.currentAllowedDirection = direction;
    } else if (direction != player.direction) {
        player.direction = direction;
        setThrowInPlayerCoordinates(player);
        setPlayerAnimationTable(player, getAboutToThrowInAnimTable());
        player.state = PlayerState::kThrowIn;
        player.playerDownTimer = 0;
    }

    if (team.quickFire && isRestartTurnDirectionAllowed(swos.playerTurnFlags, direction)) {
        setThrowInUsesPass(true);
        setPlayerAnimationTable(player, getThrowInShortAnimTable());
        player.playerDownTimer = kShortThrowInDuration;
    } else if (team.normalFire && isRestartTurnDirectionAllowed(swos.playerTurnFlags, direction)) {
        setThrowInUsesPass(false);
        setPlayerAnimationTable(player, getThrowInLongAnimTable());
        player.playerDownTimer = kLongThrowInDuration;
    }
    return PlayerUpdateResult::kFinalize;
}

// Advance a sliding tackle, applying ground and out-of-pitch friction. While
// the player is moving, resolve either ball contact or a possible foul.
static PlayerUpdateResult updateTacklingPlayer(TeamGeneralInfo& team, Sprite& player)
{
    if (static_cast<int16_t>(player.tacklingTimer) >= 0)
        player.tacklingTimer++;
    if (!--player.playerDownTimer) {
        player.state = PlayerState::kNormal;
        setPlayerAnimationTable(player, getPlayerNormalStandingAnimTable());
        return PlayerUpdateResult::kStop;
    }

    if (static_cast<int16_t>(player.tacklingTimer) >= 0 && team.playerNumber && !team.firePressed) {
        player.tacklingTimer = -static_cast<int16_t>(player.tacklingTimer);
        if (static_cast<int16_t>(player.tacklingTimer) >= -2)
            player.tacklingTimer = static_cast<uint16_t>(-1);
    }

    if (!player.speed)
        return PlayerUpdateResult::kFinalize;
    player.speed -= kPlayerGroundFriction;
    if (player.speed <= 0) {
        player.speed = 0;
        setPlayerDowntimeAfterTackle(team, player);
        return PlayerUpdateResult::kFinalize;
    }

    constexpr int kTackleBoundaryMargin = 8;
    constexpr int kTackleGoalMouthLeftX = 275;
    constexpr int kTackleGoalMouthRightX = 396;

    bool outsidePitchX = player.x.whole() < kLeftThrowInLine - kTackleBoundaryMargin ||
        player.x.whole() > kRightThrowInLine + kTackleBoundaryMargin;
    bool outsidePitchY = player.y.whole() < kTopPitchLine || player.y.whole() > kBottomPitchLine;
    if (outsidePitchX) {
        player.speed -= static_cast<uint16_t>(player.speed) >> 3;
        return PlayerUpdateResult::kFinalize;
    }
    if (outsidePitchY) {
        bool withinGoalMouth = player.x.whole() >= kTackleGoalMouthLeftX &&
            player.x.whole() <= kTackleGoalMouthRightX;
        if (withinGoalMouth)
            player.speed = (static_cast<uint16_t>(player.speed) >> 4) | 1;
        else if (player.y.whole() < kTopPitchLine - kTackleBoundaryMargin ||
            player.y.whole() > kBottomPitchLine + kTackleBoundaryMargin)
            player.speed -= static_cast<uint16_t>(player.speed) >> 3;
        return PlayerUpdateResult::kFinalize;
    }

    bool tackledBall = swos.gameStatePl == GameState::kInProgress &&
        player.tackleState == TackleState::kStarted && player.ballDistance <= 64 &&
        (team.ballLessEqual4 || team.ball4To8);
    if (tackledBall) {
        swos.lastTeamPlayed = &team;
        m_lastPlayerPlayed = &player;
        swos.penalty = false;
        swos.playerHadBall = team.opponentTeam->playerHasBall;
        completeSuccessfulTackle(team, player);
    } else if (player.speed >= 1.0_speed) {
        playerTacklingTestFoul(team, player);
    }
    return PlayerUpdateResult::kFinalize;
}

// Advance a flying header and translate the movement/boundary helper results
// into the common player-update actions.
static PlayerUpdateResult updateJumpHeadingPlayer(TeamGeneralInfo& team, Sprite& player)
{
    switch (updateJumpHeaderMovement(player)) {
    case JumpHeaderMovementResult::kStopPlayer:
        return PlayerUpdateResult::kStop;
    case JumpHeaderMovementResult::kUpdatePosition:
        return PlayerUpdateResult::kFinalize;
    case JumpHeaderMovementResult::kCheckPitchBounds:
        break;
    }

    switch (checkJumpHeaderBoundary(player)) {
    case JumpHeaderBoundaryResult::kSlowDown: {
        uint16_t speedReduction = static_cast<uint16_t>(player.speed) >> 2;
        player.speed -= speedReduction;
        player.speed -= speedReduction >> 1;
        break;
    }
    case JumpHeaderBoundaryResult::kFinish:
        player.speed = 0;
        player.playerDownTimer = 0;
        restoreNormalPlayerState(player);
        return PlayerUpdateResult::kStop;
    case JumpHeaderBoundaryResult::kTryHeadingBall:
        tryCompletingJumpHeader(team, player);
        break;
    }
    return PlayerUpdateResult::kFinalize;
}

// Guide the nominated receiver toward the pass, transfer control when the ball
// is reachable, or cancel the pass when a goalkeeper/goal-out condition wins.
static PlayerUpdateResult updatePassReceiver(TeamGeneralInfo& team, Sprite& player)
{
    bool outsidePitch = player.x.whole() < kLeftThrowInLine || player.x.whole() > kRightThrowInLine ||
        player.y.whole() < kTopPitchLine || player.y.whole() > kBottomPitchLine;
    if (outsidePitch) {
        team.passingToPlayer = false;
        team.passingBall = false;
        return PlayerUpdateResult::kStop;
    }

    updateHumanPassAssistTarget(team, player);
    if (!team.playerNumber)
        cpuPlayerAttemptTackle(team, player);

    if (swos.gameStatePl != GameState::kInProgress) {
        if (swos.lastTeamPlayedBeforeBreak != &team)
            return PlayerUpdateResult::kFinalize;
        if ((swos.gameState == GameState::kKeeperHoldsTheBall && player.isGoalkeeper()) || !player.ballDistance) {
            if (!isValidDirection(swos.cameraDirection))
                swos.cameraDirection = player.direction;
            completePassToReceiver(team, player);
            return PlayerUpdateResult::kFinalize;
        }
    } else {
        if (tryStartingPassReceiverTackle(team, player))
            return PlayerUpdateResult::kFinalize;
        if (passReceiverCanControlBall(team)) {
            passReceiverTakesControl(team, player);
            return PlayerUpdateResult::kFinalize;
        }
    }

    if (!setPlayerBallChaseDestination(team, player) ||
        (player.x.whole() == player.destX && player.y.whole() == player.destY))
        return PlayerUpdateResult::kFinalize;

    if (shouldCancelGoalkeeperPass(team, player)) {
        team.passToPlayerPtr.reset();
        team.passingBall = false;
        team.passingToPlayer = false;
        return PlayerUpdateResult::kStop;
    }
    return shouldStopPlayerForGoalOut(player) ? PlayerUpdateResult::kStop : PlayerUpdateResult::kFinalize;
}

// Decelerate an active goalkeeper dive and resolve its single allowed ball
// contact as a catch, primary save, or weaker secondary deflection.
static PlayerUpdateResult updateDivingGoalkeeper(TeamGeneralInfo& team, Sprite& player)
{
    if (tryFinishingGoalkeeperDive(team, player, swos.ballSprite))
        return PlayerUpdateResult::kStop;
    if (swos.gameStatePl == GameState::kInProgress && player.playerDownTimer <= 60) {
        restoreNormalPlayerState(player);
        return PlayerUpdateResult::kStop;
    }

    player.speed -= getDivingGoalkeeperSpeedReduction(team, player, swos.ballSprite);
    if (player.speed < 0)
        player.speed = 0;

    bool ballMovingAway = (&team == &swos.topTeamData && swos.ballSprite.deltaY.raw() >= 0) ||
        (&team == &swos.bottomTeamData && swos.ballSprite.deltaY.raw() <= 0);
    if (swos.gameStatePl != GameState::kInProgress || team.goalkeeperDiveContactMade || ballMovingAway ||
        !isBallWithinDivingGoalkeeperWidth(player, swos.ballSprite) ||
        !tryDivingGoalkeeperBallContact(team, player, swos.ballSprite))
        return PlayerUpdateResult::kFinalize;

    assert(team.shotChanceTable);
    bool penaltyPlay = swos.penalty || swos.playingPenalties;
    int catchChance = penaltyPlay ? 2 : team.shotChanceTable->divingCatchChance;
    int secondaryChance = penaltyPlay ? 0 : team.shotChanceTable->divingSecondaryDeflectionChance;
    auto contactResult = chooseDivingGoalkeeperContactResult(
        catchChance, secondaryChance, team.goalkeeperSavedCommentTimer);

    team.goalkeeperDiveContactMade = true;
    player.speed -= player.speed >> (player.state == PlayerState::kGoalieDivingHigh ? 2 : 1);
    swos.penalty = false;
    swos.playerHadBall = false;
    team.passKickTimer = kPassKickTimerDuration;
    team.opponentTeam->passingToPlayer = false;
    if (team.goalkeeperSavedCommentTimer >= 0)
        team.opponentTeam->passingKickingPlayer.reset();
    if (!m_ballInGoalkeeperArea)
        team.opponentTeam->teamStatsPtr->goalAttempts++;

    resetBothTeamSpinTimers();
    if (contactResult == DivingGoalkeeperContactResult::kCatch &&
        tryDivingGoalkeeperCatch(team, player, swos.ballSprite))
        return PlayerUpdateResult::kFinalize;

    if (contactResult == DivingGoalkeeperContactResult::kPrimaryDeflection)
        applyPrimaryGoalkeeperDeflection(team, player, swos.ballSprite);
    else
        applySecondaryGoalkeeperDeflection(team, player, swos.ballSprite);
    finishDivingGoalkeeperDeflection(team, player, swos.ballSprite);
    return PlayerUpdateResult::kFinalize;
}

// A goalkeeper who is no longer actively chasing the ball rejoins ordinary
// off-ball scheduling. On most in-play frames only the Y destination is
// clamped; every eighth frame the full common setup path is allowed to run.
static PlayerUpdateResult continueAfterLastGoalkeeperTouch(Sprite& player)
{
    if (swos.gameStatePl != GameState::kInProgress)
        return PlayerUpdateResult::kSchedule;

    if (swos.frameCount & 0x0e) {
        player.destY = std::clamp<int16_t>(player.destY, kTopPitchLine, kBottomPitchLine);
        return PlayerUpdateResult::kFinalize;
    }

    return PlayerUpdateResult::kCommonSchedule;
}

// Finish a goalkeeper chase decision. First attempt an anticipated catch, then
// handle immediate close-range possession. A same-team loose ball becomes
// directly controlled; an opponent touch enters the claimed-ball state.
static PlayerUpdateResult finishGoalkeeperMovement(TeamGeneralInfo& team, Sprite& player)
{
    if (swos.lastTeamPlayed != &team || swos.playerHadBall) {
        int handlingRoll = (swos.currentGameTick & 0xf0) >> 4;
        bool handlingSucceeded = m_ballInGoalkeeperArea ||
            handlingRoll < team.shotChanceTable->distantBallHandlingChance;
        bool validCatchProjection = m_ballNextGroundX >= 0 &&
            m_ballDefensivePosition.z > 12 && m_ballDefensivePosition.z <= 27;
        bool closeEnough = player.ballDistance <= 2116 &&
            std::abs(player.x.whole() - m_ballDefensivePosition.x) <= 12 &&
            std::abs(player.y.whole() - m_ballDefensivePosition.y) <= 12;

        if (handlingSucceeded && validCatchProjection && closeEnough)
            goalkeeperCaughtTheBall(team, player, m_ballNextGroundX, m_ballNextGroundY);
    }

    bool canTakeBall = &player != m_lastPlayerPlayed && player.ballDistance <= 72 && !team.ballAbove17 &&
        (player.state == PlayerState::kGoalieCatchingBall || !team.ball12To17);
    if (canTakeBall) {
        stopPlayer(player);
        if (swos.lastTeamPlayed == &team && !swos.playerHadBall) {
            updateBallWithControllingGoalkeeper(player);
            team.controlledPlayer = &player;
            team.passToPlayerPtr.reset();
            team.playerSwitchTimer = kPlayerSwitchCooldown;
            team.passingBall = false;
            team.passingToPlayer = false;
            swos.lastTeamPlayed = &team;
            m_lastPlayerPlayed = &player;
            swos.penalty = false;
            swos.playerHadBall = false;
            team.passKickTimer = 0;
            team.passingKickingPlayer.reset();
            team.reserved116 = 0;
            team.goalkeeperPlaying = true;
            team.goaliePlayingOrOut = 1;
            team.ballOutOfPlayOrKeeper = 1;
            return PlayerUpdateResult::kFinalize;
        }

        swos.lastKeeperPlayed = m_lastPlayerPlayed;
        swos.lastTeamPlayed = &team;
        m_lastPlayerPlayed = &player;
        swos.penalty = false;
        swos.playerHadBall = false;
        updateBallWithControllingGoalkeeper(player);
        goalkeeperClaimedTheBall(team, player, swos.ballSprite);
    }

    player.destY = std::clamp<int16_t>(player.destY, kTopPitchLine, kBottomPitchLine);
    return PlayerUpdateResult::kFinalize;
}

// Limit speculative interception to a keeper closer than both teams' selected
// players and pass receivers, matching the four original pointer checks.
static bool goalkeeperIsClosestRelevantPlayer(const TeamGeneralInfo& team, const Sprite& goalkeeper)
{
    for (auto candidate : { team.controlledPlayer.asPtr(), team.passToPlayerPtr.asPtr(),
        team.opponentTeam->controlledPlayer.asPtr(), team.opponentTeam->passToPlayerPtr.asPtr() }) {
        if (candidate && goalkeeper.ballDistance > candidate->ballDistance)
            return false;
    }
    return true;
}

// Chase the precomputed point where a high ball descends into a playable range.
static void setGoalkeeperProjectedDestination(TeamGeneralInfo& team, Sprite& player, int16_t speed)
{
    player.speed = speed;
    player.destX = m_ballNotHighPosition.x;
    player.destY = m_ballNotHighPosition.y;
}

// Start a save dive toward the projected ball. Penalties sometimes deliberately
// choose the opposite side and use the slower dive mode to preserve SWOS odds.
static PlayerUpdateResult makeGoalkeeperDive(TeamGeneralInfo& team, Sprite& player)
{
    bool jumpLeft = m_ballDefensivePosition.x < player.x.whole();
    Direction jumpDirection = jumpLeft ? Direction::kLeft : Direction::kRight;
    GoalkeeperJumpSpeedMode speedMode = GoalkeeperJumpSpeedMode::kFast;

    if (swos.playingPenalties || swos.penalty) {
        int oppositeDiveMask = jumpLeft ? 0x0c : 0x06;
        speedMode = GoalkeeperJumpSpeedMode::kSlower;
        if (swos.currentGameTick & oppositeDiveMask)
            speedMode = GoalkeeperJumpSpeedMode::kFast;
        else
            jumpDirection = jumpLeft ? Direction::kRight : Direction::kLeft;
    }

    player.direction = jumpDirection;
    goalkeeperJumping(team, player, jumpDirection, jumpDirection, speedMode, m_ballNotHighPosition.z);
    team.passKickTimer = 0;
    team.passingKickingPlayer.reset();
    player.destY = std::clamp<int16_t>(player.destY, kTopPitchLine, kBottomPitchLine);
    return PlayerUpdateResult::kFinalize;
}

// Resolve a shot already identified as threatening the goal. A close fired
// shot gets the finishing-versus-goalkeeping roll; otherwise the keeper dives
// when reachable or runs toward the projected playable point.
static PlayerUpdateResult handleShotAtGoal(TeamGeneralInfo& team, Sprite& player)
{
    if (swos.ballSprite.z.whole() > 16) {
        setGoalkeeperProjectedDestination(team, player, team.shotChanceTable->goalkeeperHighBallRunSpeed);
        return finishGoalkeeperMovement(team, player);
    }

    bool testScoringChance = team.goalkeeperSavedCommentTimer <= 0 && player.ballDistance <= 128 &&
        !team.ballAbove17 && team.opponentTeam->firePressed &&
        (team.opponentTeam->playerHasBall || team.opponentTeam->passKickTimer >= 22);
    if (testScoringChance) {
        int shooterFinishing = 0;
        auto shooter = team.opponentTeam->lastHeadingTacklingPlayer.asPtr();
        if (shooter)
            shooterFinishing = getPlayerPointerFromShirtNumber(team, *shooter).finishing;

        int goalkeeperSkill = getPlayerPointerFromShirtNumber(team, player).goalieSkill;
        int chanceIndex = shooterFinishing - goalkeeperSkill + 7;
        assert(chanceIndex >= 0 && chanceIndex < 15);
        int goalRoll = (swos.currentGameTick >> 1) & 0x0f;
        if (goalRoll < chanceIndex + 1) {
            team.goalkeeperSavedCommentTimer = -5;
            Direction jumpDirection = swos.ballSprite.x.whole() < player.x.whole() ?
                Direction::kLeft : Direction::kRight;
            player.direction = jumpDirection;
            team.opponentTeam->spinTimer = static_cast<uint16_t>(-1);
            if (swos.ballSprite.speed > 1536)
                swos.ballSprite.speed = 1536;

            goalkeeperJumping(team, player, jumpDirection, jumpDirection,
                GoalkeeperJumpSpeedMode::kRandomSlow, m_ballNotHighPosition.z);
            team.passKickTimer = 0;
            team.passingKickingPlayer.reset();
            player.destY = std::clamp<int16_t>(player.destY, kTopPitchLine, kBottomPitchLine);
            return PlayerUpdateResult::kFinalize;
        }
        team.goalkeeperSavedCommentTimer = 5;
    }

    if (shouldGoalkeeperDive(player, swos.ballSprite, team))
        return makeGoalkeeperDive(team, player);

    setGoalkeeperProjectedDestination(team, player, team.shotChanceTable->goalkeeperHighBallRunSpeed);
    return finishGoalkeeperMovement(team, player);
}

// Select the goalkeeper response to a ball aimed between the posts: charge a
// nearby ball, prepare for a shot, follow a slow loose ball, or cover its
// projected descent. The ordering intentionally mirrors the original branches.
static PlayerUpdateResult handleGoalAttempt(TeamGeneralInfo& team, Sprite& player)
{
    if (swos.lastTeamPlayed == &team && !swos.playerHadBall) {
        setGoalkeeperProjectedDestination(team, player, dseg_1105EF);
        return finishGoalkeeperMovement(team, player);
    }

    if (player.ballDistance < 512 && !team.ballAbove17) {
        player.destX = swos.ballSprite.x.whole();
        player.destY = swos.ballSprite.y.whole();
        return handleShotAtGoal(team, player);
    }

    if (player.ballDistance <= 2048 && !team.ballAbove17) {
        player.destX = kPitchCenterX + ((swos.ballSprite.x.whole() - kPitchCenterX) >> 1);
        player.destY = swos.ballSprite.y.whole() < kPitchCenterY ?
            kTopPitchLine + (swos.ballSprite.y.whole() - kTopPitchLine) / 2 :
            swos.ballSprite.y.whole() + (kBottomPitchLine - swos.ballSprite.y.whole()) / 2;
        player.speed = team.shotChanceTable->goalkeeperRunSpeed;
        return handleShotAtGoal(team, player);
    }

    if (team.opponentTeam->playerHasBall || team.playerHasBall || team.plVeryCloseToBall) {
        if (team.ballAbove17) {
            setGoalkeeperProjectedDestination(team, player, team.shotChanceTable->goalkeeperHighBallRunSpeed);
        } else {
            player.destX = swos.ballSprite.x.whole();
            player.destY = swos.ballSprite.y.whole();
            player.speed = team.shotChanceTable->goalkeeperRunSpeed;
        }
        return finishGoalkeeperMovement(team, player);
    }

    if (swos.lastTeamPlayed == &team && !swos.playerHadBall) {
        setGoalkeeperProjectedDestination(team, player, dseg_1105EF);
        return finishGoalkeeperMovement(team, player);
    }

    if (swos.ballSprite.speed > kShotAtGoalMinimumSpeed)
        return handleShotAtGoal(team, player);

    bool useHighProjection = player.ballDistance > 5000 || swos.ballSprite.z.whole() > 12 ||
        swos.ballSprite.deltaZ.raw() != 0.5_fp.raw();
    if (useHighProjection) {
        setGoalkeeperProjectedDestination(team, player, team.shotChanceTable->goalkeeperHighBallRunSpeed);
    } else {
        player.destX = swos.ballSprite.x.whole();
        player.destY = swos.ballSprite.y.whole();
        player.speed = team.shotChanceTable->goalkeeperRunSpeed;
    }

    return finishGoalkeeperMovement(team, player);
}

// Choose the normal-state goalkeeper action for this frame. The keeper may be
// delegated to direct controls/pass reception, intercept a projected landing,
// respond to a goal attempt, advance within the area, or return to formation.
static PlayerUpdateResult updateGoalkeeper(TeamGeneralInfo& team, Sprite& player)
{
    swos.goalTypeScored = static_cast<int>(GoalType::kRegular);
    if (swos.playingPenalties && swos.penaltiesTimer > 55)
        return PlayerUpdateResult::kFinalize;

    updatePlayerShotChanceTable(team, player);
    m_ballNextGroundX = -1;
    if (team.goalkeeperPlaying) {
        team.goaliePlayingOrOut = 1;
        team.ballOutOfPlayOrKeeper = 0;
        if (team.controlledPlayer == &player)
            return PlayerUpdateResult::kControlled;
        team.goalkeeperPlaying = false;
    }

    team.ballOutOfPlayOrKeeper = false;
    team.goaliePlayingOrOut = false;
    if (swos.gameStatePl != GameState::kInProgress) {
        *reinterpret_cast<uint8_t *>(&team.goaliePlayingOrOut) = 0xff;
        *reinterpret_cast<uint8_t *>(&team.ballOutOfPlayOrKeeper) = 0xff;
    }

    player.speed = kGoalkeeperGameSpeed;
    bool goalkeeperInOwnHalf = player.y.whole() <= kPitchCenterY ?
        &team != &swos.bottomTeamData : &team != &swos.topTeamData;
    if (!goalkeeperInOwnHalf)
        return continueAfterLastGoalkeeperTouch(player);
    if (team.controlledPlayer == &player)
        return PlayerUpdateResult::kControlled;
    if (team.passToPlayerPtr == &player)
        return PlayerUpdateResult::kPassReceiver;
    if (swos.gameStatePl != GameState::kInProgress)
        return PlayerUpdateResult::kSchedule;

    bool ballInTeamPenaltyArea = &team == &swos.bottomTeamData ?
        m_ballInLowerPenaltyArea : m_ballInUpperPenaltyArea;
    if (!ballInTeamPenaltyArea) {
        team.ballOutOfPlayOrKeeper = false;
        if (team.passToPlayerPtr == &player) {
            team.passToPlayerPtr.reset();
            team.passingBall = false;
            team.passingToPlayer = false;
        }
        return continueAfterLastGoalkeeperTouch(player);
    }

    if (&player == m_lastPlayerPlayed)
        return continueAfterLastGoalkeeperTouch(player);

    updateGoalkeeperBallProjections(player, swos.ballSprite, team);
    calculateBallNextGroundXYPositions(swos.ballSprite);
    int handlingRoll = (swos.currentGameTick & 0xf0) >> 4;
    constexpr int kGoalkeeperProjectionPitchInset = 8;
    int minimumY = &team == &swos.bottomTeamData ?
        kBottomPenaltyAreaTop : kTopPitchLine + kGoalkeeperProjectionPitchInset;
    int maximumY = &team == &swos.bottomTeamData ?
        kBottomPitchLine - kGoalkeeperProjectionPitchInset : kTopPenaltyAreaBottom;
    bool validGroundProjection = (m_ballInGoalkeeperArea ||
        handlingRoll < team.shotChanceTable->distantBallHandlingChance) && m_ballNextGroundX >= 0 &&
        m_ballNextGroundY >= minimumY && m_ballNextGroundY <= maximumY &&
        m_ballNextGroundX >= kPenaltyAreaLeft && m_ballNextGroundX <= kPenaltyAreaRight;

    if (validGroundProjection) {
        int keeperDeltaX = m_ballNextGroundX - player.x.whole();
        int keeperDeltaY = m_ballNextGroundY - player.y.whole();
        int ballDeltaX = m_ballNextGroundX - swos.ballSprite.x.whole();
        int ballDeltaY = m_ballNextGroundY - swos.ballSprite.y.whole();
        int keeperDistanceSquared = keeperDeltaX * keeperDeltaX + keeperDeltaY * keeperDeltaY;
        int ballDistanceSquared = ballDeltaX * ballDeltaX + ballDeltaY * ballDeltaY;
        if (keeperDistanceSquared * 4 <= ballDistanceSquared) {
            player.destX = m_ballNextGroundX;
            player.destY = m_ballNextGroundY;
            player.speed = kGoalkeeperMoveToBallSpeed;
            return finishGoalkeeperMovement(team, player);
        }
    }

    if (m_strikeDestinationX >= 295 && m_strikeDestinationX <= 376)
        return handleGoalAttempt(team, player);

    if (player.ballDistance < 512 && !team.ballAbove17) {
        player.destX = swos.ballSprite.x.whole();
        player.destY = swos.ballSprite.y.whole();
        return finishGoalkeeperMovement(team, player);
    }

    if (!team.ballAbove17) {
        bool canAdvance = m_ballInGoalkeeperArea;
        if (!canAdvance) {
            bool ballMovingTowardGoal = swos.ballSprite.y.whole() < kPitchCenterY ?
                swos.ballSprite.deltaY.raw() < 0 : swos.ballSprite.deltaY.raw() > 0;
            canAdvance = ballMovingTowardGoal;
        }
        if (canAdvance) {
            player.destX = m_ballInGoalkeeperArea ? swos.ballSprite.x.whole() :
                kPitchCenterX + ((swos.ballSprite.x.whole() - kPitchCenterX) >> 1);
            player.destY = swos.ballSprite.y.whole() < kPitchCenterY ?
                kTopPitchLine + (swos.ballSprite.y.whole() - kTopPitchLine) / 2 :
                swos.ballSprite.y.whole() + (kBottomPitchLine - swos.ballSprite.y.whole()) / 2;
            player.speed = team.shotChanceTable->goalkeeperRunSpeed;
            return finishGoalkeeperMovement(team, player);
        }
    }

    bool useDefensiveProjection = swos.ballSprite.z.whole() <= 16 && m_ballDefensivePosition.x >= 0 &&
        goalkeeperIsClosestRelevantPlayer(team, player);
    if (!useDefensiveProjection)
        return continueAfterLastGoalkeeperTouch(player);

    player.destX = m_ballDefensivePosition.x;
    player.destY = m_ballDefensivePosition.y;
    return finishGoalkeeperMovement(team, player);
}

// Perform the shared off-ball setup once a player is selected for an update:
// restart waiting, CPU controls, card/substitution movement, and formation.
static PlayerUpdateResult updateCommonScheduledPlayer(TeamGeneralInfo& team, Sprite& player)
{
    if (shouldWaitForRestartSetup())
        return PlayerUpdateResult::kSkip;

    // State 1 means the destination was reached during the previous update;
    // promote it once so the movement code can observe the event.
    if (player.destReachedState == DestinationState::kStarting)
        player.destReachedState = DestinationState::kTraveling;

    if (!team.controlledPlayer && !team.playerNumber)
        updateCpuPlayerControls(team);

    if (updatePlayerAwaitingCard(player) || updateSubstitutionMovement(player) || player.sentAway)
        return PlayerUpdateResult::kFinalize;

    setPlayerDestinationForGameState(team, player);
    return PlayerUpdateResult::kFinalize;
}

// Throttle ordinary off-ball players so only the team's rotating slot performs
// expensive formation work each frame. Substituted players bypass that gate.
static PlayerUpdateResult updateScheduledPlayer(TeamGeneralInfo& team, Sprite& player, int playerIndex)
{
    bool substitutedPlayer = swos.g_substituteInProgress && swos.substitutedPlSprite == &player;
    if (!substitutedPlayer) {
        if (team.updatePlayerIndex != 10 - playerIndex)
            return PlayerUpdateResult::kSkip;
        if (swos.gameStatePl != GameState::kInProgress)
            tryStartPostMatchReaction(team, player);
    }
    return updateCommonScheduledPlayer(team, player);
}

// Dispatch states that require ball-distance classification. Normal players
// are further routed by their role rather than by assembly-style labels.
static PlayerUpdateResult updatePlayerByState(TeamGeneralInfo& team, Sprite& player)
{
    switch (player.state) {
    case PlayerState::kTackling:
        return updateTacklingPlayer(team, player);
    case PlayerState::kJumpHeader:
        return updateJumpHeadingPlayer(team, player);
    case PlayerState::kStaticHeader:
        return updateStaticHeader(team, player) ? PlayerUpdateResult::kStop : PlayerUpdateResult::kFinalize;
    case PlayerState::kThrowIn:
        return updateThrowInPlayer(team, player);
    case PlayerState::kGoalieDivingHigh:
    case PlayerState::kGoalieDivingLow:
        return updateDivingGoalkeeper(team, player);
    case PlayerState::kGoalieCatchingBall:
        updateGoalkeeperCatchingBall(team, player, swos.ballSprite);
        return PlayerUpdateResult::kFinalize;
    case PlayerState::kDown:
        updateDownPlayerState(player);
        return PlayerUpdateResult::kFinalize;
    case PlayerState::kBooked:
        updateBookedPlayerState(player);
        return PlayerUpdateResult::kFinalize;
    case PlayerState::kSad:
    case PlayerState::kHappy:
        updateCelebratingPlayerState(player);
        return PlayerUpdateResult::kFinalize;
    case PlayerState::kGoalieClaimed:
        updateGoalkeeperClaimedState(player);
        return PlayerUpdateResult::kFinalize;
    case PlayerState::kNormal:
        if (player.isGoalkeeper())
            return updateGoalkeeper(team, player);
        else if (team.controlledPlayer == &player)
            return PlayerUpdateResult::kControlled;
        else if (team.passToPlayerPtr == &player)
            return PlayerUpdateResult::kPassReceiver;
        debugBreak();
        assert(false);
        return PlayerUpdateResult::kFinalize;
    case PlayerState::kTackled:
    case PlayerState::kInjured:
        assert(false);
        return PlayerUpdateResult::kFinalize;
    default:
        return PlayerUpdateResult::kFinalize;
    }
}

// Update one sprite and funnel every state through the appropriate shared tail.
// Injured/tackled states are handled before ball classification, just as in the
// original routine; inactive normal players use the throttled scheduling path.
static void updatePlayer(TeamGeneralInfo& team, Sprite& player, int playerIndex)
{
    player.playerDirection = static_cast<int16_t>(player.direction);
    *reinterpret_cast<uint8_t *>(&player.isMoving) = player.deltaX || player.deltaY ? 0xff : 0;

    PlayerUpdateResult result;
    if (player.state == PlayerState::kTackled) {
        result = updateTackledPlayer(player) ? PlayerUpdateResult::kStop : PlayerUpdateResult::kFinalize;
    } else if (player.state == PlayerState::kInjured) {
        result = updateInjuredPlayerState(player) ? PlayerUpdateResult::kStop : PlayerUpdateResult::kFinalize;
    } else {
        bool needsBallClassification = team.controlledPlayer == &player || player.isGoalkeeper() ||
            team.passToPlayerPtr == &player || player.state != PlayerState::kNormal;
        if (needsBallClassification) {
            classifyBallForPlayer(team, player, swos.ballSprite);
            result = updatePlayerByState(team, player);
        } else if (swos.injuriesForever && static_cast<int16_t>(player.injuryLevel) == -1) {
            player.speed = kInjuryForeverSpeed;
            player.destY -= 8;
            result = PlayerUpdateResult::kFinalize;
        } else {
            result = PlayerUpdateResult::kSchedule;
        }
    }

    if (result == PlayerUpdateResult::kControlled)
        result = updateControlledPlayer(team, player);
    else if (result == PlayerUpdateResult::kPassReceiver)
        result = updatePassReceiver(team, player);
    else if (result == PlayerUpdateResult::kSchedule)
        result = updateScheduledPlayer(team, player, playerIndex);
    else if (result == PlayerUpdateResult::kCommonSchedule)
        result = updateCommonScheduledPlayer(team, player);

    if (result == PlayerUpdateResult::kStop)
        stopPlayer(player);
    if (result != PlayerUpdateResult::kSkip)
        finalizePlayerUpdate(team, player);
}

// Update all eleven players. Each state handler returns the small amount of
// control-flow information the outer loop needs instead of jumping into a
// shared tail as the original assembly did.
static void updateAllPlayers(TeamGeneralInfo& team)
{
    for (int playerIndex = 0; playerIndex < 11; playerIndex++)
        updatePlayer(team, *team.players[playerIndex], playerIndex);
}

// Preserve the player-name attribution when a goalkeeper becomes the last
// player to touch the ball. This team-level bookkeeping belongs after the full
// 11-player loop, not to any individual player's state update.
static void finishUpdatingAllPlayers()
{
    if ((swos.gameStatePl == GameState::kInProgress || swos.gameState == GameState::kKeeperHoldsTheBall) &&
        (m_lastPlayerPlayed == &swos.goalie1Sprite || m_lastPlayerPlayed == &swos.goalie2Sprite)) {
        auto goalkeeper = m_lastPlayerPlayed == &swos.goalie1Sprite ? &swos.goalie1Sprite : &swos.goalie2Sprite;
        if (m_prevLastPlayer != goalkeeper) {
            m_lastPlayerBeforeGoalkeeper = m_prevLastPlayer;
            swos.lastTeamScored = m_prevLastTeamPlayed;
            swos.nobodysBallTimer = 50;
        }
    }
}
