#include "settings.h"

#include <windows.h>
#include <mutex>

namespace settings {

namespace {

std::wstring g_ini;
bool g_moved = false;
std::mutex g_lock;
bool g_created = false;
bool g_imported = false;

std::wstring Wide(const char* s) {
    std::wstring w;
    while (*s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s++)));
    return w;
}

void WriteRaw(const wchar_t* ini, const char* section, const char* key, const std::wstring& value) {
    WritePrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), value.c_str(), ini);
}

// Default file contents for a first run. Comments explain every key; values
// are the shipped defaults. Kept in sync with dist/w1zsMenu.ini.
const char kDefaultIni[] =
    "; w1z's menu - settings. The menu saves changes here automatically.\r\n"
    "\r\n"
    "[Menu]\r\n"
    "; Keys that open/close the menu (Windows virtual-key codes, 0 = off).\r\n"
    "; 45 = Insert, 120 = F9, 189 = '-'\r\n"
    "MenuKey=45\r\n"
    "MenuKey2=120\r\n"
    "; 1 = L3 + R3 on a controller opens/closes the menu.\r\n"
    "ControllerToggle=1\r\n"
    "; 1 = pause the game while the menu is open.\r\n"
    "PauseWhileOpen=1\r\n"
    "; 1 = feature toggles are remembered for the next launch.\r\n"
    "RememberToggles=1\r\n"
    "; 1 = Play as Ellie / Joel are remembered too (the swap applies when a game loads).\r\n"
    "RememberSwap=0\r\n"
    "; Menu size in percent (75-200).\r\n"
    "Scale=100\r\n"
    "; 1 = small on-screen message when a hotkey toggles something.\r\n"
    "Notifications=1\r\n"
    "Favorites=\r\n"
    "\r\n"
    "[InfiniteAmmo]\r\n"
    "; Same keys as the TLOU Infinite Ammo mod.\r\n"
    "InfiniteAmmo=1\r\n"
    "ShowInfinitySign=1\r\n"
    "NoReload=0\r\n"
    "; 118 = F7, 119 = F8, 0 = no hotkey\r\n"
    "ToggleInfiniteAmmoKey=118\r\n"
    "ToggleNoReloadKey=119\r\n"
    "BeepOnToggle=1\r\n"
    "Logging=1\r\n";

const char* kLegacyKeys[] = {"InfiniteAmmo", "ShowInfinitySign", "NoReload", "ToggleInfiniteAmmoKey",
                             "ToggleNoReloadKey", "BeepOnToggle", "Logging"};

} // namespace

void Init(const wchar_t* iniPath, const wchar_t* oldIniPath, const wchar_t* legacyIniPath) {
    g_ini = iniPath;
    if (GetFileAttributesW(iniPath) != INVALID_FILE_ATTRIBUTES) return;
    // The folder (Documents\w1z's menu) may not exist yet.
    std::wstring dir(iniPath);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    CreateDirectoryW(dir.c_str(), nullptr);
    if (oldIniPath && wcscmp(oldIniPath, iniPath) != 0 && GetFileAttributesW(oldIniPath) != INVALID_FILE_ATTRIBUTES &&
        CopyFileW(oldIniPath, iniPath, TRUE)) {
        g_moved = true;
        return;
    }

    HANDLE f = CreateFileW(iniPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, kDefaultIni, sizeof(kDefaultIni) - 1, &written, nullptr);
    CloseHandle(f);
    g_created = true;

    // Carry over the Infinite Ammo mod's settings so they keep working.
    if (legacyIniPath && GetFileAttributesW(legacyIniPath) != INVALID_FILE_ATTRIBUTES) {
        for (const char* key : kLegacyKeys) {
            wchar_t buf[64];
            const DWORD n = GetPrivateProfileStringW(L"InfiniteAmmo", Wide(key).c_str(), L"\x01", buf, 64, legacyIniPath);
            if (n > 0 && buf[0] != L'\x01') WriteRaw(iniPath, "InfiniteAmmo", key, buf);
        }
        g_imported = true;
    }
}

const std::wstring& Path() { return g_ini; }
bool MovedFromGameFolder() { return g_moved; }

bool GetBool(const char* section, const char* key, bool def) {
    return GetInt(section, key, def ? 1 : 0) != 0;
}

int GetInt(const char* section, const char* key, int def) {
    std::lock_guard<std::mutex> l(g_lock);
    return static_cast<int>(GetPrivateProfileIntW(Wide(section).c_str(), Wide(key).c_str(), def, g_ini.c_str()));
}

std::string GetString(const char* section, const char* key, const char* def) {
    std::lock_guard<std::mutex> l(g_lock);
    wchar_t buf[2048];
    GetPrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), Wide(def).c_str(), buf, 2048, g_ini.c_str());
    std::string s;
    for (const wchar_t* p = buf; *p; ++p) s.push_back(*p < 128 ? static_cast<char>(*p) : '?');
    return s;
}

void SetBool(const char* section, const char* key, bool value) { SetInt(section, key, value ? 1 : 0); }

void SetInt(const char* section, const char* key, int value) {
    std::lock_guard<std::mutex> l(g_lock);
    WriteRaw(g_ini.c_str(), section, key, std::to_wstring(value));
}

void SetString(const char* section, const char* key, const std::string& value) {
    std::lock_guard<std::mutex> l(g_lock);
    WriteRaw(g_ini.c_str(), section, key, Wide(value.c_str()));
}

bool CreatedThisRun() { return g_created; }
bool ImportedLegacy() { return g_imported; }

} // namespace settings
