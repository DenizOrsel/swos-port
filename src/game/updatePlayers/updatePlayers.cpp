#include "updatePlayers.h"
#include "sfx.h"
#include "ball.h"
#include "player.h"
#include "referee.h"
#include "team.h"
#include "amigaMode.h"
#include "comments.h"
#include "animation.h"
#include "ai.h"
#include "pitchConstants.h"
#include "random.h"
#include "result.h"
#include "restartPositions.h"
#include "sprites.h"
#include "updateSprite.h"

static constexpr auto kGoalkeeperCatchSpeed = 1.5_speed;
static constexpr auto kGoalkeeperMoveToBallSpeed = 2.0_speed;
static constexpr auto kGoalkeeperNearJumpSpeed = 2.0_speed;
static constexpr auto kGoalkeeperFarJumpSpeed = 4.0_speed;
static constexpr auto kGoalkeeperFarJumpSlowerSpeed = 2.5_speed;
static constexpr auto kGoalkeeperGameSpeed = 2.0_speed;
static constexpr auto kShotAtGoalMinimumSpeed = 1.0_speed;
static constexpr auto kPlayerTacklingSpeed = 3.5_speed;
static constexpr auto kJumpHeaderSpeed = 4.0_speed;
static constexpr auto kJumpHeaderAirResistance = 0.140625_speed;
static constexpr auto kPlayerGroundFriction = 0.1875_speed;
static constexpr auto kInjuryForeverSpeed = 1.5_speed;
static constexpr auto kSubstitutionSpeed = 3.0_speed;
static constexpr int kPlayerSwitchCooldown = 25;
static constexpr int kPassKickTimerDuration = 25;
static constexpr int16_t kFreeKickFactors[] = { 0, 3, 4, 5, 4, 3, 0 };
static constexpr uint8_t kPersistentCardValues[] = { 0, 1, 3, 3, 3 };
static constexpr uint8_t kPersistentCardValuesWithPreviousCards[] = { 1, 2, 4, 4, 4 };
static constexpr uint16_t kYellowCard = 1;
static constexpr uint16_t kRedCard = 2;
static constexpr uint16_t kSecondYellowCard = 3;

static constexpr int dseg_1105EF = 512;
static constexpr int dseg_110611 = 112;
static constexpr int dseg_110613 = 192;
static constexpr int dseg_110615 = 128;

enum class GoalkeeperJumpSpeedMode : uint16_t
{
    kFast,
    kSlower,
    kRandomSlow,
};

enum class JumpHeaderMovementResult
{
    kStopPlayer,
    kUpdatePosition,
    kCheckPitchBounds,
};

enum class JumpHeaderBoundaryResult
{
    kSlowDown,
    kFinish,
    kTryHeadingBall,
};

enum class DivingGoalkeeperContactResult
{
    kCatch,
    kPrimaryDeflection,
    kSecondaryDeflection,
};

struct BallProjection
{
    int16_t x;
    int16_t y;
    int16_t z;
};

struct PositionOffset
{
    int16_t x;
    int16_t y;
};

static constexpr PositionOffset kPassAssistOffsets[] = {
    { 0, -1000 }, { 199, -1000 }, { 414, -1000 }, { 668, -1000 },
    { 1000, -1000 }, { 1000, -668 }, { 1000, -414 }, { 1000, -199 },
    { 1000, 0 }, { 1000, 199 }, { 1000, 414 }, { 1000, 668 },
    { 1000, 1000 }, { 668, 1000 }, { 414, 1000 }, { 199, 1000 },
    { 0, 1000 }, { -199, 1000 }, { -414, 1000 }, { -668, 1000 },
    { -1000, 1000 }, { -1000, 668 }, { -1000, 414 }, { -1000, 199 },
    { -1000, 0 }, { -1000, -199 }, { -1000, -414 }, { -1000, -668 },
    { -1000, -1000 }, { -668, -1000 }, { -414, -1000 }, { -199, -1000 },
};

