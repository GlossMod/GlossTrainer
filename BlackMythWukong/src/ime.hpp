#pragma once
#include <Windows.h>

namespace gloss::input::ime {
inline constexpr UINT UpdateMessage = WM_APP + 0x352;

// Positions are relative to the HWND client area. Requests contain no pointers
// into ImGui and can safely be posted by its rendering thread.
struct TextInput {
    bool wantTextInput{}, wantVisible{};
    POINT position{};
    LONG lineHeight{};
};

// Attach is idempotent and never changes the window's input context.
void Attach(HWND window);
void Request(HWND window, const TextInput& input);
void Clear(HWND window);

// The remaining operations must run on the thread that owns window, without
// holding the overlay or input-state locks. Sync returns true when the raw
// keyboard registration needs to be read again by the input router.
bool Sync(HWND window, bool menuActive);
bool OwnsMessages(HWND window);
bool IsImeMessage(UINT message);
LRESULT ProcessMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
void Detach(HWND window);
}
