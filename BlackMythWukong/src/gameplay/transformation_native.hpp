#pragma once
#include "engine_bridge.hpp"
#include "transformation.hpp"
#include <vector>

namespace gloss::gameplay::transformation::native
{
    // All managed layout assumptions are gated by the exact assembly identity
    // in aot.cpp and the mapped native method fingerprints in Initialize.
    void Initialize();
    bool Ready();
    bool SetProxy(engine::WeakObject proxy);
    void ClearProxy();
    // Stack scope around one immediate K2_DestroyActor call. It retains only
    // synchronous isolation while PendingKill invalidates the normal weak
    // resolver. Never retain this scope across frames or asynchronous work.
    class ScopedCleanup
    {
    public:
        explicit ScopedCleanup(std::uintptr_t actor);
        ~ScopedCleanup();
        ScopedCleanup(const ScopedCleanup &) = delete;
        ScopedCleanup &operator=(const ScopedCleanup &) = delete;
        ScopedCleanup(ScopedCleanup &&) = delete;
        ScopedCleanup &operator=(ScopedCleanup &&) = delete;
        explicit operator bool() const noexcept { return active_; }
    private:
        bool active_{};
    };
    // Empty handle releases mapped input while preserving proxy isolation.
    bool SetInputOwner(engine::WeakObject originalPlayer);
    std::vector<Action> ConsumeActions();
    struct MoveInput { float forward{}, right{}; };
    // Values come from the game's mapped axis actions, expire when no events
    // arrive for 250 ms, and reset whenever the input/session owner changes.
    MoveInput MovementAxes();
    bool DeathRequested();
    bool SetTamerType(std::uintptr_t nativeActor, int type);
    bool Initialized(std::uintptr_t nativeActor);
    bool CopyTeam(std::uintptr_t proxy, std::uintptr_t originalPlayer);
    bool SetTarget(std::uintptr_t proxy, std::uintptr_t target);
}
