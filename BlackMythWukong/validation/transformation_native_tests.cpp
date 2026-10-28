#include "../src/gameplay/transformation_native.cpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>

namespace
{
    struct Object
    {
        std::array<std::uintptr_t, 32> managed{};
        std::array<std::uintptr_t, 8> reference{};
        std::array<std::uintptr_t, 12> native{};
        int team{}, tamer{};
        Object *owner{};
        bool ownerReadable{true};
        bool isWeapon{}, weaponActive{}, weaponInitialized{true};
        Object *weaponMaster{};
        bool animTop{}, animMove{}, animMM{};
        bool hidden{};
        std::uint8_t mode{3}, collision{3};
        Object()
        {
            managed[0] = reference[0] = native[0] = 1;
            managed[0x58 / 8] = reinterpret_cast<std::uintptr_t>(native.data());
            reference[0x10 / 8] = reinterpret_cast<std::uintptr_t>(managed.data());
            reference[0x20 / 8] = reinterpret_cast<std::uintptr_t>(native.data());
        }
        std::uintptr_t Address() { return reinterpret_cast<std::uintptr_t>(native.data()); }
        std::uintptr_t Managed() { return reinterpret_cast<std::uintptr_t>(managed.data()); }
    } proxy, player, other, child, grandchild, playerMovement, playerCapsule, playerMesh, animationMesh, mmNode;
    struct Binding { int serial; Object *object; bool pendingKill{}; };
    std::map<int, Binding> bindings;
    DWORD gameThreadId = GetCurrentThreadId();
    int failures{}, setterCalls{}, targetCalls{}, clearTargetCalls{}, inputParameter{};
    int inputSpeedMode{2}, sprintInputFailureStage{};
    std::uintptr_t sprintMethodAddress{};
    int findRefCalls{}, getEventCollectionCalls{}, getClearEventCalls{}, rpcReflectionAttempts{}, clearFailureStage{};
    std::uintptr_t eventCollectionManaged{}, clearCallbackManaged{};
    std::array<std::uintptr_t, 24> clearCallbackObject{};
    std::array<std::uintptr_t, 4> facingCallbackObject{}, facingSubscriberObject{};
    int facingCalls{}, facingFailureStage{};
    gloss::gameplay::engine::Rotator facingRotation{};
    std::array<std::uintptr_t, 4> masterCallbackObject{}, activeCallbackObject{};
    std::array<std::uintptr_t, 4> masterSubscriberObject{}, activeSubscriberObject{};
    int weaponReadyCalls{}, weaponReadyFailure{}, weaponReadyFlipCall{};
    std::uintptr_t unreadableAddress{};
    bool weaponRetireOnReady{}, weaponDetachOnReady{}, weaponRetireOnActive{};
    std::array<std::uintptr_t, 32> animationSetting{}, animationCommon{}, animationMatching{}, animationEcs{};
    int animationGetterCalls{}, animationFailureStage{}, animationChangeStage{}, animationOwnerCalls{}, animationModeCalls{};
    bool animationRetire{}, animationDetach{}, animationNewSession{};
    std::array<std::uintptr_t, 32> animationMmData{};
    std::array<bool, 9> mmKeys{};
    std::vector<std::uint32_t> mmQueriedStates;
    bool mmReflection{}, mmMovePresent{true}, mmNodePresent{true}, mmGraphChanged{};
    int mmReflectionFailure{}, mmQueryFailure{}, mmChangeStage{}, mmGraphCalls{}, mmQueryEffectAt{};
    constexpr int animationMeshIndex = 100, mmNodeIndex = 101;
    std::vector<std::vector<gloss::gameplay::engine::Arg>> weaponRecords;
    std::map<int, Object *> weaponByIndex;
    int weaponExtent{}, weaponFailureStage{}, weaponMasterCalls{}, weaponActiveCalls{}, weaponLookupCalls{};
    bool weaponTableReadable{true}, weaponCountReadable{true}, weaponLookupReadable{true};
    int weaponHiddenReads{}, weaponHiddenFailureRead{};
    bool weaponActiveNoOp{};
    std::vector<std::string> diagnosticLogs;
    std::uintptr_t weaponCollection{}, masterCallback{}, activeCallback{};
    Object *weaponEventOwner{};
    std::array<std::array<std::uintptr_t, 4>, 3> propertyCallbacks{};
    std::array<std::uintptr_t, 24> propComponent{};
    std::array<std::uintptr_t, 12> propData{};
    struct PropRequest { std::uint32_t handle; int property, value; };
    std::vector<PropRequest> propRequests;
    int propSetterCalls{}, propResetCalls{}, propApplyCalls{}, propFailureStage{};
    int playerStateReads{}, playerStateFailureRead{};
    std::vector<int> appliedProperties;
    int duplicateProperty{};
    bool retirePlayerOnApply{}, revealPlayerOnMovement{};
    float inputAxis{};
    bool battleAllowed{true};
    bool simulateHookInstall{};
    std::uintptr_t rejectedFingerprint{};
    int failHookAt{-1}, hookCreateCalls{}, hookDestroyed{}, fingerprintChecks{}, checksAtFirstHook{};
    std::array<std::uintptr_t, 5> descriptor{};
    std::array<std::uintptr_t, 4> parameters{};
    std::uintptr_t inputManaged{};
    using MonoStackData = gloss::gameplay::transformation::native::detail::MonoStackData;
    struct GcEntry { MonoStackData *stack; std::uintptr_t cookie; };
    std::vector<GcEntry> gcEntries;
    bool gcUnsafe{}, monoAttached{true}, simulateRuntimeResolver{}, nestedClear{};
    int gcEnterCalls{}, gcExitCalls{}, gcCurrentCalls{}, gcFailureStage{}, rejectedRuntimePattern{-1}, duplicateRuntimePattern{-1}, outsideRuntimePattern{-1};
#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)
    std::uintptr_t CurrentMonoThread()
    {
        ++gcCurrentCalls;
        if (gcFailureStage == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        return monoAttached ? 0x12340000ull : 0;
    }
    std::uintptr_t EnterMonoUnsafe(MonoStackData *stack)
    {
        ++gcEnterCalls;
        CHECK(stack && stack->stackPointer == stack && stack->functionName && *stack->functionName);
        if (gcFailureStage == 2) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        const auto cookie = gcUnsafe ? 0 : 0x12340000ull;
        gcEntries.push_back({stack, cookie});
        gcUnsafe = true;
        return cookie;
    }
    void ExitMonoUnsafe(std::uintptr_t cookie, MonoStackData *stack)
    {
        ++gcExitCalls;
        CHECK(gcUnsafe && !gcEntries.empty());
        CHECK(stack && stack->stackPointer == stack);
        if (!gcEntries.empty())
        {
            CHECK(gcEntries.back().stack == stack && gcEntries.back().cookie == cookie);
            gcEntries.pop_back();
        }
        if (cookie)
            gcUnsafe = false;
        if (gcFailureStage == 3) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }
    Object *Lookup(std::uintptr_t address)
    {
        for (const auto &[index, binding] : bindings)
        {
            auto *object = binding.object;
            if (object->Address() == address || object->Managed() == address)
                return object;
        }
        return nullptr;
    }
    std::uintptr_t FindRef(std::uintptr_t address)
    {
        CHECK(gcUnsafe);
        ++findRefCalls;
        auto *object = Lookup(address);
        return object ? reinterpret_cast<std::uintptr_t>(object->reference.data()) : 0;
    }
    void EmitHidden(bool value, bool propertySource)
    {
        struct Frame { gloss::gameplay::Context context{}; std::array<std::uintptr_t, 8> stack{}; } frame;
        auto &context = frame.context;
        frame.stack[0] = propertySource ? gloss::gameplay::transformation::native::methods.hiddenPropertyReturn : 0x12345000;
        context.rcx = player.Managed(); context.rdx = value;
        gloss::gameplay::transformation::native::ObservePlayerHidden(context, nullptr);
        player.hidden = static_cast<std::uint8_t>(context.rdx) != 0;
    }
    void ApplyProperty(std::uintptr_t component, int property)
    {
        CHECK(gcUnsafe && component == reinterpret_cast<std::uintptr_t>(propComponent.data()));
        ++propApplyCalls;
        appliedProperties.push_back(property);
        int value = property == 302 ? 0 : property == 13 ? 3 : 3;
        for (auto request = propRequests.rbegin(); request != propRequests.rend(); ++request)
            if (request->property == property) { value = request->value; break; }
        if (property == 302) EmitHidden(value != 0, true);
        if (property == 13) playerMovement.mode = static_cast<std::uint8_t>(value);
        if (property == 13 && revealPlayerOnMovement) EmitHidden(false, false);
        if (property == 202) playerCapsule.collision = static_cast<std::uint8_t>(value);
        if (property == 105) playerMesh.collision = static_cast<std::uint8_t>(value);
        if (retirePlayerOnApply)
        {
            gloss::gameplay::Context end{}; end.rcx = component;
            gloss::gameplay::transformation::native::RetirePlayerIsolation(end, nullptr);
        }
        if (propFailureStage == 2) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }
    void DeferredRequest(int property, int value, std::uint32_t handle, std::uint32_t notify, std::uint8_t deferred)
    {
        namespace native = gloss::gameplay::transformation::native;
        CHECK(gcUnsafe && notify == 0 && deferred == 1);
        struct Frame { gloss::gameplay::Context context{}; std::array<std::uintptr_t, 8> stack{}; } frame;
        frame.context.rcx = reinterpret_cast<std::uintptr_t>(propComponent.data());
        frame.context.rdx = property; frame.context.r9 = handle;
        frame.stack[5] = notify; frame.stack[6] = deferred;
        native::CaptureProperty(frame.context, nullptr);
        if (frame.context.resume == native::returnStub) return;
        ++propSetterCalls;
        if (!handle)
        {
            handle = static_cast<std::uint32_t>(++propData[0x48 / 8]);
            propRequests.push_back({handle, property, value});
        }
        else
            for (auto &request : propRequests)
                if (request.handle == handle) request.value = value;
        if (propFailureStage == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }
}
namespace gloss
{
    void Log(std::string_view message) { diagnosticLogs.emplace_back(message); }
}
namespace gloss::gameplay
{
    bool ReadMemory(std::uintptr_t address, void *data, std::size_t size) noexcept
    {
        if (address == unreadableAddress) return false;
        SIZE_T copied{};
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), data, size, &copied) && copied == size;
    }
    bool WriteMemory(std::uintptr_t address, const void *data, std::size_t size) noexcept
    {
        SIZE_T copied{};
        return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), data, size, &copied) && copied == size;
    }
    Pattern Pattern::Parse(const char *text) { return simulateHookInstall && text && *text ? Pattern{{0x55}} : Pattern{}; }
    std::vector<std::uintptr_t> FindPattern(const Pattern &, std::uintptr_t address, std::size_t, std::size_t)
    {
        ++fingerprintChecks;
        return simulateHookInstall && address != rejectedFingerprint ? std::vector<std::uintptr_t>{address} : std::vector<std::uintptr_t>{};
    }
    std::vector<std::uintptr_t> FindExecutablePattern(HMODULE, const char *text)
    {
        if (!simulateRuntimeResolver)
            return {};
        const auto &patterns = transformation::native::managedRuntimeFingerprints;
        const std::array addresses{reinterpret_cast<std::uintptr_t>(CurrentMonoThread), reinterpret_cast<std::uintptr_t>(EnterMonoUnsafe), reinterpret_cast<std::uintptr_t>(ExitMonoUnsafe)};
        for (std::size_t i = 0; i != patterns.size(); ++i)
            if (!std::strcmp(patterns[i], text))
            {
                if (static_cast<int>(i) == rejectedRuntimePattern)
                    return {};
                if (static_cast<int>(i) == duplicateRuntimePattern)
                    return {addresses[i], addresses[i]};
                if (static_cast<int>(i) == outsideRuntimePattern)
                    return {0x10000};
                return {addresses[i]};
            }
        CHECK(false);
        return {};
    }
    std::unique_ptr<MidHook> MidHook::Create(void *address, Callback callback, void *user, std::string &error)
    {
        if (!simulateHookInstall)
            return {};
        if (!hookCreateCalls)
            checksAtFirstHook = fingerprintChecks;
        const int call = hookCreateCalls++;
        if (call == failHookAt)
        {
            error = "simulated hook failure";
            return {};
        }
        // This is an ownership-only mock; no instructions are patched.
        auto hook = std::unique_ptr<MidHook>(new MidHook());
        hook->address_ = address;
        hook->callback = callback;
        hook->user = user;
        return hook;
    }
    MidHook::~MidHook() { ++hookDestroyed; }
}
namespace gloss::gameplay::aot
{
    bool Ready() { return false; }
    std::uintptr_t Method(std::uint32_t rid)
    {
        if (rid == 0x11c09 && sprintMethodAddress) return sprintMethodAddress;
        return simulateHookInstall ? 0x10000000ull + rid * 16ull : 0;
    }
    std::uintptr_t RuntimeMethod(std::uint32_t) { return 0; }
    std::uintptr_t EngineMethod(std::uint32_t rid) { return simulateHookInstall ? 0x20000000ull + rid * 16ull : 0; }
}
namespace gloss::gameplay::engine
{
    bool IsGameThread() noexcept { return GetCurrentThreadId() == gameThreadId; }
    bool WeakReady() { return true; }
    bool IsA(std::uintptr_t object, const wchar_t *path) noexcept
    {
        const auto found = Lookup(object);
        if (std::wstring_view(path) == L"/Script/b1-Managed.BGUWeaponBase") return found && found->isWeapon;
        if (std::wstring_view(path) == L"/Script/b1-Managed.BUAnimInstanceBase") return found && found->animTop;
        if (std::wstring_view(path) == L"/Script/b1-Managed.BUAnimHumanoidCS_Move") return found && found->animMove;
        if (std::wstring_view(path) == L"/Script/b1-Managed.BUAnimHumanoidCS_MotionMatching") return found && found->animMM;
        return found != nullptr;
    }
    bool IsPawn(std::uintptr_t object) noexcept { return Lookup(object) != nullptr; }
    bool IsActor(std::uintptr_t object) noexcept { return Lookup(object) != nullptr; }
    std::uintptr_t ResolveWeak(WeakObject weak)
    {
        const auto it = bindings.find(weak.index);
        return it != bindings.end() && !it->second.pendingKill && it->second.serial == weak.serial ? it->second.object->Address() : 0;
    }
    WeakObject MakeWeak(std::uintptr_t object)
    {
        for (const auto &[index, binding] : bindings)
            if (!binding.pendingKill && binding.object->Address() == object)
                return {index, binding.serial};
        return {};
    }
    bool WeakLifetimeCurrent(WeakObject weak)
    {
        const auto it = bindings.find(weak.index);
        return it != bindings.end() && it->second.serial == weak.serial;
    }
    bool Property(std::uintptr_t object, Arg &arg, bool write)
    {
        CHECK(!write);
        if (mmReflection && object == proxy.Address() && std::string_view(arg.name) == "Mesh")
        {
            if (mmReflectionFailure == 1) return false;
            arg = Arg::Object("Mesh", animationMesh.Address()); return true;
        }
        if (object == player.Address())
        {
            const std::string_view name(arg.name);
            if (name == "bHidden")
            {
                if (++playerStateReads == playerStateFailureRead) return false;
                arg = Arg::Boolean("bHidden", player.hidden); return true;
            }
            if (name == "CharacterMovement") { arg = Arg::Object("CharacterMovement", playerMovement.Address()); return true; }
            if (name == "CapsuleComponent") { arg = Arg::Object("CapsuleComponent", playerCapsule.Address()); return true; }
            if (name == "Mesh") { arg = Arg::Object("Mesh", playerMesh.Address()); return true; }
        }
        if (object == playerMovement.Address() && std::string_view(arg.name) == "MovementMode")
        { arg = Arg::Byte("MovementMode", playerMovement.mode); return true; }
        if (auto *weapon = Lookup(object); weapon && weapon->isWeapon && !std::strcmp(arg.name, "bHidden"))
        {
            if (++weaponHiddenReads == weaponHiddenFailureRead) return false;
            arg = Arg::Boolean("bHidden", !weapon->weaponActive);
            return true;
        }
        if (auto *actor = Lookup(object); actor && !std::strcmp(arg.name, "Owner"))
        {
            if (!actor->ownerReadable)
                return false;
            arg = Arg::Object("Owner", actor->owner ? actor->owner->Address() : 0);
            return true;
        }
        if (!std::strcmp(arg.name, "RPCEventCollection"))
            ++rpcReflectionAttempts;
        return false; // The private managed auto-property is not reflected.
    }
    bool PropertyPath(std::uintptr_t object, std::span<const std::string_view> path, Arg &arg, bool write)
    {
        CHECK(!write && object == player.Address() && path.size() == 1 && path[0] == "UnitCDesc");
        CHECK(arg.type == Type::StructArray && arg.structPath == L"/Script/b1-Managed.UnitWeapon" && arg.fields.size() == 2);
        CHECK(std::string_view(arg.name) == "Weapons");
        if (!weaponTableReadable) return false;
        arg.records = weaponRecords;
        return true;
    }
    CallResult Call(std::wstring_view function, std::span<Arg> args, std::uintptr_t instance)
    {
        if (function == L"/Script/Engine.SkeletalMeshComponent:GetAnimInstance")
        {
            CHECK(mmReflection && instance == animationMesh.Address() && args.size() == 1 && args[0].output);
            if (mmReflectionFailure == 2) return {false, "unreadable top"};
            args[0] = Arg::Object("ReturnValue", child.Address()); return {true, {}};
        }
        if (function == L"/Script/Engine.AnimInstance:GetLinkedAnimGraphInstanceByTag")
        {
            CHECK(mmReflection && args.size() == 2 && std::string_view(args[0].name) == "InTag" && args[0].type == Type::Name && args[1].output);
            ++mmGraphCalls;
            if (instance == child.Address())
            {
                CHECK(args[0].text == L"Move");
                if (mmReflectionFailure == 3) return {false, "unreadable move"};
                args[1] = Arg::Object("ReturnValue", mmMovePresent ? grandchild.Address() : 0);
            }
            else
            {
                CHECK(instance == grandchild.Address() && args[0].text == L"MotionMatching");
                if (mmReflectionFailure == 4) return {false, "unreadable mm"};
                args[1] = Arg::Object("ReturnValue", mmNodePresent ? (mmGraphChanged ? other.Address() : mmNode.Address()) : 0);
            }
            return {true, {}};
        }
        if (function == L"/Script/Engine.AnimInstance:GetOwningActor")
        {
            ++animationOwnerCalls;
            const auto object = Lookup(instance);
            CHECK(object && args.size() == 1);
            args[0] = Arg::Object("ReturnValue", object && object->owner ? object->owner->Address() : 0);
            return {true, {}};
        }
        if (function.ends_with(L":GetCollisionEnabled"))
        {
            CHECK(args.size() == 1 && args[0].type == Type::Byte && args[0].output);
            CHECK(instance == playerCapsule.Address() || instance == playerMesh.Address());
            args[0] = Arg::Byte("ReturnValue", Lookup(instance)->collision, true);
            return {true, {}};
        }
        CHECK(!instance && args[0].Get<std::uintptr_t>() == proxy.Address());
        if (function.ends_with(L":BGUGetWeaponNum"))
        {
            CHECK(args.size() == 2 && std::string_view(args[0].name) == "Unit");
            if (!weaponCountReadable) return {false, "unreadable count"};
            args[1] = Arg::Int("ReturnValue", weaponExtent);
            return {true, {}};
        }
        CHECK(function.ends_with(L":BGUGetWeaponByIndex") && args.size() == 3);
        CHECK(std::string_view(args[0].name) == "Owner" && std::string_view(args[1].name) == "index" && args[1].type == Type::Int);
        ++weaponLookupCalls;
        if (!weaponLookupReadable) return {false, "unreadable slot"};
        const auto found = weaponByIndex.find(args[1].Get<int>());
        args[2] = Arg::Object("ReturnValue", found == weaponByIndex.end() ? 0 : found->second->Address());
        return {true, {}};
    }
}
namespace
{
    namespace native = gloss::gameplay::transformation::native;
    using gloss::gameplay::Context;
    using gloss::gameplay::transformation::Action;
    void Reset()
    {
        native::Hooks().clear();
        simulateHookInstall = false;
        rejectedFingerprint = 0;
        failHookAt = -1;
        hookCreateCalls = hookDestroyed = fingerprintChecks = checksAtFirstHook = 0;
        proxy = Object{}; player = Object{}; other = Object{}; child = Object{}; grandchild = Object{};
        playerMovement = Object{}; playerCapsule = Object{}; playerMesh = Object{};
        animationMesh = Object{}; mmNode = Object{};
        // Rebind self-references after value assignment from the temporary.
        for (auto *object : {&proxy, &player, &other, &child, &grandchild, &playerMovement, &playerCapsule, &playerMesh, &animationMesh, &mmNode})
        {
            object->managed[0x58 / 8] = object->Address();
            object->reference[0x10 / 8] = object->Managed();
            object->reference[0x20 / 8] = object->Address();
        }
        bindings = {{1, {7, &proxy}}, {2, {9, &player}}, {3, {11, &other}}, {4, {13, &child}}, {5, {15, &grandchild}},
                    {6, {17, &playerMovement}}, {7, {19, &playerCapsule}}, {8, {21, &playerMesh}}};
        native::isolationClaim = {}; native::propCapture = nullptr; native::propertyPhase = native::PropertyPhase::External;
        native::ClearProxy();
        native::ready.store(true);
        native::animationProbeReady.store(false);
        native::animationProbeMethods = {};
        native::motionMatchingSelectorReady.store(false);
        native::motionMatchingMethods = {};
        native::facingEventReady.store(true);
        native::sprintInputReady.store(true);
        sprintMethodAddress = 0;
        inputSpeedMode = 2;
        sprintInputFailureStage = 0;
        animationMmData = {}; mmKeys = {}; mmQueriedStates.clear();
        mmReflection = mmGraphChanged = false; mmMovePresent = mmNodePresent = true;
        mmReflectionFailure = mmQueryFailure = mmChangeStage = mmGraphCalls = mmQueryEffectAt = 0;
        animationSetting = animationCommon = animationMatching = animationEcs = {};
        animationGetterCalls = animationFailureStage = animationChangeStage = animationOwnerCalls = animationModeCalls = 0;
        animationRetire = animationDetach = animationNewSession = false;
        native::returnStub = 0x12345678;
        CHECK(gcEntries.empty());
        gcUnsafe = false;
        monoAttached = true;
        simulateRuntimeResolver = nestedClear = false;
        gcEnterCalls = gcExitCalls = gcCurrentCalls = gcFailureStage = 0;
        rejectedRuntimePattern = duplicateRuntimePattern = outsideRuntimePattern = -1;
        native::managedRuntime = {reinterpret_cast<std::uintptr_t>(CurrentMonoThread), reinterpret_cast<std::uintptr_t>(EnterMonoUnsafe), reinterpret_cast<std::uintptr_t>(ExitMonoUnsafe)};
        native::methods.findRef = reinterpret_cast<std::uintptr_t>(FindRef);
        native::methods.setTamerType = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t address, int value) { CHECK(gcUnsafe); ++setterCalls; Lookup(address)->tamer = value; });
        native::methods.initialized = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t) { CHECK(gcUnsafe); return 1; });
        native::methods.inputOwner = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t) { CHECK(gcUnsafe); return inputManaged; });
        native::methods.getTeam = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t address) { CHECK(gcUnsafe); return Lookup(address)->team; });
        native::methods.setTeam = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t address, int value) { CHECK(gcUnsafe); Lookup(address)->team = value; });
        native::methods.receiveBattle = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t, bool) { CHECK(gcUnsafe); return battleAllowed ? 1 : 0; });
        native::methods.receiveAction = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t, int) { CHECK(gcUnsafe); return battleAllowed ? 1 : 0; });
        native::methods.actionDesc = reinterpret_cast<std::uintptr_t>(+[](int id) { CHECK(gcUnsafe); return id == 42 ? reinterpret_cast<std::uintptr_t>(descriptor.data()) : 0; });
        native::methods.firstParam = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value, int index) {
            CHECK(gcUnsafe && value == reinterpret_cast<std::uintptr_t>(parameters.data()) && (index == 0 || index == 1));
            if (index == 1)
            {
                if (sprintInputFailureStage == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
                if (sprintInputFailureStage == 2) native::ResetSprintInput();
                if (sprintInputFailureStage == 3) CHECK(native::SetProxy({1, 7}));
                if (sprintInputFailureStage == 4) ++bindings[2].serial;
                if (sprintInputFailureStage == 5) CHECK(native::SetInputOwner({2, 9}));
            }
            return index ? inputSpeedMode : inputParameter;
        });
        native::methods.getAxis = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t) { CHECK(gcUnsafe); return inputAxis; });
        native::methods.getEventCollection = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) {
            ++getEventCollectionCalls;
            CHECK(gcUnsafe && (value == proxy.Managed() || value == player.Managed()));
            if (clearFailureStage == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            return eventCollectionManaged;
        });
        native::methods.forceFacingEvent = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) {
            CHECK(gcUnsafe && value == other.Managed());
            if (facingFailureStage == 2) ++bindings[1].serial;
            if (facingFailureStage == 3) CHECK(native::SetProxy({1, 7}));
            return reinterpret_cast<std::uintptr_t>(facingCallbackObject.data());
        });
        native::methods.invokeFacing = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value, const gloss::gameplay::engine::Rotator *rotation) {
            CHECK(gcUnsafe && value == reinterpret_cast<std::uintptr_t>(facingCallbackObject.data()) && rotation);
            ++facingCalls;
            facingRotation = *rotation;
            if (facingFailureStage == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (facingFailureStage == 4) CHECK(native::SetProxy({1, 7}));
        });
        native::methods.clearTargetEvent = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) {
            ++getClearEventCalls;
            CHECK(gcUnsafe && value == other.Managed());
            if (clearFailureStage == 2) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            return clearCallbackManaged;
        });
        native::methods.invokeVoid = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) {
            ++clearTargetCalls;
            CHECK(gcUnsafe && value == reinterpret_cast<std::uintptr_t>(clearCallbackObject.data()));
            if (nestedClear)
            {
                CHECK(native::Initialized(proxy.Address()));
                CHECK(gcUnsafe && gcEntries.size() == 1); // Nested cookie=0 preserves the outer operation.
            }
            if (clearFailureStage == 3) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        });
        native::methods.makeTargetInfo = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t target, std::uint8_t source, std::uint8_t way) {
            CHECK(gcUnsafe && target == other.Managed() && source == 0 && way == 0);
            return other.Managed();
        });
        native::methods.setTargetInfo = reinterpret_cast<std::uintptr_t>(+[](bool online, std::uintptr_t owner, std::uintptr_t info) {
            CHECK(gcUnsafe && !online && owner == proxy.Managed() && info == other.Managed()); ++targetCalls;
        });
        setterCalls = 0;
        targetCalls = clearTargetCalls = 0;
        findRefCalls = getEventCollectionCalls = getClearEventCalls = rpcReflectionAttempts = clearFailureStage = 0;
        clearCallbackObject = {};
        clearCallbackObject[0] = 1; // Plain managed GSDel_Void: no UObject Address.
        eventCollectionManaged = other.Managed();
        clearCallbackManaged = reinterpret_cast<std::uintptr_t>(clearCallbackObject.data());
        facingCallbackObject = {};
        facingCallbackObject[0] = 1;
        facingSubscriberObject = {};
        facingSubscriberObject[0] = 1;
        facingCallbackObject[0x10 / 8] = reinterpret_cast<std::uintptr_t>(facingSubscriberObject.data());
        facingCalls = facingFailureStage = 0;
        facingRotation = {};
        weaponRecords.clear();
        weaponByIndex.clear();
        weaponExtent = weaponFailureStage = weaponMasterCalls = weaponActiveCalls = weaponLookupCalls = 0;
        weaponTableReadable = weaponCountReadable = weaponLookupReadable = true;
        weaponHiddenReads = weaponHiddenFailureRead = 0;
        weaponActiveNoOp = false;
        weaponReadyCalls = weaponReadyFailure = weaponReadyFlipCall = 0;
        unreadableAddress = 0;
        weaponRetireOnReady = weaponDetachOnReady = weaponRetireOnActive = false;
        diagnosticLogs.clear();
        masterCallbackObject = activeCallbackObject = {};
        masterCallbackObject[0] = activeCallbackObject[0] = 1;
        masterSubscriberObject = activeSubscriberObject = {};
        masterSubscriberObject[0] = activeSubscriberObject[0] = 1;
        masterCallbackObject[0x10 / 8] = reinterpret_cast<std::uintptr_t>(masterSubscriberObject.data());
        activeCallbackObject[0x10 / 8] = reinterpret_cast<std::uintptr_t>(activeSubscriberObject.data());
        weaponCollection = other.Managed();
        weaponEventOwner = nullptr;
        masterCallback = reinterpret_cast<std::uintptr_t>(masterCallbackObject.data());
        activeCallback = reinterpret_cast<std::uintptr_t>(activeCallbackObject.data());
        native::methods.weaponInitialized = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t weapon) -> std::uint8_t {
            CHECK(gcUnsafe && Lookup(weapon) && Lookup(weapon)->isWeapon && Lookup(weapon)->Managed() == weapon);
            ++weaponReadyCalls;
            if (weaponReadyFailure) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (weaponRetireOnReady) ++bindings[4].serial;
            if (weaponDetachOnReady) child.owner = &player;
            if (weaponReadyCalls == weaponReadyFlipCall) return 0;
            return Lookup(weapon)->weaponInitialized;
        });
        native::methods.weaponEvents = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t weapon) {
            CHECK(gcUnsafe && Lookup(weapon) && Lookup(weapon)->isWeapon && Lookup(weapon)->Managed() == weapon);
            weaponEventOwner = Lookup(weapon);
            if (weaponFailureStage == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            return weaponCollection;
        });
        native::methods.weaponMasterEvent = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t collection) {
            CHECK(gcUnsafe && collection == other.Managed());
            if (weaponFailureStage == 2) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            return masterCallback;
        });
        native::methods.weaponActiveEvent = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t collection) {
            CHECK(gcUnsafe && collection == other.Managed());
            if (weaponFailureStage == 3) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            return activeCallback;
        });
        native::methods.invokeActor = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t callback, std::uintptr_t master) {
            CHECK(gcUnsafe && callback == reinterpret_cast<std::uintptr_t>(masterCallbackObject.data()) && master == proxy.Managed());
            ++weaponMasterCalls;
            CHECK(weaponEventOwner);
            weaponEventOwner->weaponMaster = &proxy;
            if (weaponFailureStage == 4) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        });
        native::methods.invokeBool = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t callback, std::uint8_t active) {
            CHECK(gcUnsafe && callback == reinterpret_cast<std::uintptr_t>(activeCallbackObject.data()) && active <= 1 && weaponEventOwner && weaponEventOwner->weaponMaster == &proxy);
            ++weaponActiveCalls;
            if (!weaponActiveNoOp) weaponEventOwner->weaponActive = active != 0;
            if (weaponRetireOnActive) ++bindings[4].serial;
            if (weaponFailureStage == 5) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        });
        propertyCallbacks = {}; propComponent = {}; propData = {}; propRequests.clear();
        for (auto &callback : propertyCallbacks) callback[0] = 1;
        propComponent[0] = propData[0] = 1;
        propComponent[0x10 / 8] = player.Managed();
        propComponent[0x98 / 8] = reinterpret_cast<std::uintptr_t>(propData.data());
        propSetterCalls = propResetCalls = propApplyCalls = propFailureStage = duplicateProperty = 0;
        playerStateReads = playerStateFailureRead = 0; appliedProperties.clear();
        retirePlayerOnApply = revealPlayerOnMovement = false;
        native::methods.hiddenPropertyReturn = 0x2725dee;
        native::methods.boolPropertyEvent = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) { CHECK(gcUnsafe && value == other.Managed()); return reinterpret_cast<std::uintptr_t>(propertyCallbacks[0].data()); });
        native::methods.enumPropertyEvent = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) { CHECK(gcUnsafe && value == other.Managed()); return reinterpret_cast<std::uintptr_t>(propertyCallbacks[1].data()); });
        native::methods.resetPropertyEvent = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) { CHECK(gcUnsafe && value == other.Managed()); return reinterpret_cast<std::uintptr_t>(propertyCallbacks[2].data()); });
        native::methods.propHandle = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t data) { CHECK(gcUnsafe && data == reinterpret_cast<std::uintptr_t>(propData.data())); return static_cast<std::uint32_t>(propData[0x48 / 8]); });
        native::methods.calculateProperty = reinterpret_cast<std::uintptr_t>(ApplyProperty);
        native::methods.invokeBoolProperty = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t callback, int property, std::uint8_t value, std::uint32_t handle, std::uint32_t notify, std::uint8_t deferred) {
            CHECK(callback == reinterpret_cast<std::uintptr_t>(propertyCallbacks[0].data()) && property == 302 && value == 1);
            DeferredRequest(property, value, handle, notify, deferred);
            if (duplicateProperty == property) DeferredRequest(property, value, handle, notify, deferred);
        });
        native::methods.invokeEnumProperty = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t callback, int property, int value, std::uint32_t handle, std::uint32_t notify, std::uint8_t deferred, std::uintptr_t resource) {
            CHECK(callback == reinterpret_cast<std::uintptr_t>(propertyCallbacks[1].data()) && property != 302 && value == 0 && resource == 0);
            DeferredRequest(property, value, handle, notify, deferred);
            if (duplicateProperty == property) DeferredRequest(property, value, handle, notify, deferred);
        });
        native::methods.invokeResetProperty = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t callback, std::uint32_t handle, std::uint32_t notify) {
            CHECK(gcUnsafe && callback == reinterpret_cast<std::uintptr_t>(propertyCallbacks[2].data()) && handle && !notify);
            for (int subscriber = 0; subscriber != (duplicateProperty ? 2 : 1); ++subscriber)
            {
                ++propResetCalls;
                const auto found = std::find_if(propRequests.begin(), propRequests.end(), [&](const auto &request) { return request.handle == handle; });
                if (found != propRequests.end())
                {
                    const int property = found->property;
                    propRequests.erase(found);
                    ApplyProperty(reinterpret_cast<std::uintptr_t>(propComponent.data()), property);
                }
                if (propFailureStage == 3) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            }
        });
        descriptor = {};
        parameters = {};
        descriptor[0x20 / 8] = reinterpret_cast<std::uintptr_t>(parameters.data());
        parameters[0x18 / 8] = 2;
        inputParameter = 0;
        inputAxis = 0;
        battleAllowed = true;
        inputManaged = player.Managed();
        CHECK(native::SetProxy({1, 7}));
    }
    Context ComponentContext(Object &owner, std::array<std::uintptr_t, 4> &component);
    std::uint32_t ExternalRequest(int property, int value)
    {
        const auto handle = static_cast<std::uint32_t>(++propData[0x48 / 8]);
        propRequests.push_back({handle, property, value});
        CHECK(native::ManagedOperation([&] { ApplyProperty(reinterpret_cast<std::uintptr_t>(propComponent.data()), property); return true; }));
        return handle;
    }
    void TestPlayerLeasesRemoveOnlyOwnedHandlesIncludingSameValueStoryRequests()
    {
        Reset(); CHECK(native::SetInputOwner({2, 9}));
        native::PlayerIsolation lease;
        CHECK(native::AcquirePlayerIsolation(player.Address(), lease) && lease.token);
        CHECK(propSetterCalls == 4 && propRequests.size() == 4);
        CHECK(player.hidden && !playerMovement.mode && !playerCapsule.collision && !playerMesh.collision);
        CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
        const auto externalHidden = ExternalRequest(302, 1);
        const auto externalMovement = ExternalRequest(13, 0);
        const auto externalCapsule = ExternalRequest(202, 0);
        const auto externalMesh = ExternalRequest(105, 0);
        const int applyBefore = propApplyCalls;
        CHECK(native::RefreshPlayerIsolation(lease) && propApplyCalls == applyBefore);
        auto copied = lease;
        CHECK(!native::ReleasePlayerIsolation(copied)); // Token copies cannot withdraw the owner's requests.
        CHECK(native::ReleasePlayerIsolation(lease) && !lease.token);
        CHECK(propRequests.size() == 4 && player.hidden && !playerMovement.mode && !playerCapsule.collision && !playerMesh.collision);
        for (const auto handle : {externalHidden, externalMovement, externalCapsule, externalMesh})
            CHECK(std::any_of(propRequests.begin(), propRequests.end(), [&](auto request) { return request.handle == handle; }));
        const int resetBefore = propResetCalls;
        CHECK(native::ReleasePlayerIsolation(lease) && propResetCalls == resetBefore);
    }
    void TestPlayerHiddenShadowPreservesDirectSameValueWritesAndExternalOrdering()
    {
        Reset(); CHECK(native::SetInputOwner({2, 9}));
        native::PlayerIsolation lease;
        CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
        EmitHidden(true, false); // Movie direct write equals our currently hidden value.
        CHECK(native::ReleasePlayerIsolation(lease));
        CHECK(player.hidden && playerMovement.mode == 3 && playerCapsule.collision == 3 && playerMesh.collision == 3);
        Reset(); CHECK(native::SetInputOwner({2, 9})); lease = {};
        CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
        EmitHidden(false, false);
        CHECK(!player.hidden && native::RefreshPlayerIsolation(lease) && player.hidden);
        CHECK(native::ReleasePlayerIsolation(lease) && !player.hidden); // Reassertion must not lose external false.
        Reset(); CHECK(native::SetInputOwner({2, 9})); lease = {};
        ExternalRequest(302, 0); // Existing game request below our later request.
        CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
        EmitHidden(true, false);
        CHECK(native::ManagedOperation([&] { ApplyProperty(reinterpret_cast<std::uintptr_t>(propComponent.data()), 302); return true; }));
        CHECK(!native::isolationClaim.shadowValid); // A later external property calculation supersedes direct shadow.
        CHECK(native::ReleasePlayerIsolation(lease) && !player.hidden && propRequests.size() == 1);
    }
    void TestPlayerLeaseFailuresRetryRecalculationAndRejectInvalidLifetimes()
    {
        Reset(); CHECK(native::SetInputOwner({2, 9}));
        native::PlayerIsolation lease;
        propFailureStage = 1; // Deferred request inserted, but setter raises before applying anything.
        CHECK(!native::AcquirePlayerIsolation(player.Address(), lease));
        CHECK(propRequests.size() == 1 && !player.hidden && playerMovement.mode == 3);
        propFailureStage = 0;
        CHECK(native::ReleasePlayerIsolation(lease) && propRequests.empty());
        Reset(); CHECK(native::SetInputOwner({2, 9})); lease = {};
        CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
        propFailureStage = 2; // Reset removes requests; applying remaining state throws.
        CHECK(!native::ReleasePlayerIsolation(lease) && lease.token && propRequests.empty());
        player.hidden = true; playerMovement.mode = 0; playerCapsule.collision = playerMesh.collision = 0;
        propFailureStage = 0;
        CHECK(native::ReleasePlayerIsolation(lease)); // Missing handles still require explicit recalculation.
        CHECK(!player.hidden && playerMovement.mode == 3 && playerCapsule.collision == 3 && playerMesh.collision == 3);
        Reset(); CHECK(native::SetInputOwner({2, 9})); lease = {};
        propData[0x48 / 8] = (std::numeric_limits<std::uint32_t>::max)();
        CHECK(!native::AcquirePlayerIsolation(player.Address(), lease) && propRequests.empty());
        CHECK(native::ReleasePlayerIsolation(lease));
        Reset(); CHECK(native::SetInputOwner({2, 9})); lease = {};
        CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
        const int resets = propResetCalls;
        ++bindings[2].serial;
        CHECK(native::ReleasePlayerIsolation(lease) && propResetCalls == resets); // Same-address player is foreign.
        Reset(); CHECK(native::SetInputOwner({2, 9})); lease = {};
        CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
        Context end{}; end.rcx = reinterpret_cast<std::uintptr_t>(propComponent.data());
        native::RetirePlayerIsolation(end, nullptr);
        CHECK(!native::RefreshPlayerIsolation(lease));
        CHECK(native::ReleasePlayerIsolation(lease) && !propResetCalls); // EndPlay will reset old data; never touch rebuilt component.
        Reset(); lease = {};
        CHECK(!native::AcquirePlayerIsolation(player.Address(), lease) && !lease.token); // Input identity is required.
    }
    void TestPlayerLeaseRefreshHidesLastAfterMovementCallbacks()
    {
        for (const bool initiallyHidden : {false, true})
        {
            Reset(); CHECK(native::SetInputOwner({2, 9}));
            native::PlayerIsolation lease;
            CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
            const auto handles = native::isolationClaim.handles;
            const auto counter = propData[0x48 / 8];
            player.hidden = initiallyHidden;
            playerMovement.mode = 3;
            revealPlayerOnMovement = true;
            appliedProperties.clear();
            CHECK(native::RefreshPlayerIsolation(lease));
            CHECK(appliedProperties == std::vector<int>({13, 302}));
            CHECK(player.hidden && !playerMovement.mode && !playerCapsule.collision && !playerMesh.collision);
            CHECK(native::isolationClaim.handles == handles && propData[0x48 / 8] == counter && propRequests.size() == 4);
            CHECK(diagnosticLogs.empty());
            // Releasing input before the lease is valid for cinematic suspend.
            CHECK(native::SetInputOwner({}));
            CHECK(native::ReleasePlayerIsolation(lease) && !lease.token && !player.hidden && propRequests.empty());
            CHECK(!gcUnsafe && gcEntries.empty());
        }
    }
    void TestPlayerLeaseRefreshPreservesExternalPriorityAndDeduplicatesDiagnostics()
    {
        for (const int property : {302, 13, 202, 105})
        {
            Reset(); CHECK(native::SetInputOwner({2, 9}));
            native::PlayerIsolation lease;
            CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
            const auto story = ExternalRequest(property, property == 302 ? 0 : 3);
            const auto handles = native::isolationClaim.handles;
            const auto counter = propData[0x48 / 8];
            CHECK(!native::RefreshPlayerIsolation(lease));
            CHECK(diagnosticLogs.size() == 1 && diagnosticLogs[0].find("reason=post-mismatch") != std::string::npos);
            CHECK(diagnosticLogs[0].find("handles=[") != std::string::npos && diagnosticLogs[0].find("weak=2:9") != std::string::npos);
            CHECK(!native::RefreshPlayerIsolation(lease) && diagnosticLogs.size() == 1);
            CHECK(propRequests.size() == 5 && native::isolationClaim.handles == handles && propData[0x48 / 8] == counter);
            CHECK(native::ReleasePlayerIsolation(lease));
            CHECK(propRequests.size() == 1 && propRequests[0].handle == story);
            CHECK(!player.hidden && playerMovement.mode == 3 && playerCapsule.collision == 3 && playerMesh.collision == 3);
        }
    }
    void TestPlayerLeaseRefreshDiagnosticsIdentifyFailuresWithoutRecoveryWrites()
    {
        const auto logged = [](std::string_view reason) {
            return std::any_of(diagnosticLogs.begin(), diagnosticLogs.end(), [&](const auto &line) { return line.find(reason) != std::string::npos; });
        };
        for (const std::string_view reason : {"not-ready", "not-game-thread", "lease-missing", "retired", "uncertain", "generation-mismatch", "actor-invalid", "state-read", "event", "hidden-handle-missing", "deferred", "calc-invoke", "calc-lifetime-after", "post-state-read", "managed-transition"})
        {
            Reset(); CHECK(native::SetInputOwner({2, 9}));
            native::PlayerIsolation lease;
            CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
            player.hidden = false;
            const auto handles = native::isolationClaim.handles;
            const auto counter = propData[0x48 / 8];
            if (reason == "not-ready") native::ready.store(false);
            if (reason == "lease-missing") lease.token = 0;
            if (reason == "retired") native::isolationClaim.retired = true;
            if (reason == "uncertain") native::isolationClaim.uncertain = true;
            if (reason == "generation-mismatch") ++native::isolationClaim.generation;
            if (reason == "actor-invalid") ++bindings[2].serial;
            if (reason == "state-read") playerStateFailureRead = playerStateReads + 1;
            if (reason == "event") clearFailureStage = 1;
            if (reason == "hidden-handle-missing") native::isolationClaim.handles[0] = 0;
            if (reason == "deferred") duplicateProperty = 302;
            if (reason == "calc-invoke") propFailureStage = 2;
            if (reason == "calc-lifetime-after") retirePlayerOnApply = true;
            if (reason == "post-state-read") playerStateFailureRead = playerStateReads + 2;
            if (reason == "managed-transition") monoAttached = false;
            if (reason == "not-game-thread")
            {
                std::thread foreign([&] { CHECK(!native::RefreshPlayerIsolation(lease)); });
                foreign.join();
            }
            else CHECK(!native::RefreshPlayerIsolation(lease));
            CHECK(logged(std::string("reason=") + std::string(reason)));
            CHECK(propRequests.size() == 4 && propData[0x48 / 8] == counter && !propResetCalls);
            if (reason != "hidden-handle-missing") CHECK(native::isolationClaim.handles == handles);
            CHECK(!gcUnsafe && gcEntries.empty());
        }
    }
    void TestPlayerLeaseStopsAtSynchronousEndPlay()
    {
        Reset(); CHECK(native::SetInputOwner({2, 9}));
        native::PlayerIsolation lease;
        retirePlayerOnApply = true;
        CHECK(!native::AcquirePlayerIsolation(player.Address(), lease));
        CHECK(propApplyCalls == 1 && !playerMovement.mode && playerCapsule.collision == 3 && !player.hidden);
        CHECK(native::isolationClaim.retired && native::ReleasePlayerIsolation(lease) && !propResetCalls);
        Reset(); CHECK(native::SetInputOwner({2, 9})); lease = {};
        CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
        const int before = propApplyCalls;
        retirePlayerOnApply = true;
        CHECK(!native::ReleasePlayerIsolation(lease) && lease.token);
        CHECK(propResetCalls == 1 && propApplyCalls == before + 1 && native::isolationClaim.retired);
        CHECK(native::ReleasePlayerIsolation(lease) && !lease.token && propResetCalls == 1);
        CHECK(gcEntries.empty() && !gcUnsafe);
    }
    void TestDuplicatePropertySubscribersFailActivationAndRetainExactRecoverableHandles()
    {
        for (const int property : {302, 13})
        {
            Reset(); CHECK(native::SetInputOwner({2, 9}));
            native::PlayerIsolation lease;
            duplicateProperty = property;
            CHECK(!native::AcquirePlayerIsolation(player.Address(), lease) && lease.token);
            const std::size_t expected = property == 302 ? 1 : 2;
            CHECK(propRequests.size() == expected && propSetterCalls == expected && !native::isolationClaim.uncertain);
            CHECK(!player.hidden && playerMovement.mode == 3 && !propApplyCalls);
            const auto handles = native::isolationClaim.handles;
            for (std::size_t i = 0; i < expected; ++i)
                CHECK(handles[i] == propRequests[i].handle && handles[i] != 0);
            const auto story = ExternalRequest(302, 1); // A later game's ID must never become "ours".
            CHECK(native::ReleasePlayerIsolation(lease) && !lease.token);
            CHECK(propResetCalls == static_cast<int>(expected * 2)); // Repeated Reset is idempotent.
            CHECK(propRequests.size() == 1 && propRequests[0].handle == story && player.hidden);
            CHECK(gcEntries.empty() && !gcUnsafe);
        }
        Reset(); CHECK(native::SetInputOwner({2, 9}));
        native::PlayerIsolation lease;
        CHECK(native::AcquirePlayerIsolation(player.Address(), lease));
        duplicateProperty = 302;
        EmitHidden(false, false);
        CHECK(!native::RefreshPlayerIsolation(lease)); // Duplicate discovery cannot promote a live refresh to success.
        CHECK(propRequests.size() == 4 && !player.hidden);
        CHECK(native::ReleasePlayerIsolation(lease) && propRequests.empty() && !player.hidden);
    }
    void SeedAnimationProbe()
    {
        child.animTop = true;
        grandchild.animMove = true;
        child.owner = grandchild.owner = &proxy;
        animationSetting[0] = animationCommon[0] = animationMatching[0] = animationEcs[0] = 1;
        child.managed[0xb0 / 8] = reinterpret_cast<std::uintptr_t>(animationSetting.data());
        animationSetting[0x10 / 8] = reinterpret_cast<std::uintptr_t>(animationCommon.data());
        animationSetting[0x20 / 8] = reinterpret_cast<std::uintptr_t>(animationMatching.data());
        grandchild.managed[0xd0 / 8] = reinterpret_cast<std::uintptr_t>(animationEcs.data());
        reinterpret_cast<std::uint8_t *>(animationCommon.data())[0x41] = 10;
        reinterpret_cast<std::uint8_t *>(animationMatching.data())[0x51] = 11;
        reinterpret_cast<std::uint8_t *>(animationEcs.data())[0xb8] = 10;
        native::animationProbeReady.store(true);
        native::animationProbeMethods.setting = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) {
            CHECK(gcUnsafe && value == child.Managed());
            ++animationGetterCalls;
            if (animationFailureStage == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (animationChangeStage == 1 && animationGetterCalls == 2) return std::uintptr_t{};
            return child.managed[0xb0 / 8];
        });
        native::animationProbeMethods.configuredMode = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) -> std::uint8_t {
            CHECK(gcUnsafe && value == reinterpret_cast<std::uintptr_t>(animationCommon.data()));
            ++animationModeCalls;
            if (animationFailureStage == 2) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (animationChangeStage == 2) animationSetting[0x10 / 8] = 0;
            return reinterpret_cast<std::uint8_t *>(animationCommon.data())[0x41];
        });
        native::animationProbeMethods.spareMode = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) -> std::uint8_t {
            CHECK(gcUnsafe && value == reinterpret_cast<std::uintptr_t>(animationMatching.data()));
            ++animationModeCalls;
            if (animationFailureStage == 3) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (animationChangeStage == 3) animationSetting[0x20 / 8] = 0;
            return reinterpret_cast<std::uint8_t *>(animationMatching.data())[0x51];
        });
        native::animationProbeMethods.finalMode = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t value) -> std::uint8_t {
            CHECK(gcUnsafe && value == reinterpret_cast<std::uintptr_t>(animationEcs.data()));
            ++animationModeCalls;
            if (animationFailureStage == 4) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (animationChangeStage == 4) grandchild.managed[0xd0 / 8] = 0;
            if (animationRetire) ++bindings[5].serial;
            if (animationDetach) grandchild.owner = &player;
            if (animationNewSession) CHECK(native::SetProxy({1, 7}));
            return reinterpret_cast<std::uint8_t *>(animationEcs.data())[0xb8];
        });
    }
    void SeedMotionMatchingSelector()
    {
        SeedAnimationProbe();
        // Keep selector-only identities separate from the owner-chain fixture
        // (indices 10..26), and register them only in tests which use them.
        CHECK(bindings.emplace(animationMeshIndex, Binding{23, &animationMesh}).second);
        CHECK(bindings.emplace(mmNodeIndex, Binding{25, &mmNode}).second);
        mmReflection = true;
        mmNode.animMM = true; mmNode.owner = &proxy;
        animationMmData[0] = 1;
        mmNode.managed[0xd0 / 8] = reinterpret_cast<std::uintptr_t>(animationMmData.data());
        reinterpret_cast<std::uint8_t *>(animationCommon.data())[0x41] = 4;
        native::motionMatchingSelectorReady.store(true);
        native::motionMatchingMethods.setting = native::animationProbeMethods.setting;
        native::motionMatchingMethods.configuredMode = native::animationProbeMethods.configuredMode;
        native::motionMatchingMethods.validState = reinterpret_cast<std::uintptr_t>(+[](std::uintptr_t data, std::uintptr_t setting, std::uint32_t state) -> std::uint8_t {
            CHECK(gcUnsafe && data == reinterpret_cast<std::uintptr_t>(animationMmData.data()) && setting == reinterpret_cast<std::uintptr_t>(animationMatching.data()));
            CHECK(state >= 3 && state <= 8); // Full register value, never None or uninitialized high bits.
            mmQueriedStates.push_back(state);
            if (!mmQueryEffectAt || mmQueriedStates.size() == static_cast<std::size_t>(mmQueryEffectAt))
            {
                if (mmQueryFailure == 1) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
                if (mmQueryFailure == 2) return 2;
                if (mmChangeStage == 1) child.managed[0xb0 / 8] = 0;
                if (mmChangeStage == 2) animationSetting[0x10 / 8] = 0;
                if (mmChangeStage == 3) animationSetting[0x20 / 8] = 0;
                if (mmChangeStage == 4) mmNode.managed[0xd0 / 8] = 0;
                if (mmChangeStage == 5) reinterpret_cast<std::uint8_t *>(animationCommon.data())[0x41] = 10;
                if (mmChangeStage == 6) ++bindings.at(mmNodeIndex).serial;
                if (mmChangeStage == 7) mmNode.owner = &player;
                if (mmChangeStage == 8) CHECK(native::SetProxy({1, 7}));
                if (mmChangeStage == 9) mmGraphChanged = true;
                if (mmChangeStage == 10) ++bindings[4].serial;
                if (mmChangeStage == 11) ++bindings.at(animationMeshIndex).serial;
            }
            return mmKeys[state] || mmKeys[state >= 6 ? 1 : 2] ? 1 : 0;
        });
    }
    void TestMotionMatchingSelectorOptionalFingerprintIsolation()
    {
        std::vector<std::uint32_t> dependencies;
        for (std::size_t i = 0; i != 4; ++i) dependencies.push_back(native::animationProbeSpecs[i].rid);
        for (const auto &spec : native::motionMatchingSpecs) dependencies.push_back(spec.rid);
        for (const auto rid : dependencies)
        {
            Reset(); simulateHookInstall = true;
            rejectedFingerprint = gloss::gameplay::aot::Method(rid);
            CHECK(!native::VerifyMotionMatchingSelector());
            CHECK(native::Ready() && !native::motionMatchingSelectorReady.load());
            const auto selection = native::SelectMotionMatching(proxy.Address(), 1, false);
            CHECK(!selection.available && !selection.current && selection.state == -1 && !selection.valid);
            CHECK(!hookCreateCalls && !gcEnterCalls && !animationGetterCalls);
        }
        Reset(); simulateHookInstall = true;
        CHECK(native::VerifyMotionMatchingSelector() && native::Ready());
        CHECK(native::motionMatchingSelectorReady.load() && !native::animationProbeReady.load());
        CHECK(native::motionMatchingMethods.validState == gloss::gameplay::aot::Method(0xe783));
        CHECK(!hookCreateCalls && !gcEnterCalls);
    }
    void TestMotionMatchingSelectorUsesOnlyRoleSpecificValidSpeedStates()
    {
        for (int speed = 0; speed <= 2; ++speed)
            for (const bool locked : {false, true})
            {
                Reset(); SeedMotionMatchingSelector();
                mmKeys[locked ? 1 : 2] = true; // Generic Lock/Free fallback inside the game's query.
                const auto setting = animationSetting, common = animationCommon, matching = animationMatching, data = animationMmData;
                const auto selection = native::SelectMotionMatching(proxy.Address(), speed, locked);
                CHECK(selection.available && selection.current && selection.required && selection.valid && selection.usesLocked == locked);
                CHECK(selection.state == (locked ? 6 : 3) + speed && mmQueriedStates == std::vector<std::uint32_t>({static_cast<std::uint32_t>(selection.state)}));
                CHECK(animationSetting == setting && animationCommon == common && animationMatching == matching && animationMmData == data);
                CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
                CHECK(!propSetterCalls && !weaponMasterCalls && !weaponActiveCalls && !targetCalls);
            }
        Reset(); SeedMotionMatchingSelector(); mmKeys[4] = true;
        const auto fallback = native::SelectMotionMatching(proxy.Address(), 1, true);
        CHECK(fallback.current && fallback.required && fallback.valid && !fallback.usesLocked && fallback.state == 4);
        CHECK(mmQueriedStates == std::vector<std::uint32_t>({7, 4}));
        Reset(); SeedMotionMatchingSelector();
        const auto missing = native::SelectMotionMatching(proxy.Address(), 2, true);
        CHECK(missing.current && missing.required && !missing.valid && !missing.usesLocked && missing.state == -1);
        CHECK(mmQueriedStates == std::vector<std::uint32_t>({8, 5}));
        Reset(); SeedMotionMatchingSelector(); mmKeys[7] = true;
        const auto freeToLock = native::SelectMotionMatching(proxy.Address(), 1, false);
        CHECK(freeToLock.current && freeToLock.required && freeToLock.valid && freeToLock.usesLocked && freeToLock.state == 7);
        CHECK(mmQueriedStates == std::vector<std::uint32_t>({4, 7}));
        Reset(); SeedMotionMatchingSelector();
        reinterpret_cast<std::uint8_t *>(animationCommon.data())[0x41] = 10;
        animationSetting[0x20 / 8] = 0; mmMovePresent = mmNodePresent = false;
        const auto otherMode = native::SelectMotionMatching(proxy.Address(), 1, true);
        CHECK(otherMode.current && !otherMode.required && !otherMode.valid && otherMode.state == -1 && !mmGraphCalls && mmQueriedStates.empty());
    }
    void TestMotionMatchingSelectorFreeInputFallsBackToOwnLockStates()
    {
        for (int speed = 0; speed <= 2; ++speed)
            for (const bool genericLock : {false, true})
            {
                Reset(); SeedMotionMatchingSelector();
                const auto freeState = static_cast<std::uint32_t>(3 + speed), lockState = static_cast<std::uint32_t>(6 + speed);
                mmKeys[genericLock ? 1 : lockState] = true;
                const auto setting = animationSetting, common = animationCommon, matching = animationMatching, data = animationMmData;
                const auto selection = native::SelectMotionMatching(proxy.Address(), speed, false);
                CHECK(selection.available && selection.current && selection.required && selection.valid && selection.usesLocked);
                CHECK(selection.state == static_cast<int>(lockState) && mmQueriedStates == std::vector<std::uint32_t>({freeState, lockState}));
                CHECK(animationSetting == setting && animationCommon == common && animationMatching == matching && animationMmData == data);
                CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
                CHECK(!setterCalls && !propSetterCalls && !propResetCalls && !propApplyCalls && !weaponMasterCalls && !weaponActiveCalls);
                CHECK(!targetCalls && !clearTargetCalls && !getEventCollectionCalls && !getClearEventCalls);
            }
        Reset(); SeedMotionMatchingSelector(); mmKeys[1] = true; gcUnsafe = true;
        const auto nested = native::SelectMotionMatching(proxy.Address(), 1, false);
        CHECK(nested.current && nested.valid && nested.usesLocked && nested.state == 7);
        CHECK(mmQueriedStates == std::vector<std::uint32_t>({4, 7}));
        CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && gcUnsafe && gcEntries.empty());
        CHECK(!setterCalls && !propSetterCalls && !weaponMasterCalls && !weaponActiveCalls && !targetCalls && !clearTargetCalls);
        gcUnsafe = false;
    }
    void TestMotionMatchingSelectorFreeInputPrefersFreeAndRejectsDifferentSpeeds()
    {
        for (int speed = 0; speed <= 2; ++speed)
        {
            const auto freeState = static_cast<std::uint32_t>(3 + speed), lockState = static_cast<std::uint32_t>(6 + speed);
            for (const bool genericFree : {false, true})
            {
                Reset(); SeedMotionMatchingSelector();
                mmKeys[genericFree ? 2 : freeState] = mmKeys[1] = mmKeys[lockState] = true;
                const auto setting = animationSetting, common = animationCommon, matching = animationMatching, data = animationMmData;
                const auto selection = native::SelectMotionMatching(proxy.Address(), speed, false);
                CHECK(selection.available && selection.current && selection.required && selection.valid && !selection.usesLocked);
                CHECK(selection.state == static_cast<int>(freeState) && mmQueriedStates == std::vector<std::uint32_t>({freeState}));
                CHECK(animationSetting == setting && animationCommon == common && animationMatching == matching && animationMmData == data);
                CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
                CHECK(!setterCalls && !propSetterCalls && !propResetCalls && !propApplyCalls && !weaponMasterCalls && !weaponActiveCalls);
                CHECK(!targetCalls && !clearTargetCalls && !getEventCollectionCalls && !getClearEventCalls);
            }
            for (int otherSpeed = 0; otherSpeed <= 2; ++otherSpeed)
                if (otherSpeed != speed)
                    for (const bool locked : {false, true})
                    {
                        Reset(); SeedMotionMatchingSelector();
                        mmKeys[3 + otherSpeed] = mmKeys[6 + otherSpeed] = true;
                        const auto setting = animationSetting, common = animationCommon, matching = animationMatching, data = animationMmData;
                        const auto selection = native::SelectMotionMatching(proxy.Address(), speed, locked);
                        const auto expected = locked ? std::vector<std::uint32_t>({lockState, freeState}) : std::vector<std::uint32_t>({freeState, lockState});
                        CHECK(selection.available && selection.current && selection.required && !selection.valid && !selection.usesLocked && selection.state == -1);
                        CHECK(mmQueriedStates == expected);
                        CHECK(animationSetting == setting && animationCommon == common && animationMatching == matching && animationMmData == data);
                        CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
                        CHECK(!setterCalls && !propSetterCalls && !propResetCalls && !propApplyCalls && !weaponMasterCalls && !weaponActiveCalls);
                        CHECK(!targetCalls && !clearTargetCalls && !getEventCollectionCalls && !getClearEventCalls);
                    }
        }
    }
    void TestMotionMatchingSelectorSecondQueryFailuresRemainUnknown()
    {
        for (const bool locked : {false, true})
            for (const int failure : {1, 2})
            {
                Reset(); SeedMotionMatchingSelector();
                mmKeys[locked ? 2 : 1] = true;
                mmQueryEffectAt = 2; mmQueryFailure = failure;
                const auto setting = animationSetting, common = animationCommon, matching = animationMatching, data = animationMmData;
                const auto selection = native::SelectMotionMatching(proxy.Address(), 1, locked);
                const auto expected = locked ? std::vector<std::uint32_t>({7, 4}) : std::vector<std::uint32_t>({4, 7});
                CHECK(selection.available && !selection.current && !selection.required && !selection.valid && !selection.usesLocked && selection.state == -1);
                CHECK(mmQueriedStates == expected);
                CHECK(animationSetting == setting && animationCommon == common && animationMatching == matching && animationMmData == data);
                CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
                CHECK(!setterCalls && !propSetterCalls && !propResetCalls && !propApplyCalls && !weaponMasterCalls && !weaponActiveCalls);
                CHECK(!targetCalls && !clearTargetCalls && !getEventCollectionCalls && !getClearEventCalls);
            }
    }
    void TestMotionMatchingSelectorMissingDataAndFailuresRemainUnknown()
    {
        for (int failure = 0; failure != 20; ++failure)
        {
            Reset(); SeedMotionMatchingSelector(); mmKeys[2] = true;
            if (failure < 4) mmReflectionFailure = failure + 1;
            if (failure == 4) mmMovePresent = false;
            if (failure == 5) mmNodePresent = false;
            if (failure == 6) child.managed[0xb0 / 8] = 0;
            if (failure == 7) animationSetting[0x10 / 8] = 0;
            if (failure == 8) animationSetting[0x20 / 8] = 0;
            if (failure == 9) mmNode.managed[0xd0 / 8] = 0;
            if (failure == 10) unreadableAddress = mmNode.Managed() + 0xd0;
            if (failure == 11) mmNode.animMM = false;
            if (failure == 12) mmNode.owner = &player;
            if (failure == 13) mmQueryFailure = 1;
            if (failure == 14) mmQueryFailure = 2;
            if (failure == 15) reinterpret_cast<std::uint8_t *>(animationCommon.data())[0x41] = 255;
            if (failure == 16) gcFailureStage = 1;
            if (failure == 17) gcFailureStage = 2;
            if (failure == 18) gcFailureStage = 3;
            if (failure == 19) animationFailureStage = 2;
            const auto selection = native::SelectMotionMatching(proxy.Address(), 1, true);
            CHECK(selection.available && !selection.current && !selection.valid && selection.state == -1);
            CHECK(!propSetterCalls && !weaponMasterCalls && !weaponActiveCalls && gcEntries.empty() && !gcUnsafe);
        }
        for (const int speed : {-1, 3})
        {
            Reset(); SeedMotionMatchingSelector();
            CHECK(!native::SelectMotionMatching(proxy.Address(), speed, false).current && !gcEnterCalls);
        }
        Reset(); SeedMotionMatchingSelector(); mmKeys[2] = true; gcUnsafe = true;
        CHECK(native::SelectMotionMatching(proxy.Address(), 1, false).valid && gcUnsafe && gcEntries.empty());
        gcUnsafe = false;
    }
    void TestMotionMatchingSelectorRejectsReferenceAndLifetimeReplacement()
    {
        for (const int query : {1, 2})
            for (int changed = 1; changed <= 11; ++changed)
            {
                Reset(); SeedMotionMatchingSelector(); mmKeys[1] = true; mmChangeStage = changed; mmQueryEffectAt = query;
                const auto selection = native::SelectMotionMatching(proxy.Address(), 1, query == 1);
                CHECK(selection.available && !selection.current && !selection.required && !selection.valid && !selection.usesLocked && selection.state == -1);
                const auto expected = query == 1 ? std::vector<std::uint32_t>({7}) : std::vector<std::uint32_t>({4, 7});
                CHECK(mmQueriedStates == expected);
                CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
                CHECK(!setterCalls && !propSetterCalls && !propResetCalls && !propApplyCalls && !weaponMasterCalls && !weaponActiveCalls);
                CHECK(!targetCalls && !clearTargetCalls && !getEventCollectionCalls && !getClearEventCalls);
            }
    }
    void TestAnimationProbeOptionalFingerprintsNeverDisableGameplay()
    {
        for (const auto &spec : native::animationProbeSpecs)
        {
            Reset(); simulateHookInstall = true;
            rejectedFingerprint = gloss::gameplay::aot::Method(spec.rid);
            CHECK(!native::VerifyAnimationProbe());
            CHECK(native::Ready() && !native::animationProbeReady.load());
            const auto snapshot = native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address());
            CHECK(!snapshot.available && !snapshot.current && snapshot.finalMode == -1);
            CHECK(!gcEnterCalls && !hookCreateCalls && !animationGetterCalls);
        }
        Reset(); simulateHookInstall = true;
        CHECK(native::VerifyAnimationProbe());
        CHECK(native::Ready() && native::animationProbeReady.load());
        CHECK(native::animationProbeMethods.setting == gloss::gameplay::aot::Method(0x18753));
        CHECK(native::animationProbeMethods.configuredMode == gloss::gameplay::aot::Method(0x3b98));
        CHECK(native::animationProbeMethods.spareMode == gloss::gameplay::aot::Method(0x3bc0));
        CHECK(native::animationProbeMethods.finalMode == gloss::gameplay::aot::Method(0xe6d9));
        CHECK(!hookCreateCalls && !gcEnterCalls);
    }
    void TestAnimationProbeReadsOnlyExistingCachesAndExactByteModes()
    {
        using native::AnimationDataState;
        Reset(); SeedAnimationProbe();
        const auto settingBefore = animationSetting, commonBefore = animationCommon, matchingBefore = animationMatching, ecsBefore = animationEcs;
        const auto sample = native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address());
        CHECK(sample.available && sample.current && sample.setting == AnimationDataState::Present);
        CHECK(sample.commonSetting == AnimationDataState::Present && sample.configuredMode == 10);
        CHECK(sample.motionMatchingSetting == AnimationDataState::Present && sample.spareMode == 11);
        CHECK(sample.moveCommon == AnimationDataState::Present && sample.finalMode == 10);
        CHECK(animationGetterCalls == 2 && animationModeCalls == 3 && animationOwnerCalls == 4);
        CHECK(animationSetting == settingBefore && animationCommon == commonBefore && animationMatching == matchingBefore && animationEcs == ecsBefore);
        CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && gcEntries.empty() && !gcUnsafe);
        CHECK(!weaponMasterCalls && !weaponActiveCalls && !propSetterCalls);
        Reset(); SeedAnimationProbe();
        animationSetting[0x10 / 8] = animationSetting[0x20 / 8] = 0;
        const auto missingCaches = native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address());
        CHECK(missingCaches.current && missingCaches.setting == AnimationDataState::Present);
        CHECK(missingCaches.commonSetting == AnimationDataState::Missing && missingCaches.motionMatchingSetting == AnimationDataState::Missing);
        CHECK(missingCaches.configuredMode == -1 && missingCaches.spareMode == -1 && missingCaches.finalMode == 10);
        CHECK(animationModeCalls == 1 && !animationSetting[0x10 / 8] && !animationSetting[0x20 / 8]);
        Reset(); SeedAnimationProbe(); child.managed[0xb0 / 8] = 0; grandchild.managed[0xd0 / 8] = 0;
        const auto missingRoots = native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address());
        CHECK(missingRoots.current && missingRoots.setting == AnimationDataState::Missing && missingRoots.moveCommon == AnimationDataState::Missing);
        CHECK(missingRoots.commonSetting == AnimationDataState::Unknown && missingRoots.configuredMode == -1 && missingRoots.finalMode == -1 && !animationModeCalls);
        Reset(); SeedAnimationProbe();
        reinterpret_cast<std::uint8_t *>(animationEcs.data())[0xb8] = 0;
        const auto none = native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address());
        CHECK(none.current && none.moveCommon == AnimationDataState::Present && none.finalMode == 0);
        Reset(); SeedAnimationProbe(); gcUnsafe = true;
        CHECK(native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address()).current);
        CHECK(gcUnsafe && gcEnterCalls == 1 && gcExitCalls == 1 && gcEntries.empty());
        gcUnsafe = false;
    }
    void TestAnimationProbeFailuresStayUnknownAndNeverBecomeNone()
    {
        using native::AnimationDataState;
        for (int failure = 1; failure <= 4; ++failure)
        {
            Reset(); SeedAnimationProbe(); animationFailureStage = failure;
            const auto sample = native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address());
            CHECK(sample.current);
            if (failure == 1) CHECK(sample.setting == AnimationDataState::Unknown && sample.configuredMode == -1 && sample.finalMode == 10);
            if (failure == 2) CHECK(sample.commonSetting == AnimationDataState::Unknown && sample.configuredMode == -1 && sample.finalMode == 10);
            if (failure == 3) CHECK(sample.motionMatchingSetting == AnimationDataState::Unknown && sample.spareMode == -1 && sample.finalMode == 10);
            if (failure == 4) CHECK(sample.moveCommon == AnimationDataState::Unknown && sample.finalMode == -1 && sample.configuredMode == 10);
            CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && gcEntries.empty() && !gcUnsafe);
        }
        for (int failure = 0; failure < 6; ++failure)
        {
            Reset(); SeedAnimationProbe();
            if (failure == 0) unreadableAddress = reinterpret_cast<std::uintptr_t>(animationSetting.data()) + 0x10;
            if (failure == 1) unreadableAddress = reinterpret_cast<std::uintptr_t>(animationCommon.data());
            if (failure == 2) unreadableAddress = grandchild.Managed() + 0xd0;
            if (failure == 3) reinterpret_cast<std::uint8_t *>(animationEcs.data())[0xb8] = 255;
            if (failure == 4) reinterpret_cast<std::uint8_t *>(animationCommon.data())[0x41] = 14;
            if (failure == 5) reinterpret_cast<std::uint8_t *>(animationMatching.data())[0x51] = 254;
            const auto sample = native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address());
            CHECK(sample.current);
            if (failure == 0 || failure == 1 || failure == 4) CHECK(sample.commonSetting == AnimationDataState::Unknown && sample.configuredMode == -1);
            if (failure == 2 || failure == 3) CHECK(sample.moveCommon == AnimationDataState::Unknown && sample.finalMode == -1);
            if (failure == 5) CHECK(sample.motionMatchingSetting == AnimationDataState::Unknown && sample.spareMode == -1);
        }
    }
    void TestAnimationProbeRejectsReplacedReferencesAndInvalidSessions()
    {
        for (int changed = 1; changed <= 7; ++changed)
        {
            Reset(); SeedAnimationProbe();
            animationChangeStage = changed <= 4 ? changed : 0;
            animationRetire = changed == 5; animationDetach = changed == 6; animationNewSession = changed == 7;
            const auto sample = native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address());
            CHECK(sample.available && !sample.current && sample.configuredMode == -1 && sample.spareMode == -1 && sample.finalMode == -1);
            CHECK(gcEnterCalls == gcExitCalls && gcEntries.empty() && !gcUnsafe);
        }
        for (int rejected = 0; rejected < 4; ++rejected)
        {
            Reset(); SeedAnimationProbe();
            if (rejected == 0) child.animTop = false;
            if (rejected == 1) grandchild.animMove = false;
            if (rejected == 2) child.owner = &player;
            if (rejected == 3) bindings[5].pendingKill = true;
            CHECK(!native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address()).current);
            CHECK(!gcEnterCalls && !animationGetterCalls && !animationModeCalls);
        }
        Reset(); SeedAnimationProbe();
        std::thread foreign([&] { CHECK(!native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address()).current); });
        foreign.join();
        CHECK(!gcEnterCalls);
        for (int failure = 1; failure <= 2; ++failure)
        {
            Reset(); SeedAnimationProbe(); gcFailureStage = failure;
            CHECK(!native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address()).current);
            CHECK(!animationGetterCalls && !animationModeCalls && !gcUnsafe);
        }
        Reset(); SeedAnimationProbe(); gcFailureStage = 3;
        const auto failedExit = native::InspectAnimationState(proxy.Address(), child.Address(), grandchild.Address());
        CHECK(!failedExit.current && failedExit.finalMode == -1 && failedExit.configuredMode == -1);
        CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && gcEntries.empty() && !gcUnsafe);
    }
    void SeedWeapon(int index = 0, bool inactive = false)
    {
        using gloss::gameplay::engine::Arg;
        child.isWeapon = true;
        child.owner = &proxy;
        weaponRecords.assign(index + 1, {Arg::Object("Weapon", other.Address()), Arg::Boolean("bDefaultInactive", true)});
        weaponRecords[index][1] = Arg::Boolean("bDefaultInactive", inactive);
        weaponByIndex[index] = &child;
        weaponExtent = index + 1;
    }
    void TestWeaponLocalInitializationPreservesSlotsDefaultsAndSessionIdentity()
    {
        Reset();
        SeedWeapon(2); // Failed spawns at indices 0/1 must not compact slot 2.
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(weaponLookupCalls == 6 && weaponMasterCalls == 1 && weaponActiveCalls == 1);
        CHECK(child.weaponMaster == &proxy && child.weaponActive);
        CHECK(gcEnterCalls == 2 && gcExitCalls == 2 && gcEntries.empty() && !gcUnsafe);
        CHECK(native::OwnedChildren().size() == 1);
        child.weaponActive = false; // A later skill hides it; retries must preserve that.
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(!child.weaponActive && weaponMasterCalls == 1 && weaponActiveCalls == 1);
        ++bindings[4].serial; // Same address, new lifetime requires fresh initialization.
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(child.weaponActive && weaponMasterCalls == 2 && weaponActiveCalls == 2);
        CHECK(native::SetProxy({1, 7})); // A new session must not reuse the old claim.
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(weaponMasterCalls == 3 && weaponActiveCalls == 3);
        native::ClearProxy();
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(weaponMasterCalls == 3 && weaponActiveCalls == 3);

        Reset();
        SeedWeapon(0, true);
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(child.weaponMaster == &proxy && !child.weaponActive && weaponActiveCalls == 1);
        Reset();
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address())); // Valid character without weapons.
        CHECK(!weaponMasterCalls && !weaponActiveCalls && !gcEnterCalls);
        SeedWeapon();
        weaponExtent = 0; // Source configured, native spawn missing: diagnose, never fabricate.
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(!weaponLookupCalls && !weaponMasterCalls && !weaponActiveCalls);
        Reset();
        weaponExtent = 1; // An unknown dynamic table with no source defaults is untouched.
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(!weaponLookupCalls && !weaponMasterCalls && !weaponActiveCalls);
    }
    void TestWeaponInitializationRejectsUnknownOwnershipAndInvalidBoundaries()
    {
        for (int reason = 0; reason != 10; ++reason)
        {
            Reset(); SeedWeapon();
            if (reason == 0) child.owner = &player;
            if (reason == 1) child.ownerReadable = false;
            if (reason == 2) child.isWeapon = false;
            if (reason == 3) bindings[4].pendingKill = true;
            if (reason == 4) weaponTableReadable = false;
            if (reason == 5) weaponCountReadable = false;
            if (reason == 6) weaponLookupReadable = false;
            if (reason == 7) weaponExtent = -1;
            if (reason == 8) weaponExtent = 257;
            if (reason == 9) weaponRecords.resize(257);
            CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
            CHECK(!weaponMasterCalls && !weaponActiveCalls && !gcEnterCalls);
        }
        Reset(); SeedWeapon();
        CHECK(!native::InitializeWeapons(player.Address(), player.Address()));
        CHECK(!weaponLookupCalls && !weaponMasterCalls && !gcEnterCalls);
        bool foreignResult = true;
        std::thread foreign([&] { foreignResult = native::InitializeWeapons(proxy.Address(), player.Address()); });
        foreign.join();
        CHECK(!foreignResult && !weaponLookupCalls && !weaponMasterCalls && !gcEnterCalls);
    }
    void TestWeaponManagedAbiValidatesBeforeWritesAndNeverReplaysAmbiguousCalls()
    {
        for (int stage = 1; stage != 6; ++stage)
        {
            Reset(); SeedWeapon(); weaponFailureStage = stage;
            CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
            CHECK(weaponMasterCalls == (stage >= 4 ? 1 : 0) && weaponActiveCalls == (stage == 5 ? 1 : 0));
            CHECK(gcEnterCalls == (stage >= 4 ? 2 : 1) && gcExitCalls == gcEnterCalls && gcEntries.empty() && !gcUnsafe);
            weaponFailureStage = 0;
            if (stage >= 4)
            {
                CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
                CHECK(weaponMasterCalls == 1 && weaponActiveCalls == (stage == 5 ? 1 : 0));
            }
            else CHECK(native::InitializeWeapons(proxy.Address(), player.Address())); // No mutation occurred.
        }
        for (int missing = 0; missing != 4; ++missing)
        {
            Reset(); SeedWeapon();
            if (missing == 0) weaponCollection = 0;
            if (missing == 1) weaponCollection = other.Address(); // Native address is never a managed collection.
            if (missing == 2) masterCallback = 0;
            if (missing == 3) activeCallback = 0;
            CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
            CHECK(!weaponMasterCalls && !weaponActiveCalls && gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe);
        }
    }
    void TestWeaponFailureDiagnosticsPreserveRejectionAndIdentifyStage()
    {
        const auto logged = [](std::string_view fragment) {
            return std::any_of(diagnosticLogs.begin(), diagnosticLogs.end(), [&](const auto &line) { return line.find(fragment) != std::string::npos; });
        };
        for (int stage = 1; stage <= 5; ++stage)
        {
            Reset(); SeedWeapon(2, true); child.weaponActive = true; weaponFailureStage = stage;
            CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
            CHECK(logged(stage < 4 ? "stage=event-get" : stage == 4 ? "stage=master-invoke, reason=seh" : "stage=active-invoke, reason=seh"));
            CHECK(logged("index=2, configured=3, extent=3"));
            CHECK(logged("weak=4:13"));
            if (stage >= 4) CHECK(logged("active=0, beforeHidden=0, afterHidden=-1"));
            CHECK(weaponMasterCalls == (stage >= 4 ? 1 : 0) && weaponActiveCalls == (stage == 5 ? 1 : 0));
        }
        Reset(); SeedWeapon(); weaponTableReadable = false;
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(logged("stage=preflight, reason=source-weapons-read, index=-1"));
        Reset(); SeedWeapon(); child.owner = &player;
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(logged("stage=owner, reason=owner-mismatch"));
        Reset(); SeedWeapon(); child.isWeapon = false;
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(logged("stage=class, reason=not-weapon-base"));
        Reset(); SeedWeapon(); bindings[4].pendingKill = true;
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(logged("stage=weak, reason=weapon-lifetime-invalid"));
        Reset(); SeedWeapon(); child.reference[0x10 / 8] = 0;
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(logged("stage=managed-map, reason=proxy-or-weapon-map"));
        CHECK(!weaponMasterCalls && !weaponActiveCalls);
        Reset(); SeedWeapon(); weaponHiddenFailureRead = 2;
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(logged("stage=post-hidden-read, reason=property-unreadable"));
        CHECK(logged("active=1, beforeHidden=1, afterHidden=-1"));
        CHECK(weaponMasterCalls == 1 && weaponActiveCalls == 1);
        Reset(); SeedWeapon(1, true); child.weaponActive = true; weaponActiveNoOp = true;
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(logged("stage=post-hidden-mismatch, reason=visibility-differs-from-default"));
        CHECK(logged("index=1, configured=2, extent=2"));
        CHECK(logged("active=0, beforeHidden=0, afterHidden=0"));
        CHECK(weaponMasterCalls == 1 && weaponActiveCalls == 1);
        Reset(); SeedWeapon();
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(logged("initialization accepted: stage=post-hidden, reason=matches-default"));
        CHECK(logged("active=1, beforeHidden=1, afterHidden=0"));
    }
    void TestWeaponReadinessWaitsForAllChildrenBeforeAnyMutation()
    {
        using native::WeaponReadiness;
        Reset(); SeedWeapon(1, true);
        grandchild.isWeapon = true;
        grandchild.owner = &proxy;
        grandchild.weaponInitialized = false;
        weaponByIndex[0] = &child;
        weaponByIndex[1] = &grandchild;
        weaponRecords[0][1] = gloss::gameplay::engine::Arg::Boolean("bDefaultInactive", false);
        CHECK(native::CheckWeaponsReady(proxy.Address(), player.Address()) == WeaponReadiness::Pending);
        CHECK(!weaponMasterCalls && !weaponActiveCalls && native::weaponClaims.empty());
        const auto logCount = diagnosticLogs.size();
        CHECK(native::CheckWeaponsReady(proxy.Address(), player.Address()) == WeaponReadiness::Pending);
        CHECK(diagnosticLogs.size() == logCount); // Unchanged pending observations do not flood each frame.
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(!weaponMasterCalls && !weaponActiveCalls && native::weaponClaims.empty());
        grandchild.weaponInitialized = true;
        CHECK(native::CheckWeaponsReady(proxy.Address(), player.Address()) == WeaponReadiness::Ready);
        CHECK(diagnosticLogs.size() == logCount + 1);
        CHECK(!weaponMasterCalls && !weaponActiveCalls && native::weaponClaims.empty());
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(weaponMasterCalls == 2 && weaponActiveCalls == 2);
        CHECK(child.weaponActive && !grandchild.weaponActive);
        CHECK(native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(weaponMasterCalls == 2 && weaponActiveCalls == 2);
        CHECK(gcEnterCalls == gcExitCalls && gcEntries.empty() && !gcUnsafe);
        native::ClearProxy();
        CHECK(native::weaponObservations.empty() && native::weaponClaims.empty());
    }
    void TestWeaponReadinessRejectsMissingSubscribersAndInvalidLifetimes()
    {
        using native::WeaponReadiness;
        for (int failure = 0; failure < 9; ++failure)
        {
            Reset(); SeedWeapon();
            if (failure == 0) weaponReadyFailure = 1;
            if (failure == 1) masterCallbackObject[0x10 / 8] = 0;
            if (failure == 2) activeCallbackObject[0x10 / 8] = 0;
            if (failure == 3) unreadableAddress = masterCallback + 0x10;
            if (failure == 4) unreadableAddress = activeCallback + 0x10;
            if (failure == 5) activeCallbackObject[0x10 / 8] = 0x20000; // Aligned but unreadable delegate object.
            if (failure == 6) weaponRetireOnReady = true;
            if (failure == 7) weaponDetachOnReady = true;
            if (failure == 8) native::methods.weaponInitialized = 0;
            CHECK(native::CheckWeaponsReady(proxy.Address(), player.Address()) == WeaponReadiness::Failed);
            CHECK(!weaponMasterCalls && !weaponActiveCalls && native::weaponClaims.empty());
            CHECK(gcEnterCalls == gcExitCalls && gcEntries.empty() && !gcUnsafe);
        }
        Reset(); SeedWeapon(); child.weaponInitialized = false;
        masterCallbackObject[0x10 / 8] = activeCallbackObject[0x10 / 8] = 0;
        CHECK(native::CheckWeaponsReady(proxy.Address(), player.Address()) == WeaponReadiness::Pending);
        CHECK(!weaponEventOwner); // No lazy event gets before ECS readiness.
        child.weaponInitialized = true;
        CHECK(native::CheckWeaponsReady(proxy.Address(), player.Address()) == WeaponReadiness::Failed);
        CHECK(!weaponMasterCalls && !weaponActiveCalls && native::weaponClaims.empty());
        Reset(); SeedWeapon(); weaponReadyFlipCall = 2; // Ready at preflight, pending at mutation boundary.
        CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
        CHECK(!weaponMasterCalls && !weaponActiveCalls && native::weaponClaims.empty());
    }
    void TestWeaponPostcheckFailureNeverCompletesOrReplays()
    {
        for (int failure = 0; failure != 3; ++failure)
        {
            Reset(); SeedWeapon(0, true); child.weaponActive = true;
            weaponActiveNoOp = failure == 0;
            weaponHiddenFailureRead = failure == 1 ? 2 : 0;
            weaponRetireOnActive = failure == 2;
            CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
            CHECK(native::weaponClaims.size() == 1 && native::weaponClaims[0].phase == native::WeaponPhase::Indeterminate);
            CHECK(weaponMasterCalls == 1 && weaponActiveCalls == 1);
            weaponActiveNoOp = weaponRetireOnActive = false;
            weaponHiddenFailureRead = 0;
            if (failure == 2) bindings[4].pendingKill = true; // Original claim's lifetime remains retired.
            CHECK(!native::InitializeWeapons(proxy.Address(), player.Address()));
            CHECK(weaponMasterCalls == 1 && weaponActiveCalls == 1);
            CHECK(gcEnterCalls == gcExitCalls && gcEntries.empty() && !gcUnsafe);
        }
    }
    void TestManagedRuntimeRequiresCompleteUniqueSameImageEntries()
    {
        Reset();
        simulateRuntimeResolver = true;
        CHECK(native::VerifyManagedRuntime());
        CHECK(native::managedRuntime.currentThread == reinterpret_cast<std::uintptr_t>(CurrentMonoThread));
        CHECK(native::managedRuntime.enterUnsafe == reinterpret_cast<std::uintptr_t>(EnterMonoUnsafe));
        CHECK(native::managedRuntime.exitUnsafe == reinterpret_cast<std::uintptr_t>(ExitMonoUnsafe));
        for (int index = 0; index != 3; ++index)
            for (int failure = 0; failure != 3; ++failure)
            {
                Reset();
                simulateRuntimeResolver = true;
                (failure == 0 ? rejectedRuntimePattern : failure == 1 ? duplicateRuntimePattern : outsideRuntimePattern) = index;
                CHECK(!native::VerifyManagedRuntime());
                CHECK(!native::managedRuntime.currentThread && !native::managedRuntime.enterUnsafe && !native::managedRuntime.exitUnsafe);
                CHECK(!native::SetTarget(proxy.Address(), 0));
                CHECK(!gcEnterCalls && !findRefCalls && !clearTargetCalls);
            }
    }
    void TestManagedOperationsAreContinuousAndRestorePreviousGcState()
    {
        Reset();
        CHECK(native::SetTarget(proxy.Address(), 0));
        CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
        Reset();
        CHECK(native::SetTarget(proxy.Address(), other.Address()));
        CHECK(targetCalls == 1 && gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
        Reset();
        player.team = 4;
        CHECK(native::CopyTeam(proxy.Address(), player.Address()));
        CHECK(proxy.team == 4 && gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
        Reset();
        nestedClear = true;
        CHECK(native::SetTarget(proxy.Address(), 0));
        CHECK(gcEnterCalls == 2 && gcExitCalls == 2 && !gcUnsafe && gcEntries.empty());
        Reset();
        gcUnsafe = true; // Input mid-hooks already execute inside a managed call.
        CHECK(native::SetTarget(proxy.Address(), 0));
        CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && gcUnsafe && gcEntries.empty());
        Reset();
        CHECK(!native::SetTarget(player.Address(), 0));
        CHECK(gcCurrentCalls == 0 && gcEnterCalls == 0 && gcExitCalls == 0);
        bool foreignResult = true;
        std::thread foreign([&] { foreignResult = native::SetTarget(proxy.Address(), 0); });
        foreign.join();
        CHECK(!foreignResult && gcCurrentCalls == 0 && gcEnterCalls == 0 && gcExitCalls == 0);
        Reset();
        bool caught{};
        try
        {
            native::ManagedOperation([]() -> bool { CHECK(gcUnsafe); throw std::runtime_error("simulated operation exception"); });
        }
        catch (const std::runtime_error &) { caught = true; }
        CHECK(caught && gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
    }
    void TestManagedRegionRejectsUnavailableAndUnwindsFailures()
    {
        Reset();
        monoAttached = false;
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(gcCurrentCalls == 1 && gcEnterCalls == 0 && gcExitCalls == 0 && findRefCalls == 0);
        for (int stage = 1; stage != 4; ++stage)
        {
            Reset();
            gcFailureStage = stage;
            CHECK(!native::SetTarget(proxy.Address(), 0));
            CHECK(gcCurrentCalls == 1 && gcEnterCalls == (stage >= 2 ? 1 : 0) && gcExitCalls == (stage == 3 ? 1 : 0));
            CHECK(findRefCalls == (stage == 3 ? 1 : 0) && clearTargetCalls == (stage == 3 ? 1 : 0));
            CHECK(gcEntries.empty() && !gcUnsafe);
            if (stage == 3) CHECK(!native::Ready()); // Unknown exit state disables future operations.
        }
        for (int stage = 1; stage != 4; ++stage)
        {
            Reset();
            clearFailureStage = stage;
            CHECK(!native::SetTarget(proxy.Address(), 0));
            CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
        }
        Reset();
        eventCollectionManaged = 0; // A checked early return must leave too.
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
    }
    void TestCleanupRetainsManagedRegionThroughSynchronousCallbacks()
    {
        Reset();
        {
            native::ScopedCleanup cleanup(proxy.Address());
            CHECK(static_cast<bool>(cleanup) && gcUnsafe && gcEntries.size() == 1 && gcExitCalls == 0);
            {
                native::ScopedCleanup duplicate(proxy.Address());
                CHECK(!duplicate && gcUnsafe && gcEntries.size() == 2);
            }
            CHECK(gcUnsafe && gcEntries.size() == 1 && gcExitCalls == 1);
            CHECK(native::Initialized(proxy.Address()));
            CHECK(gcUnsafe && gcEntries.size() == 1);
            bindings[1].pendingKill = true;
            proxy.managed[0x58 / 8] = 0;
            std::array<std::uintptr_t, 4> component;
            auto ending = ComponentContext(proxy, component);
            native::Suppress(ending, nullptr);
            CHECK(ending.resume == native::returnStub && gcUnsafe);
            native::ClearProxy();
        }
        CHECK(gcEnterCalls == gcExitCalls && !gcUnsafe && gcEntries.empty());
        Reset();
        proxy.reference[0x20 / 8] = other.Address();
        {
            native::ScopedCleanup failedMapping(proxy.Address());
            CHECK(!failedMapping && gcUnsafe && gcEntries.size() == 1);
        }
        CHECK(gcEnterCalls == 1 && gcExitCalls == 1 && !gcUnsafe && gcEntries.empty());
    }
    Context ComponentContext(Object &owner, std::array<std::uintptr_t, 4> &component)
    {
        component = {1, 0, owner.Managed(), 0};
        Context result{};
        result.rcx = reinterpret_cast<std::uintptr_t>(component.data());
        result.resume = 0x7777;
        return result;
    }
    constexpr std::array<std::uint32_t, 7> cooperativeRids{0xafe6, 0xafe7, 0xafe8, 0xafe9, 0xafea, 0xafeb, 0xafec};
    void TestCooperativeHookContractsAndAtomicInstallation()
    {
        Reset();
        CHECK(native::hookSpecs.size() == 21);
        for (const auto rid : cooperativeRids)
        {
            CHECK(std::count_if(native::hookSpecs.begin(), native::hookSpecs.end(), [&](const auto &spec) { return spec.rid == rid; }) == 1);
            const auto spec = std::find_if(native::hookSpecs.begin(), native::hookSpecs.end(), [&](const auto &value) { return value.rid == rid; });
            if (spec != native::hookSpecs.end())
            {
                CHECK(spec->callback == native::Suppress);
                CHECK(spec->abi == native::HookAbi::InstanceVoid && spec->user == nullptr);
                CHECK(spec->bytes && std::strlen(spec->bytes) >= 50);
            }
        }
        CHECK(std::none_of(native::hookSpecs.begin(), native::hookSpecs.end(), [](const auto &spec) { return spec.rid == 0xafe5; })); // Preserve local OnAttach bindings.
        for (std::size_t i = 0; i != native::hookSpecs.size(); ++i)
        {
            const auto &spec = native::hookSpecs[i];
            CHECK(spec.callback && spec.bytes && *spec.bytes);
            CHECK(std::none_of(native::hookSpecs.begin(), native::hookSpecs.begin() + i, [&](const auto &previous) { return previous.rid == spec.rid; }));
            if (spec.callback == native::Suppress || spec.callback == native::OnDeath)
                CHECK(spec.abi == native::HookAbi::InstanceVoid);

            Reset();
            native::ready.store(false);
            simulateHookInstall = true;
            rejectedFingerprint = native::HookAddress(spec);
            CHECK(!native::InstallHooks());
            CHECK(!native::Ready() && native::Hooks().empty());
            CHECK(fingerprintChecks == static_cast<int>(i + 1) && hookCreateCalls == 0 && hookDestroyed == 0);

            Reset();
            native::ready.store(false);
            simulateHookInstall = true;
            failHookAt = static_cast<int>(i);
            CHECK(!native::InstallHooks());
            CHECK(!native::Ready() && native::Hooks().empty());
            CHECK(fingerprintChecks == static_cast<int>(native::hookSpecs.size()) && checksAtFirstHook == static_cast<int>(native::hookSpecs.size()));
            CHECK(hookCreateCalls == static_cast<int>(i + 1) && hookDestroyed == static_cast<int>(i));
        }
        Reset();
        native::ready.store(false);
        simulateHookInstall = true;
        CHECK(native::InstallHooks());
        CHECK(native::Hooks().size() == 21 && hookCreateCalls == 21 && hookDestroyed == 0 && checksAtFirstHook == 21);
        CHECK(!native::Ready()); // Only Initialize can publish readiness after its other verifications.
        native::Initialize(); // AOT binding is unavailable in this test executable.
        CHECK(!native::Ready());
        native::Hooks().clear();
        CHECK(hookDestroyed == 21);
        simulateHookInstall = false;
    }
    void TestEveryCooperativeHookIsScopedToSessionObjects()
    {
        for (const auto rid : cooperativeRids)
        {
            const auto found = std::find_if(native::hookSpecs.begin(), native::hookSpecs.end(), [&](const auto &spec) { return spec.rid == rid; });
            if (found == native::hookSpecs.end())
            {
                CHECK(false);
                continue;
            }
            const auto callback = found->callback;
            Reset();
            std::array<std::uintptr_t, 4> component;
            auto root = ComponentContext(proxy, component);
            root.rax = 0x98765432;
            callback(root, found->user);
            CHECK(root.resume == native::returnStub && root.rax == 0x98765432); // Void suppression preserves return registers.
            auto unrelated = ComponentContext(other, component);
            callback(unrelated, found->user);
            CHECK(unrelated.resume == 0x7777);
            auto retained = ComponentContext(player, component);
            callback(retained, found->user);
            CHECK(retained.resume == 0x7777);
            std::thread foreign([&] {
                std::array<std::uintptr_t, 4> foreignComponent;
                auto outside = ComponentContext(proxy, foreignComponent);
                callback(outside, found->user);
                CHECK(outside.resume == 0x7777);
            });
            foreign.join();
            child.owner = &proxy;
            grandchild.owner = &child;
            auto nested = ComponentContext(grandchild, component);
            callback(nested, found->user);
            CHECK(nested.resume == native::returnStub && native::OwnedChildren().size() == 2);
            child.owner = nullptr;
            auto detached = ComponentContext(child, component);
            callback(detached, found->user);
            CHECK(detached.resume == native::returnStub);
            {
                native::ScopedCleanup cleanup(child.Address());
                CHECK(static_cast<bool>(cleanup));
                bindings[4].pendingKill = true;
                child.managed[0x58 / 8] = 0;
                auto ending = ComponentContext(child, component);
                callback(ending, found->user);
                CHECK(ending.resume == native::returnStub);
            }
            auto expired = ComponentContext(child, component);
            callback(expired, found->user);
            CHECK(expired.resume == 0x7777);
            native::ClearProxy();
            root = ComponentContext(proxy, component);
            callback(root, found->user);
            CHECK(root.resume == 0x7777);
            nested = ComponentContext(grandchild, component);
            callback(nested, found->user);
            CHECK(nested.resume == 0x7777);

            Reset();
            ++bindings[1].serial;
            root = ComponentContext(proxy, component);
            callback(root, found->user);
            CHECK(root.resume == 0x7777);
            Reset();
            child.owner = &proxy;
            auto ownedChild = ComponentContext(child, component);
            callback(ownedChild, found->user);
            CHECK(ownedChild.resume == native::returnStub);
            child.owner = nullptr;
            ++bindings[4].serial;
            auto reused = ComponentContext(child, component);
            callback(reused, found->user);
            CHECK(reused.resume == 0x7777);
            Reset();
            {
                native::ScopedCleanup cleanup(proxy.Address());
                CHECK(static_cast<bool>(cleanup));
                bindings[1].pendingKill = true;
                proxy.managed[0x58 / 8] = 0;
                auto ending = ComponentContext(proxy, component);
                callback(ending, found->user);
                CHECK(ending.resume == native::returnStub);
                native::ClearProxy();
                ending = ComponentContext(proxy, component);
                callback(ending, found->user);
                CHECK(ending.resume == 0x7777);
            }
        }
    }
    void TestIsolationAndWeakSerialReuse()
    {
        Reset();
        std::array<std::uintptr_t, 4> component;
        auto owned = ComponentContext(proxy, component);
        native::Suppress(owned, nullptr);
        CHECK(owned.resume == native::returnStub);
        auto unrelated = ComponentContext(other, component);
        native::OnDeath(unrelated, nullptr);
        CHECK(unrelated.resume == 0x7777);
        CHECK(!native::DeathRequested());
        auto death = ComponentContext(proxy, component);
        native::OnDeath(death, nullptr);
        CHECK(death.resume == native::returnStub);
        CHECK(native::DeathRequested());
        native::ClearProxy();
        CHECK(!native::DeathRequested());
        CHECK(native::SetProxy({1, 7}));
        bindings[1].serial = 8; // Same native address, a different UObject lifetime.
        auto reused = ComponentContext(proxy, component);
        native::Suppress(reused, nullptr);
        CHECK(reused.resume == 0x7777);
        CHECK(!native::SetProxy({1, 7}));
    }
    void TestManagedIdentityAndOwnedWrites()
    {
        Reset();
        CHECK(native::SetTamerType(proxy.Address(), 3));
        CHECK(proxy.tamer == 3);
        CHECK(setterCalls == 1);
        CHECK(!native::SetTamerType(player.Address(), 3));
        CHECK(!native::SetTamerType(proxy.Address(), 99));
        CHECK(setterCalls == 1);
        CHECK(native::Initialized(proxy.Address()));
        player.team = 4;
        CHECK(native::CopyTeam(proxy.Address(), player.Address()));
        CHECK(proxy.team == 4);
        CHECK(!native::CopyTeam(other.Address(), player.Address()));
        CHECK(native::SetTarget(proxy.Address(), other.Address()));
        CHECK(targetCalls == 1 && clearTargetCalls == 0);
        CHECK(native::SetTarget(proxy.Address(), 0));
        CHECK(targetCalls == 1 && clearTargetCalls == 1);
        CHECK(!native::SetTarget(other.Address(), 0));
        proxy.reference[0x20 / 8] = other.Address();
        CHECK(!native::SetTamerType(proxy.Address(), 1));
        CHECK(!native::Initialized(proxy.Address()));
        CHECK(setterCalls == 1);
    }
    void TestClearTargetManagedGetterAbiAndFailures()
    {
        Reset();
        CHECK(native::NativeAddress(eventCollectionManaged) == other.Address());
        CHECK(native::NativeAddress(clearCallbackManaged) == 0); // GSDel_Void is not a UObject.
        CHECK(native::SetTarget(proxy.Address(), 0));
        CHECK(findRefCalls == 1 && rpcReflectionAttempts == 0); // Map only the original native proxy.
        CHECK(getEventCollectionCalls == 1 && getClearEventCalls == 1 && clearTargetCalls == 1 && targetCalls == 0);
        CHECK(!native::SetTarget(player.Address(), 0));
        CHECK(!native::SetTarget(other.Address(), 0));
        CHECK(!native::SetTarget(child.Address(), 0));
        CHECK(getEventCollectionCalls == 1 && clearTargetCalls == 1 && findRefCalls == 1);
        bool foreignResult = true;
        std::thread foreign([&] { foreignResult = native::SetTarget(proxy.Address(), 0); });
        foreign.join();
        CHECK(!foreignResult && getEventCollectionCalls == 1 && clearTargetCalls == 1);
        native::ClearProxy();
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(getEventCollectionCalls == 1 && clearTargetCalls == 1);

        Reset();
        eventCollectionManaged = other.Address(); // Returning a native pointer must not be remapped into success.
        CHECK(native::NativeAddress(eventCollectionManaged) == 0);
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(findRefCalls == 1 && getEventCollectionCalls == 1 && getClearEventCalls == 0 && clearTargetCalls == 0 && rpcReflectionAttempts == 0);
        Reset();
        eventCollectionManaged = 0;
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(getEventCollectionCalls == 1 && getClearEventCalls == 0 && clearTargetCalls == 0);
        Reset();
        eventCollectionManaged = clearCallbackManaged; // A plain managed object is not the UObject event collection.
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(getEventCollectionCalls == 1 && getClearEventCalls == 0 && clearTargetCalls == 0);
        Reset();
        clearCallbackManaged = 0;
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(getEventCollectionCalls == 1 && getClearEventCalls == 1 && clearTargetCalls == 0);
        Reset();
        clearCallbackManaged = 1; // Invalid/unaligned callback.
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(getClearEventCalls == 1 && clearTargetCalls == 0);
        Reset();
        clearCallbackManaged = 0x10000; // Aligned but unreadable callback.
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(getClearEventCalls == 1 && clearTargetCalls == 0);
        for (int stage = 1; stage <= 3; ++stage)
        {
            Reset();
            clearFailureStage = stage;
            CHECK(!native::SetTarget(proxy.Address(), 0));
            CHECK(getEventCollectionCalls == 1 && getClearEventCalls == (stage >= 2 ? 1 : 0) && clearTargetCalls == (stage == 3 ? 1 : 0));
            CHECK(findRefCalls == 1 && rpcReflectionAttempts == 0);
        }
        Reset();
        proxy.reference[0x20 / 8] = other.Address(); // Native/managed identity disagreement.
        CHECK(!native::SetTarget(proxy.Address(), 0));
        CHECK(getEventCollectionCalls == 0 && getClearEventCalls == 0 && clearTargetCalls == 0 && rpcReflectionAttempts == 0);
    }
    void TestLocomotionFacingDispatchUsesHorizontalYawAndRestoresGc()
    {
        Reset();
        const gloss::gameplay::extensions::Vec3 direction{0.0, 2.0, 99.0};
        CHECK(native::SetLocomotionFacing(proxy.Address(), direction));
        CHECK(facingCalls == 1 && std::abs(facingRotation.pitch) < 1e-9 &&
              std::abs(facingRotation.yaw - 90.0) < 1e-9 && std::abs(facingRotation.roll) < 1e-9);
        CHECK(!gcUnsafe && gcEntries.empty());
        CHECK(!native::SetLocomotionFacing(proxy.Address(), gloss::gameplay::extensions::Vec3{0.0, 0.0, 1.0}));
        CHECK(!native::SetLocomotionFacing(proxy.Address(), gloss::gameplay::extensions::Vec3{std::numeric_limits<double>::quiet_NaN(), 1.0, 0.0}));
        CHECK(facingCalls == 1);

        Reset();
        facingCallbackObject[0x10 / 8] = 0;
        CHECK(!native::SetLocomotionFacing(proxy.Address(), gloss::gameplay::extensions::Vec3{1.0, 0.0, 0.0}));
        CHECK(facingCalls == 0 && !gcUnsafe && gcEntries.empty());

        Reset();
        facingFailureStage = 1;
        CHECK(!native::SetLocomotionFacing(proxy.Address(), gloss::gameplay::extensions::Vec3{-1.0, 0.0, 0.0}));
        CHECK(facingCalls == 1 && !gcUnsafe && gcEntries.empty());

        Reset();
        native::facingEventReady.store(false);
        CHECK(!native::SetLocomotionFacing(proxy.Address(), gloss::gameplay::extensions::Vec3{1.0, 0.0, 0.0}));
        CHECK(facingCalls == 0 && !gcUnsafe && gcEntries.empty());
    }
    void TestLocomotionFacingRejectsReusedIdentityAndReentrantSessionChanges()
    {
        for (int stage : {2, 3, 4})
        {
            Reset(); facingFailureStage = stage;
            CHECK(!native::SetLocomotionFacing(proxy.Address(), {1., 1., 0.}));
            CHECK(facingCalls == (stage == 4 ? 1 : 0));
            CHECK(!gcUnsafe && gcEntries.empty() && gcEnterCalls == gcExitCalls);
        }
        Reset();
        CHECK(!native::SetLocomotionFacing(player.Address(), {1., 0., 0.}));
        CHECK(!native::SetLocomotionFacing(other.Address(), {1., 0., 0.}));
        CHECK(!facingCalls && !getEventCollectionCalls);
        Reset(); simulateHookInstall = true;
        CHECK(native::VerifyFacingEvent() && native::Ready());
        for (auto rid : {0x30afu, 0x15313u})
        {
            rejectedFingerprint = gloss::gameplay::aot::Method(rid);
            CHECK(!native::VerifyFacingEvent() && native::Ready());
            CHECK(!native::facingEventReady && !native::methods.forceFacingEvent && !native::methods.invokeFacing);
        }
    }
    void TestOwnedChildIsolationAndDeathCleanup()
    {
        Reset();
        child.owner = &proxy;
        grandchild.owner = &child;
        std::array<std::uintptr_t, 4> component;
        auto nested = ComponentContext(grandchild, component);
        native::Suppress(nested, nullptr);
        CHECK(nested.resume == native::returnStub);
        const auto family = native::OwnedChildren();
        CHECK(family.size() == 2 && family[0].index == 5 && family[1].index == 4);
        CHECK(native::OwnedChildren(true).empty());
        CHECK(!native::SetTamerType(child.Address(), 3));
        CHECK(!native::CopyTeam(child.Address(), player.Address()));
        CHECK(!native::SetTarget(child.Address(), other.Address()));
        CHECK(!native::SetTarget(proxy.Address(), child.Address())); // Never aim the root at its own summon.
        child.owner = nullptr; // Detaching a proven temporary object must not escape isolation.
        auto detached = ComponentContext(child, component);
        native::OnDeath(detached, nullptr);
        CHECK(detached.resume == native::returnStub && !native::DeathRequested());
        const auto dead = native::OwnedChildren(true);
        CHECK(dead.size() == 1 && dead[0].index == 4);
        {
            native::ScopedCleanup cleanup(child.Address());
            CHECK(static_cast<bool>(cleanup));
            bindings[4].pendingKill = true;
            child.managed[0x58 / 8] = 0;
            auto ending = ComponentContext(child, component);
            native::Suppress(ending, nullptr);
            CHECK(ending.resume == native::returnStub);
            native::OnDeath(ending, nullptr);
            CHECK(!native::DeathRequested());
        }
        CHECK(native::OwnedChildren().size() == 1); // A PendingKill child is never returned for another destroy.
        auto outsideScope = ComponentContext(child, component);
        native::Suppress(outsideScope, nullptr);
        CHECK(outsideScope.resume == 0x7777);
        native::ClearProxy();
        CHECK(native::OwnedChildren().empty());
        auto reset = ComponentContext(grandchild, component);
        native::Suppress(reset, nullptr);
        CHECK(reset.resume == 0x7777);
    }
    void TestChildOwnershipRejectsCyclesForeignActorsAndReuse()
    {
        Reset();
        std::array<std::uintptr_t, 4> component;
        child.owner = &grandchild;
        grandchild.owner = &child;
        auto cycle = ComponentContext(child, component);
        native::Suppress(cycle, nullptr);
        CHECK(cycle.resume == 0x7777 && native::OwnedChildren().empty());
        child.owner = &player; // The retained player's other children are not ours.
        auto unrelated = ComponentContext(child, component);
        native::Suppress(unrelated, nullptr);
        CHECK(unrelated.resume == 0x7777 && native::OwnedChildren().empty());
        child.owner = &proxy;
        child.ownerReadable = false;
        auto unavailable = ComponentContext(child, component);
        native::Suppress(unavailable, nullptr);
        CHECK(unavailable.resume == 0x7777 && native::OwnedChildren().empty());
        child.ownerReadable = true;
        auto owned = ComponentContext(child, component);
        native::Suppress(owned, nullptr);
        CHECK(owned.resume == native::returnStub && native::OwnedChildren().size() == 1);
        child.owner = nullptr;
        ++bindings[4].serial; // Same addresses, unrelated new lifetime.
        auto reused = ComponentContext(child, component);
        native::Suppress(reused, nullptr);
        CHECK(reused.resume == 0x7777 && native::OwnedChildren().empty());
        native::ScopedCleanup stale(child.Address());
        CHECK(!stale);
        bool foreignSet = true;
        std::thread foreign([&] { foreignSet = native::SetProxy({1, 7}); });
        foreign.join();
        CHECK(!foreignSet && native::Identity(false).serial == 7);
    }
    void TestChildOwnerTraversalBound()
    {
        Reset();
        std::vector<std::unique_ptr<Object>> chain;
        for (int i = 0; i != 17; ++i)
        {
            auto object = std::make_unique<Object>();
            object->owner = i ? chain.back().get() : &proxy;
            CHECK(bindings.emplace(10 + i, Binding{20 + i, object.get()}).second);
            chain.push_back(std::move(object));
        }
        std::array<std::uintptr_t, 4> component;
        auto excessive = ComponentContext(*chain[16], component);
        native::Suppress(excessive, nullptr);
        CHECK(excessive.resume == 0x7777 && native::OwnedChildren().empty());
        auto bounded = ComponentContext(*chain[15], component);
        native::Suppress(bounded, nullptr);
        const auto family = native::OwnedChildren();
        CHECK(bounded.resume == native::returnStub && family.size() == 16);
        if (family.size() == 16) CHECK(family.front().index == 25 && family.back().index == 10);
        // Resolve from an already-proven ancestor without a new arbitrary walk.
        excessive = ComponentContext(*chain[16], component);
        native::Suppress(excessive, nullptr);
        CHECK(excessive.resume == native::returnStub && native::OwnedChildren().size() == 17);
        native::ClearProxy();
        for (int i = 0; i != 17; ++i)
            bindings.erase(10 + i);
    }
    void TestSynchronousCleanupScopeAndReuse()
    {
        Reset();
        std::array<std::uintptr_t, 4> component;
        {
            native::ScopedCleanup unrelated(other.Address());
            CHECK(!unrelated);
        }
        {
            native::ScopedCleanup cleanup(proxy.Address());
            CHECK(static_cast<bool>(cleanup));
            bindings[1].pendingKill = true;
            CHECK(gloss::gameplay::engine::ResolveWeak({1, 7}) == 0);
            auto pending = ComponentContext(proxy, component);
            native::Suppress(pending, nullptr);
            CHECK(pending.resume == native::returnStub);
            auto unrelated = ComponentContext(other, component);
            native::Suppress(unrelated, nullptr);
            CHECK(unrelated.resume == 0x7777);
            {
                native::ScopedCleanup nested(proxy.Address());
                CHECK(!nested); // PendingKill cannot open a new claim.
            }
            pending = ComponentContext(proxy, component);
            proxy.managed[0x58 / 8] = 0; // Managed detach can happen before callbacks finish.
            native::Suppress(pending, nullptr);
            CHECK(pending.resume == native::returnStub);
            proxy.managed[0x58 / 8] = proxy.Address();
            std::thread foreign([&] {
                std::array<std::uintptr_t, 4> foreignComponent;
                auto outsideThread = ComponentContext(proxy, foreignComponent);
                native::Suppress(outsideThread, nullptr);
                CHECK(outsideThread.resume == 0x7777);
            });
            foreign.join();
        }
        auto after = ComponentContext(proxy, component);
        native::Suppress(after, nullptr);
        CHECK(after.resume == 0x7777); // No raw address claim survives the stack scope.
        Reset();
        {
            native::ScopedCleanup cleanup(proxy.Address());
            CHECK(static_cast<bool>(cleanup));
            ++bindings[1].serial; // New valid UObject, same native and managed addresses.
            auto reused = ComponentContext(proxy, component);
            native::Suppress(reused, nullptr);
            CHECK(reused.resume == 0x7777);
            bindings[1].pendingKill = true; // Same rejection if replacement is also dying.
            reused.resume = 0x7777;
            native::Suppress(reused, nullptr);
            CHECK(reused.resume == 0x7777);
        }
        native::ScopedCleanup stale(proxy.Address());
        CHECK(!stale);
        Reset();
        {
            native::ScopedCleanup cleanup(proxy.Address());
            CHECK(static_cast<bool>(cleanup));
            bindings[1].pendingKill = true;
            native::ClearProxy();
            auto cancelled = ComponentContext(proxy, component);
            native::Suppress(cancelled, nullptr);
            CHECK(cancelled.resume == 0x7777);
        }
        Reset();
        {
            native::ScopedCleanup previousSession(proxy.Address());
            CHECK(static_cast<bool>(previousSession));
            native::ClearProxy();
            CHECK(native::SetProxy({1, 7})); // Even reuse of the same live root starts a new session.
            bindings[1].pendingKill = true;
            auto revivedClaim = ComponentContext(proxy, component);
            native::Suppress(revivedClaim, nullptr);
            CHECK(revivedClaim.resume == 0x7777);
        }
    }
    void TestMappedActionFilteringAndCancellation()
    {
        Reset();
        CHECK(!native::SetInputOwner({1, 7})); // Proxy must never become the retained player's input owner.
        CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        frame.context.rcx = 0x123000;
        frame.context.r8 = 2;
        frame.context.resume = 0x7777;
        native::Input(frame.context, reinterpret_cast<void *>(Action::LightAttack));
        auto actions = native::ConsumeActions();
        CHECK(actions.size() == 1 && actions[0] == Action::LightAttack);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 1);
        frame.context.r8 = 5; // Release is consumed without adding a new attack.
        native::Input(frame.context, reinterpret_cast<void *>(Action::HeavyAttack));
        CHECK(native::ConsumeActions().empty());
        frame.context.r8 = 2;
        std::uintptr_t prediction = 1;
        CHECK(gloss::gameplay::Write(frame.context.rsp() + 0x28, prediction));
        native::Input(frame.context, reinterpret_cast<void *>(Action::HeavyAttack));
        CHECK(native::ConsumeActions().empty());
        CHECK(frame.context.rax == 0);
        prediction = 0;
        CHECK(gloss::gameplay::Write(frame.context.rsp() + 0x28, prediction));
        inputManaged = other.Managed();
        frame.context.resume = 0x7777;
        native::Input(frame.context, reinterpret_cast<void *>(Action::Dodge));
        CHECK(frame.context.resume == 0x7777);
        CHECK(native::ConsumeActions().empty());
        inputManaged = player.Managed();
        native::Input(frame.context, reinterpret_cast<void *>(Action::Dodge));
        native::ClearProxy();
        CHECK(native::ConsumeActions().empty());
        frame.context.resume = 0x7777;
        native::Input(frame.context, nullptr);
        CHECK(frame.context.resume == 0x7777);
    }
    void TestMappedSkillSlotsAndUnknownActions()
    {
        Reset();
        CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        frame.context.rdx = 42;
        frame.context.r8 = 2;
        for (int spell = 1; spell <= 4; ++spell)
        {
            inputParameter = spell;
            frame.context.resume = 0x7777;
            native::SkillInput(frame.context, nullptr);
            const auto queued = native::ConsumeActions();
            CHECK(queued.size() == 1 && queued[0] == static_cast<Action>(static_cast<int>(Action::Skill1) + spell - 1));
            CHECK(frame.context.resume == native::returnStub);
        }
        inputParameter = 0; // Default spell can replace/destroy the retained pawn.
        frame.context.resume = 0x7777;
        native::SkillInput(frame.context, nullptr);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 0 && native::ConsumeActions().empty());
        inputParameter = 9; // Ride must not reach the original transformation.
        frame.context.resume = 0x7777;
        native::SkillInput(frame.context, nullptr);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 0 && native::ConsumeActions().empty());
        inputParameter = 4;
        frame.context.rdx = 7; // Descriptor lookup failed.
        frame.context.resume = 0x7777;
        native::SkillInput(frame.context, nullptr);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 0 && native::ConsumeActions().empty());
        frame.context.rdx = 42;
        frame.context.r8 = 0; // UseSkillByType normalizes Triggered to Started.
        native::SkillInput(frame.context, nullptr);
        CHECK(native::ConsumeActions().size() == 1);
        battleAllowed = false;
        native::SkillInput(frame.context, nullptr);
        CHECK(native::ConsumeActions().empty() && frame.context.rax == 0);
    }
    void TestReleaseInputPreservesProxyIsolation()
    {
        Reset();
        CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        frame.context.r8 = 2;
        native::Input(frame.context, reinterpret_cast<void *>(Action::LightAttack));
        CHECK(native::actionCount == 1);
        native::movement = {.5f, -.75f};
        native::movementTime = {GetTickCount64(), GetTickCount64()};
        native::rejectedSkillCount = 1;
        CHECK(native::SetInputOwner({}));
        CHECK(native::actionCount == 0 && native::ConsumeActions().empty());
        CHECK(native::movement.forward == 0 && native::movement.right == 0);
        CHECK(native::movementTime[0] == 0 && native::movementTime[1] == 0 && native::rejectedSkillCount == 0);
        frame.context.resume = 0x7777;
        native::Input(frame.context, reinterpret_cast<void *>(Action::LightAttack));
        CHECK(frame.context.resume == 0x7777 && native::ConsumeActions().empty());
        std::array<std::uintptr_t, 4> component;
        auto isolation = ComponentContext(proxy, component);
        native::Suppress(isolation, nullptr);
        CHECK(isolation.resume == native::returnStub);
        const auto identity = native::Identity(false);
        CHECK(identity.index == 1 && identity.serial == 7);

        CHECK(native::SetInputOwner({2, 9}));
        bool foreignRelease = true;
        std::thread foreign([&] { foreignRelease = native::SetInputOwner({}); });
        foreign.join();
        CHECK(!foreignRelease);
        CHECK(native::Identity(true).index == 2 && native::Identity(true).serial == 9);
        CHECK(!native::SetInputOwner({2, 8})); // Nonempty stale identities do not release input.
        native::ready.store(false);
        CHECK(native::SetInputOwner({})); // Release only needs the game thread.
        native::ready.store(true);
        native::ClearProxy();
        CHECK(native::SetInputOwner({})); // Idempotent after full session cleanup.
    }
    void TestMappedMovementExpiryAndRelease()
    {
        Reset();
        CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        std::uintptr_t managedValue = 0x123000;
        frame.context.r9 = reinterpret_cast<std::uintptr_t>(&managedValue);
        frame.context.rdx = 42;
        frame.context.r8 = 0;
        inputParameter = 0; inputAxis = .75f;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().forward == .75f);
        inputParameter = 1; inputAxis = -.5f;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().right == -.5f);
        frame.context.r8 = 5;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().right == 0.f);
        native::movementTime[0] = GetTickCount64() - 251;
        CHECK(native::MovementAxes().forward == 0.f);
        frame.context.r8 = 0;
        battleAllowed = false;
        inputParameter = 0;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().forward == 0.f);
        battleAllowed = true;
        inputAxis = (std::numeric_limits<float>::quiet_NaN)();
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().forward == 0.f);
        inputAxis = 3.f;
        native::Move(frame.context, nullptr);
        CHECK(native::MovementAxes().forward == 1.f);
        parameters[0x18 / 8] = 1; // Movement requires exactly two descriptor ints.
        frame.context.resume = 0x7777;
        native::Move(frame.context, nullptr);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 0);
        CHECK(native::MovementAxes().forward == 0.f && native::MovementAxes().right == 0.f);
        parameters[0x18 / 8] = 2;
        inputParameter = 9; // A resolved but unknown axis also cannot reach the hidden pawn.
        frame.context.resume = 0x7777;
        native::Move(frame.context, nullptr);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 0);
        inputManaged = other.Managed();
        frame.context.resume = 0x7777;
        native::Move(frame.context, nullptr);
        CHECK(frame.context.resume == 0x7777); // Unknown actions for other actors remain untouched.
        native::ClearProxy();
        CHECK(native::MovementAxes().forward == 0.f && native::MovementAxes().right == 0.f);
    }
    void TestMappedSprintHoldReleaseResetAndPrediction()
    {
        Reset(); CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        frame.context.rdx = 42; frame.context.r8 = 2;
        inputParameter = 2; inputSpeedMode = 2;
        native::SprintInput(frame.context, nullptr);
        CHECK(frame.context.resume == native::returnStub && frame.context.rax == 1 && native::MovementAxes().sprint);
        native::movement.forward = .75f; native::movement.right = -.5f;
        native::movementTime = {GetTickCount64() - 251, GetTickCount64() - 251};
        CHECK(native::MovementAxes().sprint && native::MovementAxes().forward == 0.f); // Hold does not expire with axes.
        frame.context.r8 = 0;
        native::SprintInput(frame.context, nullptr);
        CHECK(native::MovementAxes().sprint);
        for (const auto releaseEvent : {4, 5})
        {
            frame.context.r8 = 2; native::SprintInput(frame.context, nullptr);
            battleAllowed = false;
            frame.context.r8 = releaseEvent; native::SprintInput(frame.context, nullptr);
            CHECK(!native::MovementAxes().sprint && frame.context.resume == native::returnStub);
            battleAllowed = true;
        }
        frame.context.r8 = 2; native::SprintInput(frame.context, nullptr);
        CHECK(native::MovementAxes().sprint);
        std::uintptr_t prediction = 1;
        CHECK(gloss::gameplay::Write(frame.context.rsp() + 0x28, prediction));
        frame.context.r8 = 5; native::SprintInput(frame.context, nullptr);
        CHECK(native::MovementAxes().sprint && frame.context.rax == 0); // Replayed release cannot clear live hold.
        native::ResetSprintInput();
        CHECK(!native::MovementAxes().sprint && native::movement.forward == .75f && native::movement.right == -.5f);
        frame.context.r8 = 2; native::SprintInput(frame.context, nullptr);
        CHECK(!native::MovementAxes().sprint); // Replayed press cannot re-enable after suppression.
        prediction = 0; CHECK(gloss::gameplay::Write(frame.context.rsp() + 0x28, prediction));
        native::SprintInput(frame.context, nullptr);
        CHECK(native::MovementAxes().sprint); // A fresh live press resumes normally.
        CHECK(native::SetInputOwner({}));
        CHECK(!native::MovementAxes().sprint && !native::movement.sprint);
        CHECK(!gcUnsafe && gcEntries.empty() && gcEnterCalls == gcExitCalls);
    }
    void TestMappedSprintToggleFollowsConfiguredEventsAndRejectsForeignActors()
    {
        Reset(); CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        frame.context.rdx = 42; inputParameter = 2; inputSpeedMode = 1;
        for (const auto configuredEvent : {2, 1, 5})
        {
            frame.context.r8 = configuredEvent;
            native::SprintInput(frame.context, nullptr);
            CHECK(native::MovementAxes().sprint);
            const std::uintptr_t prediction = 1;
            CHECK(gloss::gameplay::Write(frame.context.rsp() + 0x28, prediction));
            native::SprintInput(frame.context, nullptr);
            CHECK(native::MovementAxes().sprint && frame.context.rax == 0);
            const std::uintptr_t live = 0;
            CHECK(gloss::gameplay::Write(frame.context.rsp() + 0x28, live));
            native::SprintInput(frame.context, nullptr);
            CHECK(!native::MovementAxes().sprint); // Match the DB-selected callback, not hardcoded key/event.
        }
        inputManaged = other.Managed(); frame.context.resume = 0x7777;
        native::SprintInput(frame.context, nullptr);
        CHECK(frame.context.resume == 0x7777 && !native::MovementAxes().sprint);
        inputManaged = player.Managed();
        CHECK(native::SetInputOwner({})); frame.context.resume = 0x7777;
        native::SprintInput(frame.context, nullptr);
        CHECK(frame.context.resume == 0x7777 && !native::MovementAxes().sprint); // Ordinary retained-player behavior is untouched.
        CHECK(native::SetInputOwner({2, 9}));
        std::thread foreign([&] { native::SprintInput(frame.context, nullptr); }); foreign.join();
        CHECK(frame.context.resume == 0x7777 && !native::MovementAxes().sprint);
        native::SprintInput(frame.context, nullptr);
        CHECK(native::MovementAxes().sprint);
        std::thread resetForeign([] { native::ResetSprintInput(); }); resetForeign.join();
        CHECK(native::MovementAxes().sprint);
        native::ready.store(false); native::ResetSprintInput(); native::ready.store(true);
        CHECK(!native::MovementAxes().sprint); // Cleanup remains usable if the adapter has failed.
        native::SprintInput(frame.context, nullptr); CHECK(native::MovementAxes().sprint);
        CHECK(native::SetProxy({1, 7})); CHECK(!native::MovementAxes().sprint && !native::movement.sprint);
    }
    void TestMappedSprintRejectsUnknownDescriptorsAndReentrantLifetimes()
    {
        for (int failure = 0; failure < 12; ++failure)
        {
            Reset(); CHECK(native::SetInputOwner({2, 9}));
            struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
            frame.context.rdx = 42; frame.context.r8 = 2;
            inputParameter = 2; inputSpeedMode = 2;
            native::SprintInput(frame.context, nullptr); CHECK(native::MovementAxes().sprint);
            native::ResetSprintInput();
            if (failure == 0) inputParameter = 1; // Walk cannot modify the hidden player.
            if (failure == 1) inputParameter = 9;
            if (failure == 2) frame.context.rdx = 43;
            if (failure == 3) parameters[0x18 / 8] = 1;
            if (failure == 4) parameters[0x18 / 8] = 33;
            if (failure == 5) descriptor[0x20 / 8] = 0;
            if (failure >= 6 && failure <= 10) sprintInputFailureStage = failure - 5;
            if (failure == 11) battleAllowed = false;
            frame.context.resume = 0x7777;
            native::SprintInput(frame.context, nullptr);
            CHECK(!native::MovementAxes().sprint && !native::movement.sprint);
            CHECK(frame.context.resume == native::returnStub && frame.context.rax == 0);
            CHECK(!gcUnsafe && gcEntries.empty() && gcEnterCalls == gcExitCalls);
        }
        Reset(); CHECK(native::SetInputOwner({2, 9}));
        struct Frame { Context context{}; std::array<std::byte, 64> stack{}; } frame;
        frame.context.rdx = 42; frame.context.r8 = 2; inputParameter = 2;
        native::sprintInputReady.store(false); frame.context.resume = 0x7777;
        native::SprintInput(frame.context, nullptr);
        CHECK(frame.context.resume == 0x7777 && !native::MovementAxes().sprint);
        CHECK(native::Ready()); // Optional input failure cannot disable transformation/facing.
    }
    void TestSprintOptionalHookFingerprintAndInstallationIsolation()
    {
        CHECK(std::strlen(native::sprintInputFingerprint) == 1076 * 3 - 1);
        for (int failure = 0; failure < 4; ++failure)
        {
            Reset(); simulateHookInstall = true;
            CHECK(native::InstallHooks() && native::Hooks().size() == 21);
            std::array<std::uint8_t, 1076> code{};
            sprintMethodAddress = reinterpret_cast<std::uintptr_t>(code.data());
            native::methods.firstParam = sprintMethodAddress + 0x300;
            for (const auto call : {0x12c, 0x153})
            {
                code[call] = 0xe8;
                const auto relative = static_cast<std::int32_t>(0x300 - (call + 5));
                std::memcpy(code.data() + call + 1, &relative, sizeof(relative));
            }
            if (failure == 0) rejectedFingerprint = sprintMethodAddress;
            if (failure == 1) code[0x153] = 0;
            if (failure == 2) failHookAt = hookCreateCalls;
            CHECK(native::InstallSprintInputHook() == (failure == 3));
            CHECK(native::sprintInputReady.load() == (failure == 3));
            CHECK(native::Ready() && native::Hooks().size() == (failure == 3 ? 22 : 21));
            if (failure == 3) CHECK(native::Hooks().back()->callback == native::SprintInput);
        }
    }
}
int main()
{
    TestMotionMatchingSelectorOptionalFingerprintIsolation();
    TestMotionMatchingSelectorUsesOnlyRoleSpecificValidSpeedStates();
    TestMotionMatchingSelectorFreeInputFallsBackToOwnLockStates();
    TestMotionMatchingSelectorFreeInputPrefersFreeAndRejectsDifferentSpeeds();
    TestMotionMatchingSelectorSecondQueryFailuresRemainUnknown();
    TestMotionMatchingSelectorMissingDataAndFailuresRemainUnknown();
    TestMotionMatchingSelectorRejectsReferenceAndLifetimeReplacement();
    TestAnimationProbeOptionalFingerprintsNeverDisableGameplay();
    TestAnimationProbeReadsOnlyExistingCachesAndExactByteModes();
    TestAnimationProbeFailuresStayUnknownAndNeverBecomeNone();
    TestAnimationProbeRejectsReplacedReferencesAndInvalidSessions();
    TestPlayerLeasesRemoveOnlyOwnedHandlesIncludingSameValueStoryRequests();
    TestPlayerHiddenShadowPreservesDirectSameValueWritesAndExternalOrdering();
    TestPlayerLeaseRefreshHidesLastAfterMovementCallbacks();
    TestPlayerLeaseRefreshPreservesExternalPriorityAndDeduplicatesDiagnostics();
    TestPlayerLeaseRefreshDiagnosticsIdentifyFailuresWithoutRecoveryWrites();
    TestPlayerLeaseFailuresRetryRecalculationAndRejectInvalidLifetimes();
    TestPlayerLeaseStopsAtSynchronousEndPlay();
    TestDuplicatePropertySubscribersFailActivationAndRetainExactRecoverableHandles();
    TestWeaponLocalInitializationPreservesSlotsDefaultsAndSessionIdentity();
    TestWeaponInitializationRejectsUnknownOwnershipAndInvalidBoundaries();
    TestWeaponManagedAbiValidatesBeforeWritesAndNeverReplaysAmbiguousCalls();
    TestWeaponFailureDiagnosticsPreserveRejectionAndIdentifyStage();
    TestWeaponReadinessWaitsForAllChildrenBeforeAnyMutation();
    TestWeaponReadinessRejectsMissingSubscribersAndInvalidLifetimes();
    TestWeaponPostcheckFailureNeverCompletesOrReplays();
    TestManagedRuntimeRequiresCompleteUniqueSameImageEntries();
    TestManagedOperationsAreContinuousAndRestorePreviousGcState();
    TestManagedRegionRejectsUnavailableAndUnwindsFailures();
    TestCleanupRetainsManagedRegionThroughSynchronousCallbacks();
    TestCooperativeHookContractsAndAtomicInstallation();
    TestEveryCooperativeHookIsScopedToSessionObjects();
    TestIsolationAndWeakSerialReuse();
    TestManagedIdentityAndOwnedWrites();
    TestClearTargetManagedGetterAbiAndFailures();
    TestLocomotionFacingDispatchUsesHorizontalYawAndRestoresGc();
    TestLocomotionFacingRejectsReusedIdentityAndReentrantSessionChanges();
    TestOwnedChildIsolationAndDeathCleanup();
    TestChildOwnershipRejectsCyclesForeignActorsAndReuse();
    TestChildOwnerTraversalBound();
    TestSynchronousCleanupScopeAndReuse();
    TestMappedActionFilteringAndCancellation();
    TestMappedSkillSlotsAndUnknownActions();
    TestReleaseInputPreservesProxyIsolation();
    TestMappedMovementExpiryAndRelease();
    TestMappedSprintHoldReleaseResetAndPrediction();
    TestMappedSprintToggleFollowsConfiguredEventsAndRejectsForeignActors();
    TestMappedSprintRejectsUnknownDescriptorsAndReentrantLifetimes();
    TestSprintOptionalHookFingerprintAndInstallationIsolation();
    CHECK(!gcUnsafe && gcEntries.empty());
    std::cout << "Transformation native tests: " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
