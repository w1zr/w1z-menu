// w1z's menu: page tree, navigation (keyboard, mouse, controller) and
// drawing. Everything is drawn on ImGui's foreground draw list with the
// menu's own layout; no ImGui windows or widgets are used.
#pragma once

namespace menu {
// Registers the menu's own settings as features and builds the pages.
// Call after every module has registered its features.
void Init();
// Creates the ImGui context and loads fonts (render thread, first frame).
void CreateContext();
// Called once per frame between ImGui::NewFrame and ImGui::Render.
void Draw();
bool IsOpen();
} // namespace menu
