// Copied from the TLOU Outfit Changer (C:\TLOU Outfit Changer\src\scan.*),
// renamed to namespace skinscan so it sits beside the menu's own scanner.
// Byte-pattern scanning and patch building. Kept free of Windows/game
// dependencies so the same code is exercised by the offline test (tests/).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace skinscan {

struct Pattern {
    std::vector<int> bytes; // -1 = wildcard
};

// Parses "32 D2 EB 02 ?? ..." into a Pattern.
Pattern Parse(const char* text);

// Returns every offset in [begin, end) of `data` where the pattern matches.
std::vector<size_t> FindAll(const uint8_t* data, size_t begin, size_t end, const Pattern& p);

// A section of the mapped image, as offsets from the image base.
struct Range {
    size_t begin = 0;
    size_t end = 0;
};

// A `cmp byte ptr [rip+disp32], ...` whose disp32 the mod points at its own byte.
struct RipRead {
    size_t instr = 0;      // start of the instruction
    size_t dispOffset = 0; // where the disp32 lives
    size_t end = 0;        // address of the next instruction (rip base)
};

// `cmp dword ptr [reg+0CCh], ebx` + `je owned` (8 bytes) inside one of the
// engine's "does the player own this skin?" checks. reg holds the skin record
// from skins-viewer.bin: +0xCC is its point cost.
struct OwnedSite {
    size_t offset = 0; // start of the cmp
    uint8_t modrm = 0; // 0x98 | reg
    size_t owned = 0;  // je target: the "owned" result
    std::vector<uint8_t> original; // the 8 bytes being replaced
    bool equip = false; // the equip check (vs. the menu's owned/locked display)
};

struct SkinSites {
    bool ok = false;
    std::string error;
    size_t unlockAll = 0; // debug "Unlock All Extras Content" bool
    RipRead mainMenu;     // its read in the main menu's Extras > Skins case
    RipRead gameMenu;     // its read in the in-game menu's Skins case
    std::vector<OwnedSite> owned; // 2 display checks + 1 equip check
};

// `image` must be laid out as in memory (section RVAs == offsets), which is
// true for the loaded module and for the test harness's manual mapping.
// `code` is the executable section; `data` are the sections searched for the
// debug menu string. Nothing is returned unless every site matches exactly.
SkinSites LocateSkinSites(const uint8_t* image, const Range& code, const std::vector<Range>& data);

// Code for one ownership site, placed at `stub`: when the mod's byte at `flag`
// is set, jump to the "owned" result; otherwise run the original cmp/je and
// continue after the site. Skins flagged Deluxe Edition (+0xE4 bit 4) are
// included: the base game sells them for points after the story like every
// other skin (the buy routine never checks the flag), Deluxe owners just get
// them free. Addresses are
// absolute; the stub must be within +-2 GB of `flag`. Empty on a range error.
std::vector<uint8_t> BuildOwnedStub(uint64_t stub, uint64_t site, uint64_t owned, uint64_t flag,
                                    const OwnedSite& s);

// `jmp stub` plus 3 nops, replacing the 8-byte cmp/je at `site`.
std::vector<uint8_t> BuildSiteJump(uint64_t site, uint64_t stub);

} // namespace skinscan
