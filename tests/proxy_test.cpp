// Offline test for the winmm.dll proxy: every export of System32\winmm.dll
// must exist in build\winmm.dll with the same name and ordinal, and calls
// made through the proxy must reach the real DLL. Loading the proxy here is
// safe: the menu itself only starts inside tlou-i.exe / tlou-i-l.exe.
//
// usage: proxy_test.exe [path to proxy winmm.dll]   (default build\winmm.dll)
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...)                                     \
    do {                                                     \
        if (cond) { std::printf("  PASS  "); }               \
        else { std::printf("  FAIL  "); ++g_failures; }      \
        std::printf(__VA_ARGS__);                            \
        std::printf("\n");                                   \
    } while (0)

// ordinal -> name ("" for ordinal-only), read from the file on disk.
static bool ReadExports(const wchar_t* path, std::map<WORD, std::string>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    auto rvaToOff = [&](DWORD rva) -> size_t {
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + sec[i].SizeOfRawData)
                return rva - sec[i].VirtualAddress + sec[i].PointerToRawData;
        return 0;
    };
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) return false;
    auto* ed = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(file.data() + rvaToOff(dir.VirtualAddress));
    auto* funcs = reinterpret_cast<DWORD*>(file.data() + rvaToOff(ed->AddressOfFunctions));
    auto* names = reinterpret_cast<DWORD*>(file.data() + rvaToOff(ed->AddressOfNames));
    auto* ords = reinterpret_cast<WORD*>(file.data() + rvaToOff(ed->AddressOfNameOrdinals));
    for (DWORD i = 0; i < ed->NumberOfFunctions; ++i)
        if (funcs[i]) out[static_cast<WORD>(ed->Base + i)] = "";
    for (DWORD i = 0; i < ed->NumberOfNames; ++i)
        out[static_cast<WORD>(ed->Base + ords[i])] = reinterpret_cast<const char*>(file.data() + rvaToOff(names[i]));
    return true;
}

int wmain(int argc, wchar_t** argv) {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    lstrcatW(sys, L"\\winmm.dll");
    const wchar_t* proxyPath = argc > 1 ? argv[1] : L"build\\winmm.dll";

    std::printf("exports\n");
    std::map<WORD, std::string> real, proxy;
    CHECK(ReadExports(sys, real), "read System32 winmm.dll exports (%zu)", real.size());
    CHECK(ReadExports(proxyPath, proxy), "read proxy exports (%zu)", proxy.size());
    int missing = 0, renamed = 0;
    for (const auto& [ord, name] : real) {
        auto it = proxy.find(ord);
        if (it == proxy.end()) ++missing;
        else if (it->second != name) ++renamed;
    }
    CHECK(missing == 0 && renamed == 0 && proxy.size() == real.size(),
          "same names and ordinals (%d missing, %d different, %zu extra)", missing, renamed,
          proxy.size() > real.size() ? proxy.size() - real.size() : 0);

    std::printf("forwarding\n");
    HMODULE p = LoadLibraryW(proxyPath);
    CHECK(p != nullptr, "proxy loads (%lu)", p ? 0 : GetLastError());
    HMODULE r = GetModuleHandleW(sys);
    CHECK(r != nullptr && r != p, "proxy loaded the real System32 winmm.dll");
    if (!p || !r) return 1;

    using U = UINT(WINAPI*)();
    using T = DWORD(WINAPI*)();
    using P = UINT(WINAPI*)(UINT);
    auto pt = reinterpret_cast<T>(GetProcAddress(p, "timeGetTime"));
    auto rt = reinterpret_cast<T>(GetProcAddress(r, "timeGetTime"));
    const DWORD a = pt(), b = rt();
    CHECK(pt != rt && b - a < 1000, "timeGetTime through the proxy (%lu vs %lu)", a, b);
    auto pw = reinterpret_cast<U>(GetProcAddress(p, "waveOutGetNumDevs"));
    auto rw = reinterpret_cast<U>(GetProcAddress(r, "waveOutGetNumDevs"));
    CHECK(pw() == rw(), "waveOutGetNumDevs matches (%u)", rw());
    auto pj = reinterpret_cast<U>(GetProcAddress(p, "joyGetNumDevs"));
    auto rj = reinterpret_cast<U>(GetProcAddress(r, "joyGetNumDevs"));
    CHECK(pj() == rj(), "joyGetNumDevs matches (%u)", rj());
    auto pb = reinterpret_cast<P>(GetProcAddress(p, "timeBeginPeriod"));
    auto pe = reinterpret_cast<P>(GetProcAddress(p, "timeEndPeriod"));
    CHECK(pb(1) == 0 && pe(1) == 0, "timeBeginPeriod / timeEndPeriod through the proxy (TIMERR_NOERROR)");
    for (const auto& [ord, name] : real) {
        if (!name.empty()) continue;
        FARPROC po = GetProcAddress(p, MAKEINTRESOURCEA(ord));
        CHECK(po != nullptr, "ordinal-only export #%u is exported", ord);
    }

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
