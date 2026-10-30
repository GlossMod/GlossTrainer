// Exercise the actual adapter modules with private virtual memory and mocked
// MinHook operations. This executable never opens or attaches to the game.
#include "../src/gameplay/native_adapter.cpp"
#include <iostream>

namespace gloss {
    void Log(std::string_view) {}
    const std::filesystem::path& DataDirectory() {
        static const std::filesystem::path directory = std::filesystem::current_path();
        return directory;
    }
}

namespace {
    using namespace gloss::gameplay::native;
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)
    struct MockHook {
        void* entry{};
        std::array<unsigned char, 18> original{}, patch{};
        unsigned creates{}, enables{}, disables{};
        MH_STATUS createStatus{MH_OK}, enableStatus{MH_OK}, disableStatus{MH_OK};
    };
    std::array<MockHook, 4> mockHooks;
    std::array<MockHook, 2> costMockHooks;
    MockHook* MockFor(void* entry) {
        for (auto& mock : mockHooks) if (mock.entry == entry) return &mock;
        for (auto& mock : costMockHooks) if (mock.entry == entry) return &mock;
        return nullptr;
    }
    template<class T> void Put(Address address, T value) { std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value)); }
    void Commit(Address address) {
        CHECK(VirtualAlloc(reinterpret_cast<void*>(address & ~Address{0xfff}), 0x1000,
            MEM_COMMIT, PAGE_EXECUTE_READWRITE) != nullptr);
    }
    unsigned originalCalls{};
    std::int32_t lastCount{};
    void __fastcall MedicineOriginal(Address, NameValue, std::int32_t count) { ++originalCalls; lastCount = count; }
    void __fastcall ThrowingMedicineOriginal(Address, NameValue, std::int32_t) { ++originalCalls; throw 17; }
    void __fastcall PropsOriginal(Address, std::int32_t count, NameValue, bool) { ++originalCalls; lastCount = count; }
    void __fastcall ShopOriginal(Address, NameValue, const NameValue*, std::int32_t count) { ++originalCalls; lastCount = count; }
    float __fastcall CooldownOriginal(Address, const NameValue*) { ++originalCalls; return 2.5f; }
    enum class CostOriginalAction { None, RecycleObject, StopSession };
    CostOriginalAction costOriginalAction{};
    void __fastcall CostBodyOriginal(Address object, Address, void* result) {
        ++originalCalls;
        if (result) *static_cast<double*>(result) = 20;
        if (costOriginalAction == CostOriginalAction::RecycleObject)
            Put<std::uint32_t>(object + 0xc, Read<std::uint32_t>(object + 0xc) + 1);
        if (costOriginalAction == CostOriginalAction::StopSession) ShachiCostSessionChanged();
    }
    void __fastcall CostPropertyOriginal(Address object, Address frame, void* result) {
        ++originalCalls;
        const auto code = Read<Address>(frame + 0x20), field = Read<Address>(code);
        const auto address = object + Read<std::uint32_t>(field + 0x44);
        Put(frame + 0x20, code + 8); Put(frame + 0x30, field);
        Put(frame + 0x38, address); Put(frame + 0x40, object);
        if (result) *static_cast<double*>(result) = Read<double>(address);
    }

    void TestPrepareFailures() {
        modulePinned.store(false);
        CHECK(!PrepareEconomyHook(0)); CHECK(!PrepareCooldownHook());
        CHECK(mockHooks[0].creates == 0); CHECK(mockHooks[3].creates == 0);
        modulePinned.store(true);
        for (const auto index : {0u, 3u}) {
            auto& mock = mockHooks[index];
            static_cast<unsigned char*>(mock.entry)[0] ^= 1;
            CHECK(!(index ? PrepareCooldownHook() : PrepareEconomyHook(0)));
            CHECK(mock.creates == 0); CHECK(mock.enables == 0);
            std::memcpy(mock.entry, mock.original.data(), mock.original.size());
            mock.createStatus = MH_ERROR_MEMORY_ALLOC;
            CHECK(!(index ? PrepareCooldownHook() : PrepareEconomyHook(0)));
            CHECK(mock.creates == 1); CHECK(mock.enables == 0);
            CHECK(!(index ? cooldownHookCreated.load() : economyHooksCreated[0].load()));
            mock.createStatus = MH_OK; mock.creates = 0;
        }
    }
    void TestPrepareAndReuse() {
        CHECK(PrepareEconomyHooks()); CHECK(PrepareCooldownHook());
        CHECK(PrepareEconomyHooks()); CHECK(PrepareCooldownHook());
        for (auto& mock : mockHooks) {
            CHECK(mock.creates == 1); CHECK(mock.enables == 0); CHECK(mock.disables == 0);
            CHECK(std::memcmp(mock.entry, mock.original.data(), mock.original.size()) == 0);
        }
        for (std::size_t i = 0; i < 3; ++i) CHECK(InstallEconomyHook(i));
        CHECK(InstallCooldownHook());
        for (auto& mock : mockHooks) { CHECK(mock.creates == 1); CHECK(mock.enables == 1); }
        CHECK(RestoreExtendedHooks()); CHECK(RestoreCooldownHooks());
        for (auto& mock : mockHooks) {
            CHECK(mock.disables == 1); CHECK(std::memcmp(mock.entry, mock.original.data(), mock.original.size()) == 0);
        }
        CHECK(RestoreExtendedHooks()); CHECK(RestoreCooldownHooks());
        for (auto& mock : mockHooks) CHECK(mock.disables == 1);
        for (std::size_t i = 0; i < 3; ++i) CHECK(InstallEconomyHook(i));
        CHECK(InstallCooldownHook());
        for (auto& mock : mockHooks) { CHECK(mock.creates == 1); CHECK(mock.enables == 2); }
    }
    void TestForeignEntriesArePreserved() {
        for (auto& mock : mockHooks) static_cast<unsigned char*>(mock.entry)[1] ^= 0x40;
        CHECK(!RestoreExtendedHooks()); CHECK(!RestoreCooldownHooks());
        for (auto& mock : mockHooks) CHECK(mock.disables == 1);
        for (std::size_t i = 0; i < 3; ++i) CHECK(!InstallEconomyHook(i));
        CHECK(!InstallCooldownHook());
        for (auto& mock : mockHooks) {
            CHECK(mock.enables == 2);
            std::memcpy(mock.entry, mock.patch.data(), mock.patch.size());
        }
        CHECK(RestoreExtendedHooks()); CHECK(RestoreCooldownHooks());
        for (auto& mock : mockHooks) CHECK(mock.disables == 2);
    }
    void TestFailuresKeepState() {
        mockHooks[0].enableStatus = MH_ERROR_MEMORY_PROTECT;
        CHECK(!InstallEconomyHook(0)); CHECK(!economyHooks[0].load());
        mockHooks[0].enableStatus = MH_OK;
        CHECK(InstallEconomyHook(0));
        mockHooks[0].disableStatus = MH_ERROR_MEMORY_PROTECT;
        CHECK(!RestoreExtendedHooks()); CHECK(economyHooks[0].load());
        mockHooks[0].disableStatus = MH_OK;
        CHECK(RestoreExtendedHooks()); CHECK(!economyHooks[0].load());
        mockHooks[3].enableStatus = MH_ERROR_MEMORY_PROTECT;
        CHECK(!InstallCooldownHook()); CHECK(!cooldownHookInstalled.load());
        mockHooks[3].enableStatus = MH_OK;
        CHECK(InstallCooldownHook());
        mockHooks[3].disableStatus = MH_ERROR_MEMORY_PROTECT;
        CHECK(!RestoreCooldownHooks()); CHECK(cooldownHookInstalled.load());
        mockHooks[3].disableStatus = MH_OK;
        CHECK(RestoreCooldownHooks()); CHECK(!cooldownHookInstalled.load());
    }
    void TestPublishedOwnershipIsImmutable() {
        for (const auto index : {0u, 3u}) {
            auto& mock = mockHooks[index]; const auto before = mock.disables;
            mock.patch[2] ^= 0x20;
            CHECK(!(index ? InstallCooldownHook() : InstallEconomyHook(0)));
            CHECK(!(index ? CooldownHookOwnership() : EconomyHookOwnership(0)));
            CHECK(!(index ? RestoreCooldownHooks() : RestoreExtendedHooks()));
            CHECK(mock.disables == before);
            mock.patch[2] ^= 0x20;
            std::memcpy(mock.entry, mock.patch.data(), mock.patch.size());
            CHECK(index ? RestoreCooldownHooks() : RestoreExtendedHooks());
        }
    }
    void TestEconomyLocalScopeAndFailOpen(Address arena) {
        const Address world = arena + 0x1000, instance = arena + 0x2000, viewport = arena + 0x3000;
        const Address engine = arena + 0x4000, player = arena + 0x5000, controller = arena + 0x6000;
        const Address pawn = arena + 0x7000, attributes = arena + 0x9000, object = arena + 0xa000;
        const Address type = arena + 0xb000, entries = arena + 0xc000;
        Put(base + 0xdf741c8, engine); Put(engine + 0xc88, viewport);
        Put(viewport + 0x78, world); Put(viewport + 0x80, instance);
        Put(instance + 0x38, arena + 0xd000); Put(arena + 0xd000, player);
        Put(player + 0x30, controller); Put(controller + 0x358, pawn);
        Put<std::uint32_t>(pawn + 0xc, 17); Put(pawn + 0x758, attributes);
        Put<std::uint32_t>(attributes + 0xc, 23); Put(attributes + 0x20, pawn);
        Put<std::uint32_t>(object + 0xc, 31); Put(object + 0x10, type); Put(object + 0x20, world);
        combatBinding = {}; combatBinding.context.world = world; combatBinding.context.instance = instance;
        combatBinding.context.player = player; combatBinding.context.controller = controller; combatBinding.context.pawn = pawn;
        combatBinding.pawnIndex = 17; combatBinding.attributes = attributes; combatBinding.attributeIndex = 23;
        ItemSparseMap map; map.data = entries; map.count = map.capacity = map.numBits = 1;
        map.maxBits = 128; map.inlineBits[0] = 1;
        Put(world + 0x9e0, map); Put(entries, type); Put(entries + 8, object);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(InstallEconomyHook(i)); economySwitches[i].enabled = true;
            economyBindings[i] = {object, type, world, pawn, type, 31, 17, 0};
        }
        originalMedicineUse = &MedicineOriginal; originalPropsRemove = &PropsOriginal; originalShopSell = &ShopOriginal;
        const auto invoke = [&](std::size_t i, Address target, std::int32_t count) {
            const auto before = originalCalls;
            if (i == 0) HookMedicineUse(target, {}, count);
            else if (i == 1) HookPropsRemove(target, count, {}, true);
            else HookShopSell(target, {}, nullptr, count);
            CHECK(originalCalls == before + 1); CHECK(callbacksInFlight.load() == 0);
        };
        CHECK(CombatBindingCurrent());
        for (std::size_t i = 0; i < 3; ++i) {
            invoke(i, object, 3); CHECK(lastCount == 0);
            invoke(i, object + 8, 3); CHECK(lastCount == 3);
            invoke(i, object, -2); CHECK(lastCount == -2);
            stopping.store(true); invoke(i, object, 3); CHECK(lastCount == 3); stopping.store(false);
            faulted.store(true); invoke(i, object, 3); CHECK(lastCount == 3); faulted.store(false);
            Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId() + 1);
            invoke(i, object, 3); CHECK(lastCount == 3);
            Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId());
            Put<std::uint32_t>(object + 0xc, 32); invoke(i, object, 3); CHECK(lastCount == 3);
            Put<std::uint32_t>(object + 0xc, 31);
            Put(entries + 8, object + 8); invoke(i, object, 3); CHECK(lastCount == 3); Put(entries + 8, object);
            Put<Address>(world + 0x9e0, 1); invoke(i, object, 3); CHECK(lastCount == 3); Put(world + 0x9e0, map);
            // A reserved, uncommitted map pointer exercises a guarded failed
            // read. The query still calls the original exactly once.
            Put(world + 0x9e0, base + 0x1000); invoke(i, object, 3); CHECK(lastCount == 3); Put(world + 0x9e0, map);
            static_cast<unsigned char*>(mockHooks[i].entry)[1] ^= 0x40;
            invoke(i, object, 3); CHECK(lastCount == 3);
            std::memcpy(mockHooks[i].entry, mockHooks[i].patch.data(), mockHooks[i].patch.size());
            economySwitches[i].enabled = false; invoke(i, object, 3); CHECK(lastCount == 3);
        }
        // Exceptions from the game original are not re-run or swallowed by the
        // trainer's query guard; the callback count still unwinds correctly.
        originalMedicineUse = &ThrowingMedicineOriginal;
        const auto before = originalCalls;
        bool caught{};
        try { HookMedicineUse(object, {}, 3); } catch (int value) { caught = value == 17; }
        CHECK(caught); CHECK(originalCalls == before + 1); CHECK(callbacksInFlight.load() == 0);
        originalMedicineUse = &MedicineOriginal;
        CHECK(RestoreExtendedHooks());
    }

    void TestCostHookLifecycle() {
        modulePinned.store(false);
        CHECK(!PrepareShachiCostHooks());
        modulePinned.store(true);
        static_cast<unsigned char*>(costMockHooks[0].entry)[0] ^= 1;
        CHECK(!PrepareShachiCostHooks()); CHECK(costMockHooks[0].creates == 0);
        std::memcpy(costMockHooks[0].entry, costMockHooks[0].original.data(), costMockHooks[0].original.size());
        costMockHooks[0].createStatus = MH_ERROR_MEMORY_ALLOC;
        CHECK(!PrepareShachiCostHooks()); CHECK(!shachiCostHookCreated[0].load());
        costMockHooks[0].createStatus = MH_OK;
        costMockHooks[1].createStatus = MH_ERROR_MEMORY_ALLOC;
        CHECK(!PrepareShachiCostHooks()); CHECK(shachiCostHookCreated[0].load()); CHECK(!shachiCostHookCreated[1].load());
        const auto firstCreates = costMockHooks[0].creates;
        costMockHooks[1].createStatus = MH_OK;
        CHECK(PrepareShachiCostHooks()); CHECK(PrepareShachiCostHooks());
        CHECK(costMockHooks[0].creates == firstCreates); CHECK(costMockHooks[1].creates == 2);
        costMockHooks[1].enableStatus = MH_ERROR_MEMORY_PROTECT;
        CHECK(!InstallShachiCostHooks()); CHECK(shachiCostHookInstalled[0].load()); CHECK(!shachiCostHookInstalled[1].load());
        CHECK(!shachiCostActive.load()); CHECK(ShachiCostCleanup());
        costMockHooks[1].enableStatus = MH_OK;
        CHECK(InstallShachiCostHooks()); CHECK(ShachiCostHooksOwned());
        for (std::size_t i = 0; i < costMockHooks.size(); ++i) {
            auto& mock = costMockHooks[i];
            const auto before = mock.disables;
            static_cast<unsigned char*>(mock.entry)[1] ^= 0x40;
            CHECK(!ShachiCostCleanup()); CHECK(mock.disables == before);
            CHECK(!InstallShachiCostHooks());
            std::memcpy(mock.entry, mock.patch.data(), mock.patch.size());
            CHECK(ShachiCostCleanup()); CHECK(InstallShachiCostHooks());
        }
        costMockHooks[0].disableStatus = MH_ERROR_MEMORY_PROTECT;
        CHECK(!ShachiCostCleanup()); CHECK(shachiCostHookInstalled[0].load());
        costMockHooks[0].disableStatus = MH_OK;
        CHECK(ShachiCostCleanup()); CHECK(ShachiCostCleanup());
        const auto disables = costMockHooks[0].disables;
        costMockHooks[0].patch[2] ^= 0x20;
        CHECK(!InstallShachiCostHooks()); CHECK(!ShachiCostHookOwnership(0));
        CHECK(!ShachiCostCleanup()); CHECK(costMockHooks[0].disables == disables);
        costMockHooks[0].patch[2] ^= 0x20;
        std::memcpy(costMockHooks[0].entry, costMockHooks[0].patch.data(), costMockHooks[0].patch.size());
        CHECK(ShachiCostCleanup()); CHECK(InstallShachiCostHooks());
    }

    void TestCostOutputsAndSessionScope(Address arena) {
        const auto& context = combatBinding.context;
        const auto pawn = context.pawn, attributes = combatBinding.attributes;
        const Address component = arena + 0xe000, ability = arena + 0x10000, type = arena + 0x12000;
        const Address getter = arena + 0x13000, actorInfo = arena + 0x14000, frame = arena + 0x15000;
        const Address initial = arena + 0x16000, constant = initial + 0x100, unrelated = initial + 0x200, code = arena + 0x17000;
        Put<std::uint32_t>(context.world + 0xc, 3); combatBinding.context.worldIndex = 3;
        Put(pawn + 0x750, component); Put<std::uint32_t>(component + 0xc, 31);
        Put(component + 0x3f8, pawn); Put(component + 0x400, pawn);
        Put(ability + 0x10, type); Put<std::uint32_t>(ability + 0xc, 44);
        Put(ability + 0x328, actorInfo); Put<double>(ability + 0x650, 20); Put<double>(ability + 0x658, 30);
        Put<std::uint32_t>(type + 0xc, 42); Put<std::uint32_t>(type + 0xa8, 0x800);
        Put<std::uint32_t>(getter + 0xc, 43); Put(frame + 0x10, getter);
        Put<std::uint32_t>(initial + 0x44, 0x650); Put<std::uint32_t>(constant + 0x44, 0x658);
        Put<std::uint32_t>(unrelated + 0x44, 0x650);
        Put<std::uint32_t>(actorInfo + 0x10, 17); Put<std::uint32_t>(actorInfo + 0x14, 1); Put(actorInfo + 0x18, pawn);
        Put<std::uint32_t>(actorInfo + 0x20, 31); Put<std::uint32_t>(actorInfo + 0x24, 1); Put(actorInfo + 0x28, component);
        const auto arm = [&](double multiplier) {
            shachiCostContracts = {type, getter, type, initial, constant, 42, 43, 42};
            CHECK(shachiCostPreference.Commit(ShachiCostIdentity(context), multiplier, true, true));
            shachiCostActive.store(true);
        };
        const auto body = [&](void* result) {
            const auto before = originalCalls;
            HookShachiCostBody(ability, frame, result);
            CHECK(originalCalls == before + 1); CHECK(callbacksInFlight.load() == 0);
        };
        const auto property = [&](Address field, void* result) {
            Put(frame + 0x20, code); Put(code, field);
            const auto before = originalCalls;
            HookShachiCostProperty(ability, frame, result);
            CHECK(originalCalls == before + 1); CHECK(callbacksInFlight.load() == 0);
        };
        CHECK(ShachiCostIdentityCurrent(ShachiCostIdentity(context)));
        CHECK(ShachiCostWeakObject(actorInfo + 0x10) == pawn);
        CHECK(ShachiCostWeakObject(actorInfo + 0x20) == component);
        arm(3);
        double output{};
        body(&output); CHECK(output == 60);
        CHECK(Read<double>(ability + 0x650) == 20);
        body(reinterpret_cast<void*>(ability + 0x650)); CHECK(Read<double>(ability + 0x650) == 20);
        body(nullptr); // Reference-only evaluation still executes the original.
        arm(0.5);
        property(initial, &output); CHECK(output == 10); CHECK(Read<double>(ability + 0x650) == 20);
        property(constant, &output); CHECK(output == 15); CHECK(Read<double>(ability + 0x658) == 30);
        property(initial, reinterpret_cast<void*>(ability + 0x650)); CHECK(Read<double>(ability + 0x650) == 20);
        property(initial, nullptr); CHECK(Read<double>(ability + 0x650) == 20);
        property(unrelated, &output); CHECK(output == 20);
        Put<std::uint32_t>(actorInfo + 0x10, 18); body(&output); CHECK(output == 20);
        Put<std::uint32_t>(actorInfo + 0x10, 17);
        Put(actorInfo + 0x18, pawn + 8); body(&output); CHECK(output == 20); Put(actorInfo + 0x18, pawn);
        Put<std::uint32_t>(ability + 8, 0x10); body(&output); CHECK(output == 20); Put<std::uint32_t>(ability + 8, 0);
        stopping.store(true); body(&output); CHECK(output == 20); stopping.store(false);
        faulted.store(true); body(&output); CHECK(output == 20); faulted.store(false);
        Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId() + 1); body(&output); CHECK(output == 20);
        Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId()); CHECK(LocalGameThread());
        static_cast<unsigned char*>(costMockHooks[0].entry)[1] ^= 0x40;
        body(&output); CHECK(output == 20);
        std::memcpy(costMockHooks[0].entry, costMockHooks[0].patch.data(), costMockHooks[0].patch.size());
        costOriginalAction = CostOriginalAction::RecycleObject;
        body(&output); CHECK(output == 20); Put<std::uint32_t>(ability + 0xc, 44); costOriginalAction = CostOriginalAction::None;
        Put<std::uint32_t>(attributes + 0xc, 24); body(&output); CHECK(output == 20); Put<std::uint32_t>(attributes + 0xc, 23);
        auto call = CaptureShachiCostInvocation(ability, frame, &output, false); CHECK(call.selected);
        ShachiCostSessionChanged(); output = 20; ApplyShachiCostInvocation(call, &output); CHECK(output == 20);
        arm(2); ApplyShachiCostInvocation(call, &output); CHECK(output == 20); // Old epoch after re-enable.
        costOriginalAction = CostOriginalAction::StopSession; body(&output); CHECK(output == 20);
        costOriginalAction = CostOriginalAction::None;
        arm(2);
        // A read-only temporary can be read by the guard but must fail open on
        // write; the failed binding stops rather than retrying every VM call.
        const auto protectedOutput = arena + 0x18000;
        Put<double>(protectedOutput, 20);
        call = CaptureShachiCostInvocation(ability, frame, reinterpret_cast<void*>(protectedOutput), false); CHECK(call.selected);
        DWORD protection{};
        CHECK(VirtualProtect(reinterpret_cast<void*>(protectedOutput), 0x1000, PAGE_READONLY, &protection));
        ApplyShachiCostInvocation(call, reinterpret_cast<void*>(protectedOutput));
        CHECK(Read<double>(protectedOutput) == 20); CHECK(shachiCostPreference.uncertain); CHECK(!shachiCostActive.load());
        DWORD ignored{}; CHECK(VirtualProtect(reinterpret_cast<void*>(protectedOutput), 0x1000, protection, &ignored));
        body(&output); CHECK(output == 20);
        arm(0); body(&output); CHECK(output == 0);
        CHECK(ShachiCostCleanup());
        CHECK(originalShachiCostVm[0] != nullptr && originalShachiCostVm[1] != nullptr);
        body(&output); CHECK(output == 20); property(initial, &output); CHECK(output == 20); // Late entrants.
    }

    void TestFlightActionsBecomeReadyWithoutSessionRebind() {
        using namespace gloss::gameplay;
        auto& backend = GetBackend();
        backend.SetRuntimeState(false, "test reset", "delayed-flight");
        backend.SetRuntimeState(true, "session bound before movement components", "delayed-flight");
        const auto generation = backend.GetSnapshot().generation;
        const auto ready = [&](std::string_view id) {
            const auto snapshot = backend.GetSnapshot();
            return std::any_of(snapshot.capabilities.begin(), snapshot.capabilities.end(),
                [&](const auto& capability) { return capability.featureId == id && capability.ready; });
        };
        RefreshExtendedCapabilities({"flight", false, false, "waiting for movement components"});
        for (const auto id : {"flight_up", "flight_down"}) {
            CHECK(!ready(id)); CHECK(!backend.Submit(id, {"100"}).accepted);
        }
        // The periodic flight state becomes known in the same session; both
        // default hotkey actions must now reach the executor without rebinding.
        RefreshExtendedCapabilities({"flight", true, true, "verified flight contract", {"600"}});
        CHECK(ready("flight_on"));
        for (const auto id : {"flight_up", "flight_down"}) {
            CHECK(ready(id));
            const auto submitted = backend.Submit(id, {"100"});
            CHECK(submitted.accepted);
            unsigned calls{};
            CHECK(backend.Pump([&](const Request& request) {
                ++calls;
                CHECK(request.featureId == id);
                CHECK(request.arguments == std::vector<std::string>{id, "100"});
                return ExecutionResult{RequestState::Verified, "mock flight step"};
            }, 1) == 1);
            CHECK(calls == 1);
        }
        CHECK(backend.GetSnapshot().generation == generation);
        // Losing the verified contract revokes a previously published action;
        // queued work is cancelled before reaching the native executor.
        CHECK(backend.Submit("flight_up", {"100"}).accepted);
        RefreshExtendedCapabilities({"flight", false, false, "movement contract changed"});
        for (const auto id : {"flight_up", "flight_down"}) {
            CHECK(!ready(id)); CHECK(!backend.Submit(id, {"100"}).accepted);
        }
        unsigned calls{};
        CHECK(backend.Pump([&](const Request&) {
            ++calls;
            return ExecutionResult{RequestState::Verified, "must not dispatch"};
        }, 1) == 0);
        CHECK(calls == 0);
        CHECK(backend.GetSnapshot().generation == generation);
        backend.SetRuntimeState(false, "test complete", "delayed-flight");
    }
}

extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID entry, LPVOID, LPVOID* original) {
    auto* mock = MockFor(entry);
    if (!mock) return MH_ERROR_NOT_EXECUTABLE;
    ++mock->creates;
    if (mock->createStatus == MH_OK) {
        if (mock == &costMockHooks[0]) *original = reinterpret_cast<void*>(&CostBodyOriginal);
        else if (mock == &costMockHooks[1]) *original = reinterpret_cast<void*>(&CostPropertyOriginal);
        else *original = reinterpret_cast<void*>(&CooldownOriginal);
    }
    return mock->createStatus;
}
extern "C" MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID entry) {
    auto* mock = MockFor(entry);
    if (!mock) return MH_ERROR_NOT_CREATED;
    ++mock->enables;
    if (mock->enableStatus == MH_OK) std::memcpy(entry, mock->patch.data(), mock->patch.size());
    return mock->enableStatus;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID entry) {
    auto* mock = MockFor(entry);
    if (!mock) return MH_ERROR_NOT_CREATED;
    ++mock->disables;
    if (mock->disableStatus == MH_OK) std::memcpy(entry, mock->original.data(), mock->original.size());
    return mock->disableStatus;
}
extern "C" const char* WINAPI MH_StatusToString(MH_STATUS) { return "mock"; }

