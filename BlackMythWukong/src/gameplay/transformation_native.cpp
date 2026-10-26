#include "transformation_native.hpp"
#include "aot.hpp"
#include "midhook.hpp"
#include "../runtime.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <memory>
#include <mutex>
#include <limits>

namespace gloss::gameplay::transformation::native
{
    namespace
    {
        std::atomic_bool ready{}, deathRequested{};
        std::atomic_bool animationProbeReady{};
        std::atomic_bool motionMatchingSelectorReady{};
        struct MotionMatchingMethods
        {
            std::uintptr_t setting{}, configuredMode{}, validState{};
        } motionMatchingMethods;
        struct AnimationProbeMethods
        {
            std::uintptr_t setting{}, configuredMode{}, spareMode{}, finalMode{};
        } animationProbeMethods;
        std::mutex initializeMutex, stateMutex, cleanupMutex;
        engine::WeakObject proxyIdentity, inputIdentity;
        std::uint64_t sessionGeneration{};
        struct ChildClaim { engine::WeakObject identity; bool dead{}; };
        std::vector<ChildClaim> children;
        enum class WeaponPhase { Prepared, MasterAssigned, Complete, Indeterminate };
        struct WeaponClaim { engine::WeakObject identity; std::uint64_t generation{}; WeaponPhase phase{}; };
        std::vector<WeaponClaim> weaponClaims;
        struct WeaponObservation
        {
            engine::WeakObject identity;
            std::uint64_t generation{};
            std::size_t index{};
            WeaponReadiness state{};
            int initialized{-1}, master{-1}, active{-1};
            std::string_view reason;
        };
        std::vector<WeaponObservation> weaponObservations;
        constexpr std::array<int, 4> isolationProperties{302, 13, 202, 105};
        struct IsolationClaim
        {
            PlayerIsolation *owner{};
            std::uint64_t token{}, generation{};
            engine::WeakObject actor;
            std::array<std::uint32_t, 4> handles{};
            bool retired{}, uncertain{};
            bool shadowValid{}, shadowHidden{};
        } isolationClaim;
        std::uint64_t nextIsolationToken{};
        struct IsolationRefreshFailure
        {
            std::string_view reason;
            int property{}, matches{-1};
            bool operator==(const IsolationRefreshFailure &) const = default;
        };
        struct IsolationRefreshDiagnostics
        {
            std::uint64_t token{}, generation{};
            std::array<IsolationRefreshFailure, 32> seen{};
            std::size_t count{};
        } isolationRefreshDiagnostics;
        struct PropCapture
        {
            engine::WeakObject actor;
            int property{};
            std::uint32_t handle{}, before{};
            volatile std::uintptr_t component{}, data{};
            int count{};
            bool firstValid{}, invalid{};
        };
        thread_local PropCapture *propCapture{};
        enum class PropertyPhase { External, Apply, Release };
        thread_local PropertyPhase propertyPhase{PropertyPhase::External};
        struct PropertyScope
        {
            PropertyPhase previous;
            explicit PropertyScope(PropertyPhase phase) : previous(propertyPhase) { propertyPhase = phase; }
            ~PropertyScope() { propertyPhase = previous; }
        };
        struct CleanupClaim
        {
            const ScopedCleanup *scope{};
            engine::WeakObject identity, session;
            std::uint64_t generation{};
            std::uintptr_t actor{}, managedOwner{};
            DWORD thread{};
        } cleanupClaim;
        std::array<Action, 64> actions{};
        std::size_t actionCount{};
        MoveInput movement;
        std::array<ULONGLONG, 2> movementTime{};
        std::array<int, 16> rejectedSkillInputs{};
        std::size_t rejectedSkillCount{};
        std::uintptr_t returnStub{};
        struct Methods
        {
            std::uintptr_t findRef{}, setTamerType{}, initialized{}, inputOwner{}, getTeam{}, setTeam{};
            std::uintptr_t getEventCollection{}, clearTargetEvent{}, invokeVoid{}, makeTargetInfo{}, setTargetInfo{};
            std::uintptr_t weaponInitialized{}, weaponEvents{}, weaponMasterEvent{}, weaponActiveEvent{}, invokeActor{}, invokeBool{};
            std::uintptr_t boolPropertyEvent{}, enumPropertyEvent{}, resetPropertyEvent{}, invokeBoolProperty{}, invokeEnumProperty{}, invokeResetProperty{}, propHandle{}, calculateProperty{};
            std::uintptr_t hiddenPropertyReturn{};
            std::uintptr_t actionDesc{}, firstParam{}, getAxis{}, receiveBattle{}, receiveAction{};
        } methods;
        struct ManagedRuntime
        {
            std::uintptr_t currentThread{}, enterUnsafe{}, exitUnsafe{};
        } managedRuntime;
        // The same layout is constructed by this build's mono_runtime_invoke
        // before calling the balanced internal GC transition pair. The stack
        // marker belongs to the complete native operation, not one AOT call.
        using detail::MonoStackData;
        static_assert(sizeof(MonoStackData) == 16 && offsetof(MonoStackData, functionName) == 8);
        constexpr std::array<const char *, 3> managedRuntimeFingerprints{
            "83 3D 41 E3 63 0E 00 74 31 65 48 8B 0C 25 30 00 00 00 8B 05 18 E3 63 0E 83 F8 40 73 09 48 8B 84 C1 80 14 00 00 C3 48 8B 91 80 17 00 00 48 85 D2 74 08 83 C0 C0 48 8B 04 C2 C3 33 C0 C3",
            "40 53 48 83 EC 20 48 8B D9 E8 12 CE 03 00 0F BE 15 A4 37 12 0F 83 FA 01 74 2A 83 C2 FE 83 FA 01 76 12 BA 30 00 00 00 48 8D 0D 22 73 F6 02 E8 BD 37 FD FF CC 48 8B D3 48 8B C8 48 83 C4 20 5B E9 5C 00 00 00 33 C0 48 83 C4 20 5B C3",
            "40 53 48 83 EC 20 0F BE 05 6C 34 12 0F 48 8B DA 83 F8 01 74 2F 44 8D 40 FE 41 83 F8 01 77 2B 83 F8 01 74 20 41 83 F8 01 77 20 48 85 C9 74 15 E8 AC CA 03 00 48 8B C8 48 8B D3 48 83 C4 20 5B E9 2C FA FF FF 48 83 C4 20 5B C3 BA 30 00 00 00 48 8D 0D BA 6F F6 02 E8 55 34 FD FF CC"
        };
        auto &Hooks()
        {
            static auto *hooks = new std::vector<std::unique_ptr<MidHook>>;
            return *hooks;
        }
        template <class F, class R, class... A> bool Safe(F fn, R &result, A... args) noexcept
        {
            __try { result = fn(args...); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        template <class F, class... A> bool SafeVoid(F fn, A... args) noexcept
        {
            __try { fn(args...); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool Pointer(std::uintptr_t value) { return value >= 0x10000 && value <= 0x00007fffffffffff && !(value & 7); }
        bool EnterManagedRegion(MonoStackData &stack, std::uintptr_t &cookie) noexcept
        {
            if (!Ready() || !engine::IsGameThread() || !managedRuntime.currentThread || !managedRuntime.enterUnsafe || !managedRuntime.exitUnsafe)
                return false;
            std::uintptr_t info{};
            // Never attach a foreign thread or change Mono TLS ourselves.
            return Safe(reinterpret_cast<std::uintptr_t (*)()>(managedRuntime.currentThread), info) && Pointer(info) &&
                   Safe(reinterpret_cast<std::uintptr_t (*)(MonoStackData *)>(managedRuntime.enterUnsafe), cookie, &stack);
        }
        bool LeaveManagedRegion(std::uintptr_t cookie, MonoStackData &stack) noexcept
        {
            // A zero cookie is a successful nested entry from a thread
            // already in managed mode. Pair it too; Mono preserves that
            // previous state rather than forcing the thread GC-safe.
            const bool result = SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, MonoStackData *)>(managedRuntime.exitUnsafe), cookie, &stack);
            if (!result)
                ready.store(false, std::memory_order_release);
            return result;
        }
        class ManagedRegion
        {
        public:
            ManagedRegion() noexcept : stack_{&stack_, "GlossTrainer transformation native operation"}
            {
                entered_ = EnterManagedRegion(stack_, cookie_);
            }
            ~ManagedRegion() { if (entered_) Leave(); }
            ManagedRegion(const ManagedRegion &) = delete;
            ManagedRegion &operator=(const ManagedRegion &) = delete;
            explicit operator bool() const noexcept { return entered_; }
            bool Leave() noexcept
            {
                if (!entered_)
                    return false;
                entered_ = false;
                return LeaveManagedRegion(cookie_, stack_);
            }
        private:
            MonoStackData stack_;
            std::uintptr_t cookie_{};
            bool entered_{};
        };
        template <class F> bool ManagedOperation(F &&operation)
        {
            ManagedRegion region;
            if (!region)
                return false;
            const bool result = operation();
            return region.Leave() && result;
        }
        bool Matches(std::uintptr_t address, const char *bytes)
        {
            const auto pattern = Pattern::Parse(bytes);
            return address && !pattern.bytes.empty() && FindPattern(pattern, address, pattern.bytes.size()).size() == 1;
        }
        std::uintptr_t NativeAddress(std::uintptr_t managed)
        {
            // UObject.get_Address (Runtime RID 145F): mov rax,[rax+58].
            return Pointer(managed) && Read<std::uintptr_t>(managed) ? Read<std::uintptr_t>(managed + 0x58) : 0;
        }
        std::uintptr_t Managed(std::uintptr_t object)
        {
            if (!Ready() || !engine::IsGameThread() || !engine::IsA(object, L"/Script/CoreUObject.Object"))
                return 0;
            std::uintptr_t reference{};
            if (!Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.findRef), reference, object) || !Pointer(reference))
                return 0;
            // GCHelper.FindRef is nongeneric. Find<T> additionally requires a
            // Mono R10 generic context and must never be invoked as this ABI.
            // UObjectRef.Initialize (Runtime RID 169D) and FindInternal (168D)
            // independently prove Native +20 and Managed +10.
            const auto managed = Read<std::uintptr_t>(reference + 0x10);
            return Read<std::uintptr_t>(reference + 0x20) == object && NativeAddress(managed) == object ? managed : 0;
        }
        engine::WeakObject Identity(bool input)
        {
            std::unique_lock lock(stateMutex, std::try_to_lock);
            return lock ? (input ? inputIdentity : proxyIdentity) : engine::WeakObject{};
        }
        bool Owned(std::uintptr_t object)
        {
            return object && engine::ResolveWeak(Identity(false)) == object;
        }
        bool SameIdentity(engine::WeakObject a, engine::WeakObject b)
        {
            return a.index >= 0 && a.serial > 0 && a.index == b.index && a.serial == b.serial;
        }
        struct SessionIdentity { engine::WeakObject root; std::uint64_t generation{}; };
        SessionIdentity Session()
        {
            std::unique_lock lock(stateMutex, std::try_to_lock);
            return lock ? SessionIdentity{proxyIdentity, sessionGeneration} : SessionIdentity{};
        }
        enum class Relation { None, Root, Child };
        Relation CleanupRelation(std::uintptr_t actor, std::uintptr_t managedOwner = 0)
        {
            CleanupClaim claim;
            {
                std::unique_lock lock(cleanupMutex, std::try_to_lock);
                if (!lock)
                    return Relation::None;
                claim = cleanupClaim;
            }
            const auto session = Session();
            if (!claim.scope || claim.thread != GetCurrentThreadId() || claim.generation != session.generation ||
                !SameIdentity(session.root, claim.session) || !engine::WeakLifetimeCurrent(claim.identity) ||
                (managedOwner ? managedOwner != claim.managedOwner : actor != claim.actor))
                return Relation::None;
            const auto address = NativeAddress(claim.managedOwner);
            if (address && address != claim.actor)
                return Relation::None;
            const auto current = engine::MakeWeak(claim.actor);
            if (current.index >= 0 && current.serial > 0 && !SameIdentity(current, claim.identity))
                return Relation::None;
            return SameIdentity(claim.identity, claim.session) ? Relation::Root : Relation::Child;
        }
        bool RecordedChild(engine::WeakObject identity)
        {
            std::unique_lock lock(stateMutex, std::try_to_lock);
            return lock && std::any_of(children.begin(), children.end(), [&](const auto &child) { return SameIdentity(child.identity, identity); });
        }
        Relation ActorRelation(std::uintptr_t actor)
        {
            if (!Ready() || !engine::IsGameThread() || !actor)
                return Relation::None;
            const auto session = Session();
            if (session.root.index < 0 || session.root.serial <= 0)
                return Relation::None;
            const auto root = engine::ResolveWeak(session.root);
            std::array<engine::WeakObject, 16> path{};
            std::size_t count{};
            auto current = actor;
            Relation relation = Relation::None;
            for (;;)
            {
                if (current == root && root)
                {
                    relation = count ? Relation::Child : Relation::Root;
                    break;
                }
                const auto cleanup = CleanupRelation(current);
                if (cleanup != Relation::None)
                {
                    relation = count || cleanup == Relation::Child ? Relation::Child : Relation::Root;
                    break;
                }
                const auto identity = engine::MakeWeak(current);
                if (!engine::IsActor(current) || engine::ResolveWeak(identity) != current)
                    return Relation::None;
                if (RecordedChild(identity))
                {
                    relation = Relation::Child;
                    break;
                }
                if (count == path.size() || std::any_of(path.begin(), path.begin() + count,
                    [&](const auto &seen) { return SameIdentity(seen, identity); }))
                    return Relation::None;
                path[count++] = identity;
                // Native reflection validates the Actor class, object property
                // type and bounds. No per-form managed/native offsets, attach
                // hierarchy, Instigator or arbitrary scene Tamer are trusted.
                auto owner = engine::Arg::Object("Owner", 0);
                if (!engine::Property(current, owner) || !(current = owner.Get<std::uintptr_t>()))
                    return Relation::None;
            }
            if (count)
            {
                std::unique_lock lock(stateMutex, std::try_to_lock);
                if (!lock || sessionGeneration != session.generation || !SameIdentity(proxyIdentity, session.root))
                    return Relation::None;
                // Ancestors are recorded before descendants; recovery consumes
                // the list in reverse. A detached child remains this session's
                // temporary object, but a reused serial never inherits it.
                while (count)
                {
                    const auto identity = path[--count];
                    if (std::none_of(children.begin(), children.end(), [&](const auto &child) { return SameIdentity(child.identity, identity); }))
                        children.push_back({identity});
                }
            }
            return relation;
        }
        Relation ComponentRelation(std::uintptr_t component)
        {
            if (!Ready() || !engine::IsGameThread() || !Pointer(component))
                return Relation::None;
            // UActorCompBaseCS.GetOwner (Main RID DA21) proves Owner +10.
            const auto owner = Read<std::uintptr_t>(component + 0x10);
            // Only the exact synchronous claim accepts Address=0 after detach.
            if (const auto cleanup = CleanupRelation(0, owner); cleanup != Relation::None)
                return cleanup;
            return ActorRelation(NativeAddress(owner));
        }
        bool OwnedComponent(std::uintptr_t component)
        {
            return ComponentRelation(component) != Relation::None;
        }
        void CaptureProperty(Context &context, void *)
        {
            auto *capture = propCapture;
            if (!capture || !Ready() || !engine::IsGameThread() || !Pointer(context.rcx) ||
                static_cast<int>(context.rdx) != capture->property || static_cast<std::uint32_t>(context.r9) != capture->handle)
                return;
            const auto actor = engine::ResolveWeak(capture->actor);
            const auto owner = Read<std::uintptr_t>(context.rcx + 0x10);
            std::uint32_t notify{};
            std::uint8_t deferred{};
            if (!actor || NativeAddress(owner) != actor ||
                !ReadMemory(context.rsp() + 0x28, &notify, sizeof(notify)) || notify ||
                !ReadMemory(context.rsp() + 0x30, &deferred, sizeof(deferred)) || deferred != 1)
                return;
            if (++capture->count != 1)
            {
                // The first subscriber may already have registered our request.
                // Keep its exact data/counter and suppress every extra dispatch
                // before mutation; failure must remain recoverable by its ID.
                capture->invalid = true;
                context.resume = returnStub;
                return;
            }
            capture->component = context.rcx;
            capture->data = Read<std::uintptr_t>(context.rcx + 0x98);
            if (!Pointer(capture->data) || !Read<std::uintptr_t>(capture->data) ||
                !Safe(reinterpret_cast<std::uint32_t (*)(std::uintptr_t)>(methods.propHandle), capture->before, static_cast<std::uintptr_t>(capture->data)) ||
                (!capture->handle && capture->before == (std::numeric_limits<std::uint32_t>::max)()))
            {
                capture->invalid = true;
                context.resume = returnStub; // Only our deferred request, before mutation.
            }
            else capture->firstValid = true;
        }
        void RetirePlayerIsolation(Context &context, void *)
        {
            if (!Ready() || !engine::IsGameThread() || !Pointer(context.rcx)) return;
            const auto actor = NativeAddress(Read<std::uintptr_t>(context.rcx + 0x10));
            std::unique_lock lock(stateMutex, std::try_to_lock);
            if (lock && isolationClaim.token && actor && engine::ResolveWeak(isolationClaim.actor) == actor)
                isolationClaim.retired = true;
            // Observation only: the game's original EndPlay must reset its data.
        }
        void ObservePlayerHidden(Context &context, void *)
        {
            if (!Ready() || !engine::IsGameThread()) return;
            const auto actor = NativeAddress(context.rcx);
            std::uintptr_t caller{};
            if (!actor || !ReadMemory(context.rsp(), &caller, sizeof(caller))) return;
            std::unique_lock lock(stateMutex, std::try_to_lock);
            if (!lock || !isolationClaim.token || isolationClaim.retired || isolationClaim.generation != sessionGeneration || engine::ResolveWeak(isolationClaim.actor) != actor) return;
            if (caller == methods.hiddenPropertyReturn)
            {
                if (propertyPhase == PropertyPhase::External) isolationClaim.shadowValid = false;
                else if (propertyPhase == PropertyPhase::Release && isolationClaim.shadowValid)
                    context.rdx = (context.rdx & ~std::uint64_t{0xff}) | static_cast<std::uint8_t>(isolationClaim.shadowHidden);
            }
            else
            {
                // Same-value writes count too. Movie nodes can bypass the
                // property manager; preserve their last actual hidden value.
                isolationClaim.shadowValid = true;
                isolationClaim.shadowHidden = static_cast<std::uint8_t>(context.rdx) != 0;
            }
        }
        bool IsolationSnapshot(PlayerIsolation &lease, IsolationClaim &claim)
        {
            std::lock_guard lock(stateMutex);
            if (!lease.token || isolationClaim.owner != &lease || isolationClaim.token != lease.token) return false;
            claim = isolationClaim;
            return true;
        }
        bool IsolationCurrent(const IsolationClaim &claim)
        {
            std::lock_guard lock(stateMutex);
            return claim.token && isolationClaim.owner == claim.owner && isolationClaim.token == claim.token &&
                !isolationClaim.retired && isolationClaim.generation == claim.generation && sessionGeneration == claim.generation &&
                SameIdentity(isolationClaim.actor, claim.actor) && engine::ResolveWeak(claim.actor) != 0;
        }
        bool IsolationEvents(std::uintptr_t actor, std::array<std::uintptr_t, 3> &callbacks)
        {
            const auto managed = Managed(actor);
            std::uintptr_t events{};
            if (!managed || !Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.getEventCollection), events, managed) ||
                !Pointer(events) || !engine::IsA(NativeAddress(events), L"/Script/b1-Managed.BUS_GSEventCollection")) return false;
            const std::array getters{methods.boolPropertyEvent, methods.enumPropertyEvent, methods.resetPropertyEvent};
            for (std::size_t i = 0; i < callbacks.size(); ++i)
                if (!Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(getters[i]), callbacks[i], events) ||
                    !Pointer(callbacks[i]) || !Read<std::uintptr_t>(callbacks[i])) return false;
            return true;
        }
        bool DeferredProperty(const IsolationClaim &claim, const std::array<std::uintptr_t, 3> &callbacks, std::size_t index,
                              std::uint32_t handle, PropCapture &capture, bool recovery = false)
        {
            if (propCapture || !IsolationCurrent(claim)) return false; // A reentrant acquisition must not steal the outer capture.
            capture.actor = claim.actor; capture.property = isolationProperties[index]; capture.handle = handle;
            struct Scope
            {
                explicit Scope(PropCapture &capture) { propCapture = &capture; }
                ~Scope() { propCapture = nullptr; }
            } scope(capture);
            // default=true defers native handlers; obtain/record our exact
            // generated handle before applying any Actor/Movement callback.
            const bool invoked = index == 0 ?
                SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, int, std::uint8_t, std::uint32_t, std::uint32_t, std::uint8_t)>(methods.invokeBoolProperty),
                         callbacks[0], isolationProperties[index], std::uint8_t{1}, handle, std::uint32_t{}, std::uint8_t{1}) :
                SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, int, int, std::uint32_t, std::uint32_t, std::uint8_t, std::uintptr_t)>(methods.invokeEnumProperty),
                         callbacks[1], isolationProperties[index], 0, handle, std::uint32_t{}, std::uint8_t{1}, std::uintptr_t{});
            // A duplicate means activation/refresh failed, but the first call
            // can already own before+1. Record that ID before returning failure.
            if (!capture.firstValid || !IsolationCurrent(claim)) return false;
            std::uint32_t after{};
            if (!Safe(reinterpret_cast<std::uint32_t (*)(std::uintptr_t)>(methods.propHandle), after, static_cast<std::uintptr_t>(capture.data)))
            {
                std::lock_guard lock(stateMutex); isolationClaim.uncertain = true; return false;
            }
            const auto expected = handle ? capture.before : capture.before + 1;
            if (!handle && after == expected)
            {
                std::lock_guard lock(stateMutex);
                if (isolationClaim.token != claim.token || isolationClaim.retired) return false;
                isolationClaim.handles[index] = expected;
            }
            else if (after != expected && after != capture.before)
            {
                std::lock_guard lock(stateMutex); isolationClaim.uncertain = true; return false;
            }
            // Recovery may locate the already-owned handle despite suppressed
            // duplicate subscribers. It neither allocates nor promotes a failed
            // activation to success; subsequent Reset still uses our exact ID.
            return invoked && after == expected && ((!capture.invalid && capture.count == 1) ||
                (recovery && handle && capture.count > 1));
        }
        bool IsolationState(std::uintptr_t actor, std::array<bool, 4> &matches)
        {
            auto hidden = engine::Arg::Boolean("bHidden", false);
            auto movement = engine::Arg::Object("CharacterMovement", 0);
            auto mode = engine::Arg::Byte("MovementMode", 0);
            if (!engine::Property(actor, hidden) || !engine::Property(actor, movement) ||
                !movement.Get<std::uintptr_t>() || !engine::Property(movement.Get<std::uintptr_t>(), mode)) return false;
            matches[0] = hidden.Get<std::int32_t>() != 0;
            matches[1] = mode.Get<std::uint8_t>() == 0;
            for (std::size_t i = 2; i < matches.size(); ++i)
            {
                auto component = engine::Arg::Object(i == 2 ? "CapsuleComponent" : "Mesh", 0);
                auto collision = std::array{engine::Arg::Byte("ReturnValue", 0, true)};
                if (!engine::Property(actor, component) || !component.Get<std::uintptr_t>() ||
                    !engine::Call(L"/Script/Engine.PrimitiveComponent:GetCollisionEnabled", collision, component.Get<std::uintptr_t>()).success) return false;
                matches[i] = collision[0].Get<std::uint8_t>() == 0;
            }
            return true;
        }
        void Suppress(Context &context, void *)
        {
            if (OwnedComponent(context.rcx))
                context.resume = returnStub;
        }
        void OnDeath(Context &context, void *)
        {
            const auto relation = ComponentRelation(context.rcx);
            if (relation != Relation::None)
            {
                if (relation == Relation::Root)
                    deathRequested.store(true, std::memory_order_release);
                else
                {
                    const auto owner = Read<std::uintptr_t>(context.rcx + 0x10);
                    const auto identity = engine::MakeWeak(NativeAddress(owner));
                    std::unique_lock lock(stateMutex, std::try_to_lock);
                    if (lock)
                        for (auto &child : children)
                            if (SameIdentity(child.identity, identity))
                                child.dead = true;
                }
                context.resume = returnStub;
            }
        }
        std::uintptr_t InputOwner(const Context &context)
        {
            if (!Ready() || !engine::IsGameThread())
                return 0;
            const auto owner = engine::ResolveWeak(Identity(true));
            if (!owner || !engine::ResolveWeak(Identity(false)))
                return 0;
            std::uintptr_t managedOwner{};
            if (!Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.inputOwner), managedOwner, context.rcx) || NativeAddress(managedOwner) != owner)
                return 0;
            return managedOwner;
        }
        bool BattleAllowed(std::uintptr_t owner, int type)
        {
            int general{}, action{};
            return Safe(reinterpret_cast<int (*)(std::uintptr_t, bool)>(methods.receiveBattle), general, owner, false) && general == 1 &&
                   Safe(reinterpret_cast<int (*)(std::uintptr_t, int)>(methods.receiveAction), action, owner, type) && action == 1;
        }
        bool InputParam(int id, int requiredCount, int &result)
        {
            std::uintptr_t descriptor{};
            if (id <= 0 || !Safe(reinterpret_cast<std::uintptr_t (*)(int)>(methods.actionDesc), descriptor, id) || !Pointer(descriptor))
                return false;
            // The same two inlined getters are fingerprinted independently in
            // BUIAMove and BUIAUseSkillByType: descriptor.Params +20 and Count +18.
            const auto parameters = Read<std::uintptr_t>(descriptor + 0x20);
            const int count = Pointer(parameters) ? Read<int>(parameters + 0x18) : 0;
            return count > 0 && count <= 32 && (!requiredCount || count == requiredCount) &&
                   Safe(reinterpret_cast<int (*)(std::uintptr_t, int)>(methods.firstParam), result, parameters, 0);
        }
        void QueueInput(Context &context, Action action, bool triggered, int type, std::uintptr_t owner)
        {
            // Fifth argument is the prediction key. The mapped methods reject
            // non-null prediction calls, so do not queue them a second time.
            const auto prediction = Read<std::uintptr_t>(context.rsp() + 0x28);
            const auto event = static_cast<std::uint8_t>(context.r8);
            const bool allowed = !prediction && BattleAllowed(owner, type);
            if (allowed && (event == 2 || (triggered && event == 0)))
            {
                std::unique_lock lock(stateMutex, std::try_to_lock);
                if (lock && actionCount < actions.size())
                    actions[actionCount++] = action;
            }
            context.rax = allowed ? 1 : 0;
            context.resume = returnStub;
        }
        void Input(Context &context, void *user)
        {
            ManagedRegion region;
            if (!region)
                return;
            const auto owner = InputOwner(context);
            if (!owner)
                return;
            const auto action = static_cast<Action>(reinterpret_cast<std::uintptr_t>(user));
            const int type = action == Action::LightAttack ? 1 : action == Action::HeavyAttack ? 2 : action == Action::Dodge ? 18 : 30;
            QueueInput(context, action, action == Action::Special, type, owner);
        }
        void SkillInput(Context &context, void *)
        {
            ManagedRegion region;
            if (!region)
                return;
            const auto owner = InputOwner(context);
            if (!owner)
                return;
            int spell{};
            const bool resolved = InputParam(static_cast<int>(context.rdx), 0, spell);
            if (!resolved || spell < 1 || spell > 4)
            {
                // The default-spell/ride paths can replace and destroy the
                // retained player pawn. Unknown actions in this same casting
                // handler cannot safely fall through during a proxy session.
                context.rax = 0;
                context.resume = returnStub;
                bool report{};
                const int id = static_cast<int>(context.rdx);
                {
                    std::unique_lock lock(stateMutex, std::try_to_lock);
                    if (lock && rejectedSkillCount < rejectedSkillInputs.size() &&
                        std::find(rejectedSkillInputs.begin(), rejectedSkillInputs.begin() + rejectedSkillCount, id) == rejectedSkillInputs.begin() + rejectedSkillCount)
                    {
                        rejectedSkillInputs[rejectedSkillCount++] = id;
                        report = true;
                    }
                }
                if (report)
                    Log(std::format("Transformation input rejected: action ID {}, spell type {} ({}); retained player must not enter native transformation", id, spell, resolved ? "unsupported" : "descriptor unavailable"));
                return;
            }
            // GSE.ProtobufDB SpellType: ShenFa1, HaoMao2, QiShu3, BianShen4.
            QueueInput(context, static_cast<Action>(static_cast<int>(Action::Skill1) + spell - 1), true, 29, owner);
        }
        void Move(Context &context, void *)
        {
            ManagedRegion region;
            if (!region)
                return;
            const auto owner = InputOwner(context);
            if (!owner)
                return;
            int axis{};
            if (!InputParam(static_cast<int>(context.rdx), 2, axis) || axis < 0 || axis > 1)
            {
                // This handler belongs to the retained player even when its
                // descriptor is unavailable. Falling through would move that
                // hidden pawn while the proxy session owns movement.
                std::unique_lock lock(stateMutex, std::try_to_lock);
                if (lock)
                {
                    movement = {};
                    movementTime = {};
                }
                context.rax = 0;
                context.resume = returnStub;
                return;
            }
            float value{};
            const auto prediction = Read<std::uintptr_t>(context.rsp() + 0x28);
            const auto event = static_cast<std::uint8_t>(context.r8);
            const auto managedValue = Read<std::uintptr_t>(context.r9);
            const bool allowed = !prediction && BattleAllowed(owner, 14);
            if (allowed && event != 4 && event != 5 && Pointer(managedValue))
            {
                if (!Safe(reinterpret_cast<float (*)(std::uintptr_t)>(methods.getAxis), value, managedValue) || !std::isfinite(value))
                    value = 0;
            }
            value = (std::clamp)(value, -1.f, 1.f);
            {
                std::unique_lock lock(stateMutex, std::try_to_lock);
                if (lock)
                {
                    (axis ? movement.right : movement.forward) = value;
                    movementTime[axis] = GetTickCount64();
                }
            }
            context.rax = allowed ? 1 : 0;
            context.resume = returnStub;
        }
        bool SameImageCode(std::uintptr_t address, std::uintptr_t owner)
        {
            MEMORY_BASIC_INFORMATION target{}, source{};
            return VirtualQuery(reinterpret_cast<void *>(address), &target, sizeof(target)) &&
                   VirtualQuery(reinterpret_cast<void *>(owner), &source, sizeof(source)) && target.Type == MEM_IMAGE &&
                   target.AllocationBase == source.AllocationBase && target.State == MEM_COMMIT &&
                   !(target.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (target.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        }
        bool VerifyManagedRuntime()
        {
            managedRuntime = {};
            const auto module = GetModuleHandleW(nullptr);
            std::array<std::uintptr_t, 3> addresses{};
            for (std::size_t i = 0; i != addresses.size(); ++i)
            {
                const auto found = FindExecutablePattern(module, managedRuntimeFingerprints[i]);
                if (found.size() != 1 || !SameImageCode(found[0], reinterpret_cast<std::uintptr_t>(module)))
                {
                    Log(std::format("Transformation Mono GC transition fingerprint rejected: {}", i));
                    return false;
                }
                addresses[i] = found[0];
            }
            managedRuntime = {addresses[0], addresses[1], addresses[2]};
            return true;
        }
        std::uintptr_t CallTarget(std::uintptr_t call)
        {
            return Read<std::uint8_t>(call) == 0xe8 ? call + 5 + Read<std::int32_t>(call + 1) : 0;
        }
        bool MakeReturnStub()
        {
            if (returnStub)
                return true;
            auto *memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!memory)
                return false;
            *static_cast<std::uint8_t *>(memory) = 0xc3;
            DWORD previous{};
            if (!VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &previous) || !FlushInstructionCache(GetCurrentProcess(), memory, 1))
            {
                VirtualFree(memory, 0, MEM_RELEASE);
                return false;
            }
            returnStub = reinterpret_cast<std::uintptr_t>(memory);
            return true;
        }
        struct AnimationProbeSpec { std::uint32_t rid; const char *bytes; };
        // These exact-build bodies prove four pure getters and the three
        // managed reference slots. The lazy cache getters and Move lifecycle
        // methods below are fingerprint evidence only, never invoked.
        const std::array<AnimationProbeSpec, 8> animationProbeSpecs{{
            {0x18753, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 80 B0 00 00 00 48 8D 65 00 5D C3"},
            {0x3b77, "55 48 8B EC 48 83 EC 30 48 89 4D F8 48 85 C9 0F 84 19 00 00 00 48 8B D1 48 83 C2 10 4D 8B 15 8D 3D 66 1B E8 5E 57 80 02 48 8D 65 00 5D C3 BA 2E 00 00 00 B9 43 01 00 00 E8 41 74 7F 02"},
            {0x3b79, "55 48 8B EC 48 83 EC 30 48 89 4D F8 48 85 C9 0F 84 19 00 00 00 48 8B D1 48 83 C2 20 4D 8B 15 1D 3D 66 1B E8 F2 56 80 02 48 8D 65 00 5D C3 BA 2E 00 00 00 B9 43 01 00 00 E8 C1 73 7F 02"},
            {0x3b98, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 0F B6 40 41 48 8D 65 00 5D C3"},
            {0x3bc0, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 0F B6 40 51 48 8D 65 00 5D C3"},
            {0xe6d9, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 0F B6 80 B8 00 00 00 48 8D 65 00 5D C3"},
            {0x17f34, "55 48 8B EC 48 83 EC 40 48 89 75 E0 48 89 7D E8 4C 89 75 F0 4C 89 7D F8 48 8B F1 48 8B 8E B0 00 00 00 33 D2 E8 D3 3E 6E 01 85 C0 0F 85 2D 01 00 00 48 8B BE B0 00 00 00 4C 8B FF 48 85 FF 0F 84 6B 00 00 00 4C 8B 37 41 8B 46 28 49 8B 0D DE 5E 58 1A 48 3B C1 72 30 49 8B 46 20 49 8B 0D CE 5E 58 1A 48 8B D1 48 C1 FA 03 48 03 C2 0F B6 00 83 E1 07 BA 01 00 00 00 D3 E2 48 8B CA 23 C1 48 85 C0 0F 85 28 00 00 00 49 8B 06 49 8B 0D F7 27 55 1A 48 3B C1 75 16 48 8B 47 20 48 85 C0 74 0D 48 8B CF E8 EF A5 71 01 48 85 C0 75 03 45 33 FF 4D 85 FF 40 0F 97 C0 48 0F B6 C0 85 C0 0F 84 9C 00 00 00 48 8B 8E B0 00 00 00 4D 8B 15 C8 86 5A 1A E8 E1 29 73 01 48 8D 8E D0 00 00 00 48 89 01 C1 E9 09 48 81 E1 FF FF 7F 00 49 8B 15 18 28 53 1A 48 03 CA C6 01 01 48 8B 8E B0 00 00 00 4D 8B 15 A4 B8 59 1A E8 CB 6B 72 01 48 8D 8E D8 00 00 00 48 89 01 C1 E9 09 48 81 E1 FF FF 7F 00 49 8B 15 E4 27 53 1A 48 03 CA C6 01 01 48 8B 8E B0 00 00 00 4D 8B 15 88 B8 5B 1A E8 D3 D3 73 01 48 8D 8E E0 00 00 00 48 89 01 C1 E9 09 48 81 E1 FF FF 7F 00 49 8B 15 B0 27 53 1A 48 03 CA C6 01 01 48 8B 75 E0 48 8B 7D E8 4C 8B 75 F0 4C 8B 7D F8 48 8D 65 00 5D C3"},
            {0x17f36, "55 48 8B EC 48 83 EC 60 48 89 75 F0 4C 89 7D F8 48 8B F1 33 C0 48 89 45 D8 48 89 45 E0 48 89 45 E8 48 8B 86 D0 00 00 00 48 85 C0 74 25 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 57 B5 5B 1A FF 90 70 FF FF FF 48 8B D0 48 8B CE E8 7C B8 76 01 48 8B 86 D8 00 00 00 48 85 C0 0F 84 CD 00 00 00 48 8B 86 D8 00 00 00 48 8B C8 48 8B 00 4D 8B 15 1A 16 62 1A FF 50 C0 48 8B D0 48 8B CE E8 86 B8 76 01 48 8B 86 D8 00 00 00 48 8B C8 48 8B 00 4D 8B 15 00 16 62 1A FF 50 B0 48 8B CE F2 0F 10 C8 E8 6D B8 76 01 48 8B 86 D8 00 00 00 48 8B C8 48 8B 00 4D 8B 15 E5 15 62 1A FF 90 78 FF FF FF 48 89 75 D0 85 C0 0F 84 53 00 00 00 0F B6 86 E8 00 00 00 85 C0 0F 84 44 00 00 00 48 8B CE E8 3A B8 76 01 E8 8F 34 6E 01 F2 0F 11 45 C8 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 A6 15 62 1A FF 50 A0 F3 0F 10 C8 F2 0F 10 45 C8 4D 33 FF 67 0F 2F C8 7A 04 41 0F 92 C7 E9 03 00 00 00 45 33 FF 48 8B 4D D0 49 8B D7 E8 F9 B7 76 01 48 8B 86 E0 00 00 00 48 85 C0 0F 84 47 00 00 00 48 8B 86 E0 00 00 00 48 8B D5 48 83 C2 D8 48 8B C8 48 8B 00 4D 8B 15 0E F6 55 1A FF 50 E8 48 8B CD 48 83 C1 D8 E8 AD A4 6E 01 F3 0F 10 0D 2F 00 00 00 48 33 D2 67 0F 2F C8 7A 04 40 0F 92 C2 48 8B CE E8 AC B7 76 01 48 8B 75 F0 4C 8B 7D F8 48 8D 65 00 5D C3"},
        }};
        bool VerifyAnimationProbe()
        {
            animationProbeReady.store(false, std::memory_order_release);
            animationProbeMethods = {};
            for (const auto &spec : animationProbeSpecs)
                if (!Matches(aot::Method(spec.rid), spec.bytes)) return false;
            animationProbeMethods = {aot::Method(0x18753), aot::Method(0x3b98), aot::Method(0x3bc0), aot::Method(0xe6d9)};
            animationProbeReady.store(true, std::memory_order_release);
            return true;
        }
        // Exact full RUNTIME_FUNCTION bodies from build 21393610. The node
        // InitBUCData/InitData pair proves its existing MMData reference +D0;
        // only E783 is invoked, never either lifecycle method.
        const std::array<AnimationProbeSpec, 3> motionMatchingSpecs{{
            {0xe783, "55 48 8B EC 48 83 EC 40 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B FA 49 8B F0 33 C0 4C 0F B6 F8 83 3F 00 48 8B 4F 28 48 8B C1 83 38 00 48 63 41 40 48 63 49 48 2B C1 85 C0 0F 8E E6 01 00 00 4C 8B FE 83 FE 09 0F 83 C1 01 00 00 49 8B C7 48 C1 E0 03 8B C8 49 8B 05 BD 5A DE 1A 48 03 C1 48 8B 00 FF E0 B8 01 00 00 00 4C 0F B6 F8 E9 B4 01 00 00 83 3F 00 48 8B 47 28 48 8B C8 BA 03 00 00 00 83 38 00 E8 66 46 F4 01 85 C0 75 1C 83 3F 00 48 8B 47 28 48 8B C8 BA 02 00 00 00 83 38 00 E8 4B 46 F4 01 48 8B F0 EB 05 BE 01 00 00 00 4C 0F B6 FE E9 6F 01 00 00 83 3F 00 48 8B 47 28 48 8B C8 BA 04 00 00 00 83 38 00 E8 21 46 F4 01 85 C0 75 1C 83 3F 00 48 8B 47 28 48 8B C8 BA 02 00 00 00 83 38 00 E8 06 46 F4 01 48 8B F0 EB 05 BE 01 00 00 00 4C 0F B6 FE E9 2A 01 00 00 83 3F 00 48 8B 47 28 48 8B C8 BA 05 00 00 00 83 38 00 E8 DC 45 F4 01 85 C0 75 1C 83 3F 00 48 8B 47 28 48 8B C8 BA 02 00 00 00 83 38 00 E8 C1 45 F4 01 48 8B F0 EB 05 BE 01 00 00 00 4C 0F B6 FE E9 E5 00 00 00 83 3F 00 48 8B 47 28 48 8B C8 BA 06 00 00 00 83 38 00 E8 97 45 F4 01 85 C0 75 1C 83 3F 00 48 8B 47 28 48 8B C8 BA 01 00 00 00 83 38 00 E8 7C 45 F4 01 48 8B F0 EB 05 BE 01 00 00 00 4C 0F B6 FE E9 A0 00 00 00 83 3F 00 48 8B 47 28 48 8B C8 BA 07 00 00 00 83 38 00 E8 52 45 F4 01 85 C0 75 1C 83 3F 00 48 8B 47 28 48 8B C8 BA 01 00 00 00 83 38 00 E8 37 45 F4 01 48 8B F0 EB 05 BE 01 00 00 00 4C 0F B6 FE E9 5B 00 00 00 83 3F 00 48 8B 47 28 48 8B C8 BA 08 00 00 00 83 38 00 E8 0D 45 F4 01 85 C0 75 1C 83 3F 00 48 8B 47 28 48 8B C8 BA 01 00 00 00 83 38 00 E8 F2 44 F4 01 48 8B F0 EB 05 BE 01 00 00 00 4C 0F B6 FE EB 19 83 3F 00 48 8B 47 28 48 8B C8 48 8B D6 83 38 00 E8 CD 44 F4 01 4C 0F B6 F8 49 8B C7 48 8B 75 E8 48 8B 7D F0 4C 8B 7D F8 48 8D 65 00 5D C3"},
            {0x17ea7, "55 48 8B EC 48 83 EC 40 48 89 75 E0 48 89 7D E8 4C 89 75 F0 4C 89 7D F8 48 8B F1 48 8B 8E B0 00 00 00 33 D2 E8 43 BC 6E 01 85 C0 0F 85 61 01 00 00 48 8B BE B0 00 00 00 4C 8B FF 48 85 FF 0F 84 6B 00 00 00 4C 8B 37 41 8B 46 28 49 8B 0D 4E DC 58 1A 48 3B C1 72 30 49 8B 46 20 49 8B 0D 3E DC 58 1A 48 8B D1 48 C1 FA 03 48 03 C2 0F B6 00 83 E1 07 BA 01 00 00 00 D3 E2 48 8B CA 23 C1 48 85 C0 0F 85 28 00 00 00 49 8B 06 49 8B 0D 67 A5 55 1A 48 3B C1 75 16 48 8B 47 20 48 85 C0 74 0D 48 8B CF E8 5F 23 72 01 48 85 C0 75 03 45 33 FF 4D 85 FF 40 0F 97 C0 48 0F B6 C0 85 C0 0F 84 D0 00 00 00 48 8B 8E B0 00 00 00 4D 8B 15 98 8B 62 1A E8 FD 33 77 01 48 8D 8E D0 00 00 00 48 89 01 C1 E9 09 48 81 E1 FF FF 7F 00 49 8B 15 88 A5 53 1A 48 03 CA C6 01 01 48 8B 8E B0 00 00 00 4D 8B 15 6C 8B 62 1A E8 D3 33 77 01 48 8D 8E D8 00 00 00 48 89 01 C1 E9 09 48 81 E1 FF FF 7F 00 49 8B 15 54 A5 53 1A 48 03 CA C6 01 01 48 8B 8E B0 00 00 00 4D 8B 15 20 8B 62 1A E8 81 33 77 01 48 8D 8E E0 00 00 00 48 89 01 C1 E9 09 48 81 E1 FF FF 7F 00 49 8B 15 20 A5 53 1A 48 03 CA C6 01 01 48 8B 8E B0 00 00 00 4D 8B 15 C4 35 5C 1A E8 0F 51 74 01 48 8D 8E E8 00 00 00 48 89 01 C1 E9 09 48 81 E1 FF FF 7F 00 49 8B 15 EC A4 53 1A 48 03 CA C6 01 01 48 8B 75 E0 48 8B 7D E8 4C 8B 75 F0 4C 8B 7D F8 48 8D 65 00 5D C3"},
            {0x17ea8, "55 48 8B EC 48 83 EC 60 48 89 75 F8 48 8B F1 E8 22 33 77 01 48 8B 86 D0 00 00 00 48 85 C0 0F 84 EC 01 00 00 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 90 8A 62 1A FF 50 A0 48 8B D0 48 8B CE E8 FA 32 77 01 48 8B 86 D0 00 00 00 48 8B D5 48 83 C2 C8 48 8B C8 48 8B 00 4D 8B 15 D7 C4 5C 1A FF 50 C8 48 8B CE 48 8B C5 48 83 C0 E0 48 8B 55 C8 48 89 55 E0 48 8B 55 D0 48 89 55 E8 48 8B 55 D8 48 89 55 F0 48 8D 10 E8 BC 32 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 2E 8A 62 1A FF 50 88 48 8B D0 48 8B CE E8 A4 32 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 9C 41 5C 1A FF 90 78 FF FF FF 48 8B D0 48 8B CE E8 89 32 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 EF 89 62 1A FF 50 88 48 8B CE F2 0F 10 C8 E8 70 32 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 D4 89 62 1A FF 50 F8 48 8B CE F2 0F 10 C8 E8 57 32 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 B9 89 62 1A FF 50 C8 48 8B CE F2 0F 10 C8 E8 3E 32 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 FE 40 5C 1A FF 50 B8 48 8B CE F2 0F 10 C8 E8 25 32 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 BB C3 5C 1A FF 50 D0 48 8B CE F2 0F 10 C8 E8 0C 32 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 58 89 62 1A FF 50 80 48 8B D0 48 8B CE E8 F4 31 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 3E 89 62 1A FF 90 78 FF FF FF 48 8B D0 48 8B CE E8 D9 31 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 21 89 62 1A FF 50 A0 48 8B D0 48 8B CE E8 C1 31 77 01 48 8B 86 D0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 07 89 62 1A FF 50 88 48 8B CE F2 0F 10 C8 E8 A8 31 77 01 48 8B 86 D8 00 00 00 48 85 C0 0F 84 23 00 00 00 48 8B 86 D8 00 00 00 48 8B C8 48 8B 00 4D 8B 15 1C 32 5C 1A FF 50 D8 48 8B CE F2 0F 10 C8 E8 7F 31 77 01 48 8B 86 E0 00 00 00 48 85 C0 0F 84 44 00 00 00 48 8B 86 E0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 49 88 62 1A FF 50 90 48 8B D0 48 8B CE E8 8F 30 77 01 48 8B 86 E0 00 00 00 48 8B C8 48 8B 00 4D 8B 15 FF 2F 5C 1A FF 50 A0 48 8B D0 48 8B CE E8 35 31 77 01 48 8B 75 F8 48 8D 65 00 5D C3"},
        }};
        bool VerifyMotionMatchingSelector()
        {
            motionMatchingSelectorReady.store(false, std::memory_order_release);
            motionMatchingMethods = {};
            // Top getter, both cache slots and the pure configured-mode getter.
            // Reuse their complete evidence without depending on Move.Common.
            for (std::size_t i = 0; i != 4; ++i)
                if (!Matches(aot::Method(animationProbeSpecs[i].rid), animationProbeSpecs[i].bytes)) return false;
            for (const auto &spec : motionMatchingSpecs)
                if (!Matches(aot::Method(spec.rid), spec.bytes)) return false;
            motionMatchingMethods = {aot::Method(0x18753), aot::Method(0x3b98), aot::Method(0xe783)};
            motionMatchingSelectorReady.store(true, std::memory_order_release);
            return true;
        }
        bool VerifyMethods()
        {
            methods = {};
            methods.findRef = aot::RuntimeMethod(0x168e);
            methods.setTamerType = aot::Method(0xd9b8);
            methods.initialized = aot::Method(0xd9d5);
            methods.inputOwner = aot::Method(0x11beb);
            methods.getTeam = aot::Method(0xd9d3);
            methods.setTeam = aot::Method(0xd9d2);
            methods.getEventCollection = aot::Method(0xd9c1);
            methods.clearTargetEvent = aot::Method(0x3233);
            methods.invokeVoid = aot::Method(0x15219);
            methods.makeTargetInfo = aot::Method(0x108f6);
            methods.setTargetInfo = aot::Method(0x106eb);
            methods.weaponEvents = aot::Method(0xffd1);
            methods.weaponInitialized = aot::Method(0x95b6);
            methods.weaponMasterEvent = aot::Method(0x313b);
            methods.weaponActiveEvent = aot::Method(0x33bf);
            methods.invokeActor = aot::Method(0x152cd);
            methods.invokeBool = aot::Method(0x1525a);
            methods.boolPropertyEvent = aot::Method(0x3445);
            methods.enumPropertyEvent = aot::Method(0x3451);
            methods.resetPropertyEvent = aot::Method(0x3443);
            methods.invokeBoolProperty = aot::Method(0x15cc8);
            methods.invokeEnumProperty = aot::Method(0x15ce6);
            methods.invokeResetProperty = aot::Method(0x15cc3);
            methods.propHandle = aot::Method(0xaddb);
            methods.calculateProperty = aot::Method(0xb116);
            methods.hiddenPropertyReturn = aot::Method(0xaec3) + 0x5e;
            methods.actionDesc = aot::Method(0x11ea6);
            methods.getAxis = aot::Method(0x64e3);
            methods.receiveBattle = aot::Method(0x109d5);
            methods.receiveAction = aot::Method(0x109d1);
            const auto move = aot::Method(0x11c0e), skill = aot::Method(0x11c23);
            methods.firstParam = CallTarget(move + 0xd3);
            return Matches(methods.findRef, "55 48 8B EC 48 83 EC 50 48 89 75 F8 48 89 4D E0 48 C7 45 E8 00 00 00 00 C7 45 F0 00 00 00 00") &&
                   Matches(methods.findRef + 0xe5, "48 8B 45 E8 48 8B 75 F8 48 8D 65 00 5D C3") &&
                   Matches(aot::RuntimeMethod(0x168d) + 0x24, "48 8B 46 10 48 89 45 E0") &&
                   Matches(aot::RuntimeMethod(0x169d) + 0x42, "48 8B 45 D8 49 89 45 20") &&
                   Matches(aot::RuntimeMethod(0x145f), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 40 58 48 8D 65 00 5D C3") &&
                   Matches(aot::Method(0xda21), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 40 10 48 8D 65 00 5D C3") &&
                   Matches(methods.setTamerType, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 89 55 F0 48 8B C1 48 63 4D F0 89 88 E4 02 00 00 48 8D 65 00 5D C3") &&
                   Matches(methods.initialized, "55 48 8B EC 48 83 EC 30 48 89 4D F8 E8 ? ? ? ? 0F B6 80 55 01 00 00 48 8D 65 00 5D C3") &&
                   Matches(methods.inputOwner, "55 48 8B EC 48 83 EC 30 48 89 4D F8 48 8B C1 48 8B 48 10 33 D2 E8 ? ? ? ? 48 8D 65 00 5D C3") &&
                   Matches(methods.getTeam, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 63 80 CC 02 00 00 48 8D 65 00 5D C3") &&
                   Matches(methods.setTeam, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 89 55 F0 48 8B C1 48 63 4D F0 89 88 CC 02 00 00 48 8D 65 00 5D C3") &&
                   Matches(methods.getEventCollection, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 80 A0 02 00 00 48 8D 65 00 5D C3") &&
                   Matches(methods.clearTargetEvent, "55 48 8B EC 48 83 EC 40 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1 48 8B 86 D8 00 00 00 81 78 18 45 01 00 00") &&
                   Matches(methods.invokeVoid, "55 48 8B EC 48 83 EC 30 48 89 75 F8 48 8B F1 48 8B 46 10 48 8B F0 48 85 C0 75 02 EB 06 48 8B CE FF 56 18") &&
                   Matches(methods.makeTargetInfo, "55 48 8B EC 48 83 EC 70 4C 89 6D E8 4C 89 75 F0 4C 89 7D F8 4C 8B F9 48 89 55 E0 4C 89 45 D8") &&
                   Matches(methods.setTargetInfo, "55 48 8B EC 48 81 EC B0 00 00 00 48 89 4D 88 48 89 55 80 4C 89 85 78 FF FF FF 48 8B CA") &&
                   Matches(methods.weaponEvents, "55 48 8B EC 48 83 EC 40 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1 E8 1A 6A DA 01 85 C0 74 07") &&
                   // BGUSimpleActorBaseCS, not BGUCharacterCS: weapons have an
                   // independent ECS container and LateBeginPlay lifecycle.
                   Matches(methods.weaponInitialized, "55 48 8B EC 48 83 EC 30 48 89 4D F8 E8 61 B6 35 02 0F B6 80 55 01 00 00 48 8D 65 00 5D C3") &&
                   // Include the distinct event-cache index and slot, not just
                   // the common getter prologue shared by hundreds of events.
                   Matches(methods.weaponMasterEvent, "55 48 8B EC 48 83 EC 40 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1 48 8B 86 D8 00 00 00 81 78 18 C9 00 00 00 0F 86 89 00 00 00 48 8B B8 68 06 00 00") &&
                   Matches(methods.weaponActiveEvent, "55 48 8B EC 48 83 EC 40 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1 48 8B 86 D8 00 00 00 81 78 18 0B 02 00 00 0F 86 89 00 00 00 48 8B B8 78 10 00 00") &&
                   Matches(methods.invokeActor, "55 48 8B EC 48 83 EC 30 48 89 7D F8 48 8B F9 48 89 55 F0 48 8B 47 10 48 8B F8 48 85 C0 75 02 EB 0A 48 8B CF 48 8B 55 F0 FF 57 18 48 8B 7D F8 48 8D 65 00 5D C3") &&
                   Matches(methods.invokeBool, "55 48 8B EC 48 83 EC 30 48 89 7D F8 48 8B F9 48 89 55 F0 48 8B 47 10 48 8B F8 48 85 C0 75 02 EB 0A 48 8B CF 0F B6 55 F0 FF 57 18 48 8B 7D F8 48 8D 65 00 5D C3") &&
                   Matches(methods.boolPropertyEvent, "55 48 8B EC 48 83 EC 40 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1 48 8B 86 D8 00 00 00 81 78 18 4E 02 00 00 0F 86 89 00 00 00 48 8B B8 90 12 00 00") &&
                   Matches(methods.enumPropertyEvent, "55 48 8B EC 48 83 EC 40 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1 48 8B 86 D8 00 00 00 81 78 18 54 02 00 00 0F 86 89 00 00 00 48 8B B8 C0 12 00 00") &&
                   Matches(methods.resetPropertyEvent, "55 48 8B EC 48 83 EC 40 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1 48 8B 86 D8 00 00 00 81 78 18 4D 02 00 00 0F 86 89 00 00 00 48 8B B8 88 12 00 00") &&
                   Matches(methods.invokeBoolProperty, "55 48 8B EC 48 83 EC 50 4C 89 65 F8 4C 8B E1 48 89 55 F0 4C 89 45 E8 4C 89 4D E0 49 8B 44 24 10 4C 8B E0 48 85 C0 75 02 EB 26 8B 45 30 48 89 44 24 20 0F B6 45 38 48 89 44 24 28 49 8B CC 48 63 55 F0") &&
                   Matches(methods.invokeEnumProperty, "55 48 8B EC 48 83 EC 60 48 89 5D F8 48 8B D9 48 89 55 F0 4C 89 45 E8 4C 89 4D E0 48 8B 43 10 48 8B D8 48 85 C0 75 02 EB 2C 8B 45 30 48 89 44 24 20 0F B6 45 38 48 89 44 24 28 48 8B 45 40 48 89 44 24 30") &&
                   Matches(methods.invokeResetProperty, "55 48 8B EC 48 83 EC 40 4C 89 7D F8 4C 8B F9 48 89 55 F0 4C 89 45 E8 49 8B 47 10 4C 8B F8 48 85 C0 75 02 EB 0E 49 8B CF 8B 55 F0 44 8B 45 E8 41 FF 57 18 4C 8B 7D F8 48 8D 65 00 5D C3") &&
                   Matches(methods.propHandle, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 8B 40 48 48 8D 65 00 5D C3") &&
                   Matches(methods.calculateProperty, "55 48 8B EC 48 83 EC 50 48 89 75 F0 48 89 7D F8 48 8B F9 48 8B F2 48 C7 45 E8 00 00 00 00 48 C7 45 E0 00 00 00 00 48 8B 87 98 00 00 00 48 8B C8 83 39 00 48 8B 40 18 48 8B C8 48 8B D6 4C 8B C5") &&
                   Matches(aot::Method(0xaec3) + 0x44, "48 8B 45 E8 48 8B 40 20 83 3E 00 0F B6 56 20 48 8B C8 83 38 00 E8 CA 8C 20 02") &&
                   Matches(aot::Method(0xaec3), "55 48 8B EC 48 83 EC 40 48 89 75 F0 4C 89 7D F8 48 89 4D E8 48 8B F2 4C 8B FE 48 85 F6 74 1D 48 8B 06 48 8B 00 48 8B 40 10 48 8B 40 10 49 8B 0D A4 83 0A 1B 48 3B C1 74 03 45 33 FF 49 8B F7 4D 85 FF 74 1A 48 8B 45 E8 48 8B 40 20 83 3E 00 0F B6 56 20 48 8B C8 83 38 00 E8 CA 8C 20 02 48 8B 75 F0 4C 8B 7D F8 48 8D 65 00 5D C3") &&
                   Matches(methods.actionDesc, "55 48 8B EC 48 83 EC 30 48 89 4D F0 49 8B 0D ? ? ? ? 4D 8B 15 ? ? ? ? E8 ? ? ? ? 48 8B C8 48 63 55 F0 83 38 00 E8 ? ? ? ? 48 8D 65 00 5D C3") &&
                   Matches(methods.getAxis, "55 48 8B EC 48 83 EC 30 48 89 4D F8 E8 ? ? ? ? 48 8B 4D F8 48 85 C9 0F 84 0F 00 00 00 48 83 C1 28 E8 ? ? ? ? 48 8D 65 00 5D C3") &&
                   Matches(methods.receiveBattle, "55 48 8B EC 48 83 EC 40 48 89 75 E0 48 89 7D E8 4C 89 75 F0 4C 89 7D F8 48 8B F9 48 8B F2") &&
                   Matches(methods.receiveAction, "55 48 8B EC 48 83 EC 50 48 89 75 D8 48 89 7D E0 4C 89 6D E8 4C 89 75 F0 4C 89 7D F8 48 8B F9 48 8B F2") &&
                   Matches(move + 0x94, "83 3E 00 48 8B 46 20 48 8B C8 83 39 00 48 63 40 18 83 F8 02") &&
                   Matches(skill + 0x105, "83 3B 00 48 8B 43 20 48 8B C8 83 39 00 48 63 40 18") &&
                   Matches(skill + 0x121, "83 3B 00 48 8B 43 20 48 8B C8 33 D2 83 38 00 E8 ? ? ? ?") &&
                   // These concrete calls carry only rcx/edx, no hidden R10
                   // context. Use their shared inflated RepeatedField<int>
                   // indexer PLT rather than invoking an open generic method.
                   methods.firstParam == CallTarget(skill + 0x130) && SameImageCode(methods.firstParam, move) && Matches(methods.firstParam, "FF 25 ? ? ? ?");
        }
        enum class HookAbi { InstanceVoid, InstanceBool };
        enum class HookModule { Main, Engine };
        struct Spec
        {
            std::uint32_t rid;
            const char *bytes;
            Callback callback;
            void *user{};
            HookAbi abi{HookAbi::InstanceVoid};
            HookModule module{HookModule::Main};
        };
        std::uintptr_t HookAddress(const Spec &spec) { return spec.module == HookModule::Engine ? aot::EngineMethod(spec.rid) : aot::Method(spec.rid); }
        const std::array<Spec, 21> hookSpecs{{
            {0xce8, "55 48 8B EC 48 83 EC 40 4C 89 75 F0 4C 89 7D F8 48 89 4D E8 48 89 55 E0 E8 F3 7E 27 01 49 8B 05 A4 F6 AE 17 0F B6 00 85 C0 75 0C 49 8B 05 9E F6 AE 17 E9 FA 00 00 00 49 8B 05 9A F6 AE 17 48 63 00 83 C0 10 44 8B F8 4D 85 FF 75 05 45 33 F6 EB 5D", ObservePlayerHidden, nullptr, HookAbi::InstanceVoid, HookModule::Engine},
            {0xb117, "55 48 8B EC 48 81 EC F0 00 00 00 48 89 5D C8 48 89 75 D0 48 89 7D D8 4C 89 65 E0 4C 89 6D E8 4C 89 75 F0 4C 89 7D F8 4C 8B E1 4C 8B EA 4D 8B F0 4D 8B F9 48 8B 7D 30 48 8B 75 38", CaptureProperty},
            {0xb109, "55 48 8B EC 48 81 EC B0 00 00 00 48 89 75 E0 48 89 7D E8 4C 89 75 F0 4C 89 7D F8 48 8B F1 33 C0 48 89 45 A8 48 89 45 B0 48 89 45 B8 48 89 45 C0 48 89 45 C8 48 89 45 D0 48 89 45 D8", RetirePlayerIsolation},
            {0x10367, "55 48 8B EC 48 83 EC 40 48 89 75 F8 48 8B F1 48 8B 86 98 00 00 00", Suppress},
            {0x954f, "55 48 8B EC 48 83 EC 40 48 89 7D F8 48 8B F9 48 89 55 F0 4C 89 45 E8", Suppress},
            {0xff69, "55 48 8B EC 48 81 EC D0 04 00 00 48 89 5D C8 48 89 75 D0 48 89 7D D8", OnDeath},
            {0x51df, "55 48 8B EC 48 81 EC E0 00 00 00 48 89 7D F0 4C 89 7D F8 48 8B F9", Suppress},
            {0x51e2, "55 48 8B EC 48 83 EC 50 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1", Suppress},
            // BUS_BattleAICooperativeCompl: preserve AFE5 OnAttach's local event
            // binding. Every global register/unregister/dispatch entry below
            // is independently intercepted, including direct/repeated calls.
            // All seven methods are instance void; rcx is their component.
            {0xafe6, "55 48 8B EC 48 83 EC 30 48 89 4D F8 48 8B 81 98 00 00 00 48 8B D0 83 3A 00 48 8B 50 10", Suppress}, // OnBeginPlay
            {0xafe7, "55 48 8B EC 48 83 EC 40 48 89 75 F0 48 89 7D F8 48 8B F1 48 8B 86 98 00 00 00 48 8B C8 83 39 00", Suppress}, // OnEndPlay
            {0xafe8, "55 48 8B EC 48 83 EC 30 48 89 75 F0 48 89 7D F8 48 8B F9 48 8B F2 48 8B 87 98 00 00 00 83 38 00", Suppress}, // RegisterSC
            {0xafe9, "55 48 8B EC 48 81 EC 90 00 00 00 48 89 75 F0 48 89 7D F8 48 8B F1 48 89 55 D8", Suppress}, // HandleBattleMessage
            {0xafea, "55 48 8B EC 48 83 EC 70 48 89 7D F0 4C 89 7D F8 48 8B F9 48 89 55 E0 48 C7 45 E8 00 00 00 00", Suppress}, // OnMulticastCachedPointSet
            {0xafeb, "55 48 8B EC 48 83 EC 40 48 89 4D F8 33 D2 E8 13 5E 22 02 48 8D 65 00 5D C3", Suppress}, // OnUnitTriggerDead
            {0xafec, "55 48 8B EC 48 83 EC 40 48 89 7D F0 4C 89 7D F8 48 8B F9 48 89 55 E8 48 8B 87 98 00 00 00", Suppress}, // HandleBattleMessageByReason
            {0x11bf9, "55 48 8B EC 48 83 EC 60 48 89 75 F8 48 89 4D F0 4C 89 45 E0", Input, reinterpret_cast<void *>(Action::LightAttack), HookAbi::InstanceBool},
            {0x11bfc, "55 48 8B EC 48 83 EC 60 48 89 75 F8 48 89 4D F0 48 89 55 E8 4C 89 45 E0", Input, reinterpret_cast<void *>(Action::HeavyAttack), HookAbi::InstanceBool},
            {0x11c19, "55 48 8B EC 48 83 EC 70 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F9", Input, reinterpret_cast<void *>(Action::Dodge), HookAbi::InstanceBool},
            {0x11c0e, "55 48 8B EC 48 83 EC 50 48 89 75 E8 48 89 7D F0 4C 89 75 F8 4C 8B F1 48 89 55 D8 49 8B F9", Move, nullptr, HookAbi::InstanceBool},
            {0x11c23, "55 48 8B EC 48 81 EC 60 01 00 00 48 89 5D C8 48 89 75 D0 48 89 7D D8 4C 89 65 E0 4C 89 6D E8", SkillInput, nullptr, HookAbi::InstanceBool},
            {0x11c25, "55 48 8B EC 48 83 EC 50 48 89 75 F0 48 89 7D F8 48 8B F1", Input, reinterpret_cast<void *>(Action::Special), HookAbi::InstanceBool}
        }};
        bool InstallHooks()
        {
            // Resolve and validate every entry before the first mutation. One
            // failed cooperative fingerprint keeps the whole adapter offline.
            std::vector<std::uintptr_t> targets;
            targets.reserve(hookSpecs.size());
            for (const auto &spec : hookSpecs)
            {
                const auto address = HookAddress(spec);
                if (!Matches(address, spec.bytes))
                {
                    Log(std::format("Transformation native method fingerprint rejected: {:X}", spec.rid));
                    return false;
                }
                targets.push_back(address);
            }
            std::vector<std::unique_ptr<MidHook>> hooks;
            hooks.reserve(hookSpecs.size());
            for (std::size_t i = 0; i != hookSpecs.size(); ++i)
            {
                const auto &spec = hookSpecs[i];
                std::string error;
                auto hook = MidHook::Create(reinterpret_cast<void *>(targets[i]), spec.callback, spec.user, error);
                if (!hook)
                {
                    Log(std::format("Transformation native hook {:X}: {}", spec.rid, error));
                    return false; // Local ownership rolls back every installed hook.
                }
                hooks.push_back(std::move(hook));
            }
            Hooks() = std::move(hooks);
            return true;
        }
    }
    void Initialize()
    {
        std::lock_guard lock(initializeMutex);
        if (Ready() || !aot::Ready() || !engine::WeakReady() || !VerifyMethods() || !VerifyManagedRuntime() || !MakeReturnStub() || !InstallHooks())
            return;
        ready.store(true, std::memory_order_release);
        Log(VerifyAnimationProbe() ? "Transformation animation data observation ready (read-only)" : "Transformation animation data observation unavailable; gameplay adapter remains enabled");
        Log(VerifyMotionMatchingSelector() ? "Transformation Motion Matching resource selector ready (read-only)" : "Transformation Motion Matching resource selector unavailable; gameplay adapter remains enabled");
        Log("Transformation native adapter: managed identity, owned cooperative isolation and mapped input hooks ready (experimental)");
    }
    bool Ready() { return ready.load(std::memory_order_acquire); }
    AnimationStateSnapshot InspectAnimationState(std::uintptr_t proxy, std::uintptr_t topAnim, std::uintptr_t moveAnim)
    {
        AnimationStateSnapshot result;
        result.available = animationProbeReady.load(std::memory_order_acquire);
        if (!result.available || !Ready() || !engine::IsGameThread() || !Owned(proxy) ||
            !engine::IsA(topAnim, L"/Script/b1-Managed.BUAnimInstanceBase") ||
            !engine::IsA(moveAnim, L"/Script/b1-Managed.BUAnimHumanoidCS_Move")) return result;
        const auto session = Session();
        const auto topIdentity = engine::MakeWeak(topAnim), moveIdentity = engine::MakeWeak(moveAnim);
        const auto owns = [&](std::uintptr_t anim, engine::WeakObject identity) {
            auto owner = std::array{engine::Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
            return engine::ResolveWeak(identity) == anim &&
                engine::Call(L"/Script/Engine.AnimInstance:GetOwningActor", owner, anim).success && owner[0].Get<std::uintptr_t>() == proxy;
        };
        if (!owns(topAnim, topIdentity) || !owns(moveAnim, moveIdentity)) return result;
        const auto discard = [&] { AnimationStateSnapshot unknown; unknown.available = result.available; return unknown; };
        const bool observed = ManagedOperation([&] {
            // Intermediate roots remain on this native stack until all reads and
            // reference checks finish, within one continuous GC-unsafe region.
            volatile std::uintptr_t top = Managed(topAnim), move = Managed(moveAnim);
            if (!top || !move) return false;
            std::array<std::pair<std::uintptr_t, std::uintptr_t>, 3> references{};
            std::size_t referenceCount{};
            const auto readableObject = [](std::uintptr_t value) {
                std::uintptr_t header{};
                return Pointer(value) && ReadMemory(value, &header, sizeof(header)) && header;
            };
            const auto reference = [&](std::uintptr_t address, std::uintptr_t &value) {
                if (!ReadMemory(address, &value, sizeof(value))) return false;
                references[referenceCount++] = {address, value};
                return true;
            };
            const auto mode = [&](std::uintptr_t method, std::uintptr_t object, AnimationDataState &state, int &value) {
                if (!object) { state = AnimationDataState::Missing; return; }
                std::uint8_t read{};
                if (readableObject(object) && method && Safe(reinterpret_cast<std::uint8_t (*)(std::uintptr_t)>(method), read, object) && read <= 13)
                { state = AnimationDataState::Present; value = read; }
            };
            std::uintptr_t initialSetting{};
            const bool settingRead = animationProbeMethods.setting && Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(animationProbeMethods.setting), initialSetting, static_cast<std::uintptr_t>(top));
            volatile std::uintptr_t setting = initialSetting;
            if (settingRead)
            {
                if (!setting) result.setting = AnimationDataState::Missing;
                else if (readableObject(setting))
                {
                    result.setting = AnimationDataState::Present;
                    std::uintptr_t common{}, matching{};
                    // 3B77/3B79 prove these cache slots but are not called: their
                    // getters allocate and initialize a missing cache.
                    if (reference(setting + 0x10, common)) mode(animationProbeMethods.configuredMode, common, result.commonSetting, result.configuredMode);
                    if (reference(setting + 0x20, matching)) mode(animationProbeMethods.spareMode, matching, result.motionMatchingSetting, result.spareMode);
                }
            }
            std::uintptr_t initialCommon{};
            if (reference(move + 0xd0, initialCommon))
            {
                // Move.InitBUCData (17F34) stores the result of the exact
                // <IBUC_ABPCommonSettingData,BUC_ABPCommonSettingData> MethodSpec
                // here; UpdateData (17F36) independently reads the same slot.
                volatile std::uintptr_t common = initialCommon;
                mode(animationProbeMethods.finalMode, common, result.moveCommon, result.finalMode);
            }
            if (settingRead)
            {
                std::uintptr_t current{};
                if (!Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(animationProbeMethods.setting), current, static_cast<std::uintptr_t>(top)) || current != initialSetting) return false;
            }
            for (std::size_t i = 0; i < referenceCount; ++i)
            {
                std::uintptr_t current{};
                if (!ReadMemory(references[i].first, &current, sizeof(current)) || current != references[i].second) return false;
            }
            return true;
        });
        if (!observed || !Owned(proxy) || !owns(topAnim, topIdentity) || !owns(moveAnim, moveIdentity)) return discard();
        {
            std::lock_guard lock(stateMutex);
            if (session.generation != sessionGeneration || !SameIdentity(session.root, proxyIdentity)) return discard();
        }
        result.current = true;
        return result;
    }
    MotionMatchingSelection SelectMotionMatching(std::uintptr_t proxy, int speedType, bool locked)
    {
        MotionMatchingSelection result;
        result.available = motionMatchingSelectorReady.load(std::memory_order_acquire);
        if (!result.available || !Ready() || !engine::IsGameThread() || !Owned(proxy) || speedType < 0 || speedType > 2) return result;
        const auto session = Session();
        const auto discard = [&] { MotionMatchingSelection unknown; unknown.available = result.available; return unknown; };
        const auto getMesh = [&] {
            auto mesh = engine::Arg::Object("Mesh", 0);
            return engine::Property(proxy, mesh) ? mesh.Get<std::uintptr_t>() : 0;
        };
        const auto getTop = [](std::uintptr_t mesh) {
            auto args = std::array{engine::Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
            return mesh && engine::Call(L"/Script/Engine.SkeletalMeshComponent:GetAnimInstance", args, mesh).success ? args[0].Get<std::uintptr_t>() : 0;
        };
        const auto getLinked = [](std::uintptr_t anim, const wchar_t *tag) {
            auto args = std::array{engine::Arg::Name("InTag", tag), engine::Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
            return anim && engine::Call(L"/Script/Engine.AnimInstance:GetLinkedAnimGraphInstanceByTag", args, anim).success ? args[1].Get<std::uintptr_t>() : 0;
        };
        const auto owns = [&](std::uintptr_t anim, engine::WeakObject identity) {
            auto args = std::array{engine::Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
            return anim && engine::ResolveWeak(identity) == anim &&
                engine::Call(L"/Script/Engine.AnimInstance:GetOwningActor", args, anim).success && args[0].Get<std::uintptr_t>() == proxy;
        };
        const auto mesh = getMesh(), topAnim = getTop(mesh);
        const auto meshIdentity = engine::MakeWeak(mesh), topIdentity = engine::MakeWeak(topAnim);
        if (!mesh || engine::ResolveWeak(meshIdentity) != mesh || !engine::IsA(topAnim, L"/Script/b1-Managed.BUAnimInstanceBase") || !owns(topAnim, topIdentity)) return result;
        std::uintptr_t moveAnim{}, mmAnim{};
        engine::WeakObject moveIdentity, mmIdentity;
        const bool observed = ManagedOperation([&] {
            const auto readable = [](std::uintptr_t object) {
                std::uintptr_t header{};
                return Pointer(object) && ReadMemory(object, &header, sizeof(header)) && header;
            };
            volatile std::uintptr_t top = Managed(topAnim), setting{}, common{}, matching{}, node{}, mmData{};
            std::uintptr_t initialSetting{}, initialCommon{}, initialMatching{}, initialData{};
            if (!top || !Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(motionMatchingMethods.setting), initialSetting, static_cast<std::uintptr_t>(top))) return false;
            setting = initialSetting;
            if (!readable(setting) || !ReadMemory(setting + 0x10, &initialCommon, sizeof(initialCommon))) return false;
            common = initialCommon;
            std::uint8_t mode{};
            if (!readable(common) || !Safe(reinterpret_cast<std::uint8_t (*)(std::uintptr_t)>(motionMatchingMethods.configuredMode), mode, static_cast<std::uintptr_t>(common)) || mode > 13) return false;
            result.required = mode == 4;
            if (result.required)
            {
                if (!ReadMemory(setting + 0x20, &initialMatching, sizeof(initialMatching))) return false;
                matching = initialMatching;
                if (!readable(matching)) return false;
                moveAnim = getLinked(topAnim, L"Move");
                moveIdentity = engine::MakeWeak(moveAnim);
                if (!engine::IsA(moveAnim, L"/Script/b1-Managed.BUAnimHumanoidCS_Move") || !owns(moveAnim, moveIdentity)) return false;
                // A Final=None graph can legitimately have no linked MM node.
                // Keep that unknown; never call InitData or create a cache.
                mmAnim = getLinked(moveAnim, L"MotionMatching");
                mmIdentity = engine::MakeWeak(mmAnim);
                if (!engine::IsA(mmAnim, L"/Script/b1-Managed.BUAnimHumanoidCS_MotionMatching") || !owns(mmAnim, mmIdentity)) return false;
                node = Managed(mmAnim);
                if (!node || !ReadMemory(node + 0xd0, &initialData, sizeof(initialData))) return false;
                mmData = initialData;
                if (!readable(mmData)) return false;
                const auto valid = [&](std::uint32_t state, bool &accepted) {
                    std::uint8_t value{};
                    // E783 reads the full 32-bit state register. Explicit zero
                    // extension avoids depending on the byte ABI's high bits.
                    if (!Safe(reinterpret_cast<std::uint8_t (*)(std::uintptr_t, std::uintptr_t, std::uint32_t)>(motionMatchingMethods.validState), value,
                        static_cast<std::uintptr_t>(mmData), static_cast<std::uintptr_t>(matching), state) || value > 1) return false;
                    accepted = value != 0;
                    return true;
                };
                bool accepted{};
                if (locked)
                {
                    const auto state = static_cast<std::uint32_t>(6 + speedType);
                    if (!valid(state, accepted)) return false;
                    if (accepted) { result.valid = result.usesLocked = true; result.state = static_cast<int>(state); }
                }
                if (!result.valid)
                {
                    const auto state = static_cast<std::uint32_t>(3 + speedType);
                    if (!valid(state, accepted)) return false;
                    if (accepted) { result.valid = true; result.state = static_cast<int>(state); }
                }
            }
            std::uintptr_t currentSetting{}, currentCommon{}, currentMatching{}, currentData{};
            std::uint8_t currentMode{};
            if (!Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(motionMatchingMethods.setting), currentSetting, static_cast<std::uintptr_t>(top)) || currentSetting != initialSetting ||
                !ReadMemory(setting + 0x10, &currentCommon, sizeof(currentCommon)) || currentCommon != initialCommon ||
                !Safe(reinterpret_cast<std::uint8_t (*)(std::uintptr_t)>(motionMatchingMethods.configuredMode), currentMode, static_cast<std::uintptr_t>(common)) || currentMode != mode) return false;
            if (result.required && (!ReadMemory(setting + 0x20, &currentMatching, sizeof(currentMatching)) || currentMatching != initialMatching ||
                !ReadMemory(node + 0xd0, &currentData, sizeof(currentData)) || currentData != initialData)) return false;
            return true;
        });
        if (!observed || !Owned(proxy) || engine::ResolveWeak(meshIdentity) != mesh || getMesh() != mesh || getTop(mesh) != topAnim || !owns(topAnim, topIdentity)) return discard();
        if (result.required && (getLinked(topAnim, L"Move") != moveAnim || getLinked(moveAnim, L"MotionMatching") != mmAnim ||
            !owns(moveAnim, moveIdentity) || !owns(mmAnim, mmIdentity))) return discard();
        {
            std::lock_guard lock(stateMutex);
            if (session.generation != sessionGeneration || !SameIdentity(session.root, proxyIdentity)) return discard();
        }
        result.current = true;
        return result;
    }
    bool RunManagedOperation(bool (*operation)(void *), void *state)
    {
        return operation && ManagedOperation([&] {
            bool result{};
            return Safe(operation, result, state) && result;
        });
    }
    engine::WeakObject OwnedComponentOwner(std::uintptr_t component)
    {
        if (!OwnedComponent(component)) return {};
        const auto actor = NativeAddress(Read<std::uintptr_t>(component + 0x10));
        const auto identity = engine::MakeWeak(actor);
        return engine::ResolveWeak(identity) == actor ? identity : engine::WeakObject{};
    }
    ScopedCleanup::ScopedCleanup(std::uintptr_t actor) : managedStack_{&managedStack_, "GlossTrainer transformation synchronous cleanup"}
    {
        if (!Ready() || !engine::IsGameThread())
            return;
        const auto session = Session();
        const auto actorIdentity = engine::MakeWeak(actor);
        if (!actor || engine::ResolveWeak(actorIdentity) != actor ||
            (!SameIdentity(actorIdentity, session.root) && !RecordedChild(actorIdentity)))
            return;
        managedEntered_ = EnterManagedRegion(managedStack_, managedCookie_);
        if (!managedEntered_)
            return;
        // Keep this complete cleanup unsafe through K2_DestroyActor and all
        // synchronous callbacks. UObjectRef's Weak GCHandle does not pin its
        // managed wrapper; the claim must never outlive this stack region.
        const auto owner = Managed(actor);
        if (!owner)
            return;
        managedOwner_ = owner;
        std::scoped_lock lock(stateMutex, cleanupMutex);
        if (sessionGeneration != session.generation || !SameIdentity(proxyIdentity, session.root) || cleanupClaim.scope)
            return; // An inactive nested scope cannot revoke the outer claim.
        cleanupClaim = {this, actorIdentity, session.root, session.generation, actor, owner, GetCurrentThreadId()};
        active_ = true;
    }
    ScopedCleanup::~ScopedCleanup()
    {
        if (active_)
        {
            std::lock_guard lock(cleanupMutex);
            if (cleanupClaim.scope == this)
                cleanupClaim = {};
        }
        if (managedEntered_)
            LeaveManagedRegion(managedCookie_, managedStack_);
        managedOwner_ = 0;
    }
    bool SetProxy(engine::WeakObject proxy)
    {
        if (!engine::IsGameThread())
            return false;
        const auto object = engine::ResolveWeak(proxy);
        if (!Ready() || !object || !engine::IsA(object, L"/Script/b1-Managed.BGUCharacterCS"))
            return false;
        std::lock_guard lock(stateMutex);
        proxyIdentity = proxy;
        ++sessionGeneration;
        children.clear();
        weaponClaims.clear();
        weaponObservations.clear();
        inputIdentity = {};
        actionCount = 0;
        movement = {};
        movementTime = {};
        rejectedSkillCount = 0;
        deathRequested.store(false, std::memory_order_release);
        return true;
    }
    void ClearProxy()
    {
        std::lock_guard lock(stateMutex);
        proxyIdentity = {};
        ++sessionGeneration;
        children.clear();
        weaponClaims.clear();
        weaponObservations.clear();
        inputIdentity = {};
        actionCount = 0;
        movement = {};
        movementTime = {};
        rejectedSkillCount = 0;
        deathRequested.store(false, std::memory_order_release);
    }
    std::vector<engine::WeakObject> OwnedChildren(bool deadOnly)
    {
        if (!Ready() || !engine::IsGameThread())
            return {};
        std::lock_guard lock(stateMutex);
        std::erase_if(children, [](const auto &child) { return !engine::WeakLifetimeCurrent(child.identity); });
        std::vector<engine::WeakObject> result;
        for (auto child = children.rbegin(); child != children.rend(); ++child)
            if ((!deadOnly || child->dead) && engine::ResolveWeak(child->identity))
                result.push_back(child->identity);
        return result;
    }
    bool SetInputOwner(engine::WeakObject originalPlayer)
    {
        if (!engine::IsGameThread())
            return false;
        const bool release = originalPlayer.index == -1 && originalPlayer.serial == 0;
        if (!release && (!Ready() || SameIdentity(originalPlayer, Identity(false)) ||
            !engine::ResolveWeak(Identity(false)) || !engine::IsPawn(engine::ResolveWeak(originalPlayer))))
            return false;
        std::lock_guard lock(stateMutex);
        inputIdentity = originalPlayer;
        actionCount = 0;
        movement = {};
        movementTime = {};
        rejectedSkillCount = 0;
        return true;
    }
    std::vector<Action> ConsumeActions()
    {
        std::lock_guard lock(stateMutex);
        std::vector<Action> result(actions.begin(), actions.begin() + actionCount);
        actionCount = 0;
        return result;
    }
    bool DeathRequested() { return deathRequested.load(std::memory_order_acquire); }
    MoveInput MovementAxes()
    {
        if (!Ready() || !engine::ResolveWeak(Identity(true)) || !engine::ResolveWeak(Identity(false)))
            return {};
        std::lock_guard lock(stateMutex);
        const auto now = GetTickCount64();
        return {now - movementTime[0] <= 250 ? movement.forward : 0.f, now - movementTime[1] <= 250 ? movement.right : 0.f};
    }
    bool SetTamerType(std::uintptr_t object, int type)
    {
        if (!Owned(object) || type < 0 || type > 3)
            return false;
        return ManagedOperation([&] {
            const auto managed = Managed(object);
            return managed && SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, int)>(methods.setTamerType), managed, type);
        });
    }
    bool Initialized(std::uintptr_t object)
    {
        if (!engine::IsA(object, L"/Script/b1-Managed.BGUCharacterCS"))
            return false;
        return ManagedOperation([&] {
            const auto managed = Managed(object);
            std::int32_t result{};
            return managed && Safe(reinterpret_cast<std::int32_t (*)(std::uintptr_t)>(methods.initialized), result, managed) && result == 1;
        });
    }
    bool CopyTeam(std::uintptr_t proxy, std::uintptr_t originalPlayer)
    {
        if (!Owned(proxy) || !engine::IsA(originalPlayer, L"/Script/b1-Managed.BGUCharacterCS"))
            return false;
        return ManagedOperation([&] {
            const auto target = Managed(proxy), source = Managed(originalPlayer);
            int team{};
            return target && source && Safe(reinterpret_cast<int (*)(std::uintptr_t)>(methods.getTeam), team, source) &&
                   SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, int)>(methods.setTeam), target, team);
        });
    }
    bool AcquirePlayerIsolation(std::uintptr_t actor, PlayerIsolation &lease)
    {
        if (!Ready() || !engine::IsGameThread() || !actor || engine::ResolveWeak(Identity(true)) != actor || !engine::ResolveWeak(Identity(false))) return false;
        auto hidden = engine::Arg::Boolean("bHidden", false);
        if (!engine::Property(actor, hidden)) return false;
        IsolationClaim claim;
        {
            std::lock_guard lock(stateMutex);
            if (lease.token || isolationClaim.token) return false;
            if (!++nextIsolationToken) ++nextIsolationToken;
            lease.token = nextIsolationToken;
            isolationClaim = {&lease, lease.token, sessionGeneration, inputIdentity};
            isolationClaim.shadowValid = true;
            isolationClaim.shadowHidden = hidden.Get<std::int32_t>() != 0;
            claim = isolationClaim;
        }
        return ManagedOperation([&] {
            std::array<std::uintptr_t, 3> callbacks{};
            if (!IsolationEvents(actor, callbacks)) return false;
            std::uintptr_t component{};
            for (std::size_t i = 0; i < isolationProperties.size(); ++i)
            {
                PropCapture capture;
                if (!DeferredProperty(claim, callbacks, i, 0, capture)) return false;
                if (component && component != capture.component) return false;
                component = capture.component;
            }
            // Hide last: movement handlers can synchronously dispatch events.
            for (const auto i : {1u, 2u, 3u, 0u})
            {
                PropertyScope phase(i == 0 ? PropertyPhase::Apply : PropertyPhase::External);
                if (!IsolationCurrent(claim) || !SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, int)>(methods.calculateProperty), component, isolationProperties[i]) ||
                    !IsolationCurrent(claim)) return false;
            }
            std::array<bool, 4> matches{};
            return IsolationState(actor, matches) && std::all_of(matches.begin(), matches.end(), [](bool value) { return value; });
        });
    }
    bool RefreshPlayerIsolation(PlayerIsolation &lease)
    {
        IsolationClaim claim;
        std::array<bool, 4> matches{};
        int matchMask = -1, property = 0;
        PropCapture capture;
        const auto failure = [&](std::string_view reason) {
            IsolationClaim current;
            std::uint64_t generation{};
            {
                std::lock_guard lock(stateMutex);
                current = isolationClaim;
                generation = sessionGeneration;
                auto &diagnostics = isolationRefreshDiagnostics;
                if (diagnostics.token != lease.token || diagnostics.generation != generation)
                    diagnostics = {lease.token, generation};
                const IsolationRefreshFailure key{reason, property, matchMask};
                const auto end = diagnostics.seen.begin() + diagnostics.count;
                if (std::find(diagnostics.seen.begin(), end, key) != end || diagnostics.count == diagnostics.seen.size()) return false;
                diagnostics.seen[diagnostics.count++] = key;
            }
            // Report each distinct branch/property/state once per lease, with
            // a hard bound even if an external system alternates every frame.
            Log(std::format("Transformation player isolation refresh rejected: reason={}, property={}, matchMask={} (bits hidden/movement/capsule/mesh; -1=unknown), token={}, claimToken={}, generation={}/{}, weak={}:{}, retired={}, uncertain={}, handles=[{},{},{},{}], deferredCount={}, deferredInvalid={}, deferredBefore={}",
                reason, property, matchMask, lease.token, current.token, current.generation, generation,
                current.actor.index, current.actor.serial, current.retired, current.uncertain,
                current.handles[0], current.handles[1], current.handles[2], current.handles[3], capture.count, capture.invalid, capture.before));
            return false;
        };
        const auto readState = [&](std::uintptr_t actor) {
            matchMask = -1;
            if (!IsolationState(actor, matches)) return false;
            matchMask = 0;
            for (std::size_t i = 0; i < matches.size(); ++i)
                if (matches[i]) matchMask |= 1 << i;
            return true;
        };
        if (!Ready()) return failure("not-ready");
        if (!engine::IsGameThread()) return failure("not-game-thread");
        if (!IsolationSnapshot(lease, claim)) return failure("lease-missing");
        if (claim.retired) return failure("retired");
        if (claim.uncertain) return failure("uncertain");
        if (claim.generation != Session().generation) return failure("generation-mismatch");
        const auto actor = engine::ResolveWeak(claim.actor);
        if (!actor) return failure("actor-invalid");
        if (!readState(actor)) return failure("state-read");
        if (std::all_of(matches.begin(), matches.end(), [](bool value) { return value; })) return true;
        std::string_view reason = "managed-transition";
        const bool refreshed = ManagedOperation([&] {
            std::array<std::uintptr_t, 3> callbacks{};
            reason = "event";
            if (!IsolationEvents(actor, callbacks)) return false;
            reason = "hidden-handle-missing";
            if (!claim.handles[0]) return false;
            reason = "deferred";
            if (!DeferredProperty(claim, callbacks, 0, claim.handles[0], capture)) return false;
            // Movement/collision handlers can synchronously reveal the actor.
            // Always recalculate hidden last, even if it matched before those
            // callbacks. Existing request order and external priority remain.
            for (const auto i : {1u, 2u, 3u, 0u})
                if (i == 0 || !matches[i])
                {
                    property = isolationProperties[i];
                    PropertyScope phase(i == 0 ? PropertyPhase::Apply : PropertyPhase::External);
                    reason = "calc-lifetime-before";
                    if (!IsolationCurrent(claim)) return false;
                    reason = "calc-invoke";
                    if (!SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, int)>(methods.calculateProperty), static_cast<std::uintptr_t>(capture.component), property)) return false;
                    reason = "calc-lifetime-after";
                    if (!IsolationCurrent(claim)) return false;
                }
            property = 0;
            reason = "post-state-read";
            if (!readState(actor)) return false;
            reason = "post-mismatch";
            if (!std::all_of(matches.begin(), matches.end(), [](bool value) { return value; })) return false;
            reason = "managed-transition";
            return true;
        });
        return refreshed || failure(reason);
    }
    bool ReleasePlayerIsolation(PlayerIsolation &lease)
    {
        if (!lease.token) return true;
        IsolationClaim claim;
        if (!engine::IsGameThread() || !IsolationSnapshot(lease, claim)) return false;
        const auto clear = [&] {
            std::lock_guard lock(stateMutex);
            if (isolationClaim.owner != &lease || isolationClaim.token != lease.token) return false;
            isolationClaim = {}; lease.token = 0; return true;
        };
        const auto actor = engine::ResolveWeak(claim.actor);
        if (!actor || claim.retired) return clear(); // Never write a reused Actor or a rebuilt ECS component.
        if (!Ready() || claim.uncertain) return false;
        if (std::none_of(claim.handles.begin(), claim.handles.end(), [](auto id) { return id != 0; })) return clear();
        const bool released = ManagedOperation([&] {
            std::array<std::uintptr_t, 3> callbacks{};
            PropCapture capture;
            if (!IsolationEvents(actor, callbacks) || !claim.handles[0] || !DeferredProperty(claim, callbacks, 0, claim.handles[0], capture, true)) return false;
            bool ok = true;
            for (std::size_t i = 0; i < claim.handles.size(); ++i)
                if (claim.handles[i])
                {
                    PropertyScope phase(i == 0 ? PropertyPhase::Release : PropertyPhase::External);
                    if (!IsolationCurrent(claim)) return false;
                    ok &= SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, std::uint32_t, std::uint32_t)>(methods.invokeResetProperty), callbacks[2], claim.handles[i], std::uint32_t{});
                    // Reset/application can synchronously retire the original
                    // ECS component. Do not use its captured address again.
                    if (!IsolationCurrent(claim)) return false;
                }
            // Reset removes bookkeeping before applying its native handler.
            // Even a retried, now-missing handle needs an explicit recalc.
            // Hidden is last so synchronous movement callbacks retain ordering.
            for (const auto i : {1u, 2u, 3u, 0u})
                if (claim.handles[i])
                {
                    PropertyScope phase(i == 0 ? PropertyPhase::Release : PropertyPhase::External);
                    if (!IsolationCurrent(claim)) return false;
                    ok &= SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, int)>(methods.calculateProperty), static_cast<std::uintptr_t>(capture.component), isolationProperties[i]);
                    if (!IsolationCurrent(claim)) return false;
                }
            return ok;
        });
        return released && clear();
    }
    namespace
    {
        // All arguments/results here are managed references and may only be
        // used inside the caller's one continuous ManagedOperation.
        WeaponReadiness InspectWeaponEvents(std::uintptr_t managedWeapon, std::uintptr_t &collection,
            std::uintptr_t &masterCallback, std::uintptr_t &activeCallback, int &initialized,
            int &masterSubscriber, int &activeSubscriber, std::string_view &stage, std::string_view &reason)
        {
            stage = "readiness"; reason = "weapon-ecs-invoke";
            std::uint8_t finished{};
            if (!methods.weaponInitialized || !Safe(reinterpret_cast<std::uint8_t (*)(std::uintptr_t)>(methods.weaponInitialized), finished, managedWeapon))
                return WeaponReadiness::Failed;
            initialized = finished != 0;
            if (!finished) { reason = "weapon-ecs-pending"; return WeaponReadiness::Pending; }
            stage = "event-get"; reason = "collection-invoke";
            if (!Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.weaponEvents), collection, managedWeapon)) return WeaponReadiness::Failed;
            reason = "collection-invalid";
            if (!Pointer(collection) || !engine::IsA(NativeAddress(collection), L"/Script/b1-Managed.BUS_GSEventCollection")) return WeaponReadiness::Failed;
            reason = "master-event-invoke";
            if (!Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.weaponMasterEvent), masterCallback, collection)) return WeaponReadiness::Failed;
            reason = "master-event-invalid";
            if (!Pointer(masterCallback) || !Read<std::uintptr_t>(masterCallback)) return WeaponReadiness::Failed;
            reason = "active-event-invoke";
            if (!Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.weaponActiveEvent), activeCallback, collection)) return WeaponReadiness::Failed;
            reason = "active-event-invalid";
            if (!Pointer(activeCallback) || !Read<std::uintptr_t>(activeCallback)) return WeaponReadiness::Failed;
            // VerifyMethods checks both complete Invoke bodies: GSDel callbacks
            // read _MultiCastDel at +0x10 and silently return if it is null.
            const auto subscriber = [](std::uintptr_t callback, int &present) {
                std::uintptr_t value{}, header{};
                if (!ReadMemory(callback + 0x10, &value, sizeof(value))) return false;
                present = value ? 1 : 0;
                return !value || (Pointer(value) && ReadMemory(value, &header, sizeof(header)) && header);
            };
            stage = "subscriber"; reason = "master-subscriber-unreadable";
            if (!subscriber(masterCallback, masterSubscriber)) return WeaponReadiness::Failed;
            reason = "active-subscriber-unreadable";
            if (!subscriber(activeCallback, activeSubscriber)) return WeaponReadiness::Failed;
            reason = "master-subscriber-empty";
            if (!masterSubscriber) return WeaponReadiness::Failed;
            reason = "active-subscriber-empty";
            if (!activeSubscriber) return WeaponReadiness::Failed;
            reason = "ready";
            return WeaponReadiness::Ready;
        }
    }
    WeaponReadiness CheckWeaponsReady(std::uintptr_t proxy, std::uintptr_t sourceConfig)
    {
        int index = -1, extent = -1, initialized = -1, master = -1, active = -1;
        std::size_t configuredCount{};
        engine::WeakObject identity;
        std::uintptr_t weapon{};
        const auto session = Session();
        const auto report = [&](WeaponReadiness state, std::string_view stage, std::string_view reason) {
            bool changed = true;
            if (index >= 0 && identity.index >= 0)
            {
                std::lock_guard lock(stateMutex);
                if (session.generation != sessionGeneration || !SameIdentity(session.root, proxyIdentity)) return;
                const auto found = std::find_if(weaponObservations.begin(), weaponObservations.end(), [&](const auto &entry) {
                    return entry.generation == session.generation && entry.index == static_cast<std::size_t>(index) && SameIdentity(entry.identity, identity);
                });
                if (found != weaponObservations.end())
                {
                    changed = found->state != state || found->initialized != initialized || found->master != master || found->active != active || found->reason != reason;
                    *found = {identity, session.generation, static_cast<std::size_t>(index), state, initialized, master, active, reason};
                }
                else weaponObservations.push_back({identity, session.generation, static_cast<std::size_t>(index), state, initialized, master, active, reason});
            }
            if (changed) Log(std::format("Transformation weapon readiness {}: stage={}, reason={}, index={}, configured={}, extent={}, proxy=0x{:x}, weapon=0x{:x}, weak={}:{}, generation={}, initialized={}, masterSubscriber={}, activeSubscriber={} (-1=unknown)",
                state == WeaponReadiness::Ready ? "ready" : state == WeaponReadiness::Pending ? "pending" : "failed", stage, reason,
                index, configuredCount, extent, proxy, weapon, identity.index, identity.serial, session.generation, initialized, master, active));
        };
        const auto fail = [&](std::string_view stage, std::string_view reason) { report(WeaponReadiness::Failed, stage, reason); return WeaponReadiness::Failed; };
        if (!Ready()) return fail("preflight", "native-not-ready");
        if (!engine::IsGameThread()) return fail("preflight", "not-game-thread");
        if (!Owned(proxy)) return fail("preflight", "proxy-not-owned");
        constexpr std::array<std::string_view, 1> path{"UnitCDesc"};
        auto configured = engine::Arg::Structs("Weapons", L"/Script/b1-Managed.UnitWeapon",
            {engine::Arg::Object("Weapon", 0), engine::Arg::Boolean("bDefaultInactive", false)});
        if (!engine::PropertyPath(sourceConfig, path, configured)) return fail("preflight", "source-weapons-read");
        configuredCount = configured.records.size();
        if (configuredCount > 256) return fail("preflight", "source-weapons-bound");
        auto count = std::array{engine::Arg::Object("Unit", proxy), engine::Arg::Data("ReturnValue", engine::Type::Int, int{}, true)};
        if (!engine::Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUGetWeaponNum", count).success) return fail("preflight", "weapon-extent-read");
        extent = count.back().Get<int>();
        if (extent < 0 || extent > 256) return fail("preflight", "weapon-extent-bound");
        bool pending{};
        for (std::size_t slot = 0; slot < configured.records.size(); ++slot)
        {
            index = static_cast<int>(slot); identity = {}; weapon = 0; initialized = master = active = -1;
            const auto &record = configured.records[slot];
            if (record.size() != 2 || record[0].type != engine::Type::Object || record[0].bytes.size() != sizeof(std::uintptr_t) ||
                record[1].type != engine::Type::Bool || record[1].bytes.size() != sizeof(std::int32_t)) return fail("preflight", "source-record-layout");
            if (index >= extent) continue;
            auto lookup = std::array{engine::Arg::Object("Owner", proxy), engine::Arg::Int("index", index), engine::Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
            if (!engine::Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUGetWeaponByIndex", lookup).success) return fail("preflight", "weapon-index-read");
            weapon = lookup.back().Get<std::uintptr_t>();
            if (!weapon) continue;
            identity = engine::MakeWeak(weapon);
            auto owner = engine::Arg::Object("Owner", 0);
            if (!record[0].Get<std::uintptr_t>()) return fail("class", "source-class-null");
            if (!engine::IsA(weapon, L"/Script/b1-Managed.BGUWeaponBase")) return fail("class", "not-weapon-base");
            if (engine::ResolveWeak(identity) != weapon) return fail("weak", "weapon-lifetime-invalid");
            if (!engine::Property(weapon, owner)) return fail("owner", "owner-read");
            if (owner.Get<std::uintptr_t>() != proxy) return fail("owner", "owner-mismatch");
            if (ActorRelation(weapon) != Relation::Child) return fail("owner", "not-owned-child");
            std::string_view stage = "managed-map", reason = "managed-region-enter";
            WeaponReadiness result = WeaponReadiness::Failed;
            const bool inspected = ManagedOperation([&] {
                const auto managedWeapon = Managed(weapon);
                reason = "proxy-or-weapon-map";
                if (!managedWeapon || !Managed(proxy)) return false;
                std::uintptr_t collection{}, masterCallback{}, activeCallback{};
                result = InspectWeaponEvents(managedWeapon, collection, masterCallback, activeCallback, initialized, master, active, stage, reason);
                return result != WeaponReadiness::Failed;
            });
            if (!inspected)
            {
                if (result != WeaponReadiness::Failed) { stage = "managed-map"; reason = "managed-region-leave"; }
                return fail(stage, reason);
            }
            if (engine::ResolveWeak(identity) != weapon || !Owned(proxy)) return fail("weak", "lifetime-changed-during-readiness");
            if (!engine::Property(weapon, owner) || owner.Get<std::uintptr_t>() != proxy) return fail("owner", "owner-changed-during-readiness");
            bool sameSession{};
            { std::lock_guard lock(stateMutex); sameSession = session.generation == sessionGeneration && SameIdentity(session.root, proxyIdentity); }
            if (!sameSession) return fail("weak", "session-changed");
            report(result, stage, reason);
            pending |= result == WeaponReadiness::Pending;
        }
        return pending ? WeaponReadiness::Pending : WeaponReadiness::Ready;
    }
    bool InitializeWeapons(std::uintptr_t proxy, std::uintptr_t sourceConfig)
    {
        // No weapon is mutated until all currently generated source slots pass.
        // In particular, an early slot must not be initialized on every retry
        // while a later weapon is still waiting for its independent ECS tick.
        if (CheckWeaponsReady(proxy, sourceConfig) != WeaponReadiness::Ready) return false;
        int diagnosticIndex = -1, diagnosticActive = -1, diagnosticBefore = -1, diagnosticAfter = -1, generated = -1;
        std::size_t configuredCount{};
        std::uintptr_t diagnosticWeapon{};
        engine::WeakObject diagnosticIdentity;
        std::uint64_t diagnosticGeneration{};
        const auto diagnostic = [&](std::string_view outcome, std::string_view stage, std::string_view reason) {
            Log(std::format("Transformation weapon initialization {}: stage={}, reason={}, index={}, configured={}, extent={}, proxy=0x{:x}, weapon=0x{:x}, weak={}:{}, generation={}, active={}, beforeHidden={}, afterHidden={} (-1=unknown)",
                outcome, stage, reason, diagnosticIndex, configuredCount, generated, proxy, diagnosticWeapon,
                diagnosticIdentity.index, diagnosticIdentity.serial, diagnosticGeneration, diagnosticActive, diagnosticBefore, diagnosticAfter));
        };
        const auto reject = [&](std::string_view stage, std::string_view reason) { diagnostic("rejected", stage, reason); return false; };
        if (!Ready()) return reject("preflight", "native-not-ready");
        if (!engine::IsGameThread()) return reject("preflight", "not-game-thread");
        if (!Owned(proxy)) return reject("preflight", "proxy-not-owned");
        const auto session = Session();
        diagnosticGeneration = session.generation;
        constexpr std::array<std::string_view, 1> path{"UnitCDesc"};
        auto configured = engine::Arg::Structs("Weapons", L"/Script/b1-Managed.UnitWeapon",
            {engine::Arg::Object("Weapon", 0), engine::Arg::Boolean("bDefaultInactive", false)});
        auto count = std::array{engine::Arg::Object("Unit", proxy), engine::Arg::Data("ReturnValue", engine::Type::Int, int{}, true)};
        if (!engine::PropertyPath(sourceConfig, path, configured)) return reject("preflight", "source-weapons-read");
        configuredCount = configured.records.size();
        if (configuredCount > 256) return reject("preflight", "source-weapons-bound");
        if (!engine::Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUGetWeaponNum", count).success)
            return reject("preflight", "weapon-extent-read");
        generated = count.back().Get<int>();
        if (generated < 0 || generated > 256)
            return reject("preflight", "weapon-extent-bound");
        Log(std::format("Transformation initial weapons: configured={}, native index extent={}", configured.records.size(), generated));
        // The native extent is max dictionary key + 1, not the number of live
        // weapons. Source indices survive failed spawns and must not compact.
        // Missing weapons are diagnostic only; never spawn a replacement or
        // make unknown/dynamic entries visible using a guessed default.
        for (std::size_t index = 0; index < configured.records.size(); ++index)
        {
            diagnosticIndex = static_cast<int>(index);
            diagnosticWeapon = 0;
            diagnosticIdentity = {};
            diagnosticActive = diagnosticBefore = diagnosticAfter = -1;
            const auto &record = configured.records[index];
            if (record.size() != 2 || record[0].type != engine::Type::Object || record[0].bytes.size() != sizeof(std::uintptr_t) ||
                record[1].type != engine::Type::Bool || record[1].bytes.size() != sizeof(std::int32_t))
                return reject("preflight", "source-record-layout");
            diagnosticActive = record[1].Get<std::int32_t>() == 0;
            if (index >= static_cast<std::size_t>(generated))
                continue;
            auto lookup = std::array{engine::Arg::Object("Owner", proxy), engine::Arg::Int("index", static_cast<int>(index)),
                engine::Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
            if (!engine::Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUGetWeaponByIndex", lookup).success)
                return reject("preflight", "weapon-index-read");
            const auto weapon = lookup.back().Get<std::uintptr_t>();
            diagnosticWeapon = weapon;
            if (!weapon)
                continue; // A source slot whose native spawn failed is a hole.
            const auto identity = engine::MakeWeak(weapon);
            diagnosticIdentity = identity;
            auto owner = engine::Arg::Object("Owner", 0);
            auto hidden = engine::Arg::Boolean("bHidden", false);
            if (!record[0].Get<std::uintptr_t>()) return reject("class", "source-class-null");
            if (!engine::IsA(weapon, L"/Script/b1-Managed.BGUWeaponBase")) return reject("class", "not-weapon-base");
            if (engine::ResolveWeak(identity) != weapon) return reject("weak", "weapon-lifetime-invalid");
            if (!engine::Property(weapon, owner)) return reject("owner", "owner-read");
            if (owner.Get<std::uintptr_t>() != proxy) return reject("owner", "owner-mismatch");
            if (ActorRelation(weapon) != Relation::Child) return reject("owner", "not-owned-child");
            if (!engine::Property(weapon, hidden)) return reject("preflight", "before-hidden-read");
            diagnosticBefore = hidden.Get<std::int32_t>() != 0;
            const bool active = record[1].Get<std::int32_t>() == 0;
            WeaponPhase phase = WeaponPhase::Prepared;
            bool currentSession{};
            {
                std::lock_guard lock(stateMutex);
                currentSession = session.generation == sessionGeneration && SameIdentity(session.root, proxyIdentity);
                const auto found = std::find_if(weaponClaims.begin(), weaponClaims.end(), [&](const auto &claim) {
                    return claim.generation == session.generation && SameIdentity(claim.identity, identity);
                });
                if (found != weaponClaims.end()) phase = found->phase;
            }
            if (!currentSession) return reject("weak", "session-changed");
            if (phase == WeaponPhase::Complete)
                continue; // Never undo an animation notify or repeat master notification.
            if (phase == WeaponPhase::Indeterminate)
                return reject("preflight", "previous-invoke-indeterminate");
            Log(std::format("Transformation weapon index {}: generated, hidden={}, default active={}", index, hidden.Get<std::int32_t>() != 0, active));
            const auto currentOwner = [&] {
                auto current = engine::Arg::Object("Owner", 0);
                return engine::ResolveWeak(identity) == weapon && Owned(proxy) && engine::Property(weapon, current) && current.Get<std::uintptr_t>() == proxy;
            };
            const auto mark = [&](WeaponPhase next) {
                std::lock_guard lock(stateMutex);
                if (session.generation != sessionGeneration || !SameIdentity(session.root, proxyIdentity)) return false;
                const auto found = std::find_if(weaponClaims.begin(), weaponClaims.end(), [&](const auto &claim) {
                    return claim.generation == session.generation && SameIdentity(claim.identity, identity);
                });
                if (found == weaponClaims.end()) weaponClaims.push_back({identity, session.generation, next});
                else found->phase = next;
                return true;
            };
            std::string_view stage = "managed-map", reason = "managed-region-enter";
            if (!ManagedOperation([&] {
                const auto managedProxy = Managed(proxy), managedWeapon = Managed(weapon);
                std::uintptr_t collection{}, masterCallback{}, activeCallback{};
                // Validate both local events before the first mutation. Keep
                // every intermediate managed reference unsafe until consumed.
                reason = "proxy-or-weapon-map";
                if (!managedProxy || !managedWeapon) return false;
                int initialized = -1, masterSubscriber = -1, activeSubscriber = -1;
                if (InspectWeaponEvents(managedWeapon, collection, masterCallback, activeCallback,
                    initialized, masterSubscriber, activeSubscriber, stage, reason) != WeaponReadiness::Ready) return false;
                if (phase != WeaponPhase::MasterAssigned)
                {
                    // Invoke can throw after executing a subscriber. Mark that
                    // boundary before the call; ambiguous work is never replayed.
                    stage = "owner"; reason = "before-master-invoke";
                    if (!currentOwner()) return false;
                    stage = "weak"; reason = "master-claim-start";
                    if (!mark(WeaponPhase::Indeterminate)) return false;
                    stage = "master-invoke"; reason = "seh";
                    if (!SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, std::uintptr_t)>(methods.invokeActor), masterCallback, managedProxy)) return false;
                    stage = "weak"; reason = "master-claim-finish";
                    if (!mark(WeaponPhase::MasterAssigned)) return false;
                }
                stage = "owner"; reason = "before-active-invoke";
                if (!currentOwner()) return false;
                // SetMaster synchronously notifies local subscribers. Re-read
                // lifecycle and events rather than use a callback retired by it.
                if (InspectWeaponEvents(managedWeapon, collection, masterCallback, activeCallback,
                    initialized, masterSubscriber, activeSubscriber, stage, reason) != WeaponReadiness::Ready) return false;
                stage = "owner"; reason = "after-master-notification";
                if (!currentOwner()) return false;
                stage = "weak"; reason = "active-claim-start";
                if (!mark(WeaponPhase::Indeterminate)) return false;
                stage = "active-invoke"; reason = "seh";
                if (!SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, std::uint8_t)>(methods.invokeBool), activeCallback, static_cast<std::uint8_t>(active))) return false;
                stage = "managed-map"; reason = "managed-region-leave";
                return true;
            })) return reject(stage, reason);
            if (!currentOwner()) return reject("owner", "after-active-invoke");
            if (!engine::Property(weapon, hidden)) return reject("post-hidden-read", "property-unreadable");
            diagnosticAfter = hidden.Get<std::int32_t>() != 0;
            if ((hidden.Get<std::int32_t>() == 0) != active) return reject("post-hidden-mismatch", "visibility-differs-from-default");
            if (!mark(WeaponPhase::Complete)) return reject("weak", "active-claim-finish");
            diagnostic("accepted", "post-hidden", "matches-default");
        }
        return true;
    }
    bool SetTarget(std::uintptr_t proxy, std::uintptr_t target)
    {
        if (!Ready() || !Owned(proxy) || (target && (!engine::IsActor(target) || ActorRelation(target) != Relation::None)))
            return false;
        return ManagedOperation([&] {
            if (!target)
            {
                const auto managedProxy = Managed(proxy);
                std::uintptr_t collection{}, callback{};
                // RPCEventCollection is a private managed auto-property, not a
                // reflected UPROPERTY. GetEventCollection returns its managed
                // instance directly; it must not enter the native->managed mapper
                // again. Only the retained proxy itself needs that mapping.
                if (!managedProxy || !Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.getEventCollection), collection, managedProxy) ||
                    !Pointer(collection) || !engine::IsA(NativeAddress(collection), L"/Script/b1-Managed.BUS_GSEventCollection"))
                    return false;
                // GSDel_Void is an ordinary managed object, not a UObject. Its
                // instance Invoke ABI takes this returned callback in rcx.
                return Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.clearTargetEvent), callback, collection) && Pointer(callback) && Read<std::uintptr_t>(callback) &&
                       SafeVoid(reinterpret_cast<void (*)(std::uintptr_t)>(methods.invokeVoid), callback);
            }
            const auto managedProxy = Managed(proxy), managedTarget = Managed(target);
            std::uintptr_t targetInfo{};
            // SetTargetInfo's local route updates target data without OnCatchTarget,
            // which would restart the default AI behavior tree and wake the unit.
            return managedProxy && managedTarget &&
                   Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t, std::uint8_t, std::uint8_t)>(methods.makeTargetInfo), targetInfo, managedTarget, std::uint8_t{}, std::uint8_t{}) && targetInfo &&
                   SafeVoid(reinterpret_cast<void (*)(bool, std::uintptr_t, std::uintptr_t)>(methods.setTargetInfo), false, managedProxy, targetInfo);
        });
    }
}
