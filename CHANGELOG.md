# Changelog

## 1.2.0 — 2026-10-30

**Playable Ellie that actually works.** In Joel's chapters Ellie now moves,
aims, shoots and crafts like a real player character, Joel's 9mm sits
straight in her hands, and Joel can follow her as the AI companion. Tested in
game; the character swap is no longer marked experimental.

### Added

- **Character > Replace Campaign Ellie with Joel** (on by default, always
  saved). While you play as Ellie, the story companion becomes Joel. It is the
  same companion, with her AI, scripted moments and lines, but with Joel's body
  in the chapter's outfit. Every look the game uses goes through one engine
  function, which the menu finds by signature. While Ellie is the player,
  Ellie looks it resolves become the matching Joel outfit, except those for the
  player spawn. Joel's companion animations, which the game uses for him in the
  epilogue (`npc-buddy-all`, `t1x-anim-npc-joel`, `anim-npc-m-buddy-ambi`), are
  streamed in with the swap. Turn it off to keep Ellie as the companion. The
  companion still speaks Ellie's lines in her voice.
- **Settings > Remember Character Swap** (off by default): Play as Ellie / Joel
  stay on for the next launch. A swapped player that spawns without its
  animations gets them loaded and the checkpoint restarted once.
- **Character > Give Ellie Crafting Recipes** (on by default) after a swap.
- **Settings are kept in `Documents\w1z's menu\w1zsMenu.ini`**, so updating
  or reinstalling (or Fluffy) never resets them. The old file in the game
  folder is copied over once. The zip no longer ships an ini.
- Research logging (`[Research] Trace=1`, `[Research] Overlays=1`), off by
  default.

### Fixed (tested in game)

- **Swapped Ellie could only crouch-walk, and couldn't aim, shoot or
  interact.** Each chapter loads only its own player's animation packages.
  - Swap Now now adds Ellie's player level sets to the chapter's player set in
    memory.
  - It then asks the game to rebuild its want-load list, using the game's own
    `LevelMgr` code found from the `LoadLevelSet` console command.
  - It waits until her packages are streamed in, then restarts the checkpoint.
  - If her packages don't load, Swap Now cancels instead of spawning a frozen
    Ellie.
- **Joel's 9mm pointed about 70 degrees up in Ellie's hands.** Its Ellie
  overlay table (`player-specific-weapon-overlays/t1x-guns/t1x-pistol-9mm`) is
  made for the scripted scenes where she holds his gun.
  - While Ellie is the player, the 9mm uses the menu's own copy of the
    revolver's table instead. The revolver's table is made for Ellie in normal
    play.
  - The copy's two hold-pose entries use Ellie's own pistol hold. The
    revolver's own hold is made for a bigger gun and turned the 9mm slightly to
    the right.
  - Result: the gun points straight, recoils on time and doesn't twitch. Joel's
    9mm and the revolver are unchanged.
- **Ellie couldn't open crafting outside Winter.** The game's "crafting
  allowed" check lets an Ellie player craft only while the Winter world
  `lakeside` is loaded, or when a game flag (off by default) is set. The menu
  sets that flag while Ellie is the player and restores it afterwards.

### Removed

- Giving Ellie her own pistol after the swap (the game's give function raised
  an error for it). The 9mm fix makes it unnecessary.

### Known problems

- Ellie's arm clips through the bow when it's put away (game animation; a fix
  attempt made aiming worse and was reverted).
- Ellie's shotgun and hunting rifle twitch in from the right when aiming.
- The companion Joel speaks Ellie's lines in Ellie's voice, and the player
  Ellie speaks Joel's.
- Ellie's melee is her switchblade, also with a gun out.

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
