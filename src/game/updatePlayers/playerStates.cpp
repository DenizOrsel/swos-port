// Implementation fragment included by updatePlayers.cpp; do not compile separately.

// Return a temporary-action state to the ordinary standing state. Movement is
// stopped separately by callers that need it; this helper only changes state
// and animation.
static void restoreNormalPlayerState(Sprite& player)
{
    player.state = PlayerState::kNormal;
    setPlayerAnimationTable(player, getPlayerNormalStandingAnimTable());
}

// A booked player remains in the card animation until the referee finishes
// displaying the card and clears whichCard.
static void updateBookedPlayerState(Sprite& player)
{
    if (!swos.whichCard)
        restoreNormalPlayerState(player);
}

// Sad/happy animations persist during the result sequence, but are cleared as
// soon as players start walking off for halftime or full time.
static void updateCelebratingPlayerState(Sprite& player)
{
    if (swos.gameState == GameState::kGoingToHalftime ||
        swos.gameState == GameState::kPlayersGoingToShower)
        restoreNormalPlayerState(player);
}

// Count down an ordinary knockdown and restore the standing animation when it expires.
static void updateDownPlayerState(Sprite& player)
{
    if (!--player.playerDownTimer)
        restoreNormalPlayerState(player);
}

// Hold the goalkeeper in the claim animation, then announce the completed save.
static void updateGoalkeeperClaimedState(Sprite& goalkeeper)
{
    if (!--goalkeeper.playerDownTimer) {
        restoreNormalPlayerState(goalkeeper);
        playKeeperClaimedComment();
    }
}

// Returns true when the recovered player must also be stopped at their current
// position. With permanent injuries enabled, the timer intentionally remains
// untouched and the injured animation continues indefinitely.
static bool updateInjuredPlayerState(Sprite& player)
{
    if (swos.injuriesForever || --player.playerDownTimer)
        return false;

    restoreNormalPlayerState(player);
    return true;
}

// Compatibility hook retained for callers that configure legacy timing. Tackle
// initialization currently reproduces SWOS's later unconditional -1 overwrite.
void setPlayerDownTacklingInterval(int)
{
    // Retained for compatibility. The original tackle initialization always overwrites this interval with -1.
}

// Override the duration of newly started flying-header attempts.
void setPlayerDownHeadingInterval(int interval)
{
    m_playerDownHeadingInterval = interval;
}

// Decides whether the goalkeeper should begin a dive for the current shot.
//
// A ball that has passed the keeper by no more than ten pixels still triggers a
// late dive. Penalties use only a randomized distance limit, ensuring that the
// keeper visibly commits to a side. During normal play SWOS compares three
// timings: the ball reaching the keeper, the keeper's current horizontal motion,
// and a dive at the speed selected by the goalkeeper's skill table.
static bool shouldGoalkeeperDive(const Sprite& goalkeeper, const Sprite& ball, const TeamGeneralInfo& team)
{
    constexpr int kLateDiveDistance = 10;
    constexpr int kPenaltySaveDistanceFar = 20;
    constexpr int kPenaltySaveDistanceNear = 12;
    constexpr int kPenaltyDistanceRandomMask = 0x18;

    // Positive means that the ball is still in front of the goalkeeper in the
    // direction from midfield toward this team's goal.
    int goalwardBallDistance = ball.y.whole() - goalkeeper.y.whole();
    if (&team == &swos.bottomTeamData)
        goalwardBallDistance = -goalwardBallDistance;

    if (goalwardBallDistance < 0)
        return goalwardBallDistance >= -kLateDiveDistance;

    if (swos.playingPenalties || swos.penalty) {
        // Bits 3 and 4 are both clear one quarter of the time, granting the
        // longer activation range; the other three combinations use the near
        // range. Once inside that range, a penalty keeper always dives even if
        // the following reachability calculation would reject the attempt.
        int saveDistance = (SWOS::rand() & kPenaltyDistanceRandomMask) == 0 ?
            kPenaltySaveDistanceFar : kPenaltySaveDistanceNear;
        return goalwardBallDistance <= saveDistance;
    }

    if (goalwardBallDistance > swos.kKeeperSaveDistance)
        return false;

    int distanceToKeeperY = std::abs(ball.y.whole() - goalkeeper.y.whole());
    uint16_t ballArrivalFrames = getFramesNeededToCoverDistance(ball.deltaY, distanceToKeeperY);
    if (!ballArrivalFrames)
        return false;

    int horizontalDistance = std::abs(m_ballDefensivePosition.x - goalkeeper.x.whole());
    uint16_t currentMovementFrames =
        getFramesNeededToCoverDistance(goalkeeper.deltaX, horizontalDistance);

    // If the keeper's existing horizontal movement reaches the interception
    // point no later than the ball, continuing to run is preferred over diving.
    if (!currentMovementFrames || currentMovementFrames <= ballArrivalFrames)
        return false;

    assert(team.shotChanceTable);
    const auto& shotChanceTable = *team.shotChanceTable;
    int diveSpeedIndex = std::max<int>(
        shotChanceTable.goalkeeperDiveSpeedIndex[swos.currentGameTick & 0x0f] - 1, 0);
    assert(diveSpeedIndex < std::size(swos.kGoalkeeperDiveDeltas));

    uint16_t diveFrames = getFramesNeededToCoverDistance(
        swos.kGoalkeeperDiveDeltas[diveSpeedIndex], horizontalDistance);
    if (!diveFrames)
        return false;

    // This comparison looks reversed at first glance. A dive is started only
    // when its calculated travel time is at least the ball-arrival time; faster
    // cases are left to the keeper's ordinary movement. This intentionally
    // preserves the original decision rule.
    return diveFrames >= ballArrivalFrames;
}

