# Dynamic LLM & TTS Match Commentator (`swos-port`)

This document captures the design options for replacing or augmenting the static event-based audio commentary in `src/audio/comments.cpp` with dynamic **LLM-generated commentary** (using the local/cloud provider configuration in `ai.ini` from `docs/features/LLM.md`).

---

## 1. Current System (`src/audio/comments.cpp`)

- Commentary is driven by **26 fixed event categories** (`corner`, `dirty_tackle`, `end_game_rout`, `end_game_sensational`, `end_game_so_close`, `free_kick`, `goal`, `good_play`, `good_tackle`, `header`, `hit_bar`, `hit_post`, `injury`, `keeper_claimed`, `keeper_saved`, `near_miss`, `own_goal`, `penalty`, `penalty_missed`, `penalty_scored`, `penalty_saved`, `red_card`, `substitution`, `tactic_changed`, `throw_in`, `yellow_card`).
- Each category plays a random pre-recorded sample via SDL_mixer on `m_commentaryChannel`.
- Static clips have no awareness of team names, player names, scoreline, match minute, weather/pitch type, or cup/league stakes.

---

## 2. Integration Approaches

Because an LLM outputs **text** while SWOS commentary is **audio**, there are three practical ways to integrate dynamic commentary without blocking the 60 FPS match loop:

### Option A: Live Broadcast / Pundit Ticker *(Text Overlay, Lowest Latency)*
- **How it works:** Instant classic SWOS audio plays as normal (`<50ms`), while a non-blocking background HTTP request sends match context (`team1`, `team2`, `score`, `minute`, `player`, `weather`, `event`) to the LLM configured in `ai.ini`.
- When the response arrives (~0.4–1.2s later), render a retro 1990s TV broadcast / pundit commentary banner on screen.
- **Pros:** Zero TTS dependencies, works out of the box with local Ollama (`gemma4:latest`) or cloud endpoints, never stalls gameplay.

### Option B: Spoken LLM Co-Commentary *(LLM -> Fast Local/Cloud TTS -> SDL_mixer)*
- **How it works:** Pipe the LLM's 1-sentence output into a fast local TTS engine (e.g., **Piper**, **Kokoro**, or Windows SAPI) or a cloud TTS API to synthesize a WAV/PCM buffer in memory, then play it on `m_commentaryChannel` via `Mix_LoadWAV_RW` / `Mix_QuickLoad_WAV`.
- **Latency considerations:** LLM (~0.4–1.5s) + TTS (~0.2–0.5s) results in a **~0.6–2.0s** turnaround.
  - Fast micro-events (`good_play`, `good_tackle`, `header`, `throw_in`, `corner`) should remain on instant static clips.
  - Major moments (`goal`, `own_goal`, `penalty`, `red_card`, `yellow_card`, `hit_bar`, `hit_post`, `substitution`, `end_game_*`) naturally suit a 1-second "co-commentator" follow-up right after the instant classic shout (e.g., during goal celebrations/replays).

### Option C: Pre-Match Dynamic Voice Pack *(Zero In-Match Latency)*
- **How it works:** While on the pre-match Stadium screen, query the LLM + TTS once to generate 8–12 match-specific audio clips referencing the two clubs, star players, weather, and stakes, and inject them into `SampleTable` in memory for that match.
- **Pros:** **0ms latency** when triggered during play, and no mid-match inference load.

---

## 3. Recommended Hybrid Architecture

Combine **Option A** and **Option B** behind `ai.ini` toggles:
1. Keep instant static clips for high-frequency events (`good_tackle`, `header`, `throw_in`, `corner`) so match audio stays immediate.
2. On major events (`goal`, `own_goal`, `penalty`, `red_card`, `hit_bar`, `hit_post`, `end_game_*`), enqueue an async LLM request with compact structured match state (~150 tokens).
3. Display the returned 1-sentence line in an optional broadcast ticker, and—when a TTS backend is configured—speak it as a co-commentator line on `m_commentaryChannel` after any currently playing instant clip finishes.
4. Fall back silently to static commentary whenever `ai.ini` disables the feature or the local/cloud provider is unreachable.
