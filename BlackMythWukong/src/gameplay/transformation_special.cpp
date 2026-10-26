#include "transformation_special.hpp"
#include "aot.hpp"
#include "midhook.hpp"
#include "../runtime.hpp"
#include <array>
#include <atomic>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace gloss::gameplay::transformation::special
{
    namespace
    {
        std::atomic_bool ready{};
        std::mutex initializeMutex;
        Host host;
        std::uintptr_t returnStub{};
        struct Methods
        {
            std::uintptr_t data{}, instances{}, getPaused{}, setPaused{}, getInstancePaused{}, setInstancePaused{};
        } methods;
        struct Observation
        {
            engine::WeakObject owner;
            std::uint32_t starts{}, ticks{};
        };
        // Only the game thread accesses session state. No managed object or
        // List address survives a callback/GC scope, and hooks never allocate.
        struct Session
        {
            engine::WeakObject root;
            std::uint64_t generation{};
            std::array<Observation, 64> observations{};
            std::size_t count{};
            Fault fault{Fault::None};
        } session;
        std::uint64_t nextGeneration{};
        auto &Hooks()
        {
            static auto *hooks = new std::vector<std::unique_ptr<MidHook>>;
            return *hooks;
        }
        template <class F, class R, class... A> bool Safe(F fn, R &result, A... args)
        {
            __try { result = fn(args...); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        template <class F, class... A> bool SafeVoid(F fn, A... args)
        {
            __try { fn(args...); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool Pointer(std::uintptr_t value)
        {
            return value >= 0x10000 && value <= 0x00007fffffffffff && !(value & 7);
        }
        bool Same(engine::WeakObject a, engine::WeakObject b)
        {
            return a.index >= 0 && a.serial > 0 && a.index == b.index && a.serial == b.serial;
        }
        void Fail(Fault fault)
        {
            if (session.fault == Fault::None)
                session.fault = fault;
        }
        void Increment(std::uint32_t &count)
        {
            if (count != (std::numeric_limits<std::uint32_t>::max)())
                ++count;
        }
        Observation *Observe(engine::WeakObject owner)
        {
            for (std::size_t i = 0; i != session.count; ++i)
                if (Same(session.observations[i].owner, owner))
                    return &session.observations[i];
            if (session.count == session.observations.size())
            {
                Fail(Fault::ObservationLimit);
                return nullptr;
            }
            auto &item = session.observations[session.count++];
            item.owner = owner;
            return &item;
        }
        Fault Freeze(std::uintptr_t component)
        {
            std::uintptr_t data{}, instances{};
            if (!Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.data), data, component) ||
                !Pointer(data) || !Read<std::uintptr_t>(data))
                return Fault::DataUnavailable;
            std::int32_t count{-1};
            const bool readable = Safe(reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(methods.instances), instances, data) &&
                Pointer(instances) && Read<std::uintptr_t>(instances) && ReadMemory(instances + 0x20, &count, sizeof(count)) && count >= 0;
            // List<FsmInstance> Count +20 is proved independently in CEE9 and
            // CEEA, and both full instruction sequences are validated below.
            // Do not call a generic List method without its hidden Mono ABI.
            const bool paused = SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, std::uint8_t)>(methods.setPaused), data, std::uint8_t{1});
            const bool instancePaused = SafeVoid(reinterpret_cast<void (*)(std::uintptr_t, std::uint8_t)>(methods.setInstancePaused), data, std::uint8_t{1});
            std::uint8_t p{}, ip{};
            const bool verified = paused && instancePaused &&
                Safe(reinterpret_cast<std::uint8_t (*)(std::uintptr_t)>(methods.getPaused), p, data) &&
                Safe(reinterpret_cast<std::uint8_t (*)(std::uintptr_t)>(methods.getInstancePaused), ip, data) && p == 1 && ip == 1;
            // Freeze as far as possible even after a bad count. Never Clear or
            // discard instances: original OnEndPlay owns their Shutdown.
            if (!readable) return Fault::InstancesUnreadable;
            if (count) return Fault::InstancesAlreadyStarted;
            return verified ? Fault::None : Fault::PauseFailed;
        }
        enum class Event { Startup, Pause, Tick };
        struct EventCall
        {
            Context *context{};
            Event event{};
            std::uint64_t generation{};
        };
        bool ApplyEventInner(void *argument)
        {
            auto &call = *static_cast<EventCall *>(argument);
            const auto owner = host.componentOwner(call.context->rcx);
            if (owner.index < 0 || owner.serial <= 0)
                return true;
            if (call.generation != session.generation || session.root.index < 0)
                return false;
            // Set continuation before any managed getter/setter can fail.
            // All faults are contained inside Handle rather than escaping to
            // MidHook::Dispatch, which would restore the original continuation.
            if (call.event != Event::Tick)
                call.context->resume = returnStub;
            auto *observation = Observe(owner);
            if (observation)
            {
                if (call.event == Event::Startup)
                {
                    Increment(observation->starts);
                    if (observation->starts != 1)
                        Fail(Fault::DuplicateStartup);
                }
                else if (call.event == Event::Tick)
                {
                    Increment(observation->ticks);
                    if (observation->starts != 1)
                        Fail(Fault::NotObserved);
                }
            }
            const auto fault = Freeze(call.context->rcx);
            if (fault != Fault::None)
                Fail(fault);
            // The original tick ALWAYS continues. Its final Evt_UpdateCBG is
            // required by direct skills/combos; only graph/solver flags freeze.
            return true;
        }
        bool ApplyEvent(void *argument)
        {
            // Native's GC wrapper also contains an SEH guard. Catch ordinary
            // C++ failures here so none crosses its noexcept ABI helper and
            // no continuation edited above is restored by MidHook::Dispatch.
            try { return ApplyEventInner(argument); }
            catch (...) { Fail(Fault::ManagedOperationFailed); return false; }
        }
        void Handle(Context &context, Event event) noexcept
        {
            if (!Ready() || !engine::IsGameThread() || session.root.index < 0 || !Pointer(context.rcx))
                return;
            EventCall call{&context, event, session.generation};
            try
            {
                bool completed{};
                if (!Safe(host.managedOperation, completed, ApplyEvent, static_cast<void *>(&call)) || !completed)
                    Fail(Fault::ManagedOperationFailed);
            }
            catch (...)
            {
                Fail(Fault::ManagedOperationFailed);
            }
        }
        void BeforeStartup(Context &context, void *) { Handle(context, Event::Startup); }
        void BeforePause(Context &context, void *) { Handle(context, Event::Pause); }
        void BeforeTick(Context &context, void *) { Handle(context, Event::Tick); }
        bool Matches(std::uintptr_t address, const char *bytes)
        {
            const auto pattern = Pattern::Parse(bytes);
            return address && !pattern.bytes.empty() && FindPattern(pattern, address, pattern.bytes.size()).size() == 1;
        }
        bool MakeReturnStub()
        {
            if (returnStub) return true;
            auto *memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!memory) return false;
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
        struct Spec { std::uint32_t rid; const char *bytes; Callback callback; };
        const std::array<Spec, 4> hookSpecs{{
            {0xcee6, "55 48 8B EC 48 81 EC 90 00 00 00 48 89 75 D0 48 89 7D D8 4C 89 65 E0 4C 89 6D E8 4C 89 75 F0", BeforeStartup},
            {0xceed, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 89 55 F0 48 8B C1 48 8B 80 98 00 00 00 83 38 00 0F B6 4D F0 40 88 48 48 48 8D 65 00 5D C3", BeforePause},
            {0xceee, "55 48 8B EC 48 83 EC 30 48 89 75 F0 48 89 7D F8 48 8B F9 48 8B F2 48 8B 87 98 00 00 00 83 38 00 40 88 70 49", BeforePause},
            {0xcee8, "55 48 8B EC 48 83 EC 70 48 89 75 F0 48 89 7D F8 48 8B F1 F3 0F 11 4D D0", BeforeTick}
        }};
        bool VerifyMethods()
        {
            methods = {aot::Method(0xcee1), aot::Method(0xcdf6), aot::Method(0xcdf7),
                aot::Method(0xcdf8), aot::Method(0xcdf9), aot::Method(0xcdfa)};
            const auto update = aot::Method(0xcee9), end = aot::Method(0xceea), tick = aot::Method(0xcee8);
            return Matches(methods.data, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 80 98 00 00 00 48 8D 65 00 5D C3") &&
                Matches(methods.instances, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 40 38 48 8D 65 00 5D C3") &&
                Matches(methods.getPaused, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 0F B6 40 48 48 8D 65 00 5D C3") &&
                Matches(methods.setPaused, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 89 55 F0 48 8B C1 0F B6 4D F0 40 88 48 48 48 8D 65 00 5D C3") &&
                Matches(methods.getInstancePaused, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 0F B6 40 49 48 8D 65 00 5D C3") &&
                Matches(methods.setInstancePaused, "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 89 55 F0 48 8B C1 0F B6 4D F0 40 88 48 49 48 8D 65 00 5D C3") &&
                // Independent Count layout proofs, never hooked/suppressed.
                Matches(update + 0x93, "48 8B 87 98 00 00 00 48 8B C8 83 39 00 48 8B 40 38 48 8B C8 83 39 00 48 63 40 20") &&
                Matches(end + 0x4a, "48 8B 86 98 00 00 00 48 8B C8 83 39 00 48 8B 40 38 48 8B C8 83 39 00 48 63 40 20") &&
                // Pauses guard graph updates; final CBG delegate stays outside.
                Matches(tick + 0x4d, "0F B6 40 48 48 0F B6 F8 48 8B 86 98 00 00 00 48 8B C8 83 39 00 0F B6 40 49 85 C0 0F 85 30 00 00 00") &&
                Matches(tick + 0x167, "48 8B 46 50 48 8B C8 83 38 00 E8 ? ? ? ? 48 8B C8 F3 0F 10 45 D0 F2 0F 10 C8 83 38 00 E8 ? ? ? ?");
        }
        bool InstallHooks()
        {
            std::array<std::uintptr_t, hookSpecs.size()> targets{};
            for (std::size_t i = 0; i != targets.size(); ++i)
            {
                targets[i] = aot::Method(hookSpecs[i].rid);
                if (!Matches(targets[i], hookSpecs[i].bytes))
                {
                    Log(std::format("Transformation special FSM fingerprint rejected: {:X}", hookSpecs[i].rid));
                    return false;
                }
            }
            std::vector<std::unique_ptr<MidHook>> hooks;
            hooks.reserve(targets.size());
            for (std::size_t i = 0; i != targets.size(); ++i)
            {
                std::string error;
                auto hook = MidHook::Create(reinterpret_cast<void *>(targets[i]), hookSpecs[i].callback, nullptr, error);
                if (!hook)
                {
                    Log(std::format("Transformation special FSM hook {:X}: {}", hookSpecs[i].rid, error));
                    return false;
                }
                hooks.push_back(std::move(hook));
            }
            Hooks() = std::move(hooks);
            return true;
        }
    }
    void Initialize(Host value)
    {
        std::lock_guard lock(initializeMutex);
        if (Ready() || !value.componentOwner || !value.managedOperation || !aot::Ready() || !engine::WeakReady())
            return;
        if (!VerifyMethods() || !MakeReturnStub() || !InstallHooks()) return;
        host = value;
        ready.store(true, std::memory_order_release);
        Log("Transformation special adapter: owned FSM startup isolation ready (experimental)");
    }
    bool Ready() { return ready.load(std::memory_order_acquire); }
    bool SetSession(engine::WeakObject root)
    {
        if (!Ready() || !engine::IsGameThread() || session.root.index >= 0 || root.index < 0 || root.serial <= 0 || !engine::ResolveWeak(root))
            return false;
        session = {};
        session.root = root;
        if (!++nextGeneration) ++nextGeneration;
        session.generation = nextGeneration;
        return true;
    }
    void ClearSession()
    {
        if (engine::IsGameThread()) session = {};
    }
    Status Check(engine::WeakObject root)
    {
        Status result;
        if (!engine::IsGameThread()) { result.fault = Fault::WrongThread; return result; }
        if (!Ready()) { result.fault = Fault::Unavailable; return result; }
        if (!Same(root, session.root)) { result.fault = Fault::NoSession; return result; }
        if (!engine::ResolveWeak(root)) { result.fault = Fault::InvalidIdentity; return result; }
        result.componentCount = static_cast<std::uint32_t>(session.count);
        for (std::size_t i = 0; i != session.count; ++i)
            if (Same(session.observations[i].owner, root))
            {
                result.startupCount = session.observations[i].starts;
                result.tickCount = session.observations[i].ticks;
                result.observed = result.startupCount != 0;
                break;
            }
        result.fault = session.fault != Fault::None ? session.fault : result.observed ? Fault::None : Fault::NotObserved;
        result.healthy = result.observed && result.startupCount == 1 && result.fault == Fault::None;
        return result;
    }
    std::string_view Reason(Fault fault)
    {
        switch (fault)
        {
        case Fault::None: return "已隔离自主状态图；特殊阶段与技能仍需实测";
        case Fault::Unavailable: return "特殊角色状态图隔离能力未就绪";
        case Fault::WrongThread: return "特殊角色状态检查不在游戏线程";
        case Fault::NoSession: return "特殊角色隔离会话不匹配";
        case Fault::InvalidIdentity: return "特殊角色对象身份已失效";
        case Fault::NotObserved: return "未观测到特殊角色启动前状态图隔离";
        case Fault::DuplicateStartup: return "特殊角色状态图出现重复启动请求";
        case Fault::ObservationLimit: return "特殊角色子对象数量超出隔离观测上限";
        case Fault::DataUnavailable: return "特殊角色状态图数据无法读取";
        case Fault::InstancesUnreadable: return "特殊角色状态图实例列表无法校验";
        case Fault::InstancesAlreadyStarted: return "特殊角色状态图已启动，需退出恢复";
        case Fault::PauseFailed: return "特殊角色状态图暂停标志无法校验";
        case Fault::ManagedOperationFailed: return "特殊角色托管隔离操作失败";
        }
        return "未知特殊角色隔离错误";
    }
}
