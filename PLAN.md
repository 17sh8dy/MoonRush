# Moonrush — plan

Modular SMO gameplay mod. Mario gets faster as he collects Moons. Ryujinx only, SMO **1.0.0**.

## Verified facts (2026-09-27)

- Brandon's SMO is **1.0.0** — `main` build ID `3CA12DFAAF9C82DA064D1698DF79CDA1`.
  The OdysseyDecomp project (`_reference/OdysseyDecomp`) targets exactly this build, so its
  function offsets (`data/file_list.yml`) apply directly.
- The base game's exefs uses only `subsdk0`. The installed randomizer ("Emulator build") is an
  exlaunch module in `subsdk9` with its own `main.npdm`. Moonrush will use a different slot (`subsdk8`).
- All of Mario's movement numbers live in one object, `PlayerConst` (`PlayerActorHakoniwa::mConst`),
  read by virtual getters (`getNormalMaxSpeed()`, `getHeadSlidingSpeed()`, `getSwimHighSpeedMaxH()`, ...).
- Moon counts (from `GameDataFunction`):
  - `getCurrentShineNum` = Moons **held right now** (collected in this kingdom minus Moons paid into the
    Odyssey; after the credits, summed across kingdoms). Goes up and down.
  - `getTotalShineNum` = **total Moons on the save file**. Only goes up.

## Architecture

```
Moonrush/
  game/       exlaunch C++ module -> deploy/subsdk8 (+ main.npdm). Reads config, hooks PlayerConst getters.
  launcher/   Windows app: Play -> settings pages -> writes config -> starts Ryujinx with SMO.
  config      %APPDATA%\Ryujinx\sdcard\Moonrush\settings.ini  (the game reads it as sd:/Moonrush/settings.ini)
```

Game side: hook the 22 PlayerConst getters and return `result * curve(moons)` for the playable Mario only.
The multiplier is refreshed once per frame from a hook on `PlayerActorHakoniwa::movement`.
(v0.1 wrote the values straight into memory instead. Replaced 2026-09-27 so Moonrush can stack with the randomizer.)
Multiplier 1.0, or the module not installed, means vanilla. Game files on disk are never touched.

Proof of work: the module writes one log line per change (`[Moonrush] moons=… mult=…`) to Ryujinx's
guest log, so behaviour can be checked from real logs, not just assumed.

Modular: each feature (Moon Speed now, First Person later) is its own module with its own config section
and launcher page.

## Moon Speed — values per action group (approved 2026-09-27)

| Group | PlayerConst fields scaled |
|---|---|
| Walk / Run | NormalMinSpeed, NormalMaxSpeed, RunAfterTurnSpeedMax, DashJudgeSpeed |
| Crouch walk | SquatWalkSpeed |
| Dive | HeadSlidingSpeed, HeadSlidingSpeedMin |
| Roll | SlopeRollingSpeedStart, SlopeRollingSpeedBoost, SlopeRollingMaxSpeed, SlopeRollingReStartMaxAdd |
| Long jump | LongJumpInitSpeed, LongJumpSpeed, LongJumpSpeedMin |
| Jump air speed | JumpBaseSpeedMax, JumpMoveSpeedMin, JumpMoveSpeedMax |
| Swim | SwimSurfaceSpeedMaxH, SwimHighSpeedMaxH, SwimLowSpeedMaxH, SwimFloorSpeedMaxH, SwimWalkMaxSpeed |

Not touched: jump height, gravity, fall speed, Cappy, captures, enemies, anything outside Mario.

## Captures + Cappy (approved 2026-09-28, v0.3.0, played 2026-09-28)

- Captures use the **same multiplier** as Mario. **All captures scale**, with per-capture off switches in the launcher.
  **Cappy** throw speed/reach: **optional toggle, default off**.
- How: hooks `al::Collider::collide` and `PlayerCollider::collide`. When the collider's trans pointer is the captured
  actor's, the sideways part of the move (perpendicular to gravity) is multiplied before collision runs, so walls
  are still checked over the full distance. Vertical part and the actor's own velocity are untouched (no compounding).
- Captures that move another way (rails, fixed, `isCollideOff`) are not sped up. The log line
  `capture: name=X moves through Collider|PlayerCollider|none` says which, and the launcher shows it per capture.
- Capture names are the game's internal ids (HackObjInfo). The launcher learns them from Ryujinx logs
  (`captures_seen.json` in %APPDATA%\Moonrush) and shows English names only for ids we're sure of.
- Cappy: multiplies MaxSpeed, ContinuousThrowSpeed, Reach, ReturnMaxSpeed, the water versions, tornado reach and
  roll speed/reach in place (HackCapThrowParam at HackCap+0x220). Heights, times and angles stay vanilla. Originals are
  remembered and restored when the toggle is off. Never writes to the shared "missing param" value.

## First Person (approved 2026-09-28, v0.3.0, played 2026-09-28)

- Normal controls (right stick looks, movement camera-relative), eye in Mario's head, **Mario hidden completely**.
- Launcher switch + in-game button: **tap** = switch first/third person, **hold** = peek (optional). Default button is
  the **left-stick click**. The right-stick click is never offered: SMO uses it for its own look-around view
  (`PlayerInputFunction::isTriggerCameraSubjective`).
