// Pauses the game while the menu is open, using the engine's own pause.
#pragma once
#include <string>

namespace pause {
// Locates the engine's pause by signature (startup worker thread).
void Init();
bool Available();
std::string Reason(); // why it is unavailable
// Returns true if the game was paused by this call.
bool Set(bool paused);
} // namespace pause
