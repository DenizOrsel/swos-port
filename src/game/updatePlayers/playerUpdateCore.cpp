// Implementation fragment included by updatePlayers.cpp; do not compile separately.

// Update team timers and ball-location state needed by the per-player pass.
static void preparePlayersUpdate(TeamGeneralInfo& team)
{
    m_prevLastPlayer = m_lastPlayerPlayed;
    m_prevLastTeamPlayed = swos.lastTeamPlayed;

    // A goalkeeper touch is relevant only until an outfield player touches the
    // ball. Keep it while the same goalkeeper remains the latest player.
    if (swos.lastKeeperPlayed && m_lastPlayerPlayed != &swos.goalie1Sprite &&
        m_lastPlayerPlayed != &swos.goalie2Sprite)
        swos.lastKeeperPlayed.reset();

    // Positive and negative values represent mirrored commentary cooldowns;
    // both count toward zero.
    if (team.goalkeeperSavedCommentTimer > 0)
        team.goalkeeperSavedCommentTimer--;
    else if (team.goalkeeperSavedCommentTimer < 0)
        team.goalkeeperSavedCommentTimer++;

    if (team.passKickTimer && !--team.passKickTimer)
        team.passingKickingPlayer.reset();
    if (team.wonTheBallTimer)
        team.wonTheBallTimer--;
    if (team.playerSwitchTimer)
        team.playerSwitchTimer--;

    applyBallAfterTouch(team);

    int ballX = swos.ballSprite.x.whole();
    int ballY = swos.ballSprite.y.whole();
    bool withinPenaltyAreaWidth = ballX >= kPenaltyAreaLeft && ballX <= kPenaltyAreaRight;
    bool withinPitchLength = ballY >= kTopPitchLine && ballY <= kBottomPitchLine;

    m_ballInUpperPenaltyArea = withinPenaltyAreaWidth && withinPitchLength &&
        ballY <= kTopPenaltyAreaBottom;
    m_ballInLowerPenaltyArea = withinPenaltyAreaWidth && withinPitchLength &&
        ballY >= kBottomPenaltyAreaTop;

    bool withinGoalkeeperAreaWidth = ballX >= kGoalkeeperAreaLeft && ballX <= kGoalkeeperAreaRight;
    m_ballInGoalkeeperArea = withinGoalkeeperAreaWidth &&
        (ballY <= kTopGoalkeeperAreaBottom || ballY >= kBottomGoalkeeperAreaTop) &&
        (m_ballInUpperPenaltyArea || m_ballInLowerPenaltyArea);

    // Once play resumes away from either penalty area, a stale goal-out flag
    // must not survive into the next ball-out decision.
    if (swos.gameStatePl == GameState::kInProgress && swos.goalOut &&
        !m_ballInUpperPenaltyArea && !m_ballInLowerPenaltyArea)
        swos.goalOut = 0;

    if (++team.updatePlayerIndex == 11)
        team.updatePlayerIndex = 0;
}

// Apply the common end-of-update movement, facing and animation work.
static void finalizePlayerUpdate(const TeamGeneralInfo& team, Sprite& player)
{
    updatePlayerSpeedAndFrameDelay(team, player);

    auto movement = calculateDeltaXAndY(player.speed, player.x.whole(), player.y.whole(), player.destX, player.destY);
    player.deltaX = movement.deltaX;
    player.deltaY = movement.deltaY;

    int movementDirection = movement.direction;
    bool goalkeeperFacesBall = player.isGoalkeeper() &&
        swos.gameStatePl == GameState::kInProgress && !team.goalkeeperPlaying &&
        (m_ballInUpperPenaltyArea || m_ballInLowerPenaltyArea);
    if (goalkeeperFacesBall)
        movementDirection = (player.fullDirection + 128) & 0xff;

    if (movementDirection >= 0) {
        player.direction = static_cast<Direction>(((movementDirection + 16) & 0xff) >> 5);
    } else if (team.controlledPlayer != &player && team.passingKickingPlayer != &player) {
        // Stationary automatic players face the ball using the direction saved
        // during their main state update. Manual players retain their input.
        player.direction = static_cast<Direction>(((player.fullDirection + 128 + 16) & 0xff) >> 5);
    }

    int targetX;
    int targetY;
    if (swos.gameStatePl == GameState::kInProgress || swos.gameState > GameState::kThrowInBackLeft) {
        targetX = swos.ballSprite.x.whole();
        targetY = swos.ballSprite.y.whole();
    } else {
        targetX = swos.foulXCoordinate;
        targetY = swos.foulYCoordinate;
    }

    auto facing = calculateDeltaXAndY(256, targetX, targetY, player.x.whole(), player.y.whole()).direction;
    player.fullDirection = std::max(facing, 0);

    if (player.state == PlayerState::kNormal) {
        bool isMoving = player.deltaX || player.deltaY;
        bool wasMoving = *reinterpret_cast<const uint8_t *>(&player.isMoving) != 0;
        bool directionChanged = player.playerDirection != static_cast<int16_t>(player.direction);
        if (isMoving != wasMoving || directionChanged) {
            setPlayerAnimationTable(player,
                isMoving ? getPlayerRunningAnimTable() : getPlayerNormalStandingAnimTable());
        }
    }
}