static int m_playerDownHeadingInterval = 55;

static Sprite *m_lastPlayerPlayed;
static Sprite *m_prevLastPlayer;
static Sprite *m_lastPlayerBeforeGoalkeeper;
static TeamGeneralInfo *m_prevLastTeamPlayed;

static BallProjection m_ballDefensivePosition;
static BallProjection m_ballNotHighPosition;
static int16_t m_ballNextGroundX = -1;
static int16_t m_ballNextGroundY;
static int16_t m_freeKickDestinationX;
static int16_t m_strikeDestinationX;
static uint16_t m_ballQuadrantIndex;
static bool m_ballInUpperPenaltyArea;
static bool m_ballInLowerPenaltyArea;
static bool m_ballInGoalkeeperArea;
static bool m_throwInUsesPass;

static void preparePlayersUpdate(TeamGeneralInfo& team);
static void updateAllPlayers(TeamGeneralInfo& team);
static void finishUpdatingAllPlayers();
static void finalizePlayerUpdate(const TeamGeneralInfo& team, Sprite& player);
static void classifyBallForPlayer(TeamGeneralInfo& team, const Sprite& player, const Sprite& ball);
static bool updateTackledPlayer(Sprite& player);
static void restoreNormalPlayerState(Sprite& player);
static void tryStartPostMatchReaction(const TeamGeneralInfo& team, Sprite& player);
static bool shouldWaitForRestartSetup();
static bool updatePlayerAwaitingCard(Sprite& player);
static bool updateSubstitutionMovement(Sprite& player);
static uint16_t selectRestartPositionIndex(const TeamGeneralInfo& team, const Sprite& player);
static bool setRestartPosition(const TeamGeneralInfo& team, Sprite& player, const RestartPositions& positions,
    uint16_t positionIndex, int ballX, int ballY);
static void moveDefenderAwayFromRestart(const TeamGeneralInfo& team, Sprite& player);
static bool setPlayerBallChaseDestination(TeamGeneralInfo& team, Sprite& player);
static bool shouldCancelGoalkeeperPass(const TeamGeneralInfo& team, const Sprite& player);
static bool shouldStopPlayerForGoalOut(const Sprite& player);
static bool tryStartingPassReceiverTackle(TeamGeneralInfo& team, Sprite& player);
static bool passReceiverCanControlBall(const TeamGeneralInfo& team);
static void passReceiverTakesControl(TeamGeneralInfo& team, Sprite& player);
static void completePassToReceiver(TeamGeneralInfo& team, Sprite& player);
static void updateHumanPassAssistTarget(TeamGeneralInfo& team, const Sprite& player);
static JumpHeaderMovementResult updateJumpHeaderMovement(Sprite& player);
static JumpHeaderBoundaryResult checkJumpHeaderBoundary(const Sprite& player);
static void tryCompletingJumpHeader(TeamGeneralInfo& team, Sprite& player);
static bool updateStaticHeader(TeamGeneralInfo& team, Sprite& player);
static void completeSuccessfulTackle(TeamGeneralInfo& team, Sprite& player);
static void updateBookedPlayerState(Sprite& player);
static void updateCelebratingPlayerState(Sprite& player);
static void updateDownPlayerState(Sprite& player);
static void updateGoalkeeperClaimedState(Sprite& goalkeeper);
static bool updateInjuredPlayerState(Sprite& player);
static bool shouldGoalkeeperDive(const Sprite& goalkeeper, const Sprite& ball, const TeamGeneralInfo& team);
static void goalkeeperJumping(TeamGeneralInfo& team, Sprite& goalkeeper, Direction controlledDirection,
    Direction diveDirection, GoalkeeperJumpSpeedMode speedMode, uint16_t ballHeight);
static void goalkeeperCaughtTheBall(TeamGeneralInfo& team, Sprite& goalkeeper,
    int ballNextGroundX, int ballNextGroundY);
