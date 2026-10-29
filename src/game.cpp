#include "game.h"

#include "log.h"
#include "scan.h"

#include <windows.h>
#include <psapi.h>
#include <cstring>
#include <string>
#include <vector>
#include <wchar.h>

namespace game {

namespace {

const uint8_t* g_base = nullptr;
size_t g_textEnd = 0;

// Reads of each debug flag in tlou-i.exe / tlou-i-l.exe 1.1.4.0 (see tests).
constexpr size_t kInfiniteReads = 2;   // IsInfiniteAmmoActive + a weapon-type check
constexpr size_t kBottomlessReads = 5; // reload fill, can-reload, has-ammo x3

// rip-relative operands reach +-2 GB, so the private bytes must sit close to
// the image. Walk down from the image base in allocation-granularity steps.
uint8_t* AllocNear(const uint8_t* base, DWORD protect) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(base) & ~static_cast<uintptr_t>(0xFFFF);
    for (uintptr_t step = 1; step < 0x6000; ++step) { // up to ~1.5 GB below
        const uintptr_t at = start - step * 0x10000;
        void* p = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, protect);
        if (p) return static_cast<uint8_t*>(p);
    }
    return nullptr;
}

bool WriteCode(uint8_t* at, const std::vector<uint8_t>& bytes) {
    DWORD old = 0;
    if (!VirtualProtect(at, bytes.size(), PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes.data(), bytes.size());
    VirtualProtect(at, bytes.size(), old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, bytes.size());
    return true;
}

// No Reload: detour the magazine decrement in the weapon fire routine to a
// stub that skips it only when the flag is set and the weapon's owner is the
// player. Enemies and buddies keep using and reloading magazines normally.
bool InstallNoReload(const uint8_t* base, size_t textEnd, const scan::FlagOffsets& r, const uint8_t* flag) {
    const scan::ClipStore c = scan::LocateClipStore(base, textEnd);
    if (!c.ok) {
        logx::Write("NoReload unavailable: %s", c.error.c_str());
        return false;
    }
    if (c.isInfinite != r.infiniteCheck) {
        logx::Write("NoReload unavailable: fire routine does not call the expected infinite-ammo check");
        return false;
    }
    if (base[c.store] != 0x66 || base[c.store + 1] != 0x89) {
        logx::Write("NoReload unavailable: magazine store already modified (another mod?)");
        return false;
    }
    uint8_t* stub = AllocNear(base, PAGE_EXECUTE_READWRITE);
    if (!stub) {
        logx::Write("NoReload unavailable: no memory near the game image");
        return false;
    }
    const auto u = [&](size_t rva) { return reinterpret_cast<uint64_t>(base) + rva; };
    const auto code = scan::BuildNoReloadStub(reinterpret_cast<uint64_t>(stub), u(c.store),
                                              reinterpret_cast<uint64_t>(flag), u(c.getOwner), u(c.asPlayer));
    const auto jump = scan::BuildDetourJump(u(c.store), reinterpret_cast<uint64_t>(stub));
    if (code.empty() || jump.empty()) {
        logx::Write("NoReload unavailable: detour out of range");
        return false;
    }
    std::memcpy(stub, code.data(), code.size());
    FlushInstructionCache(GetCurrentProcess(), stub, code.size());
    if (!WriteCode(const_cast<uint8_t*>(base + c.store), jump)) {
        logx::Write("NoReload unavailable: could not patch the fire routine (%lu)", GetLastError());
        return false;
    }
    logx::Write("NoReload hook installed at RVA 0x%zX (player only)", c.store);
    return true;
}

bool Redirect(const uint8_t* base, const std::vector<scan::RipRead>& reads, const uint8_t* target) {
    for (const auto& rd : reads) {
        const int64_t disp64 = reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(base + rd.end);
        const int32_t disp = static_cast<int32_t>(disp64);
        if (disp != disp64) return false;
        auto* at = const_cast<uint8_t*>(base + rd.dispOffset);
        DWORD old = 0;
        if (!VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
        std::memcpy(at, &disp, 4);
        VirtualProtect(at, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, 4);
    }
    return true;
}

bool IsWritableData(const void* p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD prot = mbi.Protect & 0xFF;
    return prot == PAGE_READWRITE || prot == PAGE_WRITECOPY;
}

} // namespace

bool IsSupportedHost() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"tlou-i.exe") == 0 || _wcsicmp(name, L"tlou-i-l.exe") == 0;
}

