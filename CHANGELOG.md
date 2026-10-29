# Changelog

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
