#include "menu.h"

#include "features.h"
#include "hotkeys.h"
#include "engine_defs.h"
#include "input.h"
#include "log.h"
#include "pause.h"
#include "settings.h"
#include "version.h"

#include "../third_party/imgui/imgui.h"

#include <windows.h>
#include <xinput.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <string>
#include <vector>

namespace menu {

namespace {

// ---------------------------------------------------------------- pages

enum class ItemType { Link, Feature, Header, Info };

struct Item {
    ItemType type;
    std::string text;      // Link/Header/Info label
    std::string help;      // Link/Info footer text
    int page = -1;         // Link target
    std::string feature;   // Feature id
    std::string value;     // Info: right-hand text
};

struct Page {
    std::string title;
    std::vector<Item> items;
};

enum PageId {
    kHome, kSearch, kFavorites, kPlayer, kWeapons, kGiveJoel, kGiveEllie, kGiveMelee, kGiveItems,
    kInventory, kCamera, kUnlocks, kScenes, kMisc, kSettings, kStatus, kPageCount
};
std::vector<Page> g_pages;

Item Link(const char* text, int page, const char* help) { return {ItemType::Link, text, help, page}; }
Item Feat(const char* id) { return {ItemType::Feature, "", "", -1, id}; }
Item Header(const char* text) { return {ItemType::Header, text}; }
Item Info(const std::string& text, const std::string& value, const std::string& help) {
    return {ItemType::Info, text, help, -1, "", value};
}

// ---------------------------------------------------------------- state

struct Level {
    int page;
    int selected; // index into the visible items
    int scroll;   // first visible row
};

std::atomic<bool> g_open{false};
std::vector<Level> g_stack;
std::string g_query;
std::vector<std::string> g_favorites;
std::vector<Item> g_view; // items of the current page as drawn this frame
ImFont* g_font = nullptr;
ImFont* g_fontBold = nullptr;
bool g_wasPaused = false;

std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void LoadFavorites() {
    g_favorites.clear();
    std::string s = settings::GetString("Menu", "Favorites", "");
    size_t start = 0;
    while (start < s.size()) {
        size_t comma = s.find(',', start);
        if (comma == std::string::npos) comma = s.size();
        std::string id = s.substr(start, comma - start);
        if (!id.empty() && features::Find(id)) g_favorites.push_back(id);
        start = comma + 1;
    }
}

void SaveFavorites() {
    std::string s;
    for (const auto& id : g_favorites) s += (s.empty() ? "" : ",") + id;
    settings::SetString("Menu", "Favorites", s);
}

bool IsFavorite(const std::string& id) {
    return std::find(g_favorites.begin(), g_favorites.end(), id) != g_favorites.end();
}

void ToggleFavorite(const Item& it) {
    if (it.type != ItemType::Feature) return;
    auto pos = std::find(g_favorites.begin(), g_favorites.end(), it.feature);
    const features::Feature* f = features::Find(it.feature);
    if (pos == g_favorites.end()) {
        g_favorites.push_back(it.feature);
        if (f) features::Notify("Added to Favorites: " + f->label);
    } else {
        g_favorites.erase(pos);
        if (f) features::Notify("Removed from Favorites: " + f->label);
    }
    SaveFavorites();
}

// Page that lists a feature (for search results).
std::string PageOf(const std::string& id) {
    for (int p = kPlayer; p < kPageCount; ++p) {
        if (p == kStatus) continue;
        for (const auto& it : g_pages[p].items)
            if (it.type == ItemType::Feature && it.feature == id) return g_pages[p].title;
    }
    return "";
}

// Builds the item list for the current page (search, favorites and status
// are generated each frame).
void BuildView(int page) {
    g_view.clear();
    if (page == kSearch) {
        const std::string q = Lower(g_query);
        if (q.empty()) {
            g_view.push_back(Info("Start typing...", "", "Type to find any option. Backspace deletes, Esc goes back."));
            return;
        }
        features::ForEach([&](features::Feature& f) {
            if (Lower(f.label).find(q) != std::string::npos && !PageOf(f.id).empty()) g_view.push_back(Feat(f.id.c_str()));
        });
        if (g_view.empty()) g_view.push_back(Info("No matches", "", "Nothing matches \"" + g_query + "\"."));
        return;
    }
    if (page == kFavorites) {
        for (const auto& id : g_favorites) g_view.push_back(Feat(id.c_str()));
        if (g_view.empty())
            g_view.push_back(Info("No favorites yet", "", "Press Tab (keyboard) or X / Square (controller) on any option to add it here."));
        return;
    }
    if (page == kStatus) {
        int loaded, total;
        features::Counts(loaded, total);
        g_view.push_back(Header((std::to_string(loaded) + " OF " + std::to_string(total) + " FEATURES LOADED").c_str()));
        features::ForEach([&](features::Feature& f) {
            if (f.section == "Menu") return;
            g_view.push_back(Info(f.label, f.available ? "OK" : "OFF", f.available ? f.help : f.reason));
        });
        return;
    }
    g_view = g_pages[page].items;
}

bool Selectable(const Item& it) { return it.type != ItemType::Header; }

int FirstSelectable() {
    for (int i = 0; i < static_cast<int>(g_view.size()); ++i)
        if (Selectable(g_view[i])) return i;
    return 0;
}

void Push(int page) {
    g_stack.push_back({page, 0, 0});
    if (page == kSearch) g_query.clear();
    BuildView(page);
    g_stack.back().selected = FirstSelectable();
}

void Close();

void Back() {
    if (g_stack.size() <= 1) {
        Close();
        return;
    }
    g_stack.pop_back();
    BuildView(g_stack.back().page);
}

void Open() {
    if (g_open) return;
    if (g_stack.empty()) Push(kHome);
    g_open = true;
    input::OnMenuOpened();
    g_wasPaused = false;
    if (features::Find("PauseWhileOpen")->on && pause::Available()) g_wasPaused = pause::Set(true);
    logx::Write("menu opened");
}

void Close() {
    if (!g_open) return;
    g_open = false;
    if (g_wasPaused) pause::Set(false);
    g_wasPaused = false;
    input::OnMenuClosed();
    logx::Write("menu closed");
}

void Move(int dir) {
    Level& lv = g_stack.back();
    const int n = static_cast<int>(g_view.size());
    if (n == 0) return;
    int i = lv.selected;
    for (int k = 0; k < n; ++k) {
        i = (i + dir + n) % n;
        if (Selectable(g_view[i])) break;
    }
    lv.selected = i;
}

void Activate(const Item& it) {
    switch (it.type) {
    case ItemType::Link: Push(it.page); break;
    case ItemType::Feature: {
        features::Feature* f = features::Find(it.feature);
        if (!f || !f->available) break;
        if (f->kind == features::Kind::Toggle) {
            features::SetToggle(*f, !f->on, false);
            if (f->restartRequired) features::Notify(f->label + ": applies on the next launch");
        } else if (f->kind == features::Kind::Action) {
            features::Run(*f);
        }
        break;
    }
    default: break;
    }
}

void Adjust(const Item& it, int dir) {
    if (it.type != ItemType::Feature) return;
    features::Feature* f = features::Find(it.feature);
    if (f && f->available && f->kind == features::Kind::Number) features::SetNumber(*f, f->value + dir * f->step);
}

// ---------------------------------------------------------------- drawing

// Palette: black, greys and white with one amber accent.
constexpr ImU32 kPanel = IM_COL32(12, 13, 15, 240);
constexpr ImU32 kHeaderBg = IM_COL32(20, 21, 24, 250);
constexpr ImU32 kRowSel = IM_COL32(44, 46, 50, 255);
constexpr ImU32 kRowHover = IM_COL32(30, 32, 35, 255);
constexpr ImU32 kText = IM_COL32(236, 236, 236, 255);
constexpr ImU32 kDim = IM_COL32(140, 143, 148, 255);
constexpr ImU32 kFaint = IM_COL32(86, 89, 94, 255);
constexpr ImU32 kLine = IM_COL32(40, 42, 46, 255);
constexpr ImU32 kAccent = IM_COL32(245, 166, 35, 255);

struct Ctx {
    ImDrawList* dl;
    float s; // scale
};

ImVec2 TextSize(ImFont* f, float size, const char* t) { return f->CalcTextSizeA(size, FLT_MAX, 0, t); }

void Text(const Ctx& c, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t) { c.dl->AddText(f, size, p, col, t); }

// Letter-spaced title.
void Spaced(const Ctx& c, ImFont* f, float size, float centerX, float y, ImU32 col, const char* t, float spacing) {
    float w = 0;
    for (const char* p = t; *p; ++p) {
        const char ch[2] = {*p, 0};
        w += TextSize(f, size, ch).x + (p[1] ? spacing : 0);
    }
    float x = centerX - w * 0.5f;
    for (const char* p = t; *p; ++p) {
        const char ch[2] = {*p, 0};
        c.dl->AddText(f, size, ImVec2(x, y), col, ch);
        x += TextSize(f, size, ch).x + spacing;
    }
}

// Wraps `text` to `width` and returns the height used.
float Wrapped(const Ctx& c, ImFont* f, float size, ImVec2 p, float width, ImU32 col, const std::string& t) {
    c.dl->AddText(f, size, p, col, t.c_str(), nullptr, width);
    return f->CalcTextSizeA(size, FLT_MAX, width, t.c_str()).y;
}

void Chevron(const Ctx& c, float x, float cy, ImU32 col) {
    const float h = 5.f * c.s;
    c.dl->AddLine(ImVec2(x - h * 0.6f, cy - h), ImVec2(x + h * 0.4f, cy), col, 1.8f * c.s);
    c.dl->AddLine(ImVec2(x + h * 0.4f, cy), ImVec2(x - h * 0.6f, cy + h), col, 1.8f * c.s);
}

// Favorite marker: the black star glyph (U+2605, from Segoe UI Symbol).
void Star(const Ctx& c, ImVec2 center, float r, ImU32 col) {
    const char* star = "\xE2\x98\x85";
    const float size = r * 2.4f;
    const ImVec2 ts = TextSize(g_font, size, star);
    c.dl->AddText(g_font, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), col, star);
}

std::string Breadcrumb() {
    std::string s;
    for (size_t i = 0; i < g_stack.size(); ++i) {
        if (i) s += "  >  ";
        std::string t = g_pages[g_stack[i].page].title;
        for (auto& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        s += t;
    }
    return s;
}

void DrawNotices(const Ctx& c, float screenH) {
    float y = screenH - 40.f * c.s;
    std::vector<std::pair<std::string, unsigned>> list;
    features::ForEachNotice(2500, [&](const std::string& t, unsigned age) { list.push_back({t, age}); });
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        const float alpha = it->second > 2000 ? (2500 - it->second) / 500.f : 1.f;
        const float size = 16.f * c.s;
        const ImVec2 ts = TextSize(g_font, size, it->first.c_str());
        const float h = ts.y + 14.f * c.s;
        const ImVec2 a(36.f * c.s, y - h), b(a.x + ts.x + 30.f * c.s, y);
        c.dl->AddRectFilled(a, b, IM_COL32(12, 13, 15, static_cast<int>(230 * alpha)), 3.f * c.s);
        c.dl->AddRectFilled(a, ImVec2(a.x + 3.f * c.s, b.y), (kAccent & 0x00FFFFFF) | (static_cast<ImU32>(255 * alpha) << 24));
        c.dl->AddText(g_font, size, ImVec2(a.x + 15.f * c.s, a.y + 7.f * c.s),
                      (kText & 0x00FFFFFF) | (static_cast<ImU32>(255 * alpha) << 24), it->first.c_str());
        y -= h + 8.f * c.s;
    }
}

void DrawMenu(const Ctx& c) {
    ImGuiIO& io = ImGui::GetIO();
    const float s = c.s;
    Level& lv = g_stack.back();
    BuildView(lv.page);
    if (lv.selected >= static_cast<int>(g_view.size())) lv.selected = FirstSelectable();

    const float x = 36.f * s, y = 36.f * s, w = 560.f * s;
    const float headerH = 62.f * s, crumbH = 46.f * s, rowH = 43.f * s, sectionH = 30.f * s;
    const float pad = 22.f * s;
    const int maxRows = 12;
    const float fontRow = 19.f * s, fontSmall = 13.f * s;

    // Rows to show and scrolling (keep the selection visible).
    const int n = static_cast<int>(g_view.size());
    if (lv.selected < lv.scroll) lv.scroll = lv.selected;
    if (lv.selected >= lv.scroll + maxRows) lv.scroll = lv.selected - maxRows + 1;
    if (lv.scroll > std::max(0, n - maxRows)) lv.scroll = std::max(0, n - maxRows);
    if (lv.scroll < 0) lv.scroll = 0;
    const int last = std::min(n, lv.scroll + maxRows);
    float listH = 0;
    for (int i = lv.scroll; i < last; ++i) listH += g_view[i].type == ItemType::Header ? sectionH : rowH;

    // Footer text for the selected row.
    std::string help;
    bool helpIsReason = false;
    if (lv.selected < n) {
        const Item& it = g_view[lv.selected];
        if (it.type == ItemType::Feature) {
            const features::Feature* f = features::Find(it.feature);
            if (f) {
                helpIsReason = !f->available;
                help = f->available ? f->help : "Unavailable: " + f->reason;
                if (lv.page == kSearch || lv.page == kFavorites) {
                    const std::string where = PageOf(f->id);
                    if (!where.empty()) help += "  (" + where + ")";
                }
            }
        } else {
            help = it.help;
        }
    }
    const float wrapW = w - 2 * pad;
    const float helpH = help.empty() ? 0 : g_font->CalcTextSizeA(16.f * s, FLT_MAX, wrapW, help.c_str()).y;
    const bool home = lv.page == kHome;
    const float statusH = home ? 22.f * s : 0;
    const float footerH = 14.f * s + helpH + statusH + 12.f * s + fontSmall + 14.f * s;
    const float searchH = lv.page == kSearch ? 46.f * s : 0;
    const float totalH = headerH + crumbH + searchH + listH + footerH;

    // Panel and header.
    c.dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + totalH), kPanel, 4.f * s);
    c.dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + headerH), kHeaderBg, 4.f * s, ImDrawFlags_RoundCornersTop);
    Spaced(c, g_fontBold, 25.f * s, x + w * 0.5f, y + (headerH - 25.f * s) * 0.5f - 1.f * s, kText, "W1Z'S MENU", 7.f * s);
    c.dl->AddRectFilled(ImVec2(x, y + headerH - 2.f * s), ImVec2(x + w, y + headerH), kAccent);

    // Breadcrumb + counter. Earlier levels grey, current one in the accent.
    float cy = y + headerH;
    {
        const float ty = cy + (crumbH - fontSmall) * 0.5f;
        float tx = x + pad;
        for (size_t i = 0; i < g_stack.size(); ++i) {
            std::string t = g_pages[g_stack[i].page].title;
            for (auto& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const bool cur = i + 1 == g_stack.size();
            Text(c, g_font, fontSmall, ImVec2(tx, ty), cur ? kAccent : kDim, t.c_str());
            tx += TextSize(g_font, fontSmall, t.c_str()).x;
            if (!cur) {
                Text(c, g_font, fontSmall, ImVec2(tx + 8.f * s, ty), kFaint, ">");
                tx += 24.f * s;
            }
        }
        int selectable = 0, pos = 0;
        for (int i = 0; i < n; ++i) {
            if (!Selectable(g_view[i])) continue;
            ++selectable;
            if (i == lv.selected) pos = selectable;
        }
        const std::string counter = std::to_string(pos) + "/" + std::to_string(selectable);
        Text(c, g_font, fontSmall, ImVec2(x + w - pad - TextSize(g_font, fontSmall, counter.c_str()).x, ty), kDim, counter.c_str());
    }
    cy += crumbH;

    // Search box.
    if (lv.page == kSearch) {
        const ImVec2 a(x + pad, cy), b(x + w - pad, cy + 34.f * s);
        c.dl->AddRectFilled(a, b, IM_COL32(24, 25, 28, 255), 3.f * s);
        c.dl->AddRect(a, b, kLine, 3.f * s);
        const bool caret = (GetTickCount64() / 500) % 2 == 0;
        const std::string shown = g_query.empty() ? "Search options" : g_query;
        Text(c, g_font, 17.f * s, ImVec2(a.x + 12.f * s, a.y + 7.f * s), g_query.empty() ? kFaint : kText, shown.c_str());
        if (caret) {
            const float cx = a.x + 12.f * s + (g_query.empty() ? 0 : TextSize(g_font, 17.f * s, g_query.c_str()).x) + 1.f * s;
            c.dl->AddLine(ImVec2(cx, a.y + 8.f * s), ImVec2(cx, b.y - 8.f * s), kAccent, 1.5f * s);
        }
        cy += searchH;
    }

    // Rows.
    const float listTop = cy;
    int hovered = -1;
    for (int i = lv.scroll; i < last; ++i) {
        const Item& it = g_view[i];
        if (it.type == ItemType::Header) {
            const float ty = cy + sectionH - fontSmall - 6.f * s;
            Text(c, g_font, fontSmall, ImVec2(x + pad, ty), kDim, it.text.c_str());
            const float tw = TextSize(g_font, fontSmall, it.text.c_str()).x;
            c.dl->AddLine(ImVec2(x + pad + tw + 10.f * s, ty + fontSmall * 0.55f), ImVec2(x + w - pad, ty + fontSmall * 0.55f), kLine, 1.f);
            cy += sectionH;
            continue;
        }
        const ImVec2 a(x, cy), b(x + w, cy + rowH);
        const bool hover = io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;
        if (hover) hovered = i;
        const bool sel = i == lv.selected;
        if (sel) {
            c.dl->AddRectFilled(a, b, kRowSel);
            c.dl->AddRectFilled(a, ImVec2(a.x + 3.f * s, b.y), kAccent);
        } else if (hover) {
            c.dl->AddRectFilled(a, b, kRowHover);
        }

        std::string label = it.text, value;
        ImU32 labelCol = kText, valueCol = kDim;
        bool chevron = false, available = true, favorite = false;
        if (it.type == ItemType::Link) {
            chevron = true;
        } else if (it.type == ItemType::Info) {
            value = it.value;
            valueCol = value == "OK" ? kAccent : kDim;
        } else if (const features::Feature* f = features::Find(it.feature)) {
            label = f->label;
            available = f->available;
            favorite = IsFavorite(f->id);
            if (f->kind == features::Kind::Toggle) {
                value = f->on ? "ON" : "OFF";
                valueCol = f->on ? kAccent : kDim;
            } else if (f->kind == features::Kind::Number) {
                value = "<  " + std::to_string(f->value.load()) + (f->id == "Scale" || f->id == "GameSpeed" ? "%" : "") + "  >";
                valueCol = kText;
            }
            if (!available) {
                labelCol = kFaint;
                valueCol = kFaint;
                value = "N/A";
            }
        }
        const float ty = cy + (rowH - fontRow) * 0.5f;
        Text(c, sel ? g_fontBold : g_font, fontRow, ImVec2(x + pad + (sel ? 2.f * s : 0), ty), labelCol, label.c_str());
        float right = x + w - pad;
        if (chevron) {
            Chevron(c, right - 3.f * s, cy + rowH * 0.5f, sel ? kAccent : kDim);
            right -= 18.f * s;
        }
        if (!value.empty()) {
            const float vs = 15.f * s;
            const float vw = TextSize(g_fontBold, vs, value.c_str()).x;
            Text(c, g_fontBold, vs, ImVec2(right - vw, cy + (rowH - vs) * 0.5f), valueCol, value.c_str());
            right -= vw + 12.f * s;
        }
        if (favorite) Star(c, ImVec2(right - 6.f * s, cy + rowH * 0.5f), 6.f * s, kAccent);
        cy += rowH;
    }

    // Scrollbar when the list is longer than the panel.
    if (n > maxRows) {
        const float trackX = x + w - 4.f * s;
        const float thumbH = std::max(24.f * s, listH * maxRows / n);
        const float thumbY = listTop + (listH - thumbH) * lv.scroll / std::max(1, n - maxRows);
        c.dl->AddRectFilled(ImVec2(trackX, listTop), ImVec2(trackX + 2.f * s, listTop + listH), kLine);
        c.dl->AddRectFilled(ImVec2(trackX, thumbY), ImVec2(trackX + 2.f * s, thumbY + thumbH), kAccent);
    }

    // Footer.
    c.dl->AddLine(ImVec2(x + pad, cy), ImVec2(x + w - pad, cy), kLine, 1.f);
    cy += 14.f * s;
    if (!help.empty()) cy += Wrapped(c, g_font, 16.f * s, ImVec2(x + pad, cy), wrapW, helpIsReason ? kDim : kText, help);
    if (home) {
        int loaded, total;
        features::Counts(loaded, total);
        const std::string st = std::to_string(loaded) + " of " + std::to_string(total) + " features loaded";
        cy += 6.f * s;
        c.dl->AddCircleFilled(ImVec2(x + pad + 4.f * s, cy + fontSmall * 0.55f), 3.5f * s, loaded == total ? kAccent : kDim);
        Text(c, g_font, fontSmall, ImVec2(x + pad + 14.f * s, cy), loaded == total ? kDim : kText, st.c_str());
        cy += statusH - 6.f * s;
    }
    cy += 12.f * s;
    Text(c, g_font, fontSmall, ImVec2(x + pad, cy), kFaint, "Tab / X: Favorite     Esc / B: Back");
    const char* ver = "w1z's menu v" MENU_VERSION;
    Text(c, g_font, fontSmall, ImVec2(x + w - pad - TextSize(g_font, fontSmall, ver).x, cy), kFaint, ver);

    // Mouse: hover selects, left click activates, right click goes back,
    // wheel scrolls. Clicking the left/right third of a number changes it.
    if (hovered >= 0 && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) lv.selected = hovered;
    if (hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        lv.selected = hovered;
        const Item& it = g_view[hovered];
        const features::Feature* f = it.type == ItemType::Feature ? features::Find(it.feature) : nullptr;
        if (f && f->kind == features::Kind::Number) Adjust(it, io.MousePos.x < x + w - 70.f * s ? -1 : 1);
        else Activate(it);
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) Back();
    if (io.MouseWheel != 0 && !g_stack.empty()) {
        Level& l2 = g_stack.back();
        l2.scroll = std::max(0, l2.scroll - static_cast<int>(io.MouseWheel));
        if (l2.selected < l2.scroll) l2.selected = l2.scroll;
        if (l2.selected >= l2.scroll + maxRows) l2.selected = l2.scroll + maxRows - 1;
        if (l2.selected < static_cast<int>(g_view.size()) && !Selectable(g_view[l2.selected])) Move(1);
    }
}