static void updateGoalkeeperCatchingBall(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball);
static bool tryFinishingGoalkeeperDive(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball);
static int16_t getDivingGoalkeeperSpeedReduction(const TeamGeneralInfo& team,
    const Sprite& goalkeeper, const Sprite& ball);
static bool isBallWithinDivingGoalkeeperWidth(const Sprite& goalkeeper, const Sprite& ball);
static bool tryDivingGoalkeeperBallContact(TeamGeneralInfo& team, const Sprite& goalkeeper, Sprite& ball);
static bool tryDivingGoalkeeperCatch(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball);
static DivingGoalkeeperContactResult chooseDivingGoalkeeperContactResult(
    int catchChance, int secondaryChance, int16_t savedCommentTimer);
static void applyPrimaryGoalkeeperDeflection(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball);
static void applySecondaryGoalkeeperDeflection(TeamGeneralInfo& team, Sprite& goalkeeper, Sprite& ball);
static void finishDivingGoalkeeperDeflection(TeamGeneralInfo& team, const Sprite& goalkeeper, Sprite& ball);
static bool isRestartTurnDirectionAllowed(uint16_t turnFlags, Direction direction);
static bool prepareControlledPlayerForBreak(TeamGeneralInfo& team, Sprite& player);
static void handleControlledPlayerBreakInput(const TeamGeneralInfo& team);
static void tryControlledPlayerStaticHeader(TeamGeneralInfo& team, Sprite& player);
static uint16_t getFramesNeededToCoverDistance(FixedPoint movementDelta, int distance);
static void updateGoalkeeperBallProjections(const Sprite& goalkeeper, const Sprite& ball, const TeamGeneralInfo& team);
static void calculateBallNextGroundXYPositions(const Sprite& ball);
static void playerTacklingTestFoul(TeamGeneralInfo& tacklingTeam, Sprite& tacklingPlayer);
static void resolveFoulAsPenaltyFreeKickOrOrdinaryFoul(const TeamGeneralInfo& foulingTeam, const Sprite& fouledPlayer);
static bool tryBookingThePlayer(TeamGeneralInfo& team, Sprite& player);
static bool trySendingOffThePlayer(TeamGeneralInfo& team, Sprite& player);
static void playerBeginTackling(TeamGeneralInfo& team, Sprite& player, Direction direction);
static void playerTackled(TeamGeneralInfo& team, Sprite& player);
static void playerAttemptingJumpHeader(Sprite& player, Direction direction);
static void setThrowInPlayerCoordinates(Sprite& player);
static void setPlayerWithNoBallDestination(const TeamGeneralInfo& team, Sprite& player, int ballX, int ballY);
static void attemptStaticHeader(Sprite& player);
static void setStaticHeaderDirection(const TeamGeneralInfo& team, Sprite& player);

// Update the selected team. SWOS alternates teams between frames, so the
// preparation, player pass, and post-pass bookkeeping must remain together.
void updatePlayers(TeamGeneralInfo *team)
{
    assert(team);
    preparePlayersUpdate(*team);
    updateAllPlayers(*team);
    finishUpdatingAllPlayers();
}

Sprite *getLastPlayerPlayed()
{
    return m_lastPlayerPlayed;
}

void resetLastPlayerPlayed()
{
    m_lastPlayerPlayed = nullptr;
}

Sprite *getLastPlayerBeforeGoalkeeper()
{
    return m_lastPlayerBeforeGoalkeeper;
}

void setLastPlayerBeforeGoalkeeper(Sprite *player)
{
    m_lastPlayerBeforeGoalkeeper = player;
}

void resetLastPlayerBeforeGoalkeeper()
{
    m_lastPlayerBeforeGoalkeeper = nullptr;
}

// Private implementation fragments remain in this translation unit because the
// converted player state machine intentionally shares file-local state.
#include "playerUpdateCore.cpp"
#include "updateAllPlayers.cpp"
#include "playerStates.cpp"
#include "ballProjections.cpp"
#include "foulsAndCards.cpp"
#include "playerActions.cpp"
