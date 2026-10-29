// winmm.dll proxy. The game imports WINMM.dll, it is not a KnownDLL, and
// Windows looks in the game folder before System32, so dropping this DLL
// there loads the menu with no third-party loader. Every export (same names
// and ordinals, see winmm.def) is a stub in winmm_stubs.asm that jumps
// through g_winmmTable to the real System32 winmm.dll.
//
// The other executables in the game folder (launcher, crash handler) load
// this DLL too, so forwarding always works; the menu itself only starts in
// tlou-i.exe / tlou-i-l.exe.
#include "proxy_winmm.h"

#include <windows.h>

extern "C" void* g_winmmTable[];

namespace {

struct Export {
    const char* name; // nullptr = ordinal-only
    WORD ordinal;
};

const Export kExports[] = {
#include "winmm_exports.inc"
};

// Used for an export the system DLL lacks (a future Windows could drop one).
extern "C" UINT_PTR WINAPI MissingExport() {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return 0;
}

int g_missing = 0;

} // namespace

namespace proxy {

bool LoadRealWinmm() {
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 12) return false;
    lstrcatW(path, L"\\winmm.dll");
    // Runs from DllMain, before anything can call an export. winmm's own
    // dependencies are system DLLs that are already loaded at this point.
    HMODULE real = LoadLibraryW(path);
    for (size_t i = 0; i < sizeof(kExports) / sizeof(kExports[0]); ++i) {
        FARPROC p = nullptr;
        if (real) p = GetProcAddress(real, kExports[i].name ? kExports[i].name : MAKEINTRESOURCEA(kExports[i].ordinal));
        if (!p) ++g_missing;
        g_winmmTable[i] = p ? reinterpret_cast<void*>(p) : reinterpret_cast<void*>(&MissingExport);
    }
    return real != nullptr;
}

int MissingExports() { return g_missing; }

} // namespace proxy
