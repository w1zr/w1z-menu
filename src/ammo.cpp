#include "ammo.h"

#include "features.h"
#include "game.h"
#include "log.h"
#include "settings.h"

#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace ammo {

namespace {

game::Flags g_flags;
HANDLE g_worker = nullptr;
volatile LONG g_stop = 0;
features::Feature* g_infinite = nullptr;
features::Feature* g_noReload = nullptr;
features::Feature* g_hud = nullptr;

// The flags are private bytes only the mod writes (see game::Resolve). If the
// player turned on the official modifier themselves, mirror it so its HUD and
// behaviour keep working; that is why this runs every tick.
void Apply() {
    const bool want = g_infinite->on;
    const bool official = game::OfficialModifierOn(g_flags);
    *g_flags.infiniteAmmo = (want || official) ? 1 : 0;
    *g_flags.bottomless = want ? 1 : 0;
    if (g_flags.noReload) *g_flags.noReload = g_noReload->on ? 1 : 0;
}

// True when the standalone TLOU Infinite Ammo mod (a version.dll in the game
// folder) is loaded. Both would patch the same ammo code, so the menu steps
// aside and leaves ammo to it.
bool StandaloneAmmoModLoaded() {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    HMODULE mods[1024];
    DWORD bytes = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &bytes)) return false;
    for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i) {
        wchar_t path[MAX_PATH];
        if (!GetModuleFileNameW(mods[i], path, MAX_PATH)) continue;
        const wchar_t* name = wcsrchr(path, L'\\');
        if (!name || _wcsicmp(name + 1, L"version.dll") != 0) continue;
        if (_wcsnicmp(path, exe, wcslen(exe)) != 0) continue; // the System32 copy
        // A version.dll from the game folder: is it the Infinite Ammo mod?
        const auto* base = reinterpret_cast<const uint8_t*>(mods[i]);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        static const char kTag[] = "TLOU Infinite Ammo ";
        const size_t size = nt->OptionalHeader.SizeOfImage;
        const auto* sec = IMAGE_FIRST_SECTION(nt);
        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
            if (!(sec[s].Characteristics & IMAGE_SCN_MEM_READ) || (sec[s].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
            const size_t lo = sec[s].VirtualAddress, hi = (std::min)(size, lo + sec[s].Misc.VirtualSize);
            for (size_t k = lo; k + sizeof(kTag) - 1 <= hi; ++k)
                if (std::memcmp(base + k, kTag, sizeof(kTag) - 1) == 0) return true;
        }
    }
    return false;
}

DWORD WINAPI Worker(LPVOID) {
    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        Apply();
        Sleep(100);
    }
    return 0;
}

} // namespace

void Register() {
    using features::Kind;
    g_infinite = &features::Add("InfiniteAmmo", "InfiniteAmmo", "Infinite Ammo",
                                "Never run out of ammo. Magazines still empty and you reload normally. Hotkey F7.",
                                Kind::Toggle, 1);
    g_noReload = &features::Add("NoReload", "InfiniteAmmo", "No Reload",
                                "Your magazine never goes down. Player only; enemies still reload. Hotkey F8.",
                                Kind::Toggle, 0);
    g_hud = &features::Add("ShowInfinitySign", "InfiniteAmmo", "Show \xE2\x88\x9E On HUD",
                           "Reserve ammo shows the infinity sign while Infinite Ammo is on. Applies on the next launch.",
                           Kind::Toggle, 1);
    g_hud->restartRequired = true;
    // The Infinite Ammo mod's keys are always saved, even with RememberToggles off.
    g_infinite->alwaysSave = g_noReload->alwaysSave = g_hud->alwaysSave = true;
}

void Init() {
    features::Feature& hud = *g_hud;
    if (StandaloneAmmoModLoaded()) {
        const char* why = "The TLOU Infinite Ammo mod is installed and handles ammo (F7/F8). "
                          "Uninstall it to use these options here.";
        logx::Write("standalone TLOU Infinite Ammo mod detected; menu ammo features stay off");
        features::MarkUnavailable(*g_infinite, why);
        features::MarkUnavailable(*g_noReload, why);
        features::MarkUnavailable(hud, why);
        return;
    }
    if (!game::Resolve(g_flags)) {
        const char* why = "Game code not found (unsupported game version?). See w1zsMenu.log.";
        features::MarkUnavailable(*g_infinite, why);
        features::MarkUnavailable(*g_noReload, why);
        features::MarkUnavailable(hud, why);
        return;
    }
    features::MarkAvailable(*g_infinite);
    if (g_flags.noReload) features::MarkAvailable(*g_noReload);
    else features::MarkUnavailable(*g_noReload, "Weapon fire code not found. See w1zsMenu.log.");

    // The HUD rewrite must happen before the HUD is first drawn, so it is a
    // startup-only setting.
    if (hud.on) {
        if (game::PatchHud(g_flags)) features::MarkAvailable(hud);
        else features::MarkUnavailable(hud, "HUD code not found; ammo is still infinite. See w1zsMenu.log.");
    } else {
        features::MarkAvailable(hud);
    }

    Apply();
    logx::Write("ammo active: InfiniteAmmo=%d NoReload=%d", g_infinite->on.load(), g_noReload->on.load());
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
}

void Stop() {
    // Called from DLL_PROCESS_DETACH (loader lock held): signal only, never wait.
    InterlockedExchange(&g_stop, 1);
}

} // namespace ammo
