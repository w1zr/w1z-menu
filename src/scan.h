// Byte-pattern scanning and flag resolution. Kept free of Windows/game
// dependencies so the same code is exercised by the offline test (tests/).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace scan {

struct Pattern {
    std::vector<int> bytes; // -1 = wildcard
};

// Parses "32 C9 38 0D ?? ?? ..." into a Pattern.
Pattern Parse(const char* text);

// Returns every offset in [data, data+size) where the pattern matches.
std::vector<size_t> FindAll(const uint8_t* data, size_t size, const Pattern& p);

// Result of locating the engine's debug ammo flags inside a mapped image.
// Offsets are relative to the start of the scanned buffer.
struct FlagOffsets {
    bool ok = false;
    std::string error;
    size_t infiniteAmmo = 0;  // debug "Infinite Ammo" bool
    size_t bottomless = 0;    // debug bool right after it: reloads fill without using reserve
    size_t noReload = 0;      // debug "No Reload" bool
    size_t optionsPtr = 0;    // global pointer to the options object
    size_t infiniteCheck = 0; // start of the engine's IsInfiniteAmmoActive()
    uint32_t modifierOffset = 0; // Gameplay Modifiers > Infinite Ammo byte inside it
};

// A HUD instruction that tests the official modifier byte and can be
// rewritten, at the same length, to test the debug Infinite Ammo flag.
struct HudSite {
    size_t offset = 0; // start of the cmp instruction
    size_t length = 0; // 6 or 7
    std::vector<uint8_t> original;
};

// Finds the five HUD checks (ammo counter x3, accessibility text x2).
// Returns an empty vector unless every one matches exactly once.
std::vector<HudSite> LocateHudSites(const uint8_t* image, size_t scanSize, std::string& error);

// Bytes for `cmp byte ptr [rip+flag], <same operand>` at `site`.
std::vector<uint8_t> BuildHudPatch(const HudSite& site, size_t flagOffset);

// The store that takes one round out of the magazine when a weapon fires, in
// the shared weapon fire routine (RVA 0x1DC14FC). The two calls after it map
// the weapon to its owner and test whether that owner is the player.
struct ClipStore {
    bool ok = false;
    std::string error;
    size_t store = 0;     // mov word ptr [rbx+0xC20], cx (7 bytes)
    size_t getOwner = 0;  // weapon -> owning character
    size_t asPlayer = 0;  // character -> player or null
    size_t isInfinite = 0; // must equal the infinite-ammo check (cross-check)
};
ClipStore LocateClipStore(const uint8_t* image, size_t scanSize);

// Code for the No Reload detour, placed at `stub`. It runs the original store
// unless the No Reload byte is set AND the weapon belongs to the player, then
// jumps back to `site + 7`. All arguments are absolute addresses; the stub
// must be within +-2 GB of `flag`. Returns empty on a range error.
std::vector<uint8_t> BuildNoReloadStub(uint64_t stub, uint64_t site, uint64_t flag, uint64_t getOwner, uint64_t asPlayer);

// `jmp stub; nop; nop` replacing the 7-byte store at `site`.
std::vector<uint8_t> BuildDetourJump(uint64_t site, uint64_t stub);

// A `cmp byte ptr [rip+disp32], r8` (38 /r) or `cmp byte ptr [rip+disp32], imm8`
// (80 3D) that reads `target`. Only the disp32 needs rewriting to redirect it.
struct RipRead {
    size_t dispOffset = 0; // where the disp32 lives
    size_t end = 0;        // address of the next instruction (rip base)
};

// Every compare in [0, scanSize) that reads the byte at `target`. The game
// resets its debug flag block on its own, so the mod redirects these reads to
// private bytes rather than writing the game's flags.
std::vector<RipRead> FindByteCompares(const uint8_t* image, size_t scanSize, size_t target);

// `image` must be laid out as in memory (section RVAs == offsets), which is
// true for the loaded module and for the test harness's manual mapping.
// Patterns are searched in [0, scanSize); resolved targets must be < imageSize.
FlagOffsets LocateFlags(const uint8_t* image, size_t scanSize, size_t imageSize);

} // namespace scan