// Starts the goalkeeper's dive animation and movement after shot resolution decides
// that a dive should be shown. This is called both for a genuine save attempt and
// for a shot that has already beaten the goalkeeper, where the failed dive is still
// part of the visual result.
static void goalkeeperJumping(TeamGeneralInfo& team, Sprite& goalkeeper, Direction controlledDirection,
    Direction diveDirection, GoalkeeperJumpSpeedMode speedMode, uint16_t ballHeight)
{
    constexpr uint32_t kNearBallDistance = 128;
    constexpr uint16_t kLowDiveMaximumBallHeight = 5;
    constexpr int8_t kDiveDownTime = 75;

    assert(isValidDirection(controlledDirection));
    assert(isValidDirection(diveDirection));

    if (goalkeeper.ballDistance <= kNearBallDistance) {
        goalkeeper.speed = kGoalkeeperNearJumpSpeed;
    } else {
        switch (speedMode) {
        case GoalkeeperJumpSpeedMode::kFast:
            goalkeeper.speed = kGoalkeeperFarJumpSpeed;
            break;

        case GoalkeeperJumpSpeedMode::kSlower:
            goalkeeper.speed = kGoalkeeperFarJumpSlowerSpeed;
            break;

        default:
            // The original game varies failed/late dives using the low tick byte.
            goalkeeper.speed = kGoalkeeperNearJumpSpeed + (swos.currentGameTick & 0xff);
            break;
        }
    }

    // Clear the current dive-side bookkeeping before the movement code determines it again.
    team.goalkeeperDiveContactMade = false;
    team.goalkeeperDivingRight = 0;

    bool diveHigh = ballHeight > kLowDiveMaximumBallHeight;
    goalkeeper.state = diveHigh ? PlayerState::kGoalieDivingHigh : PlayerState::kGoalieDivingLow;

    bool isBottomGoalkeeper = &team == &swos.bottomTeamData;
    int16_t animationTable;
    if (diveHigh) {
        animationTable = isBottomGoalkeeper ?
            getBottomGoalieDivingHighAnimTable() : getTopGoalieDivingHighAnimTable();
    } else {
        animationTable = isBottomGoalkeeper ?
            getBottomGoalieDivingLowAnimTable() : getTopGoalieDivingLowAnimTable();
    }
    setPlayerAnimationTable(goalkeeper, animationTable);

    goalkeeper.playerDownTimer = kDiveDownTime;
    team.controlledPlDirection = controlledDirection;

    const auto& destination = getDefaultBallDestinations()[static_cast<int>(diveDirection)];
    goalkeeper.destX = goalkeeper.x.whole() + destination.x;
    goalkeeper.destY = goalkeeper.y.whole() + destination.y;
}

