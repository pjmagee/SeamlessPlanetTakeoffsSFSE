# Seamless planet landing — RE map and design

Status: **design / RE findings only. No implementation yet.**

Target binary for every address below: `Starfield.exe 1.16.236`. Offsets are
`decompile address − 0x140000000`. `ID_<n>` is the Address Library ID — the same ID space as
`REL::ID(...)` in `src/plugin.cpp`, so these are directly usable.

RE was done against the Ghidra project in the sibling repo
`starfield-complete-planet-survey-mod` (`ghidra-project/Starfield.gpr`, headless via
`re/ghidra/scripts/`).

## 1. The landing path mirrors the takeoff path

Starfield's landing code sits in the same `1198xx` cluster as the takeoff functions this mod
already hooks, immediately below them in the address space. The pairing was established by
asking which functions call the takeoff/landing mode accessors (`XrefsToId`).

| role | takeoff — already hooked | landing — counterpart |
|---|---|---|
| mode-global accessor | `ID_119788` `SpaceshipTakeoffMode` @ `1421089d0` | `ID_119787` `SpaceshipLandingMode` @ `142108940` |
| refs mode, pre-sequence | `ID_119907` @ `14211c8f0` | `ID_119892` @ `14211a8a0` |
| initiate sequence | `ID_119908` @ `14211cbf0` (`src/plugin.cpp:678`) | within `119892`–`119896` |
| fires the event / completes | `ID_119911` @ `14211cfd0` (`src/plugin.cpp:661`) | `ID_119896` @ `14211b650` |
| camera path form | `SpaceshipTakeoffCameraPath_DO` `ID_5522` | `SpaceshipLandingCameraPath_DO` `ID_5523` |
| finished string | `TakeOffFinished` `ID_3139` | `LandingFinished` `ID_3140` |

**`ID_119896` is the primary hook target.** It calls `ID_120551`, which CommonLibSF declares as
`RE::Spaceship::LandingEvent::GetEventSource` — so this is the function that fires the landing
event. It also calls the mode accessor `ID_119787`. Signature:
`void ID_119896(longlong param_1, char param_2)`.

Landing has an authored camera path (`SpaceshipLandingCameraPath_DO`), directly analogous to the
takeoff camera path this mod suppresses via `DisableTakeOffCam` (`src/config.h:20`). There is a
real landing scene to hook rather than one to author from scratch.

## 2. Other landing anchors

- `ID_67028` @ `140c7c840` — player-ship landing entry. Takes body + lon/lat; carries the strings
  "Failed to find the valid body %s for landing." and "Landing player spaceship currently
  requires player to be seated on the pilot seat."
