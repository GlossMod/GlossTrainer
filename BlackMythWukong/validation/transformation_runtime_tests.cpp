#include "../src/gameplay/engine_bridge.hpp"
#include "../src/gameplay/transformation_native.hpp"
#include "../src/runtime.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Compile the production transformation driver/backend into this test. Only the
// reflected engine boundary, native adapter and process services are mocked.
static ULONGLONG TransformationTestTickCount();
static HWND WINAPI TransformationTestGetForegroundWindow();
static DWORD WINAPI TransformationTestGetWindowThreadProcessId(HWND, DWORD *);
#define GetTickCount64 TransformationTestTickCount
#define GetForegroundWindow TransformationTestGetForegroundWindow
#define GetWindowThreadProcessId TransformationTestGetWindowThreadProcessId
#include "../src/gameplay/transformation.cpp"
#undef GetTickCount64
#undef GetForegroundWindow
#undef GetWindowThreadProcessId

namespace eng = gloss::gameplay::engine;
namespace trans = gloss::gameplay::transformation;
namespace native = gloss::gameplay::transformation::native;
namespace binding_model = gloss::gameplay::transformation::bindings;

namespace fixture
{
    using Vec3 = gloss::gameplay::extensions::Vec3;
    constexpr std::uintptr_t Player = 0x1000;
    constexpr std::uintptr_t Controller = 0x2000;
    constexpr std::uintptr_t World = 0x3000;
    constexpr std::uintptr_t OtherWorld = 0x3010;
    constexpr std::uintptr_t Movement = 0x4000;
    constexpr std::uintptr_t SpringArm = 0x5000;
    constexpr std::uintptr_t TamerClass = 0x6000;
    constexpr std::uintptr_t TamerCDO = 0x6001;
    constexpr std::uintptr_t MonsterClass = 0x6100;
    constexpr std::uintptr_t MonsterCDO = 0x6101;
    constexpr std::uintptr_t PlayerConfig = 0x6200;
    constexpr std::uintptr_t CDOConfig = 0x6201;
    constexpr std::uintptr_t CDOExtend = 0x6202;
    constexpr std::uintptr_t CDOCharacterMovement = 0x6203;
    constexpr std::uintptr_t ConfigExtend = 0x6300;
    constexpr std::uintptr_t ConfigUnit = 0x6301;
    constexpr std::uintptr_t ProxyExtend = 0x6400;
    constexpr std::uintptr_t ProxyMovement = 0x6401;

    struct Object
    {
        std::int32_t serial{1};
        std::uintptr_t klass{};
        bool alive{true};
        std::map<std::string, eng::Arg> properties;
        std::map<std::string, bool> collision;
        std::map<std::string, gloss::gameplay::extensions::Vec3> locations;
        std::map<std::string, gloss::gameplay::extensions::Vec3> bounds;
    };
    std::unordered_map<std::uintptr_t, Object> objects;
    std::unordered_map<std::uintptr_t, int> immuneLayers;
    std::uintptr_t nextActor{0x10000};
    std::uint64_t now{1000};
    int spawnCount{};
    int skillRequests{};
    int failures{};
    std::vector<int> availableSkillIds{41001, 41002, 41003};
    bool hotkeysEnabled{true};
    bool blockChordConflict{};
    bool runtimeInitialized{};
    int failCollisionEnable{};
    int failCollisionDisable{};
    int failHiddenRestore{};
    int failModeRestore{};
    int failImmuneRemove{};
    int indeterminateImmuneAdd{}, indeterminateImmuneRemove{};
    int immuneAddCalls{}, immuneRemoveCalls{};
    int hiddenWrites{}, collisionWrites{}, modeWrites{}, armWrites{};
    int playerBaselineWrites{};
    int cleanupConstructs{}, cleanupDestructs{}, guardedProxyDestroyCalls{};
    bool cleanupScopeActive{};
    bool overlayVisible{}, foreground{true};

#define CHECK(...)                                                                                         \
    do                                                                                                     \
    {                                                                                                      \
        if (!(__VA_ARGS__))                                                                                \
        {                                                                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #__VA_ARGS__ "\n";              \
            ++fixture::failures;                                                                           \
        }                                                                                                  \
    } while (false)

