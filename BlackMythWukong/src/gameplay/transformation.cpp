#include "transformation.hpp"
#include "transformation_model.hpp"
#include "transformation_presets.hpp"
#include "transformation_bindings.hpp"
#include "transformation_native.hpp"
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
            return Finite(extent) && extent.z > 0.;
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
        bool InitialAdapter(const CharacterEntry &entry)
        {
            const auto path = model::StableId(entry.path);
            return path.find("tamer_mgd_yangjian_01.") != std::string::npos || path.find("tamer_mgd_jsds.") != std::string::npos ||
                   path.find("tamer_gycy_lang_01.") != std::string::npos;
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
                if (!name.starts_with("TAMER_") && !name.starts_with("Tamer_") && !name.starts_with("tamer_")) continue;
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
            auto skills = std::array{Arg::Object("Unit", cdo), Arg::Integers("ReturnValue")};
            if (!Call(L"/Script/b1-Managed.AutoTestHelperLib:GetUnitAllSkillID", skills))
            {
                message = "当前版本未开放角色技能目录接口";
                return false;
            }
            out.skills.clear();
            for (std::size_t i = 0; i + sizeof(int) <= skills[1].bytes.size(); i += sizeof(int))
            {
                int id{};
                std::memcpy(&id, skills[1].bytes.data() + i, sizeof(id));
                if (id > 0 && std::none_of(out.skills.begin(), out.skills.end(), [&](const auto &s) { return s.id == id; }))
                    out.skills.push_back({id, std::format("技能 {}", id), true, "所属角色技能描述存在；施放条件由游戏判断"});
            }
            {
                std::lock_guard lock(mutex);
                const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto &v) { return v.id == entry.id; });
                if (it != catalog.end())
                {
                    it->resourceId = out.resource;
                    it->overrideId = out.overrideId;
                    it->support = out.skills.empty() ? Support::Unavailable : InitialAdapter(entry) ? Support::Experimental : Support::Unverified;
                    it->reason = out.skills.empty() ? "该角色未找到有效技能描述，不能启动变身" : InitialAdapter(entry) ? "通用代理适配；尚需游戏内移动、技能和剧情隔离验证" : "配置可读；该形态尚未完成代理适配";
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
            engine::WeakObject player, controller, world, tamer, proxy, movement, arm, target;
            bool baseline{}, modified{}, hidden{}, collision{}, frozen{}, armChanged{}, ownsImmuneLayer{}, immuneLayerUncertain{};
            std::uint8_t movementMode{}, customMode{};
            float armLength{};
            Vec3 origin{};
            engine::Transform transform;
            int stage{};
            std::uint64_t sessionStarted{};
            model::Combo combo;
            std::string characterId, exitReason;

            void RecordExit(std::string reason)
            {
                if (reason.empty() || !exitReason.empty()) return;
                exitReason = std::move(reason);
                Log("Transformation exit: " + exitReason);
            }

            Progress Load(const CharacterEntry &entry) override
            {
                if (!InitialAdapter(entry)) return Fail("该形态尚未完成代理适配；可先检查技能目录");
                if (!capabilities.isolation || !capabilities.input || !native::Ready()) return Fail("角色隔离或输入桥接尚未通过当前版本校验");
                if (!native::Initialized(context.player)) return Fail("原玩家初始化尚未完成");
                prepared = {};
                if (!Prepare(entry, prepared)) return Fail(message);
                if (prepared.skills.empty()) return Fail("角色缺少有效技能描述");
                characterId = entry.id;
                player = Weak(context.key.player);
                controller = Weak(context.key.controller);
                world = Weak(context.key.world);
                stage = 0;
                return Progress::Complete;
            }
            std::uintptr_t Spawn(std::uintptr_t cls, std::uintptr_t owner)
            {
                auto args = std::array{Arg::Object("WorldContextObject", engine::ResolveWeak(world)), Arg::Object("ActorClass", cls), Arg::TransformValue("SpawnTransform", transform),
                    Arg::Byte("CollisionHandlingOverride", 1), Arg::Object("Owner", owner), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
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
                stage = 2;
                return Progress::Complete;
            }
            Progress Activate() override
            {
                const auto original = engine::ResolveWeak(player), actor = engine::ResolveWeak(proxy);
                if (!original || !actor || stage != 2) return Fail("角色或玩家在激活前失效");
                auto hiddenValue = Arg::Boolean("bHidden", false);
                auto collide = std::array{Arg::Data("ReturnValue", engine::Type::Bool, std::int32_t{}, true)};
                const auto move = Object(original, "CharacterMovement");
                auto mode = Arg::Byte("MovementMode", 0), custom = Arg::Byte("CustomMovementMode", 0);
                auto camera = std::array{Arg::Object("ComponentClass", engine::Find(L"/Script/Engine.SpringArmComponent")), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                if (!camera[0].Get<std::uintptr_t>() || !Call(L"/Script/Engine.Actor:GetComponentByClass", camera, original)) return Fail("无法读取玩家相机组件");
                const auto cameraArm = camera[1].Get<std::uintptr_t>();
                auto length = Arg::Float("TargetArmLength", 0.f);
                Vec3 originalBounds{}, targetBounds{};
                if (!engine::Property(original, hiddenValue) || !Call(L"/Script/Engine.Actor:GetActorEnableCollision", collide, original) ||
                    !move || !engine::Property(move, mode) || !engine::Property(move, custom) ||
                    !cameraArm || !engine::Property(cameraArm, length) || !std::isfinite(length.Get<float>()) || length.Get<float>() <= 0.f ||
                    !Bounds(original, originalBounds) || !Bounds(actor, targetBounds)) return Fail("无法保存玩家或相机恢复状态");
                hidden = hiddenValue.Get<std::int32_t>() != 0;
                collision = collide[0].Get<std::int32_t>() != 0;
                movementMode = mode.Get<std::uint8_t>(); customMode = custom.Get<std::uint8_t>();
                movement = engine::MakeWeak(move);
                arm = engine::MakeWeak(cameraArm);
                armLength = length.Get<float>();
                const auto playerSize = (std::max)({originalBounds.x, originalBounds.y, originalBounds.z});
                const auto targetSize = (std::max)({targetBounds.x, targetBounds.y, targetBounds.z});
                length = Arg::Float("TargetArmLength", armLength * static_cast<float>(std::clamp(targetSize / playerSize, .75, 4.)));
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
                if (!Mode(move, 0, 0) || !Collision(original, false) || !Hidden(original, true) || !Collision(actor, true) || !Hidden(actor, false)) return Fail("变身激活失败，正在恢复悟空");
                frozen = true;
                combo.Reset();
                stage = 3;
                message = "实验性变身已激活；保留角色原始数值";
                return Progress::Complete;
            }
            Progress Update() override
            {
                const auto actor = engine::ResolveWeak(proxy), original = engine::ResolveWeak(player);
                if (!actor || !original || !engine::ResolveWeak(controller)) return Fail("角色或控制器已失效，正在恢复");
                if (native::DeathRequested()) return Fail("变身角色死亡，正在恢复悟空");
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
                auto lockTargetParams = std::array{Arg::Object("Unit", original), Arg::Data("ReturnValue", engine::Type::Object, std::uintptr_t{}, true)};
                if (!Call(L"/Script/b1-Managed.BGUFunctionLibraryCS:GetUnitLockTargetActor", lockTargetParams)) return Fail("目标选择接口失效");
                const auto lockedActor = lockTargetParams[1].Get<std::uintptr_t>();
                const auto selected = lockedActor != actor && lockedActor != original ? engine::MakeWeak(lockedActor) : engine::WeakObject{};
                if (selected.index != target.index || selected.serial != target.serial)
                {
                    if (!native::SetTarget(actor, engine::ResolveWeak(selected))) return Fail("无法转接角色锁定目标");
                    target = selected;
                }
                // Physical locomotion belongs to the target's movement component.
                // Only the hidden original actor follows it as an input/camera anchor.
                if (!overlay::Visible() && Foreground())
                {
                    auto view = std::array{Arg::Rotation("ReturnValue", {}, true)};
                    if (!Call(L"/Script/Engine.Pawn:GetControlRotation", view, original)) return Fail("无法读取玩家视角方向");
                    const auto rotation = view[0].Get<engine::Rotator>();
                    if (!std::isfinite(rotation.yaw)) return Fail("玩家视角方向无效");
                    constexpr double degreesToRadians = .017453292519943295;
                    const auto yaw = rotation.yaw * degreesToRadians;
                    const auto axes = native::MovementAxes();
                    if (!std::isfinite(axes.forward) || !std::isfinite(axes.right)) return Fail("角色移动输入无效");
                    const auto forward = std::clamp(static_cast<double>(axes.forward), -1., 1.);
                    const auto right = std::clamp(static_cast<double>(axes.right), -1., 1.);
                    Vec3 direction{std::cos(yaw) * forward - std::sin(yaw) * right, std::sin(yaw) * forward + std::cos(yaw) * right, 0.};
                    const auto magnitude = std::sqrt(direction.x * direction.x + direction.y * direction.y);
                    if (magnitude > .001)
                    {
                        direction.x /= magnitude; direction.y /= magnitude;
                        auto move = std::array{Arg::Vector("WorldDirection", direction), Arg::Float("ScaleValue", static_cast<float>((std::min)(magnitude, 1.))), Arg::Boolean("bForce", true)};
                        if (!Call(L"/Script/Engine.Pawn:AddMovementInput", move, actor)) return Fail("角色移动接口失效");
                    }
                    else direction = {std::cos(yaw), std::sin(yaw), 0.};
                    if (casting[1].Get<int>() <= 0 && !engine::ResolveWeak(target) && !Face(actor, direction)) return Fail("角色朝向接口失效");
                    if (const auto selectedActor = engine::ResolveWeak(target); selectedActor && casting[1].Get<int>() <= 0)
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
                else { native::ConsumeActions(); combo.Reset(); }
                if (const auto next = combo.Next(casting[1].Get<int>(), GetTickCount64()))
                {
                    auto use = std::array{Arg::Object("Unit", actor), Arg::Int("SkillID", *next), Arg::Enum("SourceType", 0), Arg::Data("ReturnValue", engine::Type::Int, int{}, true)};
                    const bool accepted = Call(L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIUseSkillWithSkillID", use) && use[3].Get<int>() > 0;
                    combo.Result(accepted, GetTickCount64());
                    if (!accepted) message = std::format("游戏拒绝技能 {}；检查当前状态、目标和冷却", *next);
                }
                if (combo.Failed()) { message = "连招已停止：技能被拒绝或施放状态超时"; combo.Reset(); }
                return Progress::Complete;
            }
            Progress Restore() override
            {
                combo.Reset();
                native::SetInputOwner({});
                if (const auto actor = engine::ResolveWeak(proxy))
                {
                    // Restoration may need several frames. Stop exposing the
                    // proxy to combat while retained baselines are retried.
                    Hidden(actor, true);
                    Collision(actor, false);
                    if (const auto move = Object(actor, "CharacterMovement")) Mode(move, 0, 0);
                }
                bool ok = true;
                if (baseline)
                {
                    if (const auto original = engine::ResolveWeak(player))
                    {
                        auto position = origin;
                        if (const auto actor = engine::ResolveWeak(proxy)) { Vec3 p{}; if (Location(actor, p)) position = p; }
                        ok &= SetLocation(original, position);
                        ok &= Hidden(original, hidden);
                        ok &= Collision(original, collision);
                        if (const auto move = engine::ResolveWeak(movement)) ok &= Mode(move, movementMode, customMode);
                    }
                    else ownsImmuneLayer = immuneLayerUncertain = false;
                    if (armChanged)
                    {
                        if (const auto cameraArm = engine::ResolveWeak(arm))
                        {
                            auto length = Arg::Float("TargetArmLength", armLength);
                            ok &= engine::Property(cameraArm, length, true);
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
                const auto destroy = [&](engine::WeakObject &weak)
                {
                    const auto actor = engine::ResolveWeak(weak);
                    if (!actor) { weak = {}; return true; }
                    std::optional<native::ScopedCleanup> cleanup;
                    if (weak.index == proxy.index && weak.serial == proxy.serial)
                    {
                        cleanup.emplace(actor);
                        if (!*cleanup) { RecordExit("临时角色清理隔离身份失效"); return false; }
                    }
                    if (!Empty(L"/Script/Engine.Actor:K2_DestroyActor", actor)) { RecordExit("临时角色清理请求失败"); return false; }
                    // A successful ProcessEvent only confirms the request ran.
                    // Keep ownership until the weak handle stops resolving.
                    if (engine::ResolveWeak(weak)) return false;
                    weak = {};
                    return true;
                };
                if (!destroy(proxy) || !destroy(tamer)) { message = "临时角色清理失败；正在重试"; return Progress::Failed; }
                native::ClearProxy();
                native::SetInputOwner({});
                stage = 0;
                target = {};
                characterId.clear();
                message = exitReason.empty() ? "已恢复悟空" : "已恢复悟空：" + exitReason;
                return Progress::Complete;
            }
            Progress Fail(std::string text) { RecordExit(text); message = std::move(text); return Progress::Failed; }
        } backend;

        void Probe()
        {
            native::Initialize();
            const auto has = [](const wchar_t *path) { return engine::Find(path) != 0; };
            capabilities.spawn = has(L"/Script/b1-Managed.BUTamerActor") && has(L"/Script/b1.BGUFunctionLibrary:BGUBeginDeferredActorSpawnFromClass") &&
                has(L"/Script/b1.BGUFunctionLibrary:BGUFinishSpawningActor") && has(L"/Script/b1.BGUCharacter:SetTamerOwner");
            capabilities.skills = has(L"/Script/b1-Managed.AutoTestHelperLib:GetUnitAllSkillID") && has(L"/Script/b1-Managed.BGUFuncLibSkillCS:BGUGetCastingSkillID") && has(L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIUseSkillWithSkillID");
            capabilities.movement = has(L"/Script/Engine.Pawn:AddMovementInput") && has(L"/Script/Engine.Pawn:GetControlRotation") &&
                has(L"/Script/UnrealExtent.GSE_ActorFuncLib:SetActorRotationSimple") && has(L"/Script/b1-Managed.BGUFunctionLibraryCS:GetUnitLockTargetActor");
            capabilities.recovery = engine::WeakReady() && has(L"/Script/Engine.Actor:K2_DestroyActor") && has(L"/Script/Engine.Actor:SetActorHiddenInGame") && has(L"/Script/Engine.CharacterMovementComponent:SetMovementMode") &&
                has(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUHasUnitSimpleState") && has(L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUSetUnitSimpleState");
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
                        (session.State() != Phase::Active || (!overlay::Visible() && Foreground())) : true;
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
                    if (!model::ValidSlots(slots, prepared.skills)) message = "技能槽含失效或不属于当前角色的技能 ID";
                    else if (overlay::Visible() || !Foreground()) message = "请关闭修改器菜单并返回游戏后施放技能";
                    else if (work->slot >= slots.size() || slots[work->slot].skills.empty()) message = "该技能槽尚未配置";
                    else if (!backend.combo.Queue(slots[work->slot].skills, now)) message = "连招正在施放";
                }
                else if (work->kind == RequestKind::Skill && session.State() == Phase::Active)
                {
                    const auto skill = std::find_if(prepared.skills.begin(), prepared.skills.end(), [&](const auto &value) { return value.id == work->skillId; });
                    if (work->id != session.Character().id || skill == prepared.skills.end()) message = "技能不属于当前变身角色；请求已取消";
                    else if (!skill->available) message = skill->reason.empty() ? "该技能当前不可用" : skill->reason;
                    else if (!backend.combo.Queue(std::span<const int>(&work->skillId, 1), now)) message = "角色正在施放技能，请稍后再试";
                }
            }
            if (session.Exclusive() && (context.key != session.Context() || !context.key.Valid())) backend.RecordExit("玩家或场景已变化");
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