// Called after the shot-resolution checks establish that the goalkeeper can catch
// the ball: its projected defensive position is within 12 pixels of the keeper,
// its height is catchable, and the ball is within the permitted distance.
static void goalkeeperCaughtTheBall(TeamGeneralInfo& team, Sprite& goalkeeper,
    int ballNextGroundX, int ballNextGroundY)
{
    constexpr int8_t kCatchingBallTime = 15;
    constexpr int kCatchDestinationPitchInset = 8;

    goalkeeper.direction = &team == &swos.topTeamData ? Direction::kBottom : Direction::kTop;
    team.goalkeeperDivingLeft = 0;

    setPlayerAnimationTable(goalkeeper, getGoalkeeperJumpUpAnimTable());
    goalkeeper.state = PlayerState::kGoalieCatchingBall;
    goalkeeper.playerDownTimer = kCatchingBallTime;
    goalkeeper.speed = kGoalkeeperCatchSpeed;
    goalkeeper.destX = ballNextGroundX;
    goalkeeper.destY = std::clamp(ballNextGroundY, kTopPitchLine + kCatchDestinationPitchInset,
        kBottomPitchLine - kCatchDestinationPitchInset);
}

// Continue the keeper's upward catch. While the keeper is close enough to the
// ball, the team's close-range handling value decides whether the ball is held
// or deflected. goalkeeperDivingLeft doubles here as the original catch latch.
static void updateGoalkeeperCatchingBall(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball)
{
    if (!--goalkeeper.playerDownTimer) {
        restoreNormalPlayerState(goalkeeper);
        if (team.goalkeeperDivingLeft) {
            team.goalkeeperDivingLeft = false;
            updateBallWithControllingGoalkeeper(goalkeeper);
            goalkeeperClaimedTheBall(team, goalkeeper, ball);
            ball.z.setWhole(5);
        }
        return;
    }

    if (team.goalkeeperDivingLeft || !team.plVeryCloseToBall)
        return;

    assert(team.shotChanceTable);
    const auto& shotChanceTable = *team.shotChanceTable;
    int catchRoll = (swos.currentGameTick & 0xf0) >> 4;
    if (catchRoll < shotChanceTable.closeBallCatchChance) {
        goalkeeper.speed = 0;
        ball.speed = 0;
        team.goalkeeperDivingLeft = true;
        goalkeeperClaimedTheBall(team, goalkeeper, ball);
    } else {
        goalkeeperDeflectedBall(team, ball);
    }

    playKeeperClaimedComment();
}

// End a dive when its timer expires. A keeper who latched onto the ball during
// the dive rises earlier, once the remaining timer reaches 42 ticks.
static bool tryFinishingGoalkeeperDive(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball)
{
    if (!--goalkeeper.playerDownTimer) {
        restoreNormalPlayerState(goalkeeper);
        return true;
    }

    if (!team.goalkeeperDivingRight || goalkeeper.playerDownTimer > 42)
        return false;

    team.goalkeeperDivingRight = false;
    updateBallWithControllingGoalkeeper(goalkeeper);
    goalkeeperClaimedTheBall(team, goalkeeper, ball);
    ball.z.setWhole(5);
    restoreNormalPlayerState(goalkeeper);
    return true;
}

// Select the per-tick slowdown while a goalkeeper is completing a dive. Grounded
// animation frames stop faster; otherwise motion toward the team's goal line
// receives the slightly stronger slowdown used by the original game.
static int16_t getDivingGoalkeeperSpeedReduction(const TeamGeneralInfo& team,
    const Sprite& goalkeeper, const Sprite& ball)
{
    constexpr int16_t kNormalReduction = 112;
    constexpr int16_t kTowardGoalReduction = 128;
    constexpr int16_t kGroundedReduction = 192;

    if (team.goalkeeperDiveContactMade)
        return kNormalReduction;

    switch (goalkeeper.imageIndex) {
    case kTeam1TopGoalkeeperCaughtBallRightSprite:
    case kTeam1TopGoalkeeperCaughtBallLeftSprite:
    case kTeam1BottomGoalkeeperCaughtBallRightSprite:
    case kTeam1BottomGoalkeeperCaughtBallLeftSprite:
    case kTeam2TopGoalkeeperCaughtBallRightSprite:
    case kTeam2TopGoalkeeperCaughtBallLeftSprite:
    case kTeam2BottomGoalkeeperCaughtBallRightSprite:
    case kTeam2BottomGoalkeeperCaughtBallLeftSprite:
        return kGroundedReduction;
    }

    int ballYDistance = ball.y.whole() - goalkeeper.y.whole();
    bool movingTowardGoal = &team == &swos.topTeamData ? ballYDistance < -5 : ballYDistance > 5;
    return movingTowardGoal ? kTowardGoalReduction : kNormalReduction;
}

