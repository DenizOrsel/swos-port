# Feature Specification & Agentic Blueprint: SWOS Telemetry Analytics Dashboard (Next.js + Tailwind + shadcn/ui + TypeScript)

## 1. System Overview & Vision

The **SWOS Telemetry Analytics Dashboard** is a standalone **Next.js (App Router) + TypeScript + Tailwind CSS + shadcn/ui** web application that consumes the native **Telemetry V2 JSON** files exported by `swos-port` (`dist/swos-port/telemetry/match-YYYYMMDD-HHMMSS.json`).

Inspired by modern broadcast and scouting suites (**Opta**, **Sofascore**, **FBref**) blended with crisp 1996/97 _Sensible World of Soccer_ pitch aesthetics, the dashboard turns every played SWOS match — and entire Career Mode seasons — into interactive visual analytics:

- **Interactive Shot Map & Expected Goals (xG)** with shot geometry, body part, situation, and cumulative xG race charts.
- **Pitch Heatmaps & Hotspots** for the ball, each team, and every individual player (rendered cleanly over an exact-proportion SWOS SVG pitch).
- **Average Positions & Passing Networks** showing team shape and who combines most frequently with whom.
- **5-Minute Attack Momentum Graph** highlighting dominance swings across 90 minutes (and extra time).
- **Sofascore-Style Individual Player Ratings (`3.0 – 10.0`)**, Man of the Match (MOTM) spotlight, and sortable squad tables.
- **Multi-Match Career & Season Aggregator** tracking form, xG over/under-performance, top scorers/assisters, pitch-weather win rates, and Stadium/Charisma progression across all exported JSON files.

---

## 2. Tech Stack & Architecture

| Layer              | Choice                                            | Purpose                                                                                             |
| ------------------ | ------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| **Framework**      | Next.js 16+ (App Router) Zustand state manager    | Server API routes for reading local `telemetry/` JSON files + fast interactive client views         |
| **Language**       | TypeScript (`strict: true`)                       | End-to-end type safety against Telemetry V2 schema                                                  |
| **Styling & UI**   | Tailwind CSS + shadcn/ui + `lucide-react`         | Dark tactical-room theme, cards, tabs, badges, tooltips, sheets, sortable tables                    |
| **Validation**     | `zod`                                             | Safe runtime validation of V2 JSON files (plus graceful V1 fallback)                                |
| **Charts**         | `recharts`                                        | Cumulative xG step chart, 5-minute diverging momentum bars, player radar charts, career trend lines |
| **Pitch Graphics** | Custom SVG Pitch + HTML5 `<canvas>` Heatmap Layer | Crisp resolution-independent SWOS pitch lines with smooth Gaussian/bilinear hotspot rendering       |

### Data Ingestion Modes (Dual Support)

1. **Automatic Local Folder Sync (Primary for Local Play):**
   - Configure `SWOS_TELEMETRY_DIR` in `.env.local` (e.g., `C:\Users\deniz\Desktop\swos-port\dist\swos-port\telemetry`).
   - Server route `GET /api/matches` scans `*.json`, returns lightweight match summaries sorted newest-first.
   - Server route `GET /api/matches/[matchId]` returns the full validated Telemetry V2 JSON payload.
   - Include a **Refresh / Live Auto-Poll (15s)** toggle in the top bar so newly finished SWOS matches appear automatically right after full-time.
2. **Drag-and-Drop / Multi-File JSON Upload (Portable Mode):**
   - Users can drop one or multiple `match-*.json` files directly into the browser (stored in client memory / IndexedDB) so the dashboard also works when deployed or shared without local disk access.

---

## 3. Exact Telemetry V2 JSON Contract (`src/types/telemetry.ts`)

This matches the exact JSON output produced by `exportMatchTelemetryJsonToPath()` in `src/telemetry/matchTelemetry.cpp`.

### Coordinate System Guarantee (Halftime-Swap Proof)

