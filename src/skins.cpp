#include "skins.h"

#include "features.h"
#include "game.h"
#include "log.h"
#include "skinscan.h"

#include <windows.h>
#include <cstring>
#include <vector>

namespace skins {

namespace {

features::Feature* g_feature = nullptr;

bool WriteCode(uint8_t* at, const void* bytes, size_t n) {
    DWORD old = 0;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

bool Disp32(const uint8_t* base, const skinscan::RipRead& rd, const uint8_t* target, int32_t& out) {
    const int64_t d = reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(base + rd.end);
    out = static_cast<int32_t>(d);
    return out == d;
}

} // namespace

void Register() {
    g_feature = &features::Add("UnlockSkins", "Engine", "Unlock Skins",
                               "Opens Extras > Skins and Pause > Skins without finishing the story, with every skin owned. "
                               "Change your outfit in-game from the pause menu.",
                               features::Kind::Toggle, 1);
}

std::vector<size_t> Install() {
    if (game::GameFolderModLoaded(L"xinput1_4.dll", "TLOU Outfit Changer")) {
        features::MarkUnavailable(*g_feature, "Your TLOU Outfit Changer mod is installed and already unlocks skins.");
        return {};
    }
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    skinscan::Range code;
    std::vector<skinscan::Range> data;
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const skinscan::Range r{sec[i].VirtualAddress, sec[i].VirtualAddress + sec[i].Misc.VirtualSize};
        const DWORD ch = sec[i].Characteristics;
        if (ch & IMAGE_SCN_MEM_EXECUTE) {
            if (!code.end) code = r;
        } else if ((ch & IMAGE_SCN_MEM_READ) && !(ch & IMAGE_SCN_MEM_DISCARDABLE)) {
            data.push_back(r);
        }
    }

    const skinscan::SkinSites s = skinscan::LocateSkinSites(base, code, data);
    if (!s.ok) {
        features::MarkUnavailable(*g_feature, "Skin checks not found (" + s.error + ").");
        return {};
    }

    // One byte the menu toggles; the two menu reads and three ownership stubs test it.
    uint8_t* flag = game::AllocDataNearImage();
    uint8_t* stubs = game::AllocDataNearImage();
    if (!flag || !stubs) {
        features::MarkUnavailable(*g_feature, "No memory near the game image.");
        return {};
    }
    *flag = g_feature->on ? 1 : 0;

    // Build everything before writing anything, so a range error changes nothing.
    int32_t mainDisp, gameDisp;
    if (!Disp32(base, s.mainMenu, flag, mainDisp) || !Disp32(base, s.gameMenu, flag, gameDisp)) {
        features::MarkUnavailable(*g_feature, "Patch out of range.");
        return {};
    }
    std::vector<std::vector<uint8_t>> jumps;
    size_t used = 0;
    const auto u = [&](size_t rva) { return reinterpret_cast<uint64_t>(base) + rva; };
    for (const auto& o : s.owned) {
        uint8_t* at = stubs + used;
        const auto stub = skinscan::BuildOwnedStub(reinterpret_cast<uint64_t>(at), u(o.offset), u(o.owned),
                                                   reinterpret_cast<uint64_t>(flag), o);
        const auto jump = skinscan::BuildSiteJump(u(o.offset), reinterpret_cast<uint64_t>(at));
        if (stub.empty() || jump.empty()) {
            features::MarkUnavailable(*g_feature, "Patch out of range.");
            return {};
        }
        std::memcpy(at, stub.data(), stub.size());
        used += (stub.size() + 15) & ~static_cast<size_t>(15);
        jumps.push_back(jump);
    }
    DWORD old = 0;
    if (!VirtualProtect(stubs, 0x1000, PAGE_EXECUTE_READ, &old)) {
        features::MarkUnavailable(*g_feature, "Could not prepare the patch.");
        return {};
    }
    FlushInstructionCache(GetCurrentProcess(), stubs, used);

    bool ok = WriteCode(base + s.mainMenu.dispOffset, &mainDisp, 4) && WriteCode(base + s.gameMenu.dispOffset, &gameDisp, 4);
    for (size_t i = 0; ok && i < s.owned.size(); ++i) ok = WriteCode(base + s.owned[i].offset, jumps[i].data(), jumps[i].size());
    if (!ok) {
        features::MarkUnavailable(*g_feature, "Patching the game failed.");
        return {};
    }
    g_feature->onToggle = [flag](bool on) { *flag = on ? 1 : 0; };
    features::MarkAvailable(*g_feature);
    logx::Write("skins: menu checks at RVA 0x%zX / 0x%zX and %zu ownership checks patched", s.mainMenu.instr,
                s.gameMenu.instr, s.owned.size());
    return {s.mainMenu.dispOffset, s.gameMenu.dispOffset};
}

} // namespace skins
