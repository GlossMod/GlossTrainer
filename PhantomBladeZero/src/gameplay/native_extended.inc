// Included after movement, item and damage adapters. Every mutation is scoped
// to the verified local world and only runs on its game thread.
        #include "native_resources.inc"
        bool WriteRemainingDays(Address address, int days) noexcept {
            __try { *reinterpret_cast<int*>(address) = days; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        struct EconomySwitch { std::string_view id; bool enabled{}; std::uint64_t prevented{}; };
        std::array<EconomySwitch, 3> economySwitches{{
            {"infinite_medicine"}, {"infinite_items"}, {"infinite_shop_stock"}}};
        using MedicineUse = void(__fastcall*)(Address, NameValue, std::int32_t);
        using PropsRemove = void(__fastcall*)(Address, std::int32_t, NameValue, bool);
        using ShopSell = void(__fastcall*)(Address, NameValue, const NameValue*, std::int32_t);
        MedicineUse originalMedicineUse{};
        PropsRemove originalPropsRemove{};
        ShopSell originalShopSell{};
        std::array<std::atomic<bool>, 3> economyHooks{}, economyHooksCreated{};
        std::array<bool, 3> economyPatchCaptured{};
        std::array<std::array<unsigned char, 16>, 3> ownedEconomyEntries{};
        struct EconomyBinding {
            Address object{}, type{}, world{}, pawn{}, subsystemKey{};
            std::uint32_t objectIndex{}, pawnIndex{};
            std::int32_t subsystemSlot{-1};
        };
        std::array<EconomyBinding, 3> economyBindings{};
        constexpr std::array<Address, 3> EconomyRvas{0x6974ca0, 0x696c480, 0x69a3180};
        constexpr std::array<std::array<unsigned char, 10>, 3> EconomyEntries{{
            {0x48,0x89,0x5c,0x24,0x18,0x55,0x57,0x41,0x56,0x48},
            {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10},
            {0x45,0x85,0xc9,0x0f,0x8e,0xab,0,0,0,0x48}}};

        Address ExtendedWorldSubsystem(const Context& context, std::string_view className) {
            ItemSparseMap map;
            const auto location = context.world + 0x9e0;
            if (!ItemReadMap(location, map) || map.count > 2048) return 0;
            for (int i = 0; i < map.count; ++i) {
                bool allocated{};
                ItemMapEntry entry;
                if (!ItemSlotAllocated(location, map, i, allocated)) return 0;
                if (!allocated) continue;
                if (!ItemReadMapEntry(map, i, entry)) return 0;
                const auto object = entry.value;
                if (ObjectValid(object) && ObjectName(Read<Address>(object + 0x10)) == className &&
                    Read<Address>(object + 0x20) == context.world) return object;
            }
            return 0;
        }
        bool EconomyEntryMatches(std::size_t index) noexcept {
            std::array<unsigned char, EconomyEntries[0].size()> entry{};
            return ReadBytes(base + EconomyRvas[index], entry.data(), entry.size()) && entry == EconomyEntries[index];
        }
        bool EconomyHookOwnership(std::size_t index) noexcept {
            std::array<unsigned char, 16> entry{};
            return economyHooks[index].load() && economyPatchCaptured[index] &&
                ReadBytes(base + EconomyRvas[index], entry.data(), entry.size()) && entry == ownedEconomyEntries[index];
        }
        bool EconomyBindingCurrent(std::size_t index, Address object) {
            // This path only reads cached identities and sparse-map metadata.
            // Reflection and names are resolved when the toggle is enabled.
            if (!CombatBindingCurrent()) return false;
            const auto& binding = economyBindings[index];
            if (object < 0x10000 || object != binding.object || binding.world != combatBinding.context.world ||
                binding.pawn != combatBinding.context.pawn || binding.pawnIndex != combatBinding.pawnIndex ||
                Read<std::uint32_t>(object + 0xc) != binding.objectIndex ||
                Read<Address>(object + 0x10) != binding.type || Read<Address>(object + 0x20) != binding.world ||
                (Read<std::uint32_t>(object + 8) & 0x40018000)) return false;
            ItemSparseMap map;
            const auto location = binding.world + 0x9e0;
            bool allocated{};
            ItemMapEntry entry;
            return ItemReadMap(location, map) && map.count <= 2048 && binding.subsystemSlot >= 0 &&
                binding.subsystemSlot < map.count && ItemSlotAllocated(location, map, binding.subsystemSlot, allocated) &&
                allocated && ItemReadMapEntry(map, binding.subsystemSlot, entry) && entry.value == object &&
                Read<Address>(map.data + static_cast<Address>(binding.subsystemSlot) * 24) == binding.subsystemKey;
        }
        bool EconomyShouldPrevent(std::size_t index, Address object, std::int32_t count) noexcept {
            try {
                // Gate plain game-thread state before reading it. Any failed
                // identity/ownership query preserves the original consumption.
                return count > 0 && !stopping.load() && !faulted.load() && LocalGameThread() &&
                    economySwitches[index].enabled && EconomyHookOwnership(index) && EconomyBindingCurrent(index, object);
            } catch (...) { return false; }
        }
        void __fastcall HookMedicineUse(Address object, NameValue item, std::int32_t count) {
            ++callbacksInFlight;
            __try {
                if (EconomyShouldPrevent(0, object, count)) {
                    count = 0; ++economySwitches[0].prevented;
                }
                originalMedicineUse(object, item, count);
            } __finally { --callbacksInFlight; }
        }
        void __fastcall HookPropsRemove(Address object, std::int32_t count, NameValue item, bool broadcast) {
            ++callbacksInFlight;
            __try {
                if (EconomyShouldPrevent(1, object, count)) {
                    count = 0; ++economySwitches[1].prevented;
                }
                originalPropsRemove(object, count, item, broadcast);
            } __finally { --callbacksInFlight; }
        }
        void __fastcall HookShopSell(Address object, NameValue group, const NameValue* item, std::int32_t count) {
            ++callbacksInFlight;
            __try {
                // This function only decrements the shop's stock and broadcasts.
                // Payment/grant happens in the caller and remains untouched.
                if (EconomyShouldPrevent(2, object, count)) {
                    count = 0; ++economySwitches[2].prevented;
                }
                originalShopSell(object, group, item, count);
            } __finally { --callbacksInFlight; }
        }
        bool EconomyContract(const Context& context, std::size_t index, EconomyBinding* captured = nullptr) {
            if (!CombatBindingCurrent() || context.pawn != combatBinding.context.pawn ||
                context.world != combatBinding.context.world) return false;
            const auto object = index == 2 ? ExtendedWorldSubsystem(context, "ShopSystem") : Backpack(context);
            if (!ObjectValid(object)) return false;
            const auto type = Read<Address>(object + 0x10);
            bool signature{};
            if (index == 0) signature = CombatSignature(FindFunction(type, "UseMedicine"), "UseMedicine", 0x65dac70, 0x04020401, 12, {
                {"MedicineID","NameProperty",0,8,0x18001040000280ull,""},
                {"Num","IntProperty",8,4,0x18001040000280ull,""}});
            if (index == 1) signature = CombatSignature(FindFunction(type, "RemovePropsFromBackPack"), "RemovePropsFromBackPack", 0x65d9440, 0x04020401, 13, {
                {"Num","IntProperty",0,4,0x18001040000280ull,""},
                {"ItemId","NameProperty",4,8,0x18001040000280ull,""},
                {"bSendBroadcast","BoolProperty",12,1,0x18001040000280ull,""}});
            if (index == 2) signature = CombatSignature(FindFunction(type, "SellShopItems"), "SellShopItems", 0x65aec20, 0x04420401, 20, {
                {"ShopGroupTag","StructProperty",0,8,0x18001000000080ull,"GameplayTag"},
                {"ShopItemID","NameProperty",8,8,0x18001048000382ull,""},
                {"SellNumber","IntProperty",16,4,0x18001040000280ull,""}});
            if (!signature || !Executable(base + EconomyRvas[index]) ||
                !(economyHooks[index].load() ? EconomyHookOwnership(index) : EconomyEntryMatches(index))) return false;
            const auto location = context.world + 0x9e0;
            ItemSparseMap map;
            if (!ItemReadMap(location, map) || map.count > 2048) return false;
            for (int slot = 0; slot < map.count; ++slot) {
                bool allocated{};
                ItemMapEntry entry;
                if (!ItemSlotAllocated(location, map, slot, allocated)) return false;
                if (!allocated) continue;
                if (!ItemReadMapEntry(map, slot, entry)) return false;
                if (entry.value != object) continue;
                ItemSparseMap after;
                const auto key = Read<Address>(map.data + static_cast<Address>(slot) * 24);
                if (!key || !ItemReadMap(location, after) || !ItemSameMap(map, after)) return false;
                if (captured) *captured = {object, type, context.world, context.pawn, key,
                    Read<std::uint32_t>(object + 0xc), combatBinding.pawnIndex, slot};
                return true;
            }
            return false;
        }
        bool PrepareEconomyHook(std::size_t index) {
            if (economyHooksCreated[index].load()) return true;
            if (!modulePinned.load() || !Executable(base + EconomyRvas[index]) || !EconomyEntryMatches(index)) {
                gloss::Log(std::format("PBZ {} hook prepare: entry verification failed", economySwitches[index].id));
                return false;
            }
            const std::array<void*, 3> callbacks{reinterpret_cast<void*>(&HookMedicineUse),
                reinterpret_cast<void*>(&HookPropsRemove), reinterpret_cast<void*>(&HookShopSell)};
            const std::array<void**, 3> originals{reinterpret_cast<void**>(&originalMedicineUse),
                reinterpret_cast<void**>(&originalPropsRemove), reinterpret_cast<void**>(&originalShopSell)};
            auto* address = reinterpret_cast<void*>(base + EconomyRvas[index]);
            const auto created = MH_CreateHook(address, callbacks[index], originals[index]);
            gloss::Log(std::format("PBZ {} hook prepare: {}", economySwitches[index].id, MH_StatusToString(created)));
            if (created != MH_OK) return false;
            economyHooksCreated[index].store(true);
            return true;
        }
        bool PrepareEconomyHooks() {
            bool complete = true;
            for (std::size_t i = 0; i < economyHooksCreated.size(); ++i)
                if (!PrepareEconomyHook(i)) complete = false;
            return complete;
        }
        bool InstallEconomyHook(std::size_t index) {
            if (economyHooks[index].load()) return EconomyHookOwnership(index);
            if (!PrepareEconomyHook(index) || !Executable(base + EconomyRvas[index]) || !EconomyEntryMatches(index)) return false;
            auto* address = reinterpret_cast<void*>(base + EconomyRvas[index]);
            const auto enabled = MH_EnableHook(address);
            gloss::Log(std::format("PBZ {} hook enable: {}", economySwitches[index].id, MH_StatusToString(enabled)));
            if (enabled != MH_OK) return false;
            std::array<unsigned char, 16> patch{};
            if (!ReadBytes(base + EconomyRvas[index], patch.data(), patch.size()) ||
                (economyPatchCaptured[index] && patch != ownedEconomyEntries[index])) {
                economyHooks[index].store(true);
                return false;
            }
            if (!economyPatchCaptured[index]) { ownedEconomyEntries[index] = patch; economyPatchCaptured[index] = true; }
            economyHooks[index].store(true);
            return EconomyHookOwnership(index);
        }
        bool RestoreExtendedHooks() {
            bool complete = true;
            for (std::size_t i = 0; i < economyHooks.size(); ++i) if (economyHooks[i].load()) {
                if (!EconomyHookOwnership(i)) { complete = false; continue; }
                const auto result = MH_DisableHook(reinterpret_cast<void*>(base + EconomyRvas[i]));
                if ((result != MH_OK && result != MH_ERROR_DISABLED) || !EconomyEntryMatches(i)) {
                    complete = false; continue;
                }
                economyHooks[i].store(false);
                // Keep trampolines allocated: late entrants are protected by the
                // process-lifetime DLL pin, as with the existing vtable callbacks.
            }
            return complete;
        }
        bool ExtendedCapability(const Context& context, std::string_view id, std::string& reason) {
            if (MovementFeature(id)) return MovementCapability(context, id, reason);
            if (DamageFeature(id)) return DamageCapability(context, id, reason);
            if (ResourcesFeature(id)) return ResourcesCapability(context, id, reason);
            if (CooldownFeature(id)) return CooldownCapability(context, id, reason);
            if (StealthFeature(id)) return StealthCapability(context, id, reason);
            if (ProgressFeature(id)) return ProgressCapability(context, id, reason);
            if (id == "remaining_days") {
                const bool ready = PropertyLayout(FindProperty(Read<Address>(context.pawn + 0x10), "RemainingDays"), "IntProperty", 4, 0x103c);
                reason = ready ? "玩家剩余天数字段已核对；存档由游戏正常保存" : "剩余天数字段未通过核对";
                return ready;
            }
            for (std::size_t i = 0; i < economySwitches.size(); ++i) if (economySwitches[i].id == id) {
                const bool valid = EconomyContract(context, i);
                reason = valid ? "扣减函数与本地背包/商店契约已核对" : "扣减函数或本地背包/商店契约未通过核对";
                return valid;
            }
            reason = "此功能的游戏结算入口尚未完成核对";
            return false;
        }
        ExecutionResult ExecuteExtended(const Context& context, const Request& request) {
            if (request.arguments.empty()) return {RequestState::Failed, "扩展功能参数缺失"};
            const auto& id = request.arguments[0];
            if (MovementFeature(id)) return ExecuteMovement(context, request);
            if (DamageFeature(id)) return ExecuteDamage(context, request);
            if (ResourcesFeature(id)) return ExecuteResources(context, request);
            if (CooldownFeature(id)) return ExecuteCooldown(context, request);
            if (StealthFeature(id)) return ExecuteStealth(context, request);
            if (ProgressFeature(id)) return ExecuteProgress(context, request);
            if (id == "remaining_days" && request.arguments.size() == 2) {
                std::string reason;
                const auto& text = request.arguments[1];
                int days{};
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), days);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || days < 0 || days > 66 ||
                    !ExtendedCapability(context, id, reason)) return {RequestState::Failed, "剩余天数参数或字段无效"};
                const auto index = Read<std::uint32_t>(context.pawn + 0xc);
                if (!WriteRemainingDays(context.pawn + 0x103c, days)) return {RequestState::Failed, "剩余天数写入失败"};
                const auto after = CurrentContext();
                if (!after || after->Key() != context.Key() || Read<std::uint32_t>(after->pawn + 0xc) != index ||
                    Read<int>(after->pawn + 0x103c) != days) return {RequestState::Indeterminate, "剩余天数写入后回读不符"};
                return {RequestState::Verified, std::format("玩家剩余天数回读 {}；由游戏正常保存进度", days)};
            }
            if (request.arguments.size() == 2 && (request.arguments[1] == "on" || request.arguments[1] == "off")) {
                for (std::size_t i = 0; i < economySwitches.size(); ++i) if (economySwitches[i].id == id) {
                    const bool enabled = request.arguments[1] == "on";
                    EconomyBinding candidate;
                    if (enabled && (!EconomyContract(context, i, &candidate) || !InstallEconomyHook(i)))
                        return {RequestState::Failed, "扣减入口核对或安装失败，未启用"};
                    economyBindings[i] = enabled ? candidate : EconomyBinding{};
                    economySwitches[i].enabled = enabled;
                    return {RequestState::Verified, enabled ? "扣减保护已安装；实际使用次数会在状态中回读" : "已停止扣减保护"};
                }
            }
            return {RequestState::Failed, "此功能的游戏结算入口尚未完成核对"};
        }
        std::vector<ToggleState> ExtendedToggleStates(const Context& context) {
            auto states = MovementToggleStates(context);
            for (auto& state : DamageToggleStates(context)) states.push_back(std::move(state));
            for (auto& state : ResourcesToggleStates(context)) states.push_back(std::move(state));
            for (auto& state : CooldownToggleStates(context)) states.push_back(std::move(state));
            for (auto& state : StealthToggleStates(context)) states.push_back(std::move(state));
            for (std::size_t i = 0; i < economySwitches.size(); ++i) {
                const auto& state = economySwitches[i];
                const bool known = EconomyContract(context, i) &&
                    (!state.enabled || EconomyBindingCurrent(i, economyBindings[i].object));
                states.push_back({std::string(state.id), known, known && state.enabled,
                    std::format("已保护 {} 次扣减；开关关闭后正常消耗", state.prevented)});
            }
            return states;
        }
        bool ExtendedSessionChanged(const Context& context) {
            DamageSessionChanged();
            CooldownSessionChanged();
            const bool restored = ResourcesSessionChanged(context);
            const bool stealth = StealthSessionChanged(context);
            for (auto& state : economySwitches) state.enabled = false;
            economyBindings = {};
            return restored && stealth;
        }
        bool ExtendedCleanup() {
            for (auto& state : economySwitches) state.enabled = false;
            economyBindings = {};
            const bool damage = DamageCleanup();
            const bool cooldown = CooldownCleanup();
            const bool resources = ResourcesCleanup();
            const bool stealth = StealthCleanup();
            return damage && cooldown && resources && stealth;
        }
