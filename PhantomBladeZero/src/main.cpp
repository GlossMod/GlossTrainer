#include "runtime.hpp"
#include "gameplay/native_adapter.hpp"
#include "gameplay/backend.hpp"
#include <MinHook.h>
#include <atomic>
#include <format>

extern "C" __declspec(dllexport) void InitializeASI()
{
    static std::atomic_flag initialized = ATOMIC_FLAG_INIT;
    if (initialized.test_and_set())
        return;
    try {
        gloss::Log(std::format("GlossTrainer PBZ {} initializing; process {}", gloss::Version, GetCurrentProcessId()));
        const auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            gloss::Log(std::format("MinHook initialization failed: {}", MH_StatusToString(status)));
            return;
        }
        std::string settingsError;
        if (!gloss::gameplay::GetBackend().InitializeSettings(gloss::DataDirectory() / L"features.ini", &settingsError))
            gloss::Log("Feature settings: " + settingsError);
        gloss::gameplay::native::Initialize();
        if (!gloss::input::InstallHooks()) {
            gloss::Log("Input hooks unavailable; overlay disabled and game input remains active");
            return;
        }
        if (!gloss::graphics::InstallHooks()) {
            gloss::Log("Graphics hooks unavailable; overlay disabled");
            return;
        }
        gloss::Log("PBZ overlay initialized; gameplay readiness is reported by the native adapter");
    }
    catch (const std::exception &error) {
        gloss::Log(std::format("Initialization failed: {}", error.what()));
    }
    catch (...) {
        gloss::Log("Initialization failed: unknown exception");
    }
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        gloss::SetModule(module);
    return TRUE;
}
