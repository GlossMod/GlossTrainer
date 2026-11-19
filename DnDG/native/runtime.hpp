#pragma once

#include <Windows.h>

namespace gloss::dndg {

inline constexpr char Version[] = "1.0.0";
void ConfigureNativeImGui();
// An explicit path lets isolated validation use its own preferences; production
// falls back to the per-user DnDG file.
void ConfigureNativeImGui(const wchar_t* configPath);
void DrawNativeOverlay(HWND hwnd);
bool NativeMenuOpen() noexcept;
// Called by the renderer on WM_KEYDOWN. Return true to consume the event.
bool HandleNativeKey(unsigned int vk, bool repeated) noexcept;
void Log(const char* message) noexcept;

} // namespace gloss::dndg
