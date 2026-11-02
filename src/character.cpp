#include "character.h"

#include "dscan.h"
#include "engine.h"
#include "engine_defs.h"
#include "features.h"
#include "game.h"
#include "log.h"
#include "scan.h"
#include "settings.h"

#include "../third_party/minhook/include/MinHook.h"

#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <algorithm>
#include <array>
#include <utility>
#include <string>
#include <thread>
#include <vector>

namespace character {
void TraceHit(uint64_t index);
}

// Research trace (see trace_thunks.asm and character::InstallTrace).
extern "C" void* g_traceThunks[256];
extern "C" void* g_traceOrig[256];
extern "C" void TraceLog(uint64_t index) { character::TraceHit(index); }

namespace character {

namespace {

using CoreSpawn = void*(__fastcall*)(void*, void*, void*, void*, uint64_t, void*);
using Restart = bool(__fastcall*)(bool, bool);
using LoadPackage = bool(__fastcall*)(const char*, int, bool);
using PackageState = bool(__fastcall*)(void*, uint64_t);
using RebuildWants = void(__fastcall*)(void*);

CoreSpawn oCore = nullptr;
Restart g_restart = nullptr;

// The engine's package loader (see engine_defs.h).
LoadPackage g_loadPackage = nullptr;
PackageState g_loadingOrLoaded = nullptr, g_loaded = nullptr;
void** g_packageMgr = nullptr;

// LevelMgr and its want-load rebuild (see dscan::FindLevelSetLoader).
void** g_levelMgr = nullptr;
RebuildWants g_rebuildWants = nullptr;

features::Feature* g_asEllie = nullptr;
features::Feature* g_asJoel = nullptr;
features::Feature* g_outfit = nullptr;
features::Feature* g_giveWeapons = nullptr;
features::Feature* g_giveRecipes = nullptr;
features::Feature* g_swapNow = nullptr;
features::Feature* g_joelBuddy = nullptr;
features::Feature* g_bowOnBack = nullptr;

// Character id -> name, for every Joel / Ellie player character the menu knows.
std::map<uint64_t, std::string> g_names;
std::map<uint64_t, uint64_t> g_toEllie, g_toJoel;
std::vector<std::string> g_ellieList; // outfit choices for "Ellie's Outfit" (index 1..n)
std::atomic<bool> g_spawnedEllie{false};
std::atomic<bool> g_giveAfterSpawn{false};

// Frames since Swap Now while reporting package state; -1 = not reporting.
int g_reportFrames = -1;
// Frames Swap Now has waited for the new player's packages; -1 = not waiting.
int g_swapWait = -1;
// The player the last spawn was swapped to: 0 = none, 1 = Ellie, 2 = Joel.
std::atomic<int> g_swappedTo{0};
bool g_research = false; // research hooks on ([Research] Trace=1)
std::atomic<int> g_ovlEpoch{0}; // overlay research: log budget resets per spawn

// Counts player spawns, so Tick can check each swapped spawn once.
std::atomic<int> g_spawns{0};
// The thread inside the core player spawn (0 = none), and look log lines since it.
std::atomic<DWORD> g_playerSpawnThread{0};
std::atomic<int> g_lookLogs{0};
// From Playable Ellie (WORKING) 1.1 (tested by its players, 2026-11-01):
// The thread that runs player spawns (the companion spawns there too).
std::atomic<DWORD> g_spawnThread{0};
// The look given to the player at the last swap to Ellie (0 = none). The game
// re-reads the player's look on a worker thread after the spawn (more often on
// many-core CPUs: her body went invisible); that look stays the player's.
std::atomic<uint64_t> g_playerLook{0};
std::atomic<int> g_playerLookOther{0};
const uint8_t* g_skinLookReturn = nullptr;  // the Skins menu's look call: never swapped
const uint8_t* g_levelLookReturn = nullptr; // the level-load look registration: never swapped
uint64_t g_ellieBase = 0; // t1x-ellie, Ellie without an outfit number (the Skins menu's preview)
// Ellie's Outfit 0 dresses her in the outfit the chapter's own Ellie (the
// companion) wears: the level always loads that one. Matching Joel's outfit
// number instead only works when the game streams the other outfit in fast
// enough (on a player's laptop her torso and arms stayed invisible). Known
// pairs are listed; others are learned from the companion's look in the first
// seconds after a spawn and used from the next spawn (no automatic restart).
std::map<uint64_t, uint64_t> g_chapterOutfit; // Joel's look -> the chapter Ellie's look (game thread)
std::atomic<uint64_t> g_spawnJoelLook{0};      // the Joel look of the last swapped spawn
std::atomic<uint64_t> g_spawnTick{0};
std::atomic<uint64_t> g_seenCompanionEllie{0}; // an Ellie look resolved for someone else after that spawn
// Frames since Swap Now restarted the checkpoint; -1 = none. Swap Now is
// ignored meanwhile (a second restart during the first one hung the game).
std::atomic<int> g_restarting{-1};

// ------------------------------------------------------- research trace
// Logs the first calls of every function in the backpack / crafting code and
// the crafting menu code after each spawn, to see which step of crafting a
// swapped Ellie never reaches compared with Joel. Only installed on the exact
// game build it was written for (image timestamp below).
constexpr uint32_t kTraceTimestamp = 0x685BEBE1;
constexpr size_t kTraceMax = 256;
uint32_t g_traceRva[kTraceMax] = {};
std::atomic<int> g_traceHits[kTraceMax] = {};
std::atomic<int> g_traceSeen[kTraceMax] = {};
std::atomic<int> g_traceEpoch{1};
size_t g_traceCount = 0;

// Research: logs why the game refuses the crafting menu (see LogCraftingCheck).
using CanCraft = bool(__fastcall*)(void*);
CanCraft oCanCraft = nullptr;
const uint8_t* g_canCraftFn = nullptr;

uint64_t H(const std::string& s) { return dscan::Fnv1a64(s.c_str()); }

std::string Outfit(const char* who, int n, bool noBackpack) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "t1x-%s-costume-%02d%s", who, n, noBackpack ? "-no-backpack" : "");
    return buf;
}

template <size_t N>
bool Has(const int (&list)[N], int n) {
    for (int x : list)
        if (x == n) return true;
    return false;
}

template <size_t N>
int Closest(const int (&list)[N], int n) {
    int best = list[0];
    for (int x : list)
        if (x <= n) best = x;
    return best;
}

void BuildMaps() {
    using namespace engine;
    g_chapterOutfit[H("t1x-joel-costume-02")] = H("t1x-ellie-costume-01"); // Pittsburgh (Playable Ellie logs)
    g_names[H(kJoel)] = kJoel;
    g_names[H(kEllie)] = kEllie;
    g_ellieBase = H(kEllie);
    g_toEllie[H(kJoel)] = H(kEllie);
    g_toJoel[H(kEllie)] = H(kJoel);
    for (int n : kJoelOutfits) {
        for (bool nb : {false, true}) {
            if (nb && !Has(kJoelNoBackpack, n)) continue;
            const int e = Closest(kEllieOutfits, n);
            const std::string joel = Outfit("joel", n, nb), ellie = Outfit("ellie", e, nb && Has(kEllieNoBackpack, e));
            g_names[H(joel)] = joel;
            g_toEllie[H(joel)] = H(ellie);
        }
    }
    for (int n : kEllieOutfits) {
        g_ellieList.push_back(Outfit("ellie", n, false));
        for (bool nb : {false, true}) {
            if (nb && !Has(kEllieNoBackpack, n)) continue;
            const int j = Closest(kJoelOutfits, n);
            const std::string ellie = Outfit("ellie", n, nb), joel = Outfit("joel", j, nb && Has(kJoelNoBackpack, j));
            g_names[H(ellie)] = ellie;
            g_toJoel[H(ellie)] = H(joel);
        }
    }
}

std::string NameOf(uint64_t id) {
    auto it = g_names.find(id);
    if (it != g_names.end()) return it->second;
    char hex[32];
    std::snprintf(hex, sizeof hex, "0x%016llX", static_cast<unsigned long long>(id));
    return hex;
}

// The chapter's Ellie outfit for Joel's look, or 0. Joel's no-backpack variant
// (after some cutscenes) counts as the same outfit: Ellie gets her matching
// no-backpack variant when there is one.
uint64_t ChapterEllie(uint64_t joel) {
    auto ch = g_chapterOutfit.find(joel);
    if (ch != g_chapterOutfit.end()) return ch->second;
    const std::string name = NameOf(joel), nb = "-no-backpack";
    if (name.size() <= nb.size() || name.compare(name.size() - nb.size(), nb.size(), nb) != 0) return 0;
    ch = g_chapterOutfit.find(H(name.substr(0, name.size() - nb.size())));
    if (ch == g_chapterOutfit.end()) return 0;
    const uint64_t ellieNb = H(NameOf(ch->second) + nb);
    return g_names.count(ellieNb) ? ellieNb : ch->second;
}

// Every player spawn passes through here with the character to spawn.
void* __fastcall HkCore(void* a, void* b, void* c, void* d, uint64_t id, void* f) {
    uint64_t use = id;
    if (g_asEllie->on) {
        auto it = g_toEllie.find(id);
        if (it != g_toEllie.end()) {
            const int pick = g_outfit->value;
            use = (pick >= 1 && pick <= static_cast<int>(g_ellieList.size())) ? H(g_ellieList[pick - 1]) : it->second;
            if (pick == 0) {
                const uint64_t ch = ChapterEllie(id);
                if (ch) use = ch;
            }
        }
    } else if (g_asJoel->on) {
        auto it = g_toJoel.find(id);
        if (it != g_toJoel.end()) use = it->second;
    }
    logx::Write("player spawn: %s%s%s", NameOf(id).c_str(), use != id ? " -> " : "", use != id ? NameOf(use).c_str() : "");
    g_spawnedEllie = NameOf(use).rfind("t1x-ellie", 0) == 0;
    g_swappedTo = use == id ? 0 : g_spawnedEllie ? 1 : 2;
    ++g_spawns;
    ++g_traceEpoch; // the research trace logs each function again after every spawn
    ++g_ovlEpoch;
    g_lookLogs = 0;
    g_playerLook = g_swappedTo == 1 ? use : 0;
    g_playerLookOther = 0;
    g_spawnJoelLook = g_swappedTo == 1 ? id : 0;
    g_seenCompanionEllie = 0;
    g_spawnTick = GetTickCount64();
    g_restarting = -1;
    g_spawnThread = GetCurrentThreadId();
    g_playerSpawnThread = GetCurrentThreadId(); // looks resolved inside are the player's
    void* r = oCore(a, b, c, d, use, f);
    g_playerSpawnThread = 0;
    return r;
}

