#include "../src/gameplay/transformation_special.cpp"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace special = gloss::gameplay::transformation::special;
namespace engine = gloss::gameplay::engine;
using gloss::gameplay::Context;
namespace
{
    int failures{};
#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)
    struct Component
    {
        std::array<std::uintptr_t, 24> value{};
        std::array<std::uintptr_t, 12> data{};
        std::array<std::uintptr_t, 6> instances{};
        engine::WeakObject owner;
        bool owned{true};
        Component(int index = 1, int serial = 7) : owner{index, serial}
        {
            value[0] = data[0] = instances[0] = 1;
        }
        std::uintptr_t Address() { return reinterpret_cast<std::uintptr_t>(value.data()); }
        std::uintptr_t Data() { return reinterpret_cast<std::uintptr_t>(data.data()); }
        std::uintptr_t Instances() { return reinterpret_cast<std::uintptr_t>(instances.data()); }
        void Count(int count) { std::memcpy(reinterpret_cast<std::byte *>(instances.data()) + 0x20, &count, sizeof(count)); }
        int Count() const { int count{}; std::memcpy(&count, reinterpret_cast<const std::byte *>(instances.data()) + 0x20, sizeof(count)); return count; }
        void Paused(bool paused, bool instancesPaused)
        {
            reinterpret_cast<std::uint8_t *>(data.data())[0x48] = paused;
            reinterpret_cast<std::uint8_t *>(data.data())[0x49] = instancesPaused;
        }
        bool Paused() const { return reinterpret_cast<const std::uint8_t *>(data.data())[0x48] == 1; }
        bool InstancePaused() const { return reinterpret_cast<const std::uint8_t *>(data.data())[0x49] == 1; }
    } root, child{2, 9}, other{3, 11};
    std::vector<Component *> components;
    std::map<int, int> bindings;
    DWORD gameThreadId = GetCurrentThreadId();
    bool gcUnsafe{}, failManagedBefore{}, failManagedAfter{}, ownerThrows{}, ownerCppThrows{}, blockCount{}, ignorePause{};
    int gcEntries{}, gcExits{}, getterFailure{}, setterFailure{}, setterCalls{};
    bool simulateInstall{}, aotReady{true};
    std::uintptr_t rejectedFingerprint{};
    int fingerprintChecks{}, checksAtFirstHook{}, failHookAt{-1}, hookCreateCalls{}, hookDestroyed{};
    Component *Find(std::uintptr_t address)
    {
        for (auto *component : components)
            if (component->Address() == address || component->Data() == address || component->Instances() == address)
                return component;
        return nullptr;
    }
    std::uintptr_t GetData(std::uintptr_t component)
    {
        CHECK(gcUnsafe);
        if (getterFailure == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        const auto found = Find(component);
        return found ? found->Data() : 0;
    }
    std::uintptr_t GetInstances(std::uintptr_t data)
    {
        CHECK(gcUnsafe);
        if (getterFailure == 2) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        const auto found = Find(data);
        return found ? found->Instances() : 0;
    }
    std::uint8_t GetPaused(std::uintptr_t data)
    {
        CHECK(gcUnsafe);
        if (getterFailure == 3) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        const auto found = Find(data);
        return found && found->Paused();
    }
    std::uint8_t GetInstancePaused(std::uintptr_t data)
    {
        CHECK(gcUnsafe);
        if (getterFailure == 4) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        const auto found = Find(data);
        return found && found->InstancePaused();
    }
    void SetPaused(std::uintptr_t data, std::uint8_t value)
    {
        CHECK(gcUnsafe && value == 1);
        ++setterCalls;
        if (setterFailure == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        if (!ignorePause) reinterpret_cast<std::uint8_t *>(data)[0x48] = value;
    }
    void SetInstancePaused(std::uintptr_t data, std::uint8_t value)
    {
        CHECK(gcUnsafe && value == 1);
        ++setterCalls;
        if (setterFailure == 2) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        if (!ignorePause) reinterpret_cast<std::uint8_t *>(data)[0x49] = value;
    }
    engine::WeakObject Owner(std::uintptr_t component)
    {
        CHECK(gcUnsafe);
        if (ownerThrows) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        if (ownerCppThrows) throw std::runtime_error("simulated owner allocation failure");
        const auto found = Find(component);
        return found && found->owned && engine::ResolveWeak(found->owner) ? found->owner : engine::WeakObject{};
    }
    bool ManagedOperation(special::Operation operation, void *argument)
    {
        CHECK(!gcUnsafe);
        if (failManagedBefore) return false;
        gcUnsafe = true;
        ++gcEntries;
        bool result{}, completed{};
        try { completed = special::Safe(operation, result, argument); }
        catch (...) { gcUnsafe = false; ++gcExits; throw; }
        gcUnsafe = false;
        ++gcExits;
        return completed && result && !failManagedAfter;
    }
}
namespace gloss { void Log(std::string_view) {} }
namespace gloss::gameplay
{
    bool ReadMemory(std::uintptr_t address, void *destination, std::size_t size) noexcept
    {
        if (blockCount)
            for (auto *component : components)
                if (address == component->Instances() + 0x20) return false;
        SIZE_T copied{};
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), destination, size, &copied) && copied == size;
    }
    Pattern Pattern::Parse(const char *text) { return text && *text ? Pattern{{0x55}} : Pattern{}; }
    std::vector<std::uintptr_t> FindPattern(const Pattern &, std::uintptr_t address, std::size_t, std::size_t)
    {
        ++fingerprintChecks;
        return simulateInstall && address != rejectedFingerprint ? std::vector<std::uintptr_t>{address} : std::vector<std::uintptr_t>{};
    }
    std::unique_ptr<MidHook> MidHook::Create(void *address, Callback callback, void *user, std::string &error)
    {
        if (!hookCreateCalls) checksAtFirstHook = fingerprintChecks;
        const int call = hookCreateCalls++;
        if (call == failHookAt) { error = "simulated patch failure"; return {}; }
        auto hook = std::unique_ptr<MidHook>(new MidHook);
        hook->address_ = address;
        hook->callback = callback;
        hook->user = user;
        return hook;
    }
    MidHook::~MidHook() { ++hookDestroyed; }
}
namespace gloss::gameplay::aot
{
    bool Ready() { return aotReady; }
    std::uintptr_t Method(std::uint32_t rid) { return 0x10000000ull + rid * 0x1000ull; }
}
namespace gloss::gameplay::engine
{
    bool IsGameThread() noexcept { return GetCurrentThreadId() == gameThreadId; }
    bool WeakReady() { return true; }
    std::uintptr_t ResolveWeak(WeakObject identity)
    {
        const auto it = bindings.find(identity.index);
        return it != bindings.end() && it->second == identity.serial ? 0x30000000ull + identity.index * 0x10000ull : 0;
    }
}
namespace
{
    void Reset(bool enable = true)
    {
        special::ready.store(false);
        special::Hooks().clear();
        special::session = {};
        special::host = {Owner, ManagedOperation};
        special::methods = {reinterpret_cast<std::uintptr_t>(GetData), reinterpret_cast<std::uintptr_t>(GetInstances),
            reinterpret_cast<std::uintptr_t>(GetPaused), reinterpret_cast<std::uintptr_t>(SetPaused),
            reinterpret_cast<std::uintptr_t>(GetInstancePaused), reinterpret_cast<std::uintptr_t>(SetInstancePaused)};
        special::returnStub = 0x77770000;
        root = Component{}; child = Component{2, 9}; other = Component{3, 11}; other.owned = false;
        components = {&root, &child, &other};
        bindings = {{1, 7}, {2, 9}, {3, 11}};
        gcUnsafe = failManagedBefore = failManagedAfter = ownerThrows = ownerCppThrows = blockCount = ignorePause = false;
        gcEntries = gcExits = getterFailure = setterFailure = setterCalls = 0;
        simulateInstall = false; aotReady = true; rejectedFingerprint = 0;
        fingerprintChecks = checksAtFirstHook = hookCreateCalls = hookDestroyed = 0; failHookAt = -1;
        special::ready.store(enable);
    }
    Context Frame(Component &component)
    {
        Context frame{};
        frame.rcx = component.Address(); frame.rdx = 0; frame.r8 = 128; frame.resume = 0xabcdef00;
        frame.xmm[1].f[0] = 1.f / 60.f;
        return frame;
    }
    void Start(Component &component)
    {
        auto frame = Frame(component);
        special::BeforeStartup(frame, nullptr);
        CHECK(frame.resume == special::returnStub);
    }
    void TestStartupObservationAndCbgTickContinuity()
    {
        Reset();
        CHECK(special::SetSession(root.owner));
        CHECK(!special::Check(root.owner).healthy && special::Check(root.owner).fault == special::Fault::NotObserved);
        Start(root);
        auto status = special::Check(root.owner);
        CHECK(status.observed && status.healthy && status.startupCount == 1 && status.componentCount == 1);
        CHECK(root.Paused() && root.InstancePaused() && root.Count() == 0);
        root.Paused(false, false);
        auto frame = Frame(root);
        const auto before = frame;
        special::BeforeTick(frame, nullptr);
        CHECK(root.Paused() && root.InstancePaused());
        CHECK(std::memcmp(&before, &frame, sizeof(frame)) == 0); // CBG tail and all float/register parameters continue.
        CHECK(special::Check(root.owner).healthy && special::Check(root.owner).tickCount == 1);
        CHECK(gcEntries == gcExits && !gcUnsafe);
        for (const auto &spec : special::hookSpecs)
            CHECK(spec.rid != 0xceea && spec.rid != 0xcee5); // OnAttach and Shutdown are preserved.
    }
    void TestUnrelatedActorsAndEmptySessionRemainUnchanged()
    {
        Reset();
        auto frame = Frame(root), before = frame;
        special::BeforeStartup(frame, nullptr);
        CHECK(std::memcmp(&before, &frame, sizeof(frame)) == 0 && setterCalls == 0);
        CHECK(special::SetSession(root.owner));
        frame = Frame(other); before = frame;
        special::BeforeStartup(frame, nullptr); special::BeforePause(frame, nullptr); special::BeforeTick(frame, nullptr);
        CHECK(std::memcmp(&before, &frame, sizeof(frame)) == 0 && setterCalls == 0);
        CHECK(!other.Paused() && !other.InstancePaused() && !special::Check(root.owner).observed);
        special::ClearSession();
        frame = Frame(root); before = frame;
        special::BeforeStartup(frame, nullptr);
        CHECK(std::memcmp(&before, &frame, sizeof(frame)) == 0);
    }
    void TestPauseEventsCannotUnlockAndDuplicateStartupIsLatched()
    {
        Reset(); CHECK(special::SetSession(root.owner)); Start(root);
        for (bool desired : {false, true})
        {
            root.Paused(false, false);
            auto frame = Frame(root); frame.rdx = desired;
            special::BeforePause(frame, nullptr);
            CHECK(frame.resume == special::returnStub && frame.rdx == desired);
            CHECK(root.Paused() && root.InstancePaused() && special::Check(root.owner).healthy);
        }
        CHECK(!special::SetSession(root.owner)); // Cannot erase previous startup observations.
        Start(root);
        CHECK(!special::Check(root.owner).healthy && special::Check(root.owner).fault == special::Fault::DuplicateStartup);
        root.Paused(false, false);
        auto frame = Frame(root); special::BeforeTick(frame, nullptr);
        CHECK(root.Paused() && root.InstancePaused() && special::Check(root.owner).fault == special::Fault::DuplicateStartup);
    }
    void TestExistingInstancesAreNeverClearedOrShutdownByAdapter()
    {
        Reset(); CHECK(special::SetSession(root.owner)); root.Count(2); Start(root);
        CHECK(root.Count() == 2 && root.Paused() && root.InstancePaused());
        CHECK(special::Check(root.owner).fault == special::Fault::InstancesAlreadyStarted);
        root.Count(0); auto frame = Frame(root); special::BeforeTick(frame, nullptr);
        CHECK(special::Check(root.owner).fault == special::Fault::InstancesAlreadyStarted); // Failure remains even if another system clears the list.
        special::ClearSession(); CHECK(special::SetSession(root.owner)); Start(root);
        CHECK(special::Check(root.owner).healthy);
    }
    void TestReadAndManagedFailuresKeepOwnedStartupSuppressed()
    {
        for (int stage = 1; stage <= 4; ++stage)
        {
            Reset(); CHECK(special::SetSession(root.owner)); getterFailure = stage; Start(root);
            CHECK(!special::Check(root.owner).healthy && special::Check(root.owner).observed);
            CHECK(gcEntries == gcExits && !gcUnsafe);
        }
        for (int stage = 1; stage <= 2; ++stage)
        {
            Reset(); CHECK(special::SetSession(root.owner)); setterFailure = stage; Start(root);
            CHECK(special::Check(root.owner).fault == special::Fault::PauseFailed && setterCalls == 2);
        }
        Reset(); CHECK(special::SetSession(root.owner)); blockCount = true; Start(root);
        CHECK(special::Check(root.owner).fault == special::Fault::InstancesUnreadable && root.Paused() && root.InstancePaused());
        Reset(); CHECK(special::SetSession(root.owner)); ignorePause = true; Start(root);
        CHECK(special::Check(root.owner).fault == special::Fault::PauseFailed);
        Reset(); CHECK(special::SetSession(root.owner)); failManagedAfter = true; Start(root);
        CHECK(special::Check(root.owner).fault == special::Fault::ManagedOperationFailed && !gcUnsafe);
        Reset(); CHECK(special::SetSession(root.owner)); failManagedBefore = true;
        auto frame = Frame(root); special::BeforeStartup(frame, nullptr);
        CHECK(!special::Check(root.owner).observed && special::Check(root.owner).fault == special::Fault::ManagedOperationFailed);
        CHECK(frame.resume == 0xabcdef00); // An unrecognized owner is never guessed.
        for (bool cpp : {false, true})
        {
            Reset(); CHECK(special::SetSession(root.owner)); ownerThrows = !cpp; ownerCppThrows = cpp;
            frame = Frame(root); special::BeforeStartup(frame, nullptr);
            CHECK(special::Check(root.owner).fault == special::Fault::ManagedOperationFailed && !gcUnsafe);
        }
    }
    void TestChildrenAndMissedStartupFailTheWholeSession()
    {
        Reset(); CHECK(special::SetSession(root.owner)); Start(child);
        CHECK(!special::Check(root.owner).observed && !special::Check(root.owner).healthy && special::Check(root.owner).componentCount == 1);
        Reset(); CHECK(special::SetSession(root.owner)); Start(root); Start(child);
        CHECK(special::Check(root.owner).healthy && special::Check(root.owner).componentCount == 2);
        child.Count(1); auto frame = Frame(child); special::BeforeTick(frame, nullptr);
        CHECK(special::Check(root.owner).fault == special::Fault::InstancesAlreadyStarted && frame.resume == 0xabcdef00);
        Reset(); CHECK(special::SetSession(root.owner)); Start(root);
        frame = Frame(child); special::BeforeTick(frame, nullptr);
        CHECK(special::Check(root.owner).fault == special::Fault::NotObserved && child.Paused() && child.InstancePaused());
        Reset(); CHECK(special::SetSession(root.owner));
        std::array<Component, 65> many;
        components.clear(); bindings.clear();
        for (int i = 0; i != 65; ++i)
        {
            many[i] = Component{i + 1, 7}; components.push_back(&many[i]); bindings[i + 1] = 7;
            Start(many[i]);
        }
        CHECK(special::Check(root.owner).fault == special::Fault::ObservationLimit);
    }
    void TestWeakSerialReuseAndGameThreadRequirements()
    {
        Reset(); CHECK(special::SetSession(root.owner)); Start(root);
        bindings[1] = 8;
        CHECK(special::Check(root.owner).fault == special::Fault::InvalidIdentity);
        auto frame = Frame(root), before = frame; special::BeforeStartup(frame, nullptr);
        CHECK(std::memcmp(&frame, &before, sizeof(frame)) == 0);
        root.owner.serial = 8;
        CHECK(!special::SetSession(root.owner));
        special::ClearSession(); CHECK(special::SetSession(root.owner)); Start(root);
        CHECK(special::Check(root.owner).healthy && special::Check({1, 7}).fault == special::Fault::NoSession);
        bool foreignSet{true}; special::Status foreignStatus;
        frame = Frame(root); before = frame;
        std::thread foreign([&] {
            foreignSet = special::SetSession(child.owner); foreignStatus = special::Check(root.owner);
            special::ClearSession(); special::BeforeTick(frame, nullptr);
        }); foreign.join();
        CHECK(!foreignSet && foreignStatus.fault == special::Fault::WrongThread);
        CHECK(std::memcmp(&frame, &before, sizeof(frame)) == 0 && special::Check(root.owner).healthy);
    }
    void TestCompleteFingerprintsAndTransactionalInstallation()
    {
        Reset(false); simulateInstall = true;
        special::Initialize({Owner, ManagedOperation});
        CHECK(special::Ready() && hookCreateCalls == 4 && checksAtFirstHook == 14);
        CHECK(special::Hooks().size() == 4);
        const auto hostBefore = special::host.componentOwner;
        special::Initialize({}); CHECK(special::Ready() && special::host.componentOwner == hostBefore);
        Reset(false); simulateInstall = true; aotReady = false;
        special::Initialize({Owner, ManagedOperation}); CHECK(!special::Ready() && !hookCreateCalls);
        Reset(false); simulateInstall = true; special::Initialize({});
        CHECK(!special::Ready() && !hookCreateCalls);
        for (const auto rid : {0xcee1u, 0xcdf6u, 0xcdf7u, 0xcdf8u, 0xcdf9u, 0xcdfau, 0xcee6u, 0xceedu, 0xceeeu, 0xcee8u})
        {
            Reset(false); simulateInstall = true; rejectedFingerprint = gloss::gameplay::aot::Method(rid);
            special::Initialize({Owner, ManagedOperation}); CHECK(!special::Ready() && !hookCreateCalls);
        }
        for (const auto address : {gloss::gameplay::aot::Method(0xcee9) + 0x93, gloss::gameplay::aot::Method(0xceea) + 0x4a,
                gloss::gameplay::aot::Method(0xcee8) + 0x4d, gloss::gameplay::aot::Method(0xcee8) + 0x167})
        {
            Reset(false); simulateInstall = true; rejectedFingerprint = address;
            special::Initialize({Owner, ManagedOperation}); CHECK(!special::Ready() && !hookCreateCalls);
        }
        for (int stage = 0; stage != 4; ++stage)
        {
            Reset(false); simulateInstall = true; failHookAt = stage;
            special::Initialize({Owner, ManagedOperation});
            CHECK(!special::Ready() && special::Hooks().empty() && hookCreateCalls == stage + 1 && hookDestroyed == stage);
        }
        CHECK(special::Check(root.owner).fault == special::Fault::Unavailable);
        CHECK(!special::SetSession(root.owner));
    }
}
int main()
{
    TestStartupObservationAndCbgTickContinuity();
    TestUnrelatedActorsAndEmptySessionRemainUnchanged();
    TestPauseEventsCannotUnlockAndDuplicateStartupIsLatched();
    TestExistingInstancesAreNeverClearedOrShutdownByAdapter();
    TestReadAndManagedFailuresKeepOwnedStartupSuppressed();
    TestChildrenAndMissedStartupFailTheWholeSession();
    TestWeakSerialReuseAndGameThreadRequirements();
    TestCompleteFingerprintsAndTransactionalInstallation();
    CHECK(!gcUnsafe && gcEntries == gcExits);
    std::cout << "Transformation special tests: " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
