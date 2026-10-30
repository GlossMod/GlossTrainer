// Exact-build Blueprint VM output filters. Damage and native GE magnitudes are
// deliberately outside this allowlist; no ability/default/attribute is patched.
        using ShachiCostVm = void(__fastcall*)(Address, Address, void*);
        constexpr std::array<Address, 2> ShachiCostVmRvas{0x1631900, 0x1634d50};
        constexpr std::array<std::array<unsigned char, 18>, 2> ShachiCostVmEntries{{
            {0x48,0x89,0x5c,0x24,0x20,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x70,0x48,0x8b,0x05,0x6c,0xc0},
            {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x81,0xec,0x90,0,0,0}}};
        constexpr std::array<unsigned char, 15> ShachiCostWeakEntry{
            0x44,0x8b,0x49,0x04,0x45,0x85,0xc9,0x74,0x48,0x8b,0x01,0x85,0xc0,0x78,0x42};
        std::array<ShachiCostVm, 2> originalShachiCostVm{};
        std::array<std::atomic<bool>, 2> shachiCostHookCreated{}, shachiCostHookInstalled{};
        std::array<std::array<unsigned char, 18>, 2> ownedShachiCostEntries{};
        std::array<bool, 2> shachiCostPatchCaptured{};
        std::atomic<bool> shachiCostActive{};
        ShachiCostPreference shachiCostPreference;
        std::uint64_t shachiCostAdjustedQueries{};
        struct ShachiCostContracts {
            Address heavyClass{}, heavyGetter{}, abilityClass{}, initial{}, constant{};
            std::uint32_t heavyClassIndex{}, getterIndex{}, abilityClassIndex{};
            bool operator==(const ShachiCostContracts&) const = default;
        } shachiCostContracts;
        struct ShachiCostContractCache {
            ResourceIdentity identity;
            Address componentType{}, data{};
            std::uint32_t componentTypeIndex{};
            std::int32_t count{};
            ShachiCostContracts contracts;
            bool captured{};
        } shachiCostContractCache;

        ResourceIdentity ShachiCostIdentity(const Context& context) noexcept {
            const auto value = CaptureAttributeIdentity(context);
            return {context.world, value.pawn, value.component, value.attributes, context.worldIndex,
                value.pawnIndex, value.componentIndex, value.attributeIndex};
        }
        bool ShachiCostIdentityCurrent(const ResourceIdentity& identity) noexcept {
            return CombatBindingCurrent() && identity.Valid() &&
                identity == ShachiCostIdentity(combatBinding.context) &&
                Read<std::uint32_t>(identity.world + 0xc) == identity.worldIndex &&
                !(Read<std::uint32_t>(identity.component + 8) & 0x40018000) &&
                Read<Address>(identity.component + 0x400) == identity.pawn &&
                Read<Address>(identity.component + 0x3f8) == identity.pawn;
        }
        bool ShachiCostEntryMatches(std::size_t index, const std::array<unsigned char, 18>& expected) noexcept {
            std::array<unsigned char, 18> current{};
            return ReadBytes(base + ShachiCostVmRvas[index], current.data(), current.size()) && current == expected;
        }
        bool ShachiCostHookOwnership(std::size_t index) noexcept {
            return shachiCostHookInstalled[index].load() && shachiCostPatchCaptured[index] &&
                ShachiCostEntryMatches(index, ownedShachiCostEntries[index]);
        }
        bool ShachiCostHooksOwned() noexcept { return ShachiCostHookOwnership(0) && ShachiCostHookOwnership(1); }
        bool ShachiCostWeakContract() noexcept {
            std::array<unsigned char, ShachiCostWeakEntry.size()> actual{};
            return ReadBytes(base + 0x16730b0, actual.data(), actual.size()) && actual == ShachiCostWeakEntry;
        }
        Address ShachiCostWeakObject(Address weak) noexcept {
            __try { return reinterpret_cast<Address(__fastcall*)(Address)>(base + 0x16730b0)(weak); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }
        bool ShachiCostArray(Address address, Address& data, std::int32_t& count, int limit) noexcept {
            struct Array { Address data; std::int32_t count, capacity; } value{};
            if (!ReadBytes(address, &value, sizeof(value)) || value.count < 0 || value.count > limit ||
                value.capacity < value.count || value.capacity > limit * 2 || (value.count && !value.data)) return false;
            data = value.data;
            count = value.count;
            return true;
        }
        Address ShachiCostAncestor(Address type, std::string_view name) {
            for (unsigned depth = 0; type && depth < 64; ++depth) {
                if (ObjectName(type) == name) return type;
                type = Read<Address>(type + 0x40);
            }
            return 0;
        }
        bool ShachiCostHeavyContract(Address owner, ShachiCostContracts& result) {
            const auto getter = FindFunction(owner, "Get Shachi Cost");
            const auto field = Read<Address>(getter + 0x50);
            if (!ObjectValid(owner) || !ObjectValid(getter) || Read<Address>(getter + 0x20) != owner ||
                Read<Address>(getter + 0x128) != base + 0x1631800 ||
                Read<std::uint32_t>(getter + 0x100) != 0x5c480000 ||
                Read<std::uint8_t>(getter + 0x104) != 1 || Read<std::uint16_t>(getter + 0x106) != 8 ||
                Name(Read<std::uint32_t>(field + 0x20)) != "ReturnValue" ||
                !PropertyLayout(field, "DoubleProperty", 8, 0) ||
                Read<std::uint64_t>(field + 0x38) != 0x8001040000780ull ||
                !PropertyLayout(FindProperty(owner, "Shachi Cost"), "DoubleProperty", 8, 0x680) ||
                !PropertyLayout(FindProperty(owner, "Shachi Cost Mul"), "DoubleProperty", 8, 0x688) ||
                !PropertyLayout(FindProperty(owner, "Shachi Cost Min"), "DoubleProperty", 8, 0x690)) return false;
            if (result.heavyClass && result.heavyClass != owner) return false;
            result.heavyClass = owner;
            result.heavyGetter = getter;
            result.heavyClassIndex = Read<std::uint32_t>(owner + 0xc);
            result.getterIndex = Read<std::uint32_t>(getter + 0xc);
            return true;
        }
        bool ShachiCostAbilityContract(Address owner, ShachiCostContracts& result) {
            const auto initial = FindProperty(owner, "Shachi Initial Cost"), constant = FindProperty(owner, "Shachi Constant Cost");
            if (!ObjectValid(owner) || !PropertyLayout(initial, "DoubleProperty", 8, 0x650) ||
                !PropertyLayout(constant, "DoubleProperty", 8, 0x658) ||
                (Read<Address>(initial + 0x18) & ~Address{1}) != owner ||
                (Read<Address>(constant + 0x18) & ~Address{1}) != owner ||
                Read<std::uint64_t>(initial + 0x38) != 0x8001040010205ull ||
                Read<std::uint64_t>(constant + 0x38) != 0x8001040010205ull ||
                (result.abilityClass && result.abilityClass != owner)) return false;
            result.abilityClass = owner;
            result.initial = initial;
            result.constant = constant;
            result.abilityClassIndex = Read<std::uint32_t>(owner + 0xc);
            return true;
        }
        bool ShachiCostObserveAbility(Address ability, ShachiCostContracts& result) {
            if (!ability) return true;
            if (!ObjectValid(ability)) return false;
            const auto type = Read<Address>(ability + 0x10);
            if (!ClassNamed(type, "GameplayAbility")) return false;
            const auto heavy = ShachiCostAncestor(type, "BP_HeavyAttack_Base_C");
            const auto cost = ShachiCostAncestor(type, "BP_ShachiAbility_Base_C");
            return (!heavy || (result.heavyClass == heavy || ShachiCostHeavyContract(heavy, result))) &&
                (!cost || (result.abilityClass == cost || ShachiCostAbilityContract(cost, result)));
        }
        bool CaptureShachiCostContracts(const Context& context, ShachiCostContracts& result, bool refresh = false) {
            result = {};
            const auto identity = ShachiCostIdentity(context);
            if (!ShachiCostIdentityCurrent(identity) || context.controller != combatBinding.context.controller ||
                !PlayerAttributesReady(context)) return false;
            const auto type = Read<Address>(identity.component + 0x10);
            const auto array = identity.component + 0x418 + 0x108;
            Address data{};
            std::int32_t count{};
            if (!ShachiCostArray(array, data, count, 4096)) return false;
            const auto& cache = shachiCostContractCache;
            if (!refresh && cache.captured && cache.identity == identity && cache.componentType == type &&
                cache.componentTypeIndex == Read<std::uint32_t>(type + 0xc) && cache.data == data && cache.count == count) {
                // Recheck the actual reflected endpoints, but avoid resolving all
                // ability names every status poll (186 specs in the live capture).
                ShachiCostContracts verified;
                const auto& saved = cache.contracts;
                if ((saved.heavyClass && !ShachiCostHeavyContract(saved.heavyClass, verified)) ||
                    (saved.abilityClass && !ShachiCostAbilityContract(saved.abilityClass, verified)) || verified != saved) return false;
                result = verified;
                return ShachiCostIdentityCurrent(identity);
            }
            const auto container = FindProperty(type, "ActivatableAbilities");
            const auto containerType = Read<Address>(container + 0x70);
            const auto items = FindProperty(containerType, "Items"), inner = Read<Address>(items + 0x78);
            const auto specType = Read<Address>(inner + 0x70);
            if (!PropertyLayout(container, "StructProperty", 288, 0x418) ||
                ObjectName(containerType) != "GameplayAbilitySpecContainer" ||
                !PropertyLayout(items, "ArrayProperty", 16, 0x108) ||
                !PropertyLayout(inner, "StructProperty", 240, 0) || ObjectName(specType) != "GameplayAbilitySpec" ||
                Read<std::int32_t>(specType + 0xa8) != 240 ||
                !PropertyLayout(FindProperty(specType, "Ability"), "ObjectProperty", 8, 0x10)) return false;
            for (const auto& pair : {std::pair{"NonReplicatedInstances", 0x78}, std::pair{"ReplicatedInstances", 0x88}}) {
                const auto instanceProperty = FindProperty(specType, pair.first);
                if (!PropertyLayout(instanceProperty, "ArrayProperty", 16, pair.second) ||
                    !PropertyLayout(Read<Address>(instanceProperty + 0x78), "ObjectProperty", 8, 0)) return false;
            }
            for (std::int32_t i = 0; i < count; ++i) {
                const auto spec = data + static_cast<Address>(i) * 240;
                if (!ShachiCostObserveAbility(Read<Address>(spec + 0x10), result)) return false;
                for (const auto offset : {0x78, 0x88}) {
                    Address instances{};
                    std::int32_t instanceCount{};
                    if (!ShachiCostArray(spec + offset, instances, instanceCount, 128)) return false;
                    for (std::int32_t n = 0; n < instanceCount; ++n)
                        if (!ShachiCostObserveAbility(Read<Address>(instances + static_cast<Address>(n) * 8), result)) return false;
                }
            }
            Address after{};
            std::int32_t afterCount{};
            const bool valid = (result.heavyClass || result.abilityClass) && ShachiCostIdentityCurrent(identity) &&
                ShachiCostArray(array, after, afterCount, 4096) && after == data && afterCount == count;
            if (valid) shachiCostContractCache = {identity, type, data, Read<std::uint32_t>(type + 0xc), count, result, true};
            return valid;
        }
        struct ShachiCostInvocation {
            Address object{}, actorInfo{}, type{}, frame{}, field{}, node{};
            std::uint32_t objectIndex{}, objectSize{};
            std::uint64_t generation{};
            double multiplier{};
            ShachiCostSource source{ShachiCostSource::Other};
            bool selected{};
        };
        bool ShachiCostActorCurrent(Address object, Address actorInfo) noexcept {
            if (!actorInfo || Read<Address>(object + 0x328) != actorInfo ||
                (Read<std::uint32_t>(object + 8) & 0x40018010)) return false; // excludes class defaults
            const auto& identity = shachiCostPreference.identity;
            return ShachiCostIdentityCurrent(identity) &&
                Read<std::uint32_t>(actorInfo + 0x10) == identity.pawnIndex &&
                Read<std::uint32_t>(actorInfo + 0x20) == identity.componentIndex &&
                ShachiCostWeakObject(actorInfo + 0x10) == identity.pawn &&
                ShachiCostWeakObject(actorInfo + 0x20) == identity.component &&
                Read<Address>(object + 0x328) == actorInfo;
        }
        ShachiCostInvocation CaptureShachiCostInvocation(Address object, Address frame, void* output, bool propertyRead) noexcept {
            ShachiCostInvocation call{};
            __try {
                if (!shachiCostActive.load() || stopping.load() || faulted.load() ||
                    GetCurrentThreadId() != expectedThread.load() || !LocalGameThread()) return call;
                const auto node = Read<Address>(frame + 0x10);
                const auto field = propertyRead ? Read<Address>(Read<Address>(frame + 0x20)) : 0;
                const auto& contracts = shachiCostContracts;
                Address owner{};
                auto source = ShachiCostSource::Other;
                if (!propertyRead && node && node == contracts.heavyGetter) {
                    owner = contracts.heavyClass;
                    source = ShachiCostSource::HeavyAttackGetter;
                    if (Read<std::uint32_t>(node + 0xc) != contracts.getterIndex ||
                        Read<std::uint32_t>(owner + 0xc) != contracts.heavyClassIndex) return call;
                } else if (propertyRead && field && (field == contracts.initial || field == contracts.constant)) {
                    owner = contracts.abilityClass;
                    source = field == contracts.initial ? ShachiCostSource::InitialAbilityProperty : ShachiCostSource::ConstantAbilityProperty;
                    if (Read<std::uint32_t>(owner + 0xc) != contracts.abilityClassIndex) return call;
                } else return call;
                const auto type = Read<Address>(object + 0x10);
                const auto size = Read<std::uint32_t>(type + 0xa8);
                if (!ShachiCostTemporaryOutput(reinterpret_cast<Address>(output), object, size) ||
                    !IsClass(type, owner) || !ShachiCostHooksOwned() ||
                    !shachiCostPreference.Current(shachiCostPreference.identity, shachiCostPreference.generation)) return call;
                const auto actorInfo = Read<Address>(object + 0x328);
                if (!ShachiCostActorCurrent(object, actorInfo)) return call;
                call = {object, actorInfo, type, frame, field, node, Read<std::uint32_t>(object + 0xc), size,
                    shachiCostPreference.generation, shachiCostPreference.multiplier, source, true};
            } __except (EXCEPTION_EXECUTE_HANDLER) { return {}; }
            return call;
        }
        void ApplyShachiCostInvocation(const ShachiCostInvocation& call, void* output) noexcept {
            __try {
                if (!call.selected || !shachiCostActive.load() || stopping.load() || faulted.load() ||
                    GetCurrentThreadId() != expectedThread.load() || !LocalGameThread() ||
                    !shachiCostPreference.Current(ShachiCostIdentity(combatBinding.context), call.generation) ||
                    !ShachiCostHooksOwned() || Read<std::uint32_t>(call.object + 0xc) != call.objectIndex ||
                    Read<Address>(call.object + 0x10) != call.type || Read<Address>(call.frame + 0x10) != call.node ||
                    !ShachiCostTemporaryOutput(reinterpret_cast<Address>(output), call.object, call.objectSize) ||
                    !ShachiCostActorCurrent(call.object, call.actorInfo)) return;
                if (call.field && (Read<Address>(call.frame + 0x30) != call.field ||
                    Read<Address>(call.frame + 0x38) != call.object + Read<std::uint32_t>(call.field + 0x44) ||
                    Read<Address>(call.frame + 0x40) != call.object)) return;
                auto* result = static_cast<double*>(output);
                const auto scaled = ScaledShachiCost(*result, call.multiplier, call.source, true);
                if (!scaled) return;
                const auto before = *result;
                *result = *scaled;
                if (*result != *scaled) {
                    shachiCostPreference.uncertain = true;
                    shachiCostActive.store(false);
                } else if (*result != before) ++shachiCostAdjustedQueries;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                shachiCostPreference.uncertain = true;
                shachiCostActive.store(false); // Failed outputs never retry automatically.
            }
        }
        void __fastcall HookShachiCostBody(Address object, Address frame, void* result) noexcept {
            ++callbacksInFlight;
            const auto call = CaptureShachiCostInvocation(object, frame, result, false);
            __try {
                originalShachiCostVm[0](object, frame, result);
                ApplyShachiCostInvocation(call, result);
            } __finally { --callbacksInFlight; }
        }
        void __fastcall HookShachiCostProperty(Address object, Address frame, void* result) noexcept {
            ++callbacksInFlight;
            const auto call = CaptureShachiCostInvocation(object, frame, result, true);
            __try {
                originalShachiCostVm[1](object, frame, result);
                ApplyShachiCostInvocation(call, result);
            } __finally { --callbacksInFlight; }
        }
        bool PrepareShachiCostHooks() {
            if (!modulePinned.load() || !ShachiCostWeakContract()) return false;
            const std::array<void*, 2> detours{reinterpret_cast<void*>(&HookShachiCostBody), reinterpret_cast<void*>(&HookShachiCostProperty)};
            for (std::size_t i = 0; i < detours.size(); ++i) {
                if (shachiCostHookCreated[i].load()) continue;
                if (!Executable(base + ShachiCostVmRvas[i]) || !ShachiCostEntryMatches(i, ShachiCostVmEntries[i])) return false;
                const auto status = MH_CreateHook(reinterpret_cast<void*>(base + ShachiCostVmRvas[i]), detours[i],
                    reinterpret_cast<void**>(&originalShachiCostVm[i]));
                gloss::Log(std::format("PBZ shachi cost hook {} prepare: {}", i, MH_StatusToString(status)));
                if (status != MH_OK) return false;
                shachiCostHookCreated[i].store(true);
            }
            return true;
        }
        bool InstallShachiCostHooks() {
            // Trampolines are prepared at startup, before game-thread requests.
            if (!shachiCostHookCreated[0].load() || !shachiCostHookCreated[1].load() || !ShachiCostWeakContract()) return false;
            for (std::size_t i = 0; i < ShachiCostVmRvas.size(); ++i) {
                if (shachiCostHookInstalled[i].load()) { if (!ShachiCostHookOwnership(i)) return false; continue; }
                if (!ShachiCostEntryMatches(i, ShachiCostVmEntries[i]) ||
                    MH_EnableHook(reinterpret_cast<void*>(base + ShachiCostVmRvas[i])) != MH_OK) return false;
                shachiCostHookInstalled[i].store(true);
                std::array<unsigned char, 18> patch{};
                if (!ReadBytes(base + ShachiCostVmRvas[i], patch.data(), patch.size()) ||
                    (shachiCostPatchCaptured[i] && patch != ownedShachiCostEntries[i])) return false;
                if (!shachiCostPatchCaptured[i]) { ownedShachiCostEntries[i] = patch; shachiCostPatchCaptured[i] = true; }
            }
            return ShachiCostHooksOwned();
        }
        bool ShachiCostCapability(const Context& context, std::string& reason) {
            ShachiCostContracts candidate;
            if (!shachiCostHookCreated[0].load() || !shachiCostHookCreated[1].load() ||
                !CaptureShachiCostContracts(context, candidate) || !ShachiCostWeakContract()) {
                reason = "本地能力列表、主动杀气消耗蓝图或 ActorInfo 契约尚未通过核对";
                return false;
            }
            for (std::size_t i = 0; i < ShachiCostVmRvas.size(); ++i) {
                if (!(shachiCostHookInstalled[i].load() ? ShachiCostHookOwnership(i) : ShachiCostEntryMatches(i, ShachiCostVmEntries[i]))) {
                    reason = "杀气消耗 VM 入口或钩子所有权已变化";
                    return false;
                }
            }
            if (shachiCostPreference.enabled && (candidate != shachiCostContracts || shachiCostPreference.uncertain ||
                !shachiCostPreference.Current(ShachiCostIdentity(context), shachiCostPreference.generation))) {
                reason = "杀气消耗绑定已变化或输出写入未确认；自动覆盖已停止";
                return false;
            }
            return true;
        }
        void ShachiCostSessionChanged() noexcept {
            shachiCostActive.store(false);
            shachiCostPreference.Stop();
            shachiCostContracts = {};
            shachiCostContractCache = {};
        }
        ExecutionResult SetShachiCost(const Context& context, bool enabled, double multiplier) {
            if (!enabled) {
                ShachiCostSessionChanged();
                return {RequestState::Verified, "已停止主动技能消耗结果覆盖；技能与杀气原始数据保持游戏值"};
            }
            std::string reason;
            ShachiCostContracts candidate;
            const auto identity = ShachiCostIdentity(context);
            if (!ShachiCostCapability(context, reason) || !CaptureShachiCostContracts(context, candidate, true) ||
                !InstallShachiCostHooks() || !shachiCostPreference.Commit(identity, multiplier,
                    ShachiCostHooksOwned(), ShachiCostIdentityCurrent(identity)))
                return {RequestState::Failed, reason.empty() ? "主动杀气消耗钩子或当前玩家绑定失败，未提交新倍率" : reason};
            shachiCostContracts = candidate;
            shachiCostActive.store(true);
            return {RequestState::Verified, "主动技能蓝图消耗输出覆盖已安装；受击削杀气不变，原生风雷转换尚未覆盖，需实战核验"};
        }
        ToggleState ShachiCostToggleState(const Context& context) {
            std::string reason;
            const bool known = ShachiCostCapability(context, reason);
            const bool enabled = known && shachiCostPreference.enabled && shachiCostActive.load();
            return {"shachi_cost", known, enabled, known ?
                std::format("已调整 {} 次主动技能蓝图消耗；受击不变，原生风雷转换未覆盖，需实战核验", shachiCostAdjustedQueries) : reason,
                enabled ? std::vector<std::string>{std::format("{:.9g}", shachiCostPreference.multiplier)} : std::vector<std::string>{}};
        }
        bool ShachiCostCleanup() {
            if (!LocalGameThread()) return false;
            ShachiCostSessionChanged();
            bool restored = true;
            for (std::size_t i = 0; i < ShachiCostVmRvas.size(); ++i) {
                if (!shachiCostHookInstalled[i].load()) continue;
                if (!ShachiCostHookOwnership(i)) { restored = false; continue; }
                const auto status = MH_DisableHook(reinterpret_cast<void*>(base + ShachiCostVmRvas[i]));
                if ((status != MH_OK && status != MH_ERROR_DISABLED) || !ShachiCostEntryMatches(i, ShachiCostVmEntries[i])) {
                    restored = false;
                    continue;
                }
                shachiCostHookInstalled[i].store(false);
                // Preserve immutable trampolines for callbacks already in flight.
            }
            return restored;
        }
