# Feature Specification & Agentic Blueprint: SWOS Global Achievements System (Native Source Integration)

## 1. System Overview & Vision

Built directly into the **SWOS source code**, the **Global Achievements System** tracks player milestones across *all* game modes—Friendly matches, DIY/Preset Competitions, International Tournaments, and Career Mode.

Unlike Career-specific save data, achievements are persisted in a **Global Profile File** (`achievements.dat` / `swos_profile.sav`), ensuring that progress earned in quick matches, cup runs, or across multiple careers accumulates toward a unified **Manager Gamerscore** (`0 – 2,000+ PTS`). Unlocking an achievement triggers a non-intrusive retro 16-bit toast banner at the top/bottom of the screen with classic SWOS crowd/whistle audio cues, and players can browse all locked, in-progress, and unlocked trophies from the Main Menu.

---

## 2. Complete Master Achievement Catalog

Achievements are organized into **6 Categories** and **5 Point Tiers**:
* **10 PTS (Bronze):** First steps, basic mechanics, entry-level milestones.
* **20 PTS (Silver):** Skilled execution, tactical discipline, solid progression.
* **30 PTS (Gold):** Tough match feats, cup upsets, notable career milestones.
* **50 PTS (Platinum):** Major silverware, rare match events, season dominance.
* **100 PTS (Legend):** Ultimate mastery, treble-winning glory, legendary feats.

---

### Category A: First Steps & Match Basics (10–20 PTS)

| ID | Enum Identifier | Title | Description | Points |
| :--- | :--- | :--- | :--- | :---: |
| `01` | `ACH_FIRST_KICKOFF` | **Jumpers for Goalposts** | Complete your very first match in any game mode. | **10** |
| `02` | `ACH_FRIENDLY_WIN` | **Warm-Up Act** | Win a Friendly match. | **10** |
| `03` | `ACH_FIRST_GOAL` | **Off the Mark** | Score your first goal in any match. | **10** |
| `04` | `ACH_CLEAN_SHEET` | **Shut the Door** | Win a match without conceding a single goal. | **10** |
| `05` | `ACH_PENALTY_SHOOTOUT_WIN` | **Nerves of Steel** | Win a match via a Penalty Shootout. | **20** |
| `06` | `ACH_EXTRA_TIME_WINNER` | **After Hours** | Score a winning goal in Extra Time. | **20** |
| `07` | `ACH_VETERAN_50_MATCHES` | **Seasoned Gaffer** | Play 50 matches across any game mode. | **20** |
| `08` | `ACH_CENTURION_100_GOALS` | **Century Club** | Score 100 cumulative goals across all matches. | **20** |

---

### Category B: Pitch Craft & Goal Types (10–50 PTS)

| ID | Enum Identifier | Title | Description | Points |
| :--- | :--- | :--- | :--- | :---: |
| `09` | `ACH_PENALTY_GOAL` | **Spot On** | Score from a penalty kick during regular or extra time. | **10** |
| `10` | `ACH_HEADER_GOAL` | **Bullet Header** | Score a goal with a header from a cross or corner. | **20** |
| `11` | `ACH_OUTSIDE_BOX_GOAL` | **Screamer!** | Score a goal from outside the 18-yard penalty box. | **20** |
| `12` | `ACH_FREE_KICK_GOAL` | **Dead-Ball Specialist** | Score directly from a free kick. | **30** |
| `13` | `ACH_VOLLEY_OR_OVERHEAD` | **Worldie** | Score with a volley or bicycle/overhead kick. | **30** |
| `14` | `ACH_CORNER_DIRECT_OR_QUICK` | **Training Ground Routine** | Score within 3 seconds of taking a corner kick. | **20** |
| `15` | `ACH_HAT_TRICK` | **Match Ball** | Score a hat-trick (3+ goals) with a single player in one match. | **30** |
| `16` | `ACH_DOUBLE_HAT_TRICK` | **Unstoppable Force** | Score 6 goals with a single player in a single match. | **50** |
| `17` | `ACH_GOALKEEPER_GOAL` | **Up for the Corner** | Score a goal with your goalkeeper in open play or a set piece. | **50** |
| `18` | `ACH_EARLY_BLITZ` | **Straight Out the Tunnel** | Score a goal within the first 2 in-game minutes. | **20** |
| `19` | `ACH_FERGIE_TIME` | **90th-Minute Drama** | Score a winning goal in the 89th minute or later. | **30** |
| `20` | `ACH_NO_CARDS_WIN` | **Fair Play Award** | Win a competitive match with 0 yellow and 0 red cards. | **10** |
| `21` | `ACH_DOWN_TO_TEN_WIN` | **Against All Odds** | Win a match after receiving a red card before the 60th minute. | **30** |

