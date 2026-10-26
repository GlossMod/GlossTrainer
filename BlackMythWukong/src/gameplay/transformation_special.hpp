#pragma once
#include "engine_bridge.hpp"
#include <cstdint>
#include <string_view>

namespace gloss::gameplay::transformation::special
{
    using Operation = bool (*)(void *);
    struct Host
    {
        // Called only inside managedOperation. Empty means this component is
        // unrelated to the native adapter's current, owned proxy session.
        engine::WeakObject (*componentOwner)(std::uintptr_t managedComponent){};
        // Must keep one balanced Mono GC-unsafe scope around the complete
        // operation, including owner validation and all managed reads/calls.
        bool (*managedOperation)(Operation operation, void *argument){};
    };
    enum class Fault
    {
        None, Unavailable, WrongThread, NoSession, InvalidIdentity, NotObserved,
        DuplicateStartup, ObservationLimit, DataUnavailable, InstancesUnreadable,
        InstancesAlreadyStarted, PauseFailed, ManagedOperationFailed
    };
    struct Status
    {
        bool observed{}, healthy{};
        std::uint32_t startupCount{}, tickCount{}, componentCount{};
        Fault fault{Fault::NotObserved};
    };
    // Optional capability: failure must not disable unrelated ordinary forms.
    // Main assembly identity and every method/layout fingerprint are checked
    // before the first hook is installed. Partial hook installation rolls back.
    void Initialize(Host host);
    bool Ready();
    // Game-thread only, after native::SetProxy and BEFORE FinishSpawningActor.
    // Re-registering an active session is rejected; it cannot erase evidence
    // that the root graph may already have started.
    bool SetSession(engine::WeakObject root);
    // Keep the claim until owned actors have completed synchronous destruction.
    void ClearSession();
    // A required special adapter may activate only after observed && healthy.
    // Check again while active; failures remain latched until ClearSession.
    Status Check(engine::WeakObject root);
    std::string_view Reason(Fault fault);
}