// ------------------------------------------------- Joel as the companion
// While Ellie is the player, the story companion Ellie (same buddy: AI, script
// name, scripted moves, dialogue) gets Joel's look in the matching outfit.
// Looks resolved during the player spawn are the player's and stay Ellie.
// Every Ellie/Joel look resolved is logged with its caller, so the log shows
// which code asked (companion spawner, cutscene actor, player, menu, ...).
using ResolveLook = void*(__fastcall*)(void*, uint64_t, void*, void*);
ResolveLook oResolveLook = nullptr;
const uint8_t* g_imageBase = nullptr;

bool JoelBuddyActive() { return g_joelBuddy && g_joelBuddy->on && (g_asEllie->on || g_swappedTo == 1); }

// Joel's look for the companion: the outfit Joel wears in this chapter (the one
// the player spawn started from, which the level loads), not the one with the
// matching number (in Pittsburgh that one never streamed in on a player's
// laptop: an invisible companion). The no-backpack variant is kept when both
// have one.
uint64_t ChapterJoel(uint64_t ellieLook, uint64_t byNumber) {
    const uint64_t joel = g_spawnJoelLook;
    if (!joel) return byNumber;
    const std::string name = NameOf(ellieLook), joelName = NameOf(joel);
    const std::string nb = "-no-backpack";
    const bool ellieNb = name.size() > nb.size() && name.compare(name.size() - nb.size(), nb.size(), nb) == 0;
    const bool joelNb = joelName.size() > nb.size() && joelName.compare(joelName.size() - nb.size(), nb.size(), nb) == 0;
    if (ellieNb == joelNb) return joel;
    const std::string other = ellieNb ? joelName + nb : joelName.substr(0, joelName.size() - nb.size());
    return g_names.count(H(other)) ? H(other) : joel;
}

void* __fastcall HkResolveLook(void* out, uint64_t look, void* c, void* d) {
    if (g_names.count(look)) {
        const auto caller = reinterpret_cast<const uint8_t*>(_ReturnAddress());
        const bool spawnThread = g_playerSpawnThread == GetCurrentThreadId();
        // The player's outfit asked for again on a worker thread is the player's.
        // Asked for on the spawn thread outside the spawn, it is the companion's.
        const bool player = spawnThread || (look == g_playerLook && GetCurrentThreadId() != g_spawnThread);
        if (player && !spawnThread && g_playerLookOther++ < 20)
            logx::Write("look: %s resolved outside the player spawn; kept as the player's (caller 0x%llX)", NameOf(look).c_str(),
                        static_cast<unsigned long long>(caller - g_imageBase));
        // The Skins menu's previews are left alone (turned into Joel they crashed
        // it); the same call right after a spawn dresses the companion, so that
        // one is still swapped.
        const bool skinCall = g_skinLookReturn && caller == g_skinLookReturn;
        const bool skinMenu = skinCall && GetTickCount64() - g_spawnTick > 3000;
        // Looks registered while a level loads, and the base look t1x-ellie (no
        // outfit number, the Skins menu's preview), are never swapped either.
        const bool levelLoad = g_levelLookReturn && caller == g_levelLookReturn;
        const bool baseLook = look == g_ellieBase;
        const bool other = !player && !skinMenu && !levelLoad && !baseLook;
        if (other && g_spawnJoelLook && g_toJoel.count(look) && GetTickCount64() - g_spawnTick < 5000) {
            uint64_t none = 0;
            g_seenCompanionEllie.compare_exchange_strong(none, look);
        }
        uint64_t use = look;
        if (other && JoelBuddyActive()) {
            auto it = g_toJoel.find(look);
            if (it != g_toJoel.end()) use = ChapterJoel(look, it->second);
        }
        if (g_lookLogs++ < 200)
            logx::Write("look: %s%s%s (caller 0x%llX%s)", NameOf(look).c_str(), use != look ? " -> " : "",
                        use != look ? NameOf(use).c_str() : "", static_cast<unsigned long long>(caller - g_imageBase),
                        spawnThread ? ", player spawn" : player ? ", player's look" : skinMenu ? ", Skins menu: left alone"
                        : levelLoad ? ", level load: left alone" : baseLook ? ", base look: left alone" : "");
        look = use;
    }
    return oResolveLook(out, look, c, d);
}

// Ellie's animations only fit her own pistol (t1x-pistol-ellie); with Joel's
// 9mm the gun points up when she aims. Gives her that pistol.
void RunAction(const char* id, const char* what) {
    features::Feature* f = features::Find(id);
    if (f && f->available && f->onAction) {
        logx::Write("swap: %s", what);
        f->onAction();
    }
}

// Swapped-in Ellie starts with an inventory that knows no crafting recipes in
// Joel's chapters, so the crafting menu has nothing to show and never opens.
void SetUpEllie() {
    if (g_giveRecipes->on) RunAction("AllRecipes", "teaching Ellie the crafting recipes");
}

// Each player's packages that the game actually has (see engine_defs.h).
std::vector<const char*> g_elliePackages, g_joelPackages;
std::vector<const char*> g_ellieJoelBuddyPackages; // Ellie's plus Joel's companion packages
// Ellie's outfits are made of parts (ellie-costume-NN-torso, -legs, bandages)
// that no level set lists; the look system streams them in when she puts the
// outfit on, which in Joel's chapters doesn't always happen in time (a player
// with a fixed outfit: arms, then everything but her head missing). With a
// fixed Ellie's Outfit, the patch also loads that outfit's parts and the swap
// waits for them.
constexpr const char* kOutfitParts[] = {"torso", "torso-no-backpack", "legs", "bandage-bicep",
                                        "bandage-forearm", "bandages-bicep", "bandages-forearm"};
std::vector<std::vector<std::string>> g_outfitParts; // per Ellie's Outfit 1..n (index 0 unused)
std::vector<const char*> g_needed;                    // scratch for NeededPackages()
// Never waited for: not in Ellie's set in Joel's chapters (50 of 51 in every test).
constexpr const char* kOptionalPackages[] = {"anim-ellie-player-mm-wade"};

const std::vector<std::string>& OutfitParts() {
    static const std::vector<std::string> none;
    const int pick = g_outfit ? g_outfit->value.load() : 0;
    return pick >= 1 && pick < static_cast<int>(g_outfitParts.size()) ? g_outfitParts[pick] : none;
}

// Keeps the names listed in the game's actor97.txt; false if it can't be read.
template <size_t N>
bool FilterPackages(const char* const (&list)[N], const std::vector<std::string>& actors, std::vector<const char*>& out,
                    const char* who) {
    for (const char* name : list) {
        bool found = false;
        for (const auto& a : actors)
            if (a == name) {
                found = true;
                break;
            }
        if (found) out.push_back(name);
        else logx::Write("character: %s package %s is not in actor97.txt; skipped", who, name);
    }
    return !out.empty();
}

bool LoadPackageLists() {
    char exe[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    std::string path(exe);
    path = path.substr(0, path.find_last_of("\\/") + 1) + "build\\pc\\main\\actor97.txt";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") || !f) {
        logx::Write("character: cannot read %s", path.c_str());
        return false;
    }
    std::vector<std::string> actors;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (!s.empty()) actors.push_back(s);
    }
    std::fclose(f);
    g_ellieJoelBuddyPackages.clear();
    FilterPackages(engine::kJoelBuddyPackages, actors, g_ellieJoelBuddyPackages, "Joel companion");
    const bool ok = FilterPackages(engine::kEllieOnlyPackages, actors, g_elliePackages, "Ellie") &&
                    FilterPackages(engine::kJoelOnlyPackages, actors, g_joelPackages, "Joel");
    g_ellieJoelBuddyPackages.insert(g_ellieJoelBuddyPackages.begin(), g_elliePackages.begin(), g_elliePackages.end());
    g_outfitParts.assign(std::size(engine::kEllieOutfits) + 1, {});
    for (size_t i = 0; i < std::size(engine::kEllieOutfits); ++i)
        for (const char* part : kOutfitParts) {
            char name[64];
            std::snprintf(name, sizeof name, "ellie-costume-%02d-%s", engine::kEllieOutfits[i], part);
            if (std::find(actors.begin(), actors.end(), name) != actors.end()) g_outfitParts[i + 1].push_back(name);
        }
    logx::Write("character: %zu Ellie / %zu Joel player packages available (%zu actors listed)", g_elliePackages.size(),
                g_joelPackages.size(), actors.size());
    return ok;
}

// Packages the chosen player needs that the chapter does not load, or none.
const std::vector<const char*>* NeededPackages() {
    if (!g_loaded) return nullptr;
    if (g_asEllie->on) {
        g_needed = g_joelBuddy->on && g_joelBuddy->available ? g_ellieJoelBuddyPackages : g_elliePackages;
        for (const auto& part : OutfitParts()) g_needed.push_back(part.c_str());
        return &g_needed;
    }
    if (g_asJoel->on) return &g_joelPackages;
    return nullptr;
}

// Game thread only. How many of the needed packages are fully loaded, and
// (if asked) how many of the ones the swap waits for are still missing.
int LoadedCount(size_t& total, int* requiredMissing = nullptr) {
    const auto* list = NeededPackages();
    void* mgr = g_packageMgr ? *g_packageMgr : nullptr;
    total = list ? list->size() : 0;
    if (requiredMissing) *requiredMissing = static_cast<int>(total);
    if (!list || !mgr) return 0;
    int loaded = 0, missing = 0;
    for (const char* name : *list) {
        if (g_loaded(mgr, dscan::Fnv1a64(name))) ++loaded;
        else if (std::none_of(std::begin(kOptionalPackages), std::end(kOptionalPackages),
                              [name](const char* o) { return std::strcmp(o, name) == 0; }))
            ++missing;
    }
    if (requiredMissing) *requiredMissing = missing;
    return loaded;
}

