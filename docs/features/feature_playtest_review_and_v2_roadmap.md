# SWOS-Port Feature Playtest Review & V2 Improvement Roadmap

> **Date:** 2026-11-24  
> **Scope:** Playtest evaluation of implemented Career, LLM, Stadium, Charisma, Sponsor Challenge, and Achievement systems, with concrete high-impact V2 upgrades and bug fixes.

---

## Executive Summary (Playtest Verdict)

| Feature | Current Playtest Status | Core Verdict | Priority |
| --- | --- | --- | --- |
| **Sponsor Challenges** | **Loved / High Impact** | Huge joy in Division 4 careers due to meaningful financial risk/reward. Expand to 5 simultaneous offers + richer variety. | **P1** |
| **Live Stream (Pundit & Personas)** | **Loved / High Impact** | Entertaining even while actively playing. Keep core intact and feed gentle hooks from Challenges, Form, and Stadium. | **Keep & Polish** |
| **Stadium & Facilities** | **Promising / Needs Tuning** | Fun foundation, but build times are far too short, capacity/costs need smoothing, ticket pricing should react to last-5 form, Medical feels invisible, and VIP/Commercial needs clarity. | **P1** |
| **Global Achievements** | **Loved / Too Few** | Exciting and fun to unlock, but 50 achievements leave a mid-career dry spell. Expand to 100–120 with tiered ladders and match-craft specials. | **P2** |
| **Coach Charisma (Press)** | **Needs Rebalance** | Maxes out to 100 almost immediately because answers are obvious (3 positive, 1 cartoonishly hostile). Needs contextual trade-offs, shuffled options, and form decay. | **P2** |
| **Dressing Room Voices** | **Turned OFF + Bug** | Feels boring/passive, **and still pops up occasionally even when OFF** due to a gate bug in `onCareerWeeklyTickLlm()`. | **P0 (Bug) / P3 (Revamp)** |
| **Post-Match Newspaper** | **Turned OFF** | Boring routine prose after standard fixtures. Candidate for milestone-only editions or non-blocking ticker. | **P3** |
| **Pre-Match Assistant (Scouting)** | **Turned OFF** | Boring generic text that adds popup fatigue. Candidate for compact squad-data intel or folding into Challenge screen. | **P3** |
| **Transfer News / Rumours** | **Turned OFF** | Insufficient excitement (pure flavour whispers). Candidate for actionable bargain alerts or incoming offer dilemmas. | **P3** |

---

## Detailed Review & Improvement Plan

### 1. Dressing Room Voices — Bug Fix + Optional Redesign
- **Codebase Findings (`src/llm/llmFeatures.cpp`):**
  - In `onCareerWeeklyTickLlm()` (lines `1131–1154`), the guard checks `(!cfg.features.dressingRoomVoices && !cfg.features.transferNegotiations)`, then sets `isMarket = cfg.features.transferNegotiations && ((s_weeklyTickCounter % 6) == 0)`.
  - **Bug:** When `dressingRoomVoices = OFF` and `transferNegotiations = ON`, every odd multiple of 3 weekly ticks (`3, 9, 15...`) evaluates `isMarket == false` and still displays a `"DRESSING ROOM"` popup.
- **Why it feels flat:** Purely passive prose with zero mechanical impact on morale, form, tactics, or finances.
- **Planned Improvements:**
  1. **Immediate Bug Fix:** Strictly check `cfg.features.dressingRoomVoices` before emitting any Dressing Room popup in `onCareerWeeklyTickLlm()`.
  2. **Optional High-Impact Redesign:** Trigger only on true flashpoints (3-match slump, star player dropped, title run-in) as a **1-click Managerial Dilemma** (e.g. *Fine player / Back publicly / Promise next start*) with real consequences (charisma, morale/sponsor boost, or transfer demand).

---

### 2. Newspaper, Pre-Match Assistant (Scouting), & Transfer News — Reducing Popup Fatigue
All three currently interrupt the Career loop with passive prose popups that carry no gameplay stakes.

