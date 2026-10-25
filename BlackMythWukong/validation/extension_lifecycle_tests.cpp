#include "../src/gameplay/engine_bridge.hpp"
#include "../src/gameplay/additional_hooks.hpp"
#include "../src/runtime.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Compile the real session/queue implementation once in this translation unit.
// Only external engine boundaries and the clock/input observations are mocked.
static ULONGLONG LifecycleTickCount();
static SHORT LifecycleKeyState(int);
#define GetTickCount64 LifecycleTickCount
#define GetAsyncKeyState LifecycleKeyState
#include "../src/gameplay/extensions.cpp"
#undef GetAsyncKeyState
#undef GetTickCount64

namespace ex = gloss::gameplay::extensions;
namespace eng = gloss::gameplay::engine;
using eng::Arg;
using gloss::features::Id;

namespace fixture
{
    enum class Kind
    {
        Pawn,
        Controller,
        World,
        Movement,
        Widget,
        Arm,
        Player,
        Manager,
        DebugController
    };
    struct Object
    {
        Kind kind;
        std::uintptr_t world{};
        std::uintptr_t owner{};
        bool alive = true;
        std::int32_t serial = 1;
        std::map<std::string, Arg> properties;
    };
    constexpr std::uintptr_t Pawn = 0x1000, Controller = 0x2000, OtherController = 0x2100,
                             World = 0x3000, OtherWorld = 0x3100, Movement = 0x4000, NewMovement = 0x4100,
                             Root = 0x5000, NewRoot = 0x5100, CollapsedRoot = 0x5200, HiddenRoot = 0x5300, ChildWidget = 0x5400,
                             Arm = 0x6000, NewArm = 0x6100,
                             LocalPlayer = 0x7000, DebugController = 0x8000, ArmClass = 0xa000, ManagerClass = 0xa100, WidgetClass = 0xa200;
    std::unordered_map<std::uintptr_t, Object> objects;
    std::uintptr_t firstController{}, possessedPawn{}, currentArm{}, nextObject{};
    std::vector<std::uintptr_t> topWidgets;
    std::map<std::uintptr_t, int> failWidgetReads, failWidgetWrites, widgetWriteAttempts;
    bool weakReady{};
    bool updateBlocked{};
    std::uint64_t updateGeneration{};
    bool transformationExclusive{};
    bool transformationControlsAvailable{};
    int transformationTicks{};
    int widgetDiscoveries{}, invalidWidgetCalls{};
    ULONGLONG now{};
    float dilation{};
    bool paused{};
    int failDilationWrites{}, failPauseWrites{}, gmCalls{}, consoleCalls{}, pawnReads{};
    std::uintptr_t disableManager{}, disableManagerOwner{};
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> captured;
    std::map<Id, bool> extras;
    std::vector<std::pair<Id, gloss::features::Result>> completed;
    std::map<std::pair<std::uintptr_t, std::uint8_t>, bool> states;
    std::map<std::pair<std::uintptr_t, std::string>, int> writes;
    int failures{};

#define CHECK(...)                                                                         \
    do                                                                                     \
    {                                                                                      \
        if (!(__VA_ARGS__))                                                                \
        {                                                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #__VA_ARGS__ "\n"; \
            ++fixture::failures;                                                           \
        }                                                                                  \
    } while (false)

    void Add(std::uintptr_t p, Kind kind, std::uintptr_t world = World, std::uintptr_t owner = 0)
    {
        objects.emplace(p, Object{kind, world, owner});
    }
    void Put(std::uintptr_t p, Arg value) { objects.at(p).properties.insert_or_assign(value.name, std::move(value)); }
    void AddWidget(std::uintptr_t p, std::uint8_t visibility, bool topLevel = true, std::uintptr_t world = World)
    {
        Add(p, Kind::Widget, world);
        Put(p, Arg::Enum("Visibility", visibility));
        if (topLevel)
            topWidgets.push_back(p);
    }
    template <class T>
    T Get(std::uintptr_t p, const char *name) { return objects.at(p).properties.at(name).Get<T>(); }
    bool Alive(std::uintptr_t p)
    {
        const auto i = objects.find(p);
        return i != objects.end() && i->second.alive;
    }
    Arg &Named(std::span<Arg> args, const char *name)
    {
        const auto i = std::find_if(args.begin(), args.end(), [&](const auto &a)
                                    { return std::string_view(a.name) == name; });
        if (i == args.end())
            throw std::runtime_error(std::string("Unexpected mock argument: ") + name);
        return *i;
    }
    template <class T>
    void Output(std::span<Arg> args, T value)
    {
        auto &out = Named(args, "ReturnValue");
        out.bytes.resize(sizeof(T));
        std::memcpy(out.bytes.data(), &value, sizeof(T));
    }
    bool Ends(std::wstring_view path, std::wstring_view suffix) { return path.ends_with(suffix); }

