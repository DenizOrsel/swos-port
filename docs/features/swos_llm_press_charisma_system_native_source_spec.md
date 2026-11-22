# Feature Specification & Agentic Blueprint: SWOS LLM Press & Coach Charisma System (Native Source Integration)

## 1. System Overview & Vision

Built directly into the **SWOS source code**, the **AI Press & Coach Charisma System** integrates natively into Career Mode's match lifecycle, save-file serialization, and end-of-season financial summary. 

Rather than relying on external memory bridges or window overlays, the game engine itself gathers live Career Mode context (league table, recent results, derby status, match scoreline, and pre-match promises) and dispatches non-blocking HTTP requests to a local or cloud LLM endpoint (e.g., Ollama on `localhost:11434` or OpenAI-compatible APIs). Player responses are rendered using SWOS's native 16-bit menu system, dynamically adjusting the manager's **Coach Charisma**, which feeds into matchday attendance (linking with the Stadium System) and converts into a **Commercial & Media Bonus** alongside TV Rights at the start of the next season.

## 2. Core Gameplay Loop

1. **Pre-Match Press Conference (`MENU_PRESS_PRE_MATCH`):**
   * Triggered natively between squad confirmation and match kick-off.
   * The engine sends pre-match context to the async LLM service and displays a journalist question with **4 distinct managerial responses** (*Confident/Boastful*, *Humble/Cautious*, *Tactical/Analytical*, *Mind Games/Hostile*).
   * The chosen `stance` is saved in `CareerPressState` so the post-match journalist can hold the manager accountable.
2. **Post-Match Press Conference (`MENU_PRESS_POST_MATCH`):**
   * Triggered natively after the full-time whistle and match statistics rollup.
   * The LLM evaluates the actual result (scoreline, upset win/loss, red cards) against the saved `pre_match_stance` to generate a reactive follow-up question and 4 response options.
3. **Charisma Adjustment & Offline Fallback:**
   * Selecting an option applies its `charisma_delta` (`-8` to `+8`) directly to `club->press_state.charisma_points` (clamped `0..100`, starting at `50`).
   * If the LLM endpoint is unreachable or times out (`> 2500ms`), the engine seamlessly draws from a built-in C/C++ pool of 40+ context-tagged fallback templates so gameplay is never blocked.
4. **End-of-Season Financial Conversion:**
   * During the native end-of-season financial rollup (where Prize Money and TV Rights are credited), accumulated **Charisma Points (CP)** calculate a **Charisma & Media Bonus**, added directly to `club->finances.cash` and rendered on the Season Financial Summary screen directly below **TV Rights**.

## 3. Native Data Structures & Save Serialization

### 3.1 Core Structs (`press_charisma.h`)

```c
typedef enum {
    PRESS_PHASE_PRE_MATCH = 0,
    PRESS_PHASE_POST_MATCH = 1
} PressPhase;

typedef enum {
    STANCE_NONE = 0,
    STANCE_HUMBLE = 1,
    STANCE_CONFIDENT = 2,
    STANCE_TACTICAL = 3,
    STANCE_HOSTILE = 4
} ManagerialStance;

typedef struct {
    char text[96];
    ManagerialStance stance;
    int8_t charisma_delta;
    char media_reaction[96];
} PressOption;

typedef struct {
    char journalist_name[48];
    char question[180];
    PressOption options[4];
    bool is_ready;
} PressConferencePrompt;

typedef struct {
    int16_t charisma_points;             // 0..100 (Starts at 50)
    ManagerialStance last_pre_match_stance;
    char last_pre_match_quote[96];
    uint16_t interviews_completed;
    int64_t last_season_charisma_bonus;
} CareerPressState;
```

### 3.2 Save-File Integration

* Embed `CareerPressState press_state;` directly inside `CareerClubState` (alongside `StadiumData`).
* Serialize `press_state` inside `save_career_file()` and `load_career_file()` under the bumped save version header, initializing `charisma_points = 50` when migrating legacy saves.