// Test the ball against the horizontal extent of the current dive frame. The
// original bounds are asymmetric and use different endpoint rules depending
// on travel direction, so preserve them explicitly.
static bool isBallWithinDivingGoalkeeperWidth(const Sprite& goalkeeper, const Sprite& ball)
{
    if (goalkeeper.imageIndex < 0)
        return false;

    auto sprites = *swos.g_spriteGraphicsPtr;
    int frameWidth = sprites[goalkeeper.imageIndex].width + 6;
    int goalkeeperX = goalkeeper.x.whole();
    int ballX = ball.x.whole();

    if (goalkeeper.deltaX.raw() < 0) {
        int left = goalkeeperX - 5;
        return ballX > left && ballX < left + frameWidth;
    }

    int right = goalkeeperX + 5;
    return ballX >= right - frameWidth && ballX <= right;
}

// Check the vertical portion of the dive hitbox and apply the preliminary ball
// response. Penalties use a tighter range and slow the shot; ordinary play
// instead cancels any pending pass reception by the opponents.
static bool tryDivingGoalkeeperBallContact(TeamGeneralInfo& team, const Sprite& goalkeeper, Sprite& ball)
{
    if (ball.z.whole() > 20)
        return false;

    bool penaltyPlay = swos.playingPenalties || swos.penalty;
    int verticalTolerance = penaltyPlay ? 5 : 7;
    if (std::abs(goalkeeper.y.whole() - ball.y.whole()) > verticalTolerance)
        return false;

    if (penaltyPlay) {
        ball.speed -= static_cast<uint16_t>(ball.speed) >> 2;
    } else {
        team.passKickTimer = kPassKickTimerDuration;
        team.opponentTeam->passingToPlayer = false;
        team.opponentTeam->passingKickingPlayer.reset();
    }
    return true;
}

// A favorable handling roll becomes a catch only when the ball is still near
// the keeper's center. Attach the ball and latch the dive until the rise phase.
static bool tryDivingGoalkeeperCatch(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball)
{
    if (std::abs(goalkeeper.x.whole() - ball.x.whole()) > 6)
        return false;

    swos.lastTeamPlayed = &team;
    m_lastPlayerPlayed = &goalkeeper;
    ball.x.setWhole(goalkeeper.x.whole());
    ball.y.setWhole(goalkeeper.y.whole());
    ball.destX = ball.x.whole();
    ball.destY = ball.y.whole();
    ball.speed = 0;
    team.goalkeeperDivingRight = true;

    goalkeeperClaimedTheBall(team, goalkeeper, ball);
    playGoalkeeperSavedComment();
    playMissGoalSample();
    return true;
}

// Use the two handling thresholds at offsets 42 and 44 of the selected table.
// The original swaps the two deflection variants while a negative save-comment
// timer is active, but the catch range remains unchanged.
static DivingGoalkeeperContactResult chooseDivingGoalkeeperContactResult(
    int catchChance, int secondaryChance, int16_t savedCommentTimer)
{
    int roll = (swos.currentGameTick & 0xf0) >> 4;
    if (roll < catchChance)
        return DivingGoalkeeperContactResult::kCatch;

    bool withinSecondaryRange = roll - catchChance < secondaryChance;
    bool usePrimaryDeflection = withinSecondaryRange == (savedCommentTimer >= 0);
    return usePrimaryDeflection ?
        DivingGoalkeeperContactResult::kPrimaryDeflection :
        DivingGoalkeeperContactResult::kSecondaryDeflection;
}

// Apply the gentler of the two dive deflections. During the negative comment
// cooldown it adds horizontal variation; otherwise it moves the ball partway
// toward its Y destination using the original tick-selected smoothing shift.
static void applyPrimaryGoalkeeperDeflection(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball)
{
    static constexpr uint8_t kDestinationYShifts[] = { 1, 4, 3, 3, 3, 2, 2, 2 };

    swos.lastKeeperPlayed = m_lastPlayerPlayed;
    swos.lastTeamPlayed = &team;
    m_lastPlayerPlayed = &goalkeeper;
    ball.speed -= static_cast<uint16_t>(ball.speed) >> 2;
    if (swos.ballSprite.speed > 3.5_speed)
        swos.ballSprite.speed = 3.5_speed;

    if (team.goalkeeperSavedCommentTimer < 0) {
        ball.destX += ((swos.currentGameTick & 0x0f) << 7) - 960;
    } else {
        int shift = kDestinationYShifts[(swos.currentGameTick & 0x0e) >> 1];
        ball.destY = ball.y.whole() + ((ball.destY - ball.y.whole()) >> shift);
        ball.destX = goalkeeper.destX;
    }
}

