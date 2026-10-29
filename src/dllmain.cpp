#include "ammo.h"
#include "engine.h"
#include "features.h"
#include "game.h"
#include "hotkeys.h"
#include "input.h"
#include "log.h"
#include "menu.h"
#include "pause.h"
#include "proxy_winmm.h"
#include "render.h"
#include "settings.h"
#include "version.h"

#include "../third_party/minhook/include/MinHook.h"

#include <windows.h>

namespace {

// Builds "<game folder>\<file>" from the host executable's path.
bool GamePath(const wchar_t* file, wchar_t* out, DWORD cap) {
    const DWORD n = GetModuleFileNameW(nullptr, out, cap);
    if (n == 0 || n >= cap) return false;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    return wcscat_s(out, cap, file) == 0;
}

DWORD WINAPI Startup(LPVOID) {
    // Every feature is registered first so the menu can show all of them
    // (greyed out) while the signature scans run.
    ammo::Register();
    engine::Register();
    pause::Init();
    menu::Init();

    // Hooks go in before the game creates its swapchain.
    const bool mh = MH_Initialize() == MH_OK;
    if (!mh) {
        logx::Write("ERROR: MinHook could not initialise; the menu is disabled. Ammo features still load.");
    } else {
        render::Install();
        input::Install();
    }

    ammo::Init();
    if (mh) engine::Init(); // needs MinHook for the game-loop hook
    else logx::Write("engine features stay off: MinHook is not available");
    hotkeys::Start(); // after ammo::Init: F7/F8 are only registered when the ammo features loaded
    int loaded, total;
    features::Counts(loaded, total);
    logx::Write("startup complete: %d of %d features loaded", loaded, total);
    return 0;
}

void Init() {
    // winmm.dll is loaded by other processes in the game folder too
    // (launcher, crash handler); they only get the forwarding.
    if (!game::IsSupportedHost()) return;

    wchar_t ini[MAX_PATH], legacy[MAX_PATH], log[MAX_PATH];
    if (!GamePath(L"w1zsMenu.ini", ini, MAX_PATH) || !GamePath(L"TLOUInfiniteAmmo.ini", legacy, MAX_PATH) ||
        !GamePath(L"w1zsMenu.log", log, MAX_PATH))
        return;

    settings::Init(ini, legacy);
    logx::Open(log, settings::GetBool("InfiniteAmmo", "Logging", true));
    logx::Write("w1z's menu " MENU_VERSION " loaded (winmm.dll)");
    if (proxy::MissingExports())
        logx::Write("warning: %d winmm exports not found in System32\\winmm.dll", proxy::MissingExports());
    if (settings::CreatedThisRun())
        logx::Write("created w1zsMenu.ini%s", settings::ImportedLegacy() ? " (settings imported from TLOUInfiniteAmmo.ini)" : "");

    // Loader lock is held here: do the real work on a thread.
    HANDLE t = CreateThread(nullptr, 0, Startup, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    else logx::Write("ERROR: could not create startup thread (%lu)", GetLastError());
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(inst);
        if (!proxy::LoadRealWinmm()) return FALSE; // without the real DLL the game cannot run
        Init();
        break;
    case DLL_PROCESS_DETACH:
        ammo::Stop();
        hotkeys::Stop();
        break;
    }
    return TRUE;
}
