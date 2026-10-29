// Minimal file logger: w1zsMenu.log next to the game executable,
// truncated on every launch. Disabled entirely when Logging=0 in the ini.
#pragma once

namespace logx {
void Open(const wchar_t* path, bool enabled);
void Write(const char* fmt, ...);
void Close();
} // namespace logx
