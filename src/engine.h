// Features built on Naughty Dog's developer menu (player, weapons, items,
// inventory, unlocks, camera, scenes). Toggles redirect the engine's own
// debug-flag reads to bytes the menu owns; actions run the developer menu's
// own functions on the game thread, from a hook on GameLoopUpdate.
#pragma once
#include <cstdint>
#include <functional>

namespace engine {
// Registers the features (greyed out until Init finds them).
void Register();
// Scans the game, installs the patches and the game-loop hook. Startup thread.
void Init();

// A developer-menu callback: callback(item, message); message 5 = run.
using DevCallback = bool(__fastcall*)(void* item, int message);

// True once the GameLoopUpdate hook is installed (game-thread work possible).
bool GameLoopHooked();
// Queues work for the game thread (runs at the start of the next frame).
void RunOnGameThread(std::function<void()> fn);
// Called every frame on the game thread (before queued work).
void AddTickHandler(std::function<void()> fn);
// Runs a developer-menu callback with the item data at +0x60 set to `data`.
// Game thread only. False if the callback raised an exception.
bool CallDevCallback(DevCallback cb, uint64_t data, int message, bool* result = nullptr);
// True when a player exists (in gameplay, not on the main menu). Game thread.
bool PlayerPresent();
// The player object (nullptr when there is none). Game thread.
void* Player();
} // namespace engine