- `swos-port` automatically normalizes **every shot, heatmap cell, and player average position** into canonical attacking space across both halves:
  - `normX ∈ [0.0 .. 1.0]`: left touchline (`0.0`) → right touchline (`1.0`).
  - `normY ∈ [0.0 .. 1.0]`: own goal line (`0.0`) → opponent goal line (`1.0`).
  - **Heatmaps** are `16 rows × 12 cols` (`heatmap[row][col]`): `row 0` is own goal line, `row 15` is opponent goal line; `col 0` is left flank, `col 11` is right flank.
  - For `heatmaps.ball`, orientation is fixed to **Team 1 attacking toward `row 15`** (so Team 1's box is always `row 0..2`, Team 2's box is `row 13..15`).
  - On a **horizontal pitch SVG** (left-to-right attack), map:
    - Team 1 attacking left → right: `svgX = normY * pitchW`, `svgY = normX * pitchH`.
    - Team 2 attacking right → left (when overlaid on the same pitch): `svgX = (1 - normY) * pitchW`, `svgY = (1 - normX) * pitchH`.

### TypeScript Interfaces

```ts
export interface PitchDimensions {
  leftX: number; // 81
  rightX: number; // 590
  topY: number; // 129
  bottomY: number; // 769
  goalLeftX: number; // 303
  goalRightX: number; // 367
  heatmapCols: number; // 12
  heatmapRows: number; // 16
}

export interface TeamMatchStats {
  name: string;
  coach: string;
  isHuman: boolean;
  goals: number;
  htGoals: number;
  penGoals: number;
  xg: number;
  shots: number;
  shotsOnTarget: number;
  possessionPct: number;
  corners: number;
  fouls: number;
  yellowCards: number;
  redCards: number;
  passesCompleted: number;
  tacklesWon: number;
  longRangeGoals: number;
  headerGoals: number;
  attackZonePct: number;
}

export interface StadiumSnapshot {
  name: string;
  capacityLevel: number; // 1..10
  maxCapacity: number; // 5,000..85,000
  pitchLevel: number; // 1..5
  pitchQuality: string; // "MUDDY TURF" .. "PRISTINE TURF"
  trainingLevel: number; // 1..5
  medicalLevel: number; // 1..5
  commercialLevel: number; // 1..5
  ticketPolicy: "CHEAP" | "STANDARD" | "EXPENSIVE" | string;
  ticketPrice: number;
  coachCharisma: number; // 0..100
}

export interface MotmSummary {
  teamNum: 1 | 2;
  team: string;
  player: string;
  shirtNumber: number;
  pos: string;
  rating: number;
  goals: number;
  assists: number;
  xg: number;
}

export type HeatmapGrid = number[][]; // [16 rows][12 cols]

export interface ShotEvent {
  minute: number;
  half: number;
  teamNum: 1 | 2;
  team: string;
  player: string;
  shirtNumber: number;
  assistedBy: string;
  normX: number; // 0..1 pitch width
  normY: number; // 0..1 own goal -> opponent goal
  rawX: number;
  rawY: number;
  rawZ: number;
  distYards: number;
  angleDeg: number;
  bodyPart: "foot" | "header" | "volley";
  situation: "open_play" | "penalty" | "free_kick" | "corner";
  outcome: "goal" | "saved" | "post" | "wide" | "own_goal";
  xg: number;
}

export interface TimelineBucket {
  fromMin: number;
  toMin: number;
  poss1Pct: number;
  shots1: number;
  shots2: number;
  xg1: number;
  xg2: number;
  goals1: number;
  goals2: number;
  finalThird1: number;
  finalThird2: number;
  momentum: number; // -100 (team2 dominant) .. +100 (team1 dominant)
}

export interface PassLink {
  from: string;
  fromShirt: number;
  to: string;
  toShirt: number;
  count: number;
}

export interface PlayerMatchRow {
  teamNum: 1 | 2;
  team: string;
  name: string;
  shirtNumber: number;
  pos: string; // "GK" | "RB" | "LB" | "DEF" | "RW" | "LW" | "MID" | "ATT" | "SUB"
  isStarter: boolean;
  goals: number;
  assists: number;
  shots: number;
  shotsOnTarget: number;
  xg: number;
  touches: number;
  passesCompleted: number;
  turnovers: number;
  tacklesWon: number;
  yellowCards: number;
  redCards: number;
  gkSaves: number;
  avgX: number; // 0..1
  avgY: number; // 0..1 (toward opponent goal)
  rating: number; // 3.0..10.0
  heatmap: HeatmapGrid;
}

export interface MatchEventItem {
  minute: number;
  type: string;
  team: string;
  player: string;
  detail: string;
}

export interface SwosMatchTelemetryV2 {
  schemaVersion: 2;
  matchId: string;
  timestamp: string;
  competition: string;
  pitch: string;
  minute: number;
  half: number;
  playingPenalties: boolean;
  pitchDimensions: PitchDimensions;

  // Backward-compatible V1 fields
  team1: string;
  team2: string;
  goals1: number;
  goals2: number;
  shots1: number;
  shots2: number;
  possession1: number;
  possession2: number;
  territory: {
    team1AtkPct: number;
    midPct: number;
    team2AtkPct: number;
  };

  teams: {
    team1: TeamMatchStats;
    team2: TeamMatchStats;
  };
  stadium: StadiumSnapshot | null;
  motm: MotmSummary | null;
  heatmaps: {
    ball: HeatmapGrid;
    team1: HeatmapGrid;
    team2: HeatmapGrid;
  };
  shots: ShotEvent[];
  timeline: TimelineBucket[];
  passNetworks: {
    team1: PassLink[];
    team2: PassLink[];
  };
  players: PlayerMatchRow[];
  events: MatchEventItem[];
}
```

