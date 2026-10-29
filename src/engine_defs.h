// Engine features that come from Naughty Dog's developer menu, located by the
// exact label of their developer-menu item (see dscan.h). Shared by the menu
// (engine.cpp) and the offline test (tests/scan_test.cpp) so the test checks
// exactly what the menu will patch.
#pragma once
#include <cstddef>

namespace engine {

// How the item's value is found.
enum class Src {
    BoolR8,    // ItemBool: lea r8, [flag]
    BoolCbR9,  // ItemBool with callback: lea r9, [flag]
    Callback3, // ItemBool with callback: lea r8, [callback] whose first compares are the flags (Undetectable)
};

// A toggle the menu drives by pointing every read of the engine's debug flag
// at a private byte (the game resets its debug block, so writing the flag
// itself does not stick). `reads` is the exact number of reads verified for
// this game build; any other count means the code changed and the feature
// stays off.
struct FlagDef {
    const char* id;      // feature id / ini key
    const char* label;   // developer-menu label, exact
    Src src;
    int reads[3];        // per flag (Callback3 has three flags), verified for tlou-i.exe and tlou-i-l.exe 1.1.4.0
};

inline const FlagDef kFlags[] = {
    {"GodMode", "Invincible Player", Src::BoolCbR9, {15}},
    {"NoHitReactions", "  No Hit Reactions When Invincible", Src::BoolCbR9, {1}},
    {"InfiniteSprint", "Infinite Sprint", Src::BoolR8, {1}},
    {"Undetectable", "Undetectable Player", Src::Callback3, {11, 6, 7}},
    {"OneHitKills", "One Hit Kills", Src::BoolR8, {1}},
    {"InfiniteWeaponHealth", "Infinite Weapon Health", Src::BoolR8, {2}},
    {"NoWeaponSway", "  Disable Weapon Sway", Src::BoolR8, {4}},
    {"NoAutoReload", "Disable Auto Reload", Src::BoolR8, {3}},
    {"NoAutoHolster", "Disable Auto Holster", Src::BoolR8, {1}},
    {"InfiniteCrafting", "Infinite Crafting", Src::BoolR8, {5}},
    {"InfiniteCraftables", "Infinite Craftables", Src::BoolR8, {1}},
    {"AllChapters", "Give All Chapters", Src::BoolR8, {9}},
    {"AllEncounters", "Give All Encounters", Src::BoolR8, {1}},
    {"AllExtras", "Unlock All Extras Content", Src::BoolR8, {33}},
};

// Developer-menu functions the menu runs on the game thread: callback(item, 5)
// with the item's data (offset 0x60) set to `data` (a StringId64 hash of the
// given text, or 0).
struct ActionDef {
    const char* id;
    const char* label;     // developer-menu label, exact
    const char* dataText;  // hashed into the item's data; nullptr = 0
    bool boolCallback;     // ItemBool-with-callback (else ItemFunction)
};

inline const ActionDef kActions[] = {
    {"DropAllWeapons", "Drop All Weapons", nullptr, true},
    {"GiveEachCraftable", "Give Each Craftable", nullptr, false},
    {"GiveAllInventory", "Give All Inventory and Upgrades", nullptr, false},
    {"Parts10", "t1x-parts x 10", "t1x-parts x 10", false},
    {"Parts100", "t1x-parts x 10", "t1x-parts x 100", false},
    {"Pills10", "t1x-player-upgrade-supplements x 10", "t1x-player-upgrade-supplements x 10", false},
    {"Pills100", "t1x-player-upgrade-supplements x 100", "t1x-player-upgrade-supplements x 100", false},
    {"AllRecipes", "All Recipes", "all", true},
    {"NoRecipes", "No Recipes", "none", true},
    {"TeleportToCamera", "Teleport Player", nullptr, false},
    {"ReplayScene", "Re-play current task", nullptr, true},
    {"GiveTreasures", "Give all Treasures", nullptr, false},
    {"RemoveTreasures", "Remove all Treasures", nullptr, false},
    {"GiveJournal", "Give all Journal Pages", nullptr, false},
    {"ResetJournal", "Reset Journal", nullptr, false},
};

// Items given through the developer menu's own give-item callback (the one
// behind "t1x-parts x 10"); names are ones the game executable contains.
struct GiveDef {
    const char* id;
    const char* name; // game item id
    const char* label;
    int group; // 0 Joel, 1 Ellie, 2 melee, 3 items
};

inline const GiveDef kGives[] = {
    // Names from the game's weapon-art data; each is confirmed in-game with the
    // engine's weapon lookup before it can be used.
    {"GiveJoelPistol", "t1x-pistol-9mm", "9mm Pistol", 0},
    {"GiveJoelRevolver", "t1x-pistol-revolver", "Revolver", 0},
    {"GiveJoelElDiablo", "t1x-pistol-uber", "El Diablo", 0},
    {"GiveJoelShorty", "t1x-pistol-shotgun", "Shorty", 0},
    {"GiveJoelShotgun", "t1x-shotgun-pump", "Shotgun", 0},
    {"GiveJoelRifle", "t1x-rifle-bolt", "Hunting Rifle", 0},
    {"GiveJoelSemiAuto", "t1x-rifle-sub-mounted", "Semi-Auto Rifle", 0},
    {"GiveJoelBow", "t1x-bow", "Bow", 0},
    {"GiveJoelFlamethrower", "t1x-flamethrower", "Flamethrower", 0},
    {"GiveElliePistol", "t1x-pistol-ellie", "Pistol", 1},
    {"GiveEllieBow", "t1x-bow-ellie", "Bow", 1},
    {"GiveEllieRifle", "t1x-rifle-bolt-m24", "Hunting Rifle", 1},
    {"GiveLeadPipe", "t1x-lead-pipe", "Lead Pipe", 2},
    {"GiveTwoByFour", "t1x-two-by-four", "2x4", 2},
    {"GiveBat", "t1x-baseball-bat", "Baseball Bat", 2},
    {"GiveMachete", "t1x-machete", "Machete", 2},
    {"GiveHatchet", "t1x-hatchet", "Hatchet", 2},
    {"GiveSwitchblade", "t1x-switchblade", "Switchblade", 2},
    {"GiveShiv", "t1x-shiv", "Shiv", 3},
    {"GiveHealthKit", "t1x-health-kit", "Health Kit", 3},
    {"GiveMolotov", "t1x-molotov", "Molotov", 3},
    {"GiveNailBomb", "t1x-nail-bomb", "Nail Bomb", 3},
    {"GiveSmokeBomb", "t1x-smoke-bomb", "Smoke Bomb", 3},
    {"GiveBrick", "t1x-brick", "Brick", 3},
    {"GiveBottle", "t1x-bottle", "Bottle", 3},
    {"GiveAlcohol", "t1x-alcohol", "Alcohol", 3},
    {"GiveRag", "t1x-rag", "Rag", 3},
    {"GiveBlade", "t1x-blade", "Blade", 3},
    {"GiveBinding", "t1x-binding", "Binding", 3},
    {"GiveExplosive", "t1x-explosive", "Explosive", 3},
};

// A weapon every game has; when the lookup finds it, the weapon list is loaded.
inline const char* kWeaponProbe = "t1x-pistol-revolver";

// Weapons (groups 0-2) are given through the give-weapon callback and are
// only offered when the game's weapon lookup knows the name; items (group 3)
// go through the give-item callback.
inline bool IsWeapon(const GiveDef& g) { return g.group <= 2; }

// Prologue of GameLoopUpdate(unsigned __int64), the per-frame game loop the
// menu runs queued actions from. Found through its profiling name string.
inline const char* kGameLoopName = "void __cdecl GameLoopUpdate(unsigned __int64)";
inline const char* kGameLoopPrologue =
    "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 48 8D A8";

// Give-weapon callback prologue; it is the function that reads the
// "Sync Load Weapons" flag, and it calls the weapon lookup right after
// loading the item data: mov rbx,[rbx+60h]; mov rcx,rbx; call lookup.
inline const char* kGiveWeaponPrologue =
    "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC";
inline const char* kWeaponLookupCall = "48 8B 5B 60 48 8B CB E8";

} // namespace engine