---

### Category C: Comebacks & Dominance (20–50 PTS)

| ID | Enum Identifier | Title | Description | Points |
| :--- | :--- | :--- | :--- | :---: |
| `22` | `ACH_HALFTIME_COMEBACK` | **Hairdryer Treatment** | Win a match after trailing at half-time. | **20** |
| `23` | `ACH_TWO_GOAL_COMEBACK` | **Great Escape** | Win a match after trailing by 2 or more goals. | **30** |
| `24` | `ACH_THREE_GOAL_COMEBACK` | **Miracle of Istanbul** | Win a match after trailing by 3 or more goals. | **50** |
| `25` | `ACH_FIVE_GOAL_ROUT` | **Five-Star Performance** | Win a competitive match by a margin of 5 or more goals. | **20** |
| `26` | `ACH_DOUBLE_DIGITS` | **Total Football** | Score 10 or more goals in a single competitive match. | **50** |
| `27` | `ACH_POSSESSION_MASTER` | **Tiki-Taka** | Win a match while holding 65% or more ball possession. | **20** |
| `28` | `ACH_CLINICAL_FINISHING` | **One Shot, One Kill** | Win a match while scoring on 75%+ of your shots on target (min. 3 goals). | **30** |

---

### Category D: Cup Runs & Giant Killing (20–50 PTS)

| ID | Enum Identifier | Title | Description | Points |
| :--- | :--- | :--- | :--- | :---: |
| `29` | `ACH_GIANT_KILLER_1_TIER` | **Cup Upset** | Eliminate a higher-division club in a domestic cup match. | **20** |
| `30` | `ACH_GIANT_KILLER_2_TIERS` | **David vs. Goliath** | Eliminate a club at least 2 divisions higher in a cup match. | **30** |
| `31` | `ACH_GIANT_KILLER_3_TIERS` | **Non-League Legend** | As a 4th-tier (or lowest-tier) club, defeat a Top-Flight club in a cup tie. | **50** |
| `32` | `ACH_DOMESTIC_CUP_WIN` | **Wembley Glory** | Win your nation's primary Domestic Cup competition. | **50** |
| `33` | `ACH_EUROPEAN_CUP_WIN` | **Kings of Europe** | Win the European Champions Cup / continental top-tier club trophy. | **100** |
| `34` | `ACH_WORLD_CUP_WIN` | **World Champions** | Win the World Cup managing a national team. | **100** |

---

### Category E: Career Mode Progression & Glory (10–100 PTS)

| ID | Enum Identifier | Title | Description | Points |
| :--- | :--- | :--- | :--- | :---: |
| `35` | `ACH_CAREER_FIRST_WIN` | **Three Points on the Board** | Win your first league match in Career Mode. | **10** |
| `36` | `ACH_PROMOTION` | **Going Up!** | Achieve promotion to a higher division in Career Mode. | **30** |
| `37` | `ACH_BACK_TO_BACK_PROMOTION` | **Unstoppable Rise** | Achieve consecutive promotions in back-to-back Career Mode seasons. | **50** |
| `38` | `ACH_LEAGUE_CHAMPION` | **Champions!** | Win the top-flight league title in Career Mode. | **50** |
| `39` | `ACH_UNBEATEN_SEASON` | **The Invincibles** | Complete an entire Career Mode league season with 0 losses. | **100** |
| `40` | `ACH_CONTINENTAL_TREBLE` | **The Treble** | Win the Top-Flight League, Domestic Cup, and Champions Cup in one season. | **100** |
| `41` | `ACH_INTERNATIONAL_CALLUP` | **National Duty** | Receive and accept an offer to manage a National Team in Career Mode. | **50** |
| `42` | `ACH_CAREER_TEN_SEASONS` | **Dynasty Builder** | Complete 10 full seasons in a single Career Mode save. | **50** |

