// PBZ CL-302256_JKS-17280. Included after native_items.inc. All calls run on
// the existing game-thread dispatcher; no equipment slots or inventory are written.
// Evidence: output/pbz-features-20261104/accessories/.
        struct AccessoryArray { Address data{}; std::int32_t count{}, capacity{}; };
        struct AccessoryHandle { std::int32_t id{-1}; std::uint8_t applied{}; std::uint8_t padding[3]{}; };
        static_assert(sizeof(AccessoryArray) == 16 && sizeof(AccessoryHandle) == 8);
        struct AccessoryObject {
            Address address{};
            std::int32_t index{-1}, serial{};
        };
        struct AccessoryEffect {
            NameValue item{};
            std::int32_t itemLevel{}, level{};
            Address entry{}, effectClass{}, definition{};
            bool highCycle{};
        };
        struct AccessoryContribution {
            AccessoryEffect effect;
            AccessoryHandle handle;
        };
        struct AccessoryActive {
            std::int32_t handle{-1};
            Address definition{};
            bool inhibited{};
        };
        struct AccessoryState {
            bool enabled{}, uncertain{}, synchronized{};
            Context context;
            AccessoryObject pawn, component, backpack;
            std::vector<AccessoryContribution> owned;
            std::size_t inventoryCount{}, equippedCount{}, desiredCount{}, skipped{};
            Clock::time_point next{};
            std::string reason{"尚未启用"};
        } accessories;

        bool AccessoryReadArray(Address address, AccessoryArray& array, int limit) {
            return ReadBytes(address, &array, sizeof(array)) && array.count >= 0 && array.count <= limit &&
                array.capacity >= array.count && array.capacity <= limit * 4 &&
                (!array.count || (array.data >= 0x10000 && !(array.data & 7)));
        }
        Address AccessoryObjectSlot(std::int32_t index) {
            if (index < 0 || index >= Read<std::int32_t>(base + 0xde28c70)) return 0;
            const auto chunks = Read<Address>(base + 0xde28c68);
            const auto chunk = Read<Address>(chunks + static_cast<Address>(index >> 16) * 8);
            return chunk ? chunk + static_cast<Address>(index & 65535) * 24 : 0;
        }
        AccessoryObject AccessoryIdentify(Address object) {
            if (!ObjectValid(object)) return {};
            const auto index = Read<std::int32_t>(object + 0xc);
            const auto slot = AccessoryObjectSlot(index);
            if (!slot || Read<Address>(slot + 8) != object) return {};
            return {object, index, Read<std::int32_t>(slot + 16)};
        }
        std::optional<bool> AccessoryIdentityStatus(const AccessoryObject& object) {
            if (!object.address || object.index < 0) return false;
            std::int32_t count{}, serial{};
            Address chunks{}, chunk{}, pointer{};
            if (!ReadBytes(base + 0xde28c70, &count, 4) || !ReadBytes(base + 0xde28c68, &chunks, 8)) return {};
            if (object.index >= count) return false;
            if (!ReadBytes(chunks + static_cast<Address>(object.index >> 16) * 8, &chunk, 8) || !chunk) return {};
            const auto slot = chunk + static_cast<Address>(object.index & 65535) * 24;
            if (!ReadBytes(slot + 8, &pointer, 8) || !ReadBytes(slot + 16, &serial, 4)) return {};
            return pointer == object.address && serial == object.serial;
        }
        bool AccessoryObjectCurrent(const AccessoryObject& object) {
            return AccessoryIdentityStatus(object).value_or(false) && ObjectValid(object.address);
        }
        bool AccessoryEntriesValid() {
            constexpr unsigned char normal[]{0x4d,0x85,0xc0,0x0f,0x84,0xff,0x02,0x00,0x00,0x4c,0x8b,0xdc};
            constexpr unsigned char refine[]{0x4d,0x85,0xc9,0x0f,0x84,0x03,0x03,0x00,0x00,0x4c,0x8b,0xdc};
            unsigned char bytes[12]{};
            return ReadBytes(base + 0x697fb30, bytes, sizeof(bytes)) && std::memcmp(bytes, normal, sizeof(bytes)) == 0 &&
                ReadBytes(base + 0x6980040, bytes, sizeof(bytes)) && std::memcmp(bytes, refine, sizeof(bytes)) == 0;
        }
        Address AccessoryRemoveFunction(Address component) {
            const auto function = FindFunction(Read<Address>(component + 0x10), "RemoveActiveGameplayEffect");
            return CombatSignature(function, "RemoveActiveGameplayEffect", 0x4cbd430, 0x04020406, 13, {
                {"Handle", "StructProperty", 0, 8, 0x18001000000080ull, "ActiveGameplayEffectHandle"},
                {"StacksToRemove", "IntProperty", 8, 4, 0x18001040000280ull, {}},
                {"ReturnValue", "BoolProperty", 12, 1, 0x18001040000780ull, {}}}) ? function : 0;
        }
        bool AccessoryActiveLayout(Address component) {
            const auto property = FindProperty(Read<Address>(component + 0x10), "ActiveGameplayEffects");
            if (!PropertyLayout(property, "StructProperty", 768, 0x8a8)) return false;
            const auto structure = Read<Address>(property + 0x70);
            const auto array = FindProperty(structure, "GameplayEffects_Internal");
            const auto inner = Read<Address>(array + 0x78);
            const auto active = Read<Address>(inner + 0x70);
            const auto spec = FindProperty(active, "Spec");
            return ObjectName(structure) == "ActiveGameplayEffectsContainer" &&
                PropertyLayout(array, "ArrayProperty", 16, 0x130) && ItemFieldType(inner, "StructProperty", 872) &&
                ObjectName(active) == "ActiveGameplayEffect" && PropertyLayout(spec, "StructProperty", 664, 24) &&
                PropertyLayout(FindProperty(Read<Address>(spec + 0x70), "Def"), "ObjectProperty", 8, 0) &&
                PropertyLayout(FindProperty(active, "bIsInhibited"), "BoolProperty", 1, 732);
        }
        bool AccessoriesReady(const Context& context, std::string& reason) {
            const auto backpack = Backpack(context), component = Read<Address>(context.pawn + 0x750);
            const auto type = Read<Address>(backpack + 0x10);
            if (!LocalGameThread() || stopping.load() || faulted.load() || !ObjectValid(backpack) || !ObjectValid(component) ||
                Read<Address>(component + 0x20) != context.pawn || !AccessoryEntriesValid() ||
                !PropertyLayout(FindProperty(Read<Address>(context.pawn + 0x10), "AbilitySystemComponent"), "ObjectProperty", 8, 0x750) ||
                !PropertyLayout(FindProperty(type, "Accessories"), "MapProperty", 80, 0x570) ||
                !PropertyLayout(FindProperty(type, "AssemblyAccessory"), "ArrayProperty", 16, 0x560) ||
                !PropertyLayout(FindProperty(type, "PreloadedInventoryClasses"), "ArrayProperty", 16, 0xd88) ||
                !ItemRowLayout(type, ItemTables[2]) || !AccessoryRemoveFunction(component) || !AccessoryActiveLayout(component)) {
                reason = "饰品背包、等级表或效果应用/撤销契约不匹配"; return false;
            }
            if (!AccessoryIdentify(context.pawn).address || !AccessoryIdentify(component).address || !AccessoryIdentify(backpack).address) {
                reason = "饰品对象身份未通过核对"; return false;
            }
            reason = "饰品效果契约已核对；只补充已拥有且未装备的饰品";
            return true;
        }
        bool AccessoryReadActive(Address component, std::vector<AccessoryActive>& effects) {
            AccessoryArray array;
            if (!AccessoryReadArray(component + 0x9d8, array, 4096)) return false;
            effects.clear(); effects.reserve(array.count);
            for (int i = 0; i < array.count; ++i) {
                const auto entry = array.data + static_cast<Address>(i) * 872;
                AccessoryActive effect;
                std::uint8_t inhibited{};
                if (!ReadBytes(entry + 12, &effect.handle, 4) || !ReadBytes(entry + 24, &effect.definition, 8) ||
                    !ReadBytes(entry + 732, &inhibited, 1) || effect.handle < 0 || !ObjectValid(effect.definition)) return false;
                effect.inhibited = inhibited != 0;
                effects.push_back(effect);
            }
            return true;
        }
        const AccessoryActive* AccessoryFindActive(const std::vector<AccessoryActive>& effects, std::int32_t handle) {
            const auto found = std::find_if(effects.begin(), effects.end(), [handle](const auto& effect) { return effect.handle == handle; });
            return found == effects.end() ? nullptr : &*found;
        }
        bool AccessorySameEffect(const AccessoryEffect& a, const AccessoryEffect& b) {
            return ItemSameName(a.item, b.item) && a.itemLevel == b.itemLevel && a.level == b.level &&
                a.entry == b.entry && a.effectClass == b.effectClass && a.definition == b.definition && a.highCycle == b.highCycle;
        }
        bool AccessoryPolicySafe(Address effectClass, std::string& reason) {
            const auto cdo = Read<Address>(effectClass + 0x160);
            if (!ObjectValid(effectClass) || !ObjectValid(cdo) || !ClassNamed(effectClass, "GameplayEffect") ||
                !PropertyLayout(FindProperty(effectClass, "DurationPolicy"), "EnumProperty", 1, 0x30) ||
                !PropertyLayout(FindProperty(effectClass, "StackingType"), "EnumProperty", 1, 0xbc1) ||
                Read<std::uint8_t>(cdo + 0x30) != 1 || Read<std::uint8_t>(cdo + 0xbc1) != 0) {
                reason = "效果不是无限时长且独立句柄的类型"; return false;
            }
            // No periodic execution, secondary applications, expiration effects or
            // arbitrary execution calculations: those cannot be undone by this ledger.
            if (Read<float>(cdo + 0x3f8) != 0.0f || Read<Address>(cdo + 0x400) || Read<Address>(cdo + 0x408)) {
                reason = "效果有周期执行配置"; return false;
            }
            // These legacy removal filters can delete somebody else's effects at
            // application time, which removing our own handle cannot restore.
            std::array<std::uint8_t, 408> removalQuery{};
            std::array<std::uint8_t, 96> removalTags{};
            if (!ReadBytes(cdo + 0xa28, removalQuery.data(), removalQuery.size()) ||
                !ReadBytes(cdo + 0x7a0, removalTags.data(), removalTags.size()) ||
                std::any_of(removalQuery.begin(), removalQuery.end(), [](auto value) { return value != 0; }) ||
                std::any_of(removalTags.begin(), removalTags.end(), [](auto value) { return value != 0; })) {
                reason = "效果包含移除其他效果的规则"; return false;
            }
            for (const auto offset : {0x438, 0x480, 0x490, 0x4a8, 0x4b8, 0xbd0}) {
                AccessoryArray array;
                if (!AccessoryReadArray(cdo + offset, array, 128) || array.count) {
                    reason = "效果含无法独立撤销的附加效果或旧式能力授予"; return false;
                }
            }
            AccessoryArray components;
            if (!AccessoryReadArray(cdo + 0xc40, components, 32)) return false;
            for (int i = 0; i < components.count; ++i) {
                const auto object = Read<Address>(components.data + static_cast<Address>(i) * 8);
                if (!ObjectValid(object)) return false;
                const auto type = Read<Address>(object + 0x10);
                const auto name = std::string(ObjectName(type));
                if (name == "TargetTagsGameplayEffectComponent") continue;
                if (name != "GrantAndActivateAbilities") { reason = "效果含未验证的组件"; return false; }
                const auto property = FindProperty(type, "GrantAbilityConfigs");
                const auto inner = Read<Address>(property + 0x78);
                const auto structure = Read<Address>(inner + 0x70);
                AccessoryArray configs;
                if (!PropertyLayout(property, "ArrayProperty", 16, 40) || !ItemFieldType(inner, "StructProperty", 56) ||
                    !PropertyLayout(FindProperty(structure, "RemovalPolicy"), "EnumProperty", 1, 52) ||
                    !AccessoryReadArray(object + 40, configs, 16)) return false;
                for (int j = 0; j < configs.count; ++j) {
                    // CancelAbilityImmediately (0), never DoNothing or RemoveOnEnd.
                    if (Read<std::uint8_t>(configs.data + static_cast<Address>(j) * 56 + 52) != 0) {
                        reason = "附带能力不支持随效果立即撤销"; return false;
                    }
                }
            }
            return true;
        }
        // Reproduce the game's 69aef10/69aef90 backward level selection. A level
        // index is one-based; an empty level falls back to the preceding level.
        bool AccessorySelect(Address levels, int level, int stride, Address& selected) {
            AccessoryArray array;
            selected = 0;
            if (level < 0 || level > 64 || !AccessoryReadArray(levels, array, 64)) return false;
            for (int i = (std::min)(level, array.count) - 1; i >= 0; --i) {
                const auto entry = array.data + static_cast<Address>(i) * stride;
                AccessoryArray effects;
                if (!AccessoryReadArray(entry, effects, 64)) return false;
                for (int j = 0; j < effects.count; ++j)
                    if (Read<std::uint64_t>(effects.data + static_cast<Address>(j) * (stride == 232 ? 144 : 88) + 8)) {
                        selected = entry; return true;
                    }
            }
            return true;
        }
        bool AccessoryCollectEffects(Address arrayAddress, NameValue item, int itemLevel, int level, bool high,
            const std::vector<Address>& classes, std::vector<AccessoryEffect>& desired, std::size_t& skipped, std::string& reason) {
            AccessoryArray array;
            if (!AccessoryReadArray(arrayAddress, array, 64)) return false;
            for (int i = 0; i < array.count; ++i) {
                const auto entry = array.data + static_cast<Address>(i) * (high ? 88 : 144);
                const auto package = Read<NameValue>(entry + 8), asset = Read<NameValue>(entry + 16);
                if (!package.comparison && !package.number) continue;
                Address effectClass{};
                for (const auto candidate : classes) {
                    const auto outer = Read<Address>(candidate + 0x20);
                    if (ItemSameName(asset, Read<NameValue>(candidate + 0x18)) &&
                        ItemSameName(package, Read<NameValue>(outer + 0x18))) {
                        if (effectClass && effectClass != candidate) return false;
                        effectClass = candidate;
                    }
                }
                const auto effectLevel = high ? level : Read<std::int32_t>(entry + 44);
                if (!effectClass || effectLevel <= 0 || effectLevel > 64 || !AccessoryPolicySafe(effectClass, reason)) {
                    ++skipped;
                    if (!effectClass) reason = "部分饰品效果尚未预载";
                    continue;
                }
                desired.push_back({item, itemLevel, effectLevel, entry, effectClass, Read<Address>(effectClass + 0x160), high});
            }
            return true;
        }
        bool AccessorySnapshot(Address backpack, std::vector<AccessoryEffect>& desired, std::string& reason) {
            const auto type = Read<Address>(backpack + 0x10);
            const auto rowProperty = FindProperty(FindFunction(type, "GetAccessoryData"), "ReturnValue");
            const auto rowType = Read<Address>(rowProperty + 0x70);
            const auto normalProperty = FindProperty(rowType, "AccessoryLevelDatas");
            const auto highProperty = FindProperty(rowType, "HighCycleAccessoryLevelDatas");
            const auto defaultProperty = FindProperty(rowType, "DefaultAccessoryData");
            const auto normalInner = Read<Address>(normalProperty + 0x78), highInner = Read<Address>(highProperty + 0x78);
            const auto geInner = Read<Address>(defaultProperty + 0x78);
            const auto geType = Read<Address>(geInner + 0x70);
            const auto normalGE = FindProperty(Read<Address>(normalInner + 0x70), "GEData");
            const auto highGE = FindProperty(Read<Address>(highInner + 0x70), "GEData");
            const auto highGEInner = Read<Address>(highGE + 0x78);
            if (!PropertyLayout(FindProperty(rowType, "MaxRefineLevel"), "IntProperty", 4, 0x158) ||
                !PropertyLayout(FindProperty(rowType, "AccessoryLevelDatas"), "ArrayProperty", 16, 0x160) ||
                !PropertyLayout(FindProperty(rowType, "DefaultAccessoryData"), "ArrayProperty", 16, 0x170) ||
                !PropertyLayout(FindProperty(rowType, "HighCycleAccessoryLevelDatas"), "ArrayProperty", 16, 0x180) ||
                !ItemFieldType(normalInner, "StructProperty", 232) || !ItemFieldType(highInner, "StructProperty", 104) ||
                !ItemFieldType(geInner, "StructProperty", 144) || !ItemFieldType(highGEInner, "StructProperty", 88) ||
                !PropertyLayout(normalGE, "ArrayProperty", 16, 0) || !PropertyLayout(highGE, "ArrayProperty", 16, 0) ||
                !PropertyLayout(FindProperty(geType, "SoftClassPtr"), "SoftClassProperty", 40, 0) ||
                !PropertyLayout(FindProperty(geType, "GELevel"), "IntProperty", 4, 44) ||
                !PropertyLayout(FindProperty(Read<Address>(highGEInner + 0x70), "GEClass"), "SoftClassProperty", 40, 0)) return false;
            AccessoryArray equipped, preloaded;
            ItemSparseMap inventory, table;
            if (!AccessoryReadArray(backpack + 0x560, equipped, 16) || !AccessoryReadArray(backpack + 0xd88, preloaded, 4096) ||
                !ItemReadMap(backpack + 0x570, inventory) || !ItemTableValid(ItemTables[2], table)) return false;
            std::vector<Address> classes;
            for (int i = 0; i < preloaded.count; ++i) {
                const auto object = Read<Address>(preloaded.data + static_cast<Address>(i) * 8);
                if (ObjectValid(object)) classes.push_back(object);
            }
            accessories.inventoryCount = accessories.equippedCount = accessories.skipped = 0;
            for (int i = 0; i < inventory.count; ++i) {
                bool allocated{};
                if (!ItemSlotAllocated(backpack + 0x570, inventory, i, allocated)) return false;
                if (!allocated) continue;
                ItemMapEntry item;
                if (!ItemReadMapEntry(inventory, i, item) || !ObjectValid(item.value) ||
                    !ClassNamed(Read<Address>(item.value + 0x10), "PBZAccessoryInfo") ||
                    !PropertyLayout(FindProperty(Read<Address>(item.value + 0x10), "CurrentLevel"), "IntProperty", 4, 40)) return false;
                ++accessories.inventoryCount;
                bool isEquipped{};
                for (int j = 0; j < equipped.count; ++j)
                    if (ItemSameName(item.id, Read<NameValue>(equipped.data + static_cast<Address>(j) * 8))) isEquipped = true;
                if (isEquipped) { ++accessories.equippedCount; continue; }
                Address row{};
                for (int j = 0; j < table.count; ++j) {
                    if (!ItemSlotAllocated(base + ItemTables[2].tableRva + 8, table, j, allocated)) return false;
                    if (!allocated) continue;
                    ItemMapEntry candidate;
                    if (!ItemReadMapEntry(table, j, candidate)) return false;
                    if (ItemSameName(candidate.id, item.id)) { row = candidate.value; break; }
                }
                if (!row || !ItemSameName(item.id, Read<NameValue>(row + 8))) return false;
                const auto level = Read<std::int32_t>(item.value + 40), maximum = Read<std::int32_t>(row + 0x158);
                if (level < 0 || level > 64 || maximum < 0 || maximum > 64) return false;
                Address normal{}, high{};
                if (!AccessorySelect(row + 0x160, (std::min)(level, maximum), 232, normal)) return false;
                if (!AccessoryCollectEffects(normal ? normal : row + 0x170, item.id, level, 1, false,
                    classes, desired, accessories.skipped, reason)) return false;
                if (level > maximum) {
                    if (!AccessorySelect(row + 0x180, level - maximum, 104, high)) return false;
                    if (high && !AccessoryCollectEffects(high, item.id, level, (std::min)(level - maximum, 6), true,
                        classes, desired, accessories.skipped, reason)) return false;
                }
            }
            ItemSparseMap after;
            return ItemReadMap(backpack + 0x570, after) && ItemSameMap(inventory, after);
        }
        bool AccessoryApplyLeaf(Address backpack, Address component, const AccessoryEffect& effect,
            AccessoryArray* output) noexcept {
            AccessoryArray input{effect.entry, 1, 1};
            __try {
                if (effect.highCycle)
                    reinterpret_cast<void(__fastcall*)(Address, const AccessoryArray*, int, Address, AccessoryArray*)>(base + 0x6980040)(
                        backpack, &input, effect.level, component, output);
                else
                    reinterpret_cast<void(__fastcall*)(Address, const AccessoryArray*, Address, AccessoryArray*)>(base + 0x697fb30)(
                        backpack, &input, component, output);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        void AccessoryFault(std::string reason) {
            accessories.uncertain = true;
            accessories.synchronized = false;
            accessories.reason = std::move(reason);
            faulted.store(true);
            GetBackend().SetRuntimeState(false, accessories.reason);
        }
        bool AccessoryRemoveOwned(std::size_t index) {
            if (index >= accessories.owned.size()) return false;
            if (stopping.load() || faulted.load() || !LocalGameThread()) {
                accessories.reason = "饰品引擎调用已停止，保留撤销记录"; return false;
            }
            const auto identity = AccessoryIdentityStatus(accessories.component);
            if (!identity) { accessories.reason = "原饰品对象身份无法读取，保留撤销记录"; return false; }
            // Erase only this entry: reconciliation iterates backward by index.
            // Clearing the entire ledger here would invalidate its next index.
            if (!*identity) { accessories.owned.erase(accessories.owned.begin() + index); return true; }
            if (!ObjectValid(accessories.component.address)) {
                accessories.reason = "原饰品对象尚未销毁且不可调用，保留撤销记录"; return false;
            }
            const auto component = accessories.component.address;
            const auto function = AccessoryRemoveFunction(component);
            std::vector<AccessoryActive> before;
            if (!function || !AccessoryActiveLayout(component) || !AccessoryReadActive(component, before)) return false;
            const auto& owned = accessories.owned[index];
            const auto current = AccessoryFindActive(before, owned.handle.id);
            if (!current) { accessories.owned.erase(accessories.owned.begin() + index); return true; }
            if (current->definition != owned.effect.definition) {
                accessories.reason = "饰品效果句柄身份变化，保留撤销记录"; return false;
            }
            struct Parameters { AccessoryHandle handle; std::int32_t stacks{-1}; std::uint8_t result{}; } parameters;
            parameters.handle = owned.handle;
            if (!CallProcessEvent(component, function, &parameters)) {
                AccessoryFault("饰品效果撤销异常，已停止调用并保留记录"); return false;
            }
            ++dispatchedCalls;
            std::vector<AccessoryActive> after;
            if (!AccessoryReadActive(component, after) || AccessoryFindActive(after, owned.handle.id)) {
                accessories.reason = "饰品效果尚未确认撤销，保留记录"; return false;
            }
            accessories.owned.erase(accessories.owned.begin() + index);
            return true;
        }
        bool AccessoriesCleanup() {
            accessories.enabled = false;
            accessories.synchronized = false;
            if ((!accessories.owned.empty() || accessories.uncertain) &&
                (stopping.load() || faulted.load() || !LocalGameThread())) {
                accessories.reason = "饰品引擎调用已停止，保留撤销记录"; return false;
            }
            while (!accessories.owned.empty()) if (!AccessoryRemoveOwned(accessories.owned.size() - 1)) return false;
            if (accessories.uncertain && AccessoryIdentityStatus(accessories.component) != std::optional<bool>{false}) return false;
            accessories.uncertain = false;
            accessories.synchronized = true;
            accessories.reason = "已关闭，修改器添加的饰品效果已撤销";
            return true;
        }
        bool AccessoriesSessionChanged(const Context&) {
            // The adapter replays persisted toggles only after successful cleanup.
            return AccessoriesCleanup();
        }
        bool AccessoryReconcile(const Context& context) {
            accessories.synchronized = false;
            if (accessories.uncertain || faulted.load() || !LocalGameThread()) return false;
            std::string reason;
            if (!AccessoriesReady(context, reason)) { accessories.reason = reason; return false; }
            const auto backpack = Backpack(context), component = Read<Address>(context.pawn + 0x750);
            if ((!accessories.owned.empty() && (!accessories.context.SameKey(context) ||
                accessories.component.address != component || !AccessoryObjectCurrent(accessories.component)))) {
                const bool wanted = accessories.enabled;
                if (!AccessoriesCleanup()) return false;
                accessories.enabled = wanted;
            }
            accessories.context = context;
            accessories.pawn = AccessoryIdentify(context.pawn);
            accessories.component = AccessoryIdentify(component);
            accessories.backpack = AccessoryIdentify(backpack);
            std::vector<AccessoryEffect> desired;
            if (!AccessorySnapshot(backpack, desired, reason)) {
                accessories.reason = "饰品库存或等级数据暂不可读，保留现有效果记录"; return false;
            }
            accessories.desiredCount = desired.size();
            for (std::size_t i = accessories.owned.size(); i-- > 0;) {
                const auto& owned = accessories.owned[i];
                if (std::none_of(desired.begin(), desired.end(), [&](const auto& effect) { return AccessorySameEffect(effect, owned.effect); }))
                    if (!AccessoryRemoveOwned(i)) return false;
            }
            std::vector<AccessoryActive> active;
            if (!AccessoryReadActive(component, active)) { accessories.reason = "活动饰品效果无法回读"; return false; }
            for (std::size_t i = accessories.owned.size(); i-- > 0;) {
                const auto current = AccessoryFindActive(active, accessories.owned[i].handle.id);
                if (!current) accessories.owned.erase(accessories.owned.begin() + i);
                else if (current->definition != accessories.owned[i].effect.definition) {
                    accessories.reason = "饰品句柄与原定义不一致，暂停补充"; return false;
                }
            }
            accessories.owned.reserve(accessories.owned.size() + desired.size());
            for (const auto& effect : desired) {
                if (std::any_of(accessories.owned.begin(), accessories.owned.end(), [&](const auto& owned) { return AccessorySameEffect(effect, owned.effect); })) continue;
                if (std::any_of(active.begin(), active.end(), [&](const auto& found) { return found.definition == effect.definition; })) {
                    ++accessories.skipped; reason = "已有同类外部效果，未重复添加"; continue;
                }
                if (!AccessoryObjectCurrent(accessories.pawn) || !AccessoryObjectCurrent(accessories.component) ||
                    !AccessoryObjectCurrent(accessories.backpack)) { accessories.reason = "饰品应用前玩家对象变化"; return false; }
                // A one-element output buffer prevents engine allocation. The helper
                // appends one trivial handle; no GETextData copies need destruction.
                accessories.owned.push_back({effect, {}});
                auto& contribution = accessories.owned.back();
                AccessoryArray output{reinterpret_cast<Address>(&contribution.handle), 0, 1};
                const bool returned = AccessoryApplyLeaf(backpack, component, effect, &output);
                ++dispatchedCalls;
                std::vector<AccessoryActive> after;
                const bool readback = AccessoryReadActive(component, after);
                std::vector<AccessoryActive> added;
                if (readback) for (const auto& found : after)
                    if (found.definition == effect.definition && !AccessoryFindActive(active, found.handle)) added.push_back(found);
                // Even when a helper faults after application but before appending,
                // preserve a uniquely attributable newly-created handle for diagnosis.
                const auto returnedHandle = contribution.handle;
                if (added.size() == 1) contribution.handle = {added.front().handle, 1, {}};
                const bool outputValid = output.data == reinterpret_cast<Address>(&contribution.handle) &&
                    output.capacity == 1 && (output.count == 0 || output.count == 1);
                const bool verified = returned && readback && outputValid && output.count == 1 && added.size() == 1 &&
                    returnedHandle.applied && returnedHandle.id == added.front().handle;
                if (!returned || !readback || !outputValid || added.size() > 1 || (!verified && !added.empty()) ||
                    (output.count == 1 && (added.empty() || returnedHandle.id != added.front().handle))) {
                    AccessoryFault("饰品应用结果无法唯一确认，已停止调用并保留记录"); return false;
                }
                if (!verified) {
                    if (added.empty()) accessories.owned.pop_back();
                    ++accessories.skipped; reason = "部分饰品效果被游戏条件拒绝";
                }
                active = std::move(after);
            }
            const auto inhibited = std::count_if(accessories.owned.begin(), accessories.owned.end(), [&](const auto& owned) {
                const auto current = AccessoryFindActive(active, owned.handle.id); return current && current->inhibited;
            });
            accessories.reason = std::format("背包 {} 件，已装备 {} 件；补充 {} 个效果", accessories.inventoryCount,
                accessories.equippedCount, accessories.owned.size());
            if (accessories.skipped) accessories.reason += std::format("；跳过 {} 项：{}", accessories.skipped, reason);
            if (inhibited) accessories.reason += std::format("；{} 个效果受游戏条件抑制", inhibited);
            accessories.synchronized = true;
            return true;
        }
        void AccessoriesTick(const Context& context) {
            if (!accessories.enabled || stopping.load() || faulted.load() || Clock::now() < accessories.next) return;
            accessories.next = Clock::now() + std::chrono::seconds(1);
            AccessoryReconcile(context);
        }
        ToggleState AccessoriesToggleState(const Context& context) {
            if (accessories.enabled || !accessories.owned.empty() || accessories.uncertain)
                return {"all_accessories", !accessories.uncertain && accessories.synchronized &&
                    !stopping.load() && !faulted.load() && (accessories.enabled || accessories.owned.empty()),
                    accessories.enabled || !accessories.owned.empty(), accessories.reason};
            std::string reason;
            const bool ready = AccessoriesReady(context, reason);
            return {"all_accessories", ready, false, ready ? accessories.reason : reason};
        }
        ExecutionResult ExecuteAccessories(const Context& context, const Request& request) {
            if (request.arguments.size() != 1 || (request.arguments[0] != "on" && request.arguments[0] != "off"))
                return {RequestState::Failed, "全部饰品生效只接受 on/off"};
            if (request.arguments[0] == "off") {
                const bool clean = AccessoriesCleanup();
                return {clean ? RequestState::Verified : RequestState::Indeterminate, accessories.reason};
            }
            std::string reason;
            if (!AccessoriesReady(context, reason)) {
                accessories.synchronized = false; accessories.reason = reason;
                return {RequestState::Failed, reason};
            }
            accessories.enabled = true;
            const bool reconciled = AccessoryReconcile(context);
            accessories.next = Clock::now() + std::chrono::seconds(1);
            return {reconciled ? RequestState::Verified : RequestState::Indeterminate, accessories.reason};
        }
