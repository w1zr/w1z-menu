// Feature registry. Every menu item that does something is a Feature. Each
// one is registered at startup and marked available only once its code was
// found by signature; otherwise it stays greyed out in the menu with the
// reason, and the rest keep working.
#pragma once
#include <atomic>
#include <functional>
#include <string>

namespace features {

enum class Kind { Toggle, Action, Number };

struct Feature {
    std::string id;      // unique; also the ini key for toggles/numbers
    std::string section; // ini section ("" = not saved)
    std::string label;
    std::string help;
    Kind kind = Kind::Toggle;

    std::atomic<bool> available{false};
    std::string reason = "Not loaded yet.";

    std::atomic<bool> on{false};  // Toggle
    std::atomic<int> value{0};    // Number
    int minValue = 0, maxValue = 0, step = 1;
    bool restartRequired = false; // change applies on the next launch
    bool alwaysSave = false;      // saved even when RememberToggles is off (menu settings)

    std::function<void(bool)> onToggle; // called on every change (any thread)
    std::function<void()> onAction;
    std::function<void(int)> onNumber;
};

// Creates (or returns the existing) feature with this id. Toggles and numbers
// with a section load their saved value from the ini here.
Feature& Add(const char* id, const char* section, const char* label, const char* help, Kind kind, int def = 0);
Feature* Find(const std::string& id);
void ForEach(const std::function<void(Feature&)>& fn);

void MarkAvailable(Feature& f);
void MarkUnavailable(Feature& f, const std::string& reason);

// Changes a toggle, runs its callback, saves it and, when `notify` is set,
// shows an on-screen message.
void SetToggle(Feature& f, bool on, bool notify);
void SetNumber(Feature& f, int value);
void Run(Feature& f);

// Loaded/total counts for the status line.
void Counts(int& loaded, int& total);

// Short on-screen messages ("Infinite Ammo ON").
void Notify(const std::string& text);
// Calls fn(text, ageMs) for messages younger than maxAgeMs, oldest first.
void ForEachNotice(unsigned maxAgeMs, const std::function<void(const std::string&, unsigned)>& fn);

} // namespace features
