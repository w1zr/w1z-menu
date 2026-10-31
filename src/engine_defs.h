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

namespace engine {

// Character swap. The developer menu's "Spawn Player" item toggles the player:
// it despawns an existing player, or spawns one at the camera through the
// player-spawn function, passing a character id (0 = the default, t1x-joel).
// That function calls the core spawn every player spawn goes through
// (checkpoints, deaths, chapter loads) with the character id as its fifth
// argument; the menu swaps that id.
inline const char* kSpawnPlayerLabel = "Spawn Player";
// In the callback: mov r8,[rip+id]; lea rcx,[rsp+20h]; xor r9d,r9d; call spawn
inline const char* kSpawnCall = "4C 8B 05 ?? ?? ?? ?? 48 8D 4C 24 20 45 33 C9 E8";
// In the spawn function: movabs r14, <default character id>
inline const char* kDefaultCharacter = "49 BE";
// ... and: mov [rsp+40h], rdi; call core
inline const char* kCoreCall = "48 89 7C 24 40 E8";
inline const char* kCorePrologue = "40 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? B8 ?? ?? ?? ?? E8";
inline const char* kJoel = "t1x-joel";
inline const char* kEllie = "t1x-ellie";

// Start of the Give Each Craftable callback: sub rsp,28h; lea rcx,[rip+player
// handle]; mov r9d,edx; call player lookup.
inline const char* kPlayerLookupStart = "48 83 EC 28 48 8D 0D ?? ?? ?? ?? 44 8B CA E8";

// GameRestartCheckpoint(bool, bool): the pause menu's Restart Checkpoint calls
// it with (false, false). Found through its profiling name string.
inline const char* kRestartName = "bool __cdecl GameRestartCheckpoint(bool,bool)";
inline const char* kRestartPrologue = "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 60 20 55 41 56 41 57 48 8D 68 A1";

// Player animation packages. A chapter loads only its own player's level set
// (joel-player-all / ellie-player-all in level-sets-common), so a swapped
// player is missing its standing movement, aiming, throwing and so on. These
// are the actor packages in one character's set (plus traversal and water)
// that the other character's set does not load, from the game's level-sets
// data (anim-ellie-player-water-u4 is listed there but is not a PC package).
// The menu loads them through the engine's own package loader, and only the
// ones in the game's build\pc\main\actor97.txt: asking for a package the
// game has no definition for trips an assert in LevelDefCollection.
//
// At a call site in the game: mov rcx,[rip+package manager]; call
// IsLoadedOrLoading(mgr, FNV-1a name hash); test al,al; jnz; mov r8b,1;
// xor edx,edx; mov rcx,rdi; call LoadPackage(name, 0, flag).
inline const char* kPackageCheckCall = "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 75 0D 41 B0 01 33 D2 48 8B CF E8";
inline const char* kLoadPackagePrologue = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 50 41 BF 02 00 00 00";
// IsLoadedOrLoading: state 11 or 12. IsLoaded (state 12) follows 0x44 later.
inline const char* kPackageLoadingCheck = "48 83 EC 28 E8 ?? ?? ?? ?? 48 85 C0 74 0F 8B 40 40 B9 01 00 00 00 83 C0 F5";
inline const char* kPackageLoadedCheck = "48 83 EC 28 E8 ?? ?? ?? ?? 48 8B C8 33 C0 48 85 C9 74 08 83 79 40 0C";
inline constexpr size_t kPackageLoadedOffset = 0x44;

// The developer console's LoadLevelSet command is registered as
//     lea r9, [args]; lea rdx, ["LoadLevelSet"]; lea rcx, [handler]; jmp register
// and its handler calls LevelMgr::SetLevelSet(g_levelMgr, hash, true, true).
// SetLevelSet rebuilds the want-load list from the current level set's tree
// with the function behind kRebuildWantsCall. Calling that rebuild ourselves
// makes the game stream in the sets a patched record adds, during normal play.
inline const char* kLevelSetCommand = "LoadLevelSet";
inline const char* kConsoleRegister = "4C 8D 0D ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E9";
inline const char* kSetLevelSetCall = "41 B1 01 48 8B 0D ?? ?? ?? ?? 45 8A C1 E8";
inline const char* kSetLevelSetPrologue =
    "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D 6C 24 90";
inline const char* kRebuildWantsCall = "44 38 74 24 32 74 08 49 8B CD E8";
// Crafting for Ellie. The crafting controller's "crafting allowed" check lets
// an Ellie player craft only while the Winter world "lakeside" is loaded, or
// when a game flag (off by default, read only here) is set; that is why every
// Ellie swap could only craft in Winter. kEllieCraftFlagRead is the flag read
// right after the lakeside test (the "lakeside" string must be referenced in
// the 0x80 bytes before it).
inline const char* kEllieCraftLevel = "lakeside";
inline const char* kEllieCraftFlagRead = "40 84 F6 75 09 40 38 35 ?? ?? ?? ?? 74 03 40 8A F7 40 84 ED";
inline const char* kRebuildWantsPrologue = "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 48 8D A8 E8 35 FF FF B8 10 CB 00 00";

// Joel as the AI companion. Every character's look (e.g. t1x-ellie-costume-02)
// is resolved by name hash in this function (rdx), after the game's own look
// remap list and before its look2 data is read. The companion's look comes
// from its spawner, so changing the hash here gives the same buddy (its AI,
// scripts and dialogue) Joel's body.
inline const char* kLookResolve =
    "48 89 5C 24 10 48 89 6C 24 18 56 57 41 54 41 56 41 57 48 83 EC 70 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 60 "
    "48 8B F9 48 8D 4C 24 20 E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 4C 8D 4C 24 50 45 33 FF 48 8D 54 24 48 48 8B 18 4C "
    "89 7C 24 50 4C 8B C3 4C 89 7C 24 58 48 8B 01 FF 90 80 02 00 00";
// What the game loads for Joel as a companion in the epilogue, where Ellie is
// the player and Joel follows her (level-sets-lab: tsk-lab-3-post-fence). His
// body, facial and gesture animations are already in Joel's chapters.
inline const char* const kJoelBuddySets[] = {"npc-buddy-all"};
inline const char* const kJoelBuddyPackages[] = {"t1x-anim-npc-joel", "anim-npc-m-buddy-ambi"};

// Research: the player's "can enter the crafting menu" check, reached from its
// script function. The body pattern covers every offset LogCraftingCheck reads.
inline const char* kCanCraftName = "player-can-enter-crafting-menu?";
inline const char* kScriptFuncRegister = "48 83 EC 28 4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8D 0D";
inline const char* kCanCraftWrapper = "48 83 EC 28 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B C8 33 C0 48 85 C9 74 05 E8";
inline const char* kCanCraftBody =
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 48 8B D9 48 8B 89 58 33 00 00 48 8B 01 "
    "FF 90 E0 01 00 00 48 85 C0 0F 85 43 02 00 00 48 39 83 20 5F 00 00 0F 85 36 02 00 00 48 8B 8B C0 32 00 00 48 8B 01 "
    "FF 50 60 84 C0 0F 85 21 02 00 00 41 B8 06 00 00 00 48 8D 0D ?? ?? ?? ?? 48 BE 25 23 22 84 E4 9C F2 CB 49 BE B3 01 "
    "00 00 00 01 00 00 48 8B D6 41 8D 78 FB 48 0F BE 01 48 03 CF 48 33 D0 49 0F AF D6 4C 2B C7 75 ED 48 8B CB E8 ?? ?? "
    "?? ?? 84 C0 0F 85 D6 01 00 00 48 8B 83 30 33 00 00 80 B8 20 01 00 00 00 0F 85 C2 01 00 00 C5 FA 10 05 ?? ?? ?? ?? "
    "C5 F8 2F 80 10 02 00 00 0F 87 AC 01 00 00 48 8B D6 48 8D 0D ?? ?? ?? ?? 41 B8 0D 00 00 00 48 0F BE 01 48 03 CF 48 "
    "33 D0 49 0F AF D6 4C 2B C7 75 ED 48 8B CB E8 ?? ?? ?? ?? 84 C0 0F 85 79 01 00 00 48 8B 8B F0 32 00 00 48 8B 01 FF "
    "90 A8 02 00 00 84 C0 0F 85 61 01 00 00 48 8B CB E8 ?? ?? ?? ?? 84";
// 49 packages
inline const char* const kEllieOnlyPackages[] = {
    "t1x-rifle-bolt-m24", "t1x-bow-ellie", "pistol-revolver-hunter", "rifle-bolt-hunter", "shotgun-pump-hunter",
    "anim-ellie-player-mm-explore", "anim-ellie-player-mm-uneasy", "anim-shared-player-mm-explore-scripted-goto",
    "anim-shared-player-mm-crouch-scripted-goto", "anim-t1x-ellie-inspect", "anim-t1x-ellie-inspect-backpack",
    "t1x-anim-ellie-player-scavenge", "t1x-anim-ellie-player", "joel-skel", "t1x-joel-skel", "base-female-skel",
    "prop-backpack-ellie-skel", "prop-backpack-ellie", "prop-backpack-ellie-02", "prop-backpack-ellie-03",
    "craft-backpack-ellie-skel", "craft-backpack-ellie", "craft-backpack-ellie-02", "craft-backpack-ellie-03",
    "anim-ellie-player-hands", "anim-ellie-t1x-facial", "t1x-anim-ellie-player-gestures", "anim-exhausted-breathing",
    "anim-shared-player-healing", "anim-ellie-player-throw", "t1x-anim-ellie-player-mm-aim", "t1x-flashlight-ellie",
    "t1x-anim-ellie-player-mm-cover", "anim-ellie-player-mm-tense", "anim-ellie-player-mm-tense-shared",
    "anim-ellie-player-mm-melee", "t1x-anim-ellie-melee-common", "anim-melee-ellie-t2", "t1x-anim-ellie-v-thug-melee",
    "t1x-anim-ellie-v-inf-melee", "t1x-anim-ellie-v-inf-dlc-melee", "anim-ellie-player-balance",
    "anim-ellie-lose-balance", "anim-ellie-player-ledge-walk", "t1x-anim-ellie-player-mm-shimmy",
    "anim-ellie-player-ladder", "anim-ellie-player-ladder-carry", "anim-ellie-player-mm-wade",
    "t1x-anim-ellie-pistol", // her animations for Joel's 9mm (see character.cpp)
};
// 30 packages
inline const char* const kJoelOnlyPackages[] = {
    "anim-t1x-joel-player-mm-balance", "anim-t1x-joel-player-mm-crouch", "anim-t1x-joel-player-mm-explore",
    "anim-t1x-joel-player-mm-uneasy", "t1x-anim-joel-relaxed", "t1x-anim-joel-relaxed-sprint",
    "anim-t1x-joel-player-mm-explore-goto", "anim-t1x-joel-player-mm-uneasy-goto", "t1x-anim-joel-gestures",
    "abby-collision", "prop-backpack-joel", "prop-backpack-joel-02", "prop-backpack-joel-03",
    "prop-backpack-joel-skel", "anim-t1x-joel-player-mm-aim", "anim-t1x-joel-player-facial", "anim-joel-facial-idles",
    "anim-t1x-joel-player-safe", "t1x-anim-joel-melee", "anim-t1x-joel-player-ladder",
    "anim-t1x-joel-player-ladder-carry", "anim-t1x-joel-player-mm-shimmy", "anim-t1x-joel-player-mm-cover",
    "anim-t1x-joel-player-mm-tense", "t1x-anim-joel-stealth", "t1x-anim-npc-melee", "anim-melee-player-shared-inf-t2",
    "t1x-anim-joel-swim", "anim-t1x-joel-player-mm-wading", "t1x-anim-joel-swim-push",
};

// Outfit characters in the game's character data (spawn-character.bin).
// Joel's chapter outfit N becomes Ellie's outfit N (or her closest earlier one).
inline const int kEllieOutfits[] = {1, 2, 3, 4, 5, 6, 7, 8, 11, 13, 15};
inline const int kEllieNoBackpack[] = {1, 2, 3, 8};
inline const int kJoelOutfits[] = {1, 2, 3, 4, 5, 7, 14, 15, 16};
inline const int kJoelNoBackpack[] = {2, 4, 14, 15, 16};


// Weapon upgrades. The script functions player-weapon-upgrade-apply-bundle /
// -remove-bundle take (player, weapon id, bundle id): the weapon in the
// inventory gets the bundle, or it is kept for when the player picks it up.
// Bundle names per weapon come from the game's *weapon-upgrade-bundles* map
// (dc1/weapon-upgrades.bin). A "-N" bundle is applied with its lower levels.
inline const char* kUpgradeApplyName = "player-weapon-upgrade-apply-bundle";
inline const char* kUpgradeRemoveName = "player-weapon-upgrade-remove-bundle";
inline const char* kUpgradeApplyStart =
    "48 85 C9 0F 84 ?? ?? ?? ?? 4C 8B DC 49 89 5B 08 49 89 6B 10 49 89 73 18 57 48 83 EC 30 48 8B F1 49 8B F8 "
    "48 8B 89 F8 2F 00 00";
inline const char* kUpgradeRemoveStart =
    "48 85 C9 0F 84 ?? ?? ?? ?? 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B 89 F8 2F 00 00";

struct UpgradeWeapon {
    const char* id;       // menu id part
    const char* label;    // menu page title
    const char* weapon;   // weapon id
    const char* bundles;  // space-separated bundle ids, in menu order
};
inline const UpgradeWeapon kUpgradeWeapons[] = {
    {"9mm", "9mm Pistol", "t1x-pistol-9mm",
     "clip-capacity-1 clip-capacity-2 clip-capacity-3 clip-capacity-4 fire-rate-1 fire-rate-2 fire-rate-3 "
     "reload-speed-1 reload-speed-2 reload-speed-3"},
    {"Revolver", "Revolver", "t1x-pistol-revolver",
     "fire-rate-1 fire-rate-2 fire-rate-3 reload-speed-1 reload-speed-2 reload-speed-3"},
    {"ElDiablo", "El Diablo", "t1x-pistol-uber",
     "clip-capacity-1 clip-capacity-2 fire-rate-1 fire-rate-2 fire-rate-3 reload-speed-1 reload-speed-2 "
     "penetration-1 penetration-2"},
    {"Shorty", "Shorty", "t1x-pistol-shotgun",
     "clip-capacity-1 clip-capacity-2 clip-capacity-3 clip-capacity-4 fire-rate-1 fire-rate-2 fire-rate-3 "
     "reload-speed-1 reload-speed-2 recoil-1 recoil-2 spread-1 spread-2 spread-3 spread-4"},
    {"Shotgun", "Shotgun", "t1x-shotgun-pump",
     "clip-capacity-1 clip-capacity-2 clip-capacity-3 clip-capacity-4 fire-rate-1 fire-rate-2 fire-rate-3 "
     "reload-speed-1 reload-speed-2 reload-speed-3 recoil-1 recoil-2 range-1 range-2"},
    {"HuntingRifle", "Hunting Rifle", "t1x-rifle-bolt",
     "scope-1 scope-2 clip-capacity-1 clip-capacity-2 reload-speed-1 reload-speed-2 damage-1 penetration-1 penetration-2"},
    {"SemiAuto", "Semi-Auto Rifle", "t1x-rifle-sub-mounted", "zoom-2"},
    {"Bow", "Bow", "t1x-bow",
     "rangefinder arrow-explosive drawspeed-1 drawspeed-2 drawspeed-3 range-1 range-2 range-3 reload-speed-1"},
    {"EllieBow", "Ellie's Bow", "t1x-bow-ellie", "rangefinder arrow-explosive range-bow-ellie"},
    {"Flamethrower", "Flamethrower", "t1x-flamethrower", "damage-1 range-1 reload-speed-1 reload-speed-2"},
    {"ShortHolster", "Short-Gun Holster", "t1x-holsters-shortgun", "short-gun-second-holster"},
    {"LongHolster", "Long-Gun Holster", "t1x-holsters-longgun", "long-gun-second-holster"},
};

} // namespace engine
