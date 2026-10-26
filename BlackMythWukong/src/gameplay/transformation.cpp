#include "transformation.hpp"
#include "transformation_model.hpp"
#include "transformation_presets.hpp"
#include "transformation_bindings.hpp"
#include "transformation_camera.hpp"
#include "transformation_adapter.hpp"
#include "transformation_native.hpp"
#include "transformation_special.hpp"
#include "engine_bridge.hpp"
#include "../runtime.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <format>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>

namespace gloss::gameplay::transformation
{
    namespace
    {
        using engine::Arg;
        using extensions::Vec3;
        using model::Progress;
        std::mutex mutex;
        std::once_flag initialization;
        Status published;
        std::vector<CharacterEntry> catalog;
        std::vector<SkillEntry> publishedSkills;
        std::map<std::string, std::vector<SkillEntry>> skillCache;
        std::map<std::string, std::vector<SkillSlot>> slotConfig;
        bindings::Config bindingConfig;
        camera::Config cameraConfig;
        float activeCameraMeters{};
        struct Context
        {
            std::uintptr_t player{}, controller{}, world{};
            model::ContextKey key;
        } context;
        enum class RequestKind { Catalog, Inspect, Begin, Restore, Slot, Skill };
        struct Request
        {
            RequestKind kind;
            std::string id;
            std::size_t slot{};
            std::uint64_t time{}, generation{};
            int skillId{};
            bool input{};
        };
        std::optional<Request> request;
        model::Session session;
        std::atomic_bool exclusive{};
        bool catalogRequested{true};
        std::uint64_t lastProbe{};
        Capabilities capabilities;
        std::string message = "等待游戏角色";
        std::string catalogMessage = "参考资料目录；尚未枚举游戏资源";
        bool catalogComplete{};

