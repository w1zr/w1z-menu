#include "character.h"

#include "dscan.h"
#include "engine.h"
#include "engine_defs.h"
#include "features.h"
#include "game.h"
#include "log.h"
#include "scan.h"

#include "../third_party/minhook/include/MinHook.h"

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace character {

namespace {

using CoreSpawn = void*(__fastcall*)(void*, void*, void*, void*, uint64_t, void*);
using Restart = bool(__fastcall*)(bool, bool);

CoreSpawn oCore = nullptr;
Restart g_restart = nullptr;

features::Feature* g_asEllie = nullptr;
features::Feature* g_asJoel = nullptr;
features::Feature* g_outfit = nullptr;
features::Feature* g_giveWeapons = nullptr;
features::Feature* g_swapNow = nullptr;

// Character id -> name, for every Joel / Ellie player character the menu knows.
std::map<uint64_t, std::string> g_names;
std::map<uint64_t, uint64_t> g_toEllie, g_toJoel;
std::vector<std::string> g_ellieList; // outfit choices for "Ellie's Outfit" (index 1..n)
std::atomic<bool> g_spawnedEllie{false};
std::atomic<bool> g_giveAfterSpawn{false};

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
    g_names[H(kJoel)] = kJoel;
    g_names[H(kEllie)] = kEllie;
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

// Every player spawn passes through here with the character to spawn.
void* __fastcall HkCore(void* a, void* b, void* c, void* d, uint64_t id, void* f) {
    uint64_t use = id;
    if (g_asEllie->on) {
        auto it = g_toEllie.find(id);
        if (it != g_toEllie.end()) {
            const int pick = g_outfit->value;
            use = (pick >= 1 && pick <= static_cast<int>(g_ellieList.size())) ? H(g_ellieList[pick - 1]) : it->second;
        }
    } else if (g_asJoel->on) {
        auto it = g_toJoel.find(id);
        if (it != g_toJoel.end()) use = it->second;
    }
    logx::Write("player spawn: %s%s%s", NameOf(id).c_str(), use != id ? " -> " : "", use != id ? NameOf(use).c_str() : "");
    g_spawnedEllie = NameOf(use).rfind("t1x-ellie", 0) == 0;
    if (use != id && g_spawnedEllie && g_giveWeapons->on) g_giveAfterSpawn = true;
    return oCore(a, b, c, d, use, f);
}

void GiveEllieKit() {
    for (const char* id : {"GiveElliePistol", "GiveSwitchblade"}) {
        features::Feature* f = features::Find(id);
        if (f && f->available && f->onAction) f->onAction();
    }
}

// Ellie's weapons are given once the swapped player is up and running.
void Tick() {
    static int wait = 0;
    if (!g_giveAfterSpawn) {
        wait = 0;
        return;
    }
    if (!engine::PlayerPresent() || ++wait < 90) return; // ~1.5 s after the spawn
    g_giveAfterSpawn = false;
    GiveEllieKit();
}

} // namespace

void Register() {
    using features::Kind;
    const char* warn = " EXPERIMENTAL - may not work: Ellie may be unable to move in Joel's chapters. To go back, turn "
                       "this off and use Pause > Restart Checkpoint. Not saved between launches.";
    g_asEllie = &features::Add("PlayAsEllie", "", "Play as Ellie (Experimental)",
                               (std::string("Joel's chapters use Ellie as the player, in the outfit for the chapter. "
                                            "Applies with Swap Now or at your next checkpoint.") + warn).c_str(),
                               Kind::Toggle, 0);
    g_asJoel = &features::Add("PlayAsJoel", "", "Play as Joel in Winter (Experimental)",
                              (std::string("Ellie's chapters use Joel as the player.") + warn).c_str(), Kind::Toggle, 0);
    g_outfit = &features::Add("EllieOutfit", "Engine", "Ellie's Outfit",
                              "0 = match the chapter. 1-11 = a fixed outfit (01-08, 11, 13, 15). Left / Right to change; "
                              "applies on the next swap.",
                              Kind::Number, 0);
    g_outfit->minValue = 0;
    g_outfit->maxValue = static_cast<int>(sizeof(engine::kEllieOutfits) / sizeof(int));
    // Giving weapons right after the swap raised an exception in testing, so it
    // stays off and is not shown; use Weapons and Items > Give Ellie instead.
    g_giveWeapons = &features::Add("SwapGivesWeapons", "", "Give Ellie Her Weapons",
                                   "After you become Ellie, give her her pistol and switchblade.", Kind::Toggle, 0);
    g_swapNow = &features::Add("SwapNow", "", "Swap Now (Experimental)",
                               "Restarts from your last checkpoint (like Pause > Restart Checkpoint) with the chosen "
                               "character. Progress since that checkpoint is lost. EXPERIMENTAL - may not work; avoid "
                               "pressing it twice in a row.",
                               Kind::Action, 0);
    g_asEllie->onToggle = [](bool on) {
        if (on && g_asJoel->on) features::SetToggle(*g_asJoel, false, false);
    };
    g_asJoel->onToggle = [](bool on) {
        if (on && g_asEllie->on) features::SetToggle(*g_asEllie, false, false);
    };
}

void Init() {
    auto fail = [](const std::string& why) {
        for (auto* f : {g_asEllie, g_asJoel, g_outfit, g_giveWeapons, g_swapNow}) features::MarkUnavailable(*f, why);
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
    for (auto* f : {g_asEllie, g_asJoel, g_outfit, g_giveWeapons}) features::MarkAvailable(*f);
    engine::AddTickHandler(&Tick);

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
            logx::Write("swap: restarting checkpoint (%s)", g_asEllie->on ? "Ellie" : g_asJoel->on ? "Joel in Ellie's chapters" : "story characters");
            features::Notify("Swapping...");
            g_restart(false, false);
        });
    };
    features::MarkAvailable(*g_swapNow);
}

} // namespace character
