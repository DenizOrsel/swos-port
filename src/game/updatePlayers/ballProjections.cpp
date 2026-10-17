// Implementation fragment included by updatePlayers.cpp; do not compile separately.

// Returns the number of ticks needed to cover an integer distance at the given
// 16.16 movement delta. SWOS rounds exact divisions up by one tick. For deltas
// below one pixel per tick, it calculates in progressively larger tick groups;
// retaining that grouping is required for replay-compatible rounding.
static uint16_t getFramesNeededToCoverDistance(FixedPoint movementDelta, int distance)
{
    if (!movementDelta)
        return 0;

    assert(distance >= 0);
    uint64_t delta = movementDelta.raw() < 0 ? -static_cast<int64_t>(movementDelta.raw()) : movementDelta.raw();
    uint16_t ticksPerStep = 1;

    while (delta < 1_fp.raw()) {
        delta <<= 1;
        ticksPerStep <<= 1;
    }

    uint64_t distanceRaw = static_cast<uint64_t>(distance) << 16;
    uint64_t steps = distanceRaw / delta + 1;
    return static_cast<uint16_t>(steps * ticksPerStep);
}

// Predicts the ball positions used by goalkeeper decision-making and shot
// statistics. Depending on the direction of travel, it projects the trajectory
// to the goalkeeper, to two nearby goal-line reference points, or to the
// goalkeeper's x coordinate for a ball travelling mostly sideways.
//
// The original routine deliberately retains the vertical-velocity decrement
// from the first tick beyond each reference line while rewinding the position
// to the preceding tick. Later projections therefore start with that advanced
// velocity. Keeping this quirk is required for replay-compatible predictions.
static void updateGoalkeeperBallProjections(const Sprite& goalkeeper, const Sprite& ball, const TeamGeneralInfo& team)
{
    constexpr auto kMinimumProjectionSpeed = 1.0_fp;
    constexpr auto kTopNotHighLine = 135.0_fp;
    constexpr FixedPoint kTopGoalLine{kTopPitchLine};
    constexpr auto kBottomNotHighLine = 763.0_fp;
    constexpr FixedPoint kBottomGoalLine{kBottomPitchLine};

    struct Trajectory
    {
        FixedPoint x;
        FixedPoint y;
        FixedPoint z;
        FixedPoint deltaZ;
    } trajectory = { ball.x, ball.y, ball.z, ball.deltaZ };

    const auto airFriction = FixedPoint::fromRaw(getBallAirFriction());

    auto advanceOneTick = [&] {
        trajectory.deltaZ -= airFriction;
        trajectory.z += trajectory.deltaZ;
        trajectory.x += ball.deltaX;
        trajectory.y += ball.deltaY;
    };

    auto rewindPositionOneTick = [&] {
        trajectory.x -= ball.deltaX;
        trajectory.y -= ball.deltaY;
        trajectory.z -= trajectory.deltaZ;
        // Do not restore deltaZ: the original predictor keeps the air-friction
        // step from the overshooting tick.
    };

    auto projectedPosition = [&] {
        return BallProjection {
            trajectory.x.whole(),
            trajectory.y.whole(),
            static_cast<int16_t>(std::max<int>(trajectory.z.whole(), 0)),
        };
    };

    auto currentBallPosition = [&] {
        return BallProjection { ball.x.whole(), ball.y.whole(), ball.z.whole() };
    };

    auto finishWithoutGoalLineProjection = [&] {
        m_ballNotHighPosition = m_ballDefensivePosition;
        m_strikeDestinationX = 0;
    };

    if (ball.deltaY < -kMinimumProjectionSpeed) {
        if (&team != &swos.topTeamData) {
            m_ballDefensivePosition = currentBallPosition();
            finishWithoutGoalLineProjection();
            return;
        }

        // Project toward the top goalkeeper when the ball has not passed them.
        if (trajectory.y >= goalkeeper.y) {
            do {
                advanceOneTick();
            } while (trajectory.y >= goalkeeper.y);
            rewindPositionOneTick();
            m_ballDefensivePosition = projectedPosition();
        } else {
            m_ballDefensivePosition = currentBallPosition();
        }

        // The 135-pixel line supplies the lower-height sample used to choose a
        // goalkeeper jump, while kTopPitchLine is the actual goal line used by shot
        // statistics. Each projection continues from the previous one.
        if (trajectory.y >= kTopNotHighLine) {
            do {
                advanceOneTick();
            } while (trajectory.y >= kTopNotHighLine);
            rewindPositionOneTick();
            trajectory.y = kTopNotHighLine;
            m_ballNotHighPosition = projectedPosition();
        } else {
            m_ballNotHighPosition = m_ballDefensivePosition;
        }

        if (trajectory.y >= kTopGoalLine) {
            do {
                advanceOneTick();
            } while (trajectory.y >= kTopGoalLine);
            rewindPositionOneTick();
            trajectory.y = kTopGoalLine;
            m_strikeDestinationX = projectedPosition().x;
        } else {
            m_strikeDestinationX = 0;
        }
    } else if (ball.deltaY > kMinimumProjectionSpeed) {
        if (&team != &swos.bottomTeamData) {
            m_ballDefensivePosition = currentBallPosition();
            finishWithoutGoalLineProjection();
            return;
        }

        if (trajectory.y < goalkeeper.y) {
            do {
                advanceOneTick();
            } while (trajectory.y < goalkeeper.y);
            rewindPositionOneTick();
            m_ballDefensivePosition = projectedPosition();
        } else {
            m_ballDefensivePosition = currentBallPosition();
        }

        if (trajectory.y < kBottomNotHighLine) {
            do {
                advanceOneTick();
            } while (trajectory.y < kBottomNotHighLine);
            rewindPositionOneTick();
            trajectory.y = kBottomNotHighLine;
            m_ballNotHighPosition = projectedPosition();
        } else {
            m_ballNotHighPosition = m_ballDefensivePosition;
        }

        if (trajectory.y < kBottomGoalLine) {
            do {
                advanceOneTick();
            } while (trajectory.y < kBottomGoalLine);
            rewindPositionOneTick();
            trajectory.y = kBottomGoalLine;
            m_strikeDestinationX = projectedPosition().x;
        } else {
            m_strikeDestinationX = 0;
        }
    } else {
        // For mostly horizontal travel, predict where the ball crosses the
        // goalkeeper's x coordinate. There are no separate goal-line samples.
        bool projectLeft = ball.deltaX < -kMinimumProjectionSpeed && trajectory.x >= goalkeeper.x;
        bool projectRight = ball.deltaX > kMinimumProjectionSpeed && trajectory.x < goalkeeper.x;
        if (projectLeft || projectRight) {
            do {
                advanceOneTick();
            } while (projectLeft ? trajectory.x >= goalkeeper.x : trajectory.x < goalkeeper.x);
            rewindPositionOneTick();
            m_ballDefensivePosition = projectedPosition();
        } else {
            m_ballDefensivePosition = currentBallPosition();
        }

        finishWithoutGoalLineProjection();
    }
}

