#include "input.h"

#include "log.h"
#include "menu.h"

#include "../third_party/imgui/imgui.h"
#include "../third_party/minhook/include/MinHook.h"

#include <windowsx.h>
#include <xinput.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace input {

namespace {

enum class Ev { Key, Char, Button, Wheel };
struct Event {
    Ev type;
    int a = 0;    // vk / char / button index
    bool down = false;
    float wheel = 0;
};

std::mutex g_lock;
std::vector<Event> g_events;
std::atomic<bool> g_open{false};
std::atomic<bool> g_rawKeyboardSeen{false};
std::atomic<bool> g_rawMouseSeen{false};
bool g_keyDown[256] = {};

using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

GetRawInputDataFn oGetRawInputData = nullptr;
ClipCursorFn oClipCursor = nullptr;
SetCursorPosFn oSetCursorPos = nullptr;
XInputGetStateFn oXInputGetState = nullptr;   // XInputGetState (ordinal 2)
XInputGetStateFn oXInputGetStateEx = nullptr; // ordinal 100, also reports the Guide button
XInputGetStateFn g_readPad = nullptr;         // original reader used by the menu

std::atomic<bool> g_clipPending{false};
RECT g_clipRect{};
bool g_clipWasNull = true;

void Push(const Event& e) {
    std::lock_guard<std::mutex> l(g_lock);
    if (g_events.size() < 512) g_events.push_back(e);
}

void HandleKeyboard(RAWKEYBOARD& kb) {
    if (!g_rawKeyboardSeen.exchange(true)) logx::Write("input: raw keyboard events seen");
    UINT vk = kb.VKey;
    const bool down = !(kb.Flags & RI_KEY_BREAK);
    if (vk == 0 || vk >= 255) return;
    // Raw input reports generic Shift/Ctrl/Alt; ImGui only needs the generic ones.
    if (vk < 256) g_keyDown[vk] = down;
    if (!g_open) return;

    Push({Ev::Key, static_cast<int>(vk), down});
    if (down) {
        BYTE state[256] = {};
        if (g_keyDown[VK_SHIFT]) state[VK_SHIFT] = 0x80;
        if (GetKeyState(VK_CAPITAL) & 1) state[VK_CAPITAL] = 0x01;
        wchar_t buf[4];
        // Flag 4: do not change the keyboard state (Windows 10 1607+).
        const int n = ToUnicodeEx(vk, kb.MakeCode, state, buf, 4, 4, GetKeyboardLayout(0));
        for (int i = 0; i < n; ++i)
            if (buf[i] >= 32) Push({Ev::Char, buf[i]});
        // Blank the press for the game. 0xFF is "no key" (KEYBOARD_OVERRUN_MAKE_CODE).
        kb.VKey = 0xFF;
        kb.MakeCode = 0xFF;
    }
}

void HandleMouse(RAWMOUSE& m) {
    if (!g_rawMouseSeen.exchange(true)) logx::Write("input: raw mouse events seen");
    if (!g_open) return;
    const USHORT f = m.usButtonFlags;
    if (f & RI_MOUSE_LEFT_BUTTON_DOWN) Push({Ev::Button, 0, true});
    if (f & RI_MOUSE_LEFT_BUTTON_UP) Push({Ev::Button, 0, false});
    if (f & RI_MOUSE_RIGHT_BUTTON_DOWN) Push({Ev::Button, 1, true});
    if (f & RI_MOUSE_RIGHT_BUTTON_UP) Push({Ev::Button, 1, false});
    if (f & RI_MOUSE_MIDDLE_BUTTON_DOWN) Push({Ev::Button, 2, true});
    if (f & RI_MOUSE_MIDDLE_BUTTON_UP) Push({Ev::Button, 2, false});
    if (f & RI_MOUSE_WHEEL) Push({Ev::Wheel, 0, false, static_cast<SHORT>(m.usButtonData) / static_cast<float>(WHEEL_DELTA)});
    // Keep only releases; drop movement, presses and the wheel.
    constexpr USHORT kUps = RI_MOUSE_BUTTON_1_UP | RI_MOUSE_BUTTON_2_UP | RI_MOUSE_BUTTON_3_UP | RI_MOUSE_BUTTON_4_UP |
                            RI_MOUSE_BUTTON_5_UP;
    m.usButtonFlags = f & kUps;
    m.usButtonData = 0;
    m.lLastX = m.lLastY = 0;
}

UINT WINAPI HkGetRawInputData(HRAWINPUT h, UINT cmd, LPVOID data, PUINT size, UINT header) {
    const UINT r = oGetRawInputData(h, cmd, data, size, header);
    if (cmd == RID_INPUT && data && r != static_cast<UINT>(-1) && r >= sizeof(RAWINPUTHEADER)) {
        auto* ri = static_cast<RAWINPUT*>(data);
        if (ri->header.dwType == RIM_TYPEKEYBOARD) HandleKeyboard(ri->data.keyboard);
        else if (ri->header.dwType == RIM_TYPEMOUSE) HandleMouse(ri->data.mouse);
    }
    return r;
}

BOOL WINAPI HkClipCursor(const RECT* rc) {
    if (g_open) {
        // Remember what the game wanted and apply it when the menu closes.
        g_clipWasNull = rc == nullptr;
        if (rc) g_clipRect = *rc;
        g_clipPending = true;
        return TRUE;
    }
    return oClipCursor(rc);
}

BOOL WINAPI HkSetCursorPos(int x, int y) {
    if (g_open) return TRUE;
    return oSetCursorPos(x, y);
}

DWORD Blank(DWORD r, XINPUT_STATE* s) {
    if (r == ERROR_SUCCESS && s && g_open) ZeroMemory(&s->Gamepad, sizeof(s->Gamepad));
    return r;
}
DWORD WINAPI HkXInputGetState(DWORD i, XINPUT_STATE* s) { return Blank(oXInputGetState(i, s), s); }
DWORD WINAPI HkXInputGetStateEx(DWORD i, XINPUT_STATE* s) { return Blank(oXInputGetStateEx(i, s), s); }

bool Hook(void* target, void* detour, void** original, const char* name) {
    if (!target) {
        logx::Write("input: %s not found", name);
        return false;
    }
    if (MH_CreateHook(target, detour, original) != MH_OK || MH_EnableHook(target) != MH_OK) {
        logx::Write("input: hooking %s failed", name);
        return false;
    }
    return true;
}

// Window subclass: swallow legacy input messages while the menu is open.
WNDPROC g_oldProc = nullptr;
HWND g_window = nullptr;

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_open) {
        switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MOUSEWHEEL:
            // Only used when the game never registered raw mouse input.
            if (!g_rawMouseSeen) {
                if (msg == WM_MOUSEWHEEL) Push({Ev::Wheel, 0, false, GET_WHEEL_DELTA_WPARAM(wp) / static_cast<float>(WHEEL_DELTA)});
                else {
                    const int b = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) ? 0 : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) ? 1 : 2;
                    const bool d = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
                    Push({Ev::Button, b, d});
                }
            }
            return 0;
        case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MOUSEMOVE:
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_CHAR:
            return 0;
        default:
            break;
        }
    }
    return CallWindowProcW(g_oldProc, h, msg, wp, lp);
}