bool Resolve(Flags& out) {
    auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        logx::Write("ERROR: main module has no DOS header");
        return false;
    }
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        logx::Write("ERROR: main module is not a PE32+ image");
        return false;
    }
    logx::Write("image timestamp 0x%08X, size 0x%X", nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);

    // Only scan executable sections; the flags themselves live in .data.
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    size_t textEnd = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            textEnd = sec[i].VirtualAddress + sec[i].Misc.VirtualSize;
            break;
        }
    }
    if (!textEnd) {
        logx::Write("ERROR: no executable section found");
        return false;
    }

    // Scan up to the end of the code section; the flags resolve into .data,
    // which lies further into the image.
    const scan::FlagOffsets r = scan::LocateFlags(base, textEnd, nt->OptionalHeader.SizeOfImage);
    if (!r.ok) {
        logx::Write("ERROR: %s. Mod disabled; nothing was changed.", r.error.c_str());
        return false;
    }

    auto* inf = const_cast<uint8_t*>(base + r.infiniteAmmo);
    auto* bot = const_cast<uint8_t*>(base + r.bottomless);
    auto* nor = const_cast<uint8_t*>(base + r.noReload);
    auto* opt = reinterpret_cast<uint8_t* const*>(base + r.optionsPtr);
    if (!IsWritableData(inf) || !IsWritableData(bot) || !IsWritableData(nor) || !IsWritableData(opt)) {
        logx::Write("ERROR: resolved flags are not in writable data. Mod disabled; nothing was changed.");
        return false;
    }
    logx::Write("flags resolved: InfiniteAmmo @ RVA 0x%zX, Bottomless @ RVA 0x%zX, NoReload @ RVA 0x%zX, "
                "options @ RVA 0x%zX + 0x%X",
                r.infiniteAmmo, r.bottomless, r.noReload, r.optionsPtr, r.modifierOffset);
    g_base = base;
    g_textEnd = textEnd;

    // The game resets its debug flag block by itself, so writing those bytes
    // does not stick. Instead every compare that reads them is pointed at
    // private bytes the mod owns. Counts are what this build was verified with;
    // anything else means the code changed and we refuse to patch.
    const auto infReads = scan::FindByteCompares(base, textEnd, r.infiniteAmmo);
    const auto botReads = scan::FindByteCompares(base, textEnd, r.bottomless);
    logx::Write("flag reads found: InfiniteAmmo %zu, Bottomless %zu", infReads.size(), botReads.size());
    if (infReads.size() != kInfiniteReads || botReads.size() != kBottomlessReads) {
        logx::Write("ERROR: unexpected number of ammo flag reads. Mod disabled; nothing was changed.");
        return false;
    }

    uint8_t* priv = AllocNear(base, PAGE_READWRITE);
    if (!priv) {
        logx::Write("ERROR: could not reserve memory near the game image. Mod disabled; nothing was changed.");
        return false;
    }
    priv[0] = priv[1] = priv[2] = 0; // the worker sets the wanted state right after
    if (!Redirect(base, infReads, priv + 0) || !Redirect(base, botReads, priv + 1)) {
        logx::Write("ERROR: redirecting flag reads failed.");
        return false;
    }
    // The engine's own "No Reload" debug flag only affects reload prompts (it
    // does not stop the magazine emptying, verified in-game), so it is left
    // alone and the mod's No Reload is a separate player-only hook.
    const bool noReloadOk = InstallNoReload(base, textEnd, r, priv + 2);

    out.infiniteAmmo = priv + 0;
    out.bottomless = priv + 1;
    out.noReload = noReloadOk ? priv + 2 : nullptr;
    out.options = opt;
    out.modifierOffset = r.modifierOffset;
    return true;
}

