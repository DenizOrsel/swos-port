# Visual & Physics Effects Roadmap (`swos-port`)

This document catalogs modern visual, physics, and atmospheric effects designed for **Sensible World of Soccer (`swos-port`)**. Every effect is designed to preserve original SWOS match mechanics and determinism while adding tactile physical feedback and retro-faithful visual polish behind user-configurable options.

---

## 1. Pitch Surface & Contact Particles (Pitch-Type Reactive) *(Implemented)*

Controlled via **OPTIONS -> GAMEPLAY -> PITCH & WEATHER FX** (`ON` by default, persisted in `swos.ini` under `[gameplay] pitchWeatherEffects`), `src/game/pitch/pitchEffects.cpp` + `src/game/pitch/pitch.cpp`:

- **Weather-Reactive Pitch Color Grading & Wear Zones**
  - Subtle `SDL_SetTextureColorMod` grass tinting based on `getPitchType()` (`Frozen` icy frost tint, `Muddy` earthy damp olive-brown, `Wet` darker rain-soaked green, `Soft` lush green, `Normal` classic SWOS green, `Dry`/`Hard` warm sun-dried grass), plus worn mud/frost patches in goalmouths and the centre circle.
- **Slide-Tackle & Diving Goalkeeper Sprays**
  - **Wet / Muddy pitches:** Water spray droplets and mud splashes kicking up behind sliding tackles and diving goalkeepers.
  - **Normal / Soft pitches:** Tiny grass blades and turf divots kicked up on slide tackles and dives.
  - **Frozen / Dry / Hard pitches:** Frost/ice crystal spray (`Frozen`) or chalk/dust puffs (`Dry` / `Hard`).
- **Persistent Slide-Tackle Skid Marks**
  - Darkened mud/turf/frost streak marks left on the grass where a slide tackle or keeper dive occurred, persisting across the match (and tracked in replays).

---

## 2. Live Weather & Stadium Lighting *(Implemented)*

Controlled via **OPTIONS -> GAMEPLAY -> PITCH & WEATHER FX** (`ON` by default):

- **Dynamic Rain & Snowfall Overlay**
  - Slanted falling rain streaks with animated ground puddle ripples during `Wet` or `Muddy` matches.
  - Drifting snowflakes and cold player breath puffs on `Frozen` pitches.
- **4-Corner Stadium Floodlight Shadows**
  - Soft, multi-directional translucent shadows under players and the ball (simulating 4 corner stadium floodlight towers) whose angles and lengths shift subtly based on pitch position.

---

## 3. Physics Corner Flags (Flexible Spring Pole + Cloth Pennant)

- Replace the fixed 4-frame looping corner flag sprites (`updateCornerFlags()` in `src/sprites/gameSprites.cpp`) with a **spring-hinged flexible pole and a mini Verlet cloth flag pennant**:
  - Flutters dynamically in the wind and casts a soft oblique ground shadow.
  - Bends over and whips back realistically whenever the ball strikes it or a player runs through the corner / takes a corner kick.

---

## 4. Ball Physics & Goalframe Feedback *(Implemented)*

Controlled via **OPTIONS -> GAMEPLAY -> BALL & FRAME EFFECTS** (`ON` by default, persisted in `swos.ini` under `[game] ballAndGoalframeEffects`):

- **Post & Crossbar Metallic Vibration into the Net**
  - When a shot strikes the left post, right post, or crossbar (`handlePostAndCrossbarCollisions()` in `src/game/ball/ball.cpp` and replay frame collision detection), the cylindrical post/crossbar visibly shudders with damped high-frequency harmonic oscillation and a brief specular impact flash.
  - The post/crossbar impact simultaneously transmits a physical shockwave ripple into the attached 3D Verlet goal net and its ground shadow.
- **Aftertouch Spin Vapor Trail & Wet-Grass Skid Wake**
  - High-velocity shots and passes with active curl/aftertouch (`applyBallAfterTouch()`) render a smooth, tapered translucent motion-blur ribbon following the ball's curved 3D trajectory.
  - Fast ground passes and skidding balls on `Wet`, `Muddy`, or `Frozen` pitches leave a subtle surface skid/spray wake along the grass.

---

## 5. Match & Goal Atmosphere

- **Photographer Flashbulbs & Confetti on Goals**
  - When a goal is scored (especially in cup finals and derbies), brief white camera flashbulb pops appear behind the perimeter ad boards/stands alongside bursts of team-colored confetti and paper streamers near the touchlines.
- **Reactive Perimeter & Dugout Activity**
  - Subtle reactive animations for touchline photographers, stewards, and bench personnel when a goal or near-miss occurs nearby.
