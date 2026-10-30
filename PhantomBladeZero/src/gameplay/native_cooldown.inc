// Exact-build getter: RegisterSwitchWeaponCooldownReduction subtracts seconds
// from only the weapon timer. The getter also includes the generic timer, so
// override its temporary result without editing either timer or talent values.
        using SwitchCooldownQuery = float(__fastcall*)(Address, const NameValue*);
        constexpr Address SwitchCooldownQueryRva = 0x5e76860;
        constexpr std::array<unsigned char, 16> SwitchCooldownEntry{
            0x40,0x53,0x55,0x57,0x48,0x83,0xec,0x70,0x48,0x83,0xb9,0xe8,0,0,0,0};
        constexpr std::array<std::string_view, 4> SwitchCooldownSlotNames{
            "Weapon.Slot.Shachi", "Weapon.Slot.Main", "Weapon.Slot.Item2", "Weapon.Slot.Item1"};
        struct CooldownBinding {
            Address pawn{}, component{}, type{};
            std::uint32_t pawnIndex{}, componentIndex{};
            std::array<NameValue, 4> slots{};
            std::size_t slotCount{};
        } cooldownBinding;
        SwitchCooldownQuery originalSwitchCooldown{};
        std::atomic<bool> cooldownHookCreated{}, cooldownHookInstalled{};
        std::array<unsigned char, SwitchCooldownEntry.size()> ownedCooldownEntry{};
        bool cooldownPatchCaptured{};
        bool cooldownEnabled{};
        std::uint64_t cooldownBypassedQueries{};

        bool CooldownFeature(std::string_view id) { return id == "weapon_switch_cooldown"; }
        bool CooldownEntryMatches(const std::array<unsigned char, SwitchCooldownEntry.size()>& expected) noexcept {
            std::array<unsigned char, SwitchCooldownEntry.size()> current{};
            return ReadBytes(base + SwitchCooldownQueryRva, current.data(), current.size()) && current == expected;
        }
        bool CooldownHookOwnership() noexcept {
            return cooldownHookInstalled.load() && cooldownPatchCaptured && CooldownEntryMatches(ownedCooldownEntry);
        }
        bool CooldownFunctionContract(Address type) {
            return CombatSignature(FindFunction(type, "GetCurrentChangeWeaponCooldown"),
                "GetCurrentChangeWeaponCooldown", 0x5e0bb90, 0x54420401, 12, {
                    {"SlotTag","StructProperty",0,8,0x18001008000182ull,"GameplayTag"},
                    {"ReturnValue","FloatProperty",8,4,0x18001040000780ull,""}});
        }
        bool CooldownMapContract(Address type) {
            const auto property = FindProperty(type, "RuntimeWeaponData");
            if (!PropertyLayout(property, "MapProperty", 80, 0xf8)) return false;
            const auto key = Read<Address>(property + 0x70), value = Read<Address>(property + 0x78);
            const auto structure = Read<Address>(value + 0x70);
            return PropertyLayout(key, "StructProperty", 8, 0) &&
                ObjectName(Read<Address>(key + 0x70)) == "GameplayTag" &&
                PropertyLayout(value, "StructProperty", 32, 8) &&
                ObjectValid(structure) && ObjectName(structure) == "RuntimeWeaponSlotData" &&
                Read<std::int32_t>(structure + 0xa8) == 32 &&
                PropertyLayout(FindProperty(structure, "Weapons"), "ArrayProperty", 16, 0) &&
                PropertyLayout(FindProperty(type, "DecreaseSwitchWeaponCooldown"), "FloatProperty", 4, 0x390);
        }
        bool CaptureCooldownBinding(const Context& context, CooldownBinding& result) {
            if (!CombatBindingCurrent() || context.pawn != combatBinding.context.pawn) return false;
            const auto pawnType = Read<Address>(context.pawn + 0x10);
            const auto property = FindProperty(pawnType, "Character Weapons Component");
            const auto component = Read<Address>(context.pawn + 0xec0), type = Read<Address>(component + 0x10);
            if (!PropertyLayout(property, "ObjectProperty", 8, 0xec0) ||
                ObjectName(Read<Address>(property + 0x70)) != "CharacterWeaponsComponent" ||
                !ObjectValid(component) || ObjectName(type) != "CharacterWeaponsComponent" ||
                Read<Address>(component + 0x20) != context.pawn ||
                !CooldownFunctionContract(type) || !CooldownMapContract(type)) return false;
            result = {context.pawn, component, type, Read<std::uint32_t>(context.pawn + 0xc),
                Read<std::uint32_t>(component + 0xc)};
            ItemSparseMap map;
            const auto location = component + 0xf8;
            if (!ItemReadMap(location, map) || map.count > 16) return false;
            for (std::int32_t slot = 0; slot < map.count; ++slot) {
                bool allocated{};
                if (!ItemSlotAllocated(location, map, slot, allocated)) return false;
                if (!allocated) continue;
                NameValue tag{};
                if (!ReadBytes(map.data + static_cast<Address>(slot) * 48, &tag, sizeof(tag))) return false;
                const auto name = Name(tag.comparison);
                if (!name || tag.number || std::find(SwitchCooldownSlotNames.begin(), SwitchCooldownSlotNames.end(), *name) ==
                    SwitchCooldownSlotNames.end()) continue;
                if (result.slotCount == result.slots.size()) return false;
                for (std::size_t i = 0; i < result.slotCount; ++i)
                    if (ItemSameName(result.slots[i], tag)) return false;
                result.slots[result.slotCount++] = tag;
            }
            ItemSparseMap after;
            return result.slotCount && ItemReadMap(location, after) && ItemSameMap(map, after);
        }
        bool CooldownBindingCurrent(Address object) noexcept {
            // No reflection, name allocation or engine calls in the getter hook.
            return LocalGameThread() && CombatBindingCurrent() && object == cooldownBinding.component &&
                cooldownBinding.pawn == combatBinding.context.pawn &&
                cooldownBinding.pawnIndex == combatBinding.pawnIndex &&
                Read<Address>(cooldownBinding.pawn + 0xec0) == object &&
                Read<std::uint32_t>(object + 0xc) == cooldownBinding.componentIndex &&
                Read<Address>(object + 0x10) == cooldownBinding.type &&
                Read<Address>(object + 0x20) == cooldownBinding.pawn &&
                !(Read<std::uint32_t>(object + 8) & 0x40018000);
        }
        bool CooldownKnownSlot(Address component, const NameValue* parameter) noexcept {
            NameValue tag{};
            if (!ReadBytes(reinterpret_cast<Address>(parameter), &tag, sizeof(tag))) return false;
            bool known{};
            for (std::size_t i = 0; i < cooldownBinding.slotCount; ++i)
                known |= ItemSameName(cooldownBinding.slots[i], tag);
            if (!known) return false;
            ItemSparseMap map;
            const auto location = component + 0xf8;
            if (!ItemReadMap(location, map) || map.count > 16) return false;
            for (std::int32_t slot = 0; slot < map.count; ++slot) {
                bool allocated{};
                if (!ItemSlotAllocated(location, map, slot, allocated)) return false;
                if (!allocated) continue;
                NameValue current{};
                if (!ReadBytes(map.data + static_cast<Address>(slot) * 48, &current, sizeof(current))) return false;
                if (ItemSameName(current, tag)) return true;
            }
            return false;
        }
        float __fastcall HookSwitchCooldown(Address object, const NameValue* slot) {
            ++callbacksInFlight;
            float remaining{};
            __try {
                remaining = originalSwitchCooldown(object, slot);
                if (!stopping.load() && !faulted.load() && LocalGameThread() && cooldownEnabled &&
                    CooldownHookOwnership() && CooldownBindingCurrent(object)) {
                    const auto result = ApplySwitchCooldown(remaining, true, true, CooldownKnownSlot(object, slot));
                    remaining = result.remaining;
                    if (result.bypassed) ++cooldownBypassedQueries;
                }
            } __finally { --callbacksInFlight; }
            return remaining;
        }
        bool PrepareCooldownHook() {
            if (cooldownHookCreated.load()) return true;
            if (!modulePinned.load() || !Executable(base + SwitchCooldownQueryRva) ||
                !CooldownEntryMatches(SwitchCooldownEntry)) {
                gloss::Log("PBZ switch cooldown hook prepare: entry verification failed");
                return false;
            }
            auto* entry = reinterpret_cast<void*>(base + SwitchCooldownQueryRva);
            const auto created = MH_CreateHook(entry, reinterpret_cast<void*>(&HookSwitchCooldown),
                reinterpret_cast<void**>(&originalSwitchCooldown));
            gloss::Log(std::format("PBZ switch cooldown hook prepare: {}", MH_StatusToString(created)));
            if (created != MH_OK) return false;
            cooldownHookCreated.store(true);
            return true;
        }
        bool InstallCooldownHook() {
            if (cooldownHookInstalled.load()) return CooldownHookOwnership();
            if (!PrepareCooldownHook() || !Executable(base + SwitchCooldownQueryRva) ||
                !CooldownEntryMatches(SwitchCooldownEntry)) return false;
            auto* entry = reinterpret_cast<void*>(base + SwitchCooldownQueryRva);
            const auto enabled = MH_EnableHook(entry);
            gloss::Log(std::format("PBZ switch cooldown hook enable: {}", MH_StatusToString(enabled)));
            if (enabled != MH_OK) return false;
            std::array<unsigned char, SwitchCooldownEntry.size()> patch{};
            if (!ReadBytes(base + SwitchCooldownQueryRva, patch.data(), patch.size()) ||
                (cooldownPatchCaptured && patch != ownedCooldownEntry)) {
                // The enable succeeded, but ownership is unconfirmed. Leave the
                // entry untouched rather than overwrite a possible replacement.
                cooldownHookInstalled.store(true);
                return false;
            }
            // Immutable after publication: late callbacks can still inspect it.
            if (!cooldownPatchCaptured) { ownedCooldownEntry = patch; cooldownPatchCaptured = true; }
            cooldownHookInstalled.store(true);
            return CooldownHookOwnership();
        }
        bool RestoreCooldownHooks() {
            if (!cooldownHookInstalled.load()) return true;
            // Never disable another party's replacement at this entry.
            if (!CooldownHookOwnership()) return false;
            const auto status = MH_DisableHook(reinterpret_cast<void*>(base + SwitchCooldownQueryRva));
            if (status != MH_OK && status != MH_ERROR_DISABLED) return false;
            if (!CooldownEntryMatches(SwitchCooldownEntry)) return false;
            cooldownHookInstalled.store(false);
            // Preserve the trampoline for callbacks that entered before disabling.
            return true;
        }
        bool CooldownCapability(const Context& context, std::string_view id, std::string& reason) {
            reason.clear();
            if (!CooldownFeature(id)) return false;
            CooldownBinding candidate;
            if (stopping.load() || faulted.load() || !LocalGameThread() ||
                !CaptureCooldownBinding(context, candidate)) {
                reason = "本地武器组件、槽位或冷却查询契约未通过核对"; return false;
            }
            if (!Executable(base + SwitchCooldownQueryRva) ||
                !(cooldownHookInstalled.load() ? CooldownHookOwnership() : CooldownEntryMatches(SwitchCooldownEntry))) {
                reason = "武器冷却查询入口或钩子拥有者已变化"; return false;
            }
            return true;
        }
        ExecutionResult ExecuteCooldown(const Context& context, const Request& request) {
            if (request.arguments.size() != 2 || !CooldownFeature(request.arguments[0]) ||
                (request.arguments[1] != "on" && request.arguments[1] != "off"))
                return {RequestState::Failed, "武器冷却开关参数错误"};
            if (request.arguments[1] == "off") {
                cooldownEnabled = false;
                cooldownBinding = {};
                return {RequestState::Verified, "已停止武器冷却查询覆盖；游戏计时数据保持原值"};
            }
            std::string reason;
            CooldownBinding candidate;
            if (!CooldownCapability(context, request.arguments[0], reason)) return {RequestState::Failed, reason};
            if (!CaptureCooldownBinding(context, candidate) || !InstallCooldownHook())
                return {RequestState::Failed, "武器冷却查询钩子安装或对象核对失败"};
            cooldownBinding = candidate;
            cooldownEnabled = true;
            return {RequestState::Verified, "本地武器公共与槽位冷却查询已覆盖；实际切换仍需实战验证"};
        }
        std::vector<ToggleState> CooldownToggleStates(const Context& context) {
            std::string reason;
            const bool capable = CooldownCapability(context, "weapon_switch_cooldown", reason);
            const bool bound = !cooldownEnabled || (cooldownHookInstalled.load() &&
                CooldownBindingCurrent(cooldownBinding.component));
            if (capable && !bound) reason = "武器组件身份已变化，冷却查询覆盖已停止";
            const bool known = capable && bound;
            return {{"weapon_switch_cooldown", known, known && cooldownEnabled,
                known ? std::format("已覆盖 {} 次本地武器冷却查询；未改写计时或天赋", cooldownBypassedQueries) : reason}};
        }
        void CooldownSessionChanged() {
            cooldownEnabled = false;
            cooldownBinding = {};
        }
        bool CooldownCleanup() {
            if (!LocalGameThread()) return false;
            CooldownSessionChanged();
            return RestoreCooldownHooks();
        }
