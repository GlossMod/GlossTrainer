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

namespace gloss::gameplay::transformation::native
{
    namespace
    {
        std::atomic_bool ready{}, deathRequested{};
        std::mutex initializeMutex, stateMutex, cleanupMutex;
        engine::WeakObject proxyIdentity, inputIdentity;
        struct CleanupClaim
        {
            const ScopedCleanup *scope{};
            engine::WeakObject identity;
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
            std::uintptr_t clearTargetEvent{}, invokeVoid{}, makeTargetInfo{}, setTargetInfo{};
            std::uintptr_t actionDesc{}, firstParam{}, getAxis{}, receiveBattle{}, receiveAction{};
        } methods;
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
        bool CleanupOwns(std::uintptr_t managedOwner)
        {
            CleanupClaim claim;
            {
                std::unique_lock lock(cleanupMutex, std::try_to_lock);
                if (!lock)
                    return false;
                claim = cleanupClaim;
            }
            if (!claim.scope || claim.thread != GetCurrentThreadId() || managedOwner != claim.managedOwner ||
                !SameIdentity(Identity(false), claim.identity) || !engine::WeakLifetimeCurrent(claim.identity))
                return false;
            // The old managed owner may already have Address=0 during EndPlay.
            // A different nonzero address never belongs to this cleanup stack.
            const auto address = NativeAddress(managedOwner);
            if (address && address != claim.actor)
                return false;
            // The lifetime test above also rejects a new PendingKill UObject
            // at the same address. A live replacement has its own serial.
            const auto current = engine::MakeWeak(claim.actor);
            return current.index < 0 || current.serial <= 0 || SameIdentity(current, claim.identity);
        }
        bool OwnedComponent(std::uintptr_t component)
        {
            if (!Ready() || !engine::IsGameThread() || !Pointer(component))
                return false;
            // UActorCompBaseCS.GetOwner (Main RID DA21) proves Owner +10.
            const auto owner = Read<std::uintptr_t>(component + 0x10);
            return Owned(NativeAddress(owner)) || CleanupOwns(owner);
        }
        void Suppress(Context &context, void *)
        {
            if (OwnedComponent(context.rcx))
                context.resume = returnStub;
        }
        void OnDeath(Context &context, void *)
        {
            if (OwnedComponent(context.rcx))
            {
                deathRequested.store(true, std::memory_order_release);
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
            const auto owner = InputOwner(context);
            if (!owner)
                return;
            const auto action = static_cast<Action>(reinterpret_cast<std::uintptr_t>(user));
            const int type = action == Action::LightAttack ? 1 : action == Action::HeavyAttack ? 2 : action == Action::Dodge ? 18 : 30;
            QueueInput(context, action, action == Action::Special, type, owner);
        }
        void SkillInput(Context &context, void *)
        {
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
            const auto owner = InputOwner(context);
            int axis{};
            if (!owner || !InputParam(static_cast<int>(context.rdx), 2, axis) || axis < 0 || axis > 1)
                return;
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
        bool VerifyMethods()
        {
            methods = {};
            methods.findRef = aot::RuntimeMethod(0x168e);
            methods.setTamerType = aot::Method(0xd9b8);
            methods.initialized = aot::Method(0xd9d5);
            methods.inputOwner = aot::Method(0x11beb);
            methods.getTeam = aot::Method(0xd9d3);
            methods.setTeam = aot::Method(0xd9d2);
            methods.clearTargetEvent = aot::Method(0x3233);
            methods.invokeVoid = aot::Method(0x15219);
            methods.makeTargetInfo = aot::Method(0x108f6);
            methods.setTargetInfo = aot::Method(0x106eb);
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
                   Matches(methods.clearTargetEvent, "55 48 8B EC 48 83 EC 40 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1 48 8B 86 D8 00 00 00 81 78 18 45 01 00 00") &&
                   Matches(methods.invokeVoid, "55 48 8B EC 48 83 EC 30 48 89 75 F8 48 8B F1 48 8B 46 10 48 8B F0 48 85 C0 75 02 EB 06 48 8B CE FF 56 18") &&
                   Matches(methods.makeTargetInfo, "55 48 8B EC 48 83 EC 70 4C 89 6D E8 4C 89 75 F0 4C 89 7D F8 4C 8B F9 48 89 55 E0 4C 89 45 D8") &&
                   Matches(methods.setTargetInfo, "55 48 8B EC 48 81 EC B0 00 00 00 48 89 4D 88 48 89 55 80 4C 89 85 78 FF FF FF 48 8B CA") &&
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
    }
    void Initialize()
    {
        std::lock_guard lock(initializeMutex);
        if (Ready() || !aot::Ready() || !engine::WeakReady() || !VerifyMethods() || !MakeReturnStub())
            return;
        struct Spec { std::uint32_t rid; const char *bytes; Callback callback; void *user{}; };
        const Spec specs[]{
            {0x10367, "55 48 8B EC 48 83 EC 40 48 89 75 F8 48 8B F1 48 8B 86 98 00 00 00", Suppress},
            {0x954f, "55 48 8B EC 48 83 EC 40 48 89 7D F8 48 8B F9 48 89 55 F0 4C 89 45 E8", Suppress},
            {0xff69, "55 48 8B EC 48 81 EC D0 04 00 00 48 89 5D C8 48 89 75 D0 48 89 7D D8", OnDeath},
            {0x51df, "55 48 8B EC 48 81 EC E0 00 00 00 48 89 7D F0 4C 89 7D F8 48 8B F9", Suppress},
            {0x51e2, "55 48 8B EC 48 83 EC 50 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F1", Suppress},
            {0x11bf9, "55 48 8B EC 48 83 EC 60 48 89 75 F8 48 89 4D F0 4C 89 45 E0", Input, reinterpret_cast<void *>(Action::LightAttack)},
            {0x11bfc, "55 48 8B EC 48 83 EC 60 48 89 75 F8 48 89 4D F0 48 89 55 E8 4C 89 45 E0", Input, reinterpret_cast<void *>(Action::HeavyAttack)},
            {0x11c19, "55 48 8B EC 48 83 EC 70 48 89 75 E8 48 89 7D F0 4C 89 7D F8 48 8B F9", Input, reinterpret_cast<void *>(Action::Dodge)},
            {0x11c0e, "55 48 8B EC 48 83 EC 50 48 89 75 E8 48 89 7D F0 4C 89 75 F8 4C 8B F1 48 89 55 D8 49 8B F9", Move},
            {0x11c23, "55 48 8B EC 48 81 EC 60 01 00 00 48 89 5D C8 48 89 75 D0 48 89 7D D8 4C 89 65 E0 4C 89 6D E8", SkillInput},
            {0x11c25, "55 48 8B EC 48 83 EC 50 48 89 75 F0 48 89 7D F8 48 8B F1", Input, reinterpret_cast<void *>(Action::Special)}
        };
        for (const auto &spec : specs)
            if (!Matches(aot::Method(spec.rid), spec.bytes))
            {
                Log(std::format("Transformation native method fingerprint rejected: {:X}", spec.rid));
                return;
            }
        std::vector<std::unique_ptr<MidHook>> hooks;
        for (const auto &spec : specs)
        {
            std::string error;
            auto hook = MidHook::Create(reinterpret_cast<void *>(aot::Method(spec.rid)), spec.callback, spec.user, error);
            if (!hook)
            {
                Log(std::format("Transformation native hook {:X}: {}", spec.rid, error));
                return; // Local ownership rolls back every already-installed hook.
            }
            hooks.push_back(std::move(hook));
        }
        Hooks() = std::move(hooks);
        ready.store(true, std::memory_order_release);
        Log("Transformation native adapter: managed identity, isolation and mapped input hooks ready (experimental)");
    }
    bool Ready() { return ready.load(std::memory_order_acquire); }
    ScopedCleanup::ScopedCleanup(std::uintptr_t actor)
    {
        if (!Ready() || !engine::IsGameThread())
            return;
        const auto identity = Identity(false);
        if (!actor || engine::ResolveWeak(identity) != actor || !SameIdentity(engine::MakeWeak(actor), identity))
            return;
        const auto owner = Managed(actor);
        if (!owner)
            return;
        std::lock_guard lock(cleanupMutex);
        if (cleanupClaim.scope)
            return; // An inactive nested scope cannot revoke the outer claim.
        cleanupClaim = {this, identity, actor, owner, GetCurrentThreadId()};
        active_ = true;
    }
    ScopedCleanup::~ScopedCleanup()
    {
        if (!active_)
            return;
        std::lock_guard lock(cleanupMutex);
        if (cleanupClaim.scope == this)
            cleanupClaim = {};
    }
    bool SetProxy(engine::WeakObject proxy)
    {
        const auto object = engine::ResolveWeak(proxy);
        if (!Ready() || !object || !engine::IsA(object, L"/Script/b1-Managed.BGUCharacterCS"))
            return false;
        std::lock_guard lock(stateMutex);
        proxyIdentity = proxy;
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
        inputIdentity = {};
        actionCount = 0;
        movement = {};
        movementTime = {};
        rejectedSkillCount = 0;
        deathRequested.store(false, std::memory_order_release);
    }
    bool SetInputOwner(engine::WeakObject originalPlayer)
    {
        if (!engine::IsGameThread())
            return false;
        const bool release = originalPlayer.index == -1 && originalPlayer.serial == 0;
        if (!release && (!Ready() || !engine::ResolveWeak(Identity(false)) || !engine::IsPawn(engine::ResolveWeak(originalPlayer))))
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
        const auto managed = Owned(object) && type >= 0 && type <= 3 ? Managed(object) : 0;
        return managed && SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, int)>(methods.setTamerType), managed, type);
    }
    bool Initialized(std::uintptr_t object)
    {
        if (!engine::IsA(object, L"/Script/b1-Managed.BGUCharacterCS"))
            return false;
        const auto managed = Managed(object);
        std::int32_t result{};
        return managed && Safe(reinterpret_cast<std::int32_t (*)(std::uintptr_t)>(methods.initialized), result, managed) && result == 1;
    }
    bool CopyTeam(std::uintptr_t proxy, std::uintptr_t originalPlayer)
    {
        if (!Owned(proxy) || !engine::IsA(originalPlayer, L"/Script/b1-Managed.BGUCharacterCS"))
            return false;
        const auto target = Managed(proxy), source = Managed(originalPlayer);
        int team{};
        return target && source && Safe(reinterpret_cast<int (*)(std::uintptr_t)>(methods.getTeam), team, source) &&
               SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, int)>(methods.setTeam), target, team);
    }
    bool SetTarget(std::uintptr_t proxy, std::uintptr_t target)
    {
        if (!Ready() || !Owned(proxy) || (target && !engine::IsActor(target)))
            return false;
        if (!target)
        {
            auto event = engine::Arg::Object("RPCEventCollection", 0);
            if (!engine::Property(proxy, event))
                return false;
            const auto collection = Managed(event.Get<std::uintptr_t>());
            std::uintptr_t callback{};
            return collection && Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.clearTargetEvent), callback, collection) && callback &&
                   SafeVoid(reinterpret_cast<void (*)(std::uintptr_t)>(methods.invokeVoid), callback);
        }
        const auto managedProxy = Managed(proxy), managedTarget = Managed(target);
        std::uintptr_t targetInfo{};
        // SetTargetInfo's local route updates target data without OnCatchTarget,
        // which would restart the default AI behavior tree and wake the unit.
        return managedProxy && managedTarget &&
               Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t, std::uint8_t, std::uint8_t)>(methods.makeTargetInfo), targetInfo, managedTarget, std::uint8_t{}, std::uint8_t{}) && targetInfo &&
               SafeVoid(reinterpret_cast<void (*)(bool, std::uintptr_t, std::uintptr_t)>(methods.setTargetInfo), false, managedProxy, targetInfo);
    }
}