// Projects where a moving airborne ball will next reach its effective ground
// plane. Goalkeeper logic uses this point to decide whether to intercept, dive,
// or catch the ball.
//
// The original game does not always project to z == 0. It first chooses a
// landing-plane height from 20 down to 13 pixels, selecting the highest plane
// that is not above the ball. This approximates the useful contact height for a
// goalkeeper. The subsequent loop simulates the real per-tick order exactly:
// move horizontally, apply air friction to vertical velocity, then move in z.
static void calculateBallNextGroundXYPositions(const Sprite& ball)
{
    constexpr auto kMinimumProjectionSpeed = 1.0_fp;
    constexpr auto kInitialLandingPlaneHeight = 20.0_fp;
    constexpr auto kMinimumLandingPlaneHeight = 13.0_fp;

    // Very slow horizontal movement is treated as a standing ball, even if it
    // is still moving vertically. The comparisons are deliberately strict, so
    // exactly +/-1 pixel per tick remains classified as standing.
    if (ball.deltaX >= -kMinimumProjectionSpeed && ball.deltaX <= kMinimumProjectionSpeed &&
        ball.deltaY >= -kMinimumProjectionSpeed && ball.deltaY <= kMinimumProjectionSpeed) {
        m_ballNextGroundX = -1;
        return;
    }

    FixedPoint landingPlaneHeight = kInitialLandingPlaneHeight;
    while (ball.z < landingPlaneHeight) {
        if (landingPlaneHeight == kMinimumLandingPlaneHeight) {
            m_ballNextGroundX = -1;
            return;
        }
        landingPlaneHeight -= 1;
    }

    FixedPoint projectedX = ball.x;
    FixedPoint projectedY = ball.y;
    FixedPoint projectedHeight = ball.z - landingPlaneHeight;
    FixedPoint projectedDeltaZ = ball.deltaZ;
    const FixedPoint airFriction = FixedPoint::fromRaw(getBallAirFriction());

    do {
        projectedX += ball.deltaX;
        projectedY += ball.deltaY;
        projectedDeltaZ -= airFriction;
        projectedHeight += projectedDeltaZ;
    } while (projectedHeight.raw() >= 0);

    // Only x/y survive. The assembly restored the selected plane to z, then
    // wrote that result to a dead variable and immediately cleared it.
    m_ballNextGroundX = projectedX.whole();
    m_ballNextGroundY = projectedY.whole();
}