- **Pre-Match Assistant / Scouting:**
  - *Problem:* Generic flavour text before every routine fixture.
  - *V2 Direction:* Surface **real, compact SWOS squad intel** (opponent formation, top scorer's rating/value, weakest defender/GK) with an optional pre-match focus — or embed a 2-line scouting strip directly inside the Sponsor Challenge header so there is zero extra popup.
- **Post-Match Newspaper:**
  - *Problem:* Repetitive after normal league games.
  - *V2 Direction:* Restrict modal editions to **Landmark Matches** (Derbies, Cup Finals, Promotions/Relegations, 5+ goal routs, unbeaten record breakers), and optionally move routine headlines into a non-blocking Career Hub ticker or telemetry export.
- **Transfer News / Rumours:**
  - *Problem:* Random whispers detached from real SWOS transfer deals.
  - *V2 Direction:* Convert into **Actionable Market Alerts** — highlight genuine undervalued players currently on the transfer list (bargain tips for Div 4/3 budgets) or trigger an **Incoming Bid / Counter-Offer** event for one of your in-form players.

---

### 3. Coach Charisma / Press Conferences — Rebalance for Depth & Stakes
- **Codebase Findings (`src/game/pressCharisma.cpp`):**
  - Career starts at `50/100` with 2 interviews per match (pre-match + post-match).
  - In the 42 templates (lines `322–587`), **3 of 4 choices grant `+2` to `+5`**, while only the obviously hostile option loses points (`-2` to `-8`). Players effortlessly hit `100` within ~6–7 matches.
- **V2 Improvements:**
  - **Contextual Trade-Offs (No Free Wins):** Make answers depend on real table/form context (e.g., boasting when mid-table or after a lucky 1–0 win *costs* charisma; being overly cautious before a derby disappoints supporters; making bold pre-match promises creates a sharper win payoff or defeat backfire).
  - **Shuffled Choice Order & Hidden Stance Patterns:** Randomize button ordering so players read the question instead of always clicking slot #2.
  - **Slower Climb + Form Decay:** Smaller normal gains (`+1` to `+3`), sharper penalties (`-4` to `-8`) for broken promises/losses, and natural drift back toward `50` during losing/winless runs — making *Iconic (90+)* genuinely earned and maintained.
  - **Stronger Gameplay Rewards:** Tie Charisma directly into **Sponsor Challenge payout multipliers (`+5%` to `+20%`)** alongside home attendance demand.

---

### 4. Sponsor Challenges — Expand to 5 Offers & Richer Variety
- **Codebase Findings (`src/game/sponsorChallenge.cpp`):**
  - Currently offers `kOfferedChallengeCount = 3` challenges drawn from **9 types** (`CleanSheetWin`, `PlayerGoals`, `FirstHalfGoals`, `PossessionWin`, `ShotsOnTarget`, `WinByMargin`, `TeamGoals`, `LongRangeGoal`, `HeaderGoal`).
- **V2 Improvements:**
  - **Expand to 5 Simultaneous Challenges** organized across risk tiers (*Safe / Standard / Tactical / High-Risk / Wildcard*) with **tiered combo jackpots** (e.g., accept 3+ for `+25%`, All-5 Clean Sweep for `+75%`!).
  - **Expand Pool from 9 → 18–20+ Types** using existing `matchTelemetry.cpp` data:
    - *Disciplined Display* — 0 yellow and 0 red cards
    - *Early Blitz* — score within the first 15 in-game minutes
    - *Second-Half Surge* — score 2+ goals after halftime
    - *Playmaker Assist* — named starter records an assist
    - *Shutdown Defence* — hold opponent to `<= 2` shots on target
    - *Clinical Finishing* — score 2+ goals with `>= 50%` shot accuracy
    - *Non-Attacker Goal* — midfielder or defender scores (high payout)
    - *Set-Piece / Corner Goal* or *Volley Special* — high-risk jackpot
    - *Keeper Heroics* — win with `3+` GK saves or a clean sheet
    - *Underdog / Giant-Killer Bonus* — result against a higher-ranked/higher-division opponent
  - **Optional Multi-Match Mini-Contract (Slot #5):** e.g., *"Win 3 of next 4 matches"* or *"Go 3 matches unbeaten"* for a larger Division 4 financial boost.

---

### 5. Stadium & Facilities — Realistic Pacing, Form-Based Tickets, Visible Medical & VIP Value
- **Codebase Findings (`src/game/stadiumFacilities.cpp`):**
  - **Build Duration:** `2, 3, 4, 5, 6, 6, 7, 7, 8` matches — far too fast (doubling a stadium in 2 weeks feels instant).
  - **Capacity Curve:** `5k → 12k → 20k → 30k → 45k → 52k → 60k → 68k → 76k → 85k` (`£150k` for the first +7,000 seats).
  - **Ticket Prices:** Static manual 3-tier toggle by division (`£3 / £5 / £7` in Div 4 up to `£7 / £10 / £15` in Div 1) with no form sensitivity.
  - **Medical Center:** Silently reduces newly acquired post-match injury durations by `10%–50%` and logs only to `swos.log` — completely invisible in-game.
  - **Commercial / VIP:** Silently adds `+10%..+50%` on top of home ticket revenue; with notable-only gate popups, its value and purpose are unclear to the player.
- **V2 Improvements:**
  - **Realistic Build Durations, Capacity Steps & Costs:**
    - First capacity expansion **8–10 matches**, scaling up to **20–30 matches** (half a season+) for large stands; facilities **6–15 matches**.
    - Smoother early capacity steps (e.g., `5k → 8k → 12.5k → 18k → 25k → 35k → 47k → 60k → 72k → 85k`) and rebalanced upgrade costs for meaningful season-to-season progression.
  - **Form-Driven Dynamic Ticket Pricing (Last 5 Matches):**
    - Track the club's **last 5 match results (`W/D/L`)** and show a clear **Form & Demand Meter** inside `[F6] STADIUM`.
    - Hot form (4–5 wins in last 5) automatically nudges effective ticket price up (`+£1` to `+£2` with stronger crowd tolerance); poor form (3+ losses) nudges prices down to preserve turnout (with `AUTO (FORM)` mode or automatic modifier on top of base policy).
  - **Visibly Impactful Medical Center:**
    - Add a **match injury-prevention roll** (`15%–55%` across L2–L5 to turn a match injury into a minor knock / 0 games missed) + **faster weekly recovery** for already-injured squad players.
    - Display a clear **post-match Medical Center notice** whenever an injury is prevented or shortened (e.g., *"MEDICAL LAB SAVED T. MOONEY: 4 WEEKS -> 2 WEEKS"*).
  - **Clear & Rewarding Commercial / VIP:**
    - Display a live **Estimated Matchday P&L Breakdown** inside `[F6] STADIUM` (`Tickets + VIP/Merch - Upkeep = Net / Home Match`).
    - Add a **guaranteed flat corporate box baseline per home match** (resilient against poor weather/form) **plus a Sponsor Challenge payout bonus (`+5%` per level, up to `+25%`)**, tying Stadium directly into Sponsor Challenges.

---

### 6. Global Achievements — Expand 50 → 100–120+
- **Codebase Findings (`src/game/achievements.cpp`):**
  - Currently **50 achievements** across 6 categories (`Basics: 8`, `Pitch Craft: 13`, `Comebacks: 7`, `Cup Glory: 6`, `Career: 8`, `Management: 8`).
  - Heavy split between very quick starter unlocks and ultra-late career trophies (Treble, World Cup, 10 Seasons, 85k Sellout), leaving a quiet stretch during a Division 4 → Division 1 climb.
- **V2 Improvements (Target: 100–120 Achievements):**
  - **Multi-Tier Progression Ladders:** Wins (`10 / 25 / 50 / 100 / 250`), Goals (`50 / 250 / 500 / 1,000`), Clean Sheets (`5 / 15 / 30`), Win Streaks (`3 / 5 / 10`), Unbeaten Runs (`5 / 10 / 20`).
  - **Single-Match Pitch Craft & Telemetry Specials:** 2+ Long-Range Goals in 1 Match, Win on Frozen/Muddy Pitch (*"Cold Rainy Night"*), Smash & Grab (win with `< 35%` possession), Brick Wall (GK 6+ saves & clean sheet), Defender Goal / Hat-Trick, Super-Sub Brace, Score in Both Halves, Comeback from 80'+ Down, Own-Goal Win.
  - **Division Climb & Career Economy Milestones:** Climb Div 4 → 3 → 2 → 1, Golden Boot (25+ / 40+ season goals), Complete `5 / 10 / 25 / 50` Sponsor Challenges, First All-In Clean Sweep, 3 Clean Sweeps, Earn `£1M / £5M` from Sponsors, 5 Consecutive Home Sellouts, First Medical Save.

---

### 7. Live Stream (Pundit & Personas) — Keep Strong & Enrich
- Already a core highlight during active play. Keep pacing and tone intact, and gently enrich context with:
  - Mid-match reactions to **active Sponsor Challenges** (e.g., *"One more goal and the sponsor bonus is locked in!"*)
  - Mentions of **last-5 form streaks** and **Stadium sellouts / ongoing construction**.

---

## Recommended Execution Order

1. **P0 — Dressing Room Off-Gate Bug Fix (`DONE`):** Stop `onCareerWeeklyTickLlm()` from showing Dressing Room popups when disabled.
2. **P1 — Sponsor Challenges V2 (`DONE`):** 5 simultaneous offers, 21 single-match challenge types (Star Striker ATT-only 2+ goals, Total Football >=60%, Volley removed), tiered Combo/Clean Sweep jackpots (`+25% / +50% / +75%`), and 5 persistent Multi-Match Contracts (M1–M5) in Slot 5.
3. **P1 — Stadium & Facilities V2 (`DONE`):** Realistic build durations (`10–28` matches stands, `8–18` facilities), smoothed capacity/cost curve (`5k–85k`), last-5-match dynamic form ticket pricing (`±£2` & demand boost), visible Medical knock prevention + rehab boosts + post-match notices, and clear VIP corporate box income + live home P&L + Sponsor Challenge `+8%..+30%` synergy.
4. **P2 — Global Achievements V2 (`DONE`):** Expanded from 50 → **100 achievements** (`3,240` PTS) with multi-tier progression ladders, win/unbeaten streaks, single-match telemetry craft specials, division climb milestones, sponsor/contract/medical/stadium milestones, and lossless V1/V2 → V3 save migration.
5. **P2 — Coach Charisma V2 (`DONE`):** Free-text alphanumeric interview answer box + `NO COMMENT` button, telemetry-powered journalist questions + strict LLM Judge, 10-level Charisma progression (`0–1000 CP`, 100 pts/level, Level 1 `UNKNOWN ROOKIE` → Level 10 `LIVING LEGEND`), Job Offer reputation gating (blocking AC Milan / mega-clubs / National Teams for low-level coaches), Live Stream quote callback, and Sponsor Challenge payout bonus (`+3%..+15%`).
6. **Keep & Polish — Live Stream Pundit & Personas Enrichment (`DONE`):** Preserved stream pacing/tone while adding live mid-match Sponsor Challenge stakes/proximity callouts (`ALMOST THERE`, `LOCKED IN`, `BROKEN`, Clean Sweep jackpot & Multi-Match Contract watch), pre-match live home attendance/sellout vs away detection, clear last-5 form streak analysis, and ongoing stadium construction cues for both Fan Personas and Live Pundit / Ömer Üründül commentary.
7. **P3 — Retro 1996/97 Color Tabloid Newspaper V2 (`DONE`):** Replaced the plain post-match text dialog with a full-page aged-newsprint **Daily Soccer Echo** back page featuring a live action match photograph captured cleanly before HUD/stream overlays with a 90s color tabloid print filter, screaming banner headline & sub-head, 2-column match report, pixel-art SWOS player sprite **Star Man (MOTM)** card in real club kit colors, boxed **Chairman's Verdict**, **Back-Page Briefing** (Sponsors, Stadium, Press quote), pixel kit matchup fallback for fast-sim View Result games, and **`[S] Save Edition PNG`** souvenir export to `screenshots/`.
8. **P3 — Remaining Optional Trim/Revamp:** Compact Scouting intel, actionable Transfer Alerts, and flashpoint Dressing Room dilemmas.