---

### Category F: Transfers, Stadium, Press & Challenges (10–50 PTS)

*(Integrates directly with native Transfers and our Stadium, Press Charisma, and Sponsor Challenge subsystems)*

| ID | Enum Identifier | Title | Description | Points |
| :--- | :--- | :--- | :--- | :---: |
| `43` | `ACH_FIRST_SIGNING` | **Welcome to the Club** | Sign a player from the transfer market in Career Mode. | **10** |
| `44` | `ACH_MILLION_POUND_SIGNING` | **Galáctico** | Sign a single player for £1,000,000 or more. | **20** |
| `45` | `ACH_PROFITABLE_FLIP` | **Wheeler Dealer** | Sell a player for at least £1,000,000 more than you bought him for. | **30** |
| `46` | `ACH_STADIUM_FIRST_UPGRADE` | **Breaking Ground** | Complete your first Stadium or Facility upgrade. | **10** |
| `47` | `ACH_FORTRESS_85K` | **Theatre of Dreams** | Expand your Stadium Stand Capacity to Level 10 (85,000 seats) and sell out a match. | **50** |
| `48` | `ACH_MAX_FACILITIES` | **Five-Star Complex** | Upgrade Pitch, Training, Medical, and Commercial facilities all to Level 5. | **50** |
| `49` | `ACH_ICONIC_CHARISMA` | **Box Office Boss** | Reach `90+` Coach Charisma Points (`Iconic` tier) in Career Mode. | **30** |
| `50` | `ACH_SPONSOR_STREAK` | **Sponsor's Darling** | Complete 10 Pre-Match Sponsor Challenges in Career Mode. | **30** |

> **Total Catalog Summary:** `50 Achievements` | `1,650 Total Gamerscore Points`

---

## 3. Native Data Structures & Global Profile Persistence

### 3.1 Core Structs (`achievements.h`)

```c
#define ACHIEVEMENT_COUNT 50
#define ACHIEVEMENT_TOAST_QUEUE_MAX 8

typedef enum {
    ACH_CAT_BASICS = 0,
    ACH_CAT_PITCH_CRAFT = 1,
    ACH_CAT_COMEBACKS = 2,
    ACH_CAT_CUP_GLORY = 3,
    ACH_CAT_CAREER = 4,
    ACH_CAT_MANAGEMENT = 5
} AchievementCategory;

typedef struct {
    uint16_t id;
    const char* enum_name;
    const char* title;
    const char* description;
    AchievementCategory category;
    uint16_t points;            // 10, 20, 30, 50, or 100
    uint32_t target_progress;   // 1 for single-event achievements, >1 for cumulative (e.g., 50 matches, 100 goals)
    bool is_secret;
} AchievementDef;

typedef struct {
    bool unlocked;
    uint32_t current_progress;
    uint64_t unlock_timestamp;
} AchievementRecord;

typedef struct {
    uint32_t magic;             // 0x53574F53 ('SWOS')
    uint16_t version;
    uint32_t total_points;
    uint32_t unlocked_count;
    
    // Persistent lifetime counters
    uint32_t total_matches_played;
    uint32_t total_goals_scored;
    uint32_t sponsor_challenges_completed;

    AchievementRecord records[ACHIEVEMENT_COUNT];
} GlobalAchievementProfile;
```

### 3.2 Global Persistence (`achievements_io.c`)

* Stored in a dedicated global file (`swos_achievements.dat`) in the game's user/save directory so progress persists across all modes and career saves.
* Saved automatically whenever an achievement unlocks or at the end of a match, using atomic write-and-rename (`swos_achievements.tmp` → `swos_achievements.dat`) to prevent save corruption.

---

## 4. Native Match Engine & Game Loop Event Hooks

Because we are modifying the SWOS source directly, achievements evaluate via lightweight event dispatchers rather than polling:

