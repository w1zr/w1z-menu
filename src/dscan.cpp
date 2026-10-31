#include "dscan.h"

#include "engine_defs.h"
#include "scan.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace dscan {

namespace {

int32_t Disp(const uint8_t* p) {
    int32_t d;
    std::memcpy(&d, p, 4);
    return d;
}

bool IsRex(uint8_t b) { return b >= 0x40 && b <= 0x4F; }
bool RipModrm(uint8_t m) { return (m & 0xC7) == 0x05; } // mod=00, rm=101

const std::vector<Item> kNoItems;
const Refs kNoRefs;

} // namespace

uint64_t Fnv1a64(const char* s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (; *s; ++s) {
        h ^= static_cast<uint64_t>(static_cast<int64_t>(static_cast<signed char>(*s))); // movsx, as the engine does
        h *= 0x100000001b3ull;
    }
    return h;
}

std::vector<size_t> FindCString(const uint8_t* image, size_t size, const char* text) {
    std::vector<size_t> out;
    const size_t n = std::strlen(text) + 1; // include the NUL
    if (n < 2 || size < n + 1) return out;
    for (size_t i = 1; i + n <= size; ++i) {
        if (image[i] != static_cast<uint8_t>(text[0]) || image[i - 1] != 0) continue;
        if (std::memcmp(image + i, text, n) == 0) out.push_back(i);
    }
    return out;
}

Index::Index(const uint8_t* image, size_t textEnd, size_t imageSize)
    : image_(image), textEnd_(textEnd), imageSize_(imageSize) {}

void Index::WantLabel(const std::string& label) {
    wantedLabels_.insert(label);
    items_[label]; // present even when not found
}

void Index::WantFlag(size_t rva) { flags_[rva]; }

size_t Index::RipTarget(size_t dispOffset, size_t end) const {
    return static_cast<size_t>(static_cast<int64_t>(end) + Disp(image_ + dispOffset));
}

void Index::Build() {
    const uint8_t* m = image_;
    // Pass 1: where each wanted label string lives (exact, NUL-delimited).
    if (!wantedLabels_.empty()) {
        size_t maxLen = 0;
        for (const auto& l : wantedLabels_) maxLen = (std::max)(maxLen, l.size());
        for (size_t i = 1; i + maxLen + 1 < imageSize_; ++i) {
            if (m[i - 1] != 0 || m[i] < 0x20 || m[i] >= 0x7F) continue;
            size_t n = 0;
            while (n <= maxLen && m[i + n] >= 0x20 && m[i + n] < 0x7F) ++n;
            if (n > maxLen || m[i + n] != 0) {
                i += n;
                continue;
            }
            const std::string s(reinterpret_cast<const char*>(m + i), n);
            if (wantedLabels_.count(s)) labelAt_[i] = s;
            i += n;
        }
    }
    for (size_t i = 8; i + 8 <= textEnd_; ++i) {
        // Developer-menu item: lea rdx, [rip+label]
        if (m[i] == 0x48 && m[i + 1] == 0x8D && m[i + 2] == 0x15 && !labelAt_.empty()) {
            const size_t target = RipTarget(i + 3, i + 7);
            auto it = labelAt_.find(target);
            if (it != labelAt_.end()) {
                Item item;
                item.site = i;
                item.ctor = FirstCall(m, i + 7, 32);
                for (size_t back = 48; back >= 7; --back) {
                    const size_t j = i - back;
                    if (m[j] == 0x4C && m[j + 1] == 0x8D && (m[j + 2] == 0x05 || m[j + 2] == 0x0D)) {
                        const size_t t = RipTarget(j + 3, j + 7);
                        (m[j + 2] == 0x05 ? item.r8 : item.r9) = t; // the last one before the label wins
                    }
                }
                if (item.ctor) items_[it->second].push_back(item);
            }
        }

        if (flags_.empty()) continue;
        // mov m16, imm16 (66 C7 05 disp imm16) writes two flags at once.
        if (m[i] == 0x66 && m[i + 1] == 0xC7 && m[i + 2] == 0x05) {
            auto f = flags_.find(RipTarget(i + 3, i + 9));
            if (f != flags_.end()) ++f->second.writes;
        }
        // Flag references. A disp32 at i that resolves to a wanted flag for an
        // instruction ending at i+4 (no immediate) or i+5 (imm8).
        if (!RipModrm(m[i - 1])) continue; // every form below has a rip-relative ModRM right before the disp32
        for (size_t k : {size_t(4), size_t(5)}) {
            const size_t target = RipTarget(i, i + k);
            auto f = flags_.find(target);
            if (f == flags_.end()) continue;
            const uint8_t op = m[i - 2], modrm = m[i - 1];
            Refs& r = f->second;
            if (k == 4) {
                if (op == 0x38 || op == 0x3A || op == 0x84 || op == 0x8A) r.reads.push_back({i, i + 4});
                else if (m[i - 3] == 0x0F && (op == 0xB6 || op == 0xBE)) r.reads.push_back({i, i + 4});
                else if (op == 0x88 || (op == 0x89 && m[i - 3] == 0x66)) ++r.writes;
                else if (op == 0x8D && (IsRex(m[i - 3]))) ++r.leas;
            } else {
                if (op == 0x80 && modrm == 0x3D) r.reads.push_back({i, i + 5});      // cmp m8, imm8
                else if (op == 0xF6 && modrm == 0x05) r.reads.push_back({i, i + 5}); // test m8, imm8
                else if (op == 0xC6 && modrm == 0x05) ++r.writes;                    // mov m8, imm8
            }
        }
    }
}

