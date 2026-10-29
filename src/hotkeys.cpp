#include "hotkeys.h"

#include "features.h"
#include "log.h"
#include "settings.h"

#include <windows.h>
#include <atomic>

namespace hotkeys {

namespace {

DWORD g_threadId = 0;
std::atomic<bool> g_menuToggle{false};

enum : int { kMenu1 = 1, kMenu2, kInfinite, kNoReload };

bool GameHasFocus() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void RegisterKey(int id, int vk, const char* what) {
    if (vk <= 0 || vk > 0xFE) return;
    if (RegisterHotKey(nullptr, id, MOD_NOREPEAT, static_cast<UINT>(vk)))
        logx::Write("hotkey for %s registered (vk 0x%X)", what, vk);
    else
        logx::Write("hotkey for %s unavailable: key 0x%X is taken by another program (%lu)", what, vk, GetLastError());
}

void ToggleFeature(const char* id) {
    features::Feature* f = features::Find(id);
    if (!f || !f->available) return;
    features::SetToggle(*f, !f->on, true);
    if (settings::GetBool("InfiniteAmmo", "BeepOnToggle", true)) MessageBeep(f->on ? MB_OK : MB_ICONHAND);
}

DWORD WINAPI Thread(LPVOID) {
    RegisterKey(kMenu1, settings::GetInt("Menu", "MenuKey", VK_INSERT), "menu");
    RegisterKey(kMenu2, settings::GetInt("Menu", "MenuKey2", VK_F9), "menu (second key)");
    // F7/F8 only when the menu runs the ammo features (not when the
    // standalone Infinite Ammo mod is installed and owns those keys).
    auto loaded = [](const char* id) { features::Feature* f = features::Find(id); return f && f->available; };
    if (loaded("InfiniteAmmo"))
        RegisterKey(kInfinite, settings::GetInt("InfiniteAmmo", "ToggleInfiniteAmmoKey", VK_F7), "InfiniteAmmo");
    if (loaded("NoReload"))
        RegisterKey(kNoReload, settings::GetInt("InfiniteAmmo", "ToggleNoReloadKey", VK_F8), "NoReload");
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message != WM_HOTKEY || !GameHasFocus()) continue;
        switch (msg.wParam) {
        case kMenu1:
        case kMenu2: g_menuToggle = true; break;
        case kInfinite: ToggleFeature("InfiniteAmmo"); break;
        case kNoReload: ToggleFeature("NoReload"); break;
        }
    }
    for (int id = kMenu1; id <= kNoReload; ++id) UnregisterHotKey(nullptr, id);
    return 0;
}

} // namespace

void Start() {
    HANDLE t = CreateThread(nullptr, 0, Thread, nullptr, 0, &g_threadId);
    if (t) CloseHandle(t);
}

void Stop() {
    if (g_threadId) PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
}

bool TakeMenuToggle() { return g_menuToggle.exchange(false); }

} // namespace hotkeys
