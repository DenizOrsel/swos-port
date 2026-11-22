# Feature Specification & Agentic Blueprint: SWOS Stadium & Facilities System (Native Source Integration)

## 1. System Overview & Vision

Because we are building directly from the **SWOS source code**, the **Stadium & Facilities System** will be implemented as a first-class native Career Mode subsystem. Rather than relying on external memory bridges or overlays, stadium data will live directly inside the native `Club` / `CareerState` structs, serialize directly into the career save file, hook cleanly into the native matchday & end-of-season game loop functions, and render using SWOS's native 16-bit menu/font renderer.

Managers can inspect their ground from the Career Club Menu, expand seating capacity, upgrade pitch quality, invest in training/medical/commercial facilities, set ticket prices, and experience direct financial and match-engine effects.

## 2. Core Gameplay Mechanics

### 2.1 Stadium & Facility Modules

Every club in Career Mode initializes with procedurally seeded stadium attributes derived from its **Division Tier** (`1` = Top Flight, `4` = Lower League) and **Club Reputation**.

| Module | Levels | Core Effect | Native Gameplay & Financial Impact | 
| ----- | ----- | ----- | ----- | 
| **Stand Capacity** | `1 – 10` (`5,000` to `85,000`) | Increases maximum home attendance | Scales matchday gate receipts on home fixtures | 
| **Pitch Quality** | `1 – 5` (Muddy → Pristine Turf) | Improves surface & lowers injury risk | Overrides home pitch condition generation (`NORMAL`/`DRY` at Lvl 4+) & lowers match injury roll probability | 
| **Training Complex** | `1 – 5` (Basic → Elite) | Accelerates player skill progression | Applies a multiplier to end-of-season player attribute growth & market value appreciation | 
| **Medical Center** | `1 – 5` (Physio Room → Sports Lab) | Accelerates injury recovery | Reduces `player.injury_matches` upon injury assignment and speeds up per-turn recovery ticks | 
| **Commercial & VIP** | `1 – 5` (Kiosks → Luxury Boxes) | Multiplier on matchday revenue | Adds `+10%` to `+50%` non-ticket matchday hospitality & merchandise revenue | 

### 2.2 Dynamic Attendance & Gate Receipts Formula

During the native home post-match financial calculation routine, attendance is computed dynamically using capacity, ticket price elasticity, opponent prestige, league position, and **Coach Charisma** (if the Press/Charisma module is active):

$$
\text{Attendance} = \min\left(\text{Capacity}, \left\lfloor \text{Capacity} \times F_{\text{base}} \times M_{\text{opp}} \times M_{\text{price}} \times M_{\text{charisma}} \right\rfloor\right)
$$

* $F_{\text{base}}$: Base fill rate from current league standing (`0.55` to `0.95`).

* $M_{\text{opp}}$: Opponent prestige multiplier (`1.25` for top-table/derby rivals, `0.90` for lower-table clubs).

* $M_{\text{price}}$: Ticket price elasticity (`1.15` for Cheap, `1.00` for Standard, `0.78` for Expensive).

* $M_{\text{charisma}}$: Optional multiplier from Coach Charisma (`0.90` to `1.15`).

**Net Matchday Profit** added directly to `club->finances.cash`:

$$
\text{Net Gate Profit} = (\text{Attendance} \times \text{Ticket Price}) \times (1 + \text{Commercial Bonus}) - \text{Matchday Upkeep}
$$

## 3. Native Data Structures & Save-File Serialization

### 3.1 Core Structs (`stadium.h` / `stadium_types`)