1. **Goal Scored Hook (`match_on_goal_scored(const GoalEvent* ev)`):**
   * Inspects `ev->shot_type` (`HEADER`, `VOLLEY`, `OVERHEAD`, `FREE_KICK`, `PENALTY`, `OPEN_PLAY`), `ev->distance_from_goal_yards` (`>= 18.0f` for *Screamer!*), `ev->scorer_position == POS_GK` (*Up for the Corner*), `ev->seconds_since_corner <= 3` (*Training Ground Routine*), `ev->match_minute <= 2` (*Straight Out the Tunnel*), `ev->match_minute >= 89 && ev->is_go_ahead_goal` (*90th-Minute Drama*), and per-player match goal count (`3` for *Match Ball*, `6` for *Unstoppable Force*).
2. **Red Card Hook (`match_on_red_card(int team, int minute)`):**
   * Records `had_early_red_card = true` if `minute < 60` so full-time victory triggers *Against All Odds*.
3. **Full-Time Match Completion Hook (`game_on_match_finished(const MatchSummary* summary)`):**
   * Increments `total_matches_played` and `total_goals_scored`.
   * Checks Friendly win, Clean Sheet, Penalty Shootout win, Extra-Time winner, Half-Time comeback, 2/3-goal deficits overcome, 5+ margin, 10+ goals, 65%+ possession win, clinical shot ratio, Fair Play (`0` cards), and Cup Giant-Killing (`opponent_division_tier < player_division_tier`).
4. **Career & Management Hooks:**
   * `career_on_transfer_completed()`: Evaluates *Welcome to the Club*, *Galáctico* (`fee >= 1000000`), and *Wheeler Dealer* (`sell_fee - buy_fee >= 1000000`).
   * `career_on_season_end()`: Evaluates *Going Up!*, *Unstoppable Rise*, *Champions!*, *The Invincibles*, *The Treble*, and *Dynasty Builder*.
   * `stadium_on_upgrade_completed()` / `press_on_charisma_changed()` / `challenge_on_completed()`: Evaluates management subsystem milestones.

---

## 5. Native UI Mockups (SWOS Menu Renderer)

### 5.1 Live Retro Unlock Toast Notification

Rendered in the top overlay layer for `3.5 seconds` without pausing gameplay, accompanied by a crisp chime sound effect:

```text
+-------------------------------------------------------+
| [TROPHY] ACHIEVEMENT UNLOCKED!              [+30 PTS] |
|          DEAD-BALL SPECIALIST - Free kick goal!       |
+-------------------------------------------------------+
```

### 5.2 Main Menu Achievements Browser (`MENU_ACHIEVEMENTS`)

Accessible directly from the SWOS Main Menu (`ACHIEVEMENTS`):

```text
+-------------------------------------------------------------------+
|  MANAGER ACHIEVEMENTS             UNLOCKED: 18/50   PTS: 430/1650 |
+-------------------------------------------------------------------+
|  CATEGORY: < ALL >   [BASICS] [PITCH] [COMEBACK] [CUP] [CAREER]   |
+-------------------------------------------------------------------+
|  [OK] SCREAMER! ................................. [20 PTS]        |
|       Score a goal from outside the 18-yard penalty box.          |
|  [OK] DEAD-BALL SPECIALIST ...................... [30 PTS]        |
|       Score directly from a free kick.                            |
|  [..] CENTURY CLUB (74 / 100 GOALS) ............. [20 PTS]        |
|       Score 100 cumulative goals across all matches.              |
|  [--] MIRACLE OF ISTANBUL ....................... [50 PTS]        |
|       Win a match after trailing by 3 or more goals.              |
+-------------------------------------------------------------------+
|  [UP/DOWN] SCROLL   [LEFT/RIGHT] FILTER CATEGORY       [ESC] BACK |
+-------------------------------------------------------------------+
```

---

## 6. Agentic Development Implementation Plan & Prompts

Feed these phased prompts sequentially to your coding agent to implement the entire system cleanly from source.

### Phase 1 Prompt: Achievement Catalog, Profile Persistence & Unlock Engine