// Cache the distance and height bands used throughout the selected player's state update.
// Only one flag in each group is set. The original stores these on the team because many
// later branches consult them without recomputing the distance or ball height.
static void classifyBallForPlayer(TeamGeneralInfo& team, const Sprite& player, const Sprite& ball)
{
    team.prevPlVeryCloseToBall = team.plVeryCloseToBall;
    team.plVeryCloseToBall = false;
    team.plCloseToBall = false;
    team.plNotFarFromBall = false;

    if (player.ballDistance <= 32)
        team.plVeryCloseToBall = true;
    else if (player.ballDistance <= 72)
        team.plCloseToBall = true;
    else if (player.ballDistance <= 2450)
        team.plNotFarFromBall = true;

    team.ballLessEqual4 = false;
    team.ball4To8 = false;
    team.ball8To12 = false;
    team.ball12To17 = false;
    team.ballAbove17 = false;

    auto ballHeight = static_cast<uint16_t>(ball.z.whole());
    if (ballHeight > 17)
        team.ballAbove17 = true;
    else if (ballHeight > 12)
        team.ball12To17 = true;
    else if (ballHeight > 8)
        team.ball8To12 = true;
    else if (ballHeight > 4)
        team.ball4To8 = true;
    else
        team.ballLessEqual4 = true;
}

// Advance a player who is lying on the ground after a tackle. Returns true when the player
// recovered normally and their destination must be snapped to the current position.
// An injured player instead transitions into the rolling injury state and retains
// the ordinary finalization path.
static bool updateTackledPlayer(Sprite& player)
{
    if (!--player.playerDownTimer) {
        if (player.injuryLevel) {
            player.speed = 0;
            player.state = PlayerState::kInjured;
            player.playerDownTimer = static_cast<uint8_t>(player.injuryLevel);
            setPlayerAnimationTable(player, getPlayerInjuredAnimTable());
            return false;
        }

        restoreNormalPlayerState(player);
        return true;
    }

    if (!player.speed)
        return false;

    // Sliding inside either goal mouth loses an additional quarter of its
    // speed before the normal ground friction is applied.
    int x = player.x.whole();
    int y = player.y.whole();
    if ((y < 159 || y > 739) && x >= 265 && x <= 406)
        player.speed -= player.speed >> 2;

    player.speed = std::max<int16_t>(player.speed - kPlayerGroundFriction, 0);
    return false;
}

// Give a stationary outfield player an occasional win/lose reaction after the
// result is known. The caller invokes this for only one round-robin player per
// team update, matching the original pacing and random-number consumption.
static void tryStartPostMatchReaction(const TeamGeneralInfo& team, Sprite& player)
{
    if (swos.gameState == GameState::kGameEnded && swos.winningTeamPtr &&
        player.state == PlayerState::kNormal && !player.isGoalkeeper() &&
        !player.deltaX && !player.deltaY && SWOS::rand() <= 64) {
        bool won = team.inGameTeamPtr == swos.winningTeamPtr;
        setPlayerAnimationTable(player, won ? getPlayerWinningReactionAnimTable() : getPlayerLosingReactionAnimTable());
        player.state = won ? PlayerState::kHappy : PlayerState::kSad;
    }
}

