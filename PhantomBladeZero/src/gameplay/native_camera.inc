// Include after native_movement.inc inside the adapter's anonymous namespace.
// camera_rules.hpp must be included at file scope. All state is game-thread only.
        constexpr std::array<std::string_view, 2> CameraToggleIds{"camera_distance", "camera_height"};
        struct CameraOverride {
            Context context;
            CameraBaseline baseline;
            CameraValues applied;
            float parameter{};
            bool enabled{}, uncertain{};
            std::string error;
        };
        std::array<CameraOverride, CameraToggleIds.size()> cameraOverrides;

        bool CameraFeature(std::string_view id) {
            return std::find(CameraToggleIds.begin(), CameraToggleIds.end(), id) != CameraToggleIds.end();
        }
        CameraOverride* CameraOverrideFor(std::string_view id) {
            for (std::size_t i = 0; i < CameraToggleIds.size(); ++i)
                if (CameraToggleIds[i] == id) return &cameraOverrides[i];
            return nullptr;
        }
        bool CameraThreadReady() { return LocalGameThread() && !stopping.load() && !faulted.load(); }
        bool CameraObjectContracts(const CameraIdentity& identity) {
            const auto cameraType = Read<Address>(identity.camera + 0x10);
            const auto locationType = Read<Address>(identity.location + 0x10);
            return ClassNamed(cameraType, "NBCameraComponent") && ClassNamed(locationType, "ATPCCameraLocationObject") &&
                Read<Address>(identity.camera + 0x20) == identity.pawn &&
                PropertyLayout(FindProperty(cameraType, "LocationObject"), "ObjectProperty", 8, 0x338) &&
                Read<Address>(identity.camera + 0x338) == identity.location &&
                Read<Address>(identity.location + 0x20) == identity.camera &&
                PropertyLayout(FindProperty(locationType, "OwningCamera"), "ObjectProperty", 8, 0x28) &&
                Read<Address>(identity.location + 0x28) == identity.camera;
        }
        TagTargetStatus CameraTargetStatus(const CameraIdentity& identity) {
            // After a failed engine call, even cleanup must preserve the ledger
            // without touching UObject storage again.
            if (!CameraThreadReady()) return TagTargetStatus::Unavailable;
            if (!identity.Valid()) return TagTargetStatus::Expired;
            for (const auto& [object, expected] : std::array<std::pair<Address, std::uint32_t>, 4>{{
                {identity.world, identity.worldIndex}, {identity.pawn, identity.pawnIndex},
                {identity.camera, identity.cameraIndex}, {identity.location, identity.locationIndex}}}) {
                std::uint32_t index{}, flags{};
                if (!ReadBytes(object + 0xc, &index, sizeof(index)) || !ReadBytes(object + 8, &flags, sizeof(flags))) {
                    MEMORY_BASIC_INFORMATION info{};
                    if (VirtualQuery(reinterpret_cast<void*>(object), &info, sizeof(info)) &&
                        (info.State == MEM_FREE || info.State == MEM_RESERVE)) return TagTargetStatus::Expired;
                    return TagTargetStatus::Unavailable;
                }
                if (index != expected || (flags & 0x40018000)) return TagTargetStatus::Expired;
                if (!ObjectValid(object)) return TagTargetStatus::Unavailable;
            }
            return CameraObjectContracts(identity) ? TagTargetStatus::Current : TagTargetStatus::Unavailable;
        }
        std::optional<CameraIdentity> CurrentCameraIdentity(const Context& context) {
            if (!CameraThreadReady()) return {};
            const auto current = CurrentContext();
            if (!current || !current->SameKey(context)) return {};
            const auto pawnType = Read<Address>(context.pawn + 0x10);
            if (!PropertyLayout(FindProperty(pawnType, "NBCamera"), "ObjectProperty", 8, 0xee8) ||
                !PropertyLayout(FindProperty(pawnType, "CineCamera1"), "ObjectProperty", 8, 0xf20)) return {};
            const auto camera = Read<Address>(context.pawn + 0xee8), cine = Read<Address>(context.pawn + 0xf20);
            if (!ObjectValid(camera) || !ObjectValid(cine) || Read<Address>(cine + 0x20) != context.pawn ||
                !PropertyLayout(FindProperty(Read<Address>(cine + 0x10), "AttachParent"), "ObjectProperty", 8, 0xd0) ||
                Read<Address>(cine + 0xd0) != camera) return {};
            const auto manager = Read<Address>(context.controller + 0x368);
            if (!PropertyLayout(FindProperty(Read<Address>(context.controller + 0x10), "PlayerCameraManager"), "ObjectProperty", 8, 0x368) ||
                !ObjectValid(manager) || !ClassNamed(Read<Address>(manager + 0x10), "PBZPlayerCameraManager") ||
                !PropertyLayout(FindProperty(Read<Address>(manager + 0x10), "ViewTarget"), "StructProperty", 2592, 0x340) ||
                Read<Address>(manager + 0x340) != context.pawn) return {};
            const auto location = Read<Address>(camera + 0x338);
            CameraIdentity identity{context.world, context.pawn, camera, location, context.worldIndex,
                Read<std::uint32_t>(context.pawn + 0xc), Read<std::uint32_t>(camera + 0xc), Read<std::uint32_t>(location + 0xc)};
            return CameraTargetStatus(identity) == TagTargetStatus::Current ? std::optional(identity) : std::nullopt;
        }
        template<std::size_t N> bool CameraBodyMatches(Address rva, const std::array<unsigned char, N>& expected) {
            std::array<unsigned char, N> observed{};
            return ReadBytes(base + rva, observed.data(), observed.size()) && observed == expected;
        }
        bool CameraBodiesValid() {
            constexpr std::array<unsigned char, 25> distanceGet{0x48,0x8b,0x81,0x38,0x03,0x00,0x00,0x48,0x85,0xc0,0x74,0x09,0xf3,0x0f,0x10,0x80,0xfc,0x01,0x00,0x00,0xc3,0x0f,0x57,0xc0,0xc3};
            constexpr std::array<unsigned char, 9> targetGet{0xf3,0x0f,0x10,0x81,0xf8,0x01,0x00,0x00,0xc3};
            constexpr std::array<unsigned char, 17> distanceRoute{0x48,0x8b,0x89,0x38,0x03,0x00,0x00,0x48,0x85,0xc9,0x0f,0x85,0x10,0x00,0x00,0x00,0xc3};
            constexpr std::array<unsigned char, 106> distanceSet{
                0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xf9,0x0f,0x29,0x74,0x24,0x20,0x48,0x8b,0x49,0x28,0x41,0x0f,0xb6,0xd8,0x0f,0x28,0xf1,0xe8,0xae,0x42,0xff,0xff,
                0xf3,0x0f,0x10,0x80,0x94,0x00,0x00,0x00,0xf3,0x0f,0x5d,0xc6,0xf3,0x0f,0x5f,0x80,0x90,0x00,0x00,0x00,0xf3,0x0f,0x11,0x87,0xf8,0x01,0x00,0x00,0x84,0xdb,0x75,0x18,
                0xf3,0x0f,0x58,0x87,0x04,0x02,0x00,0x00,0xf3,0x0f,0x11,0x87,0xfc,0x01,0x00,0x00,0xf3,0x0f,0x11,0x87,0x00,0x02,0x00,0x00,0x48,0x8b,0x5c,0x24,0x40,0x0f,0x28,0x74,0x24,0x20,0x48,0x83,0xc4,0x30,0x5f,0xc3};
            constexpr std::array<unsigned char, 104> heightSet{
                0x48,0x83,0xec,0x18,0x48,0x8b,0x81,0x38,0x03,0x00,0x00,0x48,0x85,0xc0,0x74,0x53,0x0f,0x10,0x02,0xc6,0x44,0x24,0x04,0x00,0x48,0xc7,0x44,0x24,0x08,0x00,0x00,0x00,0x00,
                0x0f,0x11,0x80,0xb0,0x01,0x00,0x00,0xf2,0x0f,0x10,0x4a,0x10,0x0f,0x10,0x04,0x24,0xf2,0x0f,0x11,0x88,0xc0,0x01,0x00,0x00,0xf3,0x0f,0x10,0xc2,0x0f,0x11,0x80,0xc8,0x01,0x00,0x00,
                0x0f,0x57,0xc0,0x0f,0x2f,0xd0,0x77,0x17,0x0f,0x10,0x02,0x0f,0x11,0x80,0x98,0x01,0x00,0x00,0xf2,0x0f,0x10,0x4a,0x10,0xf2,0x0f,0x11,0x88,0xa8,0x01,0x00,0x00,0x48,0x83,0xc4,0x18,0xc3};
            return CameraBodyMatches(0x5f43ae0, distanceGet) && CameraBodyMatches(0x5f43f70, targetGet) &&
                CameraBodyMatches(0x5f4fc90, distanceRoute) && CameraBodyMatches(0x5f4fcb0, distanceSet) && CameraBodyMatches(0x5f4fbd0, heightSet);
        }
        bool CameraContracts(const CameraIdentity& identity, bool force = false) {
            struct Cache { CameraIdentity identity; Clock::time_point next; bool ready{}; };
            static Cache cache;
            const auto now = Clock::now();
            if (!force && cache.identity == identity && now < cache.next) return cache.ready;
            const auto type = Read<Address>(identity.camera + 0x10);
            const bool ready = CameraBodiesValid() &&
                CombatSignature(FindFunction(type, "SetCameraDistance"), "SetCameraDistance", 0x5f351c0, 0x04020401, 5, {
                    {"NewDistance", "FloatProperty", 0, 4, 0x18001040000280ull, ""},
                    {"bInterpolate", "BoolProperty", 4, 1, 0x18001040000280ull, ""}}) &&
                CombatSignature(FindFunction(type, "GetCameraDistance"), "GetCameraDistance", 0x5f341f0, 0x54020401, 4, {
                    {"ReturnValue", "FloatProperty", 0, 4, 0x18001040000780ull, ""}}) &&
                CombatSignature(FindFunction(type, "GetCurrentCameraMode"), "GetCurrentCameraMode", 0x5f34400, 0x14020401, 8, {
                    {"ReturnValue", "ObjectProperty", 0, 8, 0x18001000000780ull, "ATPCCameraModeDataAsset"}}) &&
                CombatSignature(FindFunction(type, "SetAdditiveTargetOffset"), "SetAdditiveTargetOffset", 0x5f350e0, 0x04c20401, 28, {
                    {"Offset", "StructProperty", 0, 24, 0x18001048000382ull, "Vector"},
                    {"InterpSpeed", "FloatProperty", 24, 4, 0x18001040000280ull, ""}});
            cache = {identity, now + std::chrono::seconds(1), ready};
            return ready;
        }
        std::optional<CameraValues> ReadCameraValues(const CameraIdentity& identity) {
            if (CameraTargetStatus(identity) != TagTargetStatus::Current) return {};
            CameraValues values;
            if (!ReadBytes(identity.location + 0x1f8, &values.distance, sizeof(float)) ||
                !ReadBytes(identity.location + 0x1fc, &values.actualDistance, sizeof(float)) ||
                !ReadBytes(identity.location + 0x204, &values.additiveDistance, sizeof(float)) ||
                !ReadBytes(identity.location + 0x198, &values.height, sizeof(CameraVector)) ||
                !ReadBytes(identity.location + 0x1b0, &values.heightTarget, sizeof(CameraVector)) ||
                !ReadBytes(identity.location + 0x1c8, &values.heightInterpSpeed, sizeof(float)) || !values.Valid()) return {};
            return values;
        }
        bool CameraInvoke(const CameraIdentity& identity, std::string_view name, void* parameters) {
            if (CameraTargetStatus(identity) != TagTargetStatus::Current || !CameraContracts(identity, true)) return false;
            const auto function = FindFunction(Read<Address>(identity.camera + 0x10), name);
            if (!function) return false;
            const bool called = CallProcessEvent(identity.camera, function, parameters);
            ++dispatchedCalls;
            if (!called) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "相机调用异常，适配器停止；保留原值记录且不再调用引擎");
            }
            return called;
        }
        std::optional<std::pair<float, float>> CameraDistanceLimits(const CameraIdentity& identity) {
            Address asset{};
            if (!CameraInvoke(identity, "GetCurrentCameraMode", &asset) ||
                CameraTargetStatus(identity) != TagTargetStatus::Current || !ObjectValid(asset) ||
                !ClassNamed(Read<Address>(asset + 0x10), "ATPCCameraModeDataAsset")) return {};
            const auto settings = FindProperty(Read<Address>(asset + 0x10), "CameraModeSettings");
            if (!PropertyLayout(settings, "StructProperty", 880, 0x38) ||
                ObjectName(Read<Address>(settings + 0x70)) != "ATPCCameraMode") return {};
            const auto location = FindProperty(Read<Address>(settings + 0x70), "LocationSettings");
            if (!PropertyLayout(location, "StructProperty", 248, 8) ||
                ObjectName(Read<Address>(location + 0x70)) != "ATPCLocationSettings") return {};
            const auto type = Read<Address>(location + 0x70);
            if (!PropertyLayout(FindProperty(type, "MinCameraDistance"), "FloatProperty", 4, 0x50) ||
                !PropertyLayout(FindProperty(type, "MaxCameraDistance"), "FloatProperty", 4, 0x54)) return {};
            const auto minimum = Read<float>(asset + 0x90), maximum = Read<float>(asset + 0x94);
            if (!CameraDistanceAllowed(minimum, minimum, maximum)) return {};
            return std::pair(minimum, maximum);
        }
        bool CameraCapability(const Context& context, std::string_view id, std::string& reason) {
            reason.clear();
            if (!CameraFeature(id)) { reason = "相机参数不存在"; return false; }
            const auto identity = CurrentCameraIdentity(context);
            if (!identity) { reason = "等待本地玩家的实际 NBCamera 镜头，过场期间不接受相机编辑"; return false; }
            const auto values = CameraContracts(*identity) ? ReadCameraValues(*identity) : std::nullopt;
            if (!values) { reason = "相机对象、函数参数或实际数值契约未通过"; return false; }
            if (id == "camera_height" && !CameraVectorClose(values->height, values->heightTarget)) {
                reason = "游戏高度偏移仍在过渡，等待稳定后再编辑"; return false;
            }
            return true;
        }
        ExecutionResult ApplyCameraOverride(CameraOverride& state, std::string_view id, bool enabled, float value) {
            const auto identity = state.baseline.identity;
            const auto before = ReadCameraValues(identity);
            if (!before || !CameraContracts(identity, true)) return {RequestState::Failed, "相机对象或原值契约变化，保留恢复记录"};
            const auto desiredDistance = enabled ? value : state.baseline.original.distance;
            CameraVector desiredHeight{};
            if (id == "camera_distance") {
                const auto limits = CameraDistanceLimits(identity);
                if (!limits) return {faulted.load() ? RequestState::Indeterminate : RequestState::Failed, "无法核对当前镜头允许的距离，保留原值记录"};
                if (!CameraDistanceAllowed(desiredDistance, limits->first, limits->second))
                    return {RequestState::Failed, std::format("当前镜头距离范围 {:.0f} 至 {:.0f}，未写入；原值记录保留", limits->first, limits->second)};
            } else {
                const auto target = CameraHeightTarget(state.baseline.original.heightTarget, enabled ? value : 0);
                if (!target) return {RequestState::Failed, "相机高度偏移超出有效范围，未执行"};
                desiredHeight = *target;
            }
            state.uncertain = true;
            if (id == "camera_distance") {
                struct Parameters { float distance; std::uint8_t interpolate{}; } parameters{desiredDistance};
                if (!CameraInvoke(identity, "SetCameraDistance", &parameters))
                    return {RequestState::Indeterminate, "相机距离调用未完成，保留原值记录"};
            } else {
                struct Parameters { CameraVector offset; float speed{}; } parameters{desiredHeight};
                if (!CameraInvoke(identity, "SetAdditiveTargetOffset", &parameters))
                    return {RequestState::Indeterminate, "相机高度调用未完成，保留原值记录"};
                if (!enabled && state.baseline.original.heightInterpSpeed > 0) {
                    parameters.speed = state.baseline.original.heightInterpSpeed;
                    if (!CameraInvoke(identity, "SetAdditiveTargetOffset", &parameters))
                        return {RequestState::Indeterminate, "相机高度插值设置恢复未完成，保留原值记录"};
                }
            }
            const auto after = ReadCameraValues(identity);
            const bool matched = after && (id == "camera_distance" ?
                CameraValueClose(after->distance, desiredDistance) &&
                    CameraValueClose(after->actualDistance, after->distance + after->additiveDistance) :
                CameraVectorClose(after->height, desiredHeight) && CameraVectorClose(after->heightTarget, desiredHeight) &&
                    CameraValueClose(after->heightInterpSpeed, enabled ? 0 : state.baseline.original.heightInterpSpeed));
            if (!matched) return {RequestState::Indeterminate, "相机参数未匹配实际回读，已停止继续调整并保留原值"};
            state.applied = *after; state.enabled = enabled; state.uncertain = false; state.parameter = value; state.error.clear();
            const auto message = !enabled ? "已恢复此相机启用前的参数并通过回读" : id == "camera_distance" ?
                std::format("基础镜头距离回读 {:.1f}，当前合成距离 {:.1f}；遮挡和游戏镜头规则仍然生效", after->distance, after->actualDistance) :
                std::format("镜头高度偏移 {:.1f} 已通过实际回读，保留原有水平偏移", value);
            if (!enabled) state = {};
            return {RequestState::Verified, message};
        }
        ExecutionResult RestoreCameraOverride(CameraOverride& state, std::string_view id) {
            if (!state.baseline.captured) return {RequestState::Verified, "此相机参数未启用"};
            const auto status = CameraTargetStatus(state.baseline.identity);
            if (status == TagTargetStatus::Expired) { state = {}; return {RequestState::Verified, "旧相机对象已回收，未对新对象写入旧原值"}; }
            if (status != TagTargetStatus::Current) return {RequestState::Indeterminate, "原相机暂不可安全访问，保留恢复记录"};
            const auto result = ApplyCameraOverride(state, id, false, 0);
            if (result.state != RequestState::Verified) state.error = result.message;
            return result;
        }
        ExecutionResult ExecuteCamera(const Context& context, const Request& request) {
            if (request.arguments.size() < 2 || !CameraFeature(request.arguments[0])) return {RequestState::Failed, "相机参数无效"};
            const auto& id = request.arguments[0]; const auto& operation = request.arguments[1];
            auto& state = *CameraOverrideFor(id);
            if (operation == "off" && request.arguments.size() == 2) return RestoreCameraOverride(state, id);
            if (operation != "on" || request.arguments.size() != 3) return {RequestState::Failed, "相机开关参数无效"};
            float value{}; const auto& argument = request.arguments[2];
            const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size() || !std::isfinite(value) ||
                (id == "camera_distance" ? value < 50 || value > 2000 : value < -300 || value > 300))
                return {RequestState::Failed, "镜头距离范围为 50 至 2000，高度偏移范围为 -300 至 300"};
            if (state.uncertain) return {RequestState::Indeterminate, "上次相机操作尚未确认，请先关闭以恢复原值"};
            std::string reason;
            if (!CameraCapability(context, id, reason)) return {RequestState::Failed, std::move(reason)};
            const auto identity = CurrentCameraIdentity(context);
            const auto before = identity ? ReadCameraValues(*identity) : std::nullopt;
            const bool firstCapture = !state.baseline.captured;
            if (!before || !state.baseline.Capture(*identity, *before)) return {RequestState::Failed, "相机身份变化或原值不可保存，未执行"};
            state.context = context;
            const auto result = ApplyCameraOverride(state, id, true, value);
            if (result.state != RequestState::Verified) state.error = result.message;
            if (result.state == RequestState::Indeterminate) state.uncertain = true;
            // A rejected preflight did not change the camera. Do not make a new
            // session restore a baseline for an operation that never dispatched.
            if (firstCapture && result.state == RequestState::Failed && !state.uncertain) state = {};
            return result;
        }
        ToggleState CameraToggleState(const Context& context, std::string_view id) {
            const auto* state = CameraOverrideFor(id);
            if (!state) return {std::string(id), false, false, "相机参数不存在"};
            std::string reason;
            bool known = CameraCapability(context, id, reason);
            if (state->uncertain) { known = false; reason = state->error.empty() ? "相机操作尚未通过回读，原值记录保留" : state->error; }
            if (state->enabled && known) {
                const auto identity = CurrentCameraIdentity(context);
                const auto values = identity && state->baseline.CanRestore(*identity) ? ReadCameraValues(*identity) : std::nullopt;
                known = values && (id == "camera_distance" ? CameraValueClose(values->distance, state->applied.distance) :
                    CameraVectorClose(values->heightTarget, state->applied.heightTarget) && CameraVectorClose(values->height, state->applied.height));
                if (!known) reason = "游戏镜头或相机对象已变化，参数尚未匹配";
            }
            if (reason.empty()) reason = "本地相机参数实际回读";
            return {std::string(id), known, known && state->enabled, std::move(reason),
                known && state->enabled ? std::vector<std::string>{std::format("{:.9g}", state->parameter)} : std::vector<std::string>{}};
        }
        std::vector<ToggleState> CameraToggleStates(const Context& context) {
            std::vector<ToggleState> states;
            for (const auto id : CameraToggleIds) states.push_back(CameraToggleState(context, id));
            return states;
        }
        void CameraTick(const Context& context) {
            if (std::none_of(cameraOverrides.begin(), cameraOverrides.end(), [](const auto& state) { return state.enabled && !state.uncertain; })) return;
            static Clock::time_point nextCheck;
            const auto now = Clock::now();
            if (now < nextCheck || !CameraThreadReady()) return;
            nextCheck = now + std::chrono::milliseconds(250);
            const auto identity = CurrentCameraIdentity(context);
            for (std::size_t i = 0; i < cameraOverrides.size(); ++i) {
                auto& state = cameraOverrides[i];
                if (!state.enabled || state.uncertain) continue;
                const auto values = identity && state.baseline.CanRestore(*identity) && CameraContracts(*identity) ? ReadCameraValues(*identity) : std::nullopt;
                const bool matched = values && (i == 0 ? CameraValueClose(values->distance, state.applied.distance) :
                    CameraVectorClose(values->heightTarget, state.applied.heightTarget) && CameraVectorClose(values->height, state.applied.height));
                if (!matched) {
                    state.enabled = false; state.uncertain = true;
                    state.error = "游戏改变了镜头或参数，已停止相机调整；关闭时将尝试恢复原相机参数";
                }
            }
        }
        bool CameraCleanup() {
            bool restored = true;
            for (std::size_t i = 0; i < cameraOverrides.size(); ++i)
                restored &= RestoreCameraOverride(cameraOverrides[i], CameraToggleIds[i]).state == RequestState::Verified;
            return restored;
        }
        bool CameraSessionChanged(const Context&) { return CameraCleanup(); }