---

## 4. Dashboard Pages & Analytics Modules

### 4.1 Match Center (`/match/[matchId]` + Home View)

1. **Scoreboard & Context Hero Card:**
   - Team names (`HUMAN` badge on user-managed side), full-time score, half-time score (`HT 1-0`), penalty shootout score if applicable, Competition badge, Pitch condition badge (`Dry`, `Wet`, `Muddy`, `Frozen`), and Stadium + Coach Charisma pill (in Career mode).
   - **Man of the Match (MOTM) Banner:** Gold star card highlighting `motm.player`, shirt #, rating badge (`8.9`), goals, assists, and xG.
   - **Match Verdict Pill (Auto-Computed):** e.g. _"Clinical Finishing (+1.42 G-xG)"_, _"Dominated Territory (64%)"_, or _"Smash & Grab Win"_.

2. **Interactive Pitch Canvas (`PitchVisualizer` Tabs):**
   - **Tab 1 — Shot Map & xG:**
     - Full horizontal SVG football pitch with 18-yard boxes, 6-yard boxes, penalty spots, D-arcs, and goals.
     - Team 1 shots plotted attacking right; Team 2 shots plotted attacking left.
     - Marker size proportional to `xg` (min radius 5px, max 18px).
     - Color & shape by `outcome`: **Goal** (filled emerald/gold ring), **Saved** (cyan), **Post/Bar** (amber triangle/ring), **Wide** (translucent slate/red outline).
     - Interactive filter pills: Team (`All` / `Team 1` / `Team 2`), Outcome (`Goals Only` / `On Target` / `All`), Situation (`Open Play`, `Set Pieces`, `Penalties`).
     - Rich hover tooltip: `42' S. YURAN — Goal (0.34 xG) • 11.4 yds • 29° angle • Header • Assisted by B. OKOCHA`.
   - **Tab 2 — Pitch Heatmaps & Hotspots:**
     - Smooth HTML5 `<canvas>` heatmap rendered over the SVG pitch using a thermal football palette (transparent → emerald → amber → crimson hotspot core) with bilinear interpolation + Gaussian blur on the `16×12` grid.
     - Selector buttons: **Ball Hotspots**, **Team 1 Presence**, **Team 2 Presence**, or **Any Individual Player** (dropdown or click player from XI table!).
     - Optional **Zone Overlay** showing exact % of activity in Defensive Third, Midfield Third, and Final Third + Left / Central / Right channels!
   - **Tab 3 — Passing Network & Average Positions:**
     - Team switcher (`Team 1` / `Team 2`).
     - Plots each participating player at `(avgX, avgY)` with node radius scaled by `touches`, shirt number inside, name below, and color-coded rating halo.
     - Draws weighted pass edges from `passNetworks` with opacity & stroke width scaled by `count` + slider for _"Min passes (1..5)"_.