// During the opening frames of corners and free kicks, non-selected players
// keep their prepared positions instead of immediately resuming normal movement.
// breakState records the restart that began the current in-play transition.
static bool shouldWaitForRestartSetup()
{
    if (swos.gameStatePl != GameState::kInProgress || swos.inGameCounter > 25)
        return false;

    return swos.breakState == GameState::kCornerLeft || swos.breakState == GameState::kCornerRight ||
        swos.breakState >= GameState::kFreeKickOuterLeft && swos.breakState <= GameState::kFreeKickOuterRight;
}

// Move the player selected for a card beside the foul spot. Once both player
// and referee are ready, start the matching booking animation. Returning true
// tells the caller that this player's ordinary positioning must be skipped.
static bool updatePlayerAwaitingCard(Sprite& player)
{
    if (swos.bookedPlayer != &player)
        return false;

    auto cardX = swos.foulXCoordinate + 21;
    auto cardY = swos.foulYCoordinate;
    if (player.x.whole() != cardX || player.y.whole() != cardY) {
        player.destX = cardX;
        player.destY = cardY;
        return true;
    }

    static constexpr uint16_t kRefWaitingForPlayer = 2;
    static constexpr uint16_t kRefAboutToGiveCard = 3;
    if (swos.refState != kRefWaitingForPlayer)
        return true;

    swos.refState = kRefAboutToGiveCard;
    switch (swos.whichCard) {
    case kYellowCard:
        setPlayerAnimationTable(player, getPlayerGettingYellowCardAnimTable());
        break;
    case kRedCard:
        setPlayerAnimationTable(player, getPlayerGettingRedCardAnimTable());
        break;
    default:
        setPlayerAnimationTable(player, getPlayerGetting2ndYellowCardAnimTable());
        break;
    }

    player.state = PlayerState::kBooked;
    player.playerDownTimer = 1;
    return true;
}

// Advance the two-stage substitution walk. The outgoing player first reaches
// the touchline, then the incoming player is placed at the stored entrance
// point. Returns true while the selected player is still walking and should
// skip ordinary tactical positioning for this update.
static bool updateSubstitutionMovement(Sprite& player)
{
    if (!swos.g_substituteInProgress)
        return false;

    if (swos.g_substituteInProgress < 0) {
        if (swos.substitutedPlSprite == &player) {
            player.speed = kSubstitutionSpeed;
            if (!player.deltaX && !player.deltaY)
                swos.g_substituteInProgress = 0;
        }
        return false;
    }

    if (swos.substitutedPlSprite != &player)
        return false;

    bool incomingPlayer = player.injuryLevel == -2;
    bool reachedDestination = player.x.whole() == swos.substitutedPlDestX &&
        player.y.whole() == swos.substitutedPlDestY;

    if (incomingPlayer || reachedDestination && swos.g_substituteInProgress == 2) {
        swos.g_substituteInProgress = -1;
        player.x.setWhole(swos.plSubstitutedX);
        player.y.setWhole(swos.plSubstitutedY);
        player.speed = kSubstitutionSpeed;
        player.sentAway = 0;
        return false;
    }

    if (reachedDestination) {
        swos.g_substituteInProgress = 2;
        swos.substitutedPlDestX = swos.plComingX;
        swos.substitutedPlDestY = swos.plComingY;
    }

    player.destX = swos.substitutedPlDestX;
    player.destY = swos.substitutedPlDestY;
    player.speed = kSubstitutionSpeed;
    return true;
}