```
typedef enum {
    TICKET_POLICY_CHEAP = 0,
    TICKET_POLICY_STANDARD = 1,
    TICKET_POLICY_EXPENSIVE = 2
} TicketPolicy;

typedef enum {
    MODULE_CAPACITY = 0,
    MODULE_PITCH = 1,
    MODULE_TRAINING = 2,
    MODULE_MEDICAL = 3,
    MODULE_COMMERCIAL = 4,
    MODULE_COUNT = 5
} StadiumModuleType;

typedef struct {
    bool is_active;
    StadiumModuleType module;
    uint8_t target_level;
    uint8_t matches_remaining;
    int32_t cost_paid;
} ConstructionProject;

typedef struct {
    char stadium_name[32];
    uint8_t capacity_level;      // 1..10
    uint32_t max_capacity;       // 5,000..85,000
    uint8_t pitch_level;         // 1..5
    uint8_t training_level;      // 1..5
    uint8_t medical_level;       // 1..5
    uint8_t commercial_level;    // 1..5
    TicketPolicy ticket_policy;
    int32_t ticket_price;
    ConstructionProject active_project;
    
    // Seasonal tracking
    uint32_t season_total_attendance;
    int64_t season_gate_revenue;
    int64_t season_upkeep_paid;
} StadiumData;


```

### 3.2 Save Serialization Integration

* Embed `StadiumData stadium;` inside the managed club's `CareerClubState` struct.

* Bump the save-file format version header (`SAVE_VERSION_STADIUM`) and read/write the fixed-size `StadiumData` block sequentially inside `save_career_file()` and `load_career_file()` (with backward-compatible initialization via `stadium_init_for_club()` when loading legacy saves).

### 3.3 Upgrade Progression Table

| Target Level | Capacity Upgrade Cost | Facility Upgrade Cost (Pitch/Train/Med/Comm) | Build Time (Matches) | Matchday Upkeep | 
| ----- | ----- | ----- | ----- | ----- | 
| **Level 2** | `£150,000` (`12,000` seats) | `£100,000` | `2 Matches` | `£5,000` | 
| **Level 3** | `£400,000` (`20,000` seats) | `£250,000` | `3 Matches` | `£12,000` | 
| **Level 4** | `£850,000` (`30,000` seats) | `£500,000` | `4 Matches` | `£22,000` | 
| **Level 5** | `£1,600,000` (`45,000` seats) | `£1,000,000` | `5 Matches` | `£38,000` | 
| **Level 6–10** | `£2.5M – £12M` (up to `85,000`) | *(Max Level 5)* | `6–8 Matches` | `£55k – £140k` | 

*Rule:* Only **1 construction project** may run at a time. Each competitive match played decrements `active_project.matches_remaining` by `1`.

## 4. Native Game Loop & Engine Integration Points

1. **Career Club Menu (`menu_club_hub`):**

   * Add a native `STADIUM` menu entry alongside `SQUAD`, `TACTICS`, `TRANSFERS`, and `FINANCES` that pushes `MENU_STADIUM_MANAGE` onto the UI menu stack.

2. **Pre-Match Pitch Generation (`match_init_conditions`):**

   * When initializing a home fixture, check `home_club->stadium.pitch_level`. If `pitch_level >= 4`, clamp weather-degraded pitch states (`MUDDY`, `WET`) to `NORMAL` or `DRY`.

3. **Match Engine Injury & Recovery (`player_apply_injury` / `career_advance_turn`):**

   * When a managed player sustains an injury, scale down `injury_matches` by `stadium.medical_level * 10%` (minimum `1` match).

4. **Post-Match Processing (`career_on_match_finished`):**

   * **Construction Tick:** If `stadium.active_project.is_active`, decrement `matches_remaining`. When it hits `0`, increment the target module level, mark `is_active = false`, and enqueue a native post-match news/alert banner (`"STADIUM UPGRADE COMPLETE!"`).

   * **Home Gate Calculation:** Replace the legacy static gate receipt calculation with `stadium_calculate_matchday_report()`, credit `net_gate_profit` directly to `club->cash`, and pass the `MatchdayReport` struct to the native Post-Match Financial Summary screen.

5. **End-of-Season Player Growth (`career_end_of_season_progression`):**

   * Apply a `+5%` to `+25%` skill progression bonus based on `stadium.training_level` during annual player development rolls, and reset `season_total_attendance`, `season_gate_revenue`, and `season_upkeep_paid`.