// ---------------------------------------------------------------- level sets
// Packages can only be loaded safely by the game itself during a level load
// (loading them from the menu runs texture jobs outside the game's job
// system and trips an assert). So while a swap is on, the player's level set
// for the chapter (e.g. joel-player-all) also lists the other character's
// set, and the game loads those packages with everything else.
//
// The level sets live in memory as DC data (level-sets-common.bin): a table
// of {name hash, type hash, record} entries; a record is {name string, name
// hash, body}, and body+0x40 is the level-sets array {pointer, count |
// 0xFFFFFFFF << 32} of pointers to {set name hash, 0}.
constexpr uint64_t kLevelSetType = 0xA84077CAA2495B60;
constexpr size_t kLevelSetsField = 0x40;
constexpr size_t kActorsField = 0x20;

bool ReadMem(uint64_t addr, void* out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(addr), out, n, &got) && got == n;
}

struct SetRef {
    uint64_t hash, zero;
};

// An actor (package) entry in a level set: {name, name hash}.
struct ActorRef {
    const char* name;
    uint64_t hash;
};

struct SetPatch {
    const char* set;              // the chapter's player set
    std::vector<const char*> add; // sets to add to it
    std::vector<const char*> actors = {}; // single packages to add to it
    bool joelBuddy = false;       // also Joel's companion set and packages while that option is on
    uint64_t field = 0;           // address of {pointer, count} in memory
    uint64_t origPtr = 0, origCount = 0;
    std::vector<SetRef> refs;     // our entries (built once per apply)
    std::vector<uint64_t> array;  // original entries + pointers to refs
    uint64_t origActorPtr = 0, origActorCount = 0;
    std::vector<ActorRef> actorRefs;
    std::vector<uint64_t> actorArray;
    bool applied = false;
};

std::vector<SetPatch> g_elliePatches, g_joelPatches;
std::atomic<bool> g_setsFound{false}, g_setsSearching{false};

// Validates a candidate record for `name`; returns the level-sets field address.
uint64_t CheckRecord(uint64_t rec, const char* name) {
    uint64_t r[3];
    if (!ReadMem(rec, r, sizeof r) || r[1] != dscan::Fnv1a64(name)) return 0;
    char str[64] = {};
    const size_t len = std::strlen(name) + 1;
    if (len > sizeof str || !ReadMem(r[0], str, len) || std::memcmp(str, name, len) != 0) return 0;
    uint64_t f[2];
    const uint64_t field = r[2] + kLevelSetsField;
    if (!ReadMem(field, f, sizeof f) || (f[1] >> 32) != 0xFFFFFFFF) return 0;
    const uint32_t n = static_cast<uint32_t>(f[1]);
    if (n == 0 || n > 64) return 0;
    for (uint32_t i = 0; i < n; ++i) { // every entry points at {hash, 0}
        uint64_t e, ref[2];
        if (!ReadMem(f[0] + 8 * i, &e, 8) || !ReadMem(e, ref, sizeof ref) || ref[1] != 0) return 0;
    }
    return field;
}

// Searches the game's memory for the player level-set records (a few seconds;
// run off the game thread). Reads go through ReadProcessMemory, so memory the
// game frees meanwhile cannot crash the scan.
void FindLevelSets() {
    std::vector<SetPatch*> all;
    for (auto* v : {&g_elliePatches, &g_joelPatches})
        for (auto& p : *v) all.push_back(&p);
    std::vector<uint8_t> buf(1 << 20);
    MEMORY_BASIC_INFORMATION mbi{};
    size_t found = 0;
    const DWORD start = GetTickCount();
    for (uint64_t a = 0x10000; found < all.size() && VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi);
         a = reinterpret_cast<uint64_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || (mbi.Protect & 0xFF) != PAGE_READWRITE) continue;
        const uint64_t base = reinterpret_cast<uint64_t>(mbi.BaseAddress), end = base + mbi.RegionSize;
        for (uint64_t at = base; at + 24 <= end; at += buf.size() - 24) {
            const size_t n = static_cast<size_t>((std::min<uint64_t>)(buf.size(), end - at));
            if (!ReadMem(at, buf.data(), n)) break;
            for (SetPatch* p : all) {
                if (p->field) continue;
                const uint64_t key[2] = {dscan::Fnv1a64(p->set), kLevelSetType};
                for (size_t i = 0; i + 24 <= n; i += 8) { // DC tables are 8-byte aligned
                    if (std::memcmp(buf.data() + i, key, 16) != 0) continue;
                    uint64_t rec;
                    std::memcpy(&rec, buf.data() + i + 16, 8);
                    if (const uint64_t field = CheckRecord(rec, p->set)) {
                        p->field = field;
                        ++found;
                        break;
                    }
                }
            }
            if (n < buf.size()) break;
        }
    }
    for (SetPatch* p : all) logx::Write("character: level set %s %s", p->set, p->field ? "found" : "NOT found");
    logx::Write("character: level-set search took %lu ms", GetTickCount() - start);
    g_setsFound = found == all.size();
}

// Game thread only. Points each set's level-sets array at original + added.
void ApplyPatches(std::vector<SetPatch>& patches) {
    for (auto& p : patches) {
        if (p.applied || !p.field) continue;
        uint64_t f[2];
        if (!ReadMem(p.field, f, sizeof f)) continue;
        const uint32_t n = static_cast<uint32_t>(f[1]);
        p.origPtr = f[0];
        p.origCount = f[1];
        std::vector<const char*> add = p.add, actors = p.actors;
        if (p.joelBuddy && g_joelBuddy->on && g_joelBuddy->available) {
            add.insert(add.end(), std::begin(engine::kJoelBuddySets), std::end(engine::kJoelBuddySets));
            actors.insert(actors.end(), std::begin(engine::kJoelBuddyPackages), std::end(engine::kJoelBuddyPackages));
        }
        if (p.joelBuddy) // the Ellie patches: a fixed outfit's parts too
            for (const auto& part : OutfitParts()) actors.push_back(part.c_str());
        p.refs.clear();
        p.refs.reserve(add.size());
        for (const char* s : add) p.refs.push_back({dscan::Fnv1a64(s), 0});
        p.array.assign(n, 0);
        if (!ReadMem(f[0], p.array.data(), 8 * n)) continue;
        for (auto& r : p.refs) p.array.push_back(reinterpret_cast<uint64_t>(&r));
        // Actors array (body+0x20, 0x20 before the level-sets array): same layout.
        const uint64_t actorField = p.field - kLevelSetsField + kActorsField;
        uint64_t af[2];
        bool actorsOk = false;
        if (!actors.empty() && ReadMem(actorField, af, sizeof af) && (af[1] >> 32) == 0xFFFFFFFF) {
            const uint32_t an = static_cast<uint32_t>(af[1]);
            p.origActorPtr = af[0];
            p.origActorCount = af[1];
            p.actorRefs.clear();
            p.actorRefs.reserve(actors.size()); // the array points into it
            for (const char* a : actors) p.actorRefs.push_back({a, dscan::Fnv1a64(a)});
            p.actorArray.assign(an, 0);
            if (an == 0 || ReadMem(af[0], p.actorArray.data(), 8 * an)) {
                for (auto& r : p.actorRefs) p.actorArray.push_back(reinterpret_cast<uint64_t>(&r));
                actorsOk = true;
            }
        }
        auto* slot = reinterpret_cast<volatile uint64_t*>(p.field);
        slot[0] = reinterpret_cast<uint64_t>(p.array.data()); // pointer first: an old count stays in bounds
        slot[1] = (f[1] & 0xFFFFFFFF00000000ull) | p.array.size();
        if (actorsOk) {
            auto* aslot = reinterpret_cast<volatile uint64_t*>(actorField);
            aslot[0] = reinterpret_cast<uint64_t>(p.actorArray.data());
            aslot[1] = (af[1] & 0xFFFFFFFF00000000ull) | p.actorArray.size();
        }
        p.applied = true;
        logx::Write("character: level set %s now also loads %zu more set(s) and %zu package(s)", p.set, add.size(),
                    actorsOk ? actors.size() : 0);
    }
}

void RemovePatches(std::vector<SetPatch>& patches) {
    for (auto& p : patches) {
        if (!p.applied) continue;
        auto* slot = reinterpret_cast<volatile uint64_t*>(p.field);
        slot[1] = p.origCount; // count first: never past the end of the original array
        slot[0] = p.origPtr;
        if (!p.actorArray.empty()) {
            auto* aslot = reinterpret_cast<volatile uint64_t*>(p.field - kLevelSetsField + kActorsField);
            aslot[1] = p.origActorCount;
            aslot[0] = p.origActorPtr;
            p.actorArray.clear();
        }
        p.applied = false;
        logx::Write("character: level set %s restored", p.set);
    }
}

void SyncPatches();

// ------------------------------------------------------------- 9mm hold
// Root cause (tested in game): Joel's 9mm has an Ellie overlay table in
// player-specific-weapon-overlays/t1x-guns/t1x-pistol-9mm (12 entries). All of
// its entries are animations made for the scripted scenes where she holds his
// gun, so in normal play the gun tilts ~70 degrees up in her hand (any subset
// of them still tilts it, tested). With the table empty the gun sits level but
// has no recoil; Ellie's own pistol table makes her twitchy with late recoil.
// Joel's revolver table is made for Ellie too, and with it the 9mm sits level,
// recoils on time and doesn't twitch (tested). So while Ellie is the player the
// 9mm uses the revolver's table (empty if that table isn't found); restored for
// Joel. Only the 9mm's reference changes; both tables stay as they are.
//
// The revolver's hold pose is made for a bigger gun, so the 9mm still sat
// slightly turned to the right when aiming (the revolver does too). The 9mm
// therefore uses the menu's own copy of the revolver table whose two hold
// entries take Ellie's own pistol hold instead (from t1x-pistol-ellie): the
// gun points straight (tested in game) and the rest stays the revolver's.
//
// Tables are found by their first entries; each is confirmed by its {table,
// count} reference at a fixed distance before it (same DC file layout).
constexpr uint64_t k9mmOverlayFirst[2] = {0x3A44AABBD653F271, 0x999E2C69E65BC987};
constexpr uint64_t kGripAim = 0x8C1A665995B5EE09; // pistol-beretta-grip-friendly-aim-part, entry 6 (+0x180)
constexpr uint64_t k9mmOverlayCount = 12;
constexpr uint64_t kRevolverOverlayFirst[2] = {0x3A44AABBD653F271, 0x19E1B5667FD820F7};
constexpr uint64_t kRevolverSecondKey = 0x8D99EFE7C9519C6E; // entry 1 (+0x40)
constexpr uint64_t kRevolverOverlayCount = 55;
uint64_t g_9mmOverlayCount = 0; // address of the 9mm's {table, count} count field, 0 = not found
uint64_t g_9mmTable = 0, g_revolverTable = 0;
constexpr uint64_t kHoldKeys[2] = {0x3A44AABBD653F271, 0x8D99EFE7C9519C6E}; // revolver entries 0 and 1
constexpr uint64_t kRevolverHold = 0x19E1B5667FD820F7, kEllieHold = 0x5A3555FEAA9DAA3B;
std::vector<uint64_t> g_9mmMix; // the copy (55 entries of 8 values), empty = not made

