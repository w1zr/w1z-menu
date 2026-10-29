// Copied from the TLOU Outfit Changer (C:\TLOU Outfit Changer\src\scan.*),
// renamed to namespace skinscan so it sits beside the menu's own scanner.
#include "skinscan.h"

#include <cstdlib>
#include <cstring>

namespace skinscan {

namespace {

// The debug menu item that unlocks every Extras page. Its bool is the one the
// skins checks below read; it is located through its registration
// (`lea r8, [flag]; lea rdx, [name]`) so that every read can be cross-checked.
const char kUnlockAllName[] = "Unlock All Extras Content";
const char* kRegister = "4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ??";

// IsMainMenuItemUnlocked(), case main-menu-skins (tlou-i.exe 1.1.4.0 RVA 0x1BF13F9):
//   xor dl,dl / jmp / mov dl,bl            ; deluxe-edition entitlement
//   cmp byte ptr [rip+unlockAll], 0        ; <- redirected
//   jne unlocked
//   ... game-completed, deluxe, pre-order
const char* kMainMenu = "32 D2 EB 02 8A D3 80 3D ?? ?? ?? ?? 00 75";
// The in-game menu's skins case (RVA 0x1BF17C1): same checks, different order.
//   test dil,dil / jne / test al,al / jne
//   cmp byte ptr [rip+unlockAll], al       ; <- redirected
//   jne unlocked / test sil,sil
const char* kGameMenu = "40 84 FF 0F 85 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? 38 05 ?? ?? ?? ?? 0F 85 ?? ?? ?? ?? 40 84 F6";

// cmp dword ptr [reg+0CCh], ebx ; je owned  -- "the skin is free (cost 0)"
const char* kCostCheck = "39 ?? CC 00 00 00 74";
constexpr size_t kOwnedChecks = 2; // skins menu display checks (RVA 0x12A6E38, 0x12A77E1)
constexpr size_t kEquipChecks = 1; // equip check (RVA 0x12A784A)

size_t RipTarget(const uint8_t* image, size_t dispOffset, size_t end) {
    int32_t disp;
    std::memcpy(&disp, image + dispOffset, sizeof(disp));
    return end + static_cast<int64_t>(disp);
}

bool Matches(const uint8_t* image, size_t at, size_t limit, const Pattern& p) {
    if (at + p.bytes.size() > limit) return false;
    for (size_t k = 0; k < p.bytes.size(); ++k)
        if (p.bytes[k] >= 0 && image[at + k] != p.bytes[k]) return false;
    return true;
}

void Put32(std::vector<uint8_t>& v, size_t at, int32_t x) { std::memcpy(v.data() + at, &x, 4); }

bool Rel32(uint64_t from, uint64_t to, int32_t& out) {
    const int64_t d = static_cast<int64_t>(to - from);
    out = static_cast<int32_t>(d);
    return out == d;
}

} // namespace

Pattern Parse(const char* text) {
    Pattern p;
    const char* s = text;
    while (*s) {
        while (*s == ' ') ++s;
        if (!*s) break;
        if (s[0] == '?') {
            p.bytes.push_back(-1);
            while (*s == '?') ++s;
        } else {
            char* end = nullptr;
            p.bytes.push_back(static_cast<int>(std::strtoul(s, &end, 16)));
            s = end;
        }
    }
    return p;
}

std::vector<size_t> FindAll(const uint8_t* data, size_t begin, size_t end, const Pattern& p) {
    std::vector<size_t> hits;
    const size_t n = p.bytes.size();
    if (n == 0 || end < begin + n) return hits;
    const int first = p.bytes[0];
    for (size_t i = begin; i + n <= end; ++i) {
        if (first >= 0 && data[i] != first) continue;
        size_t k = 1;
        for (; k < n; ++k) {
            const int b = p.bytes[k];
            if (b >= 0 && data[i + k] != b) break;
        }
        if (k == n) hits.push_back(i);
    }
    return hits;
}

SkinSites LocateSkinSites(const uint8_t* image, const Range& code, const std::vector<Range>& data) {
    SkinSites r;

    // 1. The debug bool, via the menu item that registers it.
    std::vector<size_t> names;
    for (const auto& d : data) {
        for (size_t i = d.begin; i + sizeof(kUnlockAllName) <= d.end; ++i)
            if (std::memcmp(image + i, kUnlockAllName, sizeof(kUnlockAllName)) == 0) names.push_back(i);
    }
    if (names.size() != 1) {
        r.error = "debug menu name found " + std::to_string(names.size()) + " times (expected 1)";
        return r;
    }
    size_t flag = 0, registrations = 0;
    for (size_t at : FindAll(image, code.begin, code.end, Parse(kRegister))) {
        if (RipTarget(image, at + 10, at + 14) != names[0]) continue;
        flag = RipTarget(image, at + 3, at + 7);
        ++registrations;
    }
    if (registrations != 1) {
        r.error = "debug menu registration found " + std::to_string(registrations) + " times (expected 1)";
        return r;
    }
    bool inData = false;
    for (const auto& d : data) inData |= flag >= d.begin && flag < d.end;
    if (!inData) {
        r.error = "debug unlock flag does not resolve into a data section";
        return r;
    }
    r.unlockAll = flag;

    // 2. The two menu reads of it; both must read exactly that byte.
    const auto menu = FindAll(image, code.begin, code.end, Parse(kMainMenu));
    if (menu.size() != 1) {
        r.error = "main menu skins check matched " + std::to_string(menu.size()) + " times (expected 1)";
        return r;
    }
    r.mainMenu = {menu[0] + 6, menu[0] + 8, menu[0] + 13};
    const auto game = FindAll(image, code.begin, code.end, Parse(kGameMenu));
    if (game.size() != 1) {
        r.error = "in-game menu skins check matched " + std::to_string(game.size()) + " times (expected 1)";
        return r;
    }
    r.gameMenu = {game[0] + 17, game[0] + 19, game[0] + 23};
    if (RipTarget(image, r.mainMenu.dispOffset, r.mainMenu.end) != flag ||
        RipTarget(image, r.gameMenu.dispOffset, r.gameMenu.end) != flag) {
        r.error = "skins menu checks do not read the debug unlock flag (game build changed?)";
        return r;
    }

    // 3. The ownership checks. Each `je` must land on `mov bl, 1` (result =
    // owned). Display checks are preceded by the Deluxe Edition test on the
    // same record register; the equip check is followed by a read of the
    // record's bit index (+0xD0).
    const Pattern deluxe = Parse("F6 ?? E4 00 00 00 04");
    const Pattern bitIndex = Parse("48 63 ?? D0 00 00 00");
    size_t owned = 0, equip = 0;
    for (size_t at : FindAll(image, code.begin, code.end, Parse(kCostCheck))) {
        const uint8_t modrm = image[at + 1];
        if ((modrm & 0xF8) != 0x98 || (modrm & 7) == 4) continue; // [reg+disp32], ebx; no SIB
        OwnedSite s;
        s.offset = at;
        s.modrm = modrm;
        s.owned = at + 8 + static_cast<int8_t>(image[at + 7]);
        if (image[s.owned] != 0xB3 || image[s.owned + 1] != 0x01) continue;
        bool isDisplay = false;
        for (size_t back = 7; back <= 0x60 && back <= at - code.begin; ++back) {
            if (Matches(image, at - back, code.end, deluxe) && image[at - back + 1] == (0x80 | (modrm & 7))) {
                isDisplay = true;
                break;
            }
        }
        s.equip = !isDisplay && Matches(image, at + 8, code.end, bitIndex) &&
                  (image[at + 10] & 0xC7) == (0x80 | (modrm & 7));
        if (!isDisplay && !s.equip) continue;
        s.original.assign(image + at, image + at + 8);
        (s.equip ? equip : owned)++;
        r.owned.push_back(s);
    }
    if (owned != kOwnedChecks || equip != kEquipChecks) {
        r.error = "skin ownership checks: " + std::to_string(owned) + " display + " + std::to_string(equip) +
                  " equip (expected " + std::to_string(kOwnedChecks) + " + " + std::to_string(kEquipChecks) + ")";
        r.owned.clear();
        return r;
    }

    r.ok = true;
    return r;
}

std::vector<uint8_t> BuildOwnedStub(uint64_t stub, uint64_t site, uint64_t owned, uint64_t flag, const OwnedSite& s) {
    std::vector<uint8_t> c = {
        0x80, 0x3D, 0, 0, 0, 0, 0x00,                   //  0: cmp byte ptr [rip+flag], 0
        0x74, 23 - 9,                                   //  7: je original     (mod off)
        0xFF, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //  9: jmp [rip+0] -> owned
        0, 0, 0, 0, 0, 0,                               // 23: original cmp (6 bytes)
        0x75, 45 - 31,                                  // 29: jne back        (original je not taken)
        0xFF, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 31: jmp [rip+0] -> owned
        0xFF, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 45: back: jmp [rip+0] -> site + 8
    };
    int32_t d;
    if (!Rel32(stub + 7, flag, d)) return {};
    Put32(c, 2, d);
    std::memcpy(c.data() + 15, &owned, 8);
    std::memcpy(c.data() + 23, s.original.data(), 6);
    std::memcpy(c.data() + 37, &owned, 8);
    const uint64_t back = site + 8;
    std::memcpy(c.data() + 51, &back, 8);
    return c;
}

std::vector<uint8_t> BuildSiteJump(uint64_t site, uint64_t stub) {
    int32_t d;
    if (!Rel32(site + 5, stub, d)) return {};
    std::vector<uint8_t> j = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    Put32(j, 1, d);
    return j;
}

} // namespace skinscan
