# LLM Integration Architecture (`swos-port`)

This document outlines how to integrate **Local LLMs** (e.g., Ollama running `gemma4:latest`) and **Cloud LLMs** (OpenAI-compatible / Gemini / Anthropic endpoints) into **Sensible World of Soccer (`swos-port`)** via a dedicated configuration file (`ai.ini`).

---

## 1. Configuration (`ai.ini`)

Store LLM settings in `ai.ini` alongside `swos.ini` (parsed with `SimpleIni`, already vendored in `3rd-party/SimpleIni`) so users can switch between local and cloud providers without recompiling—and keep API keys out of `swos.ini`.

```ini
[llm]
enabled = true
provider = ollama          ; ollama | openai | custom
endpoint = http://localhost:11434/api/chat
model = gemma4:latest
apiKey =                   ; optional for local Ollama, required for cloud endpoints
timeoutMs = 3000
maxTokens = 120
temperature = 0.7

[features]
postMatchNewspaper = true
preMatchScouting = true
transferNegotiations = true
livePunditTicker = true
cpuTacticalManager = false
```

### Provider Modes
- **`ollama` (Local):** Calls `POST http://localhost:11434/api/chat` (`stream: false`, `options: { num_predict, temperature }`). Ideal for zero-cost, offline play with `gemma4:latest`.
- **`openai` / `custom` (Cloud or Local OpenAI-Compatible):** Calls `POST <endpoint>` (e.g., `https://api.openai.com/v1/chat/completions`, OpenRouter, Groq, or LM Studio) with `Authorization: Bearer <apiKey>` and standard `messages` JSON payload.

---

## 2. Technical Architecture (Non-Blocking & Deterministic)

- **Transport:** Use Windows' built-in **WinHTTP** (`winhttp.lib`)—zero extra third-party networking libraries required.
- **Async Worker Thread:** All HTTP requests execute on a background worker thread with a thread-safe request/response queue. The 60 FPS match loop (`coreGameUpdate()`) and menus **never block** waiting for inference.
- **Graceful Fallback:** If `enabled = false`, Ollama is offline, a cloud key is missing, or `timeoutMs` expires, `swos-port` silently falls back to built-in static SWOS text without interrupting gameplay.
- **Compact JSON Context:** Keep prompts small (~150–250 tokens) by passing structured match/career facts (teams, score, scorers, minutes, weather, league positions) and requesting short, punchy 1990s-style responses.

---

## 3. Gameplay & Career Integration Points

### A. Post-Match Sports Newspaper & Chairman Reactions *(Highest Value, Zero Latency Risk)*
- Triggered automatically at full time while the stats/replay transition runs.
- Generates a retro **1990s sports-page headline + 2-sentence match report** and a **Chairman / Board reaction** referencing actual goalscorers, weather (`Frozen`, `Muddy`, `Wet`, etc.), shots/possession (`GameStats`), and league/cup stakes.

### B. Pre-Match Assistant Manager / Scout Briefing
- Triggered when entering the pre-match / Stadium screen.
- Summarizes the opponent's real SWOS squad (star players, formation, recent form) plus pitch-condition advice (e.g., fast ground skids on `Frozen`, dead bounces on `Muddy`).

### C. Living Transfer Market & Dressing Room Voices *(Career Mode)*
- **Transfer & Contract Negotiations:** Generates short in-character replies from rival managers or agents reflecting club stature, offer fairness, and Deadline Day urgency.
- **Dressing Room Pulses:** Occasional 1-line player reactions (e.g., a bench striker asking for a start after scoring, or a captain rallying the squad before a cup tie).

### D. Async Live Match Pundit / Commentary Ticker
- Fires non-blocking prompts on major events (goals, red cards, woodwork hits, penalties) and displays a brief retro broadcast ticker line once the response arrives (~0.5–1.5s later).

### E. High-Level CPU Tactical Manager *(Optional)*
- **Never** run an LLM inside per-frame player AI (`src/game/ai/ai.cpp`).
- Instead, optionally query the LLM once at halftime or around the 70th minute to pick a high-level CPU formation/tactical switch (e.g., switching to `4-2-4` when chasing a goal).