// Game thread only. The 9mm's table for Ellie: the copy, else the revolver's own.
uint64_t EllieGunTable() {
    if (g_9mmMix.empty() && g_revolverTable) {
        std::vector<uint64_t> t(kRevolverOverlayCount * 8);
        if (ReadMem(g_revolverTable, t.data(), t.size() * 8) && t[0] == kHoldKeys[0] && t[1] == kRevolverHold &&
            t[8] == kHoldKeys[1] && t[9] == kRevolverHold) {
            t[1] = t[9] = kEllieHold;
            g_9mmMix = std::move(t);
        } else {
            logx::Write("character: revolver table changed; 9mm uses it as it is");
        }
    }
    return g_9mmMix.empty() ? g_revolverTable : reinterpret_cast<uint64_t>(g_9mmMix.data());
}

// The game's flag that lets an Ellie player craft outside Winter (see
// dscan::FindEllieCraftFlag). On while Ellie is the player, restored after.
volatile uint8_t* g_ellieCraftFlag = nullptr;
uint8_t g_ellieCraftOrig = 0;
bool g_ellieCraftOn = false;

// Game thread only.
void SyncCraftFlag(bool ellie) {
    if (ellie == g_ellieCraftOn || !g_ellieCraftFlag) return;
    if (ellie) {
        g_ellieCraftOrig = *g_ellieCraftFlag;
        *g_ellieCraftFlag = 1;
    } else {
        *g_ellieCraftFlag = g_ellieCraftOrig;
    }
    g_ellieCraftOn = ellie;
    logx::Write("character: crafting for Ellie outside Winter %s", ellie ? "on" : "restored");
}
bool g_9mmOverlayOff = false;

// Bow on her back (Playable Ellie 1.1, tested): while Ellie plays, Joel's bow
// record gets Ellie's bow settings at +0x158, +0x180 (holster) and +0x198, so
// it sits vertically on her back instead of across her backpack. Her bow's
// overlay table is NOT used (that made her crouch and hid the bow when aiming).
constexpr uint64_t kProcessWeaponBow = 0x63CB48375E6E92D6;
constexpr uint64_t kJoelBowHolster = 0x5062ADC8205A58B9;  // s_bow-slot1-holster
constexpr uint64_t kEllieBowHolster = 0xAB0ABC3E74FAA491; // s_ellie-bow-slot1-holster
constexpr uint32_t kBowFields[3] = {0x180, 0x158, 0x198};
uint64_t g_joelBow = 0, g_joelBowOrig[3] = {}, g_ellieBowVals[3] = {};
bool g_bowApplied = false;

// The weapon record whose process-weapon field is at `type`, if its name is
// `name` and its holster block names `holster`; 0 otherwise.
uint64_t BowRecord(uint64_t type, const char* name, uint64_t holster) {
    uint64_t namePtr = 0, block = 0, value = 0;
    char str[16] = {};
    const size_t len = std::strlen(name) + 1;
    if (len > sizeof str || !ReadMem(type - 8, &namePtr, 8) || !ReadMem(namePtr, str, len) ||
        std::memcmp(str, name, len) != 0 || !ReadMem(type - 0x38 + 0x180, &block, 8) || !block ||
        !ReadMem(block + 0x40, &value, 8) || value != holster)
        return 0;
    return type - 0x38;
}

// Game thread only.
void SyncBow(bool ellie) {
    const bool want = ellie && g_bowOnBack && g_bowOnBack->on;
    if (want == g_bowApplied || !g_joelBow) return;
    auto* rec = reinterpret_cast<volatile uint64_t*>(g_joelBow);
    for (int f = 0; f < 3; ++f)
        if (rec[kBowFields[f] / 8] != (g_bowApplied ? g_ellieBowVals[f] : g_joelBowOrig[f])) {
            logx::Write("character: bow settings changed; left alone");
            g_joelBow = 0;
            return;
        }
    for (int f = 0; f < 3; ++f) rec[kBowFields[f] / 8] = want ? g_ellieBowVals[f] : g_joelBowOrig[f];
    g_bowApplied = want;
    logx::Write("character: Joel's bow uses %s", want ? "Ellie's bow settings (on her back)" : "its own settings");
}

void FindGunOverlay() {
    uint64_t found = 0, table9 = 0, revolver = 0, joelBow = 0, ellieBow = 0;
    std::vector<uint8_t> buf(1 << 20);
    MEMORY_BASIC_INFORMATION mbi{};
    const DWORD start = GetTickCount();
    auto done = [&] { return found && revolver && joelBow && ellieBow; };
    for (uint64_t a = 0x10000; !done() && VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi);
         a = reinterpret_cast<uint64_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || (mbi.Protect & 0xFF) != PAGE_READWRITE) continue;
        const uint64_t base = reinterpret_cast<uint64_t>(mbi.BaseAddress), end = base + mbi.RegionSize;
        for (uint64_t at = base; at + 16 <= end && !done(); at += buf.size() - 16) {
            const size_t n = static_cast<size_t>((std::min<uint64_t>)(buf.size(), end - at));
            if (!ReadMem(at, buf.data(), n)) break;
            for (size_t i = 0; i + 16 <= n; i += 8) {
                const uint64_t table = at + i;
                uint64_t key2 = 0, ref[2] = {}, word = 0;
                std::memcpy(&word, buf.data() + i, 8);
                if (word == kProcessWeaponBow && (!joelBow || !ellieBow)) {
                    if (!joelBow) joelBow = BowRecord(table, "t1x-bow", kJoelBowHolster);
                    if (!ellieBow) ellieBow = BowRecord(table, "t1x-bow-ellie", kEllieBowHolster);
                    continue;
                }
                if (!found && std::memcmp(buf.data() + i, k9mmOverlayFirst, 16) == 0 && ReadMem(table + 0x180, &key2, 8) &&
                    key2 == kGripAim && ReadMem(table - 0xE8, ref, sizeof ref) && ref[0] == table &&
                    ref[1] == k9mmOverlayCount) {
                    found = table - 0xE0;
                    table9 = table;
                } else if (!revolver && std::memcmp(buf.data() + i, kRevolverOverlayFirst, 16) == 0 &&
                           ReadMem(table + 0x40, &key2, 8) && key2 == kRevolverSecondKey &&
                           ReadMem(table - 0x220, ref, sizeof ref) && ref[0] == table && ref[1] == kRevolverOverlayCount) {
                    revolver = table;
                }
            }
            if (n < buf.size()) break;
        }
    }
    logx::Write("character: 9mm Ellie overlay %s, revolver overlay %s, bow records %s (%lu ms)", found ? "found" : "NOT found",
                revolver ? "found" : "NOT found", joelBow && ellieBow ? "found" : "NOT found", GetTickCount() - start);
    engine::RunOnGameThread([found, table9, revolver, joelBow, ellieBow] {
        if (!g_9mmOverlayOff) g_9mmOverlayCount = found, g_9mmTable = table9, g_revolverTable = revolver;
        if (!g_joelBow && !g_bowApplied && joelBow && ellieBow) {
            for (int f = 0; f < 3; ++f)
                if (!ReadMem(joelBow + kBowFields[f], &g_joelBowOrig[f], 8) || !ReadMem(ellieBow + kBowFields[f], &g_ellieBowVals[f], 8))
                    return;
            g_joelBow = joelBow;
        }
    });
}

// Game thread only.
void SyncGunOverlay(bool ellie) {
    if (ellie == g_9mmOverlayOff || !g_9mmOverlayCount) return;
    // The {table, count} reference: count at g_9mmOverlayCount, pointer just before.
    auto* ref = reinterpret_cast<volatile uint64_t*>(g_9mmOverlayCount - 8);
    uint64_t cur[2] = {};
    const uint64_t mine[2] = {g_9mmTable, k9mmOverlayCount};
    const uint64_t ellieRef[2] = {g_revolverTable ? EllieGunTable() : g_9mmTable, g_revolverTable ? kRevolverOverlayCount : 0};
    const uint64_t* expect = ellie ? mine : ellieRef;
    if (!ReadMem(g_9mmOverlayCount - 8, cur, sizeof cur) || cur[0] != expect[0] || cur[1] != expect[1]) {
        logx::Write("character: 9mm Ellie overlay changed; left alone");
        g_9mmOverlayCount = 0;
        return;
    }
    const uint64_t* want = ellie ? ellieRef : mine;
    ref[1] = 0; // count first: never a count past the end of either table
    ref[0] = want[0];
    ref[1] = want[1];
    g_9mmOverlayOff = ellie;
    logx::Write("character: 9mm uses %s", !ellie ? "its normal animations"
                                         : !g_9mmMix.empty() ? "the revolver's animations with Ellie's pistol hold (straight, with recoil)"
                                         : g_revolverTable ? "the revolver's animations (level, with recoil)"
                                                           : "no overlay (level, no recoil)");
}

