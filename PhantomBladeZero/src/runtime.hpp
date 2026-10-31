#pragma once
#include <Windows.h>
#include <filesystem>
#include <mutex>
#include <string_view>

namespace gloss
{
    inline constexpr char Version[] = "1.1.1";
    void SetModule(HMODULE module);
    HMODULE Module();
    const std::filesystem::path &DataDirectory();
    void Log(std::string_view message);
    std::recursive_mutex &OverlayMutex();

    namespace overlay
    {
        bool Initialize(HWND window);
        void DrawFrame(const char *renderer);
        bool Visible();
        void SetVisible(bool visible);
        void SetRendererReady(bool ready);
        void OnFocusLost();
    }
    namespace input
    {
        bool InstallHooks();
        bool Attach(HWND window);
        void SetMenuVisible(bool visible);
        void Shutdown();
    }
    namespace graphics
    {
        bool InstallHooks();
    }
}
