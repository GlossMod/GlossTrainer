// Included after native_attributes.inc and native_combat.inc, inside the
// adapter's anonymous namespace. All state is confined to the game thread.
        struct MovementOverride {
            Context context;
            std::uint32_t contextPawnIndex{};
            MovementBaseline baseline;
            std::array<float, 5> originalCurrent{};
            std::array<float, 5> target{};
            float parameter{1};
            bool enabled{};
            bool uncertain{};
            std::string error;
        };
        MovementOverride walkOverride, playerTimeOverride, jumpOverride, gravityOverride, globalTimeOverride, fovOverride;
        struct FlightOverride {
            Context context;
            MovementBaseline baseline; // Original mode, fly speed, collision flag.
            Address capsule{};
            std::uint32_t capsuleIndex{};
            float speed{600};
            bool enabled{}, uncertain{};
        } flightOverride;
        struct MovementCleanupScope {
            Context context;
            MovementIdentity identity;
            std::uint32_t pawnIndex{};
        };
        thread_local const MovementCleanupScope* movementCleanupScope{};
        struct MovementCleanupGuard {
            const MovementCleanupScope* previous{movementCleanupScope};
            explicit MovementCleanupGuard(const MovementCleanupScope* scope) { movementCleanupScope = scope; }
            ~MovementCleanupGuard() { movementCleanupScope = previous; }
        };
        struct MovementVector { double x{}, y{}, z{}; };
        bool FlightContract(const Context& context, std::string& reason);
        ExecutionResult SetFlight(const Context& context, bool enabled, float speed);
        ExecutionResult FlightStep(const Context& context, const Request& request);
        bool FovContract(const Context& context);
        Address MovementCamera(const Context& context);
        std::optional<float> ReadMovementFov(const Context& context);
        ExecutionResult SetMovementFov(const Context& context, bool enabled, float value);
        struct AiMovementEntry {
            MovementIdentity identity;
            Address controller{};
            std::uint32_t controllerIndex{};
            float baseline{}, target{};
        };
        struct AiMovementOverride {
            std::vector<AiMovementEntry> entries;
            bool enabled{}, uncertain{};
            float parameter{1};
            Clock::time_point nextRefresh{};
            std::string error;
        } aiMovementOverride;
        bool AiMovementContract(const Context& context);
        ExecutionResult SetAiMovement(const Context& context, bool enabled, float value);
        bool AiMovementReadback(const Context& context);
        bool AiMovementCleanup(const Context& context);
        void AiMovementTick(const Context& context);

        bool MovementFeature(std::string_view id) {
            return id == "move_speed" || id == "player_speed" || id == "jump_height" || id == "gravity" ||
                id == "game_speed" || id == "ai_speed" || id == "fov" || id == "flight" ||
                id == "flight_up" || id == "flight_down";
        }
        MovementOverride* MovementOverrideFor(std::string_view id) {
            if (id == "move_speed") return &walkOverride;
            if (id == "player_speed") return &playerTimeOverride;
            if (id == "jump_height") return &jumpOverride;
            if (id == "gravity") return &gravityOverride;
            if (id == "game_speed") return &globalTimeOverride;
            if (id == "fov") return &fovOverride;
            return nullptr;
        }
        bool WriteMovementFloat(Address address, float value) noexcept {
            __try { *reinterpret_cast<float*>(address) = value; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        Address MovementComponent(const Context& context) {
            if (!LocalGameThread() || stopping.load() || faulted.load() || !ObjectValid(context.pawn) ||
                !PropertyLayout(FindProperty(Read<Address>(context.pawn + 0x10), "CharacterMovement"),
                    "ObjectProperty", 8, 0x338)) return 0;
            const auto movement = Read<Address>(context.pawn + 0x338);
            if (!ObjectValid(movement) || Read<Address>(movement + 0x20) != context.pawn ||
                !ClassNamed(Read<Address>(movement + 0x10), "PBZCharacterMovementComponent")) return 0;
            return movement;
        }
        MovementIdentity MovementIdentityFor(const Context& context, Address component) {
            return {context.world, context.pawn, component, context.worldIndex,
                Read<std::uint32_t>(context.pawn + 0xc), Read<std::uint32_t>(component + 0xc)};
        }
        MovementIdentity MovementWorldIdentity(const Context& context) {
            return {context.world, context.world, context.world,
                context.worldIndex, context.worldIndex, context.worldIndex};
        }
        MovementIdentity MovementObjectIdentity(const Context& context, Address object) {
            const auto index = Read<std::uint32_t>(object + 0xc);
            return {context.world, object, object, context.worldIndex, index, index};
        }
        bool MovementContextCurrent(const Context& context) {
            if (!LocalGameThread() || stopping.load() || faulted.load()) return false;
            const auto current = CurrentContext();
            if (!current) return false;
            if (movementCleanupScope && movementCleanupScope->context.SameKey(context)) {
                const auto& identity = movementCleanupScope->identity;
                return ObjectValid(context.world) && Read<std::uint32_t>(context.world + 0xc) == context.worldIndex &&
                    ObjectValid(context.pawn) && Read<std::uint32_t>(context.pawn + 0xc) == movementCleanupScope->pawnIndex &&
                    ObjectValid(identity.component) && Read<std::uint32_t>(identity.component + 0xc) == identity.componentIndex;
            }
            return current && current->SameKey(context) &&
                Read<std::uint32_t>(current->pawn + 0xc) == Read<std::uint32_t>(context.pawn + 0xc);
        }
        constexpr std::array<std::pair<std::string_view, std::int32_t>, 5> WalkAttributes{{
            {"WalkSpeed", 0xd88}, {"JogSpeed", 0xd98}, {"SprintSpeed", 0xda8},
            {"CrouchSpeed", 0xdb8}, {"FinalWalkSpeed", 0xdc8}}};
        bool WalkAttributesReady(const Context& context) {
            if (!PlayerAttributesReady(context)) return false;
            const auto attributes = Read<Address>(context.pawn + 0x758);
            const auto type = Read<Address>(attributes + 0x10);
            for (const auto& [name, offset] : WalkAttributes) {
                const auto property = FindProperty(type, name);
                if (!PropertyLayout(property, "StructProperty", 16, offset) ||
                    ObjectName(Read<Address>(property + 0x70)) != "GameplayAttributeData") return false;
                const auto original = Read<float>(attributes + offset + 8);
                const auto current = Read<float>(attributes + offset + 12);
                if (!std::isfinite(original) || original <= 0 || original > 1000000 ||
                    !std::isfinite(current) || current <= 0 || current > 1000000) return false;
            }
            return MovementComponent(context) != 0;
        }
        bool MovementScalar(const Context& context, std::string_view id, Address& object, std::int32_t& offset) {
            if (!MovementContextCurrent(context)) return false;
            std::string_view propertyName;
            if (id == "player_speed") {
                object = context.pawn; offset = 0x68; propertyName = "CustomTimeDilation";
            } else if (id == "jump_height" || id == "gravity") {
                object = MovementComponent(context);
                offset = id == "jump_height" ? 0x1b0 : 0x1a8;
                propertyName = id == "jump_height" ? "JumpZVelocity" : "GravityScale";
            } else return false;
            if (!object || !PropertyLayout(FindProperty(Read<Address>(object + 0x10), propertyName),
                "FloatProperty", 4, offset)) return false;
            float value{};
            return ReadBytes(object + offset, &value, sizeof(value)) && std::isfinite(value) && value >= 0 && value <= 1000000;
        }
        bool MovementAttributeIdentityCurrent(const Context& context, const AttributeIdentity& identity) {
            if (!movementCleanupScope) return AttributeIdentityCurrent(context, identity);
            if (!MovementContextCurrent(context) || !PlayerAttributesReady(context)) return false;
            const auto observed = CaptureAttributeIdentity(context);
            return observed.pawn == identity.pawn && observed.pawnIndex == identity.pawnIndex &&
                observed.component == identity.component && observed.componentIndex == identity.componentIndex &&
                observed.attributes == identity.attributes && observed.attributeIndex == identity.attributeIndex;
        }
        std::optional<float> ReadMovementGlobalTime(const Context& context) {
            if (!MovementContextCurrent(context) || !TimeDilationReady()) return {};
            const auto owner = Read<Address>(base + 0xdf45bf8);
            struct Parameters { Address world; float value; } parameters{context.world,
                std::numeric_limits<float>::quiet_NaN()};
            if (!CallProcessEvent(Read<Address>(owner + 0x160), FindFunction(owner, "GetGlobalTimeDilation"), &parameters))
                return {};
            if (!std::isfinite(parameters.value) || parameters.value < 0.0001f || parameters.value > 20) return {};
            return parameters.value;
        }
        ExecutionResult WriteMovementGlobalTime(const Context& context, float value) {
            if (!MovementContextCurrent(context) || !TimeDilationReady() || !std::isfinite(value) || value < 0.0001f || value > 20)
                return {RequestState::Failed, "全局时间倍率契约或恢复值无效"};
            const auto worldIdentity = MovementWorldIdentity(context);
            const auto owner = Read<Address>(base + 0xdf45bf8);
            struct Parameters { Address world; float value; } parameters{context.world, value};
            if (!CallProcessEvent(Read<Address>(owner + 0x160), FindFunction(owner, "SetGlobalTimeDilation"), &parameters))
                return {RequestState::Indeterminate, "全局时间倍率调用异常，保留原值记录"};
            ++dispatchedCalls;
            const auto after = ReadMovementGlobalTime(context);
            if (!after || MovementWorldIdentity(context) != worldIdentity)
                return {RequestState::Indeterminate, "全局时间倍率调用后世界变化或无法回读"};
            return MovementValueClose(*after, value) ? ExecutionResult{RequestState::Verified, "全局时间倍率已通过引擎Get接口回读"} :
                ExecutionResult{RequestState::Dispatched, "全局时间倍率回读未匹配目标"};
        }
        bool MovementCapability(const Context& context, std::string_view id, std::string& reason) {
            reason.clear();
            if (!MovementContextCurrent(context)) { reason = "等待本地玩家与游戏线程核对"; return false; }
            if (id == "flight" || id == "flight_up" || id == "flight_down") return FlightContract(context, reason);
            if (id == "fov") {
                const bool ready = FovContract(context) && ReadMovementFov(context).has_value();
                if (!ready) reason = "相机 SetFOV/UnlockFOV 入口或实际视野回读契约未通过";
                return ready;
            }
            if (id == "ai_speed") {
                const bool ready = AiMovementContract(context);
                if (!ready) reason = "AI 玩家枚举、控制器归属与时间倍率契约未通过";
                return ready;
            }
            if (id == "move_speed") {
                const bool ready = WalkAttributesReady(context);
                if (!ready) reason = "本地移动属性、能力系统或 CharacterMovement 契约不符";
                return ready;
            }
            if (id == "game_speed") {
                const bool ready = ReadMovementGlobalTime(context).has_value();
                if (!ready) reason = "全局时间倍率 Set/Get 契约或回读无效";
                return ready;
            }
            Address object{};
            std::int32_t offset{};
            const bool ready = MovementScalar(context, id, object, offset);
            if (!ready) reason = "本地玩家移动字段类型、偏移或实际数值未通过核对";
            return ready;
        }
        ExecutionResult SetMovementScalar(const Context& context, std::string_view id, bool enabled, float value) {
            auto& state = *MovementOverrideFor(id);
            Address object{};
            std::int32_t offset{};
            if (!MovementScalar(context, id, object, offset)) return {RequestState::Failed, "移动字段契约已变化，未写入"};
            const auto identity = MovementIdentityFor(context, object);
            const float before = Read<float>(object + offset);
            if (!enabled && !state.baseline.captured) return {RequestState::Verified, "当前玩家未启用此移动修改"};
            if (!enabled && !state.baseline.CanRestore(identity)) return {RequestState::Failed, "玩家或组件身份变化，未写入旧原值"};
            const bool firstCapture = !state.baseline.captured;
            if (enabled && !state.baseline.Capture(identity, &before, 1))
                return {RequestState::Failed, "移动原值无法保存或属于另一玩家，未执行"};
            if (enabled && firstCapture) { state.context = context; state.contextPawnIndex = identity.pawnIndex; }
            const auto target = !enabled ? std::optional(state.baseline.original[0]) :
                MovementTarget(state.baseline.original[0], value,
                    id == "jump_height" ? MovementValueKind::JumpHeight : MovementValueKind::Multiplier,
                    id == "gravity" ? 0.0f : 0.1f, id == "jump_height" ? 10.0f : 5.0f);
            if (!target) return {RequestState::Failed, "移动倍率或计算结果超出有效范围"};
            if (!MovementContextCurrent(context) || !WriteMovementFloat(object + offset, *target))
                { state.uncertain = true; return {RequestState::Indeterminate, "移动字段写入未完成，保留原值记录"}; }
            Address afterObject{};
            std::int32_t afterOffset{};
            if (!MovementScalar(context, id, afterObject, afterOffset) || afterObject != object ||
                !state.baseline.CanRestore(MovementIdentityFor(context, afterObject)))
                { state.uncertain = true; return {RequestState::Indeterminate, "移动字段写入后对象身份变化，未继续写入"}; }
            const auto observed = Read<float>(object + offset);
            if (!MovementValueClose(observed, *target)) {
                state.uncertain = !WriteMovementFloat(object + offset, before) ||
                    !MovementValueClose(Read<float>(object + offset), before);
                state.error = "移动字段未匹配目标；已尝试恢复本次编辑前值";
                return {state.uncertain ? RequestState::Indeterminate : RequestState::Failed, state.error};
            }
            state.enabled = enabled;
            state.uncertain = false;
            state.parameter = value;
            state.target[0] = *target;
            state.error.clear();
            if (!enabled) state.baseline.Clear();
            return {RequestState::Verified, enabled ? "移动参数已写入并核对回读" : "移动字段已恢复启用前原值并核对回读"};
        }
        bool WriteWalkBases(const Context& context, const AttributeIdentity& identity,
            const std::array<float, 5>& values) {
            for (std::size_t i = 0; i < WalkAttributes.size(); ++i) {
                if (!MovementAttributeIdentityCurrent(context, identity) ||
                    !SetEngineAttribute(identity.component,
                        FindProperty(Read<Address>(identity.attributes + 0x10), WalkAttributes[i].first), values[i])) return false;
                ++dispatchedCalls;
            }
            return MovementAttributeIdentityCurrent(context, identity);
        }
        ExecutionResult SetWalkMultiplier(const Context& context, bool enabled, float value) {
            auto& state = walkOverride;
            if (!WalkAttributesReady(context)) return {RequestState::Failed, "移动属性已失效，未执行"};
            const auto attributeIdentity = CaptureAttributeIdentity(context);
            const auto identity = MovementIdentityFor(context, attributeIdentity.attributes);
            std::array<float, 5> before{}, current{};
            for (std::size_t i = 0; i < before.size(); ++i) {
                before[i] = Read<float>(attributeIdentity.attributes + WalkAttributes[i].second + 8);
                current[i] = Read<float>(attributeIdentity.attributes + WalkAttributes[i].second + 12);
            }
            if (!enabled && !state.baseline.captured) return {RequestState::Verified, "当前玩家未启用移动速度修改"};
            if (!enabled && !state.baseline.CanRestore(identity)) return {RequestState::Failed, "玩家属性身份变化，未恢复旧移动原值"};
            const bool firstCapture = !state.baseline.captured;
            if (enabled && !state.baseline.Capture(identity, before.data(), before.size()))
                return {RequestState::Failed, "移动属性原值无法保存，未执行"};
            if (firstCapture) { state.originalCurrent = current; state.context = context; state.contextPawnIndex = identity.pawnIndex; }
            std::array<float, 5> targetBases{}, expected{};
            for (std::size_t i = 0; i < expected.size(); ++i) {
                const auto target = MovementTarget(state.originalCurrent[i], enabled ? value : 1,
                    MovementValueKind::Multiplier, 0.1f, 5);
                if (!target) return {RequestState::Failed, "移动速度计算结果无效"};
                expected[i] = *target;
                targetBases[i] = enabled ? state.baseline.original[i] * value : state.baseline.original[i];
            }
            if (!WriteWalkBases(context, attributeIdentity, targetBases)) {
                state.uncertain = true;
                if (MovementAttributeIdentityCurrent(context, attributeIdentity)) {
                    state.uncertain = !WriteWalkBases(context, attributeIdentity, before);
                    for (std::size_t i = 0; !state.uncertain && i < current.size(); ++i)
                        state.uncertain |= !MovementValueClose(Read<float>(attributeIdentity.attributes + WalkAttributes[i].second + 12), current[i]);
                }
                return {RequestState::Indeterminate, "移动速度写入未完成，已尝试回滚；保留原值记录"};
            }
            bool matched = true;
            for (std::size_t i = 0; i < expected.size(); ++i) {
                const auto baseValue = Read<float>(attributeIdentity.attributes + WalkAttributes[i].second + 8);
                const auto actual = Read<float>(attributeIdentity.attributes + WalkAttributes[i].second + 12);
                matched &= MovementValueClose(baseValue, targetBases[i]) && MovementValueClose(actual, expected[i]);
            }
            if (!matched) {
                const bool rolledBack = WriteWalkBases(context, attributeIdentity, before);
                state.uncertain = !rolledBack;
                for (std::size_t i = 0; !state.uncertain && i < current.size(); ++i)
                    state.uncertain |= !MovementValueClose(Read<float>(attributeIdentity.attributes + WalkAttributes[i].second + 12), current[i]);
                state.error = rolledBack ? "移动速度实际回读未匹配，已恢复本次编辑前原值" : "移动速度回读与回滚均未完成";
                return {state.uncertain ? RequestState::Indeterminate : RequestState::Failed, state.error};
            }
            state.enabled = enabled;
            state.uncertain = false;
            state.parameter = value;
            state.target = expected;
            state.error.clear();
            if (!enabled) state.baseline.Clear();
            return {RequestState::Verified, enabled ? "五项玩家移动属性已按原值倍率更新并回读" : "五项玩家移动属性已恢复启用前原值并回读"};
        }
        ExecutionResult SetMovementGlobalTime(const Context& context, bool enabled, float value) {
            auto& state = globalTimeOverride;
            const auto before = ReadMovementGlobalTime(context);
            if (!before) return {RequestState::Failed, "全局时间倍率无法读取，未执行"};
            const auto identity = MovementWorldIdentity(context);
            if (!enabled && !state.baseline.captured) return {RequestState::Verified, "当前会话未启用游戏速度修改"};
            if (!enabled && !state.baseline.CanRestore(identity)) return {RequestState::Failed, "世界身份变化，未恢复旧时间倍率"};
            const bool firstCapture = !state.baseline.captured;
            if (enabled && !state.baseline.Capture(identity, &*before, 1)) return {RequestState::Failed, "时间倍率原值无法保存"};
            if (enabled && firstCapture) { state.context = context; state.contextPawnIndex = Read<std::uint32_t>(context.pawn + 0xc); }
            // Global speed is an absolute world multiplier, unlike player speed.
            const auto target = enabled ? value : state.baseline.original[0];
            auto result = WriteMovementGlobalTime(context, target);
            if (result.state != RequestState::Verified) {
                state.uncertain = true;
                if (MovementContextCurrent(context)) {
                    state.uncertain = WriteMovementGlobalTime(context, *before).state != RequestState::Verified;
                }
                return result;
            }
            state.enabled = enabled;
            state.uncertain = false;
            state.parameter = value;
            state.target[0] = target;
            state.error.clear();
            if (!enabled) state.baseline.Clear();
            return result;
        }
        ExecutionResult ExecuteMovement(const Context& context, const Request& request) {
            if (!request.arguments.empty() && (request.arguments[0] == "flight_up" || request.arguments[0] == "flight_down"))
                return FlightStep(context, request);
            if (request.arguments.size() < 2 || !MovementFeature(request.arguments[0]) ||
                (request.arguments[1] != "on" && request.arguments[1] != "off"))
                return {RequestState::Failed, "移动开关参数错误"};
            const auto& id = request.arguments[0];
            const bool enabled = request.arguments[1] == "on";
            const auto* definition = FindFeature(id + (enabled ? "_on" : "_off"));
            std::vector<std::string> parameters(request.arguments.begin() + 2, request.arguments.end());
            std::string reason;
            if (!definition || !ValidateParameters(*definition, parameters, reason) || !MovementCapability(context, id, reason))
                return {RequestState::Failed, reason.empty() ? "移动功能尚未完成运行时适配" : reason};
            float value{1};
            if (enabled && !parameters.empty()) {
                const auto& text = parameters[0];
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
                    return {RequestState::Failed, "移动参数不是有效浮点数"};
            }
            if (id == "move_speed") return SetWalkMultiplier(context, enabled, value);
            if (id == "game_speed") return SetMovementGlobalTime(context, enabled, value);
            if (id == "fov") return SetMovementFov(context, enabled, value);
            if (id == "flight") return SetFlight(context, enabled, value);
            if (id == "ai_speed") return SetAiMovement(context, enabled, value);
            return SetMovementScalar(context, id, enabled, value);
        }
        constexpr std::array<std::string_view, 8> MovementToggleIds{
            "move_speed", "player_speed", "jump_height", "gravity", "game_speed", "ai_speed", "fov", "flight"};
        ToggleState MovementToggleState(const Context& context, const std::string id) {
            std::string reason;
            bool known = MovementCapability(context, id, reason);
            const auto* state = MovementOverrideFor(id);
            bool enabled = known && state && state->enabled;
            if (enabled) {
                if (id == "game_speed") {
                    const auto actual = ReadMovementGlobalTime(context);
                    known = actual && state->baseline.CanRestore(MovementWorldIdentity(context)) &&
                        MovementValueClose(*actual, state->target[0]);
                } else if (id == "move_speed") {
                    const auto attributes = Read<Address>(context.pawn + 0x758);
                    known = state->baseline.CanRestore(MovementIdentityFor(context, attributes));
                    for (std::size_t i = 0; known && i < WalkAttributes.size(); ++i)
                        known &= MovementValueClose(Read<float>(attributes + WalkAttributes[i].second + 12), state->target[i]);
                } else if (id == "fov") {
                    const auto actual = ReadMovementFov(context);
                    known = actual && state->baseline.CanRestore(MovementObjectIdentity(context, MovementCamera(context))) &&
                        MovementValueClose(*actual, state->target[0]);
                } else {
                    Address object{};
                    std::int32_t offset{};
                    known = MovementScalar(context, id, object, offset) &&
                        state->baseline.CanRestore(MovementIdentityFor(context, object)) &&
                        MovementValueClose(Read<float>(object + offset), state->target[0]);
                }
                if (!known) reason = "已启用的移动参数被游戏覆盖或对象变化，当前结果未匹配";
            }
            if (state && state->uncertain) { known = false; reason = "上次移动写入或恢复未通过回读，状态不确定"; }
            if (id == "flight" && known) {
                const auto movement = MovementComponent(context);
                enabled = flightOverride.enabled;
                known = !flightOverride.uncertain && (!enabled ||
                    (flightOverride.baseline.CanRestore(MovementIdentityFor(context, movement)) &&
                    Read<std::uint8_t>(movement + 0x241) == 5 &&
                    !(Read<std::uint8_t>(context.pawn + 0x5d) & 4) &&
                    MovementValueClose(Read<float>(movement + 0x294), flightOverride.speed)));
                if (!known) reason = "飞行状态或恢复结果未通过实际回读";
            }
            if (id == "ai_speed" && known) {
                enabled = aiMovementOverride.enabled;
                known = !aiMovementOverride.uncertain && (!enabled || AiMovementReadback(context));
                if (!known) reason = aiMovementOverride.error.empty() ? "AI 倍率尚未通过实际对象回读" : aiMovementOverride.error;
            }
            if (reason.empty()) reason = state && !state->error.empty() ? state->error : "本地玩家参数实时回读";
            return {id, known, known && enabled, std::move(reason),
                enabled && known ? std::vector<std::string>{std::format("{:.9g}",
                    id == "flight" ? flightOverride.speed : id == "ai_speed" ? aiMovementOverride.parameter : state->parameter)} : std::vector<std::string>{}};
        }
        bool MovementCleanup(const Context& context) {
            if (!MovementContextCurrent(context)) return false;
            bool complete = true;
            complete &= AiMovementCleanup(context);
            if (flightOverride.baseline.captured) {
                const auto movement = Read<Address>(context.pawn + 0x338);
                if (flightOverride.baseline.CanRestore(MovementIdentityFor(context, movement)))
                    complete &= SetFlight(context, false, 600).state == RequestState::Verified;
                else {
                    const auto& identity = flightOverride.baseline.identity;
                    const bool alive = ObjectValid(identity.world) && Read<std::uint32_t>(identity.world + 0xc) == identity.worldIndex &&
                        ObjectValid(identity.pawn) && Read<std::uint32_t>(identity.pawn + 0xc) == identity.pawnIndex &&
                        ObjectValid(identity.component) && Read<std::uint32_t>(identity.component + 0xc) == identity.componentIndex;
                    if (DecideMovementCleanup(false, alive, true) == MovementCleanupDecision::DiscardExpired) flightOverride = {};
                    else {
                        const MovementCleanupScope scope{flightOverride.context, identity, identity.pawnIndex};
                        const MovementCleanupGuard guard(&scope);
                        const auto result = SetFlight(flightOverride.context, false, 600);
                        complete &= result.state == RequestState::Verified;
                    }
                }
            }
            for (const std::string id : {"move_speed", "player_speed", "jump_height", "gravity", "game_speed", "fov"}) {
                auto& state = *MovementOverrideFor(id);
                if (!state.baseline.captured) continue;
                MovementIdentity identity;
                if (id == "game_speed") identity = MovementWorldIdentity(context);
                else if (id == "fov") identity = MovementObjectIdentity(context, Read<Address>(context.controller + 0x368));
                else if (id == "move_speed") identity = MovementIdentityFor(context, Read<Address>(context.pawn + 0x758));
                else {
                    const auto object = id == "player_speed" ? context.pawn : Read<Address>(context.pawn + 0x338);
                    identity = MovementIdentityFor(context, object);
                }
                const bool detached = !state.baseline.CanRestore(identity);
                if (detached) {
                    const auto& previous = state.baseline.identity;
                    const bool alive = ObjectValid(previous.world) && Read<std::uint32_t>(previous.world + 0xc) == previous.worldIndex &&
                        ObjectValid(previous.pawn) && Read<std::uint32_t>(previous.pawn + 0xc) == previous.pawnIndex &&
                        ObjectValid(previous.component) && Read<std::uint32_t>(previous.component + 0xc) == previous.componentIndex;
                    if (DecideMovementCleanup(false, alive, true) == MovementCleanupDecision::DiscardExpired) { state = {}; continue; }
                }
                const Context targetContext = detached ? state.context : context;
                const MovementCleanupScope scope{targetContext, state.baseline.identity, state.contextPawnIndex};
                const MovementCleanupGuard guard(detached ? &scope : nullptr);
                const auto result = id == "move_speed" ? SetWalkMultiplier(targetContext, false, 1) :
                    id == "game_speed" ? SetMovementGlobalTime(targetContext, false, 1) :
                    id == "fov" ? SetMovementFov(targetContext, false, 90) : SetMovementScalar(targetContext, id, false, 1);
                complete &= result.state == RequestState::Verified;
            }
            return complete;
        }
        bool MovementSessionChanged(const Context& context) { return MovementCleanup(context); }
        bool MovementCleanup() {
            if (!LocalGameThread()) return false;
            const auto context = CurrentContext();
            if (context) return MovementCleanup(*context);
            return !walkOverride.baseline.captured && !playerTimeOverride.baseline.captured &&
                !jumpOverride.baseline.captured && !gravityOverride.baseline.captured && !globalTimeOverride.baseline.captured &&
                !flightOverride.baseline.captured && !fovOverride.baseline.captured && aiMovementOverride.entries.empty();
        }
        void MovementTick(const Context& context) {
            // Do not repeatedly patch engine properties after a failed write or
            // unexpected game-side change. AI refresh only admits newly spawned
            // pawns and validates the ownership of each object before writing.
            AiMovementTick(context);
            if (!playerTimeOverride.enabled || !MovementContextCurrent(context)) return;
            for (const std::string_view id : {"player_speed"}) {
                auto& state = *MovementOverrideFor(id);
                if (!state.enabled || state.uncertain || !state.baseline.captured) continue;
                Address object{};
                std::int32_t offset{};
                const bool identityCurrent = MovementScalar(context, id, object, offset) &&
                    state.baseline.CanRestore(MovementIdentityFor(context, object));
                const auto maintenance = DecideMovementMaintenance(state.enabled, state.uncertain, identityCurrent,
                    identityCurrent ? Read<float>(object + offset) : 0, state.target[0]);
                if (maintenance == MovementMaintenanceDecision::Stop) {
                    state.uncertain = true;
                    state.error = "已启用的移动参数对象身份或字段契约变化，停止维持";
                    continue;
                }
                if (maintenance != MovementMaintenanceDecision::Rewrite) continue;
                if (!WriteMovementFloat(object + offset, state.target[0]) ||
                    !MovementContextCurrent(context) || !state.baseline.CanRestore(MovementIdentityFor(context, object)) ||
                    !MovementValueClose(Read<float>(object + offset), state.target[0])) {
                    state.uncertain = true;
                    state.error = "移动参数维持未通过立即回读，停止继续写入并保留原值";
                }
            }
        }

        Address FlightCapsule(const Context& context) {
            if (!MovementContextCurrent(context) ||
                !PropertyLayout(FindProperty(Read<Address>(context.pawn + 0x10), "CapsuleComponent"),
                    "ObjectProperty", 8, 0x340)) return 0;
            const auto capsule = Read<Address>(context.pawn + 0x340);
            return ObjectValid(capsule) && Read<Address>(capsule + 0x20) == context.pawn &&
                ClassNamed(Read<Address>(capsule + 0x10), "CapsuleComponent") ? capsule : 0;
        }
        Address FlightSystemLibrary() {
            const auto type = Read<Address>(Read<Address>(base + 0xdf4d1c0) + 0x20);
            return ObjectValid(type) && ObjectName(type) == "KismetSystemLibrary" &&
                ObjectValid(Read<Address>(type + 0x160)) ? type : 0;
        }
        bool FlightTraceSignature(Address function) {
            if (!CombatSignature(function, "CapsuleTraceSingleByProfile", 0x407bb80, 0x04c22403, 401, {
                {"WorldContextObject", "ObjectProperty", 0, 8, 0x18001000000282ull, "Object"},
                {"Start", "StructProperty", 8, 24, 0x18001040000282ull, "Vector"},
                {"End", "StructProperty", 0x20, 24, 0x18001040000282ull, "Vector"},
                {"Radius", "FloatProperty", 0x38, 4, 0x18001040000280ull, ""},
                {"HalfHeight", "FloatProperty", 0x3c, 4, 0x18001040000280ull, ""},
                {"ProfileName", "NameProperty", 0x40, 8, 0x18001040000280ull, ""},
                {"bTraceComplex", "BoolProperty", 0x48, 1, 0x18001040000280ull, ""},
                {"ActorsToIgnore", "ArrayProperty", 0x50, 16, 0x10000008000382ull, ""},
                {"DrawDebugType", "ByteProperty", 0x60, 1, 0x18001040000280ull, "EDrawDebugTrace"},
                {"OutHit", "StructProperty", 0x68, 256, 0x10009040000180ull, "HitResult"},
                {"bIgnoreSelf", "BoolProperty", 0x168, 1, 0x18001040000280ull, ""},
                {"TraceColor", "StructProperty", 0x16c, 16, 0x18041040000280ull, "LinearColor"},
                {"TraceHitColor", "StructProperty", 0x17c, 16, 0x18041040000280ull, "LinearColor"},
                {"DrawTime", "FloatProperty", 0x18c, 4, 0x18041040000280ull, ""},
                {"ReturnValue", "BoolProperty", 0x190, 1, 0x18001040000780ull, ""}})) return false;
            const auto ignored = FindProperty(function, "ActorsToIgnore");
            const auto inner = Read<Address>(ignored + 0x78);
            return inner && Read<std::int32_t>(inner + 0x34) == 8 &&
                Name(Read<std::uint32_t>(Read<Address>(inner + 8) + 0x30)) == "ObjectProperty" &&
                ObjectName(Read<Address>(inner + 0x70)) == "Actor";
        }
        bool FlightContract(const Context& context, std::string& reason) {
            const auto movement = MovementComponent(context), capsule = FlightCapsule(context);
            const auto pawnType = Read<Address>(context.pawn + 0x10);
            const auto movementType = Read<Address>(movement + 0x10), capsuleType = Read<Address>(capsule + 0x10);
            const auto collision = FindProperty(pawnType, "bActorEnableCollision");
            const auto type = FlightSystemLibrary();
            const bool valid = movement && capsule && type &&
                PropertyLayout(FindProperty(movementType, "MaxFlySpeed"), "FloatProperty", 4, 0x294) &&
                PropertyLayout(FindProperty(movementType, "MovementMode"), "ByteProperty", 1, 0x241) &&
                PropertyLayout(FindProperty(movementType, "CustomMovementMode"), "ByteProperty", 1, 0x242) &&
                PropertyLayout(collision, "BoolProperty", 1, 0x5d) && Read<std::uint32_t>(collision + 0x70) == 0x04040001 &&
                CombatSignature(FindFunction(movementType, "SetMovementMode"), "SetMovementMode", 0x3d276f0, 0x04020402, 2, {
                    {"NewMovementMode", "ByteProperty", 0, 1, 0x18001040000280ull, "EMovementMode"},
                    {"NewCustomMode", "ByteProperty", 1, 1, 0x18001040000280ull, ""}}) &&
                CombatSignature(FindFunction(pawnType, "SetActorEnableCollision"), "SetActorEnableCollision", 0x3aceb90, 0x04020403, 1, {
                    {"bNewActorEnableCollision", "BoolProperty", 0, 1, 0x18001040000280ull, ""}}) &&
                CombatSignature(FindFunction(pawnType, "GetActorEnableCollision"), "GetActorEnableCollision", 0x3ac8fd0, 0x54020401, 1, {
                    {"ReturnValue", "BoolProperty", 0, 1, 0x18001040000780ull, ""}}) &&
                CombatSignature(FindFunction(pawnType, "K2_AddActorWorldOffset"), "K2_AddActorWorldOffset", 0x3acba60, 0x04c20403, 289, {
                    {"DeltaLocation", "StructProperty", 0, 24, 0x18001040000280ull, "Vector"},
                    {"bSweep", "BoolProperty", 0x18, 1, 0x18001040000280ull, ""},
                    {"SweepHitResult", "StructProperty", 0x20, 256, 0x10009040000180ull, "HitResult"},
                    {"bTeleport", "BoolProperty", 0x120, 1, 0x18001040000280ull, ""}}) &&
                CombatSignature(FindFunction(capsuleType, "GetScaledCapsuleSize"), "GetScaledCapsuleSize", 0x3d24cb0, 0x54420401, 8, {
                    {"OutRadius", "FloatProperty", 0, 4, 0x18001040000380ull, ""},
                    {"OutHalfHeight", "FloatProperty", 4, 4, 0x18001040000380ull, ""}}) &&
                CombatSignature(FindFunction(capsuleType, "GetCollisionProfileName"), "GetCollisionProfileName", 0x3d73550, 0x54020403, 8, {
                    {"ReturnValue", "NameProperty", 0, 8, 0x18001040000780ull, ""}}) &&
                CombatSignature(FindFunction(capsuleType, "K2_GetComponentLocation"), "K2_GetComponentLocation", 0x3dcd140, 0x54820403, 24, {
                    {"ReturnValue", "StructProperty", 0, 24, 0x18001040000780ull, "Vector"}}) &&
                FlightTraceSignature(FindFunction(type, "CapsuleTraceSingleByProfile"));
            if (!valid) reason = "飞行移动模式、胶囊、碰撞与空间检查反射契约未通过";
            return valid;
        }
        bool FlightIdentityCurrent(const Context& context) {
            const auto movement = MovementComponent(context), capsule = FlightCapsule(context);
            return MovementContextCurrent(context) && flightOverride.baseline.CanRestore(MovementIdentityFor(context, movement)) &&
                capsule && capsule == flightOverride.capsule && Read<std::uint32_t>(capsule + 0xc) == flightOverride.capsuleIndex;
        }
        std::optional<bool> FlightCollision(const Context& context) {
            std::uint8_t enabled{2};
            if (!MovementContextCurrent(context) || !CallProcessEvent(context.pawn,
                FindFunction(Read<Address>(context.pawn + 0x10), "GetActorEnableCollision"), &enabled) || enabled > 1) return {};
            return enabled != 0;
        }
        bool FlightCollision(const Context& context, bool enabled) {
            auto value = static_cast<std::uint8_t>(enabled);
            if (!FlightIdentityCurrent(context) || !CallProcessEvent(context.pawn,
                FindFunction(Read<Address>(context.pawn + 0x10), "SetActorEnableCollision"), &value)) return false;
            const auto observed = FlightCollision(context);
            return FlightIdentityCurrent(context) && observed && *observed == enabled;
        }
        bool FlightMode(const Context& context, std::uint8_t mode, std::uint8_t custom) {
            const auto movement = MovementComponent(context);
            struct Parameters { std::uint8_t mode, custom; } parameters{mode, custom};
            return FlightIdentityCurrent(context) && CallProcessEvent(movement,
                FindFunction(Read<Address>(movement + 0x10), "SetMovementMode"), &parameters) &&
                FlightIdentityCurrent(context) && Read<std::uint8_t>(movement + 0x241) == mode &&
                Read<std::uint8_t>(movement + 0x242) == custom;
        }
        std::optional<MovementVector> FlightLocation(const Context& context) {
            const auto capsule = FlightCapsule(context);
            MovementVector location;
            if (!capsule || !CallProcessEvent(capsule,
                FindFunction(Read<Address>(capsule + 0x10), "K2_GetComponentLocation"), &location) ||
                !MovementContextCurrent(context) || !std::isfinite(location.x) || !std::isfinite(location.y) || !std::isfinite(location.z)) return {};
            return location;
        }
        std::optional<bool> FlightSpaceFree(const Context& context) {
            if (!FlightIdentityCurrent(context)) return {};
            const auto capsule = FlightCapsule(context), type = FlightSystemLibrary();
            const auto location = FlightLocation(context);
            struct Size { float radius{}, halfHeight{}; } size;
            NameValue profile;
            if (!location || !CallProcessEvent(capsule, FindFunction(Read<Address>(capsule + 0x10), "GetScaledCapsuleSize"), &size) ||
                !CallProcessEvent(capsule, FindFunction(Read<Address>(capsule + 0x10), "GetCollisionProfileName"), &profile) ||
                !Name(profile.comparison) || !std::isfinite(size.radius) || !std::isfinite(size.halfHeight) ||
                size.radius < 1 || size.halfHeight < size.radius || size.halfHeight > 10000 || !FlightIdentityCurrent(context)) return {};
            alignas(16) std::array<std::byte, 416> parameters{};
            const auto put = [&](std::size_t offset, const auto& value) { std::memcpy(parameters.data() + offset, &value, sizeof(value)); };
            const auto end = MovementVector{location->x, location->y, location->z + 0.01};
            put(0, context.pawn); put(8, *location); put(0x20, end);
            put(0x38, size.radius); put(0x3c, size.halfHeight); put(0x40, profile);
            Address ignoredActor = context.pawn;
            struct Ignored { Address data; std::int32_t count, capacity; } ignored{reinterpret_cast<Address>(&ignoredActor), 1, 1};
            put(0x50, ignored);
            parameters[0x168] = std::byte{1}; // Ignore the local pawn.
            parameters[0x190] = std::byte{2}; // Reject a missing return-value write.
            if (!CallProcessEvent(Read<Address>(type + 0x160), FindFunction(type, "CapsuleTraceSingleByProfile"), parameters.data()) ||
                !FlightIdentityCurrent(context) || parameters[0x190] > std::byte{1}) return {};
            return parameters[0x190] == std::byte{};
        }
        ExecutionResult SetFlight(const Context& context, bool enabled, float speed) {
            std::string reason;
            if (!FlightContract(context, reason)) return {RequestState::Failed, reason};
            const auto movement = MovementComponent(context), capsule = FlightCapsule(context);
            auto& state = flightOverride;
            if (!enabled && !state.baseline.captured) return {RequestState::Verified, "当前玩家未启用飞行"};
            if (state.baseline.captured && !FlightIdentityCurrent(context))
                return {RequestState::Failed, "飞行对象身份变化，未恢复旧原值"};
            const auto collision = FlightCollision(context);
            const auto mode = Read<std::uint8_t>(movement + 0x241), custom = Read<std::uint8_t>(movement + 0x242);
            if (!collision || mode > 6 || (enabled && (speed < 50 || speed > 5000 || !std::isfinite(speed))))
                return {RequestState::Failed, "飞行状态或速度参数无效"};
            const float baseline[]{static_cast<float>(mode), Read<float>(movement + 0x294), *collision ? 1.0f : 0.0f,
                static_cast<float>(custom)};
            const bool firstCapture = !state.baseline.captured;
            if (enabled && !state.baseline.Capture(MovementIdentityFor(context, movement), baseline, 4))
                return {RequestState::Failed, "飞行原值无法保存，未执行"};
            if (enabled && firstCapture) state.context = context;
            if (enabled && !state.capsule) { state.capsule = capsule; state.capsuleIndex = Read<std::uint32_t>(capsule + 0xc); }
            if (!enabled) {
                const auto freeSpace = FlightSpaceFree(context);
                if (!SafeFlightExit(FlightIdentityCurrent(context), freeSpace.has_value(), freeSpace.value_or(false)))
                    return {RequestState::Failed, freeSpace ? "当前位置有阻挡，保持飞行；请移动到空旷位置后关闭" : "碰撞空间无法核对，保持飞行"};
                state.uncertain = true;
                if (!WriteMovementFloat(movement + 0x294, state.baseline.original[1]) ||
                    !FlightMode(context, static_cast<std::uint8_t>(state.baseline.original[0]), static_cast<std::uint8_t>(state.baseline.original[3])) ||
                    !FlightCollision(context, state.baseline.original[2] != 0) ||
                    !MovementValueClose(Read<float>(movement + 0x294), state.baseline.original[1]))
                    return {RequestState::Indeterminate, "飞行关闭尚未全部恢复，保留原值记录"};
                state = {};
                return {RequestState::Verified, "已确认胶囊空间无阻挡，并恢复原移动模式、碰撞和飞行速度"};
            }
            state.uncertain = true;
            if (!FlightCollision(context, false) || !WriteMovementFloat(movement + 0x294, speed) ||
                !FlightMode(context, 5, 0) || !MovementValueClose(Read<float>(movement + 0x294), speed))
                return {RequestState::Indeterminate, "飞行启用或调速未全部通过回读；保留恢复记录，可关闭补偿"};
            state.enabled = true;
            state.uncertain = false;
            state.speed = speed;
            return {RequestState::Verified, "飞行模式、禁用碰撞和飞行速度均已回读；水平移动沿用游戏输入"};
        }
        ExecutionResult FlightStep(const Context& context, const Request& request) {
            const auto id = request.arguments.empty() ? std::string{} : request.arguments[0];
            const auto* definition = FindFeature(id);
            std::string reason;
            if (request.arguments.size() != 2 || !definition ||
                !ValidateParameters(*definition, {request.arguments[1]}, reason) || !FlightContract(context, reason) ||
                !flightOverride.enabled || flightOverride.uncertain || !FlightIdentityCurrent(context))
                return {RequestState::Failed, reason.empty() ? "请先启用并核对飞行状态" : reason};
            const auto before = FlightLocation(context);
            double distance{};
            const auto& text = request.arguments[1];
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), distance);
            if (!before || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(distance))
                return {RequestState::Failed, "飞行位置或移动距离无法读取"};
            if (id == "flight_down") distance = -distance;
            alignas(16) std::array<std::byte, 304> parameters{};
            const MovementVector delta{0, 0, distance};
            std::memcpy(parameters.data(), &delta, sizeof(delta));
            parameters[0x120] = std::byte{1};
            if (!CallProcessEvent(context.pawn, FindFunction(Read<Address>(context.pawn + 0x10), "K2_AddActorWorldOffset"), parameters.data()))
                return {RequestState::Indeterminate, "飞行位置调用异常，未重试"};
            ++dispatchedCalls;
            const auto after = FlightLocation(context);
            if (!after || !FlightIdentityCurrent(context)) return {RequestState::Indeterminate, "移动后玩家或坐标已变化，未重试"};
            if (std::abs(after->z - before->z - distance) <= 0.01 &&
                std::abs(after->x - before->x) <= 0.01 && std::abs(after->y - before->y) <= 0.01)
                return {RequestState::Verified, "飞行高度变化已从胶囊世界坐标回读"};
            return {RequestState::Dispatched, "飞行位移调用已返回，但实际坐标变化未匹配请求；未重试"};
        }

        Address MovementCamera(const Context& context) {
            if (!MovementContextCurrent(context) ||
                !PropertyLayout(FindProperty(Read<Address>(context.controller + 0x10), "PlayerCameraManager"),
                    "ObjectProperty", 8, 0x368)) return 0;
            const auto camera = Read<Address>(context.controller + 0x368);
            return ObjectValid(camera) && ClassNamed(Read<Address>(camera + 0x10), "PBZPlayerCameraManager") &&
                Read<Address>(camera) == base + 0xc036f20 ? camera : 0;
        }
        bool FovContract(const Context& context) {
            const auto camera = MovementCamera(context);
            if (!camera || !PropertyLayout(FindProperty(Read<Address>(camera + 0x10), "DefaultFOV"), "FloatProperty", 4, 0x2c8)) return false;
            const auto vtable = Read<Address>(camera);
            constexpr std::array<unsigned char, 9> setter{0xf3, 0x0f, 0x11, 0x89, 0xcc, 0x02, 0x00, 0x00, 0xc3};
            constexpr std::array<unsigned char, 11> unlock{0xc7, 0x81, 0xcc, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3};
            std::array<unsigned char, setter.size()> observedSetter{};
            std::array<unsigned char, unlock.size()> observedUnlock{};
            const auto locked = Read<float>(camera + 0x2cc);
            return Read<Address>(vtable + 0x800) == base + 0x4372da0 &&
                Read<Address>(vtable + 0x808) == base + 0x4384680 && Read<Address>(vtable + 0x810) == base + 0x438ab90 &&
                ReadBytes(base + 0x4384680, observedSetter.data(), observedSetter.size()) && observedSetter == setter &&
                ReadBytes(base + 0x438ab90, observedUnlock.data(), observedUnlock.size()) && observedUnlock == unlock &&
                std::isfinite(locked) && locked >= 0 && locked <= 360 &&
                CombatSignature(FindFunction(Read<Address>(camera + 0x10), "GetFOVAngle"), "GetFOVAngle", 0x4393670, 0x54020402, 4, {
                    {"ReturnValue", "FloatProperty", 0, 4, 0x18001040000780ull, ""}});
        }
        std::optional<float> ReadMovementFov(const Context& context) {
            const auto camera = MovementCamera(context);
            if (!camera || !FovContract(context)) return {};
            float actual = std::numeric_limits<float>::quiet_NaN();
            if (!CallProcessEvent(camera, FindFunction(Read<Address>(camera + 0x10), "GetFOVAngle"), &actual) ||
                !MovementContextCurrent(context) || MovementCamera(context) != camera || !std::isfinite(actual) || actual <= 0 || actual > 360) return {};
            return actual;
        }
        bool WriteMovementFovLeaf(Address camera, float value, bool unlock) noexcept {
            __try {
                if (unlock) reinterpret_cast<void(__fastcall*)(void*)>(base + 0x438ab90)(reinterpret_cast<void*>(camera));
                else reinterpret_cast<void(__fastcall*)(void*, float)>(base + 0x4384680)(reinterpret_cast<void*>(camera), value);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        ExecutionResult SetMovementFov(const Context& context, bool enabled, float value) {
            auto& state = fovOverride;
            const auto camera = MovementCamera(context);
            const auto beforeFov = ReadMovementFov(context);
            if (!camera || !beforeFov) return {RequestState::Failed, "实际相机视野或入口契约不可读，未执行"};
            const auto identity = MovementObjectIdentity(context, camera);
            const float beforeLock = Read<float>(camera + 0x2cc);
            const float baseline[]{beforeLock, *beforeFov};
            if (!enabled && !state.baseline.captured) return {RequestState::Verified, "当前相机未启用视野修改"};
            if (enabled && (value < 30 || value > 150 || !std::isfinite(value))) return {RequestState::Failed, "视野范围必须为30至150"};
            const bool firstCapture = !state.baseline.captured;
            if ((enabled && !state.baseline.Capture(identity, baseline, 2)) || (!enabled && !state.baseline.CanRestore(identity)))
                return {RequestState::Failed, "相机身份变化，未写入旧相机原值"};
            if (enabled && firstCapture) { state.context = context; state.contextPawnIndex = Read<std::uint32_t>(context.pawn + 0xc); }
            const auto desiredLock = enabled ? value : state.baseline.original[0];
            state.uncertain = true;
            if (!MovementContextCurrent(context) || MovementCamera(context) != camera || !FovContract(context) ||
                !WriteMovementFovLeaf(camera, desiredLock, !enabled && desiredLock <= 0))
                return {RequestState::Indeterminate, "相机视野调用未完成，保留恢复记录"};
            ++dispatchedCalls;
            const auto actual = ReadMovementFov(context);
            const bool matched = actual && state.baseline.CanRestore(MovementObjectIdentity(context, MovementCamera(context))) &&
                MovementValueClose(Read<float>(camera + 0x2cc), desiredLock) && (desiredLock <= 0 || MovementValueClose(*actual, desiredLock));
            if (!matched) {
                if (MovementContextCurrent(context) && MovementCamera(context) == camera && FovContract(context) &&
                    WriteMovementFovLeaf(camera, beforeLock, beforeLock <= 0)) {
                    const auto rolledBack = ReadMovementFov(context);
                    state.uncertain = !rolledBack || !MovementValueClose(Read<float>(camera + 0x2cc), beforeLock) ||
                        (beforeLock > 0 && !MovementValueClose(*rolledBack, beforeLock));
                }
                return {state.uncertain ? RequestState::Indeterminate : RequestState::Failed, "视野调用回读未匹配，已尝试恢复编辑前相机状态"};
            }
            state.enabled = enabled;
            state.uncertain = false;
            state.parameter = value;
            state.target[0] = value;
            state.error.clear();
            if (!enabled) state.baseline.Clear();
            return {RequestState::Verified, enabled ? "实际视野角度已通过游戏相机接口回读" : "已恢复原相机锁定视野或解除本修改器视野锁定"};
        }

        Address AiMovementPawnClass(const Context& context) {
            auto type = Read<Address>(context.pawn + 0x10);
            for (unsigned depth = 0; type && depth < 64; ++depth) {
                if (ObjectValid(type) && ObjectName(type) == "Pawn") return type;
                const auto parent = Read<Address>(type + 0x40);
                if (parent == type) break;
                type = parent;
            }
            return 0;
        }
        bool AiMovementContract(const Context& context) {
            if (!MovementContextCurrent(context) || !AiMovementPawnClass(context) || !AttributeEntriesValid()) return false;
            const auto owner = Read<Address>(base + 0xdf45bf8);
            const auto function = FindFunction(owner, "GetAllActorsOfClass");
            if (!ObjectValid(owner) || ObjectName(owner) != "GameplayStatics" || !ObjectValid(Read<Address>(owner + 0x160)) ||
                !CombatSignature(function, "GetAllActorsOfClass", 0x3f96270, 0x04422403, 32, {
                    {"WorldContextObject", "ObjectProperty", 0, 8, 0x18001000000282ull, "Object"},
                    {"ActorClass", "ClassProperty", 8, 8, 0x1c001000000280ull, "Class"},
                    {"OutActors", "ArrayProperty", 16, 16, 0x10000000000380ull, ""}})) return false;
            const auto array = FindProperty(function, "OutActors"), inner = Read<Address>(array + 0x78);
            return ObjectName(Read<Address>(FindProperty(function, "ActorClass") + 0x78)) == "Actor" &&
                inner && Read<std::int32_t>(inner + 0x34) == 8 &&
                Name(Read<std::uint32_t>(Read<Address>(inner + 8) + 0x30)) == "ObjectProperty" &&
                ObjectName(Read<Address>(inner + 0x70)) == "Actor";
        }
        bool AiMovementPawn(const Context& context, Address pawn, Address& controller) {
            if (!MovementContextCurrent(context) || !ObjectValid(pawn) || pawn == context.pawn ||
                !ClassNamed(Read<Address>(pawn + 0x10), "Pawn")) return false;
            const auto type = Read<Address>(pawn + 0x10);
            if (!PropertyLayout(FindProperty(type, "Controller"), "ObjectProperty", 8, 0x2e0) ||
                !PropertyLayout(FindProperty(type, "CustomTimeDilation"), "FloatProperty", 4, 0x68)) return false;
            controller = Read<Address>(pawn + 0x2e0);
            const bool valid = ObjectValid(controller) &&
                PropertyLayout(FindProperty(Read<Address>(controller + 0x10), "Pawn"), "ObjectProperty", 8, 0x2f0);
            return IsOwnedAiPawn({pawn, controller, valid ? Read<Address>(controller + 0x2f0) : 0,
                Read<Address>(pawn + 0x2e0), valid && ClassNamed(Read<Address>(controller + 0x10), "AIController"),
                pawn == context.pawn, valid});
        }
        bool AiMovementIdentityCurrent(const Context& context, const AiMovementEntry& entry) {
            Address controller{};
            return context.world == entry.identity.world && context.worldIndex == entry.identity.worldIndex &&
                AiMovementPawn(context, entry.identity.pawn, controller) && controller == entry.controller &&
                Read<std::uint32_t>(controller + 0xc) == entry.controllerIndex &&
                Read<std::uint32_t>(entry.identity.pawn + 0xc) == entry.identity.pawnIndex;
        }
        bool AiMovementRestoreIdentity(const Context& context, const AiMovementEntry& entry) {
            // The controller may unpossess a still-live pawn. It is safe to
            // restore that pawn's own baseline, never apply another AI update.
            return MovementContextCurrent(context) && ObjectValid(entry.identity.world) &&
                Read<std::uint32_t>(entry.identity.world + 0xc) == entry.identity.worldIndex && ObjectValid(entry.identity.pawn) &&
                Read<std::uint32_t>(entry.identity.pawn + 0xc) == entry.identity.pawnIndex &&
                PropertyLayout(FindProperty(Read<Address>(entry.identity.pawn + 0x10), "CustomTimeDilation"), "FloatProperty", 4, 0x68);
        }
        bool AiMovementWrite(const Context& context, const AiMovementEntry& entry, float value, bool restoring) {
            if (!(restoring ? AiMovementRestoreIdentity(context, entry) : AiMovementIdentityCurrent(context, entry)) ||
                !std::isfinite(value) || value < 0.0001f || value > 100 ||
                !WriteMovementFloat(entry.identity.pawn + 0x68, value)) return false;
            return (restoring ? AiMovementRestoreIdentity(context, entry) : AiMovementIdentityCurrent(context, entry)) &&
                MovementValueClose(Read<float>(entry.identity.pawn + 0x68), value);
        }
        bool FreeMovementEngineArray(Address allocation) noexcept {
            if (!allocation) return true;
            __try { reinterpret_cast<void(__fastcall*)(void*)>(base + 0x12e1930)(reinterpret_cast<void*>(allocation)); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        std::optional<std::vector<Address>> AiMovementPawns(const Context& context) {
            if (!AiMovementContract(context)) return {};
            const auto owner = Read<Address>(base + 0xdf45bf8);
            struct Parameters { Address world, type, data{}; std::int32_t count{}, capacity{}; }
                parameters{context.world, AiMovementPawnClass(context)};
            const bool called = CallProcessEvent(Read<Address>(owner + 0x160), FindFunction(owner, "GetAllActorsOfClass"), &parameters);
            const auto release = [&]() { return FreeMovementEngineArray(parameters.data); };
            if (!called || !MovementContextCurrent(context) || parameters.count < 0 || parameters.count > 8192 ||
                parameters.capacity < parameters.count || parameters.capacity > 16384 ||
                (parameters.count && !parameters.data)) { release(); return {}; }
            std::vector<Address> pawns(parameters.count);
            const bool read = pawns.empty() || ReadBytes(parameters.data, pawns.data(), pawns.size() * sizeof(Address));
            if (!release() || !read) return {};
            return pawns;
        }
        bool AiMovementCleanup(const Context& context) {
            if (!MovementContextCurrent(context)) return false;
            auto& state = aiMovementOverride;
            bool complete = true;
            for (auto it = state.entries.begin(); it != state.entries.end();) {
                if (!AiMovementRestoreIdentity(context, *it)) {
                    if (ObjectValid(it->identity.world) && Read<std::uint32_t>(it->identity.world + 0xc) == it->identity.worldIndex &&
                        ObjectValid(it->identity.pawn) && Read<std::uint32_t>(it->identity.pawn + 0xc) == it->identity.pawnIndex) {
                        complete = false;
                        ++it;
                        continue;
                    }
                    it = state.entries.erase(it);
                    continue;
                }
                if (AiMovementWrite(context, *it, it->baseline, true)) it = state.entries.erase(it);
                else { complete = false; ++it; }
            }
            if (complete) state = {};
            else { state.enabled = false; state.uncertain = true; state.error = "部分 AI 原时间倍率未能确认恢复"; }
            return complete;
        }
        ExecutionResult SetAiMovement(const Context& context, bool enabled, float value) {
            if (!enabled) return AiMovementCleanup(context) ? ExecutionResult{RequestState::Verified, "已恢复仍有效 AI 的原时间倍率"} :
                ExecutionResult{RequestState::Indeterminate, "部分 AI 时间倍率恢复未通过回读"};
            if (!AiMovementContract(context) || !std::isfinite(value) || value < 0.1f || value > 5)
                return {RequestState::Failed, "AI 枚举契约或倍率无效，未执行"};
            const auto pawns = AiMovementPawns(context);
            if (!pawns) return {RequestState::Failed, "当前世界 AI 枚举未完成，未执行"};
            auto& state = aiMovementOverride;
            const auto originalEntries = state.entries;
            struct Change { AiMovementEntry entry; float previous; };
            std::vector<Change> changed;
            std::vector<AiMovementEntry> next;
            bool success = true;
            for (const auto pawn : *pawns) {
                Address controller{};
                if (!AiMovementPawn(context, pawn, controller)) continue;
                const auto index = Read<std::uint32_t>(pawn + 0xc);
                const auto found = std::find_if(state.entries.begin(), state.entries.end(), [&](const auto& entry) {
                    return entry.identity.pawn == pawn && entry.identity.pawnIndex == index;
                });
                const auto before = Read<float>(pawn + 0x68);
                if (!std::isfinite(before) || before < 0.0001f || before > 20) { success = false; break; }
                AiMovementEntry entry = found == state.entries.end() ? AiMovementEntry{
                    {context.world, pawn, pawn, context.worldIndex, index, index}, controller,
                    Read<std::uint32_t>(controller + 0xc), before, before} : *found;
                entry.controller = controller;
                entry.controllerIndex = Read<std::uint32_t>(controller + 0xc);
                entry.target = entry.baseline * value;
                if (!std::isfinite(entry.target) || entry.target < 0.0001f || entry.target > 100) { success = false; break; }
                changed.push_back({entry, before});
                if (!AiMovementWrite(context, entry, entry.target, false)) { success = false; break; }
                next.push_back(entry);
            }
            if (!success) {
                bool rolledBack = true;
                for (auto it = changed.rbegin(); it != changed.rend(); ++it)
                    rolledBack &= AiMovementWrite(context, it->entry, it->previous, true);
                state.uncertain = !rolledBack;
                if (!rolledBack) {
                    for (const auto& change : changed) {
                        const bool tracked = std::any_of(state.entries.begin(), state.entries.end(), [&](const auto& entry) {
                            return entry.identity == change.entry.identity;
                        });
                        if (!tracked) state.entries.push_back(change.entry);
                    }
                }
                state.error = "AI 倍率应用未全部回读，已尝试回滚本次修改";
                return {rolledBack ? RequestState::Failed : RequestState::Indeterminate, state.error};
            }
            // Restore pawns which were modified earlier and are no longer AI.
            state.uncertain = false;
            for (const auto& entry : originalEntries) {
                if (std::none_of(next.begin(), next.end(), [&](const auto& item) { return item.identity == entry.identity; }) &&
                    AiMovementRestoreIdentity(context, entry) && !AiMovementWrite(context, entry, entry.baseline, true)) {
                    next.push_back(entry);
                    state.uncertain = true;
                }
            }
            state.entries = std::move(next);
            state.enabled = true;
            state.parameter = value;
            state.nextRefresh = Clock::now() + std::chrono::milliseconds(500);
            if (state.uncertain) return {RequestState::Indeterminate, "AI 所有权变化后部分原时间倍率未恢复"};
            state.error.clear();
            return {state.entries.empty() ? RequestState::Dispatched : RequestState::Verified,
                state.entries.empty() ? "当前世界尚无有效 AI，等待新 AI 出现后回读" : "仅 AIController 双向归属的角色时间倍率已回读"};
        }
        bool AiMovementReadback(const Context& context) {
            const auto& state = aiMovementOverride;
            if (!state.enabled || state.uncertain || state.entries.empty()) return false;
            return std::all_of(state.entries.begin(), state.entries.end(), [&](const auto& entry) {
                return AiMovementIdentityCurrent(context, entry) && MovementValueClose(Read<float>(entry.identity.pawn + 0x68), entry.target);
            });
        }
        void AiMovementTick(const Context& context) {
            auto& state = aiMovementOverride;
            if (!state.enabled || state.uncertain || Clock::now() < state.nextRefresh) return;
            state.nextRefresh = Clock::now() + std::chrono::milliseconds(500);
            const auto result = SetAiMovement(context, true, state.parameter);
            if (result.state == RequestState::Failed || result.state == RequestState::Indeterminate) {
                state.uncertain = true;
                state.error = result.message;
            }
        }
