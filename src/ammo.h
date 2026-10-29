// Infinite Ammo and No Reload, carried over from the TLOU Infinite Ammo mod.
// The engine's own infinite-ammo path is turned on (the one the Extras >
// Gameplay Modifiers > Infinite Ammo option uses), so magazines still empty
// and reloads stay normal; No Reload is a player-only detour on the magazine
// store. See game.cpp / scan.cpp for the signatures.
#pragma once

namespace ammo {
// Registers the menu features (greyed out until Init finds the game code).
void Register();
// Scans the game and installs the patches. Runs on the startup worker thread.
void Init();
void Stop();
} // namespace ammo
