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

## Known risks

- Very high speed can push Mario through thin walls (collision is checked per frame). Hence the max-speed cap.
- Some timed Moons and races become easier.
- Run animations may foot-slide at high multipliers.

## Decisions (Brandon, 2026-09-27)

- Start below vanilla: 0.75x, +0.25%/Moon, linear, cap 2.0x, total save-file Moons. All 7 groups on.
- Cappy/capture movement: separate module, later. First Person: planned page only.
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
