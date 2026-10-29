#include "scan.h"

#include <cstdlib>
#include <cstring>

namespace scan {

namespace {

// IsInfiniteAmmoActive() in the engine (tlou-i.exe 1.1.4.0 RVA 0x1DC879C):
//   xor  cl, cl
//   cmp  byte ptr [rip+X], cl        ; debug "Infinite Ammo" flag
//   jne  +0xF
//   mov  rax, [rip+Y]                ; global options object
//   cmp  byte ptr [rax+0x4968], cl   ; Extras > Gameplay Modifiers > Infinite Ammo
//   je   +2
//   mov  cl, 1
//   mov  al, cl
//   ret
// Every weapon path that implements the built-in modifier (refund on fire,
// reload refill, can-reload check) calls this function.
const char* kInfiniteCheck =
    "32 C9 38 0D ?? ?? ?? ?? 75 0F 48 8B 05 ?? ?? ?? ?? 38 88 ?? ?? ?? ?? 74 02 B1 01 8A C1 C3";

// Inside the weapon "can reload" helper (RVA 0x1DC11E1) the engine tests three
// neighbouring debug bools: "No Reload", an unnamed one, and the one right after
// "Infinite Ammo". Used to find the No Reload flag and to cross-check layout.
const char* kReloadFlags =
    "40 38 3D ?? ?? ?? ?? 40 8A CF 74 02 B1 01 40 38 3D ?? ?? ?? ?? 75 0C 40 38 3D";

size_t RipTarget(const uint8_t* image, size_t instr, size_t dispOffset, size_t instrLen) {
    int32_t disp;
    std::memcpy(&disp, image + instr + dispOffset, sizeof(disp));
    return instr + instrLen + static_cast<int64_t>(disp);
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

std::vector<size_t> FindAll(const uint8_t* data, size_t size, const Pattern& p) {
    std::vector<size_t> hits;
    const size_t n = p.bytes.size();
    if (n == 0 || size < n) return hits;
    const int first = p.bytes[0];
    for (size_t i = 0; i + n <= size; ++i) {
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

FlagOffsets LocateFlags(const uint8_t* image, size_t scanSize, size_t imageSize) {
    FlagOffsets r;

    auto a = FindAll(image, scanSize, Parse(kInfiniteCheck));
    if (a.size() != 1) {
        r.error = "infinite-ammo check signature matched " + std::to_string(a.size()) + " times (expected 1)";
        return r;
    }
    auto b = FindAll(image, scanSize, Parse(kReloadFlags));
    if (b.size() != 1) {
        r.error = "reload-flags signature matched " + std::to_string(b.size()) + " times (expected 1)";
        return r;
    }

    // The third cmp's disp32 lies just past the pattern's last byte.
    if (b[0] + 30 > imageSize) {
        r.error = "reload-flags signature too close to end of image";
        return r;
    }

    // cmp byte ptr [rip+disp], cl  -> 38 0D disp32, starts 2 bytes in, 6 bytes long
    const size_t infinite = RipTarget(image, a[0] + 2, 2, 6);
    // cmp byte ptr [rip+disp], dil -> 40 38 3D disp32, 7 bytes long
    const size_t noReload = RipTarget(image, b[0], 3, 7);
    const size_t afterInfinite = RipTarget(image, b[0] + 23, 3, 7);

    if (infinite >= imageSize || noReload >= imageSize || afterInfinite >= imageSize) {
        r.error = "resolved flag address outside the image";
        return r;
    }
    // The debug bools are adjacent members of one struct. If the layout no
    // longer matches, refuse to write anything.
    if (afterInfinite != infinite + 1 || noReload != infinite + 4) {
        r.error = "debug flag layout mismatch (game build changed?)";
        return r;
    }

    // mov rax, [rip+disp] at +10 (7 bytes) loads the options object pointer;
    // the cmp at +17 carries the modifier byte's offset as a disp32 at +19.
    const size_t optionsPtr = RipTarget(image, a[0] + 10, 3, 7);
    uint32_t modifierOffset;
    std::memcpy(&modifierOffset, image + a[0] + 19, sizeof(modifierOffset));
    if (optionsPtr >= imageSize) {
        r.error = "options pointer outside the image";
        return r;
    }

    r.ok = true;
    r.infiniteAmmo = infinite;
    r.bottomless = afterInfinite;
    r.noReload = noReload;
    r.optionsPtr = optionsPtr;
    r.modifierOffset = modifierOffset;
    r.infiniteCheck = a[0];
    return r;
}

namespace {

struct HudPattern {
    const char* name;
    const char* bytes;
    size_t cmpAt;  // offset of the cmp inside the match
    size_t cmpLen; // 7 = cmp [reg+disp32], imm8 ; 6 = cmp [reg+disp32], r8
};

// Hud2::HealthAndAmmo (RVA 0x1C115F4) chooses between the number and the
// infinity glyph and picks the text colour; the accessibility status function
// (RVA 0x1CAD150) chooses the "infinity" screen-reader strings.
const HudPattern kHud[] = {
    {"ammo glyph", "48 8B 0D ?? ?? ?? ?? 4C 8B 00 80 B9 68 49 00 00 00 48 8B C8 0F 94 C2", 10, 7},
    {"ammo number", "48 8B 05 ?? ?? ?? ?? 49 8D B7 40 05 00 00 49 BB B3 01 00 00 00 01 00 00 80 B8 68 49 00 00 00", 24, 7},
    {"ammo colour", "48 8B 05 ?? ?? ?? ?? 80 B8 68 49 00 00 00 75 ?? 49 8B CD", 7, 7},
    {"a11y gun ammo", "48 8B 0D ?? ?? ?? ?? 4C 8D 45 ?? 8B 44 24 ?? C7 45 ?? 02 00 00 00 38 99 68 49 00 00", 22, 6},
    {"a11y weapon cross", "48 8B 0D ?? ?? ?? ?? 4C 8D 45 ?? 38 99 68 49 00 00 48 8D 4D ?? 0F 84", 11, 6},
};

} // namespace

std::vector<HudSite> LocateHudSites(const uint8_t* image, size_t scanSize, std::string& error) {
    std::vector<HudSite> sites;
    for (const auto& h : kHud) {
        auto hits = FindAll(image, scanSize, Parse(h.bytes));
        if (hits.size() != 1) {
            error = std::string("HUD site '") + h.name + "' matched " + std::to_string(hits.size()) + " times";
            return {};
        }
        HudSite s;
        s.offset = hits[0] + h.cmpAt;
        s.length = h.cmpLen;
        s.original.assign(image + s.offset, image + s.offset + s.length);
        sites.push_back(s);
    }
    return sites;
}

ClipStore LocateClipStore(const uint8_t* image, size_t scanSize) {
    ClipStore c;
    // movsx eax,[rbx+C20]; mov ecx,r12d; sub eax,1; cmovns cx,ax; mov [rbx+C20],cx;
    // call IsInfiniteAmmoActive; test al,al; je; mov rcx,rbx; call GetOwner; mov rcx,rax; call AsPlayer
    const auto hits = FindAll(image, scanSize, Parse(
        "0F BF 83 20 0C 00 00 41 8B CC 83 E8 01 66 0F 49 C8 66 89 8B 20 0C 00 00 "
        "E8 ?? ?? ?? ?? 84 C0 74 ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ??"));
    if (hits.size() != 1) {
        c.error = "magazine store signature matched " + std::to_string(hits.size()) + " times (expected 1)";
        return c;
    }
    const size_t m = hits[0];
    c.store = m + 17;
    c.isInfinite = RipTarget(image, m + 24, 1, 5);
    c.getOwner = RipTarget(image, m + 36, 1, 5);
    c.asPlayer = RipTarget(image, m + 44, 1, 5);
    c.ok = true;
    return c;
}

namespace {

void Put32(std::vector<uint8_t>& v, size_t at, int32_t x) { std::memcpy(v.data() + at, &x, 4); }

bool Rel32(uint64_t from, uint64_t to, int32_t& out) {
    const int64_t d = static_cast<int64_t>(to - from);
    out = static_cast<int32_t>(d);
    return out == d;
}

} // namespace

std::vector<uint8_t> BuildNoReloadStub(uint64_t stub, uint64_t site, uint64_t flag, uint64_t getOwner, uint64_t asPlayer) {
    // Entered by a jmp from inside the fire routine, where rsp is 16-byte
    // aligned (it is just before a call). 7 pushes + 0x28 keeps it aligned and
    // provides the 32-byte shadow space. The instruction after the store is a
    // call, so flags and volatile registers are dead there; we still restore
    // the integer volatiles to leave the routine's state untouched.
    std::vector<uint8_t> c = {
        0x80, 0x3D, 0, 0, 0, 0, 0x00,             //  0: cmp byte ptr [rip+flag], 0
        0x74, 0x00,                               //  7: je store
        0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, //  9: push rax rcx rdx r8 r9
        0x41, 0x52, 0x41, 0x53,                   // 16: push r10 r11
        0x48, 0x83, 0xEC, 0x28,                   // 20: sub rsp, 28h
        0x48, 0x8B, 0xCB,                         // 24: mov rcx, rbx        (weapon)
        0xFF, 0x15, 0, 0, 0, 0,                   // 27: call [rip+getOwner]
        0x48, 0x8B, 0xC8,                         // 33: mov rcx, rax
        0xFF, 0x15, 0, 0, 0, 0,                   // 36: call [rip+asPlayer]
        0x48, 0x85, 0xC0,                         // 42: test rax, rax
        0x48, 0x8D, 0x64, 0x24, 0x28,             // 45: lea rsp, [rsp+28h]  (keeps flags)
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58, // 50: pops
        0x75, 0x07,                               // 61: jnz back            (player: skip store)
        0x66, 0x89, 0x8B, 0x20, 0x0C, 0x00, 0x00, // 63: store: mov [rbx+0C20h], cx
        0xFF, 0x25, 0, 0, 0, 0,                   // 70: back: jmp [rip+0]
        0, 0, 0, 0, 0, 0, 0, 0,                   // 76: site + 7
        0, 0, 0, 0, 0, 0, 0, 0,                   // 84: getOwner
        0, 0, 0, 0, 0, 0, 0, 0,                   // 92: asPlayer
    };
    c[8] = 63 - 9; // je store
    int32_t d;
    if (!Rel32(stub + 7, flag, d)) return {};
    Put32(c, 2, d);
    Put32(c, 29, static_cast<int32_t>(84 - 33));
    Put32(c, 38, static_cast<int32_t>(92 - 42));
    const uint64_t back = site + 7;
    std::memcpy(c.data() + 76, &back, 8);
    std::memcpy(c.data() + 84, &getOwner, 8);
    std::memcpy(c.data() + 92, &asPlayer, 8);
    return c;
}

std::vector<uint8_t> BuildDetourJump(uint64_t site, uint64_t stub) {
    int32_t d;
    if (!Rel32(site + 5, stub, d)) return {};
    std::vector<uint8_t> j = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    Put32(j, 1, d);
    return j;
}

std::vector<RipRead> FindByteCompares(const uint8_t* image, size_t scanSize, size_t target) {
    std::vector<RipRead> reads;
    for (size_t i = 0; i + 7 <= scanSize; ++i) {
        size_t end = 0;
        if (image[i] == 0x38 && (image[i + 1] & 0xC7) == 0x05)
            end = i + 6; // cmp [rip+disp32], r8 (an optional REX prefix before it doesn't move the end)
        else if (image[i] == 0x80 && image[i + 1] == 0x3D)
            end = i + 7; // cmp [rip+disp32], imm8
        else
            continue;
        int32_t disp;
        std::memcpy(&disp, image + i + 2, sizeof(disp));
        if (static_cast<int64_t>(end) + disp != static_cast<int64_t>(target)) continue;
        reads.push_back({i + 2, end});
    }
    return reads;
}

std::vector<uint8_t> BuildHudPatch(const HudSite& site, size_t flagOffset) {
    const int64_t disp64 = static_cast<int64_t>(flagOffset) - static_cast<int64_t>(site.offset + site.length);
    const int32_t disp = static_cast<int32_t>(disp64);
    std::vector<uint8_t> out;
    if (disp != disp64) return out;
    if (site.length == 7) {
        // 80 /7 with ModRM 3D = cmp byte ptr [rip+disp32], imm8
        out = {0x80, 0x3D, 0, 0, 0, 0, site.original[6]};
    } else {
        // 38 /r: keep the register field of the original ModRM, mod=00 rm=101 (rip)
        const uint8_t reg = site.original[1] & 0x38;
        out = {0x38, static_cast<uint8_t>(reg | 0x05), 0, 0, 0, 0};
    }
    std::memcpy(out.data() + 2, &disp, sizeof(disp));
    return out;
}

} // namespace scan