## 5. Native UI Screens (SWOS Menu System)

### 5.1 Native Stadium Management Menu (`MENU_STADIUM_MANAGE`)

Built using SWOS's standard menu box primitives, bitmap font routines, and cursor/key navigation:

```
+-------------------------------------------------------------------+
|  STADIUM & FACILITIES: DE MEER ARENA       BANK: £3,420,000       |
+-------------------------------------------------------------------+
|  [1] STAND CAPACITY .... LVL 4/10 (28,000 SEATS)   UPG: £850,000  |
|  [2] PITCH QUALITY ..... LVL 3/5  (GOOD TURF)      UPG: £250,000  |
|  [3] TRAINING COMPLEX .. LVL 2/5  (REGIONAL)       UPG: £250,000  |
|  [4] MEDICAL CENTER .... LVL 2/5  (-15% INJURY)    UPG: £250,000  |
|  [5] COMMERCIAL & VIP .. LVL 2/5  (+15% REVENUE)   UPG: £250,000  |
+-------------------------------------------------------------------+
|  [T] TICKET PRICING:  < STANDARD (£25) >   EST. FILL: 88%         |
|  STATUS: EXPANDING STANDS TO LVL 5 (3 MATCHES REMAINING)          |
+-------------------------------------------------------------------+
|  [SELECT] UPGRADE MODULE   [LEFT/RIGHT] TICKET PRICE   [ESC] BACK |
+-------------------------------------------------------------------+


```

### 5.2 Native Home Post-Match Gate Report (`MENU_POST_MATCH_FINANCES`)

```
+-------------------------------------------------------------------+
|               DE MEER ARENA - MATCHDAY GATE REPORT                |
+-------------------------------------------------------------------+
|  ATTENDANCE: 26,840 / 28,000 (95.8% FULL - RIVALRY BOOST)         |
|  TICKET REVENUE (26,840 x £25) ...................    £671,000    |
|  COMMERCIAL & VIP BONUS (LVL 2: +15%) ............   +£100,650    |
|  STADIUM MAINTENANCE UPKEEP ......................    -£22,000    |
+-------------------------------------------------------------------+
|  NET MATCHDAY STADIUM PROFIT .....................   +£749,650    |
+-------------------------------------------------------------------+


```

## 6. Agentic Development Implementation Plan & Prompts

Feed these phased prompts sequentially to your coding agent so it can inspect your repository conventions and implement the feature cleanly from source.

### Phase 1 Prompt: Core Data Structures, Economy Logic & Save Serialization

```
### ROLE & TASK
You are an expert game engine developer working directly in our *Sensible World of Soccer* (SWOS) source codebase. Implement the core **Stadium & Facilities System** data structures, economy rules, and native save-file serialization.

### REQUIREMENTS
1. **Inspect Codebase Conventions:**
   - Locate where Career Mode club state (`cash`, `division`, `reputation`, `squad`) and career save/load serialization are defined in our repository.
2. **Define Stadium Data Models (`stadium.h` / `stadium.c` or matching project language):**
   - Implement `StadiumData`, `StadiumModuleType` (`CAPACITY` 1-10, `PITCH` 1-5, `TRAINING` 1-5, `MEDICAL` 1-5, `COMMERCIAL` 1-5), `TicketPolicy` (`CHEAP`, `STANDARD`, `EXPENSIVE`), and `ConstructionProject`.
   - Implement `stadium_init_for_club(Club* club)` to procedurally seed realistic starting levels based on division tier and club reputation.
3. **Implement Upgrade & Economy Engine:**
   - `stadium_get_upgrade_quote(const StadiumData* stadium, StadiumModuleType module, int64_t club_cash)` returning cost, build time in matches, preview text, and status (`OK`, `INSUFFICIENT_FUNDS`, `MAX_LEVEL`, `CONSTRUCTION_IN_PROGRESS`).
   - `stadium_start_upgrade(StadiumData* stadium, int64_t* club_cash, StadiumModuleType module)` to deduct cash directly and initiate the construction project.
   - `stadium_calculate_matchday(const StadiumData* stadium, int club_rank, int opp_rank, bool is_derby, int charisma_score)` implementing ticket price elasticity, opponent prestige, commercial bonus, and upkeep deduction.
4. **Save Serialization:**
   - Integrate `StadiumData` into the career save/load routines with safe version checking so legacy save files auto-initialize cleanly.
5. **Unit Tests:**
   - Add unit tests verifying procedural seeding across divisions, upgrade validation edge cases, construction countdowns, and attendance/revenue math.


```

