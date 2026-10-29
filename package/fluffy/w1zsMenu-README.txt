w1z's menu v1.0.0 - in-game mod menu for The Last of Us Part I (PC)
====================================================================

Open / close:  Insert or F9  (controller: L3 + R3)
Navigate:      Up / Down, Enter = select, Esc or Backspace = back,
               Left / Right = change a number
               Mouse: hover, click, right-click = back, wheel = scroll
               Controller: D-pad, A / Cross = select, B / Circle = back
Favorite:      Tab (controller: X / Square) on any option
Search:        just start typing
Hotkeys:       F7 Infinite Ammo, F8 No Reload (menu closed)

While the menu is open the game receives no keyboard, mouse or controller
input.

Pages
  Player          God Mode, No Hit Reactions, Infinite Sprint,
                  Undetectable Player, One Hit Kills
  Weapons/Items   Infinite Ammo, No Reload, infinity sign on the HUD,
                  Infinite Weapon Health, Disable Weapon Sway,
                  Disable Auto Reload, Disable Auto Holster, Drop All Weapons,
                  Give Joel / Ellie / Melee / Items,
                  Infinite Crafting, Infinite Craftables
  Inventory       Give All + Upgrades, Give Each Craftable,
                  Weapon Parts x10 / x100, Pills x10 / x100,
                  All Recipes, No Recipes
  Camera          Manual Camera, Teleport Player To Camera, Hide HUD
  Unlocks         Give / Remove Treasures, Give Journal Pages, Reset Journal,
                  Unlock Skins: change your outfit in-game from Pause > Skins
                  (and Extras > Skins) without finishing the story,
                  All Chapters / Encounters / Extras (only while the menu is
                  loaded; never written to your profile)
  Scene Teleport  Replay Current Scene
  Misc            Game Speed (experimental)
  Settings        remember toggles, notifications, hotkey sound, menu size,
                  feature status page

How it works: the game still contains Naughty Dog's developer menu. The
menu finds each developer-menu item by its exact name when the game starts
and uses the engine's own flag or function. Anything it cannot find is
greyed out with the reason; everything else keeps working.

Giving items, weapons, parts, treasures or journal pages changes the
current game. The menu never writes save files itself, but the game saves
your inventory at its next checkpoint like any other change.

Files (next to tlou-i.exe)
  winmm.dll              the menu (forwards everything else to Windows' winmm.dll)
  w1zsMenu.ini           settings, saved automatically by the menu
  w1zsMenu.log           what loaded and why anything did not
  w1zsMenu-README.txt    this file

Uninstall: untick it in Fluffy Mod Manager, or delete the four files above.

If the standalone TLOU Infinite Ammo mod is also installed, it keeps handling
ammo and the menu's ammo options are greyed out. If the TLOU Outfit Changer
is installed, it keeps handling skins and the menu's skin options are greyed
out. The menu includes everything both mods do, so you only need the menu.

by w1z

Known issue: in Encounters, a weapon's magazine size can change after a restart.
This is the game's own behaviour (it happens with no mods installed).
