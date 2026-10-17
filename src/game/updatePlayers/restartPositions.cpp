#include "restartPositions.h"

static constexpr RestartPositions kBottomInitialPositions = {{{250, 1}, {100, 155}, {200, 133}, {300, 133}, {400, 155}, {60, 320},
    {190, 244}, {310, 244}, {355, 320}, {220, 320}, {260, 316}}};

static constexpr RestartPositions kBottomGoalOutLeftPositions = {{{290, 10}, {100, 166}, {200, 133}, {300, 133}, {400, 166},
    {100, 355}, {200, 277}, {300, 277}, {400, 355}, {200, 444}, {300, 444}}};

static constexpr RestartPositions kBottomGoalOutRightPositions = {{{210, 10}, {100, 166}, {200, 133}, {300, 133}, {400, 166},
    {100, 355}, {200, 277}, {300, 277}, {400, 355}, {200, 444}, {300, 444}}};

static constexpr RestartPositions kBottomKeeperHoldsBallPositions = {{{1000, 0}, {100, 166}, {200, 133}, {300, 133}, {400, 166},
    {100, 355}, {200, 277}, {300, 277}, {400, 355}, {200, 444}, {300, 444}}};

static constexpr RestartPositions kBottomCornerLeftPositions = {{{250, 5}, {100, 600}, {200, 277}, {300, 277}, {490, 600},
    {150, 500}, {250, 600}, {350, 500}, {450, 555}, {210, 611}, {290, 611}}};

static constexpr RestartPositions kBottomCornerRightPositions = {{{250, 5}, {10, 600}, {200, 277}, {300, 277}, {400, 277},
    {50, 555}, {150, 500}, {250, 600}, {350, 500}, {210, 611}, {290, 611}}};

