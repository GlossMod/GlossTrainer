#include "../src/gameplay/transformation_native.cpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <thread>

namespace
{
    struct Object
    {
        std::array<std::uintptr_t, 24> managed{};
        std::array<std::uintptr_t, 8> reference{};
        std::array<std::uintptr_t, 4> native{};
        int team{}, tamer{};
        Object()
        {
            managed[0] = reference[0] = native[0] = 1;
            managed[0x58 / 8] = reinterpret_cast<std::uintptr_t>(native.data());
            reference[0x10 / 8] = reinterpret_cast<std::uintptr_t>(managed.data());
            reference[0x20 / 8] = reinterpret_cast<std::uintptr_t>(native.data());
        }
        std::uintptr_t Address() { return reinterpret_cast<std::uintptr_t>(native.data()); }
        std::uintptr_t Managed() { return reinterpret_cast<std::uintptr_t>(managed.data()); }
    } proxy, player, other;
    struct Binding { int serial; Object *object; bool pendingKill{}; };
    std::map<int, Binding> bindings;
    DWORD gameThreadId = GetCurrentThreadId();
    int failures{}, setterCalls{}, targetCalls{}, clearTargetCalls{}, inputParameter{};
    float inputAxis{};
    bool battleAllowed{true};
    std::array<std::uintptr_t, 5> descriptor{};
    std::array<std::uintptr_t, 4> parameters{};
    std::uintptr_t inputManaged{};
#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)
    Object *Lookup(std::uintptr_t address)
    {
        for (auto *object : {&proxy, &player, &other})
            if (object->Address() == address || object->Managed() == address)
                return object;
        return nullptr;
    }
    std::uintptr_t FindRef(std::uintptr_t address)
    {
        auto *object = Lookup(address);
        return object ? reinterpret_cast<std::uintptr_t>(object->reference.data()) : 0;
    }
}
namespace gloss
{
    void Log(std::string_view) {}
}
namespace gloss::gameplay
{
    bool ReadMemory(std::uintptr_t address, void *data, std::size_t size) noexcept
    {
        SIZE_T copied{};
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), data, size, &copied) && copied == size;
    }
    bool WriteMemory(std::uintptr_t address, const void *data, std::size_t size) noexcept
    {
        SIZE_T copied{};
        return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), data, size, &copied) && copied == size;
    }
    Pattern Pattern::Parse(const char *) { return {}; }
    std::vector<std::uintptr_t> FindPattern(const Pattern &, std::uintptr_t, std::size_t, std::size_t) { return {}; }
    std::unique_ptr<MidHook> MidHook::Create(void *, Callback, void *, std::string &) { return {}; }
    MidHook::~MidHook() {}
}
namespace gloss::gameplay::aot
{
    bool Ready() { return false; }
    std::uintptr_t Method(std::uint32_t) { return 0; }
    std::uintptr_t RuntimeMethod(std::uint32_t) { return 0; }
}
namespace gloss::gameplay::engine
{
    bool IsGameThread() noexcept { return GetCurrentThreadId() == gameThreadId; }
    bool WeakReady() { return true; }
    bool IsA(std::uintptr_t object, const wchar_t *) noexcept { return Lookup(object) != nullptr; }
    bool IsPawn(std::uintptr_t object) noexcept { return Lookup(object) != nullptr; }
    bool IsActor(std::uintptr_t object) noexcept { return Lookup(object) != nullptr; }
    std::uintptr_t ResolveWeak(WeakObject weak)
    {
        const auto it = bindings.find(weak.index);
        return it != bindings.end() && !it->second.pendingKill && it->second.serial == weak.serial ? it->second.object->Address() : 0;
    }
    WeakObject MakeWeak(std::uintptr_t object)
    {
        for (const auto &[index, binding] : bindings)
            if (!binding.pendingKill && binding.object->Address() == object)
                return {index, binding.serial};
        return {};
    }
    bool WeakLifetimeCurrent(WeakObject weak)
    {
        const auto it = bindings.find(weak.index);
        return it != bindings.end() && it->second.serial == weak.serial;
    }
    bool Property(std::uintptr_t object, Arg &arg, bool)
    {
        if (object != proxy.Address() || std::strcmp(arg.name, "RPCEventCollection"))
            return false;
        arg = Arg::Object("RPCEventCollection", other.Address());
        return true;
    }
}
namespace
{
    namespace native = gloss::gameplay::transformation::native;
    using gloss::gameplay::Context;
    using gloss::gameplay::transformation::Action;
    void Reset()
    {
        proxy = Object{}; player = Object{}; other = Object{};
        // Rebind self-references after value assignment from the temporary.
        for (auto *object : {&proxy, &player, &other})
        {
            object->managed[0x58 / 8] = object->Address();
            object->reference[0x10 / 8] = object->Managed();
            object->reference[0x20 / 8] = object->Address();
        }
        bindings = {{1, {7, &proxy}}, {2, {9, &player}}, {3, {11, &other}}};
        native::ClearProxy();
        native::ready.store(true);
        native::returnStub = 0x12345678;
        native::methods.findRef = reinterpret_cast<std::uintptr_t>(FindRef);
        native::methods.setTamerType = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t address, int value) { ++setterCalls; Lookup(address)->tamer = value; });
        native::methods.initialized = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t) { return 1; });
        native::methods.inputOwner = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t) { return inputManaged; });
        native::methods.getTeam = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t address) { return Lookup(address)->team; });
        native::methods.setTeam = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t address, int value) { Lookup(address)->team = value; });
        native::methods.receiveBattle = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t, bool) { return battleAllowed ? 1 : 0; });
        native::methods.receiveAction = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t, int) { return battleAllowed ? 1 : 0; });
        native::methods.actionDesc = reinterpret_cast<std::uintptr_t>(+[](int id) { return id == 42 ? reinterpret_cast<std::uintptr_t>(descriptor.data()) : 0; });
        native::methods.firstParam = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value, int index) {
            CHECK(value == reinterpret_cast<std::uintptr_t>(parameters.data()) && index == 0);
            return inputParameter;
        });
        native::methods.getAxis = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t) { return inputAxis; });
        native::methods.clearTargetEvent = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) { CHECK(value == other.Managed()); return other.Managed(); });
        native::methods.invokeVoid = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) { CHECK(value == other.Managed()); ++clearTargetCalls; });
        native::methods.makeTargetInfo = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t target, std::uint8_t source, std::uint8_t way) {
            CHECK(target == other.Managed() && source == 0 && way == 0);
            return other.Managed();
        });
        native::methods.setTargetInfo = reinterpret_cast<std::uintptr_t>(+[](bool online, std::uintptr_t owner, std::uintptr_t info) {
            CHECK(!online && owner == proxy.Managed() && info == other.Managed()); ++targetCalls;
        });
        setterCalls = 0;
        targetCalls = clearTargetCalls = 0;
        descriptor = {};
        parameters = {};
        descriptor[0x20 / 8] = reinterpret_cast<std::uintptr_t>(parameters.data());
        parameters[0x18 / 8] = 2;
        inputParameter = 0;
        inputAxis = 0;
        battleAllowed = true;
        inputManaged = player.Managed();
        CHECK(native::SetProxy({1, 7}));
    }
    Context ComponentContext(Object &owner, std::array<std::uintptr_t, 4> &component)
    {
        component = {1, 0, owner.Managed(), 0};
        Context result{};
        result.rcx = reinterpret_cast<std::uintptr_t>(component.data());
        result.resume = 0x7777;
        return result;
    }
    void TestIsolationAndWeakSerialReuse()
    {
        Reset();
        std::array<std::uintptr_t, 4> component;
        auto owned = ComponentContext(proxy, component);
        native::Suppress(owned, nullptr);
        CHECK(owned.resume == native::returnStub);
        auto unrelated = ComponentContext(other, component);
        native::OnDeath(unrelated, nullptr);
        CHECK(unrelated.resume == 0x7777);
        CHECK(!native::DeathRequested());
        auto death = ComponentContext(proxy, component);
        native::OnDeath(death, nullptr);
        CHECK(death.resume == native::returnStub);
        CHECK(native::DeathRequested());
        native::ClearProxy();
        CHECK(!native::DeathRequested());
        CHECK(native::SetProxy({1, 7}));
        bindings[1].serial = 8; // Same native address, a different UObject lifetime.
        auto reused = ComponentContext(proxy, component);
        native::Suppress(reused, nullptr);
        CHECK(reused.resume == 0x7777);
        CHECK(!native::SetProxy({1, 7}));
    }
    void TestManagedIdentityAndOwnedWrites()
    {
        Reset();
        CHECK(native::SetTamerType(proxy.Address(), 3));
        CHECK(proxy.tamer == 3);
        CHECK(setterCalls == 1);
        CHECK(!native::SetTamerType(player.Address(), 3));
        CHECK(!native::SetTamerType(proxy.Address(), 99));
        CHECK(setterCalls == 1);
        CHECK(native::Initialized(proxy.Address()));
        player.team = 4;
        CHECK(native::CopyTeam(proxy.Address(), player.Address()));
        CHECK(proxy.team == 4);
        CHECK(!native::CopyTeam(other.Address(), player.Address()));
        CHECK(native::SetTarget(proxy.Address(), other.Address()));
        CHECK(targetCalls == 1 && clearTargetCalls == 0);
        CHECK(native::SetTarget(proxy.Address(), 0));
        CHECK(targetCalls == 1 && clearTargetCalls == 1);
        CHECK(!native::SetTarget(other.Address(), 0));
        proxy.reference[0x20 / 8] = other.Address();
        CHECK(!native::SetTamerType(proxy.Address(), 1));
        CHECK(!native::Initialized(proxy.Address()));
        CHECK(setterCalls == 1);
    }
    void TestSynchronousCleanupScopeAndReuse()
    {
        Reset();
        std::array<std::uintptr_t, 4> component;
        {
            native::ScopedCleanup unrelated(other.Address());
            CHECK(!unrelated);
        }
        {
            native::ScopedCleanup cleanup(proxy.Address());
            CHECK(static_cast<bool>(cleanup));
            bindings[1].pendingKill = true;
            CHECK(gloss::gameplay::engine::ResolveWeak({1, 7}) == 0);
            auto pending = ComponentContext(proxy, component);
            native::Suppress(pending, nullptr);
            CHECK(pending.resume == native::returnStub);
            auto unrelated = ComponentContext(other, component);
            native::Suppress(unrelated, nullptr);
            CHECK(unrelated.resume == 0x7777);
            {
                native::ScopedCleanup nested(proxy.Address());
                CHECK(!nested); // PendingKill cannot open a new claim.
            }
            pending = ComponentContext(proxy, component);
            proxy.managed[0x58 / 8] = 0; // Managed detach can happen before callbacks finish.
            native::Suppress(pending, nullptr);
            CHECK(pending.resume == native::returnStub);
            proxy.managed[0x58 / 8] = proxy.Address();
            std::thread foreign([&] {
                std::array<std::uintptr_t, 4> foreignComponent;
                auto outsideThread = ComponentContext(proxy, foreignComponent);
                native::Suppress(outsideThread, nullptr);
                CHECK(outsideThread.resume == 0x7777);
            });
            foreign.join();
        }
        auto after = ComponentContext(proxy, component);
        native::Suppress(after, nullptr);
        CHECK(after.resume == 0x7777); // No raw address claim survives the stack scope.
        Reset();
        {
            native::ScopedCleanup cleanup(proxy.Address());
            CHECK(static_cast<bool>(cleanup));
            ++bindings[1].serial; // New valid UObject, same native and managed addresses.
            auto reused = ComponentContext(proxy, component);
            native::Suppress(reused, nullptr);
            CHECK(reused.resume == 0x7777);
            bindings[1].pendingKill = true; // Same rejection if replacement is also dying.
            reused.resume = 0x7777;
            native::Suppress(reused, nullptr);
            CHECK(reused.resume == 0x7777);
        }
        native::ScopedCleanup stale(proxy.Address());
        CHECK(!stale);
        Reset();
        {
            native::ScopedCleanup cleanup(proxy.Address());
            CHECK(static_cast<bool>(cleanup));
            bindings[1].pendingKill = true;
            native::ClearProxy();
            auto cancelled = ComponentContext(proxy, component);
            native::Suppress(cancelled, nullptr);
            CHECK(cancelled.resume == 0x7777);
        }
    }
    void TestMappedActionFilteringAndCancellation()
    {
        Reset();
        CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        frame.context.rcx = 0x123000;
        frame.context.r8 = 2;
        frame.context.resume = 0x7777;
        native::Input(frame.context, reinterpret_cast<void *>(Action::LightAttack));
        auto actions = native::ConsumeActions();
        CHECK(actions.size() == 1 && actions[0] == Action::LightAttack);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 1);
        frame.context.r8 = 5; // Release is consumed without adding a new attack.
        native::Input(frame.context, reinterpret_cast<void *>(Action::HeavyAttack));
        CHECK(native::ConsumeActions().empty());
        frame.context.r8 = 2;
        std::uintptr_t prediction = 1;
        CHECK(gloss::gameplay::Write(frame.context.rsp() + 0x28, prediction));
        native::Input(frame.context, reinterpret_cast<void *>(Action::HeavyAttack));
        CHECK(native::ConsumeActions().empty());
        CHECK(frame.context.rax == 0);
        prediction = 0;
        CHECK(gloss::gameplay::Write(frame.context.rsp() + 0x28, prediction));
        inputManaged = other.Managed();
        frame.context.resume = 0x7777;
        native::Input(frame.context, reinterpret_cast<void *>(Action::Dodge));
        CHECK(frame.context.resume == 0x7777);
        CHECK(native::ConsumeActions().empty());
        inputManaged = player.Managed();
        native::Input(frame.context, reinterpret_cast<void *>(Action::Dodge));
        native::ClearProxy();
        CHECK(native::ConsumeActions().empty());
        frame.context.resume = 0x7777;
        native::Input(frame.context, nullptr);
        CHECK(frame.context.resume == 0x7777);
    }
    void TestMappedSkillSlotsAndUnknownActions()
    {
        Reset();
        CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        frame.context.rdx = 42;
        frame.context.r8 = 2;
        for (int spell = 1; spell <= 4; ++spell)
        {
            inputParameter = spell;
            frame.context.resume = 0x7777;
            native::SkillInput(frame.context, nullptr);
            const auto queued = native::ConsumeActions();
            CHECK(queued.size() == 1 && queued[0] == static_cast<Action>(static_cast<int>(Action::Skill1) + spell - 1));
            CHECK(frame.context.resume == native::returnStub);
        }
        inputParameter = 0; // Default spell can replace/destroy the retained pawn.
        frame.context.resume = 0x7777;
        native::SkillInput(frame.context, nullptr);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 0 && native::ConsumeActions().empty());
        inputParameter = 9; // Ride must not reach the original transformation.
        frame.context.resume = 0x7777;
        native::SkillInput(frame.context, nullptr);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 0 && native::ConsumeActions().empty());
        inputParameter = 4;
        frame.context.rdx = 7; // Descriptor lookup failed.
        frame.context.resume = 0x7777;
        native::SkillInput(frame.context, nullptr);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 0 && native::ConsumeActions().empty());
        frame.context.rdx = 42;
        frame.context.r8 = 0; // UseSkillByType normalizes Triggered to Started.
        native::SkillInput(frame.context, nullptr);
        CHECK(native::ConsumeActions().size() == 1);
        battleAllowed = false;
        native::SkillInput(frame.context, nullptr);
        CHECK(native::ConsumeActions().empty() && frame.context.rax == 0);
    }
    void TestReleaseInputPreservesProxyIsolation()
    {
        Reset();
        CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        frame.context.r8 = 2;
        native::Input(frame.context, reinterpret_cast<void *>(Action::LightAttack));
        CHECK(native::actionCount == 1);
        native::movement = {.5f, -.75f};
        native::movementTime = {GetTickCount64(), GetTickCount64()};
        native::rejectedSkillCount = 1;
        CHECK(native::SetInputOwner({}));
        CHECK(native::actionCount == 0 && native::ConsumeActions().empty());
        CHECK(native::movement.forward == 0 && native::movement.right == 0);
        CHECK(native::movementTime[0] == 0 && native::movementTime[1] == 0 && native::rejectedSkillCount == 0);
        frame.context.resume = 0x7777;
        native::Input(frame.context, reinterpret_cast<void *>(Action::LightAttack));
        CHECK(frame.context.resume == 0x7777 && native::ConsumeActions().empty());
        std::array<std::uintptr_t, 4> component;
        auto isolation = ComponentContext(proxy, component);
        native::Suppress(isolation, nullptr);
        CHECK(isolation.resume == native::returnStub);
        const auto identity = native::Identity(false);
        CHECK(identity.index == 1 && identity.serial == 7);

        CHECK(native::SetInputOwner({2, 9}));
        bool foreignRelease = true;
        std::thread foreign([&] { foreignRelease = native::SetInputOwner({}); });
        foreign.join();
        CHECK(!foreignRelease);
        CHECK(native::Identity(true).index == 2 && native::Identity(true).serial == 9);
        CHECK(!native::SetInputOwner({2, 8})); // Nonempty stale identities do not release input.
        native::ready.store(false);
        CHECK(native::SetInputOwner({})); // Release only needs the game thread.
        native::ready.store(true);
        native::ClearProxy();
        CHECK(native::SetInputOwner({})); // Idempotent after full session cleanup.
    }
    void TestMappedMovementExpiryAndRelease()
    {
        Reset();
        CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        std::uintptr_t managedValue = 0x123000;
        frame.context.r9 = reinterpret_cast<std::uintptr_t>(&managedValue);
        frame.context.rdx = 42;
        frame.context.r8 = 0;
        inputParameter = 0; inputAxis = .75f;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().forward == .75f);
        inputParameter = 1; inputAxis = -.5f;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().right == -.5f);
        frame.context.r8 = 5;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().right == 0.f);
        native::movementTime[0] = GetTickCount64() - 251;
        CHECK(native::MovementAxes().forward == 0.f);
        frame.context.r8 = 0;
        battleAllowed = false;
        inputParameter = 0;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().forward == 0.f);
        battleAllowed = true;
        inputAxis = (std::numeric_limits<float>::quiet_NaN)();
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().forward == 0.f);
        inputAxis = 3.f;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().forward == 1.f);
        parameters[0x18 / 8] = 1; // Movement requires exactly two descriptor ints.
        frame.context.resume = 0x7777;
        native::Move(frame.context, nullptr);
        CHECK(frame.context.resume == 0x7777);
        native::ClearProxy();
        CHECK(native::MovementAxes().forward == 0.f && native::MovementAxes().right == 0.f);
    }
}
int main()
{
    TestIsolationAndWeakSerialReuse();
    TestManagedIdentityAndOwnedWrites();
    TestSynchronousCleanupScopeAndReuse();
    TestMappedActionFilteringAndCancellation();
    TestMappedSkillSlotsAndUnknownActions();
    TestReleaseInputPreservesProxyIsolation();
    TestMappedMovementExpiryAndRelease();
    std::cout << "Transformation native tests: " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