3. **Match Flow & Dominance Charts (`Recharts`):**
   - **Attack Momentum Chart:** Diverging bar chart across `timeline[]` (`0-5'` .. `85-90'+`). Positive bars in Team 1 color (above 0), negative bars in Team 2 color (below 0), with football icons pinned on goal buckets.
   - **Cumulative xG Race Chart:** Step-line chart comparing Team 1 vs Team 2 cumulative xG from `0'` to `90'+`, with annotated dots on every scored goal.
   - **Head-to-Head Stat Bars:** Mirrored horizontal comparison bars for xG, Shots, Shots on Target, Shot Accuracy %, Possession %, Passes Completed, Tackles Won, Corners, Fouls, Yellow/Red Cards, Long-Range Goals, Header Goals.

4. **Individual Player Analytics & Spotlight Drawer:**
   - **Sortable Squad Table (Team 1 / Team 2 tabs):** `shadcn/ui Table` with columns: `#`, `Player`, `Pos`, `Rating` (color-coded pill: `>=8.0` cyan/emerald, `7.0-7.9` green, `6.0-6.9` amber, `<6.0` red), `G`, `A`, `Sh (SOT)`, `xG`, `G - xG`, `Pass`, `Touch`, `Tkl`, `TO`, `Saves`, `Cards`.
   - **Player Spotlight Sheet (on Row Click):**
     - Personal `16×12` pitch hotspot map + that player's shots highlighted on the pitch.
     - Top pass partners (outgoing & incoming from `passNetworks`).
     - 6-Axis **Player Performance Radar** (`Recharts RadarChart`): Finishing, Threat (xG), Passing Volume, Ball Retention, Defending/Tackles, Overall Rating.

5. **Chronological Match Event Feed:**
   - Filterable event log (`All`, `Goals`, `Saves`, `Woodwork`, `Cards`) showing minute, team badge, player, and rich SWOS mechanics detail.

---

### 4.2 Career & Season Analytics Hub (`/career`)

Aggregates **every** exported JSON file in `telemetry/` (filterable by Club Name, Competition, or Date Range):

- **Season KPI Cards:** Matches Played, W-D-L Record & Win %, Goals Scored vs Conceded, Total xG For vs xG Against, **Finishing Overperformance (`Goals - xG`)**, Average Possession.
- **Rolling Form & xG Trend Line Chart:** Match-by-match Goals vs xG and Opponent Goals vs xGA across the season.
- **Squad Leaderboards:** Top Scorers, Top Assisters, Highest xG, Best Average Match Rating (min 2 matches), Most Passes, Most Tackles, Goalkeeper Save %.
- **Tactical & Environmental Splits:**
  - **Home vs Away** win rate, goals/game, and xG/game.
  - **Pitch Weather Performance:** Win rate & avg goals on `Dry` / `Normal` / `Soft` / `Wet` / `Muddy` / `Frozen` pitches.
  - **Shot Profile Breakdown:** % of goals from Open Play vs Set Pieces, Foot vs Header vs Volley, Inside Box vs Long Range.
  - **Stadium & Charisma Timeline:** Tracks Stadium capacity/facility upgrades and Coach Charisma across Career matches.

---

## 5. Recommended Next.js Directory Structure

