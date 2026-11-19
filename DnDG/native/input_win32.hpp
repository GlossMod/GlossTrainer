#pragma once

#include <windows.h>

namespace gloss::dndg {

// Install or schedule a window-thread subclass for the selected renderer HWND.
// An off-thread call does not wait for the game window to pump a message.
bool HookNativeInput(HWND hwnd) noexcept;

}  // namespace gloss::dndg
