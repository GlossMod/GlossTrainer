// Included after native_combat.inc and native_items.inc. Only the verified
// game thread edits temporary calculator output; no persistent attributes change.
        using DamageCalculation = void(__fastcall*)(Address, Address, Address, Address, Address, Address);
        constexpr Address DamageCalculationRva = 0x7d679d0;
        constexpr Address DamageCalculationSlot = 0x2b8;
        constexpr std::array<Address, 6> DamageVtables{
            0xc65f700, 0xc65f9c0, 0xc65fc80, 0xc660010, 0xc660938, 0xc65f440};
        struct DamageSwitch {
            std::string_view id;
            bool enabled{};
            float parameter{1};
        };
        std::array<DamageSwitch, 5> damageSwitches{{
            {"damage"}, {"defense"}, {"poise_damage"}, {"one_hit_kill"}, {"instant_break"}}};
        std::atomic<bool> damageHooksInstalled{};
        std::array<bool, DamageVtables.size()> ownedDamageSlots{};
        std::uint64_t damageAdjustedDeltas{};
        thread_local unsigned damageCalculationDepth{};

        bool DamageFeature(std::string_view id) {
            return std::any_of(damageSwitches.begin(), damageSwitches.end(),
                [&](const auto& feature) { return feature.id == id; });
        }
        DamageSwitch* DamageSwitchFor(std::string_view id) {
            for (auto& feature : damageSwitches) if (feature.id == id) return &feature;
            return nullptr;
        }
        bool DamageMapProperty(Address property, std::string_view name, std::int32_t offset,
            bool modification) {
            if (Name(Read<std::uint32_t>(property + 0x20)) != name ||
                !PropertyLayout(property, "MapProperty", 80, offset) ||
                Read<std::uint64_t>(property + 0x38) != (modification ? 0x10000000000180ull : 0x10000008000182ull)) return false;
            const auto key = Read<Address>(property + 0x70), value = Read<Address>(property + 0x78);
            return PropertyLayout(key, "StructProperty", modification ? 56 : 8, 0) &&
                ObjectName(Read<Address>(key + 0x70)) == (modification ? "GameplayAttribute" : "GameplayTag") &&
                Read<std::uint64_t>(key + 0x38) == (modification ? 0x8000000000000ull : 0x8001000000000ull) &&
                PropertyLayout(value, "FloatProperty", 4, modification ? 56 : 8) &&
                Read<std::uint64_t>(value + 0x38) == 0x8001040000200ull;
        }
        bool DamageFunctionContract(Address owner) {
            const auto function = FindFunction(owner, "CalculateAttributeModification");
            if (!ObjectValid(function) || Read<Address>(function + 0x128) != base + 0x5da64e0 ||
                Read<std::uint32_t>(function + 0x100) != 0x54420400 ||
                Read<std::uint8_t>(function + 0x104) != 5 || Read<std::uint16_t>(function + 0x106) != 256) return false;
            auto field = Read<Address>(function + 0x50);
            for (const auto& item : {std::pair{"AttackerAbilitySystem", 0}, std::pair{"TargetAbilitySystem", 8}}) {
                if (Name(Read<std::uint32_t>(field + 0x20)) != item.first ||
                    !PropertyLayout(field, "ObjectProperty", 8, item.second) ||
                    ObjectName(Read<Address>(field + 0x70)) != "AbilitySystemComponent" ||
                    Read<std::uint64_t>(field + 0x38) != 0x18001000080282ull) return false;
                field = Read<Address>(field + 0x10);
            }
            if (!DamageMapProperty(field, "DamageData", 0x10, false)) return false;
            field = Read<Address>(field + 0x10);
            if (!DamageMapProperty(field, "AttackerModification", 0x60, true)) return false;
            field = Read<Address>(field + 0x10);
            return DamageMapProperty(field, "TargetModification", 0xb0, true) && !Read<Address>(field + 0x10);
        }
        bool DamageClassContract() {
            const auto owner = Read<Address>(base + 0xe053aa0);
            return ObjectValid(owner) && ObjectName(owner) == "DamageCalculator_PBZBase" &&
                DamageFunctionContract(owner);
        }
        struct DamageActor {
            Address component{}, pawn{}, attributes{}, healthProperty{}, shachiProperty{};
            std::uint32_t componentIndex{}, pawnIndex{}, attributeIndex{};
        };
        bool DamageActorFor(Address component, DamageActor& result) {
            if (!ObjectValid(component) || !ClassNamed(Read<Address>(component + 0x10), "PBZAbilitySystemComponent")) return false;
            const auto type = Read<Address>(component + 0x10);
            if (!PropertyLayout(FindProperty(type, "OwnerActor"), "ObjectProperty", 8, 0x3f8) ||
                !PropertyLayout(FindProperty(type, "AvatarActor"), "ObjectProperty", 8, 0x400)) return false;
            const auto pawn = Read<Address>(component + 0x400);
            if (!ObjectValid(pawn) || Read<Address>(component + 0x3f8) != pawn || Read<Address>(component + 0x20) != pawn) return false;
            const auto pawnType = Read<Address>(pawn + 0x10);
            if (!PropertyLayout(FindProperty(pawnType, "AbilitySystemComponent"), "ObjectProperty", 8, 0x750) ||
                !PropertyLayout(FindProperty(pawnType, "AttributeSetBase"), "ObjectProperty", 8, 0x758) ||
                Read<Address>(pawn + 0x750) != component) return false;
            const auto attributes = Read<Address>(pawn + 0x758), attributeType = Read<Address>(attributes + 0x10);
            if (!ObjectValid(attributes) || ObjectName(attributeType) != "BasicAttributeSet" || Read<Address>(attributes + 0x20) != pawn) return false;
            const auto health = FindProperty(attributeType, "Health"), shachi = FindProperty(attributeType, "Shachi");
            if (!PropertyLayout(health, "StructProperty", 16, 0x58) ||
                ObjectName(Read<Address>(health + 0x70)) != "GameplayAttributeData" ||
                !PropertyLayout(shachi, "StructProperty", 16, 0x638) ||
                Read<Address>(shachi + 0x70) != Read<Address>(health + 0x70)) return false;
            result = {component, pawn, attributes, health, shachi, Read<std::uint32_t>(component + 0xc),
                Read<std::uint32_t>(pawn + 0xc), Read<std::uint32_t>(attributes + 0xc)};
            return true;
        }
        bool DamageActorCurrent(const DamageActor& actor) {
            return ObjectValid(actor.component) && ObjectValid(actor.pawn) && ObjectValid(actor.attributes) &&
                Read<std::uint32_t>(actor.component + 0xc) == actor.componentIndex &&
                Read<std::uint32_t>(actor.pawn + 0xc) == actor.pawnIndex &&
                Read<std::uint32_t>(actor.attributes + 0xc) == actor.attributeIndex &&
                Read<Address>(actor.component + 0x400) == actor.pawn && Read<Address>(actor.component + 0x3f8) == actor.pawn &&
                Read<Address>(actor.pawn + 0x750) == actor.component && Read<Address>(actor.pawn + 0x758) == actor.attributes;
        }
        bool WriteDamageDelta(Address address, float before, float after) noexcept {
            __try {
                auto* output = reinterpret_cast<float*>(address);
                if (*output != before) return false;
                *output = after;
                return *output == after;
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool DamageHookOwnership();
        void AdjustDamageOutput(Address calculator, Address attacker, Address target, Address output) {
            if (stopping.load() || faulted.load() || !damageHooksInstalled.load() || !LocalGameThread() ||
                !CombatBindingCurrent() || !DamageHookOwnership() ||
                !std::any_of(damageSwitches.begin(), damageSwitches.end(), [](const auto& feature) { return feature.enabled; })) return;
            const auto local = Read<Address>(combatBinding.context.pawn + 0x750);
            const auto direction = PlayerDamageDirection(attacker == local, target == local);
            if (direction == DamageDirection::Unrelated || !ObjectValid(calculator) ||
                !ClassNamed(Read<Address>(calculator + 0x10), "DamageCalculator_PBZBase") ||
                !DamageFunctionContract(Read<Address>(calculator + 0x10))) return;
            DamageActor source, victim;
            if (!DamageActorFor(attacker, source) || !DamageActorFor(target, victim)) return;
            const bool outgoing = direction == DamageDirection::Outgoing;
            const auto& localActor = outgoing ? source : victim;
            if (localActor.pawn != combatBinding.context.pawn || localActor.attributes != combatBinding.attributes ||
                localActor.pawnIndex != combatBinding.pawnIndex || localActor.attributeIndex != combatBinding.attributeIndex) return;
            ItemSparseMap map;
            if (!ItemReadMap(output, map) || map.count > 128) return;
            struct Change { Address entry{}, property{}; float before{}, after{}; std::int32_t slot{}; };
            std::array<Change, 128> changes{};
            std::size_t count{};
            const auto& damage = damageSwitches[0];
            const auto& defense = damageSwitches[1];
            const auto& posture = damageSwitches[2];
            for (std::int32_t slot = 0; slot < map.count; ++slot) {
                bool allocated{};
                if (!ItemSlotAllocated(output, map, slot, allocated)) return;
                if (!allocated) continue;
                const auto entry = map.data + static_cast<Address>(slot) * 72;
                Address property{};
                float delta{};
                if (!ReadBytes(entry + 0x10, &property, sizeof(property)) || !ReadBytes(entry + 0x38, &delta, sizeof(delta))) return;
                const bool health = property == victim.healthProperty;
                if ((!health && property != victim.shachiProperty) || !std::isfinite(delta) || delta >= 0) continue;
                if (!health && !outgoing) continue;
                const bool lethal = outgoing && (health ? damageSwitches[3].enabled : damageSwitches[4].enabled);
                const float outgoingMultiplier = health ? (damage.enabled ? damage.parameter : 1.f) : (posture.enabled ? posture.parameter : 1.f);
                const float incomingMultiplier = health && defense.enabled ? defense.parameter : 1.f;
                const auto current = Read<float>(victim.attributes + (health ? 0x64 : 0x644));
                const auto replacement = ExtendedDamageDelta(delta, outgoing, !outgoing,
                    outgoingMultiplier, incomingMultiplier, lethal, current);
                if (replacement && *replacement != delta) changes[count++] = {entry, property, delta, *replacement, slot};
            }
            ItemSparseMap after;
            if (!ItemReadMap(output, after) || !ItemSameMap(map, after) || !CombatBindingCurrent() ||
                !DamageActorCurrent(source) || !DamageActorCurrent(victim)) return;
            for (std::size_t index = 0; index < count; ++index) {
                const auto& change = changes[index];
                bool allocated{};
                if (!ItemSlotAllocated(output, map, change.slot, allocated) || !allocated ||
                    Read<Address>(change.entry + 0x10) != change.property) return;
                if (WriteDamageDelta(change.entry + 0x38, change.before, change.after)) ++damageAdjustedDeltas;
            }
        }
        void TryAdjustDamageOutput(Address calculator, Address attacker, Address target, Address output) noexcept {
            // Reflection helpers may allocate. A trainer-side exception must not
            // escape through the game's native calculation frame.
            try { AdjustDamageOutput(calculator, attacker, target, output); }
            catch (...) { /* Fail open; never retry this temporary calculation. */ }
        }
        void __fastcall HookDamageCalculation(Address calculator, Address attacker, Address target,
            Address data, Address attackerOutput, Address targetOutput) {
            ++callbacksInFlight;
            ++damageCalculationDepth;
            __try {
                reinterpret_cast<DamageCalculation>(base + DamageCalculationRva)(calculator, attacker, target, data, attackerOutput, targetOutput);
                // A nested calculator may feed the same output to its caller.
                // Apply exactly once at the outermost calculation boundary.
                if (damageCalculationDepth == 1) TryAdjustDamageOutput(calculator, attacker, target, targetOutput);
            } __finally { --damageCalculationDepth; --callbacksInFlight; }
        }
        bool DamageHookOwnership() {
            return std::all_of(DamageVtables.begin(), DamageVtables.end(), [&](Address table) {
                return Read<Address>(base + table + DamageCalculationSlot) == reinterpret_cast<Address>(&HookDamageCalculation);
            });
        }
        bool DamageSlotsReady() {
            return Executable(base + DamageCalculationRva) &&
                std::all_of(DamageVtables.begin(), DamageVtables.end(), [&](Address table) {
                    const auto current = Read<Address>(base + table + DamageCalculationSlot);
                    return current == base + DamageCalculationRva || current == reinterpret_cast<Address>(&HookDamageCalculation);
                });
        }
        bool RestoreDamageHooks() {
            bool restored = true;
            for (std::size_t index = 0; index < DamageVtables.size(); ++index) {
                if (!ownedDamageSlots[index]) continue;
                const auto slot = base + DamageVtables[index] + DamageCalculationSlot;
                const auto current = Read<Address>(slot);
                if (current == reinterpret_cast<Address>(&HookDamageCalculation)) {
                    if (ReplaceSlot(slot, current, base + DamageCalculationRva)) ownedDamageSlots[index] = false;
                    else restored = false;
                } else if (current == base + DamageCalculationRva) ownedDamageSlots[index] = false;
                else restored = false;
            }
            if (restored) damageHooksInstalled.store(false);
            return restored;
        }
        bool InstallDamageHooks() {
            if (damageHooksInstalled.load()) return DamageHookOwnership();
            if (!modulePinned.load() || !DamageSlotsReady()) return false;
            for (std::size_t index = 0; index < DamageVtables.size(); ++index) {
                const auto slot = base + DamageVtables[index] + DamageCalculationSlot;
                if (Read<Address>(slot) == reinterpret_cast<Address>(&HookDamageCalculation)) { ownedDamageSlots[index] = true; continue; }
                if (!ReplaceSlot(slot, base + DamageCalculationRva, reinterpret_cast<Address>(&HookDamageCalculation))) {
                    RestoreDamageHooks(); return false;
                }
                ownedDamageSlots[index] = true;
            }
            damageHooksInstalled.store(true);
            return DamageHookOwnership();
        }
        bool DamageCapability(const Context& context, std::string_view id, std::string& reason) {
            reason.clear();
            if (!DamageFeature(id)) return false;
            if (!LocalGameThread() || stopping.load() || faulted.load() || !CombatBindingCurrent() ||
                context.pawn != combatBinding.context.pawn) { reason = "等待本地玩家战斗属性核对"; return false; }
            DamageActor player;
            if (!DamageClassContract() || !DamageActorFor(Read<Address>(context.pawn + 0x750), player) || !DamageSlotsReady()) {
                reason = "伤害结算虚表或玩家能力系统归属契约未通过"; return false;
            }
            return true;
        }
        ExecutionResult ExecuteDamage(const Context& context, const Request& request) {
            if (request.arguments.size() < 2) return {RequestState::Failed, "战斗结算开关参数错误"};
            auto* feature = DamageSwitchFor(request.arguments[0]);
            const bool enabled = request.arguments[1] == "on";
            const bool numeric = feature && (feature->id == "damage" || feature->id == "defense" || feature->id == "poise_damage");
            if (!feature || (request.arguments[1] != "on" && request.arguments[1] != "off") ||
                request.arguments.size() != (enabled && numeric ? 3u : 2u)) return {RequestState::Failed, "战斗结算开关参数错误"};
            if (!enabled) {
                feature->enabled = false;
                return {RequestState::Verified, "已停止此战斗结算修改；未改写持久属性"};
            }
            float parameter = 1;
            if (numeric) {
                const auto& text = request.arguments[2];
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), parameter);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !ValidDamageMultiplier(parameter))
                    return {RequestState::Failed, "战斗倍率必须为0.1至100"};
            }
            std::string reason;
            if (!DamageCapability(context, feature->id, reason)) return {RequestState::Failed, reason};
            if (!InstallDamageHooks()) return {RequestState::Failed, "伤害结算入口安装或拥有者核对失败"};
            feature->parameter = parameter;
            feature->enabled = true;
            return {RequestState::Verified, "战斗结算入口和参数已核对；实际攻击效果仍需实战验证"};
        }
        std::vector<ToggleState> DamageToggleStates(const Context& context) {
            std::vector<ToggleState> result;
            for (const auto& feature : damageSwitches) {
                std::string reason;
                const bool capable = DamageCapability(context, feature.id, reason);
                const bool owned = !damageHooksInstalled.load() || DamageHookOwnership();
                const bool known = capable && owned;
                if (!owned) reason = "伤害结算虚表拥有者已变化，停止修改";
                const bool numeric = feature.id == "damage" || feature.id == "defense" || feature.id == "poise_damage";
                result.push_back({std::string(feature.id), known, known && feature.enabled,
                    known ? std::format("本地玩家战斗结算修改；已核对 {} 次临时数值写入", damageAdjustedDeltas) : reason,
                    numeric ? std::vector<std::string>{std::format("{:.9g}", feature.parameter)} : std::vector<std::string>{}});
            }
            return result;
        }
        void DamageSessionChanged() {
            for (auto& feature : damageSwitches) feature.enabled = false;
        }
        bool DamageCleanup() {
            if (!LocalGameThread()) return false;
            DamageSessionChanged();
            return RestoreDamageHooks();
        }