ImGuiKey ToImGuiKey(int vk) {
    switch (vk) {
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_TAB: return ImGuiKey_Tab;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_SHIFT: return ImGuiMod_Shift;
    case VK_CONTROL: return ImGuiMod_Ctrl;
    case VK_MENU: return ImGuiMod_Alt;
    default: return ImGuiKey_None;
    }
}

} // namespace

bool Install() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    bool ok = Hook(reinterpret_cast<void*>(GetProcAddress(user32, "GetRawInputData")),
                   reinterpret_cast<void*>(&HkGetRawInputData), reinterpret_cast<void**>(&oGetRawInputData), "GetRawInputData");
    Hook(reinterpret_cast<void*>(GetProcAddress(user32, "ClipCursor")), reinterpret_cast<void*>(&HkClipCursor),
         reinterpret_cast<void**>(&oClipCursor), "ClipCursor");
    Hook(reinterpret_cast<void*>(GetProcAddress(user32, "SetCursorPos")), reinterpret_cast<void*>(&HkSetCursorPos),
         reinterpret_cast<void**>(&oSetCursorPos), "SetCursorPos");

    // The game imports xinput1_4.dll by ordinal (2 = XInputGetState, 100 = XInputGetStateEx).
    HMODULE xi = GetModuleHandleW(L"xinput1_4.dll");
    if (!xi) xi = LoadLibraryW(L"xinput1_4.dll");
    if (xi) {
        if (Hook(reinterpret_cast<void*>(GetProcAddress(xi, "XInputGetState")), reinterpret_cast<void*>(&HkXInputGetState),
                 reinterpret_cast<void**>(&oXInputGetState), "XInputGetState"))
            g_readPad = oXInputGetState;
        Hook(reinterpret_cast<void*>(GetProcAddress(xi, MAKEINTRESOURCEA(100))), reinterpret_cast<void*>(&HkXInputGetStateEx),
             reinterpret_cast<void**>(&oXInputGetStateEx), "XInputGetStateEx");
        if (!g_readPad) g_readPad = reinterpret_cast<XInputGetStateFn>(GetProcAddress(xi, "XInputGetState"));
    }
    logx::Write("input hooks %s", ok ? "installed" : "INCOMPLETE (menu keyboard input may not work)");
    return ok;
}