    void Add(std::uintptr_t p, std::uintptr_t klass = 0)
    {
        Object value;
        value.klass = klass;
        objects.insert_or_assign(p, std::move(value));
    }
    bool Alive(std::uintptr_t p)
    {
        const auto it = objects.find(p);
        return it != objects.end() && it->second.alive;
    }
    void Put(std::uintptr_t p, eng::Arg value)
    {
        objects.at(p).properties.insert_or_assign(value.name, std::move(value));
    }
    void SeedExtension(std::uintptr_t p)
    {
        Put(p, eng::Arg::Int("BehaviorGraphTemplateID", 1));
        Put(p, eng::Arg::Int("DialogueGraphTemplateID", 1));
        Put(p, eng::Arg::Int("InteractiveUnitID", 1));
        Put(p, eng::Arg::Int("DefalutInteractiveGroupID", 1));
        Put(p, eng::Arg::Int("ShopID", 1));
        Put(p, eng::Arg::Int("RewardDropID", 1));
        Put(p, eng::Arg::Object("StateGraphTemplate", 0x7000));
        Put(p, eng::Arg::Object("PhaseStateGraphTemplate", 0x7001));
        Put(p, eng::Arg::Object("BattleSC", 0x7002));
        Put(p, eng::Arg::Boolean("bCanAsPerformer", true));
        Put(p, eng::Arg::Boolean("bCanAsNPC", true));
        Put(p, eng::Arg::Boolean("bCanAsFollowPartner", true));
    }
    template <class T> T Get(std::uintptr_t p, const char *name)
    {
        return objects.at(p).properties.at(name).Get<T>();
    }
    eng::Arg *PropertyValue(std::uintptr_t p, const char *name)
    {
        const auto it = objects.find(p);
        if (it == objects.end()) return nullptr;
        const auto prop = it->second.properties.find(name);
        return prop == it->second.properties.end() ? nullptr : &prop->second;
    }
    eng::Arg *Named(std::span<eng::Arg> args, const char *name)
    {
        const auto it = std::find_if(args.begin(), args.end(), [&](const auto &arg) { return std::string_view(arg.name) == name; });
        return it == args.end() ? nullptr : &*it;
    }
    template <class T> void Output(std::span<eng::Arg> args, const char *name, T value)
    {
        auto *out = Named(args, name);
        if (!out) throw std::runtime_error(std::string("missing mock output: ") + name);
        out->bytes.resize(sizeof(T));
        std::memcpy(out->bytes.data(), &value, sizeof(T));
    }
    void InitializeWorld()
    {
        objects.clear();
        immuneLayers.clear();
        nextActor = 0x10000;
        spawnCount = hiddenWrites = collisionWrites = modeWrites = armWrites = 0;
        skillRequests = playerBaselineWrites = 0;
        availableSkillIds = {41001, 41002, 41003};
        hotkeysEnabled = true;
        blockChordConflict = false;
        cleanupConstructs = cleanupDestructs = guardedProxyDestroyCalls = 0;
        cleanupScopeActive = false;
        overlayVisible = false;
        foreground = true;
        failCollisionEnable = failCollisionDisable = failHiddenRestore = failModeRestore = 0;
        failImmuneRemove = 0;
        indeterminateImmuneAdd = indeterminateImmuneRemove = 0;
        immuneAddCalls = immuneRemoveCalls = 0;
        Add(Player);
        Add(Controller);
        Add(World);
        Add(OtherWorld);
        Add(Movement);
        Add(SpringArm);
        Add(TamerClass);
        Add(TamerCDO);
        Add(MonsterClass);
        Add(MonsterCDO);
        Add(PlayerConfig);
        Add(CDOConfig);
        Add(CDOExtend);
        Add(CDOCharacterMovement);
        Put(Player, eng::Arg::Object("CharacterMovement", Movement));
        Put(Player, eng::Arg::Boolean("bHidden", false));
        Put(Movement, eng::Arg::Byte("MovementMode", 3));
        Put(Movement, eng::Arg::Byte("CustomMovementMode", 0));
        Put(SpringArm, eng::Arg::Float("TargetArmLength", 350.f));
        Put(TamerCDO, eng::Arg::Object("ConfigInfoComp", CDOConfig));
        Put(TamerCDO, eng::Arg::Object("MonsterClassObj", MonsterClass));
        Put(MonsterCDO, eng::Arg::Object("ExtendConfigComp", CDOExtend));
        Put(MonsterCDO, eng::Arg::Object("CharacterMovement", CDOCharacterMovement));
        SeedExtension(CDOExtend);
        Put(CDOCharacterMovement, eng::Arg::Float("MaxWalkSpeed", 600.f));
        Put(CDOConfig, eng::Arg::Int("ResID", 830001));
        Put(CDOConfig, eng::Arg::Int("OverrideID", 0));
        Put(CDOExtend, eng::Arg::Int("BehaviorGraphTemplateID", 77));
        Put(CDOExtend, eng::Arg::Int("DialogueGraphTemplateID", 78));
        Put(CDOExtend, eng::Arg::Int("InteractiveUnitID", 79));
        Put(CDOExtend, eng::Arg::Int("DefalutInteractiveGroupID", 80));
        Put(CDOExtend, eng::Arg::Int("ShopID", 81));
        Put(CDOExtend, eng::Arg::Int("RewardDropID", 82));
        Put(CDOExtend, eng::Arg::Object("StateGraphTemplate", 0x7000));
        Put(CDOExtend, eng::Arg::Object("PhaseStateGraphTemplate", 0x7001));
        Put(CDOExtend, eng::Arg::Object("BattleSC", 0x7002));
        Put(CDOExtend, eng::Arg::Boolean("bCanAsPerformer", true));
        Put(CDOExtend, eng::Arg::Boolean("bCanAsNPC", true));
        Put(CDOExtend, eng::Arg::Boolean("bCanAsFollowPartner", true));
        objects[Player].collision["enabled"] = true;
        objects[Player].locations["actor"] = {};
        objects[Player].bounds["extent"] = {50., 50., 90.};
        objects[SpringArm].bounds["extent"] = {};
        if (!runtimeInitialized)
        {
            std::error_code error;
            std::filesystem::remove_all(gloss::DataDirectory(), error);
        }
        std::filesystem::create_directories(gloss::DataDirectory());
        now += 100;
    }
    void Tick(std::uint64_t step = 1)
    {
        now += step;
        trans::Tick(Player, Controller, World, 1, true);
    }
    std::uintptr_t Proxy()
    {
        for (const auto &[address, object] : objects)
            if (object.klass == MonsterClass && address >= 0x10000) return address;
        return 0;
    }
    std::uintptr_t Tamer()
    {
        for (const auto &[address, object] : objects)
            if (object.klass == TamerClass && address >= 0x10000) return address;
        return 0;
    }
    void StartAndReachActivation()
    {
        trans::Initialize();
        runtimeInitialized = true;
        Tick(); // Publish the game context and adapter capability probe.
        const auto catalog = trans::Catalog();
        const auto entry = std::find_if(catalog.begin(), catalog.end(), [](const auto &item)
        {
            return item.path.find("TAMER_mgd_yangjian_01") != std::string::npos;
        });
        CHECK(entry != catalog.end());
        if (entry == catalog.end()) return;
        const auto queued = trans::Begin(entry->id);
        CHECK(queued.success);
        Tick(); // Load and validate the selected Tamer / skill configuration.
        Tick(); // Spawn deferred Tamer and proxy.
        Tick(); // Initialize the proxy and attempt activation.
        CHECK(Proxy() != 0);
        CHECK(Tamer() != 0);
    }
}

