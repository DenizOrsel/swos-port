# Single-Player Career Immersion Roadmap (`swos-port`)

This document catalogs single-player Career Mode immersion features designed to deepen the football management and matchday narrative while preserving Sensible World of Soccer's fast, arcade-friendly flow.

---

## 1. Dynamic Matchday & Season Storytelling

- **Pre-Match Context Banner ("Why This Match Matters")**
  - On the Versus / Stadium screen before kickoff, display a concise 1-line context banner generated from the live league table or cup round (e.g., *"1st vs 2nd — Title Six-Pointer"*, *"Relegation Battle"*, *"Local Derby"*, or *"Unbeaten in 6 Matches"*).
- **Post-Match Newspaper / Headlines Card**
  - A quick, skippable 1-screen retro sports-page summary after notable results (hat-tricks, giant-killing cup upsets, 5+ goal thrillers, title clinchers, or manager milestone wins such as *"50th Win in Charge"*).
- **End-of-Season Awards & Club Hall of Fame**
  - Track **Top Scorer (Golden Boot)**, **Most Appearances**, **Biggest Win**, **Record Transfer Fee**, and the **Trophy Cabinet** across an entire multi-season career (persisted alongside `.CAR` / `.hot` saves).

---

## 2. Living Dressing Room & Player Personalities

- **Player Form Arrows & Morale Streaks**
  - Track recent match ratings, goals, and clean sheets over the last 3–5 matches and display subtle **Up / Down form arrows** next to squad players on the Tactics/Squad screen, granting a small, authentic performance boost or dip.
- **Squad Milestones & Testimonial Notices**
  - Brief chairman/club news popups when a player reaches **50 / 100 goals**, **100 appearances**, wins **Player of the Month**, or announces retirement at the end of a long career.
- **Youth Academy Call-Ups**
  - Once or twice per season, offer a low-cost 16–18 year old academy prospect from the club's region (with randomized skills and potential) who can be promoted to cover injury or suspension gaps.

---

## 3. Transfer Market Drama & Scouting

- **Deadline Day Buzz & Bidding Wars**
  - On the final transfer window week, show a *"TRANSFER DEADLINE DAY"* ticker with late rival bids or bargain panic-loan offers for transfer-listed players.
- **Scout Reports on Upcoming Opponents**
  - Before a match (especially Cup ties or European away legs), provide a 1-click **"SCOUT OPPONENT"** view showing their danger man (top scorer / highest-value player), usual formation (`4-4-2`, `3-5-2`, etc.), and recent form (`W-W-D-L-W`).
- **Short-Term Loans**
  - Ability to loan in a player for half a season or the remainder of the season when the squad is hit by injuries or red-card suspensions.

---

## 4. Matchday Atmosphere & Manager Presence

- **Home Crowd & Stadium Attendance Meter**
  - Dynamically scale crowd noise and chant intensity (`src/audio/chants.cpp`) based on match stakes (Cup Final, European knockout, top-of-the-table clash vs. mid-table fixture) and home form.
- **Manager Touchline / Reputation Meter**
  - Track **Board Confidence** and **Manager Reputation** across clubs—starting in lower divisions and receiving unsolicited mid-season job offers from bigger clubs or national teams when overachieving (with a clear accept/decline choice).
- **Interactive Injury / Tactical Prompts**
  - When a key player picks up a knock in-game, show a quick touchline alert (*"JONES is carrying a knock — Sub now?"*) for active bench management.

---

## 5. Career Stats & Timeline Polish

- **Season-by-Season History Log**
  - A career ledger screen recording every managed season: **Club, League Position, W-D-L, Goals For/Against, Top Scorer, and Cups Won**, turning a multi-season save into a persistent football journey.