### Phase 2 Prompt: Game Loop, Match Engine & Progression Hooks

```
### ROLE & TASK
Wire the **Stadium & Facilities System** directly into the native SWOS Career Mode match lifecycle, injury/pitch mechanics, and end-of-season progression.

### REQUIREMENTS
1. **Pre-Match Pitch Condition Hook:**
   - In the match initialization routine where pitch conditions are selected, check the home club's `stadium.pitch_level`. If `pitch_level >= 4`, prevent `MUDDY` or `WET` pitch conditions and upgrade them to `NORMAL` or `DRY`.
2. **Medical Center Injury Hook:**
   - In the player injury assignment routine, check the club's `stadium.medical_level` and reduce newly assigned `injury_matches` by `medical_level * 10%` (clamped to a minimum of `1` match).
3. **Post-Match Routine (`on_match_completed`):**
   - Advance `stadium.active_project`: decrement `matches_remaining`; when it reaches `0`, apply the module level increment, clear `is_active`, and set a flag/message for the post-match news banner (`"STADIUM UPGRADE COMPLETE!"`).
   - For home matches: replace the default gate receipt calculation with `stadium_calculate_matchday()`, add `net_gate_profit` directly to the club's `cash` field, update seasonal stadium totals, and pass the resulting `MatchdayReport` struct to the post-match summary state.
4. **End-of-Season Progression Hook:**
   - In the annual player skill progression routine, apply a skill-growth multiplier based on `stadium.training_level` (`+5%` per level above 1), and reset the stadium's seasonal attendance/revenue counters for the new season.


```

### Phase 3 Prompt: Native SWOS Menu & Post-Match UI Screens

```
### ROLE & TASK
Implement the native UI screens for the **Stadium & Facilities System** using SWOS's existing menu stack, input handling, and bitmap/UI rendering primitives.

### REQUIREMENTS
1. **Career Hub Entry Point:**
   - Add a `STADIUM` button/entry to the main Career Club Menu that transitions into `MENU_STADIUM_MANAGE`.
2. **Stadium Management Screen (`MENU_STADIUM_MANAGE`):**
   - Render using native SWOS menu panels and font drawing calls:
     - Header showing Stadium Name and current Club Bank Balance.
     - Selectable rows for all 5 modules (`Stand Capacity`, `Pitch Quality`, `Training Complex`, `Medical Center`, `Commercial & VIP`) displaying current level, active stats, and next upgrade cost.
     - Interactive Ticket Policy selector (`CHEAP`, `STANDARD`, `EXPENSIVE`) showing ticket price and estimated fill percentage.
     - Active construction progress banner (`[███░░] X MATCHES REMAINING`) when a build is underway.
   - Handle native directional/action inputs: up/down to select module, left/right on the ticket row to cycle pricing, action button to purchase upgrade (with a native Yes/No confirmation dialog), and back/cancel to return to the Career Club Menu.
3. **Post-Match Gate Report & Construction Banner:**
   - In the post-match financial summary screen, render the native `MatchdayReport` breakdown (Attendance vs. Capacity, Ticket Revenue, Commercial Bonus, Upkeep, and Net Profit).
   - If a construction project completed after the match, display a native congratulatory banner highlighting the newly upgraded facility.


```