    void Reset()
    {
        ex::controls = {};
        ex::status = {};
        ex::bookmarks.clear();
        ex::request.reset();
        ex::candidate = 0;
        ex::player = ex::controller = ex::worldContext = 0;
        ex::generation = 0;
        ex::registered = false;
        ex::captureAt = ex::lastProbe = 0;
        ex::inFrame = false;
        ex::spaceDown = false;
        ex::observedUpdateGeneration = 0;
        ex::updateRestores = {};
        ex::lastUpdateRestoreAttempt = 0;
        updateBlocked = false;
        updateGeneration = 0;
        transformationExclusive = false;
        transformationControlsAvailable = false;
        transformationTicks = 0;
        ex::recoveries.clear();
        ex::recovering = false;
        ex::states.clear();
        ex::fields.clear();
        ex::hiddenWidgets.clear();
        ex::lastWidgetDiscovery = 0;
        ex::pausedOwned = ex::pausedBaseline = false;
        ex::pausedPlayer = 0;
        ex::dilationOwned = false;
        ex::dilationBaseline = 1;
        ex::cheatManager = ex::originalCheatManager = ex::cameraController = 0;
        ex::debugController = ex::debugManager = ex::originalDebugManager = 0;
        ex::cameraOwned = false;
        objects.clear();
        captured.clear();
        extras.clear();
        completed.clear();
        states.clear();
        writes.clear();
        topWidgets.clear();
        failWidgetReads.clear();
        failWidgetWrites.clear();
        widgetWriteAttempts.clear();
        weakReady = true;
        widgetDiscoveries = invalidWidgetCalls = 0;
        firstController = Controller;
        possessedPawn = Pawn;
        currentArm = Arm;
        nextObject = 0xb000;
        now = 1000;
        dilation = 1.f;
        paused = false;
        failDilationWrites = failPauseWrites = 0;
        gmCalls = consoleCalls = pawnReads = 0;
        disableManager = disableManagerOwner = 0;
        Add(Pawn, Kind::Pawn);
        Add(Controller, Kind::Controller);
        Add(OtherController, Kind::Controller);
        Add(World, Kind::World);
        Add(OtherWorld, Kind::World);
        Add(Movement, Kind::Movement);
        Add(NewMovement, Kind::Movement);
        AddWidget(Root, 0);
        Add(Arm, Kind::Arm);
        Add(NewArm, Kind::Arm);
        Add(LocalPlayer, Kind::Player);
        Put(Pawn, Arg::Object("CharacterMovement", Movement));
        Put(Pawn, Arg::Boolean("bHidden", false));
        Put(Movement, Arg::Float("GravityScale", 1.f));
        Put(NewMovement, Arg::Float("GravityScale", .35f));
        Put(Arm, Arg::Float("TargetArmLength", 350.f));
        Put(NewArm, Arg::Float("TargetArmLength", 230.f));
        Put(Controller, Arg::Object("CheatManager", 0));
        Put(OtherController, Arg::Object("CheatManager", 0));
        ex::CapturePlayer(Pawn);
        ex::Frame();
        CHECK(ex::GetStatus().ready);
        CHECK(ex::player == Pawn);
        CHECK(ex::controller == Controller);
    }
    void Frame()
    {
        ++now;
        ex::Frame();
    }
    void Toggle(Id id, bool enabled, double value = 0)
    {
        const auto queued = ex::SetEnabled(id, enabled, value);
        CHECK(queued.success);
        CHECK(ex::GetStatus().pending);
        Frame();
        CHECK(!ex::GetStatus().pending);
        CHECK(!completed.empty());
        if (!completed.empty())
            CHECK(completed.back().second.success);
        CHECK(ex::Query(id).enabled == enabled);
    }
    void CheckAllDisabled()
    {
        for (const auto &c : ex::controls)
            CHECK(!c.enabled);
    }

