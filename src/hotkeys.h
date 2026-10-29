// Global hotkeys (RegisterHotKey). The game's raw keyboard input hides key
// presses from GetAsyncKeyState and keyboard hooks, but Windows hotkeys are
// delivered regardless. Used for the menu keys and the F7/F8 toggles from the
// Infinite Ammo mod. Presses while another window is focused are ignored.
#pragma once

namespace hotkeys {
void Start();
void Stop();
// True once per menu-key press (consumed by the render thread).
bool TakeMenuToggle();
} // namespace hotkeys