- `ID_66541` @ `140c53a60` — initiates a landing request from landing markers or a procedural
  search ("SetAllowFlying failed to initiate a landing request because no landing markers were
  found and a procedural search was not specified.").
- `ID_5112` @ `1400812d0` — registers the ExtraData types `PendingLandingEvent` and
  `PendingDynamicNavmesh`. The latter confirms navmesh generation is deferred on landing.
- `MovementMessageBeginLandingApproach` — the landing-approach movement message, counterpart to
  `MovementMessageTakeOff`.

## 3. CommonLibSF gaps to watch

- `RE::Spaceship::LandingEvent` exists (`include/RE/E/Events.h`, `GetEventSource` = `REL::ID
  120551`) but **its struct body is empty**. `TakeOffEvent` by contrast declares
  `NiPointer<TESObjectREFR> ship` + `uint32_t state`, which `TakeOffEventSink`
  (`src/plugin.cpp:321`) relies on. Assume the same layout, but **verify before dereferencing** —
  a wrong guess here is a crash in the player's game.
- `StarMapMenu_SelectedLandingSite::GetEventSource` is `REL::ID 0` in CommonLibSF (source comment
  says `142197`). Unusable as written; do not call it. This matters because site-selection is
  otherwise the ideal early trigger — see §4.

## 4. The hard part: landing is not takeoff reversed

Takeoff's destination is the galaxy cell `0x18343` — essentially free to load. That is why hiding
the swap behind 0.05 s of frozen frames works (`src/plugin.cpp:522`).

Landing's destination is procedurally generated: terrain, POI layout, navmesh
(`PendingDynamicNavmesh`), LOD, actor spawns. That generation is what the vanilla loading screen
is paying for, and it is seconds, not milliseconds. `toggleFrameDraw(false)`
(`src/plugin.cpp:276`) held that long is a frozen screen — worse than the loading screen it
replaces.

The generation is driven by the `PlanetContent` "virtual landing block" system, which exposes
tunable INI settings — a promising lever for shaping the request pattern during descent:

```
uPlanetContentVirtualLandingBlockMaxRequests:PlanetContent
uPlanetContentVirtualLandingBlockMinRequests:PlanetContent
uPlanetContentVirtualLandingBlockMinSize:PlanetContent
uPlanetContentVirtualLandingBlockMaxSize:PlanetContent
bPlanetContentVirtualLandingBlockReduceBlockRequests:PlanetContent
uLandingKeepBufferSize:Landing
```

So the descent **cannot** be a fixed timer like `TakeoffExtensionLength` (`src/config.h:15`). It
has to start generation as early as possible, run the descent over the top of it, and swap when
generation signals complete — with a floor so fast machines don't get a jarring 1 s descent, and
a ceiling that falls back to a brief fade if generation overruns.

## 5. Directly reusable from the takeoff implementation

- `toggleFrameDraw` (`src/plugin.cpp:276`) and the `unloadCurrentLocation` load-screen suppression
  (`src/plugin.cpp:669`) — generic TES-level, already direction-agnostic.
- The cell-swap dance: `removeObjectFromCell` → `SetParentCell` → `attatchObjectToCell`
  (`src/plugin.cpp:75-108`).
- `hook_unkFunc` atmosphere capture (`src/plugin.cpp:582`) and `hook_unkFunc2` imagespace/blur
  interception (`src/plugin.cpp:589`).
- The lat/lon → world-basis math in `hook_setSpacePlanetOrbit` (`src/plugin.cpp:394-431`), run in
  reverse to descend rather than ascend.
- `fadeWeather` / `fadeAtmospherics` / `fadeImageSpaceSettings` / `fadeStarGlow` need only
  reverse-direction variants.

## 6. The mirrored pipeline

Vanilla landing order, from in-game observation: **pick landing site → loading screen → landing
camera scene**. The scene therefore plays *after* the cell load, on the surface. It cannot hide
generation, so the orbit→atmosphere descent leg must be authored in space and the vanilla scene
used only for touchdown.

That makes the feature a direct reverse of the existing takeoff pipeline:

| takeoff (implemented) | landing (mirrored) |
|---|---|
| `TakeOffEvent` → capture cloud form, atmosphere form, star visibility | `LandingEvent` (`ID_120551`) → capture space-side state |
| 9 s vanilla anim (`prefadeTimer`), then fade atmosphere/clouds/imagespace **out** | descend from orbit altitude, fade atmosphere/clouds/imagespace **in** |
| `fadeBlur(t, true)` over final 0.5 s | `fadeBlur(t, true)` over final 0.5 s |
| `toggleFrameDraw(false)` → `manualLoadSystem` → galaxy cell `0x18343` | `toggleFrameDraw(false)` → load surface cell |
| `toggleFrameDraw(true)`, `sky->mode = 1`, `fadeStarGlow` in | `toggleFrameDraw(true)`, `sky->mode = 0`, hand into landing scene |

Two pieces of maths already exist and only need their endpoints swapped:

- **Descent path** — `hook_setSpacePlanetOrbit` (`src/plugin.cpp:417-423`) places the ship at
  `planetComponent + worldUp * (radius + altitude)` with `altitude = 2500000` (`/500` for bodies
  under 5000 km radius). Descent is the same expression with `altitude` interpolated toward zero.
- **Atmosphere entry** — `fadeAtmospherics` (`src/plugin.cpp:207`) already lerps
  `atmosphereTopRadius` between `surfaceRadius + 100` and `originalAtmosphereTopRadius`, and
  `staticVisibility` between original and 1.0. Landing runs the same lerp with the endpoints
  reversed. This is the "fog / atmosphere on entry" effect — it is already written.

`fadeWeather` and `fadeImageSpaceSettings` reverse the same way; `fadeStarGlow` runs down instead
of up.

### The one place the mirror does not hold

Takeoff's destination (galaxy cell `0x18343`) is nearly free to load, which is why a 0.05 s
frame-draw freeze covers it (`src/plugin.cpp:522`). The surface is procedurally generated, and
that cost is exactly what the vanilla loading screen spends.

So the descent length cannot be authored like `TakeoffExtensionLength`. The surface request has to
be issued at site-selection — the same moment vanilla issues it — and the descent must run at
least as long as generation takes. Done right, the freeze at the swap is as short as takeoff's.
Done wrong, the player gets a frozen screen instead of a loading screen, which is worse than
vanilla. This is the single parameter that must be load-driven rather than tuned.

## 7. Open questions, in priority order

1. ~~Does the landing camera path run before or after the surface cell load?~~ **Resolved by
   observation: after.** Vanilla order is site-selection → loading screen → landing scene. Design
   above assumes this; worth confirming in RE before relying on it for timing.
2. Confirm the `LandingEvent` payload layout (§3).
3. Which of `119892`–`119896` is the true `initiateLandingSequence`, and does its completion
   counterpart contain an `addCellToLoader` call to NOP, as `ID_119911+0x354` does for takeoff
   (`src/plugin.cpp:662`)?
4. Is there a completion signal on the virtual-landing-block system to drive the swap, or must it
   be polled?

## 8. Proposed config surface (not yet implemented)

```ini
EnableSeamlessLanding   = 0     ; default off until in-game verified
DescentMinLength        = 6.0   ; floor, seconds
DescentMaxWait          = 20.0  ; ceiling before falling back to a fade
DisableLandingCam       = 0     ; likely unnecessary — landing has its own camera path
```