void HandleKeys(const input::Pad& pad) {
    if (!g_open || g_stack.empty()) return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    const bool padUp = pad.pressed & XINPUT_GAMEPAD_DPAD_UP, padDown = pad.pressed & XINPUT_GAMEPAD_DPAD_DOWN;
    const bool padLeft = pad.pressed & XINPUT_GAMEPAD_DPAD_LEFT, padRight = pad.pressed & XINPUT_GAMEPAD_DPAD_RIGHT;
    Level& lv = g_stack.back();
    const bool searching = lv.page == kSearch;

    if (searching) {
        ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            const ImWchar ch = io.InputQueueCharacters[i];
            if (ch >= 32 && ch < 127 && g_query.size() < 40) g_query.push_back(static_cast<char>(ch));
        }
        io.InputQueueCharacters.resize(0);
        if (pressed(ImGuiKey_Backspace) && !g_query.empty()) {
            g_query.pop_back();
            lv.selected = 0;
            return;
        }
    }

    if (pressed(ImGuiKey_UpArrow) || padUp) Move(-1);
    if (pressed(ImGuiKey_DownArrow) || padDown) Move(1);
    if (pressed(ImGuiKey_PageUp)) for (int i = 0; i < 8; ++i) Move(-1);
    if (pressed(ImGuiKey_PageDown)) for (int i = 0; i < 8; ++i) Move(1);
    if (lv.selected < static_cast<int>(g_view.size())) {
        const Item& it = g_view[lv.selected];
        if (pressed(ImGuiKey_LeftArrow) || padLeft) Adjust(it, -1);
        if (pressed(ImGuiKey_RightArrow) || padRight) Adjust(it, 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || (pad.pressed & XINPUT_GAMEPAD_A)) {
            Activate(it);
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) || (pad.pressed & XINPUT_GAMEPAD_X)) ToggleFavorite(it);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (!searching && ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) ||
        (pad.pressed & XINPUT_GAMEPAD_B))
        Back();
    // Typing on any page jumps to Search, like "Type to find any option".
    if (!searching && g_open) {
        ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            const ImWchar ch = io.InputQueueCharacters[i];
            if (std::isalnum(ch)) {
                Push(kSearch);
                g_query.push_back(static_cast<char>(ch));
                break;
            }
        }
        io.InputQueueCharacters.resize(0);
    }
}

