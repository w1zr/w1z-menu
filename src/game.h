// Game integration: identifies the host executable, resolves the engine's
// debug ammo flags in the loaded image and redirects the HUD's infinite-ammo
// checks to them.
#pragma once
#include <cstdint>

namespace game {

// Private bytes that the engine's flag checks were redirected to.
struct Flags {
    volatile uint8_t* infiniteAmmo = nullptr; // engine credits fired rounds back to the reserve
    volatile uint8_t* bottomless = nullptr;   // reloads fill the magazine without using the reserve
    volatile uint8_t* noReload = nullptr;     // null if this build's No Reload checks didn't match
    uint8_t* const* options = nullptr;        // -> options object (may be null early in startup)
    uint32_t modifierOffset = 0;              // official Infinite Ammo modifier byte in it
};

// True when running inside tlou-i.exe or tlou-i-l.exe.
bool IsSupportedHost();

// Scans the main module. On failure logs the reason and returns false;
// no memory is written in that case.
bool Resolve(Flags& out);

// Points the HUD's five "is infinite ammo on?" checks at the debug flag so the
// reserve count shows the infinity sign. Returns false (and changes nothing)
// if any site is missing. Must run before the HUD is first drawn.
bool PatchHud(const Flags& flags);

// Reads the official modifier byte, or false if the options object is not up yet.
bool OfficialModifierOn(const Flags& flags);

// The main module: base, end of its first executable section, image size.
bool ImageInfo(const uint8_t*& base, size_t& textEnd, size_t& imageSize);

// A page of read/write memory within rip-relative range of the game image.
uint8_t* AllocDataNearImage();

// True when a DLL with this file name, loaded from the game folder, contains
// `tag` in its read-only data (used to recognise the user's other mods).
bool GameFolderModLoaded(const wchar_t* dllName, const char* tag);

} // namespace game
