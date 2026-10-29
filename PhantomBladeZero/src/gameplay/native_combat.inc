// Exact-build combat hooks. Included inside native_adapter's anonymous namespace.
        using AttributePreChange = void(__fastcall*)(void*, const void*, float*);
        constexpr Address BasicAttributeVtableRva = 0xbc80180;
        constexpr Address ShachiPreCurrentRva = 0x5dd5470;
        constexpr Address ShachiPreBaseRva = 0x5dd5360;
        struct CombatBinding {
            Context context;
            Address attributes{}, property{}, darkSourceProperty{};
            std::uint32_t attributeIndex{}, pawnIndex{};
        };
        CombatBinding combatBinding; // Written/read only on the verified game thread.
        bool shachiEnabled{};
        bool darkSourceEnabled{};
        std::atomic<bool> shachiHooksInstalled{};
        std::atomic<std::uint64_t> shachiPreventedWrites{};
        std::atomic<std::uint64_t> darkSourcePreventedWrites{};

        bool CombatBindingCurrent() {
            if (stopping.load() || faulted.load() || !LocalGameThread() || !combatBinding.attributes) return false;
            const auto& bound = combatBinding.context;
            const auto engine = Read<Address>(base + 0xdf741c8);
            const auto viewport = Read<Address>(engine + 0xc88);
            const auto players = Read<Address>(bound.instance + 0x38);
            return Read<Address>(viewport + 0x78) == bound.world &&
                Read<Address>(viewport + 0x80) == bound.instance &&
                Read<Address>(players) == bound.player && Read<Address>(bound.player + 0x30) == bound.controller &&
                Read<Address>(bound.controller + 0x358) == bound.pawn &&
                Read<std::uint32_t>(bound.pawn + 0xc) == combatBinding.pawnIndex &&
                Read<Address>(bound.pawn + 0x758) == combatBinding.attributes &&
                Read<std::uint32_t>(combatBinding.attributes + 0xc) == combatBinding.attributeIndex &&
                Read<Address>(combatBinding.attributes + 0x20) == bound.pawn &&
                !(Read<std::uint32_t>(bound.pawn + 8) & 0x40018000) &&
                !(Read<std::uint32_t>(combatBinding.attributes + 8) & 0x40018000);
        }
        bool BindCombat(const Context& context) {
            combatBinding = {};
            const auto pawnType = Read<Address>(context.pawn + 0x10);
            if (!PropertyLayout(FindProperty(pawnType, "AttributeSetBase"), "ObjectProperty", 8, 0x758)) return false;
            const auto attributes = Read<Address>(context.pawn + 0x758);
            const auto type = Read<Address>(attributes + 0x10);
            if (!ObjectValid(attributes) || ObjectName(type) != "BasicAttributeSet" ||
                Read<Address>(attributes) != base + BasicAttributeVtableRva ||
                Read<Address>(attributes + 0x20) != context.pawn) return false;
            const auto property = FindProperty(type, "Shachi");
            const auto maximum = FindProperty(type, "Shachi_Max");
            if (!PropertyLayout(property, "StructProperty", 16, 0x638) ||
                !PropertyLayout(maximum, "StructProperty", 16, 0x658) ||
                ObjectName(Read<Address>(property + 0x70)) != "GameplayAttributeData" ||
                Read<Address>(maximum + 0x70) != Read<Address>(property + 0x70)) return false;
            const auto darkSource = FindProperty(type, "SpiritShadow");
            const auto darkMaximum = FindProperty(type, "SpiritShadow_Max");
            const bool darkReady = PropertyLayout(darkSource, "StructProperty", 16, 0x8f8) &&
                PropertyLayout(darkMaximum, "StructProperty", 16, 0x908) &&
                Read<Address>(darkSource + 0x70) == Read<Address>(property + 0x70) &&
                Read<Address>(darkMaximum + 0x70) == Read<Address>(property + 0x70);
            combatBinding = {context, attributes, property, darkReady ? darkSource : 0, Read<std::uint32_t>(attributes + 0xc),
                Read<std::uint32_t>(context.pawn + 0xc)};
            return CombatBindingCurrent();
        }
        void ProtectShachi(void* object, const void* attribute, float* proposed, bool baseValue) noexcept {
            // No UObject calls, allocation or exceptions in this leaf. The engine already
            // owns the parameter; never patch permanent attribute data or its maximum.
            __try {
                if (stopping.load() || faulted.load() || !shachiHooksInstalled.load() ||
                    GetCurrentThreadId() != expectedThread.load()) return;
                if (reinterpret_cast<Address>(object) != combatBinding.attributes ||
                    !attribute || !proposed || !CombatBindingCurrent()) return;
                const auto property = Read<Address>(reinterpret_cast<Address>(attribute) + 0x10);
                const bool dark = darkSourceEnabled && combatBinding.darkSourceProperty && property == combatBinding.darkSourceProperty;
                if (!dark && !(shachiEnabled && property == combatBinding.property)) return;
                const auto currentOffset = dark ? (baseValue ? 0x900 : 0x904) : (baseValue ? 0x640 : 0x644);
                const auto current = Read<float>(combatBinding.attributes + currentOffset);
                const auto maximum = Read<float>(combatBinding.attributes + (dark ? 0x914 : 0x664));
                const auto value = ProtectedShachi(current, maximum, *proposed);
                if (*proposed < value) {
                    *proposed = value;
                    if (dark) ++darkSourcePreventedWrites; else ++shachiPreventedWrites;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { /* Fail open for an invalidated binding. */ }
        }
        void __fastcall HookShachiCurrent(void* object, const void* attribute, float* proposed) {
            ++callbacksInFlight;
            __try {
                reinterpret_cast<AttributePreChange>(base + ShachiPreCurrentRva)(object, attribute, proposed);
                ProtectShachi(object, attribute, proposed, false);
            } __finally { --callbacksInFlight; }
        }
        void __fastcall HookShachiBase(void* object, const void* attribute, float* proposed) {
            ++callbacksInFlight;
            __try {
                reinterpret_cast<AttributePreChange>(base + ShachiPreBaseRva)(object, attribute, proposed);
                ProtectShachi(object, attribute, proposed, true);
            } __finally { --callbacksInFlight; }
        }
        bool ShachiHookOwnership() {
            const auto vtable = base + BasicAttributeVtableRva;
            return Read<Address>(vtable + 0x2c8) == reinterpret_cast<Address>(&HookShachiCurrent) &&
                Read<Address>(vtable + 0x2d8) == reinterpret_cast<Address>(&HookShachiBase);
        }
        bool InstallShachiHooks() {
            if (shachiHooksInstalled.load()) return ShachiHookOwnership();
            if (!modulePinned.load() || !CombatBindingCurrent()) return false;
            const auto currentSlot = base + BasicAttributeVtableRva + 0x2c8;
            const auto baseSlot = base + BasicAttributeVtableRva + 0x2d8;
            if (Read<Address>(currentSlot) != base + ShachiPreCurrentRva ||
                Read<Address>(baseSlot) != base + ShachiPreBaseRva) return false;
            if (!ReplaceSlot(currentSlot, base + ShachiPreCurrentRva, reinterpret_cast<Address>(&HookShachiCurrent))) return false;
            if (!ReplaceSlot(baseSlot, base + ShachiPreBaseRva, reinterpret_cast<Address>(&HookShachiBase))) {
                ReplaceSlot(currentSlot, reinterpret_cast<Address>(&HookShachiCurrent), base + ShachiPreCurrentRva);
                return false;
            }
            shachiHooksInstalled.store(true);
            return ShachiHookOwnership();
        }
        bool RestoreShachiHooks() {
            if (!shachiHooksInstalled.load()) return true;
            const auto vtable = base + BasicAttributeVtableRva;
            bool restored = true;
            for (const auto pair : {std::pair{Address{0x2c8}, std::pair{reinterpret_cast<Address>(&HookShachiCurrent), ShachiPreCurrentRva}},
                std::pair{Address{0x2d8}, std::pair{reinterpret_cast<Address>(&HookShachiBase), ShachiPreBaseRva}}}) {
                const auto slot = vtable + pair.first;
                const auto current = Read<Address>(slot);
                if (current == pair.second.first) restored &= ReplaceSlot(slot, current, base + pair.second.second);
                else if (current != base + pair.second.second) restored = false;
            }
            if (restored) shachiHooksInstalled.store(false);
            return restored;
        }
        ToggleState ShachiToggleState() {
            const bool known = CombatBindingCurrent() && (!shachiHooksInstalled.load() || ShachiHookOwnership());
            return {"infinite_shachi", known, known && shachiEnabled && shachiHooksInstalled.load(),
                known ? "仅阻止本地玩家杀气降低；允许恢复，不改变上限" : "等待玩家属性与扣减入口核对"};
        }
        ExecutionResult ExecuteShachi(const Request& request) {
            if (request.arguments.size() != 1 || (request.arguments[0] != "on" && request.arguments[0] != "off"))
                return {RequestState::Failed, "杀气开关参数无效"};
            const bool enabled = request.arguments[0] == "on";
            if (enabled && (!CombatBindingCurrent() || !InstallShachiHooks()))
                return {RequestState::Failed, "本地玩家属性或扣减入口已变化，未启用"};
            shachiEnabled = enabled;
            return {RequestState::Verified, enabled ? "已启用本地玩家杀气扣减保护；恢复和上限保持游戏规则" : "已停止杀气扣减保护"};
        }

        bool DarkSourceReady() {
            return CombatBindingCurrent() && combatBinding.darkSourceProperty &&
                PlayerAttributesReady(combatBinding.context);
        }
        bool FillDarkSource() {
            if (!DarkSourceReady()) return false;
            const auto maximum = Read<float>(combatBinding.attributes + 0x914);
            if (!std::isfinite(maximum) || maximum <= 0 || maximum > 100000000) return false;
            const auto component = Read<Address>(combatBinding.context.pawn + 0x750);
            const auto identity = CaptureAttributeIdentity(combatBinding.context);
            if (!SetEngineAttribute(component, combatBinding.darkSourceProperty, maximum)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "暗源属性调用异常，适配器停止");
                return false;
            }
            ++dispatchedCalls;
            if (!AttributeIdentityCurrent(combatBinding.context, identity) || !CombatBindingCurrent()) return false;
            const auto value = Read<float>(combatBinding.attributes + 0x904);
            return std::isfinite(value) && std::abs(value - maximum) < 0.01f;
        }
        ToggleState DarkSourceToggleState() {
            const bool known = DarkSourceReady() && (!shachiHooksInstalled.load() || ShachiHookOwnership());
            return {"infinite_dark_source", known, known && darkSourceEnabled && shachiHooksInstalled.load(),
                known ? std::format("暗源 {:.0f} / {:.0f}；关闭后恢复正常扣减", Read<float>(combatBinding.attributes + 0x904),
                    Read<float>(combatBinding.attributes + 0x914)) : "等待玩家暗源属性与扣减入口核对"};
        }
        ExecutionResult ExecuteDarkSource(const Request& request) {
            if (request.arguments.size() != 1 || (request.arguments[0] != "on" && request.arguments[0] != "off"))
                return {RequestState::Failed, "暗源开关参数无效"};
            if (request.arguments[0] == "off") {
                darkSourceEnabled = false;
                return {RequestState::Verified, "已停止暗源扣减保护"};
            }
            if (!DarkSourceReady() || !InstallShachiHooks()) return {RequestState::Failed, "暗源属性或扣减入口未通过核对"};
            if (!FillDarkSource()) { darkSourceEnabled = false; return {RequestState::Indeterminate, "暗源补满未通过回读，未启用扣减保护"}; }
            darkSourceEnabled = true;
            return {RequestState::Verified, "暗源已补满并启用扣减保护；未改变暗源上限"};
        }

        struct CombatParameter {
            std::string_view name, type;
            std::int32_t offset, size;
            std::uint64_t flags;
            std::string_view subtype;
        };
        bool CombatSignature(Address function, std::string_view name, Address rva, std::uint32_t flags, int size,
            std::initializer_list<CombatParameter> expected) {
            if (!ObjectValid(function) || ObjectName(function) != name || Read<Address>(function + 0x128) != base + rva ||
                Read<std::uint32_t>(function + 0x100) != flags || Read<std::uint16_t>(function + 0x106) != size ||
                Read<std::uint8_t>(function + 0x104) != expected.size()) return false;
            auto field = Read<Address>(function + 0x50);
            for (const auto& parameter : expected) {
                if (Name(Read<std::uint32_t>(field + 0x20)) != parameter.name ||
                    !PropertyLayout(field, parameter.type, parameter.size, parameter.offset) ||
                    Read<std::uint64_t>(field + 0x38) != parameter.flags ||
                    (!parameter.subtype.empty() && ObjectName(Read<Address>(field + 0x70)) != parameter.subtype) ||
                    (parameter.type == "BoolProperty" && Read<std::uint32_t>(field + 0x70) != 0xff010001)) return false;
                field = Read<Address>(field + 0x10);
            }
            return !field;
        }
        constexpr std::string_view PerfectDeflectTag = "CharacterState.EditorDebug.Deflect.AlwaysPerfect";
        bool perfectDeflectEnabled{}, perfectDodgeEnabled{};
        struct DeflectContribution {
            Context context;
            std::uint32_t pawnIndex{};
            std::int32_t previousCount{};
            bool owned{};
        } deflectContribution;
        std::string combatError;
        std::int32_t lastDodgeHandle{-1};
        double lastDodgeHitTime{};
        std::uint64_t dodgeActivations{};
        std::atomic<bool> combatCleanupRequested{}, combatCleanupComplete{};

        Address PerfectTagFunction(bool add) {
            const auto type = Read<Address>(base + 0xe019ac8);
            if (!ObjectValid(type) || ObjectName(type) != "PBZAbilityBlueprintLibrary") return 0;
            const auto name = add ? "AddLooseGameplayTag" : "RemoveLooseGameplayTag";
            const auto function = FindFunction(type, name);
            return CombatSignature(function, name, add ? 0x65c0740 : 0x65c2ed0, 0x04422401, 18, {
                {"Actor", "ObjectProperty", 0, 8, 0x18001000000280ull, "Actor"},
                {"GameplayTag", "StructProperty", 8, 8, 0x18001008000182ull, "GameplayTag"},
                {"bShouldReplicate", "BoolProperty", 16, 1, 0x18001040000280ull, ""},
                {"ReturnValue", "BoolProperty", 17, 1, 0x18001040000780ull, ""}}) ? function : 0;
        }
        bool PerfectTagCall(const Context& context, bool add) {
            const auto function = PerfectTagFunction(add);
            const auto tag = FindExistingName(PerfectDeflectTag);
            const auto type = Read<Address>(function + 0x20);
            const auto cdo = Read<Address>(type + 0x160);
            if (!function || !tag || !ObjectValid(cdo) || !LocalGameThread() || !ObjectValid(context.pawn)) return false;
            struct Parameters { Address actor; NameValue tag; std::uint8_t replicate{}, result{}; } parameters{context.pawn, *tag};
            if (!CallProcessEvent(cdo, function, &parameters)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "格挡状态调用异常，停止自动操作");
                return false;
            }
            return parameters.result != 0;
        }
        bool RemoveDeflectContribution() {
            if (!deflectContribution.owned) return true;
            const auto record = deflectContribution;
            if (!ObjectValid(record.context.pawn) || Read<std::uint32_t>(record.context.pawn + 0xc) != record.pawnIndex) {
                deflectContribution = {}; return true;
            }
            const auto before = ReadGameplayTagCount(record.context, PerfectDeflectTag);
            if (!before) return false;
            // If the game already removed our contribution, never remove another source.
            if (*before <= record.previousCount) { deflectContribution = {}; return true; }
            if (!PerfectTagCall(record.context, false)) return false;
            const auto after = ReadGameplayTagCount(record.context, PerfectDeflectTag);
            if (!after || *after != *before - 1) return false;
            deflectContribution = {};
            return true;
        }
        bool EnsurePerfectDeflect(const Context& context) {
            if (!perfectDeflectEnabled) return true;
            if (deflectContribution.owned) return true;
            const auto before = ReadGameplayTagCount(context, PerfectDeflectTag);
            if (!before || *before > 100000) return false;
            if (!PerfectTagCall(context, true)) return false;
            // Record ownership even if readback fails; an indeterminate write must
            // not be blindly retried or lose its eventual cleanup record.
            deflectContribution = {context, Read<std::uint32_t>(context.pawn + 0xc), *before, true};
            const auto after = ReadGameplayTagCount(context, PerfectDeflectTag);
            return after && *after == *before + 1;
        }
        bool DodgeContract(const Context& context) {
            const auto pawnType = Read<Address>(context.pawn + 0x10);
            if (!PropertyLayout(FindProperty(pawnType, "Deflect Component"), "ObjectProperty", 8, 0xed0) ||
                !PropertyLayout(FindProperty(pawnType, "Ability Input Component"), "ObjectProperty", 8, 0xeb8)) return false;
            const auto deflect = Read<Address>(context.pawn + 0xed0), input = Read<Address>(context.pawn + 0xeb8);
            if (!ObjectValid(deflect) || !ObjectValid(input) || Read<Address>(deflect + 0x20) != context.pawn ||
                Read<Address>(input + 0x20) != context.pawn) return false;
            const auto query = FindFunction(Read<Address>(deflect + 0x10), "GetNearestValidReceivedFrame");
            const auto activate = FindFunction(Read<Address>(input + 0x10), "TryActivateExternalActionAbilityInterruptLowerByTag");
            const auto system = Read<Address>(Read<Address>(base + 0xdf4d1c0) + 0x20);
            const auto time = FindFunction(system, "GetGameTimeInSeconds");
            return CombatSignature(query, "GetNearestValidReceivedFrame", 0x5fd7c90, 0x04420401, 100, {
                {"DeflectAngle", "FloatProperty", 0, 4, 0x18001040000280ull, ""},
                {"MaxPreimpactTime", "FloatProperty", 4, 4, 0x18001040000280ull, ""},
                {"IsDodge", "BoolProperty", 8, 1, 0x18001040000280ull, ""},
                {"CurrentHandle", "IntProperty", 12, 4, 0x18001040000280ull, ""},
                {"OutFrame", "StructProperty", 16, 80, 0x10001000000180ull, "DeflectReceiveFrame"},
                {"ReturnValue", "IntProperty", 96, 4, 0x18001040000780ull, ""}}) &&
                CombatSignature(activate, "TryActivateExternalActionAbilityInterruptLowerByTag", 0x5de66e0, 0x04420401, 21, {
                {"AbilityTag", "StructProperty", 0, 8, 0x18001008000182ull, "GameplayTag"},
                {"InputAction", "ObjectProperty", 8, 8, 0x18001000000282ull, "InputAction"},
                {"ActionPriority", "IntProperty", 16, 4, 0x18001040000280ull, ""},
                {"ReturnValue", "BoolProperty", 20, 1, 0x18001040000780ull, ""}}) &&
                CombatSignature(time, "GetGameTimeInSeconds", 0x3f9a910, 0x14022403, 16, {
                {"WorldContextObject", "ObjectProperty", 0, 8, 0x18001000000282ull, "Object"},
                {"ReturnValue", "DoubleProperty", 8, 8, 0x18001040000780ull, ""}});
        }
        Address DodgeInputAction(const Context& context) {
            const auto playerInput = Read<Address>(context.controller + 0x428);
            if (!ObjectValid(playerInput) || !PropertyLayout(FindProperty(Read<Address>(playerInput + 0x10), "EnhancedActionMappings"),
                "ArrayProperty", 16, 0x628)) return 0;
            const auto data = Read<Address>(playerInput + 0x628);
            const auto count = Read<std::int32_t>(playerInput + 0x630);
            if (!data || count < 1 || count > 2048) return 0;
            for (int i = 0; i < count; ++i) {
                const auto action = Read<Address>(data + i * 80 + 0x20);
                if (ObjectName(action) == "IA_Dodge" && ObjectValid(action) &&
                    ObjectName(Read<Address>(action + 0x20)) == "/PBZDeflectSystem/Input/IA_Dodge") return action;
            }
            return 0;
        }
        void CombatSessionChanged(const Context& context) {
            if (deflectContribution.owned && (deflectContribution.context.Key() != context.Key() ||
                deflectContribution.pawnIndex != Read<std::uint32_t>(context.pawn + 0xc))) {
                if (!RemoveDeflectContribution()) { perfectDeflectEnabled = false; combatError = "旧玩家格挡状态未能确认恢复"; }
            }
            lastDodgeHandle = -1; lastDodgeHitTime = 0;
        }
        bool CombatCleanup() {
            if (!LocalGameThread()) return false;
            if (combatCleanupRequested.load()) {
                shachiEnabled = darkSourceEnabled = perfectDodgeEnabled = perfectDeflectEnabled = false;
                // A fault forbids further engine calls, but local hooks can stop
                // even when no valid pawn remains. Never clear an unverified tag.
                if (!deflectContribution.owned || (!faulted.load() && RemoveDeflectContribution()))
                    combatCleanupComplete.store(true);
                return true;
            }
            return false;
        }
        void CombatTick(const Context& context) {
            if (CombatCleanup()) return;
            if (!perfectDodgeEnabled && !perfectDeflectEnabled) return;
            if (!CombatBindingCurrent()) return;
            if (perfectDeflectEnabled && !EnsurePerfectDeflect(context)) {
                perfectDeflectEnabled = false; combatError = "完美格挡状态未通过回读，自动操作已停止";
            }
            if (!perfectDodgeEnabled) return;
            if (!DodgeContract(context)) { perfectDodgeEnabled = false; combatError = "闪避入口已变化，自动操作已停止"; return; }
            const auto deflect = Read<Address>(context.pawn + 0xed0), input = Read<Address>(context.pawn + 0xeb8);
            const auto system = Read<Address>(Read<Address>(base + 0xdf4d1c0) + 0x20);
            const auto cdo = Read<Address>(system + 0x160);
            struct Time { Address world; double result; } time{context.world, -1};
            struct Query {
                float angle{360}, preimpact{10000}; std::uint8_t dodge{1}, padding[3]{};
                std::int32_t current{-1}; std::array<std::byte, 80> frame{}; std::int32_t result{-1};
            } query;
            if (!ObjectValid(cdo) || !CallProcessEvent(cdo, FindFunction(system, "GetGameTimeInSeconds"), &time) ||
                !CallProcessEvent(deflect, FindFunction(Read<Address>(deflect + 0x10), "GetNearestValidReceivedFrame"), &query)) {
                perfectDodgeEnabled = false; faulted.store(true); return;
            }
            double hitTime{}; std::memcpy(&hitTime, query.frame.data() + 0x30, sizeof(hitTime));
            // Same timing gate as BP_PerfectDodgePassive, with the real incoming
            // frame selected by DeflectComponent and the normal ability checks.
            if (!ShouldAttemptPerfectDodge(query.result, hitTime, time.result, query.frame[0x39] != std::byte{},
                lastDodgeHandle, lastDodgeHitTime)) return;
            const auto tag = FindExistingName("Ability.Action.Hun.PerfectDodge");
            const auto action = DodgeInputAction(context);
            if (!tag || !action) return;
            struct Activate { NameValue tag; Address action; std::int32_t priority{-5}; std::uint8_t result{}; } parameters{*tag, action};
            if (!CallProcessEvent(input, FindFunction(Read<Address>(input + 0x10), "TryActivateExternalActionAbilityInterruptLowerByTag"), &parameters)) {
                perfectDodgeEnabled = false; faulted.store(true); return;
            }
            lastDodgeHandle = query.result; lastDodgeHitTime = hitTime;
            if (parameters.result) ++dodgeActivations;
        }
        ExecutionResult ExecutePerfect(const Context& context, const Request& request) {
            if (request.arguments.size() != 1 || (request.arguments[0] != "on" && request.arguments[0] != "off"))
                return {RequestState::Failed, "开关参数无效"};
            const bool enabled = request.arguments[0] == "on";
            if (request.featureId.starts_with("perfect_deflect_")) {
                if (!enabled) {
                    perfectDeflectEnabled = false;
                    return RemoveDeflectContribution() ? ExecutionResult{RequestState::Verified, "已撤销修改器添加的完美格挡状态"} :
                        ExecutionResult{RequestState::Indeterminate, "状态恢复未通过回读"};
                }
                if (!PerfectTagFunction(true) || !PerfectTagFunction(false)) return {RequestState::Failed, "格挡状态契约未通过"};
                perfectDeflectEnabled = true;
                if (!EnsurePerfectDeflect(context)) { perfectDeflectEnabled = false; return {RequestState::Indeterminate, "格挡状态未通过回读"}; }
                return {RequestState::Verified, "游戏完美格挡判定标签已生效；实际来袭判定需实战验证"};
            }
            if (enabled && (!DodgeContract(context) || !DodgeInputAction(context))) return {RequestState::Failed, "闪避入口或实际输入动作未就绪"};
            perfectDodgeEnabled = enabled;
            lastDodgeHandle = -1;
            return {RequestState::Verified, enabled ? "已启用来袭帧检测；按游戏规则自动尝试完美闪避" : "已停止自动闪避"};
        }
        ToggleState PerfectDeflectState(const Context& context) {
            const auto count = ReadGameplayTagCount(context, PerfectDeflectTag);
            return {"perfect_deflect", count.has_value(), count.value_or(0) > 0,
                count ? "游戏完美格挡判定标签实时回读；需处于可格挡动作" : "格挡状态未读回"};
        }
        ToggleState PerfectDodgeState(const Context& context) {
            const bool ready = CombatBindingCurrent() && DodgeContract(context);
            return {"perfect_dodge", ready, ready && perfectDodgeEnabled,
                combatError.empty() ? std::format("游戏接受的自动闪避请求：{}", dodgeActivations) : combatError};
        }