// Apply the stronger dive deflection: reflect the current Y destination around
// the ball, add deterministic variation, and remove either 1/4 or 3/8 speed.
static void applySecondaryGoalkeeperDeflection(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball)
{
    swos.lastTeamPlayed = &team;
    m_lastPlayerPlayed = &goalkeeper;
    ball.destY = 2 * ball.y.whole() - ball.destY + ((swos.currentGameTick & 0x1f) << 4) - 256;
    ball.destX = goalkeeper.destX;

    auto speedReduction = static_cast<uint16_t>(ball.speed) >> 2;
    ball.speed -= speedReduction;
    if (swos.currentGameTick & 0x10)
        ball.speed -= speedReduction >> 1;
    if (swos.ballSprite.speed > 3.0_speed)
        swos.ballSprite.speed = 3.0_speed;
}

// Finish either deflection just beyond the keeper toward the corresponding
// goal line, then reset pass state and play the common save feedback.
static void finishDivingGoalkeeperDeflection(TeamGeneralInfo& team, const Sprite& goalkeeper, Sprite& ball)
{
    int goalLineOffset = &team == &swos.topTeamData ? 1 : -1;
    ball.y.setWhole(goalkeeper.y.whole() + goalLineOffset);
    team.passKickTimer = kPassKickTimerDuration;
    team.opponentTeam->passingToPlayer = false;
    if (team.goalkeeperSavedCommentTimer >= 0)
        team.opponentTeam->passingKickingPlayer.reset();

    playGoalkeeperSavedComment();
    playMissGoalSample();
}

// Treat playerTurnFlags as an eight-direction mask, rejecting kNoDirection and
// any corrupt/out-of-range direction before shifting.
static bool isRestartTurnDirectionAllowed(uint16_t turnFlags, Direction direction)
{
    int directionIndex = static_cast<int16_t>(direction);
    return directionIndex >= 0 && directionIndex < 8 && (turnFlags & (1u << directionIndex));
}

// During a restart only the team awarded the restart remains controllable. Its
// player must face one of playerTurnFlags; camera direction is tried first,
// followed by the highest permitted direction, matching the original search.
static bool prepareControlledPlayerForBreak(TeamGeneralInfo& team, Sprite& player)
{
    if (swos.gameStatePl == GameState::kInProgress)
        return true;
    if (swos.lastTeamPlayedBeforeBreak != &team)
        return false;

    auto turnFlags = swos.playerTurnFlags;
    if (!isRestartTurnDirectionAllowed(turnFlags, player.direction))
        player.direction = swos.cameraDirection;

    if (!isRestartTurnDirectionAllowed(turnFlags, player.direction)) {
        int directionIndex = 7;
        while (directionIndex >= 0 && !(turnFlags & (1u << directionIndex)))
            directionIndex--;
        if (directionIndex < 0)
            directionIndex = 0;

        swos.cameraDirection = static_cast<Direction>(directionIndex);
        player.direction = swos.cameraDirection;
    }
    return true;
}

// Hide the result overlay when restart input is accepted. CPU teams wait up to
// 55 ticks; human teams may also advance immediately once a direction is set.
static void handleControlledPlayerBreakInput(const TeamGeneralInfo& team)
{
    bool hideResult = false;
    bool inputAccepted = false;

    if (!team.playerNumber) {
        hideResult = swos.stoppageTimerActive > 55;
    } else if (static_cast<int16_t>(team.currentAllowedDirection) >= 0) {
        inputAccepted = true;
    }

    if (!hideResult && !inputAccepted)
        inputAccepted = team.firePressed || team.quickFire || team.normalFire;

    if (inputAccepted) {
        if (swos.statsTimer)
            swos.fireBlocked = true;
        hideResult = true;
    }

    if (hideResult)
        swos.resultTimer = -swos.resultTimer;
}

// With no ground movement direction, fire initiates a standing header when a
// human-controlled outfield player is near a ball at heading height.
static void tryControlledPlayerStaticHeader(TeamGeneralInfo& team, Sprite& player)
{
    if (team.playerNumber && !player.isGoalkeeper() && team.fireThisFrame &&
        (team.plVeryCloseToBall || team.plCloseToBall || team.plNotFarFromBall) &&
        (team.ball8To12 || team.ball12To17 || team.ballAbove17)) {
        team.headerOrTackle = true;
        attemptStaticHeader(player);
        team.lastHeadingTacklingPlayer = &player;
        team.ballCanBeControlled = false;
    }
}