static ULONGLONG TransformationTestTickCount() { return fixture::now; }
static HWND WINAPI TransformationTestGetForegroundWindow()
{
    return fixture::foreground ? reinterpret_cast<HWND>(1) : nullptr;
}
static DWORD WINAPI TransformationTestGetWindowThreadProcessId(HWND, DWORD *process)
{
    if (process) *process = fixture::foreground ? GetCurrentProcessId() : GetCurrentProcessId() + 1;
    return 1;
}

namespace gloss::gameplay::engine
{
    bool Initialize(HMODULE) { return true; }
    bool Ready() { return true; }
    bool IsGameThread() noexcept { return true; }
    bool RegisterFrame(void (*)()) noexcept { return true; }
    bool IsPawn(std::uintptr_t object) noexcept { return fixture::Alive(object) && object == fixture::Player; }
    bool IsActor(std::uintptr_t object) noexcept { return fixture::Alive(object); }
    bool IsA(std::uintptr_t object, const wchar_t *unrealClass) noexcept
    {
        if (!fixture::Alive(object)) return false;
        const std::wstring_view type(unrealClass);
        if (type == L"/Script/b1-Managed.BUTamerActor") return object == fixture::TamerCDO || fixture::objects[object].klass == fixture::TamerClass;
        if (type == L"/Script/b1-Managed.BGU_CharacterAI") return object == fixture::MonsterCDO;
        if (type == L"/Script/b1-Managed.BGUCharacterCS") return object >= 0x10000 && fixture::objects[object].klass == fixture::MonsterClass;
        if (type == L"/Script/CoreUObject.Object") return fixture::Alive(object);
        return true;
    }
    bool WeakReady() { return true; }
    WeakObject MakeWeak(std::uintptr_t object)
    {
        if (!fixture::Alive(object) || object > static_cast<std::uintptr_t>(INT32_MAX)) return {};
        return {static_cast<std::int32_t>(object), fixture::objects[object].serial};
    }
    std::uintptr_t ResolveWeak(WeakObject weak)
    {
        if (weak.index < 0) return 0;
        const auto object = static_cast<std::uintptr_t>(weak.index);
        return fixture::Alive(object) && fixture::objects[object].serial == weak.serial ? object : 0;
    }
    std::uintptr_t Find(std::wstring_view path)
    {
        if (path == L"/Script/b1-Managed.BUTamerActor") return fixture::TamerClass;
        if (path == L"/Script/Engine.SpringArmComponent") return fixture::SpringArm;
        if (path == L"/Script/CoreUObject.Class") return 0x7100;
        return 0x7200; // All required reflected functions/classes exist in the fixture.
    }
    std::uintptr_t Load(std::wstring_view, std::wstring_view)
    {
        return fixture::TamerClass;
    }
    std::uintptr_t Construct(std::uintptr_t, std::uintptr_t) { return 0; }
    std::uintptr_t DefaultObject(std::uintptr_t klass)
    {
        if (klass == fixture::TamerClass) return fixture::TamerCDO;
        if (klass == fixture::MonsterClass) return fixture::MonsterCDO;
        return 0;
    }
    bool SoftClassPath(std::uintptr_t, const char *, std::wstring &) { return false; }
    std::uintptr_t World(std::uintptr_t) { return fixture::World; }
    bool ConsoleExists(std::wstring_view) { return false; }
    CallResult ExecuteConsole(std::wstring_view, std::uintptr_t) { return {false, "unused"}; }
    CallResult Call(std::wstring_view function, std::span<Arg> args, std::uintptr_t instance)
    {
        using namespace fixture;
        if (function.ends_with(L":GetAssetRegistry")) return {false, "fixture does not model Asset Registry"};
        if (function.ends_with(L":GetActorLocationSimple"))
        {
            const auto actor = Named(args, "Actor")->Get<std::uintptr_t>();
            Output(args, "OutPos", objects[actor].locations["actor"]);
            return {true, {}};
        }
        if (function.ends_with(L":SetActorLocationSimple"))
        {
            const auto actor = Named(args, "Actor")->Get<std::uintptr_t>();
            objects[actor].locations["actor"] = Named(args, "Pos")->Get<Vec3>();
            return {true, {}};
        }
        if (function.ends_with(L":SetActorHiddenInGame"))
        {
            const bool hidden = Named(args, "bNewHidden")->Get<std::int32_t>() != 0;
            if (instance == Player && !hidden && failHiddenRestore > 0)
            {
                --failHiddenRestore;
                return {false, "injected hidden restore failure"};
            }
            Put(instance, Arg::Boolean("bHidden", hidden));
            ++hiddenWrites;
            if (instance == Player) ++playerBaselineWrites;
            return {true, {}};
        }
        if (function.ends_with(L":SetActorEnableCollision"))
        {
            const bool enabled = Named(args, "bNewActorEnableCollision")->Get<std::int32_t>() != 0;
            if (instance >= 0x10000 && enabled && failCollisionEnable > 0)
            {
                --failCollisionEnable;
                return {false, "injected activation proxy-collision failure"};
            }
            if (instance == Player && !enabled && failCollisionDisable > 0)
            {
                --failCollisionDisable;
                return {false, "injected activation collision failure"};
            }
            objects[instance].collision["enabled"] = enabled;
            ++collisionWrites;
            if (instance == Player) ++playerBaselineWrites;
            return {true, {}};
        }
        if (function.ends_with(L":GetActorEnableCollision"))
        {
            Output(args, "ReturnValue", std::int32_t(objects[instance].collision["enabled"]));
            return {true, {}};
        }
        if (function.ends_with(L":GetActorBounds"))
        {
            Output(args, "Origin", Vec3{});
            const auto extent = objects[instance].bounds.contains("extent") ? objects[instance].bounds["extent"] : Vec3{60., 60., 100.};
            Output(args, "BoxExtent", extent);
            return {true, {}};
        }
        if (function.ends_with(L":GetComponentByClass"))
        {
            Output(args, "ReturnValue", std::uintptr_t(SpringArm));
            return {true, {}};
        }
        if (function.ends_with(L":SetMovementMode"))
        {
            const auto newMode = Named(args, "NewMovementMode")->Get<std::uint8_t>();
            if (instance == Movement && newMode == 3 && failModeRestore > 0)
            {
                --failModeRestore;
                return {false, "injected movement restore failure"};
            }
            if (auto *mode = PropertyValue(instance, "MovementMode")) mode->bytes = Named(args, "NewMovementMode")->bytes;
            if (auto *custom = PropertyValue(instance, "CustomMovementMode")) custom->bytes = Named(args, "NewCustomMode")->bytes;
            ++modeWrites;
            if (instance == Movement) ++playerBaselineWrites;
            return {true, {}};
        }
        if (function.ends_with(L":GetTransform"))
        {
            Output(args, "ReturnValue", Transform{});
            return {true, {}};
        }
        if (function.ends_with(L":BGUBeginDeferredActorSpawnFromClass"))
        {
            const auto klass = Named(args, "ActorClass")->Get<std::uintptr_t>();
            const auto actor = nextActor++;
            Add(actor, klass);
            if (klass == TamerClass)
            {
                Put(actor, Arg::Object("ConfigInfoComp", ConfigUnit));
                Put(actor, Arg::Object("ExtendConfigComp", ConfigExtend));
                Put(actor, Arg::Object("MonsterClassObj", 0));
                Put(actor, Arg::Boolean("bHidden", false));
                Add(ConfigUnit);
                Add(ConfigExtend);
                SeedExtension(ConfigExtend);
            }
            else
            {
                Put(actor, Arg::Object("ExtendConfigComp", ProxyExtend));
                Put(actor, Arg::Object("CharacterMovement", ProxyMovement));
                Put(actor, Arg::Boolean("bHidden", false));
                Add(ProxyExtend);
                Add(ProxyMovement);
                SeedExtension(ProxyExtend);
                Put(ProxyMovement, Arg::Boolean("bRunPhysicsWithNoController", false));
                objects[actor].collision["enabled"] = true;
                objects[actor].locations["actor"] = {};
                objects[actor].bounds["extent"] = {100., 100., 180.};
            }
            ++spawnCount;
            Output(args, "ReturnValue", actor);
            return {true, {}};
        }
        if (function.ends_with(L":BGUFinishSpawningActor"))
        {
            Output(args, "ReturnValue", Named(args, "Actor")->Get<std::uintptr_t>());
            return {true, {}};
        }
        if (function.ends_with(L":BGUGetFloatAttr"))
        {
            Output(args, "ReturnValue", 500.f);
            return {true, {}};
        }
        if (function.ends_with(L":BGUSetBehaviorTree") || function.ends_with(L":SetTamerOwner") ||
            function.ends_with(L":AddMovementInput") || function.ends_with(L":SetActorRotationSimple")) return {true, {}};
        if (function.ends_with(L":GetLastMovementInputVector"))
        {
            Output(args, "ReturnValue", Vec3{});
            return {true, {}};
        }
        if (function.ends_with(L":GetControlRotation"))
        {
            Output(args, "ReturnValue", eng::Rotator{});
            return {true, {}};
        }
        if (function.ends_with(L":BGUGetCastingSkillID"))
        {
            Output(args, "ReturnValue", int{});
            return {true, {}};
        }
        if (function.ends_with(L":BGUHasUnitSimpleState"))
        {
            const auto unit = Named(args, "Unit")->Get<std::uintptr_t>();
            const auto count = immuneLayers[unit];
            Output(args, "ReturnValue", std::int32_t(count > 0));
            return {true, {}};
        }
        if (function.ends_with(L":BGUSetUnitSimpleState"))
        {
            const auto unit = Named(args, "Unit")->Get<std::uintptr_t>();
            CHECK(Named(args, "SimpleState")->Get<std::uint8_t>() == 1);
            const bool remove = Named(args, "IsRemove")->Get<std::int32_t>() != 0;
            if (remove)
            {
                ++immuneRemoveCalls;
                if (indeterminateImmuneRemove > 0)
                {
                    --indeterminateImmuneRemove;
                    immuneLayers[unit] = (std::max)(0, immuneLayers[unit] - 1); // Event took effect before throwing.
                    return {false, "injected indeterminate immunity removal", eng::CallState::Indeterminate};
                }
                if (failImmuneRemove > 0)
                {
                    --failImmuneRemove;
                    return {false, "injected immunity restore failure"}; // NotCalled: safe to retry.
                }
                immuneLayers[unit] = (std::max)(0, immuneLayers[unit] - 1);
            }
            else
            {
                ++immuneAddCalls;
                ++immuneLayers[unit];
                if (indeterminateImmuneAdd > 0)
                {
                    --indeterminateImmuneAdd;
                    return {false, "injected indeterminate immunity addition", eng::CallState::Indeterminate};
                }
            }
            return {true, {}, eng::CallState::Completed};
        }
        if (function.ends_with(L":GetUnitLockTargetActor"))
        {
            Output(args, "ReturnValue", std::uintptr_t{});
            return {true, {}};
        }
        if (function.ends_with(L":BGURequestAIUseSkillWithSkillID"))
        {
            ++skillRequests;
            Output(args, "ReturnValue", Named(args, "SkillID")->Get<int>());
            return {true, {}};
        }
        if (function.ends_with(L":GetUnitAllSkillID"))
        {
            auto *out = Named(args, "ReturnValue");
            out->bytes.resize(availableSkillIds.size() * sizeof(int));
            if (!availableSkillIds.empty()) std::memcpy(out->bytes.data(), availableSkillIds.data(), out->bytes.size());
            return {true, {}};
        }
        if (function.ends_with(L":K2_DestroyActor"))
        {
            if (instance == Proxy() && cleanupScopeActive) ++guardedProxyDestroyCalls;
            objects[instance].alive = false;
            return {true, {}};
        }
        return {false, "unhandled mock function"};
    }
    bool Property(std::uintptr_t object, Arg &value, bool write)
    {
        if (!fixture::Alive(object)) return false;
        auto *property = fixture::PropertyValue(object, value.name);
        if (!property) return false;
        if (write)
        {
            *property = value;
            if (object == fixture::SpringArm && std::string_view(value.name) == "TargetArmLength")
            {
                ++fixture::armWrites;
                ++fixture::playerBaselineWrites;
            }
        }
        else value = *property;
        return true;
    }
    bool PropertyPath(std::uintptr_t object, std::span<const std::string_view>, Arg &value, bool write)
    {
        if (object != fixture::CDOConfig && object != fixture::ConfigUnit) return false;
        if (write) return true;
        if (std::string_view(value.name) == "ResID") value = Arg::Int("ResID", 830001);
        else if (std::string_view(value.name) == "OverrideID") value = Arg::Int("OverrideID", 0);
        else return false;
        return true;
    }
    bool CopyStructProperty(std::uintptr_t source, std::uintptr_t destination, const char *name)
    {
        return fixture::Alive(source) && fixture::Alive(destination) &&
            (std::string_view(name) == "UnitCDesc" || std::string_view(name) == "UnitPhysicsDesc" || std::string_view(name) == "FlagConfig");
    }
    std::string ObjectName(std::uintptr_t) { return "fixture"; }
}