// Select the player's entry in a stoppage-position table. For the defending
// team at an inner free kick, red-carded players are removed from the compact
// wall ordering and the wall's horizontal offset is adjusted accordingly.
static uint16_t selectRestartPositionIndex(const TeamGeneralInfo& team, const Sprite& player)
{
    m_freeKickDestinationX = 0;

    bool innerFreeKick = swos.gameState > GameState::kFreeKickOuterLeft &&
        swos.gameState < GameState::kFreeKickOuterRight;
    if (swos.lastTeamPlayedBeforeBreak == &team || !innerFreeKick ||
        player.playerOrdinal < 2 || player.playerOrdinal > 5)
        return player.playerOrdinal;

    uint16_t positionIndex = 2;
    for (uint16_t ordinal = 2; ordinal < player.playerOrdinal; ordinal++) {
        if (team.players[ordinal - 1]->cards >= 0)
            positionIndex++;
    }

    auto stateIndex = static_cast<word>(swos.gameState) - static_cast<word>(GameState::kFreeKickOuterLeft);
    assert(stateIndex < std::size(kFreeKickFactors));
    int16_t factor = kFreeKickFactors[stateIndex];
    auto freeKickDestinationX = static_cast<int16_t>(factor * 4);
    for (uint16_t playerIndex = 1; playerIndex <= 4; playerIndex++) {
        if (team.players[playerIndex]->cards >= 0)
            freeKickDestinationX -= factor;
    }
    m_freeKickDestinationX = freeKickDestinationX;
    return positionIndex;
}

// Decode one entry from the original restart-position table. Ordinary entries
// are pitch-relative; x coordinates outside +/-1000 mark ball-relative wall
// positions. Bottom-team entries are mirrored around the pitch dimensions.
static bool setRestartPosition(const TeamGeneralInfo& team, Sprite& player, const RestartPositions& positions,
    uint16_t positionIndex, int ballX, int ballY)
{
    constexpr int16_t kNoPosition = 22222;

    assert(positionIndex > 0 && positionIndex <= positions.size());
    auto [x, y] = positions[positionIndex - 1];
    if (x == kNoPosition)
        return false;

    bool ballRelative = x <= -1000 || x >= 1000;
    if (ballRelative) {
        x += x < 0 ? 1000 : -1000;
        if (&team == &swos.topTeamData) {
            x += m_freeKickDestinationX;
        } else {
            x = -x;
            y = -y;
            x -= m_freeKickDestinationX;
        }
        player.destX = ballX + x;
        player.destY = ballY + y;
    } else if (&team == &swos.topTeamData) {
        player.destX = x + kLeftCornerX;
        player.destY = y + kTopPitchLine;
    } else {
        player.destX = kRightCornerX - x;
        player.destY = kBottomPitchLine - y;
    }
    return true;
}

// Keep opposing outfield players outside the 65-pixel restart radius. The
// original resolves an intrusion by placing the destination 70 pixels from the
// foul toward midfield, leaving goalkeepers and the restarting team alone.
static void moveDefenderAwayFromRestart(const TeamGeneralInfo& team, Sprite& player)
{
    if (swos.gameStatePl != GameState::kInProgress && !player.isGoalkeeper() &&
        swos.lastTeamPlayedBeforeBreak != &team) {
        int deltaX = static_cast<int16_t>(player.destX - swos.foulXCoordinate);
        int deltaY = static_cast<int16_t>(player.destY - swos.foulYCoordinate);
        constexpr int kRestartExclusionRadius = 65;
        if (deltaX * deltaX + deltaY * deltaY <= kRestartExclusionRadius * kRestartExclusionRadius)
            player.destX = swos.foulXCoordinate + (swos.foulXCoordinate < kPitchCenterX ? 70 : -70);
    }
}

// Choose whether this player joins the ball chase. A nearby selected player
// keeps priority while it is at least as close as this candidate; otherwise
// the team transitions into pass/chase mode and this player follows the team's
// projected ball coordinates. Returns false when the player should hold still.
static bool setPlayerBallChaseDestination(TeamGeneralInfo& team, Sprite& player)
{
    constexpr uint32_t kNearbyBallDistance = 3'200;

    if (!team.passingBall) {
        auto controlledPlayer = team.controlledPlayer;
        if (!controlledPlayer || controlledPlayer->ballDistance > kNearbyBallDistance ||
            controlledPlayer->ballDistance > player.ballDistance) {
            team.passingBall = true;
        } else if (player.ballDistance <= kNearbyBallDistance) {
            player.destX = player.x.whole();
            player.destY = player.y.whole();
            return false;
        }
    }

    player.destX = team.ballX;
    player.destY = team.ballY;
    return true;
}