void OnMenuOpened() {
    g_open = true;
    if (oClipCursor) oClipCursor(nullptr);
    std::lock_guard<std::mutex> l(g_lock);
    g_events.clear();
}

void OnMenuClosed() {
    g_open = false;
    if (g_clipPending.exchange(false) && oClipCursor) oClipCursor(g_clipWasNull ? nullptr : &g_clipRect);
}

void AttachWindow(HWND window) {
    if (g_window || !window) return;
    g_window = window;
    g_oldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
    logx::Write("input: game window 0x%p %s", window, g_oldProc ? "attached" : "could not be attached");
}

void FeedImGui(HWND window, float bbW, float bbH) {
    ImGuiIO& io = ImGui::GetIO();
    std::vector<Event> ev;
    {
        std::lock_guard<std::mutex> l(g_lock);
        ev.swap(g_events);
    }
    for (const auto& e : ev) {
        switch (e.type) {
        case Ev::Key: {
            const ImGuiKey k = ToImGuiKey(e.a);
            if (k != ImGuiKey_None) io.AddKeyEvent(k, e.down);
            break;
        }
        case Ev::Char: io.AddInputCharacter(static_cast<unsigned>(e.a)); break;
        case Ev::Button: io.AddMouseButtonEvent(e.a, e.down); break;
        case Ev::Wheel: io.AddMouseWheelEvent(0, e.wheel); break;
        }
    }

    // Cursor position in back-buffer pixels (the client area can differ in size).
    POINT p;
    RECT rc;
    if (window && GetCursorPos(&p) && ScreenToClient(window, &p) && GetClientRect(window, &rc) && rc.right > 0 && rc.bottom > 0)
        io.AddMousePosEvent(p.x * bbW / rc.right, p.y * bbH / rc.bottom);
}

Pad PollPad() {
    // XInputGetState on an empty slot is slow, so empty slots are only
    // re-checked every two seconds and a found pad's slot is remembered.
    static uint16_t last = 0;
    static int slot = -1;
    static ULONGLONG nextScan = 0;
    Pad pad;
    if (!g_readPad) return pad;
    XINPUT_STATE s{};
    if (slot >= 0 && g_readPad(static_cast<DWORD>(slot), &s) != ERROR_SUCCESS) slot = -1;
    if (slot < 0 && GetTickCount64() >= nextScan) {
        nextScan = GetTickCount64() + 2000;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            if (g_readPad(i, &s) == ERROR_SUCCESS) {
                slot = static_cast<int>(i);
                break;
            }
        }
    }
    if (slot >= 0) {
        pad.connected = true;
        pad.held = s.Gamepad.wButtons;
    }
    pad.pressed = static_cast<uint16_t>(pad.held & ~last);
    last = pad.held;
    return pad;
}

} // namespace input
