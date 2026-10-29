#pragma once

namespace proxy {
// Loads System32\winmm.dll and points every export stub at it. Must run
// first thing in DLL_PROCESS_ATTACH. Returns false if the real DLL is missing.
bool LoadRealWinmm();
// Exports the real DLL did not have (they return 0 instead of crashing).
int MissingExports();
} // namespace proxy
