// Implementation fragment included by updatePlayers.cpp; do not compile separately.

// Starts a tackle after either the player-control or CPU decision path has selected it.
// The player lunges in the requested direction until the tackle state logic takes over.
static void playerBeginTackling(TeamGeneralInfo& team, Sprite& player, Direction direction)
{
    assert(isValidDirection(direction));

    player.tackleState = TackleState::kStarted;
    team.controlledPlDirection = direction;
    player.direction = direction;
    setPlayerAnimationTable(player, getPlayerTacklingAnimTable());
    player.state = PlayerState::kTackling;

    // SWOS first writes the normal interval (or 25 for the unused faster-tackle flag),
    // then unconditionally replaces it with -1. Preserve that effective behavior.
    player.playerDownTimer = -1;

    const auto& destination = getDefaultBallDestinations()[static_cast<int>(direction)];
    player.destX = player.x.whole() + destination.x;
    player.destY = player.y.whole() + destination.y;
    player.speed = kPlayerTacklingSpeed;
    player.tacklingTimer = 0;
}

// This function is called when this player was tackled. It sets player's state to
// tackled and sets animation table. Also randomly sets injury and injury level.
static void playerTackled(TeamGeneralInfo& team, Sprite& player)
{
    static constexpr uint8_t kInjuryProbability[] = { 48, 28, 20, 14 };
    static constexpr uint8_t kPreviouslyInjuredProbability[] = { 96, 57, 41, 28 };
    static constexpr uint8_t kInjurySeverityWeights[] = { 42, 7, 5, 4, 3, 2, 1 };
    static constexpr uint8_t kPreviouslyInjuredSeverityWeights[] = { 14, 15, 12, 9, 7, 5, 2 };
    static constexpr uint16_t kPlayerDownTimes[] = { 0, 60, 70, 80, 90, 100, 110, 130 };

    constexpr uint8_t kInjurySeverityMask = 0xe0;
    constexpr uint8_t kLowestInjurySeverity = 0x20;
    constexpr int8_t kTackledDownTime = 50;

    auto& injuriesRemaining = team.teamNumber == 1 ?
        swos.team1NumAllowedInjuries : swos.team2NumAllowedInjuries;

    // Training reduces injury attempts to one in four. Ordinary matches always
    // reach the probability roll, provided this team may sustain another injury.
    bool tryInjury = !swos.g_trainingGame || (SWOS::rand() & 3) == 0;
    if (tryInjury && injuriesRemaining) {
        assert(team.inGameTeamPtr);
        assert(player.playerOrdinal >= 1 && player.playerOrdinal <= std::size(team.inGameTeamPtr->players));
        assert(swos.gameLengthInGame < std::size(kInjuryProbability));

        auto& playerInfo = team.inGameTeamPtr->players[player.playerOrdinal - 1];
        bool hasLowestSeverityInjury =
            (playerInfo.injuriesBitfield & kInjurySeverityMask) == kLowestInjurySeverity;
        const auto& injuryProbability = hasLowestSeverityInjury ?
            kPreviouslyInjuredProbability : kInjuryProbability;

        if (static_cast<uint8_t>(SWOS::rand()) < injuryProbability[swos.gameLengthInGame]) {
            playInjuryComment(team);

            const auto& severityWeights = hasLowestSeverityInjury ?
                kPreviouslyInjuredSeverityWeights : kInjurySeverityWeights;
            int randomSeverity = SWOS::rand() & 0x3f;
            size_t severityIndex = 0;
            while (randomSeverity >= severityWeights[severityIndex])
                randomSeverity -= severityWeights[severityIndex++];

            uint8_t encodedSeverity = static_cast<uint8_t>((severityIndex + 1) << 5);
            playerInfo.isInjured = true;
            if (encodedSeverity > playerInfo.injuriesBitfield)
                playerInfo.injuriesBitfield = encodedSeverity;

            player.injuryLevel = kPlayerDownTimes[encodedSeverity >> 5];
            injuriesRemaining--;
        }
    }

    player.state = PlayerState::kTackled;
    player.playerDownTimer = kTackledDownTime;
    setPlayerAnimationTable(player, getPlayerTackledAnimTable());
}