## 4. Native LLM Client & Structured JSON Schema

### 4.1 Non-Blocking HTTP Client (`llm_client.h` / `llm_client.c`)

* Uses a lightweight background worker thread (`std::thread` / `SDL_CreateThread` / Win32/POSIX socket) so the SWOS main render loop never freezes while waiting for inference.
* Displays a native retro `"INCOMING PRESS QUESTION..."` spinner box if the player arrives at the press screen before the HTTP response completes, with an automatic fallback timeout of `2.5s`.
* Configurable via `swos_mod.ini` (`llm_enabled`, `llm_endpoint_url`, `llm_model`, `llm_api_key`, `llm_timeout_ms`).

### 4.2 Context JSON Sent to LLM

```json
{
  "phase": "POST_MATCH",
  "manager_name": "J. Doe",
  "club": "Ajax",
  "opponent": "Feyenoord",
  "is_derby": true,
  "league_position": 2,
  "opponent_position": 1,
  "recent_form": ["W 2-0", "D 1-1", "W 3-1"],
  "pre_match_stance": "CONFIDENT",
  "pre_match_quote": "Nobody in this league can stop us right now.",
  "match_result": {
    "club_goals": 0,
    "opponent_goals": 3,
    "red_cards": 1
  },
  "current_charisma": 64
}
```

### 4.3 Deterministic Response Schema

```json
{
  "journalist_name": "Piet van Dijk (De Telegraaf)",
  "question": "You claimed nobody could stop you, yet you lost 3-0 to your biggest rivals. Do you regret those words?",
  "options": [
    {
      "stance": "HUMBLE",
      "text": "I got it wrong today. Feyenoord were superior and I take full responsibility.",
      "charisma_delta": 3,
      "media_reaction": "The press respects your honesty after a heavy defeat."
    },
    {
      "stance": "TACTICAL",
      "text": "The early red card forced us out of shape. We will analyze the tape.",
      "charisma_delta": 0,
      "media_reaction": "A measured, analytical response that calms the room."
    },
    {
      "stance": "CONFIDENT",
      "text": "It is one battle lost, not the war. Judge us at the end of the season.",
      "charisma_delta": -2,
      "media_reaction": "Bold defiance, though fans remain frustrated by the scoreline."
    },
    {
      "stance": "HOSTILE",
      "text": "Next question. I am not answering ridiculous traps.",
      "charisma_delta": -6,
      "media_reaction": "You snapped at reporters, souring relations with the press."
    }
  ]
}
```

## 5. Charisma Tiers & End-of-Season Financial Formula

### 5.1 Charisma Tiers & Multipliers

| Charisma Rating | Seasonal CP | Reputation Tier | TV Rights Bonus Multiplier ($M_{\text{tier}}$) | Stadium Attendance Factor ($M_{\text{charisma}}$) |
| :--- | :--- | :--- | :--- | :--- |
| **Toxic** | `< 20 CP` | Media Pariah | `0.00x` (No Bonus) | `0.90x` |
| **Uninspiring** | `20 – 44 CP` | Dull Tactician | `0.25x` | `0.95x` |
| **Respectable** | `45 – 69 CP` | Media Friendly | `0.50x` | `1.00x` |
| **Charismatic** | `70 – 89 CP` | Box Office Manager | `1.00x` | `1.08x` |
| **Iconic** | `90 – 100 CP` | Global Superstar | `1.50x` | `1.15x` |

### 5.2 End-of-Season Bonus Formula

Tied directly to the club's native **Division Base TV Rights** ($R_{TV}$) so payouts scale automatically across all leagues and nations:

$$\text{Charisma Bonus} = \left\lfloor R_{TV} \times M_{\text{tier}} \times \left(\frac{\text{CP}}{100}\right) \right\rfloor$$

## 6. Native UI Mockups (SWOS Menu Renderer)

### 6.1 Native Press Conference Screen (`MENU_PRESS_CONFERENCE`)

