# Added Features & Improvements (`swos-port`)

Overview of the features and enhancements implemented on the `feature/hotseat-multi-league` branch.

---

## 1. Hotseat & Multi-League / Multi-Country Career Mode

- **Multi-Player Hotseat Career (`HotseatCareerManager`):**
  - Supports multi-player hotseat career sessions (`careerPlayerCount.mnu` + per-player setup wizard), lifting the 4-player cap and isolating per-player SWOS VM contexts (`src/game/hotseat/`).
  - Turn-based lockstep scheduler (`hotseatScheduler.cpp`) with silent CPU stepping, **END TURN** flow on the league table and career hub, head-to-head fixture handling, and Euro Cup team pointer relinking across stages.
- **Multi-League & Multi-Country World Multiplexer:**
  - Lets different hotseat players manage clubs in **different divisions and different countries** within the same career (`hotseatWorld.cpp`), with unified Contest Menu fixture schedules and cross-division cup isolation.
  - Multi-world end-of-season processing (`hotseatEndSeason.cpp`) with promotion/relegation splits and cross-league job offers.
  - **V9 Multi-World Save/Load** (`hotseatSave.cpp`).
- **Human-to-Human & Cross-Country Transfers (`hotseatTransfers.cpp`):**
  - Interactive player-to-player transfer offers, counter-offers, and rejections via `showContinueAbortPrompt`, plus cross-country transfer market pool syncing.

---

## 2. 3D Physics Goal Nets & Ball / Goalframe Feedback

- **Toggleable 3D Verlet Mass-Spring Physics Goal Nets (`src/game/pitch/goalNet.cpp`):**
  - Replaces static baked goal nets with a 3D Verlet mass-spring mesh (roof, rear wall, and stitched side curtains) with slender cylindrical gradient-shaded goalposts/crossbar, dynamic projected shadows, and native clean pitch grass/line tile restoration beneath the goal area.
  - Soft hanging drapery profile (`1.85f` px roof sag, `1.15f` px side sag, tuned `kRestRestore` / `kDamping`), anchored rear stanchion corners, clamped per-frame impulse steps, 8 solver iterations, and hard max-stretch limits to prevent corner seam tearing on hard shots.
  - Active during **action replays and highlights** (`stepGoalNetReplayPhysics`).
- **Ball Physics & Goalframe Feedback (`goalNet.cpp`):**
  - Post and crossbar impact vibration, net shockwaves on woodwork hits, and goalframe feedback effects behind a gameplay option toggle.

---

## 3. Weather-Reactive Pitch, Spectators & Visual Polish

- **Weather-Reactive Pitch & Atmospheric Effects (`src/game/pitch/pitchEffects.cpp`, `src/game/pitch/pitch.cpp`):**
  - Per-pixel weather grass palette remapping across pitch types (`Frozen`, `Muddy`, `Wet`, `Soft`, `Normal`, `Dry`, `Hard`) with default `pitchType` set to `SEASONAL` (`-2` in `src/options/options.cpp`).
  - Surface splash/dust/snow particles, rain/snow weather overlays, and floodlight shadows.
  - Pitch type and season remain selectable in the Friendly menu even when fixed in options.
- **Authentic Team-Colored Animated Stadium Spectators (`src/game/pitch/pitch.cpp`):**
  - Restores animated stadium crowd sprites from original SWOS data, dynamically team-colored, with a padded texture atlas + nearest-neighbor scaling (eliminating tile seams and blurriness) and transparent lower grass borders so crowd stands blend seamlessly onto any pitch palette.
  - Fullscreen `Alt+Tab` texture reload fix.

---

## 4. Striker-Style Free-Kick Aiming

- **Interactive Dotted Trajectory & Curl Control (`src/game/freeKickAim.cpp`):**
  - Toggleable *Striker*-style dotted trajectory arc during free kicks with configurable elevation, aim angle, and left/right curl control.

---

## 5. Local & Cloud LLM Integration (`ai.ini`)

- **Configuration & Async WinHTTP Client (`src/llm/llmClient.h`, `src/llm/llmClient.cpp`):**
  - Configured via `ai.ini` alongside `swos.ini` (auto-creates a default `ai.ini` for local Ollama `gemma4:latest`, and supports `ollama`, `openai`, and `custom` endpoints with `apiKey`, `timeoutMs`, `maxTokens`, `temperature`, and per-feature toggles).
  - Runs all requests on a non-blocking background worker thread via WinHTTP with silent fallback if disabled, offline, or timed out.
- **Implemented LLM Features (`src/llm/llmFeatures.h`, `src/llm/llmFeatures.cpp`):**
  - **Post-Match Sports Newspaper & Chairman Reactions (`postMatchNewspaper`):** Generates `"DAILY SOCCER ECHO"` headline, match blurb, and Chairman reaction using real teams, score, penalties, scorers, stats, and pitch condition.
  - **Pre-Match Assistant Manager Briefing (`preMatchScouting`):** Generates `"ASSISTANT MANAGER"` tactical & pitch scouting report before kickoff.
  - **Living Transfer Market & Dressing Room Voices (`transferNegotiations`, `dressingRoomVoices`):**
    - `"TRANSFER DESK"` reactions when signing or selling players in **single-player Career** and **Hotseat Career**, plus in-character manager quotes on Hotseat transfer bids/counter-offers.
    - `"DRESSING ROOM VOICE"` post-match player reactions and periodic weekly training/transfer whispers in Career & Hotseat.
  - **Async Live Match Pundit Ticker (`livePunditTicker`):** Fires on major commentary events (`src/audio/comments.cpp`) and renders a non-blocking `"PUNDIT: ..."` broadcast bar at the bottom of the pitch in `src/game/gameLoop.cpp`.

---

## 6. Build, Testing & Documentation

- **Build & Packaging (`build.ps1`, `docs/build.md`):** Automated release compilation, test execution, and Windows demo packaging (`dist/swos-port-demo-windows-x64.zip`).
- **Expanded Test Suite (`tests/src/tests/`):** Unit tests covering Hotseat multi-league career, 3D physics goal nets, free-kick aiming, and LLM JSON/async queue/pundit flow (`49` animation + `570` mnu2h + `56` C++ tests).
- **Design & Architecture Docs (`docs/features/`):**
  - `docs/features/LLM.md` — local/cloud LLM architecture & `ai.ini` specification.
  - `docs/features/commentator.md` — future dynamic LLM + TTS spoken commentary design.
  - Single-player career immersion & visual/ball effects roadmaps.
