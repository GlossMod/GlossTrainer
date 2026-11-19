#include "input_win32.hpp"
#include "renderer_dx12.hpp"
#include "runtime.hpp"

#include <CommCtrl.h>
#include <windowsx.h>

#include <array>
#include <atomic>
#include <mutex>
#include <new>

#pragma comment(lib, "comctl32.lib")

namespace gloss::dndg {
namespace {

constexpr UINT_PTR SubclassId = 1;
std::atomic<HWND> activeWindow{};

enum class Route : unsigned char { Unknown, Game, Overlay, Hotkey };
struct InputMessage { UINT message; WPARAM wparam; LPARAM lparam; };
struct InputState {
    std::array<InputMessage, 32> deferred{};
    size_t head{}, count{};
    UINT_PTR timer{};
    bool forwarding{};
    std::array<Route, 256> keys{};
    std::array<Route, 5> buttons{};
    unsigned depth{};
    bool destroyed{};
};

struct InputScope {
    explicit InputScope(InputState* value) : state(value) { ++state->depth; }
    ~InputScope() { if (--state->depth == 0 && state->destroyed) delete state; }
    InputState* state;
};

bool OsShortcut(WPARAM key, LPARAM data) {
    if (key == VK_LWIN || key == VK_RWIN || key == VK_APPS) return true;
    // Leave standard window management and task switching with the game/OS.
    if (data & (LPARAM{1} << 29))
        return key == VK_F4 || key == VK_TAB || key == VK_SPACE || key == VK_ESCAPE;
    return key == VK_ESCAPE && (GetKeyState(VK_CONTROL) & 0x8000);
}

void StopTimer(HWND hwnd, InputState& state) {
    if (state.timer) KillTimer(hwnd, state.timer);
    state.timer = 0;
}

void Enqueue(InputState& state, InputMessage message) {
    state.deferred[(state.head + state.count++) % state.deferred.size()] = message;
}

void DrainDeferred(HWND hwnd, InputState& state) {
    if (state.forwarding) return;
    state.forwarding = true;
    while (state.count && !state.destroyed) {
        const auto next = state.deferred[state.head];
        LRESULT unused{};
        if (!TryForwardNativeWin32Message(hwnd, next.message, next.wparam, next.lparam, unused)) break;
        state.head = (state.head + 1) % state.deferred.size();
        --state.count;
    }
    state.forwarding = false;
    if (!state.count) StopTimer(hwnd, state);
}

WPARAM ReleaseWparam(size_t button) {
    return button >= 3 ? MAKEWPARAM(0, button == 3 ? XBUTTON1 : XBUTTON2) : 0;
}
constexpr UINT MouseReleases[]{WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP};

LRESULT Forward(HWND hwnd, InputState& state, UINT message, WPARAM wparam, LPARAM lparam, bool defer = true) {
    LRESULT result{};
    if (state.destroyed) return result;
    if (state.forwarding) {
        // SetCapture/ReleaseCapture can deliver nested capture notifications.
        TryForwardNativeWin32Message(hwnd, message, wparam, lparam, result);
        return result;
    }
    DrainDeferred(hwnd, state);
    if (state.destroyed) return 0;
    if (!state.count && TryForwardNativeWin32Message(hwnd, message, wparam, lparam, result)) return result;
    if (!defer) return 0;
    if (state.count == state.deferred.size()) {
        // A stalled renderer must not create an unbounded WndProc backlog.
        // Drop stale input and release the backend's capture/focus state.
        state.head = state.count = 0;
        for (size_t i = 0; i < state.buttons.size(); ++i)
            if (state.buttons[i] != Route::Game) Enqueue(state, {MouseReleases[i], ReleaseWparam(i), 0});
        Enqueue(state, {WM_KILLFOCUS, 0, 0});
        if (GetFocus() == hwnd) Enqueue(state, {WM_SETFOCUS, 0, 0});
    }
    Enqueue(state, {message, wparam, lparam});
    if (!state.timer) state.timer = SetTimer(hwnd, reinterpret_cast<UINT_PTR>(&state), USER_TIMER_MINIMUM, nullptr);
    return 0;
}

void ClearFocus(HWND hwnd, InputState& state) {
    state.keys.fill(Route::Unknown);
    // The Win32 backend may own mouse capture after an overlay press. Let it
    // release only its own tracked buttons instead of changing game capture.
    for (size_t i = 0; i < state.buttons.size(); ++i) {
        const Route route = state.buttons[i];
        state.buttons[i] = Route::Unknown;
        if (route == Route::Overlay) Forward(hwnd, state, MouseReleases[i], ReleaseWparam(i), 0);
    }
    Forward(hwnd, state, WM_KILLFOCUS, 0, 0);
}


int MouseButton(UINT message, WPARAM wparam, bool& down) {
    down = false;
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: down = true; [[fallthrough]];
    case WM_LBUTTONUP: return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: down = true; [[fallthrough]];
    case WM_RBUTTONUP: return 1;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: down = true; [[fallthrough]];
    case WM_MBUTTONUP: return 2;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: down = true; [[fallthrough]];
    case WM_XBUTTONUP: return GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4;
    default: return -1;
    }
}

LRESULT CALLBACK InputProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
    auto* state = reinterpret_cast<InputState*>(data);
    const InputScope scope(state);
    if (message == WM_NCDESTROY) {
        state->destroyed = true;
        StopTimer(hwnd, *state);
        RemoveWindowSubclass(hwnd, InputProc, id);
        HWND expected = hwnd;
        activeWindow.compare_exchange_strong(expected, nullptr);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    if (message == WM_TIMER && wparam == reinterpret_cast<UINT_PTR>(state)) {
        DrainDeferred(hwnd, *state);
        return 0;
    }
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wparam) ||
        (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE) || message == WM_CANCELMODE) {
        ClearFocus(hwnd, *state);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    if (message == WM_SETFOCUS || message == WM_CAPTURECHANGED || message == WM_MOUSELEAVE ||
        message == WM_NCMOUSELEAVE) {
        Forward(hwnd, *state, message, wparam, lparam);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }

    const bool selected = activeWindow.load() == hwnd;
    const bool active = selected && GetActiveWindow() == hwnd;
    const bool menu = active && NativeMenuOpen();
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP) &&
        wparam < state->keys.size()) {
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool repeated = (lparam & (LPARAM{1} << 30)) != 0;
        auto& key = state->keys[wparam];
        if (down && (!repeated || key == Route::Unknown)) {
            if (!active || OsShortcut(wparam, lparam)) key = Route::Game;
            else if (HandleNativeKey(static_cast<unsigned>(wparam), repeated)) key = Route::Hotkey;
            else key = menu ? Route::Overlay : Route::Game;
        }
        const Route route = key;
        if (!down) key = Route::Unknown;
        if (route == Route::Overlay) Forward(hwnd, *state, message, wparam, lparam);
        // Pair capture decisions across Insert, Escape, and focus transitions.
        if (route == Route::Overlay || route == Route::Hotkey) return 0;
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }

    bool down{};
    const int button = MouseButton(message, wparam, down);
    if (button >= 0) {
        auto& held = state->buttons[static_cast<size_t>(button)];
        if (down && held == Route::Unknown) held = menu ? Route::Overlay : Route::Game;
        const Route route = held;
        if (!down) held = Route::Unknown;
        if (route == Route::Overlay) {
            Forward(hwnd, *state, message, wparam, lparam);
            return button >= 3 ? TRUE : 0;
        }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }

    switch (message) {
    case WM_MOUSEMOVE:
        if (selected) Forward(hwnd, *state, message, wparam, lparam, false);
        // For absolute pointer input a menu can ignore movement in the game;
        // don't transact with the game's capture mode on its behalf.
        if (menu) return 0;
        break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_CHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
        if (menu) {
            Forward(hwnd, *state, message, wparam, lparam);
            return 0;
        }
        break;
    case WM_UNICHAR:
        if (menu) {
            if (wparam == UNICODE_NOCHAR) return TRUE;
            // ImGui's Unicode Win32 backend receives UTF-16 through WM_CHAR.
            if (wparam <= 0xffff) {
                if (wparam < 0xd800 || wparam > 0xdfff) Forward(hwnd, *state, WM_CHAR, wparam, lparam);
            } else if (wparam <= 0x10ffff) {
                const auto relative = wparam - 0x10000;
                Forward(hwnd, *state, WM_CHAR, 0xd800 + (relative >> 10), lparam);
                Forward(hwnd, *state, WM_CHAR, 0xdc00 + (relative & 0x3ff), lparam);
            }
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (menu) {
            const LRESULT handled = Forward(hwnd, *state, message, wparam, lparam, false);
            if (handled) return handled;
        }
        break;
    case WM_INPUT:
        if (menu) {
            // RIM_INPUT foreground packet cleanup remains the OS's job.
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

bool InstallSubclass(HWND hwnd) {
    DWORD_PTR data{};
    if (GetWindowSubclass(hwnd, InputProc, SubclassId, &data)) return true;
    auto* state = new (std::nothrow) InputState;
    if (!state) return false;
    if (SetWindowSubclass(hwnd, InputProc, SubclassId, reinterpret_cast<DWORD_PTR>(state))) return true;
    delete state;
    return false;
}

UINT BootstrapMessage() {
    static const UINT message = RegisterWindowMessageW(L"GlossTrainer.DnDG.NativeInput.20261118");
    return message;
}

struct BootstrapState {
    std::mutex mutex;
    HWND hwnd{};
    HHOOK hook{};
    WPARAM ticket{};
};
BootstrapState& Bootstrap() {
    static auto* state = new BootstrapState;
    return *state;
}

LRESULT CALLBACK BootstrapProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && wparam == PM_REMOVE) {
        auto& message = *reinterpret_cast<MSG*>(lparam);
        if (message.message == BootstrapMessage()) {
            auto& state = Bootstrap();
            HHOOK hook{};
            {
                // Do not wait behind a renderer thread replacing a request.
                const std::unique_lock lock(state.mutex, std::try_to_lock);
                if (lock && state.hook && state.hwnd == message.hwnd && state.ticket == message.wParam) {
                    hook = state.hook;
                    state.hook = nullptr;
                    state.hwnd = nullptr;
                }
            }
            if (hook) {
                if (activeWindow.load() == message.hwnd && !InstallSubclass(message.hwnd))
                    OutputDebugStringA("[DnDG] Win32 input subclass installation failed\n");
                message.message = WM_NULL;
                message.wParam = 0;
                message.lParam = 0;
                UnhookWindowsHookEx(hook);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

}  // namespace

bool HookNativeInput(HWND hwnd) noexcept {
    try {
        DWORD pid{};
        const DWORD thread = GetWindowThreadProcessId(hwnd, &pid);
        if (!thread || pid != GetCurrentProcessId()) return false;
        activeWindow.store(hwnd);
        if (thread == GetCurrentThreadId()) return InstallSubclass(hwnd);
        const UINT message = BootstrapMessage();
        if (!message) return false;
        auto& state = Bootstrap();
        WPARAM ticket{};
        {
            const std::lock_guard lock(state.mutex);
            if (state.hook && state.hwnd == hwnd) return true;
            if (state.hook) UnhookWindowsHookEx(state.hook);
            state.hook = SetWindowsHookExW(WH_GETMESSAGE, BootstrapProc, nullptr, thread);
            if (!state.hook) {
                state.hwnd = nullptr;
                return false;
            }
            state.hwnd = hwnd;
            ticket = ++state.ticket;
        }
        // WH_GETMESSAGE cannot observe this until all request state is published.
        if (PostMessageW(hwnd, message, ticket, 0)) return true;
        const std::lock_guard lock(state.mutex);
        if (state.hook && state.hwnd == hwnd && state.ticket == ticket) {
            UnhookWindowsHookEx(state.hook);
            state.hook = nullptr;
            state.hwnd = nullptr;
        }
    } catch (...) {
        return false;
    }
    return false;
}

}  // namespace gloss::dndg
