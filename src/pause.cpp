#include "pause.h"

namespace pause {

namespace {
bool g_available = false;
std::string g_reason = "The engine's pause has not been located yet.";
} // namespace

void Init() {}

bool Available() { return g_available; }
std::string Reason() { return g_reason; }

bool Set(bool) { return false; }

} // namespace pause