```text
swos-analytics/
├── .env.local                       # SWOS_TELEMETRY_DIR=C:\Users\deniz\Desktop\swos-port\dist\swos-port\telemetry
├── src/
│   ├── app/
│   │   ├── layout.tsx               # Dark tactical shell + sidebar match switcher
│   │   ├── page.tsx                 # Latest match dashboard (or dropzone if empty)
│   │   ├── match/[matchId]/page.tsx # Specific match deep-dive view
│   │   ├── career/page.tsx          # Multi-match Career & Season aggregator
│   │   └── api/
│   │       └── matches/
│   │           ├── route.ts         # Lists all match-*.json metadata
│   │           └── [matchId]/route.ts # Loads & validates single match JSON
│   ├── types/
│   │   └── telemetry.ts             # Exact V2 TypeScript interfaces + Zod schemas
│   ├── lib/
│   │   ├── telemetryStore.ts        # Local FS reader + V1-to-V2 adapter
│   │   ├── pitchGeometry.ts         # SWOS normX/normY -> SVG & Canvas transforms
│   │   └── analyticsMath.ts         # Channel splits, cumulative xG, career aggregations
│   └── components/
│       ├── ui/                      # shadcn/ui primitives (card, tabs, table, badge, sheet, tooltip, slider)
│       ├── MatchSidebar.tsx         # Match list + drag-and-drop JSON uploader + auto-refresh
│       ├── ScoreboardHero.tsx       # Scoreline, MOTM badge, stadium/pitch pills
│       ├── PitchSvg.tsx             # Reusable SWOS retro-pro horizontal/vertical SVG pitch
│       ├── ShotMapPitch.tsx         # Interactive xG shot map + filters + hover tooltip
│       ├── HeatmapPitch.tsx         # Smooth Canvas hotspot overlay over PitchSvg
│       ├── PassNetworkPitch.tsx     # Avg positions + weighted passing edges
│       ├── MomentumChart.tsx        # 5-min diverging attack momentum bar chart
│       ├── XgRaceChart.tsx          # Cumulative xG step chart with goal pins
│       ├── TeamComparisonBars.tsx   # Mirrored Opta-style stat bars
│       ├── PlayerRatingsTable.tsx   # Sortable individual stats & ratings table
│       └── PlayerSpotlightSheet.tsx # Slide-over player heatmap + radar + pass partners
```

---

## 6. Agentic Implementation Plan & Phased Prompts

Feed these 4 prompts sequentially to build the Next.js app cleanly from scratch.

### Phase 1 Prompt: Project Scaffold, Schema Validation & Data Ingestion

```text
### ROLE & TASK
Scaffold a new Next.js (App Router) + TypeScript (strict) + Tailwind CSS + shadcn/ui + Recharts project named `swos-analytics` to consume SWOS Telemetry V2 JSON files.

### REQUIREMENTS
1. Initialize Next.js with App Router, TypeScript, Tailwind CSS, `lucide-react`, `recharts`, `zod`, and shadcn/ui components (`card`, `tabs`, `table`, `badge`, `button`, `sheet`, `tooltip`, `select`, `slider`, `scroll-area`, `separator`).
2. Create `src/types/telemetry.ts` containing the exact `SwosMatchTelemetryV2` TypeScript interfaces and matching `zod` schemas from Section 3 of `swos_telemetry_analytics_dashboard_nextjs_spec.md` (with safe fallback normalization if an older V1 JSON is loaded).
3. Implement local filesystem API routes:
   - `GET /api/matches`: reads `process.env.SWOS_TELEMETRY_DIR` (defaulting to `../dist/swos-port/telemetry`), returns sorted match summaries (newest first).
   - `GET /api/matches/[matchId]`: validates path traversal safety, reads and returns the full JSON payload.
4. Build a sleek dark tactical-room layout (`bg-zinc-950`, emerald/gold SWOS accents) with a collapsible `MatchSidebar` showing all matches (score, teams, date, competition badge), a Refresh/Auto-Poll toggle, and a client-side Drag & Drop JSON uploader so users can also drop `match-*.json` files directly.
```

### Phase 2 Prompt: Interactive SWOS Pitch Visualizations (Shot Map, Heatmaps & Pass Network)