// Game thread only. Applies the patches for the current toggles. A patch
// stays while the player it loads is still the one playing (the game re-reads
// level sets on its own, and unloading the player's animations under them
// would freeze or crash the game); it is removed after the next normal spawn.
void SyncPatches() {
    SyncGunOverlay(g_asEllie->on || g_swappedTo == 1);
    SyncBow(g_asEllie->on || g_swappedTo == 1);
    SyncCraftFlag(g_asEllie->on || g_swappedTo == 1);
    if (!g_setsFound) return;
    if (g_asEllie->on) ApplyPatches(g_elliePatches);
    else if (g_swappedTo != 1) RemovePatches(g_elliePatches);
    if (g_asJoel->on) ApplyPatches(g_joelPatches);
    else if (g_swappedTo != 2) RemovePatches(g_joelPatches);
}

// Game thread only. Makes the game rebuild its want-load list from the
// current level set, so the sets the patches add start streaming in.
bool RequestPackages() {
    void* mgr = g_levelMgr ? *g_levelMgr : nullptr;
    if (!g_rebuildWants || !mgr) return false;
    g_rebuildWants(mgr);
    logx::Write("swap: asked the game to stream in the new player's packages");
    return true;
}

void BuildPatches() {
    // t1x-anim-ellie-pistol: the animations the 9mm's Ellie overlay
    // (player-specific-weapon-overlays/t1x-guns/t1x-pistol-9mm) points to. The
    // game loads it for the scenes where Ellie uses Joel's 9mm; without it her
    // hold and aim fall back to a default pose and the gun points up.
    g_elliePatches = {{"joel-player-all", {"ellie-player-all", "ellie-player-anims-traversal-all"}, {"t1x-anim-ellie-pistol"}, true},
                      {"joel-player-all-no-explosives", {"ellie-player-all-no-explosives", "ellie-player-anims-traversal-all"},
                       {"t1x-anim-ellie-pistol"}, true}};
    g_joelPatches = {{"ellie-player-all", {"joel-player-all"}},
                     {"ellie-player-all-no-explosives", {"joel-player-all-no-explosives"}}};
}

// Finds the level sets once (in the background), then applies the patches.
void PrepareSwap() {
    if (g_setsFound) {
        engine::RunOnGameThread(&SyncPatches);
        return;
    }
    if (g_setsSearching.exchange(true)) return;
    std::thread([] {
        FindLevelSets();
        g_setsSearching = false;
        engine::RunOnGameThread(&SyncPatches);
        FindGunOverlay(); // after, so it never delays the swap
        engine::RunOnGameThread(&SyncPatches);
    }).detach();
}

void DoRestart() {
    logx::Write("swap: restarting checkpoint (%s)", g_asEllie->on ? "Ellie" : g_asJoel->on ? "Joel in Ellie's chapters" : "story characters");
    features::Notify("Swapping...");
    g_restarting = 0;
    g_restart(false, false);
}

// Game thread only. Logs the needed packages that are not loaded.
void LogMissing() {
    const auto* list = NeededPackages();
    void* mgr = g_packageMgr ? *g_packageMgr : nullptr;
    if (!list || !mgr) return;
    for (const char* name : *list)
        if (!g_loaded(mgr, dscan::Fnv1a64(name))) logx::Write("swap: not loaded: %s", name);
}

// A swapped player that spawned without its animations (e.g. the swap was
// remembered from the last launch, or turned on before a checkpoint) gets
// them loaded, then the checkpoint restarts once. At most twice per launch.
void CheckSwappedSpawn() {
    static int seen = 0, wait = 0, fixes = 0;
    const int spawns = g_spawns;
    if (spawns == seen || g_swappedTo == 0 || g_swapWait >= 0 || !engine::PlayerPresent()) {
        if (spawns == seen) wait = 0;
        return;
    }
    if (++wait < 90) return; // ~1.5 s after the spawn
    seen = spawns;
    size_t total = 0;
    const int loaded = LoadedCount(total);
    logx::Write("swap: spawned as the swapped player with %d of %zu packages loaded", loaded, total);
    if (total && loaded + 4 >= static_cast<int>(total)) {
        // Her animations are in: give her own pistol once she has settled.
        if (g_swappedTo == 1 && g_giveRecipes->on) g_giveAfterSpawn = true;
        return;
    }
    if (!total || fixes >= 2 || !g_setsFound) return;
    ++fixes;
    SyncPatches();
    if (RequestPackages()) {
        g_swapWait = 0;
        features::Notify("Loading animations...");
    }
}

// Research: the inventory/crafting input handler (0x18EDD38 in this build)
// only opens crafting (0x18ED060) after these checks; on each Tab press the
// menu logs them all, for Joel and for Ellie, to find the one that differs.
using IsInState = bool(__fastcall*)(void*, uint64_t);
IsInState g_isInState = nullptr;
void* g_lastHandler = nullptr; // the handler's object (r13 / rcx), saved by HkInvHandler
using InvHandler = void(__fastcall*)(void*, void*, void*, void*);
InvHandler oInvHandler = nullptr;

void __fastcall HkInvHandler(void* a, void* b, void* c, void* d) {
    g_lastHandler = a;
    oInvHandler(a, b, c, d);
}

// ------------------------------------------------------- overlay research
// Animation overlays live in slots of an overlay set; every change ends in
// SetSlot(slots, index, def) (0x1FD1C84 in this build). Name lookups
// (0x1FD1D54 / 0x1FD1D20) map a name hash to its def, so the log can name
// the overlays. Right mouse button edges are logged as AIM markers.
using SetSlot = void(__fastcall*)(void*, int, void*);
using ApplyByName = bool(__fastcall*)(void*, uint64_t, void*);
using LookupDef = void*(__fastcall*)(uint64_t, void*);
SetSlot oSetSlot = nullptr;
ApplyByName oApplyByName = nullptr, oApplyByName2 = nullptr;
LookupDef g_lookupDef = nullptr;
std::mutex g_ovlLock;
std::map<uint64_t, std::string> g_ovlNames;    // name hash -> name (from exe strings)
std::map<void*, uint64_t> g_ovlDefHash;        // def -> name hash
std::map<std::pair<void*, int>, void*> g_ovlSlot; // (slots, index) -> def
int g_ovlLines = 0;
bool g_ovlOn = false;

std::string OvlName(void* def) {
    auto it = g_ovlDefHash.find(def);
    if (it == g_ovlDefHash.end()) return "?";
    auto n = g_ovlNames.find(it->second);
    if (n != g_ovlNames.end()) return n->second;
    char b[24];
    std::snprintf(b, sizeof b, "#%016llX", static_cast<unsigned long long>(it->second));
    return b;
}

void __fastcall HkSetSlot(void* slots, int index, void* def) {
    oSetSlot(slots, index, def);
    std::lock_guard<std::mutex> l(g_ovlLock);
    void*& cur = g_ovlSlot[{slots, index}];
    static int epoch = -1;
    if (epoch != g_ovlEpoch) epoch = g_ovlEpoch, g_ovlLines = 0;
    if (cur == def || g_ovlLines > 6000) return;
    const std::string from = cur ? OvlName(cur) : "-", to = def ? OvlName(def) : "-";
    cur = def;
    ++g_ovlLines;
    logx::Write("ovl %p[%d] %s -> %s (%s)", slots, index, from.c_str(), to.c_str(),
                g_swappedTo == 1 ? "Ellie" : "Joel");
}

void RememberDef(uint64_t hash, void* ctx) {
    if (!g_lookupDef) return;
    void* def = g_lookupDef(hash, ctx);
    if (!def) return;
    std::lock_guard<std::mutex> l(g_ovlLock);
    g_ovlDefHash[def] = hash;
}

bool __fastcall HkApplyByName(void* set, uint64_t hash, void* flag) {
    RememberDef(hash, *reinterpret_cast<void**>(static_cast<uint8_t*>(set) + 0x10));
    return oApplyByName(set, hash, flag);
}
bool __fastcall HkApplyByName2(void* set, uint64_t hash, void* ctx) {
    RememberDef(hash, ctx);
    return oApplyByName2(set, hash, ctx);
}

void InstallOverlayResearch(const uint8_t* base, size_t size) {
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kTraceTimestamp) return;
    // Names: every printable string in the image that looks like an anim/overlay name.
    for (size_t i = 1; i + 8 < size; ++i) {
        if (base[i - 1] != 0) continue;
        size_t n = 0;
        while (i + n < size && n < 96 && base[i + n] >= 0x21 && base[i + n] < 0x7F) ++n;
        if (n < 6 || i + n >= size || base[i + n] != 0) continue;
        const std::string str(reinterpret_cast<const char*>(base + i), n);
        if (str.find("overlay") != std::string::npos || str.find("aim") != std::string::npos ||
            str.find("bow") != std::string::npos || str.find("rifle") != std::string::npos ||
            str.find("shotgun") != std::string::npos || str.find("grip") != std::string::npos ||
            str.find("holster") != std::string::npos || str.find("ellie") != std::string::npos ||
            str.find("joel") != std::string::npos || str.find("long-gun") != std::string::npos)
            g_ovlNames[dscan::Fnv1a64(str.c_str())] = str;
        i += n;
    }
    g_lookupDef = reinterpret_cast<LookupDef>(const_cast<uint8_t*>(base + 0x1EFBD28));
    struct H { size_t rva; void* hk; void** orig; } hooks[] = {
        {0x1FD1C84, reinterpret_cast<void*>(&HkSetSlot), reinterpret_cast<void**>(&oSetSlot)},
        {0x1FD1D54, reinterpret_cast<void*>(&HkApplyByName), reinterpret_cast<void**>(&oApplyByName)},
        {0x1FD1D20, reinterpret_cast<void*>(&HkApplyByName2), reinterpret_cast<void**>(&oApplyByName2)},
    };
    int ok = 0;
    for (auto& h : hooks) {
        void* t = const_cast<uint8_t*>(base + h.rva);
        if (MH_CreateHook(t, h.hk, h.orig) == MH_OK && MH_EnableHook(t) == MH_OK) ++ok;
    }
    g_ovlOn = ok == 3;
    logx::Write("research: overlay logging %s (%zu names)", g_ovlOn ? "on" : "FAILED", g_ovlNames.size());
}