const std::vector<Item>& Index::Items(const std::string& label) const {
    auto it = items_.find(label);
    return it == items_.end() ? kNoItems : it->second;
}

const Refs& Index::FlagRefs(size_t rva) const {
    auto it = flags_.find(rva);
    return it == flags_.end() ? kNoRefs : it->second;
}

namespace {

size_t Agree(const std::vector<Item>& items, bool r9, const std::string& label, std::string& error) {
    if (items.empty()) {
        error = "developer-menu item \"" + label + "\" not found";
        return 0;
    }
    size_t v = 0;
    for (const auto& it : items) {
        const size_t x = r9 ? it.r9 : it.r8;
        if (!x || (v && x != v)) {
            error = "developer-menu item \"" + label + "\" has an unexpected layout";
            return 0;
        }
        v = x;
    }
    return v;
}

} // namespace

size_t Index::ItemValue(const std::string& label, size_t ctor, bool r9, std::string& error) const {
    std::vector<Item> matching;
    for (const auto& it : Items(label))
        if (it.ctor == ctor) matching.push_back(it);
    return Agree(matching, r9, label, error);
}

size_t Index::ItemR8(const std::string& label, std::string& error) const { return Agree(Items(label), false, label, error); }
size_t Index::ItemR9(const std::string& label, std::string& error) const { return Agree(Items(label), true, label, error); }

size_t FirstCall(const uint8_t* image, size_t from, size_t span) {
    for (size_t i = from; i < from + span; ++i)
        if (image[i] == 0xE8) return static_cast<size_t>(static_cast<int64_t>(i + 5) + Disp(image + i + 1));
    return 0;
}

size_t FindFunctionByName(const uint8_t* image, size_t textEnd, size_t imageSize, const char* name,
                          const char* prologue, std::string& error) {
    const auto strings = FindCString(image, imageSize, name);
    if (strings.size() != 1) {
        error = std::string("name string \"") + name + "\" found " + std::to_string(strings.size()) + " times";
        return 0;
    }
    const scan::Pattern p = scan::Parse(prologue);
    std::set<size_t> funcs;
    for (size_t i = 3; i + 7 <= textEnd; ++i) {
        if (image[i + 1] != 0x8D || !IsRex(image[i]) || !RipModrm(image[i + 2])) continue;
        if (static_cast<size_t>(static_cast<int64_t>(i + 7) + Disp(image + i + 3)) != strings[0]) continue;
        // The function start is the closest earlier position (within 2 KB)
        // where the expected prologue matches; functions are not always
        // preceded by int3 padding.
        for (size_t j = i; j > 0 && i - j < 0x800; --j) {
            if (j + p.bytes.size() > textEnd) continue;
            bool match = true;
            for (size_t k = 0; k < p.bytes.size() && match; ++k)
                if (p.bytes[k] >= 0 && image[j + k] != p.bytes[k]) match = false;
            if (match) {
                funcs.insert(j);
                break;
            }
        }
    }
    if (funcs.size() != 1) {
        error = std::string("function for \"") + name + "\" matched " + std::to_string(funcs.size()) + " times";
        return 0;
    }
    return *funcs.begin();
}

