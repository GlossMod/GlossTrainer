// Exercise the actual adapter modules with private virtual memory and mocked
// MinHook operations. This executable never opens or attaches to the game.
#include "../src/gameplay/native_adapter.cpp"
#include <iostream>
#include <thread>

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
    MockHook scriptVerifyMock;
    MockHook* MockFor(void* entry) {
        for (auto& mock : mockHooks) if (mock.entry == entry) return &mock;
        for (auto& mock : costMockHooks) if (mock.entry == entry) return &mock;
        if (scriptVerifyMock.entry == entry) return &scriptVerifyMock;
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
    unsigned scriptVerifyOriginalCalls{};
    bool __fastcall ScriptVerifyOriginalMock(const StringValue*, const ScriptBytes*) { ++scriptVerifyOriginalCalls; return false; }
    bool __fastcall ThrowingScriptVerifyOriginal(const StringValue*, const ScriptBytes*) { ++scriptVerifyOriginalCalls; throw 17; }
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
        const Address propRow = arena + 0xe000, propEntries = arena + 0xf000;
        const Address propTable = base + ItemTables[0].tableRva + 8;
        const NameValue protectedItem{83, 0};
        Put(base + 0xdfe9278, engine); Put(engine + 0xc88, viewport);
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
        const auto invoke = [&](std::size_t i, Address target, std::int32_t count, NameValue item = NameValue{83, 0}) {
            const auto before = originalCalls;
            if (i == 0) HookMedicineUse(target, {}, count);
            else if (i == 1) HookPropsRemove(target, count, item, true);
            else HookShopSell(target, {}, nullptr, count);
            CHECK(originalCalls == before + 1); CHECK(callbacksInFlight.load() == 0);
        };
        CHECK(CombatBindingCurrent());
        // No prepared whitelist means fail open even for a candidate identifier.
        protectedProps.clear(); protectedPropTable = {}; inventoryEditDepth = 0;
        invoke(1, object, 3); CHECK(lastCount == 3);
        invoke(1, object, 3, {}); CHECK(lastCount == 3);
        Commit(propTable);
        std::memset(reinterpret_cast<void*>(propRow), 0, 856);
        std::memset(reinterpret_cast<void*>(propEntries), 0, 24);
        ItemSparseMap propMap = map; propMap.data = propEntries;
        Put(propTable, propMap); Put(propEntries, protectedItem); Put(propEntries + 8, propRow);
        Put(propRow + 8, protectedItem);
        Put<std::uint8_t>(propRow + 0x24c, 0);
        Put<std::uint8_t>(propRow + 0x26c, 1);
        Put<std::uint8_t>(propRow + 0x274, 0);
        protectedPropTable = propMap;
        protectedProps.push_back({protectedItem, propRow, 0});
        invoke(1, object, 3); CHECK(lastCount == 0);
        invoke(1, object, 3, {}); CHECK(lastCount == 3);
        invoke(1, object, 3, NameValue{83, 1}); CHECK(lastCount == 3);
        // Intentional quantity edits bypass protection for this call/thread only.
        inventoryEditDepth = 1; invoke(1, object, 3); CHECK(lastCount == 3); inventoryEditDepth = 0;
        invoke(1, object, 3); CHECK(lastCount == 0);
        for (const auto classification : {std::uint8_t{1}, std::uint8_t{3}, std::uint8_t{7}, std::uint8_t{255}}) {
            Put(propRow + 0x274, classification); invoke(1, object, 3); CHECK(lastCount == 3);
        }
        Put<std::uint8_t>(propRow + 0x274, 0);
        for (const auto offset : {0x244, 0x280, 0x300, 0x328}) {
            Put<std::uint64_t>(propRow + offset, 1); invoke(1, object, 3); CHECK(lastCount == 3);
            Put<std::uint64_t>(propRow + offset, 0);
        }
        Put<std::uint32_t>(propTable + 16, 0); invoke(1, object, 3); CHECK(lastCount == 3); Put(propTable, propMap);
        Put(propEntries + 8, propRow + 8); invoke(1, object, 3); CHECK(lastCount == 3); Put(propEntries + 8, propRow);
        Put(propRow + 8, NameValue{84, 0}); invoke(1, object, 3); CHECK(lastCount == 3); Put(propRow + 8, protectedItem);
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
        protectedProps.clear(); protectedPropTable = {}; inventoryEditDepth = 0;
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

    void TestNativeActionsRejectStaleTargets(Address arena) {
        const auto dispatchedBefore = dispatchedCalls.load();
        CHECK(Contract("GM_SetHealth") == nullptr);
        CHECK(Contract("GM_FullHeal") == nullptr);
        CHECK(Contract("UpgradeMedicine") == nullptr);
        for (const auto& historical : ExpectedFunctions) CHECK(Contract(historical.name) == nullptr);
        Context context;
        CHECK(!LocationReady(context));
        CHECK(ReadLocation(context).state == gloss::gameplay::RequestState::Failed);
        CHECK(ExecuteInvulnerable(context, {}).state == gloss::gameplay::RequestState::Failed);
        // A reclaimed world/pawn must release only our ledger. It must never call
        // RemoveLooseGameplayTag on an object now occupying the same address.
        invulnerableBinding = {};
        CHECK(invulnerableBinding.contribution.BeginAdd(2));
        invulnerableBinding.enabled = true;
        invulnerableBinding.identity = {arena + 0x1000, arena + 0x2000, arena + 0x3000, 1, 2, 3};
        CHECK(InvulnerableTargetStatus() == gloss::gameplay::TagTargetStatus::Expired);
        CHECK(RemoveInvulnerableContribution());
        CHECK(!invulnerableBinding.enabled);
        CHECK(!invulnerableBinding.contribution.owned);
        CHECK(RemoveInvulnerableContribution());
        CHECK(dispatchedCalls.load() == dispatchedBefore);
        // A live target that cannot currently be touched retains the ownership
        // ledger for later cleanup, including an indeterminate prior removal.
        Commit(base + 0xdd86ac8);
        const auto names = arena + 0x19000, vtable = arena + 0x1a000, type = arena + 0x1b000;
        Put<std::uint32_t>(base + 0xdd86ac8, 0);
        Put<std::uint32_t>(base + 0xdd86acc, 64);
        Put(base + 0xdd86ad0, names);
        Put<std::uint16_t>(names, 4 << 6);
        std::memcpy(reinterpret_cast<void*>(names + 2), "Mock", 4);
        Put(vtable, base + 0x1672710);
        for (const auto address : {arena + 0x1c000, arena + 0x1d000, arena + 0x1e000}) {
            Put(address, vtable); Put<std::uint32_t>(address + 8, 0);
            Put<std::uint32_t>(address + 0xc, 51); Put(address + 0x10, type);
            Put<std::uint32_t>(address + 0x18, 0);
        }
        invulnerableBinding.identity = {arena + 0x1c000, arena + 0x1d000, arena + 0x1e000, 51, 51, 51};
        CHECK(invulnerableBinding.contribution.BeginAdd(3));
        CHECK(invulnerableBinding.contribution.ObserveAdd(4));
        CHECK(invulnerableBinding.contribution.BeginRemove(4));
        stopping.store(true);
        CHECK(InvulnerableTargetStatus() == gloss::gameplay::TagTargetStatus::Unavailable);
        CHECK(!RemoveInvulnerableContribution());
        CHECK(invulnerableBinding.contribution.owned && invulnerableBinding.contribution.removalPending);
        stopping.store(false); faulted.store(true);
        CHECK(!RemoveInvulnerableContribution());
        CHECK(invulnerableBinding.contribution.owned && invulnerableBinding.contribution.removalPending);
        faulted.store(false);
        Put<std::uint32_t>(arena + 0x1d000 + 0xc, 52);
        CHECK(RemoveInvulnerableContribution());
        CHECK(!invulnerableBinding.contribution.owned);
        CHECK(dispatchedCalls.load() == dispatchedBefore);
    }

    Address tagCountFunction{}, tagAddFunction{}, tagRemoveFunction{};
    unsigned tagReadCalls{}, tagAddCalls{}, tagRemoveCalls{};
    std::int32_t mockTagCount{};
    bool tagMutationRaisesSeh{};
    void __fastcall TagProcessEvent(void*, void* function, void* parameters) {
        const auto target = reinterpret_cast<Address>(function);
        if (target == tagCountFunction) {
            ++tagReadCalls;
            std::memcpy(static_cast<std::byte*>(parameters) + 8, &mockTagCount, sizeof(mockTagCount));
            return;
        }
        if (target == tagAddFunction) ++tagAddCalls;
        if (target == tagRemoveFunction) ++tagRemoveCalls;
        if (tagMutationRaisesSeh) {
            // Model an engine mutation that raises after changing the count.
            // The caller cannot infer whether the operation completed.
            mockTagCount += target == tagAddFunction ? 1 : -1;
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        }
        static_cast<std::byte*>(parameters)[17] = std::byte{}; // Ordinary gameplay rejection.
    }

    void TestInvulnerableSehStopsEngineCalls() {
        using namespace gloss::gameplay;
        const auto memory = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        CHECK(memory != nullptr);
        if (!memory) return;
        const auto arena = reinterpret_cast<Address>(memory);
        const auto names = arena, vtable = arena + 0x1000, pawnType = arena + 0x2000;
        const auto world = arena + 0x3000, pawn = arena + 0x4000, component = arena + 0x5000;
        const auto componentType = arena + 0x6000, library = arena + 0x7000, cdo = arena + 0x8000;
        tagAddFunction = arena + 0x9000; tagRemoveFunction = arena + 0x9400; tagCountFunction = arena + 0x9800;
        Address nextField = arena + 0xa000, nextFieldType = arena + 0xc000;
        const auto actorType = arena + 0xd000, tagType = arena + 0xd400;
        std::uint32_t cursor{}, nextIndex{100};
        std::unordered_map<std::string, std::uint32_t> nameIds;
        std::unordered_map<std::string, Address> fieldTypes;
        const auto nameId = [&](std::string_view text) {
            const auto key = std::string(text);
            if (const auto found = nameIds.find(key); found != nameIds.end()) return found->second;
            const auto id = cursor / 2;
            Put<std::uint16_t>(names + cursor, static_cast<std::uint16_t>(text.size() << 6));
            std::memcpy(reinterpret_cast<void*>(names + cursor + 2), text.data(), text.size());
            cursor += static_cast<std::uint32_t>((text.size() + 3) & ~std::size_t{1});
            nameIds.emplace(key, id);
            return id;
        };
        const auto object = [&](Address target, Address type, std::string_view name) {
            Put(target, vtable); Put<std::uint32_t>(target + 0xc, nextIndex++);
            Put(target + 0x10, type); Put(target + 0x18, nameId(name));
        };
        const auto field = [&](const CombatParameter& parameter) {
            const auto target = nextField; nextField += 0x100;
            auto& type = fieldTypes[std::string(parameter.type)];
            if (!type) { type = nextFieldType; nextFieldType += 0x100; Put(type + 0x30, nameId(parameter.type)); }
            Put(target + 8, type); Put(target + 0x20, nameId(parameter.name));
            Put(target + 0x34, parameter.size); Put(target + 0x44, parameter.offset); Put(target + 0x38, parameter.flags);
            if (parameter.type == "BoolProperty") Put<std::uint32_t>(target + 0x70, 0xff010001);
            else if (!parameter.subtype.empty()) Put(target + 0x70, parameter.subtype == "Actor" ? actorType : tagType);
            return target;
        };
        const auto function = [&](Address target, Address owner, std::string_view name, Address rva,
            std::uint32_t flags, std::uint16_t size, std::initializer_list<CombatParameter> parameters) {
            object(target, library, name); Put(target + 0x20, owner); Put(target + 0x128, base + rva);
            Put(target + 0x100, flags); Put<std::uint8_t>(target + 0x104, static_cast<std::uint8_t>(parameters.size()));
            Put(target + 0x106, size);
            Address previous{};
            for (const auto& parameter : parameters) {
                const auto current = field(parameter);
                Put(previous ? previous + 0x10 : target + 0x50, current); previous = current;
            }
        };
        Commit(base + ObjectProcessEventRva); Commit(base + 0xe08ead0);
        Commit(base + 0xdd86ac8);
        // Only private test memory is patched. Tail-jump into a real C++ mock so
        // CallProcessEvent's production SEH handler catches RaiseException.
        std::array<unsigned char, 12> jump{0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
        const auto entry = reinterpret_cast<Address>(&TagProcessEvent);
        std::memcpy(jump.data() + 2, &entry, sizeof(entry));
        std::memcpy(reinterpret_cast<void*>(base + ObjectProcessEventRva), jump.data(), jump.size());
        CHECK(FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(base + ObjectProcessEventRva), jump.size()));
        Put(vtable, base + ObjectProcessEventRva);
        object(pawnType, pawnType, "MockPawn"); object(world, pawnType, "MockWorld"); object(pawn, pawnType, "MockPawn");
        object(componentType, componentType, "PBZAbilitySystemComponent"); object(component, componentType, "MockASC");
        object(library, library, "PBZAbilityBlueprintLibrary"); object(cdo, library, "MockCDO");
        object(actorType, actorType, "Actor"); object(tagType, tagType, "GameplayTag");
        Put(pawnType + 0x50, field({"AbilitySystemComponent", "ObjectProperty", 0x750, 8, 0, ""}));
        Put(pawn + 0x750, component); Put(component + 0x20, pawn); Put(library + 0x160, cdo);
        for (const bool add : {true, false}) {
            function(add ? tagAddFunction : tagRemoveFunction, library, add ? "AddLooseGameplayTag" : "RemoveLooseGameplayTag",
                add ? 0x6609620 : 0x660bdb0, 0x04422401, 18, {
                    {"Actor", "ObjectProperty", 0, 8, 0x18001000000280ull, "Actor"},
                    {"GameplayTag", "StructProperty", 8, 8, 0x18001008000182ull, "GameplayTag"},
                    {"bShouldReplicate", "BoolProperty", 16, 1, 0x18001040000280ull, ""},
                    {"ReturnValue", "BoolProperty", 17, 1, 0x18001040000780ull, ""}});
        }
        function(tagCountFunction, componentType, "GetGameplayTagCount", 0x4cb6940, 0x54020403, 12, {
            {"GameplayTag", "StructProperty", 0, 8, 0x18001000000080ull, "GameplayTag"},
            {"ReturnValue", "IntProperty", 8, 4, 0x18001040000780ull, ""}});
        Put(library + 0x48, tagAddFunction); Put(tagAddFunction + 0x28, tagRemoveFunction);
        Put(componentType + 0x48, tagCountFunction); Put(base + 0xe08ead0, library);
        nameId(InvulnerableTag);
        Put<std::uint32_t>(base + 0xdd86ac8, 0); Put(base + 0xdd86acc, cursor); Put(base + 0xdd86ad0, names);
        propertyLookups.Clear(); functionLookups.Clear();
        Context context; context.world = world; context.pawn = pawn; context.worldIndex = Read<std::uint32_t>(world + 0xc);
        const TagTargetIdentity identity{world, pawn, component, context.worldIndex,
            Read<std::uint32_t>(pawn + 0xc), Read<std::uint32_t>(component + 0xc)};
        Request on, off; on.arguments = {"on"}; off.arguments = {"off"};
        auto& backend = GetBackend();
        const auto reset = [&] {
            faulted.store(false); invulnerableBinding = {};
            tagReadCalls = tagAddCalls = tagRemoveCalls = 0;
            backend.SetRuntimeState(true, "mock tag runtime", "tag-seh");
        };
        // A normal false return is a rejected operation, not an adapter fault.
        reset(); mockTagCount = 3; tagMutationRaisesSeh = false;
        CHECK(ExecuteInvulnerable(context, on).state == RequestState::Failed);
        CHECK(!faulted.load()); CHECK(backend.IsReady());
        CHECK(tagAddCalls == 1 && tagRemoveCalls == 0 && mockTagCount == 3);
        CHECK(!invulnerableBinding.contribution.owned);

        reset(); mockTagCount = 3; tagMutationRaisesSeh = true;
        const auto beforeAdd = dispatchedCalls.load();
        CHECK(ExecuteInvulnerable(context, on).state == RequestState::Indeterminate);
        CHECK(faulted.load()); CHECK(!backend.IsReady());
        CHECK(backend.GetSnapshot().status.find("无敌标签调用异常") != std::string::npos);
        CHECK(tagReadCalls == 2 && tagAddCalls == 1 && tagRemoveCalls == 0 && mockTagCount == 4);
        CHECK(dispatchedCalls.load() == beforeAdd + 1);
        CHECK(invulnerableBinding.contribution.owned && !invulnerableBinding.contribution.confirmed);
        CHECK(!invulnerableBinding.contribution.removalPending);
        CHECK(!ReadGameplayTagCount(context, InvulnerableTag));
        CHECK(!RemoveInvulnerableContribution());
        CHECK(tagReadCalls == 2 && tagAddCalls == 1 && tagRemoveCalls == 0);
        CHECK(invulnerableBinding.contribution.owned);

        reset(); mockTagCount = 4;
        invulnerableBinding.context = context; invulnerableBinding.identity = identity;
        CHECK(invulnerableBinding.contribution.BeginAdd(3));
        CHECK(invulnerableBinding.contribution.ObserveAdd(4));
        invulnerableBinding.enabled = true;
        const auto beforeRemove = dispatchedCalls.load();
        CHECK(ExecuteInvulnerable(context, off).state == RequestState::Indeterminate);
        CHECK(faulted.load()); CHECK(!backend.IsReady());
        CHECK(tagReadCalls == 1 && tagAddCalls == 0 && tagRemoveCalls == 1 && mockTagCount == 3);
        CHECK(dispatchedCalls.load() == beforeRemove + 1);
        CHECK(invulnerableBinding.contribution.owned && invulnerableBinding.contribution.removalPending);
        CHECK(invulnerableBinding.contribution.removalTarget == 3);
        CHECK(ExecuteInvulnerable(context, off).state == RequestState::Indeterminate);
        CHECK(tagReadCalls == 1 && tagRemoveCalls == 1);
        CHECK(invulnerableBinding.contribution.owned && invulnerableBinding.contribution.removalPending);

        invulnerableBinding = {}; faulted.store(false);
        backend.SetRuntimeState(false, "tag SEH test complete", "tag-seh");
        propertyLookups.Clear(); functionLookups.Clear();
        Put<Address>(base + 0xe08ead0, 0); Put<Address>(base + 0xdd86ad0, 0);
        Put<std::uint32_t>(base + 0xdd86acc, 0);
        VirtualFree(memory, 0, MEM_RELEASE);
    }

    void TestAccessoryOwnershipAndCleanupFailures() {
        // Exercise the production ownership/cleanup state machine using private
        // UObject slots. No ProcessEvent mock is installed: these paths must never
        // dispatch an engine call or discard an unreadable live contribution.
        const auto memory = VirtualAlloc(nullptr, 0x5000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        CHECK(memory != nullptr);
        if (!memory) return;
        const auto arena = reinterpret_cast<Address>(memory);
        const auto names = arena, vtable = arena + 0x1000, type = arena + 0x1800;
        const auto component = arena + 0x2000, chunks = arena + 0x3000, records = arena + 0x4000;
        const auto slot = records + 24;
        Commit(base + 0xde28c68); Commit(base + 0xdd86ac8);
        Put(base + 0xde28c68, chunks); Put<std::int32_t>(base + 0xde28c70, 2); Put(chunks, records);
        Put(slot + 8, component); Put<std::int32_t>(slot + 16, 7);
        Put<std::uint32_t>(base + 0xdd86ac8, 0); Put<std::uint32_t>(base + 0xdd86acc, 6);
        Put(base + 0xdd86ad0, names); Put<std::uint16_t>(names, 4 << 6);
        std::memcpy(reinterpret_cast<void*>(names + 2), "Mock", 4);
        Put(vtable, base + 0x1672710); Put(component, vtable);
        Put<std::uint32_t>(component + 0xc, 1); Put(component + 0x10, type);
        Put(type, vtable); Put(type + 0x10, type);
        propertyLookups.Clear(); functionLookups.Clear();
        const auto calls = dispatchedCalls.load();
        const auto prepare = [&] {
            accessories = {};
            accessories.enabled = accessories.synchronized = true;
            accessories.component = {component, 1, 7};
            AccessoryContribution first, second;
            first.handle = {71, 1, {}}; first.effect.definition = arena + 0x2600;
            second.handle = {72, 1, {}}; second.effect.definition = arena + 0x2800;
            accessories.owned = {first, second};
        };
        prepare();
        CHECK(AccessoryIdentityStatus(accessories.component) == std::optional<bool>{true});
        CHECK(AccessoriesToggleState({}).known);
        accessories.synchronized = false;
        CHECK(!AccessoriesToggleState({}).known);
        accessories.synchronized = true;

        // An unreadable GUObject array is unknown, never evidence of collection.
        Put(base + 0xde28c68, base + 0x1000);
        CHECK(!AccessoryIdentityStatus(accessories.component).has_value());
        CHECK(!AccessoriesCleanup()); CHECK(accessories.owned.size() == 2 && !accessories.enabled);
        CHECK(!AccessoriesCleanup()); CHECK(accessories.owned.size() == 2);
        const auto faults = readFaults.load();
        stopping.store(true);
        CHECK(!AccessoriesCleanup()); CHECK(!AccessoryRemoveOwned(1));
        CHECK(accessories.owned.size() == 2 && readFaults.load() == faults);
        stopping.store(false); faulted.store(true);
        CHECK(!AccessoriesCleanup()); CHECK(!AccessoryRemoveOwned(1));
        CHECK(!AccessoriesToggleState({}).known);
        CHECK(accessories.owned.size() == 2 && readFaults.load() == faults);
        faulted.store(false);
        Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId() + 1);
        CHECK(!AccessoriesCleanup()); CHECK(accessories.owned.size() == 2 && readFaults.load() == faults);
        Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId());
        Put(base + 0xde28c68, chunks);

        // A live object lacking a verified removal contract keeps both handles.
        CHECK(ObjectValid(component));
        CHECK(!AccessoriesCleanup()); CHECK(accessories.owned.size() == 2);
        Put<std::uint32_t>(component + 8, 0x8000);
        CHECK(!AccessoriesCleanup()); CHECK(accessories.owned.size() == 2);
        Put<std::uint32_t>(component + 8, 0);

        // Reuse at the same address is detected by serial. Removing one stale
        // record must not clear its siblings while reconciliation holds indices.
        Put<std::int32_t>(slot + 16, 8);
        CHECK(AccessoryIdentityStatus(accessories.component) == std::optional<bool>{false});
        CHECK(AccessoryRemoveOwned(1));
        CHECK(accessories.owned.size() == 1 && accessories.owned.front().handle.id == 71);
        CHECK(!AccessoryRemoveOwned(1)); // stale caller index fails closed
        CHECK(AccessoriesCleanup()); CHECK(accessories.owned.empty());
        CHECK(AccessoriesCleanup()); CHECK(accessories.owned.empty());

        // An uncertain application with no attributable handle cannot be called
        // clean until the original component's destruction is positively known.
        prepare(); accessories.owned.clear(); accessories.uncertain = true;
        Put<std::int32_t>(slot + 16, 7);
        CHECK(!AccessoriesCleanup()); CHECK(accessories.uncertain);
        Put<Address>(slot + 8, component + 0x100);
        CHECK(AccessoriesSessionChanged({})); CHECK(!accessories.uncertain && accessories.owned.empty());
        CHECK(dispatchedCalls.load() == calls);
        accessories = {};
        const auto idleFaults = readFaults.load();
        AccessoriesTick({});
        CHECK(readFaults.load() == idleFaults && dispatchedCalls.load() == calls);
        accessories = {}; propertyLookups.Clear(); functionLookups.Clear();
        Put<Address>(base + 0xde28c68, 0); Put<std::int32_t>(base + 0xde28c70, 0);
        Put<Address>(base + 0xdd86ad0, 0); Put<std::uint32_t>(base + 0xdd86acc, 0);
        VirtualFree(memory, 0, MEM_RELEASE);
    }

    void TestScriptVerifierLeaseAndRestoration() {
        auto& mock = scriptVerifyMock;
        Commit(base + ScriptVerifyRva);
        mock.entry = reinterpret_cast<void*>(base + ScriptVerifyRva);
        std::memcpy(mock.entry, ScriptVerifyEntry.data(), ScriptVerifyEntry.size());
        std::copy_n(ScriptVerifyEntry.begin(), mock.original.size(), mock.original.begin());
        mock.patch = mock.original; mock.patch[0] = 0xe9; mock.patch[1] = 0x33;
        scriptVerifyCreated = scriptVerifyPatched = scriptVerifyPatchCaptured = false;
        modulePinned.store(false); CHECK(!InstallScriptVerifyHook()); CHECK(mock.creates == 0);
        modulePinned.store(true);
        mock.createStatus = MH_ERROR_MEMORY_ALLOC;
        CHECK(!InstallScriptVerifyHook()); CHECK(!scriptVerifyCreated); CHECK(!scriptVerifyPatched);
        mock.createStatus = MH_OK; mock.enableStatus = MH_ERROR_MEMORY_PROTECT;
        CHECK(PrepareScriptVerifyHook()); CHECK(PrepareScriptVerifyHook());
        CHECK(mock.enables == 0); CHECK(!scriptVerifyPatched); CHECK(ScriptVerifyOriginalEntry());
        CHECK(!InstallScriptVerifyHook()); CHECK(scriptVerifyCreated); CHECK(!scriptVerifyPatched);
        CHECK(ScriptVerifyOriginalEntry()); CHECK(mock.creates == 2);
        mock.enableStatus = MH_OK;
        CHECK(InstallScriptVerifyHook()); CHECK(ScriptVerifyOwnership());
        CHECK(InstallScriptVerifyHook()); CHECK(mock.enables == 2);
        static_cast<unsigned char*>(mock.entry)[2] ^= 0x40;
        CHECK(!RestoreScriptVerifyHook()); CHECK(scriptVerifyPatched); CHECK(mock.disables == 0);
        std::memcpy(mock.entry, mock.patch.data(), mock.patch.size());
        mock.disableStatus = MH_ERROR_MEMORY_PROTECT;
        CHECK(!RestoreScriptVerifyHook()); CHECK(scriptVerifyPatched); CHECK(ScriptVerifyOwnership());
        mock.disableStatus = MH_OK;
        CHECK(RestoreScriptVerifyHook()); CHECK(!scriptVerifyPatched); CHECK(scriptVerifyPatchCaptured);
        CHECK(ScriptVerifyOriginalEntry()); CHECK(RestoreScriptVerifyHook()); CHECK(mock.disables == 2);
        // A later enable must not adopt different patch bytes as its receipt.
        mock.patch[2] ^= 0x40;
        CHECK(!InstallScriptVerifyHook()); CHECK(scriptVerifyPatched); CHECK(!ScriptVerifyOwnership());
        CHECK(!RestoreScriptVerifyHook()); CHECK(mock.disables == 2);
        mock.patch[2] ^= 0x40; std::memcpy(mock.entry, mock.patch.data(), mock.patch.size());
        CHECK(RestoreScriptVerifyHook()); CHECK(mock.creates == 2);

        faulted.store(false); stopping.store(false);
        Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId());
        scriptExecuting = true; scriptSource = "owned private source";
        std::vector<char> bytes(scriptSource.begin(), scriptSource.end()); bytes.resize(bytes.size() + 2);
        ScriptBytes descriptor{reinterpret_cast<Address>(bytes.data()), static_cast<int>(scriptSource.size()), static_cast<int>(bytes.size())};
        scriptDataDescriptor = reinterpret_cast<Address>(&descriptor); scriptLoadedBytes = descriptor;
        StringValue path{const_cast<wchar_t*>(ScriptModuleName), static_cast<int>(std::size(ScriptModuleName)), static_cast<int>(std::size(ScriptModuleName))};
        scriptPrivateVerified = false; scriptVerifyOriginalCalls = 0;
        CHECK(HookScriptVerify(&path, &descriptor)); CHECK(scriptPrivateVerified); CHECK(scriptVerifyOriginalCalls == 0);
        const auto rejected = [&](const StringValue* inputPath, const ScriptBytes* data) {
            const auto before = scriptVerifyOriginalCalls; scriptPrivateVerified = false;
            CHECK(!HookScriptVerify(inputPath, data)); CHECK(!scriptPrivateVerified);
            CHECK(scriptVerifyOriginalCalls == before + 1); CHECK(callbacksInFlight.load() == 0);
        };
        auto copiedDescriptor = descriptor; rejected(&path, &copiedDescriptor);
        bytes[0] ^= 1; rejected(&path, &descriptor); bytes[0] ^= 1;
        bytes.back() = 1; rejected(&path, &descriptor); bytes.back() = 0;
        --descriptor.count; rejected(&path, &descriptor); ++descriptor.count;
        --descriptor.capacity; rejected(&path, &descriptor); ++descriptor.capacity;
        ++descriptor.data; rejected(&path, &descriptor); --descriptor.data;
        wchar_t otherName[] = L"project/index.js";
        StringValue otherPath{otherName, static_cast<int>(std::size(otherName)), static_cast<int>(std::size(otherName))};
        rejected(&otherPath, &descriptor);
        scriptExecuting = false; rejected(&path, &descriptor); scriptExecuting = true;
        faulted.store(true); rejected(&path, &descriptor); faulted.store(false);
        stopping.store(true); rejected(&path, &descriptor); stopping.store(false);
        Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId() + 1); rejected(&path, &descriptor);
        Put<DWORD>(base + GameThreadIdRva, GetCurrentThreadId());
        // TLS prevents a second thread from seeing the active private lease.
        const auto before = scriptVerifyOriginalCalls; bool result = true;
        std::thread otherThread([&] { result = HookScriptVerify(&path, &descriptor); }); otherThread.join();
        CHECK(!result); CHECK(scriptVerifyOriginalCalls == before + 1); CHECK(callbacksInFlight.load() == 0);
        CHECK(HookScriptVerify(&path, &descriptor)); CHECK(callbacksInFlight.load() == 0);
        scriptExecuting = false; scriptDataDescriptor = 0; scriptLoadedBytes = {}; scriptSource.clear();
        scriptPrivateVerified = false;
        originalScriptVerify.store(&ThrowingScriptVerifyOriginal);
        bool threw{};
        try { HookScriptVerify(&path, &descriptor); } catch (int value) { threw = value == 17; }
        CHECK(threw); CHECK(callbacksInFlight.load() == 0);
        originalScriptVerify.store(&ScriptVerifyOriginalMock);
        // Cleanup must retain the outstanding native receipt even without JS state.
        CHECK(InstallScriptVerifyHook()); scriptStateOwned = false;
        mock.disableStatus = MH_ERROR_MEMORY_PROTECT;
        CHECK(!ScriptCleanup()); CHECK(scriptVerifyPatched);
        mock.disableStatus = MH_OK;
        CHECK(ScriptCleanup()); CHECK(!scriptVerifyPatched); CHECK(ScriptVerifyOriginalEntry());
    }
    void TestScriptVerifierLeasePolicy() {
        auto& mock = scriptVerifyMock;
        const auto enablesBefore = mock.enables, disablesBefore = mock.disables;
        mock.enableStatus = MH_OK; mock.disableStatus = MH_OK;
        scriptEnabled.fill(false); scriptPatched.fill(false);

        CHECK(!ScriptVerifierWanted("probe"));
        CHECK(!ScriptVerifierWanted("cleanup"));
        CHECK(!ScriptVerifierWanted("error"));
        CHECK(!ScriptVerifierWanted("tick"));
        CHECK(ScriptVerifierWanted("toggle", "auto_collect", true));
        CHECK(ScriptVerifierWanted("toggle", "highlight_loot", true));
        scriptEnabled[2] = true;
        CHECK(ScriptVerifierWanted("tick"));
        CHECK(!ScriptVerifierWanted("toggle", "auto_collect", false));
        scriptEnabled[3] = true;
        CHECK(ScriptVerifierWanted("toggle", "auto_collect", false));
        CHECK(ScriptVerifierWanted("toggle", "highlight_loot", false));

        // A successful feature call keeps one verifier patch between ticks.
        CHECK(InstallScriptVerifyHook());
        CHECK(ScriptVerifyOwnership());
        const auto firstEnable = mock.enables;
        for (unsigned tick = 0; tick < 4; ++tick) {
            CHECK(ScriptVerifierWanted("tick"));
            CHECK(InstallScriptVerifyHook());
            CHECK(RestoreScriptHooks(ScriptVerifierWanted("tick")));
            CHECK(scriptVerifyPatched && ScriptVerifyOwnership());
        }
        CHECK(mock.enables == firstEnable);
        CHECK(mock.disables == disablesBefore);

        // Keep only the owned verifier; every temporary vtable slot is restored.
        const auto slots = ScriptHookSlots(), hooks = ScriptHookFunctions();
        for (std::size_t i = 0; i < slots.size(); ++i) {
            Commit(slots[i]);
            scriptSavedSlots[i].store(base + 0x101000 + i * 0x10);
            Put<Address>(slots[i], hooks[i]);
            scriptPatched[i] = true;
        }
        CHECK(RestoreScriptHooks(true));
        CHECK(scriptVerifyPatched && mock.disables == disablesBefore);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            CHECK(Read<Address>(slots[i]) == scriptSavedSlots[i].load());
            CHECK(!scriptPatched[i]);
        }

        // Turning off one feature keeps the verifier while another remains on;
        // turning off the last feature restores it immediately.
        CHECK(ScriptVerifierWanted("toggle", "auto_collect", false));
        scriptEnabled[2] = false;
        CHECK(!scriptEnabled[2] && scriptEnabled[3]); // Only highlight_loot remains on.
        CHECK(ScriptVerifierWanted("tick"));
        CHECK(!ScriptVerifierWanted("toggle", "highlight_loot", false));
        CHECK(RestoreScriptHooks(ScriptVerifierWanted("toggle", "highlight_loot", false)));
        scriptEnabled[3] = false;
        CHECK(!scriptVerifyPatched && mock.disables == disablesBefore + 1);
        CHECK(!ScriptVerifierWanted("tick"));

        // Probe, cleanup, and an unacknowledged/error command always release the lease.
        for (const auto* command : {"probe", "cleanup", "error"}) {
            scriptEnabled[0] = true;
            CHECK(InstallScriptVerifyHook());
            CHECK(!ScriptVerifierWanted(command));
            CHECK(RestoreScriptHooks(ScriptVerifierWanted(command)));
            CHECK(!scriptVerifyPatched);
            scriptEnabled[0] = false;
        }

        // A foreign verifier patch is never adopted or overwritten; retain the
        // ledger until the exact owned bytes return, then cleanup can retry.
        scriptEnabled[0] = true;
        CHECK(InstallScriptVerifyHook());
        std::array<unsigned char, 16> owned{};
        owned = scriptVerifyOwnedEntry;
        static_cast<unsigned char*>(mock.entry)[2] ^= 0x40;
        const auto foreignDisables = mock.disables;
        CHECK(!RestoreScriptHooks(true));
        CHECK(scriptVerifyPatched && !ScriptVerifyOwnership());
        CHECK(mock.disables == foreignDisables);
        std::memcpy(mock.entry, owned.data(), owned.size());
        CHECK(RestoreScriptHooks(false));
        CHECK(!scriptVerifyPatched && mock.disables == foreignDisables + 1);
        scriptEnabled[0] = false;

        // A Disable failure keeps the patch receipt; a later cleanup succeeds.
        scriptEnabled[0] = true;
        CHECK(InstallScriptVerifyHook());
        mock.disableStatus = MH_ERROR_MEMORY_PROTECT;
        const auto failedDisableCount = mock.disables;
        CHECK(!RestoreScriptHooks(false));
        CHECK(scriptVerifyPatched && ScriptVerifyOwnership());
        CHECK(mock.disables == failedDisableCount + 1);
        mock.disableStatus = MH_OK;
        CHECK(RestoreScriptHooks(false));
        CHECK(!scriptVerifyPatched && mock.disables == failedDisableCount + 2);
        scriptEnabled[0] = false;

        // If a temporary callback slot becomes foreign while the verifier is
        // leased, restore the verifier but leave that slot's ledger for retry.
        CHECK(InstallScriptVerifyHook());
        Commit(slots[0]);
        const auto savedSlot = base + 0x102000;
        scriptSavedSlots[0].store(savedSlot);
        Put<Address>(slots[0], base + 0x202000);
        scriptPatched[0] = true;
        const auto foreignSlot = Read<Address>(slots[0]);
        CHECK(!RestoreScriptHooks(true));
        CHECK(!scriptVerifyPatched && !ScriptVerifyOwnership());
        CHECK(scriptPatched[0] && Read<Address>(slots[0]) == foreignSlot);
        Put<Address>(slots[0], hooks[0]);
        CHECK(RestoreScriptHooks(false));
        CHECK(!scriptPatched[0] && Read<Address>(slots[0]) == savedSlot);

        CHECK(mock.enables >= enablesBefore + 1);
        scriptEnabled.fill(false); scriptPatched.fill(false);
        for (auto& slot : scriptSavedSlots) slot.store(0);
    }
    void TestScriptDistanceValidation() {
        CHECK(ScriptDistanceValid("auto_collect", 1)); CHECK(ScriptDistanceValid("auto_collect", 10));
        CHECK(ScriptDistanceValid("auto_collect", 50)); CHECK(ScriptDistanceValid("highlight_loot", 100));
        CHECK(ScriptDistanceValid("highlight_loot", 1.25));
        CHECK(!ScriptDistanceValid("auto_collect", 50.0001)); CHECK(!ScriptDistanceValid("highlight_loot", 100.0001));
        CHECK(!ScriptDistanceValid("auto_collect", 0)); CHECK(!ScriptDistanceValid("highlight_loot", -1));
        CHECK(!ScriptDistanceValid("free_teleport", 10)); CHECK(!ScriptDistanceValid("unknown", 10));
        CHECK(!ScriptDistanceValid("auto_collect", std::numeric_limits<double>::infinity()));
        CHECK(!ScriptDistanceValid("highlight_loot", std::numeric_limits<double>::quiet_NaN()));
        const auto sequence = scriptSequence;
        const auto savedDistances = scriptDistances;
        for (const auto& arguments : std::vector<std::vector<std::string>>{
            {"auto_collect", "on"}, {"highlight_loot", "on", ""}, {"auto_collect", "on", "nan"},
            {"auto_collect", "on", "inf"}, {"auto_collect", "on", "10;throw 1"},
            {"auto_collect", "on", "10 "}, {"auto_collect", "on", "51"}, {"highlight_loot", "on", "101"},
            {"highlight_loot", "on", "0"}, {"auto_collect", "off", "10"}, {"free_teleport", "on", "10"}}) {
            gloss::gameplay::Request request; request.arguments = arguments;
            CHECK(ExecuteScript({}, request).state == gloss::gameplay::RequestState::Failed);
            CHECK(scriptSequence == sequence); CHECK(scriptDistances == savedDistances);
        }
    }
    void TestScriptModuleReceiptAndDispatch() {
        std::array<std::byte, 32> instance{};
        Context context; context.instance = reinterpret_cast<Address>(instance.data());
        Put<std::uint32_t>(context.instance + 0xc, 19);
        scriptModuleReceipt = {};
        CHECK(!ScriptModuleCurrent(context, 11, 22, 33));
        CHECK(ScriptCommandBody("tick", false) == nullptr);
        CHECK(ScriptCommandBody("toggle", false) == ScriptFeatureSource);
        CHECK(ScriptCommandBody("cleanup", false) == ScriptFeatureSource);
        CHECK(std::string_view(ScriptCommandBody("probe", false)) == "console.log(ack+'ok');");
        RecordScriptModule(context, 11, 22, 33, true);
        CHECK(ScriptModuleCurrent(context, 11, 22, 33));
        for (const auto command : {"toggle", "tick", "cleanup"}) {
            CHECK(ScriptCommandBody(command, true) == ScriptDispatchSource);
            CHECK(std::string_view(ScriptCommandBody(command, true)).size() < 512);
        }
        CHECK(!ScriptModuleCurrent(context, 12, 22, 33));
        CHECK(!ScriptModuleCurrent(context, 11, 23, 33));
        CHECK(!ScriptModuleCurrent(context, 11, 22, 34));
        Put<std::uint32_t>(context.instance + 0xc, 20);
        CHECK(!ScriptModuleCurrent(context, 11, 22, 33));
        Put<std::uint32_t>(context.instance + 0xc, 19);
        CHECK(ScriptModuleCurrent(context, 11, 22, 33));
        Context other = context; other.world = context.world + 1;
        CHECK(!ScriptModuleCurrent(other, 11, 22, 33));
        RecordScriptModule(context, 11, 22, 33, false);
        CHECK(!ScriptModuleCurrent(context, 11, 22, 33));
        CHECK(ScriptCommandBody("unknown", true) == nullptr);
        CHECK(std::string_view(ScriptFeatureSource).size() > 40000);
        CHECK(std::string_view(ScriptDispatchSource).find("d.value(command,id,enabled,range)") != std::string_view::npos);
        scriptModuleReceipt = {};
    }
    void TestScriptExceptionDiagnostics() {
        EXCEPTION_RECORD record{}; CONTEXT registers{};
        record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
        record.ExceptionAddress = reinterpret_cast<void*>(base + 0x1234);
        record.NumberParameters = 2; record.ExceptionInformation[0] = 1; record.ExceptionInformation[1] = 0x5678;
        registers.Rcx = 11; registers.Rdx = 22; registers.R8 = 33; registers.R9 = 44;
        EXCEPTION_POINTERS pointers{&record, &registers};
        scriptException = {}; scriptStage = "test-stage"; faulted.store(false);
        CHECK(ScriptExceptionFilter(&pointers, "test-boundary") == EXCEPTION_EXECUTE_HANDLER);
        CHECK(faulted.load()); CHECK(scriptException.code == EXCEPTION_ACCESS_VIOLATION);
        CHECK(scriptException.instruction == base + 0x1234); CHECK(scriptException.accessType == 1);
        CHECK(scriptException.accessAddress == 0x5678); CHECK(scriptException.rcx == 11 && scriptException.rdx == 22);
        CHECK(scriptException.r8 == 33 && scriptException.r9 == 44);
        CHECK(std::string_view(scriptException.stage) == "test-stage");
        CHECK(std::string_view(scriptException.boundary) == "test-boundary");
        record.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION; record.ExceptionAddress = reinterpret_cast<void*>(base + 0x9999);
        CHECK(ScriptExceptionFilter(&pointers, "secondary") == EXCEPTION_EXECUTE_HANDLER);
        CHECK(scriptException.code == EXCEPTION_ACCESS_VIOLATION); CHECK(scriptException.instruction == base + 0x1234);
        scriptException = {}; scriptStage = "idle"; faulted.store(false);
        scriptProbeConfirmed = false; CHECK(!ScriptProbeCurrent({}));
        scriptProbeConfirmed = true; scriptProbeContext = {}; scriptProbeContext.instance = 0x1234;
        CHECK(!ScriptProbeCurrent({})); scriptProbeConfirmed = false;
        // Same world/controller key does not imply that the Puerts owner survived.
        std::array<std::byte, 32> instance{}; Context context;
        context.instance = reinterpret_cast<Address>(instance.data()); Put<std::uint32_t>(context.instance + 0xc, 17);
        scriptStateOwned = true; scriptContext = context; scriptInstanceIndex = 17;
        scriptEnv = 11; scriptLoader = 22; scriptLogger = 33;
        CHECK(ScriptOwnerMatches(context, 11, 22, 33));
        CHECK(!ScriptOwnerMatches(context, 44, 22, 33)); CHECK(scriptEnv == 11);
        CHECK(!ScriptOwnerMatches(context, 11, 44, 33)); CHECK(scriptLoader == 22);
        CHECK(!ScriptOwnerMatches(context, 11, 22, 44)); CHECK(scriptLogger == 33);
        Put<std::uint32_t>(context.instance + 0xc, 18); CHECK(!ScriptOwnerMatches(context, 11, 22, 33));
        scriptStateOwned = false; CHECK(ScriptOwnerMatches(context, 44, 55, 66));
        scriptContext = {}; scriptEnv = scriptLoader = scriptLogger = 0;
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
        else if (mock == &scriptVerifyMock) *original = reinterpret_cast<void*>(&ScriptVerifyOriginalMock);
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
    auto* memory = VirtualAlloc(nullptr, 0xe200000, MEM_RESERVE, PAGE_NOACCESS);
    CHECK(memory != nullptr);
    if (!memory) return 1;
    base = reinterpret_cast<Address>(memory); modulePinned.store(true);
    Commit(base + GameThreadIdRva); Commit(base + 0xdfe9278);
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
    Commit(base + 0x1672710);
    std::memcpy(reinterpret_cast<void*>(base + 0x1672710), ShachiCostWeakEntry.data(), ShachiCostWeakEntry.size());
    // Same verified weak-resolver prefix, then return this private test weak
    // record's pointer payload. Serial>0/index>=0 keep both prefix branches untaken.
    constexpr std::array<unsigned char, 5> weakReturn{0x48,0x8b,0x41,0x08,0xc3};
    std::memcpy(reinterpret_cast<void*>(base + 0x1672710 + ShachiCostWeakEntry.size()), weakReturn.data(), weakReturn.size());
    auto* arena = VirtualAlloc(nullptr, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(arena != nullptr);
    TestPrepareFailures(); TestPrepareAndReuse(); TestForeignEntriesArePreserved();
    TestFailuresKeepState(); TestPublishedOwnershipIsImmutable();
    if (arena) TestEconomyLocalScopeAndFailOpen(reinterpret_cast<Address>(arena));
    TestCostHookLifecycle();
    if (arena) TestCostOutputsAndSessionScope(reinterpret_cast<Address>(arena));
    if (arena) TestNativeActionsRejectStaleTargets(reinterpret_cast<Address>(arena));
    TestInvulnerableSehStopsEngineCalls();
    TestAccessoryOwnershipAndCleanupFailures();
    TestScriptVerifierLeaseAndRestoration();
    TestScriptVerifierLeasePolicy();
    TestScriptDistanceValidation();
    TestScriptModuleReceiptAndDispatch();
    TestScriptExceptionDiagnostics();
    TestFlightActionsBecomeReadyWithoutSessionRebind();
    if (arena) VirtualFree(arena, 0, MEM_RELEASE);
    VirtualFree(memory, 0, MEM_RELEASE);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