void LogCraftChecks() {
    void* h = g_lastHandler;
    if (!h || !g_isInState || !oCanCraft) return;
    char out[600];
    __try {
        void* r12 = *reinterpret_cast<void**>(static_cast<uint8_t*>(h) + 8);
        void* player = *reinterpret_cast<void**>(static_cast<uint8_t*>(r12) + 8);
        const bool enabled = reinterpret_cast<bool(__fastcall*)(void*)>((*reinterpret_cast<void***>(r12))[0x58 / 8])(r12);
        const uint8_t* g = *reinterpret_cast<uint8_t* const*>(reinterpret_cast<const uint8_t*>(g_canCraftFn) - 0x1A906C4 + 0x6433100);
        std::snprintf(out, sizeof out,
                      "craft keys (%s): canEnter=%d handlerEnabled=%d flag3f9a=%d dw4844=%d Move=%d Inventory=%d Prone=%d "
                      "GroundLanding=%d Script=%d RideHorse=%d Swim=%d Crouch=%d Jump=%d byte44=%d byte47=%d",
                      g_swappedTo == 1 ? "Ellie" : "Joel", oCanCraft(player), enabled, g ? g[0x3F9A] : -1,
                      g ? *reinterpret_cast<const int*>(g + 0x4844) : -1, g_isInState(player, dscan::Fnv1a64("Move")),
                      g_isInState(player, dscan::Fnv1a64("Inventory")), g_isInState(player, dscan::Fnv1a64("Prone")),
                      g_isInState(player, dscan::Fnv1a64("GroundLanding")), g_isInState(player, dscan::Fnv1a64("Script")),
                      g_isInState(player, dscan::Fnv1a64("RideHorse")), g_isInState(player, dscan::Fnv1a64("Swim")),
                      g_isInState(player, dscan::Fnv1a64("Crouch")), g_isInState(player, dscan::Fnv1a64("Jump")),
                      static_cast<uint8_t*>(r12)[0x44], static_cast<uint8_t*>(r12)[0x47]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(out, sizeof out, "craft keys: unreadable");
    }
    logx::Write("%s", out);
}

// A chapter outfit learned from the companion's look after a swapped spawn
// (Ellie's Outfit 0): used from the next spawn, no automatic restart.
void CheckChapterOutfit() {
    const uint64_t seen = g_seenCompanionEllie, joel = g_spawnJoelLook, mine = g_playerLook;
    if (!seen || !joel || !g_outfit || g_outfit->value != 0 || g_swappedTo != 1) return;
    g_seenCompanionEllie = 0;
    if (seen == mine || ChapterEllie(joel)) return;
    g_chapterOutfit[joel] = seen;
    logx::Write("outfit: this chapter's Ellie wears %s (Joel: %s); the player wore %s; from the next spawn on, she wears that one",
                NameOf(seen).c_str(), NameOf(joel).c_str(), NameOf(mine).c_str());
}

void Tick() {
    CheckSwappedSpawn();
    CheckChapterOutfit();
    if (g_restarting >= 0 && ++g_restarting > 20 * 60) g_restarting = -1; // no spawn after ~20 s
    if (g_ovlOn) { // research: aim markers
        static bool aim = false;
        const bool now = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (now != aim) logx::Write("----- AIM %s (%s)", now ? "DOWN" : "UP", g_swappedTo == 1 ? "Ellie" : "Joel");
        aim = now;
    }
    {   // research: Tab edge
        static bool down = false;
        const bool now = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
        if (now && !down && g_research) LogCraftChecks();
        down = now;
    }
    // A swap remembered from the last launch: find the level sets once the
    // game is up (they may not exist yet at the main menu, so retry).
    static int retry = 0;
    if ((g_asEllie->on || g_asJoel->on) && !g_setsFound && !g_setsSearching && ++retry % 600 == 0 && retry <= 6000)
        PrepareSwap();

    // Swap Now waits here until the new player's packages are in, then restarts.
    // Not every listed package belongs to the player's level set, so it also
    // goes ahead once the count has stopped rising for ~5 s with nearly all in.
    static int lastLoaded = -1, stableSince = 0;
    if (g_swapWait == 0) lastLoaded = -1;
    if (g_swapWait >= 0 && engine::PlayerPresent() && ++g_swapWait % 30 == 0) {
        size_t total = 0;
        int missing = 0;
        const int loaded = LoadedCount(total, &missing);
        if (loaded != lastLoaded) lastLoaded = loaded, stableSince = g_swapWait;
        const bool settled = g_swapWait - stableSince >= 300 && loaded + 4 >= static_cast<int>(total);
        if (missing == 0 || settled) {
            logx::Write("swap: %d of %zu player packages loaded after %d frames", loaded, total, g_swapWait);
            LogMissing();
            g_swapWait = -1;
            g_reportFrames = 0;
            DoRestart();
        } else if (g_swapWait >= 45 * 60) {
            LogMissing();
            logx::Write("swap: gave up waiting, %d of %zu player packages loaded", loaded, total);
            g_swapWait = -1;
            features::Notify("Ellie's animations did not load, so the swap was cancelled");
        } else {
            char msg[64];
            std::snprintf(msg, sizeof msg, "Loading animations... %d / %zu", loaded, total);
            features::Notify(msg);
            if (g_swapWait % 120 == 0) logx::Write("swap: waiting, %d of %zu player packages loaded", loaded, total);
        }
    }
    // Removes a patch once its player is no longer the one playing.
    static int sync = 0;
    if (++sync % 60 == 0) SyncPatches();

    // After Swap Now, log every ~2 s for ~20 s how many packages are loaded.
    if (g_reportFrames >= 0 && engine::PlayerPresent()) {
        if (++g_reportFrames % 120 == 0) {
            size_t total = 0;
            const int loaded = LoadedCount(total);
            logx::Write("swap: %d of %zu player packages loaded", loaded, total);
            if (loaded == static_cast<int>(total) || g_reportFrames >= 1200) g_reportFrames = -1;
        }
    }

    // Ellie's pistol is given ~3 s after she is up and running with her
    // animations (giving during the restart itself raised an exception).
    static int wait = 0;
    if (!g_giveAfterSpawn || g_swapWait >= 0) {
        wait = 0;
        return;
    }
    if (!engine::PlayerPresent()) {
        wait = 0;
        return;
    }
    if (++wait < 180) return;
    g_giveAfterSpawn = false;
    if (g_swappedTo == 1) SetUpEllie();
}

// Finds the engine's package loader and its two state checks.
bool FindPackageLoader(const uint8_t* base, size_t textEnd) {
    const auto hits = scan::FindAll(base, textEnd, scan::Parse(engine::kPackageCheckCall));
    const scan::Pattern load = scan::Parse(engine::kLoadPackagePrologue);
    const scan::Pattern loading = scan::Parse(engine::kPackageLoadingCheck);
    const scan::Pattern loaded = scan::Parse(engine::kPackageLoadedCheck);
    auto target = [&](size_t at) { // rel32 operand at `at`, from the end of the operand
        int32_t rel;
        std::memcpy(&rel, base + at, 4);
        return static_cast<size_t>(static_cast<int64_t>(at) + 4 + rel);
    };
    for (size_t h : hits) {
        const size_t mgr = target(h + 3), check = target(h + 8), fn = target(h + 25);
        if (check + 0x60 > textEnd || fn + 0x40 > textEnd) continue;
        if (scan::FindAll(base + fn, load.bytes.size(), load).size() != 1) continue;
        if (scan::FindAll(base + check, loading.bytes.size(), loading).size() != 1) continue;
        const size_t full = check + engine::kPackageLoadedOffset;
        if (scan::FindAll(base + full, loaded.bytes.size(), loaded).size() != 1) continue;
        g_packageMgr = reinterpret_cast<void**>(const_cast<uint8_t*>(base + mgr));
        g_loadingOrLoaded = reinterpret_cast<PackageState>(const_cast<uint8_t*>(base + check));
        g_loaded = reinterpret_cast<PackageState>(const_cast<uint8_t*>(base + full));
        g_loadPackage = reinterpret_cast<LoadPackage>(const_cast<uint8_t*>(base + fn));
        logx::Write("character: package loader RVA 0x%zX, checks 0x%zX / 0x%zX, manager 0x%zX", fn, check, full, mgr);
        return true;
    }
    logx::Write("character: package loader not found (%zu candidate sites)", hits.size());
    return false;
}

// ------------------------------------------------------- crafting research
// Player::CanEnterCraftingMenu, behind the script function
// "player-can-enter-crafting-menu?". While the player is swapped, each change
// of its answer is logged with its individual checks, to find what stops the
// crafting menu for a swapped player. The offsets below are inside that
// function, which is matched in full first (kCanCraftBody).
using GetPtr = void*(__fastcall*)(void*);
using GetBoolV = bool(__fastcall*)(void*);
using StateCheck = bool(__fastcall*)(void*, uint64_t);
using VehicleOf = void*(__fastcall*)(void*, int);

template <typename F>
F VFn(void* obj, size_t offset) {
    return reinterpret_cast<F>((*reinterpret_cast<void***>(obj))[offset / 8]);
}

void LogCraftingCheck(void* p, bool result) {
    const uint8_t* fn = g_canCraftFn;
    auto call = [&](size_t at) { // target of the call rel32 at fn+at
        int32_t r;
        std::memcpy(&r, fn + at + 1, 4);
        return fn + at + 5 + r;
    };
    auto field = [&](size_t off) { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(p) + off); };
    const auto inState = reinterpret_cast<StateCheck>(call(0x95));
    const auto busy = reinterpret_cast<GetBoolV>(call(0x11A));
    int32_t d;
    std::memcpy(&d, fn + 0xB6 + 4, 4);
    const float minVal = *reinterpret_cast<const float*>(fn + 0xB6 + 8 + d);
    char out[512];
    __try {
        void* a = field(0x3358);
        void* b = field(0x32C0);
        uint8_t* c = static_cast<uint8_t*>(field(0x3330));
        void* e = field(0x32F0);
        std::snprintf(out, sizeof out,
                      "craft check = %d: busy3358=%d pending5F20=%d blocked32C0=%d Script=%d flag120=%d value210=%.2f "
                      "(needs %.2f) GroundLanding=%d blocked32F0=%d busyFn=%d vehicle=%d",
                      result, VFn<GetPtr>(a, 0x1E0)(a) != nullptr, field(0x5F20) != nullptr, VFn<GetBoolV>(b, 0x60)(b),
                      inState(p, dscan::Fnv1a64("Script")), c[0x120], *reinterpret_cast<float*>(c + 0x210), minVal,
                      inState(p, dscan::Fnv1a64("GroundLanding")), VFn<GetBoolV>(e, 0x2A8)(e), busy(p),
                      VFn<VehicleOf>(p, 0x12F0)(p, 0) != nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(out, sizeof out, "craft check = %d (details unreadable)", result);
    }
    logx::Write("%s", out);
}

bool __fastcall HkCanCraft(void* p) {
    const bool r = oCanCraft(p);
    static int last = -1, logged = 0;
    if ((r ? 1 : 0) != last && logged < 40) {
        ++logged;
        LogCraftingCheck(p, r);
    }
    last = r ? 1 : 0;
    return r;
}

// Research: logs calls to the crafting script functions (and their results)
// while the player is swapped, to see which step of opening crafting stops.
// Script natives take at most four pointer arguments and return in rax.
using Native = uint64_t(__fastcall*)(void*, void*, void*, void*);
constexpr const char* kCraftNatives[] = {"menu2-push-crafting-menu", "menu2-crafting-tab-available?",
                                         "is-crafting-disabled-this-frame?", "player-start-crafting-behavior",
                                         "player-can-craft?", "player-can-craft-anything?"};
constexpr size_t kNativeCount = sizeof(kCraftNatives) / sizeof(kCraftNatives[0]);
Native g_nativeOrig[kNativeCount] = {};

template <size_t I>
uint64_t __fastcall HkNative(void* a, void* b, void* c, void* d) {
    const uint64_t r = g_nativeOrig[I](a, b, c, d);
    static uint64_t last = ~0ull;
    static int logged = 0;
    if (r != last && logged < 30) { // Joel too, to compare with a swapped player
        ++logged;
        logx::Write("craft: %s -> %llu (%s)", kCraftNatives[I], static_cast<unsigned long long>(r & 0xFF),
                    g_swappedTo == 1 ? "Ellie" : g_swappedTo == 2 ? "swapped Joel" : "normal player");
    }
    last = r;
    return r;
}

template <size_t... I>
constexpr std::array<void*, sizeof...(I)> NativeHooks(std::index_sequence<I...>) {
    return {reinterpret_cast<void*>(&HkNative<I>)...};
}

void HookCraftingNatives(const uint8_t* base, size_t textEnd, size_t size) {
    static const auto hooks = NativeHooks(std::make_index_sequence<kNativeCount>{});
    auto rel = [&](size_t disp) {
        int32_t r;
        std::memcpy(&r, base + disp, 4);
        return static_cast<size_t>(static_cast<int64_t>(disp) + 4 + r);
    };
    const auto sites = scan::FindAll(base, textEnd, scan::Parse(engine::kScriptFuncRegister));
    for (size_t i = 0; i < kNativeCount; ++i) {
        const auto names = dscan::FindCString(base, size, kCraftNatives[i]);
        if (names.size() != 1) continue;
        for (size_t at : sites) {
            if (rel(at + 7) != names[0]) continue;
            void* fn = const_cast<uint8_t*>(base + rel(at + 14));
            if (MH_CreateHook(fn, hooks[i], reinterpret_cast<void**>(&g_nativeOrig[i])) == MH_OK && MH_EnableHook(fn) == MH_OK)
                logx::Write("character: watching %s at RVA 0x%zX", kCraftNatives[i], rel(at + 14));
            break;
        }
    }
}

void HookCraftingCheck(const uint8_t* base, size_t textEnd, size_t size) {
    const auto names = dscan::FindCString(base, size, engine::kCanCraftName);
    if (names.size() != 1) return;
    auto rel = [&](size_t disp) {
        int32_t r;
        std::memcpy(&r, base + disp, 4);
        return static_cast<size_t>(static_cast<int64_t>(disp) + 4 + r);
    };
    for (size_t at : scan::FindAll(base, textEnd, scan::Parse(engine::kScriptFuncRegister))) {
        if (rel(at + 7) != names[0]) continue;
        const size_t wrapper = rel(at + 14);
        if (wrapper + 0x30 > textEnd || scan::FindAll(base + wrapper, 0x20, scan::Parse(engine::kCanCraftWrapper)).size() != 1)
            break;
        const size_t fn = rel(wrapper + 0x1B);
        if (fn + 0x290 > textEnd) break;
        const auto body = scan::FindAll(base + fn, 0x290, scan::Parse(engine::kCanCraftBody));
        if (body.size() != 1 || body[0] != 0) break;
        g_canCraftFn = base + fn;
        void* target = const_cast<uint8_t*>(base + fn);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&HkCanCraft), reinterpret_cast<void**>(&oCanCraft)) == MH_OK &&
            MH_EnableHook(target) == MH_OK)
            logx::Write("character: crafting check RVA 0x%zX watched", fn);
        return;
    }
    logx::Write("character: crafting check not recognised");
}

} // namespace