    void UpdateBarrierRestoresMultipleSwitchesAndCancelsOldAction()
    {
        Reset();
        Toggle(Id::NoGravity, true);
        Toggle(Id::HidePlayer, true);
        Toggle(Id::BulletTime, true, .25);
        CHECK(Get<float>(Movement, "GravityScale") == 0);
        CHECK(Get<std::int32_t>(Pawn, "bHidden"));
        CHECK(dilation == .25f);
        CHECK(ex::ChangeItem(0, 1, false).success);
        CHECK(ex::GetStatus().pending);
        updateBlocked = true;
        ++updateGeneration;
        // These three calls all succeed without competing for the one normal slot.
        CHECK(ex::SetEnabled(Id::NoGravity, false, 0).success);
        CHECK(ex::SetEnabled(Id::HidePlayer, false, 0).success);
        CHECK(ex::SetEnabled(Id::BulletTime, false, .25).success);
        CHECK(!ex::ChangeItem(0, 1, false).success);
        Frame();
        CheckAllDisabled();
        CHECK(!ex::GetStatus().pending);
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);
        CHECK(!Get<std::int32_t>(Pawn, "bHidden"));
        CHECK(dilation == 1.f);
        CHECK(gmCalls == 0);
        updateBlocked = false;
        Frame();
        CheckAllDisabled();
        CHECK(ex::ChangeItem(0, 1, false).success);
        Frame();
        CHECK(gmCalls == 1);
    }

    void TransformationExclusivitySuspendsConflictsAndRestoresThem()
    {
        Reset();
        Toggle(Id::HidePlayer, true);
        Toggle(Id::NoGravity, true);
        Toggle(Id::FreezeWorld, true);
        Toggle(Id::PauseBackground, true);
        CHECK(Get<std::int32_t>(Pawn, "bHidden"));
        CHECK(Get<float>(Movement, "GravityScale") == 0.f);
        CHECK(paused);

        transformationExclusive = true;
        Frame();
        CHECK(transformationTicks > 0);
        CHECK(transformationControlsAvailable);
        CHECK(!ex::Query(Id::HidePlayer).enabled);
        CHECK(!ex::Query(Id::NoGravity).enabled);
        CHECK(!Get<std::int32_t>(Pawn, "bHidden"));
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);
        CHECK(!paused);
        CHECK(!ex::SetEnabled(Id::HidePlayer, true, 0).success);

        transformationExclusive = false;
        Frame();
        CHECK(ex::Query(Id::HidePlayer).enabled);
        CHECK(ex::Query(Id::NoGravity).enabled);
        CHECK(Get<std::int32_t>(Pawn, "bHidden"));
        CHECK(Get<float>(Movement, "GravityScale") == 0.f);
        CHECK(paused);
    }

    void TransformationCancelsAlreadyQueuedConflictingEnable()
    {
        Reset();
        CHECK(ex::SetEnabled(Id::NoGravity, true, 0).success);
        CHECK(ex::GetStatus().pending);
        CHECK(!ex::Query(Id::NoGravity).enabled);

        // The queued request has not touched the game yet. If transformation
        // becomes exclusive before the game-thread frame drains it, reject it.
        transformationExclusive = true;
        Frame();
        CHECK(transformationTicks > 0);
        CHECK(!ex::GetStatus().pending);
        CHECK(!ex::Query(Id::NoGravity).enabled);
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);
        CHECK(!completed.empty());
        if (!completed.empty())
        {
            CHECK(completed.back().first == Id::NoGravity);
            CHECK(!completed.back().second.success);
        }

        transformationExclusive = false;
        Frame();
        CHECK(!ex::Query(Id::NoGravity).enabled);
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);

        transformationExclusive = true;
        CHECK(ex::Queue([]
                        {
                            ++gmCalls;
                            return gloss::features::Result{true, "restoration action"};
                        },
                        Id::HidePlayer, false, true)
                  .success);
        Frame();
        CHECK(!ex::GetStatus().pending);
        CHECK(gmCalls == 1); // Restoration work is allowed during exclusive control.
    }

    void UpdateBarrierCancelsQueuedEnableAcrossFailedRetry()
    {
        Reset();
        CHECK(ex::SetEnabled(Id::NoGravity, true, 0).success);
        CHECK(!ex::Query(Id::NoGravity).enabled);
        CHECK(ex::GetStatus().pending);
        updateBlocked = true;
        ++updateGeneration;
        updateBlocked = false;                       // GitHub retry failed before the next game frame.
        CHECK(!ex::ChangeItem(0, 1, false).success); // The old generation has not been drained yet.
        Frame();
        CHECK(!ex::GetStatus().pending);
        CheckAllDisabled();
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);
        CHECK(!completed.empty());
        if (!completed.empty())
            CHECK(!completed.back().second.success);
        Toggle(Id::NoGravity, true);
        Toggle(Id::NoGravity, false);
    }

    void UpdateBarrierAllowsQueuedDisable()
    {
        Reset();
        Toggle(Id::NoGravity, true);
        CHECK(ex::SetEnabled(Id::NoGravity, false, 0).success);
        updateBlocked = true;
        ++updateGeneration;
        Frame();
        CHECK(!ex::GetStatus().pending);
        CheckAllDisabled();
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);
        CHECK(!completed.empty());
        if (!completed.empty())
            CHECK(completed.back().second.success);
    }

    void UpdateRestorationRetriesAfterUnlock()
    {
        Reset();
        Toggle(Id::BulletTime, true, .25);
        Toggle(Id::HidePlayer, true);
        failDilationWrites = 2;
        updateBlocked = true;
        ++updateGeneration;
        Frame();
        CHECK(ex::Query(Id::BulletTime).enabled);
        CHECK(!ex::Query(Id::HidePlayer).enabled);
        CHECK(dilation == .25f);
        CHECK(!Get<std::int32_t>(Pawn, "bHidden"));
        CHECK(failDilationWrites == 1);
        updateBlocked = false;
        CHECK(!ex::ChangeItem(0, 1, false).success);
        Frame();
        CHECK(failDilationWrites == 1); // Retry is bounded to 100 ms, not every render/game frame.
        now += 100;
        Frame();
        CHECK(failDilationWrites == 0);
        CHECK(ex::Query(Id::BulletTime).enabled);
        now += 100;
        Frame();
        CHECK(!ex::Query(Id::BulletTime).enabled);
        CHECK(dilation == 1.f);
        CHECK(ex::ChangeItem(0, 1, false).success);
        Frame();
        CHECK(gmCalls == 1);
        CheckAllDisabled();
    }

    void UpdateArrivingDuringWorkRestoresItsResult()
    {
        Reset();
        CHECK(ex::Queue([]
                        {
        updateBlocked=true;++updateGeneration;
        return ex::Toggle(Id::HidePlayer,true,0); }, Id::HidePlayer)
                  .success);
        Frame();
        CHECK(!ex::Query(Id::HidePlayer).enabled);
        CHECK(!Get<std::int32_t>(Pawn, "bHidden"));
        CHECK(!ex::GetStatus().pending);
        updateBlocked = false;
        Frame();
        CheckAllDisabled();
    }

    void ComponentReplacementRestoresEveryBaseline()
    {
        Reset();
        Toggle(Id::NoGravity, true);
        CHECK(Get<float>(Movement, "GravityScale") == 0);
        Put(Pawn, Arg::Object("CharacterMovement", NewMovement));
        Toggle(Id::NoGravity, true);
        CHECK(Get<float>(NewMovement, "GravityScale") == 0);
        CHECK(ex::fields.size() == 2);
        Toggle(Id::NoGravity, false);
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);
        CHECK(Get<float>(NewMovement, "GravityScale") == .35f);
        CHECK(ex::fields.empty());

        // Destroyed components are discarded without dereferencing their properties.
        Toggle(Id::NoGravity, true);
        objects.at(NewMovement).alive = false;
        Put(Pawn, Arg::Object("CharacterMovement", Movement));
        Toggle(Id::NoGravity, true);
        const auto deadWrites = writes[{NewMovement, "GravityScale"}];
        Toggle(Id::NoGravity, false);
        CHECK(writes[{NewMovement, "GravityScale"}] == deadWrites);
        CHECK(ex::fields.empty());
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);
        std::cout << "PASS component replacement restores all live baselines\n";
    }

    void CameraDistancePreservesLatestGameValue()
    {
        Reset();
        Toggle(Id::FixedCameraDistance, true, 555);
        CHECK(Get<float>(Arm, "TargetArmLength") == 555.f);
        Put(Arm, Arg::Float("TargetArmLength", 420.f));
        Frame();
        CHECK(Get<float>(Arm, "TargetArmLength") == 555.f);
        Toggle(Id::FixedCameraDistance, false, 555);
        CHECK(Get<float>(Arm, "TargetArmLength") == 420.f);
        CHECK(ex::fields.empty());

        Toggle(Id::FixedCameraDistance, true, 555);
        currentArm = NewArm;
        Frame();
        CHECK(Get<float>(Arm, "TargetArmLength") == 420.f);
        CHECK(Get<float>(NewArm, "TargetArmLength") == 555.f);
        CHECK(ex::fields.size() == 1);
        CHECK(ex::fields.front().object == NewArm);
        Put(NewArm, Arg::Float("TargetArmLength", 260.f));
        Frame();
        Toggle(Id::FixedCameraDistance, false, 555);
        CHECK(Get<float>(NewArm, "TargetArmLength") == 260.f);

        // An engine update arriving immediately before close must not be overwritten.
        Toggle(Id::FixedCameraDistance, true, 555);
        Put(NewArm, Arg::Float("TargetArmLength", 275.f));
        Toggle(Id::FixedCameraDistance, false, 555);
        CHECK(Get<float>(NewArm, "TargetArmLength") == 275.f);
        std::cout << "PASS camera replacement and latest engine baseline restoration\n";
    }

    void ControllerReplacementCancelsOldWork()
    {
        Reset();
        Toggle(Id::NoGravity, true);
        Toggle(Id::BulletTime, true, .25);
        Toggle(Id::FreezeWorld, true);
        Toggle(Id::InfiniteItems, true);
        CHECK(dilation == .25f);
        CHECK(paused);
        CHECK(extras[Id::InfiniteItems]);
        ex::status.positionValid = true;
        ex::status.objects.push_back({0x9999, "old", {1, 2, 3}});
        const auto oldGeneration = ex::generation;
        CHECK(ex::SetEnabled(Id::HideHud, true, 0).success);
        firstController = OtherController;
        Frame();
        CHECK(ex::generation == oldGeneration + 1);
        CHECK(ex::player == Pawn);
        CHECK(ex::controller == OtherController);
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);
        CHECK(dilation == 1.f);
        CHECK(!paused);
        CHECK(!extras[Id::InfiniteItems]);
        CheckAllDisabled();
        CHECK(!ex::GetStatus().pending);
        CHECK(!ex::GetStatus().positionValid);
        CHECK(ex::GetStatus().objects.empty());
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 0);
        CHECK(widgetDiscoveries == 0);
        CHECK(std::any_of(completed.begin(), completed.end(), [](const auto &c)
                          { return c.first == Id::HideHud && !c.second.success; }));
        CHECK(!captured.empty());
        CHECK(captured.back() == std::pair(Pawn, OtherController));
        CHECK(std::find(captured.begin(), captured.end(), std::pair(std::uintptr_t{}, std::uintptr_t{})) != captured.end());
        std::cout << "PASS same pawn with replacement controller cancels stale work\n";
    }

    void LostSessionRestoresAndCancels()
    {
        Reset();
        Toggle(Id::NoGravity, true);
        Toggle(Id::Stealth, true);
        Toggle(Id::BulletTime, true, .3);
        Toggle(Id::FreezeWorld, true);
        Toggle(Id::InfiniteItems, true);
        CHECK(states[{Pawn, std::uint8_t{66}}]);
        const auto oldGeneration = ex::generation;
        CHECK(ex::ChangeItem(0, 1, false).success);
        CHECK(ex::GetStatus().pending);
        ex::candidate = 0;
        Frame();
        CHECK(!ex::GetStatus().pending);
        CHECK(!ex::GetStatus().ready);
        CHECK(gmCalls == 0);
        CHECK(ex::player == 0);
        CHECK(ex::controller == 0);
        CHECK(ex::worldContext == 0);
        CHECK(ex::generation == oldGeneration + 1);
        CHECK(ex::fields.empty());
        CHECK(ex::states.empty());
        CHECK(Get<float>(Movement, "GravityScale") == 1.f);
        CHECK(!states[{Pawn, std::uint8_t{66}}]);
        CHECK(dilation == 1.f);
        CHECK(!paused);
        CHECK(!extras[Id::InfiniteItems]);
        CheckAllDisabled();
        CHECK(captured.back() == std::pair(std::uintptr_t{}, std::uintptr_t{}));
        CHECK(!ex::ChangeItem(0, 1, false).success);
        CHECK(ex::Query(Id::NoGravity).availability == gloss::features::Availability::Waiting);
        std::cout << "PASS invalid session cancels queued actions and restores owned effects\n";
    }

    void FailedGlobalRestoreRetriesOnOldWorld()
    {
        Reset();
        dilation = .8f;
        paused = false;
        Toggle(Id::BulletTime, true, .2);
        Toggle(Id::FreezeWorld, true);
        failDilationWrites = 2;
        failPauseWrites = 2;
        ex::candidate = 0;
        Frame();
        CHECK(ex::player == 0);
        CHECK(ex::recovering);
        CHECK(ex::recoveries.size() == 1);
        CHECK(dilation == .2f);
        CHECK(paused);
        CHECK(!ex::ChangeItem(0, 1, false).success);
        CHECK(ex::recoveries.front().world == World);
        Frame();
        CHECK(dilation == .8f);
        CHECK(!paused);
        CHECK(ex::recoveries.empty());
        CHECK(!ex::recovering);

        // A pre-existing pause is a baseline, not a temporary state owned by us.
        Reset();
        paused = true;
        Toggle(Id::FreezeWorld, true);
        ex::candidate = 0;
        Frame();
        CHECK(paused);
        std::cout << "PASS failed world restoration retries and preserves pre-existing pause\n";
    }

    void WidgetDiscoveryRespectsExistingHiddenRoots()
    {
        Reset();
        AddWidget(CollapsedRoot, 1);
        AddWidget(HiddenRoot, 2);
        AddWidget(ChildWidget, 0, false);
        Toggle(Id::HideHud, true);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 2);
        CHECK(Get<std::uint8_t>(CollapsedRoot, "Visibility") == 1);
        CHECK(Get<std::uint8_t>(HiddenRoot, "Visibility") == 2);
        CHECK(Get<std::uint8_t>(ChildWidget, "Visibility") == 0);
        CHECK(ex::hiddenWidgets.size() == 1);
        CHECK(widgetDiscoveries == 1);
        CHECK(widgetWriteAttempts[CollapsedRoot] == 0);
        CHECK(widgetWriteAttempts[HiddenRoot] == 0);
        CHECK(widgetWriteAttempts[ChildWidget] == 0);

        AddWidget(NewRoot, 4);
        Frame();
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 4);
        CHECK(widgetDiscoveries == 1);
        now += 500;
        Frame();
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 2);
        CHECK(widgetDiscoveries == 2);
        CHECK(ex::hiddenWidgets.size() == 2);
        Toggle(Id::HideHud, false);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 0);
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 4);
        CHECK(Get<std::uint8_t>(CollapsedRoot, "Visibility") == 1);
        CHECK(Get<std::uint8_t>(HiddenRoot, "Visibility") == 2);
        CHECK(ex::hiddenWidgets.empty());
        std::cout << "PASS UMG top-level discovery, new roots, and existing hidden/collapsed roots\n";
    }

    void WidgetRestoreUsesLatestGameVisibility()
    {
        Reset();
        Toggle(Id::HideHud, true);
        Put(Root, Arg::Enum("Visibility", 3));
        Frame();
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 2);
        CHECK(ex::hiddenWidgets.front().original == 3);
        Toggle(Id::HideHud, false);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 3);

        Toggle(Id::HideHud, true);
        Put(Root, Arg::Enum("Visibility", 1));
        const auto beforeCollapse = widgetWriteAttempts[Root];
        Frame();
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 1);
        CHECK(widgetWriteAttempts[Root] == beforeCollapse);
        CHECK(ex::hiddenWidgets.front().original == 1);
        Toggle(Id::HideHud, false);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 1);

        Put(Root, Arg::Enum("Visibility", 0));
        Toggle(Id::HideHud, true);
        // A game update directly before closing belongs to the game and is retained.
        Put(Root, Arg::Enum("Visibility", 4));
        Toggle(Id::HideHud, false);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 4);
        CHECK(ex::hiddenWidgets.empty());
        std::cout << "PASS UMG restores the latest game visibility and preserves layout collapse\n";
    }

    void WidgetWeakIdentityRejectsDestroyedAndReusedRoots()
    {
        Reset();
        Toggle(Id::HideHud, true);
        const auto oldWeak = eng::MakeWeak(Root);
        const auto beforeDestroy = widgetWriteAttempts[Root];
        objects.at(Root).alive = false;
        Frame();
        CHECK(eng::ResolveWeak(oldWeak) == 0);
        CHECK(ex::hiddenWidgets.empty());
        CHECK(widgetWriteAttempts[Root] == beforeDestroy);
        CHECK(invalidWidgetCalls == 0);
        Toggle(Id::HideHud, false);

        // Reuse precisely the same address/index with a new serial. The old weak
        // snapshot must never restore its visibility into the replacement root.
        objects.at(Root).alive = true;
        ++objects.at(Root).serial;
        Put(Root, Arg::Enum("Visibility", 4));
        CHECK(eng::ResolveWeak(oldWeak) == 0);
        CHECK(eng::MakeWeak(Root).serial != oldWeak.serial);
        Toggle(Id::HideHud, true);
        const auto secondWeak = eng::MakeWeak(Root);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 2);
        ++objects.at(Root).serial;
        Put(Root, Arg::Enum("Visibility", 3));
        const auto beforeReuse = widgetWriteAttempts[Root];
        Toggle(Id::HideHud, false);
        CHECK(eng::ResolveWeak(secondWeak) == 0);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 3);
        CHECK(widgetWriteAttempts[Root] == beforeReuse);
        CHECK(ex::hiddenWidgets.empty());

        Toggle(Id::HideHud, true);
        Toggle(Id::HideHud, false);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 3);
        CHECK(invalidWidgetCalls == 0);
        std::cout << "PASS UMG destroyed roots and same-address serial replacement reject stale restoration\n";
    }

    void WidgetCloseRetriesOnlyFailedRoots()
    {
        Reset();
        AddWidget(NewRoot, 4);
        Toggle(Id::HideHud, true);
        failWidgetWrites[Root] = 1;
        const auto beforeClose = completed.size();
        CHECK(ex::SetEnabled(Id::HideHud, false, 0).success);
        Frame();
        CHECK(completed.size() == beforeClose + 1);
        CHECK(completed.back().second.success);
        CHECK(!ex::Query(Id::HideHud).enabled);
        CHECK(!ex::GetStatus().pending);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 2);
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 4);
        CHECK(ex::hiddenWidgets.size() == 1);
        CHECK(ex::Query(Id::HideHud).message == "已关闭，正在恢复界面");
        const auto restoredWrites = widgetWriteAttempts[NewRoot];
        Frame();
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 0);
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 4);
        CHECK(widgetWriteAttempts[NewRoot] == restoredWrites);
        CHECK(ex::hiddenWidgets.empty());
        CHECK(ex::Query(Id::HideHud).message == "已关闭并恢复");
        std::cout << "PASS UMG accepted close retries failed live roots without rehiding restored roots\n";
    }

    void WidgetSessionTransitionRestoresPreviousWorld()
    {
        Reset();
        AddWidget(NewRoot, 3, true, OtherWorld);
        Toggle(Id::HideHud, true);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 2);
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 3);
        failWidgetWrites[Root] = 1;
        objects.at(Pawn).world = OtherWorld;
        objects.at(Controller).world = OtherWorld;
        Frame();
        CHECK(ex::worldContext == OtherWorld);
        CHECK(!ex::Query(Id::HideHud).enabled);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 2);
        CHECK(ex::hiddenWidgets.size() == 1);
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 3);
        Frame();
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 0);
        CHECK(ex::hiddenWidgets.empty());
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 3);
        Toggle(Id::HideHud, true);
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 0);
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 2);
        Toggle(Id::HideHud, false);
        CHECK(Get<std::uint8_t>(NewRoot, "Visibility") == 3);

        // Loss of the pawn must also restore live roots even before another world
        // has supplied a new playable session.
        Reset();
        Toggle(Id::HideHud, true);
        failWidgetWrites[Root] = 1;
        ex::candidate = 0;
        Frame();
        CHECK(ex::player == 0);
        CHECK(!ex::GetStatus().ready);
        CHECK(!ex::Query(Id::HideHud).enabled);
        CHECK(ex::hiddenWidgets.size() == 1);
        Frame();
        CHECK(Get<std::uint8_t>(Root, "Visibility") == 0);
        CHECK(ex::hiddenWidgets.empty());
        CHECK(invalidWidgetCalls == 0);
        std::cout << "PASS UMG scene transition and pawn loss restore old live roots on retry\n";
    }

    void FreeCameraCanCloseWithoutPawnCapture()
    {
        Reset();
        Toggle(Id::FreeCamera, true);
        CHECK(ex::cameraOwned);
        CHECK(firstController == DebugController);
        const auto originalManager = ex::cheatManager;
        const auto oldGeneration = ex::generation;
        const auto oldPawnReads = pawnReads;
        ex::candidate = 0;
        possessedPawn = 0;
        Frame();
        CHECK(ex::cameraOwned);
        CHECK(ex::player == Pawn);
        CHECK(ex::controller == Controller);
        CHECK(ex::worldContext == World);
        CHECK(ex::generation == oldGeneration);
        CHECK(pawnReads == oldPawnReads);
        CHECK(ex::GetStatus().ready);
        Toggle(Id::FreeCamera, false);
        CHECK(!ex::cameraOwned);
        CHECK(firstController == Controller);
        CHECK(possessedPawn == Pawn);
        CHECK(ex::generation == oldGeneration);
        CHECK(pawnReads == oldPawnReads);
        CHECK(disableManager != 0);
        CHECK(disableManager != originalManager);
        CHECK(disableManagerOwner == DebugController);
        CHECK(Get<std::uintptr_t>(Controller, "CheatManager") == 0);
        CHECK(!Alive(DebugController));
        CHECK(ex::cameraController == 0);
        CHECK(ex::debugController == 0);
        std::cout << "PASS free camera retains original identity and closes without a pawn\n";
    }
}