// Choose the ordinary tactical destination or a restart-table position for a
// player. Restart tables may omit a state or an individual player; both cases
// deliberately fall back to normal positioning around the ball or foul spot.
static void setPlayerDestinationForGameState(TeamGeneralInfo& team, Sprite& player)
{
    int ballX = swos.ballSprite.x.whole();
    int ballY = swos.ballSprite.y.whole();

    if (swos.gameStatePl == GameState::kInProgress && !swos.playingPenalties) {
        setPlayerWithNoBallDestination(team, player, ballX, ballY);
        moveDefenderAwayFromRestart(team, player);
        return;
    }

    ballX = swos.foulXCoordinate;
    ballY = swos.foulYCoordinate;
    if (swos.gameState == GameState::kFirstHalfEnded || swos.gameState == GameState::kGameEnded)
        return;

    if (swos.gameState != GameState::kKeeperHoldsTheBall &&
        swos.gameState != GameState::kGoalOutLeft && swos.gameState != GameState::kGoalOutRight) {
        GameState restartState = swos.playingPenalties ? GameState::kPenaltyShootout : swos.gameState;

        // Penalty shootouts always use the physical top/bottom team table. Other
        // stoppages use the table belonging to the team that last touched the ball.
        bool useBottomPositions = swos.playingPenalties ?
            &team == &swos.bottomTeamData : swos.lastTeamPlayedBeforeBreak == &team;
        auto positions = getRestartPositions(useBottomPositions, restartState);

        if (positions) {
            uint16_t positionIndex = selectRestartPositionIndex(team, player);
            if (setRestartPosition(team, player, *positions, positionIndex, ballX, ballY))
                return;
        }
    }

    setPlayerWithNoBallDestination(team, player, ballX, ballY);
    moveDefenderAwayFromRestart(team, player);
}

// A goalkeeper chasing a pass may only continue inside the rectangular
// handling area nearest their own goal. Crossing its side or goalward edge
// cancels the pass so normal positioning can take over.
static bool shouldCancelGoalkeeperPass(const TeamGeneralInfo& team, const Sprite& player)
{
    constexpr int kGoalkeeperPassAreaInset = 10;
    if (swos.gameStatePl != GameState::kInProgress || !player.isGoalkeeper() || team.goalkeeperPlaying)
        return false;

    bool insideHorizontalBounds = player.x.whole() >= kPenaltyAreaLeft + kGoalkeeperPassAreaInset &&
        player.x.whole() <= kPenaltyAreaRight - kGoalkeeperPassAreaInset;
    bool insideGoalwardBound = &team == &swos.bottomTeamData ?
        player.y.whole() >= kBottomPenaltyAreaTop + kGoalkeeperPassAreaInset :
        player.y.whole() <= kTopPenaltyAreaBottom - kGoalkeeperPassAreaInset;
    return !insideHorizontalBounds || !insideGoalwardBound;
}

// During a goal-out sequence, stop outfield players that enter either goal
// mouth while allowing players elsewhere to finish their current movement.
static bool shouldStopPlayerForGoalOut(const Sprite& player)
{
    constexpr int kGoalOutAreaMargin = 10;
    if (swos.goalOut && !player.isGoalkeeper() &&
        player.x.whole() >= kPenaltyAreaLeft - kGoalOutAreaMargin &&
        player.x.whole() <= kPenaltyAreaRight + kGoalOutAreaMargin)
        return player.y.whole() <= kTopPenaltyAreaBottom + kGoalOutAreaMargin ||
            player.y.whole() >= kBottomPenaltyAreaTop - kGoalOutAreaMargin;
    else
        return false;
}