void BuildPages() {
    g_pages.assign(kPageCount, {});
    g_pages[kHome] = {"Home",
                      {Link("Search", kSearch, "Type to find any option."),
                       Link("Favorites", kFavorites, "Options you starred with Tab / X."),
                       Link("Player", kPlayer, "God mode, sprint, stealth and more."),
                       Link("Weapons and Items", kWeapons, "Ammo, reloads, weapon options and giving weapons."),
                       Link("Inventory", kInventory, "Supplies, parts, pills and recipes."),
                       Link("Camera", kCamera, "Free camera and HUD."),
                       Link("Unlocks", kUnlocks, "Collectibles, skins and outfits, chapters and encounters."),
                       Link("Scene Teleport", kScenes, "Restart the current scene."),
                       Link("Misc", kMisc, "Game speed."),
                       Link("Settings", kSettings, "Menu keys, pause, size and feature status.")}};
    g_pages[kSearch] = {"Search", {}};
    g_pages[kFavorites] = {"Favorites", {}};
    g_pages[kPlayer] = {"Player",
                        {Feat("GodMode"), Feat("NoHitReactions"), Feat("InfiniteSprint"), Feat("Undetectable"), Feat("OneHitKills")}};
    g_pages[kWeapons] = {"Weapons and Items",
                         {Header("WEAPONS"), Feat("InfiniteAmmo"), Feat("NoReload"), Feat("ShowInfinitySign"),
                          Feat("InfiniteWeaponHealth"), Feat("NoWeaponSway"), Feat("NoAutoReload"), Feat("NoAutoHolster"),
                          Feat("DropAllWeapons"), Link("Give Joel", kGiveJoel, "Joel's guns."),
                          Link("Give Ellie", kGiveEllie, "Ellie's weapons."), Link("Give Melee", kGiveMelee, "Melee weapons."),
                          Link("Give Items", kGiveItems, "Shivs, health kits, bombs and throwables."), Header("ITEMS"),
                          Feat("InfiniteCrafting"), Feat("InfiniteCraftables")}};
    for (const auto& g : engine::kGives) {
        const int page = g.group == 0 ? kGiveJoel : g.group == 1 ? kGiveEllie : g.group == 2 ? kGiveMelee : kGiveItems;
        g_pages[page].items.push_back(Feat(g.id));
    }
    g_pages[kGiveJoel].title = "Give Joel";
    g_pages[kGiveEllie].title = "Give Ellie";
    g_pages[kGiveMelee].title = "Give Melee";
    g_pages[kGiveItems].title = "Give Items";
    g_pages[kInventory] = {"Inventory",
                           {Header("INVENTORY"), Feat("GiveAllInventory"), Feat("GiveEachCraftable"), Feat("Parts10"),
                            Feat("Parts100"), Feat("Pills10"), Feat("Pills100"), Header("RECIPES"), Feat("AllRecipes"),
                            Feat("NoRecipes")}};
    g_pages[kCamera] = {"Camera", {Feat("ManualCamera"), Feat("TeleportToCamera"), Feat("HideHud")}};
    g_pages[kUnlocks] = {"Unlocks",
                         {Header("COLLECTIBLES"), Feat("GiveTreasures"), Feat("GiveJournal"), Feat("RemoveTreasures"),
                          Feat("ResetJournal"), Header("SKINS AND OUTFITS"), Feat("UnlockSkins"),
                          Header("WHILE THE MENU IS LOADED (NOT SAVED)"), Feat("AllChapters"), Feat("AllEncounters"),
                          Feat("AllExtras")}};
    g_pages[kScenes] = {"Scene Teleport", {Feat("ReplayScene")}};
    g_pages[kMisc] = {"Misc", {Feat("GameSpeed")}};

    auto key = [](const char* section, const char* k, int def) {
        const int vk = settings::GetInt(section, k, def);
        if (vk <= 0) return std::string("off");
        wchar_t name[64] = {};
        UINT sc = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
        if (vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT) sc |= 0x100;
        std::string out;
        if (GetKeyNameTextW(static_cast<LONG>(sc << 16), name, 64) > 0)
            for (const wchar_t* p = name; *p; ++p) out.push_back(*p < 128 ? static_cast<char>(*p) : '?');
        return out.empty() ? "key " + std::to_string(vk) : out;
    };
    std::string menuKeys = key("Menu", "MenuKey", VK_INSERT);
    const std::string second = key("Menu", "MenuKey2", VK_F9);
    if (second != "off") menuKeys += " / " + second;

    g_pages[kSettings] = {
        "Settings",
        {Header("MENU"), Feat("PauseWhileOpen"), Feat("ControllerToggle"), Feat("RememberToggles"), Feat("Notifications"),
         Feat("BeepOnToggle"), Feat("Scale"), Link("Feature Status", kStatus, "Which features loaded, and why any are unavailable."),
         Header("KEYS"),
         Info("Open / Close Menu", menuKeys, "Change MenuKey / MenuKey2 in w1zsMenu.ini (restart to apply)."),
         Info("Controller", "L3 + R3", "Press both sticks to open or close the menu."),
         Info("Infinite Ammo", key("InfiniteAmmo", "ToggleInfiniteAmmoKey", VK_F7), "ToggleInfiniteAmmoKey in w1zsMenu.ini."),
         Info("No Reload", key("InfiniteAmmo", "ToggleNoReloadKey", VK_F8), "ToggleNoReloadKey in w1zsMenu.ini.")}};
    g_pages[kStatus] = {"Status", {}};
}

} // namespace