static ULONGLONG LifecycleTickCount() { return fixture::now; }
static SHORT LifecycleKeyState(int) { return 0; }

namespace gloss::gameplay::engine
{
    bool Initialize(HMODULE) { return true; }
    bool Ready() { return true; }
    bool IsGameThread() noexcept { return true; }
    bool RegisterFrame(void (*)()) noexcept { return true; }
    bool IsPawn(std::uintptr_t p) noexcept { return fixture::Alive(p) && fixture::objects.at(p).kind == fixture::Kind::Pawn; }
    bool IsActor(std::uintptr_t p) noexcept { return fixture::Alive(p); }
    bool WeakReady() { return fixture::weakReady; }
    WeakObject MakeWeak(std::uintptr_t p)
    {
        if (!fixture::weakReady || !fixture::Alive(p))
            return {};
        return {static_cast<std::int32_t>(p), fixture::objects.at(p).serial};
    }
    std::uintptr_t ResolveWeak(WeakObject weak)
    {
        if (!fixture::weakReady || weak.index < 0)
            return 0;
        const auto p = static_cast<std::uintptr_t>(weak.index);
        return fixture::Alive(p) && fixture::objects.at(p).serial == weak.serial ? p : 0;
    }
    bool IsA(std::uintptr_t p, const wchar_t *name) noexcept
    {
        if (!fixture::Alive(p))
            return false;
        const auto kind = fixture::objects.at(p).kind;
        if (std::wstring_view(name) == L"/Script/Engine.SpringArmComponent")
            return kind == fixture::Kind::Arm;
        if (std::wstring_view(name) == L"/Script/Engine.DebugCameraController")
            return kind == fixture::Kind::DebugController;
        if (std::wstring_view(name) == L"/Script/Engine.Player")
            return kind == fixture::Kind::Player;
        if (std::wstring_view(name) == L"/Script/UMG.UserWidget")
            return kind == fixture::Kind::Widget;
        return true;
    }
    std::uintptr_t Find(std::wstring_view name)
    {
        if (name == L"/Script/Engine.SpringArmComponent")
            return fixture::ArmClass;
        if (name == L"/Script/Engine.CheatManager")
            return fixture::ManagerClass;
        if (name == L"/Script/UMG.UserWidget")
            return fixture::WidgetClass;
        return 1;
    }
    std::uintptr_t Load(std::wstring_view, std::wstring_view) { return 0; }
    std::uintptr_t Construct(std::uintptr_t type, std::uintptr_t outer)
    {
        if (type != fixture::ManagerClass || !fixture::Alive(outer))
            return 0;
        const auto p = fixture::nextObject++;
        fixture::Add(p, fixture::Kind::Manager, fixture::World, outer);
        fixture::Put(p, Arg::Object("DebugCameraControllerRef", 0));
        return p;
    }
    std::uintptr_t World(std::uintptr_t p) { return fixture::Alive(p) ? fixture::objects.at(p).world : 0; }
    std::string ObjectName(std::uintptr_t) { return "test"; }
    bool Property(std::uintptr_t p, Arg &value, bool write)
    {
        if (!fixture::Alive(p))
            return false;
        auto &props = fixture::objects.at(p).properties;
        auto it = props.find(value.name);
        if (it == props.end())
            return false;
        if (write)
        {
            it->second = value;
            ++fixture::writes[{p, value.name}];
        }
        else
            value = it->second;
        return true;
    }
    CallResult Call(std::wstring_view path, std::span<Arg> args, std::uintptr_t instance)
    {
        using namespace fixture;
        if (Ends(path, L":GetFirstLocalPlayerController"))
        {
            Output(args, firstController);
            return {true, {}};
        }
        if (Ends(path, L":K2_GetPawn"))
        {
            ++pawnReads;
            Output(args, possessedPawn);
            return {true, {}};
        }
        if (Ends(path, L":GetAllWidgetsOfClass"))
        {
            ++widgetDiscoveries;
            CHECK(Named(args, "WorldContextObject").Get<std::uintptr_t>() == ex::worldContext);
            CHECK(Named(args, "WidgetClass").Get<std::uintptr_t>() == WidgetClass);
            CHECK(Named(args, "TopLevelOnly").Get<std::int32_t>() == 1);
            const auto context = Named(args, "WorldContextObject").Get<std::uintptr_t>();
            auto &found = Named(args, "FoundWidgets");
            CHECK(found.type == Type::ObjectArray);
            CHECK(found.output);
            found.bytes.clear();
            for (const auto p : topWidgets)
            {
                if (!Alive(p) || objects.at(p).world != context)
                    continue;
                const auto size = found.bytes.size();
                found.bytes.resize(size + sizeof(p));
                std::memcpy(found.bytes.data() + size, &p, sizeof(p));
            }
            return {true, {}};
        }
        if (Ends(path, L":GetVisibility") || Ends(path, L":SetVisibility"))
        {
            if (!Alive(instance) || objects.at(instance).kind != Kind::Widget)
            {
                ++invalidWidgetCalls;
                return {false, "invalid or destroyed widget"};
            }
            if (Ends(path, L":GetVisibility"))
            {
                auto &value = Named(args, "ReturnValue");
                CHECK(value.type == Type::Enum);
                CHECK(value.bytes.size() == 1);
                CHECK(value.output);
                if (failWidgetReads[instance] > 0)
                {
                    --failWidgetReads[instance];
                    return {false, "temporary visibility read failure"};
                }
                Output(args, Get<std::uint8_t>(instance, "Visibility"));
                return {true, {}};
            }
            ++widgetWriteAttempts[instance];
            const auto &value = Named(args, "InVisibility");
            CHECK(value.type == Type::Enum);
            CHECK(value.bytes.size() == 1);
            if (failWidgetWrites[instance] > 0)
            {
                --failWidgetWrites[instance];
                return {false, "temporary visibility write failure"};
            }
            Put(instance, Arg::Enum("Visibility", value.Get<std::uint8_t>()));
            return {true, {}};
        }
        if (Ends(path, L":GetComponentByClass"))
        {
            Output(args, currentArm);
            return {true, {}};
        }
        if (Ends(path, L":GetGlobalTimeDilation"))
        {
            Output(args, dilation);
            return {true, {}};
        }
        if (Ends(path, L":SetGlobalTimeDilation"))
        {
            CHECK(Named(args, "WorldContextObject").Get<std::uintptr_t>() == fixture::World);
            if (failDilationWrites > 0)
            {
                --failDilationWrites;
                return {false, "temporary world failure"};
            }
            dilation = Named(args, "TimeDilation").Get<float>();
            return {true, {}};
        }
        if (Ends(path, L":IsGamePaused"))
        {
            Output(args, std::int32_t(paused));
            return {true, {}};
        }
        if (Ends(path, L":SetGamePaused"))
        {
            CHECK(Named(args, "WorldContextObject").Get<std::uintptr_t>() == fixture::World);
            if (failPauseWrites > 0)
            {
                --failPauseWrites;
                Output(args, std::int32_t{});
                return {false, "temporary pause failure"};
            }
            paused = Named(args, "bPaused").Get<std::int32_t>() != 0;
            Output(args, std::int32_t{1});
            return {true, {}};
        }
        if (Ends(path, L":BGUHasUnitSimpleState"))
        {
            Output(args, std::int32_t(states[{Named(args, "Unit").Get<std::uintptr_t>(), Named(args, "SimpleState").Get<std::uint8_t>()}]));
            return {true, {}};
        }
        if (Ends(path, L":BGUSetUnitSimpleState"))
        {
            states[{Named(args, "Unit").Get<std::uintptr_t>(), Named(args, "SimpleState").Get<std::uint8_t>()}] = !Named(args, "IsRemove").Get<std::int32_t>();
            return {true, {}};
        }
        if (Ends(path, L":EnableDebugCamera"))
        {
            CHECK(Alive(instance));
            CHECK(objects.at(instance).owner == Controller);
            Add(fixture::DebugController, Kind::DebugController);
            Put(fixture::DebugController, Arg::Object("OriginalControllerRef", Controller));
            Put(fixture::DebugController, Arg::Object("OriginalPlayer", LocalPlayer));
            Put(fixture::DebugController, Arg::Object("CheatManager", 0));
            Put(instance, Arg::Object("DebugCameraControllerRef", fixture::DebugController));
            firstController = fixture::DebugController;
            possessedPawn = 0;
            return {true, {}};
        }
        if (Ends(path, L":DisableDebugCamera"))
        {
            disableManager = instance;
            disableManagerOwner = Alive(instance) ? objects.at(instance).owner : 0;
            if (disableManagerOwner != fixture::DebugController)
                return {false, "wrong debug manager outer"};
            firstController = Get<std::uintptr_t>(fixture::DebugController, "OriginalControllerRef");
            possessedPawn = Pawn;
            Put(fixture::DebugController, Arg::Object("OriginalControllerRef", 0));
            return {true, {}};
        }
        if (Ends(path, L":K2_DestroyActor"))
        {
            CHECK(instance == fixture::DebugController);
            objects.at(instance).alive = false;
            return {true, {}};
        }
        if (Ends(path, L":RunScriptGM"))
        {
            ++gmCalls;
            return {true, {}};
        }
        return {false, "unhandled mock engine call"};
    }
}
namespace gloss::gameplay::extra
{
    void Initialize() {}
    bool Supported(Id id) { return id == Id::InfiniteItems || id == Id::InfiniteBuffDuration || id == Id::InfiniteVesselDuration || id == Id::NewGameCycle; }
    bool Enabled(Id id) noexcept { return fixture::extras[id]; }
    features::Result SetEnabled(Id id, bool enabled)
    {
        fixture::extras[id] = enabled;
        return {true, enabled ? "enabled" : "disabled"};
    }
    void CaptureNativePlayer(std::uintptr_t pawn, std::uintptr_t controller) noexcept { fixture::captured.emplace_back(pawn, controller); }
    void PermitRemoval(int, int) {}
    features::Result SetCycle(int) { return {true, "queued"}; }
}
namespace gloss::gameplay::transformation
{
    void Initialize() {}
    void Tick(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uint64_t, bool controlsAvailable)
    {
        ++fixture::transformationTicks;
        fixture::transformationControlsAvailable = controlsAvailable;
    }
    void ContextLost() {}
    bool NeedsExclusiveControl() { return fixture::transformationExclusive; }
}
namespace gloss::gameplay::game_language
{
    void Tick() {}
}
namespace gloss::gameplay::console
{
    void Initialize() {}
    void Tick() {}
    bool TravelReady() { return true; }
    bool ShrinesReady() { return true; }
    features::Result Execute(std::wstring_view, std::uintptr_t)
    {
        ++fixture::consoleCalls;
        return {true, "sent"};
    }
}
namespace gloss::gameplay::aot
{
    bool Initialize(HMODULE) { return true; }
}
namespace gloss::features
{
    std::span<const Definition> Definitions() { return {}; }
    bool UpdateRequired() { return fixture::updateBlocked; }
    std::uint64_t UpdateGeneration() { return fixture::updateGeneration; }
    void CompleteAction(Id id, Result result) { fixture::completed.emplace_back(id, std::move(result)); }
}
namespace gloss
{
    void Log(std::string_view) {}
    const std::filesystem::path &DataDirectory()
    {
        static const std::filesystem::path directory = L".";
        return directory;
    }
    namespace overlay
    {
        bool Visible() { return false; }
    }
}

