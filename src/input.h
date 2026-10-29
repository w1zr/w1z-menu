// Input while the menu is open.
//
// The game reads the keyboard and mouse through raw input (GetRawInputData),
// so GetAsyncKeyState and keyboard hooks see nothing while it has focus. The
// menu hooks GetRawInputData instead: it reads every key/mouse event the game
// gets and, while the menu is open, blanks key presses, mouse movement and
// clicks before the game sees them. Key/button releases always pass through,
// so nothing the player was holding gets stuck. ClipCursor/SetCursorPos are
// held back while the menu is open so the mouse can move freely, and
// XInputGetState returns an idle controller.
#pragma once
#include <windows.h>
#include <cstdint>

namespace input {

// Installs the hooks. Returns false (and logs) if a required hook failed.
bool Install();

// Called by the menu when it opens/closes (render thread).
void OnMenuOpened();
void OnMenuClosed();

// Render thread, once per frame before ImGui::NewFrame: feeds queued
// keyboard/mouse events and the cursor position into ImGui.
void FeedImGui(HWND window, float backBufferW, float backBufferH);

// Controller buttons (XINPUT_GAMEPAD_* bits) of the first connected pad,
// read through the original XInputGetState. `pressed` = went down this frame.
struct Pad {
    bool connected = false;
    uint16_t held = 0;
    uint16_t pressed = 0;
};
Pad PollPad();

// Subclasses the game window (once) so legacy mouse/keyboard messages can be
// swallowed while the menu is open.
void AttachWindow(HWND window);

} // namespace input
