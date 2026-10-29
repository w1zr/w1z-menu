// DirectX 12 overlay. Hooks the real DXGI swapchain Present underneath
// NVIDIA Streamline (sl.interposer.dll wraps the game's swapchain and calls
// through to DXGI), captures the game's direct command queue when the
// swapchain is created, and draws the menu with Dear ImGui right before each
// Present on that queue.
#pragma once

namespace render {
// Installs the hooks. Returns false (and logs why) if DX12 could not be hooked.
bool Install();
} // namespace render