// Begins a flying/lob header attempt after the caller has confirmed that an outfield player fired
// in a valid direction, is close enough to the ball, and that the ball is above the static-header
// height range. Ball contact is handled later by playerHittingJumpHeader(). Player starts jumping
// in attempt to hit the ball with his head. Set his state and render him 'inoperable' for the next
// 55 cycles. Set his animation table, dest x and y, and his speed. Only triggers for flying and
// lob header attempts, not for static ones.
static void playerAttemptingJumpHeader(Sprite& player, Direction direction)
{
    assert(isValidDirection(direction));

    player.isHeadingBall = false;
    player.direction = direction;
    setPlayerAnimationTable(player, getJumpHeaderAttemptAnimTable());
    player.playerDownTimer = m_playerDownHeadingInterval;
    player.state = PlayerState::kJumpHeader;

    const auto& destination = getDefaultBallDestinations()[static_cast<int>(direction)];
    player.destX = player.x.whole() + destination.x;
    player.destY = player.y.whole() + destination.y;
    player.speed = kJumpHeaderSpeed;
}

// Place the thrower on the ball's touchline coordinate, three pixels outside
// the field, and synchronize the destination so no movement is introduced.
static void setThrowInPlayerCoordinates(Sprite& player)
{
    constexpr int kThrowInBallPlayerOffset = 3;

    int playerX = swos.ballSprite.x.whole();
    playerX += playerX >= kPitchCenterX ? kThrowInBallPlayerOffset : -kThrowInBallPlayerOffset;
    player.x.setWhole(playerX);
    player.destX = playerX;
    player.y.setWhole(swos.ballSprite.y.whole());
    player.destY = swos.ballSprite.y.whole();
}