bool FindLevelSetLoader(const uint8_t* image, size_t textEnd, size_t imageSize, LevelSetLoader& out, std::string& error) {
    const auto names = FindCString(image, imageSize, engine::kLevelSetCommand);
    if (names.size() != 1) {
        error = "LoadLevelSet command name found " + std::to_string(names.size()) + " times";
        return false;
    }
    auto rel = [&](size_t disp) { return static_cast<size_t>(static_cast<int64_t>(disp + 4) + Disp(image + disp)); };
    std::set<size_t> handlers;
    for (size_t at : scan::FindAll(image, textEnd, scan::Parse(engine::kConsoleRegister)))
        if (rel(at + 10) == names[0]) handlers.insert(rel(at + 17));
    if (handlers.size() != 1) {
        error = "LoadLevelSet command registered " + std::to_string(handlers.size()) + " times";
        return false;
    }
    const size_t handler = *handlers.begin();
    const auto call = handler + 0x80 < textEnd ? scan::FindAll(image + handler, 0x80, scan::Parse(engine::kSetLevelSetCall))
                                               : std::vector<size_t>{};
    if (call.size() != 1) {
        error = "LoadLevelSet's call to the level manager was not recognised";
        return false;
    }
    const size_t mgr = rel(handler + call[0] + 6), set = rel(handler + call[0] + 14);
    if (mgr >= imageSize || set + 0x200 > textEnd ||
        scan::FindAll(image + set, 29, scan::Parse(engine::kSetLevelSetPrologue)).size() != 1) {
        error = "LevelMgr::SetLevelSet has an unexpected layout";
        return false;
    }
    const auto rebuild = scan::FindAll(image + set, 0x200, scan::Parse(engine::kRebuildWantsCall));
    const size_t fn = rebuild.size() == 1 ? rel(set + rebuild[0] + 11) : 0;
    if (!fn || fn + 32 > textEnd || scan::FindAll(image + fn, 28, scan::Parse(engine::kRebuildWantsPrologue)).size() != 1) {
        error = "the want-load rebuild was not recognised";
        return false;
    }
    out = {mgr, set, fn};
    return true;
}

size_t FindEllieCraftFlag(const uint8_t* image, size_t textEnd, size_t imageSize, std::string& error) {
    const auto level = FindCString(image, imageSize, engine::kEllieCraftLevel);
    const auto reads = scan::FindAll(image, textEnd, scan::Parse(engine::kEllieCraftFlagRead));
    if (level.size() != 1 || reads.size() != 1) {
        error = "crafting check not recognised (" + std::to_string(reads.size()) + " flag reads)";
        return 0;
    }
    bool lakeside = false; // lea r8, ["lakeside"] shortly before
    for (size_t i = reads[0] >= 0x80 ? reads[0] - 0x80 : 0; i + 7 <= reads[0] && !lakeside; ++i)
        lakeside = image[i] == 0x4C && image[i + 1] == 0x8D && RipModrm(image[i + 2]) &&
                   static_cast<size_t>(static_cast<int64_t>(i + 7) + Disp(image + i + 3)) == level[0];
    const size_t flag = static_cast<size_t>(static_cast<int64_t>(reads[0] + 12) + Disp(image + reads[0] + 8));
    if (!lakeside || flag >= imageSize) {
        error = "crafting check has an unexpected layout";
        return 0;
    }
    return flag;
}

std::vector<size_t> CallbackFlags(const uint8_t* image, size_t func, size_t span, size_t imageSize) {
    std::vector<size_t> out;
    for (size_t i = func; i + 7 <= func + span; ++i) {
        // [REX] 38 /r with rip-relative ModRM
        if (image[i] == 0x38 && RipModrm(image[i + 1])) {
            const size_t t = static_cast<size_t>(static_cast<int64_t>(i + 6) + Disp(image + i + 2));
            if (t < imageSize) out.push_back(t);
            i += 5;
        }
    }
    return out;
}

size_t FindScriptNative(const uint8_t* image, size_t textEnd, size_t imageSize, const char* name, std::string& error) {
    const auto names = FindCString(image, imageSize, name);
    if (names.size() != 1) {
        error = std::string("script function name \"") + name + "\" not found once";
        return 0;
    }
    auto rel = [&](size_t disp) {
        int32_t r;
        std::memcpy(&r, image + disp, 4);
        return static_cast<size_t>(static_cast<int64_t>(disp) + 4 + r);
    };
    size_t found = 0, count = 0;
    for (size_t at : scan::FindAll(image, textEnd, scan::Parse(engine::kScriptFuncRegister))) {
        if (rel(at + 7) != names[0]) continue;
        found = rel(at + 14);
        ++count;
    }
    if (count != 1 || found >= textEnd) {
        error = std::string("script function \"") + name + "\" registered " + std::to_string(count) + " times";
        return 0;
    }
    return found;
}

} // namespace dscan