void Init() {
    using features::Kind;
    auto menuSetting = [](const char* id, const char* label, const char* help, Kind kind, int def) -> features::Feature& {
        features::Feature& f = features::Add(id, "Menu", label, help, kind, def);
        f.alwaysSave = true;
        features::MarkAvailable(f);
        return f;
    };
    menuSetting("PauseWhileOpen", "Pause While Open", "Pause the game while the menu is open.", Kind::Toggle, 1);
    menuSetting("ControllerToggle", "Controller Shortcut", "L3 + R3 opens and closes the menu.", Kind::Toggle, 1);
    menuSetting("RememberToggles", "Remember Toggles", "Feature toggles are restored the next time you start the game.",
                Kind::Toggle, 1);
    menuSetting("Notifications", "Notifications", "Show a short message when a hotkey toggles something.", Kind::Toggle, 1);
    features::Feature& beep = features::Add("BeepOnToggle", "InfiniteAmmo", "Hotkey Sound",
                                            "Play a Windows sound when F7/F8 toggle something.", Kind::Toggle, 1);
    beep.alwaysSave = true;
    features::MarkAvailable(beep);
    features::Feature& scale = menuSetting("Scale", "Menu Size", "Size of the menu in percent. Left / Right to change.", Kind::Number, 100);
    scale.minValue = 75;
    scale.maxValue = 200;
    scale.step = 5;
    if (!pause::Available()) features::MarkUnavailable(*features::Find("PauseWhileOpen"), pause::Reason());

    BuildPages();
    LoadFavorites();
}

