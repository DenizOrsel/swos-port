# Feature Specification & Agentic Blueprint: SWOS Pre-Match Sponsor Challenge System (Native Source Integration)

## 1. System Overview & Vision

Built directly into the **SWOS source code**, the **Pre-Match Sponsor Challenge System** integrates natively into Career Mode's pre-match flow, live match statistics tracking, post-match financial report, and save-file serialization.

Before each competitive match, the engine dispatches live squad and fixture context to the shared native **LLM Client** (`llm_client.h`) to generate **3 tailored sponsor challenges** (with instant offline template fallback). The manager selects **1 objective** using SWOS's native menu interface. Because we have direct access to the SWOS match engine, match events (goals, scorer player IDs, half-time scores, possession ticks, and shots on target) are recorded natively in memory during gameplay—allowing instant, deterministic verification at full-time and immediate credit to `club->finances.cash` on the Post-Match Financial Report screen.

## 2. Native Architecture & Match Lifecycle Integration

1. **Pre-Match Generation (`career_prepare_next_match`):**
   * Engine gathers `club_name`, `opponent_name`, `division_tier`, `active_starting_xi` (exact player IDs, names, positions, and seasonal goals), and `club->finances.cash`.
   * Calls `challenge_generate_async()` via the shared `llm_client` (with offline template fallback).
2. **Pre-Match Selection Menu (`MENU_SPONSOR_CHALLENGES`):**
   * Rendered natively right before kick-off (or immediately after the Pre-Match Press Conference).
   * Player chooses `[1]`, `[2]`, `[3]`, or `[0] DECLINE`.
   * Selection is stored directly inside `club->challenge_state.active_challenge` and persisted in the native career save file.
3. **In-Match Telemetry Tracking (`match_engine` hooks):**
   * Track exact match metrics inside a native `MatchTelemetry` struct during match execution:
     * `club_goals`, `opponent_goals`
     * `club_ht_goals`, `opponent_ht_goals` (captured in the native half-time transition hook)
     * `goal_scorers[32]` (array of `player_id`s recorded inside the native `match_on_goal_scored()` function)
     * `club_possession_pct` (computed from native ball-control frame counters)
     * `club_shots_on_target` (incremented inside the native shot-on-goal/save/goal handler)
4. **Full-Time Verification & Instant Cash Payout (`career_on_match_finished`):**
   * Runs `challenge_evaluate(&club->challenge_state.active_challenge, &match_telemetry)`.
   * If `completed == true`: adds `reward_amount` directly to `club->finances.cash`, increments seasonal challenge stats, and passes the result to the native **Post-Match Financial Report** screen.

## 3. Native Data Structures & Deterministic Rule Enum

### 3.1 Core Structs (`sponsor_challenge.h`)

```c
typedef enum {
    CHALLENGE_NONE = 0,
    CHALLENGE_CLEAN_SHEET_WIN = 1,     // Win and concede <= max_conceded
    CHALLENGE_PLAYER_GOALS = 2,        // Specific player_id scores >= min_goals
    CHALLENGE_FIRST_HALF_GOALS = 3,    // Club scores >= min_goals in 1st half
    CHALLENGE_POSSESSION_WIN = 4,      // Win and possession >= min_possession
    CHALLENGE_SHOTS_ON_TARGET = 5,     // Club records >= min_shots on target
    CHALLENGE_WIN_BY_MARGIN = 6        // Club wins by >= min_margin goals
} ChallengeType;

typedef struct {
    bool is_active;
    ChallengeType type;
    char title[28];
    char description[64];
    int32_t reward_amount;

    // Deterministic verification parameters
    int32_t target_player_id;
    char target_player_name[24];
    int16_t threshold_value;           // min_goals, min_possession, min_shots, min_margin, or max_conceded
} SponsorChallenge;

typedef struct {
    SponsorChallenge offered[3];
    bool offers_ready;
    SponsorChallenge active_challenge;
    
    // Last match outcome for Post-Match Report rendering
    bool last_match_had_challenge;
    bool last_match_completed;
    char last_match_title[28];
    int32_t last_match_payout;

    // Career statistics
    uint16_t challenges_completed;
    uint16_t challenges_failed;
    int64_t total_bonus_earned;
} CareerChallengeState;

typedef struct {
    uint8_t club_goals;
    uint8_t opponent_goals;
    uint8_t club_ht_goals;
    uint8_t opponent_ht_goals;
    uint8_t club_possession_pct;
    uint8_t club_shots_on_target;
    uint8_t scorer_count;
    int32_t scorer_player_ids[32];
} MatchTelemetry;
```

### 3.2 Deterministic Verification Matrix

| `ChallengeType` | `threshold_value` / Extra Fields | Native C/C++ Evaluation Expression |
| :--- | :--- | :--- |
| `CHALLENGE_CLEAN_SHEET_WIN` | `max_conceded` (`0`) | `(t->club_goals > t->opponent_goals) && (t->opponent_goals <= c->threshold_value)` |
| `CHALLENGE_PLAYER_GOALS` | `target_player_id`, `min_goals` | `count_player_goals(t, c->target_player_id) >= c->threshold_value` |
| `CHALLENGE_FIRST_HALF_GOALS` | `min_goals` (`1`–`3`) | `t->club_ht_goals >= c->threshold_value` |
| `CHALLENGE_POSSESSION_WIN` | `min_possession` (`55`–`65`) | `(t->club_goals > t->opponent_goals) && (t->club_possession_pct >= c->threshold_value)` |
| `CHALLENGE_SHOTS_ON_TARGET` | `min_shots` (`6`–`12`) | `t->club_shots_on_target >= c->threshold_value` |
| `CHALLENGE_WIN_BY_MARGIN` | `min_margin` (`2`–`3`) | `((int)t->club_goals - (int)t->opponent_goals) >= c->threshold_value` |