namespace gloss::gameplay::transformation::native
{
    ScopedCleanup::ScopedCleanup(std::uintptr_t actor)
    {
        ++fixture::cleanupConstructs;
        active_ = actor != 0 && actor == fixture::Proxy() && fixture::Alive(actor) && !fixture::cleanupScopeActive;
        if (active_) fixture::cleanupScopeActive = true;
    }
    ScopedCleanup::~ScopedCleanup()
    {
        if (active_)
        {
            fixture::cleanupScopeActive = false;
            ++fixture::cleanupDestructs;
        }
    }
    void Initialize() {}
    bool Ready() { return true; }
    bool SetProxy(engine::WeakObject) { return true; }
    void ClearProxy() {}
    bool SetInputOwner(engine::WeakObject) { return true; }
    std::vector<Action> ConsumeActions() { return {}; }
    MoveInput MovementAxes() { return {}; }
    bool DeathRequested() { return false; }
    bool SetTamerType(std::uintptr_t, int) { return true; }
    bool Initialized(std::uintptr_t) { return true; }
    bool CopyTeam(std::uintptr_t, std::uintptr_t) { return true; }
    bool SetTarget(std::uintptr_t, std::uintptr_t) { return true; }
}

namespace gloss::features
{
    bool UpdateRequired() { return false; }
    bool HotkeysEnabled() { return fixture::hotkeysEnabled; }
    Result CheckHotkeyConflict(unsigned key, unsigned modifiers)
    {
        if (fixture::blockChordConflict && key == 'P' && modifiers == Ctrl)
            return {false, "injected feature hotkey conflict"};
        return {true, {}};
    }
    void RegisterHotkeyConflictProbe(std::function<std::string(unsigned, unsigned)>) {}
    Result RunHotkeyTransaction(std::function<Result()> operation) { return operation(); }
}
namespace gloss
{
    void Log(std::string_view) {}
    const std::filesystem::path &DataDirectory()
    {
        static const std::filesystem::path path = std::filesystem::temp_directory_path() / L"GlossTrainerTransformationRuntimeTests";
        return path;
    }
    namespace overlay { bool Visible() { return fixture::overlayVisible; } }
}

