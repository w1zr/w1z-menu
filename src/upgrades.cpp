#include "upgrades.h"

#include "dscan.h"
#include "engine.h"
#include "engine_defs.h"
#include "features.h"
#include "game.h"
#include "log.h"
#include "scan.h"

#include <windows.h>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace upgrades {
namespace {

// (player, weapon id, bundle id); the natives check the player for null.
using BundleFn = void(__fastcall*)(void* player, uint64_t weapon, uint64_t bundle);
BundleFn g_apply = nullptr, g_remove = nullptr;
std::vector<features::Feature*> g_all;

std::vector<std::string> Bundles(const engine::UpgradeWeapon& w) {
    std::vector<std::string> out;
    std::istringstream in(w.bundles);
    for (std::string b; in >> b;) out.push_back(b);
    return out;
}

// "clip-capacity-2" -> "Magazine Capacity 2", "arrow-explosive" -> "Explosive Arrows".
std::string Pretty(const std::string& bundle) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"clip-capacity", "Magazine Capacity"}, {"fire-rate", "Fire Rate"},     {"reload-speed", "Reload Speed"},
        {"penetration", "Penetration"},          {"recoil", "Less Recoil"},      {"spread", "Tighter Spread"},
        {"range-bow-ellie", "Range"},            {"range", "Range"},             {"damage", "Damage"},
        {"scope", "Scope"},                      {"zoom", "Scope (Zoom)"},       {"drawspeed", "Draw Speed"},
        {"rangefinder", "Rangefinder"},          {"arrow-explosive", "Explosive Arrows"},
        {"short-gun-second-holster", "Second Holster (Short Gun)"},
        {"long-gun-second-holster", "Second Holster (Long Gun)"}};
    for (const auto& [key, name] : kNames) {
        const std::string k = key;
        if (bundle == k) return name;
        if (bundle.size() > k.size() + 1 && bundle.compare(0, k.size(), k) == 0 && bundle[k.size()] == '-' &&
            std::isdigit(static_cast<unsigned char>(bundle[k.size() + 1])))
            return std::string(name) + " " + bundle.substr(k.size() + 1);
    }
    return bundle;
}

// "scope-2" -> {"scope-1", "scope-2"}: lower levels first, like the workbench.
std::vector<std::string> WithLowerLevels(const std::string& bundle, const std::vector<std::string>& all) {
    const size_t dash = bundle.rfind('-');
    if (dash == std::string::npos || dash + 1 >= bundle.size() || !std::isdigit(static_cast<unsigned char>(bundle[dash + 1])))
        return {bundle};
    const std::string stem = bundle.substr(0, dash + 1);
    const int level = std::atoi(bundle.c_str() + dash + 1);
    std::vector<std::string> out;
    for (int l = 1; l <= level; ++l) {
        const std::string b = stem + std::to_string(l);
        for (const auto& a : all)
            if (a == b) out.push_back(b);
    }
    return out.empty() ? std::vector<std::string>{bundle} : out;
}

bool CallBundle(BundleFn fn, void* player, const char* weapon, const std::string& bundle) {
    __try {
        fn(player, dscan::Fnv1a64(weapon), dscan::Fnv1a64(bundle.c_str()));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Apply(const engine::UpgradeWeapon& w, std::vector<std::string> bundles, bool remove, std::string what) {
    const char* weapon = w.weapon;
    std::string label = w.label;
    engine::RunOnGameThread([weapon, label, bundles, remove, what] {
        void* player = engine::Player();
        if (!player) {
            features::Notify("Load a game first");
            return;
        }
        for (const auto& b : bundles) {
            const bool ok = CallBundle(remove ? g_remove : g_apply, player, weapon, b);
            logx::Write("upgrades: %s %s on %s: %s", remove ? "remove" : "apply", b.c_str(), weapon, ok ? "ok" : "FAILED");
            if (!ok) {
                features::Notify("Upgrade failed: " + label);
                return;
            }
        }
        features::Notify(label + ": " + what);
    });
}

} // namespace

void Register() {
    using features::Kind;
    const char* note = " Changes the live game; the game saves it at your next checkpoint, so try it in Encounters or a "
                       "test save first. If you don't have the gun yet, it gets the upgrade when you pick it up.";
    for (const auto& w : engine::kUpgradeWeapons) {
        const auto bundles = Bundles(w);
        for (const auto& b : bundles) {
            const std::string id = std::string("Upg_") + w.id + "_" + b;
            const std::string name = Pretty(b);
            const auto steps = WithLowerLevels(b, bundles);
            std::string help = "Adds " + name + " to the " + w.label + " (" + b + ")";
            if (steps.size() > 1) help += ", with the levels below it";
            auto& f = features::Add(id.c_str(), "", name.c_str(), (help + "." + note).c_str(), Kind::Action);
            f.onAction = [&w, steps, name] { Apply(w, steps, false, name + " added"); };
            g_all.push_back(&f);
        }
        const std::string id = std::string("Upg_") + w.id + "_RemoveAll";
        auto& f = features::Add(id.c_str(), "", "Remove All Upgrades",
                                (std::string("Takes every upgrade above off the ") + w.label + "." + note).c_str(), Kind::Action);
        f.onAction = [&w, bundles] { Apply(w, std::vector<std::string>(bundles.rbegin(), bundles.rend()), true, "upgrades removed"); };
        g_all.push_back(&f);
    }
}

void Init() {
    auto fail = [](const std::string& why) {
        for (auto* f : g_all) features::MarkUnavailable(*f, why);
        logx::Write("upgrades: unavailable: %s", why.c_str());
    };
    if (!engine::GameLoopHooked()) return fail("The game loop was not found, so upgrades cannot be applied safely.");
    const uint8_t* base = nullptr;
    size_t textEnd = 0, size = 0;
    if (!game::ImageInfo(base, textEnd, size)) return fail("Cannot read the game image.");
    std::string err;
    const size_t apply = dscan::FindScriptNative(base, textEnd, size, engine::kUpgradeApplyName, err);
    const size_t remove = apply ? dscan::FindScriptNative(base, textEnd, size, engine::kUpgradeRemoveName, err) : 0;
    if (!apply || !remove) return fail("The game's weapon upgrade functions were not found (" + err + ").");
    if (scan::FindAll(base + apply, 0x30, scan::Parse(engine::kUpgradeApplyStart)) != std::vector<size_t>{0} ||
        scan::FindAll(base + remove, 0x30, scan::Parse(engine::kUpgradeRemoveStart)) != std::vector<size_t>{0})
        return fail("The game's weapon upgrade functions have an unexpected layout.");
    g_apply = reinterpret_cast<BundleFn>(const_cast<uint8_t*>(base + apply));
    g_remove = reinterpret_cast<BundleFn>(const_cast<uint8_t*>(base + remove));
    for (auto* f : g_all) features::MarkAvailable(*f);
    logx::Write("upgrades: apply RVA 0x%zX, remove RVA 0x%zX, %zu options", apply, remove, g_all.size());
}

} // namespace upgrades