- Auto-off rules, each a launcher toggle (all on by default): cutscenes & scripted cameras (`rs::isActiveDemo`, or the
  camera can't be turned by the stick), 8-bit 2D (`rs::isPlayer2D`), captures (off = view from the captured object).
- How: hook `CameraPoseUpdater::exeActive`. After the game poses its camera, move pos to the eye and keep the view
  direction. Eye = Mario's position + up x 0.85 x PlayerConst::mTall (0.6 for captures). Near clip lowered 100 -> 15
  while in first person and restored afterwards. Mario is hidden by hooking `PlayerModelChangerHakoniwa::syncShowHide`
  and zeroing his show flags only for that call, so the game's own visibility state is never changed.
- Played 2026-09-28 (Brandon: good). Log: first person on/off via the button (D-pad right) and auto-off for a
  capture; Uproot (`Senobi`) sped up through PlayerCollider (x2.035-2.060); Cappy x2.035 (MaxSpeed 32 -> 65.1,
  Reach 500 -> 1017.5). Only Uproot captured so far, so other captures' paths are still unknown.
- Freezes (5-15 s, "GPU processing thread is too slow" + "WaitOnSyncpoint ... 1000ms") happen in every logged session,
  including the Moonrush-only v0.2.0 one, and line up with the game's sequences (Moon gets, messages), not with any
  Moonrush event. No Moonrush-free baseline session exists yet to compare against.
- Still to check in play: Cappy on Mario's head in view? Eye height when crouching/rolling (fixed height for now)?
  Walls closer than 15 units still cut away? Captured objects seen from inside when the capture rule is off?

## Known risks

- Very high speed can push Mario through thin walls (collision is checked per frame). Hence the max-speed cap.
- Some timed Moons and races become easier.
- Run animations may foot-slide at high multipliers.

## Decisions (Brandon, 2026-09-27)

- Start below vanilla: 0.75x, +0.25%/Moon, linear, cap 2.0x, total save-file Moons. All 7 groups on.
- Cappy/capture movement: separate module (built 2026-09-28, see above). First Person: built 2026-09-28, see above.
- Randomizer conflict (it hooks 14 of the same getters for its own speed options): **stack**, i.e.
  final = randomizer value x Moon multiplier.

## Open

- Verify Moon Speed while actually playing (needs Brandon's controller): walk/run, crouch, dive, roll, long jump, air, swim,
  alone and with the randomizer. Watch for wall clipping at high multipliers.
- CORRECTION 2026-09-28: "Emulator build" (subsdk9) is NOT the randomizer. It's a rival Moon-speed mod
  (GameBanana 551833), now disabled. "Randomizer" in this file up to here means that mod. The real randomizer is
  exefs subsdk4 + romfs (kept at Desktop\SMO-Randomizer). It references no PlayerConst getters and is not yet
  tested together with Moonrush.
- Moonrush + real randomizer VERIFIED together 2026-09-28: both load (Ryujinx warns "Multiple replacements to
  'main.npdm'", harmless). Moonrush follows the randomizer save's Moons (3→6, mult 2.015→2.030, NormalMaxSpeed 14 → 28.2-28.4).
- Moon Speed solo gameplay VERIFIED 2026-09-28 (Brandon played; log `stored=14.000 effective=32.550`).
- ANSWERED 2026-09-28 (disassembled the Emulator build speed mod's subsdk9, read-only): its 14 getter hooks never call the
  original. Each returns one of its own globals (boot values: walk/run 2.0, long jump 3.0, jump move 4.0, roll 5.0,
  crouch 0.5, swim 0.9-1.2; vanilla NormalMaxSpeed is 14.0). Its `getmoonhook` adds +1.5 / +1 / +3 to them on every
  Moon collected this session (resets each boot). The hooks are installed at boot whether or not the randomizer is
  connected, so "not connected" still means these 14 speeds are the randomizer's. Proof from the real game log:
  `moons=65 mult=2.325 NormalMaxSpeed stored=14.000 effective=4.650` (= 2.0 x 2.325).
  Needs Brandon's decision: keep stacking, or have Moonrush use the vanilla value when the randomizer is loaded.

## Moon Animation Speed (v0.6, 2026-10-04) - BUILT, NOT PLAYED
Own feature, independent of the Randomizer's "Fast Moon Demos" (whose mechanism was never found). Verified in the real 1.0.0 code
(tools/annotate.py prints any function with its callees named):
- Ordinary Moon-get = `StageSceneStateGetShine` (also the shop's Moons: `setShopShine10`). Nerves: DemoGetFirst -> DemoGet ->
  DemoShineCount -> DemoEnd (+EndWaitScreenFader / EndAndWait). The first three each run, once per frame, the scene's demo update triple
  `al::updateKitListPrev(scene)`, `rs::updateKitListDemoPlayerWithPauseEffect(scene)`, `al::updateKitListPostDemoWithPauseNormalEffect(scene)`
  (scene = `*(state + 0x18)`) and then test whether Mario's demo action / the count layout finished. One-shot work (Shine::get, achievement
  prepo, startShineCountAnim, hit reactions) is under `al::isFirstStep`. DemoEnd calls `rs::updateNormalStateExcludeGraphics` (gameplay
  resumes) so it is NOT touched.
- Mechanism: hook those three exe functions; unless it is the first step, run the triple N-1 extra times before Orig (fractional speeds
  carry). Nothing else changes: counting, saving, fanfare triggers, transitions are the game's own. Range 1.00-5.00x, default off, 2.00x.
- Optional signatures (`k_anim_signatures`): a mismatch (another mod patched those functions) turns ONLY this feature off.
- NOT covered: `StageSceneStateGetShineMain` (story Moons), `...Grand` (Grand/Multi?), `...MainSandWorld`. Which state a Multi Moon uses is
  unknown; the log line `moon animation: ordinary Moon-get demo started` shows when the ordinary state runs.
- UNVERIFIED in game: that audio/fanfare timing feels right at 2-4x, that the camera behaves, Multi/special Moons. Log lines: `settings: moon_anim=`,
  `moon animation: x2.00 active (DemoGet, +1 update(s)...)`.
