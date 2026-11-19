#pragma once

#include <windows.h>

namespace gloss::dndg {

// Call once early in startup, before the game requests its DXGI factory.
// Hooks and ImGui context are process-lifetime; live DLL unloading is unsupported.
// Success means discovery hooks were installed, not that a swap chain was seen.
bool InstallDx12Hooks();

// Provided by the native UI. The renderer owns and selects the ImGui context.
void ConfigureNativeImGui();
void DrawNativeOverlay(HWND hwnd);

// Optional input forwarder for the caller's own WndProc. Not a capture policy.
// Forward messages on the window thread; the caller must not access ImGui IO
// directly there. Returns 0 if the backend is unavailable or concurrently busy.
using NativeWin32MessageHandler = LRESULT (WINAPI*)(HWND, UINT, WPARAM, LPARAM);
NativeWin32MessageHandler GetNativeWin32MessageHandler() noexcept;

// Returns false only when the backend lock is busy, allowing the window
// thread to defer stateful input without guessing from the window result.
bool TryForwardNativeWin32Message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result) noexcept;

}  // namespace gloss::dndg