static constexpr RestartPositions kBottomFreeKickOuterLeftPositions = {{{250, 5}, {22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {160, 522}, {300, 500}, {-1030, -25}, {1000, -15}, {240, 611}, {280, 605}}};

static constexpr RestartPositions kBottomFreeKickMiddleLeftPositions = {{{250, 5}, {22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {160, 522}, {250, 411}, {-1030, -25}, {1000, -15}, {240, 611}, {280, 611}}};

static constexpr RestartPositions kBottomFreeKickInnerLeftPositions = {{{250, 5}, {22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {160, 522}, {250, 411}, {-1030, -25}, {1000, -15}, {240, 611}, {280, 611}}};

static constexpr RestartPositions kBottomFreeKickCenterPositions = {{{250, 5}, {22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {-1090, 20}, {-1035, -35}, {1020, -60}, {1120, 30}, {-1020, -10}, {1040, -15}}};

static constexpr RestartPositions kBottomFreeKickInnerRightPositions = {{{250, 5}, {22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {-1000, -15}, {1030, -25}, {250, 411}, {340, 522}, {220, 611}, {260, 611}}};

static constexpr RestartPositions kBottomFreeKickMiddleRightPositions = {{{250, 5}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {22222, 22222}, {-1000, -15}, {1030, -25}, {250, 411}, {340, 522}, {220, 611}, {260, 611}}};

static constexpr RestartPositions kBottomFreeKickOuterRightPositions = {{{250, 5}, {22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {-1000, -15}, {1030, -25}, {200, 500}, {340, 522}, {220, 605}, {260, 611}}};

static constexpr RestartPositions kBottomPenaltyPositions = {{{250, 5}, {100, 277}, {200, 277}, {300, 277}, {400, 277},
    {100, 500}, {200, 500}, {300, 500}, {400, 500}, {220, 555}, {250, 522}}};

static constexpr RestartPositions kBottomThrowInForwardRightPositions = {{{22222, 22222}, {22222, 22222}, {22222, 22222},
    {-1075, 0}, {-1025, 0}, {22222, 22222}, {22222, 22222}, {-1075, 50}, {-1050, 75}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kBottomThrowInCenterRightPositions = {{{22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {22222, 22222}, {22222, 22222}, {-1100, 25}, {-1075, -20}, {-1025, -25}, {22222, 22222}, {-1075, 75}}};

static constexpr RestartPositions kBottomThrowInBackRightPositions = {{{22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {22222, 22222}, {22222, 22222}, {-1075, -50}, {-1075, 0}, {-1025, 0}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kBottomThrowInForwardLeftPositions = {{{22222, 22222}, {1025, 0}, {1075, 0}, {22222, 22222},
    {22222, 22222}, {1075, 50}, {1050, 75}, {22222, 22222}, {22222, 22222}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kBottomThrowInCenterLeftPositions = {{{22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {22222, 22222}, {1025, -25}, {1075, -20}, {1100, 25}, {22222, 22222}, {22222, 22222}, {1075, 75}}};

static constexpr RestartPositions kBottomThrowInBackLeftPositions = {{{22222, 22222}, {22222, 22222}, {22222, 22222},
    {22222, 22222}, {22222, 22222}, {1025, 0}, {1075, 0}, {1075, -50}, {22222, 22222}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kBottomStartingGamePositions = {{{380, 255}, {365, 261}, {350, 266}, {335, 272}, {320, 277},
    {305, 283}, {290, 288}, {275, 294}, {260, 300}, {245, 305}, {230, 311}}};

static RestartPositions kBottomLeavingPitchPositions = {{{550, 277}, {550, 277}, {550, 277}, {550, 277}, {550, 277}, {550, 277},
    {550, 277}, {550, 277}, {550, 277}, {550, 277}, {550, 277}}};

static constexpr RestartPositions kBottomExtraTimeStartingPositions = {{{250, 1}, {100, 155}, {200, 133}, {300, 133}, {400, 155},
    {60, 320}, {190, 244}, {310, 244}, {355, 320}, {220, 320}, {260, 316}}};

static constexpr RestartPositions kBottomPenaltyShootoutPositions = {{{200, 522}, {260, 344}, {280, 333}, {300, 333}, {320, 344},
    {260, 366}, {280, 355}, {300, 355}, {320, 366}, {280, 377}, {300, 377}}};

static constexpr RestartPositions kTopInitialPositions = {{{250, 11}, {120, 133}, {210, 133}, {290, 133}, {380, 133}, {90, 244},
    {220, 200}, {280, 200}, {410, 244}, {200, 277}, {300, 277}}};

static constexpr RestartPositions kTopGoalOutLeftPositions = {{{250, 11}, {100, 144}, {200, 144}, {300, 144}, {400, 144},
    {100, 244}, {200, 277}, {300, 277}, {400, 244}, {200, 444}, {300, 444}}};

static constexpr RestartPositions kTopGoalOutRightPositions = {{{250, 11}, {100, 144}, {200, 144}, {300, 144}, {400, 144},
    {100, 244}, {200, 277}, {300, 277}, {400, 244}, {200, 444}, {300, 444}}};

static constexpr RestartPositions kTopKeeperHoldsBallPositions = {{{250, 11}, {100, 144}, {200, 144}, {300, 144}, {400, 144},
    {100, 244}, {200, 277}, {300, 277}, {400, 244}, {200, 444}, {300, 444}}};

static constexpr RestartPositions kTopCornerLeftPositions = {
    {{250, 2}, {175, 38}, {225, 22}, {275, 22}, {325, 38}, {150, 111}, {200, 55}, {300, 83}, {400, 166}, {200, 300}, {300, 300}}};

static constexpr RestartPositions kTopCornerRightPositions = {
    {{250, 2}, {175, 38}, {225, 22}, {275, 22}, {325, 38}, {100, 166}, {200, 83}, {300, 55}, {350, 111}, {200, 300}, {300, 300}}};

static constexpr RestartPositions kTopFreeKickOuterLeftPositions = {{{250, 2}, {1043, -37}, {1048, -29}, {280, 27}, {210, 22},
    {190, 22}, {250, 27}, {340, 50}, {340, 88}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kTopFreeKickMiddleLeftPositions = {{{250, 2}, {1030, -41}, {1037, -36}, {1043, -31},
    {1048, -26}, {200, 22}, {250, 33}, {280, 38}, {310, 11}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kTopFreeKickInnerLeftPositions = {{{250, 2}, {1014, -53}, {1023, -50}, {1031, -47}, {1038, -44},
    {200, 22}, {250, 33}, {280, 38}, {310, 11}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kTopFreeKickCenterPositions = {{{250, 2}, {-1015, -54}, {-1005, -55}, {1005, -55}, {1015, -54},
    {-1080, 20}, {-1050, -55}, {1060, -45}, {1070, 40}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kTopFreeKickInnerRightPositions = {{{250, 2}, {-1038, -44}, {-1031, -47}, {-1023, -50},
    {-1014, -53}, {190, 11}, {220, 38}, {250, 33}, {300, 22}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kTopFreeKickMiddleRightPositions = {{{250, 2}, {-1048, -26}, {-1043, -31}, {-1037, -36},
    {-1030, -41}, {190, 11}, {220, 38}, {250, 33}, {300, 22}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kTopFreeKickOuterRightPositions = {{{250, 2}, {290, 22}, {220, 27}, {-1048, -29}, {-1043, -37},
    {160, 88}, {160, 50}, {250, 27}, {310, 22}, {22222, 22222}, {22222, 22222}}};

static constexpr RestartPositions kTopPenaltyPositions = {{{250, 2}, {175, 88}, {225, 122}, {275, 122}, {325, 88}, {70, 155},
    {200, 166}, {300, 166}, {430, 155}, {150, 300}, {300, 300}}};

static constexpr RestartPositions kTopStartingGamePositions = {{{120, 255}, {135, 261}, {150, 266}, {165, 272}, {180, 277},
    {195, 283}, {210, 288}, {225, 294}, {240, 300}, {255, 305}, {270, 311}}};

static RestartPositions kTopLeavingPitchPositions = {{{-50, 377}, {-50, 377}, {-50, 377}, {-50, 377}, {-50, 377}, {-50, 377},
    {-50, 377}, {-50, 377}, {-50, 377}, {-50, 377}, {-50, 377}}};

static constexpr RestartPositions kTopExtraTimeStartingPositions = {{{250, 11}, {120, 133}, {210, 133}, {290, 133}, {380, 133},
    {90, 244}, {220, 200}, {280, 200}, {410, 244}, {200, 277}, {300, 277}}};

static constexpr RestartPositions kTopPenaltyShootoutPositions = {{{250, 2}, {260, 277}, {280, 266}, {300, 266}, {320, 277},
    {260, 300}, {280, 288}, {300, 288}, {320, 300}, {280, 311}, {300, 311}}};

// Both lookup tables are indexed directly by the numeric GameState value. Each
// entry selects the 11-player layout for that state; nullptr means that side
// has no special layout and should use normal tactical positioning instead.
static constexpr std::array<const RestartPositions *, 32> kBottomRestartPositions = {{
    &kBottomInitialPositions,
    &kBottomGoalOutLeftPositions,
    &kBottomGoalOutRightPositions,
    &kBottomKeeperHoldsBallPositions,
    &kBottomCornerLeftPositions,
    &kBottomCornerRightPositions,
    &kBottomFreeKickOuterLeftPositions,
    &kBottomFreeKickMiddleLeftPositions,
    &kBottomFreeKickInnerLeftPositions,
    &kBottomFreeKickCenterPositions,
    &kBottomFreeKickInnerRightPositions,
    &kBottomFreeKickMiddleRightPositions,
    &kBottomFreeKickOuterRightPositions,
    nullptr,
    &kBottomPenaltyPositions,
    &kBottomThrowInForwardRightPositions,
    &kBottomThrowInCenterRightPositions,
    &kBottomThrowInBackRightPositions,
    &kBottomThrowInForwardLeftPositions,
    &kBottomThrowInCenterLeftPositions,
    &kBottomThrowInBackLeftPositions,
    &kBottomStartingGamePositions,
    &kBottomStartingGamePositions,
    &kBottomLeavingPitchPositions,
    &kBottomLeavingPitchPositions,
    &kBottomLeavingPitchPositions,
    &kBottomLeavingPitchPositions,
    &kBottomExtraTimeStartingPositions,
    &kBottomExtraTimeStartingPositions,
    nullptr,
    nullptr,
    &kBottomPenaltyShootoutPositions,
}};

static constexpr std::array<const RestartPositions *, 32> kTopRestartPositions = {{
    &kTopInitialPositions,
    &kTopGoalOutLeftPositions,
    &kTopGoalOutRightPositions,
    &kTopKeeperHoldsBallPositions,
    &kTopCornerLeftPositions,
    &kTopCornerRightPositions,
    &kTopFreeKickOuterLeftPositions,
    &kTopFreeKickMiddleLeftPositions,
    &kTopFreeKickInnerLeftPositions,
    &kTopFreeKickCenterPositions,
    &kTopFreeKickInnerRightPositions,
    &kTopFreeKickMiddleRightPositions,
    &kTopFreeKickOuterRightPositions,
    nullptr,
    &kTopPenaltyPositions,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &kTopStartingGamePositions,
    &kTopStartingGamePositions,
    &kTopLeavingPitchPositions,
    &kTopLeavingPitchPositions,
    &kTopLeavingPitchPositions,
    &kTopLeavingPitchPositions,
    &kTopExtraTimeStartingPositions,
    &kTopExtraTimeStartingPositions,
    nullptr,
    nullptr,
    &kTopPenaltyShootoutPositions,
}};

const RestartPositions *getRestartPositions(bool useBottomPositions, GameState gameState)
{
    auto& positionsByGameState = useBottomPositions ? kBottomRestartPositions : kTopRestartPositions;
    auto stateIndex = static_cast<size_t>(gameState);
    assert(stateIndex < positionsByGameState.size());
    return positionsByGameState[stateIndex];
}

// Shift the halftime/full-time exit targets beyond the visible touchlines.
// This is applied once when the runtime restart tables are initialized.
void patchPlayersLeavingPitchDestinations()
{
    constexpr int kPlayersOffscreenOffsetX = 39;

    for (auto& position : kBottomLeavingPitchPositions)
        position.x += kPlayersOffscreenOffsetX;
    for (auto& position : kTopLeavingPitchPositions)
        position.x -= kPlayersOffscreenOffsetX;
}
