#include "runtime.hpp"
#include "renderer_dx12.hpp"

#include <MinHook.h>
#include <atomic>
#include <cwchar>

namespace gloss::dndg {
void Log(const char* message) noexcept {
    OutputDebugStringA("[GlossTrainer DnDG] ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}
} // namespace gloss::dndg

extern "C" __declspec(dllexport) void InitializeASI() {
    static std::atomic_flag initialized;
    if (initialized.test_and_set())
        return;

    wchar_t host[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, host, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return;
    const wchar_t* slash = wcsrchr(host, L'\\');
    const wchar_t* name = slash ? slash + 1 : host;
    if (_wcsicmp(name, L"DnDG_64.exe") != 0)
        return;

    // The renderer installs process-lifetime detours and the update worker
    // can outlive a frame. Refuse initialization if the ASI cannot be pinned.
    HMODULE self{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&gloss::dndg::Log), &self))
        return;

    const auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        gloss::dndg::Log(MH_StatusToString(status));
        return;
    }
    try {
        if (gloss::dndg::InstallDx12Hooks())
            gloss::dndg::Log("DX12 observation hooks installed; waiting for a compatible swap chain.");
        else
            gloss::dndg::Log("Could not install DX12 hooks. Overlay is disabled.");
    } catch (...) {
        gloss::dndg::Log("Exception during native initialization.");
    }
}
