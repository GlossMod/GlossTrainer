// Included after native_accessories.inc and native_extended.inc. Uses the game's
// removal event and effect APIs; no inventory quantity/attribute memory writes.
        struct InventoryAttributeEffect {
            AccessoryHandle handle;
            Address definition{};
            std::int32_t level{};
        };
        struct InventoryAttributePlan {
            AccessoryObject pawn, component, backpack, effectObject;
            Address definition{}, levelFunction{}, removeFunction{};
            std::vector<InventoryAttributeEffect> effects;
        };
        bool InventoryEditCapability(const Context& context, std::string& reason) {
            if (stopping.load() || faulted.load() || !LocalGameThread() || !itemCatalog.complete ||
                itemCatalog.session != context.Key() || !ItemBackpackMatches(context, itemCatalog.backpack) ||
                !EconomyPropLayout(itemCatalog.backpack)) {
                reason = "当前会话物品目录尚未就绪"; return false;
            }
            const auto function = FindFunction(Read<Address>(itemCatalog.backpack + 0x10), "RemovePropsFromBackPack");
            const bool ready = CombatSignature(function, "RemovePropsFromBackPack", 0x66221b0, 0x04020401, 13, {
                {"Num", "IntProperty", 0, 4, 0x18001040000280ull, {}},
                {"ItemId", "NameProperty", 4, 8, 0x18001040000280ull, {}},
                {"bSendBroadcast", "BoolProperty", 12, 1, 0x18001040000280ull, {}}});
            reason = ready ? "原生背包减量接口已核对；永久属性物品需另核对其效果句柄" : "背包减量接口契约不匹配";
            return ready;
        }
        Address InventoryLevelFunction(Address component) {
            const auto function = FindFunction(Read<Address>(component + 0x10), "SetActiveGameplayEffectLevel");
            return Read<Address>(Read<Address>(component) + 0x710) == base + 0x4ca69c0 &&
                CombatSignature(function, "SetActiveGameplayEffectLevel", 0x4cbf4a0, 0x04020406, 12, {
                {"ActiveHandle", "StructProperty", 0, 8, 0x18001000000080ull, "ActiveGameplayEffectHandle"},
                {"NewLevel", "IntProperty", 8, 4, 0x18001040000280ull, {}}}) ? function : 0;
        }
        bool InventoryAttributePolicy(Address definition) {
            const auto type = Read<Address>(definition + 0x10);
            if (!ObjectValid(definition) || !ClassNamed(type, "GameplayEffect") || Read<Address>(type + 0x160) != definition ||
                !PropertyLayout(FindProperty(type, "DurationPolicy"), "EnumProperty", 1, 0x30) ||
                !PropertyLayout(FindProperty(type, "StackingType"), "EnumProperty", 1, 0xbc1) ||
                Read<std::uint8_t>(definition + 0x30) != 1 || Read<std::uint8_t>(definition + 0xbc1) != 0 ||
                Read<float>(definition + 0x3f8) != 0.0f || Read<Address>(definition + 0x400) || Read<Address>(definition + 0x408)) return false;
            // This path only changes/removes an already active GE. Its application-
            // only regeneration effect is not replayed. Expiration effects, cues,
            // periodic execution and granted abilities remain disallowed.
            constexpr std::pair<const char*, int> emptyArrays[]{
                {"Executions", 0x438}, {"OverflowEffects", 0x490}, {"PrematureExpirationEffectClasses", 0x4a8},
                {"RoutineExpirationEffectClasses", 0x4b8}, {"GameplayCues", 0x4d0}, {"GrantedAbilities", 0xbd0}};
            for (const auto& [name, offset] : emptyArrays) {
                AccessoryArray array;
                if (!PropertyLayout(FindProperty(type, name), "ArrayProperty", 16, offset) ||
                    !AccessoryReadArray(definition + offset, array, 128) || array.count) return false;
            }
            AccessoryArray components;
            if (!PropertyLayout(FindProperty(type, "GEComponents"), "ArrayProperty", 16, 0xc40) ||
                !AccessoryReadArray(definition + 0xc40, components, 16)) return false;
            for (int i = 0; i < components.count; ++i) {
                const auto object = Read<Address>(components.data + static_cast<Address>(i) * 8);
                const auto componentType = Read<Address>(object + 0x10);
                if (!ObjectValid(object) || ObjectName(componentType) != "AdditionalEffectsGameplayEffectComponent") return false;
                constexpr std::pair<const char*, int> completionArrays[]{
                    {"OnCompleteAlways", 64}, {"OnCompleteNormal", 80}, {"OnCompletePrematurely", 96}};
                for (const auto& [name, offset] : completionArrays) {
                    AccessoryArray array;
                    if (!PropertyLayout(FindProperty(componentType, name), "ArrayProperty", 16, offset) ||
                        !AccessoryReadArray(object + offset, array, 128) || array.count) return false;
                }
            }
            return true;
        }
        bool InventoryAttributeLayout(Address backpack, Address component) {
            const auto pending = FindProperty(Read<Address>(backpack + 0x10), "ActivateGE");
            const auto container = FindProperty(Read<Address>(component + 0x10), "ActiveGameplayEffects");
            const auto array = FindProperty(Read<Address>(container + 0x70), "GameplayEffects_Internal");
            const auto active = Read<Address>(Read<Address>(array + 0x78) + 0x70);
            const auto spec = Read<Address>(FindProperty(active, "Spec") + 0x70);
            return PropertyLayout(pending, "MapProperty", 80, 0x8b8) &&
                ItemFieldType(Read<Address>(pending + 0x70), "ClassProperty", 8) &&
                ItemFieldType(Read<Address>(pending + 0x78), "IntProperty", 4) &&
                PropertyLayout(FindProperty(spec, "StackCount"), "IntProperty", 4, 448) &&
                PropertyLayout(FindProperty(spec, "Level"), "FloatProperty", 4, 656);
        }
        bool InventoryAttributeItemCurrent(Address backpack, NameValue item, std::int64_t quantity) {
            const auto location = backpack + 0x510;
            ItemSparseMap map;
            if (!ItemReadMap(location, map) || map.count > 8192) return false;
            for (int i = 0; i < map.count; ++i) {
                bool allocated{};
                ItemMapEntry entry;
                if (!ItemSlotAllocated(location, map, i, allocated)) return false;
                if (!allocated) continue;
                if (!ItemReadMapEntry(map, i, entry)) return false;
                if (!ItemSameName(entry.id, item)) continue;
                const auto type = Read<Address>(entry.value + 0x10);
                return ObjectValid(entry.value) && ClassNamed(type, "PBZInventoryInfo") &&
                    PropertyLayout(FindProperty(type, "bIsAddAttributeItem"), "BoolProperty", 1, 0x3a1) &&
                    Read<std::uint8_t>(entry.value + 0x3a1) == 1 && Read<std::int32_t>(entry.value + 0x380) == quantity &&
                    ItemSameName(Read<NameValue>(entry.value + 0x388), item);
            }
            return false;
        }
        bool InventoryReadAttributeEffects(Address backpack, Address component, Address definition,
            std::vector<InventoryAttributeEffect>& effects) {
            // 698771e appends only inventory attribute-effect handles to +b38.
            // Never discover ownership merely from a matching GE class.
            AccessoryArray handles, active;
            if (!AccessoryReadArray(backpack + 0xb38, handles, 4096) || !AccessoryReadArray(component + 0x9d8, active, 4096)) return false;
            effects.clear();
            for (int i = 0; i < handles.count; ++i) {
                AccessoryHandle handle;
                if (!ReadBytes(handles.data + static_cast<Address>(i) * 8, &handle, sizeof(handle))) return false;
                if (handle.id < 0) continue;
                for (int j = 0; j < active.count; ++j) {
                    const auto entry = active.data + static_cast<Address>(j) * 872;
                    std::int32_t activeHandle{}, stack{};
                    Address activeDefinition{};
                    float level{}, duration{};
                    if (!ReadBytes(entry + 12, &activeHandle, 4) || !ReadBytes(entry + 24, &activeDefinition, 8)) return false;
                    if (activeHandle != handle.id || activeDefinition != definition) continue;
                    if (!ReadBytes(entry + 680, &level, 4) || !ReadBytes(entry + 104, &duration, 4) ||
                        !ReadBytes(entry + 472, &stack, 4) || duration != -1.0f || stack != 1 ||
                        !std::isfinite(level) || level < 1.0f || level > 1000000.0f || std::floor(level) != level ||
                        std::any_of(effects.begin(), effects.end(), [&](const auto& e) { return e.handle.id == handle.id; })) return false;
                    effects.push_back({{handle.id, 1, {}}, definition, static_cast<std::int32_t>(level)});
                }
            }
            return true;
        }
        bool InventoryAttributePlanCurrent(const InventoryAttributePlan& plan) {
            return AccessoryObjectCurrent(plan.pawn) && AccessoryObjectCurrent(plan.component) &&
                AccessoryObjectCurrent(plan.backpack) && AccessoryObjectCurrent(plan.effectObject) &&
                Read<Address>(plan.pawn.address + 0x750) == plan.component.address &&
                Read<Address>(plan.component.address + 0x20) == plan.pawn.address;
        }
        bool InventoryPrepareAttributes(const Context& context, const NativeItem& item, std::int64_t quantity, std::int32_t target,
            InventoryAttributePlan& plan, std::string& reason) {
            const auto id = Name(item.id.comparison);
            if (!id || item.id.number || (*id != "Item_001" && *id != "Item_002")) {
                reason = "该永久强化物品尚无已验证的属性撤销路径"; return false;
            }
            const auto expected = *id == "Item_001" ? "GE_ShachiCore_C" : "GE_SecondWind_C";
            const auto expectedPackage = *id == "Item_001" ?
                "/Game/PBZContent/Blueprint/Item/GameplayEffect/Permanent/GE_ShachiCore" :
                "/Game/PBZContent/Blueprint/Item/GameplayEffect/Permanent/GE_SecondWind";
            const auto package = Read<NameValue>(item.row + 0x328), asset = Read<NameValue>(item.row + 0x330);
            if (!NameIs(package.comparison, expectedPackage) || package.number || !NameIs(asset.comparison, expected) || asset.number ||
                !PlayerAttributesReady(context) || !AccessoryActiveLayout(Read<Address>(context.pawn + 0x750)) ||
                !InventoryAttributeLayout(itemCatalog.backpack, Read<Address>(context.pawn + 0x750))) {
                reason = "永久强化物品的原生效果类型或玩家属性布局不匹配"; return false;
            }
            const auto backpack = itemCatalog.backpack;
            if (!InventoryAttributeItemCurrent(backpack, item.id, quantity)) {
                reason = "永久属性道具的背包标记与库存不匹配"; return false;
            }
            ItemSparseMap pending;
            if (!ItemReadMap(backpack + 0x8b8, pending) || pending.count != 0) {
                reason = "游戏正在结算其他永久属性物品，请结算完成后再操作"; return false;
            }
            const auto component = Read<Address>(context.pawn + 0x750);
            std::vector<AccessoryActive> active;
            if (!AccessoryReadActive(component, active)) return false;
            Address definition{};
            for (const auto& effect : active) {
                const auto effectClass = Read<Address>(effect.definition + 0x10), outer = Read<Address>(effectClass + 0x20);
                if (ItemSameName(asset, Read<NameValue>(effectClass + 0x18)) && ItemSameName(package, Read<NameValue>(outer + 0x18))) {
                    if (definition && definition != effect.definition) return false;
                    definition = effect.definition;
                }
            }
            if (!definition || !InventoryAttributePolicy(definition)) {
                reason = "永久属性效果未找到，或含不支持的副作用"; return false;
            }
            plan.pawn = AccessoryIdentify(context.pawn);
            plan.component = AccessoryIdentify(component);
            plan.backpack = AccessoryIdentify(backpack);
            plan.effectObject = AccessoryIdentify(definition);
            plan.definition = definition;
            plan.levelFunction = InventoryLevelFunction(component);
            plan.removeFunction = AccessoryRemoveFunction(component);
            if (!plan.levelFunction || !plan.removeFunction || !InventoryAttributePlanCurrent(plan) ||
                !InventoryReadAttributeEffects(backpack, component, definition, plan.effects)) return false;
            std::int64_t levels{};
            for (const auto& effect : plan.effects) levels += effect.level;
            if (levels != quantity || plan.effects.empty()) {
                reason = "永久物品库存与背包所属效果级数不一致，未改动"; return false;
            }
            if (target > 0 && plan.effects.size() != 1) {
                reason = "永久物品由多个独立效果组成，尚不能安全重分配曲线级数；当前只支持清零"; return false;
            }
            return true;
        }
        bool InventoryAdjustAttributes(const InventoryAttributePlan& plan, std::int32_t target, std::string& reason) {
            // Never merge distinct grants: their level curves may be nonlinear.
            // Partial reduction requires one existing inventory-owned grant; zero
            // removes each exact inventory-owned handle without reapplying a GE.
            auto expected = plan.effects;
            for (std::size_t i = plan.effects.size(); i-- > 0;) {
                if (!InventoryAttributePlanCurrent(plan)) { reason = "属性调整期间玩家身份变化"; return false; }
                std::vector<InventoryAttributeEffect> before;
                if (!InventoryReadAttributeEffects(plan.backpack.address, plan.component.address, plan.definition, before) ||
                    before.size() != expected.size() || !std::equal(before.begin(), before.end(), expected.begin(),
                        [](const auto& a, const auto& b) { return a.handle.id == b.handle.id && a.definition == b.definition && a.level == b.level; })) {
                    reason = "属性调整前游戏所属效果发生变化"; return false;
                }
                const auto& effect = plan.effects[i];
                if (!i && target > 0) {
                    if (effect.level == target) continue;
                    struct Parameters { AccessoryHandle handle; std::int32_t level; } parameters{effect.handle, target};
                    if (!CallProcessEvent(plan.component.address, plan.levelFunction, &parameters)) {
                        reason = "永久物品属性级数调整发生原生异常"; return false;
                    }
                    expected[i].level = target;
                } else {
                    struct Parameters { AccessoryHandle handle; std::int32_t stacks; std::uint8_t result{}; } parameters{effect.handle, -1, 0};
                    if (!CallProcessEvent(plan.component.address, plan.removeFunction, &parameters) || !parameters.result) {
                        reason = "永久物品属性撤销发生原生异常"; return false;
                    }
                    expected.erase(expected.begin() + i);
                }
                ++dispatchedCalls;
                std::vector<InventoryAttributeEffect> after;
                if (!InventoryReadAttributeEffects(plan.backpack.address, plan.component.address, plan.definition, after)) {
                    reason = "永久物品属性调整后无法回读"; return false;
                }
                const auto found = std::find_if(after.begin(), after.end(), [&](const auto& e) { return e.handle.id == effect.handle.id; });
                if ((!i && target > 0) ? (found == after.end() || found->level != target) : found != after.end()) {
                    reason = "永久物品属性调整返回但回读不符"; return false;
                }
            }
            std::vector<InventoryAttributeEffect> after;
            if (!InventoryReadAttributeEffects(plan.backpack.address, plan.component.address, plan.definition, after)) return false;
            std::int64_t levels{};
            for (const auto& effect : after) levels += effect.level;
            if (levels != target) { reason = "永久物品属性调整后的总级数与目标不符"; return false; }
            return true;
        }
        bool InventoryRemoveLeaf(Address backpack, Address function, void* parameters) noexcept {
            ++inventoryEditDepth;
            bool result{};
            __try { result = CallProcessEvent(backpack, function, parameters); }
            __finally { --inventoryEditDepth; }
            return result;
        }
        ExecutionResult ExecuteInventoryEdit(const Context& context, const Request& request) {
            std::int32_t target{};
            if (request.featureId != "set_item_quantity" || request.arguments.size() != 2 ||
                request.arguments[0].empty() || request.arguments[0].size() > 128 || !ParseInventoryTarget(request.arguments[1], target))
                return {RequestState::Failed, "目标数量必须是 0 至 2147483647 的整数"};
            std::string reason;
            if (!InventoryEditCapability(context, reason)) return {RequestState::Failed, reason};
            const auto found = std::find_if(itemCatalog.rows.begin(), itemCatalog.rows.end(),
                [&](const auto& item) { return item.entry.key == request.arguments[0]; });
            if (found == itemCatalog.rows.end() || !found->named || !ItemCurrentRow(*found))
                return {RequestState::Failed, "物品不在当前已核验目录中"};
            const auto item = *found;
            const auto backpack = itemCatalog.backpack;
            const auto before = ItemOwned(backpack, item);
            if (!before || *before < 0 || *before > std::numeric_limits<std::int32_t>::max())
                return {RequestState::Failed, "当前库存数量无法核对，未执行"};
            if (target > *before) {
                auto grant = request;
                grant.featureId = "grant_item";
                grant.arguments[1] = std::to_string(static_cast<std::int64_t>(target) - *before);
                return GrantItem(context, grant);
            }
            const auto amount = before ? InventoryRemovalAmount(*before, target) : std::nullopt;
            if (!amount) return {RequestState::Failed, "目标数量超过当前库存；增加物品请使用获取操作"};
            if (!*amount) return {RequestState::Verified, "当前库存已等于目标数量，无需修改"};
            if (item.kind != ItemKind::Prop)
                return {RequestState::Failed, "装备、饰品与药品需专用卸下和删除路径，当前未执行"};
            InventoryRowPolicy policy;
            if (!EconomyReadPropPolicy(item.row, policy)) return {RequestState::Failed, "物品分类无法核对"};
            const bool attribute = policy.attributeEffect;
            InventoryAttributePlan plan;
            reason.clear();
            if (attribute) {
                if (policy.itemType != 0 || policy.detailType != 5 || policy.materialType != 0 || policy.integration ||
                    policy.flow || policy.dialogue || policy.ability || !InventoryPrepareAttributes(context, item, *before, target, plan, reason))
                    return {RequestState::Failed, reason.empty() ? "永久物品的属性撤销契约未通过，未改动" : reason};
            } else if (!InventoryOrdinaryReduction(policy)) {
                return {RequestState::Failed, "该物品关联剧情、合成、记忆花或永久强化流程，尚无完整撤销契约"};
            }
            // Recheck before the first mutation. No automatic retry is possible
            // once a multi-step attribute/quantity transaction begins.
            if (!ItemBackpackMatches(context, backpack) || !ItemCurrentRow(item) || ItemOwned(backpack, item) != before)
                return {RequestState::Failed, "提交前玩家或库存变化，未执行"};
            const auto function = FindFunction(Read<Address>(backpack + 0x10), "RemovePropsFromBackPack");
            if (!InventoryEditCapability(context, reason)) return {RequestState::Failed, reason};
            reason.clear();
            if (attribute && !InventoryAdjustAttributes(plan, target, reason)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "永久物品减量中断，属性结果无法确认，已停止适配器");
                return {RequestState::Indeterminate, reason + "；库存未继续扣减，不自动重试"};
            }
            if (!ItemBackpackMatches(context, backpack) || !ItemCurrentRow(item) || ItemOwned(backpack, item) != before ||
                (attribute && !InventoryAttributePlanCurrent(plan))) {
                if (attribute) {
                    faulted.store(true);
                    GetBackend().SetRuntimeState(false, "永久物品减量期间玩家或库存变化，已停止适配器");
                }
                return {attribute ? RequestState::Indeterminate : RequestState::Failed, "扣减前玩家或库存变化，库存未继续扣减；不会重试"};
            }
            alignas(8) std::array<std::byte, 16> parameters{};
            std::memcpy(parameters.data(), &*amount, 4);
            std::memcpy(parameters.data() + 4, &item.id, 8);
            parameters[12] = std::byte{1};
            if (!InventoryRemoveLeaf(backpack, function, parameters.data())) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "物品减量原生调用异常，已停用；不会重试");
                return {RequestState::Indeterminate, "物品减量调用异常，库存或属性状态无法确认；不会重试"};
            }
            ++dispatchedCalls;
            if (!ItemBackpackMatches(context, backpack)) return {RequestState::Indeterminate, "减量后玩家会话变化，不自动重试"};
            const auto after = ItemOwned(backpack, item);
            const auto name = item.entry.name;
            bool attributesVerified = true;
            if (attribute) {
                std::vector<InventoryAttributeEffect> observed;
                attributesVerified = InventoryAttributePlanCurrent(plan) &&
                    InventoryReadAttributeEffects(backpack, plan.component.address, plan.definition, observed);
                std::int64_t total{};
                for (const auto& effect : observed) total += effect.level;
                attributesVerified &= total == target;
            }
            RefreshItems(context, true);
            if (after && *after == target && attributesVerified)
                return {RequestState::Verified, std::format("{}：库存 {} → {}{}", name, *before, target,
                    attribute ? "；永久属性效果级数已同步回读" : "；已发送游戏库存更新事件")};
            if (attribute) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "永久物品减量结果不一致，已停止适配器");
            }
            return {RequestState::Indeterminate, "减量已提交，库存或属性回读与目标不符；不会重试"};
        }