// A human-controlled pass receiver may turn the incoming pass into a tackle
// when firing near the ball. Do not steal the tackle from a selected teammate
// that is already closer to the ball.
static bool tryStartingPassReceiverTackle(TeamGeneralInfo& team, Sprite& player)
{
    if (player.isGoalkeeper() || !team.fireThisFrame ||
        !hasDirection(team.currentAllowedDirection) || !team.plNotFarFromBall ||
        (!team.ballLessEqual4 && !team.ball4To8))
        return false;

    auto controlledPlayer = team.controlledPlayer;
    if (controlledPlayer && controlledPlayer->ballDistance < player.ballDistance)
        return false;

    team.headerOrTackle = true;
    playerBeginTackling(team, player, team.currentAllowedDirection);
    team.ballCanBeControlled = 0;
    team.opponentTeam->passingToPlayer = false;
    team.opponentTeam->passingKickingPlayer.reset();
    return true;
}

// A receiver may take a fast pass from the wider "close" radius; slower passes
// require the tighter very-close radius. Operator grouping is made explicit
// because these two distance cases intentionally differ.
static bool passReceiverCanControlBall(const TeamGeneralInfo& team)
{
    if (!team.ballLessEqual4)
        return false;

    constexpr int16_t kFastPassSpeed = 3.0_speed;
    return swos.ballSprite.speed >= kFastPassSpeed && team.plCloseToBall || team.plVeryCloseToBall;
}

// Complete a successful pass reception and transfer team control to the
// receiver. Goalkeepers additionally decide whether this is a clean pickup or
// a claim that must enter the goalkeeper-held-ball state.
static void passReceiverTakesControl(TeamGeneralInfo& team, Sprite& player)
{
    team.controlledPlayer = team.passToPlayerPtr;
    team.passToPlayerPtr.reset();
    team.playerSwitchTimer = kPlayerSwitchCooldown;
    team.passingBall = false;
    team.passingToPlayer = false;
    team.shooting = false;
    player.destX = player.x.whole();
    player.destY = player.y.whole();

    if (player.isGoalkeeper())
        updateBallWithControllingGoalkeeper(player);
    else
        updateBallWithControllingPlayer(player);

    team.passKickTimer = 0;
    team.passingKickingPlayer.reset();
    team.reserved116 = 0;

    if (player.isGoalkeeper()) {
        if (swos.lastTeamPlayed != &team || swos.playerHadBall) {
            goalkeeperClaimedTheBall(team, player, swos.ballSprite);
        } else {
            team.goalkeeperPlaying = true;
            team.goaliePlayingOrOut = 1;
            team.ballOutOfPlayOrKeeper = 1;
        }
    }

    swos.lastTeamPlayed = &team;
    m_lastPlayerPlayed = &player;
    swos.penalty = false;
    swos.playerHadBall = false;
}

// Finish the ordinary successful-pass path. Unlike the close-control handoff,
// this path stops the ball first and may put the receiver into the dedicated
// throw-in preparation animation when the pass completes a throw-in restart.
static void completePassToReceiver(TeamGeneralInfo& team, Sprite& player)
{
    player.direction = swos.cameraDirection;
    team.controlledPlayer = team.passToPlayerPtr;
    team.passToPlayerPtr.reset();
    team.playerSwitchTimer = kPlayerSwitchCooldown;
    team.passingBall = false;
    team.passingToPlayer = false;
    team.shooting = false;
    swos.ballSprite.speed = 0;
    swos.lastTeamPlayed = &team;
    m_lastPlayerPlayed = &player;
    swos.penalty = false;
    swos.playerHadBall = false;
    updatePlayerWithBall(player);
    team.passKickTimer = 0;
    team.passingKickingPlayer.reset();
    team.reserved116 = 0;

    if (swos.gameState >= GameState::kThrowInForwardRight &&
        swos.gameState <= GameState::kThrowInBackLeft) {
        setThrowInPlayerCoordinates(player);
        setPlayerAnimationTable(player, getAboutToThrowInAnimTable());
        player.state = PlayerState::kThrowIn;
        player.playerDownTimer = 0;
        swos.hideBall = true;
    }
}

