#include "runtime.hpp"
#include "gameplay/state.hpp"
#include <MinHook.h>
#include <atomic>
#include <format>

extern "C" __declspec(dllexport) void InitializeASI()
{
    static std::atomic_flag initialized = ATOMIC_FLAG_INIT;
    if (initialized.test_and_set())
        return;
    try
    {
        gloss::Log(std::format("GlossTrainer {} initializing; process {}", gloss::Version, GetCurrentProcessId()));
        gloss::Log("Initializing MinHook");
        const auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        {
            gloss::Log(std::format("MinHook initialization failed: {}", MH_StatusToString(status)));
            return;
        }
        gloss::Log("Installing input hooks");
        if (!gloss::input::InstallHooks())
        {
            gloss::Log("Input hooks unavailable; overlay disabled and game input remains active");
            return;
        }
        gloss::Log("Installing graphics hooks");
        if (!gloss::graphics::InstallHooks())
        {
            gloss::Log("Graphics hooks unavailable; overlay disabled");
            return;
        }
        gloss::Log("Early hooks installed; waiting for the game's swapchain");
        gloss::gameplay::Initialize();
    }
    catch (const std::exception &error)
    {
        gloss::Log(std::format("Initialization failed: {}", error.what()));
    }
    catch (...)
    {
        gloss::Log("Initialization failed: unknown exception");
    }
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        gloss::SetModule(module);
    return TRUE;
}
