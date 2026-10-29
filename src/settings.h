// Settings stored in w1zsMenu.ini next to the game executable. Reads and
// writes go straight to the file (Get/WritePrivateProfileString), so every
// change made in the menu is saved immediately.
//
// The Infinite Ammo mod's keys live on unchanged in the [InfiniteAmmo]
// section. If w1zsMenu.ini does not exist yet but TLOUInfiniteAmmo.ini does,
// those keys are imported once.
#pragma once
#include <string>

namespace settings {

void Init(const wchar_t* iniPath, const wchar_t* legacyIniPath);

bool GetBool(const char* section, const char* key, bool def);
int GetInt(const char* section, const char* key, int def);
std::string GetString(const char* section, const char* key, const char* def);

void SetBool(const char* section, const char* key, bool value);
void SetInt(const char* section, const char* key, int value);
void SetString(const char* section, const char* key, const std::string& value);

// True when Init created the file (first run or imported from the old mod).
bool CreatedThisRun();
bool ImportedLegacy();

} // namespace settings
