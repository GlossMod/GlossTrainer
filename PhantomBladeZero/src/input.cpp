#include "runtime.hpp"
#include "ime.hpp"
#include "input_mouse_shortcuts.hpp"
#include "hotkeys.hpp"
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace gloss::input
{
    namespace
    {
        constexpr UINT CursorSync = WM_APP + 0x351;
        using RawKey = std::tuple<ULONG_PTR, USHORT, USHORT, USHORT>;
        struct WindowState
        {
            HWND window{};
            WNDPROC original{};
            std::mutex mutex;
            RECT desiredClip{};
            bool hasDesiredClip{}, clipSuppressed{}, cursorCaptured{}, pendingRelease{};
            bool rawMouseOnly{}, rawKeyboardOnly{};
            std::array<bool, 256> gameKeys{}, systemKeys{};
            std::array<bool, 256> rawShortcutDown{}, legacyShortcutDown{}, shortcutConsumed{}, rawShortcutConsumed{};
            std::array<bool, 512> shortcutCharacters{};
            std::array<LPARAM, 256> keyParams{};
            std::array<bool, 5> gameButtons{};
            std::set<RawKey> rawKeys;
            std::map<ULONG_PTR, std::array<bool, 5>> rawButtons;
            MouseShortcutState mouseShortcuts;
        };
        std::mutex windowsMutex;
        std::map<HWND, std::shared_ptr<WindowState>> windows;
        std::atomic<HWND> targetWindow{};
        std::atomic_bool capture{}, rawFilterReady{};
        thread_local bool bypass{};
        struct RawReleaseFilter
        {
            HRAWINPUT handle{};
            USHORT mouseUps{};
            USHORT mouseSuppressed{};
        };
        thread_local RawReleaseFilter releaseFilter;
        struct ReleaseFilterScope
        {
            RawReleaseFilter previous = releaseFilter;
            ReleaseFilterScope(HRAWINPUT handle, USHORT mouseUps, USHORT mouseSuppressed = 0)
            {
                if ((mouseUps || mouseSuppressed) && rawFilterReady.load())
                    releaseFilter = {handle, mouseUps, mouseSuppressed};
            }
            ~ReleaseFilterScope() { releaseFilter = previous; }
        };
        using ClipFn = BOOL(WINAPI *)(const RECT *);
        using PositionFn = BOOL(WINAPI *)(int, int);
        using RawDataFn = UINT(WINAPI *)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
        ClipFn originalClip = &::ClipCursor;
        PositionFn originalPosition = &::SetCursorPos;
        RawDataFn originalRawData = &::GetRawInputData;

        std::shared_ptr<WindowState> FindState(HWND window)
        {
            std::lock_guard lock(windowsMutex);
            const auto found = windows.find(window);
            return found == windows.end() ? nullptr : found->second;
        }
        bool Active(HWND window)
        {
            return rawFilterReady.load() && capture.load() && targetWindow.load() == window && GetForegroundWindow() == window;
        }
        BOOL WINAPI ClipHook(const RECT *rect)
        {
            if (!bypass)
            {
                const auto window = targetWindow.load();
                if (const auto state = FindState(window); state && Active(window))
                {
                    std::lock_guard lock(state->mutex);
                    state->hasDesiredClip = rect != nullptr;
                    if (rect)
                        state->desiredClip = *rect;
                    state->clipSuppressed = true;
                    return TRUE;
                }
            }
            return originalClip(rect);
        }
        BOOL WINAPI PositionHook(int x, int y)
        {
            if (!bypass && Active(targetWindow.load()))
                return TRUE;
            return originalPosition(x, y);
        }
        UINT WINAPI RawDataHook(HRAWINPUT handle, UINT command, LPVOID data, PUINT size, UINT headerSize)
        {
            const auto result = originalRawData(handle, command, data, size, headerSize);
            if (handle == releaseFilter.handle && (releaseFilter.mouseUps || releaseFilter.mouseSuppressed) && command == RID_INPUT && data &&
                result != UINT(-1) && result >= offsetof(RAWINPUT, data) + sizeof(RAWMOUSE))
            {
                auto &raw = *static_cast<RAWINPUT *>(data);
                if (raw.header.dwType == RIM_TYPEMOUSE)
                {
                    if (releaseFilter.mouseUps)
                    {
                        // While the menu captures input, release only buttons whose
                        // original down was already delivered to the game.
                        raw.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
                        raw.data.mouse.usButtonFlags = releaseFilter.mouseUps;
                        raw.data.mouse.usButtonData = 0;
                        raw.data.mouse.ulRawButtons = 0;
                        raw.data.mouse.lLastX = raw.data.mouse.lLastY = 0;
                    }
                    // Outside menu capture, suppress only bound transitions. Keep
                    // camera movement, wheel and unrelated buttons in the same packet.
                    FilterMouseButtons(raw.data.mouse, releaseFilter.mouseSuppressed);
                }
            }
            return result;
        }
        void ReadRawRegistration(const std::shared_ptr<WindowState> &state)
        {
            bool mouseOnly{}, keyboardOnly{};
            UINT count{};
            if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) == 0 && count <= 256)
            {
                std::vector<RAWINPUTDEVICE> devices(count);
                if (count && GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) != UINT(-1))
                {
                    for (const auto &device : devices)
                    {
                        if (device.usUsagePage != 1 || (device.dwFlags & RIDEV_NOLEGACY) != RIDEV_NOLEGACY)
                            continue;
                        if (device.usUsage == 2)
                            mouseOnly = true;
                        if (device.usUsage == 6)
                            keyboardOnly = true;
                    }
                }
            }
            std::lock_guard lock(state->mutex);
            state->rawMouseOnly = mouseOnly;
            state->rawKeyboardOnly = keyboardOnly;
        }
        void ReleaseGameKeys(const std::shared_ptr<WindowState> &state)
        {
            struct Release
            {
                UINT message;
                WPARAM key;
                LPARAM parameters;
            };
            std::vector<Release> releases;
            {
                std::lock_guard lock(state->mutex);
                for (UINT key = 0; key < state->gameKeys.size(); ++key)
                {
                    if (!state->gameKeys[key])
                        continue;
                    releases.push_back({UINT(state->systemKeys[key] ? WM_SYSKEYUP : WM_KEYUP), key,
                                        state->keyParams[key] | (LPARAM(3) << 30)});
                }
                constexpr UINT messages[] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP};
                for (size_t index = 0; index < state->gameButtons.size(); ++index)
                {
                    if (!state->gameButtons[index])
                        continue;
                    releases.push_back({messages[index], index < 3 ? WPARAM(0) : MAKEWPARAM(0, index == 3 ? XBUTTON1 : XBUTTON2), 0});
                }
                state->gameKeys.fill(false);
                state->gameButtons.fill(false);
                state->pendingRelease = false;
            }
            // Only called by our window-thread message handler, with no input/ImGui lock.
            for (const auto &release : releases)
                CallWindowProcW(state->original, state->window, release.message, release.key, release.parameters);
        }
        void RestoreClip(const std::shared_ptr<WindowState> &state)
        {
            RECT rect{};
            bool restore{}, changed{};
            {
                std::lock_guard lock(state->mutex);
                changed = state->cursorCaptured;
                state->cursorCaptured = false;
                rect = state->desiredClip;
                restore = state->hasDesiredClip && GetForegroundWindow() == state->window;
                state->clipSuppressed = false;
            }
            if (changed)
                originalClip(restore ? &rect : nullptr);
        }
        void SyncCursor(const std::shared_ptr<WindowState> &state)
        {
            if (Active(state->window))
            {
                RECT actualClip{};
                const bool hasActualClip = GetClipCursor(&actualClip) != FALSE;
                bool entered{};
                {
                    std::lock_guard lock(state->mutex);
                    entered = !state->cursorCaptured;
                    state->cursorCaptured = true;
                    if (entered && !state->clipSuppressed)
                    {
                        state->desiredClip = actualClip;
                        state->hasDesiredClip = hasActualClip;
                    }
                }
                if (entered)
                {
                    ReleaseGameKeys(state);
                    ReadRawRegistration(state);
                    // Raw-only consumers cannot be safely neutralized with synthetic
                    // WM_KEYUP messages. Retain their states until the real up packet.
                    bool rawHeld{};
                    {
                        std::lock_guard lock(state->mutex);
                        rawHeld = !state->rawKeys.empty();
                        for (const auto &[device, buttons] : state->rawButtons)
                            for (const auto held : buttons)
                                rawHeld |= held;
                    }
                    if (rawHeld)
                        Log("Held raw inputs will be released on their physical up events");
                    originalClip(nullptr);
                    if (GetCapture() == state->window)
                        ReleaseCapture();
                }
            }
            else
            {
                RestoreClip(state);
                bool pending{};
                {
                    std::lock_guard lock(state->mutex);
                    pending = state->pendingRelease;
                }
                if (pending)
                    ReleaseGameKeys(state);
                if (GetCapture() == state->window)
                    ReleaseCapture();
            }
            if (ime::Sync(state->window, Active(state->window)))
                ReadRawRegistration(state);
        }
        bool IsInputMessage(UINT message)
        {
            return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
                   (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_UNICHAR;
        }
        hotkeys::Chord CurrentChord(unsigned key)
        {
            unsigned modifiers{};
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifiers |= hotkeys::Control;
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifiers |= hotkeys::Shift;
            if (GetAsyncKeyState(VK_MENU) & 0x8000) modifiers |= hotkeys::Alt;
            if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) modifiers |= hotkeys::Windows;
            return {key, modifiers};
        }
        bool MatchesLegacyShortcut(unsigned key)
        {
            const auto chord = CurrentChord(key);
            return (key == VK_INSERT && !chord.modifiers) ||
                hotkeys::GetRegistry().Matches(chord, true, overlay::Visible());
        }
        enum class ShortcutAction
        {
            None,
            Consumed,
            ToggleMenu
        };
        ShortcutAction DispatchHotkey(unsigned key)
        {
            const auto chord = CurrentChord(key);
            // Capture has priority over the fixed menu key and gameplay actions.
            if (hotkeys::GetRegistry().QueueKey(chord, true, overlay::Visible())) return ShortcutAction::Consumed;
            return key == VK_INSERT && !chord.modifiers ? ShortcutAction::ToggleMenu : ShortcutAction::None;
        }
        void ToggleMenu(const std::shared_ptr<WindowState> &state)
        {
            {
                std::lock_guard lock(OverlayMutex());
                overlay::SetVisible(!overlay::Visible());
            }
            SyncCursor(state);
        }
        bool ReadRaw(HRAWINPUT handle, RAWINPUT &raw)
        {
            UINT size{};
            if (originalRawData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 ||
                size < sizeof(RAWINPUTHEADER) || size > 65536)
                return false;
            std::vector<BYTE> bytes(size);
            const auto read = originalRawData(handle, RID_INPUT, bytes.data(), &size, sizeof(RAWINPUTHEADER));
            if (read == UINT(-1) || read < sizeof(RAWINPUTHEADER))
                return false;
            std::memcpy(&raw, bytes.data(), (std::min)(size_t(read), sizeof(raw)));
            if (raw.header.dwType == RIM_TYPEMOUSE)
                return read >= offsetof(RAWINPUT, data) + sizeof(RAWMOUSE);
            if (raw.header.dwType == RIM_TYPEKEYBOARD)
                return read >= offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD);
            return true;
        }
        RawKey KeyIdentity(const RAWINPUT &raw)
        {
            const auto &key = raw.data.keyboard;
            return {reinterpret_cast<ULONG_PTR>(raw.header.hDevice), key.MakeCode,
                    USHORT(key.Flags & (RI_KEY_E0 | RI_KEY_E1)), key.VKey};
        }
        void TrackRaw(const std::shared_ptr<WindowState> &state, const RAWINPUT &raw)
        {
            std::lock_guard lock(state->mutex);
            if (raw.header.dwType == RIM_TYPEKEYBOARD)
            {
                const auto identity = KeyIdentity(raw);
                if (raw.data.keyboard.Flags & RI_KEY_BREAK)
                    state->rawKeys.erase(identity);
                else
                    state->rawKeys.insert(identity);
            }
            else if (raw.header.dwType == RIM_TYPEMOUSE)
            {
                auto &buttons = state->rawButtons[reinterpret_cast<ULONG_PTR>(raw.header.hDevice)];
                for (size_t index = 0; index < buttons.size(); ++index)
                {
                    if (raw.data.mouse.usButtonFlags & MouseDown[index])
                        buttons[index] = true;
                    if (raw.data.mouse.usButtonFlags & MouseUp[index])
                        buttons[index] = false;
                }
            }
        }
        // Feed raw events only for devices whose registration suppresses legacy events.
        // The caller never holds state->mutex while touching ImGui.
        bool FeedRaw(const std::shared_ptr<WindowState> &state, const RAWINPUT &raw)
        {
            bool mouseOnly{}, keyboardOnly{}, hotkeyDown{}, consumed{};
            {
                std::lock_guard lock(state->mutex);
                mouseOnly = state->rawMouseOnly;
                keyboardOnly = state->rawKeyboardOnly;
                if (raw.header.dwType == RIM_TYPEKEYBOARD && raw.data.keyboard.VKey < 256)
                {
                    const auto key = raw.data.keyboard.VKey;
                    const bool released = (raw.data.keyboard.Flags & RI_KEY_BREAK) != 0;
                    hotkeyDown = !released && !state->rawShortcutDown[key];
                    state->rawShortcutDown[key] = !released;
                    consumed = state->rawShortcutConsumed[key];
                    if (released)
                    {
                        state->rawShortcutConsumed[key] = false;
                        if (keyboardOnly || !state->legacyShortcutDown[key])
                            state->shortcutConsumed[key] = false;
                    }
                }
            }
            if (consumed)
                return true;
            if (hotkeyDown && GetForegroundWindow() == state->window)
            {
                if (!keyboardOnly)
                {
                    // Legacy will execute the action. Suppress only this raw copy so
                    // the same shortcut cannot also reach the game's raw consumer.
                    if (MatchesLegacyShortcut(raw.data.keyboard.VKey))
                    {
                        std::lock_guard lock(state->mutex);
                        state->rawShortcutConsumed[raw.data.keyboard.VKey] = true;
                        return true;
                    }
                }
                else if (const auto action = DispatchHotkey(raw.data.keyboard.VKey); action != ShortcutAction::None)
                {
                    {
                        std::lock_guard lock(state->mutex);
                        state->rawShortcutConsumed[raw.data.keyboard.VKey] = true;
                        state->shortcutConsumed[raw.data.keyboard.VKey] = true;
                    }
                    if (action == ShortcutAction::ToggleMenu)
                        ToggleMenu(state);
                    return true;
                }
            }
            if (!Active(state->window))
                return false;
            std::lock_guard lock(OverlayMutex());
            if (!ImGui::GetCurrentContext())
                return false;
            auto &io = ImGui::GetIO();
            if (raw.header.dwType == RIM_TYPEMOUSE && mouseOnly)
            {
                const auto flags = raw.data.mouse.usButtonFlags;
                for (int index = 0; index < 5; ++index)
                {
                    if (flags & MouseDown[index])
                        io.AddMouseButtonEvent(index, true);
                    if (flags & MouseUp[index])
                        io.AddMouseButtonEvent(index, false);
                }
                if (flags & RI_MOUSE_WHEEL)
                    io.AddMouseWheelEvent(0, static_cast<SHORT>(raw.data.mouse.usButtonData) / float(WHEEL_DELTA));
                if (flags & RI_MOUSE_HWHEEL)
                    io.AddMouseWheelEvent(-static_cast<SHORT>(raw.data.mouse.usButtonData) / float(WHEEL_DELTA), 0);
            }
            else if (raw.header.dwType == RIM_TYPEKEYBOARD && keyboardOnly)
            {
                const auto &key = raw.data.keyboard;
                const bool released = (key.Flags & RI_KEY_BREAK) != 0;
                const LPARAM parameters = LPARAM(key.MakeCode) << 16 | ((key.Flags & RI_KEY_E0) ? LPARAM(1) << 24 : 0) |
                                          (released ? LPARAM(3) << 30 : 0);
                ImGui_ImplWin32_WndProcHandler(state->window, released ? WM_KEYUP : WM_KEYDOWN, key.VKey, parameters);
            }
            return false;
        }
        USHORT MouseShortcuts(const std::shared_ptr<WindowState> &state, const RAWINPUT &raw)
        {
            if (raw.header.dwType != RIM_TYPEMOUSE) return 0;
            bool rawOnly{};
            {
                std::lock_guard lock(state->mutex);
                rawOnly = state->rawMouseOnly;
            }
            const bool foreground = GetForegroundWindow() == state->window;
            const auto device = reinterpret_cast<std::uintptr_t>(raw.header.hDevice);
            USHORT suppressed{};
            for (std::size_t index = 0; index < 5; ++index)
            {
                if (raw.data.mouse.usButtonFlags & MouseDown[index])
                {
                    const bool matches = !rawOnly && foreground && MatchesLegacyShortcut(MouseKeys[index]);
                    MouseShortcutEdge edge;
                    {
                        std::lock_guard lock(state->mutex);
                        edge = state->mouseShortcuts.Raw(device, index, true, rawOnly, foreground, matches);
                    }
                    if (edge.dispatch)
                    {
                        const auto action = DispatchHotkey(MouseKeys[index]);
                        edge.consumed |= action != ShortcutAction::None;
                        {
                            std::lock_guard lock(state->mutex);
                            state->mouseShortcuts.CompleteRaw(device, index, edge.consumed);
                        }
                        if (action == ShortcutAction::ToggleMenu) ToggleMenu(state);
                    }
                    if (edge.consumed) suppressed |= MouseDown[index];
                }
                if (raw.data.mouse.usButtonFlags & MouseUp[index])
                {
                    std::lock_guard lock(state->mutex);
                    if (state->mouseShortcuts.Raw(device, index, false, rawOnly, foreground, false).consumed)
                        suppressed |= MouseUp[index];
                }
            }
            return suppressed;
        }
        LRESULT DispatchRaw(const std::shared_ptr<WindowState> &state, WPARAM wparam, LPARAM lparam, bool isTarget)
        {
            const auto handle = reinterpret_cast<HRAWINPUT>(lparam);
            RAWINPUT raw{};
            const bool valid = ReadRaw(handle, raw);
            const auto suppressed = valid && isTarget ? MouseShortcuts(state, raw) : USHORT{};
            if (suppressed) FilterMouseButtons(raw.data.mouse, suppressed);
            if (valid && isTarget && FeedRaw(state, raw))
                return DefWindowProcW(state->window, WM_INPUT, wparam, lparam);
            if (!isTarget || !Active(state->window))
            {
                if (valid)
                    TrackRaw(state, raw);
                ReleaseFilterScope filter(handle, 0, suppressed);
                return CallWindowProcW(state->original, state->window, WM_INPUT, wparam, lparam);
            }
            bool releaseKeyboard{};
            USHORT mouseUps{};
            if (valid)
            {
                std::lock_guard lock(state->mutex);
                if (raw.header.dwType == RIM_TYPEKEYBOARD && (raw.data.keyboard.Flags & RI_KEY_BREAK))
                {
                    releaseKeyboard = state->rawKeys.erase(KeyIdentity(raw)) != 0;
                }
                else if (raw.header.dwType == RIM_TYPEMOUSE)
                {
                    const auto found = state->rawButtons.find(reinterpret_cast<ULONG_PTR>(raw.header.hDevice));
                    if (found != state->rawButtons.end())
                    {
                        for (size_t index = 0; index < found->second.size(); ++index)
                        {
                            if ((raw.data.mouse.usButtonFlags & MouseUp[index]) && found->second[index])
                            {
                                mouseUps |= MouseUp[index];
                                found->second[index] = false;
                            }
                        }
                    }
                }
            }
            if (releaseKeyboard || mouseUps)
            {
                ReleaseFilterScope filter(handle, mouseUps, suppressed);
                // Do not hold either mutex while the game's procedure reads this real handle.
                return CallWindowProcW(state->original, state->window, WM_INPUT, wparam, lparam);
            }
            return DefWindowProcW(state->window, WM_INPUT, wparam, lparam);
        }
        void TrackLegacy(const std::shared_ptr<WindowState> &state, UINT message, WPARAM wparam, LPARAM lparam)
        {
            std::lock_guard lock(state->mutex);
            if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wparam < state->gameKeys.size())
            {
                state->gameKeys[wparam] = true;
                state->systemKeys[wparam] = message == WM_SYSKEYDOWN;
                state->keyParams[wparam] = lparam;
            }
            if ((message == WM_KEYUP || message == WM_SYSKEYUP) && wparam < state->gameKeys.size())
                state->gameKeys[wparam] = false;
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP)
                state->gameButtons[0] = message != WM_LBUTTONUP;
            if (message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK || message == WM_RBUTTONUP)
                state->gameButtons[1] = message != WM_RBUTTONUP;
            if (message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK || message == WM_MBUTTONUP)
                state->gameButtons[2] = message != WM_MBUTTONUP;
            if (message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK || message == WM_XBUTTONUP)
                state->gameButtons[GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4] = message != WM_XBUTTONUP;
        }
        LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            const auto state = FindState(window);
            if (!state)
                return DefWindowProcW(window, message, wparam, lparam);
            if (message == CursorSync || message == ime::UpdateMessage)
            {
                SyncCursor(state);
                return 0;
            }
            const bool isTarget = window == targetWindow.load();
            if (isTarget && (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wparam)))
            {
                {
                    std::lock_guard lock(state->mutex);
                    state->rawShortcutDown.fill(false);
                    state->legacyShortcutDown.fill(false);
                    state->shortcutConsumed.fill(false);
                    state->rawShortcutConsumed.fill(false);
                    state->shortcutCharacters.fill(false);
                    state->mouseShortcuts.Reset();
                }
                {
                    std::lock_guard lock(OverlayMutex());
                    overlay::OnFocusLost();
                }
                capture = false;
                SyncCursor(state);
            }
            if (isTarget && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wparam < 256)
            {
                const auto scan = (static_cast<ULONG_PTR>(lparam) >> 16) & 0x1ff;
                bool consumed{};
                {
                    std::lock_guard lock(state->mutex);
                    state->legacyShortcutDown[wparam] = true;
                    consumed = state->shortcutConsumed[wparam];
                    state->shortcutCharacters[scan] = consumed;
                }
                if (consumed)
                    return 0;
                if (!(lparam & (LPARAM(1) << 30)) && GetForegroundWindow() == window)
                {
                    if (const auto action = DispatchHotkey(static_cast<unsigned>(wparam)); action != ShortcutAction::None)
                    {
                        {
                            std::lock_guard lock(state->mutex);
                            state->shortcutConsumed[wparam] = true;
                            state->rawShortcutConsumed[wparam] = true;
                            state->shortcutCharacters[scan] = true;
                        }
                        if (action == ShortcutAction::ToggleMenu)
                            ToggleMenu(state);
                        return 0;
                    }
                }
            }
            if (isTarget && (message == WM_KEYUP || message == WM_SYSKEYUP) && wparam < 256)
            {
                std::lock_guard lock(state->mutex);
                state->legacyShortcutDown[wparam] = false;
                const bool consumed = state->shortcutConsumed[wparam];
                state->shortcutConsumed[wparam] = false;
                if (!state->rawShortcutDown[wparam])
                    state->rawShortcutConsumed[wparam] = false;
                if (consumed)
                    return 0;
            }
            if (isTarget && (message == WM_CHAR || message == WM_SYSCHAR || message == WM_DEADCHAR || message == WM_SYSDEADCHAR))
            {
                const auto scan = (static_cast<ULONG_PTR>(lparam) >> 16) & 0x1ff;
                std::lock_guard lock(state->mutex);
                if (scan && state->shortcutCharacters[scan])
                    return 0;
            }
            std::size_t mouseIndex{};
            bool mouseDown{};
            if (isTarget && MouseButtonMessage(message, wparam, mouseIndex, mouseDown))
            {
                MouseShortcutEdge edge;
                {
                    std::lock_guard lock(state->mutex);
                    edge = state->mouseShortcuts.Legacy(mouseIndex, mouseDown, state->rawMouseOnly,
                        GetForegroundWindow() == window);
                }
                if (edge.dispatch)
                {
                    const auto action = DispatchHotkey(MouseKeys[mouseIndex]);
                    edge.consumed |= action != ShortcutAction::None;
                    {
                        std::lock_guard lock(state->mutex);
                        state->mouseShortcuts.CompleteLegacy(mouseIndex, edge.consumed);
                    }
                    if (action == ShortcutAction::ToggleMenu) ToggleMenu(state);
                }
                if (edge.consumed)
                    return message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK ? TRUE : 0;
            }
            if (message == WM_INPUT)
                return DispatchRaw(state, wparam, lparam, isTarget);
            if (ime::IsImeMessage(message))
            {
                // The 1.92.9 backend itself calls DefWindowProcW for composition.
                // Intercept before it, outside OverlayMutex: the default procedure can
                // synchronously generate WM_IME_CHAR and then WM_CHAR into this hook.
                if (message == WM_INPUTLANGCHANGE && isTarget)
                {
                    std::lock_guard lock(OverlayMutex());
                    if (ImGui::GetCurrentContext())
                        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
                }
                if ((isTarget && Active(window)) || ime::OwnsMessages(window))
                    return ime::ProcessMessage(window, message, wparam, lparam);
                // Inactive game IME messages must also skip the backend's default
                // processing, so the original window procedure receives them once.
                return CallWindowProcW(state->original, window, message, wparam, lparam);
            }
            if ((message == WM_CHAR || message == WM_UNICHAR) && !Active(window) && ime::OwnsMessages(window))
                return 0;
            if (isTarget)
            {
                {
                    std::lock_guard lock(OverlayMutex());
                    if (ImGui::GetCurrentContext())
                        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
                }
                if (Active(window) && (IsInputMessage(message) || message == WM_SETCURSOR))
                    return message == WM_SETCURSOR ? TRUE : 0;
            }
            TrackLegacy(state, message, wparam, lparam);
            if (message == WM_NCDESTROY)
            {
                if (isTarget)
                {
                    capture = false;
                    HWND expected = window;
                    targetWindow.compare_exchange_strong(expected, nullptr);
                }
                RestoreClip(state);
                ime::Detach(window);
                const auto result = CallWindowProcW(state->original, window, message, wparam, lparam);
                std::lock_guard lock(windowsMutex);
                windows.erase(window);
                return result;
            }
            return CallWindowProcW(state->original, window, message, wparam, lparam);
        }
        template <typename T>
        bool Hook(const char *name, void *replacement, T *original)
        {
            Log(std::format("Input hook {}: resolving target", name));
            const auto address = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), name));
            Log(std::format("Input hook {}: MH_CreateHook begin", name));
            auto status = MH_CreateHook(address, replacement, reinterpret_cast<void **>(original));
            Log(std::format("Input hook {}: MH_CreateHook returned {}", name, MH_StatusToString(status)));
            if (status == MH_OK)
            {
                Log(std::format("Input hook {}: MH_QueueEnableHook begin", name));
                status = MH_QueueEnableHook(address);
                Log(std::format("Input hook {}: MH_QueueEnableHook returned {}", name, MH_StatusToString(status)));
            }
            if (status != MH_OK)
                Log(std::format("Input hook {} failed: {}", name, MH_StatusToString(status)));
            return status == MH_OK;
        }
    }
    bool InstallHooks()
    {
        const bool clip = Hook("ClipCursor", reinterpret_cast<void *>(&ClipHook), &originalClip);
        const bool position = Hook("SetCursorPos", reinterpret_cast<void *>(&PositionHook), &originalPosition);
        const bool raw = Hook("GetRawInputData", reinterpret_cast<void *>(&RawDataHook), &originalRawData);
        Log("Input hooks: MH_ApplyQueued begin");
        const auto status = MH_ApplyQueued();
        Log(std::format("Input hooks: MH_ApplyQueued returned {}", MH_StatusToString(status)));
        rawFilterReady = raw && status == MH_OK;
        return clip && position && raw && status == MH_OK;
    }
    bool Attach(HWND window)
    {
        if (targetWindow == window)
            return true;
        Shutdown();
        auto state = FindState(window);
        if (!state)
        {
            state = std::make_shared<WindowState>();
            state->window = window;
            state->original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
            if (!state->original || state->original == &WindowProc)
                return false;
            {
                std::lock_guard lock(windowsMutex);
                windows.emplace(window, state);
            }
            SetLastError(0);
            const auto previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc));
            if (!previous && GetLastError())
            {
                std::lock_guard lock(windowsMutex);
                windows.erase(window);
                return false;
            }
        }
        {
            std::lock_guard lock(state->mutex);
            state->rawShortcutDown.fill(false);
            state->legacyShortcutDown.fill(false);
            state->shortcutConsumed.fill(false);
            state->rawShortcutConsumed.fill(false);
            state->shortcutCharacters.fill(false);
            state->mouseShortcuts.Reset();
        }
        ReadRawRegistration(state);
        ime::Attach(window);
        targetWindow = window;
        Log("Game window input attached; the configured menu hotkey toggles the menu");
        return true;
    }
    void SetMenuVisible(bool visible)
    {
        const auto window = targetWindow.load();
        const auto state = FindState(window);
        if (state && visible && !capture.load())
        {
            std::lock_guard lock(state->mutex);
            state->clipSuppressed = false;
        }
        capture = visible && state != nullptr && rawFilterReady.load();
        if (state)
        {
            if (!visible)
                ime::Clear(window);
            if (!visible)
                RestoreClip(state);
            PostMessageW(window, CursorSync, 0, 0);
        }
    }
    void Shutdown()
    {
        hotkeys::GetRegistry().OnFocusLost();
        capture = false;
        const auto window = targetWindow.exchange(nullptr);
        if (const auto state = FindState(window))
        {
            ime::Clear(window);
            {
                std::lock_guard lock(state->mutex);
                state->pendingRelease = true;
                state->rawShortcutDown.fill(false);
                state->legacyShortcutDown.fill(false);
                state->shortcutConsumed.fill(false);
                state->rawShortcutConsumed.fill(false);
                state->shortcutCharacters.fill(false);
                state->mouseShortcuts.Reset();
            }
            // ClipCursor can be restored from the renderer thread. Synthetic key ups
            // and ReleaseCapture are deferred to the owning window thread.
            RestoreClip(state);
            if (IsWindow(window))
                PostMessageW(window, CursorSync, 0, 0);
        }
        // Keep the forwarding subclass until WM_NCDESTROY. Another plugin can be
        // above us in the chain; retaining the stable original avoids reattach cycles.
    }
}