namespace fixture
{
    void ActivationFailureRestoresPartialChanges()
    {
        InitializeWorld();
        failCollisionEnable = 1;
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        CHECK(immuneLayers[Player] == 1);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 1);
        CHECK(!objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 0);
        CHECK(Get<float>(SpringArm, "TargetArmLength") != 350.f);
        CHECK(objects[Proxy()].alive);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 350.f);
        CHECK(!Alive(Proxy()));
        CHECK(!Alive(Tamer()));
        CHECK(immuneLayers[Player] == 0);
        CHECK(cleanupConstructs == 1 && cleanupDestructs == 1 && guardedProxyDestroyCalls == 1);
        CHECK(!cleanupScopeActive);
        std::cout << "PASS production backend rolls back an activation failure after partial changes\n";
    }

    void FailedRestoreRetainsBaselineAndRetries()
    {
        InitializeWorld();
        failCollisionEnable = 1;
        failHiddenRestore = 1;
        failModeRestore = 1;
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        CHECK(immuneLayers[Player] == 1);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::RecoveryRequired);
        CHECK(immuneLayers[Player] == 1);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 1);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 0);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 350.f);
        CHECK(Alive(Proxy()) && Alive(Tamer()));
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 350.f);
        CHECK(!Alive(Proxy()) && !Alive(Tamer()));
        CHECK(immuneLayers[Player] == 0);
        CHECK(cleanupConstructs == 1 && cleanupDestructs == 1 && guardedProxyDestroyCalls == 1);
        CHECK(!cleanupScopeActive);
        std::cout << "PASS production backend retains recovery ownership until baseline restoration succeeds\n";
    }

    void ReusedPlayerAddressDoesNotReceiveStaleWrites()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto oldProxy = Proxy(), oldTamer = Tamer();
        CHECK(oldProxy && oldTamer);
        objects[Player].serial++;
        immuneLayers[Player] = 3; // Model a different pawn that reuses this object slot.
        objects[Movement].serial++;
        objects[SpringArm].serial++;
        Put(Player, eng::Arg::Boolean("bHidden", true));
        objects[Player].collision["enabled"] = false;
        Put(Movement, eng::Arg::Byte("MovementMode", 7));
        Put(Movement, eng::Arg::Byte("CustomMovementMode", 4));
        Put(SpringArm, eng::Arg::Float("TargetArmLength", 777.f));
        const auto writesBefore = playerBaselineWrites;
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 1);
        CHECK(!objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 7);
        CHECK(Get<std::uint8_t>(Movement, "CustomMovementMode") == 4);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 777.f);
        CHECK(playerBaselineWrites == writesBefore);
        CHECK(!Alive(oldProxy) && !Alive(oldTamer));
        CHECK(immuneLayers[Player] == 3);
        CHECK(cleanupConstructs == 1 && cleanupDestructs == 1 && guardedProxyDestroyCalls == 1);
        CHECK(!cleanupScopeActive);
        std::cout << "PASS production backend rejects stale player weak identity after same-address serial reuse\n";
    }

    void DamageProtectionPreservesExistingReferenceLayers()
    {
        for (const int baseline : {0, 2})
        {
            InitializeWorld();
            immuneLayers[Player] = baseline;
            StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            CHECK(immuneLayers[Player] == baseline + 1);
            CHECK(trans::Restore().success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(immuneLayers[Player] == baseline);
        }
        std::cout << "PASS retained player immunity adds and removes exactly one reference-counted layer\n";
    }

    void FailedImmunityRemovalKeepsProxyAndRetriesWithoutDroppingLayer()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto proxy = Proxy(), tamer = Tamer();
        CHECK(immuneLayers[Player] == 1);
        failImmuneRemove = 1;
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::RecoveryRequired);
        CHECK(immuneLayers[Player] == 1);
        CHECK(Alive(proxy) && Alive(tamer));
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(immuneLayers[Player] == 0);
        CHECK(!Alive(proxy) && !Alive(tamer));
        std::cout << "PASS failed immunity-layer removal retains session objects and retries safely\n";
    }

    void IndeterminateImmunityCounterCallsAreNeverReplayed()
    {
        {
            InitializeWorld();
            immuneLayers[Player] = 2;
            indeterminateImmuneAdd = 1;
            StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(immuneAddCalls == 1 && immuneRemoveCalls == 0);
            CHECK(immuneLayers[Player] == 3);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::RecoveryRequired);
            CHECK(immuneAddCalls == 1 && immuneRemoveCalls == 0);
            CHECK(immuneLayers[Player] == 3);
            const auto proxy = Proxy(), tamer = Tamer();
            CHECK(Alive(proxy) && Alive(tamer));
            Tick();
            CHECK(immuneAddCalls == 1 && immuneRemoveCalls == 0);
            CHECK(Alive(proxy) && Alive(tamer));

            objects[Player].serial++;
            immuneLayers[Player] = 4; // Replacement player has its own unrelated layers.
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(immuneAddCalls == 1 && immuneRemoveCalls == 0);
            CHECK(immuneLayers[Player] == 4);
            CHECK(!Alive(proxy) && !Alive(tamer));
        }
        {
            InitializeWorld();
            StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            CHECK(immuneLayers[Player] == 1);
            indeterminateImmuneRemove = 1;
            CHECK(trans::Restore().success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::RecoveryRequired);
            CHECK(immuneRemoveCalls == 1);
            CHECK(immuneLayers[Player] == 0);
            const auto proxy = Proxy(), tamer = Tamer();
            CHECK(Alive(proxy) && Alive(tamer));
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::RecoveryRequired);
            CHECK(immuneRemoveCalls == 1);
            CHECK(Alive(proxy) && Alive(tamer));

            objects[Player].serial++;
            immuneLayers[Player] = 5;
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(immuneRemoveCalls == 1);
            CHECK(immuneLayers[Player] == 5);
            CHECK(!Alive(proxy) && !Alive(tamer));
        }
        std::cout << "PASS indeterminate immunity counter calls are not replayed against the same or replacement player\n";
    }

    bool ConfigureTestSkillSlot()
    {
        const auto activeId = trans::GetStatus().activeId;
        auto slots = trans::Slots(activeId);
        if (slots.size() != 8) return false;
        slots[0].name = "Mock skill";
        slots[0].skills = {41001};
        return trans::SetSlots(activeId, std::move(slots)).success;
    }

    void SkillSlotWaitsForMenuAndForegroundThenFiresOnce()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(ConfigureTestSkillSlot());

        overlayVisible = true;
        CHECK(trans::TriggerSlot(0).success);
        CHECK(!trans::TriggerSlot(0).success);
        Tick();
        CHECK(trans::GetStatus().pending);
        CHECK(skillRequests == 0);

        overlayVisible = false;
        foreground = false;
        Tick();
        CHECK(trans::GetStatus().pending);
        CHECK(skillRequests == 0);

        foreground = true;
        Tick();
        if (trans::GetStatus().pending || skillRequests != 1)
        {
            const auto status = trans::GetStatus();
            std::cerr << "slot dispatch diagnostic: pending=" << status.pending << " phase="
                      << trans::PhaseName(status.phase) << " requests=" << skillRequests
                      << " foreground=" << foreground << " overlay=" << overlayVisible
                      << " message=" << status.message << "\n";
        }
        CHECK(!trans::GetStatus().pending);
        CHECK(skillRequests == 1);
        Tick();
        CHECK(skillRequests == 1);

        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS production skill slot waits for menu/focus and dispatches once\n";
    }

    void QueuedSkillSlotTimeoutDoesNotCast()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(ConfigureTestSkillSlot());
        overlayVisible = true;
        CHECK(trans::TriggerSlot(0).success);
        Tick();
        CHECK(trans::GetStatus().pending);
        now += 30000;
        Tick();
        CHECK(!trans::GetStatus().pending);
        CHECK(skillRequests == 0);
        overlayVisible = false;
        foreground = true;
        Tick();
        CHECK(skillRequests == 0);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS production queued skill slot expires without dispatch\n";
    }

    void QueuedSkillSlotIsCancelledOnWorldGenerationChange()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(ConfigureTestSkillSlot());
        overlayVisible = true;
        CHECK(trans::TriggerSlot(0).success);
        Tick();
        CHECK(trans::GetStatus().pending);
        now++;
        trans::Tick(Player, Controller, OtherWorld, 2, true);
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(!trans::GetStatus().pending);
        CHECK(skillRequests == 0);
        std::cout << "PASS production queued skill slot cancels on scene generation change\n";
    }

    bool SameSkillIds(const std::vector<trans::SkillEntry> &skills, const std::vector<int> &ids)
    {
        if (skills.size() != ids.size()) return false;
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (skills[i].id != ids[i]) return false;
        return true;
    }

    void SkillSnapshotsStayScopedToTheInspectedCharacter()
    {
        InitializeWorld();
        trans::Initialize();
        runtimeInitialized = true;
        Tick(); // Publish context and reference catalog.
        const auto catalog = trans::Catalog();
        const auto yangjian = std::find_if(catalog.begin(), catalog.end(), [](const auto &entry)
        {
            return entry.path.find("TAMER_mgd_yangjian_01") != std::string::npos;
        });
        const auto dasheng = std::find_if(catalog.begin(), catalog.end(), [](const auto &entry)
        {
            return entry.path.find("TAMER_mgd_jsds") != std::string::npos;
        });
        CHECK(yangjian != catalog.end());
        CHECK(dasheng != catalog.end());
        if (yangjian == catalog.end() || dasheng == catalog.end()) return;

        availableSkillIds = {41011, 41012};
        CHECK(trans::Inspect(yangjian->id).success);
        Tick();
        const auto yangSkills = trans::SkillsFor(yangjian->id);
        CHECK(yangSkills.inspected);
        CHECK(SameSkillIds(yangSkills.skills, availableSkillIds));

        availableSkillIds = {42011, 42012, 42013, 42014};
        CHECK(trans::Inspect(dasheng->id).success);
        Tick();
        const auto dashengSkills = trans::SkillsFor(dasheng->id);
        CHECK(dashengSkills.inspected);
        CHECK(SameSkillIds(dashengSkills.skills, availableSkillIds));
        CHECK(SameSkillIds(trans::SkillsFor(yangjian->id).skills, {41011, 41012}));
        CHECK(SameSkillIds(trans::Skills(), {42011, 42012, 42013, 42014}));
        CHECK(!trans::SkillsFor("not-inspected").inspected);
        CHECK(trans::SkillsFor("not-inspected").skills.empty());
        std::cout << "PASS inspected skill snapshots remain keyed to each character while the current list changes\n";
    }

    bool SameBindings(const std::vector<trans::SkillBinding> &left, const std::vector<trans::SkillBinding> &right)
    {
        if (left.size() != right.size()) return false;
        for (std::size_t i = 0; i < left.size(); ++i)
            if (left[i].skillId != right[i].skillId || left[i].customName != right[i].customName ||
                left[i].key != right[i].key || left[i].modifiers != right[i].modifiers) return false;
        return true;
    }

    void PerSkillRequestsWaitForMenuAndFocusAndDispatchOnlyOnce()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto id = trans::GetStatus().activeId;

        overlayVisible = true;
        CHECK(!trans::TriggerSkillHotkey(id, 41001).success);
        CHECK(!trans::GetStatus().pending);
        overlayVisible = false;
        foreground = false;
        CHECK(!trans::TriggerSkillHotkey(id, 41001).success);
        foreground = true;
        hotkeysEnabled = false;
        CHECK(!trans::TriggerSkillHotkey(id, 41001).success);
        hotkeysEnabled = true;

        CHECK(trans::TriggerSkillHotkey(id, 41001).success);
        CHECK(trans::GetStatus().pending);
        overlayVisible = true;
        Tick(); // A queued key press is cancelled if the menu opens before dispatch.
        CHECK(!trans::GetStatus().pending);
        CHECK(skillRequests == 0);
        overlayVisible = false;
        CHECK(trans::TriggerSkillHotkey(id, 41001).success);
        foreground = false;
        Tick(); // Losing focus after queueing also cancels instead of delaying the cast.
        CHECK(!trans::GetStatus().pending);
        CHECK(skillRequests == 0);
        foreground = true;

        CHECK(trans::TriggerSkill(41001).success);
        CHECK(!trans::TriggerSkill(41002).success); // Only one pending skill request is accepted.
        overlayVisible = true;
        Tick();
        CHECK(trans::GetStatus().pending);
        CHECK(skillRequests == 0);
        overlayVisible = false;
        foreground = false;
        Tick();
        CHECK(trans::GetStatus().pending);
        CHECK(skillRequests == 0);
        foreground = true;
        Tick();
        CHECK(!trans::GetStatus().pending);
        CHECK(skillRequests == 1);
        Tick();
        CHECK(skillRequests == 1);

        overlayVisible = false;
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS per-skill requests wait for menu/focus, suppress invalid hotkey input and dispatch once\n";
    }

    void PerSkillBindingsValidateIdsAndRollbackMemoryAndFileFailures()
    {
        InitializeWorld();
        availableSkillIds.clear();
        for (int i = 0; i < 12; ++i) availableSkillIds.push_back(41001 + i);
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto id = trans::GetStatus().activeId;

        std::vector<trans::SkillBinding> values;
        for (int i = 0; i < 12; ++i)
            values.push_back({41001 + i, i == 0 ? "旋风斩" : "", static_cast<unsigned>('A' + i), 0});
        CHECK(values.size() > 8);
        CHECK(trans::SetBindings(id, values).success);
        const auto persisted = trans::Bindings(id);
        CHECK(persisted.size() == 12);
        CHECK(persisted[0].customName == "旋风斩");
        const auto file = gloss::DataDirectory() / L"transformation-bindings.cfg";
        std::ifstream savedFile(file, std::ios::binary);
        const std::string savedText{std::istreambuf_iterator<char>(savedFile), std::istreambuf_iterator<char>()};
        savedFile.close(); // Release the Windows handle before rename/atomic replacement checks.
        binding_model::Config decoded;
        CHECK(binding_model::Parse(savedText, decoded).success);
        CHECK(decoded.contains(id) && decoded.at(id).size() == 12);
        CHECK(decoded.at(id)[0].customName == "旋风斩");

        auto invented = persisted;
        invented.push_back({999999, "未知技能", 'Z', 0});
        CHECK(!trans::SetBindings(id, invented).success);
        CHECK(SameBindings(trans::Bindings(id), persisted));

        auto globalConflict = persisted;
        globalConflict[0].key = 'P';
        globalConflict[0].modifiers = gloss::features::Ctrl;
        blockChordConflict = true;
        CHECK(!trans::SetBindings(id, globalConflict).success);
        CHECK(SameBindings(trans::Bindings(id), persisted));
        blockChordConflict = false;

        auto backup = file;
        backup += L".backup";
        std::error_code error;
        std::filesystem::remove_all(backup, error);
        CHECK(std::filesystem::exists(file));
        std::filesystem::rename(file, backup, error);
        bool failureTargetReady = !error;
        if (failureTargetReady)
        {
            std::filesystem::create_directory(file, error);
            failureTargetReady = !error;
        }
        CHECK(failureTargetReady);
        if (failureTargetReady)
        {
            auto changed = persisted;
            changed[0].customName = "保存失败不应生效";
            CHECK(!trans::SetBindings(id, changed).success);
            CHECK(SameBindings(trans::Bindings(id), persisted));
            std::filesystem::remove_all(file, error);
            error.clear();
            std::filesystem::rename(backup, file, error);
            CHECK(!error);
            std::ifstream restoredFile(file, std::ios::binary);
            const std::string restoredText{std::istreambuf_iterator<char>(restoredFile), std::istreambuf_iterator<char>()};
            CHECK(restoredText == savedText);
        }
        else if (!std::filesystem::exists(file) && std::filesystem::exists(backup))
        {
            error.clear();
            std::filesystem::rename(backup, file, error);
            CHECK(!error);
        }

        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        availableSkillIds.pop_back(); // Skill 41012 vanished from the current game list.
        CHECK(trans::Inspect(id).success);
        Tick();
        const auto afterInspection = trans::Bindings(id);
        CHECK(afterInspection.size() == 12);
        CHECK(afterInspection.back().skillId == 41012);

        auto edited = afterInspection;
        edited[0].customName = "新名称";
        CHECK(trans::SetBindings(id, edited).success); // Unchanged missing record is retained.
        const auto afterEdit = trans::Bindings(id);
        CHECK(afterEdit.size() == 12 && afterEdit.back().skillId == 41012);

        auto releasedMissing = afterEdit;
        releasedMissing.back().key = 0;
        releasedMissing.back().modifiers = 0;
        CHECK(trans::SetBindings(id, releasedMissing).success); // Keep the label while freeing the stale chord.
        const auto afterRelease = trans::Bindings(id);
        CHECK(afterRelease.back().skillId == 41012);
        CHECK(afterRelease.back().customName == afterEdit.back().customName);
        CHECK(afterRelease.back().key == 0 && afterRelease.back().modifiers == 0);

        auto changedMissing = afterRelease;
        changedMissing.back().customName = "失效后不可修改";
        CHECK(!trans::SetBindings(id, changedMissing).success);
        CHECK(SameBindings(trans::Bindings(id), afterRelease));
        auto unknown = afterRelease;
        unknown.push_back({999998, "不属于角色", 'Z', 0});
        CHECK(!trans::SetBindings(id, unknown).success);
        CHECK(SameBindings(trans::Bindings(id), afterRelease));
        std::cout << "PASS per-skill records support >8 real IDs and preserve validated state on invalid IDs or failed writes\n";
    }
}

int main()
{
    fixture::ActivationFailureRestoresPartialChanges();
    fixture::FailedRestoreRetainsBaselineAndRetries();
    fixture::ReusedPlayerAddressDoesNotReceiveStaleWrites();
    fixture::DamageProtectionPreservesExistingReferenceLayers();
    fixture::FailedImmunityRemovalKeepsProxyAndRetriesWithoutDroppingLayer();
    fixture::IndeterminateImmunityCounterCallsAreNeverReplayed();
    fixture::SkillSlotWaitsForMenuAndForegroundThenFiresOnce();
    fixture::QueuedSkillSlotTimeoutDoesNotCast();
    fixture::QueuedSkillSlotIsCancelledOnWorldGenerationChange();
    fixture::SkillSnapshotsStayScopedToTheInspectedCharacter();
    fixture::PerSkillRequestsWaitForMenuAndFocusAndDispatchOnlyOnce();
    fixture::PerSkillBindingsValidateIdsAndRollbackMemoryAndFileFailures();
    if (fixture::failures)
    {
        std::cerr << fixture::failures << " transformation runtime assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer transformation runtime tests passed (0 failures)\n";
    return 0;
}