// Lead a human-controlled pass receiver according to the ball's approach
// angle and the aftertouch applied to the pass. The original friction table is
// indexed in 32 direction sectors and supplies an x/y landing offset.
static void updateHumanPassAssistTarget(TeamGeneralInfo& team, const Sprite& player)
{
    constexpr int16_t kMinimumPassAssistSpeed = 1.0_speed;

    bool longPass = team.longPass || team.longSpinPass;
    bool hasAftertouch = longPass || team.leftSpin || team.rightSpin;

    if (!team.playerNumber || !hasAftertouch || swos.ballSprite.speed < kMinimumPassAssistSpeed ||
        !team.passingToPlayer)
        return;

    if (!hasDirection(swos.ballSprite.direction)) {
        team.ballX = player.x.whole();
        team.ballY = player.y.whole();
        return;
    }

    uint8_t angleDifference = static_cast<uint8_t>(swos.ballSprite.fullDirection - player.fullDirection);
    if (angleDifference > 64 && angleDifference < 192) {
        team.passingToPlayer = false;
        team.passingBall = false;
        return;
    }

    if (angleDifference <= 4 || angleDifference >= 252) {
        team.ballX = player.x.whole();
        team.ballY = player.y.whole();
        return;
    }

    int8_t leadAngle = longPass ? -32 : -64;
    if (static_cast<int8_t>(angleDifference) >= 0)
        leadAngle = -leadAngle;

    uint8_t frictionDirection = static_cast<uint8_t>(swos.ballSprite.fullDirection + leadAngle + 4);
    auto& offset = kPassAssistOffsets[frictionDirection >> 3];
    team.ballX = player.x.whole() + offset.x;
    team.ballY = player.y.whole() + offset.y;
}

// Advance the jump itself before checking whether the player has crossed a
// pitch or goal boundary. The hit animation winds down later than an attempt.
static JumpHeaderMovementResult updateJumpHeaderMovement(Sprite& player)
{
    if (!--player.playerDownTimer) {
        restoreNormalPlayerState(player);
        return JumpHeaderMovementResult::kStopPlayer;
    }

    if (!player.speed)
        return JumpHeaderMovementResult::kCheckPitchBounds;

    setJumpHeaderHitAnimTable(player);
    uint8_t slowdownTimer = player.animTable == getJumpHeaderAttemptAnimTable() ? 37 : 17;
    if (player.playerDownTimer <= slowdownTimer) {
        player.speed = static_cast<uint16_t>(player.speed) >> 1;
        return JumpHeaderMovementResult::kUpdatePosition;
    }

    player.speed -= kJumpHeaderAirResistance;
    if (player.speed >= 0)
        return JumpHeaderMovementResult::kCheckPitchBounds;

    player.speed = 0;
    return JumpHeaderMovementResult::kUpdatePosition;
}

// Decide how a diving header interacts with the irregular pitch and goal
// outline. Keep these checks ordered: the rectangles overlap around the posts.
static JumpHeaderBoundaryResult checkJumpHeaderBoundary(const Sprite& player)
{
    int x = player.x.whole();
    int y = player.y.whole();

    constexpr int kOuterPitchMargin = 8;
    if (x < kLeftThrowInLine - kOuterPitchMargin || x > kRightThrowInLine + kOuterPitchMargin)
        return JumpHeaderBoundaryResult::kSlowDown;
    if (y >= kTopPitchLine + 3 && y <= kBottomPitchLine - 3)
        return JumpHeaderBoundaryResult::kTryHeadingBall;

    if ((x >= kLeftOuterGoalPost && x < kLeftInnerGoalPost + 3) ||
        (x > kRightInnerGoalPost && x <= kRightOuterGoalPost + 3))
        return JumpHeaderBoundaryResult::kFinish;
    if (y >= kTopPitchLine && y <= kBottomPitchLine)
        return JumpHeaderBoundaryResult::kTryHeadingBall;

    if (x >= kLeftOuterGoalPost - 6 && x <= kRightOuterGoalPost + 9)
        return JumpHeaderBoundaryResult::kFinish;
    if (y >= kTopPitchLine - kOuterPitchMargin && y <= kBottomPitchLine + kOuterPitchMargin)
        return JumpHeaderBoundaryResult::kTryHeadingBall;

    return JumpHeaderBoundaryResult::kSlowDown;
}

