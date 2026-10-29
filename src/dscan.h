// Locating Naughty Dog's developer-menu items in the game image.
//
// The retail exe still builds the developer menu at startup. Each item is
// created as
//     lea r8/r9, [value or callback]   ; within 48 bytes before
//     lea rdx,  [label string]
//     call      Item constructor        ; within 32 bytes after
// so a feature is found by its exact label text: that is the "signature".
// The value is either the debug flag the item toggles or the callback it
// runs. Like scan.*, this file has no Windows or game dependencies so the
// offline test exercises exactly the same code.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dscan {

// 64-bit FNV-1a, the engine's StringId64 hash.
uint64_t Fnv1a64(const char* s);

// Offsets of a NUL-terminated string that is also preceded by a NUL.
std::vector<size_t> FindCString(const uint8_t* image, size_t size, const char* text);

struct Item {
    size_t site = 0;      // the `lea rdx, [label]`
    size_t ctor = 0;      // constructor called right after
    size_t r8 = 0, r9 = 0; // targets of `lea r8/r9, [rip+x]` before the label (0 = none)
};

// A rip-relative byte read the menu can retarget (only its disp32 changes).
struct Read {
    size_t dispOffset = 0;
    size_t end = 0; // rip at the end of the instruction
};

struct Refs {
    std::vector<Read> reads; // cmp m8,r8 / cmp r8,m8 / cmp m8,imm8 / test / mov r8,m8 / movzx / movsx
    size_t writes = 0;       // mov m8,r8 / mov m8,imm8 / mov m16
    size_t leas = 0;
};

// Builds, in one pass over [0, textEnd), the developer-menu items for each
// label and the references to each flag. Labels are matched exactly.
class Index {
public:
    Index(const uint8_t* image, size_t textEnd, size_t imageSize);

    void WantLabel(const std::string& label);
    void WantFlag(size_t rva);
    void Build();

    const std::vector<Item>& Items(const std::string& label) const;
    const Refs& FlagRefs(size_t rva) const;

    // Every item for `label` must agree on one non-zero r8 (or r9). Returns 0
    // (and sets error) on no match or disagreement.
    size_t ItemR8(const std::string& label, std::string& error) const;
    size_t ItemR9(const std::string& label, std::string& error) const;
    // Same, but only items built by `ctor` count (others with the same label,
    // e.g. a submenu called "Manual Camera", are ignored).
    size_t ItemValue(const std::string& label, size_t ctor, bool r9, std::string& error) const;

    // rip-relative target of the disp32 at `dispOffset` for an instruction ending at `end`.
    size_t RipTarget(size_t dispOffset, size_t end) const;

    const uint8_t* image() const { return image_; }
    size_t textEnd() const { return textEnd_; }
    size_t imageSize() const { return imageSize_; }

private:
    const uint8_t* image_;
    size_t textEnd_, imageSize_;
    std::set<std::string> wantedLabels_;
    std::map<size_t, std::string> labelAt_;       // string rva -> label
    std::map<std::string, std::vector<Item>> items_;
    std::map<size_t, Refs> flags_;
};

// The function that contains a reference (lea) to the NUL-terminated `name`
// string and starts with `prologue` (a pattern as in scan::Parse), found by
// walking back from the reference. Exactly one is required; 0 otherwise.
size_t FindFunctionByName(const uint8_t* image, size_t textEnd, size_t imageSize, const char* name,
                          const char* prologue, std::string& error);

// Rip-relative byte compares at the start of a callback, e.g. Undetectable
// Player's three flags: every `cmp byte ptr [rip+x], r8` in the first `span`
// bytes of `func`, in order.
std::vector<size_t> CallbackFlags(const uint8_t* image, size_t func, size_t span, size_t imageSize);

// Target of the first `call rel32` at or after `from` within `span` bytes.
size_t FirstCall(const uint8_t* image, size_t from, size_t span);

} // namespace dscan