void CreateContext() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // the menu keeps its own settings
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Fonts are loaded from Windows at runtime (nothing is bundled). Segoe UI
    // Symbol fills in glyphs like the infinity sign.
    wchar_t dir[MAX_PATH];
    GetWindowsDirectoryW(dir, MAX_PATH);
    auto path = [&](const char* file) {
        char buf[MAX_PATH * 2];
        WideCharToMultiByte(CP_UTF8, 0, dir, -1, buf, sizeof(buf), nullptr, nullptr);
        return std::string(buf) + "\\Fonts\\" + file;
    };
    auto load = [&](const char* file, bool merge) -> ImFont* {
        const std::string p = path(file);
        if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        ImFontConfig cfg;
        cfg.MergeMode = merge;
        return io.Fonts->AddFontFromFileTTF(p.c_str(), 18.f, &cfg);
    };
    g_font = load("segoeui.ttf", false);
    if (g_font) load("seguisym.ttf", true);
    g_fontBold = load("seguisb.ttf", false);
    if (g_fontBold) load("seguisym.ttf", true);
    if (!g_font) g_font = io.Fonts->AddFontDefault();
    if (!g_fontBold) g_fontBold = g_font;
    logx::Write("menu: fonts loaded");
}

void Draw() {
    // Open/close: hotkeys (Insert / F9) or L3 + R3 on a controller.
    const input::Pad pad = input::PollPad();
    bool toggle = hotkeys::TakeMenuToggle();
    const uint16_t sticks = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
    if (features::Find("ControllerToggle")->on && (pad.held & sticks) == sticks && (pad.pressed & sticks)) toggle = true;
    if (toggle) g_open ? Close() : Open();

    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = g_open;

    const float s = io.DisplaySize.y / 1080.f * features::Find("Scale")->value / 100.f;
    Ctx c{ImGui::GetForegroundDrawList(), s};
    if (g_open) {
        HandleKeys(pad);
        if (g_open) DrawMenu(c);
    }
    DrawNotices(c, io.DisplaySize.y);
}

bool IsOpen() { return g_open; }

} // namespace menu