// Function starts in [lo, hi): after int3 padding, a usual prologue, and at
// least 16 bytes long (so the 5-byte hook jump never reaches the next one).
std::vector<size_t> TraceTargets(const uint8_t* base, size_t lo, size_t hi) {
    static const uint8_t* const kProlog[] = {
        (const uint8_t*)"\x48\x89\x5c\x24", (const uint8_t*)"\x48\x89\x4c\x24", (const uint8_t*)"\x48\x89\x54\x24",
        (const uint8_t*)"\x48\x89\x74\x24", (const uint8_t*)"\x48\x89\x6c\x24", (const uint8_t*)"\x48\x8b\xc4",
        (const uint8_t*)"\x4c\x8b\xdc", (const uint8_t*)"\x48\x83\xec", (const uint8_t*)"\x48\x81\xec"};
    static const size_t kLen[] = {4, 4, 4, 4, 4, 3, 3, 3, 3};
    std::vector<size_t> out;
    for (size_t a = lo; a < hi; ++a) {
        if (base[a - 1] != 0xCC || base[a] == 0xCC) continue;
        bool ok = false;
        for (size_t k = 0; k < 9 && !ok; ++k) ok = std::memcmp(base + a, kProlog[k], kLen[k]) == 0;
        if (!ok) continue;
        size_t end = a;
        while (end < hi && !(base[end] == 0xCC && base[end + 1] == 0xCC)) ++end;
        if (end - a >= 16) out.push_back(a);
    }
    return out;
}

void InstallTrace(const uint8_t* base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kTraceTimestamp) return;
    std::vector<size_t> targets = TraceTargets(base, 0x1830000, 0x183C000);
    for (size_t a : TraceTargets(base, 0x18E9000, 0x18F0000)) targets.push_back(a);
    for (size_t a : targets) {
        if (g_traceCount >= kTraceMax) break;
        const size_t i = g_traceCount;
        void* t = const_cast<uint8_t*>(base + a);
        if (MH_CreateHook(t, g_traceThunks[i], &g_traceOrig[i]) != MH_OK) continue;
        if (MH_EnableHook(t) != MH_OK) continue;
        g_traceRva[i] = static_cast<uint32_t>(a);
        ++g_traceCount;
    }
    logx::Write("trace: watching %zu backpack / crafting functions", g_traceCount);
}

void TraceHit(uint64_t index) {
    if (index >= g_traceCount) return;
    const int epoch = g_traceEpoch;
    if (g_traceSeen[index].exchange(epoch) != epoch) g_traceHits[index] = 0;
    if (g_traceHits[index]++ >= 3) return;
    logx::Write("trace: 0x%X (%s)", g_traceRva[index],
                g_swappedTo == 1 ? "Ellie" : g_swappedTo == 2 ? "swapped Joel" : "normal player");
}

void Register() {
    using features::Kind;
    const char* back = " To go back, turn this off and use Swap Now or Pause > Restart Checkpoint. Kept for the next "
                       "launch only with Settings > Remember Character Swap.";
    g_asEllie = &features::Add("PlayAsEllie", "Character", "Play as Ellie",
                               (std::string("Joel's chapters use Ellie as the player, in the outfit for the chapter: she "
                                            "moves, aims, shoots and crafts, and Joel's 9mm sits straight in her hands. "
                                            "Applies with Swap Now or at your next checkpoint.") + back).c_str(),
                               Kind::Toggle, 0);
    g_asJoel = &features::Add("PlayAsJoel", "Character", "Play as Joel in Winter",
                              (std::string("Ellie's chapters use Joel as the player.") + back).c_str(), Kind::Toggle, 0);
    // Saved only with Settings > Remember Character Swap (see features::Persist).
    if (!settings::GetBool("Menu", "RememberSwap", false)) g_asEllie->on = g_asJoel->on = false;
    if (g_asEllie->on && g_asJoel->on) g_asJoel->on = false;
    g_outfit = &features::Add("EllieOutfit", "Engine", "Ellie's Outfit",
                              "0 = the outfit this chapter's own Ellie wears (always loaded, so her whole body shows). 1-11 = a "
                              "fixed outfit (01-08, 11, 13, 15); its parts load with the swap. Left / Right to change; "
                              "applies on the next swap.",
                              Kind::Number, 0);
    g_outfit->minValue = 0;
    g_outfit->maxValue = static_cast<int>(sizeof(engine::kEllieOutfits) / sizeof(int));
    g_giveWeapons = &features::Add("SwapGivesPistol", "Engine", "Give Ellie Her Pistol",
                                   "After you become Ellie, also give her her own pistol. Off by default: the game's give "
                                   "function raised an error for it in testing. The 9mm is fixed for her without it.",
                                   Kind::Toggle, 0);
    g_giveRecipes = &features::Add("SwapGivesRecipes", "Engine", "Give Ellie Crafting Recipes",
                                   "After you become Ellie, teach her every crafting recipe (like Inventory > All "
                                   "Recipes) so the crafting menu opens. Saved by the game at the next checkpoint.",
                                   Kind::Toggle, 1);
    g_joelBuddy = &features::Add("ReplaceEllieWithJoel", "Engine", "Replace Campaign Ellie with Joel",
                                 "While you play as Ellie, the story companion Ellie becomes Joel: the same companion "
                                 "(her AI, scripted moments and lines) with Joel's body in the chapter's outfit. Applies "
                                 "with Swap Now or at your next checkpoint. Turn off to keep Ellie as the companion.",
                                 Kind::Toggle, 1);
    g_joelBuddy->alwaysSave = true;
    g_bowOnBack = &features::Add("BowOnBack", "Engine", "Bow On Ellie's Back",
                                 "While you play as Ellie, Joel's bow uses Ellie's own bow settings, so it sits "
                                 "vertically on her back instead of across her backpack.",
                                 Kind::Toggle, 1);
    g_bowOnBack->alwaysSave = true;
    g_bowOnBack->onToggle = [](bool) {
        if (engine::GameLoopHooked()) engine::RunOnGameThread(&SyncPatches);
    };
    // The companion's packages are part of the swap's level-set patch: rebuild it.
    g_joelBuddy->onToggle = [](bool) {
        if (!engine::GameLoopHooked()) return;
        engine::RunOnGameThread([] {
            if (!g_asEllie->on) return; // patch kept for a playing Ellie is never pulled
            for (auto& p : g_elliePatches)
                if (p.applied) {
                    RemovePatches(g_elliePatches); // put back and re-added in the same frame
                    break;
                }
            SyncPatches();
        });
    };
    g_swapNow = &features::Add("SwapNow", "", "Swap Now",
                               "Loads the chosen character's animations, then restarts from your last checkpoint (like "
                               "Pause > Restart Checkpoint) as that character. Progress since that checkpoint is lost. "
                               "Wait for it to finish before pressing it again.",
                               Kind::Action, 0);
    // The level-set patch follows the toggles (see PrepareSwap).
    g_asEllie->onToggle = [](bool on) {
        if (on && g_asJoel->on) features::SetToggle(*g_asJoel, false, false);
        if (engine::GameLoopHooked()) PrepareSwap();
    };
    g_asJoel->onToggle = [](bool on) {
        if (on && g_asEllie->on) features::SetToggle(*g_asEllie, false, false);
        if (engine::GameLoopHooked()) PrepareSwap();
    };
}

