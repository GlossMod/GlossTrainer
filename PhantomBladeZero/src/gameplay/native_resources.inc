// Local-player resource protection; included inside native_adapter's anonymous
// namespace after the attribute/combat helpers. Never hooks charge progress.
        #include "native_shachi_cost.inc"
        struct ResourceProtectionBinding {
            ResourceIdentity identity;
            Address property{};
            bool enabled{};
        } healthProtection, pureShachiProtection;
        struct PureShachiGainOverride {
            Context context;
            ResourceGainBaseline baseline;
            ResourceGainPreference preference;
            float targetBase{}, targetCurrent{};
            bool uncertain{};
        } pureShachiGainOverride;

        bool ResourcesFeature(std::string_view id) {
            return id == "infinite_health" || id == "infinite_pure_shachi" || id == "pure_shachi_gain" || id == "shachi_cost";
        }
        ResourceIdentity CurrentResourceIdentity(const Context& context) {
            const auto identity = CaptureAttributeIdentity(context);
            return {context.world, identity.pawn, identity.component, identity.attributes,
                context.worldIndex, identity.pawnIndex, identity.componentIndex, identity.attributeIndex};
        }
        bool ResourceContextCurrent(const Context& context) {
            if (!MovementContextCurrent(context) || !PlayerAttributesReady(context)) return false;
            const auto identity = CurrentResourceIdentity(context);
            if (movementCleanupScope) return pureShachiGainOverride.baseline.CanRestore(identity);
            if (!CombatBindingCurrent()) return false;
            return identity.attributes == combatBinding.attributes && identity.attributeIndex == combatBinding.attributeIndex &&
                identity.pawn == combatBinding.context.pawn && identity.pawnIndex == combatBinding.pawnIndex;
        }
        bool ResourceProperty(Address type, std::string_view name, std::int32_t offset, Address& property) {
            property = FindProperty(type, name);
            return PropertyLayout(property, "StructProperty", 16, offset) &&
                ObjectName(Read<Address>(property + 0x70)) == "GameplayAttributeData";
        }
        bool ResourcePair(const Context& context, bool health, Address& property, float& current, float& maximum) {
            if (!ResourceContextCurrent(context)) return false;
            const auto attributes = combatBinding.attributes, type = Read<Address>(attributes + 0x10);
            Address maximumProperty{};
            const auto offset = health ? 0x58 : 0x8b8, maximumOffset = health ? 0x68 : 0x8c8;
            if (!ResourceProperty(type, health ? "Health" : "UltimateChargeCount", offset, property) ||
                !ResourceProperty(type, health ? "Health_Max" : "UltimateChargeCount_Max", maximumOffset, maximumProperty) ||
                Read<Address>(property + 0x70) != Read<Address>(maximumProperty + 0x70)) return false;
            current = Read<float>(attributes + offset + 12);
            maximum = Read<float>(attributes + maximumOffset + 12);
            const auto baseValue = Read<float>(attributes + offset + 8);
            return std::isfinite(current) && current >= 0 && current <= 100000000 &&
                std::isfinite(baseValue) && baseValue >= 0 && baseValue <= 100000000 &&
                std::isfinite(maximum) && maximum > 0 && maximum <= 100000000;
        }
        bool ResourceGainProperty(const Context& context, Address& property, float& baseValue, float& current) {
            const auto attributes = Read<Address>(context.pawn + 0x758);
            if (!ResourceContextCurrent(context) || !ResourceProperty(Read<Address>(attributes + 0x10),
                "UltimateChargeGainRateMultiplier", 0x8e8, property)) return false;
            baseValue = Read<float>(attributes + 0x8f0);
            current = Read<float>(attributes + 0x8f4);
            return std::isfinite(baseValue) && baseValue >= 0 && baseValue <= 1000000 &&
                std::isfinite(current) && current >= 0 && current <= 1000000;
        }
        bool ResourcesCapability(const Context& context, std::string_view id, std::string& reason) {
            reason.clear();
            if (id == "shachi_cost") return ShachiCostCapability(context, reason);
            Address property{};
            float first{}, second{};
            bool ready{};
            if (id == "infinite_health" || id == "infinite_pure_shachi")
                ready = ResourcePair(context, id == "infinite_health", property, first, second) &&
                    (!shachiHooksInstalled.load() || ShachiHookOwnership());
            else if (id == "pure_shachi_gain") ready = ResourceGainProperty(context, property, first, second);
            if (!ready) reason = "本地玩家资源属性、实际数值或扣减入口未通过契约核对";
            return ready;
        }
        void ExtendedResourcePreChange(void* object, const void* attribute, float* proposed, bool baseValue) noexcept {
            __try {
                if (stopping.load() || faulted.load() || !shachiHooksInstalled.load() ||
                    GetCurrentThreadId() != expectedThread.load() || !attribute || !proposed ||
                    reinterpret_cast<Address>(object) != combatBinding.attributes || !CombatBindingCurrent()) return;
                const auto property = Read<Address>(reinterpret_cast<Address>(attribute) + 0x10);
                const ResourceProtectionBinding* binding{};
                bool health{};
                if (healthProtection.enabled && property == healthProtection.property) { binding = &healthProtection; health = true; }
                else if (pureShachiProtection.enabled && property == pureShachiProtection.property) binding = &pureShachiProtection;
                if (!binding) return;
                const auto& identity = binding->identity;
                if (identity.world != combatBinding.context.world || identity.worldIndex != combatBinding.context.worldIndex ||
                    identity.pawn != combatBinding.context.pawn || identity.pawnIndex != combatBinding.pawnIndex ||
                    identity.attributes != combatBinding.attributes || identity.attributeIndex != combatBinding.attributeIndex ||
                    Read<Address>(identity.pawn + 0x750) != identity.component ||
                    Read<std::uint32_t>(identity.component + 0xc) != identity.componentIndex) return;
                const auto offset = health ? 0x58 : 0x8b8, maximumOffset = health ? 0x68 : 0x8c8;
                const auto current = Read<float>(identity.attributes + offset + (baseValue ? 8 : 12));
                const auto maximum = Read<float>(identity.attributes + maximumOffset + 12);
                *proposed = ProtectedResourceValue(current, maximum, *proposed,
                    health ? ProtectedResourcePool::Health : ProtectedResourcePool::PureShachiSegments);
            } __except (EXCEPTION_EXECUTE_HANDLER) { /* An invalid identity never receives protection. */ }
        }
        ExecutionResult SetResourceProtection(const Context& context, bool health, bool enabled) {
            auto& binding = health ? healthProtection : pureShachiProtection;
            if (!enabled) {
                binding = {};
                return {RequestState::Verified, health ? "已停止生命扣减保护" : "已停止纯杀气段数扣减保护；充能转换保持游戏规则"};
            }
            Address property{};
            float current{}, maximum{};
            if (!ResourcePair(context, health, property, current, maximum) || !InstallShachiHooks())
                return {RequestState::Failed, "本地玩家资源属性或扣减入口已变化，未启用"};
            const auto identity = CurrentResourceIdentity(context);
            const auto attributeIdentity = CaptureAttributeIdentity(context);
            if (!SetEngineAttribute(identity.component, property, maximum))
                return {RequestState::Indeterminate, "资源补满调用异常，未启用新的扣减保护"};
            ++dispatchedCalls;
            if (!AttributeIdentityCurrent(context, attributeIdentity) || !ResourceContextCurrent(context) ||
                CurrentResourceIdentity(context) != identity)
                return {RequestState::Indeterminate, "补满后玩家资源对象已变化，未启用新的扣减保护"};
            Address afterProperty{};
            float after{}, afterMaximum{};
            if (!ResourcePair(context, health, afterProperty, after, afterMaximum) || property != afterProperty ||
                !ResourceValueClose(maximum, afterMaximum) || !ResourceValueClose(after, afterMaximum))
                return {RequestState::Dispatched, "已调用一次资源补满，但实际回读未匹配上限；未启用新的扣减保护"};
            binding = {identity, property, true};
            return {RequestState::Verified, health ? "已按实际生命上限补满并启用本地生命扣减保护；未改变上限" :
                "已按实际段数上限补满并保护纯杀气段数；充能进度和满额转换保持游戏规则"};
        }
        ExecutionResult SetPureShachiGain(const Context& context, bool enabled, float multiplier) {
            auto& state = pureShachiGainOverride;
            Address property{};
            float beforeBase{}, beforeCurrent{};
            if (!ResourceGainProperty(context, property, beforeBase, beforeCurrent))
                return {RequestState::Failed, "纯杀气获取倍率属性不可读，未执行"};
            const auto identity = CurrentResourceIdentity(context);
            const auto attributeIdentity = CaptureAttributeIdentity(context);
            if (!enabled && !state.baseline.captured) return {RequestState::Verified, "当前玩家未启用纯杀气获取倍率修改"};
            const bool firstCapture = !state.baseline.captured;
            if ((enabled && !state.baseline.Capture(identity, beforeBase, beforeCurrent)) ||
                (!enabled && !state.baseline.CanRestore(identity)))
                return {RequestState::Failed, "玩家资源属性身份变化，未写入旧原值"};
            if (enabled && firstCapture) state.context = context;
            const auto targetBase = state.baseline.Target(state.baseline.base, enabled ? multiplier : 1);
            const auto targetCurrent = state.baseline.Target(state.baseline.current, enabled ? multiplier : 1);
            if (!targetBase || !targetCurrent) return {RequestState::Failed, "纯杀气获取倍率或计算结果无效"};
            state.uncertain = true;
            const bool called = ResourceContextCurrent(context) && MovementAttributeIdentityCurrent(context, attributeIdentity) &&
                SetEngineAttribute(identity.component, property, *targetBase);
            if (called) ++dispatchedCalls;
            Address afterProperty{};
            float afterBase{}, afterCurrent{};
            const bool sameIdentity = ResourceContextCurrent(context) && MovementAttributeIdentityCurrent(context, attributeIdentity) &&
                CurrentResourceIdentity(context) == identity;
            const bool matched = called && sameIdentity && ResourceGainProperty(context, afterProperty, afterBase, afterCurrent) &&
                afterProperty == property && ResourceValueClose(afterBase, *targetBase) && ResourceValueClose(afterCurrent, *targetCurrent);
            if (!matched) {
                if (sameIdentity && SetEngineAttribute(identity.component, property, beforeBase)) {
                    ++dispatchedCalls;
                    Address rollbackProperty{};
                    float rollbackBase{}, rollbackCurrent{};
                    state.uncertain = !ResourceContextCurrent(context) || CurrentResourceIdentity(context) != identity ||
                        !ResourceGainProperty(context, rollbackProperty, rollbackBase, rollbackCurrent) || rollbackProperty != property ||
                        !ResourceValueClose(rollbackBase, beforeBase) || !ResourceValueClose(rollbackCurrent, beforeCurrent);
                }
                return {state.uncertain ? RequestState::Indeterminate : RequestState::Failed,
                    "纯杀气获取倍率未全部匹配实际回读；已尝试恢复编辑前原值"};
            }
            state.uncertain = false;
            state.targetBase = *targetBase;
            state.targetCurrent = *targetCurrent;
            state.preference.Commit(enabled, multiplier, true, true);
            if (!enabled) state.baseline.Clear();
            return {RequestState::Verified, enabled ? "纯杀气获取倍率的基础值与实际值均已回读" : "已恢复纯杀气获取倍率启用前原值并核对实际回读"};
        }
        ExecutionResult ExecuteResources(const Context& context, const Request& request) {
            if (request.arguments.size() < 2 || !ResourcesFeature(request.arguments[0]) ||
                (request.arguments[1] != "on" && request.arguments[1] != "off"))
                return {RequestState::Failed, "资源开关参数无效"};
            const auto& id = request.arguments[0];
            const bool enabled = request.arguments[1] == "on";
            const auto* definition = FindFeature(id + (enabled ? "_on" : "_off"));
            const std::vector<std::string> parameters(request.arguments.begin() + 2, request.arguments.end());
            std::string reason;
            if (!definition || !ValidateParameters(*definition, parameters, reason))
                return {RequestState::Failed, reason.empty() ? "资源功能定义无效" : reason};
            if (id == "shachi_cost") {
                double multiplier{1};
                if (enabled) {
                    const auto& text = parameters[0];
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), multiplier);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(multiplier))
                        return {RequestState::Failed, "杀气消耗倍率格式错误"};
                }
                return SetShachiCost(context, enabled, multiplier);
            }
            if (!enabled && id != "pure_shachi_gain") return SetResourceProtection(context, id == "infinite_health", false);
            if (!ResourcesCapability(context, id, reason)) return {RequestState::Failed, reason};
            float multiplier{1};
            if (id == "pure_shachi_gain" && enabled) {
                const auto& text = parameters[0];
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), multiplier);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(multiplier))
                    return {RequestState::Failed, "纯杀气获取倍率格式错误"};
            }
            if (id == "pure_shachi_gain") return SetPureShachiGain(context, enabled, multiplier);
            return SetResourceProtection(context, id == "infinite_health", true);
        }
        std::vector<ToggleState> ResourcesToggleStates(const Context& context) {
            std::vector<ToggleState> states;
            states.push_back(ShachiCostToggleState(context));
            for (const std::string id : {"infinite_health", "infinite_pure_shachi", "pure_shachi_gain"}) {
                std::string reason;
                bool known = ResourcesCapability(context, id, reason), enabled{};
                std::vector<std::string> parameters;
                if (id == "pure_shachi_gain") {
                    const auto& state = pureShachiGainOverride;
                    enabled = state.preference.enabled;
                    if (known && (enabled || state.uncertain)) {
                        Address property{};
                        float baseValue{}, current{};
                        known = !state.uncertain && state.baseline.CanRestore(CurrentResourceIdentity(context)) &&
                            ResourceGainProperty(context, property, baseValue, current) &&
                            ResourceValueClose(baseValue, state.targetBase) && ResourceValueClose(current, state.targetCurrent);
                        if (!known) reason = "纯杀气获取倍率被游戏覆盖、写入未确认或玩家对象变化";
                    }
                    if (enabled && known) parameters = {std::format("{:.9g}", state.preference.multiplier)};
                } else {
                    const bool health = id == "infinite_health";
                    const auto& binding = health ? healthProtection : pureShachiProtection;
                    enabled = binding.enabled;
                    if (known && enabled) known = binding.identity == CurrentResourceIdentity(context) &&
                        shachiHooksInstalled.load() && ShachiHookOwnership();
                    if (known) reason = health ? "本地生命扣减保护；生命与上限来自实际回读" :
                        "仅保护已蓄纯杀气段数；充能满额转为段数保持游戏规则";
                    else if (enabled) reason = "资源保护绑定或钩子所有权已变化";
                }
                states.push_back({id, known, known && enabled, std::move(reason), std::move(parameters)});
            }
            return states;
        }
        bool ResourcesSessionChanged(const Context& context) {
            ShachiCostSessionChanged();
            healthProtection = {};
            pureShachiProtection = {};
            auto& state = pureShachiGainOverride;
            if (!state.baseline.captured) return true;
            if (!MovementContextCurrent(context)) return false;
            // Never carry a saved base into a recreated attribute object.
            const auto& previous = state.baseline.identity;
            const auto observed = CurrentResourceIdentity(context);
            if (observed != previous) {
                const bool alive = ObjectValid(previous.world) && Read<std::uint32_t>(previous.world + 0xc) == previous.worldIndex &&
                    ObjectValid(previous.pawn) && Read<std::uint32_t>(previous.pawn + 0xc) == previous.pawnIndex &&
                    ObjectValid(previous.component) && Read<std::uint32_t>(previous.component + 0xc) == previous.componentIndex &&
                    ObjectValid(previous.attributes) && Read<std::uint32_t>(previous.attributes + 0xc) == previous.attributeIndex;
                if (DecideMovementCleanup(false, alive, true) == MovementCleanupDecision::DiscardExpired) { state = {}; return true; }
                const Context originalContext = state.context;
                const MovementCleanupScope scope{originalContext,
                    {previous.world, previous.pawn, previous.attributes, previous.worldIndex, previous.pawnIndex, previous.attributeIndex},
                    previous.pawnIndex};
                const MovementCleanupGuard guard(&scope);
                return SetPureShachiGain(originalContext, false, 1).state == RequestState::Verified;
            }
            return SetPureShachiGain(context, false, 1).state == RequestState::Verified;
        }
        bool ResourcesCleanup() {
            if (!LocalGameThread()) return false;
            const bool cost = ShachiCostCleanup();
            healthProtection = {};
            pureShachiProtection = {};
            if (!pureShachiGainOverride.baseline.captured) return cost;
            const auto context = CurrentContext();
            const bool resources = context && ResourcesSessionChanged(*context);
            return cost && resources;
        }
