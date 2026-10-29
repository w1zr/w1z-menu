// Features built on Naughty Dog's developer menu (player, weapons, items,
// inventory, unlocks, camera, scenes). Toggles redirect the engine's own
// debug-flag reads to bytes the menu owns; actions run the developer menu's
// own functions on the game thread, from a hook on GameLoopUpdate.
#pragma once

namespace engine {
// Registers the features (greyed out until Init finds them).
void Register();
// Scans the game, installs the patches and the game-loop hook. Startup thread.
void Init();
} // namespace engine