int main() {
    auto* memory = VirtualAlloc(nullptr, 0xe100000, MEM_RESERVE, PAGE_NOACCESS);
    CHECK(memory != nullptr);
    if (!memory) return 1;
    base = reinterpret_cast<Address>(memory); modulePinned.store(true);
    Commit(base + GameThreadIdRva); Commit(base + 0xdf741c8);
    Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId());
    for (std::size_t i = 0; i < mockHooks.size(); ++i) {
        const auto rva = i < 3 ? EconomyRvas[i] : SwitchCooldownQueryRva;
        Commit(base + rva); auto& mock = mockHooks[i]; mock.entry = reinterpret_cast<void*>(base + rva);
        if (i < 3) std::copy(EconomyEntries[i].begin(), EconomyEntries[i].end(), mock.original.begin());
        else std::copy(SwitchCooldownEntry.begin(), SwitchCooldownEntry.end(), mock.original.begin());
        mock.patch = mock.original; mock.patch[0] = 0xe9; mock.patch[1] = static_cast<unsigned char>(i + 1);
        std::memcpy(mock.entry, mock.original.data(), mock.original.size());
    }
    for (std::size_t i = 0; i < costMockHooks.size(); ++i) {
        Commit(base + ShachiCostVmRvas[i]); auto& mock = costMockHooks[i];
        mock.entry = reinterpret_cast<void*>(base + ShachiCostVmRvas[i]); mock.original = ShachiCostVmEntries[i];
        mock.patch = mock.original; mock.patch[0] = 0xe9; mock.patch[1] = static_cast<unsigned char>(i + 8);
        std::memcpy(mock.entry, mock.original.data(), mock.original.size());
    }
    Commit(base + 0x16730b0);
    std::memcpy(reinterpret_cast<void*>(base + 0x16730b0), ShachiCostWeakEntry.data(), ShachiCostWeakEntry.size());
    // Same verified weak-resolver prefix, then return this private test weak
    // record's pointer payload. Serial>0/index>=0 keep both prefix branches untaken.
    constexpr std::array<unsigned char, 5> weakReturn{0x48,0x8b,0x41,0x08,0xc3};
    std::memcpy(reinterpret_cast<void*>(base + 0x16730b0 + ShachiCostWeakEntry.size()), weakReturn.data(), weakReturn.size());
    auto* arena = VirtualAlloc(nullptr, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(arena != nullptr);
    TestPrepareFailures(); TestPrepareAndReuse(); TestForeignEntriesArePreserved();
    TestFailuresKeepState(); TestPublishedOwnershipIsImmutable();
    if (arena) TestEconomyLocalScopeAndFailOpen(reinterpret_cast<Address>(arena));
    TestCostHookLifecycle();
    if (arena) TestCostOutputsAndSessionScope(reinterpret_cast<Address>(arena));
    TestFlightActionsBecomeReadyWithoutSessionRebind();
    if (arena) VirtualFree(arena, 0, MEM_RELEASE);
    VirtualFree(memory, 0, MEM_RELEASE);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
