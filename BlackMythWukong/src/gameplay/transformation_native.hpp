#pragma once
#include "engine_bridge.hpp"
#include "transformation.hpp"
#include <vector>

namespace gloss::gameplay::transformation::native
{
    namespace detail
    {
        // Native MonoStackData ABI. Each instance must stay in its caller's
        // stack scope until the balanced managed operation has finished.
        struct MonoStackData { void *stackPointer{}; const char *functionName{}; };
    }
    // All managed layout assumptions are gated by the exact assembly identity
    // in aot.cpp and the complete native method fingerprint/hook set in
    // Initialize, including cooperative global registration and dispatch.
    void Initialize();
    bool Ready();
    enum class AnimationDataState { Unknown, Missing, Present };
    struct AnimationStateSnapshot
    {
        bool available{}, current{};
        AnimationDataState setting{AnimationDataState::Unknown}, commonSetting{AnimationDataState::Unknown};
        AnimationDataState motionMatchingSetting{AnimationDataState::Unknown}, moveCommon{AnimationDataState::Unknown};
        int configuredMode{-1}, spareMode{-1}, finalMode{-1};
    };
    // Optional read-only observation; does not create setting caches or hold
    // managed references beyond one call. Failure never disables transformation.
    AnimationStateSnapshot InspectAnimationState(std::uintptr_t proxy, std::uintptr_t topAnim, std::uintptr_t moveAnim);
    struct MotionMatchingSelection
    {
        bool available{}, current{}, required{}, usesLocked{}, valid{};
        int state{-1};
    };
    // Pure resource-key query for the current role's existing MM settings.
    // speedType is EAIMoveSpeedType JOG=0/RUN=1/SPRINT=2. A non-MM role
    // returns current=true, required=false without requiring a linked MM node.
    // Missing nodes/caches remain unknown; no lazy getter or state setter runs.
    MotionMatchingSelection SelectMotionMatching(std::uintptr_t proxy, int speedType, bool locked);
    bool SetProxy(engine::WeakObject proxy);
    void ClearProxy();
    // Observed child Actors must have an explicit, reflected Actor.Owner chain
    // to this session. Return live identities, deepest discovery first. The
    // driver destroys dead children while active and all children before the
    // root on recovery; ownership alone does not destroy them with the root.
    std::vector<engine::WeakObject> OwnedChildren(bool deadOnly = false);
    // Stack scope around one immediate K2_DestroyActor call on the root or a
    // recorded child. It retains only
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
        detail::MonoStackData managedStack_;
        std::uintptr_t managedCookie_{};
        // Conservative native-stack root for the wrapper retained by the
        // synchronous claim. It is never stored beyond this object lifetime.
        volatile std::uintptr_t managedOwner_{};
        bool managedEntered_{};
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
    enum class WeaponReadiness { Pending, Ready, Failed };
    // Read-only preflight for every generated source slot. Pending must be
    // handled before acquiring any player/camera/input activation state.
    WeaponReadiness CheckWeaponsReady(std::uintptr_t proxy, std::uintptr_t sourceConfig);
    // Once, before gameplay input is accepted: restore each generated weapon's
    // local master and source-configured default activity. Never call this from
    // the active update loop; animation notifies own subsequent visibility.
    bool InitializeWeapons(std::uintptr_t proxy, std::uintptr_t sourceConfig);
    struct PlayerIsolation { std::uint64_t token{}; };
    // Property-manager requests owned by this exact lease object. Releasing
    // removes only our hidden/movement/capsule/mesh handles, never snapshots.
    bool AcquirePlayerIsolation(std::uintptr_t originalPlayer, PlayerIsolation &lease);
    bool RefreshPlayerIsolation(PlayerIsolation &lease);
    bool ReleasePlayerIsolation(PlayerIsolation &lease);
    // Adapter callbacks must keep every managed intermediate inside one call.
    // OwnedComponentOwner is valid only within that managed operation.
    bool RunManagedOperation(bool (*operation)(void *), void *state);
    engine::WeakObject OwnedComponentOwner(std::uintptr_t managedComponent);
}
