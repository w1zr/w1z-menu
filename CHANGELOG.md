# Changelog

## 1.1.0 — 2026-10-29

### Fixed

- **Keys leaked to the game while the menu was open.** The game reads some keys
  (the arrow keys, Enter) outside raw input, so navigating the menu also opened
  the weapon cross and switched weapons. While the menu is open, the keyboard
  state functions and the window's key, character and mouse-button messages
  now report nothing pressed as well (releases still get through, Alt+F4 still
  works).
- **Key presses arriving within one frame were handled out of order** (e.g. Esc
  then Down then Enter), which could toggle the wrong option. The menu now takes
  one key press per frame.

### Changed

- Give Melee / Give Joel use the game's own weapon names from its `weapon-art`
  data, checked in-game with the engine's weapon lookup (18 of 18 found).
- Internal: function lookup by name scans back for the exact prologue instead
  of relying on int3 padding.

### Added (experimental — may not work)

- **Character page: Play as Ellie / Play as Joel in Winter.** Every player
  spawn goes through one engine function that receives the character to spawn
  (e.g. `t1x-joel-costume-02`); the menu swaps it for the matching Ellie outfit
  (or a chosen one). **Swap Now** applies it right away through the game's own
  Restart Checkpoint.
- Known problems: in Joel's chapters Ellie's player animation packages are not
  loaded, so she may be unable to move; pressing Swap Now twice in a row hung
  the game in testing. The swap is never saved between launches. To go back,
  turn it off and use Pause > Restart Checkpoint (or restart the game).

## 1.0.0 — 2026-10-28

First release.

- In-game menu (Insert / F9, controller L3 + R3) drawn with Dear ImGui over
  DirectX 12; keyboard, mouse and controller navigation, search, favorites,
  feature status page. The game receives no input while it is open.
- Player: God Mode, No Hit Reactions, Infinite Sprint, Undetectable Player,
  One Hit Kills.
- Weapons and Items: Infinite Ammo and No Reload (from TLOU Infinite Ammo,
  F7 / F8 kept), HUD infinity sign, Infinite Weapon Health, Disable Weapon
  Sway, Disable Auto Reload / Holster, Drop All Weapons, Give Joel / Ellie /
  Melee weapons, Give Items and crafting materials, Infinite Crafting and
  Craftables.
- Inventory: Give All + Upgrades, Give Each Craftable, Parts and Pills
  x10 / x100, All / No Recipes.
- Camera: Manual Camera, Teleport Player To Camera, Hide HUD.
- Unlocks: Treasures, Journal Pages, Unlock Skins (from TLOU Outfit Changer:
  change outfits in-game from Pause > Skins), All Chapters / Encounters /
  Extras while the menu is loaded.
- Scene Teleport: Replay Current Scene. Misc: Game Speed (experimental).
- Loads as a `winmm.dll` proxy, so it installs alongside `version.dll` and
  `xinput1_4.dll` mods; steps aside for the standalone Infinite Ammo and
  Outfit Changer mods when they are installed.
- Every feature is located by signature; a feature whose code is not found is
  greyed out with the reason. Settings in `w1zsMenu.ini`, imported once from
  `TLOUInfiniteAmmo.ini`.

### Fixed during development

- Give-weapon names now come from the game's `weapon-art` data and are
  confirmed with the engine's weapon lookup (0.2.0 used names that did not
  exist).
- Give-weapon callback is found by its prologue instead of by int3 padding.