```text
+-------------------------------------------------------------------+
|  PRE-MATCH PRESS CONFERENCE                CHARISMA: 64 CP [||||] |
+-------------------------------------------------------------------+
|  JOURNALIST (DAILY SPORT):                                        |
|  "You are unbeaten in five games ahead of today's clash with      |
|   Liverpool. Are your players feeling unstoppable right now?"     |
+-------------------------------------------------------------------+
|  [1] "We respect Liverpool, it will be a very tough afternoon."   |
|  [2] "With our current form, nobody in this league can stop us."  |
|  [3] "Form means nothing in games like this. Only focus matters." |
|  [4] "Ask their manager if he's worried—not me."                  |
+-------------------------------------------------------------------+
```

### 6.2 Native Season Financial Summary Integration

```text
+-------------------------------------------------------------------+
|                    SEASON FINANCIAL SUMMARY                       |
+-------------------------------------------------------------------+
|  LEAGUE PRIZE MONEY ..............................  £1,500,000    |
|  CUP PRIZE MONEY .................................    £250,000    |
|  TV RIGHTS REVENUE ...............................  £2,000,000    |
|  CHARISMA & MEDIA BONUS (80 CP - CHARISMATIC) ....  £1,600,000    |
+-------------------------------------------------------------------+
|  TOTAL SEASON INCOME .............................  £5,350,000    |
|  NEW TRANSFER BUDGET .............................  £6,840,000    |
+-------------------------------------------------------------------+
```

## 7. Master Prompt for AI Coding Agent

```text
### ROLE & TASK
You are an expert game engine developer working directly in our *Sensible World of Soccer* (SWOS) source codebase. Implement the **Native LLM Press Conference & Coach Charisma System** directly into Career Mode.

### REQUIREMENTS
1. **Native Data Models & Save Serialization (`press_charisma.h` / `press_charisma.c`):**
   - Define `CareerPressState` (`charisma_points` initialized to 50, `last_pre_match_stance`, `last_pre_match_quote`, `interviews_completed`, `last_season_charisma_bonus`).
   - Embed `CareerPressState` inside the native Career Mode club struct and serialize it cleanly inside the native career save/load functions with backward-compatible initialization for older saves.
2. **Async LLM HTTP Client & Offline Fallback (`llm_client.h` / `llm_client.c`):**
   - Implement a non-blocking HTTP POST helper (reading endpoint, model, API key, and timeout from `swos_mod.ini`) to fetch structured JSON press interviews.
   - Pre-fetch the pre-match interview asynchronously as soon as the player enters the pre-match fixture screen, and pre-fetch the post-match interview immediately at the full-time whistle.
   - Include a rich native array of offline fallback questions/answers categorized by match context (`DERBY`, `UNDERDOG`, `FAVORITE`, `BIG_WIN`, `HEAVY_LOSS`, `DRAW`) so the feature works smoothly even without an active LLM server.
3. **Native UI Menu Integration:**
   - Implement `MENU_PRESS_CONFERENCE` using SWOS's native menu panels, bitmap font renderer, and directional/action input handlers.
   - Display the journalist's question, current `CHARISMA: XX CP` bar, and the 4 selectable options. Upon selection, apply `charisma_delta` (clamped `0..100`), display the `media_reaction` confirmation banner for 1.5s (or until keypress), and proceed to the next state.
4. **End-of-Season Financial Rollup & TV Rights Screen:**
   - Locate the end-of-season financial transition where `TV Rights` are calculated and credited to club cash.
   - Compute `Charisma Bonus = floor(tv_rights * tier_multiplier * (charisma_points / 100.0))`, add it directly to the club's `cash` balance, and render the `CHARISMA & MEDIA BONUS` line item directly underneath `TV RIGHTS REVENUE` on the Season Financial Summary screen.
   - Expose `press_get_attendance_multiplier(int16_t charisma_points)` so the native Stadium System can apply the `0.90x..1.15x` crowd multiplier.
```