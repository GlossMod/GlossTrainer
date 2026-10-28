#include "../src/gameplay/engine_bridge.hpp"
#include "../src/gameplay/transformation_native.hpp"
#include "../src/gameplay/transformation_special.hpp"
#include "../src/runtime.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
    constexpr std::uintptr_t BaseTamerClass = 0x6002;
    constexpr std::uintptr_t BaseTamerCDO = 0x6003;
    constexpr std::uintptr_t MonsterClass = 0x6100;
    constexpr std::uintptr_t MonsterCDO = 0x6101;
    constexpr std::uintptr_t PlayerConfig = 0x6200;
    constexpr std::uintptr_t CDOConfig = 0x6201;
    constexpr std::uintptr_t CDOExtend = 0x6202;
    constexpr std::uintptr_t CDOCharacterMovement = 0x6203;
    constexpr std::uintptr_t TamerCDOExtend = 0x6204;
    constexpr std::uintptr_t BaseTamerExtend = 0x6205;
    constexpr std::uintptr_t ConfigExtend = 0x6300;
    constexpr std::uintptr_t ConfigUnit = 0x6301;
    constexpr std::uintptr_t ProxyExtend = 0x6400;
    constexpr std::uintptr_t ProxyMovement = 0x6401;
    constexpr std::uintptr_t ProxyController = 0x6500;
    constexpr std::uintptr_t ProxyMesh = 0x6501, ProxyAnim = 0x6502, ProxyABPSetting = 0x6503;
    constexpr std::uintptr_t ProxyMoveAnim = 0x6504, ProxyLocomotionAnim = 0x6505;
    constexpr std::uintptr_t AssetRegistry = 0x6800;

    struct Object
    {
        std::int32_t serial{1};
        std::uintptr_t klass{};
        bool alive{true};
        std::map<std::string, eng::Arg> properties;
        std::map<std::string, bool> collision;
        std::map<std::string, gloss::gameplay::extensions::Vec3> locations;
        std::map<std::string, gloss::gameplay::extensions::Vec3> bounds;
        std::map<std::string, std::wstring> exportedProperties;
        std::map<std::string, std::wstring> exportedPropertyPaths;
        std::map<std::string, eng::Arg> pathProperties;
        enum class ReferenceKind { Hard, Weak, Soft, Invalid };
        struct Reference
        {
            ReferenceKind kind{ReferenceKind::Invalid};
            std::uintptr_t hard{};
            eng::WeakObject weak{};
            std::wstring soft;
            bool readable{true};
        };
        std::map<std::string, Reference> references;
        std::unordered_set<std::string> exportFailures;
        std::unordered_set<std::string> exportPathFailures;
        bool dead{};
    };
    std::unordered_map<std::uintptr_t, Object> objects;
    std::unordered_map<std::uintptr_t, int> immuneLayers;
    std::unordered_map<std::uintptr_t, std::unordered_set<int>> simpleStates;
    std::uintptr_t nextActor{0x10000};
    std::uint64_t now{1000};
    int spawnCount{};
    int validSpawnRequests{};
    int rejectedSpawnRequests{};
    int skillRequests{};
    int failures{};
    std::vector<int> availableSkillIds{41001, 41002, 41003};
    std::function<std::string(unsigned, unsigned)> hotkeyConflictProbe;
    std::vector<int> instanceSkillIds{41001, 41002, 41003};
    bool useInstanceSkillIds{};
    std::vector<int> requestedSkillIds;
    std::vector<trans::Action> pendingActions;
    std::vector<std::uintptr_t> ownedChildren;
    std::vector<std::uintptr_t> destroyRequests;
    std::vector<std::uintptr_t> cleanupActors;
    std::unordered_map<std::uintptr_t, int> deferredDestroyCalls;
    std::unordered_map<std::uintptr_t, std::uintptr_t> spawnChildOnDestroy;
    native::MoveInput movementInput{};
    float customSpeedMultiplier{1.f};
    std::vector<float> speedMultiplierWrites;
    int failSpeedMultiplierCalls{};
    eng::CallState speedFailureState{eng::CallState::NotCalled};
    struct MovementRequest { Vec3 destination; std::uint8_t speedType{}, moveAIType{}; int id{}; int mmState{-1}; };
    std::vector<MovementRequest> movementRequests;
    struct DirectionInput { Vec3 direction; float scale; };
    std::vector<DirectionInput> directionInputs;
    std::vector<Vec3> facingDirections;
    int failFacingCalls{};
    bool moverFacingOverride{};
    double observedActorYaw{37.};
    Vec3 lastMovementInput{};
    int failDirectionInputCalls{}, lastInputReads{};
    bool lastInputAvailable{true}, failLastInputRead{};
    std::vector<int> cancelledMovementRequests;
    int nextMovementRequest{1}, activeMovementRequest{}, movementReturnOverride{-1}, failMovementRequestCalls{}, failMovementCancelCalls{};
    int controlledPawnReads{};
    int actorRotationCalls{};
    std::vector<std::string> diagnosticLogs;
    bool diagnosticInterfacesAvailable{true}, failDiagnosticFunctions{};
    int diagnosticProbes{}, diagnosticAccelerationCalls{}, diagnosticMaximumCalls{}, diagnosticClassPathCalls{}, diagnosticPropertyReads{}, diagnosticWrites{};
    Vec3 diagnosticAcceleration{250., 0., 0.};
    float diagnosticMaximum{2048.f};
    bool diagnosticAnimationInterfacesAvailable{true}, diagnosticHumanoid{true}, diagnosticAnimBase{true}, diagnosticAnimNull{};
    int diagnosticAnimationProbes{}, diagnosticAnimCalls{}, diagnosticAnimationPropertyReads{}, diagnosticSpecificAnimReads{};
    bool diagnosticLinkedAvailable{true}, diagnosticMoveClass{true}, diagnosticNodeClass{true}, diagnosticMoveNull{}, diagnosticNodeNull{}, diagnosticOwnerMatches{true};
    bool diagnosticSettingAvailable{true}, diagnosticSettingClass{true};
    int diagnosticSettingProbes{}, diagnosticSettingReads{};
    int diagnosticDataCalls{};
    native::AnimationStateSnapshot diagnosticData;
    native::MotionMatchingSelection movementAnimationChoice{true, true, false};
    int movementAnimationQueries{};
    int lastAnimationSpeed{-1};
    bool lastAnimationLocked{};
    std::uintptr_t lockedTarget{};
    std::vector<std::uintptr_t> forwardedTargets;
    int diagnosticLinkedProbes{}, diagnosticLinkedCalls{}, diagnosticOwnerCalls{}, diagnosticLinkedPropertyReads{};
    int diagnosticNodeMode{10};
    int castingSkillId{};
    int skillRequestReturnOverride{-1};
    float proxyHealth{500.f};
    float playerHealth{500.f};
    double controlYaw{};
    bool deathRequested{};
    bool nativeAdapterReady{true};
    bool hotkeysEnabled{true};
    bool blockChordConflict{};
    bool runtimeInitialized{};
    int failCollisionEnable{};
    int failCollisionDisable{};
    int failHiddenRestore{};
    int failModeRestore{};
    int failImmuneRemove{};
    int failTargetClearCalls{}, targetClearCalls{};
    int weaponInitializeCalls{}, failWeaponInitializeCalls{};
    native::WeaponReadiness weaponReadiness{native::WeaponReadiness::Ready};
    int weaponReadinessCalls{};
    int indeterminateImmuneAdd{}, indeterminateImmuneRemove{};
    int immuneAddCalls{}, immuneRemoveCalls{};
    int hiddenWrites{}, collisionWrites{}, modeWrites{}, armWrites{};
    int failArmWrites{}, failArmReads{}, failMovieReadCalls{};
    int playerBaselineWrites{};
    int cleanupConstructs{}, cleanupDestructs{}, guardedProxyDestroyCalls{};
    bool cleanupScopeActive{};
    bool overlayVisible{}, foreground{true};
    bool playingMovie{};
    bool specialAdapterReady{}, specialObserved{}, specialHealthy{true};
    bool specialObserveOnFinish{true};
    eng::WeakObject specialRoot;
    std::vector<std::string> specialTrace;
    int specialSetSessionCalls{}, specialCheckCalls{}, specialClearCalls{};
    struct IsolationRequest { std::uint32_t handle; int property, value; };
    std::vector<IsolationRequest> isolationRequests;
    std::array<std::uint32_t, 4> isolationHandles{};
    native::PlayerIsolation *isolationOwner{};
    eng::WeakObject isolationPlayer;
    std::uint64_t isolationTokenCounter{};
    std::uint32_t isolationHandleCounter{};
    int capsuleCollision{3}, meshCollision{3};
    bool isolationShadowValid{}, isolationShadowHidden{};
    bool invalidTamerResource{};
    struct RegistryRoot
    {
        bool success{true};
        std::vector<std::vector<eng::Arg>> records;
    };
    bool registryAvailable{}, registryLoading{}, registryLoadingKnown{true};
    std::map<std::wstring, RegistryRoot> registryRoots;
    std::vector<std::wstring> queriedRegistryRoots;

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
        const auto reference = objects.at(p).references.find(value.name);
        if (reference != objects.at(p).references.end() && reference->second.kind == Object::ReferenceKind::Hard && value.type == eng::Type::Object)
            reference->second.hard = value.Get<std::uintptr_t>();
        objects.at(p).properties.insert_or_assign(value.name, std::move(value));
    }
    void PutHardReference(std::uintptr_t p, const char *name, std::uintptr_t target)
    {
        Put(p, eng::Arg::Object(name, target));
        objects.at(p).references.insert_or_assign(name, Object::Reference{Object::ReferenceKind::Hard, target});
    }
    void PutWeakReference(std::uintptr_t p, const char *name, eng::WeakObject target = {})
    {
        Object::Reference value;
        value.kind = Object::ReferenceKind::Weak;
        value.weak = target;
        objects.at(p).references.insert_or_assign(name, std::move(value));
    }
    void PutSoftReference(std::uintptr_t p, const char *name, std::wstring path = {})
    {
        Object::Reference value;
        value.kind = Object::ReferenceKind::Soft;
        value.soft = std::move(path);
        objects.at(p).references.insert_or_assign(name, std::move(value));
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
        PutHardReference(p, "PhaseStateGraphTemplate", 0);
        PutHardReference(p, "BattleSC", 0);
        PutWeakReference(p, "FloatRelativeSpotActor");
        PutSoftReference(p, "BeginTriggerBox");
        PutSoftReference(p, "EndTriggerBox");
        Put(p, eng::Arg::String("PhaseStateGraphTemplatePath", {}));
        objects.at(p).pathProperties.emplace("BattleSC_GUID.TagName", eng::Arg::Name("TagName"));
        Put(p, eng::Arg::Boolean("bCanAsPerformer", true));
        Put(p, eng::Arg::Boolean("bCanAsNPC", true));
        Put(p, eng::Arg::Boolean("bCanAsFollowPartner", true));
        for (const auto name : {"bEnableFlyCtrl", "bEnableTurretControl", "bEnableMovePhysicsTransform", "bEnablePhysicalMove"})
            Put(p, eng::Arg::Boolean(name, false));
        objects.at(p).pathProperties.emplace("WakeUpConfig.WakeUpLinkEnable", eng::Arg::Boolean("WakeUpLinkEnable", false));
        objects.at(p).pathProperties.emplace("WakeUpConfig.CanTriggerWakeUpLinkWhenDead", eng::Arg::Boolean("CanTriggerWakeUpLinkWhenDead", false));
        for (const auto name : {"OtherFsmTemplates", "OtherFsmTemplatesPath", "FsmSubMachineOverride", "FsmSubMachinePathOverride",
                 "SpecificEventListen_BattleSC", "SplineList", "BossBarObserveTamerList"})
            objects.at(p).exportedProperties.emplace(name, L"");
        objects.at(p).exportedPropertyPaths.emplace("WakeUpConfig.TargetTamerList", L"");
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
    struct ParameterContract
    {
        const char *name;
        eng::Type type;
        std::size_t argumentSize;
        bool output{};
    };
    // Independent source: build 21393610 Main assembly BGUFuncLibAICS
    // declarations AND LoadNativeType (FEnumProperty, not FByteProperty).
    // Bool uses bridge int32 storage; this is not its reflected native size.
    constexpr std::array MoveToContract{
        ParameterContract{"Unit", eng::Type::Object, 8},
        ParameterContract{"Position", eng::Type::Vector, 24},
        ParameterContract{"SpeedRateType", eng::Type::Enum, 1},
        ParameterContract{"AcceptableRadius", eng::Type::Float, 4},
        ParameterContract{"MoveAIType", eng::Type::Enum, 1},
        ParameterContract{"IncludeSelfRadius", eng::Type::Bool, 4},
        ParameterContract{"IncludeTargetRadius", eng::Type::Bool, 4},
        ParameterContract{"ReturnValue", eng::Type::Int, 4, true}};
    constexpr std::array MoveWithMMContract{
        ParameterContract{"Unit", eng::Type::Object, 8},
        ParameterContract{"Position", eng::Type::Vector, 24},
        ParameterContract{"SpeedRateType", eng::Type::Enum, 1},
        ParameterContract{"AcceptableRadius", eng::Type::Float, 4},
        ParameterContract{"MoveAIType", eng::Type::Enum, 1},
        ParameterContract{"IncludeSelfRadius", eng::Type::Bool, 4},
        ParameterContract{"IncludeTargetRadius", eng::Type::Bool, 4},
        ParameterContract{"MotionMatchingState", eng::Type::Enum, 1},
        ParameterContract{"ReturnValue", eng::Type::Int, 4, true}};
    constexpr std::array CancelMoveContract{
        ParameterContract{"Unit", eng::Type::Object, 8},
        ParameterContract{"RequestIdx", eng::Type::Int, 4}};
    constexpr std::array ObjectReturnContract{
        ParameterContract{"ReturnValue", eng::Type::Object, 8, true}};
    bool MatchesContract(std::span<const eng::Arg> args, std::span<const ParameterContract> contract)
    {
        if (args.size() != contract.size()) return false;
        for (const auto &parameter : contract)
        {
            const eng::Arg *match{};
            for (const auto &arg : args)
                if (arg.name && std::string_view(arg.name) == parameter.name)
                {
                    if (match) return false;
                    match = &arg;
                }
            if (!match || match->type != parameter.type || match->bytes.size() != parameter.argumentSize || match->output != parameter.output)
                return false;
        }
        return true;
    }
    template <class T> void Output(std::span<eng::Arg> args, const char *name, T value)
    {
        auto *out = Named(args, name);
        if (!out) throw std::runtime_error(std::string("missing mock output: ") + name);
        out->bytes.resize(sizeof(T));
        std::memcpy(out->bytes.data(), &value, sizeof(T));
    }
    bool ApplyIsolationProperty(int property, bool own, bool releasing = false)
    {
        int value = property == 302 ? 0 : 3;
        for (auto request = isolationRequests.rbegin(); request != isolationRequests.rend(); ++request)
            if (request->property == property) { value = request->value; break; }
        if (property == 302)
        {
            if (!own) isolationShadowValid = false;
            if (releasing && isolationShadowValid) value = isolationShadowHidden;
            if (!value && failHiddenRestore > 0) { --failHiddenRestore; return false; }
            if ((Get<std::int32_t>(Player, "bHidden") != 0) != (value != 0))
            { Put(Player, eng::Arg::Boolean("bHidden", value != 0)); ++hiddenWrites; ++playerBaselineWrites; }
        }
        if (property == 13)
        {
            if (value == 3 && failModeRestore > 0) { --failModeRestore; return false; }
            if (Get<std::uint8_t>(Movement, "MovementMode") != value)
            {
                Put(Movement, eng::Arg::Byte("MovementMode", static_cast<std::uint8_t>(value)));
                Put(Movement, eng::Arg::Byte("CustomMovementMode", 0));
                ++modeWrites; ++playerBaselineWrites;
            }
        }
        auto *collision = property == 202 ? &capsuleCollision : property == 105 ? &meshCollision : nullptr;
        if (collision && *collision != value) { *collision = value; ++collisionWrites; ++playerBaselineWrites; }
        return true;
    }
    std::uint32_t ExternalIsolationProperty(int property, int value)
    {
        const auto handle = ++isolationHandleCounter;
        isolationRequests.push_back({handle, property, value});
        CHECK(ApplyIsolationProperty(property, false));
        return handle;
    }
    void ExternalHidden(bool hidden)
    {
        isolationShadowValid = true; isolationShadowHidden = hidden;
        Put(Player, eng::Arg::Boolean("bHidden", hidden)); // Same values still represent external writes.
    }
    void InitializeWorld()
    {
        registryAvailable = registryLoading = false;
        registryLoadingKnown = true;
        registryRoots.clear();
        queriedRegistryRoots.clear();
        customSpeedMultiplier = 1.f;
        speedMultiplierWrites.clear();
        failSpeedMultiplierCalls = 0;
        speedFailureState = eng::CallState::NotCalled;
        objects.clear();
        immuneLayers.clear();
        playerHealth = 500.f;
        simpleStates.clear();
        nextActor = 0x10000;
        spawnCount = hiddenWrites = collisionWrites = modeWrites = armWrites = 0;
        failArmWrites = failArmReads = failMovieReadCalls = 0;
        validSpawnRequests = rejectedSpawnRequests = 0;
        skillRequests = playerBaselineWrites = 0;
        requestedSkillIds.clear();
        pendingActions.clear();
        ownedChildren.clear();
        destroyRequests.clear();
        cleanupActors.clear();
        deferredDestroyCalls.clear();
        spawnChildOnDestroy.clear();
        movementInput = {};
        movementRequests.clear();
        directionInputs.clear(); lastMovementInput = {};
        facingDirections.clear(); failFacingCalls = 0; moverFacingOverride = false; observedActorYaw = 37.;
        failDirectionInputCalls = lastInputReads = 0; lastInputAvailable = true; failLastInputRead = false;
        movementAnimationChoice = {true, true, false};
        movementAnimationQueries = 0; lastAnimationSpeed = -1; lastAnimationLocked = false;
        lockedTarget = 0;
        forwardedTargets.clear();
        cancelledMovementRequests.clear();
        nextMovementRequest = 1;
        activeMovementRequest = 0;
        movementReturnOverride = -1;
        failMovementRequestCalls = failMovementCancelCalls = 0;
        controlledPawnReads = 0;
        actorRotationCalls = 0;
        diagnosticLogs.clear();
        diagnosticInterfacesAvailable = true;
        failDiagnosticFunctions = false;
        diagnosticProbes = diagnosticAccelerationCalls = diagnosticMaximumCalls = diagnosticClassPathCalls = diagnosticPropertyReads = diagnosticWrites = 0;
        diagnosticAcceleration = {250., 0., 0.}; diagnosticMaximum = 2048.f;
        diagnosticAnimationInterfacesAvailable = diagnosticHumanoid = diagnosticAnimBase = true;
        diagnosticAnimNull = false;
        diagnosticAnimationProbes = diagnosticAnimCalls = diagnosticAnimationPropertyReads = diagnosticSpecificAnimReads = 0;
        diagnosticLinkedAvailable = diagnosticMoveClass = diagnosticNodeClass = diagnosticOwnerMatches = true;
        diagnosticMoveNull = diagnosticNodeNull = false;
        diagnosticSettingAvailable = diagnosticSettingClass = true;
        diagnosticSettingProbes = diagnosticSettingReads = 0;
        diagnosticDataCalls = 0;
        diagnosticData = {true, true, native::AnimationDataState::Present, native::AnimationDataState::Present,
            native::AnimationDataState::Present, native::AnimationDataState::Present, 10, 11, 10};
        diagnosticLinkedProbes = diagnosticLinkedCalls = diagnosticOwnerCalls = diagnosticLinkedPropertyReads = 0;
        diagnosticNodeMode = 10;
        castingSkillId = 0;
        skillRequestReturnOverride = -1;
        proxyHealth = 500.f;
        controlYaw = 0.;
        deathRequested = false;
        nativeAdapterReady = true;
        availableSkillIds = {41001, 41002, 41003};
        instanceSkillIds = {41001, 41002, 41003};
        useInstanceSkillIds = false;
        hotkeysEnabled = true;
        blockChordConflict = false;
        cleanupConstructs = cleanupDestructs = guardedProxyDestroyCalls = 0;
        cleanupScopeActive = false;
        overlayVisible = false;
        foreground = true;
        playingMovie = false;
        specialAdapterReady = specialObserved = false; specialHealthy = specialObserveOnFinish = true;
        specialRoot = {}; specialTrace.clear(); specialSetSessionCalls = specialCheckCalls = specialClearCalls = 0;
        trans::cameraConfig.clear(); // Keep unrelated tests independent of saved in-memory preferences.
        isolationRequests.clear(); isolationHandles = {}; isolationOwner = nullptr; isolationPlayer = {};
        isolationShadowValid = isolationShadowHidden = false; capsuleCollision = meshCollision = 3;
        invalidTamerResource = false;
        failCollisionEnable = failCollisionDisable = failHiddenRestore = failModeRestore = 0;
        failImmuneRemove = 0;
        failTargetClearCalls = targetClearCalls = 0;
        weaponInitializeCalls = failWeaponInitializeCalls = 0;
        weaponReadiness = native::WeaponReadiness::Ready;
        weaponReadinessCalls = 0;
        indeterminateImmuneAdd = indeterminateImmuneRemove = 0;
        immuneAddCalls = immuneRemoveCalls = 0;
        Add(Player);
        Add(Controller);
        Add(ProxyController);
        Add(World);
        Add(OtherWorld);
        Add(Movement);
        Add(SpringArm);
        Add(TamerClass);
        Add(TamerCDO);
        Add(BaseTamerClass);
        Add(BaseTamerCDO);
        Add(MonsterClass);
        Add(MonsterCDO);
        Add(PlayerConfig);
        Add(CDOConfig);
        Add(CDOExtend);
        Add(CDOCharacterMovement);
        Add(TamerCDOExtend);
        Add(BaseTamerExtend);
        Put(Player, eng::Arg::Object("CharacterMovement", Movement));
        Put(Player, eng::Arg::Boolean("bHidden", false));
        Put(Movement, eng::Arg::Byte("MovementMode", 3));
        Put(Movement, eng::Arg::Byte("CustomMovementMode", 0));
        Put(Movement, eng::Arg::Byte("DefaultLandMovementMode", 1));
        Put(Movement, eng::Arg::Byte("DefaultWaterMovementMode", 4));
        Put(Movement, eng::Arg::Float("MaxWalkSpeed", 600.f));
        Put(Movement, eng::Arg::Float("MaxFlySpeed", 600.f));
        Put(Movement, eng::Arg::Float("MaxSwimSpeed", 600.f));
        Put(Movement, eng::Arg::Float("GravityScale", 1.f));
        Put(SpringArm, eng::Arg::Float("TargetArmLength", 350.f));
        Put(TamerCDO, eng::Arg::Object("ConfigInfoComp", CDOConfig));
        Put(TamerCDO, eng::Arg::Object("ExtendConfigComp", TamerCDOExtend));
        Put(TamerCDO, eng::Arg::Object("MonsterClassObj", MonsterClass));
        Put(BaseTamerCDO, eng::Arg::Object("ExtendConfigComp", BaseTamerExtend));
        Put(MonsterCDO, eng::Arg::Object("ExtendConfigComp", CDOExtend));
        Put(MonsterCDO, eng::Arg::Object("CharacterMovement", CDOCharacterMovement));
        SeedExtension(CDOExtend);
        SeedExtension(TamerCDOExtend);
        SeedExtension(BaseTamerExtend);
        Put(CDOCharacterMovement, eng::Arg::Float("MaxWalkSpeed", 600.f));
        Put(CDOCharacterMovement, eng::Arg::Byte("MovementMode", 1));
        Put(CDOCharacterMovement, eng::Arg::Byte("CustomMovementMode", 0));
        Put(CDOCharacterMovement, eng::Arg::Byte("DefaultLandMovementMode", 1));
        Put(CDOCharacterMovement, eng::Arg::Byte("DefaultWaterMovementMode", 4));
        Put(CDOCharacterMovement, eng::Arg::Float("MaxFlySpeed", 600.f));
        Put(CDOCharacterMovement, eng::Arg::Float("MaxSwimSpeed", 600.f));
        Put(CDOCharacterMovement, eng::Arg::Float("GravityScale", 1.f));
        Put(CDOConfig, eng::Arg::Int("ResID", 830001));
        Put(CDOConfig, eng::Arg::Int("OverrideID", 0));
        Put(CDOExtend, eng::Arg::Int("BehaviorGraphTemplateID", 77));
        Put(CDOExtend, eng::Arg::Int("DialogueGraphTemplateID", 78));
        Put(CDOExtend, eng::Arg::Int("InteractiveUnitID", 79));
        Put(CDOExtend, eng::Arg::Int("DefalutInteractiveGroupID", 80));
        Put(CDOExtend, eng::Arg::Int("ShopID", 81));
        Put(CDOExtend, eng::Arg::Int("RewardDropID", 82));
        Put(CDOExtend, eng::Arg::Object("StateGraphTemplate", 0x7000));
        Put(CDOExtend, eng::Arg::Object("PhaseStateGraphTemplate", 0));
        Put(CDOExtend, eng::Arg::Object("BattleSC", 0));
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
            if ((object.klass == TamerClass || object.klass == BaseTamerClass) && address >= 0x10000) return address;
        return 0;
    }
    void AddOwnedChild(std::uintptr_t actor, bool dead = false)
    {
        Add(actor);
        objects[actor].dead = dead;
        objects[actor].locations["actor"] = {};
        ownedChildren.push_back(actor);
    }
    void StartAndReachActivation(std::string_view pathHint = "TAMER_mgd_yangjian_01")
    {
        trans::Initialize();
        runtimeInitialized = true;
        Tick(); // Publish the game context and adapter capability probe.
        const auto catalog = trans::Catalog();
        const auto entry = std::find_if(catalog.begin(), catalog.end(), [&](const auto &item)
        {
            return item.path.find(pathHint) != std::string::npos;
        });
        CHECK(entry != catalog.end());
        if (entry == catalog.end()) return;
        const auto queued = trans::Begin(entry->id);
        CHECK(queued.success);
        Tick(); // Load and validate the selected Tamer / skill configuration.
        Tick(); // Spawn deferred Tamer and proxy.
        Tick(); // Initialize the proxy and attempt activation.
        const auto status = trans::GetStatus();
        if (status.phase != trans::Phase::Active && Proxy() == 0)
            std::cerr << "start diagnostic: phase=" << trans::PhaseName(status.phase) << " message=" << status.message << "\n";
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
        if (type == L"/Script/b1-Managed.BUTamerActor") return !fixture::invalidTamerResource && (object == fixture::TamerCDO || object == fixture::BaseTamerCDO ||
            fixture::objects[object].klass == fixture::TamerClass || fixture::objects[object].klass == fixture::BaseTamerClass);
        if (type == L"/Script/b1-Managed.BGU_CharacterAI") return object == fixture::MonsterCDO;
        if (type == L"/Script/b1-Managed.BGUCharacterCS") return object >= 0x10000 && fixture::objects[object].klass == fixture::MonsterClass;
        if (type == L"/Script/b1-Managed.BUAnimHumanoidCS") return object == fixture::ProxyAnim && fixture::diagnosticHumanoid;
        if (type == L"/Script/b1-Managed.BUAnimInstanceBase") return object == fixture::ProxyAnim && fixture::diagnosticAnimBase;
        if (type == L"/Script/b1-Managed.BUAnimHumanoidCS_Move") return object == fixture::ProxyMoveAnim && fixture::diagnosticMoveClass;
        if (type == L"/Script/b1-Managed.BUAnimHumanoidCS_MonsterLocomotion") return object == fixture::ProxyLocomotionAnim && fixture::diagnosticNodeClass && fixture::diagnosticNodeMode == 10;
        if (type == L"/Script/b1-Managed.BUAnimHumanoidCS_AdvancedMonsterLocomotion") return object == fixture::ProxyLocomotionAnim && fixture::diagnosticNodeClass && fixture::diagnosticNodeMode == 11;
        if (type == L"/Script/b1-Managed.BGWDataAsset_AbpHumanoidSetting") return object == fixture::ProxyABPSetting && fixture::diagnosticSettingClass;
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
        if (path == L"/Script/Engine.Pawn:GetLastMovementInputVector") return fixture::lastInputAvailable ? 0x7200 : 0;
        if (path == L"/Script/Engine.CharacterMovementComponent:GetCurrentAcceleration" || path == L"/Script/Engine.CharacterMovementComponent:GetMaxAcceleration")
        { ++fixture::diagnosticProbes; return fixture::diagnosticInterfacesAvailable ? 0x7200 : 0; }
        if (path == L"/Script/Engine.SkeletalMeshComponent:GetAnimInstance" || path == L"/Script/b1-Managed.BUAnimHumanoidCS" || path == L"/Script/b1-Managed.BUAnimInstanceBase")
        { ++fixture::diagnosticAnimationProbes; return fixture::diagnosticAnimationInterfacesAvailable ? 0x7200 : 0; }
        if (path == L"/Script/Engine.AnimInstance:GetLinkedAnimGraphInstanceByTag" || path == L"/Script/Engine.AnimInstance:GetOwningActor" ||
            path == L"/Script/b1-Managed.BUAnimHumanoidCS_Move" || path == L"/Script/b1-Managed.BUAnimHumanoidCS_MonsterLocomotion" ||
            path == L"/Script/b1-Managed.BUAnimHumanoidCS_AdvancedMonsterLocomotion")
        { ++fixture::diagnosticLinkedProbes; return fixture::diagnosticLinkedAvailable ? 0x7200 : 0; }
        if (path == L"/Script/b1-Managed.BGWDataAsset_AbpHumanoidSetting")
        { ++fixture::diagnosticSettingProbes; return fixture::diagnosticSettingAvailable ? 0x7200 : 0; }
        if (path == L"/Script/b1-Managed.BUTamerActor") return fixture::BaseTamerClass;
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
        if (klass == fixture::BaseTamerClass) return fixture::BaseTamerCDO;
        if (klass == fixture::MonsterClass) return fixture::MonsterCDO;
        return 0;
    }
    bool SoftClassPath(std::uintptr_t, const char *, std::wstring &) { return false; }
    bool ObjectReferenceAssigned(std::uintptr_t object, const char *name, bool &assigned)
    {
        assigned = false;
        if (!fixture::Alive(object) || !name) return false;
        const auto &value = fixture::objects.at(object);
        const auto found = value.references.find(name);
        if (found != value.references.end())
        {
            const auto &reference = found->second;
            if (!reference.readable) return false;
            switch (reference.kind)
            {
            case fixture::Object::ReferenceKind::Hard:
                assigned = reference.hard != 0;
                return true;
            case fixture::Object::ReferenceKind::Weak:
                // Native Reset writes {-1, 0}; the managed zero-init carrier
                // also uses {0, 0}. Other negative indices or zero serials are
                // malformed and must fail closed. A stale valid token remains
                // a dependency even if resolving it would return null.
                if (reference.weak.serial == 0 && (reference.weak.index == -1 || reference.weak.index == 0))
                    return true;
                if (reference.weak.index < 0 || reference.weak.serial <= 0)
                    return false;
                assigned = true;
                return true;
            case fixture::Object::ReferenceKind::Soft:
                // An unloaded asset path is still a configured dependency.
                assigned = !reference.soft.empty() && reference.soft != L"None";
                return true;
            case fixture::Object::ReferenceKind::Invalid:
                return false;
            }
        }
        const auto property = value.properties.find(name);
        if (property == value.properties.end() || property->second.type != Type::Object) return false;
        assigned = property->second.Get<std::uintptr_t>() != 0;
        return true;
    }
    bool ExportPropertyText(std::uintptr_t object, const char *property, std::wstring &text)
    {
        if (!fixture::Alive(object) || !property) return false;
        static constexpr std::array<std::string_view, 7> known{
            "OtherFsmTemplates", "OtherFsmTemplatesPath", "FsmSubMachineOverride", "FsmSubMachinePathOverride",
            "SpecificEventListen_BattleSC", "SplineList", "BossBarObserveTamerList"};
        const std::string_view name(property);
        if (std::find(known.begin(), known.end(), name) == known.end()) return false;
        const auto &value = fixture::objects.at(object);
        if (value.exportFailures.contains(std::string(name))) return false;
        const auto found = value.exportedProperties.find(std::string(name));
        if (found == value.exportedProperties.end()) return false;
        text = found->second;
        return true;
    }
    bool ExportPropertyPathText(std::uintptr_t object, std::span<const std::string_view> path, const char *property, std::wstring &text)
    {
        if (!fixture::Alive(object) || !property || path.size() != 1 || path[0] != "WakeUpConfig" || std::string_view(property) != "TargetTamerList") return false;
        auto &value = fixture::objects.at(object);
        if (value.exportPathFailures.contains("WakeUpConfig.TargetTamerList")) return false;
        const auto found = value.exportedPropertyPaths.find("WakeUpConfig.TargetTamerList");
        if (found == value.exportedPropertyPaths.end()) return false;
        text = found->second;
        return true;
    }
    std::uintptr_t World(std::uintptr_t) { return fixture::World; }
    bool ConsoleExists(std::wstring_view) { return false; }
    CallResult ExecuteConsole(std::wstring_view, std::uintptr_t) { return {false, "unused"}; }
    CallResult Call(std::wstring_view function, std::span<Arg> args, std::uintptr_t instance)
    {
        using namespace fixture;
        if (function == L"/Script/Engine.AnimInstance:GetLinkedAnimGraphInstanceByTag")
        {
            constexpr std::array contract{ParameterContract{"InTag", Type::Name, 8}, ParameterContract{"ReturnValue", Type::Object, 8, true}};
            CHECK(MatchesContract(args, contract));
            if (!MatchesContract(args, contract)) return {false, "invalid linked-graph diagnostic signature"};
            ++diagnosticLinkedCalls;
            if (failDiagnosticFunctions || !diagnosticLinkedAvailable) return {false, "optional linked graph unavailable"};
            const auto &tag = Named(args, "InTag")->text;
            const bool top = instance == ProxyAnim && tag == L"Move";
            const bool node = instance == ProxyMoveAnim && ((tag == L"MonsterLocomotion" && diagnosticNodeMode == 10) ||
                (tag == L"AdvancedMonsterLocomotion" && diagnosticNodeMode == 11));
            CHECK(top || node);
            if (!top && !node) return {false, "incorrect animation tag or graph owner"};
            Output(args, "ReturnValue", top ? (diagnosticMoveNull ? std::uintptr_t{} : ProxyMoveAnim) : (diagnosticNodeNull ? std::uintptr_t{} : ProxyLocomotionAnim));
            return {true, {}};
        }
        if (function == L"/Script/Engine.AnimInstance:GetOwningActor")
        {
            CHECK(MatchesContract(args, ObjectReturnContract) && (instance == ProxyMoveAnim || instance == ProxyLocomotionAnim));
            if (!MatchesContract(args, ObjectReturnContract) || (instance != ProxyMoveAnim && instance != ProxyLocomotionAnim)) return {false, "invalid animation owner signature"};
            ++diagnosticOwnerCalls;
            if (failDiagnosticFunctions || !diagnosticLinkedAvailable) return {false, "optional animation owner unavailable"};
            Output(args, "ReturnValue", diagnosticOwnerMatches ? Proxy() : Player);
            return {true, {}};
        }
        if (function == L"/Script/Engine.SkeletalMeshComponent:GetAnimInstance")
        {
            CHECK(MatchesContract(args, ObjectReturnContract) && instance == ProxyMesh);
            if (!MatchesContract(args, ObjectReturnContract) || instance != ProxyMesh) return {false, "invalid anim-instance diagnostic signature"};
            ++diagnosticAnimCalls;
            if (failDiagnosticFunctions) return {false, "optional animation unavailable"};
            Output(args, "ReturnValue", diagnosticAnimNull ? std::uintptr_t{} : ProxyAnim);
            return {true, {}};
        }
        if (function == L"/Script/Engine.KismetSystemLibrary:GetPathName")
        {
            constexpr std::array contract{ParameterContract{"Object", Type::Object, 8}, ParameterContract{"ReturnValue", Type::String, 0, true}};
            CHECK(MatchesContract(args, contract));
            if (!MatchesContract(args, contract)) return {false, "invalid class-path diagnostic signature"};
            ++diagnosticClassPathCalls;
            CHECK(Named(args, "Object")->Get<std::uintptr_t>() == MonsterClass);
            if (failDiagnosticFunctions) return {false, "optional class path unavailable"};
            Named(args, "ReturnValue")->text = L"/Game/Units/DiagnosticCharacter.DiagnosticCharacter_C";
            return {true, {}};
        }
        if (function == L"/Script/Engine.CharacterMovementComponent:GetCurrentAcceleration")
        {
            constexpr std::array contract{ParameterContract{"ReturnValue", Type::Vector, 24, true}};
            CHECK(MatchesContract(args, contract) && instance == ProxyMovement);
            if (!MatchesContract(args, contract) || instance != ProxyMovement) return {false, "invalid acceleration diagnostic signature"};
            ++diagnosticAccelerationCalls;
            if (failDiagnosticFunctions) return {false, "optional acceleration unavailable"};
            Output(args, "ReturnValue", diagnosticAcceleration);
            return {true, {}};
        }
        if (function == L"/Script/Engine.CharacterMovementComponent:GetMaxAcceleration")
        {
            constexpr std::array contract{ParameterContract{"ReturnValue", Type::Float, 4, true}};
            CHECK(MatchesContract(args, contract) && instance == ProxyMovement);
            if (!MatchesContract(args, contract) || instance != ProxyMovement) return {false, "invalid maximum acceleration diagnostic signature"};
            ++diagnosticMaximumCalls;
            if (failDiagnosticFunctions) return {false, "optional maximum acceleration unavailable"};
            Output(args, "ReturnValue", diagnosticMaximum);
            return {true, {}};
        }
        if (function.ends_with(L":GetAssetRegistry"))
        {
            constexpr std::array contract{ParameterContract{"ReturnValue", Type::Interface, sizeof(ScriptInterface), true}};
            CHECK(MatchesContract(args, contract));
            if (!registryAvailable) return {false, "fixture Asset Registry unavailable"};
            Output(args, "ReturnValue", ScriptInterface{AssetRegistry, AssetRegistry + 1});
            return {true, {}};
        }
        if (function == L"/Script/AssetRegistry.AssetRegistry:IsLoadingAssets")
        {
            constexpr std::array contract{ParameterContract{"ReturnValue", Type::Bool, sizeof(std::int32_t), true}};
            CHECK(MatchesContract(args, contract) && instance == AssetRegistry);
            if (!registryLoadingKnown) return {false, "fixture loading state unavailable"};
            Output(args, "ReturnValue", std::int32_t(registryLoading));
            return {true, {}};
        }
        if (function == L"/Script/AssetRegistry.AssetRegistry:GetAssetsByPath")
        {
            constexpr std::array contract{
                ParameterContract{"PackagePath", Type::Name, sizeof(NameValue)},
                ParameterContract{"OutAssetData", Type::StructArray, 0, true},
                ParameterContract{"bRecursive", Type::Bool, sizeof(std::int32_t)},
                ParameterContract{"bIncludeOnlyOnDiskAssets", Type::Bool, sizeof(std::int32_t)},
                ParameterContract{"ReturnValue", Type::Bool, sizeof(std::int32_t), true}};
            CHECK(MatchesContract(args, contract) && instance == AssetRegistry);
            CHECK(Named(args, "bRecursive")->Get<std::int32_t>() != 0);
            CHECK(Named(args, "bIncludeOnlyOnDiskAssets")->Get<std::int32_t>() == 0);
            const auto path = Named(args, "PackagePath")->text;
            queriedRegistryRoots.push_back(path);
            const auto root = registryRoots.find(path);
            if (root == registryRoots.end()) return {false, "fixture root unavailable"};
            auto *output = Named(args, "OutAssetData");
            CHECK(output->structPath == L"/Script/CoreUObject.AssetData" && output->fields.size() == 3);
            output->records = root->second.records;
            Output(args, "ReturnValue", std::int32_t(root->second.success));
            return {true, {}};
        }
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
            CHECK(instance != Player); // Retained-player state belongs to property-manager leases.
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
            CHECK(instance != Player);
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
            CHECK(instance != Movement);
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
            static constexpr std::array<std::string_view, 6> expectedNames{
                "WorldContextObject", "ActorClass", "SpawnTransform", "CollisionHandlingOverride", "Owner", "ReturnValue"};
            static constexpr std::array<Type, 6> expectedTypes{
                Type::Object, Type::Object, Type::Transform, Type::Enum, Type::Object, Type::Object};
            if (args.size() != expectedNames.size())
            {
                ++rejectedSpawnRequests;
                return {false, "deferred spawn parameter count mismatch", CallState::NotCalled};
            }
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                const bool expectedOutput = i + 1 == args.size();
                if (args[i].name != expectedNames[i] || args[i].type != expectedTypes[i] || args[i].output != expectedOutput)
                {
                    ++rejectedSpawnRequests;
                    return {false, "deferred spawn reflected parameter name/type/direction mismatch", CallState::NotCalled};
                }
            }
            if (args[0].bytes.size() != sizeof(std::uintptr_t) || args[1].bytes.size() != sizeof(std::uintptr_t) ||
                args[2].bytes.size() != sizeof(eng::Transform) || args[3].bytes.size() != sizeof(std::uint8_t) ||
                args[4].bytes.size() != sizeof(std::uintptr_t) || args[5].bytes.size() != sizeof(std::uintptr_t) ||
                args[3].Get<std::uint8_t>() != 1)
            {
                ++rejectedSpawnRequests;
                return {false, "deferred spawn reflected parameter size/value mismatch", CallState::NotCalled};
            }
            ++validSpawnRequests;
            const auto klass = Named(args, "ActorClass")->Get<std::uintptr_t>();
            const auto actor = nextActor++;
            Add(actor, klass);
            if (klass == TamerClass || klass == BaseTamerClass)
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
                Put(ProxyMovement, Arg::Byte("MovementMode", 1));
                Put(ProxyMovement, Arg::Byte("CustomMovementMode", 0));
                Put(ProxyMovement, Arg::Byte("DefaultLandMovementMode", 1));
                Put(ProxyMovement, Arg::Byte("DefaultWaterMovementMode", 4));
                Put(ProxyMovement, Arg::Float("MaxWalkSpeed", 600.f));
                Put(ProxyMovement, Arg::Float("MaxFlySpeed", 600.f));
                Put(ProxyMovement, Arg::Float("MaxSwimSpeed", 600.f));
                Put(ProxyMovement, Arg::Float("GravityScale", 1.f));
                Put(ProxyMovement, Arg::Vector("Velocity", {120., 0., 0.}));
                Put(ProxyMovement, Arg::Boolean("bRequestedMoveUseAcceleration", true));
                Put(ProxyMovement, Arg::Boolean("bUseAccelerationForPaths", false));
                Put(ProxyMovement, Arg::Boolean("bIsInFollowState", false));
                Put(actor, Arg::Object("Mesh", ProxyMesh));
                Add(ProxyMesh); Add(ProxyAnim); Add(ProxyABPSetting); Add(ProxyMoveAnim); Add(ProxyLocomotionAnim);
                Put(ProxyMesh, Arg::Boolean("bPauseAnims", false));
                Put(ProxyMesh, Arg::Float("GlobalAnimRateScale", 1.f));
                Put(ProxyAnim, Arg::Boolean("bDisableABPMove", false));
                Put(ProxyAnim, Arg::Object("ABPSetting", ProxyABPSetting));
                Put(ProxyAnim, Arg::Float("MappedSpeed", 123.f));
                Put(ProxyMoveAnim, Arg::Enum("FinalABPMoveMode", 10));
                Put(ProxyMoveAnim, Arg::Boolean("IsMoveAccelerating", true));
                objects[ProxyABPSetting].pathProperties.emplace("CommonSetting.ABPMoveMode", Arg::Enum("ABPMoveMode", 10));
                objects[ProxyABPSetting].pathProperties.emplace("MotionMatchingSetting.SpareMoveMode", Arg::Enum("SpareMoveMode", 11));
                Put(ProxyLocomotionAnim, Arg::Boolean("bIdleToMove", true));
                Put(ProxyLocomotionAnim, Arg::Boolean("bMoveStop", false));
                Put(ProxyLocomotionAnim, Arg::Float("MoveLoopFwdBlendAlpha", .5f));
                Put(ProxyLocomotionAnim, Arg::Float("MappedSpeed", 234.f));
                Put(ProxyLocomotionAnim, Arg::Enum("GaitGroundedState", 3));
                Put(ProxyLocomotionAnim, Arg::Enum("MoveGaitGroundedState", 2));
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
            const auto actor = Named(args, "Actor")->Get<std::uintptr_t>();
            specialTrace.push_back("finish");
            if (ResolveWeak(specialRoot) == actor && specialObserveOnFinish) specialObserved = true;
            Output(args, "ReturnValue", actor);
            return {true, {}};
        }
        if (function.ends_with(L":BGUGetFloatAttr"))
        {
            Output(args, "ReturnValue", Named(args, "Unit")->Get<std::uintptr_t>() == Player ? playerHealth : proxyHealth);
            return {true, {}};
        }
        if (function == L"/Script/b1-Managed.AutoTestHelperLib:IsPlayingCameraMovie")
        {
            CHECK(args.size() == 2 && Named(args, "WorldContext")->Get<std::uintptr_t>() == trans::context.world);
            CHECK(Named(args, "ReturnValue")->type == Type::Bool);
            if (failMovieReadCalls > 0) { --failMovieReadCalls; return {false, "movie state unavailable"}; }
            Output(args, "ReturnValue", std::int32_t(playingMovie));
            return {true, {}};
        }
        if (function.ends_with(L":BGUSetBehaviorTree") || function.ends_with(L":SetTamerOwner")) return {true, {}};
        if (function.ends_with(L":SetActorRotationSimple"))
        {
            CHECK(Named(args, "Actor")->Get<std::uintptr_t>() == Proxy());
            ++actorRotationCalls;
            return {true, {}};
        }
        if (function.ends_with(L":AddMovementInput"))
        {
            const std::array contract{
                ParameterContract{"WorldDirection", Type::Vector, sizeof(Vec3), false},
                ParameterContract{"ScaleValue", Type::Float, sizeof(float), false},
                ParameterContract{"bForce", Type::Bool, sizeof(std::int32_t), false}};
            CHECK(MatchesContract(args, contract) && instance == Proxy());
            if (!MatchesContract(args, contract) || instance != Proxy()) return {false, "invalid directional input signature"};
            CHECK(!Named(args, "bForce")->Get<std::int32_t>());
            // Input supplements an existing MM mover, never replaces it or
            // bypasses pause/skill/state checks. Physics is not mocked here.
            CHECK(activeMovementRequest > 0 && !movementRequests.empty() && movementRequests.back().mmState > 0);
            CHECK(!playingMovie && !overlayVisible && foreground && castingSkillId <= 0);
            const auto direction = Named(args, "WorldDirection")->Get<Vec3>();
            const float scale = Named(args, "ScaleValue")->Get<float>();
            CHECK(std::abs(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z - 1.) < .00001);
            CHECK(scale > 0.f && scale <= 1.f);
            if (failDirectionInputCalls > 0) { --failDirectionInputCalls; return {false, "directional input failed"}; }
            directionInputs.push_back({direction, scale});
            return {true, {}};
        }
        if (function == L"/Script/Engine.Pawn:GetController")
        {
            if (!MatchesContract(args, ObjectReturnContract) || instance != Proxy()) return {false, "invalid controller query"};
            Output(args, "ReturnValue", ProxyController);
            return {true, {}};
        }
        // AController.GetControlledPawn's UMetaPath/OriginalName is K2_GetPawn.
        // The managed method name deliberately has no successful mock branch.
        if (function == L"/Script/Engine.Controller:K2_GetPawn")
        {
            if (!MatchesContract(args, ObjectReturnContract) || instance != ProxyController) return {false, "invalid controlled pawn query"};
            ++controlledPawnReads;
            Output(args, "ReturnValue", Proxy());
            return {true, {}};
        }
        if (function == L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocation" ||
            function == L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocationWithMM")
        {
            const bool mm = function.ends_with(L"WithMM");
            if (!(mm ? MatchesContract(args, MoveWithMMContract) : MatchesContract(args, MoveToContract))) return {false, "native movement signature mismatch"};
            CHECK(Named(args, "Unit")->Get<std::uintptr_t>() == Proxy());
            CHECK(args.size() == (mm ? 9 : 8) && Named(args, "Position")->type == Type::Vector);
            CHECK(Named(args, "SpeedRateType")->type == Type::Enum && Named(args, "MoveAIType")->type == Type::Enum);
            CHECK(Named(args, "AcceptableRadius")->Get<float>() == 5.f);
            CHECK(Named(args, "IncludeSelfRadius")->type == Type::Bool && !Named(args, "IncludeSelfRadius")->Get<int>());
            CHECK(Named(args, "IncludeTargetRadius")->type == Type::Bool && !Named(args, "IncludeTargetRadius")->Get<int>());
            if (failMovementRequestCalls > 0) { --failMovementRequestCalls; return {false, "movement request failed"}; }
            const int id = movementReturnOverride >= 0 ? movementReturnOverride : nextMovementRequest++;
            movementRequests.push_back({Named(args, "Position")->Get<Vec3>(), Named(args, "SpeedRateType")->Get<std::uint8_t>(), Named(args, "MoveAIType")->Get<std::uint8_t>(), id,
                mm ? Named(args, "MotionMatchingState")->Get<std::uint8_t>() : -1});
            if (id > 0) { activeMovementRequest = id; moverFacingOverride = false; }
            Output(args, "ReturnValue", id);
            return {true, {}};
        }
        if (function == L"/Script/b1-Managed.BGUFunctionLibraryCS:BGUAISetSpeedRate")
        {
            const std::array contract{ParameterContract{"Unit", Type::Object, sizeof(std::uintptr_t)}, ParameterContract{"SpeedRate", Type::Float, sizeof(float)}};
            CHECK(MatchesContract(args, contract));
            CHECK(Named(args, "Unit")->Get<std::uintptr_t>() == Proxy());
            const auto multiplier = Named(args, "SpeedRate")->Get<float>();
            CHECK(std::isfinite(multiplier) && multiplier >= .1f && multiplier <= 5.f);
            if (failSpeedMultiplierCalls > 0)
            {
                --failSpeedMultiplierCalls;
                if (speedFailureState != CallState::NotCalled) customSpeedMultiplier = multiplier;
                return {false, "injected speed multiplier failure", speedFailureState};
            }
            if (multiplier != 1.f)
                CHECK(!playingMovie && !overlayVisible && foreground && castingSkillId <= 0 && activeMovementRequest > 0);
            customSpeedMultiplier = multiplier;
            speedMultiplierWrites.push_back(multiplier);
            return {true, {}, CallState::Completed};
        }
        if (function == L"/Script/b1-Managed.BGUFuncLibAICS:BGUCancelAIMoveTo")
        {
            if (!MatchesContract(args, CancelMoveContract)) return {false, "native indexed cancellation signature mismatch"};
            CHECK(Named(args, "Unit")->Get<std::uintptr_t>() == Proxy());
            CHECK(args.size() == 2 && Named(args, "RequestIdx")->type == Type::Int);
            const int id = Named(args, "RequestIdx")->Get<int>();
            CHECK(id > 0);
            cancelledMovementRequests.push_back(id);
            if (failMovementCancelCalls > 0) { --failMovementCancelCalls; return {false, "movement cancellation failed"}; }
            if (activeMovementRequest == id) { activeMovementRequest = 0; moverFacingOverride = false; } // Real StopMoveWithRequestIdx contract.
            return {true, {}};
        }
        if (function.ends_with(L":GetLastMovementInputVector"))
        {
            const std::array contract{ParameterContract{"ReturnValue", Type::Vector, sizeof(Vec3), true}};
            CHECK(MatchesContract(args, contract) && instance == Proxy());
            ++lastInputReads;
            if (failLastInputRead) return {false, "last input unreadable"};
            Output(args, "ReturnValue", lastMovementInput);
            return {true, {}};
        }
        if (function.ends_with(L":GetControlRotation"))
        {
            Output(args, "ReturnValue", eng::Rotator{0., controlYaw, 0.});
            return {true, {}};
        }
        if (function == L"/Script/Engine.Actor:K2_GetActorRotation")
        {
            CHECK(instance == Proxy() && args.size() == 1 && args[0].type == Type::Rotator && args[0].output);
            Output(args, "ReturnValue", eng::Rotator{0., observedActorYaw, 0.});
            return {true, {}};
        }
        if (function.ends_with(L":BGUGetCastingSkillID"))
        {
            Output(args, "ReturnValue", castingSkillId);
            return {true, {}};
        }
        if (function.ends_with(L":BGUHasUnitSimpleState"))
        {
            const auto unit = Named(args, "Unit")->Get<std::uintptr_t>();
            const auto state = static_cast<int>(Named(args, "SimpleState")->Get<std::uint8_t>());
            const auto immune = immuneLayers[unit] > 0;
            const auto found = simpleStates.find(unit);
            const bool present = state == 1 ? immune : found != simpleStates.end() && found->second.contains(state);
            Output(args, "ReturnValue", std::int32_t(present));
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
            Output(args, "ReturnValue", lockedTarget);
            return {true, {}};
        }
        if (function.ends_with(L":BGURequestAIUseSkillWithSkillID"))
        {
            ++skillRequests;
            const auto requested = Named(args, "SkillID")->Get<int>();
            requestedSkillIds.push_back(requested);
            const auto returned = skillRequestReturnOverride >= 0 ? skillRequestReturnOverride : requested;
            skillRequestReturnOverride = -1;
            Output(args, "ReturnValue", returned);
            return {true, {}};
        }
        if (function.ends_with(L":GetUnitAllSkillID"))
        {
            const auto unit = Named(args, "Unit")->Get<std::uintptr_t>();
            const auto &skillIds = useInstanceSkillIds && unit == Proxy() ? instanceSkillIds : availableSkillIds;
            auto *out = Named(args, "ReturnValue");
            out->bytes.resize(skillIds.size() * sizeof(int));
            if (!skillIds.empty()) std::memcpy(out->bytes.data(), skillIds.data(), out->bytes.size());
            return {true, {}};
        }
        if (function.ends_with(L":K2_DestroyActor"))
        {
            destroyRequests.push_back(instance);
            if ((instance == Proxy() || std::find(ownedChildren.begin(), ownedChildren.end(), instance) != ownedChildren.end()) && cleanupScopeActive)
            {
                ++guardedProxyDestroyCalls;
                cleanupActors.push_back(instance);
            }
            if (deferredDestroyCalls[instance] > 0)
            {
                --deferredDestroyCalls[instance];
                return {true, {}}; // Request ran, but the object is still resolving until a later frame.
            }
            if (const auto child = spawnChildOnDestroy.find(instance); child != spawnChildOnDestroy.end())
                AddOwnedChild(child->second);
            objects[instance].alive = false;
            return {true, {}};
        }
        return {false, "unhandled mock function"};
    }
    bool Property(std::uintptr_t object, Arg &value, bool write)
    {
        if (!fixture::Alive(object)) return false;
        const std::string_view propertyName(value.name);
        if ((object == fixture::ProxyAnim && propertyName == "MappedSpeed") || object == fixture::ProxyMoveAnim || object == fixture::ProxyLocomotionAnim)
        {
            ++fixture::diagnosticLinkedPropertyReads;
            if (write) { ++fixture::diagnosticWrites; CHECK(false); return false; }
            const auto expected = propertyName == "MappedSpeed" || propertyName == "MoveLoopFwdBlendAlpha" ? Type::Float :
                propertyName == "FinalABPMoveMode" || propertyName == "GaitGroundedState" || propertyName == "MoveGaitGroundedState" ? Type::Enum : Type::Bool;
            const std::size_t size = expected == Type::Enum ? 1 : 4;
            CHECK(value.type == expected && value.bytes.size() == size);
            const auto *reflected = fixture::PropertyValue(object, value.name);
            if (!reflected || value.type != expected || value.bytes.size() != size || reflected->type != expected || reflected->bytes.size() != size) return false;
        }
        const bool animationField = (object == fixture::Proxy() && propertyName == "Mesh") ||
            (object == fixture::ProxyMesh && (propertyName == "bPauseAnims" || propertyName == "GlobalAnimRateScale")) ||
            (object == fixture::ProxyAnim && (propertyName == "bDisableABPMove" || propertyName == "ABPSetting")) ||
            (object == fixture::ProxyMovement && propertyName == "bIsInFollowState");
        if (animationField)
        {
            ++fixture::diagnosticAnimationPropertyReads;
            if (object == fixture::ProxyAnim) ++fixture::diagnosticSpecificAnimReads;
            if (write) { ++fixture::diagnosticWrites; CHECK(false); return false; }
            const auto expected = propertyName == "Mesh" || propertyName == "ABPSetting" ? Type::Object :
                propertyName == "GlobalAnimRateScale" ? Type::Float : Type::Bool;
            const std::size_t size = expected == Type::Object ? 8 : 4;
            CHECK(value.type == expected && value.bytes.size() == size);
            const auto *reflected = fixture::PropertyValue(object, value.name);
            if (!reflected || value.type != expected || value.bytes.size() != size || reflected->type != expected || reflected->bytes.size() != size) return false;
        }
        if (object == fixture::ProxyMovement && (std::string_view(value.name) == "Velocity" ||
            std::string_view(value.name) == "bRequestedMoveUseAcceleration" || std::string_view(value.name) == "bUseAccelerationForPaths"))
        {
            ++fixture::diagnosticPropertyReads;
            if (write) { ++fixture::diagnosticWrites; CHECK(false); return false; }
            const auto expected = std::string_view(value.name) == "Velocity" ? Type::Vector : Type::Bool;
            const std::size_t size = expected == Type::Vector ? 24 : 4;
            CHECK(value.type == expected && value.bytes.size() == size);
            auto *reflected = fixture::PropertyValue(object, value.name);
            if (!reflected || value.type != expected || value.bytes.size() != size || reflected->type != expected || reflected->bytes.size() != size) return false;
        }
        if (!write && object == fixture::SpringArm && std::string_view(value.name) == "TargetArmLength" && fixture::failArmReads > 0)
        {
            --fixture::failArmReads;
            return false;
        }
        auto *property = fixture::PropertyValue(object, value.name);
        if (!property) return false;
        if (write)
        {
            if (object == fixture::SpringArm && std::string_view(value.name) == "TargetArmLength" && fixture::failArmWrites > 0)
            {
                --fixture::failArmWrites;
                return false;
            }
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
    bool PropertyPath(std::uintptr_t object, std::span<const std::string_view> path, Arg &value, bool write)
    {
        if (object == fixture::ProxyABPSetting)
        {
            ++fixture::diagnosticSettingReads;
            if (write) { ++fixture::diagnosticWrites; CHECK(false); return false; }
            CHECK(path.size() == 1 && value.type == Type::Enum && value.bytes.size() == 1);
            if (path.size() != 1 || value.type != Type::Enum || value.bytes.size() != 1) return false;
            const auto key = std::string(path[0]) + "." + value.name;
            const auto found = fixture::objects.at(object).pathProperties.find(key);
            if (found == fixture::objects.at(object).pathProperties.end() || found->second.type != Type::Enum || found->second.bytes.size() != 1) return false;
            value = found->second;
            return true;
        }
        if ((object == fixture::CDOConfig || object == fixture::ConfigUnit) && path.size() == 1 && path[0] == "UnitCDesc")
        {
            if (write) return true;
            if (std::string_view(value.name) == "ResID") value = Arg::Int("ResID", 830001);
            else if (std::string_view(value.name) == "OverrideID") value = Arg::Int("OverrideID", 0);
            else return false;
            return true;
        }
        if (write) return false;
        const auto &pathProperties = fixture::objects.at(object).pathProperties;
        if (path.size() != 1) return false;
        std::string key(path[0]);
        key += ".";
        key += value.name;
        if (path[0] == "BattleSC_GUID" && std::string_view(value.name) != "TagName") return false;
        if (path[0] == "WakeUpConfig" && std::string_view(value.name) != "WakeUpLinkEnable" &&
            std::string_view(value.name) != "CanTriggerWakeUpLinkWhenDead") return false;
        if (path[0] != "BattleSC_GUID" && path[0] != "WakeUpConfig") return false;
        const auto found = pathProperties.find(key);
        if (found == pathProperties.end()) return false;
        value = found->second;
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
    void ResetSprintInput() { fixture::movementInput.sprint = false; }
    bool SetLocomotionFacing(std::uintptr_t actor, const extensions::Vec3 &direction)
    {
        using namespace fixture;
        CHECK(actor == Proxy() && activeMovementRequest > 0 && !movementRequests.empty());
        CHECK(movementRequests.back().moveAIType == 1 && movementRequests.back().mmState > 0);
        CHECK(!lockedTarget && !playingMovie && !overlayVisible && foreground && castingSkillId <= 0);
        if (failFacingCalls > 0) { --failFacingCalls; return false; }
        facingDirections.push_back(direction);
        moverFacingOverride = true;
        return true;
    }
    MotionMatchingSelection SelectMotionMatching(std::uintptr_t actor, int speedType, bool locked)
    {
        CHECK(actor == fixture::Proxy() && speedType >= 0 && speedType <= 2);
        ++fixture::movementAnimationQueries;
        fixture::lastAnimationSpeed = speedType;
        fixture::lastAnimationLocked = locked;
        return fixture::movementAnimationChoice;
    }
    AnimationStateSnapshot InspectAnimationState(std::uintptr_t proxy, std::uintptr_t topAnim, std::uintptr_t moveAnim)
    {
        ++fixture::diagnosticDataCalls;
        CHECK(proxy == fixture::Proxy() && topAnim == fixture::ProxyAnim && moveAnim == fixture::ProxyMoveAnim);
        CHECK(fixture::diagnosticHumanoid && fixture::diagnosticAnimBase && fixture::diagnosticMoveClass && !fixture::diagnosticMoveNull);
        return fixture::diagnosticData;
    }
    ScopedCleanup::ScopedCleanup(std::uintptr_t actor)
    {
        ++fixture::cleanupConstructs;
        active_ = actor != 0 && (actor == fixture::Proxy() ||
            std::find(fixture::ownedChildren.begin(), fixture::ownedChildren.end(), actor) != fixture::ownedChildren.end()) &&
            fixture::Alive(actor) && !fixture::cleanupScopeActive;
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
    bool RunManagedOperation(bool (*operation)(void *), void *state) { return operation && operation(state); }
    engine::WeakObject OwnedComponentOwner(std::uintptr_t) { return {}; }
    bool Ready() { return fixture::nativeAdapterReady; }
    bool SetProxy(engine::WeakObject) { fixture::specialTrace.push_back("setProxy"); fixture::deathRequested = false; return true; }
    void ClearProxy() { fixture::deathRequested = false; }
    std::vector<engine::WeakObject> OwnedChildren(bool deadOnly)
    {
        std::vector<engine::WeakObject> result;
        for (auto child = fixture::ownedChildren.rbegin(); child != fixture::ownedChildren.rend(); ++child)
        {
            if (!fixture::Alive(*child) || (deadOnly && !fixture::objects[*child].dead)) continue;
            result.push_back(engine::MakeWeak(*child));
        }
        return result;
    }
    bool SetInputOwner(engine::WeakObject) { return true; }
    std::vector<Action> ConsumeActions()
    {
        auto actions = std::move(fixture::pendingActions);
        fixture::pendingActions.clear();
        return actions;
    }
    MoveInput MovementAxes() { return fixture::movementInput; }
    bool DeathRequested() { return fixture::deathRequested; }
    bool SetTamerType(std::uintptr_t, int) { return true; }
    bool Initialized(std::uintptr_t) { return true; }
    bool CopyTeam(std::uintptr_t, std::uintptr_t) { return true; }
    bool AcquirePlayerIsolation(std::uintptr_t actor, PlayerIsolation &lease)
    {
        using namespace fixture;
        CHECK(actor == Player && !lease.token && !isolationOwner);
        isolationPlayer = engine::MakeWeak(actor); isolationOwner = &lease; lease.token = ++isolationTokenCounter;
        isolationShadowValid = true; isolationShadowHidden = Get<std::int32_t>(Player, "bHidden") != 0;
        constexpr std::array properties{302, 13, 202, 105};
        for (std::size_t i = 0; i < properties.size(); ++i)
        {
            isolationHandles[i] = ++isolationHandleCounter;
            isolationRequests.push_back({isolationHandles[i], properties[i], i == 0 ? 1 : 0});
        }
        bool ok = true;
        for (const auto property : {13, 202, 105, 302}) ok &= ApplyIsolationProperty(property, true);
        return ok;
    }
    bool RefreshPlayerIsolation(PlayerIsolation &lease)
    {
        using namespace fixture;
        if (isolationOwner != &lease || !lease.token || engine::ResolveWeak(isolationPlayer) != Player) return false;
        bool ok = true;
        if (!Get<std::int32_t>(Player, "bHidden")) ok &= ApplyIsolationProperty(302, true);
        if (Get<std::uint8_t>(Movement, "MovementMode") != 0) ok &= ApplyIsolationProperty(13, true);
        if (capsuleCollision != 0) ok &= ApplyIsolationProperty(202, true);
        if (meshCollision != 0) ok &= ApplyIsolationProperty(105, true);
        return ok && Get<std::int32_t>(Player, "bHidden") && Get<std::uint8_t>(Movement, "MovementMode") == 0 && !capsuleCollision && !meshCollision;
    }
    bool ReleasePlayerIsolation(PlayerIsolation &lease)
    {
        using namespace fixture;
        if (!lease.token) return true;
        if (isolationOwner != &lease) return false;
        const auto clear = [&] { lease.token = 0; isolationOwner = nullptr; isolationHandles = {}; return true; };
        if (engine::ResolveWeak(isolationPlayer) != Player) return clear();
        bool ok = true;
        constexpr std::array properties{302, 13, 202, 105};
        for (std::size_t i = 0; i < isolationHandles.size(); ++i)
        {
            std::erase_if(isolationRequests, [&](const auto &request) { return request.handle == isolationHandles[i]; });
            ok &= ApplyIsolationProperty(properties[i], true, i == 0);
        }
        for (const auto property : {13, 202, 105, 302}) ok &= ApplyIsolationProperty(property, true, property == 302);
        return ok && clear();
    }
    WeaponReadiness CheckWeaponsReady(std::uintptr_t actor, std::uintptr_t sourceConfig)
    {
        ++fixture::weaponReadinessCalls;
        CHECK(actor == fixture::Proxy() && sourceConfig == fixture::ConfigUnit);
        // Counters can include a previously restored session. The dedicated
        // readiness test compares mutations while Pending in this session.
        CHECK(!fixture::isolationOwner && !IsActive());
        return fixture::weaponReadiness;
    }
    bool InitializeWeapons(std::uintptr_t actor, std::uintptr_t sourceConfig)
    {
        ++fixture::weaponInitializeCalls;
        CHECK(actor == fixture::Proxy() && sourceConfig == fixture::ConfigUnit);
        CHECK(fixture::Get<std::int32_t>(actor, "bHidden") == 0);
        CHECK(fixture::Get<std::int32_t>(fixture::Player, "bHidden") == 1);
        CHECK(!IsActive()); // No user skill can precede the one-time repair.
        if (fixture::failWeaponInitializeCalls > 0) { --fixture::failWeaponInitializeCalls; return false; }
        return true;
    }
    bool SetTarget(std::uintptr_t, std::uintptr_t target)
    {
        fixture::forwardedTargets.push_back(target);
        if (target == 0)
        {
            ++fixture::targetClearCalls;
            if (fixture::failTargetClearCalls > 0)
            {
                --fixture::failTargetClearCalls;
                return false;
            }
        }
        return true;
    }
}

namespace gloss::gameplay::transformation::special
{
    // This models only driver ordering and capability/fault propagation. The
    // dedicated executable tests the real special-form hooks and GC boundary.
    void Initialize(Host host) { CHECK(host.componentOwner && host.managedOperation); }
    bool Ready() { return fixture::specialAdapterReady; }
    bool SetSession(engine::WeakObject root)
    {
        using namespace fixture;
        ++specialSetSessionCalls; specialTrace.push_back("setSession");
        if (!Ready() || !engine::ResolveWeak(root) || specialRoot.serial > 0 ||
            std::find(specialTrace.begin(), specialTrace.end(), "finish") != specialTrace.end()) return false;
        specialRoot = root; specialObserved = false;
        return true;
    }
    void ClearSession()
    {
        using namespace fixture;
        ++specialClearCalls; specialTrace.push_back("clearSession");
        if (specialRoot.serial > 0) CHECK(!engine::ResolveWeak(specialRoot)); // Keep isolation through synchronous destruction.
        specialRoot = {}; specialObserved = false;
    }
    Status Check(engine::WeakObject root)
    {
        using namespace fixture;
        ++specialCheckCalls; specialTrace.push_back("check");
        if (!Ready()) return {false, false, 0, 0, 0, Fault::Unavailable};
        if (root.serial <= 0 || root.serial != specialRoot.serial || root.index != specialRoot.index)
            return {false, false, 0, 0, 0, Fault::NoSession};
        return {specialObserved, specialObserved && specialHealthy, specialObserved ? 1u : 0u, 0, specialObserved ? 1u : 0u,
            !specialObserved ? Fault::NotObserved : specialHealthy ? Fault::None : Fault::PauseFailed};
    }
    std::string_view Reason(Fault fault)
    {
        if (fault == Fault::NoSession) return "special FSM session was not registered before FinishSpawning";
        if (fault == Fault::NotObserved) return "special FSM startup was not observed";
        if (fault == Fault::PauseFailed) return "special FSM health failed";
        return "special FSM adapter unavailable in ordinary fixture";
    }
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
    void RegisterHotkeyConflictProbe(std::function<std::string(unsigned, unsigned)> probe) { fixture::hotkeyConflictProbe = std::move(probe); }
    Result RunHotkeyTransaction(std::function<Result()> operation) { return operation(); }
}
namespace gloss
{
    void Log(std::string_view message) { fixture::diagnosticLogs.emplace_back(message); }
    const std::filesystem::path &DataDirectory()
    {
        static const std::filesystem::path path = std::filesystem::temp_directory_path() / L"GlossTrainerTransformationRuntimeTests";
        return path;
    }
    namespace overlay { bool Visible() { return fixture::overlayVisible; } }
}

namespace fixture
{
    void CinematicSameValueRequestsAndDirectHiddenWritesRemainOwnedByTheGame()
    {
        for (bool directHidden : {false, true})
        {
            InitializeWorld(); StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            CHECK(objects[Player].collision["enabled"] && !capsuleCollision && !meshCollision);
            const auto ours = isolationHandles;
            if (directHidden) ExternalHidden(true); // Same true value still belongs to the movie.
            else ExternalIsolationProperty(302, 1);
            ExternalIsolationProperty(13, 0);
            ExternalIsolationProperty(202, 0);
            ExternalIsolationProperty(105, 0);
            objects[Player].collision["enabled"] = false; // Actor flag is entirely outside this module's lease.
            const Vec3 sequencePosition{6789., 4321., 50.};
            objects[Player].locations["actor"] = sequencePosition;
            playingMovie = true;
            Tick(); CHECK(trans::GetStatus().phase == trans::Phase::Active);
            CHECK(trans::GetStatus().message.find("暂挂变身") != std::string::npos);
            CHECK(trans::Restore().success); Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 1 && Get<std::uint8_t>(Movement, "MovementMode") == 0);
            CHECK(!capsuleCollision && !meshCollision && !objects[Player].collision["enabled"]);
            CHECK(isolationRequests.size() == (directHidden ? 3 : 4));
            for (const auto handle : ours)
                CHECK(std::none_of(isolationRequests.begin(), isolationRequests.end(), [&](const auto &request) { return request.handle == handle; }));
            const auto position = objects[Player].locations["actor"];
            CHECK(position.x == sequencePosition.x && position.y == sequencePosition.y && position.z == sequencePosition.z);
            CHECK(!isolationOwner && !immuneLayers[Player]);
        }
        std::cout << "PASS cinematic same-value hidden/movement/collision requests and direct hidden writes survive removal of only trainer-owned leases\n";
    }
    void UnreadableCinematicStateRestoresWithoutTeleportingTheRetainedPlayer()
    {
        for (bool explicitRestore : {false, true})
        {
            InitializeWorld(); StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            const auto proxy = Proxy(), tamer = Tamer();
            const Vec3 performerPosition{4567., -1234., 89.};
            objects[Player].locations["actor"] = performerPosition;
            objects[proxy].locations["actor"] = {-999., -999., -999.};
            ExternalHidden(false);
            objects[Player].collision["enabled"] = true;
            ExternalIsolationProperty(13, 5);
            Put(SpringArm, eng::Arg::Float("TargetArmLength", 987.f));
            // Unknown movie state is not a negative observation. The explicit
            // restore path has one read; Update failure followed by restore has two.
            failMovieReadCalls = explicitRestore ? 1 : 2;
            if (explicitRestore) CHECK(trans::Restore().success);
            Tick();
            if (!explicitRestore)
            {
                CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
                CHECK(trans::GetStatus().message.find("无法校验过场状态") != std::string::npos);
                Tick();
            }
            CHECK(trans::GetStatus().phase == trans::Phase::Idle && !failMovieReadCalls);
            const auto location = objects[Player].locations["actor"];
            CHECK(location.x == performerPosition.x && location.y == performerPosition.y && location.z == performerPosition.z);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0 && objects[Player].collision["enabled"]);
            CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 5 && Get<float>(SpringArm, "TargetArmLength") == 987.f);
            CHECK(!Alive(proxy) && !Alive(tamer) && immuneLayers[Player] == 0);
        }
        std::cout << "PASS unreadable cinematic state restores owned objects without teleporting the retained player or overwriting unknown sequence state\n";
    }
    void CinematicPausesAndResumesTheSameCharacterWithoutOverwritingThePerformer()
    {
        for (int trigger = 0; trigger != 4; ++trigger)
        {
            InitializeWorld(); StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            movementInput = {1.f, 0.f}; Tick();
            const auto ownRequest = activeMovementRequest;
            const auto proxy = Proxy(), tamer = Tamer();
            const auto originalMode = Get<std::uint8_t>(ProxyMovement, "MovementMode");
            const auto activeId = trans::GetStatus().activeId;
            const Vec3 performerPosition{12345., -6789., 123.};
            objects[Player].locations["actor"] = performerPosition;
            objects[proxy].locations["actor"] = {-500., -500., -500.};
            ExternalHidden(false);
            objects[Player].collision["enabled"] = true;
            ExternalIsolationProperty(13, 5);
            Put(SpringArm, eng::Arg::Float("TargetArmLength", 1234.f));
            if (trigger == 0 || trigger == 3) playingMovie = true;
            if (trigger == 1) simpleStates[Player].insert(56);
            if (trigger == 2) simpleStates[Player].insert(171);
            if (trigger == 3) CHECK(trans::Restore().success); // Restore itself must recheck a newly started movie.
            Tick();
            if (trigger != 3)
            {
                CHECK(trans::GetStatus().phase == trans::Phase::Active);
                CHECK(trans::GetStatus().message.find("暂挂变身") != std::string::npos);
                CHECK(Alive(proxy) && Alive(tamer) && trans::GetStatus().activeId == activeId);
                CHECK(!isolationOwner && !immuneLayers[Player]);
                CHECK(Get<std::int32_t>(proxy, "bHidden") == 1 && !objects[proxy].collision["enabled"]);
                CHECK(Get<std::uint8_t>(ProxyMovement, "MovementMode") == 0);
                CHECK(objects[Player].locations["actor"].x == performerPosition.x);
                CHECK(Get<float>(SpringArm, "TargetArmLength") == 1234.f);
                CHECK(trans::TriggerSkill(41001).success);
                pendingActions.push_back(trans::Action::LightAttack);
                Tick();
                CHECK(skillRequests == 0 && pendingActions.empty());
                // All three observations must clear and remain clear through
                // blend-out. The proxy resumes at the performer's final spot.
                playingMovie = false; simpleStates[Player].clear();
                Tick(); Tick(100);
                CHECK(Get<std::int32_t>(proxy, "bHidden") == 1 && !isolationOwner);
                Tick(151);
                CHECK(trans::GetStatus().phase == trans::Phase::Active && trans::GetStatus().activeId == activeId);
                CHECK(trans::GetStatus().message.find("恢复原变身角色") != std::string::npos);
                CHECK(Alive(proxy) && Alive(tamer) && Proxy() == proxy && Tamer() == tamer);
                CHECK(Get<std::int32_t>(proxy, "bHidden") == 0 && objects[proxy].collision["enabled"]);
                CHECK(Get<std::uint8_t>(ProxyMovement, "MovementMode") == originalMode);
                CHECK(objects[proxy].locations["actor"].x == performerPosition.x && objects[proxy].locations["actor"].y == performerPosition.y);
                CHECK(Get<std::int32_t>(Player, "bHidden") == 1 && Get<std::uint8_t>(Movement, "MovementMode") == 0);
                CHECK(isolationOwner && immuneLayers[Player] == 1 && skillRequests == 0);
                CHECK(weaponInitializeCalls == 1 && movementRequests.size() == 1);
                CHECK(trans::Restore().success); Tick();
            }
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            const auto location = objects[Player].locations["actor"];
            CHECK(location.x == performerPosition.x && location.y == performerPosition.y && location.z == performerPosition.z);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0 && objects[Player].collision["enabled"]);
            CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 5 && Get<std::uint8_t>(Movement, "CustomMovementMode") == 0);
            CHECK(Get<float>(SpringArm, "TargetArmLength") == 1234.f);
            CHECK(cancelledMovementRequests == std::vector<int>{ownRequest});
            CHECK(!Alive(proxy) && !Alive(tamer) && immuneLayers[Player] == 0);
            CHECK(weaponInitializeCalls == 1 && movementRequests.size() == 1);
        }
        std::cout << "PASS movies and sequence states pause then resume the same character; manual restore remains available during movies\n";
    }
    void CinematicRetainsChildVisibilityAndResumesOnlyAfterStableBlendOut()
    {
        InitializeWorld(); StartAndReachActivation();
        const auto proxy = Proxy();
        constexpr std::uintptr_t weapon = 0x29000, lateChild = 0x29010;
        AddOwnedChild(weapon); Put(weapon, eng::Arg::Boolean("bHidden", true));
        objects[weapon].collision["enabled"] = true;
        playingMovie = true; Tick();
        AddOwnedChild(lateChild); Put(lateChild, eng::Arg::Boolean("bHidden", false));
        objects[lateChild].collision["enabled"] = true;
        Tick();
        CHECK(Get<std::int32_t>(lateChild, "bHidden") == 1 && !objects[lateChild].collision["enabled"]);
        playingMovie = false; Tick(); Tick(200);
        simpleStates[Player].insert(171); Tick(); // Resets quiet interval.
        simpleStates[Player].clear(); Tick(); Tick(200);
        CHECK(Get<std::int32_t>(proxy, "bHidden") == 1 && !isolationOwner);
        Tick(51);
        CHECK(Get<std::int32_t>(proxy, "bHidden") == 0 && isolationOwner);
        CHECK(Get<std::int32_t>(weapon, "bHidden") == 1 && objects[weapon].collision["enabled"]);
        CHECK(Get<std::int32_t>(lateChild, "bHidden") == 0 && objects[lateChild].collision["enabled"]);
        playingMovie = true; Tick();
        const Vec3 end{99., 88., 77.}; objects[Player].locations["actor"] = end;
        // A context transition cancels resumption and never teleports the old
        // performer, even if the new world's movie query would return false.
        playingMovie = false;
        trans::Tick(Player, Controller, World, 2, true);
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && !Alive(proxy));
        CHECK(objects[Player].locations["actor"].x == end.x && objects[Player].locations["actor"].y == end.y);
        CHECK(!immuneLayers[Player] && !isolationOwner);
        std::cout << "PASS cinematic child states, repeated sequence transitions, blend-out debounce and context cancellation\n";
    }
    void CinematicResumeFailureReleasesReacquiredStateWithoutRespawning()
    {
        InitializeWorld(); StartAndReachActivation();
        const auto proxy = Proxy(), tamer = Tamer();
        playingMovie = true; Tick();
        playingMovie = false; Tick();
        failCollisionEnable = 1; Tick(251);
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && !Alive(proxy) && !Alive(tamer));
        CHECK(!immuneLayers[Player] && !isolationOwner && immuneAddCalls == 2 && immuneRemoveCalls == 2);
        CHECK(weaponInitializeCalls == 1 && Get<std::int32_t>(Player, "bHidden") == 0);
        std::cout << "PASS failed cinematic resumption releases exactly the reacquired player state and keeps weapon initialization single-shot\n";
    }
    void CinematicResumeRechecksBothLivesAndSpecialStateBeforeAnyPlayerMutation()
    {
        for (int fault = 0; fault < 3; ++fault)
        {
            InitializeWorld(); StartAndReachActivation();
            const auto proxy = Proxy();
            playingMovie = true; Tick(); playingMovie = false; Tick();
            const auto armBefore = armWrites, immunityBefore = immuneAddCalls;
            if (fault == 0) proxyHealth = 0.f;
            if (fault == 1) playerHealth = 0.f;
            if (fault == 2)
            {
                trans::prepared.requiresSpecialFsm = true;
                specialHealthy = false;
            }
            Tick(251);
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(Get<std::int32_t>(proxy, "bHidden") == 1 && !isolationOwner);
            CHECK(armWrites == armBefore && immuneAddCalls == immunityBefore);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle && !Alive(proxy) && !immuneLayers[Player]);
        }
        std::cout << "PASS cinematic resume rechecks original/proxy health and special-state identity before reacquiring player state\n";
    }
    void CinematicActivationIsRejectedBeforePlayerMutation()
    {
        for (int trigger = 0; trigger != 3; ++trigger)
        {
            InitializeWorld();
            if (trigger == 0) playingMovie = true;
            if (trigger == 1) simpleStates[Player].insert(56);
            if (trigger == 2) simpleStates[Player].insert(171);
            StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(trans::GetStatus().message.find("过场动画中暂停变身") != std::string::npos);
            CHECK(!playerBaselineWrites && !weaponInitializeCalls && !immuneAddCalls);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0 && objects[Player].collision["enabled"]);
            CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3 && Get<float>(SpringArm, "TargetArmLength") == 350.f);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle && !Alive(Proxy()) && !Alive(Tamer()));
        }
        std::cout << "PASS activation during movies and sequence transition states is rejected before retaining-player mutations\n";
    }
    void NonCinematicIsolationChangesAreReassertedOnlyWhenRequired()
    {
        InitializeWorld(); StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        ExternalHidden(false);
        objects[Player].collision["enabled"] = true;
        capsuleCollision = meshCollision = 3;
        Put(Movement, eng::Arg::Byte("MovementMode", 1));
        const int oldHidden = hiddenWrites, oldCollision = collisionWrites, oldMode = modeWrites;
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 1 && objects[Player].collision["enabled"] && !capsuleCollision && !meshCollision);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 0);
        CHECK(hiddenWrites == oldHidden + 1 && collisionWrites == oldCollision + 2 && modeWrites == oldMode + 1);
        Tick();
        CHECK(hiddenWrites == oldHidden + 1 && collisionWrites == oldCollision + 2 && modeWrites == oldMode + 1);
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0 && objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3 && Get<float>(SpringArm, "TargetArmLength") == 350.f);
        std::cout << "PASS ordinary hidden/collision/movement interference is reasserted once and restored normally\n";
    }
    void PerCharacterCameraPersistsUpdatesAtomicallyAndRestoresThePlayerBaseline()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto id = trans::GetStatus().activeId;
        const auto entries = trans::Catalog();
        const auto second = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) { return entry.id != id; });
        CHECK(second != entries.end());
        if (second == entries.end()) return;
        const auto otherId = second->id;
        CHECK(trans::SetCameraDistance(id, 12.f).success);
        CHECK(trans::SetCameraDistance(otherId, 24.f).success);
        CHECK(trans::CameraDistance(id) == 12.f && trans::CameraDistance(otherId) == 24.f);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 700.f); // Queued for the game-thread update.
        const int initialWrites = armWrites;
        Tick();
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 1200.f && trans::GetStatus().cameraDistanceMeters == 12.f);
        CHECK(armWrites == initialWrites + 1);
        for (int frame = 0; frame != 10; ++frame) Tick();
        CHECK(armWrites == initialWrites + 1); // Only a changed distance writes the spring arm.
        Put(SpringArm, eng::Arg::Float("TargetArmLength", 1111.f)); // Engine camera Tick reapplies its control data.
        Tick();
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 1200.f && armWrites == initialWrites + 2);
        Tick();
        CHECK(armWrites == initialWrites + 2); // A corrected arm needs no redundant write until the engine changes it again.
        CHECK(trans::SetCameraDistance(otherId, 32.f).success);
        Tick();
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 1200.f && armWrites == initialWrites + 2);

        // Re-run the production loader over the saved file, without resetting
        // the singleton's once_flag or replacing the active session backend.
        trans::cameraConfig.clear();
        trans::LoadCameraFile();
        CHECK(trans::CameraDistance(id) == 12.f && trans::CameraDistance(otherId) == 32.f);
        const auto file = gloss::DataDirectory() / L"transformation-camera.cfg";
        std::ifstream saved(file, std::ios::binary);
        const std::string savedText{std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()};
        saved.close();
        for (const float invalid : {-1.f, .5f, 201.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
            CHECK(!trans::SetCameraDistance(id, invalid).success);
        CHECK(!trans::SetCameraDistance("missing-character", 10.f).success);
        CHECK(trans::CameraDistance(id) == 12.f && trans::CameraDistance(otherId) == 32.f);

        auto backup = file; backup += L".camera-test-backup";
        std::error_code error;
        std::filesystem::remove(backup, error);
        error.clear(); std::filesystem::rename(file, backup, error);
        CHECK(!error);
        if (!error)
        {
            std::filesystem::create_directory(file, error);
            CHECK(!error);
            if (!error)
            {
                CHECK(!trans::SetCameraDistance(id, 48.f).success);
                CHECK(trans::CameraDistance(id) == 12.f);
                Tick();
                CHECK(Get<float>(SpringArm, "TargetArmLength") == 1200.f);
                std::filesystem::remove(file, error);
                CHECK(!error);
            }
            error.clear(); std::filesystem::rename(backup, file, error);
            CHECK(!error);
            std::ifstream restored(file, std::ios::binary);
            const std::string restoredText{std::istreambuf_iterator<char>(restored), std::istreambuf_iterator<char>()};
            CHECK(restoredText == savedText);
        }
        CHECK(trans::SetCameraDistance(id, 0.f).success); Tick();
        CHECK(trans::CameraDistance(id) == 0.f && trans::CameraDistance(otherId) == 32.f);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 700.f && trans::GetStatus().cameraDistanceMeters == 7.f);
        CHECK(trans::SetCameraDistance(id, 20.f).success);
        failArmWrites = 1;
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && trans::GetStatus().cameraDistanceMeters == 0.f);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 350.f);
        CHECK(trans::CameraDistance(id) == 20.f); // A runtime object failure does not erase a saved preference.
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::SetCameraDistance(id, 20.f).success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Active && Get<float>(SpringArm, "TargetArmLength") == 2000.f);
        failArmReads = 1;
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && Get<float>(SpringArm, "TargetArmLength") == 350.f);
        CHECK(trans::CameraDistance(id) == 20.f && !failArmReads);
        CHECK(trans::SetCameraDistance(id, 0.f).success && trans::SetCameraDistance(otherId, 0.f).success);
        std::cout << "PASS per-character camera settings persist, update only on change, rollback failed saves and restore the original camera\n";
    }
    void WeaponInitializationRunsOnceAfterOwnerVisibilityAndFailureRestores()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active && weaponInitializeCalls == 1);
        for (int frame = 0; frame != 10; ++frame) Tick();
        CHECK(weaponInitializeCalls == 1); // Updates must not override skill/animation visibility.
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        InitializeWorld();
        failWeaponInitializeCalls = 1;
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring && weaponInitializeCalls == 1);
        CHECK(trans::GetStatus().message.find("武器本地初始化失败") != std::string::npos);
        CHECK(immuneLayers[Player] == 1 && Get<std::int32_t>(Player, "bHidden") == 1);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && weaponInitializeCalls == 1);
        CHECK(immuneLayers[Player] == 0 && Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"] && Get<std::uint8_t>(Movement, "MovementMode") == 3);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 350.f && !Alive(Proxy()) && !Alive(Tamer()));
        std::cout << "PASS weapon repair runs once before input and failed local initialization restores the retained player\n";
    }
    void WeaponReadinessWaitsBeforePlayerMutationAndCancelsCleanly()
    {
        InitializeWorld();
        weaponReadiness = native::WeaponReadiness::Pending;
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Initializing);
        for (int frame = 0; frame < 4; ++frame) Tick(1000);
        CHECK(weaponReadinessCalls == 5 && !weaponInitializeCalls);
        CHECK(!playerBaselineWrites && !armWrites && !immuneAddCalls && !isolationOwner);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0 && Get<float>(SpringArm, "TargetArmLength") == 350.f);
        weaponReadiness = native::WeaponReadiness::Ready;
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Active && weaponInitializeCalls == 1);
        CHECK(immuneAddCalls == 1 && armWrites == 1 && isolationOwner);
        Tick();
        CHECK(weaponReadinessCalls == 6 && weaponInitializeCalls == 1);
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && !isolationOwner);

        for (int cancellation = 0; cancellation < 4; ++cancellation)
        {
            InitializeWorld();
            weaponReadiness = native::WeaponReadiness::Pending;
            StartAndReachActivation();
            const auto proxy = Proxy(), tamer = Tamer();
            if (cancellation == 0) Tick(15000);
            else if (cancellation == 1) trans::Tick(Player, Controller, OtherWorld, 2, true);
            else if (cancellation == 2) { CHECK(trans::Restore().success); Tick(); }
            else { weaponReadiness = native::WeaponReadiness::Failed; Tick(); Tick(); }
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(!Alive(proxy) && !Alive(tamer));
            CHECK(!weaponInitializeCalls && !playerBaselineWrites && !armWrites && !immuneAddCalls && !isolationOwner);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0 && Get<float>(SpringArm, "TargetArmLength") == 350.f);
        }
        std::cout << "PASS weapon readiness waits before player mutation, activates once and cleans up on timeout, scene change, restore or failure\n";
    }
    void ActivationFailureRestoresPartialChanges()
    {
        InitializeWorld();
        failCollisionEnable = 1;
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        CHECK(immuneLayers[Player] == 1);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 1);
        CHECK(objects[Player].collision["enabled"] && !capsuleCollision && !meshCollision);
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

    void TargetClearFailureRollsBackInitializationAndAllowsRetry()
    {
        InitializeWorld();
        trans::Initialize();
        runtimeInitialized = true;
        Tick(); // Publish the game context and refresh the catalog.
        const auto catalog = trans::Catalog();
        const auto selected = std::find_if(catalog.begin(), catalog.end(), [](const auto &entry)
        {
            return entry.path.find("TAMER_mgd_yangjian_01") != std::string::npos;
        });
        CHECK(selected != catalog.end());
        if (selected == catalog.end()) return;
        const auto characterId = selected->id;

        failTargetClearCalls = 1; // Simulate a failed D9C1-backed ClearTargetEvent.
        CHECK(trans::Begin(characterId).success);
        Tick(); // Load and validate the selected resource.
        Tick(); // Spawn the configuration actor and proxy.
        const auto failedProxy = Proxy(), failedTamer = Tamer();
        CHECK(failedProxy && failedTamer);
        if (!failedProxy || !failedTamer) return;
        CHECK(Alive(failedProxy) && Alive(failedTamer));

        Tick(); // Initialization fails while clearing the proxy's autonomous target.
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        CHECK(trans::GetStatus().message.find("无法清除临时角色初始化时的自主目标") != std::string::npos);
        CHECK(targetClearCalls == 1 && failTargetClearCalls == 0);
        CHECK(Alive(failedProxy) && Alive(failedTamer));
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 350.f);
        CHECK(immuneLayers[Player] == 0);

        Tick(); // Roll back and destroy both session-owned initialization actors.
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(!Alive(failedProxy) && !Alive(failedTamer));
        CHECK(destroyRequests == (std::vector<std::uintptr_t>{failedProxy, failedTamer}));
        CHECK(cleanupActors == std::vector<std::uintptr_t>{failedProxy});
        CHECK(guardedProxyDestroyCalls == 1);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 350.f);
        CHECK(immuneLayers[Player] == 0);

        // Retire the destroyed fixture records so the retry resolves only the
        // new live objects, while preserving the original-player baseline.
        objects.erase(failedProxy);
        objects.erase(failedTamer);
        CHECK(trans::Begin(characterId).success);
        Tick(); // Load.
        Tick(); // Spawn.
        Tick(); // Initialize and activate; the one-shot clear failure is gone.
        CHECK(targetClearCalls == 2 && failTargetClearCalls == 0);
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto retryProxy = Proxy(), retryTamer = Tamer();
        CHECK(retryProxy && retryTamer);
        if (!retryProxy || !retryTamer) return;
        CHECK(retryProxy != failedProxy && retryTamer != failedTamer);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 1);
        CHECK(objects[Player].collision["enabled"] && !capsuleCollision && !meshCollision);
        CHECK(immuneLayers[Player] == 1);

        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(!Alive(retryProxy) && !Alive(retryTamer));
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 350.f);
        CHECK(immuneLayers[Player] == 0);
        std::cout << "PASS D9C1-backed target clear failure rolls back owned initialization actors, restores the player, and permits retry\n";
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
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3);
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

    bool ConfigureTestSkillSlot(std::vector<int> skillIds = {41001})
    {
        const auto activeId = trans::GetStatus().activeId;
        auto slots = trans::Slots(activeId);
        if (slots.size() != 8) return false;
        slots[0].name = "Mock skill";
        slots[0].skills = std::move(skillIds);
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

    void MovementUsesCameraYawAndMappedInputDispatchesConfiguredSkill()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(ConfigureTestSkillSlot({41002}));

        controlYaw = 90.;
        movementInput = {1.f, 1.f};
        pendingActions = {trans::Action::LightAttack};
        Tick();
        CHECK(movementRequests.size() == 1);
        if (!movementRequests.empty())
        {
            CHECK(std::abs(movementRequests[0].destination.x + 1200. * std::sqrt(.5)) < 1e-3);
            CHECK(std::abs(movementRequests[0].destination.y - 1200. * std::sqrt(.5)) < 1e-3);
            CHECK(movementRequests[0].speedType == 1 && movementRequests[0].moveAIType == 3);
        }
        CHECK(requestedSkillIds == std::vector<int>{41002});
        CHECK(pendingActions.empty());

        controlYaw = 0.;
        movementInput = {.5f, 0.f};
        Tick();
        CHECK(movementRequests.size() == 2);
        if (movementRequests.size() > 1)
        {
            CHECK(std::abs(movementRequests[1].destination.x - 1200.) < 1e-3);
            CHECK(std::abs(movementRequests[1].destination.y) < 1e-5);
            CHECK(movementRequests[1].speedType == 0);
        }

        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS production movement rotates input by camera yaw and native action dispatches its configured skill\n";
    }

    std::size_t DiagnosticLogCount(std::string_view prefix)
    {
        return std::count_if(diagnosticLogs.begin(), diagnosticLogs.end(), [&](const auto &line) { return line.starts_with(prefix); });
    }
    std::string LastDiagnosticLog(std::string_view prefix)
    {
        const auto found = std::find_if(diagnosticLogs.rbegin(), diagnosticLogs.rend(), [&](const auto &line) { return line.starts_with(prefix); });
        return found == diagnosticLogs.rend() ? std::string{} : *found;
    }
    void MovementDiagnosticsAreDeferredBoundedReadOnlyAndResetPerSession()
    {
        constexpr std::string_view sessionPrefix = "Transformation movement session:", samplePrefix = "Transformation movement sample:", stopPrefix = "Transformation movement stop:";
        InitializeWorld();
        StartAndReachActivation();
        CHECK(DiagnosticLogCount(sessionPrefix) == 1 && diagnosticClassPathCalls == 1 && diagnosticProbes == 2);
        const auto session = LastDiagnosticLog(sessionPrefix);
        CHECK(session.find("id=" + trans::GetStatus().activeId) != std::string::npos);
        CHECK(session.find("resource=830001 override=0") != std::string::npos);
        CHECK(session.find("classPathRead=true classPath=/Game/Units/DiagnosticCharacter.DiagnosticCharacter_C") != std::string::npos);
        movementInput = {1.f, 0.f};
        Tick(); // Request was just issued: no acceleration snapshot this frame.
        CHECK(diagnosticAccelerationCalls == 0 && diagnosticMaximumCalls == 0 && diagnosticPropertyReads == 0);
        Tick(49);
        CHECK(DiagnosticLogCount(samplePrefix) == 0);
        objects[Proxy()].locations["actor"] = {10., 5., 0.};
        Tick(1);
        CHECK(diagnosticAccelerationCalls == 1 && diagnosticMaximumCalls == 1 && diagnosticPropertyReads == 3);
        CHECK(diagnosticAnimCalls == 1 && diagnosticAnimationProbes == 3 && DiagnosticLogCount("Transformation animation sample:") == 1);
        const auto animation = LastDiagnosticLog("Transformation animation sample:");
        CHECK(animation.find("sample=1/24 meshState=present") != std::string::npos && animation.find("animState=present") != std::string::npos);
        CHECK(animation.find("pauseAnims=false globalAnimRateScale=1.000 disableABPMove=false abpSettingState=present") != std::string::npos);
        CHECK(animation.find("followState=false") != std::string::npos);
        const auto sample = LastDiagnosticLog(samplePrefix);
        CHECK(sample.find("sample=1/24") != std::string::npos && sample.find("dtMs=50") != std::string::npos);
        CHECK(sample.find("locationRead=true location=(10.000,5.000,0.000) deltaRead=true delta=(10.000,5.000,0.000)") != std::string::npos);
        CHECK(sample.find("velocityRead=true velocity=(120.000,0.000,0.000)") != std::string::npos);
        CHECK(sample.find("accelerationRead=true acceleration=(250.000,0.000,0.000)") != std::string::npos);
        CHECK(sample.find("maxAccelerationRead=true maxAcceleration=2048.000") != std::string::npos);
        CHECK(sample.find("requestedAccelerationRead=true requestedAcceleration=true pathAccelerationRead=true pathAcceleration=false") != std::string::npos);
        CHECK(sample.find("axes=(1.000,0.000)") != std::string::npos && sample.find("casting=0 speedType=1 moveAIType=3") != std::string::npos);
        CHECK(sample.find("mode=1 customMode=0 walkSpeed=600.000") != std::string::npos);
        movementInput = {}; Tick();
        movementInput = {1.f, 0.f}; Tick(); Tick(50);
        CHECK(diagnosticAccelerationCalls == 1); // Repeated bursts cannot bypass the session rate limit.
        for (int frame = 0; frame != 100; ++frame) Tick(1);
        CHECK(diagnosticAccelerationCalls == 1 && diagnosticProbes == 2); // No per-frame probes or reads.
        movementInput = {0.f, 1.f};
        Tick(1000); // A later replan also cannot sample in its request frame.
        CHECK(diagnosticAccelerationCalls == 1);
        Tick(1);
        CHECK(diagnosticAccelerationCalls == 2);
        for (int second = 0; second != 50; ++second) { Tick(1000); Tick(1); }
        CHECK(DiagnosticLogCount(samplePrefix) == 24);
        CHECK(diagnosticAccelerationCalls == 24 && diagnosticMaximumCalls == 24 && diagnosticPropertyReads == 72);
        CHECK(diagnosticAnimCalls == 24 && DiagnosticLogCount("Transformation animation sample:") == 24);
        for (int second = 0; second != 10; ++second) { Tick(1000); Tick(1); }
        CHECK(diagnosticAccelerationCalls == 24 && diagnosticPropertyReads == 72 && diagnosticWrites == 0);
        CHECK(diagnosticAnimCalls == 24 && diagnosticAnimationProbes == 3);
        movementInput = {};
        Tick();
        CHECK(LastDiagnosticLog(stopPrefix).find("reason=input_idle cancelAttempted=true cancelCallSucceeded=true") != std::string::npos);
        for (int burst = 0; burst != 12; ++burst)
        {
            movementInput = {1.f, 0.f}; Tick();
            movementInput = {}; Tick();
        }
        CHECK(DiagnosticLogCount(stopPrefix) == 8);
        CHECK(Get<std::int32_t>(ProxyMovement, "bUseAccelerationForPaths") == 0);
        CHECK(Get<std::int32_t>(ProxyMovement, "bRequestedMoveUseAcceleration") == 1);
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);

        // Remove dead mock actors as the real object registry eventually does;
        // preserve counters/logs to verify a second session resets its budget.
        std::erase_if(objects, [](const auto &entry) { return entry.first >= 0x10000 && !entry.second.alive; });
        StartAndReachActivation();
        CHECK(DiagnosticLogCount(sessionPrefix) == 2 && diagnosticClassPathCalls == 2 && diagnosticProbes == 4);
        movementInput = {1.f, 0.f}; Tick();
        CHECK(diagnosticAccelerationCalls == 24);
        Tick(50);
        CHECK(diagnosticAccelerationCalls == 25 && DiagnosticLogCount(samplePrefix) == 25);
        CHECK(LastDiagnosticLog(samplePrefix).find("sample=1/24") != std::string::npos);
        movementInput = {}; Tick();
        CHECK(DiagnosticLogCount(stopPrefix) == 9 && LastDiagnosticLog(stopPrefix).find("event=1/8") != std::string::npos);
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && diagnosticWrites == 0);
        std::cout << "PASS movement diagnostics defer physics sampling, rate-limit, cap output and reset per session without writes\n";
    }
    void MovementDiagnosticsUnknownsNeverAbortOrProbeEveryFrame()
    {
        constexpr std::string_view samplePrefix = "Transformation movement sample:";
        InitializeWorld();
        failDiagnosticFunctions = true;
        StartAndReachActivation();
        CHECK(LastDiagnosticLog("Transformation movement session:").find("classPathRead=false classPath=unknown") != std::string::npos);
        objects[ProxyMovement].properties.erase("Velocity");
        Put(ProxyMovement, eng::Arg::Int("bRequestedMoveUseAcceleration", 41)); // Wrong reflected type is unknown, not false.
        objects[ProxyMovement].properties.erase("bUseAccelerationForPaths");
        movementInput = {1.f, 0.f}; Tick(); Tick(50);
        const auto unavailable = LastDiagnosticLog(samplePrefix);
        CHECK(unavailable.find("velocityRead=false velocity=unknown") != std::string::npos);
        CHECK(unavailable.find("accelerationRead=false acceleration=unknown maxAccelerationRead=false maxAcceleration=unknown") != std::string::npos);
        CHECK(unavailable.find("requestedAccelerationRead=false requestedAcceleration=unknown pathAccelerationRead=false pathAcceleration=unknown") != std::string::npos);
        CHECK(trans::GetStatus().phase == trans::Phase::Active && diagnosticWrites == 0);
        CHECK(Get<int>(ProxyMovement, "bRequestedMoveUseAcceleration") == 41);
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);

        InitializeWorld();
        diagnosticInterfacesAvailable = false;
        StartAndReachActivation();
        movementInput = {1.f, 0.f}; Tick(); Tick(50);
        for (int frame = 0; frame != 100; ++frame) Tick();
        CHECK(diagnosticProbes == 2 && diagnosticAccelerationCalls == 0 && diagnosticMaximumCalls == 0);
        CHECK(DiagnosticLogCount(samplePrefix) == 1 && trans::GetStatus().phase == trans::Phase::Active);
        CHECK(LastDiagnosticLog(samplePrefix).find("accelerationRead=false acceleration=unknown") != std::string::npos);
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && diagnosticWrites == 0);
        std::cout << "PASS optional movement diagnostic read/type/interface failures remain unknown and never abort gameplay\n";
    }
    void AnimationDiagnosticsDistinguishNullInapplicableAndUnreadable()
    {
        constexpr std::string_view prefix = "Transformation animation sample:";
        InitializeWorld();
        StartAndReachActivation();
        movementInput = {1.f, 0.f}; Tick(); Tick(50);
        CHECK(diagnosticSpecificAnimReads == 2);
        const auto nextSample = [] { Tick(1000); Tick(1); };

        diagnosticAnimNull = true;
        nextSample();
        CHECK(LastDiagnosticLog(prefix).find("animState=null") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("disableABPMove=not-applicable abpSettingState=not-applicable") != std::string::npos);
        CHECK(diagnosticSpecificAnimReads == 2); // No reads on a null anim instance.

        diagnosticAnimNull = false; diagnosticHumanoid = diagnosticAnimBase = false;
        nextSample();
        CHECK(LastDiagnosticLog(prefix).find("animState=present") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("disableABPMove=not-applicable abpSettingState=not-applicable") != std::string::npos);
        CHECK(diagnosticSpecificAnimReads == 2); // Classes must match before specialized fields are read.

        diagnosticHumanoid = diagnosticAnimBase = true;
        Put(ProxyAnim, eng::Arg::Float("bDisableABPMove", 0.f)); // Wrong native class is unreadable, not false.
        Put(ProxyAnim, eng::Arg::Object("ABPSetting", 0));
        Put(ProxyMesh, eng::Arg::Boolean("bPauseAnims", true));
        Put(ProxyMesh, eng::Arg::Float("GlobalAnimRateScale", std::numeric_limits<float>::quiet_NaN()));
        Put(ProxyMovement, eng::Arg::Boolean("bIsInFollowState", true));
        nextSample();
        CHECK(LastDiagnosticLog(prefix).find("pauseAnims=true globalAnimRateScale=unreadable disableABPMove=unreadable abpSettingState=null") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("followState=true") != std::string::npos);

        const auto calls = diagnosticAnimCalls;
        Put(Proxy(), eng::Arg::Object("Mesh", 0));
        nextSample();
        CHECK(LastDiagnosticLog(prefix).find("meshState=null") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("animState=not-applicable") != std::string::npos);
        CHECK(diagnosticAnimCalls == calls);

        Put(Proxy(), eng::Arg::Int("Mesh", 42));
        nextSample();
        CHECK(LastDiagnosticLog(prefix).find("meshState=unreadable") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("animState=unreadable") != std::string::npos);
        CHECK(diagnosticAnimCalls == calls && trans::GetStatus().phase == trans::Phase::Active);

        Put(Proxy(), eng::Arg::Object("Mesh", ProxyMesh));
        failDiagnosticFunctions = true;
        nextSample();
        CHECK(LastDiagnosticLog(prefix).find("animState=unreadable") != std::string::npos);
        CHECK(diagnosticAnimCalls == calls + 1 && trans::GetStatus().phase == trans::Phase::Active);
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && diagnosticWrites == 0);

        InitializeWorld();
        diagnosticAnimationInterfacesAvailable = false;
        StartAndReachActivation();
        movementInput = {1.f, 0.f}; Tick(); Tick(50);
        for (int frame = 0; frame != 100; ++frame) Tick();
        CHECK(diagnosticAnimationProbes == 3 && diagnosticAnimCalls == 0 && DiagnosticLogCount(prefix) == 1);
        CHECK(LastDiagnosticLog(prefix).find("animState=unreadable") != std::string::npos);
        CHECK(trans::GetStatus().phase == trans::Phase::Active && diagnosticWrites == 0);
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS animation diagnostics distinguish null/type mismatch/read failures without writes or gameplay failure\n";
    }
    void LocomotionGraphDiagnosticsReadRealMirrorsAndRespectAllGates()
    {
        constexpr std::string_view prefix = "Transformation locomotion graph sample:";
        InitializeWorld(); StartAndReachActivation();
        movementInput = {1.f, 0.f}; Tick(); Tick(50);
        CHECK(diagnosticLinkedProbes == 5 && diagnosticLinkedCalls == 2 && diagnosticOwnerCalls == 2);
        CHECK(LastDiagnosticLog(prefix).find("mappedSpeed=123.000") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("finalMoveMode=10 isMoveAccelerating=true moveOwnerMatches=true") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("nodeOwnerMatches=true idleToMove=true moveStop=false loopAlpha=0.500") != std::string::npos);
        CHECK(diagnosticSettingProbes == 1 && diagnosticSettingReads == 2);
        CHECK(diagnosticDataCalls == 1 && LastDiagnosticLog("Transformation animation data sample:").find("commonFinalMode=10") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("settingClassState=present configuredMoveMode=10 spareMoveMode=11") != std::string::npos);
        for (int frame = 0; frame < 100; ++frame) Tick();
        CHECK(diagnosticLinkedCalls == 2);
        const auto sample = [] { Tick(1000); Tick(); };

        diagnosticOwnerMatches = false; sample();
        CHECK(LastDiagnosticLog(prefix).find("moveOwnerMatches=false") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("nodeOwnerMatches=false") != std::string::npos);
        diagnosticOwnerMatches = true;
        diagnosticMoveNull = true; sample();
        CHECK(LastDiagnosticLog(prefix).find("moveGraphState=null") != std::string::npos);
        diagnosticMoveNull = false; diagnosticMoveClass = false; sample();
        CHECK(LastDiagnosticLog(prefix).find("moveClassState=not-applicable finalMoveMode=not-applicable") != std::string::npos);
        diagnosticMoveClass = true; diagnosticNodeNull = true; sample();
        CHECK(LastDiagnosticLog(prefix).find("nodeState=null") != std::string::npos);
        diagnosticNodeNull = false; diagnosticNodeClass = false; sample();
        CHECK(LastDiagnosticLog(prefix).find("nodeClassState=not-applicable") != std::string::npos);
        diagnosticNodeClass = true;
        diagnosticSettingClass = false; sample();
        CHECK(LastDiagnosticLog(prefix).find("configuredMoveMode=not-applicable spareMoveMode=not-applicable") != std::string::npos);
        diagnosticSettingClass = true;
        objects[ProxyABPSetting].pathProperties["CommonSetting.ABPMoveMode"] = eng::Arg::Byte("ABPMoveMode", 10);
        sample();
        CHECK(LastDiagnosticLog(prefix).find("configuredMoveMode=unreadable spareMoveMode=11") != std::string::npos);
        objects[ProxyABPSetting].pathProperties["CommonSetting.ABPMoveMode"] = eng::Arg::Enum("ABPMoveMode", 0);
        sample();
        CHECK(LastDiagnosticLog(prefix).find("configuredMoveMode=0 spareMoveMode=11") != std::string::npos);
        Put(ProxyAnim, eng::Arg::Float("MappedSpeed", std::numeric_limits<float>::quiet_NaN()));
        Put(ProxyMoveAnim, eng::Arg::Byte("FinalABPMoveMode", 10));
        Put(ProxyMoveAnim, eng::Arg::Float("IsMoveAccelerating", 1.f)); sample();
        CHECK(LastDiagnosticLog(prefix).find("mappedSpeed=unreadable") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("finalMoveMode=unreadable isMoveAccelerating=unreadable") != std::string::npos);
        Put(ProxyMoveAnim, eng::Arg::Enum("FinalABPMoveMode", 10));
        Put(ProxyMoveAnim, eng::Arg::Boolean("IsMoveAccelerating", true));
        Put(ProxyLocomotionAnim, eng::Arg::Float("MoveLoopFwdBlendAlpha", std::numeric_limits<float>::infinity())); sample();
        CHECK(LastDiagnosticLog(prefix).find("loopAlpha=unreadable") != std::string::npos);

        diagnosticNodeMode = 11; Put(ProxyMoveAnim, eng::Arg::Enum("FinalABPMoveMode", 11)); sample();
        CHECK(LastDiagnosticLog(prefix).find("finalMoveMode=11") != std::string::npos);
        CHECK(LastDiagnosticLog(prefix).find("idleToMove=not-applicable moveStop=not-applicable loopAlpha=not-applicable nodeMappedSpeed=234.000 gait=3 moveGait=2") != std::string::npos);
        Put(ProxyLocomotionAnim, eng::Arg::Int("GaitGroundedState", 3)); sample();
        CHECK(LastDiagnosticLog(prefix).find("gait=unreadable moveGait=2") != std::string::npos);
        CHECK(trans::GetStatus().phase == trans::Phase::Active && diagnosticWrites == 0);
        for (int frame = 0; frame < 30; ++frame) sample();
        CHECK(DiagnosticLogCount(prefix) == 24);
        const auto calls = diagnosticLinkedCalls, reads = diagnosticLinkedPropertyReads, owners = diagnosticOwnerCalls, settings = diagnosticSettingReads;
        const auto dataCalls = diagnosticDataCalls;
        sample(); CHECK(diagnosticLinkedCalls == calls && diagnosticLinkedPropertyReads == reads && diagnosticOwnerCalls == owners && diagnosticSettingReads == settings && diagnosticDataCalls == dataCalls);
        CHECK(trans::Restore().success); Tick();

        InitializeWorld(); diagnosticLinkedAvailable = false; diagnosticSettingAvailable = false; StartAndReachActivation();
        movementInput = {1.f, 0.f}; Tick(); Tick(50);
        CHECK(diagnosticLinkedProbes == 5 && !diagnosticLinkedCalls && !diagnosticOwnerCalls);
        CHECK(DiagnosticLogCount(prefix) == 1 && LastDiagnosticLog(prefix).find("moveGraphState=unreadable") != std::string::npos);
        CHECK(!diagnosticSettingReads && LastDiagnosticLog(prefix).find("configuredMoveMode=unreadable") != std::string::npos);
        CHECK(trans::GetStatus().phase == trans::Phase::Active && diagnosticWrites == 0);
        CHECK(trans::Restore().success); Tick();

        InitializeWorld(); StartAndReachActivation();
        diagnosticData = {};
        movementInput = {1.f, 0.f}; Tick(); Tick(50);
        CHECK(diagnosticDataCalls == 1);
        CHECK(LastDiagnosticLog("Transformation animation data sample:").find("available=false current=false settingState=unknown") != std::string::npos);
        CHECK(LastDiagnosticLog("Transformation animation data sample:").find("cachedConfiguredMode=-1 cachedSpareMode=-1 commonFinalMode=-1") != std::string::npos);
        CHECK(trans::GetStatus().phase == trans::Phase::Active && diagnosticWrites == 0);
        CHECK(trans::Restore().success); Tick();
        std::cout << "PASS locomotion graph mirrors use exact Name/Object/Enum contracts, class gates and bounded read-only diagnostics\n";
    }
    void MotionMatchingMovementRequestsChooseRoleResourcesAndKeepUnknownDistinct()
    {
        InitializeWorld(); StartAndReachActivation();
        movementAnimationChoice = {true, true, true, false, true, 4};
        movementInput = {1.f, 0.f}; Tick();
        CHECK(movementRequests.size() == 1 && movementRequests.back().mmState == 4);
        CHECK(lastAnimationSpeed == 1 && !lastAnimationLocked && movementAnimationQueries == 1);
        for (int i = 0; i < 100; ++i) Tick();
        CHECK(movementAnimationQueries == 1 && movementRequests.size() == 1);
        // Locks without a corresponding role resource use the validated free
        // animation and facing-velocity mover, rather than an invalid Lock state.
        constexpr std::uintptr_t enemy = 0x31000;
        Add(enemy); objects[enemy].locations["actor"] = {500., 500., 0.}; lockedTarget = enemy;
        Tick();
        CHECK(lastAnimationLocked && movementRequests.size() == 1);
        CHECK(actorRotationCalls == 0); // A validated free gait must not be forced to face the lock target.
        CHECK(trans::GetStatus().message.find("自身自由移动动画") != std::string::npos);
        movementAnimationChoice = {true, true, true, true, true, 7}; Tick(250);
        CHECK(movementRequests.back().mmState == 7 && movementRequests.back().moveAIType == 1);
        CHECK(trans::GetStatus().message.find("缺少对应锁定步态") == std::string::npos);
        // Changing only MM state must replan; WithMM parameter remains an Enum.
        auto malformed = std::array{eng::Arg::Object("Unit", Proxy()), eng::Arg::Vector("Position", {100., 0., 0.}),
            eng::Arg::Enum("SpeedRateType", 1), eng::Arg::Float("AcceptableRadius", 5.f), eng::Arg::Enum("MoveAIType", 3),
            eng::Arg::Boolean("IncludeSelfRadius", false), eng::Arg::Boolean("IncludeTargetRadius", false),
            eng::Arg::Byte("MotionMatchingState", 4), eng::Arg::Data("ReturnValue", eng::Type::Int, 0, true)};
        const auto count = movementRequests.size();
        CHECK(!eng::Call(L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocationWithMM", malformed).success);
        CHECK(movementRequests.size() == count);
        movementAnimationChoice = {true, true, true, false, false, -1}; Tick(250);
        CHECK(activeMovementRequest > 0 && trans::GetStatus().phase == trans::Phase::Active);
        CHECK(movementRequests.size() == count + 1 && movementRequests.back().mmState == -1);
        CHECK(movementRequests.back().moveAIType == 1); // Ordinary movement retains the requested lock.
        CHECK(trans::GetStatus().message.find("缺少当前步态动画") != std::string::npos);
        const int queries = movementAnimationQueries;
        for (int i = 0; i < 100; ++i) Tick();
        CHECK(movementAnimationQueries == queries && movementRequests.size() == count + 1);
        movementInput = {0.f, 1.f}; // Force a native replan while the animation cache is unknown.
        movementAnimationChoice = {true, false, true}; Tick(250);
        CHECK(activeMovementRequest > 0 && movementRequests.size() == count + 2 && movementRequests.back().mmState == -1);
        CHECK(trans::GetStatus().message.find("动画配置暂不可读") != std::string::npos && trans::GetStatus().phase == trans::Phase::Active);
        // A rejected move does not imply that its preceding MM event was not
        // dispatched; keep indexed request cancellation and retry semantics.
        movementAnimationChoice = {true, true, true, false, true, 4}; movementReturnOverride = 0; Tick(250);
        CHECK(movementRequests.back().mmState == 4 && movementRequests.back().id == 0);
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        movementReturnOverride = -1; Tick(250);
        CHECK(activeMovementRequest > 0 && movementRequests.back().mmState == 4);
        CHECK(trans::Restore().success); Tick();
        std::cout << "PASS MM to missing/unknown animation fallback and back, validated free gait, typed ABI and rejected-request retry\n";
    }
    void RoleLockAnalyzerWithoutTargetKeepsFreeMovementAndRestoresNormally()
    {
        for (int speed = 0; speed <= 1; ++speed)
        {
            InitializeWorld(); StartAndReachActivation();
            movementAnimationChoice = {true, true, true, true, true, 6 + speed};
            movementInput = {speed == 0 ? .5f : 1.f, 0.f};
            Tick();
            CHECK(!lockedTarget && !lastAnimationLocked && lastAnimationSpeed == speed);
            CHECK(activeMovementRequest > 0 && movementRequests.size() == 1);
            CHECK(movementRequests.back().mmState == 6 + speed && movementRequests.back().speedType == speed);
            CHECK(movementRequests.back().moveAIType == 1 && actorRotationCalls == 0);
            CHECK(facingDirections.size() == 1 && directionInputs.empty() && moverFacingOverride);
            CHECK(facingDirections.back().x == 1. && facingDirections.back().y == 0.);
            CHECK(std::all_of(forwardedTargets.begin(), forwardedTargets.end(), [](auto target) { return target == 0; }));
            const auto selection = LastDiagnosticLog("Transformation MM selection:");
            CHECK(selection.find("locked=false") != std::string::npos && selection.find("usesLocked=true") != std::string::npos);
            for (int frame = 0; frame < 120; ++frame) Tick();
            CHECK(movementRequests.size() == 1 && movementAnimationQueries == 1 && cancelledMovementRequests.empty());
            CHECK(facingDirections.size() == 121 && directionInputs.empty()); // Update facing without resetting the AI mover or adding input twice.
            controlYaw = 90.; Tick(50);
            CHECK(movementRequests.size() == 2 && movementRequests.back().mmState == 6 + speed && movementRequests.back().moveAIType == 1);
            CHECK(actorRotationCalls == 0 && !lockedTarget);
            CHECK(std::abs(facingDirections.back().x) < .00001 && std::abs(facingDirections.back().y - 1.) < .00001);
            movementInput = {}; Tick();
            CHECK(activeMovementRequest == 0 && cancelledMovementRequests.size() == 1 && !moverFacingOverride);
            CHECK(trans::Restore().success); Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        }
        std::cout << "PASS Lock-only roles receive native desired facing alongside continuous indexed jog/run requests, steering, stopping and restoration\n";
    }
    void MotionMatchingFacingRespectsIntentAndEveryPause()
    {
        // Root-motion velocity can point away from intent. Neither movement
        // facing nor its telemetry may be synthesized from that velocity.
        InitializeWorld(); StartAndReachActivation();
        movementAnimationChoice = {true, true, true, true, true, 7};
        Put(ProxyMovement, eng::Arg::Vector("Velocity", {-120., -80., 0.}));
        for (const double yaw : {0., 90., 180., -90.})
        {
            controlYaw = yaw;
            for (const auto axes : {native::MoveInput{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}, {1.f, 1.f}})
            {
                const auto before = facingDirections.size();
                movementInput = axes; Tick(50);
                CHECK(facingDirections.size() == before + 1 && directionInputs.empty());
                const auto input = facingDirections.back();
                constexpr double radians = .017453292519943295;
                const double length = std::sqrt(axes.forward * axes.forward + axes.right * axes.right);
                const double expectedX = (std::cos(yaw * radians) * axes.forward - std::sin(yaw * radians) * axes.right) / length;
                const double expectedY = (std::sin(yaw * radians) * axes.forward + std::cos(yaw * radians) * axes.right) / length;
                CHECK(std::abs(input.x - expectedX) < .00001 && std::abs(input.y - expectedY) < .00001);
                CHECK(input.z == 0. && !lockedTarget && actorRotationCalls == 0);
            }
        }
        CHECK(trans::Restore().success); Tick();
        for (int pause = 0; pause != 7; ++pause)
        {
            InitializeWorld(); StartAndReachActivation();
            movementAnimationChoice = {true, true, true, true, true, 7};
            movementInput = {1.f, 0.f}; Tick();
            CHECK(facingDirections.size() == 1 && activeMovementRequest > 0);
            const int request = activeMovementRequest;
            if (pause == 0) movementInput = {};
            if (pause == 1) overlayVisible = true;
            if (pause == 2) foreground = false;
            if (pause == 3) simpleStates[Proxy()].insert(3);
            if (pause == 4) castingSkillId = 41001;
            if (pause == 5) playingMovie = true;
            if (pause == 6) CHECK(trans::Restore().success);
            Tick();
            CHECK(facingDirections.size() == 1 && activeMovementRequest == 0 && !moverFacingOverride && directionInputs.empty());
            CHECK(cancelledMovementRequests == std::vector<int>{request});
            if (pause != 6) { CHECK(trans::Restore().success); Tick(); }
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        }
        // Ordinary, unknown, and volume paths retain their existing driver.
        for (int path = 0; path != 5; ++path)
        {
            InitializeWorld(); StartAndReachActivation();
            movementInput = {1.f, 0.f};
            if (path == 1) movementAnimationChoice = {true, false, true};
            if (path == 2 || path == 3)
            {
                movementAnimationChoice = {true, true, true, true, true, 7};
                Put(ProxyMovement, eng::Arg::Byte("MovementMode", path == 2 ? 4 : 5));
            }
            if (path == 4) movementAnimationChoice = {true, true, true, false, true, 4};
            Tick(); Tick(50);
            CHECK(activeMovementRequest > 0 && directionInputs.empty() && facingDirections.empty() && !moverFacingOverride);
            CHECK(trans::Restore().success); Tick();
        }
        InitializeWorld(); StartAndReachActivation();
        movementAnimationChoice = {true, true, true, true, true, 7}; movementInput = {1.f, 0.f};
        movementReturnOverride = 0; Tick();
        CHECK(facingDirections.empty() && trans::GetStatus().phase == trans::Phase::Active);
        movementReturnOverride = -1; failFacingCalls = 1; Tick(250);
        CHECK(facingDirections.empty() && trans::GetStatus().phase == trans::Phase::Restoring);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle && !activeMovementRequest && cancelledMovementRequests.size() == 1);
        std::cout << "PASS MM desired facing uses camera-relative intent in all quadrants, preserves pause/skill/state gates, and cleans up failed requests\n";
    }
    void FacingOverrideTransitionsCancelBeforeRealLocksAndOtherMovers()
    {
        for (int transition = 0; transition != 4; ++transition)
        {
            InitializeWorld(); StartAndReachActivation();
            movementAnimationChoice = {true, true, true, true, true, 7}; movementInput = {1.f, 0.f}; Tick();
            const int oldRequest = activeMovementRequest;
            CHECK(moverFacingOverride && facingDirections.size() == 1);
            if (transition == 0)
            {
                constexpr std::uintptr_t enemy = 0x31000;
                Add(enemy); objects[enemy].locations["actor"] = {500., 500., 0.}; lockedTarget = enemy;
            }
            if (transition == 1) Put(ProxyMovement, eng::Arg::Byte("MovementMode", 5));
            if (transition == 2)
            {
                // An animation cache change can occur before the mover's next
                // 33ms refresh; force a resource query via a gait change.
                movementInput.forward = .5f;
                movementAnimationChoice = {true, true, true, false, true, 3};
            }
            if (transition == 3) { movementInput.forward = .5f; movementAnimationChoice = {true, false, true}; }
            Tick(1);
            CHECK(cancelledMovementRequests == std::vector<int>{oldRequest});
            CHECK(activeMovementRequest > 0 && activeMovementRequest != oldRequest);
            CHECK(!moverFacingOverride && facingDirections.size() == 1 && directionInputs.empty());
            lockedTarget = 0;
            Put(ProxyMovement, eng::Arg::Byte("MovementMode", 1));
            movementAnimationChoice = {true, true, true, true, true, 6}; movementInput.forward = .5f;
            // Stop resets the selector deadline; returning to input facing
            // must establish a new mover before reapplying its override.
            Tick(250);
            CHECK(moverFacingOverride && facingDirections.size() == 2 && !lockedTarget);
            CHECK(trans::Restore().success); Tick();
            CHECK(!moverFacingOverride && !activeMovementRequest);
        }
        InitializeWorld(); StartAndReachActivation();
        movementAnimationChoice = {true, true, true, true, true, 7}; movementInput = {1.f, 0.f}; Tick();
        constexpr std::uintptr_t enemy = 0x31000;
        Add(enemy); objects[enemy].locations["actor"] = {500., 500., 0.}; lockedTarget = enemy;
        failMovementCancelCalls = 1; Tick(1);
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring && movementRequests.size() == 1 && facingDirections.size() == 1);
        Tick(); CHECK(trans::GetStatus().phase == trans::Phase::Idle && !moverFacingOverride);
        InitializeWorld(); StartAndReachActivation(); CHECK(ConfigureTestSkillSlot());
        movementAnimationChoice = {true, true, true, true, true, 7}; movementInput = {1.f, 0.f}; Tick();
        pendingActions.push_back(trans::Action::LightAttack); Tick();
        CHECK(skillRequests == 1 && !moverFacingOverride && !activeMovementRequest && directionInputs.empty());
        castingSkillId = 41001; const auto facingCount = facingDirections.size(); Tick(50);
        CHECK(facingDirections.size() == facingCount);
        castingSkillId = 0; Tick(50);
        CHECK(moverFacingOverride && activeMovementRequest > 0);
        CHECK(trans::Restore().success); Tick();
        std::cout << "PASS input-facing ownership changes cancel immediately before native lock/free/volume requests, with retryable failure restoration\n";
    }
    void MappedSprintSelectsRoleSprintGaitAndReturnsToAnalogWalking()
    {
        InitializeWorld(); StartAndReachActivation();
        movementAnimationChoice = {true, true, true, true, true, 6}; movementInput = {.4f, 0.f}; Tick();
        CHECK(lastAnimationSpeed == 0 && movementRequests.back().speedType == 0 && moverFacingOverride);
        movementInput.sprint = true; movementAnimationChoice.state = 8; Tick(50);
        CHECK(lastAnimationSpeed == 2 && movementRequests.back().speedType == 2 && movementRequests.back().mmState == 8);
        CHECK(moverFacingOverride && directionInputs.empty());
        const auto requests = movementRequests.size();
        for (int i = 0; i != 100; ++i) Tick();
        CHECK(movementRequests.size() == requests);
        CHECK(LastDiagnosticLog("Transformation movement sample:").find("sprintInput=true") != std::string::npos);
        movementInput.sprint = false; movementAnimationChoice.state = 6; Tick(50);
        CHECK(lastAnimationSpeed == 0 && movementRequests.back().speedType == 0 && movementRequests.back().mmState == 6);
        movementInput.forward = 1.f; movementAnimationChoice.state = 7; Tick(50);
        CHECK(lastAnimationSpeed == 1 && movementRequests.back().speedType == 1);
        CHECK(trans::Restore().success); Tick();

        // Sprint can be held before any directional input, and releasing it
        // does not discard the current camera-relative movement axes.
        InitializeWorld(); StartAndReachActivation();
        movementInput = {0.f, 0.f, true}; Tick();
        CHECK(movementRequests.empty() && movementInput.sprint);
        movementAnimationChoice = {true, true, true, true, true, 8}; movementInput.forward = 1.f; Tick(50);
        CHECK(movementRequests.back().speedType == 2 && moverFacingOverride);
        overlayVisible = true; Tick();
        CHECK(!movementInput.sprint && !activeMovementRequest && !moverFacingOverride);
        overlayVisible = false; movementAnimationChoice.state = 7; Tick(50);
        CHECK(movementRequests.back().speedType == 1 && moverFacingOverride);
        movementInput.sprint = true; movementAnimationChoice.state = 8; Tick(50);
        castingSkillId = 41001; Tick();
        CHECK(!movementInput.sprint && !activeMovementRequest);
        castingSkillId = 0; movementAnimationChoice.state = 7; Tick(50);
        CHECK(movementRequests.back().speedType == 1);
        CHECK(trans::Restore().success); Tick();
        std::cout << "PASS mapped sprint selects role SPRINT/MM resources without changing analog direction, and pause/skill clears stale latches\n";
    }
    void PerCharacterSpeedMultipliersPreserveNativeGaitsAndPersistAtomically()
    {
        InitializeWorld(); StartAndReachActivation();
        const auto id = trans::GetStatus().activeId;
        CHECK(trans::GetMovementSpeed(id) == trans::MovementSpeedSettings{});
        movementInput = {1.f, 0.f}; Tick();
        CHECK(speedMultiplierWrites.empty()); // Default must not require the optional speed API.
        const trans::MovementSpeedSettings configured{1.5f, 2.5f};
        CHECK(trans::SetMovementSpeed(id, configured).success);
        CHECK(!trans::SetMovementSpeed("unknown", configured).success);
        for (auto invalid : {0.f, -1.f, 5.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            CHECK(!trans::SetMovementSpeed(id, {invalid, 1.f}).success);
        CHECK(trans::GetMovementSpeed(id) == configured);
        CHECK(trans::GetMovementSpeed("another-role") == trans::MovementSpeedSettings{});
        const auto requests = movementRequests.size();
        Tick(); CHECK(customSpeedMultiplier == 1.5f && movementRequests.size() == requests);
        customSpeedMultiplier = 1.f; // A native configuration/buff reset can clear Custom.
        Tick(); CHECK(customSpeedMultiplier == 1.5f);
        movementInput.sprint = true; Tick(50);
        CHECK(customSpeedMultiplier == 2.5f && movementRequests.back().speedType == 2);
        movementInput = {.4f, 0.f}; Tick(50);
        CHECK(customSpeedMultiplier == 1.5f && movementRequests.back().speedType == 0);
        CHECK(Get<float>(Movement, "MaxWalkSpeed") == 600.f && Get<float>(ProxyMovement, "MaxWalkSpeed") == 600.f);

        const auto file = gloss::DataDirectory() / L"transformation-speed.cfg";
        std::string savedText;
        {
            std::ifstream in(file, std::ios::binary);
            savedText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        trans::speed::Config decoded;
        CHECK(trans::speed::Parse(savedText, decoded).success && decoded.at(id) == configured);
        // Reload through the production loader, as a new application session would.
        trans::speedConfig.clear();
        trans::LoadSpeedFile();
        CHECK(trans::GetMovementSpeed(id) == configured);
        auto backup = file; backup += L".before-failure";
        std::filesystem::rename(file, backup);
        std::filesystem::create_directory(file);
        CHECK(!trans::SetMovementSpeed(id, {3.f, 4.f}).success);
        CHECK(trans::GetMovementSpeed(id) == configured);
        std::filesystem::remove(file);
        std::filesystem::rename(backup, file);
        CHECK(trans::SetMovementSpeed(id, {}).success);
        Tick(); CHECK(customSpeedMultiplier == 1.f);
        const auto writes = speedMultiplierWrites.size(); Tick();
        CHECK(speedMultiplierWrites.size() == writes);
        CHECK(trans::Restore().success); Tick();
        std::cout << "PASS role-scoped speed multipliers preserve native gait, survive reload and roll back failed saves\n";
    }
    void MovementSpeedMultiplierReleasesOnEveryPauseAndFailedWrite()
    {
        for (int pause = 0; pause != 9; ++pause)
        {
            InitializeWorld(); StartAndReachActivation();
            const auto id = trans::GetStatus().activeId;
            CHECK(trans::SetMovementSpeed(id, {2.f, 3.f}).success);
            CHECK(ConfigureTestSkillSlot());
            movementInput = {1.f, 0.f}; Tick();
            CHECK(customSpeedMultiplier == 2.f);
            if (pause == 0) movementInput = {};
            if (pause == 1) overlayVisible = true;
            if (pause == 2) foreground = false;
            if (pause == 3) simpleStates[Proxy()].insert(3);
            if (pause == 4) castingSkillId = 41001;
            if (pause == 5) playingMovie = true;
            if (pause == 6) CHECK(trans::Restore().success);
            if (pause == 7) pendingActions.push_back(trans::Action::LightAttack);
            if (pause == 8) { movementInput.right = 1.f; movementReturnOverride = 0; }
            Tick(50);
            CHECK(customSpeedMultiplier == 1.f && !speedMultiplierWrites.empty() && speedMultiplierWrites.back() == 1.f);
            if (pause != 6) { CHECK(trans::Restore().success); Tick(); }
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(trans::SetMovementSpeed(id, {}).success);
        }
        for (const auto state : {eng::CallState::NotCalled, eng::CallState::Completed, eng::CallState::Indeterminate})
        {
            InitializeWorld(); StartAndReachActivation();
            const auto id = trans::GetStatus().activeId;
            CHECK(trans::SetMovementSpeed(id, {2.f, 3.f}).success);
            movementInput = {1.f, 0.f}; failSpeedMultiplierCalls = 1; speedFailureState = state;
            Tick(); Tick();
            CHECK(customSpeedMultiplier == 1.f && trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(speedMultiplierWrites.size() == (state == eng::CallState::NotCalled ? 0u : 1u));
            CHECK(trans::SetMovementSpeed(id, {}).success);
        }
        InitializeWorld(); StartAndReachActivation();
        const auto id = trans::GetStatus().activeId;
        CHECK(trans::SetMovementSpeed(id, {2.f, 3.f}).success);
        movementInput = {1.f, 0.f}; Tick();
        failSpeedMultiplierCalls = 2;
        CHECK(trans::Restore().success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::RecoveryRequired && customSpeedMultiplier == 2.f && Proxy());
        Tick(); CHECK(trans::GetStatus().phase == trans::Phase::RecoveryRequired);
        Tick(); CHECK(trans::GetStatus().phase == trans::Phase::Idle && customSpeedMultiplier == 1.f);
        CHECK(trans::SetMovementSpeed(id, {}).success);

        InitializeWorld(); StartAndReachActivation();
        CHECK(trans::SetMovementSpeed(id, {2.f, 3.f}).success);
        movementInput = {1.f, 0.f}; Tick();
        const auto writes = speedMultiplierWrites.size();
        ++objects[Proxy()].serial; // Same address is now a different actor.
        CHECK(trans::Restore().success); Tick();
        CHECK(speedMultiplierWrites.size() == writes && trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(trans::SetMovementSpeed(id, {}).success);
        std::cout << "PASS speed overrides release for pauses, rejected moves and skills; failed cleanup retries without writing reused actors\n";
    }
    void LastMovementInputTelemetryReadsThePawnAndRemainsOptional()
    {
        for (int state = 0; state != 3; ++state)
        {
            InitializeWorld();
            lastInputAvailable = state != 1; failLastInputRead = state == 2;
            lastMovementInput = {0., -1., 0.};
            StartAndReachActivation();
            movementAnimationChoice = {true, true, true, true, true, 7}; movementInput = {1.f, 0.f}; Tick(); Tick(50);
            const auto sample = LastDiagnosticLog("Transformation movement sample:");
            CHECK(sample.find("desiredDirection=(1.000,0.000,0.000)") != std::string::npos);
            CHECK(sample.find(state == 0 ? "lastInputRead=true lastInput=(0.000,-1.000,0.000)" : "lastInputRead=false lastInput=unknown") != std::string::npos);
            CHECK(lastInputReads == (state == 1 ? 0 : 1));
            CHECK(directionInputs.empty() && facingDirections.size() == 2 && trans::GetStatus().phase == trans::Phase::Active && diagnosticWrites == 0);
            CHECK(sample.find("inputFacing=true actorYawRead=true actorYaw=37.000") != std::string::npos);
            CHECK(trans::Restore().success); Tick();
        }
        std::cout << "PASS LastMovementInput telemetry reads actual Pawn state, and missing/failed optional reads cannot block movement\n";
    }
    void MissingOrUnknownAnimationsKeepNativeJogAndLockedMovementContinuous()
    {
        const std::array choices{
            native::MotionMatchingSelection{false, false, true, true, true, 7},
            native::MotionMatchingSelection{true, false, true, true, true, 7},
            native::MotionMatchingSelection{true, true, true, false, false, -1},
            native::MotionMatchingSelection{true, true, false, true, true, 7}};
        for (const auto &choice : choices)
        {
            InitializeWorld(); StartAndReachActivation();
            movementAnimationChoice = choice;
            constexpr std::uintptr_t enemy = 0x31000;
            Add(enemy); objects[enemy].locations["actor"] = {500., 500., 0.}; lockedTarget = enemy;
            movementInput = {.5f, 0.f}; Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Active && activeMovementRequest > 0);
            CHECK(movementRequests.size() == 1 && movementRequests.back().mmState == -1);
            CHECK(movementRequests.back().speedType == 0 && movementRequests.back().moveAIType == 1);
            CHECK(actorRotationCalls > 0); // Ordinary native fallback retains combat-target facing.
            CHECK(lastAnimationSpeed == 0 && lastAnimationLocked && movementAnimationQueries == 1);
            if (!choice.available || !choice.current)
                CHECK(trans::GetStatus().message.find("动画配置暂不可读") != std::string::npos);
            else if (choice.required)
                CHECK(trans::GetStatus().message.find("缺少当前步态动画") != std::string::npos);
            for (int frame = 0; frame < 120; ++frame) Tick();
            CHECK(movementRequests.size() == 1 && movementAnimationQueries == 1 && cancelledMovementRequests.empty());
            movementInput = {1.f, 0.f}; Tick();
            CHECK(movementRequests.size() == 2 && movementRequests.back().mmState == -1);
            CHECK(movementRequests.back().speedType == 1 && movementRequests.back().moveAIType == 1);
            CHECK(movementAnimationQueries == 2 && lastAnimationSpeed == 1 && lastAnimationLocked);
            movementAnimationChoice = {true, true, true, true, true, 7}; Tick(250);
            CHECK(movementRequests.back().mmState == 7 && activeMovementRequest > 0);
            CHECK(trans::GetStatus().message.find("可能滑行") == std::string::npos);
            CHECK(trans::Restore().success); Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        }
        std::cout << "PASS unavailable/unknown/missing/non-MM animation states retain continuous native jog/run and lock intent without invented MM states\n";
    }
    void AnimationFallbackStillHonorsGameplayPausesAndCancelsOnlyItsOwnMovement()
    {
        for (int reason = 0; reason < 5; ++reason)
        {
            InitializeWorld(); StartAndReachActivation();
            movementAnimationChoice = reason % 2 ? native::MotionMatchingSelection{true, true, true, false, false, -1} :
                native::MotionMatchingSelection{false, false, true};
            movementInput = {1.f, 0.f}; Tick();
            const int ownRequest = activeMovementRequest;
            CHECK(ownRequest > 0 && movementRequests.size() == 1 && movementRequests.back().mmState == -1);
            if (reason == 0) overlayVisible = true;
            if (reason == 1) foreground = false;
            if (reason == 2)
            {
                castingSkillId = 41001;
                activeMovementRequest = 9001; // A skill replaced the indexed locomotion request.
            }
            if (reason == 3) playingMovie = true;
            if (reason == 4) simpleStates[Proxy()].insert(3);
            Tick();
            CHECK(cancelledMovementRequests == std::vector<int>{ownRequest});
            CHECK(activeMovementRequest == (reason == 2 ? 9001 : 0));
            CHECK(movementRequests.size() == 1 && trans::GetStatus().phase == trans::Phase::Active);
            for (int frame = 0; frame < 100; ++frame) Tick();
            CHECK(movementRequests.size() == 1 && cancelledMovementRequests == std::vector<int>{ownRequest});
            CHECK(trans::Restore().success); Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        }
        std::cout << "PASS animation fallback still pauses for menu/focus/casting/cinematics/game states and preserves replacement skill movement\n";
    }
    void NativeMovementContractsUseDeclaredNamesTypesAndControllerOriginalName()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(controlledPawnReads == 1);
        auto pawn = std::array{eng::Arg::Data("ReturnValue", eng::Type::Object, std::uintptr_t{}, true)};
        CHECK(!eng::Call(L"/Script/Engine.Controller:GetControlledPawn", pawn, ProxyController).success);
        CHECK(eng::Call(L"/Script/Engine.Controller:K2_GetPawn", pawn, ProxyController).success);
        CHECK(pawn[0].Get<std::uintptr_t>() == Proxy());

        // These literals come from the assembly declarations/LoadNativeType,
        // not from the production call. Reject malformed signatures before
        // any request is issued or a missing Named() result is dereferenced.
        const auto valid = std::array{
            eng::Arg::Object("Unit", Proxy()), eng::Arg::Vector("Position", {100., 0., 0.}),
            eng::Arg::Enum("SpeedRateType", 0), eng::Arg::Float("AcceptableRadius", 5.f),
            eng::Arg::Enum("MoveAIType", 3), eng::Arg::Boolean("IncludeSelfRadius", false),
            eng::Arg::Boolean("IncludeTargetRadius", false), eng::Arg::Data("ReturnValue", eng::Type::Int, 0, true)};
        for (int mutation = 0; mutation != 7; ++mutation)
        {
            auto malformed = valid;
            if (mutation == 0) malformed[3].name = "AcceptRadius";
            if (mutation == 1) malformed[2] = eng::Arg::Byte("SpeedRateType", 0);
            if (mutation == 2) malformed[1].bytes.resize(12); // Wrong FVector float layout.
            if (mutation == 3) malformed[7].output = false;
            if (mutation == 4) malformed[5].name = "IncludeTargetRadius";
            if (mutation == 5) malformed[0].name = nullptr;
            if (mutation == 6) malformed[6].bytes.resize(1); // Wrong bridge bool storage.
            CHECK(!eng::Call(L"/Script/b1-Managed.BGUFuncLibAICS:BGURequestAIMoveToLocation", malformed).success);
        }
        auto cancel = std::array{eng::Arg::Object("Unit", Proxy()), eng::Arg::Int("RequestIndex", 1)};
        CHECK(!eng::Call(L"/Script/b1-Managed.BGUFuncLibAICS:BGUCancelAIMoveTo", cancel).success);
        CHECK(movementRequests.empty() && cancelledMovementRequests.empty());

        movementInput = {1.f, 0.f};
        Tick();
        CHECK(movementRequests.size() == 1 && activeMovementRequest > 0);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS movement boundary rejects wrong names/types/layouts and controller uses native K2_GetPawn\n";
    }

    void NativeLocomotionKeepsRequestsContinuousAndStopsOnlyItsOwnIndex()
    {
        InitializeWorld();
        StartAndReachActivation();
        movementInput = {1.f, 0.f};
        Tick();
        CHECK(movementRequests.size() == 1 && activeMovementRequest > 0);
        const int first = activeMovementRequest;
        for (int frame = 0; frame != 120; ++frame) Tick();
        CHECK(movementRequests.size() == 1); // No per-frame or 100ms OnEnd/Reset loop.
        movementInput = {0.f, 1.f};
        Tick();
        CHECK(movementRequests.size() == 2 && activeMovementRequest != first);
        const int second = activeMovementRequest;
        activeMovementRequest = 9001; // A skill replaced the locomotion request.
        castingSkillId = 41001;
        Tick();
        CHECK(cancelledMovementRequests == std::vector<int>{second});
        CHECK(activeMovementRequest == 9001); // A different request index is untouched.
        CHECK(movementRequests.size() == 2);
        castingSkillId = 0;
        movementInput = {};
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS native locomotion remains continuous and indexed cancellation preserves skill movement\n";
    }

    void NativeLocomotionStopsForEveryPauseAndHandlesNavigationRejection()
    {
        for (int reason = 0; reason != 5; ++reason)
        {
            InitializeWorld();
            StartAndReachActivation();
            movementInput = {1.f, 0.f};
            Tick();
            const int ownRequest = activeMovementRequest;
            CHECK(ownRequest > 0);
            if (reason == 0) movementInput = {};
            if (reason == 1) overlayVisible = true;
            if (reason == 2) foreground = false;
            if (reason == 3) simpleStates[Proxy()].insert(3);
            if (reason == 4) CHECK(trans::Restore().success);
            Tick();
            CHECK(cancelledMovementRequests == std::vector<int>{ownRequest});
            CHECK(!activeMovementRequest);
            if (reason != 4)
            {
                CHECK(trans::GetStatus().phase == trans::Phase::Active);
                CHECK(trans::Restore().success);
                Tick();
            }
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        }
        InitializeWorld();
        StartAndReachActivation();
        movementInput = {1.f, 0.f};
        movementReturnOverride = 0;
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(trans::GetStatus().message.find("移动请求被拒绝") != std::string::npos);
        CHECK(movementRequests.size() == 1 && !activeMovementRequest);
        for (int frame = 0; frame != 120; ++frame) Tick();
        CHECK(movementRequests.size() == 1); // Rejected navigation is throttled too.
        Tick(250);
        CHECK(movementRequests.size() == 2);
        movementReturnOverride = -1;
        Tick(250);
        CHECK(activeMovementRequest > 0 && movementRequests.size() == 3);
        const int ownRequest = activeMovementRequest;
        failMovementCancelCalls = 1;
        movementInput = {};
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        CHECK(activeMovementRequest == ownRequest);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(cancelledMovementRequests == (std::vector<int>{ownRequest, ownRequest}));

        InitializeWorld();
        StartAndReachActivation();
        movementInput = {1.f, 0.f};
        failMovementRequestCalls = 1;
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS native locomotion cancels on pause, reports navigation rejection and retries failed indexed cleanup\n";
    }

    void SkillComboWaitsForCastingObservationAndStopsOnRejectedSkill()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(ConfigureTestSkillSlot({41001, 41002}));
        CHECK(trans::TriggerSlot(0).success);
        Tick();
        CHECK(requestedSkillIds == std::vector<int>{41001});

        castingSkillId = 41001;
        Tick();
        CHECK(requestedSkillIds == std::vector<int>{41001}); // The second step waits for the first cast to be observed.
        castingSkillId = 0;
        Tick();
        CHECK(requestedSkillIds == (std::vector<int>{41001, 41002}));
        castingSkillId = 41002;
        Tick();
        CHECK(requestedSkillIds == (std::vector<int>{41001, 41002}));
        castingSkillId = 0;
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);

        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);

        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(ConfigureTestSkillSlot());
        skillRequestReturnOverride = 0; // The game rejected the request.
        CHECK(trans::TriggerSlot(0).success);
        Tick();
        CHECK(requestedSkillIds == std::vector<int>{41001});
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(trans::GetStatus().message.find("连招已停止") != std::string::npos);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS production skill combo advances only after casting is observed and stops on game rejection\n";
    }

    void DeathSignalsAndZeroHealthBothRestoreTheRetainedPlayer()
    {
        for (const bool useDeathSignal : {true, false})
        {
            InitializeWorld();
            StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            const auto proxy = Proxy(), tamer = Tamer();
            CHECK(proxy && tamer);
            if (useDeathSignal) deathRequested = true;
            else proxyHealth = 0.f;

            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
            CHECK(objects[Player].collision["enabled"]);
            CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3);
            CHECK(immuneLayers[Player] == 0);
            CHECK(!Alive(proxy) && !Alive(tamer));
        }
        std::cout << "PASS production death signal and zero-health detection both restore the retained player\n";
    }

    void ActiveWorldChangeRestoresTheOriginalSessionWithoutStaleWrites()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto proxy = Proxy(), tamer = Tamer();
        CHECK(proxy && tamer);

        trans::Tick(Player, Controller, OtherWorld, 2, true);
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(Get<std::uint8_t>(Movement, "MovementMode") == 3);
        CHECK(Get<float>(SpringArm, "TargetArmLength") == 350.f);
        CHECK(immuneLayers[Player] == 0);
        CHECK(!Alive(proxy) && !Alive(tamer));
        std::cout << "PASS production active-session world generation change restores and cleans the retained session\n";
    }

    void MovementSimpleStatesRejectSpecializedDriversAndSuppressRestrictedInput()
    {
        for (const int state : {27, 54, 87, 143})
        {
            InitializeWorld();
            StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            const auto proxy = Proxy();
            CHECK(proxy != 0);
            simpleStates[proxy].insert(state);
            movementInput = {1.f, .5f};
            pendingActions = {trans::Action::LightAttack};

            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(movementRequests.empty());
            CHECK(requestedSkillIds.empty());
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
            CHECK(objects[Player].collision["enabled"]);
            CHECK(immuneLayers[Player] == 0);
            CHECK(!Alive(proxy));
        }

        for (const int state : {3, 9, 82})
        {
            InitializeWorld();
            StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            CHECK(ConfigureTestSkillSlot({41001, 41002}));
            simpleStates[Proxy()].insert(state);
            movementInput = {1.f, 0.f};
            pendingActions = {trans::Action::LightAttack};
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            CHECK(movementRequests.empty());
            CHECK(requestedSkillIds == std::vector<int>{41001});

            castingSkillId = 41001;
            Tick();
            CHECK(movementRequests.empty());
            CHECK(requestedSkillIds == std::vector<int>{41001});
            castingSkillId = 0;
            Tick();
            CHECK(movementRequests.empty());
            CHECK(requestedSkillIds == (std::vector<int>{41001, 41002}));

            simpleStates[Proxy()].clear();
            movementInput = {};
            castingSkillId = 41002;
            Tick();
            castingSkillId = 0;
            Tick();
            CHECK(requestedSkillIds == (std::vector<int>{41001, 41002}));
            CHECK(trans::Restore().success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        }

        for (const int state : {6, 12})
        {
            InitializeWorld();
            StartAndReachActivation();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            CHECK(ConfigureTestSkillSlot());
            simpleStates[Proxy()].insert(state);
            movementInput = {1.f, 0.f};
            pendingActions = {trans::Action::LightAttack};
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            CHECK(movementRequests.empty());
            CHECK(requestedSkillIds.empty());
            CHECK(pendingActions.empty());

            simpleStates[Proxy()].clear();
            movementInput = {};
            Tick();
            CHECK(requestedSkillIds.empty()); // Suppressed actions cannot fire later.
            CHECK(trans::Restore().success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        }

        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        simpleStates[Proxy()].insert(2); // Unlisted states are not assumed to block movement.
        movementInput = {1.f, 0.f};
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(movementRequests.size() == 1);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS production movement-state policy rejects dedicated drivers and suppresses only listed input states\n";
    }

    void BeginRejectsNonEmptyOrUnverifiableIsolationFieldsBeforeSpawning()
    {
        for (const int failureKind : {0, 1, 2, 3, 4})
        {
            InitializeWorld();
            trans::Initialize();
            runtimeInitialized = true;
            Tick();
            const auto catalog = trans::Catalog();
            const auto selected = std::find_if(catalog.begin(), catalog.end(), [](const auto &item)
            {
                return item.path.find("TAMER_psd_wa_01") != std::string::npos;
            });
            CHECK(selected != catalog.end());
            if (selected == catalog.end()) continue;

            if (failureKind == 0)
                PutHardReference(TamerCDOExtend, "PhaseStateGraphTemplate", 0x7a00);
            else if (failureKind == 1)
                objects[TamerCDOExtend].exportedProperties["SplineList"] = L"({DependentSpline})";
            else if (failureKind == 2)
                objects[CDOExtend].exportFailures.insert("FsmSubMachinePathOverride");
            else if (failureKind == 3)
                objects[TamerCDOExtend].exportedPropertyPaths["WakeUpConfig.TargetTamerList"] = L"(DependentTamer)";
            else
                objects[CDOExtend].exportPathFailures.insert("WakeUpConfig.TargetTamerList");

            CHECK(trans::Begin(selected->id).success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(spawnCount == 0);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(spawnCount == 0);
            CHECK(trans::GetStatus().message.find("已恢复悟空") != std::string::npos);
        }
        std::cout << "PASS production Begin rejects non-isolated or unverifiable extension fields before spawn\n";
    }

    void ReflectedHardWeakAndSoftReferencesKeepTheirOwnNullSemantics()
    {
        // The valid baseline models PhaseStateGraphTemplate/BattleSC as hard
        // pointers, FloatRelativeSpotActor as FWeakObjectProperty, and trigger
        // boxes as FSoftObjectProperty. Empty references must allow activation.
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(immuneLayers[Player] == 0);

        // FWeakObjectPtr can also be zero-initialized by managed object
        // construction before native Reset has written the {-1, 0} sentinel.
        InitializeWorld();
        PutWeakReference(TamerCDOExtend, "FloatRelativeSpotActor", {0, 0});
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);

        const auto runRejectedReference = [](std::string_view name, const auto &configure)
        {
            InitializeWorld();
            trans::Initialize();
            runtimeInitialized = true;
            Tick();
            const auto catalog = trans::Catalog();
            const auto selected = std::find_if(catalog.begin(), catalog.end(), [](const auto &entry)
            {
                return entry.path.find("TAMER_psd_wa_01") != std::string::npos;
            });
            CHECK(selected != catalog.end());
            if (selected == catalog.end()) return;
            configure();
            CHECK(trans::Begin(selected->id).success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(spawnCount == 0);
            CHECK(trans::GetStatus().message.find(name) != std::string::npos);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(spawnCount == 0);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
            CHECK(objects[Player].collision["enabled"]);
            CHECK(immuneLayers[Player] == 0);
        };

        runRejectedReference("FloatRelativeSpotActor", []
        {
            // A stale but non-null FWeakObjectPtr still records a dependency;
            // resolving it to nullptr must not turn it into an empty field.
            PutWeakReference(TamerCDOExtend, "FloatRelativeSpotActor", {0x7abc, 7});
        });
        runRejectedReference("FloatRelativeSpotActor", []
        {
            PutWeakReference(TamerCDOExtend, "FloatRelativeSpotActor", {-1, 7});
        });
        runRejectedReference("FloatRelativeSpotActor", []
        {
            PutWeakReference(TamerCDOExtend, "FloatRelativeSpotActor", {7, 0});
        });
        runRejectedReference("BeginTriggerBox", []
        {
            // Soft references remain dependencies even when their asset is not
            // loaded in this world.
            PutSoftReference(TamerCDOExtend, "BeginTriggerBox", L"/Game/Map/Trigger.Trigger");
        });
        runRejectedReference("EndTriggerBox", []
        {
            PutSoftReference(CDOExtend, "EndTriggerBox", L"/Game/Map/TriggerEnd.TriggerEnd");
        });
        runRejectedReference("FloatRelativeSpotActor", []
        {
            objects.at(TamerCDOExtend).references.at("FloatRelativeSpotActor").readable = false;
        });
        runRejectedReference("FloatRelativeSpotActor", []
        {
            objects.at(TamerCDOExtend).references.at("FloatRelativeSpotActor").kind = Object::ReferenceKind::Invalid;
        });
        runRejectedReference("BeginTriggerBox", []
        {
            objects.at(TamerCDOExtend).references.erase("BeginTriggerBox");
        });
        std::cout << "PASS hard, weak and soft reference fields distinguish explicit nulls, configured dependencies and unreadable metadata\n";
    }

    void WakeUpConfigBooleansUseNestedPropertiesAndRejectDependenciesSafely()
    {
        const auto selectedId = []() -> std::optional<std::string>
        {
            const auto catalog = trans::Catalog();
            const auto selected = std::find_if(catalog.begin(), catalog.end(), [](const auto &entry)
            {
                return entry.path.find("TAMER_psd_wa_01") != std::string::npos;
            });
            if (selected == catalog.end()) return {};
            return selected->id;
        };

        // Both false nested bools must allow ordinary activation and recovery.
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(immuneLayers[Player] == 0);

        const auto verifyCdoDependency = [&](const char *field)
        {
            InitializeWorld();
            trans::Initialize();
            runtimeInitialized = true;
            Tick();
            const auto id = selectedId();
            CHECK(id.has_value());
            if (!id) return;
            const std::string key = std::string("WakeUpConfig.") + field;
            objects.at(TamerCDOExtend).pathProperties.insert_or_assign(key, eng::Arg::Boolean(field, true));
            CHECK(trans::Begin(*id).success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(trans::GetStatus().message.find(key) != std::string::npos);
            CHECK(spawnCount == 0);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
            CHECK(objects[Player].collision["enabled"]);
            CHECK(immuneLayers[Player] == 0);
        };
        verifyCdoDependency("WakeUpLinkEnable");
        verifyCdoDependency("CanTriggerWakeUpLinkWhenDead");

        // Missing nested metadata is a read failure, not an assumed false.
        InitializeWorld();
        trans::Initialize();
        runtimeInitialized = true;
        Tick();
        const auto id = selectedId();
        CHECK(id.has_value());
        if (id)
        {
            objects.at(TamerCDOExtend).pathProperties.erase("WakeUpConfig.WakeUpLinkEnable");
            CHECK(trans::Begin(*id).success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(trans::GetStatus().message.find("WakeUpConfig.WakeUpLinkEnable") != std::string::npos);
            CHECK(spawnCount == 0);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
            CHECK(objects[Player].collision["enabled"]);
        }

        // A dependency discovered only on the initialized proxy must cancel
        // activation, destroy both temporary actors and restore the player.
        for (const auto field : {"WakeUpLinkEnable", "CanTriggerWakeUpLinkWhenDead"})
        {
            InitializeWorld();
            trans::Initialize();
            runtimeInitialized = true;
            Tick();
            const auto instanceId = selectedId();
            CHECK(instanceId.has_value());
            if (!instanceId) continue;
            CHECK(trans::Begin(*instanceId).success);
            Tick(); // CDOs pass with both nested values false.
            Tick(); // Spawn the temporary Tamer and proxy.
            const auto proxy = Proxy(), tamer = Tamer();
            CHECK(proxy && tamer);
            if (!proxy || !tamer) continue;
            const std::string key = std::string("WakeUpConfig.") + field;
            objects.at(ProxyExtend).pathProperties.insert_or_assign(key, eng::Arg::Boolean(field, true));
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(trans::GetStatus().message.find(key) != std::string::npos);
            CHECK(Alive(proxy) && Alive(tamer));
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(!Alive(proxy) && !Alive(tamer));
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
            CHECK(objects[Player].collision["enabled"]);
            CHECK(immuneLayers[Player] == 0);
        }
        std::cout << "PASS WakeUpConfig bools use nested metadata and restore safely for CDO and initialized-instance dependencies\n";
    }

    void PhaseAndBattleTagDependenciesDistinguishValueFromReadFailure()
    {
        const auto rejectCdoConfig = [](std::string_view expectedReason, const auto &configure)
        {
            InitializeWorld();
            trans::Initialize();
            runtimeInitialized = true;
            Tick();
            const auto catalog = trans::Catalog();
            const auto selected = std::find_if(catalog.begin(), catalog.end(), [](const auto &entry)
            {
                return entry.path.find("TAMER_psd_wa_01") != std::string::npos;
            });
            CHECK(selected != catalog.end());
            if (selected == catalog.end()) return;
            configure();
            CHECK(trans::Begin(selected->id).success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(trans::GetStatus().message.find(expectedReason) != std::string::npos);
            CHECK(spawnCount == 0);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(spawnCount == 0);
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
            CHECK(objects[Player].collision["enabled"]);
            CHECK(immuneLayers[Player] == 0);
        };

        // A phase graph still requires a dedicated adapter. Cooperative tags
        // are handled by complete, instance-scoped native isolation instead.
        rejectCdoConfig("形态依赖 PhaseStateGraphTemplatePath", []
        {
            Put(TamerCDOExtend, eng::Arg::String("PhaseStateGraphTemplatePath", L"/Game/Phase/Graph.Graph"));
        });
        rejectCdoConfig("角色依赖字段 PhaseStateGraphTemplatePath 无法校验", []
        {
            objects.at(TamerCDOExtend).properties.erase("PhaseStateGraphTemplatePath");
        });

        rejectCdoConfig("角色依赖字段 BattleSC_GUID.TagName 无法校验", []
        {
            objects.at(TamerCDOExtend).pathProperties.erase("BattleSC_GUID.TagName");
        });
        rejectCdoConfig("角色依赖字段 SpecificEventListen_BattleSC 无法校验", []
        {
            objects.at(TamerCDOExtend).exportFailures.insert("SpecificEventListen_BattleSC");
        });
        std::cout << "PASS phase graph dependency and unreadable cooperative properties have distinct diagnostics\n";
    }

    void CooperativeTagsAndEventsUseOwnedIsolationWithoutChangingSourceConfiguration()
    {
        for (const auto &[tagName, index] : {std::pair<std::string_view, int>{"None", 0},
                 {"UGuid.BattleSC.Huan", 22205648}, {"UGuid.BattleSC.f4", 22205628}})
        {
            InitializeWorld();
            auto value = eng::Arg::Data("TagName", eng::Type::Name, eng::NameValue{index, 0});
            value.text = std::wstring(tagName.begin(), tagName.end());
            const auto events = std::wstring(L"(UnitDead)");
            for (auto extend : {CDOExtend, TamerCDOExtend})
            {
                objects.at(extend).pathProperties.insert_or_assign("BattleSC_GUID.TagName", value);
                objects.at(extend).exportedProperties["SpecificEventListen_BattleSC"] = events;
            }
            trans::Initialize();
            runtimeInitialized = true;
            Tick();
            const auto catalog = trans::Catalog();
            const auto selected = std::find_if(catalog.begin(), catalog.end(), [](const auto &entry)
            {
                return entry.path.find("TAMER_psd_wa_01") != std::string::npos;
            });
            CHECK(selected != catalog.end());
            if (selected == catalog.end()) continue;
            CHECK(trans::Begin(selected->id).success);
            Tick();
            CHECK(spawnCount == 0);
            Tick();
            const auto proxy = Proxy(), tamer = Tamer();
            CHECK(proxy && tamer && spawnCount == 2);
            if (!proxy || !tamer) continue;
            // Also validate nonempty values on the initialized objects, rather
            // than relying only on the CDO preflight path.
            for (auto extend : {ProxyExtend, ConfigExtend})
            {
                objects.at(extend).pathProperties.insert_or_assign("BattleSC_GUID.TagName", value);
                objects.at(extend).exportedProperties["SpecificEventListen_BattleSC"] = events;
            }
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Active);
            CHECK(trans::Restore().success);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(!Alive(proxy) && !Alive(tamer));
            CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
            CHECK(objects[Player].collision["enabled"]);
            CHECK(immuneLayers[Player] == 0);
            for (auto extend : {CDOExtend, TamerCDOExtend})
            {
                const auto &unchanged = objects.at(extend).pathProperties.at("BattleSC_GUID.TagName");
                CHECK(unchanged.text == value.text && unchanged.bytes == value.bytes);
                CHECK(objects.at(extend).exportedProperties.at("SpecificEventListen_BattleSC") == events);
            }
        }

        for (const int index : {0, 22205648})
        {
            InitializeWorld();
            trans::Initialize();
            runtimeInitialized = true;
            Tick();
            const auto catalog = trans::Catalog();
            CHECK(!catalog.empty());
            if (catalog.empty()) continue;
            auto value = eng::Arg::Data("TagName", eng::Type::Name, eng::NameValue{index, 0});
            objects.at(TamerCDOExtend).pathProperties.insert_or_assign("BattleSC_GUID.TagName", value);
            CHECK(trans::Begin(catalog.front().id).success);
            nativeAdapterReady = false; // Readiness can be lost after queuing.
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
            CHECK(spawnCount == 0);
            CHECK(trans::GetStatus().message.find("角色隔离或输入桥接尚未通过当前版本校验") != std::string::npos);
            Tick();
            CHECK(trans::GetStatus().phase == trans::Phase::Idle);
            CHECK(spawnCount == 0);
            nativeAdapterReady = true;
        }
        std::cout << "PASS None and nonempty cooperative tags/events require native isolation and preserve CDOs through activation/recovery\n";
    }

    void SpecialFsmRequiresPreFinishSessionAndLatchedHealth()
    {
        InitializeWorld();
        objects[CDOExtend].exportedProperties["FsmSubMachineOverride"] = L"((SpecialRoot))";
        trans::Initialize(); runtimeInitialized = true; Tick();
        auto entries = trans::Catalog(); CHECK(!entries.empty());
        if (entries.empty()) return;
        CHECK(trans::Begin(entries.front().id).success); Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring && !spawnCount && !specialSetSessionCalls);
        CHECK(trans::GetStatus().message.find("FsmSubMachineOverride") != std::string::npos);
        Tick(); CHECK(trans::GetStatus().phase == trans::Phase::Idle);

        InitializeWorld(); specialAdapterReady = true;
        const auto source = std::wstring(L"((SpecialRoot))");
        objects[CDOExtend].exportedProperties["FsmSubMachineOverride"] = source;
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active && specialSetSessionCalls == 1 && specialObserved && specialCheckCalls > 0);
        CHECK(specialTrace.size() >= 4 && specialTrace[0] == "setProxy" && specialTrace[1] == "setSession" && specialTrace[2] == "finish" && specialTrace[3] == "check");
        CHECK(objects[CDOExtend].exportedProperties["FsmSubMachineOverride"] == source);
        const auto proxy = Proxy(), tamer = Tamer();
        specialHealthy = false; Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring && trans::GetStatus().message.find("special FSM health failed") != std::string::npos);
        CHECK(!specialClearCalls); // Isolation claim survives until owned actors are destroyed.
        Tick(); CHECK(trans::GetStatus().phase == trans::Phase::Idle && !Alive(proxy) && !Alive(tamer));
        CHECK(specialClearCalls == 1 && specialRoot.serial <= 0 && specialTrace.back() == "clearSession");
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0 && !immuneLayers[Player]);

        // A dependency first discovered on the initialized instance cannot
        // acquire a session after its FSM startup may already have executed.
        InitializeWorld(); specialAdapterReady = true;
        trans::Initialize(); runtimeInitialized = true; Tick(); entries = trans::Catalog(); CHECK(!entries.empty());
        if (entries.empty()) return;
        CHECK(trans::Begin(entries.front().id).success); Tick(); Tick();
        CHECK(spawnCount == 2 && !specialSetSessionCalls && !specialObserved);
        objects[ProxyExtend].exportedProperties["FsmSubMachineOverride"] = source;
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring && !specialSetSessionCalls && specialCheckCalls == 1);
        CHECK(trans::GetStatus().message.find("not registered before FinishSpawning") != std::string::npos);
        Tick(); CHECK(trans::GetStatus().phase == trans::Phase::Idle && !Alive(Proxy()) && !Alive(Tamer()));
        std::cout << "PASS special FSM capability gates dependencies, registers before FinishSpawning, requires startup observation and cleans active faults\n";
    }

    void InitializedProxyWithUnexpectedSceneReferenceIsCleanedAndRestoresPlayer()
    {
        InitializeWorld();
        trans::Initialize();
        runtimeInitialized = true;
        Tick();
        const auto catalog = trans::Catalog();
        const auto selected = std::find_if(catalog.begin(), catalog.end(), [](const auto &entry)
        {
            return entry.path.find("TAMER_psd_wa_01") != std::string::npos;
        });
        CHECK(selected != catalog.end());
        if (selected == catalog.end()) return;

        CHECK(trans::Begin(selected->id).success);
        Tick(); // Validate class defaults: all reference fields are empty.
        Tick(); // Spawn the temporary Tamer and proxy.
        const auto proxy = Proxy(), tamer = Tamer();
        CHECK(proxy && tamer);
        if (!proxy || !tamer) return;

        // Simulate initialization populating a scene dependency only on the
        // concrete actor instance, after its CDO already passed validation.
        PutWeakReference(ProxyExtend, "FloatRelativeSpotActor", {0x7abc, 7});
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        CHECK(trans::GetStatus().message.find("FloatRelativeSpotActor") != std::string::npos);
        CHECK(Alive(proxy) && Alive(tamer));
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(!Alive(proxy) && !Alive(tamer));
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(immuneLayers[Player] == 0);
        std::cout << "PASS initialized proxy scene dependencies are detected after CDO validation and cleaned before player recovery completes\n";
    }

    void DeferredSpawnRequiresEnumCollisionHandlingAndRestoresNormally()
    {
        InitializeWorld();
        auto badArgs = std::array{
            eng::Arg::Object("WorldContextObject", World),
            eng::Arg::Object("ActorClass", TamerClass),
            eng::Arg::TransformValue("SpawnTransform", {}),
            eng::Arg::Byte("CollisionHandlingOverride", 1),
            eng::Arg::Object("Owner", Player),
            eng::Arg::Data("ReturnValue", eng::Type::Object, std::uintptr_t{}, true)};
        const auto objectCount = objects.size();
        const auto nextBefore = nextActor;
        const auto rejected = eng::Call(L"/Script/b1.BGUFunctionLibrary:BGUBeginDeferredActorSpawnFromClass", badArgs);
        CHECK(!rejected.success);
        CHECK(rejected.state == eng::CallState::NotCalled);
        CHECK(rejectedSpawnRequests == 1);
        CHECK(validSpawnRequests == 0);
        CHECK(spawnCount == 0);
        CHECK(objects.size() == objectCount);
        CHECK(nextActor == nextBefore);

        // The production request uses the reflected enum carrier and keeps the
        // rest of the six named in/out parameters intact through normal cleanup.
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(validSpawnRequests == 2); // Temporary Tamer config plus proxy.
        CHECK(rejectedSpawnRequests == 0);
        CHECK(spawnCount == 2);
        const auto proxy = Proxy(), tamer = Tamer();
        CHECK(proxy && tamer);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(!Alive(proxy) && !Alive(tamer));
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(immuneLayers[Player] == 0);
        std::cout << "PASS deferred spawn rejects byte collision mode without creating an actor and accepts the reflected enum during a full session\n";
    }

    void RuntimeProxyCustomMovementModeRejectsAndRestoresGenericCatalogPath()
    {
        InitializeWorld();
        StartAndReachActivation("TAMER_psd_wa_01");
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto proxy = Proxy();
        CHECK(proxy != 0);
        Put(ProxyMovement, eng::Arg::Byte("CustomMovementMode", 1));
        movementInput = {1.f, 0.f};
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(immuneLayers[Player] == 0);
        CHECK(!Alive(proxy));
        std::cout << "PASS production custom movement discovered on an initialized non-starter path rejects and restores\n";
    }

    void CompiledReferenceCatalogDeduplicatesPathsWithoutPromotingSupport()
    {
        InitializeWorld();
        trans::Initialize();
        runtimeInitialized = true;
        Tick();
        const auto entries = trans::Catalog();
        static constexpr trans::pak_catalog::Record references[] = {
#include "../src/gameplay/transformation_catalog.inc"
        };
        std::vector<trans::CharacterEntry> expected;
        for (const auto &record : references)
        {
            trans::CharacterEntry entry;
            entry.name = record.name;
            entry.path = record.path;
            entry.category = trans::model::CatalogCategory(record.category);
            trans::model::MergeCatalog(expected, std::move(entry));
        }
        CHECK(expected.size() == 246); // The old source and stable IDs stay intact.
        const auto oldEntries = expected;
        trans::pak_catalog::Seed(expected);
        std::unordered_set<std::string> paths, ids;
        for (const auto &entry : entries)
        {
            CHECK(entry.support == trans::Support::Unverified);
            CHECK(!entry.discovered);
            CHECK(entry.id == gloss::gameplay::transformation::model::StableId(entry.path, entry.form));
            CHECK(paths.insert(entry.path).second);
            CHECK(ids.insert(entry.id).second);
        }
        CHECK(entries.size() == expected.size());
        CHECK(entries.size() > 246);
        CHECK(paths.size() == expected.size());
        CHECK(ids.size() == expected.size());
        for (std::size_t i = 0; i < oldEntries.size(); ++i)
        {
            CHECK(entries[i].id == oldEntries[i].id);
            CHECK(entries[i].name == oldEntries[i].name);
            CHECK(entries[i].path == oldEntries[i].path);
        }
        std::cout << "PASS reference source keeps 246 stable paths and PAK candidates expand the unverified catalog to " << entries.size() << " entries\n";
    }

    void RegistryScanIncludesBossRushAndRetainsPartialRootResults()
    {
        InitializeWorld();
        registryAvailable = true;
        constexpr auto unitsRoot = L"/Game/00Main/Design/Units";
        constexpr auto bossRoot = L"/Game/00MainHZ/BossRush/Tamer";
        const auto before = trans::Catalog();
        const auto candidate = std::find_if(before.begin(), before.end(), [](const auto &entry)
        {
            return entry.path.starts_with("/Game/00MainHZ/BossRush/Tamer/");
        });
        CHECK(candidate != before.end());
        if (candidate == before.end()) return;
        const auto existingId = candidate->id;
        const auto originalName = candidate->name;
        const auto record = [](std::string_view classPath, const wchar_t *assetName, const wchar_t *assetClass = L"Blueprint")
        {
            std::wstring path(classPath.begin(), classPath.end());
            if (path.ends_with(L"_C")) path.resize(path.size() - 2);
            return std::vector<eng::Arg>{eng::Arg::Name("ObjectPath", path), eng::Arg::Name("AssetName", assetName), eng::Arg::Name("AssetClass", assetClass)};
        };
        const std::string newPath = "/Game/00MainHZ/BossRush/Tamer/BRT_fixture_runtime_only.BRT_fixture_runtime_only_C";
        registryRoots[unitsRoot].success = false; // BossRush must still be queried.
        registryRoots[bossRoot].records = {
            record(candidate->path, L"BRT_OfflineDuplicate"),
            record(newPath, L"BRT_fixture_runtime_only"),
            record(newPath, L"brt_fixture_runtime_only", L"BlueprintGeneratedClass"),
            record("/Game/Fixture/SK_skip.SK_skip_C", L"SK_skip"),
            record("/Game/Fixture/BRT_material.BRT_material_C", L"BRT_material", L"Material"),
            {eng::Arg::Name("ObjectPath", L"invalid")}};
        CHECK(trans::RequestCatalogRefresh().success);
        Tick(); Tick();
        CHECK(queriedRegistryRoots == std::vector<std::wstring>{unitsRoot, bossRoot});
        auto entries = trans::Catalog();
        CHECK(entries.size() == before.size() + 1);
        const auto offline = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) { return entry.id == existingId; });
        CHECK(offline != entries.end());
        CHECK(offline->name == originalName && offline->discovered && offline->support == trans::Support::Unverified);
        const auto dynamicId = trans::model::StableId(newPath);
        const auto dynamic = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) { return entry.id == dynamicId; });
        CHECK(dynamic != entries.end());
        CHECK(dynamic->discovered && dynamic->support == trans::Support::Unverified);
        CHECK(!trans::GetStatus().catalogComplete);
        CHECK(trans::GetStatus().catalogMessage.find("部分目录枚举失败") != std::string::npos);
        CHECK(trans::GetStatus().catalogMessage.find("2 个 Tamer") != std::string::npos);
        queriedRegistryRoots.clear();
        CHECK(trans::RequestCatalogRefresh().success);
        Tick(); Tick();
        CHECK(trans::Catalog().size() == entries.size());
        CHECK(queriedRegistryRoots == std::vector<std::wstring>{unitsRoot, bossRoot});

        // Reverse the failure: a missing BossRush root must also retain Units.
        registryRoots[unitsRoot].success = true;
        registryRoots[unitsRoot].records = {record("/Game/00Main/Design/Units/TAMER_fixture_extra.TAMER_fixture_extra_C", L"TAMER_fixture_extra")};
        registryRoots[bossRoot].success = false;
        queriedRegistryRoots.clear();
        CHECK(trans::RequestCatalogRefresh().success);
        Tick(); Tick();
        CHECK(trans::Catalog().size() == entries.size() + 1);
        CHECK(queriedRegistryRoots == std::vector<std::wstring>{unitsRoot, bossRoot});
        CHECK(!trans::GetStatus().catalogComplete);
        CHECK(trans::GetStatus().catalogMessage.find("部分目录枚举失败") != std::string::npos);

        // Both available roots still make no claim to a complete form catalog.
        registryRoots[bossRoot].success = true;
        registryLoadingKnown = false;
        CHECK(trans::RequestCatalogRefresh().success);
        Tick(); Tick();
        CHECK(trans::Catalog().size() == entries.size() + 1);
        CHECK(!trans::GetStatus().catalogComplete);
        CHECK(trans::GetStatus().catalogMessage.find("部分目录枚举失败") == std::string::npos);
        CHECK(trans::GetStatus().catalogMessage.find("资源仍可能加载中") != std::string::npos);

        // Restore the seeded list so the independent gameplay fixtures below
        // do not inherit registry-only synthetic assets or live discovery flags.
        trans::catalog = before;
        registryAvailable = false;
        std::cout << "PASS Asset Registry scans Units and BossRush independently, preserves PAK names, deduplicates repeated assets, and reports partial coverage\n";
    }

    void PakOnlyBossRushCandidateUsesTheNormalInitializationAndRestorePipeline()
    {
        InitializeWorld();
        const auto entries = trans::Catalog();
        const auto candidate = std::find_if(entries.begin(), entries.end(), [](const auto &entry)
        {
            return entry.path.starts_with("/Game/00MainHZ/BossRush/Tamer/");
        });
        CHECK(candidate != entries.end());
        if (candidate == entries.end()) return;
        const auto id = candidate->id;
        CHECK(candidate->support == trans::Support::Unverified && !candidate->discovered);
        StartAndReachActivation(candidate->path);
        CHECK(trans::GetStatus().phase == trans::Phase::Active && trans::GetStatus().activeId == id);
        CHECK(validSpawnRequests == 2 && weaponInitializeCalls == 1);
        const auto proxy = Proxy(), tamer = Tamer();
        CHECK(proxy && tamer);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(!Alive(proxy) && !Alive(tamer));
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        const auto after = trans::Catalog();
        const auto entry = std::find_if(after.begin(), after.end(), [&](const auto &item) { return item.id == id; });
        CHECK(entry != after.end() && entry->support != trans::Support::Verified);
        std::cout << "PASS PAK-only BossRush candidate reaches the real driver initialization and restore pipeline through mocked engine boundaries; this is not in-game validation\n";
    }

    void BeginCanBeAttemptedWithoutInspectionForUnverifiedOrUnavailableEntries()
    {
        InitializeWorld();
        trans::Initialize();
        runtimeInitialized = true;
        Tick();

        const auto catalog = trans::Catalog();
        const auto selected = std::find_if(catalog.begin(), catalog.end(), [](const auto &entry)
        {
            return entry.path.find("TAMER_psd_wa_01") != std::string::npos;
        });
        CHECK(selected != catalog.end());
        if (selected == catalog.end()) return;
        CHECK(selected->support == trans::Support::Unverified);

        // Do not call Inspect first. The Begin request must reach normal resource
        // loading even while the catalogue entry has never been verified.
        invalidTamerResource = true;
        CHECK(trans::Begin(selected->id).success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        CHECK(spawnCount == 0);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(spawnCount == 0);
        const auto unavailableCatalog = trans::Catalog();
        const auto unavailable = std::find_if(unavailableCatalog.begin(), unavailableCatalog.end(), [&](const auto &entry)
        {
            return entry.id == selected->id;
        });
        CHECK(unavailable != unavailableCatalog.end());
        if (unavailable == unavailableCatalog.end()) return;
        CHECK(unavailable->support == trans::Support::Unavailable);
        CHECK(!unavailable->reason.empty());

        // A currently unavailable resource can be retried directly. Once the
        // mock resource is valid, normal initialization succeeds without Inspect.
        invalidTamerResource = false;
        CHECK(trans::Begin(unavailable->id).success);
        Tick();
        Tick();
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(trans::GetStatus().activeId == unavailable->id);
        CHECK(Proxy() != 0 && Tamer() != 0);
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(Get<std::int32_t>(Player, "bHidden") == 0);
        CHECK(objects[Player].collision["enabled"]);
        CHECK(immuneLayers[Player] == 0);
        std::cout << "PASS Begin skips support-state prechecks, retries an unavailable entry, and restores after initialization failure\n";
    }

    void RestoreDestroysOwnedChildrenInReverseOrderAndRetriesDeferredRequests()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto proxy = Proxy(), tamer = Tamer();
        constexpr std::uintptr_t firstChild = 0x20000;
        constexpr std::uintptr_t lastChild = 0x20001;
        CHECK(proxy && tamer);
        AddOwnedChild(firstChild);
        AddOwnedChild(lastChild);
        deferredDestroyCalls[lastChild] = 1;

        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::RecoveryRequired);
        CHECK(destroyRequests == std::vector<std::uintptr_t>{lastChild});
        CHECK(Alive(firstChild) && Alive(lastChild) && Alive(proxy) && Alive(tamer));

        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(destroyRequests == (std::vector<std::uintptr_t>{lastChild, lastChild, firstChild, proxy, tamer}));
        CHECK(cleanupActors == (std::vector<std::uintptr_t>{lastChild, lastChild, firstChild, proxy}));
        CHECK(!Alive(firstChild) && !Alive(lastChild) && !Alive(proxy) && !Alive(tamer));
        CHECK(cleanupConstructs == cleanupDestructs && !cleanupScopeActive);
        std::cout << "PASS production recovery clears deepest owned children before roots and retains handles across deferred destroy\n";
    }

    void ActiveCleanupDestroysOnlyOwnedChildrenMarkedDead()
    {
        InitializeWorld();
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        constexpr std::uintptr_t deadChild = 0x21000;
        constexpr std::uintptr_t liveChild = 0x21001;
        AddOwnedChild(deadChild, true);
        AddOwnedChild(liveChild, false);

        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        CHECK(!Alive(deadChild));
        CHECK(Alive(liveChild));
        CHECK(destroyRequests == std::vector<std::uintptr_t>{deadChild});
        CHECK(cleanupActors == std::vector<std::uintptr_t>{deadChild});

        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(!Alive(liveChild));
        CHECK(!Alive(Proxy()) && !Alive(Tamer()));
        CHECK(cleanupActors == (std::vector<std::uintptr_t>{deadChild, liveChild, Proxy()}));
        std::cout << "PASS production active cleanup retires dead owned children and preserves live children until restore\n";
    }

    void RestoreRechecksChildrenCreatedByChildAndProxyDestruction()
    {
        InitializeWorld();
        StartAndReachActivation("TAMER_psd_wa_01");
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto proxy = Proxy(), tamer = Tamer();
        constexpr std::uintptr_t firstChild = 0x22000;
        constexpr std::uintptr_t childAfterChild = 0x22001;
        constexpr std::uintptr_t childAfterProxy = 0x22002;
        CHECK(proxy && tamer);
        AddOwnedChild(firstChild);
        spawnChildOnDestroy[firstChild] = childAfterChild;
        spawnChildOnDestroy[proxy] = childAfterProxy;

        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        CHECK(Alive(childAfterChild) && Alive(proxy) && Alive(tamer));
        CHECK(destroyRequests == std::vector<std::uintptr_t>{firstChild});

        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Restoring);
        CHECK(!Alive(proxy) && Alive(childAfterProxy) && Alive(tamer));
        CHECK(destroyRequests == (std::vector<std::uintptr_t>{firstChild, childAfterChild, proxy}));

        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        CHECK(destroyRequests == (std::vector<std::uintptr_t>{firstChild, childAfterChild, proxy, childAfterProxy, tamer}));
        CHECK(cleanupActors == (std::vector<std::uintptr_t>{firstChild, childAfterChild, proxy, childAfterProxy}));
        CHECK(!Alive(firstChild) && !Alive(childAfterChild) && !Alive(childAfterProxy));
        CHECK(!Alive(proxy) && !Alive(tamer));
        std::cout << "PASS production restore repeats child discovery after child and proxy destroy requests\n";
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

    void InitializedProxySkillSnapshotOverridesClassDefaults()
    {
        InitializeWorld();
        availableSkillIds = {41001, 41002}; // Class defaults expose these IDs.
        instanceSkillIds = {42001, 42002}; // The initialized proxy exposes a different runtime set.
        useInstanceSkillIds = true;
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto id = trans::GetStatus().activeId;
        CHECK(SameSkillIds(trans::SkillsFor(id).skills, instanceSkillIds));
        const auto defaults = trans::Bindings(id);
        CHECK(defaults.size() == 2);
        if (defaults.size() == 2)
        {
            CHECK(defaults[0].skillId == 42001 && defaults[0].key == 1);
            CHECK(defaults[1].skillId == 42002 && defaults[1].key == 2);
        }
        CHECK(ConfigureTestSkillSlot({42002}));
        CHECK(!trans::TriggerSkill(41001).success);
        CHECK(trans::TriggerSkill(42002).success);
        Tick();
        CHECK(requestedSkillIds == std::vector<int>{42002});
        CHECK(trans::Restore().success);
        Tick();
        CHECK(trans::GetStatus().phase == trans::Phase::Idle);
        std::cout << "PASS production initialized proxy skill IDs replace class-default IDs before accepting skill input\n";
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
        CHECK(trans::Bindings("not-inspected").empty());
        const auto yangBindings = trans::Bindings(yangjian->id);
        const auto dashengBindings = trans::Bindings(dasheng->id);
        CHECK(yangBindings.size() == 2 && dashengBindings.size() == 4);
        if (yangBindings.size() == 2 && dashengBindings.size() == 4)
        {
            CHECK(yangBindings[0].skillId == 41011 && yangBindings[0].key == 1);
            CHECK(dashengBindings[0].skillId == 42011 && dashengBindings[0].key == 1);
            CHECK(dashengBindings[3].skillId == 42014 && dashengBindings[3].key == '2');
        }
        CHECK(hotkeyConflictProbe && !hotkeyConflictProbe(1, 0).empty());
        CHECK(hotkeyConflictProbe && hotkeyConflictProbe(1, gloss::features::Ctrl).empty());
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

    void DefaultBindingsPreserveSavedEditsAndExplicitEmptyConfigurations()
    {
        InitializeWorld();
        availableSkillIds.clear();
        for (int i = 0; i < 14; ++i) availableSkillIds.push_back(51014 - i);
        StartAndReachActivation();
        CHECK(trans::GetStatus().phase == trans::Phase::Active);
        const auto id = trans::GetStatus().activeId;
        const auto defaults = trans::Bindings(id);
        constexpr std::array<unsigned, 14> expected{1, 2, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0, 0};
        CHECK(defaults.size() == expected.size());
        for (std::size_t i = 0; i < defaults.size() && i < expected.size(); ++i)
        {
            CHECK(defaults[i].skillId == availableSkillIds[i]);
            CHECK(defaults[i].key == expected[i] && defaults[i].modifiers == 0);
            CHECK(defaults[i].customName.empty());
        }
        // Use the effective binding directly, without first saving the editor.
        CHECK(trans::TriggerSkillHotkey(id, defaults.front().skillId).success);
        Tick();
        CHECK(requestedSkillIds == std::vector<int>{51014});
        CHECK(trans::Restore().success);
        Tick();

        auto custom = defaults;
        custom[0].customName = "保留自定义名称与留空";
        custom[0].key = 0;
        custom[2].key = 'Q';
        custom[2].modifiers = gloss::features::Ctrl;
        CHECK(trans::SetBindings(id, custom).success);
        std::reverse(availableSkillIds.begin(), availableSkillIds.end());
        availableSkillIds.push_back(59999);
        CHECK(trans::Inspect(id).success);
        Tick();
        CHECK(SameBindings(trans::Bindings(id), custom));
        // Reordering or extending the game list must never reassign saved IDs.
        const auto file = gloss::DataDirectory() / L"transformation-bindings.cfg";
        {
            std::ifstream in(file, std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            binding_model::Config decoded;
            CHECK(binding_model::Parse(text, decoded).success);
            CHECK(decoded.contains(id) && SameBindings(decoded.at(id), custom));
        }
        CHECK(trans::SetBindings(id, {}).success);
        CHECK(trans::Bindings(id).empty());
        CHECK(trans::Inspect(id).success);
        Tick();
        CHECK(trans::Bindings(id).empty());
        {
            std::ifstream in(file, std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            binding_model::Config decoded;
            CHECK(binding_model::Parse(text, decoded).success);
            CHECK(decoded.contains(id) && decoded.at(id).empty());
        }
        std::cout << "PASS defaults work before save and saved names, cleared keys and empty configurations take priority\n";
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
    fixture::CompiledReferenceCatalogDeduplicatesPathsWithoutPromotingSupport();
    fixture::RegistryScanIncludesBossRushAndRetainsPartialRootResults();
    fixture::PakOnlyBossRushCandidateUsesTheNormalInitializationAndRestorePipeline();
    fixture::BeginCanBeAttemptedWithoutInspectionForUnverifiedOrUnavailableEntries();
    fixture::ActivationFailureRestoresPartialChanges();
    fixture::WeaponInitializationRunsOnceAfterOwnerVisibilityAndFailureRestores();
    fixture::WeaponReadinessWaitsBeforePlayerMutationAndCancelsCleanly();
    fixture::TargetClearFailureRollsBackInitializationAndAllowsRetry();
    fixture::FailedRestoreRetainsBaselineAndRetries();
    fixture::ReusedPlayerAddressDoesNotReceiveStaleWrites();
    fixture::DamageProtectionPreservesExistingReferenceLayers();
    fixture::FailedImmunityRemovalKeepsProxyAndRetriesWithoutDroppingLayer();
    fixture::IndeterminateImmunityCounterCallsAreNeverReplayed();
    fixture::SkillSlotWaitsForMenuAndForegroundThenFiresOnce();
    fixture::MovementUsesCameraYawAndMappedInputDispatchesConfiguredSkill();
    fixture::MovementDiagnosticsAreDeferredBoundedReadOnlyAndResetPerSession();
    fixture::MovementDiagnosticsUnknownsNeverAbortOrProbeEveryFrame();
    fixture::AnimationDiagnosticsDistinguishNullInapplicableAndUnreadable();
    fixture::LocomotionGraphDiagnosticsReadRealMirrorsAndRespectAllGates();
    fixture::NativeMovementContractsUseDeclaredNamesTypesAndControllerOriginalName();
    fixture::MotionMatchingMovementRequestsChooseRoleResourcesAndKeepUnknownDistinct();
    fixture::RoleLockAnalyzerWithoutTargetKeepsFreeMovementAndRestoresNormally();
    fixture::MotionMatchingFacingRespectsIntentAndEveryPause();
    fixture::FacingOverrideTransitionsCancelBeforeRealLocksAndOtherMovers();
    fixture::MappedSprintSelectsRoleSprintGaitAndReturnsToAnalogWalking();
    fixture::LastMovementInputTelemetryReadsThePawnAndRemainsOptional();
    fixture::MissingOrUnknownAnimationsKeepNativeJogAndLockedMovementContinuous();
    fixture::AnimationFallbackStillHonorsGameplayPausesAndCancelsOnlyItsOwnMovement();
    fixture::NativeLocomotionKeepsRequestsContinuousAndStopsOnlyItsOwnIndex();
    fixture::NativeLocomotionStopsForEveryPauseAndHandlesNavigationRejection();
    fixture::SkillComboWaitsForCastingObservationAndStopsOnRejectedSkill();
    fixture::DeathSignalsAndZeroHealthBothRestoreTheRetainedPlayer();
    fixture::ActiveWorldChangeRestoresTheOriginalSessionWithoutStaleWrites();
    fixture::MovementSimpleStatesRejectSpecializedDriversAndSuppressRestrictedInput();
    fixture::BeginRejectsNonEmptyOrUnverifiableIsolationFieldsBeforeSpawning();
    fixture::ReflectedHardWeakAndSoftReferencesKeepTheirOwnNullSemantics();
    fixture::WakeUpConfigBooleansUseNestedPropertiesAndRejectDependenciesSafely();
    fixture::PhaseAndBattleTagDependenciesDistinguishValueFromReadFailure();
    fixture::CooperativeTagsAndEventsUseOwnedIsolationWithoutChangingSourceConfiguration();
    fixture::SpecialFsmRequiresPreFinishSessionAndLatchedHealth();
    fixture::InitializedProxyWithUnexpectedSceneReferenceIsCleanedAndRestoresPlayer();
    fixture::DeferredSpawnRequiresEnumCollisionHandlingAndRestoresNormally();
    fixture::RuntimeProxyCustomMovementModeRejectsAndRestoresGenericCatalogPath();
    fixture::RestoreDestroysOwnedChildrenInReverseOrderAndRetriesDeferredRequests();
    fixture::ActiveCleanupDestroysOnlyOwnedChildrenMarkedDead();
    fixture::RestoreRechecksChildrenCreatedByChildAndProxyDestruction();
    fixture::QueuedSkillSlotTimeoutDoesNotCast();
    fixture::QueuedSkillSlotIsCancelledOnWorldGenerationChange();
    fixture::InitializedProxySkillSnapshotOverridesClassDefaults();
    fixture::SkillSnapshotsStayScopedToTheInspectedCharacter();
    fixture::DefaultBindingsPreserveSavedEditsAndExplicitEmptyConfigurations();
    fixture::PerSkillRequestsWaitForMenuAndFocusAndDispatchOnlyOnce();
    fixture::PerSkillBindingsValidateIdsAndRollbackMemoryAndFileFailures();
    fixture::PerCharacterCameraPersistsUpdatesAtomicallyAndRestoresThePlayerBaseline();
    fixture::CinematicPausesAndResumesTheSameCharacterWithoutOverwritingThePerformer();
    fixture::CinematicRetainsChildVisibilityAndResumesOnlyAfterStableBlendOut();
    fixture::CinematicResumeFailureReleasesReacquiredStateWithoutRespawning();
    fixture::CinematicResumeRechecksBothLivesAndSpecialStateBeforeAnyPlayerMutation();
    fixture::CinematicSameValueRequestsAndDirectHiddenWritesRemainOwnedByTheGame();
    fixture::UnreadableCinematicStateRestoresWithoutTeleportingTheRetainedPlayer();
    fixture::CinematicActivationIsRejectedBeforePlayerMutation();
    fixture::NonCinematicIsolationChangesAreReassertedOnlyWhenRequired();
    fixture::PerCharacterSpeedMultipliersPreserveNativeGaitsAndPersistAtomically();
    fixture::MovementSpeedMultiplierReleasesOnEveryPauseAndFailedWrite();
    if (fixture::failures)
    {
        std::cerr << fixture::failures << " transformation runtime assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer transformation runtime tests passed (0 failures)\n";
    return 0;
}