// Derive an off-ball destination from the team's tactics and current ball
// quadrant. Goalkeepers instead track the ball within their restricted box;
// bottom-team tactics and coordinates are mirrored from the top-team tables.
static void setPlayerWithNoBallDestination(const TeamGeneralInfo& team, Sprite& player, int ballX, int ballY)
{
    static constexpr int kBottomTeamQuadrantMirror = 0xef;
    static constexpr int kPlayerApproachOffset = 4;
    static constexpr int kGoalkeeperLeftX = 285;
    static constexpr int kGoalkeeperRightX = 387;
    static constexpr int kTopGoalkeeperTopY = 135;
    static constexpr int kTopGoalkeeperBottomY = 161;
    static constexpr int kBottomGoalkeeperTopY = 737;
    static constexpr int kBottomGoalkeeperBottomY = 763;

    static constexpr int16_t kPlayerQuadrantXCoordinates[] = {
        98, 132, 166, 200, 234, 268, 302, 336, 370, 404, 438, 472, 506, 540, 574,
    };
    static constexpr int16_t kPlayerQuadrantYCoordinates[] = {
        149, 189, 229, 269, 309, 349, 389, 429, 469, 509, 549, 589, 629, 669, 709, 749,
    };

    bool isTopTeam = &team == &swos.topTeamData;

    if (player.isGoalkeeper()) {
        // Goalkeepers follow the ball, scaled into the narrow rectangle in front of their goal.
        constexpr int kPitchWidth = kRightThrowInLine - kLeftThrowInLine + 1;
        constexpr int kPitchHeight = kBottomPitchLine - kTopPitchLine + 1;
        constexpr int kGoalkeeperRangeX = kGoalkeeperRightX - kGoalkeeperLeftX + 1;
        constexpr int kGoalkeeperRangeY = kTopGoalkeeperBottomY - kTopGoalkeeperTopY + 1;

        player.destX = kGoalkeeperLeftX +
            static_cast<uint16_t>(ballX - kLeftThrowInLine) * kGoalkeeperRangeX / kPitchWidth;

        int goalkeeperTopY = isTopTeam ? kTopGoalkeeperTopY : kBottomGoalkeeperTopY;
        player.destY = goalkeeperTopY + static_cast<uint16_t>(ballY - kTopPitchLine) * kGoalkeeperRangeY / kPitchHeight;
    } else {
        assert(team.tactics < std::size(swos.g_tacticsTable));
        const TeamTactics *tactics = swos.g_tacticsTable[team.tactics];

        if (swos.gameState == GameState::kKeeperHoldsTheBall || swos.gameState == GameState::kGoalOutLeft ||
            swos.gameState == GameState::kGoalOutRight) {
            assert(tactics->ballOutOfPlayTactics < std::size(swos.g_tacticsTable));
            tactics = swos.g_tacticsTable[tactics->ballOutOfPlayTactics];
        }

        int playerIndex = player.playerOrdinal - 2;
        assert(playerIndex >= 0 && playerIndex < std::size(tactics->positions));
        assert(m_ballQuadrantIndex < kNumBallQuadrants);

        int ballQuadrant = m_ballQuadrantIndex;
        if (!isTopTeam)
            ballQuadrant = kNumBallQuadrants - 1 - ballQuadrant;

        assert(playerIndex >= 0 && playerIndex < std::size(tactics->positions));
        assert(ballQuadrant >= 0 && ballQuadrant < std::size(tactics->positions[0].positions));
        uint8_t destinationQuadrant = tactics->positions[playerIndex].positions[ballQuadrant];
        if (!isTopTeam)
            destinationQuadrant = kBottomTeamQuadrantMirror - destinationQuadrant;

        int xQuadrant = destinationQuadrant >> 4;
        int yQuadrant = destinationQuadrant & 0x0f;
        assert(xQuadrant < std::size(kPlayerQuadrantXCoordinates));
        assert(yQuadrant < std::size(kPlayerQuadrantYCoordinates));

        int approachOffset = isTopTeam ? -kPlayerApproachOffset : kPlayerApproachOffset;
        auto quadrantOffset = getPlayerQuadrantOffset();

        player.destX = std::clamp(kPlayerQuadrantXCoordinates[xQuadrant] + quadrantOffset.x + approachOffset,
            kLeftThrowInLine, kRightThrowInLine);
        player.destY = std::clamp(kPlayerQuadrantYCoordinates[yQuadrant] + quadrantOffset.y,
            kTopPitchLine, kBottomPitchLine);
    }
}

constexpr int kStaticHeaderDuration = 20;

// Players jumps in place trying to hit static header. Initial routine.
static void attemptStaticHeader(Sprite& player)
{
    constexpr auto kStaticHeaderPlayerSpeed = 0.5_speed;

    assert(isValidDirection(player.direction));

    player.isHeadingBall = false;

    const auto& destination = getDefaultBallDestinations()[static_cast<int>(player.direction)];
    player.destX = player.x.whole() + destination.x;
    player.destY = player.y.whole() + destination.y;
    player.speed = kStaticHeaderPlayerSpeed;

    setPlayerAnimationTable(player, getStaticHeaderAttemptAnimTable());
    player.state = PlayerState::kStaticHeader;
    player.playerDownTimer = kStaticHeaderDuration;
}

// Static header was activated now we need to aim it somewhere.
static void setStaticHeaderDirection(const TeamGeneralInfo& team, Sprite& player)
{
    auto direction = team.currentAllowedDirection;
    assert(direction == Direction::kNoDirection || isValidDirection(direction));

    if (hasDirection(direction) && player.playerDownTimer < kStaticHeaderDuration - 1 &&
        player.animTable == getStaticHeaderAttemptAnimTable()) {
        auto directionDiff = static_cast<Direction>((direction - player.direction) & 7);
        if (directionDiff != Direction::kTop && directionDiff != Direction::kBottom) {
            int delta = directionDiff < Direction::kBottom ? 1 : -1;
            player.direction = static_cast<Direction>((static_cast<int>(player.direction) + delta) & 7);
        }
    }
}