        std::wstring Wide(std::string_view value)
        {
            const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
            std::wstring result(length, L'\0');
            if (length) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
            return result;
        }
        std::string Utf8(std::wstring_view value)
        {
            const auto length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string result(length, '\0');
            if (length) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
            return result;
        }
        template <std::size_t N>
        bool Call(std::wstring_view path, std::array<Arg, N> &args, std::uintptr_t object = 0)
        {
            const auto result = engine::Call(path, args, object);
            if (!result.success) Log("Transformation: " + result.message);
            return result.success;
        }
        bool Empty(std::wstring_view path, std::uintptr_t object)
        {
            return engine::Call(path, {}, object).success;
        }
        std::uintptr_t Object(std::uintptr_t object, const char *name)
        {
            auto value = Arg::Object(name, 0);
            return engine::Property(object, value) ? value.Get<std::uintptr_t>() : 0;
        }
        bool Finite(Vec3 p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) < 1e8 && std::abs(p.y) < 1e8 && std::abs(p.z) < 1e8;
        }
        bool Location(std::uintptr_t actor, Vec3 &position)
        {
            auto args = std::array{Arg::Object("Actor", actor), Arg::Vector("OutPos", {}, true)};
            if (!Call(L"/Script/UnrealExtent.GSE_ActorFuncLib:GetActorLocationSimple", args)) return false;
            position = args[1].Get<Vec3>();
            return Finite(position);
        }
        bool SetLocation(std::uintptr_t actor, Vec3 position)
        {
            if (!Finite(position)) return false;
            auto args = std::array{Arg::Object("Actor", actor), Arg::Vector("Pos", position), Arg::Boolean("bSweep", false), Arg::Int("iTeleportType", 1)};
            return Call(L"/Script/UnrealExtent.GSE_ActorFuncLib:SetActorLocationSimple", args);
        }
        bool Hidden(std::uintptr_t actor, bool hidden)
        {
            auto args = std::array{Arg::Boolean("bNewHidden", hidden)};
            return Call(L"/Script/Engine.Actor:SetActorHiddenInGame", args, actor);
        }
        bool Collision(std::uintptr_t actor, bool collision)
        {
            auto args = std::array{Arg::Boolean("bNewActorEnableCollision", collision)};
            return Call(L"/Script/Engine.Actor:SetActorEnableCollision", args, actor);
        }
        bool Foreground()
        {
            DWORD process{};
            GetWindowThreadProcessId(GetForegroundWindow(), &process);
            return process == GetCurrentProcessId();
        }
        bool Bounds(std::uintptr_t actor, Vec3 &extent)
        {
            auto args = std::array{Arg::Boolean("bOnlyCollidingComponents", false), Arg::Vector("Origin", {}, true),
                Arg::Vector("BoxExtent", {}, true), Arg::Boolean("bIncludeFromChildActors", false)};
            if (!Call(L"/Script/Engine.Actor:GetActorBounds", args, actor)) return false;
            extent = args[2].Get<Vec3>();
            return Finite(extent) && extent.x >= 0. && extent.y >= 0. && extent.z > 0.;
        }
        bool Face(std::uintptr_t actor, Vec3 direction)
        {
            if (!Finite(direction) || direction.x * direction.x + direction.y * direction.y <= .001) return true;
            constexpr double radiansToDegrees = 57.29577951308232;
            auto args = std::array{Arg::Object("Actor", actor), Arg::Rotation("Rot", {0., std::atan2(direction.y, direction.x) * radiansToDegrees, 0.}), Arg::Int("iTeleportType", 0)};
            return Call(L"/Script/UnrealExtent.GSE_ActorFuncLib:SetActorRotationSimple", args);
        }
        bool Mode(std::uintptr_t movement, std::uint8_t mode, std::uint8_t custom)
        {
            auto args = std::array{Arg::Byte("NewMovementMode", mode), Arg::Byte("NewCustomMode", custom)};
            return Call(L"/Script/Engine.CharacterMovementComponent:SetMovementMode", args, movement);
        }
        bool HasDamageProtection(std::uintptr_t actor, bool &present)
        {
            auto args = std::array{Arg::Object("Unit", actor), Arg::Enum("SimpleState", 1), Arg::Data("ReturnValue", engine::Type::Bool, std::int32_t{}, true)};
            if (!Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUHasUnitSimpleState", args)) return false;
            present = args[2].Get<std::int32_t>() != 0;
            return true;
        }
        engine::CallResult DamageProtection(std::uintptr_t actor, bool remove)
        {
            auto args = std::array{Arg::Object("Unit", actor), Arg::Enum("SimpleState", 1), Arg::Boolean("IsRemove", remove)};
            auto result = engine::Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUSetUnitSimpleState", args);
            if (!result.success) Log("Transformation damage protection: " + result.message);
            return result;
        }
        model::ObjectKey Key(std::uintptr_t object)
        {
            const auto weak = engine::MakeWeak(object);
            return engine::ResolveWeak(weak) == object ? model::ObjectKey{weak.index, weak.serial} : model::ObjectKey{};
        }
        engine::WeakObject Weak(model::ObjectKey key) { return {key.index, key.serial}; }
        std::vector<SkillSlot> EmptySlots()
        {
            std::vector<SkillSlot> slots;
            for (int i = 0; i != 8; ++i) slots.push_back({ActionName(static_cast<Action>(i)), static_cast<Action>(i), {}});
            return slots;
        }
        bool ReadMovement(std::uintptr_t actor, adapter::Movement &out)
        {
            const auto movement = Object(actor, "CharacterMovement");
            if (!movement) return false;
            auto mode = Arg::Byte("MovementMode", 0), custom = Arg::Byte("CustomMovementMode", 0);
            auto land = Arg::Byte("DefaultLandMovementMode", 0), water = Arg::Byte("DefaultWaterMovementMode", 0);
            auto walk = Arg::Float("MaxWalkSpeed", 0), fly = Arg::Float("MaxFlySpeed", 0), swim = Arg::Float("MaxSwimSpeed", 0), gravity = Arg::Float("GravityScale", 0);
            if (!engine::Property(movement, mode) || !engine::Property(movement, custom) || !engine::Property(movement, land) || !engine::Property(movement, water) ||
                !engine::Property(movement, walk) || !engine::Property(movement, fly) || !engine::Property(movement, swim) || !engine::Property(movement, gravity)) return false;
            out = {mode.Get<std::uint8_t>(), custom.Get<std::uint8_t>(), land.Get<std::uint8_t>(), water.Get<std::uint8_t>(),
                walk.Get<float>(), fly.Get<float>(), swim.Get<float>(), gravity.Get<float>()};
            return true;
        }
        bool ReadSkills(std::uintptr_t actor, std::vector<SkillEntry> &out)
        {
            auto skills = std::array{Arg::Object("Unit", actor), Arg::Integers("ReturnValue")};
            if (!Call(L"/Script/b1-Managed.AutoTestHelperLib:GetUnitAllSkillID", skills) || skills[1].bytes.size() % sizeof(int)) return false;
            out.clear();
            for (std::size_t i = 0; i < skills[1].bytes.size(); i += sizeof(int))
            {
                int id{};
                std::memcpy(&id, skills[1].bytes.data() + i, sizeof(id));
                if (id > 0 && std::none_of(out.begin(), out.end(), [&](const auto &s) { return s.id == id; }))
                    out.push_back({id, std::format("技能 {}", id), true, "所属角色技能描述存在；施放条件由游戏判断"});
            }
            return true;
        }
        bool SimpleState(std::uintptr_t actor, std::uint8_t state, bool &present)
        {
            auto args = std::array{Arg::Object("Unit", actor), Arg::Enum("SimpleState", state), Arg::Data("ReturnValue", engine::Type::Bool, std::int32_t{}, true)};
            if (!Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUHasUnitSimpleState", args)) return false;
            present = args[2].Get<std::int32_t>() != 0;
            return true;
        }
        bool Cinematic(std::uintptr_t original, bool &playing)
        {
            auto movie = std::array{Arg::Object("WorldContext", context.world), Arg::Data("ReturnValue", engine::Type::Bool, std::int32_t{}, true)};
            bool inSequence{}, sequenceTransition{};
            if (!Call(L"/Script/b1-Managed.AutoTestHelperLib:IsPlayingCameraMovie", movie) ||
                !SimpleState(original, 56, inSequence) || !SimpleState(original, 171, sequenceTransition)) return false;
            playing = movie[1].Get<std::int32_t>() != 0 || inSequence || sequenceTransition;
            return true;
        }
        bool MovementStates(std::uintptr_t actor, bool &movementSuppressed, bool &inputSuppressed, std::string &reason)
        {
            // These states select ECS-specific movers, independent of CMC mode.
            for (const auto state : {27, 54, 87, 143})
            {
                bool present{};
                if (!SimpleState(actor, static_cast<std::uint8_t>(state), present)) { reason = "无法校验角色专用移动状态"; return false; }
                if (present) { reason = "该形态使用车载、飞行控制、物理或筋斗云专用驱动"; return false; }
            }
            movementSuppressed = inputSuppressed = false;
            for (const auto state : {3, 9, 82})
            {
                bool present{};
                if (!SimpleState(actor, static_cast<std::uint8_t>(state), present)) { reason = "无法读取角色输入限制状态"; return false; }
                movementSuppressed |= present;
            }
            for (const auto state : {6, 12})
            {
                bool present{};
                if (!SimpleState(actor, static_cast<std::uint8_t>(state), present)) { reason = "无法读取角色输入限制状态"; return false; }
                inputSuppressed |= present;
            }
            return true;
        }
        bool IndependentConfig(std::uintptr_t extend, std::string &reason, bool *requiresSpecialFsm = nullptr)
        {
            const auto base = engine::Find(L"/Script/b1-Managed.BUTamerActor");
            const auto baseline = Object(engine::DefaultObject(base), "ExtendConfigComp");
            if (!extend || !baseline) { reason = "无法读取角色隔离配置基线"; return false; }
            for (const auto name : {"PhaseStateGraphTemplate", "BattleSC", "FloatRelativeSpotActor", "BeginTriggerBox", "EndTriggerBox"})
            {
                // ExtendConfig mixes hard, weak and soft references. An unloaded
                // or stale reference still denotes a scene dependency.
                bool assigned{};
                if (!engine::ObjectReferenceAssigned(extend, name, assigned)) { reason = std::format("角色依赖字段 {} 无法校验", name); return false; }
                if (assigned) { reason = std::format("形态依赖 {}；需要专用阶段或场景驱动", name); return false; }
            }
            for (const auto name : {"bEnableFlyCtrl", "bEnableTurretControl", "bEnableMovePhysicsTransform", "bEnablePhysicalMove"})
            {
                auto enabled = Arg::Boolean(name, false);
                if (!engine::Property(extend, enabled)) { reason = std::format("角色控制字段 {} 无法校验", name); return false; }
                if (enabled.Get<std::int32_t>()) { reason = std::format("形态依赖 {}；缺少专用控制或关卡隔离", name); return false; }
            }
            auto phasePath = Arg::String("PhaseStateGraphTemplatePath", {}, true);
            constexpr std::array<std::string_view, 1> tagPath{"BattleSC_GUID"};
            auto tag = Arg::Name("TagName", {}, true);
            if (!engine::Property(extend, phasePath)) { reason = std::format("角色依赖字段 {} 无法校验", "PhaseStateGraphTemplatePath"); return false; }
            if (!phasePath.text.empty()) { reason = std::format("形态依赖 {}；需要专用阶段或场景驱动", "PhaseStateGraphTemplatePath"); return false; }
            if (!engine::PropertyPath(extend, tagPath, tag)) { reason = std::format("角色依赖字段 {} 无法校验", "BattleSC_GUID.TagName"); return false; }
            // FName.None formats as "None", not an empty managed string. Even
            // an unset token cannot prove that the cooperative system is inert.
            // Ready includes every instance-scoped cooperative lifecycle and
            // message hook; do not permit this route with only some installed.
            if (!native::Ready()) { reason = "角色隔离或输入桥接尚未通过当前版本校验"; return false; }
            if (tag.Get<engine::NameValue>().comparisonIndex != 0 || tag.Get<engine::NameValue>().number != 0)
            {
                Log(std::format("Transformation cooperative group isolated for temporary instances: BattleSC_GUID.TagName={} (index={}, number={})", Utf8(tag.text),
                    tag.Get<engine::NameValue>().comparisonIndex, tag.Get<engine::NameValue>().number));
            }
            // This list only selects local cooperative callbacks in OnAttach.
            // Their complete dispatch/lifecycle path is suppressed for owned
            // instances. Export it to check readability, without interpreting
            // nonempty values as empty or changing shared source configuration.
            std::wstring cooperativeEvents;
            if (!engine::ExportPropertyText(extend, "SpecificEventListen_BattleSC", cooperativeEvents))
            { reason = std::format("角色依赖字段 {} 无法校验", "SpecificEventListen_BattleSC"); return false; }
            // Only known-empty container exports qualify. Equal but non-empty
            // shared CDO values must not turn into evidence of isolation.
            for (const auto name : {"OtherFsmTemplates", "OtherFsmTemplatesPath", "FsmSubMachineOverride", "FsmSubMachinePathOverride",
                    "SplineList", "BossBarObserveTamerList"})
            {
                std::wstring actual, empty;
                if (!engine::ExportPropertyText(extend, name, actual) || !engine::ExportPropertyText(baseline, name, empty))
                { reason = std::format("角色依赖 {} 无法与隔离基线核对", name); return false; }
                if (actual != empty || (!actual.empty() && actual != L"()"))
                {
                    const std::string_view field = name;
                    const bool fsm = field.starts_with("OtherFsm") || field.starts_with("FsmSubMachine");
                    if (fsm && requiresSpecialFsm && special::Ready())
                    {
                        // Source references stay unchanged. The dedicated
                        // adapter prevents root/phase graph
                        // construction before BeginPlay and keeps local CBG.
                        *requiresSpecialFsm = true;
                        continue;
                    }
                    reason = std::format("形态包含 {} 依赖或未知序列格式；需要专用适配", name);
                    return false;
                }
            }
            constexpr std::array<std::string_view, 1> wakePath{"WakeUpConfig"};
            for (const auto name : {"WakeUpLinkEnable", "CanTriggerWakeUpLinkWhenDead"})
            {
                // These controls belong to FGSWakeUpConfig, not the owning
                // BUS_ExtendConfigComp. Validate every reflected struct hop.
                auto enabled = Arg::Boolean(name, false);
                const auto field = std::format("WakeUpConfig.{}", name);
                if (!engine::PropertyPath(extend, wakePath, enabled)) { reason = std::format("角色控制字段 {} 无法校验", field); return false; }
                if (enabled.Get<std::int32_t>()) { reason = std::format("形态依赖 {}；缺少专用控制或关卡隔离", field); return false; }
            }
            std::wstring targets;
            if (!engine::ExportPropertyPathText(extend, wakePath, "TargetTamerList", targets) || (!targets.empty() && targets != L"()"))
            { reason = "唤醒配置包含关卡角色关联，或无法校验"; return false; }
            return true;
        }
        std::optional<CharacterEntry> Entry(std::string_view id)
        {
            std::lock_guard lock(mutex);
            const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto &entry) { return entry.id == id; });
            return it == catalog.end() ? std::optional<CharacterEntry>{} : *it;
        }
        void Publish()
        {
            std::lock_guard lock(mutex);
            published.phase = session.State();
            published.activeId = session.Character().id;
            published.capabilities = capabilities;
            published.ready = context.key.Valid() && capabilities.spawn && capabilities.skills && capabilities.movement && capabilities.recovery && capabilities.input && capabilities.isolation;
            published.pending = request.has_value() || (session.State() != Phase::Idle && session.State() != Phase::Active && session.State() != Phase::RecoveryRequired);
            published.message = message;
            published.catalogMessage = catalogMessage;
            published.catalogComplete = catalogComplete;
            published.generation = context.key.generation;
            published.cameraDistanceMeters = session.State() == Phase::Active ? activeCameraMeters : 0.f;
            exclusive = session.Exclusive() || (request && request->kind == RequestKind::Begin);
        }
        bool SaveSlotsFile()
        {
            try
            {
                const auto file = DataDirectory() / L"transformation-slots.cfg";
                std::filesystem::create_directories(file.parent_path());
                const auto temporary = file.wstring() + L".tmp";
                std::ofstream out(temporary, std::ios::trunc);
                if (!out) return false;
                out << "GLOSS_TRANSFORMATION_SLOTS_1\n";
                for (const auto &[id, slots] : slotConfig)
                {
                    out << std::quoted(id) << ' ' << slots.size() << '\n';
                    for (const auto &slot : slots)
                    {
                        out << static_cast<int>(slot.action) << ' ' << std::quoted(slot.name) << ' ' << slot.skills.size();
                        for (const auto skill : slot.skills) out << ' ' << skill;
                        out << '\n';
                    }
                }
                out.close();
                return out.good() && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            }
            catch (...) { return false; }
        }
        void LoadSlotsFile()
        {
            std::ifstream in(DataDirectory() / L"transformation-slots.cfg");
            std::string magic;
            if (!std::getline(in, magic) || magic != "GLOSS_TRANSFORMATION_SLOTS_1") return;
            std::map<std::string, std::vector<SkillSlot>> loaded;
            std::string id;
            std::size_t count{};
            while (in >> std::quoted(id) >> count)
            {
                if (id.empty() || id.size() > 2048 || count > 8 || loaded.size() >= 4096) return;
                std::vector<SkillSlot> slots;
                for (std::size_t i = 0; i < count; ++i)
                {
                    SkillSlot slot;
                    int action{};
                    std::size_t skills{};
                    if (!(in >> action >> std::quoted(slot.name) >> skills) || action < 0 || action >= 8 || slot.name.size() > 128 || skills > 32) return;
                    slot.action = static_cast<Action>(action);
                    for (std::size_t j = 0; j < skills; ++j)
                    {
                        int skill{};
                        if (!(in >> skill) || skill <= 0) return;
                        slot.skills.push_back(skill);
                    }
                    slots.push_back(std::move(slot));
                }
                loaded[id] = std::move(slots);
            }
            if (in.eof()) slotConfig = std::move(loaded);
        }
        bool SaveBindingsFile(const bindings::Config &config)
        {
            std::filesystem::path temporary;
            try
            {
                const auto file = DataDirectory() / L"transformation-bindings.cfg";
                std::filesystem::create_directories(file.parent_path());
                temporary = file.wstring() + L".tmp";
                const auto text = bindings::Serialize(config);
                if (text.size() > bindings::MaximumFileBytes) return false;
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!out) return false;
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                out.close();
                if (out.good() && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
            }
            catch (...) {}
            if (!temporary.empty()) { std::error_code error; std::filesystem::remove(temporary, error); }
            return false;
        }
        void LoadBindingsFile()
        {
            try
            {
                const auto file = DataDirectory() / L"transformation-bindings.cfg";
                if (!std::filesystem::exists(file)) return;
                if (std::filesystem::file_size(file) > bindings::MaximumFileBytes) { Log("Transformation skill configuration exceeds size limit"); return; }
                std::ifstream in(file, std::ios::binary);
                const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                if (in.bad()) { Log("Transformation skill configuration could not be read"); return; }
                if (const auto result = bindings::Parse(text, bindingConfig); !result.success) Log("Transformation skill configuration ignored: " + result.message);
            }
            catch (...) { Log("Transformation skill configuration could not be loaded"); }
        }
        bool SaveCameraFile(const camera::Config &config)
        {
            std::filesystem::path temporary;
            try
            {
                const auto file = DataDirectory() / L"transformation-camera.cfg";
                std::filesystem::create_directories(file.parent_path());
                temporary = file.wstring() + L".tmp";
                const auto text = camera::Serialize(config);
                if (text.size() > camera::MaximumFileBytes) return false;
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!out) return false;
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                out.close();
                if (out.good() && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
            }
            catch (...) {}
            if (!temporary.empty()) { std::error_code error; std::filesystem::remove(temporary, error); }
            return false;
        }
        void LoadCameraFile()
        {
            try
            {
                const auto file = DataDirectory() / L"transformation-camera.cfg";
                if (!std::filesystem::exists(file)) return;
                if (std::filesystem::file_size(file) > camera::MaximumFileBytes) { Log("Transformation camera configuration exceeds size limit"); return; }
                std::ifstream in(file, std::ios::binary);
                const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                if (in.bad()) { Log("Transformation camera configuration could not be read"); return; }
                if (const auto result = camera::Parse(text, cameraConfig); !result.success) Log("Transformation camera configuration ignored: " + result.message);
            }
            catch (...) { Log("Transformation camera configuration could not be loaded"); }
        }
        void RefreshCatalog()
        {
            catalogRequested = false;
            auto registry = std::array{Arg::Interface("ReturnValue")};
            if (!Call(L"/Script/AssetRegistry.AssetRegistryHelpers:GetAssetRegistry", registry))
            {
                catalogComplete = false;
                catalogMessage = "游戏资源注册表不可用；仅显示参考资料，目录覆盖不足";
                return;
            }
            const auto object = registry[0].Get<engine::ScriptInterface>().object;
            if (!object) return;
            auto loading = std::array{Arg::Data("ReturnValue", engine::Type::Bool, std::int32_t{}, true)};
            const bool loadingKnown = Call(L"/Script/AssetRegistry.AssetRegistry:IsLoadingAssets", loading, object);
            auto assets = std::array{Arg::Name("PackagePath", L"/Game/00Main/Design/Units"),
                Arg::Structs("OutAssetData", L"/Script/CoreUObject.AssetData", {Arg::Name("ObjectPath", {}, true), Arg::Name("AssetName", {}, true), Arg::Name("AssetClass", {}, true)}),
                Arg::Boolean("bRecursive", true), Arg::Boolean("bIncludeOnlyOnDiskAssets", false),
                Arg::Data("ReturnValue", engine::Type::Bool, std::int32_t{}, true)};
            if (!Call(L"/Script/AssetRegistry.AssetRegistry:GetAssetsByPath", assets, object) || !assets[4].Get<std::int32_t>())
            {
                catalogComplete = false;
                catalogMessage = "角色资源枚举失败；参考目录不能代表全部角色";
                return;
            }
            std::lock_guard lock(mutex);
            std::size_t found{};
            for (const auto &record : assets[1].records)
            {
                if (record.size() != 3) continue;
                auto path = Utf8(record[0].text), name = Utf8(record[1].text), cls = Utf8(record[2].text);
                auto lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (!lower.starts_with("tamer") && !lower.starts_with("unit_")) continue;
                if (cls != "Blueprint" && cls != "BlueprintGeneratedClass") continue;
                if (!path.ends_with("_C")) path += "_C";
                CharacterEntry entry;
                entry.path = path;
                entry.name = name;
                entry.category = path.find("/NPC/") != std::string::npos ? "NPC" : "其他";
                entry.discovered = true;
                entry.reason = "资源存在；尚未检查配置与可操作性";
                model::MergeCatalog(catalog, std::move(entry));
                ++found;
            }
            // This bounded root may omit procedural/cutscene/player-only forms.
            // A successful Asset Registry query is not evidence of full coverage.
            catalogComplete = false;
            catalogMessage = std::format("已从游戏资源注册表找到 {} 个 Tamer 资源；特殊/程序生成形态覆盖尚未确认{}", found,
                !loadingKnown || loading[0].Get<std::int32_t>() ? "，资源仍可能加载中" : "");
            capabilities.catalog = found != 0;
        }
        struct Prepared
        {
            std::uintptr_t characterClass{};
            engine::WeakObject characterClassIdentity, sourceConfig;
            int resource{}, overrideId{};
            std::vector<SkillEntry> skills;
            adapter::Decision adapter;
            bool requiresSpecialFsm{};
        } prepared;
        bool Prepare(CharacterEntry entry, Prepared &out)
        {
            bool slotsReset{};
            const auto unavailable = [&](std::string reason)
            {
                message = reason;
                std::lock_guard lock(mutex);
                const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto &v) { return v.id == entry.id; });
                if (it != catalog.end()) { it->support = Support::Unavailable; it->reason = std::move(reason); }
                return false;
            };
            const auto cls = engine::Load(Wide(entry.path), L"/Script/CoreUObject.Class");
            const auto cdo = cls ? engine::DefaultObject(cls) : 0;
            if (!cdo) { message = "角色资源加载失败；可在资源就绪后重试"; return false; }
            if (!engine::IsA(cdo, L"/Script/b1-Managed.BUTamerActor"))
                return unavailable("资源不是当前版本的角色 Tamer 配置");
            const auto config = Object(cdo, "ConfigInfoComp");
            auto resource = Arg::Int("ResID", 0), overrideId = Arg::Int("OverrideID", 0);
            constexpr std::array<std::string_view, 1> path{"UnitCDesc"};
            if (!config || !engine::PropertyPath(config, path, resource) || !engine::PropertyPath(config, path, overrideId) || resource.Get<int>() <= 0)
                return unavailable("角色资源 ID 或覆盖配置无法校验");
            out.resource = resource.Get<int>();
            out.overrideId = overrideId.Get<int>();
            out.sourceConfig = engine::MakeWeak(config);
            out.characterClass = Object(cdo, "MonsterClassObj");
            if (!out.characterClass)
            {
                std::wstring softPath;
                if (!engine::SoftClassPath(cdo, "MonsterClass", softPath) || softPath.empty())
                {
                    message = "角色蓝图软引用无法校验";
                    return false;
                }
                out.characterClass = engine::Load(softPath, L"/Script/CoreUObject.Class");
            }
            if (!out.characterClass || !engine::IsA(engine::DefaultObject(out.characterClass), L"/Script/b1-Managed.BGU_CharacterAI"))
                return unavailable("该形态缺少通用 AI 角色初始化接口");
            out.characterClassIdentity = engine::MakeWeak(out.characterClass);
            if (engine::ResolveWeak(out.characterClassIdentity) != out.characterClass) return unavailable("角色蓝图弱引用无法校验");
            if (!ReadSkills(cdo, out.skills))
            {
                message = "当前版本未开放角色技能目录接口";
                return false;
            }
            adapter::Movement movement;
            out.adapter = ReadMovement(engine::DefaultObject(out.characterClass), movement) ? adapter::Evaluate(movement, true) :
                adapter::Decision{false, {}, "无法读取角色完整移动组件配置"};
            std::string isolationReason;
            if (!IndependentConfig(Object(cdo, "ExtendConfigComp"), isolationReason, &out.requiresSpecialFsm) ||
                !IndependentConfig(Object(engine::DefaultObject(out.characterClass), "ExtendConfigComp"), isolationReason, &out.requiresSpecialFsm))
                out.adapter = {false, {}, std::move(isolationReason)};
            {
                std::lock_guard lock(mutex);
                const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto &v) { return v.id == entry.id; });
                if (it != catalog.end())
                {
                    it->resourceId = out.resource;
                    it->overrideId = out.overrideId;
                    it->support = out.skills.empty() ? Support::Unavailable : out.adapter.allowed ? Support::Experimental : Support::Unverified;
                    it->reason = out.skills.empty() ? "该角色未找到有效技能描述，不能启动变身" : out.adapter.allowed ?
                        out.adapter.reason + "；独立代理/手动技能实验模式" : out.adapter.reason;
                }
                skillCache[entry.id] = out.skills;
                publishedSkills = out.skills;
                published.inspectedId = entry.id;
                if (!slotConfig.contains(entry.id))
                {
                    slotConfig[entry.id] = model::StarterSlots(entry.path, out.skills);
                }
                else if (!model::ValidSlots(slotConfig[entry.id], out.skills))
                {
                    // Persisted configuration is untrusted until the current
                    // character's actual skill set has been re-enumerated.
                    slotConfig[entry.id] = EmptySlots();
                    slotsReset = true;
                }
            }
            message = slotsReset ? "旧技能槽包含本角色不可用的 ID；已清空，请重新配置" : out.skills.empty() ? "角色没有可枚举技能；仍可查看资源信息" : "已读取角色配置和实际技能 ID";
            return true;
        }

        struct EngineSession final : model::SessionBackend
        {
            engine::WeakObject player, controller, world, tamer, proxy, arm, target;
            bool baseline{}, modified{}, frozen{}, armChanged{}, ownsImmuneLayer{}, immuneLayerUncertain{}, cinematicExit{};
            bool cinematicPaused{};
            std::uint64_t cinematicEndedAt{};
            struct SuspendedActor
            {
                engine::WeakObject identity;
                bool hidden{}, collision{};
            };
            std::vector<SuspendedActor> suspendedActors;
            adapter::Movement suspendedMovement;
            native::PlayerIsolation playerIsolation;
            float armLength{}, autoArmLength{}, appliedArmLength{};
            Vec3 origin{};
            engine::Transform transform;
            int stage{};
            std::uint64_t sessionStarted{};
            model::Combo combo;
            adapter::LocomotionRequests locomotion;
            native::MotionMatchingSelection movementAnimation;
            int movementAnimationSpeed{-1};
            bool movementAnimationLocked{};
            std::uint64_t nextMovementAnimationAt{};
            std::string characterId, exitReason;
            struct MovementDiagnostics
            {
                static constexpr unsigned MaximumSamples = 24, MaximumStops = 8;
                unsigned samples{}, stops{}, animationChoices{};
                std::uint64_t frame{}, requestFrame{}, nextSampleAt{}, lastLocationAt{};
                bool moving{}, locationKnown{}, accelerationAvailable{}, maxAccelerationAvailable{};
                bool animInstanceAvailable{}, humanoidClassAvailable{}, animBaseClassAvailable{};
                bool linkedGraphAvailable{}, moveClassAvailable{}, animOwnerAvailable{}, monsterClassAvailable{}, advancedClassAvailable{}, settingClassAvailable{};
                Vec3 lastLocation{};
                native::MoveInput axes{};
            } movementDiagnostics;

            void BeginMovementDiagnostics() noexcept
            {
                movementDiagnostics = {};
                try
                {
                    // Exact UMetaPaths/return types are verified in this build's
                    // Engine assembly; optional availability is probed once per
                    // session, never once per gameplay frame.
                    movementDiagnostics.accelerationAvailable = engine::Find(L"/Script/Engine.CharacterMovementComponent:GetCurrentAcceleration") != 0;
                    movementDiagnostics.maxAccelerationAvailable = engine::Find(L"/Script/Engine.CharacterMovementComponent:GetMaxAcceleration") != 0;
                    movementDiagnostics.animInstanceAvailable = engine::Find(L"/Script/Engine.SkeletalMeshComponent:GetAnimInstance") != 0;
                    movementDiagnostics.humanoidClassAvailable = engine::Find(L"/Script/b1-Managed.BUAnimHumanoidCS") != 0;
                    movementDiagnostics.animBaseClassAvailable = engine::Find(L"/Script/b1-Managed.BUAnimInstanceBase") != 0;
                    movementDiagnostics.linkedGraphAvailable = engine::Find(L"/Script/Engine.AnimInstance:GetLinkedAnimGraphInstanceByTag") != 0;
                    movementDiagnostics.moveClassAvailable = engine::Find(L"/Script/b1-Managed.BUAnimHumanoidCS_Move") != 0;
                    movementDiagnostics.animOwnerAvailable = engine::Find(L"/Script/Engine.AnimInstance:GetOwningActor") != 0;
                    movementDiagnostics.monsterClassAvailable = engine::Find(L"/Script/b1-Managed.BUAnimHumanoidCS_MonsterLocomotion") != 0;
                    movementDiagnostics.advancedClassAvailable = engine::Find(L"/Script/b1-Managed.BUAnimHumanoidCS_AdvancedMonsterLocomotion") != 0;
                    movementDiagnostics.settingClassAvailable = engine::Find(L"/Script/b1-Managed.BGWDataAsset_AbpHumanoidSetting") != 0;
                    auto path = std::array{Arg::Object("Object", prepared.characterClass), Arg::String("ReturnValue", {}, true)};
                    const bool pathRead = engine::Call(L"/Script/Engine.KismetSystemLibrary:GetPathName", path).success;
                    Log(std::format("Transformation movement session: id={} session={} generation={} resource={} override={} class=0x{:X} classPathRead={} classPath={} accelerationInterface={} maxAccelerationInterface={} animInstanceInterface={}",
                        characterId, sessionStarted, context.key.generation, prepared.resource, prepared.overrideId, prepared.characterClass,
                        pathRead, pathRead ? Utf8(path[1].text) : "unknown", movementDiagnostics.accelerationAvailable, movementDiagnostics.maxAccelerationAvailable, movementDiagnostics.animInstanceAvailable));
                }
                catch (...) {} // Optional diagnostics must never abort a session.
            }
            void MovementRequestObserved(const Vec3 &position, std::uint64_t now, int requestId) noexcept
            {
                auto &diagnostic = movementDiagnostics;
                diagnostic.requestFrame = diagnostic.frame;
                if (requestId <= 0 || diagnostic.moving) return;
                diagnostic.moving = true;
                diagnostic.locationKnown = true;
                diagnostic.lastLocation = position;
                diagnostic.lastLocationAt = now;
                // A new burst must not bypass the session's one-second rate
                // limit. The first snapshot still waits for a physics frame.
                diagnostic.nextSampleAt = (std::max)(diagnostic.nextSampleAt, now + 50);
            }
            void SampleAnimationDiagnostics(std::uintptr_t actor) noexcept
            {
                try
                {
                    // Exact reflected paths/types are retained in
                    // animation-driver-review.md. No plain ECS fields are read.
                    auto mesh = Arg::Object("Mesh", 0);
                    const bool meshRead = engine::Property(actor, mesh);
                    const auto meshObject = meshRead ? mesh.Get<std::uintptr_t>() : 0;
                    const std::string_view meshState = !meshRead ? "unreadable" : meshObject ? "present" : "null";
                    const std::string_view meshGate = !meshRead ? "unreadable" : meshObject ? "present" : "not-applicable";
                    const auto booleanText = [](std::uintptr_t object, const char *name, std::string_view gate) {
                        if (gate != "present") return std::string(gate);
                        auto value = Arg::Boolean(name, false);
                        return std::string(engine::Property(object, value) ? (value.Get<std::int32_t>() ? "true" : "false") : "unreadable");
                    };
                    const auto pause = booleanText(meshObject, "bPauseAnims", meshGate);
                    auto rate = Arg::Float("GlobalAnimRateScale", 0.f);
                    const bool rateRead = meshGate == "present" && engine::Property(meshObject, rate) && std::isfinite(rate.Get<float>());
                    const auto rateText = rateRead ? std::format("{:.3f}", rate.Get<float>()) : std::string(meshGate == "present" ? "unreadable" : meshGate);
                    auto anim = std::array{Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                    const bool animRead = meshGate == "present" && movementDiagnostics.animInstanceAvailable &&
                        engine::Call(L"/Script/Engine.SkeletalMeshComponent:GetAnimInstance", anim, meshObject).success;
                    const auto animObject = animRead ? anim[0].Get<std::uintptr_t>() : 0;
                    const std::string_view animState = meshGate != "present" ? meshGate : !animRead ? "unreadable" : animObject ? "present" : "null";
                    const auto classGate = [&](bool available, const wchar_t *type) -> std::string_view {
                        if (animState != "present") return animState == "null" ? "not-applicable" : animState;
                        if (!available) return "unreadable";
                        return engine::IsA(animObject, type) ? "present" : "not-applicable";
                    };
                    const auto humanoidGate = classGate(movementDiagnostics.humanoidClassAvailable, L"/Script/b1-Managed.BUAnimHumanoidCS");
                    const auto baseGate = classGate(movementDiagnostics.animBaseClassAvailable, L"/Script/b1-Managed.BUAnimInstanceBase");
                    const auto disableMove = booleanText(animObject, "bDisableABPMove", humanoidGate);
                    auto setting = Arg::Object("ABPSetting", 0);
                    const bool settingRead = baseGate == "present" && engine::Property(animObject, setting);
                    const auto settingObject = settingRead ? setting.Get<std::uintptr_t>() : 0;
                    const std::string_view settingState = baseGate != "present" ? baseGate : !settingRead ? "unreadable" : settingObject ? "present" : "null";
                    const auto movement = Object(actor, "CharacterMovement");
                    const auto follow = booleanText(movement, "bIsInFollowState", movement ? "present" : "unreadable");
                    Log(std::format("Transformation animation sample: id={} session={} sample={}/{} meshState={} mesh=0x{:X} animState={} anim=0x{:X} pauseAnims={} globalAnimRateScale={} disableABPMove={} abpSettingState={} abpSetting=0x{:X} followState={}",
                        characterId, sessionStarted, movementDiagnostics.samples, MovementDiagnostics::MaximumSamples,
                        meshState, meshObject, animState, animObject, pause, rateText, disableMove, settingState, settingObject, follow));
                    // Read the animation graph's public mirrors, rather than
                    // assuming CMC movement proves the ECS animation data ticked.
                    // The Move tag and these reflected types are build-verified.
                    auto mappedSpeed = Arg::Float("MappedSpeed", 0.f);
                    const bool mappedRead = humanoidGate == "present" && engine::Property(animObject, mappedSpeed) && std::isfinite(mappedSpeed.Get<float>());
                    const auto mappedText = mappedRead ? std::format("{:.3f}", mappedSpeed.Get<float>()) : std::string(humanoidGate == "present" ? "unreadable" : humanoidGate);
                    auto linked = std::array{Arg::Name("InTag", L"Move"), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                    const bool linkedRead = humanoidGate == "present" && movementDiagnostics.linkedGraphAvailable &&
                        engine::Call(L"/Script/Engine.AnimInstance:GetLinkedAnimGraphInstanceByTag", linked, animObject).success;
                    const auto moveGraph = linkedRead ? linked[1].Get<std::uintptr_t>() : 0;
                    const std::string_view linkedState = humanoidGate != "present" ? humanoidGate : !linkedRead ? "unreadable" : moveGraph ? "present" : "null";
                    const std::string_view moveGate = linkedState != "present" ? (linkedState == "null" ? "not-applicable" : linkedState) :
                        !movementDiagnostics.moveClassAvailable ? "unreadable" : engine::IsA(moveGraph, L"/Script/b1-Managed.BUAnimHumanoidCS_Move") ? "present" : "not-applicable";
                    auto mode = Arg::Enum("FinalABPMoveMode", 0);
                    const bool modeRead = moveGate == "present" && engine::Property(moveGraph, mode);
                    const auto modeText = modeRead ? std::to_string(mode.Get<std::uint8_t>()) : std::string(moveGate == "present" ? "unreadable" : moveGate);
                    const auto accelerating = booleanText(moveGraph, "IsMoveAccelerating", moveGate);
                    const std::string_view settingGate = settingState != "present" ? (settingState == "null" ? "not-applicable" : settingState) :
                        !movementDiagnostics.settingClassAvailable ? "unreadable" : engine::IsA(settingObject, L"/Script/b1-Managed.BGWDataAsset_AbpHumanoidSetting") ? "present" : "not-applicable";
                    const auto settingModeText = [&](std::string_view section, const char *name) {
                        if (settingGate != "present") return std::string(settingGate);
                        const std::array<std::string_view, 1> path{section};
                        auto value = Arg::Enum(name, 0);
                        return engine::PropertyPath(settingObject, path, value) ? std::to_string(value.Get<std::uint8_t>()) : std::string("unreadable");
                    };
                    const auto ownerText = [&](std::uintptr_t object, std::string_view gate) {
                        if (gate != "present") return std::string(gate);
                        auto args = std::array{Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                        if (!movementDiagnostics.animOwnerAvailable || !engine::Call(L"/Script/Engine.AnimInstance:GetOwningActor", args, object).success) return std::string("unreadable");
                        return std::string(args[0].Get<std::uintptr_t>() == actor ? "true" : "false");
                    };
                    const auto moveOwner = ownerText(moveGraph, moveGate);
                    const auto modeValue = modeRead ? mode.Get<std::uint8_t>() : 0;
                    const bool monsterMode = modeValue == 10, advancedMode = modeValue == 11;
                    auto nodeArgs = std::array{Arg::Name("InTag", monsterMode ? L"MonsterLocomotion" : L"AdvancedMonsterLocomotion"), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                    const bool nodeApplicable = moveGate == "present" && modeRead && (monsterMode || advancedMode);
                    const bool nodeRead = nodeApplicable && engine::Call(L"/Script/Engine.AnimInstance:GetLinkedAnimGraphInstanceByTag", nodeArgs, moveGraph).success;
                    const auto node = nodeRead ? nodeArgs[1].Get<std::uintptr_t>() : 0;
                    const std::string_view nodeState = !nodeApplicable ? "not-applicable" : !nodeRead ? "unreadable" : node ? "present" : "null";
                    const bool nodeClassAvailable = monsterMode ? movementDiagnostics.monsterClassAvailable : movementDiagnostics.advancedClassAvailable;
                    const auto nodeClass = monsterMode ? L"/Script/b1-Managed.BUAnimHumanoidCS_MonsterLocomotion" : L"/Script/b1-Managed.BUAnimHumanoidCS_AdvancedMonsterLocomotion";
                    const std::string_view nodeGate = nodeState != "present" ? (nodeState == "null" ? "not-applicable" : nodeState) :
                        !nodeClassAvailable ? "unreadable" : engine::IsA(node, nodeClass) ? "present" : "not-applicable";
                    const auto floatText = [&](const char *name, bool applicable) {
                        if (!applicable) return std::string("not-applicable");
                        if (nodeGate != "present") return std::string(nodeGate);
                        auto value = Arg::Float(name, 0.f);
                        return engine::Property(node, value) && std::isfinite(value.Get<float>()) ? std::format("{:.3f}", value.Get<float>()) : std::string("unreadable");
                    };
                    const auto gaitText = [&](const char *name) {
                        if (!advancedMode) return std::string("not-applicable");
                        if (nodeGate != "present") return std::string(nodeGate);
                        auto value = Arg::Enum(name, 0);
                        return engine::Property(node, value) ? std::to_string(value.Get<std::uint8_t>()) : std::string("unreadable");
                    };
                    Log(std::format("Transformation locomotion graph sample: id={} session={} sample={}/{} mappedSpeed={} moveGraphState={} moveGraph=0x{:X} moveClassState={} finalMoveMode={} isMoveAccelerating={} moveOwnerMatches={} nodeState={} node=0x{:X} nodeClassState={} nodeOwnerMatches={} idleToMove={} moveStop={} loopAlpha={} nodeMappedSpeed={} gait={} moveGait={} settingClassState={} configuredMoveMode={} spareMoveMode={}",
                        characterId, sessionStarted, movementDiagnostics.samples, MovementDiagnostics::MaximumSamples,
                        mappedText, linkedState, moveGraph, moveGate, modeText, accelerating, moveOwner, nodeState, node, nodeGate, ownerText(node, nodeGate),
                        booleanText(node, "bIdleToMove", monsterMode ? nodeGate : "not-applicable"), booleanText(node, "bMoveStop", monsterMode ? nodeGate : "not-applicable"),
                        floatText("MoveLoopFwdBlendAlpha", monsterMode), floatText("MappedSpeed", advancedMode), gaitText("GaitGroundedState"), gaitText("MoveGaitGroundedState"),
                        settingGate, settingModeText("CommonSetting", "ABPMoveMode"), settingModeText("MotionMatchingSetting", "SpareMoveMode")));
                    if (baseGate == "present" && moveGate == "present")
                    {
                        const auto data = native::InspectAnimationState(actor, animObject, moveGraph);
                        const auto stateText = [](native::AnimationDataState state) -> std::string_view {
                            return state == native::AnimationDataState::Present ? "present" : state == native::AnimationDataState::Missing ? "missing" : "unknown";
                        };
                        Log(std::format("Transformation animation data sample: id={} session={} sample={}/{} available={} current={} settingState={} commonSettingState={} matchingSettingState={} moveCommonState={} cachedConfiguredMode={} cachedSpareMode={} commonFinalMode={}",
                            characterId, sessionStarted, movementDiagnostics.samples, MovementDiagnostics::MaximumSamples,
                            data.available, data.current, stateText(data.setting), stateText(data.commonSetting), stateText(data.motionMatchingSetting), stateText(data.moveCommon),
                            data.configuredMode, data.spareMode, data.finalMode));
                    }
                }
                catch (...) {} // Optional animation observations share the movement sample budget.
            }
            void SampleMovementDiagnostics(std::uintptr_t actor, const Vec3 &position, const adapter::Movement &movementState, int castingSkill) noexcept
            {
                auto &diagnostic = movementDiagnostics;
                const auto now = GetTickCount64();
                if (!diagnostic.moving || diagnostic.samples >= MovementDiagnostics::MaximumSamples ||
                    diagnostic.frame <= diagnostic.requestFrame || now < diagnostic.nextSampleAt) return;
                ++diagnostic.samples;
                diagnostic.nextSampleAt = now + 1000;
                try
                {
                    const auto finite = [](const Vec3 &value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };
                    const auto vectorText = [](const Vec3 &value) { return std::format("({:.3f},{:.3f},{:.3f})", value.x, value.y, value.z); };
                    const auto movement = Object(actor, "CharacterMovement");
                    auto velocity = Arg::Vector("Velocity", {});
                    const bool velocityRead = movement && engine::Property(movement, velocity) && finite(velocity.Get<Vec3>());
                    auto acceleration = std::array{Arg::Vector("ReturnValue", {}, true)};
                    const bool accelerationRead = movement && diagnostic.accelerationAvailable &&
                        engine::Call(L"/Script/Engine.CharacterMovementComponent:GetCurrentAcceleration", acceleration, movement).success && finite(acceleration[0].Get<Vec3>());
                    auto maximum = std::array{Arg::Data("ReturnValue", engine::Type::Float, 0.f, true)};
                    const bool maximumRead = movement && diagnostic.maxAccelerationAvailable &&
                        engine::Call(L"/Script/Engine.CharacterMovementComponent:GetMaxAcceleration", maximum, movement).success &&
                        std::isfinite(maximum[0].Get<float>()) && maximum[0].Get<float>() >= 0.f;
                    auto requested = Arg::Boolean("bRequestedMoveUseAcceleration", false), paths = Arg::Boolean("bUseAccelerationForPaths", false);
                    const bool requestedRead = movement && engine::Property(movement, requested);
                    const bool pathsRead = movement && engine::Property(movement, paths);
                    const Vec3 delta{position.x - diagnostic.lastLocation.x, position.y - diagnostic.lastLocation.y, position.z - diagnostic.lastLocation.z};
                    Log(std::format("Transformation movement sample: id={} session={} sample={}/{} proxy={}:{} cmc=0x{:X} dtMs={} locationRead=true location={} deltaRead={} delta={} velocityRead={} velocity={} accelerationRead={} acceleration={} maxAccelerationRead={} maxAcceleration={} requestedAccelerationRead={} requestedAcceleration={} pathAccelerationRead={} pathAcceleration={} axes=({:.3f},{:.3f}) request={} casting={} speedType={} moveAIType={} mode={} customMode={} walkSpeed={:.3f} flySpeed={:.3f} swimSpeed={:.3f} requestedMMState={} moveAPI={}",
                        characterId, sessionStarted, diagnostic.samples, MovementDiagnostics::MaximumSamples, proxy.index, proxy.serial, movement,
                        now >= diagnostic.lastLocationAt ? now - diagnostic.lastLocationAt : 0, vectorText(position), diagnostic.locationKnown,
                        diagnostic.locationKnown ? vectorText(delta) : "unknown", velocityRead, velocityRead ? vectorText(velocity.Get<Vec3>()) : "unknown",
                        accelerationRead, accelerationRead ? vectorText(acceleration[0].Get<Vec3>()) : "unknown", maximumRead,
                        maximumRead ? std::format("{:.3f}", maximum[0].Get<float>()) : "unknown", requestedRead,
                        requestedRead ? (requested.Get<std::int32_t>() ? "true" : "false") : "unknown", pathsRead,
                        pathsRead ? (paths.Get<std::int32_t>() ? "true" : "false") : "unknown", diagnostic.axes.forward, diagnostic.axes.right,
                        locomotion.requestId, castingSkill, locomotion.previous.speedType, locomotion.previous.moveAIType,
                        movementState.mode, movementState.custom, movementState.walkSpeed, movementState.flySpeed, movementState.swimSpeed, locomotion.previous.motionMatchingState,
                        locomotion.previous.motionMatchingState ? "with-mm" : "native"));
                }
                catch (...) {}
                SampleAnimationDiagnostics(actor);
                diagnostic.lastLocation = position;
                diagnostic.lastLocationAt = now;
                diagnostic.locationKnown = true;
            }
            void RecordMovementStop(std::string_view reason, bool cancelled, bool attempted = true) noexcept
            {
                auto &diagnostic = movementDiagnostics;
                if ((!diagnostic.moving && locomotion.requestId <= 0) || diagnostic.stops >= MovementDiagnostics::MaximumStops) return;
                ++diagnostic.stops;
                try { Log(std::format("Transformation movement stop: id={} session={} event={}/{} request={} reason={} cancelAttempted={} cancelCallSucceeded={}",
                    characterId, sessionStarted, diagnostic.stops, MovementDiagnostics::MaximumStops, locomotion.requestId, reason, attempted, cancelled)); }
                catch (...) {}
            }

            bool StopLocomotion(std::uintptr_t actor, std::string_view reason = "pause")
            {
                if (locomotion.requestId > 0 && actor)
                {
                    // The game checks this request index before stopping its
                    // mover. A skill's replacement request must remain intact.
                    auto stop = std::array{Arg::Object("Unit", actor), Arg::Int("RequestIdx", locomotion.requestId)};
                    const bool cancelled = Call(L"/Script/b1-Managed.BGUFuncLibAICS:BGUCancelAIMoveTo", stop);
                    RecordMovementStop(reason, cancelled);
                    if (!cancelled) return false;
                }
                else RecordMovementStop(reason, locomotion.requestId <= 0, false);
                locomotion.Reset();
                nextMovementAnimationAt = 0;
                movementDiagnostics.moving = false;
                return true;
            }

            void RecordExit(std::string reason)
            {
                if (reason.empty() || !exitReason.empty()) return;
                exitReason = std::move(reason);
                Log("Transformation exit: " + exitReason);
            }

            Progress Load(const CharacterEntry &entry) override
            {
                if (!capabilities.isolation || !capabilities.input || !native::Ready()) return Fail("角色隔离或输入桥接尚未通过当前版本校验");
                if (!native::Initialized(context.player)) return Fail("原玩家初始化尚未完成");
                prepared = {};
                if (!Prepare(entry, prepared)) return Fail(message);
                if (prepared.skills.empty()) return Fail("角色缺少有效技能描述");
                if (!prepared.adapter.allowed) return Fail(prepared.adapter.reason);
                characterId = entry.id;
                BeginMovementDiagnostics();
                cinematicExit = false;
                cinematicPaused = false;
                cinematicEndedAt = 0;
                suspendedActors.clear();
                player = Weak(context.key.player);
                controller = Weak(context.key.controller);
                world = Weak(context.key.world);
                stage = 0;
                locomotion.Reset();
                nextMovementAnimationAt = 0;
                movementAnimation = {};
                return Progress::Complete;
            }
            Progress RejectAdapter(std::string reason)
            {
                {
                    std::lock_guard lock(mutex);
                    const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto &entry) { return entry.id == characterId; });
                    if (it != catalog.end()) { it->support = Support::Unverified; it->reason = reason; }
                }
                return Fail(std::move(reason));
            }
            std::uintptr_t Spawn(std::uintptr_t cls, std::uintptr_t owner)
            {
                auto args = std::array{Arg::Object("WorldContextObject", engine::ResolveWeak(world)), Arg::Object("ActorClass", cls), Arg::TransformValue("SpawnTransform", transform),
                    Arg::Enum("CollisionHandlingOverride", 1), Arg::Object("Owner", owner), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                return Call(L"/Script/b1.BGUFunctionLibrary:BGUBeginDeferredActorSpawnFromClass", args) ? args.back().Get<std::uintptr_t>() : 0;
            }
            Progress Initialize(const CharacterEntry &) override
            {
                const auto original = engine::ResolveWeak(player);
                if (!original || !engine::ResolveWeak(world)) return Fail("玩家或场景在初始化时失效");
                if (stage == 0)
                {
                    if (engine::ResolveWeak(prepared.characterClassIdentity) != prepared.characterClass) return Fail("角色蓝图在生成前已失效");
                    auto transformArgs = std::array{Arg::TransformValue("ReturnValue", {}, true)};
                    if (!Call(L"/Script/Engine.Actor:GetTransform", transformArgs, original)) return Fail("无法读取玩家变换");
                    transform = transformArgs[0].Get<engine::Transform>();
                    if (!Finite(transform.translation) || !Finite(transform.scale) || transform.scale.x <= 0. || transform.scale.y <= 0. || transform.scale.z <= 0. ||
                        std::any_of(std::begin(transform.rotation), std::end(transform.rotation), [](double value) { return !std::isfinite(value); })) return Fail("玩家变换数据无效");
                    origin = transform.translation;
                    const auto base = engine::Find(L"/Script/b1-Managed.BUTamerActor");
                    const auto configActor = base ? Spawn(base, original) : 0;
                    tamer = engine::MakeWeak(configActor);
                    if (!configActor || engine::ResolveWeak(tamer) != configActor)
                    {
                        if (configActor) Empty(L"/Script/Engine.Actor:K2_DestroyActor", configActor);
                        return Fail("临时配置角色生成失败");
                    }
                    if (!Hidden(configActor, true) || !Collision(configActor, false)) return Fail("临时配置隔离失败");
                    auto monster = Arg::Object("MonsterClassObj", prepared.characterClass);
                    const auto config = Object(configActor, "ConfigInfoComp");
                    const auto sourceConfig = engine::ResolveWeak(prepared.sourceConfig);
                    // Deep native property copies retain weapon/birth-buff arrays
                    // and component template/alter/reject maps. Identity, graph,
                    // behavior tree and interaction configuration stay fresh.
                    if (!sourceConfig || !config) return Fail("角色配置来源已失效");
                    for (const auto name : {"UnitCDesc", "UnitPhysicsDesc", "FlagConfig"})
                        if (!engine::CopyStructProperty(sourceConfig, config, name)) return Fail("无法深拷贝角色武器、物理或组件模板配置");
                    constexpr std::array<std::string_view, 1> configPath{"UnitCDesc"};
                    auto res = Arg::Int("ResID", prepared.resource), over = Arg::Int("OverrideID", prepared.overrideId);
                    if (!config || !engine::Property(configActor, monster, true) || !engine::PropertyPath(config, configPath, res, true) || !engine::PropertyPath(config, configPath, over, true))
                        return Fail("临时配置无法初始化");
                    const auto extend = Object(configActor, "ExtendConfigComp");
                    for (const auto name : {"BehaviorGraphTemplateID", "DialogueGraphTemplateID", "InteractiveUnitID", "DefalutInteractiveGroupID", "ShopID", "RewardDropID"})
                    {
                        auto value = Arg::Int(name, 0);
                        if (!engine::Property(extend, value, true)) return Fail("临时角色剧情/交互配置无法隔离");
                    }
                    for (const auto name : {"StateGraphTemplate", "PhaseStateGraphTemplate", "BattleSC"})
                    {
                        auto value = Arg::Object(name, 0);
                        if (!engine::Property(extend, value, true)) return Fail("临时角色状态图无法隔离");
                    }
                    for (const auto name : {"bCanAsPerformer", "bCanAsNPC", "bCanAsFollowPartner"})
                    {
                        auto value = Arg::Boolean(name, false);
                        if (!engine::Property(extend, value, true)) return Fail("临时角色剧情标志无法隔离");
                    }
                    // The configuration actor deliberately NEVER finishes spawning:
                    // BUTamerActor.BeginPlayCS -> FTamerRef.Load would register it
                    // in the level's global actor/task state.
                    const auto actor = Spawn(prepared.characterClass, configActor);
                    proxy = engine::MakeWeak(actor);
                    if (!actor || engine::ResolveWeak(proxy) != actor)
                    {
                        if (actor) Empty(L"/Script/Engine.Actor:K2_DestroyActor", actor);
                        return Fail("临时角色蓝图生成失败");
                    }
                    if (!native::SetProxy(proxy)) return Fail("临时角色隔离身份无法注册");
                    if (prepared.requiresSpecialFsm && (!special::Ready() || !special::SetSession(proxy))) return Fail("特殊形态状态机隔离尚未就绪");
                    auto owner = std::array{Arg::Object("NewOwner", configActor)};
                    if (!Call(L"/Script/b1.BGUCharacter:SetTamerOwner", owner, actor) || !native::SetTamerType(actor, 2)) return Fail("临时角色归属无法初始化");
                    if (!Hidden(actor, true) || !Collision(actor, false)) return Fail("临时角色无法隐藏或禁用碰撞");
                    auto finish = std::array{Arg::Object("Actor", actor), Arg::TransformValue("SpawnTransform", transform), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                    if (!Call(L"/Script/b1.BGUFunctionLibrary:BGUFinishSpawningActor", finish) || finish[2].Get<std::uintptr_t>() != actor)
                        return Fail("临时角色初始化未完成");
                    stage = 1;
                    return Progress::Pending;
                }
                const auto actor = engine::ResolveWeak(proxy);
                if (!actor) return Fail("临时角色在初始化中失效");
                if (!native::Initialized(actor)) return Progress::Pending;
                // Request acceptance alone does not prove the native AI mover
                // has a controller. Keep this observation separate from fixes
                // so an absent cache never causes a duplicate controller.
                auto controllerInfo = std::array{Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                const bool controllerRead = engine::Call(L"/Script/Engine.Pawn:GetController", controllerInfo, actor).success;
                const auto aiController = controllerRead ? controllerInfo[0].Get<std::uintptr_t>() : 0;
                auto controlledPawn = std::array{Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                // GetControlledPawn is the managed wrapper name; the native
                // UFunction keeps its OriginalName/UMetaPath, K2_GetPawn.
                const bool pawnRead = aiController && engine::Call(L"/Script/Engine.Controller:K2_GetPawn", controlledPawn, aiController).success;
                const auto aiData = Object(Object(actor, "BGUDataComp"), "mUBUC_AIComponent");
                Log(std::format("Transformation proxy controller: read={}, present={}, BGUAI={}, pawnRead={}, controlsProxy={}, dataCacheMatches={}",
                    controllerRead, aiController != 0, aiController && engine::IsA(aiController, L"/Script/b1.BGUAIController"),
                    pawnRead, pawnRead && controlledPawn[0].Get<std::uintptr_t>() == actor, aiController && Object(aiData, "AIController") == aiController));
                auto health = std::array{Arg::Object("Unit", actor), Arg::Enum("AttrID", 151), Arg::Data("ReturnValue", engine::Type::Float, 0.f, true)};
                if (!Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUGetFloatAttr", health) || !std::isfinite(health[2].Get<float>()) || health[2].Get<float>() <= 0)
                    return Fail("临时角色生命属性未初始化");
                auto stopAI = std::array{Arg::Object("Unit", actor), Arg::Object("BT", 0)};
                if (!Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUSetBehaviorTree", stopAI)) return Fail("无法停用临时角色自主行为");
                if (!native::CopyTeam(actor, original)) return Fail("无法转接玩家阵营");
                if (!native::SetTarget(actor, 0)) return Fail("无法清除临时角色初始化时的自主目标");
                target = {};
                const auto targetMovement = Object(actor, "CharacterMovement");
                auto physics = Arg::Boolean("bRunPhysicsWithNoController", true);
                if (!targetMovement || !engine::Property(targetMovement, physics, true)) return Fail("角色移动组件未初始化");
                adapter::Movement actualMovement;
                if (!ReadMovement(actor, actualMovement)) return RejectAdapter("生成角色的移动配置无法校验");
                bool suppressed{}, inputSuppressed{};
                std::string reason;
                if (!MovementStates(actor, suppressed, inputSuppressed, reason)) return RejectAdapter(std::move(reason));
                prepared.adapter = adapter::Evaluate(actualMovement, false, suppressed || inputSuppressed);
                if (!prepared.adapter.allowed) return RejectAdapter(prepared.adapter.reason);
                if (!IndependentConfig(Object(actor, "ExtendConfigComp"), reason, &prepared.requiresSpecialFsm) ||
                    !IndependentConfig(Object(engine::ResolveWeak(tamer), "ExtendConfigComp"), reason, &prepared.requiresSpecialFsm)) return RejectAdapter(std::move(reason));
                if (prepared.requiresSpecialFsm)
                {
                    const auto state = special::Check(proxy);
                    if (!state.observed || !state.healthy) return RejectAdapter(std::string(special::Reason(state.fault)));
                }
                // The CDO can enumerate skills not present on this initialized
                // form. Publish the real instance's IDs before accepting input.
                if (!ReadSkills(actor, prepared.skills) || prepared.skills.empty()) return RejectAdapter("生成角色没有可验证的实际技能集合");
                {
                    std::lock_guard lock(mutex);
                    skillCache[characterId] = publishedSkills = prepared.skills;
                    published.inspectedId = characterId;
                    if (!model::ValidSlots(slotConfig[characterId], prepared.skills)) slotConfig[characterId] = EmptySlots();
                }
                stage = 2;
                return Progress::Complete;
            }
            Progress Activate() override
            {
                const auto original = engine::ResolveWeak(player), actor = engine::ResolveWeak(proxy);
                if (!original || !actor || stage != 2) return Fail("角色或玩家在激活前失效");
                bool playingMovie{};
                if (!Cinematic(original, playingMovie)) return Fail("无法校验过场状态，正在恢复悟空");
                if (playingMovie) { cinematicExit = true; return Fail("过场动画中暂停变身，请在演出结束后重试"); }
                // Weapon actors have independent ECS BeginPlay queues. The
                // character being ready does not imply all of its weapons are
                // ready. Wait before saving baselines or acquiring player state.
                const auto weaponReadiness = native::CheckWeaponsReady(actor, Object(engine::ResolveWeak(tamer), "ConfigInfoComp"));
                if (weaponReadiness == native::WeaponReadiness::Pending) return Progress::Pending;
                if (weaponReadiness != native::WeaponReadiness::Ready) return Fail("角色武器就绪状态校验失败，正在恢复悟空");
                const auto move = Object(original, "CharacterMovement");
                auto custom = Arg::Byte("CustomMovementMode", 0);
                auto camera = std::array{Arg::Object("ComponentClass", engine::Find(L"/Script/Engine.SpringArmComponent")), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                if (!camera[0].Get<std::uintptr_t>() || !Call(L"/Script/Engine.Actor:GetComponentByClass", camera, original)) return Fail("无法读取玩家相机组件");
                const auto cameraArm = camera[1].Get<std::uintptr_t>();
                auto length = Arg::Float("TargetArmLength", 0.f);
                Vec3 originalBounds{}, targetBounds{};
                if (!move || !engine::Property(move, custom) || custom.Get<std::uint8_t>() != 0 ||
                    !cameraArm || !engine::Property(cameraArm, length) || !std::isfinite(length.Get<float>()) || length.Get<float>() <= 0.f ||
                    !Bounds(original, originalBounds) || !Bounds(actor, targetBounds)) return Fail("无法保存玩家或相机恢复状态");
                arm = engine::MakeWeak(cameraArm);
                armLength = length.Get<float>();
                const auto playerSize = (std::max)({originalBounds.x, originalBounds.y, originalBounds.z});
                const auto targetSize = (std::max)({targetBounds.x, targetBounds.y, targetBounds.z});
                autoArmLength = armLength * static_cast<float>(std::clamp(targetSize / playerSize, .75, 4.));
                appliedArmLength = camera::ArmLength(autoArmLength, CameraDistance(characterId));
                if (!std::isfinite(autoArmLength) || autoArmLength <= 0.f || !std::isfinite(appliedArmLength) || appliedArmLength <= 0.f)
                    return Fail("角色自动视距计算无效");
                length = Arg::Float("TargetArmLength", appliedArmLength);
                baseline = modified = true;
                armChanged = true;
                if (!engine::Property(cameraArm, length, true) || !native::SetInputOwner(player)) return Fail("变身相机或输入初始化失败，正在恢复悟空");
                // SimpleState is reference counted. Add exactly one layer to
                // the retained player and remove only that layer on exit.
                // The proxy keeps its original vulnerability and attributes.
                const auto protection = DamageProtection(original, false);
                ownsImmuneLayer = protection.success || protection.state == engine::CallState::Completed;
                immuneLayerUncertain = protection.state == engine::CallState::Indeterminate;
                if (!protection.success) return Fail("无法保护保留的悟空，正在恢复");
                bool protectedPlayer{};
                if (!HasDamageProtection(original, protectedPlayer) || !protectedPlayer) return Fail("保留悟空的免伤状态未生效");
                if (!native::AcquirePlayerIsolation(original, playerIsolation) || !Collision(actor, true) || !Hidden(actor, false)) return Fail("变身激活失败，正在恢复悟空");
                // A weapon's first Tick can miss its isolated master's global
                // GUID lookup. Repair only its local master/default activity,
                // after showing the owner and before accepting gameplay input.
                if (!native::InitializeWeapons(actor, Object(engine::ResolveWeak(tamer), "ConfigInfoComp"))) return Fail("角色武器本地初始化失败，正在恢复悟空");
                frozen = true;
                activeCameraMeters = appliedArmLength / 100.f;
                combo.Reset();
                stage = 3;
                message = "实验性变身已激活；保留角色原始数值";
                return Progress::Complete;
            }
            bool SuspendActor(engine::WeakObject identity)
            {
                const auto actor = engine::ResolveWeak(identity);
                if (!actor) return true;
                auto saved = std::find_if(suspendedActors.begin(), suspendedActors.end(), [&](const auto &value) {
                    return value.identity.index == identity.index && value.identity.serial == identity.serial;
                });
                if (saved == suspendedActors.end())
                {
                    auto hidden = Arg::Boolean("bHidden", false);
                    auto collision = std::array{Arg::Data("ReturnValue", engine::Type::Bool, std::int32_t{}, true)};
                    if (!engine::Property(actor, hidden) || !Call(L"/Script/Engine.Actor:GetActorEnableCollision", collision, actor)) return false;
                    suspendedActors.push_back({identity, hidden.Get<std::int32_t>() != 0, collision[0].Get<std::int32_t>() != 0});
                }
                return Hidden(actor, true) && Collision(actor, false);
            }
            bool ReleaseCameraForCinematic()
            {
                if (!armChanged) return true;
                if (const auto cameraArm = engine::ResolveWeak(arm))
                {
                    auto current = Arg::Float("TargetArmLength", 0.f);
                    if (!engine::Property(cameraArm, current)) return false;
                    if (current.Get<float>() == appliedArmLength)
                    {
                        auto length = Arg::Float("TargetArmLength", armLength);
                        if (!engine::Property(cameraArm, length, true)) return false;
                    }
                }
                armChanged = false;
                return true;
            }
            Progress SuspendForCinematic(std::uintptr_t actor, std::uintptr_t original)
            {
                cinematicExit = true; // Recovery must not teleport a sequence performer.
                cinematicEndedAt = 0;
                native::SetInputOwner({});
                native::ConsumeActions();
                combo.Reset();
                if (!StopLocomotion(actor, "cinematic")) return Fail("过场期间无法停止角色移动，正在恢复悟空");
                if (!cinematicPaused)
                {
                    if (!ReadMovement(actor, suspendedMovement)) return Fail("无法保存过场前角色移动状态");
                    cinematicPaused = true;
                    Log(std::format("Transformation cinematic suspended: id={} session={}", characterId, sessionStarted));
                }
                if (!SuspendActor(proxy)) return Fail("无法隐藏过场中的临时角色");
                for (const auto child : native::OwnedChildren())
                    if (!SuspendActor(child)) return Fail("无法隐藏过场中的临时附属角色");
                const auto movement = Object(actor, "CharacterMovement");
                if (!movement || !Mode(movement, 0, 0)) return Fail("无法暂停过场中的角色移动");
                if (!native::ReleasePlayerIsolation(playerIsolation)) return Fail("过场玩家状态释放失败，正在恢复");
                if (ownsImmuneLayer && !immuneLayerUncertain)
                {
                    const auto protection = DamageProtection(original, true);
                    ownsImmuneLayer = !(protection.success || protection.state == engine::CallState::Completed);
                    immuneLayerUncertain = protection.state == engine::CallState::Indeterminate;
                    if (!protection.success) return Fail("过场玩家保护层释放失败，正在恢复");
                }
                if (!ReleaseCameraForCinematic()) return Fail("过场相机释放失败，正在恢复");
                message = "过场动画中暂挂变身；演出结束后自动恢复当前角色";
                return Progress::Complete;
            }
            Progress ResumeAfterCinematic(std::uintptr_t actor, std::uintptr_t original)
            {
                native::ConsumeActions();
                combo.Reset();
                // Require a quiet blend-out interval. A single negative movie
                // observation can occur between sequence/preplay state changes.
                const auto now = GetTickCount64();
                if (!cinematicEndedAt) cinematicEndedAt = now;
                if (!model::Expired(now, cinematicEndedAt, 250))
                {
                    for (const auto child : native::OwnedChildren())
                        if (!SuspendActor(child)) return Fail("无法隔离过场退场中的临时附属角色");
                    return Progress::Complete;
                }
                const auto alive = [](std::uintptr_t unit) {
                    auto health = std::array{Arg::Object("Unit", unit), Arg::Enum("AttrID", 151), Arg::Data("ReturnValue", engine::Type::Float, 0.f, true)};
                    return Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUGetFloatAttr", health) &&
                        std::isfinite(health[2].Get<float>()) && health[2].Get<float>() > 0.f;
                };
                if (!alive(original) || !alive(actor)) return Fail("过场后玩家或变身角色已死亡，正在恢复悟空");
                if (prepared.requiresSpecialFsm)
                {
                    const auto state = special::Check(proxy);
                    if (!state.observed || !state.healthy) return Fail(std::string(special::Reason(state.fault)));
                }
                bool movementSuppressed{}, inputSuppressed{};
                std::string controlReason;
                if (!MovementStates(actor, movementSuppressed, inputSuppressed, controlReason)) return Fail(std::move(controlReason));
                if (!Location(original, origin) || !SetLocation(actor, origin)) return Fail("无法恢复角色到过场结束位置");
                auto camera = std::array{Arg::Object("ComponentClass", engine::Find(L"/Script/Engine.SpringArmComponent")), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                if (!camera[0].Get<std::uintptr_t>() || !Call(L"/Script/Engine.Actor:GetComponentByClass", camera, original)) return Fail("无法重新绑定过场后相机");
                const auto cameraArm = camera[1].Get<std::uintptr_t>();
                auto length = Arg::Float("TargetArmLength", 0.f);
                if (!cameraArm || !engine::Property(cameraArm, length) || !std::isfinite(length.Get<float>()) || length.Get<float>() <= 0.f)
                    return Fail("过场后相机基线无效");
                // Preserve the game's new camera baseline while retaining the
                // character's already computed scale and saved distance choice.
                arm = engine::MakeWeak(cameraArm);
                armLength = length.Get<float>();
                appliedArmLength = camera::ArmLength(autoArmLength, CameraDistance(characterId));
                armChanged = true;
                length = Arg::Float("TargetArmLength", appliedArmLength);
                if (!engine::Property(cameraArm, length, true) || !native::SetInputOwner(player)) return Fail("过场后相机或输入恢复失败");
                const auto protection = DamageProtection(original, false);
                ownsImmuneLayer = protection.success || protection.state == engine::CallState::Completed;
                immuneLayerUncertain = protection.state == engine::CallState::Indeterminate;
                if (!protection.success || !native::AcquirePlayerIsolation(original, playerIsolation)) return Fail("过场后玩家隔离恢复失败");
                bool protectedPlayer{};
                if (!HasDamageProtection(original, protectedPlayer) || !protectedPlayer || !native::RefreshPlayerIsolation(playerIsolation)) return Fail("过场后玩家隔离校验失败");
                const auto movement = Object(actor, "CharacterMovement");
                if (!movement || !Mode(movement, suspendedMovement.mode, suspendedMovement.custom) || !native::SetTarget(actor, 0)) return Fail("过场后角色移动恢复失败");
                target = {};
                // Restore only actors owned by this session, identified by weak
                // serial as well as address. Do not rerun weapon initialization.
                for (auto saved = suspendedActors.rbegin(); saved != suspendedActors.rend(); ++saved)
                    if (const auto object = engine::ResolveWeak(saved->identity))
                    {
                        auto hidden = Arg::Boolean("bHidden", false);
                        auto collision = std::array{Arg::Data("ReturnValue", engine::Type::Bool, std::int32_t{}, true)};
                        if (!engine::Property(object, hidden) || !Call(L"/Script/Engine.Actor:GetActorEnableCollision", collision, object))
                            return Fail("过场后临时角色可见性恢复失败");
                        // Preserve observable changes made after our last pause
                        // write; actor lifetime alone is not property ownership.
                        if ((!collision[0].Get<std::int32_t>() && !Collision(object, saved->collision)) ||
                            (hidden.Get<std::int32_t>() && !Hidden(object, saved->hidden))) return Fail("过场后临时角色可见性恢复失败");
                    }
                suspendedActors.clear();
                cinematicPaused = cinematicExit = false;
                cinematicEndedAt = 0;
                activeCameraMeters = appliedArmLength / 100.f;
                Log(std::format("Transformation cinematic resumed: id={} session={}", characterId, sessionStarted));
                message = "过场结束，已恢复原变身角色";
                return Progress::Complete;
            }
            Progress Update() override
            {
                ++movementDiagnostics.frame;
                const auto actor = engine::ResolveWeak(proxy), original = engine::ResolveWeak(player);
                if (!actor || !original || !engine::ResolveWeak(controller)) return Fail("角色或控制器已失效，正在恢复");
                if (native::DeathRequested()) return Fail("变身角色死亡，正在恢复悟空");
                // Let sequences own the real performer, preserving this proxy
                // for resumption after preplay/movie/blend-out have all ended.
                bool playingMovie{};
                if (!Cinematic(original, playingMovie)) return Fail("无法校验过场状态，正在恢复悟空");
                if (playingMovie) return SuspendForCinematic(actor, original);
                if (cinematicPaused) return ResumeAfterCinematic(actor, original);
                if (!native::RefreshPlayerIsolation(playerIsolation)) return Fail("保留悟空的状态租约失效，正在恢复");
                const auto desiredArmLength = camera::ArmLength(autoArmLength, CameraDistance(characterId));
                const auto cameraArm = engine::ResolveWeak(arm);
                auto currentArmLength = Arg::Float("TargetArmLength", 0.f);
                if (!cameraArm || !engine::Property(cameraArm, currentArmLength)) return Fail("角色视距读取失败，正在恢复悟空");
                // The game's camera control data can rewrite this property on
                // every tick. Compare the real arm, not just our cached value.
                if (currentArmLength.Get<float>() != desiredArmLength)
                {
                    auto length = Arg::Float("TargetArmLength", desiredArmLength);
                    if (!engine::Property(cameraArm, length, true)) return Fail("角色视距更新失败，正在恢复悟空");
                }
                appliedArmLength = desiredArmLength;
                activeCameraMeters = appliedArmLength / 100.f;
                if (native::DeathRequested()) return Fail("变身角色死亡，正在恢复悟空");
                if (prepared.requiresSpecialFsm)
                {
                    const auto state = special::Check(proxy);
                    if (!state.observed || !state.healthy) return Fail(std::string(special::Reason(state.fault)));
                }
                for (auto child : native::OwnedChildren(true))
                    if (!Destroy(child, true)) return Fail("临时附属角色清理尚未完成，正在恢复");
                bool protectedPlayer{};
                if (!HasDamageProtection(original, protectedPlayer) || !protectedPlayer) return Fail("保留悟空的免伤状态已失效，正在恢复");
                if (!native::CopyTeam(actor, original)) return Fail("角色阵营转接失效，正在恢复");
                auto health = std::array{Arg::Object("Unit", actor), Arg::Enum("AttrID", 151), Arg::Data("ReturnValue", engine::Type::Float, 0.f, true)};
                if (!Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUGetFloatAttr", health) || !std::isfinite(health[2].Get<float>()) || health[2].Get<float>() <= 0) return Fail("变身角色死亡或属性已失效");
                Vec3 p{};
                if (!Location(actor, p)) return Fail("玩家代理位置读取失败，正在恢复");
                origin = p;
                if (!SetLocation(original, p)) return Fail("玩家代理同步失败，正在恢复");
                auto casting = std::array{Arg::Object("Unit", actor), Arg::Data("ReturnValue", engine::Type::Int, int{}, true)};
                if (!Call(L"/Script/b1-Managed.BGUFuncLibSkillCS:BGUGetCastingSkillID", casting)) return Fail("无法读取技能施放状态");
                adapter::Movement actualMovement;
                if (!ReadMovement(actor, actualMovement)) return RejectAdapter("角色移动组件在运行中失效");
                bool movementSuppressed{}, inputSuppressed{};
                std::string controlReason;
                if (!MovementStates(actor, movementSuppressed, inputSuppressed, controlReason)) return RejectAdapter(std::move(controlReason));
                const auto control = adapter::Evaluate(actualMovement, false, casting[1].Get<int>() > 0 || movementSuppressed || inputSuppressed);
                if (!control.allowed) return RejectAdapter(control.reason);
                auto lockTargetParams = std::array{Arg::Object("Unit", original), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                if (!Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:GetUnitLockTargetActor", lockTargetParams)) return Fail("目标选择接口失效");
                const auto lockedActor = lockTargetParams[1].Get<std::uintptr_t>();
                const auto selected = lockedActor != actor && lockedActor != original ? engine::MakeWeak(lockedActor) : engine::WeakObject{};
                if (selected.index != target.index || selected.serial != target.serial)
                {
                    if (!native::SetTarget(actor, engine::ResolveWeak(selected))) return Fail("无法转接角色锁定目标");
                    target = selected;
                }
                // Drive the target's own ECS/AI mover, including its configured
                // gait, unit states and animation. Raw Pawn input alone bypasses
                // that chain. Only the retained player follows as camera anchor.
                if (!inputSuppressed && !overlay::Visible() && Foreground())
                {
                    auto view = std::array{Arg::Rotation("ReturnValue", {}, true)};
                    if (!Call(L"/Script/Engine.Pawn:GetControlRotation", view, original)) return Fail("无法读取玩家视角方向");
                    const auto rotation = view[0].Get<engine::Rotator>();
                    const auto axes = native::MovementAxes();
                    movementDiagnostics.axes = axes;
                    const auto steering = adapter::Steer(movementSuppressed || casting[1].Get<int>() > 0 ? adapter::Motion::Stationary : control.motion, rotation.yaw, rotation.pitch, axes.forward, axes.right);
                    if (!steering.valid) return Fail("角色移动输入或视角无效");
                    const bool locked = engine::ResolveWeak(target) != 0;
                    auto intent = adapter::Locomotion(actualMovement, steering, p, locked);
                    if (!intent.valid) return Fail("角色原生移动目标无效");
                    bool useMotionMatching{};
                    if (intent.moving)
                    {
                        const auto now = GetTickCount64();
                        if (!nextMovementAnimationAt || now >= nextMovementAnimationAt || movementAnimationSpeed != intent.speedType || movementAnimationLocked != locked)
                        {
                            movementAnimation = native::SelectMotionMatching(actor, intent.speedType, locked);
                            movementAnimationSpeed = intent.speedType;
                            movementAnimationLocked = locked;
                            nextMovementAnimationAt = now + 250;
                            if (movementDiagnostics.animationChoices < MovementDiagnostics::MaximumSamples)
                            {
                                ++movementDiagnostics.animationChoices;
                                Log(std::format("Transformation MM selection: id={} session={} sample={} speed={} locked={} available={} current={} required={} valid={} state={} usesLocked={}",
                                    characterId, sessionStarted, movementDiagnostics.animationChoices, intent.speedType, locked,
                                    movementAnimation.available, movementAnimation.current, movementAnimation.required,
                                    movementAnimation.valid, movementAnimation.state, movementAnimation.usesLocked));
                            }
                        }
                        // Animation availability selects the request variant,
                        // never whether this role is allowed to move. The
                        // ordinary mover remains usable without a valid MM gait.
                        useMotionMatching = movementAnimation.available && movementAnimation.current && movementAnimation.required && movementAnimation.valid;
                        std::string_view animationNotice;
                        if (useMotionMatching)
                        {
                            intent.motionMatchingState = static_cast<std::uint8_t>(movementAnimation.state);
                            if (locked && !movementAnimation.usesLocked)
                            {
                                intent.moveAIType = 3; // The role has only a free gait: face its velocity.
                                animationNotice = "该角色缺少对应锁定步态，已使用自身自由移动动画";
                            }
                        }
                        else if (!movementAnimation.available || !movementAnimation.current)
                            animationNotice = "角色移动动画配置暂不可读，已使用原生移动（可能滑行）";
                        else if (movementAnimation.required)
                            animationNotice = "角色缺少当前步态动画，已允许原生移动（可能滑行）";
                        if (!animationNotice.empty()) message = animationNotice;
                        else if (message == "该角色缺少对应锁定步态，已使用自身自由移动动画" ||
                            message == "角色移动动画配置暂不可读，已使用原生移动（可能滑行）" ||
                            message == "角色缺少当前步态动画，已允许原生移动（可能滑行）")
                            message = "实验性变身已激活；保留角色原始数值";
                    }
                    if (!intent.moving)
                    {
                        if (!StopLocomotion(actor, casting[1].Get<int>() > 0 ? "casting" : movementSuppressed ? "state_suppressed" : "input_idle")) return Fail("无法停止本次角色移动请求");
                    }
                    else if (locomotion.Due(intent, GetTickCount64()))
                    {
                        std::vector<Arg> move{Arg::Object("Unit", actor), Arg::Vector("Position", intent.destination), Arg::Enum("SpeedRateType", intent.speedType),
                            Arg::Float("AcceptableRadius", 5.f), Arg::Enum("MoveAIType", intent.moveAIType), Arg::Boolean("IncludeSelfRadius", false),
                            Arg::Boolean("IncludeTargetRadius", false)};
                        if (useMotionMatching) move.push_back(Arg::Enum("MotionMatchingState", intent.motionMatchingState));
                        move.push_back(Arg::Data("ReturnValue", engine::Type::Int, int{}, true));
                        const auto result = engine::Call(useMotionMatching ? L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocationWithMM" :
                            L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocation", move);
                        if (!result.success) { Log("Transformation: " + result.message); return Fail("角色原生移动接口失效"); }
                        const int moveRequest = move.back().Get<int>();
                        if (moveRequest <= 0)
                        {
                            if (!StopLocomotion(actor, "request_rejected")) return Fail("无法取消被拒绝的角色移动请求");
                            message = "角色原生移动请求被拒绝；请检查导航、当前状态和该形态的移动能力";
                        }
                        locomotion.Result(intent, GetTickCount64(), moveRequest);
                        MovementRequestObserved(p, GetTickCount64(), moveRequest);
                    }
                    // Free locomotion turns through the native mover. Preserve
                    // the selected combat target while its locked gait is active.
                    if (const auto selectedActor = engine::ResolveWeak(target); selectedActor && !movementSuppressed && casting[1].Get<int>() <= 0 &&
                        (!intent.moving || !useMotionMatching || movementAnimation.usesLocked))
                    {
                        Vec3 targetPosition{};
                        if (!Location(selectedActor, targetPosition) || !Face(actor, {targetPosition.x - p.x, targetPosition.y - p.y, 0.})) return Fail("角色目标朝向同步失败");
                    }
                    for (const auto action : native::ConsumeActions())
                    {
                        std::vector<SkillSlot> slots;
                        { std::lock_guard lock(mutex); slots = slotConfig[characterId]; }
                        const auto it = std::find_if(slots.begin(), slots.end(), [&](const auto &slot) { return slot.action == action; });
                        if (!model::ValidSlots(slots, prepared.skills)) { message = "技能槽包含失效或不属于该角色的技能 ID"; continue; }
                        if (it != slots.end() && !it->skills.empty() && !combo.Busy()) combo.Queue(it->skills, GetTickCount64());
                    }
                }
                else
                {
                    if (!StopLocomotion(actor, inputSuppressed ? "input_suppressed" : overlay::Visible() ? "menu" : "focus")) return Fail("无法停止暂挂的角色移动请求");
                    native::ConsumeActions(); combo.Reset();
                }
                if (const auto next = combo.Next(casting[1].Get<int>(), GetTickCount64()))
                {
                    if (!StopLocomotion(actor, "skill_dispatch")) return Fail("无法在施放技能前停止角色移动请求");
                    auto use = std::array{Arg::Object("Unit", actor), Arg::Int("SkillID", *next), Arg::Enum("SourceType", 0), Arg::Data("ReturnValue", engine::Type::Int, int{}, true)};
                    const bool accepted = Call(L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIUseSkillWithSkillID", use) && use[3].Get<int>() > 0;
                    combo.Result(accepted, GetTickCount64());
                    if (!accepted) message = std::format("游戏拒绝技能 {}；检查当前状态、目标和冷却", *next);
                }
                if (combo.Failed()) { message = "连招已停止：技能被拒绝或施放状态超时"; combo.Reset(); }
                SampleMovementDiagnostics(actor, p, actualMovement, casting[1].Get<int>());
                return Progress::Complete;
            }
            Progress Restore() override
            {
                combo.Reset();
                native::SetInputOwner({});
                if (baseline && !cinematicExit)
                    if (const auto original = engine::ResolveWeak(player))
                    {
                        bool playingMovie{};
                        // An unavailable observation is not evidence that no
                        // sequence owns this player. Avoid teleporting a
                        // potentially active performer while releasing ours.
                        if (!Cinematic(original, playingMovie) || playingMovie) cinematicExit = true;
                    }
                if (!StopLocomotion(engine::ResolveWeak(proxy), "restore")) { message = "正在停止临时角色移动请求；保留恢复记录并重试"; return Progress::Failed; }
                const auto quarantine = [&](std::uintptr_t actor)
                {
                    bool ok = Hidden(actor, true);
                    ok &= Collision(actor, false);
                    if (const auto move = Object(actor, "CharacterMovement")) ok &= Mode(move, 0, 0);
                    return ok;
                };
                if (const auto actor = engine::ResolveWeak(proxy))
                {
                    // Restoration may need several frames. Stop exposing the
                    // proxy to combat while retained baselines are retried.
                    if (!quarantine(actor)) { message = "正在隔离临时角色；保留悟空保护状态"; return Progress::Failed; }
                }
                for (const auto child : native::OwnedChildren())
                    if (const auto actor = engine::ResolveWeak(child); actor && !quarantine(actor))
                    { message = "正在隔离临时附属角色；保留悟空保护状态"; return Progress::Failed; }
                bool ok = true;
                if (baseline)
                {
                    if (const auto original = engine::ResolveWeak(player))
                    {
                        if (!cinematicExit)
                        {
                            auto position = origin;
                            if (const auto actor = engine::ResolveWeak(proxy)) { Vec3 p{}; if (Location(actor, p)) position = p; }
                            ok &= SetLocation(original, position);
                        }
                    }
                    else ownsImmuneLayer = immuneLayerUncertain = false;
                    // The property manager retains other systems' handles;
                    // never restore raw hidden/movement/collision snapshots.
                    ok &= native::ReleasePlayerIsolation(playerIsolation);
                    if (armChanged)
                    {
                        if (const auto cameraArm = engine::ResolveWeak(arm))
                        {
                            auto currentLength = Arg::Float("TargetArmLength", 0.f);
                            if (cinematicExit && !engine::Property(cameraArm, currentLength)) ok = false;
                            else if (!cinematicExit || currentLength.Get<float>() == appliedArmLength)
                            {
                                auto length = Arg::Float("TargetArmLength", armLength);
                                ok &= engine::Property(cameraArm, length, true);
                            }
                        }
                        if (ok) armChanged = false;
                    }
                    if (ownsImmuneLayer && ok && !immuneLayerUncertain)
                    {
                        const auto original = engine::ResolveWeak(player);
                        if (!original) ownsImmuneLayer = false;
                        else
                        {
                            const auto protection = DamageProtection(original, true);
                            if (protection.success || protection.state == engine::CallState::Completed) ownsImmuneLayer = false;
                            immuneLayerUncertain = protection.state == engine::CallState::Indeterminate;
                            ok &= protection.success;
                        }
                    }
                    if (immuneLayerUncertain)
                    {
                        // A void event may throw after changing the counter.
                        // Do not replay an ambiguous Add/Remove and consume an
                        // unrelated layer. Context replacement retires this lease.
                        message = "免伤层数的调用结果不明；请重新载入存档以完成恢复";
                        return Progress::Failed;
                    }
                    // An invalid weak original has already been destroyed by the
                    // engine. Never restore a new actor that reused its address.
                    if (ok) baseline = modified = frozen = false;
                }
                if (!ok) { RecordExit("玩家状态恢复曾失败"); message = "恢复玩家状态失败；保留恢复记录并重试"; return Progress::Failed; }
                for (auto child : native::OwnedChildren())
                    if (!Destroy(child, true)) { message = "临时附属角色清理未完成；正在重试"; return Progress::Failed; }
                if (!native::OwnedChildren().empty()) { message = "正在清理新出现的临时附属角色"; return Progress::Pending; }
                if (!Destroy(proxy, true)) { message = "临时角色清理失败；正在重试"; return Progress::Failed; }
                if (!native::OwnedChildren().empty()) { message = "正在清理角色销毁时出现的附属角色"; return Progress::Pending; }
                if (!Destroy(tamer, false)) { message = "临时配置清理失败；正在重试"; return Progress::Failed; }
                special::ClearSession();
                native::ClearProxy();
                native::SetInputOwner({});
                stage = 0;
                target = {};
                characterId.clear();
                cinematicPaused = false;
                cinematicEndedAt = 0;
                suspendedActors.clear();
                message = exitReason.empty() ? "已恢复悟空" : "已恢复悟空：" + exitReason;
                return Progress::Complete;
            }
            bool Destroy(engine::WeakObject &weak, bool isolated)
            {
                const auto actor = engine::ResolveWeak(weak);
                if (!actor) { weak = {}; return true; }
                std::optional<native::ScopedCleanup> cleanup;
                if (isolated)
                {
                    cleanup.emplace(actor);
                    if (!*cleanup) { RecordExit("临时角色清理隔离身份失效"); return false; }
                }
                if (!Empty(L"/Script/Engine.Actor:K2_DestroyActor", actor)) { RecordExit("临时角色清理请求失败"); return false; }
                // ProcessEvent success only confirms the request. Keep ownership
                // while the exact weak identity still resolves and retry later.
                if (engine::ResolveWeak(weak)) return false;
                weak = {};
                return true;
            }
            Progress Fail(std::string text) { RecordExit(text); message = std::move(text); return Progress::Failed; }
        } backend;

        void Probe()
        {
            native::Initialize();
            special::Initialize({native::OwnedComponentOwner, native::RunManagedOperation});
            const auto has = [](const wchar_t *path) { return engine::Find(path) != 0; };
            capabilities.spawn = has(L"/Script/b1-Managed.BUTamerActor") && has(L"/Script/b1.BGUFunctionLibrary:BGUBeginDeferredActorSpawnFromClass") &&
                has(L"/Script/b1.BGUFunctionLibrary:BGUFinishSpawningActor") && has(L"/Script/b1.BGUCharacter:SetTamerOwner");
            capabilities.skills = has(L"/Script/b1-Managed.AutoTestHelperLib:GetUnitAllSkillID") && has(L"/Script/b1-Managed.BGUFuncLibSkillCS:BGUGetCastingSkillID") && has(L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIUseSkillWithSkillID");
            capabilities.movement = has(L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocation") && has(L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocationWithMM") && has(L"/Script/b1-Managed.BGUFuncLibAICS:BGUCancelAIMoveTo") && has(L"/Script/Engine.Pawn:GetControlRotation") &&
                has(L"/Script/UnrealExtent.GSE_ActorFuncLib:SetActorRotationSimple") && has(L"/Script/b1-Managed.BGUFunctionLibraryCS:GetUnitLockTargetActor");
            capabilities.recovery = engine::WeakReady() && has(L"/Script/Engine.Actor:K2_DestroyActor") && has(L"/Script/Engine.Actor:SetActorHiddenInGame") && has(L"/Script/Engine.CharacterMovementComponent:SetMovementMode") &&
                has(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUHasUnitSimpleState") && has(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUSetUnitSimpleState") &&
                has(L"/Script/b1-Managed.AutoTestHelperLib:IsPlayingCameraMovie");
            capabilities.isolation = capabilities.input = native::Ready();
        }
        features::Result Queue(RequestKind kind, std::string_view id = {}, std::size_t slot = 0, int skillId = 0, bool input = false)
        {
            Initialize();
            std::lock_guard lock(mutex);
            if (kind == RequestKind::Restore)
            {
                request = Request{kind, {}, 0, GetTickCount64(), published.generation};
                published.pending = true;
                return {true, "已请求恢复悟空"};
            }
            if (request) return {false, "已有变身操作等待处理"};
            if (features::UpdateRequired()) return {false, "需要先更新修改器"};
            if ((kind == RequestKind::Begin || kind == RequestKind::Slot || kind == RequestKind::Skill) && !published.ready) return {false, "变身接口、隔离或输入桥接尚未就绪"};
            if (kind == RequestKind::Begin && published.phase != Phase::Idle) return {false, "请先恢复当前变身"};
            if (kind == RequestKind::Slot && (published.phase != Phase::Active || slot >= 8)) return {false, "当前没有可施放技能的变身角色"};
            if (kind == RequestKind::Skill)
            {
                if (published.phase != Phase::Active || (!id.empty() && id != published.activeId)) return {false, "当前没有可施放此技能的变身角色"};
                if (input && (!features::HotkeysEnabled() || overlay::Visible() || !Foreground())) return {false, "技能按键已暂停"};
                const auto found = skillCache.find(published.activeId);
                if (found == skillCache.end()) return {false, "当前角色技能尚未读取"};
                const auto skill = std::find_if(found->second.begin(), found->second.end(), [&](const auto &value) { return value.id == skillId; });
                if (skill == found->second.end()) return {false, "技能不属于当前变身角色"};
                if (!skill->available) return {false, skill->reason.empty() ? "当前技能不可用" : skill->reason};
                id = published.activeId;
            }
            if ((kind == RequestKind::Begin || kind == RequestKind::Inspect) && std::none_of(catalog.begin(), catalog.end(), [&](const auto &entry) { return entry.id == id; }))
                return {false, "角色标识不在目录中"};
            request = Request{kind, std::string(id), slot, GetTickCount64(), published.generation, skillId, input};
            published.pending = true;
            if (kind == RequestKind::Begin) exclusive = true;
            return {true, kind == RequestKind::Slot || kind == RequestKind::Skill ? "技能已排队；关闭修改器菜单并返回游戏后施放" : "操作已加入游戏线程队列"};
        }
    }

    void Initialize()
    {
        std::call_once(initialization, []
        {
            struct Hint { const char *name, *category, *path; };
            static constexpr Hint hints[] = {
#include "transformation_catalog.inc"
            };
            for (const auto &hint : hints)
            {
                CharacterEntry entry;
                entry.name = hint.name; entry.path = hint.path;
                entry.category = std::string_view(hint.category) == "首领" ? "Boss" : std::string_view(hint.category) == "普通角色" ? "小怪" : hint.category;
                entry.reason = "参考资料条目；尚未读取游戏配置";
                model::MergeCatalog(catalog, std::move(entry));
            }
            LoadSlotsFile();
            LoadBindingsFile();
            LoadCameraFile();
            features::RegisterHotkeyConflictProbe([](unsigned key, unsigned modifiers)
            {
                std::lock_guard lock(mutex);
                for (const auto &[id, values] : bindingConfig)
                    for (const auto &binding : values)
                        if (bindings::Matches(binding, key, modifiers)) return std::string("变身角色技能 ") + std::to_string(binding.skillId);
                return std::string{};
            });
        });
    }
    void Tick(std::uintptr_t player, std::uintptr_t controller, std::uintptr_t world, std::uint64_t generation, bool controlsAvailable)
    {
        Initialize();
        if (!engine::IsGameThread()) return;
        try
        {
            context = {player, controller, world, {Key(player), Key(controller), Key(world), generation}};
            const auto now = GetTickCount64();
            if (!lastProbe || model::Expired(now, lastProbe, 10000)) { lastProbe = now; Probe(); }
            if (features::UpdateRequired())
            {
                if (session.Exclusive()) backend.RecordExit("修改器需要更新");
                session.RequestRestore();
            }
            if (session.State() == Phase::Active && !controlsAvailable)
            {
                message = "冲突功能暂挂失效，正在恢复悟空";
                backend.RecordExit("冲突功能暂挂失效");
                session.RequestRestore();
            }
            if (catalogRequested && context.key.Valid() && !session.Exclusive()) RefreshCatalog();
            std::optional<Request> work;
            {
                std::lock_guard lock(mutex);
                if (request)
                {
                    const bool cancelled = features::UpdateRequired() || request->generation != generation || !context.key.Valid() || model::Expired(now, request->time, 30000) ||
                        (request->input && (!features::HotkeysEnabled() || overlay::Visible() || !Foreground()));
                    const bool allowed = request->kind == RequestKind::Begin ? controlsAvailable : request->kind == RequestKind::Slot || request->kind == RequestKind::Skill ?
                        (session.State() != Phase::Active || backend.cinematicPaused || (!overlay::Visible() && Foreground())) : true;
                    if (cancelled || allowed) { work = std::move(request); request.reset(); }
                    else if (request->kind == RequestKind::Slot || request->kind == RequestKind::Skill) message = "技能已排队；关闭菜单并返回游戏后施放";
                }
            }
            if (work)
            {
                if (work->kind == RequestKind::Restore) session.RequestRestore();
                else if (features::UpdateRequired() || model::Expired(now, work->time, 30000) || work->generation != generation || !context.key.Valid() ||
                    (work->input && (!features::HotkeysEnabled() || overlay::Visible() || !Foreground()))) message = "操作已取消、超时或角色/场景已变化";
                else if (work->kind == RequestKind::Catalog) catalogRequested = true;
                else if (work->kind == RequestKind::Inspect)
                {
                    if (session.Exclusive()) message = "请先恢复当前变身，再检查其他角色";
                    else if (const auto entry = Entry(work->id)) { Prepared info; Prepare(*entry, info); }
                }
                else if (work->kind == RequestKind::Begin)
                {
                    if (const auto entry = Entry(work->id))
                    {
                        if (!session.Begin(*entry, context.key, now)) message = "已有变身会话或角色状态无效";
                        else { backend.exitReason.clear(); backend.sessionStarted = now; }
                    }
                }
                else if (work->kind == RequestKind::Slot && session.State() == Phase::Active)
                {
                    const auto slots = Slots(session.Character().id);
                    if (backend.cinematicPaused) message = "过场期间已暂停变身技能；演出结束后请重新施放";
                    else if (!model::ValidSlots(slots, prepared.skills)) message = "技能槽含失效或不属于当前角色的技能 ID";
                    else if (overlay::Visible() || !Foreground()) message = "请关闭修改器菜单并返回游戏后施放技能";
                    else if (work->slot >= slots.size() || slots[work->slot].skills.empty()) message = "该技能槽尚未配置";
                    else if (!backend.combo.Queue(slots[work->slot].skills, now)) message = "连招正在施放";
                }
                else if (work->kind == RequestKind::Skill && session.State() == Phase::Active)
                {
                    const auto skill = std::find_if(prepared.skills.begin(), prepared.skills.end(), [&](const auto &value) { return value.id == work->skillId; });
                    if (backend.cinematicPaused) message = "过场期间已暂停变身技能；演出结束后请重新施放";
                    else if (work->id != session.Character().id || skill == prepared.skills.end()) message = "技能不属于当前变身角色；请求已取消";
                    else if (!skill->available) message = skill->reason.empty() ? "该技能当前不可用" : skill->reason;
                    else if (!backend.combo.Queue(std::span<const int>(&work->skillId, 1), now)) message = "角色正在施放技能，请稍后再试";
                }
            }
            if (session.Exclusive() && (context.key != session.Context() || !context.key.Valid()))
            {
                backend.cinematicExit = true; // Never move an old performer through the replacement world's context.
                backend.RecordExit("玩家或场景已变化");
            }
            if ((session.State() == Phase::Loading || session.State() == Phase::Initializing) && model::Expired(now, backend.sessionStarted, 15000))
                backend.RecordExit("角色生成或初始化超时");
            session.Tick(backend, context.key, now, controlsAvailable);
            Publish();
        }
        catch (...)
        {
            session.RequestRestore();
            backend.RecordExit("变身接口发生异常");
            message = "变身接口发生异常；正在恢复玩家";
            Publish();
        }
    }
    void ContextLost()
    {
        Initialize();
        {
            std::lock_guard lock(mutex);
            request = Request{RequestKind::Restore, {}, 0, GetTickCount64(), published.generation};
        }
        if (engine::IsGameThread()) Tick(0, 0, 0, context.key.generation + 1, false);
    }
    Status GetStatus() { Initialize(); std::lock_guard lock(mutex); return published; }
    std::vector<CharacterEntry> Catalog() { Initialize(); std::lock_guard lock(mutex); return catalog; }
    features::Result RequestCatalogRefresh() { return Queue(RequestKind::Catalog); }
    features::Result Inspect(std::string_view id) { return Queue(RequestKind::Inspect, id); }
    features::Result Begin(std::string_view id) { return Queue(RequestKind::Begin, id); }
    features::Result Restore() { return Queue(RequestKind::Restore); }
    std::vector<SkillEntry> Skills() { Initialize(); std::lock_guard lock(mutex); return publishedSkills; }
    SkillSnapshot SkillsFor(std::string_view id)
    {
        Initialize(); std::lock_guard lock(mutex);
        const auto found = skillCache.find(std::string(id));
        return found == skillCache.end() ? SkillSnapshot{} : SkillSnapshot{true, found->second};
    }
    std::vector<SkillSlot> Slots(std::string_view id)
    {
        Initialize(); std::lock_guard lock(mutex);
        const auto it = slotConfig.find(std::string(id));
        return it == slotConfig.end() ? EmptySlots() : it->second;
    }
    features::Result SetSlots(std::string_view id, std::vector<SkillSlot> slots)
    {
        Initialize(); std::lock_guard lock(mutex);
        const auto found = skillCache.find(std::string(id));
        if (found == skillCache.end()) return {false, "请先检查该角色的技能目录"};
        if (!model::ValidSlots(slots, found->second)) return {false, "技能槽含无效动作、重复动作或不属于此角色的技能 ID"};
        const auto old = slotConfig.find(std::string(id));
        const auto previous = old == slotConfig.end() ? std::optional<std::vector<SkillSlot>>{} : old->second;
        slotConfig[std::string(id)] = std::move(slots);
        if (!SaveSlotsFile())
        {
            if (previous) slotConfig[std::string(id)] = *previous; else slotConfig.erase(std::string(id));
            return {false, "技能槽配置保存失败"};
        }
        return {true, "技能槽已保存；配置使用实际技能 ID"};
    }
    features::Result TriggerSlot(std::size_t index) { return Queue(RequestKind::Slot, {}, index); }
    std::vector<SkillBinding> Bindings(std::string_view id)
    {
        Initialize(); std::lock_guard lock(mutex);
        const auto found = bindingConfig.find(std::string(id));
        return found == bindingConfig.end() ? std::vector<SkillBinding>{} : found->second;
    }
    features::Result SetBindings(std::string_view id, std::vector<SkillBinding> values)
    {
        Initialize();
        return features::RunHotkeyTransaction([&]() -> features::Result
        {
            std::lock_guard lock(mutex);
            const auto found = skillCache.find(std::string(id));
            if (found == skillCache.end()) return {false, "请先检查该角色的技能目录"};
            if (auto valid = bindings::ValidShape(values); !valid.success) return valid;
            const auto old = bindingConfig.find(std::string(id));
            for (const auto &value : values)
            {
                const bool known = std::any_of(found->second.begin(), found->second.end(), [&](const auto &skill) { return skill.id == value.skillId; });
                if (!known)
                {
                    // A game update may remove a skill. Preserve its identity and
                    // name while allowing its old chord to be released. Callers
                    // may also omit the record, but cannot invent missing IDs.
                    const bool retained = old != bindingConfig.end() && std::any_of(old->second.begin(), old->second.end(), [&](const auto &previous)
                    {
                        return previous.skillId == value.skillId && previous.customName == value.customName &&
                            ((previous.key == value.key && previous.modifiers == value.modifiers) || (!value.key && !value.modifiers));
                    });
                    if (!retained) return {false, "配置含不属于此角色或已经失效的技能 ID"};
                }
                if (auto conflict = features::CheckHotkeyConflict(value.key, value.modifiers); !conflict.success) return conflict;
            }
            auto next = bindingConfig;
            next[std::string(id)] = std::move(values);
            if (!SaveBindingsFile(next)) return {false, "技能名称与按键保存失败；仍使用之前的配置"};
            bindingConfig = std::move(next);
            return {true, "角色技能名称与按键已保存"};
        });
    }
    float CameraDistance(std::string_view id)
    {
        Initialize(); std::lock_guard lock(mutex);
        return camera::Distance(cameraConfig, id);
    }
    features::Result SetCameraDistance(std::string_view id, float meters)
    {
        Initialize(); std::lock_guard lock(mutex);
        if (std::none_of(catalog.begin(), catalog.end(), [&](const auto &entry) { return entry.id == id; })) return {false, "请先选择目录中的角色"};
        if (!camera::Valid(meters)) return {false, "角色视距须为 1 至 200 米，或使用自动视距"};
        auto next = cameraConfig;
        if (meters == 0.f) next.erase(std::string(id)); else next[std::string(id)] = meters;
        if (!SaveCameraFile(next)) return {false, "角色视距保存失败；仍使用之前的配置"};
        cameraConfig = std::move(next);
        return {true, "角色视距已保存；变身中即时生效"};
    }
    features::Result TriggerSkill(int skillId) { return Queue(RequestKind::Skill, {}, 0, skillId); }
    // Used by the input adapter: an input press is cancelled if the menu/focus or
    // hotkey master switch changes before the game-thread callback consumes it.
    features::Result TriggerSkillHotkey(std::string_view characterId, int skillId) { return Queue(RequestKind::Skill, characterId, 0, skillId, true); }
    bool IsActive() { return GetStatus().phase == Phase::Active; }
    bool NeedsExclusiveControl() { return exclusive.load(); }
    const char *SupportName(Support value)
    {
        switch (value) { case Support::Verified: return "已验证"; case Support::Experimental: return "实验性"; case Support::Unavailable: return "不可用"; default: return "未验证"; }
    }
    const char *PhaseName(Phase value)
    {
        switch (value) { case Phase::Loading: return "读取配置"; case Phase::Initializing: return "初始化角色"; case Phase::Active: return "变身中"; case Phase::Restoring: return "恢复悟空"; case Phase::RecoveryRequired: return "正在重试恢复"; default: return "未变身"; }
    }
    const char *ActionName(Action value)
    {
        switch (value) { case Action::LightAttack: return "轻攻击"; case Action::HeavyAttack: return "重攻击"; case Action::Dodge: return "闪避"; case Action::Skill1: return "技能 1"; case Action::Skill2: return "技能 2"; case Action::Skill3: return "技能 3"; case Action::Skill4: return "技能 4"; default: return "特殊技能"; }
    }
}
