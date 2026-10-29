#pragma once

namespace gloss::gameplay::native {
    // Installs only for the exact supported on-disk executable. Gameplay work
    // is deferred to the local controller's ReceiveTick on the verified thread.
    bool Initialize();
    // Restores our shared vtable slot. The module stays pinned until process exit.
    bool Shutdown();
}

extern "C" {
    __declspec(dllexport) const char* PBZ_DebugSubmit(const char* command);
    __declspec(dllexport) const char* PBZ_DebugStatus();
    __declspec(dllexport) const char* PBZ_DebugItems(int offset);
    __declspec(dllexport) int PBZ_InitializeGameplayOnly();
    __declspec(dllexport) int PBZ_ShutdownGameplayOnly();
    __declspec(dllexport) extern char PBZ_DebugBuffer[65536];
}
