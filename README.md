# Moonrush

A modular gameplay mod for **Super Mario Odyssey 1.0.0 on Ryujinx**. Its first module, **Moon Speed**,
makes Mario's movement faster the more Moons you have. It comes with a Windows launcher:
**Play** → settings pages → **Save & launch**.

## What Moon Speed changes

Mario's speed = the game's normal value × a multiplier from your Moon count.
Defaults: **0.75× at 0 Moons, +0.25% per Moon (linear), vanilla speed at 100 Moons, capped at 2.0× (500 Moons)**,
counting total save-file Moons. Everything is editable in the launcher.

| Group (toggle) | Values scaled (22 total) |
|---|---|
| Walk & run | NormalMinSpeed, NormalMaxSpeed, RunAfterTurnSpeedMax, DashJudgeSpeed |
| Crouch walk | SquatWalkSpeed |
| Dive | HeadSlidingSpeed, HeadSlidingSpeedMin |
| Roll | SlopeRollingSpeedStart, SlopeRollingSpeedBoost, SlopeRollingMaxSpeed, SlopeRollingReStartMaxAdd |
| Long jump | LongJumpInitSpeed, LongJumpSpeed, LongJumpSpeedMin |
| Air speed | JumpBaseSpeedMax, JumpMoveSpeedMin, JumpMoveSpeedMax |
| Swim | SwimSurfaceSpeedMaxH, SwimHighSpeedMaxH, SwimLowSpeedMaxH, SwimFloorSpeedMaxH, SwimWalkMaxSpeed |

Never changed: jump height, gravity, fall speed, Cappy, captures, enemies, cutscene/wardrobe Mario.
No game file is modified. Disable or uninstall Moonrush in the launcher and SMO is vanilla.

## How it works

- `game/` is an [exlaunch](https://github.com/shadowninja108/exlaunch) module in the **subsdk8** slot.
  It hooks the 22 small PlayerConst getter functions and returns `original result × multiplier`, but only for the
  playable Mario's PlayerConst. A per-frame hook on `PlayerActorHakoniwa::movement` reads the Moon count and
  works out the multiplier.
- **Stacking:** if another mod hooked a getter first (e.g. the "Every Moon Makes you Faster" mod, "Emulator build",
  hooks 14 of them), Moonrush chains on top, so the result is `other mod's value × Moon multiplier`. That mod
  ignores the game's values (walk 2.0 vs vanilla 14), so running both isn't recommended.
- **Version safety:** at startup it checks the exact instruction bytes at every address it uses. On any
  unexpected code it logs why and stays inactive.
- `launcher/` is Tauri 2 plus plain HTML/JS. It writes `%APPDATA%\Ryujinx\sdcard\Moonrush\settings.ini`, installs to
  `%APPDATA%\Ryujinx\mods\contents\0100000000010000\Moonrush`, backs up `mods.json` before every change
  (to `%APPDATA%\Moonrush\backups`), and reads the latest Ryujinx log for proof that the mod actually ran.
- All offsets are generated from the real 1.0.0 code by `tools/gen_offsets.py`, using the OdysseyDecomp
  symbol list plus disassembly. `tools/smo_disasm.py <symbol>` disassembles any function.

## Build & test

```powershell
game\build.ps1                         # needs devkitPro at D:\devkitPro (switch-dev) -> game\deploy\
cd launcher\src-tauri; cargo build     # launcher exe in target\debug
tests\run.ps1                          # launcher tests + game-side curve/parser tests (MSVC)
```

`tools/` expects `..\_reference` (OdysseyDecomp clone, hactool, and the game's extracted exefs).

## Status (2026-09-28)

Verified by playing, and from real Ryujinx logs:
- **Moon Speed alone:** 65 Moons, multiplier 2.325, run speed 14 → 32.55. Plays well.
- **Together with the SMO Randomizer** (exefs subsdk4 + romfs): both load. Ryujinx warns "Multiple replacements to
  'main.npdm'", but that's harmless. Moonrush follows the randomizer save's Moons (3→6, multiplier 2.015→2.030).
  The randomizer doesn't touch Mario's speed values.
- The launcher's install, update, enable and settings file were checked in the real app.

## License

The game module includes exlaunch, which is GPLv2, so `game/` is GPLv2.