## 4. LLM JSON Schema & Offline Fallback

### 4.1 Expected LLM Response Schema

```json
{
  "challenges": [
    {
      "title": "SHUTOUT VICTORY",
      "description": "Win the match without conceding a single goal.",
      "type": "CHALLENGE_CLEAN_SHEET_WIN",
      "target_player_id": -1,
      "target_player_name": "",
      "threshold_value": 0,
      "reward_amount": 15000
    },
    {
      "title": "STAR STRIKER",
      "description": "J. Litmanen must score at least 2 goals.",
      "type": "CHALLENGE_PLAYER_GOALS",
      "target_player_id": 1042,
      "target_player_name": "J. Litmanen",
      "threshold_value": 2,
      "reward_amount": 25000
    },
    {
      "title": "DOMINANT POSSESSION",
      "description": "Win the match with 60% or higher possession.",
      "type": "CHALLENGE_POSSESSION_WIN",
      "target_player_id": -1,
      "target_player_name": "",
      "threshold_value": 60,
      "reward_amount": 35000
    }
  ]
}
```

## 5. Native UI Mockups (SWOS Menu Renderer)

### 5.1 Pre-Match Challenge Selection Menu (`MENU_SPONSOR_CHALLENGES`)

```text
+-------------------------------------------------------------------+
|  SPONSOR MATCH CHALLENGES                  BANK: £1,450,000       |
+-------------------------------------------------------------------+
|  SELECT ONE OBJECTIVE FOR TODAY'S MATCH AGAINST ARSENAL:          |
|                                                                   |
|  [1] SHUTOUT VICTORY                                              |
|      Win the match without conceding a single goal.   [+£15,000]  |
|                                                                   |
|  [2] STAR STRIKER                                                 |
|      J. Litmanen must score at least 2 goals.         [+£25,000]  |
|                                                                   |
|  [3] DOMINANT POSSESSION                                          |
|      Win the match with 60% or higher possession.     [+£35,000]  |
|                                                                   |
|  [0] DECLINE ALL CHALLENGES                                       |
+-------------------------------------------------------------------+
```

### 5.2 Post-Match Financial Report Integration

```text
+-------------------------------------------------------------------+
|                     POST-MATCH FINANCIAL REPORT                   |
+-------------------------------------------------------------------+
|  NET MATCHDAY STADIUM PROFIT .....................   +£749,650    |
|  SPONSOR CHALLENGE: STAR STRIKER (COMPLETED!) ....    +£25,000    |
+-------------------------------------------------------------------+
|  UPDATED BANK BALANCE ............................  £2,224,650    |
+-------------------------------------------------------------------+
```

## 6. Master Prompt for AI Coding Agent

```text
### ROLE & TASK
You are an expert game engine developer working directly in our *Sensible World of Soccer* (SWOS) source codebase. Implement the **Native Pre-Match Sponsor Challenge System** directly into Career Mode and the match engine.

### REQUIREMENTS
1. **Native Data Structures & Save Serialization (`sponsor_challenge.h` / `sponsor_challenge.c`):**
   - Implement `ChallengeType`, `SponsorChallenge`, `CareerChallengeState`, and `MatchTelemetry`.
   - Embed `CareerChallengeState` inside the native Career Mode club struct and serialize it inside the native career save/load routines with safe version handling for legacy saves.
2. **Challenge Generator (LLM + Offline Fallback):**
   - Hook into pre-match setup to extract `club_name`, `opponent_name`, `division_tier`, and the active starting XI (`player_id`, `name`, `position`, `goals_scored`).
   - Use our shared `llm_client` to asynchronously request 3 distinct challenges mapped strictly to the `ChallengeType` enum, scaling `reward_amount` by `division_tier` (e.g., £5k–£15k lower leagues, £15k–£50k top flight).
   - Provide a deterministic offline fallback generator that builds 3 valid challenges (including picking the top striker from the starting XI for `CHALLENGE_PLAYER_GOALS`) whenever the LLM endpoint is offline.
3. **Native Pre-Match Selection UI (`MENU_SPONSOR_CHALLENGES`):**
   - Render a native SWOS menu screen presenting the 3 challenges, their descriptions, and cash rewards, plus `[0] DECLINE`.
   - Store the selected challenge in `club->challenge_state.active_challenge`.
4. **In-Match Telemetry & Full-Time Payout:**
   - Wire `MatchTelemetry` directly into the native match engine:
     - Capture `club_ht_goals` at the half-time whistle.
     - Append `scorer_player_id` inside the native goal-scored handler.
     - Record `club_shots_on_target` and `club_possession_pct` from native match stats.
   - At full-time (`career_on_match_finished`), run `challenge_evaluate(&active_challenge, &telemetry)`. If true, immediately credit `reward_amount` to `club->finances.cash`, record `last_match_completed = true`, and clear `active_challenge`.
5. **Post-Match Financial Report Display:**
   - Render the `SPONSOR CHALLENGE` result row (`COMPLETED! +£XX,XXX` or `FAILED £0`) and the updated bank balance on the native Post-Match Financial Report screen.
```