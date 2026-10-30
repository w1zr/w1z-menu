#include "features.h"

#include "log.h"
#include "settings.h"

#include <windows.h>
#include <deque>
#include <mutex>

namespace features {

namespace {

// deque: elements never move, so Feature& stays valid while more are added.
std::deque<Feature> g_all;
std::mutex g_lock;

struct Notice {
    std::string text;
    ULONGLONG at;
};
std::deque<Notice> g_notices;
std::mutex g_noticeLock;

bool Persist(const Feature& f) {
    if (f.section.empty()) return false;
    if (f.section == "Character") return settings::GetBool("Menu", "RememberSwap", false);
    return f.alwaysSave || settings::GetBool("Menu", "RememberToggles", true);
}

} // namespace

Feature& Add(const char* id, const char* section, const char* label, const char* help, Kind kind, int def) {
    std::lock_guard<std::mutex> l(g_lock);
    for (auto& f : g_all)
        if (f.id == id) return f;
    Feature& f = g_all.emplace_back();
    f.id = id;
    f.section = section ? section : "";
    f.label = label;
    f.help = help;
    f.kind = kind;
    if (kind == Kind::Toggle) f.on = f.section.empty() ? def != 0 : settings::GetBool(section, id, def != 0);
    if (kind == Kind::Number) f.value = f.section.empty() ? def : settings::GetInt(section, id, def);
    return f;
}

Feature* Find(const std::string& id) {
    std::lock_guard<std::mutex> l(g_lock);
    for (auto& f : g_all)
        if (f.id == id) return &f;
    return nullptr;
}

void ForEach(const std::function<void(Feature&)>& fn) {
    // Features are only added during startup; iterating without the lock
    // afterwards is safe because the deque never relocates elements.
    for (auto& f : g_all) fn(f);
}

void MarkAvailable(Feature& f) {
    f.reason.clear();
    f.available = true;
}

void MarkUnavailable(Feature& f, const std::string& reason) {
    f.available = false;
    f.reason = reason;
    logx::Write("%s unavailable: %s", f.label.c_str(), reason.c_str());
}

void SetToggle(Feature& f, bool on, bool notify) {
    if (f.kind != Kind::Toggle) return;
    f.on = on;
    if (f.available && f.onToggle) f.onToggle(on);
    if (Persist(f)) settings::SetBool(f.section.c_str(), f.id.c_str(), on);
    logx::Write("%s = %s", f.label.c_str(), on ? "ON" : "OFF");
    if (notify) Notify(f.label + (on ? "  ON" : "  OFF"));
}

void SetNumber(Feature& f, int value) {
    if (f.kind != Kind::Number) return;
    if (value < f.minValue) value = f.minValue;
    if (value > f.maxValue) value = f.maxValue;
    f.value = value;
    if (f.available && f.onNumber) f.onNumber(value);
    if (Persist(f)) settings::SetInt(f.section.c_str(), f.id.c_str(), value);
}

void Run(Feature& f) {
    if (f.kind != Kind::Action || !f.available || !f.onAction) return;
    logx::Write("action: %s", f.label.c_str());
    f.onAction();
}

void Counts(int& loaded, int& total) {
    loaded = total = 0;
    for (auto& f : g_all) {
        if (f.section == "Menu") continue; // menu settings are not game features
        ++total;
        if (f.available) ++loaded;
    }
}

void Notify(const std::string& text) {
    if (!settings::GetBool("Menu", "Notifications", true)) return;
    std::lock_guard<std::mutex> l(g_noticeLock);
    g_notices.push_back({text, GetTickCount64()});
    while (g_notices.size() > 4) g_notices.pop_front();
}

void ForEachNotice(unsigned maxAgeMs, const std::function<void(const std::string&, unsigned)>& fn) {
    std::lock_guard<std::mutex> l(g_noticeLock);
    const ULONGLONG now = GetTickCount64();
    while (!g_notices.empty() && now - g_notices.front().at > maxAgeMs) g_notices.pop_front();
    for (const auto& n : g_notices) fn(n.text, static_cast<unsigned>(now - n.at));
}

} // namespace features
