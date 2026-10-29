#include "log.h"

#include <windows.h>
#include <share.h>
#include <cstdarg>
#include <cstdio>

namespace logx {

namespace {
FILE* g_file = nullptr;
CRITICAL_SECTION g_lock;
bool g_lockInit = false;
} // namespace

void Open(const wchar_t* path, bool enabled) {
    if (!enabled || g_file) return;
    InitializeCriticalSection(&g_lock);
    g_lockInit = true;
    // Shared so the log can be opened while the game is running.
    g_file = _wfsopen(path, L"w", _SH_DENYNO);
}

void Write(const char* fmt, ...) {
    if (!g_file) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_lock);
    std::fprintf(g_file, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_file, fmt, args);
    va_end(args);
    std::fputc('\n', g_file);
    std::fflush(g_file);
    LeaveCriticalSection(&g_lock);
}

void Close() {
    if (!g_file) return;
    EnterCriticalSection(&g_lock);
    std::fclose(g_file);
    g_file = nullptr;
    LeaveCriticalSection(&g_lock);
}

} // namespace logx
