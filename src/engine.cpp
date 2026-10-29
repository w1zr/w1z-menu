#include "engine.h"

#include "dscan.h"
#include "engine_defs.h"
#include "features.h"
#include "game.h"
#include "log.h"
#include "scan.h"
#include "skins.h"

#include "../third_party/minhook/include/MinHook.h"

#include <windows.h>
#include <atomic>
#include <cstring>
#include <functional>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace engine {

namespace {

using Callback = bool(__fastcall*)(void* item, int message);
using WeaponLookup = void*(__fastcall*)(uint64_t weaponId);
using GameLoopFn = void(__fastcall*)(uint64_t);

constexpr int kExecute = 5; // developer-menu message: run / toggle

const uint8_t* g_base = nullptr;
GameLoopFn oGameLoop = nullptr;

// ---------------------------------------------------------------- game thread

std::mutex g_queueLock;
std::vector<std::function<void()>> g_queue;
std::atomic<bool> g_loopRunning{false};

void Enqueue(std::function<void()> fn) {
    std::lock_guard<std::mutex> l(g_queueLock);
    if (g_queue.size() < 64) g_queue.push_back(std::move(fn));
}

// Developer-menu callbacks read their argument from the item at +0x60.
struct alignas(16) FakeItem {
    uint8_t bytes[0x100];
};

bool CallGuarded(Callback cb, void* item, int message, bool& result) {
    __try {
        result = cb(item, message);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool LookupGuarded(WeaponLookup fn, uint64_t id, void*& out) {
    __try {
        out = fn(id);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Runs a developer-menu callback with the given item data. Game thread only.
bool RunCallback(Callback cb, uint64_t data, int message, bool* result = nullptr) {
    FakeItem item{};
    std::memcpy(item.bytes + 0x60, &data, sizeof(data));
    bool r = false;
    if (!CallGuarded(cb, &item, message, r)) {
        logx::Write("ERROR: exception in developer-menu callback 0x%zX", reinterpret_cast<const uint8_t*>(cb) - g_base);
        return false;
    }
    if (result) *result = r;
    return true;
}

// ---------------------------------------------------------------- state

struct FlagFeature {
    features::Feature* feature = nullptr;
    std::vector<uint8_t*> bytes; // private bytes the reads were pointed at
};
std::vector<FlagFeature> g_flags;

Callback g_giveItem = nullptr;
Callback g_giveWeapon = nullptr;
WeaponLookup g_weaponLookup = nullptr;
std::atomic<bool> g_weaponsChecked{false};

Callback g_manualCamera = nullptr;
features::Feature* g_cameraFeature = nullptr;

uint8_t* const* g_hudObject = nullptr; // global pointer; +g_hudOffset is "Disable HUD2 Draw"
uint8_t g_hudOffset = 0;
features::Feature* g_hideHud = nullptr;
std::atomic<bool> g_hudReset{false};

float* g_timeScale = nullptr;

// ---------------------------------------------------------------- game loop

void CheckWeapons() {
    // Weapon definitions load with the game, not the main menu. Once a known
    // weapon resolves, grey out the names this game does not have.
    void* probe = nullptr;
    static int attempts = 0;
    const bool called = LookupGuarded(g_weaponLookup, dscan::Fnv1a64(kWeaponProbe), probe);
    if (++attempts % 10 == 1) logx::Write("weapon lookup probe (%s): %s", kWeaponProbe, !called ? "exception" : probe ? "found" : "not loaded yet");
    if (!called || !probe) return;
    int found = 0, total = 0;
    for (const auto& g : kGives) {
        if (!IsWeapon(g)) continue;
        features::Feature* f = features::Find(g.id);
        void* def = nullptr;
        if (!f) continue;
        ++total;
        if (LookupGuarded(g_weaponLookup, dscan::Fnv1a64(g.name), def) && def) {
            features::MarkAvailable(*f);
            ++found;
        } else {
            features::MarkUnavailable(*f, std::string("This game has no weapon called ") + g.name + ".");
        }
    }
    g_weaponsChecked = true;
    logx::Write("weapon list checked: %d of %d weapons found", found, total);
}

void Tick() {
    static unsigned frame = 0;
    ++frame;
    if (g_weaponLookup && !g_weaponsChecked && frame % 60 == 0) CheckWeapons();

    // Hide HUD: the developer menu's "Disable HUD2 Draw" byte in the HUD object.
    if (g_hudObject && g_hideHud) {
        uint8_t* hud = *g_hudObject;
        if (hud) {
            if (g_hideHud->on) hud[g_hudOffset] = 1;
            else if (g_hudReset.exchange(false)) hud[g_hudOffset] = 0;
        }
    }

    // Keep the Manual Camera toggle in step with the game (it can be left by
    // other means, e.g. a cutscene).
    if (g_manualCamera && g_cameraFeature && frame % 30 == 0) {
        bool active = false;
        if (RunCallback(g_manualCamera, 0, 0, &active)) g_cameraFeature->on = active;
    }

    std::vector<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> l(g_queueLock);
        work.swap(g_queue);
    }
    for (auto& fn : work) fn();
}

void __fastcall HkGameLoop(uint64_t arg) {
    if (!g_loopRunning.exchange(true)) logx::Write("game loop hook running");
    Tick();
    oGameLoop(arg);
}

// ---------------------------------------------------------------- setup

struct Image {
    const uint8_t* base;
    size_t textEnd, size;
};

bool Writable(const uint8_t* p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    const DWORD prot = mbi.Protect & 0xFF;
    return prot == PAGE_READWRITE || prot == PAGE_WRITECOPY;
}

bool Redirect(const Image& img, const std::vector<dscan::Read>& reads, const uint8_t* target) {
    // All displacements are computed first so a feature is patched fully or not at all.
    std::vector<int32_t> disps;
    for (const auto& r : reads) {
        const int64_t d = reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(img.base + r.end);
        if (d != static_cast<int32_t>(d)) return false;
        disps.push_back(static_cast<int32_t>(d));
    }
    for (size_t i = 0; i < reads.size(); ++i) {
        auto* at = const_cast<uint8_t*>(img.base + reads[i].dispOffset);
        DWORD old = 0;
        if (!VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
        std::memcpy(at, &disps[i], 4);
        VirtualProtect(at, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, 4);
    }
    return true;
}

features::Feature& Toggle(const char* id, const char* label, const char* help, bool saved = true) {
    return features::Add(id, saved ? "Engine" : "", label, help, features::Kind::Toggle, 0);
}

features::Feature& Action(const char* id, const char* label, const char* help) {
    return features::Add(id, "", label, help, features::Kind::Action, 0);
}

} // namespace

void Register() {
    skins::Register();
    // Player
    Toggle("GodMode", "God Mode", "Take no damage (the engine's Invincible Player).");
    Toggle("NoHitReactions", "No Hit Reactions", "No stagger or flinch when hit while God Mode is on.");
    Toggle("InfiniteSprint", "Infinite Sprint", "Sprint without running out of breath.");
    Toggle("Undetectable", "Undetectable Player", "Enemies do not notice you.");
    Toggle("OneHitKills", "One Hit Kills", "Every hit kills.");
    // Weapons
    Toggle("InfiniteWeaponHealth", "Infinite Weapon Health", "Melee weapons never break.");
    Toggle("NoWeaponSway", "Disable Weapon Sway", "Steady aim with no sway.");
    Toggle("NoAutoReload", "Disable Auto Reload", "Empty guns wait for you to reload.");
    Toggle("NoAutoHolster", "Disable Auto Holster", "Keep your weapon out.");
    Action("DropAllWeapons", "Drop All Weapons", "Drop every weapon you carry.");
    // Items / inventory
    Toggle("InfiniteCrafting", "Infinite Crafting", "Crafting uses no materials.");
    Toggle("InfiniteCraftables", "Infinite Craftables", "Thrown and used crafted items are not used up.");
    const char* liveNote = " Changes the current game; the game saves it at its next checkpoint.";
    Action("GiveAllInventory", "Give All + Upgrades", (std::string("Every item, weapon and upgrade.") + liveNote).c_str());
    Action("GiveEachCraftable", "Give Each Craftable", (std::string("One of every craftable item.") + liveNote).c_str());
    Action("Parts10", "Weapon Parts x10", (std::string("Add 10 parts.") + liveNote).c_str());
    Action("Parts100", "Weapon Parts x100", (std::string("Add 100 parts.") + liveNote).c_str());
    Action("Pills10", "Pills x10", (std::string("Add 10 supplements.") + liveNote).c_str());
    Action("Pills100", "Pills x100", (std::string("Add 100 supplements.") + liveNote).c_str());
    Action("AllRecipes", "All Recipes", (std::string("Learn every crafting recipe.") + liveNote).c_str());
    Action("NoRecipes", "No Recipes", (std::string("Forget every crafting recipe.") + liveNote).c_str());
    for (const auto& g : kGives) {
        const std::string help = std::string("Give ") + g.label + " (" + g.name + ")." + liveNote;
        Action(g.id, g.label, help.c_str());
    }
    // Unlocks
    Action("GiveTreasures", "Give All Treasures", (std::string("Collect every treasure.") + liveNote).c_str());
    Action("GiveJournal", "Give All Journal Pages", (std::string("Collect every journal page.") + liveNote).c_str());
    Action("RemoveTreasures", "Remove All Treasures", (std::string("Forget collected treasures.") + liveNote).c_str());
    Action("ResetJournal", "Reset Journal", (std::string("Forget collected journal pages.") + liveNote).c_str());
    Toggle("AllChapters", "Unlock All Chapters", "Chapter Select shows every chapter while this is on. Not saved to your profile.", false);
    Toggle("AllEncounters", "Unlock All Encounters", "Every Encounter is available while this is on. Not saved to your profile.", false);
    Toggle("AllExtras", "Unlock All Extras", "Every Extras page is open while this is on (skins: see Unlock Skins). Not saved to your profile.", false);
    // Camera
    Toggle("ManualCamera", "Manual Camera", "Fly the camera anywhere (the engine's debug camera).", false);
    Action("TeleportToCamera", "Teleport Player To Camera", "Move the player to where the camera is.");
    Toggle("HideHud", "Hide HUD", "Hide the game's HUD.", false);
    // Scenes / misc
    Action("ReplayScene", "Replay Current Scene", "Restart the current scene from its start.");
    auto& speed = features::Add("GameSpeed", "", "Game Speed", "Speed of the game clock in percent. Experimental.",
                                features::Kind::Number, 100);
    speed.minValue = 10;
    speed.maxValue = 300;
    speed.step = 10;
}

void Init() {
    Image img{};
    if (!game::ImageInfo(img.base, img.textEnd, img.size)) {
        logx::Write("engine: cannot read the game image");
        return;
    }
    g_base = img.base;
    const auto rva = [&](size_t off) { return reinterpret_cast<uint64_t>(img.base) + off; };

    dscan::Index idx(img.base, img.textEnd, img.size);
    for (const auto& f : kFlags) idx.WantLabel(f.label);
    for (const auto& a : kActions) idx.WantLabel(a.label);
    for (const char* l : {"Infinite Sprint", "Drop All Weapons", "Give Each Craftable", "Manual Camera", "Disable HUD2 Draw",
                          "Time Scale", "Sync Load Weapons"})
        idx.WantLabel(l);
    const ULONGLONG t0 = GetTickCount64();
    idx.Build(); // labels first, so the flags can be resolved from them

    // Constructors, from items whose kind is known.
    std::string err;
    const auto ctorOf = [&](const char* label) {
        const auto& items = idx.Items(label);
        return items.size() == 1 ? items[0].ctor : 0;
    };
    const size_t itemBool = ctorOf("Infinite Sprint");
    const size_t itemBoolCb = ctorOf("Drop All Weapons");
    const size_t itemFunction = ctorOf("Give Each Craftable");
    if (!itemBool || !itemBoolCb || !itemFunction || itemBool == itemBoolCb || itemBool == itemFunction ||
        itemBoolCb == itemFunction) {
        logx::Write("engine: developer menu not recognised; engine features stay off");
        features::ForEach([](features::Feature& f) {
            if (f.section == "Engine" || (f.section.empty() && !f.available && f.reason == "Not loaded yet."))
                features::MarkUnavailable(f, "The game's developer menu code was not found (unsupported game version?).");
        });
        return;
    }

    // Flags, then a second pass for their references.
    struct Pending {
        const FlagDef* def;
        std::vector<size_t> flags;
        std::string error;
    };
    std::vector<Pending> pending;
    for (const auto& f : kFlags) {
        Pending p{&f, {}, {}};
        if (f.src == Src::BoolR8) {
            if (size_t v = idx.ItemValue(f.label, itemBool, false, p.error)) p.flags.push_back(v);
        } else if (f.src == Src::BoolCbR9) {
            if (size_t v = idx.ItemValue(f.label, itemBoolCb, true, p.error)) p.flags.push_back(v);
        } else {
            // Every copy of the callback must compare the same three flags.
            std::vector<size_t> first;
            for (const auto& it : idx.Items(f.label)) {
                if (it.ctor != itemBoolCb || !it.r8) continue;
                auto fl = dscan::CallbackFlags(img.base, it.r8, 0x30, img.size);
                if (first.empty()) first = fl;
                else if (fl != first) first.clear();
            }
            if (first.size() == 3) p.flags = first;
            else p.error = std::string("developer-menu item \"") + f.label + "\" has an unexpected layout";
        }
        pending.push_back(p);
    }
    dscan::Index refs(img.base, img.textEnd, img.size);
    for (const auto& p : pending)
        for (size_t fl : p.flags) refs.WantFlag(fl);
    refs.Build();

    // Skins first: they take over two reads of the Extras flag.
    const std::vector<size_t> skinReads = skins::Install();

    uint8_t* priv = game::AllocDataNearImage();
    size_t used = 0;
    for (const auto& p : pending) {
        features::Feature* feat = features::Find(p.def->id);
        if (!feat) continue;
        if (p.flags.empty()) {
            features::MarkUnavailable(*feat, p.error + ".");
            continue;
        }
        if (std::strcmp(p.def->id, "AllExtras") == 0 && game::GameFolderModLoaded(L"xinput1_4.dll", "TLOU Outfit Changer")) {
            features::MarkUnavailable(*feat, "Your TLOU Outfit Changer mod is installed and already unlocks skins.");
            continue;
        }
        std::string why;
        for (size_t i = 0; i < p.flags.size() && why.empty(); ++i) {
            const auto& r = refs.FlagRefs(p.flags[i]);
            if (p.flags[i] >= img.size || !Writable(img.base + p.flags[i])) why = "its flag is not in game data";
            else if (static_cast<int>(r.reads.size()) != p.def->reads[i])
                why = "game code changed (" + std::to_string(r.reads.size()) + " reads, expected " + std::to_string(p.def->reads[i]) + ")";
        }
        if (!why.empty() || !priv || used + p.flags.size() > 0x1000) {
            features::MarkUnavailable(*feat, why.empty() ? "No memory near the game image." : "Not available: " + why + ".");
            continue;
        }
        FlagFeature ff{feat, {}};
        bool ok = true;
        for (size_t fl : p.flags) {
            uint8_t* b = priv + used++;
            *b = feat->on ? 1 : 0;
            std::vector<dscan::Read> reads = refs.FlagRefs(fl).reads;
            reads.erase(std::remove_if(reads.begin(), reads.end(),
                                       [&](const dscan::Read& r) {
                                           return std::find(skinReads.begin(), skinReads.end(), r.dispOffset) != skinReads.end();
                                       }),
                        reads.end());
            ok = ok && Redirect(img, reads, b);
            ff.bytes.push_back(b);
            logx::Write("engine: %s flag RVA 0x%zX, %zu reads redirected", p.def->id, fl, refs.FlagRefs(fl).reads.size());
        }
        if (!ok) {
            features::MarkUnavailable(*feat, "Patching the game failed.");
            continue;
        }
        g_flags.push_back(ff);
        auto bytes = ff.bytes;
        feat->onToggle = [bytes](bool on) {
            for (uint8_t* b : bytes) *b = on ? 1 : 0;
        };
        features::MarkAvailable(*feat);
    }

    // Game loop hook: everything that calls game functions runs from there.
    const size_t loop = dscan::FindFunctionByName(img.base, img.textEnd, img.size, kGameLoopName, kGameLoopPrologue, err);
    bool loopOk = false;
    if (loop) {
        void* target = const_cast<uint8_t*>(img.base + loop);
        loopOk = MH_CreateHook(target, reinterpret_cast<void*>(&HkGameLoop), reinterpret_cast<void**>(&oGameLoop)) == MH_OK &&
                 MH_EnableHook(target) == MH_OK;
        logx::Write("engine: GameLoopUpdate at RVA 0x%zX %s", loop, loopOk ? "hooked" : "could NOT be hooked");
    } else {
        logx::Write("engine: %s", err.c_str());
    }
    const std::string noLoop = "The game loop was not found, so the menu cannot run game functions safely.";

    // Actions.
    for (const auto& a : kActions) {
        features::Feature* feat = features::Find(a.id);
        if (!feat) continue;
        std::string e;
        const size_t cb = idx.ItemValue(a.label, a.boolCallback ? itemBoolCb : itemFunction, false, e);
        if (!loopOk || !cb) {
            features::MarkUnavailable(*feat, !loopOk ? noLoop : e + ".");
            continue;
        }
        auto fn = reinterpret_cast<Callback>(rva(cb));
        const uint64_t data = a.dataText ? dscan::Fnv1a64(a.dataText) : 0;
        const std::string label = feat->label;
        feat->onAction = [fn, data, label] {
            Enqueue([fn, data, label] {
                if (RunCallback(fn, data, kExecute)) features::Notify(label);
            });
        };
        features::MarkAvailable(*feat);
        if (std::strcmp(a.id, "Parts10") == 0) g_giveItem = fn;
    }

    // Give weapon / item.
    {
        // The give-weapon callback is the function that reads "Sync Load Weapons".
        std::string e;
        const size_t syncFlag = idx.ItemValue("Sync Load Weapons", itemBool, false, e);
        if (syncFlag) {
            dscan::Index wr(img.base, img.textEnd, img.size);
            wr.WantFlag(syncFlag);
            wr.Build();
            const scan::Pattern pro = scan::Parse(kGiveWeaponPrologue);
            const scan::Pattern call = scan::Parse(kWeaponLookupCall);
            for (const auto& rd : wr.FlagRefs(syncFlag).reads) {
                // Closest function start before the read with the expected prologue.
                size_t j = 0;
                for (size_t k = rd.dispOffset; k + 0x200 > rd.dispOffset && k > 0; --k)
                    if (scan::FindAll(img.base + k, pro.bytes.size(), pro).size() == 1) {
                        j = k;
                        break;
                    }
                if (!j) continue;
                const auto c = scan::FindAll(img.base + j, 0x80, call);
                if (c.size() != 1) continue;
                const size_t lookup = dscan::FirstCall(img.base, j + c[0] + 7, 1);
                g_giveWeapon = reinterpret_cast<Callback>(rva(j));
                g_weaponLookup = reinterpret_cast<WeaponLookup>(rva(lookup));
                logx::Write("engine: give-weapon callback RVA 0x%zX, weapon lookup RVA 0x%zX", j, lookup);
                break;
            }
        }
        for (const auto& g : kGives) {
            features::Feature* feat = features::Find(g.id);
            if (!feat) continue;
            const Callback fn = IsWeapon(g) ? g_giveWeapon : g_giveItem;
            if (!loopOk || !fn) {
                features::MarkUnavailable(*feat, !loopOk ? noLoop : "The game's give function was not found.");
                continue;
            }
            const uint64_t data = dscan::Fnv1a64(g.name);
            const std::string label = std::string("Gave ") + g.label;
            feat->onAction = [fn, data, label] {
                Enqueue([fn, data, label] {
                    if (RunCallback(fn, data, kExecute)) features::Notify(label);
                });
            };
            // Weapons are confirmed against the game's weapon list once a game is loaded.
            if (IsWeapon(g)) features::MarkUnavailable(*feat, "Load a game first; the weapon list is checked then.");
            else features::MarkAvailable(*feat);
        }
    }

    // Manual Camera: toggled through its developer-menu callback.
    if (features::Feature* cam = features::Find("ManualCamera")) {
        std::string e;
        const size_t cb = idx.ItemValue("Manual Camera", itemBoolCb, false, e);
        if (loopOk && cb) {
            g_manualCamera = reinterpret_cast<Callback>(rva(cb));
            g_cameraFeature = cam;
            cam->onToggle = [](bool on) {
                Enqueue([on] {
                    bool active = false;
                    if (RunCallback(g_manualCamera, 0, 0, &active) && active != on) RunCallback(g_manualCamera, 0, kExecute);
                });
            };
            features::MarkAvailable(*cam);
        } else {
            features::MarkUnavailable(*cam, !loopOk ? noLoop : e + ".");
        }
    }

    // Hide HUD: "Disable HUD2 Draw" is a byte in the HUD object:
    // mov r8,[rip+global] ; lea rdx,[label] ; add r8, imm8
    if (features::Feature* hud = features::Find("HideHud")) {
        const auto& items = idx.Items("Disable HUD2 Draw");
        const uint8_t* m = img.base;
        if (loopOk && items.size() == 1) {
            const size_t s = items[0].site;
            if (m[s - 7] == 0x4C && m[s - 6] == 0x8B && m[s - 5] == 0x05 && m[s + 7] == 0x49 && m[s + 8] == 0x83 &&
                m[s + 9] == 0xC0 && items[0].ctor == itemBool) {
                int32_t d;
                std::memcpy(&d, m + s - 4, 4);
                const size_t global = static_cast<size_t>(static_cast<int64_t>(s) + d);
                if (global < img.size && Writable(m + global)) {
                    g_hudObject = reinterpret_cast<uint8_t* const*>(img.base + global);
                    g_hudOffset = m[s + 10];
                    g_hideHud = hud;
                    hud->onToggle = [](bool on) {
                        if (!on) g_hudReset = true;
                    };
                    features::MarkAvailable(*hud);
                }
            }
        }
        if (!hud->available) features::MarkUnavailable(*hud, !loopOk ? noLoop : "The HUD switch was not found.");
    }

    // Game speed: the developer menu's "Time Scale" float.
    if (features::Feature* speed = features::Find("GameSpeed")) {
        std::string e;
        const auto& items = idx.Items("Time Scale");
        const size_t v = items.size() == 1 ? items[0].r8 : 0;
        if (v && v < img.size && Writable(img.base + v)) {
            g_timeScale = reinterpret_cast<float*>(const_cast<uint8_t*>(img.base + v));
            speed->onNumber = [](int pct) { *g_timeScale = pct / 100.f; };
            features::MarkAvailable(*speed);
        } else {
            features::MarkUnavailable(*speed, "The game's time scale was not found.");
        }
    }

    logx::Write("engine: scan and patches took %llu ms", GetTickCount64() - t0);
}

} // namespace engine