```text
### ROLE & TASK
You are an expert game engine developer working directly in our *Sensible World of Soccer* (SWOS) source codebase. Implement the core **Global Achievements System** catalog, persistence layer, and unlock/progress engine.

### REQUIREMENTS
1. **Define Master Catalog (`achievements.h` / `achievements.c`):**
   - Implement the 50-achievement catalog covering Basics, Pitch Craft, Comebacks, Cup Glory, Career Mode, and Management subsystems with exact point values (`10`, `20`, `30`, `50`, `100` PTS) and `target_progress` counts.
   - Define `GlobalAchievementProfile` storing `total_points`, `unlocked_count`, lifetime counters (`total_matches_played`, `total_goals_scored`, `sponsor_challenges_completed`), and `AchievementRecord records[50]`.
2. **Global Save Persistence (`swos_achievements.dat`):**
   - Implement `achievements_load()` at game startup and `achievements_save()` using safe atomic file writes so achievements persist globally across all game modes and career saves.
3. **Unlock & Progress API:**
   - Implement `achievements_unlock(AchievementId id)` and `achievements_add_progress(AchievementId id, uint32_t amount)`.
   - When an achievement transitions from locked to unlocked, update `total_points` and `unlocked_count`, push an entry onto a thread-safe/frame-safe `ToastQueue`, and trigger `achievements_save()`.
4. **Unit Tests:**
   - Add unit tests verifying unlock idempotency (cannot earn points twice), cumulative progress thresholds (e.g., 50 matches, 100 goals), and save/load serialization integrity.
```

### Phase 2 Prompt: Match Engine & Career Mode Event Hooks

```text
### ROLE & TASK
Wire the **Global Achievements System** into our native SWOS match engine, competition system, and Career Mode events.

### REQUIREMENTS
1. **In-Match Goal & Discipline Hooks:**
   - Inside the native goal-scored handler, extract shot context (`HEADER`, `PENALTY`, `FREE_KICK`, `VOLLEY/OVERHEAD`, distance from goal `>= 18` yards, goalkeeper scorer, `<= 3s` from corner, `<= 2` min, `>= 89` min winner, and per-player match goals for hat-trick / 6-goal achievements) and invoke the corresponding `achievements_unlock()` checks.
   - Track early red cards (`< 60` min) and per-half scorelines (`halftime_player_goals`, `halftime_opp_goals`, plus max goal deficit during the match).
2. **Full-Time Match Completion Hook:**
   - At the end of every match, update `total_matches_played` and `total_goals_scored`, and evaluate: Friendly win, Clean Sheet, Penalty Shootout win, Extra-Time winner, Comeback wins (halftime, 2-goal, 3-goal deficit), 5+ goal margin, 10+ goals scored, 65%+ possession win, clinical finishing (`>= 75%` shots on target with `3+` goals), Fair Play (`0` cards), and Cup Giant-Killing (`opponent_division_tier < player_division_tier` by 1, 2, or 3+ tiers).
3. **Career & Management Hooks:**
   - Hook into Career Mode transfers (first signing, `£1M+` signing, `£1M+` resale profit), end-of-season outcomes (promotion, back-to-back promotion, top-flight title, unbeaten season, continental treble, 10 seasons), and our Stadium, Charisma (`90+ CP`), and Sponsor Challenge (`10` completed) subsystems.
```

### Phase 3 Prompt: Retro Toast Overlay & Main Menu Trophy Browser

```text
### ROLE & TASK
Implement the native UI screens and live toast notification banner for the **Global Achievements System** using SWOS's native graphics and menu primitives.

### REQUIREMENTS
1. **Live Retro Unlock Toast Notification:**
   - Hook into the main frame render loop (`ui_render_overlay`) to pop pending items from `ToastQueue` one at a time.
   - Render a sleek 16-bit retro banner at the top of the screen for `3.5 seconds` showing `[TROPHY] ACHIEVEMENT UNLOCKED!`, the title, description, and `[+XX PTS]` badge, accompanied by a celebratory SWOS sound effect.
2. **Main Menu Achievements Browser (`MENU_ACHIEVEMENTS`):**
   - Add an `ACHIEVEMENTS` option to the SWOS Main Menu.
   - Render a native menu screen displaying:
     - Header bar with `UNLOCKED: XX/50` and `GAMERSCORE: XXXX / 1650 PTS`.
     - Horizontal category filter (`ALL`, `BASICS`, `PITCH CRAFT`, `COMEBACKS`, `CUP GLORY`, `CAREER`, `MANAGEMENT`).
     - Scrollable list of achievements showing `[OK]` (highlighted) for unlocked items, `[..]` with progress bar/counter (e.g., `74 / 100`) for cumulative items, and `[--]` for locked items, along with point values (`10`–`100 PTS`).
```