int main()
{
    fixture::TransformationExclusivitySuspendsConflictsAndRestoresThem();
    fixture::TransformationCancelsAlreadyQueuedConflictingEnable();
    fixture::UpdateBarrierRestoresMultipleSwitchesAndCancelsOldAction();
    fixture::UpdateBarrierCancelsQueuedEnableAcrossFailedRetry();
    fixture::UpdateBarrierAllowsQueuedDisable();
    fixture::UpdateRestorationRetriesAfterUnlock();
    fixture::UpdateArrivingDuringWorkRestoresItsResult();
    fixture::ComponentReplacementRestoresEveryBaseline();
    fixture::CameraDistancePreservesLatestGameValue();
    fixture::ControllerReplacementCancelsOldWork();
    fixture::LostSessionRestoresAndCancels();
    fixture::FailedGlobalRestoreRetriesOnOldWorld();
    fixture::WidgetDiscoveryRespectsExistingHiddenRoots();
    fixture::WidgetRestoreUsesLatestGameVisibility();
    fixture::WidgetWeakIdentityRejectsDestroyedAndReusedRoots();
    fixture::WidgetCloseRetriesOnlyFailedRoots();
    fixture::WidgetSessionTransitionRestoresPreviousWorld();
    fixture::FreeCameraCanCloseWithoutPawnCapture();
    if (fixture::failures)
    {
        std::cerr << fixture::failures << " lifecycle assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer extension lifecycle tests passed (0 failures)\n";
    return 0;
}