bool PatchHud(const Flags& flags) {
    if (!g_base) return false;
    std::string error;
    const auto sites = scan::LocateHudSites(g_base, g_textEnd, error);
    if (sites.empty()) {
        logx::Write("HUD infinity sign unavailable: %s. Ammo still infinite; HUD shows numbers.", error.c_str());
        return false;
    }
    const size_t flagOffset = reinterpret_cast<uintptr_t>(flags.infiniteAmmo) - reinterpret_cast<uintptr_t>(g_base);

    // Build every patch first so we either change all five sites or none.
    std::vector<std::vector<uint8_t>> patches;
    for (const auto& s : sites) {
        patches.push_back(scan::BuildHudPatch(s, flagOffset));
        if (patches.back().size() != s.length) {
            logx::Write("HUD infinity sign unavailable: flag out of rip-relative range.");
            return false;
        }
    }
    for (size_t i = 0; i < sites.size(); ++i) {
        auto* at = const_cast<uint8_t*>(g_base + sites[i].offset);
        DWORD old = 0;
        if (!VirtualProtect(at, sites[i].length, PAGE_EXECUTE_READWRITE, &old)) {
            logx::Write("ERROR: VirtualProtect failed at RVA 0x%zX (%lu)", sites[i].offset, GetLastError());
            return false; // earlier sites already point at the flag, which is harmless
        }
        std::memcpy(at, patches[i].data(), sites[i].length);
        VirtualProtect(at, sites[i].length, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, sites[i].length);
    }
    logx::Write("HUD patched at %zu sites: reserve shows the infinity sign while Infinite Ammo is on", sites.size());
    return true;
}

bool OfficialModifierOn(const Flags& flags) {
    const uint8_t* options = *flags.options;
    if (!options) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(options + flags.modifierOffset, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    return options[flags.modifierOffset] != 0;
}

} // namespace game

namespace game {

bool ImageInfo(const uint8_t*& base, size_t& textEnd, size_t& imageSize) {
    base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    imageSize = nt->OptionalHeader.SizeOfImage;
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    textEnd = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            textEnd = sec[i].VirtualAddress + sec[i].Misc.VirtualSize;
            break;
        }
    }
    return textEnd != 0;
}

uint8_t* AllocDataNearImage() {
    return AllocNear(reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr)), PAGE_READWRITE);
}

bool GameFolderModLoaded(const wchar_t* dllName, const char* tag) {
    wchar_t dir[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, dir, MAX_PATH)) return false;
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    HMODULE mods[1024];
    DWORD bytes = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &bytes)) return false;
    const size_t tagLen = std::strlen(tag);
    for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i) {
        wchar_t path[MAX_PATH];
        if (!GetModuleFileNameW(mods[i], path, MAX_PATH)) continue;
        const wchar_t* name = wcsrchr(path, L'\\');
        if (!name || _wcsicmp(name + 1, dllName) != 0 || _wcsnicmp(path, dir, wcslen(dir)) != 0) continue;
        const auto* b = reinterpret_cast<const uint8_t*>(mods[i]);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(b + reinterpret_cast<const IMAGE_DOS_HEADER*>(b)->e_lfanew);
        const auto* sec = IMAGE_FIRST_SECTION(nt);
        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
            if (!(sec[s].Characteristics & IMAGE_SCN_MEM_READ) || (sec[s].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
            const size_t lo = sec[s].VirtualAddress, hi = lo + sec[s].Misc.VirtualSize;
            for (size_t k = lo; k + tagLen <= hi; ++k)
                if (std::memcmp(b + k, tag, tagLen) == 0) return true;
        }
    }
    return false;
}

} // namespace game
