#include "../src/features.hpp"
#include "../src/gameplay/action.hpp"
#include "../src/gameplay/attributes.hpp"
#include "../src/gameplay/midhook.hpp"
#include "../src/gameplay/state.hpp"
#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <future>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    int failures{};
    std::filesystem::path testDataDirectory;

#define CHECK(expression)                                                                 \
    do                                                                                    \
    {                                                                                     \
        if (!(expression))                                                                \
        {                                                                                 \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; \
            ++failures;                                                                   \
        }                                                                                 \
    } while (false)

    struct Region
    {
        void *data{};
        std::size_t size{};

        explicit Region(std::size_t bytes, DWORD protection = PAGE_READWRITE)
            : data(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, protection)), size(bytes) {}
        ~Region()
        {
            if (data)
                VirtualFree(data, 0, MEM_RELEASE);
        }
        Region(const Region &) = delete;
        Region &operator=(const Region &) = delete;
        explicit operator bool() const { return data != nullptr; }
        template <class T>
        T *at(std::size_t offset = 0) const
        {
            return reinterpret_cast<T *>(static_cast<std::byte *>(data) + offset);
        }
    };

    using FeatureId = gloss::features::Id;
    std::array<bool, gloss::features::Count> mockEnabled{};
    std::array<float, gloss::features::Count> mockValues{};

    std::size_t Index(FeatureId id) { return static_cast<std::size_t>(id); }
    void ResetGameplayMocks()
    {
        mockEnabled.fill(false);
        mockValues.fill(0.f);
    }

    void TestHotkeyConfigurationPersistenceAndCapture()
    {
        using namespace gloss::features;

        InitializeHotkeys();
        CHECK(!AnyHotkeyCapture());
        for (const auto &definition : Definitions())
        {
            const auto binding = GetHotkey(definition.id);
            CHECK(binding.key == 0);
            CHECK(binding.modifiers == 0);
            CHECK(binding.label.empty());
        }

        // Insert belongs to the overlay, and Ctrl+Shift+Home remains the global
        // hotkey enable/disable chord. Neither can be assigned to a feature.
        CHECK(!SetHotkey(Id::InfiniteHealth, VK_INSERT, NoModifier).success);
        CHECK(!SetHotkey(Id::InfiniteHealth, VK_HOME, Ctrl | Shift).success);
        CHECK(!HandleHotkey(VK_INSERT, NoModifier));

        CHECK(SetHotkey(Id::InfiniteHealth, VK_F6, Alt).success);
        const auto health = GetHotkey(Id::InfiniteHealth);
        CHECK(health.key == VK_F6);
        CHECK(health.modifiers == Alt);
        CHECK(!health.label.empty());

        // An exact chord may belong to only one entry.
        const auto conflict = SetHotkey(Id::InfiniteMana, VK_F6, Alt);
        CHECK(!conflict.success);
        CHECK(GetHotkey(Id::InfiniteMana).key == 0);

        BeginHotkeyCapture(Id::InfiniteStamina);
        CHECK(AnyHotkeyCapture());
        CHECK(CapturingHotkey(Id::InfiniteStamina));
        CHECK(HandleHotkey(VK_F7, Ctrl | Alt));
        CHECK(!AnyHotkeyCapture());
        CHECK(GetHotkey(Id::InfiniteStamina).key == VK_F7);
        CHECK(GetHotkey(Id::InfiniteStamina).modifiers == (Ctrl | Alt));

        // Failed capture due to a duplicate remains active until Escape cancels it.
        BeginHotkeyCapture(Id::InfiniteMana);
        CHECK(HandleHotkey(VK_F7, Ctrl | Alt));
        CHECK(CapturingHotkey(Id::InfiniteMana));
        CHECK(GetHotkey(Id::InfiniteMana).key == 0);
        CHECK(HandleHotkey(VK_ESCAPE, NoModifier));
        CHECK(!AnyHotkeyCapture());

        // Delete during capture clears the binding and persists the empty chord.
        BeginHotkeyCapture(Id::InfiniteStamina);
        CHECK(HandleHotkey(VK_DELETE, NoModifier));
        CHECK(GetHotkey(Id::InfiniteStamina).key == 0);
        CHECK(GetHotkey(Id::InfiniteStamina).modifiers == 0);
        CHECK(!AnyHotkeyCapture());

        CHECK(SetHotkey(Id::EditWill, VK_F8, Shift).success);
        InitializeHotkeys(); // Simulate a process restart by reloading the INI.
        CHECK(GetHotkey(Id::InfiniteHealth).key == VK_F6);
        CHECK(GetHotkey(Id::InfiniteHealth).modifiers == Alt);
        CHECK(GetHotkey(Id::InfiniteStamina).key == 0);
        CHECK(GetHotkey(Id::InfiniteStamina).modifiers == 0);
        CHECK(GetHotkey(Id::EditWill).key == VK_F8);
        CHECK(GetHotkey(Id::EditWill).modifiers == Shift);

        // Duplicate chords already present in a hand-edited INI load only once.
        const auto filePath = testDataDirectory / L"hotkeys.ini";
        const auto *file = filePath.c_str();
        CHECK(WritePrivateProfileStringW(L"Bindings", L"1", L"120,1", file));
        CHECK(WritePrivateProfileStringW(L"Bindings", L"2", L"120,1", file));
        InitializeHotkeys();
        CHECK(GetHotkey(Id::InfiniteHealth).key == VK_F9);
        CHECK(GetHotkey(Id::InfiniteHealth).modifiers == Ctrl);
        CHECK(GetHotkey(Id::InfiniteMana).key == 0);

        const bool enabledBefore = HotkeysEnabled();
        CHECK(HandleHotkey(VK_HOME, Ctrl | Shift));
        CHECK(HotkeysEnabled() != enabledBefore);
        CHECK(HandleHotkey(VK_HOME, Ctrl | Shift));
        CHECK(HotkeysEnabled() == enabledBefore);
    }

    void TestMenuHotkeyConfigurationPersistenceAndCapture()
    {
        using namespace gloss::features;

        const auto filePath = testDataDirectory / L"hotkeys.ini";
        const auto *file = filePath.c_str();
        const bool hotkeysEnabledBefore = HotkeysEnabled();
        const auto clearHotkeyFile = [&]
        {
            // Flush the Win32 profile cache before deleting the file so the next
            // InitializeHotkeys call observes a genuinely empty configuration.
            // Win32 documents a zero return for this cache-flush operation.
            (void)WritePrivateProfileStringW(nullptr, nullptr, nullptr, file);
            std::error_code clearError;
            std::filesystem::remove(filePath, clearError);
            CHECK(!clearError);
        };

        // Start with a genuinely empty, isolated configuration so the test does
        // not depend on bindings left by the feature-hotkey regression above.
        clearHotkeyFile();
        InitializeHotkeys();
        auto menu = GetMenuHotkey();
        CHECK(menu.key == VK_INSERT);
        CHECK(menu.modifiers == NoModifier);
        CHECK(IsMenuHotkey(VK_INSERT, NoModifier));
        CHECK(!IsMenuHotkey(VK_INSERT, Ctrl));
        CHECK(!AnyHotkeyCapture());

        // The menu chord cannot be cleared or assigned to a feature. The global
        // enable/disable chord is reserved for both menu and feature bindings.
        CHECK(!SetMenuHotkey(0, NoModifier).success);
        CHECK(GetMenuHotkey().key == VK_INSERT);
        CHECK(!SetMenuHotkey(VK_HOME, Ctrl | Shift).success);
        CHECK(!SetHotkey(Id::InfiniteHealth, VK_INSERT, NoModifier).success);
        CHECK(!SetHotkey(Id::InfiniteHealth, VK_HOME, Ctrl | Shift).success);

        // Move the menu chord, then verify exact-combination conflicts in both
        // directions. The old Insert chord becomes available to a feature.
        CHECK(SetMenuHotkey(VK_F9, Alt).success);
        CHECK(SetHotkey(Id::InfiniteHealth, VK_F9, Ctrl).success); // Same key, different modifiers is valid.
        CHECK(!SetMenuHotkey(VK_F9, Ctrl).success);
        CHECK(!SetHotkey(Id::InfiniteMana, VK_F9, Alt).success);
        CHECK(SetHotkey(Id::InfiniteMana, VK_INSERT, NoModifier).success);
        CHECK(!SetMenuHotkey(VK_INSERT, NoModifier).success);
        CHECK(GetMenuHotkey().key == VK_F9);
        CHECK(GetMenuHotkey().modifiers == Alt);
        CHECK(IsMenuHotkey(VK_F9, Alt));
        CHECK(!IsMenuHotkey(VK_F9, Ctrl));

        // A failed menu capture remains active, preserving the current binding;
        // pure modifiers do not finish capture and Escape cancels it.
        BeginMenuHotkeyCapture();
        CHECK(AnyHotkeyCapture());
        CHECK(CapturingMenuHotkey());
        CHECK(HandleHotkey(VK_CONTROL, Ctrl));
        CHECK(CapturingMenuHotkey());
        CHECK(GetMenuHotkey().key == VK_F9);
        CHECK(GetMenuHotkey().modifiers == Alt);
        CHECK(HandleHotkey(VK_INSERT, NoModifier)); // Conflicts with InfiniteMana.
        CHECK(CapturingMenuHotkey());
        CHECK(!GetMenuHotkeyResult().success);
        CHECK(GetMenuHotkey().key == VK_F9);
        CHECK(HandleHotkey(VK_ESCAPE, NoModifier));
        CHECK(!AnyHotkeyCapture());
        CHECK(!CapturingMenuHotkey());
        CHECK(GetMenuHotkeyResult().success);
        CHECK(GetMenuHotkey().key == VK_F9);

        // A successful capture consumes the candidate key-down. It must not run a
        // feature callback during capture, and the resulting menu chord must not
        // be treated as a feature shortcut afterward.
        int backendQueries{}, backendToggles{}, backendActions{};
        RegisterBackend({[&](Id)
                         { ++backendQueries; return BackendState{Availability::Ready, false, {}}; },
                         [&](Id, bool, double)
                         { ++backendToggles; return Result{true, "ok"}; },
                         [&](Id, double)
                         { ++backendActions; return Result{true, "ok"}; }});
        CHECK(SetHotkey(Id::EditWill, VK_F8, Shift).success);
        BeginMenuHotkeyCapture();
        CHECK(HandleHotkey(VK_F10, Alt));
        CHECK(!CapturingMenuHotkey());
        CHECK(GetMenuHotkey().key == VK_F10);
        CHECK(GetMenuHotkey().modifiers == Alt);
        CHECK(GetMenuHotkeyResult().success);
        CHECK(backendQueries == 0);
        CHECK(backendToggles == 0);
        CHECK(backendActions == 0);
        CHECK(!HandleHotkey(VK_F10, Alt));
        CHECK(backendQueries == 0);
        CHECK(backendToggles == 0);
        CHECK(backendActions == 0);
        RegisterBackend({});

        // Configuration reload keeps a valid menu chord and feature chord. It
        // also demonstrates that Insert remains bindable after changing the menu.
        CHECK(SetHotkey(Id::InfiniteMana, 0, NoModifier).success); // Release Insert from the earlier conflict case.
        CHECK(SetHotkey(Id::InfiniteStamina, VK_INSERT, NoModifier).success);
        InitializeHotkeys();
        menu = GetMenuHotkey();
        CHECK(menu.key == VK_F10);
        CHECK(menu.modifiers == Alt);
        CHECK(GetHotkey(Id::InfiniteStamina).key == VK_INSERT);
        CHECK(GetHotkey(Id::InfiniteStamina).modifiers == NoModifier);
        CHECK(GetHotkey(Id::EditWill).key == VK_F8);
        CHECK(GetHotkey(Id::EditWill).modifiers == Shift);

        // Invalid menu data falls back to Insert, and an exact feature collision
        // with that fallback is dropped. Malformed/out-of-range bindings are also
        // ignored instead of becoming active shortcuts.
        CHECK(WritePrivateProfileStringW(L"Menu", L"Toggle", L"18446744073709551615,9999", file));
        CHECK(WritePrivateProfileStringW(L"Bindings", nullptr, nullptr, file));
        CHECK(WritePrivateProfileStringW(L"Bindings", L"1", L"45,0", file));
        CHECK(WritePrivateProfileStringW(L"Bindings", L"2", L"255,8", file));
        CHECK(WritePrivateProfileStringW(L"Bindings", L"3", L"-1,0", file));
        CHECK(WritePrivateProfileStringW(L"Bindings", L"4", L"17,4294967295", file));
        InitializeHotkeys();
        CHECK(GetMenuHotkey().key == VK_INSERT);
        CHECK(GetMenuHotkey().modifiers == NoModifier);
        CHECK(IsMenuHotkey(VK_INSERT, NoModifier));
        CHECK(GetHotkey(Id::InfiniteHealth).key == 0);
        CHECK(GetHotkey(Id::InfiniteMana).key == 0);
        CHECK(GetHotkey(Id::InfiniteStamina).key == 0);
        CHECK(GetHotkey(Id::InfiniteFocus).key == 0);

        // Even when feature hotkeys are disabled, input routing can still identify
        // and toggle the menu chord independently.
        SetHotkeysEnabled(false);
        CHECK(!HotkeysEnabled());
        CHECK(IsMenuHotkey(VK_INSERT, NoModifier));
        CHECK(!HandleHotkey(VK_INSERT, NoModifier));
        CHECK(IsMenuHotkey(VK_INSERT, NoModifier));

        // An empty file restores the default menu chord and leaves every feature
        // unbound. Leave the shared feature model clean for later regression tests.
        clearHotkeyFile();
        InitializeHotkeys();
        CHECK(GetMenuHotkey().key == VK_INSERT);
        CHECK(GetMenuHotkey().modifiers == NoModifier);
        for (const auto &definition : Definitions())
        {
            const auto binding = GetHotkey(definition.id);
            CHECK(binding.key == 0);
            CHECK(binding.modifiers == NoModifier);
        }
        SetHotkeysEnabled(hotkeysEnabledBefore);
    }

    void TestFeatureCatalogAndInputStaging()
    {
        using namespace gloss::features;
        const auto definitions = Definitions();
        CHECK(definitions.size() == gloss::features::Count);
        std::array<bool, Count> ids{};
        for (std::size_t i = 0; i < definitions.size(); ++i)
        {
            const auto &definition = definitions[i];
            const auto id = static_cast<std::size_t>(definition.id);
            CHECK(id < ids.size());
            if (id < ids.size())
            {
                CHECK(!ids[id]);
                ids[id] = true;
            }
            CHECK(id == i);
            CHECK(definition.virtualKey == 0);
            CHECK(definition.modifiers == 0);
            CHECK(definition.hotkey && !*definition.hotkey);
        }

        int queryCalls{}, enableCalls{}, applyCalls{};
        RegisterBackend({[&](Id)
                         { ++queryCalls; return BackendState{Availability::Ready, false, {}}; },
                         [&](Id, bool, double)
                         { ++enableCalls; return Result{true, "ok"}; },
                         [&](Id, double)
                         { ++applyCalls; return Result{true, "ok"}; }});
        CHECK(SetValue(Id::DamageMultiplier, 3.25));
        CHECK(queryCalls == 0);
        CHECK(enableCalls == 0);
        CHECK(applyCalls == 0); // Editing a staged value makes no backend call of any kind.
        CHECK(GetSnapshot(Id::DamageMultiplier).value == 3.25);
        CHECK(queryCalls == 1); // Only the explicit snapshot queried the backend.
        CHECK(enableCalls == 0);
        CHECK(applyCalls == 0); // Staging an editor value never applies it.
    }

    void TestFeatureAvailabilityAndExactHotkeys()
    {
        using namespace gloss::features;
        int queryCalls{}, enableCalls{}, applyCalls{};
        RegisterBackend({[&](Id)
                         { ++queryCalls; return BackendState{Availability::Unavailable, false, "not ready"}; },
                         [&](Id, bool, double)
                         { ++enableCalls; return Result{true, "unexpected"}; },
                         [&](Id, double)
                         { ++applyCalls; return Result{true, "unexpected"}; }});
        CHECK(!SetEnabled(Id::InfiniteHealth, true).success);
        CHECK(!Apply(Id::EditWill).success);
        CHECK(queryCalls == 2);
        CHECK(enableCalls == 0);
        CHECK(applyCalls == 0); // An unavailable feature must not reach a write callback.

        std::array<bool, Count> backendEnabled{};
        RegisterBackend({[&](Id id)
                         { ++queryCalls; return BackendState{Availability::Ready, backendEnabled[static_cast<std::size_t>(id)], {}}; },
                         [&](Id id, bool enabled, double)
                         {
                             ++enableCalls;
                             backendEnabled[static_cast<std::size_t>(id)] = enabled;
                             return Result{true, "ok"};
                         },
                         [&](Id, double)
                         { ++applyCalls; return Result{true, "ok"}; }});
        CHECK(SetHotkey(Id::OneHitKill, VK_NUMPAD1, Ctrl).success);
        SetHotkeysEnabled(true);
        const int enablesBefore = enableCalls;
        CHECK(!HandleHotkey(VK_NUMPAD1, Ctrl | Shift));
        CHECK(!HandleHotkey(VK_NUMPAD1, Alt | Shift));
        CHECK(enableCalls == enablesBefore);
        CHECK(HandleHotkey(VK_NUMPAD1, Ctrl)); // Custom binding toggles OneHitKill.
        CHECK(enableCalls == enablesBefore + 1);
        CHECK(backendEnabled[static_cast<std::size_t>(Id::OneHitKill)]);
        CHECK(!backendEnabled[static_cast<std::size_t>(Id::GodMode)]);
    }

    void TestUpdateRequirementRestoresAndBlocksFeatures()
    {
        using namespace gloss::features;
        SetUpdateRequired(false);
        std::array<bool, Count> enabled{};
        int queryCalls{}, enableCalls{}, disableCalls{}, actionCalls{};
        Availability availability = Availability::Ready;
        bool callbackReadModel{};
        RegisterBackend({[&](Id id)
                         { ++queryCalls; return BackendState{availability, enabled[Index(id)], {}}; },
                         [&](Id id, bool value, double)
                         {
                             callbackReadModel = HotkeysEnabled(); // No callback may hold modelMutex.
                             enabled[Index(id)] = value;
                             if (value)
                                 ++enableCalls;
                             else
                                 ++disableCalls;
                             return Result{true, "ok"};
                         },
                         [&](Id, double)
                         { ++actionCalls; return Result{true, "ok"}; }});
        SetHotkeysEnabled(true);
        CHECK(SetEnabled(Id::InfiniteHealth, true).success);
        CHECK(SetEnabled(Id::DamageMultiplier, true).success);
        CHECK(enableCalls == 2);
        const auto updateGeneration = UpdateGeneration();
        SetUpdateRequired(true);
        CHECK(UpdateGeneration() == updateGeneration + 1);
        CHECK(UpdateRequired());
        CHECK(!enabled[Index(Id::InfiniteHealth)]);
        CHECK(!enabled[Index(Id::DamageMultiplier)]);
        CHECK(disableCalls == 2);
        CHECK(callbackReadModel);
        const int queriesAfterTransition = queryCalls;
        SetUpdateRequired(true);
        CHECK(UpdateGeneration() == updateGeneration + 1);
        CHECK(disableCalls == 2);
        CHECK(queryCalls == queriesAfterTransition); // Per-frame synchronization does not repeat restoration.
        CHECK(SetValue(Id::DamageMultiplier, 4));    // Editing alone is still harmless.
        CHECK(!SetEnabled(Id::InfiniteHealth, true).success);
        CHECK(!Apply(Id::DamageMultiplier).success);
        CHECK(!Apply(Id::EditWill).success);
        CHECK(queryCalls == queriesAfterTransition);
        CHECK(enableCalls == 2);
        CHECK(actionCalls == 0);

        // Explicit restoration remains possible even if the game target vanished
        // after the transition and the backend no longer reports an active switch.
        availability = Availability::Waiting;
        CHECK(SetEnabled(Id::InfiniteHealth, false).success);
        CHECK(disableCalls == 3);
        availability = Availability::Ready;

        const auto menu = GetMenuHotkey();
        CHECK(SetHotkey(Id::InfiniteHealth, VK_F11, Alt).success);
        CHECK(SetHotkey(Id::EditWill, VK_F12, Alt).success);
        const int queriesBeforeShortcuts = queryCalls;
        CHECK(HandleHotkey(VK_F11, Alt));
        CHECK(HandleHotkey(VK_F12, Alt));
        CHECK(!HandleHotkey(VK_F11, Alt | Shift));
        CHECK(!HandleHotkey(menu.key, menu.modifiers));
        CHECK(IsMenuHotkey(menu.key, menu.modifiers));
        CHECK(queryCalls == queriesBeforeShortcuts);
        CHECK(enableCalls == 2);
        CHECK(actionCalls == 0);
        CHECK(HandleHotkey(VK_HOME, Ctrl | Shift));
        CHECK(!HotkeysEnabled());
        CHECK(HandleHotkey(VK_F11, Alt)); // Bound keys stay consumed under the update gate.
        CHECK(HandleHotkey(VK_HOME, Ctrl | Shift));
        CHECK(HotkeysEnabled());

        BeginHotkeyCapture(Id::InfiniteMana);
        CHECK(HandleHotkey(VK_F10, Ctrl));
        CHECK(!AnyHotkeyCapture());
        CHECK(GetHotkey(Id::InfiniteMana).key == VK_F10);
        CHECK(GetHotkey(Id::InfiniteMana).modifiers == Ctrl);
        BeginMenuHotkeyCapture();
        CHECK(HandleHotkey(VK_F9, Shift));
        CHECK(!AnyHotkeyCapture());
        CHECK(IsMenuHotkey(VK_F9, Shift));
        CHECK(SetMenuHotkey(menu.key, menu.modifiers).success);
        CHECK(actionCalls == 0);

        SetUpdateRequired(false); // A failed update retry unlocks without restarting switches.
        CHECK(UpdateGeneration() == updateGeneration + 1);
        CHECK(!UpdateRequired());
        CHECK(enableCalls == 2);
        CHECK(disableCalls == 3);
        CHECK(!enabled[Index(Id::InfiniteHealth)]);
        CHECK(!enabled[Index(Id::DamageMultiplier)]);
        CHECK(SetEnabled(Id::InfiniteHealth, true).success);
        CHECK(Apply(Id::EditWill).success);
        CHECK(enableCalls == 3);
        CHECK(actionCalls == 1);
        SetUpdateRequired(true);
        CHECK(disableCalls == 4);
        SetUpdateRequired(false);
        CHECK(SetHotkey(Id::InfiniteHealth, 0, NoModifier).success);
        CHECK(SetHotkey(Id::InfiniteMana, 0, NoModifier).success);
        CHECK(SetHotkey(Id::EditWill, 0, NoModifier).success);
        RegisterBackend({});
    }

    void TestUpdateRequirementWaitsForInFlightWrite()
    {
        using namespace gloss::features;
        for (const bool action : {false, true})
        {
            SetUpdateRequired(false);
            std::atomic<bool> enabled{};
            std::atomic<int> enables{}, disables{}, actions{};
            std::promise<void> enteredPromise, releasePromise, gateStartedPromise, gateFinishedPromise;
            auto entered = enteredPromise.get_future();
            auto release = releasePromise.get_future().share();
            auto gateStarted = gateStartedPromise.get_future();
            auto gateFinished = gateFinishedPromise.get_future();
            RegisterBackend({[&](Id id)
                             { return BackendState{Availability::Ready, id == Id::InfiniteHealth && enabled.load(), {}}; },
                             [&](Id, bool value, double)
                             {
                                 if (value)
                                 {
                                     ++enables;
                                     enteredPromise.set_value();
                                     release.wait();
                                 }
                                 else
                                     ++disables;
                                 enabled = value;
                                 return Result{true, "ok"};
                             },
                             [&](Id, double)
                             {
                                 ++actions;
                                 enteredPromise.set_value();
                                 release.wait();
                                 return Result{true, "ok"};
                             }});
            bool writeSucceeded{};
            std::thread writer([&]
                               { writeSucceeded = action ? Apply(Id::EditWill).success : SetEnabled(Id::InfiniteHealth, true).success; });
            CHECK(entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
            std::thread gate([&]
                             {
            gateStartedPromise.set_value();
            SetUpdateRequired(true);
            gateFinishedPromise.set_value(); });
            CHECK(gateStarted.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
            const bool waitedForWrite = gateFinished.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
            releasePromise.set_value();
            writer.join();
            gate.join();
            CHECK(waitedForWrite);
            CHECK(writeSucceeded);
            CHECK(UpdateRequired());
            CHECK(!enabled);
            CHECK(enables == (action ? 0 : 1));
            CHECK(disables == (action ? 0 : 1));
            CHECK(actions == (action ? 1 : 0));
            CHECK(!SetEnabled(Id::InfiniteHealth, true).success);
            CHECK(!Apply(Id::EditWill).success);
            CHECK(enables == (action ? 0 : 1));
            CHECK(actions == (action ? 1 : 0));
            SetUpdateRequired(false);
            CHECK(!enabled);
            RegisterBackend({});
        }
    }

    void TestUpdateRequirementRestorationFailure()
    {
        using namespace gloss::features;
        SetUpdateRequired(false);
        bool health = true, mana = true, failHealth = true;
        int healthDisables{}, manaDisables{};
        RegisterBackend({[&](Id id)
                         { return BackendState{Availability::Waiting,
                                               id == Id::InfiniteHealth ? health : id == Id::InfiniteMana && mana,
                                               {}}; },
                         [&](Id id, bool value, double)
                         {
                             if (value)
                                 return Result{false, "unexpected enable"};
                             if (id == Id::InfiniteHealth)
                             {
                                 ++healthDisables;
                                 if (failHealth)
                                     throw std::runtime_error("simulated restoration failure");
                                 health = false;
                             }
                             else if (id == Id::InfiniteMana)
                             {
                                 ++manaDisables;
                                 mana = false;
                             }
                             return Result{true, "ok"};
                         },
                         {}});
        SetUpdateRequired(true);
        CHECK(UpdateRequired());
        CHECK(health);
        CHECK(!mana); // One restoration failure must not skip the other switches.
        CHECK(healthDisables == 1);
        CHECK(manaDisables == 1);
        SetUpdateRequired(true);
        CHECK(healthDisables == 1);
        failHealth = false;
        CHECK(SetEnabled(Id::InfiniteHealth, false).success);
        CHECK(!health);
        CHECK(healthDisables == 2);
        SetUpdateRequired(false);
        CHECK(!health);
        CHECK(!mana);
        RegisterBackend({});
    }

    void TestPendingActionBoundariesAndConcurrency()
    {
        using gloss::gameplay::PendingAction;

        PendingAction action;
        action.Queue(17, 1000);
        action.Expire(999); // A stale clock sample must not underflow into an expiry.
        CHECK(action.State() == PendingAction::Queued);
        int writtenValue{};
        CHECK(action.Consume(999, [&](int value)
                             { writtenValue = value; return true; }));
        CHECK(writtenValue == 17);
        CHECK(action.State() == PendingAction::Completed);
        CHECK(!action.Consume(1000, [&](int)
                              { return true; })); // A value is consumed at most once.

        action.Queue(23, 5000);
        action.Expire(5000 + PendingAction::Lifetime); // Lifetime boundary remains valid.
        CHECK(action.State() == PendingAction::Queued);
        writtenValue = 0;
        CHECK(action.Consume(5000 + PendingAction::Lifetime,
                             [&](int value)
                             { writtenValue = value; return true; }));
        CHECK(writtenValue == 23);
        CHECK(action.State() == PendingAction::Completed);

        action.Queue(31, 9000);
        bool writerCalled{};
        CHECK(!action.Consume(9000 + PendingAction::Lifetime + 1,
                              [&](int)
                              { writerCalled = true; return true; }));
        CHECK(!writerCalled);
        CHECK(action.State() == PendingAction::Expired);

        action.Queue(37, 12000);
        CHECK(!action.Consume(12000, [](int)
                              { return false; }));
        CHECK(action.State() == PendingAction::Expired); // Failed writes cannot be retried as queued work.

        // Hold Consume's transaction mutex inside the writer while Expire runs on
        // another thread. Expire must return without waiting for the writer.
        action.Queue(41, 15000);
        std::promise<void> writerEnteredPromise;
        auto writerEntered = writerEnteredPromise.get_future();
        std::promise<void> allowWriterReturnPromise;
        auto allowWriterReturn = allowWriterReturnPromise.get_future().share();
        std::promise<void> expiryFinishedPromise;
        auto expiryFinished = expiryFinishedPromise.get_future();
        bool consumeResult{};
        bool writerReceivedExpected{};
        std::thread expiryThread;
        std::thread consumeThread([&]
                                  { consumeResult = action.Consume(15000, [&](int value)
                                                                   {
            writerReceivedExpected = value == 41;
            writerEnteredPromise.set_value();
            expiryThread = std::thread([&] {
                action.Expire(15000 + PendingAction::Lifetime + 1);
                expiryFinishedPromise.set_value();
            });
            allowWriterReturn.wait();
            return true; }); });
        CHECK(writerEntered.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        const auto expiryPrompt = expiryFinished.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
        allowWriterReturnPromise.set_value();
        consumeThread.join();
        expiryThread.join();
        CHECK(expiryPrompt);
        CHECK(consumeResult);
        CHECK(writerReceivedExpected);
        CHECK(action.State() == PendingAction::Completed);

        // A stale expiry racing with a newer Queue must leave the newer request
        // intact. Queue waits for the in-flight consume transaction; Expire does not.
        PendingAction concurrent;
        concurrent.Queue(51, 20000);
        std::promise<void> concurrentWriterEnteredPromise;
        auto concurrentWriterEntered = concurrentWriterEnteredPromise.get_future();
        std::promise<void> allowConcurrentWriterReturnPromise;
        auto allowConcurrentWriterReturn = allowConcurrentWriterReturnPromise.get_future().share();
        std::promise<void> staleExpiryFinishedPromise;
        auto staleExpiryFinished = staleExpiryFinishedPromise.get_future();
        std::thread staleExpiryThread;
        bool concurrentConsumeResult{};
        bool concurrentWriterReceivedExpected{};
        std::thread concurrentConsumeThread([&]
                                            { concurrentConsumeResult = concurrent.Consume(20000, [&](int value)
                                                                                           {
            concurrentWriterReceivedExpected = value == 51;
            concurrentWriterEnteredPromise.set_value();
            staleExpiryThread = std::thread([&] {
                concurrent.Expire(20000 + PendingAction::Lifetime + 1);
                staleExpiryFinishedPromise.set_value();
            });
            allowConcurrentWriterReturn.wait();
            return true; }); });
        CHECK(concurrentWriterEntered.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        const auto staleExpiryPrompt = staleExpiryFinished.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
        std::promise<void> newerQueueFinishedPromise;
        auto newerQueueFinished = newerQueueFinishedPromise.get_future();
        std::thread newerQueueThread([&]
                                     {
        concurrent.Queue(59, 20000 + PendingAction::Lifetime + 2);
        newerQueueFinishedPromise.set_value(); });
        allowConcurrentWriterReturnPromise.set_value();
        concurrentConsumeThread.join();
        staleExpiryThread.join();
        newerQueueThread.join();
        CHECK(staleExpiryPrompt);
        CHECK(concurrentConsumeResult);
        CHECK(concurrentWriterReceivedExpected);
        CHECK(newerQueueFinished.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        CHECK(concurrent.State() == PendingAction::Queued);
        int newerValue{};
        CHECK(concurrent.Consume(20000 + PendingAction::Lifetime + 2,
                                 [&](int value)
                                 { newerValue = value; return true; }));
        CHECK(newerValue == 59);
        CHECK(concurrent.State() == PendingAction::Completed);
    }

    void TestPatternParsingAndScanning()
    {
        using namespace gloss::gameplay;
        Region bytes(4096);
        CHECK(static_cast<bool>(bytes));
        if (!bytes)
            return;
        const std::array<unsigned char, 12> data{{0xaa, 0xbb, 0xcc, 0x11, 0x22, 0xcc, 0xaa, 0xbb, 0xcc, 0x90, 0x90, 0x90}};
        std::memcpy(bytes.data, data.data(), data.size());

        const auto unique = FindPattern(Pattern::Parse("11 ?? CC"), reinterpret_cast<std::uintptr_t>(bytes.data), data.size());
        CHECK(unique.size() == 1);
        if (unique.size() == 1)
            CHECK(unique.front() == reinterpret_cast<std::uintptr_t>(bytes.data) + 3);

        const auto ambiguous = FindPattern(Pattern::Parse("AA * CC"), reinterpret_cast<std::uintptr_t>(bytes.data), data.size());
        CHECK(ambiguous.size() == 2); // The default limit reports enough to reject ambiguity.
        const auto empty = FindPattern(Pattern::Parse(""), reinterpret_cast<std::uintptr_t>(bytes.data), data.size());
        CHECK(empty.empty());
        const auto invalid = Pattern::Parse("11 GZ CC");
        CHECK(invalid.bytes.empty());
        CHECK(FindPattern(invalid, reinterpret_cast<std::uintptr_t>(bytes.data), data.size()).empty());

        const auto fullByteRange = Pattern::Parse("AA 30:40 BB");
        CHECK(fullByteRange.bytes.size() == 3);
        const std::array<unsigned char, 12> rangeData{{0xaa, 0x2f, 0xbb, 0xaa, 0x30, 0xbb,
                                                       0xaa, 0x40, 0xbb, 0xaa, 0x41, 0xbb}};
        std::memcpy(bytes.data, rangeData.data(), rangeData.size());
        const auto rangeMatches = FindPattern(fullByteRange, reinterpret_cast<std::uintptr_t>(bytes.data), rangeData.size(), 4);
        CHECK(rangeMatches.size() == 2);
        if (rangeMatches.size() == 2)
        {
            CHECK(rangeMatches[0] == reinterpret_cast<std::uintptr_t>(bytes.data) + 3);
            CHECK(rangeMatches[1] == reinterpret_cast<std::uintptr_t>(bytes.data) + 6);
        }
        CHECK(Pattern::Parse("40:30").bytes.empty());
    }

    void TestPatternSkipsNoAccessAndGuardPages()
    {
        using namespace gloss::gameplay;
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        const auto bytesPerPage = static_cast<std::size_t>(systemInfo.dwPageSize);
        Region memory(bytesPerPage * 4);
        CHECK(static_cast<bool>(memory));
        if (!memory)
            return;

        auto *raw = static_cast<unsigned char *>(memory.data);
        // This candidate would cross from the readable page into PAGE_NOACCESS.
        raw[bytesPerPage - 3] = 0xde;
        raw[bytesPerPage - 2] = 0xad;
        raw[bytesPerPage - 1] = 0xbe;
        // The guarded page also contains a full signature but must be skipped.
        raw[bytesPerPage * 2] = 0xde;
        raw[bytesPerPage * 2 + 1] = 0xad;
        raw[bytesPerPage * 2 + 2] = 0xbe;
        raw[bytesPerPage * 2 + 3] = 0xef;
        // A readable page after both protected regions contains the only valid hit.
        raw[bytesPerPage * 3 + 16] = 0xde;
        raw[bytesPerPage * 3 + 17] = 0xad;
        raw[bytesPerPage * 3 + 18] = 0xbe;
        raw[bytesPerPage * 3 + 19] = 0xef;

        DWORD oldProtection{};
        const bool noAccessSet = VirtualProtect(raw + bytesPerPage, bytesPerPage, PAGE_NOACCESS, &oldProtection) != FALSE;
        const bool guardSet = VirtualProtect(raw + bytesPerPage * 2, bytesPerPage, PAGE_READWRITE | PAGE_GUARD, &oldProtection) != FALSE;
        CHECK(noAccessSet);
        CHECK(guardSet);
        if (!noAccessSet || !guardSet)
            return;
        const auto matches = FindPattern(Pattern::Parse("DE AD BE EF"), reinterpret_cast<std::uintptr_t>(memory.data), bytesPerPage * 4, 4);
        CHECK(matches.size() == 1);
        if (matches.size() == 1)
            CHECK(matches.front() == reinterpret_cast<std::uintptr_t>(raw + bytesPerPage * 3 + 16));
    }

    using MachineFunction = std::uint64_t(__fastcall *)(std::uint64_t);
    void AddThreeToArgument(gloss::gameplay::Context &context, void *) { context.rcx += 3; }
    void AccessViolationCallback(gloss::gameplay::Context &, void *)
    {
        auto *invalid = reinterpret_cast<volatile const int *>(static_cast<std::uintptr_t>(1));
        volatile const int value = *invalid;
        (void)value;
    }

    void TestRealMidHookTrampolineAndCleanup()
    {
        using namespace gloss::gameplay;
        Region code(4096);
        CHECK(static_cast<bool>(code));
        if (!code)
            return;

        // mov rax,rcx; add rax,7; three complete arithmetic instructions cover
        // MinHook's x64 patch; NOPs leave a safe function tail before ret.
        const std::array<unsigned char, 24> machineCode{{0x48, 0x89, 0xc8,
                                                         0x48, 0x83, 0xc0, 0x07,
                                                         0x48, 0x83, 0xc0, 0x00,
                                                         0x48, 0x83, 0xc0, 0x00,
                                                         0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                                                         0xc3}};
        std::memcpy(code.data, machineCode.data(), machineCode.size());
        DWORD oldProtection{};
        const bool codeIsExecutable = VirtualProtect(code.data, code.size, PAGE_EXECUTE_READ, &oldProtection) != FALSE;
        CHECK(codeIsExecutable);
        if (!codeIsExecutable)
            return;
        FlushInstructionCache(GetCurrentProcess(), code.data, machineCode.size());
        const auto function = reinterpret_cast<MachineFunction>(code.data);
        CHECK(function(35) == 42);

        std::string error;
        auto hook = MidHook::Create(code.data, AddThreeToArgument, nullptr, error);
        CHECK(static_cast<bool>(hook));
        if (!hook)
        {
            std::cerr << "MidHook::Create failed: " << error << '\n';
            return;
        }
        CHECK(function(35) == 45); // Callback-modified RCX continues through the original trampoline.
        CHECK(MH_DisableHook(code.data) == MH_OK);
        CHECK(function(35) == 42); // Disabling restores the original code path.
        CHECK(MH_EnableHook(code.data) == MH_OK);
        CHECK(function(35) == 45);
        hook.reset();
        CHECK(function(35) == 42); // Destruction removes the hook and frees its bridge.

        error.clear();
        auto guarded = MidHook::Create(code.data, AccessViolationCallback, nullptr, error);
        CHECK(static_cast<bool>(guarded));
        if (guarded)
        {
            CHECK(function(35) == 42); // Dispatch catches callback SEH and restores the untouched context.
            guarded.reset();
            CHECK(function(35) == 42);
        }
    }

    struct alignas(32) ExtendedStateSnapshot
    {
        std::uint64_t returnValue{};
        std::uint64_t flagsBefore{};
        std::uint64_t flagsAfter{};
        std::uint32_t mxcsrAfter{};
        std::uint32_t reserved{};
        alignas(16) std::array<std::uint8_t, 16> xmm0{};
        alignas(16) std::array<std::uint8_t, 16> xmm6{};
        alignas(32) std::array<std::uint8_t, 32> ymm0{};
        alignas(32) std::array<std::uint8_t, 32> ymm6{};
    };

    using ExtendedStateFunction = std::uint64_t(__fastcall *)(ExtendedStateSnapshot *);
    using StateClobberFunction = void(__fastcall *)();
    constexpr std::uint64_t EditedReturnValue = 0x123456789abcdef0ull;
    constexpr std::uint32_t FunctionMxcsr = 0x3f80;  // Non-default rounding mode, with exceptions masked.
    constexpr std::uint32_t CallbackMxcsr = 0x5f80;  // Different rounding mode, with exceptions masked.
    constexpr std::uint64_t ArithmeticFlags = 0x8d5; // CF, PF, AF, ZF, SF, and OF.
    volatile LONG extendedStateCallbacks{};
    constexpr std::array<std::uint32_t, 8> ExpectedYmm0{{0x10203040, 0x21314151, 0x32425262, 0x43536373,
                                                         0x54647484, 0x65758595, 0x768696a6, 0x8797a7b7}};
    constexpr std::array<std::uint32_t, 8> ExpectedYmm6{{0x98a8b8c8, 0x8797a7b7, 0x768696a6, 0x65758595,
                                                         0x54647484, 0x43536373, 0x32425262, 0x21314151}};

    bool AvxStateEnabled()
    {
        int cpu[4]{};
        __cpuid(cpu, 1);
        constexpr int Avx = 1 << 28;
        constexpr int OsXsave = 1 << 27;
        if ((cpu[2] & (Avx | OsXsave)) != (Avx | OsXsave))
            return false;
        return (_xgetbv(0) & 0x6) == 0x6; // OS enabled both XMM and YMM state components.
    }

    struct CodeBuffer
    {
        std::vector<unsigned char> bytes;
        std::size_t hookOffset{};
        void Emit(std::initializer_list<unsigned char> sequence) { bytes.insert(bytes.end(), sequence); }
        void Dword(std::uint32_t value)
        {
            for (unsigned shift = 0; shift != 32; shift += 8)
                bytes.push_back(static_cast<unsigned char>(value >> shift));
        }
        void StoreStackDword(unsigned char displacement, std::uint32_t value)
        {
            Emit({0xc7, 0x44, 0x24, displacement});
            Dword(value);
        }
        void StoreRbxDisp32(std::initializer_list<unsigned char> opcode, std::size_t offset)
        {
            Emit(opcode);
            Dword(static_cast<std::uint32_t>(offset));
        }
    };

    CodeBuffer BuildExtendedStateFunction(bool avx)
    {
        CodeBuffer code;
        code.Emit({0x53});                                     // push rbx
        code.Emit({0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00}); // sub rsp,80h; maintain x64 call alignment
        code.Emit({0x48, 0x89, 0xcb});                         // mov rbx,rcx (snapshot)
        code.Emit({0x0f, 0x11, 0x74, 0x24, 0x50});             // save caller's nonvolatile xmm6
        code.Emit({0x0f, 0xae, 0x5c, 0x24, 0x44});             // save caller MXCSR

        for (unsigned i = 0; i < ExpectedYmm0.size(); ++i)
            code.StoreStackDword(static_cast<unsigned char>(i * 4), ExpectedYmm0[i]);
        for (unsigned i = 0; i < ExpectedYmm6.size(); ++i)
            code.StoreStackDword(static_cast<unsigned char>(0x20 + i * 4), ExpectedYmm6[i]);
        code.StoreStackDword(0x40, FunctionMxcsr);
        code.Emit({0x0f, 0xae, 0x54, 0x24, 0x40}); // ldmxcsr [rsp+40h]

        if (avx)
        {
            code.Emit({0xc5, 0xfe, 0x6f, 0x04, 0x24});       // vmovdqu ymm0,[rsp]
            code.Emit({0xc5, 0xfe, 0x6f, 0x74, 0x24, 0x20}); // vmovdqu ymm6,[rsp+20h]
        }
        else
        {
            code.Emit({0x0f, 0x10, 0x04, 0x24});       // movups xmm0,[rsp]
            code.Emit({0x0f, 0x10, 0x74, 0x24, 0x20}); // movups xmm6,[rsp+20h]
        }

        code.Emit({0x31, 0xc0, 0x83, 0xf8, 0x00}); // xor eax,eax; cmp eax,0 -> stable arithmetic flags
        code.Emit({0x9c, 0x41, 0x5a});             // pushfq; pop r10
        code.StoreRbxDisp32({0x4c, 0x89, 0x93}, offsetof(ExtendedStateSnapshot, flagsBefore));

        code.hookOffset = code.bytes.size();
        code.bytes.insert(code.bytes.end(), 32, 0x90); // Hook in the middle, on complete NOP instructions.

        code.StoreRbxDisp32({0x48, 0x89, 0x83}, offsetof(ExtendedStateSnapshot, returnValue));
        code.Emit({0x9c, 0x41, 0x5a}); // Capture flags without changing them.
        code.StoreRbxDisp32({0x4c, 0x89, 0x93}, offsetof(ExtendedStateSnapshot, flagsAfter));
        code.StoreRbxDisp32({0x0f, 0xae, 0x9b}, offsetof(ExtendedStateSnapshot, mxcsrAfter)); // stmxcsr
        code.StoreRbxDisp32({0x0f, 0x11, 0x83}, offsetof(ExtendedStateSnapshot, xmm0));
        code.StoreRbxDisp32({0x0f, 0x11, 0xb3}, offsetof(ExtendedStateSnapshot, xmm6));
        if (avx)
        {
            code.StoreRbxDisp32({0xc5, 0xfe, 0x7f, 0x83}, offsetof(ExtendedStateSnapshot, ymm0));
            code.StoreRbxDisp32({0xc5, 0xfe, 0x7f, 0xb3}, offsetof(ExtendedStateSnapshot, ymm6));
            code.Emit({0xc5, 0xf8, 0x77}); // vzeroupper before returning to the C++ caller.
        }
        code.Emit({0x0f, 0xae, 0x54, 0x24, 0x44}); // Restore caller MXCSR.
        code.Emit({0x0f, 0x10, 0x74, 0x24, 0x50}); // Restore caller's nonvolatile xmm6.
        code.StoreRbxDisp32({0x48, 0x8b, 0x83}, offsetof(ExtendedStateSnapshot, returnValue));
        code.Emit({0x48, 0x81, 0xc4, 0x80, 0x00, 0x00, 0x00, 0x5b, 0xc3});

        return code;
    }

    std::vector<unsigned char> BuildStateClobberThunk(bool avx)
    {
        std::vector<unsigned char> code;
        if (avx)
            code.insert(code.end(), {0xc5, 0xfc, 0x77}); // vzeroall: clobber XMM/YMM state.
        else
            code.insert(code.end(), {0x66, 0x0f, 0xef, 0xc0, 0x66, 0x0f, 0x76, 0xf6}); // pxor xmm0; pcmpeqd xmm6.
        code.insert(code.end(), {0x0f, 0xae, 0x15, 0x02, 0x00, 0x00, 0x00});           // ldmxcsr [rip+2]
        code.insert(code.end(), {0xf9, 0xc3});                                         // stc; ret
        for (unsigned shift = 0; shift != 32; shift += 8)
            code.push_back(static_cast<unsigned char>(CallbackMxcsr >> shift)); // Literal is unreachable after ret.
        return code;
    }

    void SetContextAndClobberExtendedState(gloss::gameplay::Context &context, void *user)
    {
        context.rax = EditedReturnValue; // The only intentional game-context modification.
        reinterpret_cast<StateClobberFunction>(user)();
        InterlockedIncrement(&extendedStateCallbacks);
    }

    void TestMidHookPreservesExtendedMachineState()
    {
        using namespace gloss::gameplay;
        const bool avx = AvxStateEnabled();
        Region functionMemory(4096);
        Region callbackMemory(4096);
        CHECK(static_cast<bool>(functionMemory));
        CHECK(static_cast<bool>(callbackMemory));
        if (!functionMemory || !callbackMemory)
            return;

        auto functionCode = BuildExtendedStateFunction(avx);
        const auto hookOffset = functionCode.hookOffset;

        const auto clobberCode = BuildStateClobberThunk(avx);
        std::memcpy(functionMemory.data, functionCode.bytes.data(), functionCode.bytes.size());
        std::memcpy(callbackMemory.data, clobberCode.data(), clobberCode.size());
        DWORD oldProtection{};
        const bool functionIsExecutable = VirtualProtect(functionMemory.data, functionMemory.size, PAGE_EXECUTE_READ, &oldProtection) != FALSE;
        const bool callbackIsExecutable = VirtualProtect(callbackMemory.data, callbackMemory.size, PAGE_EXECUTE_READ, &oldProtection) != FALSE;
        CHECK(functionIsExecutable);
        CHECK(callbackIsExecutable);
        if (!functionIsExecutable || !callbackIsExecutable)
            return;
        FlushInstructionCache(GetCurrentProcess(), functionMemory.data, functionCode.bytes.size());
        FlushInstructionCache(GetCurrentProcess(), callbackMemory.data, clobberCode.size());

        auto function = reinterpret_cast<ExtendedStateFunction>(functionMemory.data);
        ExtendedStateSnapshot baseline{};
        CHECK(function(&baseline) == 0);
        CHECK(baseline.returnValue == 0);
        CHECK(baseline.mxcsrAfter == FunctionMxcsr);
        const auto beforeFlags = baseline.flagsBefore & ArithmeticFlags;
        CHECK(beforeFlags == ((1ull << 2) | (1ull << 6))); // CMP 0,0 => PF=1, ZF=1; CF/AF/SF/OF=0.
        CHECK((baseline.flagsAfter & ArithmeticFlags) == beforeFlags);
        CHECK(std::memcmp(baseline.xmm0.data(), ExpectedYmm0.data(), baseline.xmm0.size()) == 0);
        CHECK(std::memcmp(baseline.xmm6.data(), ExpectedYmm6.data(), baseline.xmm6.size()) == 0);
        if (avx)
        {
            CHECK(std::memcmp(baseline.ymm0.data(), ExpectedYmm0.data(), baseline.ymm0.size()) == 0);
            CHECK(std::memcmp(baseline.ymm6.data(), ExpectedYmm6.data(), baseline.ymm6.size()) == 0);
        }

        std::string error;
        InterlockedExchange(&extendedStateCallbacks, 0);
        auto hook = MidHook::Create(static_cast<unsigned char *>(functionMemory.data) + hookOffset,
                                    SetContextAndClobberExtendedState, callbackMemory.data, error);
        CHECK(static_cast<bool>(hook));
        if (!hook)
        {
            std::cerr << "Extended-state MidHook::Create failed: " << error << '\n';
            return;
        }

        ExtendedStateSnapshot hooked{};
        CHECK(function(&hooked) == EditedReturnValue);
        CHECK(extendedStateCallbacks == 1);
        CHECK(hooked.returnValue == EditedReturnValue);
        CHECK((hooked.flagsAfter & ArithmeticFlags) == (hooked.flagsBefore & ArithmeticFlags));
        CHECK(hooked.mxcsrAfter == FunctionMxcsr);
        CHECK(std::memcmp(hooked.xmm0.data(), ExpectedYmm0.data(), hooked.xmm0.size()) == 0);
        CHECK(std::memcmp(hooked.xmm6.data(), ExpectedYmm6.data(), hooked.xmm6.size()) == 0);
        if (avx)
        {
            CHECK(std::memcmp(hooked.ymm0.data(), ExpectedYmm0.data(), hooked.ymm0.size()) == 0);
            CHECK(std::memcmp(hooked.ymm6.data(), ExpectedYmm6.data(), hooked.ymm6.size()) == 0);
        }
        hook.reset();
        ExtendedStateSnapshot afterRemoval{};
        CHECK(function(&afterRemoval) == 0);
    }

    struct ActorFixture
    {
        Region actor{4096};
        Region vtable{4096};
        Region locator{4096};
        Region attributes{4096};

        explicit operator bool() const { return actor && vtable && locator && attributes; }
        void Initialize()
        {
            *actor.at<std::uintptr_t>() = reinterpret_cast<std::uintptr_t>(vtable.data);
            *locator.at<std::uintptr_t>(0x10) = reinterpret_cast<std::uintptr_t>(actor.data);
        }
        float &Value(unsigned attribute) { return *attributes.at<float>(0x20 + attribute * sizeof(float)); }
    };

    void SetupGetterContext(gloss::gameplay::Context &context, std::uintptr_t attributes, unsigned attribute)
    {
        context = {};
        context.rax = attributes;
        context.rcx = attribute;
    }

    float GetThroughAttributeHook(gloss::gameplay::Context &context, std::uintptr_t attributes, unsigned attribute)
    {
        SetupGetterContext(context, attributes, attribute);
        gloss::gameplay::attributes::ReadAttribute(context, reinterpret_cast<void *>(0x100000));
        return context.xmm[0].f[0];
    }

    struct AttributeEventCase
    {
        FeatureId feature;
        unsigned attribute;
        float original;
        float desired;
        float setting;
    };

    void SetAttributeChangeContext(gloss::gameplay::Context &context, std::uintptr_t locator,
                                   std::uintptr_t frame, unsigned attribute, float value)
    {
        context = {};
        context.rdi = locator;
        context.rbp = frame;
        *reinterpret_cast<std::uint8_t *>(frame - 0x18) = static_cast<std::uint8_t>(attribute);
        *reinterpret_cast<float *>(frame - 0x20) = value;
    }

    void SetStaminaChangeContext(gloss::gameplay::Context &context, std::uintptr_t locator, std::uintptr_t frame, float staminaValue)
    {
        SetAttributeChangeContext(context, locator, frame, 158, staminaValue);
    }

    void TestAttributeSessionAndQueuedRestoration()
    {
        using namespace gloss::gameplay;
        using namespace gloss::gameplay::attributes;
        ResetGameplayMocks();

        ActorFixture player;
        ActorFixture npc;
        Region syntheticStack(8192);
        Region changeFrame(4096);
        CHECK(static_cast<bool>(player));
        CHECK(static_cast<bool>(npc));
        CHECK(static_cast<bool>(syntheticStack));
        CHECK(static_cast<bool>(changeFrame));
        if (!player || !npc || !syntheticStack || !changeFrame)
            return;
        player.Initialize();
        npc.Initialize();

        player.Value(8) = 100.f;  // Stamina maximum, used when replaying stamina notifications.
        player.Value(151) = 83.f; // HP
        player.Value(152) = 37.f; // MP
        player.Value(158) = 50.f; // Stamina
        npc.Value(151) = 61.f;
        npc.Value(152) = 24.f;

        // Capture occurs immediately before the virtual getter call. This synthetic
        // stack reserves the return slot that the original call will populate.
        auto *capture = syntheticStack.at<Context>(4096);
        auto *playerGetter = syntheticStack.at<Context>(2048);
        constexpr std::uintptr_t virtualReturn = 0x12345678;
        *capture = {};
        capture->rsi = reinterpret_cast<std::uintptr_t>(player.locator.data);
        capture->rdx = 151;
        capture->resume = virtualReturn;
        CapturePlayer(*capture, reinterpret_cast<void *>(virtualReturn));

        mockEnabled[Index(FeatureId::InfiniteHealth)] = true;
        mockEnabled[Index(FeatureId::InfiniteMana)] = true;
        const auto hp = GetThroughAttributeHook(*playerGetter, reinterpret_cast<std::uintptr_t>(player.attributes.data), 151);
        const auto mp = GetThroughAttributeHook(*playerGetter, reinterpret_cast<std::uintptr_t>(player.attributes.data), 152);
        CHECK(hp == 999999.f);
        CHECK(mp == 999999.f);
        CHECK(HasPlayer());
        CHECK(PlayerActor() == reinterpret_cast<std::uintptr_t>(player.actor.data));

        // An NPC's distinct attribute object is not the captured player session and
        // must retain its stored values despite player-only toggles being active.
        const auto npcHp = GetThroughAttributeHook(*playerGetter, reinterpret_cast<std::uintptr_t>(npc.attributes.data), 151);
        const auto npcMp = GetThroughAttributeHook(*playerGetter, reinterpret_cast<std::uintptr_t>(npc.attributes.data), 152);
        CHECK(npcHp == 61.f);
        CHECK(npcMp == 24.f);

        constexpr std::array<AttributeEventCase, 9> queued{{{FeatureId::WillMultiplier, 199, 111.f, 10000.f, 2.f},
                                                            {FeatureId::ExperienceMultiplier, 198, 112.f, 20000.f, 3.f},
                                                            {FeatureId::MaximumHealth, 101, 113.f, 700.f, 700.f},
                                                            {FeatureId::MaximumMana, 102, 114.f, 701.f, 701.f},
                                                            {FeatureId::MaximumStamina, 108, 115.f, 702.f, 702.f},
                                                            {FeatureId::ChillResistance, 132, 116.f, 703.f, 703.f},
                                                            {FeatureId::BurnResistance, 133, 117.f, 704.f, 704.f},
                                                            {FeatureId::PoisonResistance, 134, 118.f, 705.f, 705.f},
                                                            {FeatureId::ShockResistance, 135, 119.f, 706.f, 706.f}}};
        for (const auto &item : queued)
        {
            player.Value(item.attribute) = item.original;
            mockEnabled[Index(item.feature)] = true;
            mockValues[Index(item.feature)] = item.setting;
        }

        auto *eventContext = changeFrame.at<Context>(512);
        auto frame = reinterpret_cast<std::uintptr_t>(changeFrame.data) + 2048;
        std::size_t dispatched{};
        auto runStaminaNotification = [&](float stamina)
        {
            SetStaminaChangeContext(*eventContext, reinterpret_cast<std::uintptr_t>(player.locator.data), frame, stamina);
            ChangeAttribute(*eventContext, nullptr);
            const auto attribute = *reinterpret_cast<std::uint8_t *>(frame - 0x18);
            const auto value = *reinterpret_cast<float *>(frame - 0x20);
            if (attribute != 158)
            {
                player.Value(attribute) = value; // Simulate the game's event handler storing the dispatched value.
                ++dispatched;
            }
            (void)GetThroughAttributeHook(*playerGetter, reinterpret_cast<std::uintptr_t>(player.attributes.data), 158);
            return std::pair<unsigned, float>{attribute, value};
        };

        for (const auto &item : queued)
        {
            const auto [attribute, value] = runStaminaNotification(50.f);
            CHECK(attribute == item.attribute);
            CHECK(value == item.desired);
        }
        CHECK(dispatched == queued.size());
        for (const auto &item : queued)
            CHECK(player.Value(item.attribute) == item.desired);

        // While each feature is enabled, a legitimate game event may change its
        // underlying base. Capture that incoming value as the new disable baseline,
        // then verify the next stamina notification reapplies the active setting.
        std::array<float, queued.size()> latestOriginal{};
        dispatched = 0;
        for (std::size_t i = 0; i < queued.size(); ++i)
        {
            const auto &item = queued[i];
            const float gameUpdatedBase = item.original + 100.f;
            latestOriginal[i] = gameUpdatedBase;
            SetAttributeChangeContext(*eventContext, reinterpret_cast<std::uintptr_t>(player.locator.data),
                                      frame, item.attribute, gameUpdatedBase);
            ChangeAttribute(*eventContext, nullptr);
            const auto eventAttribute = *reinterpret_cast<std::uint8_t *>(frame - 0x18);
            const auto eventValue = *reinterpret_cast<float *>(frame - 0x20);
            CHECK(eventAttribute == item.attribute);
            CHECK(eventValue == gameUpdatedBase);
            player.Value(item.attribute) = eventValue; // Emulate the game's original event storage.
            CHECK(player.Value(item.attribute) == gameUpdatedBase);

            const auto [queuedAttribute, queuedValue] = runStaminaNotification(50.f);
            CHECK(queuedAttribute == item.attribute);
            CHECK(queuedValue == item.desired);
            CHECK(player.Value(item.attribute) == item.desired);
        }
        CHECK(dispatched == queued.size());

        // Turning the features off queues the captured originals through the same
        // nine stamina events; restoration must use the latest game event values.
        mockEnabled[Index(FeatureId::InfiniteHealth)] = false;
        mockEnabled[Index(FeatureId::InfiniteMana)] = false;
        for (const auto &item : queued)
            mockEnabled[Index(item.feature)] = false;
        dispatched = 0;
        for (std::size_t i = 0; i < queued.size(); ++i)
        {
            const auto &item = queued[i];
            const auto [attribute, value] = runStaminaNotification(50.f);
            CHECK(attribute == item.attribute);
            CHECK(value == latestOriginal[i]);
        }
        CHECK(dispatched == queued.size());
        for (std::size_t i = 0; i < queued.size(); ++i)
            CHECK(player.Value(queued[i].attribute) == latestOriginal[i]);

        const auto restoredHp = GetThroughAttributeHook(*playerGetter, reinterpret_cast<std::uintptr_t>(player.attributes.data), 151);
        const auto restoredMp = GetThroughAttributeHook(*playerGetter, reinterpret_cast<std::uintptr_t>(player.attributes.data), 152);
        CHECK(restoredHp == 83.f);
        CHECK(restoredMp == 37.f);
    }
}

namespace gloss
{
    void Log(std::string_view) {}
    const std::filesystem::path &DataDirectory() { return ::testDataDirectory; }
}

namespace gloss::gameplay
{
    bool Enabled(features::Id id) noexcept { return mockEnabled[Index(id)]; }
    float Value(features::Id id) noexcept { return mockValues[Index(id)]; }
}

int main()
{
    std::error_code fileError;
    testDataDirectory = std::filesystem::temp_directory_path() /
                        (L"GlossTrainerGameplayTests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(testDataDirectory, fileError);
    if (fileError)
    {
        std::cerr << "Could not prepare isolated gameplay-test data directory: " << fileError.message() << '\n';
        return 1;
    }
    std::filesystem::create_directories(testDataDirectory, fileError);
    if (fileError)
    {
        std::cerr << "Could not create isolated gameplay-test data directory: " << fileError.message() << '\n';
        return 1;
    }

    TestHotkeyConfigurationPersistenceAndCapture();
    TestMenuHotkeyConfigurationPersistenceAndCapture();
    TestFeatureCatalogAndInputStaging();
    TestFeatureAvailabilityAndExactHotkeys();
    TestUpdateRequirementRestoresAndBlocksFeatures();
    TestUpdateRequirementWaitsForInFlightWrite();
    TestUpdateRequirementRestorationFailure();
    TestPendingActionBoundariesAndConcurrency();
    TestPatternParsingAndScanning();
    TestPatternSkipsNoAccessAndGuardPages();

    const auto minHookStatus = MH_Initialize();
    CHECK(minHookStatus == MH_OK || minHookStatus == MH_ERROR_ALREADY_INITIALIZED);
    if (minHookStatus == MH_OK || minHookStatus == MH_ERROR_ALREADY_INITIALIZED)
    {
        TestRealMidHookTrampolineAndCleanup();
        TestMidHookPreservesExtendedMachineState();
        TestAttributeSessionAndQueuedRestoration();
        if (minHookStatus == MH_OK)
            CHECK(MH_Uninitialize() == MH_OK);
    }

    if (failures)
    {
        std::cerr << failures << " gameplay test assertion(s) failed\n";
        WritePrivateProfileStringW(nullptr, nullptr, nullptr,
                                   (testDataDirectory / L"hotkeys.ini").c_str());
        std::filesystem::remove_all(testDataDirectory, fileError);
        return 1;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr,
                               (testDataDirectory / L"hotkeys.ini").c_str());
    std::filesystem::remove_all(testDataDirectory, fileError);
    std::cout << "GlossTrainer gameplay unit tests passed\n";
    return 0;
}