// Once the jump animation is wound up, connect with a nearby ball at heading
// height. Failed timing, distance, or height checks simply leave the player in
// the current jump-header state for this update.
static void tryCompletingJumpHeader(TeamGeneralInfo& team, Sprite& player)
{
    if (player.playerDownTimer >= 42 && swos.gameStatePl == GameState::kInProgress && !player.isHeadingBall &&
        player.ballDistance <= 64 && swos.ballSprite.z.whole() >= 8 && swos.ballSprite.z.whole() <= 15) {
        swos.lastTeamPlayed = &team;
        m_lastPlayerPlayed = &player;
        swos.penalty = false;
        swos.playerHadBall = true;
        playerHittingJumpHeader(team, player);
        team.passKickTimer = kPassKickTimerDuration;
        team.opponentTeam->passingToPlayer = false;
        team.opponentTeam->passingKickingPlayer.reset();
    }
}

// Advance a standing-header animation. Returns true when the animation ends
// and the caller must also snap the player's destination to the current spot.
static bool updateStaticHeader(TeamGeneralInfo& team, Sprite& player)
{
    if (!--player.playerDownTimer) {
        restoreNormalPlayerState(player);
        return true;
    }

    player.speed -= 16;
    if (player.speed < 0)
        player.speed = 0;
    setStaticHeaderDirection(team, player);

    if (swos.gameStatePl != GameState::kInProgress || player.isHeadingBall || player.ballDistance > 64 ||
        swos.ballSprite.z.whole() < 8 || swos.ballSprite.z.whole() > 15 ||
        !hasDirection(team.currentAllowedDirection))
        return false;

    swos.lastTeamPlayed = &team;
    m_lastPlayerPlayed = &player;
    swos.penalty = false;
    swos.playerHadBall = true;
    playerHittingStaticHeader(team, player);
    team.passKickTimer = kPassKickTimerDuration;
    team.opponentTeam->passingToPlayer = false;
    team.opponentTeam->passingKickingPlayer.reset();
    return false;
}

// Apply the weak/strong ball-winning tackle and the shared possession
// bookkeeping. A tackling timer of -1 is the original weak-tackle sentinel.
static void completeSuccessfulTackle(TeamGeneralInfo& team, Sprite& player)
{
    if (player.tacklingTimer == static_cast<uint16_t>(-1))
        playerTackledTheBallWeak(team, player);
    else
        playerTackledTheBallStrong(team, player);

    team.lastHeadingTacklingPlayer = &player;
    team.passKickTimer = kPassKickTimerDuration;
    team.opponentTeam->passingToPlayer = false;
    team.opponentTeam->passingKickingPlayer.reset();
    team.opponentTeam->spinTimer = static_cast<uint16_t>(-1);
    team.opponentTeam->wonTheBallTimer = 12;
}

// The throw animation records whether its contact frame should use short-pass
// or long-kick ball mechanics.
static bool throwInUsesPass()
{
    return m_throwInUsesPass;
}

int getBallDefensivePositionX()
{
    return m_ballDefensivePosition.x;
}

bool isBallInGoalkeeperArea()
{
    return m_ballInGoalkeeperArea;
}

int getStrikeDestinationX()
{
    return m_strikeDestinationX;
}

int getBallQuadrantIndex()
{
    return m_ballQuadrantIndex;
}

void setBallQuadrantIndex(int quadrant)
{
    assert(quadrant >= 0 && quadrant < kNumBallQuadrants);
    m_ballQuadrantIndex = static_cast<uint16_t>(quadrant);
}

// Store the ball-release mode selected when the throw animation begins.
static void setThrowInUsesPass(bool pass)
{
    m_throwInUsesPass = pass;
}

// Release the ball after the throw animation reaches its contact frame and
// restore ordinary play for both teams. Short throws use passing mechanics;
// long throws use the regular kicking path.
static void takeThrowIn(TeamGeneralInfo& team, Sprite& player, bool pass)
{
    swos.lastTeamPlayed = &team;
    m_lastPlayerPlayed = &player;
    swos.playerHadBall = true;

    if (pass) {
        doPass(team, player);
        swos.ballSprite.deltaZ = FixedPoint::fromRaw(1);
    } else {
        playerKickingBall(team, player);
    }

    team.lastHeadingTacklingPlayer = &player;
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

