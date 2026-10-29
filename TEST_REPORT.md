# Test report — w1z's menu 1.0.0

**Environment:** Windows 11 Home 10.0.26300, NVIDIA RTX 4060, The Last of Us
Part I Steam build 18995818 (`tlou-i.exe` file version 1.1.4.0, in-game
version 1.1.5, image timestamp 0x685BEBE1), Fluffy Mod Manager 3.016. Built
with MSVC v143 (VS 2022 Build Tools). Date: 2026-10-28.

In-game tests used Encounters (Pittsburgh, Underpass, Joel) and, for one check
only, a story save. "Not tested" means the test was not performed.
"User-reported" means the user ran it and reported the result; the menu's log
from that run was overwritten by a later launch, so only the user's report
backs it.

## Automated (offline)

`scan_test.exe` against `tlou-i.exe` and `tlou-i-l.exe` from the install above;
`proxy_test.exe` against System32's `winmm.dll`. All checks pass on both
executables.

| Check | tlou-i.exe | tlou-i-l.exe |
|---|---|---|
| Ammo signatures, HUD sites, No Reload detour (from TLOU Infinite Ammo) | Pass | Pass |
| Developer-menu item constructors identified (bool, bool + callback, function) | Pass | Pass |
| 14 toggle flags located; exact read counts (15, 1, 1, 11/6/7, 1, 2, 4, 3, 1, 5, 1, 9, 1, 33) | Pass | Pass |
| Read counts match an independent disassembly (Capstone) | Pass | — |
| 15 action callbacks located | Pass | Pass |
| Manual Camera callback, Hide HUD switch, Time Scale float | Pass | Pass |
| GameLoopUpdate located by name string + prologue | Pass | Pass |
| Give-weapon callback and weapon lookup located | Pass | Pass |
| Skins: flag, 2 menu checks, 3 ownership checks; menu reads are 2 of the 33 Extras reads | Pass | Pass |
| winmm.dll proxy: 181 exports, same names and ordinals; calls reach System32 | Pass | — |

## In-game

| # | Test | Result |
|---|---|---|
| 1 | Menu loads as winmm.dll; DX12 overlay, input hooks, game-loop hook | **Pass** (log) |
| 2 | Insert opens/closes; arrow keys, Enter, Esc; mouse | **Pass** (log + user) |
| 3 | Game receives no input while the menu is open | **Pass** (user) |
| 4 | Toggles saved to w1zsMenu.ini and restored next launch | **Pass** (ini + log) |
| 5 | Infinite Ammo, No Reload, HUD infinity sign, F7 / F8 | **Pass** (log + user) |
| 6 | Weapon list check: 18 of 18 weapons found | **Pass** (log) |
| 7 | God Mode, Unlock All Extras toggles | **Pass** (log + user) |
| 8 | Skins patch installs; Unlock Skins | **Pass** (log); in-game outfit change user-reported |
| 9 | Give Joel / Ellie / Melee / Items, Parts, Pills, Inventory actions | User-reported working |
| 10 | Player toggles (No Hit Reactions, Infinite Sprint, Undetectable, One Hit Kills) | User-reported working |
| 11 | Weapon toggles (Weapon Health, Sway, Auto Reload / Holster, Drop All) | User-reported working |
| 12 | Manual Camera, Teleport Player To Camera, Hide HUD | User-reported working |
| 13 | Replay Current Scene, Treasures / Journal, Chapters / Encounters | User-reported working |
| 14 | Game Speed | User-reported working |
| 15 | Controller (L3 + R3, D-pad) | **Not tested** (no result recorded) |
| 16 | tlou-i-l.exe in-game | **Not tested** |
| 17 | Installs and uninstalls through Fluffy | **Pass** (installed, swapped between versions) |
| 18 | Alongside TLOU Infinite Ammo / Outfit Changer (steps aside) | **Not tested** in-game; detection code only |
| 19 | Story save load / save with the menu | **Not tested** (only a checkpoint restart, below) |
| 20 | Epic Games Store | **Not tested** |

## Encounters magazine size

The 9mm's magazine size changed (6 to 12) after restarting an Encounter. Steps
taken to find the cause:

| Run | Result |
|---|---|
| Menu installed (Infinite Ammo on), Encounter restart | Changed |
| No mods at all, Encounter restart | Changed |
| No mods, after Steam "Verify integrity" restored `bin.psarc` and `pak68.txt` | Changed |
| Story save, checkpoint restart | Unchanged |

Conclusion: game behaviour in Encounters, not caused by the menu. The menu's
ammo code cannot change capacity (the reload fill is clamped to the weapon's
capacity, and the refund adds to the reserve only).

During the investigation, leftover "Must Install First" files were moved out
of the game folder and caused a startup crash (the mod had removed `bin.psarc`);
they were moved back and the Steam verify restored the game's own files.
