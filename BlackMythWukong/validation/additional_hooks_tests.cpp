// Exercise actual callback policy with synthetic managed objects. This never
// opens the game or invokes a managed game method. Include the implementation
// to keep private hook callbacks out of the production public interface.
#include "../src/gameplay/additional_hooks.cpp"
#include <future>
#include <iostream>
#include <limits>

namespace {
int failures{};
std::uintptr_t player{};
gloss::features::Result completed;
int completions{};
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; ++failures; } } while(false)
struct alignas(16) Object {
    std::array<std::byte, 0x500> memory{};
    std::uintptr_t address() { return reinterpret_cast<std::uintptr_t>(memory.data()); }
    template<class T> void set(std::size_t offset, T value) { std::memcpy(memory.data() + offset, &value, sizeof(value)); }
    template<class T> T get(std::size_t offset) const { T value{}; std::memcpy(&value, memory.data() + offset, sizeof(value)); return value; }
};
using gloss::features::Id;
using gloss::gameplay::Context;
using namespace gloss::gameplay::extra;

struct Fixture {
    Object actor, component, data, bag, gamePlayer, rpc, channel, controller;
    Object nativePawn, nativeController, rootData, roleData, roleCs;
    Fixture() {
        player = actor.address();
        component.set(0x10, player);
        component.set(0x98, data.address());
        actor.set(0x58, nativePawn.address());
        controller.set(0x58, nativeController.address());
        bag.set(0x20, gamePlayer.address());
        gamePlayer.set(0xb8, rpc.address());
        gamePlayer.set(0x140, static_cast<unsigned char>(1));
        rpc.set(0x3d0, channel.address());
        channel.set(0x10, controller.address());
        inventory = {0x58, 0xb8, 0x3d0, 0x10};
        inventoryVerified = true;
        CaptureNativePlayer(nativePawn.address(), nativeController.address());
        for (auto& control : controls) { control.enabled = false; control.supported = true; }
        medicines.actor = 0; medicines.size = 0;
        removal.pending = false;
        itemReturn = 0x140111005;
        cycle.pending = false; cycleSupported = true;
        completions = 0;
    }
    void liveCycle() {
        gamePlayer.set(0x20, bag.address());
        gamePlayer.set(0x130, 0);
        gamePlayer.set(0x90, rootData.address());
        rootData.set(0x10, roleData.address());
        roleData.set(0x18, roleCs.address());
        roleCs.set(0x28, data.address());
        bag.set(0x10, data.address());
        bag.set(0x30, static_cast<unsigned char>(0));
        data.set(0x28, 1);
    }
};
struct CostFrame {
    std::array<std::uintptr_t, 32> words{};
    Context context{};
    explicit CostFrame(Fixture& fixture) {
        context.rbp = reinterpret_cast<std::uintptr_t>(&words[16]);
        words[4] = fixture.bag.address(); words[17] = itemReturn;
        context.r15 = 100; context.rdi = 3; context.rax = 99;
        context.r13 = fixture.data.address(); context.resume = 42;
        fixture.data.set(0x1c, 99);
    }
};
void TestDefaultAndSupport() {
    for (auto& control : controls) { control.enabled = false; control.supported = false; }
    cycleSupported = false;
    CHECK(!Supported(Id::InfiniteItems));
    CHECK(!Enabled(Id::InfiniteItems));
    CHECK(!gloss::gameplay::extra::SetEnabled(Id::InfiniteItems, true).success);
    CHECK(!gloss::gameplay::extra::SetEnabled(Id::Attack, true).success);
    CHECK(!SetCycle(2).success);
}
void TestMedicineScopeAndRestoration() {
    Fixture f;
    Context c{};
    c.rsi = f.component.address(); c.rax = f.data.address();
    f.data.set(0xa8, 1234); f.data.set(0xb4, static_cast<unsigned char>(33));
    c.xmm[0].f[0] = 10.f; c.xmm[1].f[0] = .2f;
    CHECK(gloss::gameplay::extra::SetEnabled(Id::InfiniteBuffDuration, true).success);
    BuffCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == .2f); // unclassified buff
    RememberMedicine(1234, player);
    BuffCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == 0.f);
    c.xmm[1].f[0] = .2f; f.data.set(0xb4, static_cast<unsigned char>(19));
    BuffCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == .2f); // vessel source
    f.data.set(0xb4, static_cast<unsigned char>(33));
    f.component.set(0x10, f.controller.address());
    BuffCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == .2f); // foreign owner
    f.component.set(0x10, player);
    c.xmm[0].f[0] = -1.f;
    BuffCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == .2f); // infinite/reset sentinel
    c.xmm[0].f[0] = std::numeric_limits<float>::quiet_NaN();
    BuffCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == .2f);
    c.xmm[0].f[0] = 10.f;
    ForgetMedicine(1234, player); // same ID now applied by wine/hulu
    BuffCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == .2f);
    RememberMedicine(1234, player);
    CHECK(gloss::gameplay::extra::SetEnabled(Id::InfiniteBuffDuration, false).success);
    BuffCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == .2f);
    CHECK(f.data.get<int>(0xa8) == 1234); // policy never writes duration/data
}
void TestMedicineCallerClassification() {
    Fixture f;
    struct Stack {
        Context context;
        std::uintptr_t arguments[8]{};
        std::uintptr_t frames[4]{};
    } stack{};
    auto& c = stack.context;
    medicineReturn = 0x140123456;
    stack.arguments[6] = 33;
    stack.frames[1] = medicineReturn;
    c.rbp = reinterpret_cast<std::uintptr_t>(stack.frames);
    c.rcx = f.component.address(); c.rdx = 4567; c.r8 = player; c.r9 = player;
    CHECK(MedicineCallStack(c));
    CaptureMedicine(c, nullptr); CHECK(Medicine(4567, player));
    stack.frames[1] = medicineReturn + 1;
    CaptureMedicine(c, nullptr); CHECK(!Medicine(4567, player)); // hulu/other caller
    stack.frames[0] = reinterpret_cast<std::uintptr_t>(stack.frames); // cycle
    CHECK(!MedicineCallStack(c));
    c.rbp = f.data.address(); // readable heap is not a thread call frame
    CHECK(!MedicineCallStack(c));
}
void TestVesselCastOnlyAndDisable() {
    Fixture f;
    Context c{}; c.rsi = f.component.address(); c.rax = f.data.address();
    c.xmm[0].f[0] = 5000.f; c.xmm[1].f[0] = 16.f;
    f.data.set(0x38, 2);
    CHECK(gloss::gameplay::extra::SetEnabled(Id::InfiniteVesselDuration, true).success);
    VesselCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == 0.f);
    c.xmm[1].f[0] = 16.f; f.data.set(0x38, 3);
    VesselCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == 16.f); // cooldown
    f.data.set(0x38, 2); c.xmm[0].f[0] = -1.f;
    VesselCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == 16.f);
    c.xmm[0].f[0] = 5000.f; f.component.set(0x98, f.bag.address());
    VesselCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == 16.f); // stale equipment data
    f.component.set(0x98, f.data.address());
    CHECK(gloss::gameplay::extra::SetEnabled(Id::InfiniteVesselDuration, false).success);
    VesselCountdown(c, nullptr); CHECK(c.xmm[1].f[0] == 16.f);
}
void TestLocalInventoryAndAsynchronousRemoval() {
    Fixture f;
    CHECK(LocalBag(f.bag.address(), player));
    CHECK(gloss::gameplay::extra::SetEnabled(Id::InfiniteItems, true).success);
    CostFrame frame(f); auto& c = frame.context;
    ItemCost(c, nullptr); CHECK(c.rdi == 0); CHECK(c.rax == 99 && c.resume == 42);
    PermitRemoval(100, 3);
    auto result = std::async(std::launch::async, [&] {
        CostFrame asynchronousFrame(f);
        ItemCost(asynchronousFrame.context, nullptr); return asynchronousFrame.context;
    }).get();
    CHECK(result.rdi == 3 && result.rax == 99); // cross-thread exact allowance
    c.rdi = 3; ItemCost(c, nullptr);
    CHECK(c.rdi == 0); // consumed once
    f.gamePlayer.set(0x140, static_cast<unsigned char>(0));
    c.rdi = 3; ItemCost(c, nullptr); CHECK(c.rdi == 3);
    f.gamePlayer.set(0x140, static_cast<unsigned char>(1));
    f.controller.set(0x58, f.nativePawn.address());
    ItemCost(c, nullptr); CHECK(c.rdi == 3); // different controller
    f.controller.set(0x58, f.nativeController.address());
    f.actor.set(0x58, f.nativeController.address());
    ItemCost(c, nullptr); CHECK(c.rdi == 3); // managed/native player mismatch
    f.actor.set(0x58, f.nativePawn.address());
    frame.words[17] = itemReturn + 1; // sell/other helper callsite
    ItemCost(c, nullptr); CHECK(c.rdi == 3);
    frame.words[17] = itemReturn;
    c.rax = 2; f.data.set(0x1c, 2); // never convert insufficient stock to success
    ItemCost(c, nullptr); CHECK(c.rdi == 3);
    c.rax = 99; f.data.set(0x1c, 99);
    CHECK(gloss::gameplay::extra::SetEnabled(Id::InfiniteItems, false).success);
    ItemCost(c, nullptr); CHECK(c.rdi == 3);
    PermitRemoval(100, 3);
    ItemCost(c, nullptr); CHECK(c.rdi == 3 && !removal.pending); // removal while feature is off
    CHECK(gloss::gameplay::extra::SetEnabled(Id::InfiniteItems, true).success);
    ItemCost(c, nullptr); CHECK(c.rdi == 0); // reopening cannot reuse that allowance
    c.rdi = 3;
    {
        std::lock_guard lock(removal.mutex);
        ItemCost(c, nullptr); CHECK(c.rdi == 3); // contention keeps normal game semantics
    }
    CHECK(!LocalBag(0x12340, player)); // invalid but aligned pointer safely rejected
}
void TestRemovalTimeAndMatching() {
    Fixture f;
    PermitRemoval(200, 4);
    const auto stamp = removal.time;
    CHECK(!ConsumeRemoval(201, 4, player, stamp));
    CHECK(!ConsumeRemoval(200, 3, player, stamp));
    CHECK(!ConsumeRemoval(200, 4, player, stamp - 1));
    CHECK(removal.pending);
    CHECK(ConsumeRemoval(200, 4, player, stamp + 30000));
    CHECK(!ConsumeRemoval(200, 4, player, stamp + 30000));
    PermitRemoval(200, 4);
    CHECK(!ConsumeRemoval(200, 4, player, removal.time + 30001));
    CHECK(!removal.pending);
    PermitRemoval(200, 4);
    CHECK(!ConsumeRemoval(200, 4, f.controller.address(), removal.time));
    CHECK(!removal.pending);
}
void TestCycleLiveIdentityAndOneBasedWrite() {
    Fixture f; f.liveCycle();
    CHECK(LocalHelper(f.bag.address(), player));
    CHECK(!SetCycle(0).success);
    CHECK(!SetCycle(10001).success);
    CHECK(SetCycle(2).success);
    CHECK(!SetCycle(3).success);
    std::array<std::uintptr_t, 2> frame{f.bag.address(), 0};
    Context c{}; c.rbp = reinterpret_cast<std::uintptr_t>(&frame[1]); c.rax = f.data.address();
    f.bag.set(0x30, static_cast<unsigned char>(1));
    CycleGetter(c, nullptr); CHECK(f.data.get<int>(0x28) == 1 && cycle.pending);
    f.bag.set(0x30, static_cast<unsigned char>(0));
    f.roleCs.set(0x28, f.rpc.address());
    CycleGetter(c, nullptr); CHECK(f.data.get<int>(0x28) == 1 && cycle.pending); // transaction clone
    f.roleCs.set(0x28, f.data.address());
    CycleGetter(c, nullptr); CHECK(f.data.get<int>(0x28) == 2 && !cycle.pending);
    CHECK(completions == 1 && completed.success);
    f.data.set(0x28, 4); CycleGetter(c, nullptr); CHECK(f.data.get<int>(0x28) == 4); // not permanent
    CHECK(SetCycle(1).success); CycleGetter(c, nullptr);
    CHECK(f.data.get<int>(0x28) == 1); // first cycle stores one, not zero
    CHECK(SetCycle(5).success);
    ExpireCycle(cycle.time - 1); CHECK(cycle.pending);
    ExpireCycle(cycle.time + 30001); CHECK(!cycle.pending && !completed.success);
}
void TestNativeLifecycleInvalidatesPendingState() {
    Fixture f; f.liveCycle();
    RememberMedicine(1234, player);
    PermitRemoval(100, 3);
    CHECK(SetCycle(2).success);
    const auto epoch = playerEpoch.load();
    CaptureNativePlayer(f.nativePawn.address(), f.nativeController.address());
    CHECK(playerEpoch == epoch && Medicine(1234, player) && removal.pending && cycle.pending);
    CaptureNativePlayer(0, 0);
    CHECK(playerEpoch != epoch && !Medicine(1234, player) && !removal.pending && !cycle.pending);
    CHECK(completions == 1 && !completed.success);
    CaptureNativePlayer(f.nativePawn.address(), f.nativeController.address());
    CHECK(!Medicine(1234, player) && !removal.pending && !cycle.pending); // same addresses reused
    CHECK(!ConsumeRemoval(100, 3, player, GetTickCount64()));
    // A busy state mutex cannot preserve an old request across the epoch:
    // the next consumer rejects it even before a later frame clears storage.
    PermitRemoval(100, 3);
    {
        std::lock_guard lock(removal.mutex);
        playerEpoch.fetch_add(1);
    }
    CHECK(!ConsumeRemoval(100, 3, player, GetTickCount64()) && !removal.pending);
}
}
namespace gloss { void Log(std::string_view) {} }
namespace gloss::features {
void CompleteAction(Id, Result result) { completed = std::move(result); ++completions; }
}
namespace gloss::gameplay::attributes {
std::uintptr_t PlayerActor() noexcept { return player; }
bool HasPlayer() noexcept { return player != 0; }
}
namespace gloss::gameplay::aot {
bool Ready() { return false; }
std::uintptr_t Method(std::uint32_t) { return 0; }
std::uintptr_t ScriptMethod(std::uint32_t) { return 0; }
std::uintptr_t RuntimeMethod(std::uint32_t) { return 0; }
std::uintptr_t ProtoMethod(std::uint32_t) { return 0; }
}
int main() {
    TestDefaultAndSupport();
    TestMedicineScopeAndRestoration();
    TestMedicineCallerClassification();
    TestVesselCastOnlyAndDisable();
    TestLocalInventoryAndAsynchronousRemoval();
    TestRemovalTimeAndMatching();
    TestCycleLiveIdentityAndOneBasedWrite();
    TestNativeLifecycleInvalidatesPendingState();
    std::cout << (failures ? "FAIL " : "PASS ") << "additional gameplay hook policy (" << failures << " failure(s))\n";
    return failures ? 1 : 0;
}
