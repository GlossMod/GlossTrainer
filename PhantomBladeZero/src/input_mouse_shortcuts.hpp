#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <map>

namespace gloss::input
{
    inline bool ForwardOverlayMessage(bool visible, UINT message)
    {
        return visible || message == WM_SETFOCUS || message == WM_KILLFOCUS;
    }

    inline constexpr unsigned MouseKeys[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    inline constexpr USHORT MouseDown[] = {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_DOWN,
        RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN};
    inline constexpr USHORT MouseUp[] = {RI_MOUSE_LEFT_BUTTON_UP, RI_MOUSE_RIGHT_BUTTON_UP,
        RI_MOUSE_MIDDLE_BUTTON_UP, RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_UP};

    struct MouseShortcutEdge
    {
        bool dispatch{}, consumed{};
    };

    // Window-thread state, guarded by WindowState::mutex. Dispatch is deliberately
    // separate from edge tracking so user callbacks never run under that mutex.
    struct MouseShortcutState
    {
        struct Button { bool down{}, consumed{}; };
        std::array<Button, 5> legacy;
        std::map<std::uintptr_t, std::array<Button, 5>> devices;

        bool RawDown(std::size_t index) const
        {
            for (const auto &[device, buttons] : devices)
                if (buttons[index].down) return true;
            return false;
        }
        bool RawConsumed(std::size_t index) const
        {
            for (const auto &[device, buttons] : devices)
                if (buttons[index].down && buttons[index].consumed) return true;
            return false;
        }
        MouseShortcutEdge Legacy(std::size_t index, bool down, bool rawOnly, bool foreground)
        {
            auto &button = legacy[index];
            const bool first = down && !button.down;
            bool consumed = button.consumed;
            if (first && rawOnly && RawDown(index)) consumed |= RawConsumed(index);
            button = {down, down && consumed};
            return {first && foreground && !(rawOnly && RawDown(index)), consumed};
        }
        void CompleteLegacy(std::size_t index, bool consumed)
        {
            if (legacy[index].down) legacy[index].consumed |= consumed;
        }
        MouseShortcutEdge Raw(std::uintptr_t device, std::size_t index, bool down, bool rawOnly,
            bool foreground, bool matchesLegacy)
        {
            const bool alreadyDown = RawDown(index), alreadyConsumed = RawConsumed(index);
            auto &buttons = devices[device];
            auto &button = buttons[index];
            const bool first = down && !button.down;
            bool consumed = button.consumed;
            if (first)
            {
                consumed |= legacy[index].consumed;
                if (rawOnly && alreadyDown) consumed |= alreadyConsumed;
                if (!rawOnly && foreground) consumed |= matchesLegacy;
            }
            button = {down, down && consumed};
            if (!down)
            {
                bool held{};
                for (const auto &candidate : buttons) held |= candidate.down;
                if (!held) devices.erase(device);
            }
            return {first && rawOnly && foreground && !alreadyDown && !legacy[index].down, consumed};
        }
        void CompleteRaw(std::uintptr_t device, std::size_t index, bool consumed)
        {
            const auto found = devices.find(device);
            if (found != devices.end() && found->second[index].down)
                found->second[index].consumed |= consumed;
        }
        void Reset() { legacy = {}; devices.clear(); }
    };

    inline bool MouseButtonMessage(UINT message, WPARAM wparam, std::size_t &index, bool &down)
    {
        switch (message)
        {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP: index = 0; down = message != WM_LBUTTONUP; return true;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP: index = 1; down = message != WM_RBUTTONUP; return true;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP: index = 2; down = message != WM_MBUTTONUP; return true;
        case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
            if (HIWORD(wparam) != XBUTTON1 && HIWORD(wparam) != XBUTTON2) return false;
            index = HIWORD(wparam) == XBUTTON1 ? 3 : 4;
            down = message != WM_XBUTTONUP;
            return true;
        default: return false;
        }
    }

    inline void FilterMouseButtons(RAWMOUSE &mouse, USHORT suppressed)
    {
        mouse.usButtonFlags &= ~suppressed;
        for (std::size_t index = 0; index < 5; ++index)
            if (suppressed & (MouseDown[index] | MouseUp[index])) mouse.ulRawButtons &= ~(ULONG(1) << index);
    }
}