void Init() {
    auto fail = [](const std::string& why) {
        for (auto* f : {g_asEllie, g_asJoel, g_outfit, g_giveWeapons, g_giveRecipes, g_swapNow, g_joelBuddy})
            features::MarkUnavailable(*f, why);
    };
    if (!engine::GameLoopHooked()) return fail("The game loop was not found, so the menu cannot change characters safely.");
    const uint8_t* base = nullptr;
    size_t textEnd = 0, size = 0;
    if (!game::ImageInfo(base, textEnd, size)) return fail("Cannot read the game image.");
    BuildMaps();

    // Spawn Player callback -> spawn function -> core spawn, each checked.
    dscan::Index idx(base, textEnd, size);
    idx.WantLabel(engine::kSpawnPlayerLabel);
    idx.WantLabel("Drop All Weapons"); // a known bool-with-callback item, for its constructor
    idx.Build();
    const auto& anchor = idx.Items("Drop All Weapons");
    std::string err;
    const size_t cb = anchor.size() == 1 ? idx.ItemValue(engine::kSpawnPlayerLabel, anchor[0].ctor, false, err) : 0;
    if (!cb) return fail("The developer menu's Spawn Player was not found.");
    const auto call = scan::FindAll(base + cb, 0x60, scan::Parse(engine::kSpawnCall));
    if (call.size() != 1) return fail("Spawn Player's code was not recognised.");
    const size_t spawn = dscan::FirstCall(base, cb + call[0] + 15, 1);
    const auto def = scan::FindAll(base + spawn, 0x100, scan::Parse(engine::kDefaultCharacter));
    uint64_t defaultId = 0;
    if (!def.empty()) std::memcpy(&defaultId, base + spawn + def[0] + 2, 8);
    if (defaultId != H(engine::kJoel)) return fail("The player-spawn function was not recognised (default character changed).");
    const auto coreCall = scan::FindAll(base + spawn, 0x200, scan::Parse(engine::kCoreCall));
    if (coreCall.size() != 1) return fail("The core player spawn was not found.");
    const size_t core = dscan::FirstCall(base, spawn + coreCall[0] + 5, 1);
    if (scan::FindAll(base + core, 25, scan::Parse(engine::kCorePrologue)).size() != 1)
        return fail("The core player spawn has an unexpected layout.");

    const size_t restart = dscan::FindFunctionByName(base, textEnd, size, engine::kRestartName, engine::kRestartPrologue, err);

    void* target = const_cast<uint8_t*>(base + core);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HkCore), reinterpret_cast<void**>(&oCore)) != MH_OK ||
        MH_EnableHook(target) != MH_OK)
        return fail("Hooking the player spawn failed.");
    logx::Write("character: core spawn RVA 0x%zX hooked; restart checkpoint RVA 0x%zX", core, restart);
    g_imageBase = base;
    const auto look = scan::FindAll(base, textEnd, scan::Parse(engine::kLookResolve));
    void* lookFn = look.size() == 1 ? const_cast<uint8_t*>(base + look[0]) : nullptr;
    if (lookFn && MH_CreateHook(lookFn, reinterpret_cast<void*>(&HkResolveLook), reinterpret_cast<void**>(&oResolveLook)) == MH_OK &&
        MH_EnableHook(lookFn) == MH_OK) {
        logx::Write("character: look resolver RVA 0x%zX hooked", look[0]);
        features::MarkAvailable(*g_joelBuddy);
        // The Skins menu's and the level load's look calls (left alone by the hook).
        auto callReturn = [&](const char* pattern, size_t callAt, size_t retAt, const char* what) -> const uint8_t* {
            const auto m = scan::FindAll(base, textEnd, scan::Parse(pattern));
            int32_t rel = 0;
            if (m.size() == 1) std::memcpy(&rel, base + m[0] + callAt + 1, 4);
            if (m.size() == 1 && m[0] + callAt + 5 + rel == look[0]) return base + m[0] + retAt;
            logx::Write("character: %s not recognised (%zu matches)", what, m.size());
            return nullptr;
        };
        g_skinLookReturn = callReturn(engine::kSkinLookCall, engine::kSkinLookCallAt, engine::kSkinLookReturn, "Skins menu look call");
        g_levelLookReturn = callReturn(engine::kLevelLookCall, engine::kLevelLookCallAt, engine::kLevelLookReturn, "level-load look call");
    } else {
        logx::Write("character: look resolver not found (%zu matches)", look.size());
        features::MarkUnavailable(*g_joelBuddy, "The game's look code was not recognised in this version.");
    }
    if (!FindPackageLoader(base, textEnd) || !LoadPackageLists()) g_loaded = nullptr; // no load report
    if (const size_t flag = dscan::FindEllieCraftFlag(base, textEnd, size, err)) {
        g_ellieCraftFlag = const_cast<uint8_t*>(base + flag);
        logx::Write("character: Ellie crafting flag RVA 0x%zX", flag);
    } else {
        logx::Write("character: Ellie crafting flag not found: %s", err.c_str());
    }
    dscan::LevelSetLoader lsl;
    if (dscan::FindLevelSetLoader(base, textEnd, size, lsl, err)) {
        g_levelMgr = reinterpret_cast<void**>(const_cast<uint8_t*>(base + lsl.mgrGlobal));
        g_rebuildWants = reinterpret_cast<RebuildWants>(const_cast<uint8_t*>(base + lsl.rebuildWants));
        logx::Write("character: level manager 0x%zX, SetLevelSet 0x%zX, want-load rebuild 0x%zX", lsl.mgrGlobal,
                    lsl.setLevelSet, lsl.rebuildWants);
    } else {
        logx::Write("character: level-set loader not found: %s", err.c_str());
    }
    BuildPatches();
    for (auto* f : {g_asEllie, g_asJoel, g_outfit, g_giveWeapons, g_giveRecipes}) features::MarkAvailable(*f);
    engine::AddTickHandler(&Tick);
    // Research hooks (crafting / backpack call logging) stay off unless
    // [Research] Trace=1 is set in w1zsMenu.ini.
    g_research = settings::GetBool("Research", "Trace", false);
    if (settings::GetBool("Research", "Overlays", false)) InstallOverlayResearch(base, size);
    if (g_research) {
    HookCraftingCheck(base, textEnd, size);
    HookCraftingNatives(base, textEnd, size);
    {   // research: inventory input handler and IsInState, only on this exact build
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        if (nt->FileHeader.TimeDateStamp == kTraceTimestamp && g_canCraftFn) {
            g_isInState = reinterpret_cast<IsInState>(const_cast<uint8_t*>(base + 0x1EF9174));
            void* t = const_cast<uint8_t*>(base + 0x18EDD38);
            if (MH_CreateHook(t, reinterpret_cast<void*>(&HkInvHandler), reinterpret_cast<void**>(&oInvHandler)) == MH_OK &&
                MH_EnableHook(t) == MH_OK)
                logx::Write("research: inventory input handler watched");
        }
    }
    InstallTrace(base);
    }
    if (g_asEllie->on || g_asJoel->on) {
        logx::Write("character: %s remembered from the last launch", g_asEllie->on ? "Play as Ellie" : "Play as Joel");
        PrepareSwap();
    }

    if (!restart) {
        features::MarkUnavailable(*g_swapNow, "Restart Checkpoint was not found; use Pause > Restart Checkpoint instead.");
        return;
    }
    g_restart = reinterpret_cast<Restart>(const_cast<uint8_t*>(base + restart));
    g_swapNow->onAction = [] {
        engine::RunOnGameThread([] {
            if (!engine::PlayerPresent()) {
                features::Notify("Load a game first");
                return;
            }
            if ((g_asEllie->on || g_asJoel->on) && !g_setsFound) {
                features::Notify("Still preparing the swap; try again in a few seconds");
                PrepareSwap();
                return;
            }
            if (g_swapWait >= 0) return; // already waiting for the packages
            if (g_restarting >= 0) {     // a second restart during the first one hung the game
                features::Notify("Still swapping - one moment");
                return;
            }
            SyncPatches();
            // Stream in the new player's animations first: a checkpoint restart
            // never loads them, and without them the player cannot move.
            if ((g_asEllie->on || g_asJoel->on) && g_loaded && RequestPackages()) {
                g_swapWait = 0;
                features::Notify("Loading animations...");
                return;
            }
            g_reportFrames = 0; // report how many are loaded after the restart
            DoRestart();
        });
    };
    features::MarkAvailable(*g_swapNow);
}

} // namespace character