```text
### ROLE & TASK
Implement the three interactive pitch visualization modules on top of a shared, exact-proportion SVG pitch component (`PitchSvg.tsx`).

### REQUIREMENTS
1. `PitchSvg.tsx`: Clean horizontal dark-emerald striped football pitch with crisp chalk lines, 18-yard boxes, 6-yard boxes, penalty spots, D-arcs, center circle, and goals.
2. `ShotMapPitch.tsx`:
   - Plot `shots[]` using `normX` / `normY` (Team 1 attacking right, Team 2 attacking left).
   - Scale circle radius by `xg`; style by `outcome` (`goal`, `saved`, `post`, `wide`).
   - Add filter pills (Team, Outcome, Situation) and rich hover tooltips showing minute, player, assister, xG, distance (yds), angle (°), and body part.
3. `HeatmapPitch.tsx`:
   - Render a smooth thermal `<canvas>` hotspot layer over `PitchSvg` from any `16×12` matrix (`heatmaps.ball`, `heatmaps.team1`, `heatmaps.team2`, or any individual player's `heatmap`).
   - Apply smooth upscaling + radial Gaussian blur and a football hotspot color ramp (transparent -> green -> yellow -> orange -> red).
   - Include a selector for Ball / Team 1 / Team 2 / Individual Player, plus an optional 3x3 zone percentage overlay (Def/Mid/Final third & Left/Center/Right channels).
4. `PassNetworkPitch.tsx`:
   - Team 1 / Team 2 toggle, minimum-pass-count slider.
   - Draw directed edges from `passNetworks` weighted by `count` and player nodes at `(avgX, avgY)` sized by `touches` with shirt number, name, and color-coded rating ring.
```

### Phase 3 Prompt: Match Flow Charts, Individual Player Ratings & Spotlight Drawer

```text
### ROLE & TASK
Build the match flow charts, team comparison bars, sortable individual player ratings table, and interactive Player Spotlight drawer.

### REQUIREMENTS
1. `ScoreboardHero.tsx`: Scoreline, HT/Penalty score, competition & pitch condition badges, Stadium & Coach Charisma info strip, Man of the Match gold card, and auto-computed match verdict pill.
2. `MomentumChart.tsx`: Diverging 5-minute bar chart (`timeline[]`, `momentum: -100..+100`) in team colors with goal markers pinned to goal buckets.
3. `XgRaceChart.tsx`: Cumulative xG step-line chart (`Recharts`) comparing Team 1 vs Team 2 across minutes `0..90+` with annotated goal dots.
4. `TeamComparisonBars.tsx`: Mirrored horizontal bars for xG, Shots, SOT, Possession %, Passes, Tackles, Corners, Fouls, Cards, and Territory thirds.
5. `PlayerRatingsTable.tsx` + `PlayerSpotlightSheet.tsx`:
   - Sortable shadcn/ui table with color-coded `3.0–10.0` rating badges and all individual stats (`G`, `A`, `Shots`, `SOT`, `xG`, `G-xG`, `Touches`, `Passes`, `Tackles`, `Turnovers`, `Saves`, `Cards`).
   - Clicking any player opens a slide-over `Sheet` displaying their personal pitch hotspot map, their filtered shots, top pass partners, and a 6-axis `Recharts` RadarChart.
6. Filterable `MatchEventFeed.tsx` (`All`, `Goals`, `Saves`, `Woodwork`, `Cards`).
```

### Phase 4 Prompt: Multi-Match Career & Season Analytics Page (`/career`)

```text
### ROLE & TASK
Create the `/career` page aggregating all exported SWOS Telemetry V2 matches into a full-season tactical & player scouting hub.

### REQUIREMENTS
1. Aggregate all matches from `/api/matches` (plus any client-uploaded matches) with filters for Club Name, Competition, and Home/Away.
2. Top KPI strip: Matches, W-D-L record, Win %, Goals For/Against, xG For/Against, Finishing Overperformance (`G - xG`), and Average Possession.
3. Season Trend Charts (`Recharts`): Match-by-match Goals vs xG trend line and xGA defensive solidity trend.
4. Squad Leaderboards: Tabs for Top Scorers, Top Assisters, Highest Total xG, Best Average Rating, Most Passes, Most Tackles, and Goalkeeper Save %.
5. Environmental & Tactical Splits: Home vs Away comparison card, Pitch Condition win-rate bar chart (`Dry`, `Normal`, `Soft`, `Wet`, `Muddy`, `Frozen`), Shot Profile breakdown (Open Play vs Set Piece, Foot vs Header vs Volley), and Stadium/Charisma progression timeline.
```
