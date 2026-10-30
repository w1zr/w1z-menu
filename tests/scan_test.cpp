// Offline test for the signature scanner. Maps a game executable from disk the
// same way Windows lays it out in memory and checks that the flags resolve,
// that they match the values verified by disassembly for known builds, and
// that a damaged image is rejected instead of yielding a bogus address.
//
// usage: scan_test.exe <path to tlou-i.exe> [<path to tlou-i-l.exe> ...]
#include "../src/dscan.h"
#include "../src/engine_defs.h"
#include "../src/scan.h"
#include "../src/skinscan.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...)                                     \
    do {                                                     \
        if (cond) { std::printf("  PASS  "); }               \
        else { std::printf("  FAIL  "); ++g_failures; }      \
        std::printf(__VA_ARGS__);                            \
        std::printf("\n");                                   \
    } while (0)

static bool MapImage(const char* path, std::vector<uint8_t>& image, size_t& textEnd, uint32_t& stamp) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    stamp = nt->FileHeader.TimeDateStamp;
    image.assign(nt->OptionalHeader.SizeOfImage, 0);
    std::memcpy(image.data(), file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    textEnd = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const size_t n = (std::min)(sec[i].SizeOfRawData, sec[i].Misc.VirtualSize);
        std::memcpy(image.data() + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData, n);
        if (!textEnd && (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
            textEnd = sec[i].VirtualAddress + sec[i].Misc.VirtualSize;
    }
    return true;
}

static void TestParser() {
    std::printf("parser\n");
    auto p = scan::Parse("32 C9 ?? 0d");
    CHECK(p.bytes.size() == 4 && p.bytes[0] == 0x32 && p.bytes[2] == -1 && p.bytes[3] == 0x0D, "parse hex and wildcards");
    const uint8_t buf[] = {0x00, 0x32, 0xC9, 0x55, 0x0D, 0x32, 0xC9, 0x00, 0x0D};
    auto hits = scan::FindAll(buf, sizeof(buf), p);
    CHECK(hits.size() == 2 && hits[0] == 1 && hits[1] == 5, "FindAll finds both wildcard matches");
    CHECK(scan::FindAll(buf, 3, p).empty(), "FindAll does not read past the buffer");
}

struct Known { uint32_t stamp; size_t infinite; size_t noReload; };
// Verified by disassembly of Steam build 18995818 (file version 1.1.4.0).
static const Known kKnown[] = {
    {0x685BEBE1, 0x362241D, 0x3622421},
};

static void TestImage(const char* path) {
    std::printf("image %s\n", path);
    std::vector<uint8_t> image;
    size_t textEnd = 0;
    uint32_t stamp = 0;
    if (!MapImage(path, image, textEnd, stamp)) {
        CHECK(false, "could not read file");
        return;
    }
    auto r = scan::LocateFlags(image.data(), textEnd, image.size());
    CHECK(r.ok, "flags located%s%s", r.ok ? "" : ": ", r.error.c_str());
    if (!r.ok) return;
    std::printf("        InfiniteAmmo RVA 0x%zX, NoReload RVA 0x%zX, timestamp 0x%08X\n", r.infiniteAmmo, r.noReload, stamp);
    CHECK(r.infiniteAmmo > textEnd && r.noReload > textEnd, "flags resolve outside the code section");
    for (const auto& k : kKnown) {
        if (k.stamp == stamp)
            CHECK(r.infiniteAmmo == k.infinite && r.noReload == k.noReload, "matches disassembly for this build");
    }
    CHECK(r.bottomless == r.infiniteAmmo + 1, "bottomless-reserve flag sits right after Infinite Ammo");
    CHECK(r.modifierOffset == 0x4968 && r.optionsPtr > textEnd, "options pointer resolved, modifier offset 0x%X", r.modifierOffset);

    // HUD rewrite: every site found, and each rewritten instruction must point
    // rip-relative at the Infinite Ammo flag with the original operand kept.
    std::string hudErr;
    auto sites = scan::LocateHudSites(image.data(), textEnd, hudErr);
    CHECK(sites.size() == 5, "all 5 HUD sites found%s%s", hudErr.empty() ? "" : ": ", hudErr.c_str());
    bool patchesOk = !sites.empty();
    for (const auto& s : sites) {
        auto p = scan::BuildHudPatch(s, r.infiniteAmmo);
        int32_t disp;
        std::memcpy(&disp, p.data() + 2, 4);
        const bool targetOk = p.size() == s.length && s.offset + s.length + disp == r.infiniteAmmo;
        const bool formOk = s.length == 7 ? (s.original[0] == 0x80 && p[0] == 0x80 && p[1] == 0x3D && p[6] == s.original[6])
                                          : (s.original[0] == 0x38 && p[0] == 0x38 && (p[1] & 0xC7) == 0x05 &&
                                             (p[1] & 0x38) == (s.original[1] & 0x38));
        const bool origOk = s.original[2] == 0x68 && s.original[3] == 0x49; // disp32 0x4968
        if (!(targetOk && formOk && origOk)) patchesOk = false;
    }
    CHECK(patchesOk, "HUD patches are same-length and target the Infinite Ammo flag");

    // Compares that the mod redirects to its private bytes; counts must match
    // what was verified by disassembly or the mod refuses to patch.
    const size_t inf = scan::FindByteCompares(image.data(), textEnd, r.infiniteAmmo).size();
    const size_t bot = scan::FindByteCompares(image.data(), textEnd, r.bottomless).size();
    CHECK(inf == 2 && bot == 5, "flag reads to redirect: InfiniteAmmo %zu/2, Bottomless %zu/5", inf, bot);

    // No Reload detour: the fire routine's magazine store, and a stub whose
    // relative operands land where they should when placed near the image.
    const auto c = scan::LocateClipStore(image.data(), textEnd);
    CHECK(c.ok, "magazine store found%s%s", c.ok ? "" : ": ", c.error.c_str());
    if (c.ok) {
        CHECK(c.isInfinite == r.infiniteCheck, "fire routine calls the same infinite-ammo check");
        CHECK(image[c.store] == 0x66 && image[c.store + 1] == 0x89 && image[c.store + 3] == 0x20 && image[c.store + 4] == 0x0C,
              "store is mov word ptr [rbx+0C20h], cx at RVA 0x%zX", c.store);
        const uint64_t base = 0x140000000ull, stub = base - 0x10000, flag = stub - 0x1000 + 2;
        const auto code = scan::BuildNoReloadStub(stub, base + c.store, flag, base + c.getOwner, base + c.asPlayer);
        const auto jump = scan::BuildDetourJump(base + c.store, stub);
        int32_t d;
        std::memcpy(&d, code.data() + 2, 4);
        uint64_t back, owner, player;
        std::memcpy(&back, code.data() + 76, 8);
        std::memcpy(&owner, code.data() + 84, 8);
        std::memcpy(&player, code.data() + 92, 8);
        int32_t j;
        std::memcpy(&j, jump.data() + 1, 4);
        CHECK(code.size() == 100 && stub + 7 + d == flag && back == base + c.store + 7 && owner == base + c.getOwner &&
                  player == base + c.asPlayer && std::memcmp(code.data() + 63, image.data() + c.store, 7) == 0,
              "stub checks the flag, re-runs the original store and jumps back after it");
        CHECK(jump.size() == 7 && jump[0] == 0xE9 && base + c.store + 5 + j == stub, "detour jump is 7 bytes and lands on the stub");
    }

    // Damage the cross-check instruction: the layout check must reject it.
    auto b = scan::FindAll(image.data(), textEnd, scan::Parse("40 38 3D ?? ?? ?? ?? 40 8A CF 74 02 B1 01 40 38 3D ?? ?? ?? ?? 75 0C 40 38 3D"));
    if (b.size() == 1) {
        auto copy = image;
        copy[b[0] + 26] ^= 0x40; // alter third disp32
        auto bad = scan::LocateFlags(copy.data(), textEnd, copy.size());
        CHECK(!bad.ok, "rejects image whose flag layout no longer matches (%s)", bad.error.c_str());
    }
    // Remove the main signature: must fail cleanly.
    {
        auto copy = image;
        auto a = scan::FindAll(copy.data(), textEnd, scan::Parse("32 C9 38 0D ?? ?? ?? ?? 75 0F 48 8B 05"));
        for (size_t off : a) copy[off] = 0xCC;
        auto bad = scan::LocateFlags(copy.data(), textEnd, copy.size());
        CHECK(!bad.ok, "fails safely when signature is missing (%s)", bad.error.c_str());
    }
}

// Developer-menu features (src/engine_defs.h), resolved the way engine.cpp does.
static void TestEngine(const char* path) {
    std::printf("engine features %s\n", path);
    std::vector<uint8_t> image;
    size_t textEnd = 0;
    uint32_t stamp = 0;
    if (!MapImage(path, image, textEnd, stamp)) return;
    const uint8_t* b = image.data();

    dscan::Index idx(b, textEnd, image.size());
    for (const auto& f : engine::kFlags) idx.WantLabel(f.label);
    for (const auto& a : engine::kActions) idx.WantLabel(a.label);
    for (const char* l : {"Infinite Sprint", "Drop All Weapons", "Give Each Craftable", "Manual Camera", "Disable HUD2 Draw",
                          "Time Scale", "Sync Load Weapons"})
        idx.WantLabel(l);
    idx.Build();
    auto ctorOf = [&](const char* l) { return idx.Items(l).size() == 1 ? idx.Items(l)[0].ctor : 0; };
    const size_t itemBool = ctorOf("Infinite Sprint"), itemBoolCb = ctorOf("Drop All Weapons"),
                 itemFunction = ctorOf("Give Each Craftable");
    CHECK(itemBool && itemBoolCb && itemFunction && itemBool != itemBoolCb && itemBool != itemFunction && itemBoolCb != itemFunction,
          "item constructors: bool 0x%zX, bool+callback 0x%zX, function 0x%zX", itemBool, itemBoolCb, itemFunction);

    dscan::Index refs(b, textEnd, image.size());
    std::vector<std::pair<const engine::FlagDef*, std::vector<size_t>>> flags;
    for (const auto& f : engine::kFlags) {
        std::string err;
        std::vector<size_t> fl;
        if (f.src == engine::Src::BoolR8) fl.push_back(idx.ItemValue(f.label, itemBool, false, err));
        else if (f.src == engine::Src::BoolCbR9) fl.push_back(idx.ItemValue(f.label, itemBoolCb, true, err));
        else {
            std::vector<size_t> first;
            bool agree = true;
            for (const auto& it : idx.Items(f.label)) {
                if (it.ctor != itemBoolCb || !it.r8) continue;
                auto c = dscan::CallbackFlags(b, it.r8, 0x30, image.size());
                if (first.empty()) first = c;
                else if (c != first) agree = false;
            }
            if (agree) fl = first;
        }
        for (size_t x : fl) refs.WantFlag(x);
        flags.push_back({&f, fl});
    }
    refs.Build();
    for (const auto& [def, fl] : flags) {
        bool ok = !fl.empty() && (def->src != engine::Src::Callback3 || fl.size() == 3);
        std::string counts;
        for (size_t i = 0; i < fl.size(); ++i) {
            const auto& r = refs.FlagRefs(fl[i]);
            counts += " 0x" + [](size_t v) { char t[32]; std::snprintf(t, sizeof t, "%zX", v); return std::string(t); }(fl[i]) +
                      ":" + std::to_string(r.reads.size()) + "r/" + std::to_string(r.writes) + "w/" + std::to_string(r.leas) + "l";
            if (fl[i] == 0 || fl[i] <= textEnd || static_cast<int>(r.reads.size()) != def->reads[i]) ok = false;
        }
        CHECK(ok, "%-22s%s", def->id, counts.c_str());
    }

    for (const auto& a : engine::kActions) {
        std::string err;
        const size_t cb = idx.ItemValue(a.label, a.boolCallback ? itemBoolCb : itemFunction, false, err);
        CHECK(cb && cb < textEnd, "action %-18s callback 0x%zX %s", a.id, cb, err.c_str());
    }
    {
        std::string err;
        const size_t cam = idx.ItemValue("Manual Camera", itemBoolCb, false, err);
        CHECK(cam && cam < textEnd, "Manual Camera callback 0x%zX %s", cam, err.c_str());
    }
    {
        std::string err;
        const size_t loop = dscan::FindFunctionByName(b, textEnd, image.size(), engine::kGameLoopName, engine::kGameLoopPrologue, err);
        CHECK(loop != 0, "GameLoopUpdate at 0x%zX %s", loop, err.c_str());
    }
    {
        // Give-weapon callback: reads "Sync Load Weapons", calls the weapon lookup.
        std::string err;
        const size_t sync = idx.ItemValue("Sync Load Weapons", itemBool, false, err);
        dscan::Index wr(b, textEnd, image.size());
        wr.WantFlag(sync);
        wr.Build();
        const scan::Pattern pro = scan::Parse(engine::kGiveWeaponPrologue), call = scan::Parse(engine::kWeaponLookupCall);
        size_t cb = 0, lookup = 0;
        for (const auto& rd : wr.FlagRefs(sync).reads) {
            size_t j = 0;
            for (size_t k = rd.dispOffset; k + 0x200 > rd.dispOffset && k > 0; --k)
                if (scan::FindAll(b + k, pro.bytes.size(), pro).size() == 1) {
                    j = k;
                    break;
                }
            if (!j) continue;
            const auto c = scan::FindAll(b + j, 0x80, call);
            if (c.size() != 1) continue;
            cb = j;
            lookup = dscan::FirstCall(b, j + c[0] + 7, 1);
        }
        CHECK(cb && lookup && lookup < textEnd, "give-weapon callback 0x%zX, weapon lookup 0x%zX", cb, lookup);
    }
    {
        const auto& items = idx.Items("Disable HUD2 Draw");
        bool ok = items.size() == 1;
        if (ok) {
            const size_t s = items[0].site;
            ok = b[s - 7] == 0x4C && b[s - 6] == 0x8B && b[s - 5] == 0x05 && b[s + 7] == 0x49 && b[s + 8] == 0x83 &&
                 b[s + 9] == 0xC0 && items[0].ctor == itemBool;
        }
        CHECK(ok, "Hide HUD switch found (Disable HUD2 Draw)");
        const auto& ts = idx.Items("Time Scale");
        CHECK(ts.size() == 1 && ts[0].r8 > textEnd, "Time Scale float at 0x%zX", ts.empty() ? 0 : ts[0].r8);
    }
    CHECK(dscan::Fnv1a64("a") == 0xaf63dc4c8601ec8cull && dscan::Fnv1a64("") == 0xcbf29ce484222325ull, "FNV-1a 64 matches the reference values");

    // Character swap: Spawn Player -> spawn function -> core spawn.
    {
        dscan::Index ci(b, textEnd, image.size());
        ci.WantLabel(engine::kSpawnPlayerLabel);
        ci.Build();
        std::string err;
        const size_t cb = ci.ItemValue(engine::kSpawnPlayerLabel, itemBoolCb, false, err);
        const auto call = cb ? scan::FindAll(b + cb, 0x60, scan::Parse(engine::kSpawnCall)) : std::vector<size_t>{};
        const size_t spawn = call.size() == 1 ? dscan::FirstCall(b, cb + call[0] + 15, 1) : 0;
        uint64_t def = 0;
        if (spawn) {
            const auto d = scan::FindAll(b + spawn, 0x100, scan::Parse(engine::kDefaultCharacter));
            if (!d.empty()) std::memcpy(&def, b + spawn + d[0] + 2, 8);
        }
        const auto cc = spawn ? scan::FindAll(b + spawn, 0x200, scan::Parse(engine::kCoreCall)) : std::vector<size_t>{};
        const size_t core = cc.size() == 1 ? dscan::FirstCall(b, spawn + cc[0] + 5, 1) : 0;
        const bool pro = core && scan::FindAll(b + core, 25, scan::Parse(engine::kCorePrologue)).size() == 1;
        CHECK(cb && spawn && def == dscan::Fnv1a64(engine::kJoel) && core && pro,
              "character swap: Spawn Player 0x%zX, spawn 0x%zX (default = hash(t1x-joel)), core spawn 0x%zX", cb, spawn, core);
        std::string rerr;
        const size_t rs = dscan::FindFunctionByName(b, textEnd, image.size(), engine::kRestartName, engine::kRestartPrologue, rerr);
        CHECK(rs != 0, "GameRestartCheckpoint at 0x%zX %s", rs, rerr.c_str());
        const auto pl = scan::FindAll(b + idx.ItemValue("Give Each Craftable", itemFunction, false, rerr), 15,
                                      scan::Parse(engine::kPlayerLookupStart));
        CHECK(pl.size() == 1, "player lookup found at the start of Give Each Craftable");

        // Package loader for the swapped player's animations: every call site
        // that matches must resolve to the same loader and checks.
        const auto sites = scan::FindAll(b, textEnd, scan::Parse(engine::kPackageCheckCall));
        auto rel = [&](size_t at) {
            int32_t r;
            std::memcpy(&r, b + at, 4);
            return static_cast<size_t>(static_cast<int64_t>(at) + 4 + r);
        };
        size_t fn = 0, check = 0, mgr = 0;
        int good = 0;
        for (size_t s : sites) {
            const size_t f = rel(s + 25), c = rel(s + 8);
            if (scan::FindAll(b + f, 40, scan::Parse(engine::kLoadPackagePrologue)).size() == 1 &&
                scan::FindAll(b + c, 30, scan::Parse(engine::kPackageLoadingCheck)).size() == 1 &&
                scan::FindAll(b + c + engine::kPackageLoadedOffset, 30, scan::Parse(engine::kPackageLoadedCheck)).size() == 1) {
                if (good++ == 0) fn = f, check = c, mgr = rel(s + 3);
                else if (f != fn || c != check || rel(s + 3) != mgr) good = -1000;
            }
        }
        CHECK(good >= 1, "package loader 0x%zX, checks 0x%zX, manager 0x%zX (%d of %zu sites agree)", fn, check, mgr,
              good, sites.size());
        dscan::LevelSetLoader lsl;
        std::string lerr;
        CHECK(dscan::FindLevelSetLoader(b, textEnd, image.size(), lsl, lerr),
              "level manager 0x%zX, SetLevelSet 0x%zX, want-load rebuild 0x%zX %s", lsl.mgrGlobal, lsl.setLevelSet,
              lsl.rebuildWants, lerr.c_str());
        std::string cerr2;
        const size_t craftFlag = dscan::FindEllieCraftFlag(b, textEnd, image.size(), cerr2);
        CHECK(craftFlag != 0, "Ellie crafting flag 0x%zX %s", craftFlag, cerr2.c_str());
        const auto look = scan::FindAll(b, textEnd, scan::Parse(engine::kLookResolve));
        CHECK(look.size() == 1, "look resolver found once (%zu matches, 0x%zX)", look.size(), look.empty() ? 0 : look[0]);
        static_assert(sizeof(engine::kEllieOnlyPackages) / sizeof(char*) == 49 && sizeof(engine::kJoelOnlyPackages) / sizeof(char*) == 30,
                      "player package lists: 49 for Ellie, 30 for Joel");
    }

    // Skins (from the TLOU Outfit Changer): every site found, and both menu
    // reads are reads of the Extras flag, so Unlock All Extras patches the other 31.
    {
        const skinscan::SkinSites s = skinscan::LocateSkinSites(b, {0x1000, textEnd}, {{textEnd, image.size()}});
        CHECK(s.ok, "skins: flag, 2 menu checks and %zu ownership checks found %s", s.owned.size(), s.error.c_str());
        std::string err;
        const size_t extras = idx.ItemValue("Unlock All Extras Content", itemBool, false, err);
        int both = 0;
        for (const auto& [def, fl] : flags)
            if (!fl.empty() && fl[0] == extras)
                for (const auto& r : refs.FlagRefs(extras).reads)
                    if (r.dispOffset == s.mainMenu.dispOffset || r.dispOffset == s.gameMenu.dispOffset) ++both;
        CHECK(s.ok && s.unlockAll == extras && both == 2, "skins menu reads are 2 of the Extras flag's reads (flag 0x%zX)", extras);
    }
}

int main(int argc, char** argv) {
    TestParser();
    for (int i = 1; i < argc; ++i) TestImage(argv[i]);
    for (int i = 1; i < argc; ++i) TestEngine(argv[i]);
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
