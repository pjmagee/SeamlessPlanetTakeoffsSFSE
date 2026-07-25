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

## 6a. Runtime measurements (probe, 2026-07-25)

Measured in-game with the observation probe (`src/landing.h`). These replace the guesses
the design was previously carrying.

**Player landing sequence**

```
t=67.018   LandingEvent ship=...C4085420 state=0  isPlayerShip=true
t=67.028   STALL 8.880s wall (engine dt=0.010s)  skyMode 1->3   <-- the loading screen
t=80.034   LandingEvent ship=...C4085420 state=1  isPlayerShip=true
```

- **The load is ~8.9 s.** That is the descent budget: the window a seamless landing has to
  cover. It is the single number the feature is built around.
- `LandingEvent state=0` fires ~10 ms *before* the load and is the trigger point, exactly
  mirroring `TakeOffEvent state=0` (`src/plugin.cpp:325`).
- `state=1` arrives ~13 s later, after the vanilla cutscene — the completion signal.
- Payload confirmed as `{ NiPointer<TESObjectREFR> ship; uint32_t state; }`, same as
  `TakeOffEvent`. Landing has the same NPC-ship noise takeoff does; filter by comparing
  the ship pointer against a **cached** last-known player ship — `GetSpaceship()` returns
  null mid-transition, which is precisely when the player's own event fires.

**Existing takeoff, same session, for calibration**

```
STALL 5.167s  (skyMode 3->0, takeoffState=3)
STALL 1.147s
```

Seamless takeoff already hides ~6.3 s of frozen frames behind `toggleFrameDraw(false)`
plus the blur, and that reads acceptably in practice. So the technique is proven at ~6 s
and landing needs it stretched to ~9 s — an extension of something that works, not a new
bet. This materially de-risks §6's warning about frozen screens.

**Landing camera**

The vanilla landing cutscene is an exterior/third-person shot. That is inconsistent with
seamless takeoff, which keeps the player in the cockpit looking out. `ID_119908` (takeoff)
starts its camera path via `ID_113440(ID_937788, ...)` behind a guard, and `DisableTakeOffCam`
(`src/plugin.cpp:678-682`) patches `+0x1c9` to skip that branch. **`ID_119894` (the landing
workhorse) calls the same `ID_113440`**, so a `DisableLandingCam` is the same patch applied
at the landing call site. The exact branch offset within `ID_119894` still needs a raw-asm
pass — `XrefsToId` on the camera-path default objects (`ID_5522`/`ID_5523`) returns only
data-table entries, not code sites, so it does not locate it.

## 6b. Suppressing the landing load screen — the real blocker

To insert a descent, the engine's landing cell-load must be **deferred**, not merely hidden.
Nothing can be rendered during it: the probe showed `PCUpdate` is not called at all for the
8.9 s (§6a), so there is no frame in which to draw a descent. The load has to be stopped,
the descent played, and the load then triggered by us — exactly what takeoff does.

**The cell loader is `ID_102937` @ `141a6a260`.** Takeoff NOPs a single call to it:

```
14211d324  CALL 0x141a6a260     <-- ID_119911 + 0x354, the call src/plugin.cpp:662 NOPs
```

Verified by dumping `ID_119911`'s raw assembly; the `+0x354` in the existing source lands
exactly on that instruction.

**This does not mirror.** `ID_119894` — the landing workhorse — does **not** call
`ID_102937` at all, so landing reaches the loader by a different route. `ID_102937` has ~26
callers binary-wide (fast travel, door transitions, and more), so it cannot be NOP'd
globally the way takeoff NOPs its one site; the landing caller has to be pinned down
specifically or the patch will break unrelated loads.

### The landing load site — found

**`ID_119833` @ `14210f1c0` is the candidate.** Its decompile contains:

```c
ID_102937(ID_922868 + 0xa08, &local_218);      // at 14210f422  =>  ID_119833 + 0x262
```

That is the *same shape* as takeoff's NOP site — same loader, same `global + 0xa08` first
argument, same stack-struct second argument. Compare the takeoff assembly:

```
14211d311  MOV  RCX,qword ptr [0x145f4afd0]
14211d318  ADD  RCX,0xa08
14211d31f  LEA  RDX,[RSP + 0x40]
14211d324  CALL 0x141a6a260                    // ID_119911 + 0x354, NOPed by src/plugin.cpp:662
```

So the landing equivalent of `src/plugin.cpp:662` is a 5-byte NOP at **`REL::ID(119833) + 0x262`**.

**Who reaches it** (`XrefsToId` on `ID_119833`):

| caller | note |
|---|---|
| `ID_82093` @ `1412b1260` | UI/menu cluster — `ID_82022` in the same range holds the `"TakeoffMenu"` string, so this is very likely the **star-map "Land here" confirm** path |
| `ID_119814` @ `14210ba10` | spaceship cluster — candidate for the **ship-view POI land (`R`)** path |
| `ID_120461` @ `142158590` | third entry point, unclassified |

The two user-facing triggers (star-map landing-site selection, and ship-view `R` on a POI)
therefore appear to converge on `ID_119833`, which is what makes a single NOP viable.

**Still to verify before patching:** which caller corresponds to which trigger, and whether
`ID_119833` is landing-specific or shared with other spaceship loads. If it is shared, the
NOP must be gated on our own landing state (set from `LandingEvent state=0`) rather than
applied statically — the same lesson as the `unloadCurrentLocation` crash: a patch that is
correct for one path can be fatal on another. Prefer the dynamic-toggle pattern
(`toggleUnloadLoadScreen`, `src/plugin.cpp`) over a startup patch.

## 7. Open questions, in priority order

1. ~~Does the landing camera path run before or after the surface cell load?~~ **Resolved by
   observation: after.** Vanilla order is site-selection → loading screen → landing scene. Design
   above assumes this; worth confirming in RE before relying on it for timing.
2. ~~Confirm the `LandingEvent` payload layout.~~ **Resolved by probe (§6a):**
   `{ NiPointer<TESObjectREFR> ship; uint32_t state; }`, state 0 then 1.
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
