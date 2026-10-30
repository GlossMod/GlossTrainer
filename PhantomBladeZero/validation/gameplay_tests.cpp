#include "../src/gameplay/backend.hpp"
#include "../src/gameplay/item_quantity.hpp"
#include "../src/gameplay/toggle_settings.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using namespace gloss::gameplay;
    int failures{};
    std::filesystem::path settingsRoot;

#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; ++failures; } } while (false)

    struct Shape
    {
        std::vector<ParamType> parameters;
        std::size_t fixedStringCount{};
    };

    void TestCatalogMatchesRecoveredNativeContracts()
    {
        const std::map<std::string, Shape> expected{
            {"Native_InfiniteShachi", {{}, 1}},
            {"Native_InfiniteDarkSource", {{}, 1}},
            {"Native_PerfectDeflect", {{}, 1}}, {"Native_PerfectDodge", {{}, 1}},
            {"GM_GetHealth", {{}, 0}}, {"Native_SetMaxHealth", {{ParamType::Float}, 0}},
            {"GM_FullHeal", {{}, 0}}, {"GM_State", {{}, 3}},
            {"GM_InfiniteMedicine", {{}, 0}}, {"GM_InfiniteUltimateShachi", {{}, 0}},
            {"GM_FillAllMaterilas", {{}, 0}}, {"GM_GiveMedicine", {{ParamType::Name}, 0}},
            {"GM_GiveAccessory", {{ParamType::Name}, 0}},
            {"UpgradeMainWeapon", {{ParamType::Name, ParamType::Int}, 0}},
            {"UpgradeMainWeaponMax", {{ParamType::Name}, 0}},
            {"UpgradeAllMainWeaponsMax", {{}, 0}},
            {"UpgradeShaChiWeapon", {{ParamType::Name, ParamType::Int}, 0}},
            {"UpgradeShaChiWeaponMax", {{ParamType::Name}, 0}},
            {"UpgradeAllShaChiWeaponsMax", {{}, 0}},
            {"UpgradeMedicine", {{ParamType::Name, ParamType::Int}, 0}},
            {"UpgradeMedicineMax", {{ParamType::Name}, 0}},
            {"UpgradeAllMedicinesMax", {{}, 0}},
            {"UpgradeAccessory", {{ParamType::Name, ParamType::Int}, 0}},
            {"GM_ActivateTalent", {{ParamType::Name}, 0}},
            {"GM_ActivateAllTalent", {{}, 0}}, {"GM_DiscoverAllEnemies", {{}, 0}},
            {"GM_GetPlayerLocation", {{}, 0}}, {"GM_SavePosition", {{ParamType::String}, 0}},
            {"GM_MoveTo", {{ParamType::String, ParamType::Float, ParamType::Float, ParamType::String}, 0}},
            {"GM_StopMove", {{}, 0}}, {"GM_IsMoveToComplete", {{}, 0}},
            {"GM_Travel", {{ParamType::String, ParamType::String, ParamType::String}, 0}},
            {"GM_ReloadMapAtPosition", {{}, 0}}, {"GM_IsTravelComplete", {{}, 0}},
            {"GM_SetCycleCount", {{ParamType::Int}, 0}}
        };
        const auto* grantItem = FindFeature("grant_item");
        CHECK(grantItem != nullptr);
        if (grantItem) {
            CHECK(grantItem->functionName == "Native_GrantItem");
            CHECK(grantItem->params.size() == 2);
            if (grantItem->params.size() == 2) {
                CHECK(grantItem->params[0].type == ParamType::String);
                CHECK(grantItem->params[1].type == ParamType::Int);
                CHECK(grantItem->params[1].minimum == 1 && grantItem->params[1].maximum == MaxItemQuantity);
            }
        }
        std::map<std::string, std::size_t> seen;
        for (const auto& feature : Catalog()) {
            if (feature.functionName.empty() || feature.functionName == "SetGlobalTimeDilation" ||
                feature.functionName == "Native_GrantItem" || feature.functionName == "Native_ExtendedFeature" ||
                feature.functionName == "Native_ExtendedAction")
                continue;
            const auto contract = expected.find(feature.functionName);
            CHECK(contract != expected.end());
            if (contract == expected.end())
                continue;

            const auto argumentCount = feature.fixedArguments.size() + feature.params.size();
            CHECK(argumentCount == contract->second.fixedStringCount + contract->second.parameters.size());
            CHECK(feature.fixedArguments.size() == contract->second.fixedStringCount);
            for (const auto& fixed : feature.fixedArguments)
                CHECK(!fixed.empty());
            if (feature.params.size() == contract->second.parameters.size()) {
                for (std::size_t i = 0; i < feature.params.size(); ++i)
                    CHECK(feature.params[i].type == contract->second.parameters[i]);
            }
            ++seen[feature.functionName];
        }
        for (const auto& [name, shape] : expected) {
            (void)shape;
            CHECK(seen[name] > 0);
        }

        const auto* gameSpeed = FindFeature("game_speed");
        CHECK(gameSpeed != nullptr);
        if (gameSpeed) {
            CHECK(gameSpeed->functionName == "SetGlobalTimeDilation");
            CHECK(gameSpeed->page == 0 && gameSpeed->category == 4);
            CHECK(gameSpeed->params.size() == 1);
            CHECK(gameSpeed->params[0].type == ParamType::Float);
            CHECK(gameSpeed->params[0].minimum == 0.1 && gameSpeed->params[0].maximum == 3.0);
        }
        CHECK(FindFeature("grant_item") != nullptr);
        CHECK(FindFeature("godmode") == nullptr);
        CHECK(FindFeature("state_godmode_on") == nullptr);
        CHECK(FindFeature("state_godmode_off") == nullptr);
    }

    void TestCatalogParameterValidationAndFixedArguments()
    {
        std::string error;
        const auto* health = FindFeature("set_max_health");
        CHECK(health && !ValidateParameters(*health, {"0"}, error));
        CHECK(health && ValidateParameters(*health, {"1"}, error));
        CHECK(health && ValidateParameters(*health, {"100000000"}, error));
        CHECK(health && !ValidateParameters(*health, {"nan"}, error));
        CHECK(health && !ValidateParameters(*health, {"100000001"}, error));
        CHECK(health && !ValidateParameters(*health, {"1junk"}, error));
        CHECK(health && !ValidateParameters(*health, {}, error));

        const auto* give = FindFeature("give_medicine");
        CHECK(give && give->params.size() == 1);
        CHECK(give && ValidateParameters(*give, {"0x1234:2"}, error));
        CHECK(give && !ValidateParameters(*give, {"0x100000000"}, error));
        CHECK(give && !ValidateParameters(*give, {"medicine-name"}, error));

        const auto* state = FindFeature("state_invulnerable_on");
        CHECK(state && state->fixedArguments == std::vector<std::string>({"preset", "invulnerable", "on"}));
        CHECK(state && state->params.empty());

        const auto* save = FindFeature("save_position");
        CHECK(save && ValidateParameters(*save, {"slot"}, error));
        CHECK(save && !ValidateParameters(*save, {std::string(65, 'x')}, error));

        const auto* travel = FindFeature("travel");
        CHECK(travel && !ValidateParameters(*travel, {"0,0,0", "0,0,0", ""}, error));
        CHECK(travel && ValidateParameters(*travel, {"0,0,0", "0,0,0", "MapName"}, error));

        const auto* speed = FindFeature("game_speed");
        CHECK(speed && ValidateParameters(*speed, {"0.1"}, error));
        CHECK(speed && ValidateParameters(*speed, {"3"}, error));
        CHECK(speed && !ValidateParameters(*speed, {"0.09"}, error));
        CHECK(speed && !ValidateParameters(*speed, {"3.01"}, error));
    }

    void TestToggleCatalogHasUniqueReversibleStatePairs()
    {
        const auto& toggles = ToggleCatalog();
        CHECK(toggles.size() == 27);
        std::map<std::string, bool> ids;
        std::map<std::string, bool> featureIds;
        for (const auto& toggle : toggles) {
            CHECK(!toggle.id.empty());
            CHECK(!toggle.onFeatureId.empty());
            CHECK(!toggle.offFeatureId.empty());
            CHECK(toggle.onFeatureId != toggle.offFeatureId);
            CHECK(ids.emplace(toggle.id, true).second);
            CHECK(featureIds.emplace(toggle.onFeatureId, true).second);
            CHECK(featureIds.emplace(toggle.offFeatureId, true).second);

            const auto* on = FindFeature(toggle.onFeatureId);
            const auto* off = FindFeature(toggle.offFeatureId);
            CHECK(on != nullptr && off != nullptr);
            if (!on || !off)
                continue;
            CHECK(on->functionName == off->functionName);
            CHECK(off->params.empty());
            CHECK(!on->persistent && !off->persistent);
            if (on->functionName == "Native_ExtendedFeature") {
                CHECK(on->fixedArguments == std::vector<std::string>({toggle.id, "on"}));
                CHECK(off->fixedArguments == std::vector<std::string>({toggle.id, "off"}));
                CHECK(EquivalentParameters(*on, DefaultParameters(*on), DefaultParameters(*on)));
                continue;
            }
            CHECK(on->params.empty());
            if (on->functionName.starts_with("Native_")) {
                CHECK(on->fixedArguments == std::vector<std::string>{"on"});
                CHECK(off->fixedArguments == std::vector<std::string>{"off"});
                continue;
            }
            CHECK(on->functionName == "GM_State");
            CHECK(on->fixedArguments.size() == 3 && off->fixedArguments.size() == 3);
            if (on->fixedArguments.size() == 3 && off->fixedArguments.size() == 3) {
                CHECK(on->fixedArguments[0] == "preset" && off->fixedArguments[0] == "preset");
                CHECK(on->fixedArguments[1] == toggle.id && off->fixedArguments[1] == toggle.id);
                CHECK(on->fixedArguments[2] == "on" && off->fixedArguments[2] == "off");
            }
        }
        CHECK(!ids.contains("immortal"));
        CHECK(!FindFeature("state_immortal_on"));
        CHECK(!FindFeature("state_immortal_off"));
        CHECK(!FindFeature("set_health"));
        CHECK(ids.contains("invulnerable"));
        CHECK(ids.contains("infinite_dark_source"));
        CHECK(ids.contains("infinite_shachi"));
        CHECK(ids.contains("perfect_deflect"));
        CHECK(ids.contains("perfect_dodge"));
        CHECK(!FindFeature("stamina"));
        CHECK(!ids.contains("godmode"));
    }

    void TestPublishedToggleStatesAreVisibleAndClearedOnSessionChange()
    {
        Backend backend;
        backend.SetRuntimeState(true, "ready", "session-toggle-a");
        backend.PublishToggleStates({
            {"infinite_dark_source", true, true, "active"},
            {"invulnerable", true, false, "inactive"}
        });

        const auto published = backend.GetSnapshot();
        CHECK(published.toggleStates.size() == 2);
        if (published.toggleStates.size() == 2) {
            CHECK(published.toggleStates[0].id == "infinite_dark_source");
            CHECK(published.toggleStates[0].known && published.toggleStates[0].enabled);
            CHECK(published.toggleStates[0].reason == "active");
            CHECK(published.toggleStates[1].id == "invulnerable");
            CHECK(published.toggleStates[1].known && !published.toggleStates[1].enabled);
        }

        backend.SetRuntimeState(true, "new session", "session-toggle-b");
        const auto invalidated = backend.GetSnapshot();
        CHECK(invalidated.toggleStates.empty());
    }

    void Ready(Backend& backend, std::initializer_list<std::string_view> ids)
    {
        backend.SetRuntimeState(true, "ready", "session-a");
        for (const auto id : ids)
            backend.SetCapability(id, true);
    }

    void TestRemovedClearFeaturesCannotBeSubmitted()
    {
        Backend backend;
        // A stale capability advertisement must not restore deleted features.
        Ready(backend, {"auto_kill_nearby", "kill_all_npc"});
        for (const auto id : {"auto_kill_nearby", "kill_all_npc", "GM_AutoKillNearby", "GM_KillAllNPC"}) {
            CHECK(FindFeature(id) == nullptr);
            const auto submitted = backend.Submit(id);
            CHECK(!submitted.accepted && submitted.message == "未知功能");
        }
        CHECK(!backend.Submit("auto_kill_nearby", {"on", "500"}).accepted);
        for (const auto& feature : Catalog()) {
            CHECK(feature.functionName != "GM_AutoKillNearby");
            CHECK(feature.functionName != "GM_KillAllNPC");
        }
        const auto snapshot = backend.GetSnapshot();
        CHECK(snapshot.capabilities.size() == Catalog().size());
        for (const auto& capability : snapshot.capabilities) {
            CHECK(capability.featureId != "auto_kill_nearby");
            CHECK(capability.featureId != "kill_all_npc");
        }
        CHECK(snapshot.pendingCount == 0 && snapshot.requests.empty());
        int calls{};
        CHECK(backend.Pump([&](const Request&) {
            ++calls;
            return ExecutionResult{RequestState::Dispatched, {}};
        }, 1) == 0);
        CHECK(calls == 0);
    }

    const RequestRecord* FindRecord(const Snapshot& snapshot, std::uint64_t id)
    {
        for (const auto& record : snapshot.requests)
            if (record.request.requestId == id)
                return &record;
        return nullptr;
    }

    void TestSubmissionValidationQueueCapacityAndFixedPayload()
    {
        Backend backend(1, 8);
        CHECK(!backend.Submit("set_max_health", {"100"}).accepted); // Runtime unavailable.
        Ready(backend, {"set_max_health", "state_invulnerable_on"});

        const auto first = backend.Submit("set_max_health"); // Uses catalog default.
        CHECK(first.accepted);
        CHECK(!backend.Submit("set_max_health", {"-1"}).accepted);
        CHECK(!backend.Submit("unknown", {}).accepted);
        CHECK(!backend.Submit("set_max_health", {"100"}, std::chrono::milliseconds::zero()).accepted);
        CHECK(!backend.Submit("set_max_health", {"100"}, std::chrono::seconds(61)).accepted);
        CHECK(!backend.Submit("state_invulnerable_on", {}).accepted); // Full queue.

        const auto before = backend.GetSnapshot();
        CHECK(before.pendingCount == 1);
        const auto* pending = FindRecord(before, first.requestId);
        CHECK(pending && pending->state == RequestState::Pending);
        CHECK(pending && pending->request.arguments == std::vector<std::string>({"1000"}));

        std::vector<std::string> captured;
        CHECK(backend.Pump([&](const Request& request) {
            captured = request.arguments;
            return ExecutionResult{RequestState::Dispatched, "returned"};
        }, 1) == 1);
        CHECK(captured == std::vector<std::string>({"1000"}));
        const auto after = backend.GetSnapshot();
        const auto* dispatched = FindRecord(after, first.requestId);
        CHECK(dispatched && dispatched->state == RequestState::Dispatched);
        CHECK(dispatched && dispatched->message == "returned");

        const auto state = backend.Submit("state_invulnerable_on");
        CHECK(state.accepted);
        CHECK(backend.Pump([&](const Request& request) {
            captured = request.arguments;
            return ExecutionResult{RequestState::Verified, "read back"};
        }, 1) == 1);
        CHECK(captured == std::vector<std::string>({"preset", "invulnerable", "on"}));
        const auto stateSnapshot = backend.GetSnapshot();
        const auto* verified = FindRecord(stateSnapshot, state.requestId);
        CHECK(verified && verified->state == RequestState::Verified);
    }

    ItemEntry TestItem(std::string key, int maximum = 999, bool available = true)
    {
        return {std::move(key), "测试物品", "测试目录项", "材料", maximum, 0, available,
            available ? "" : "该物品不可获取"};
    }

    void TestItemSubmissionRequiresPreparedAvailableCatalogAndValidQuantity()
    {
        Backend unavailableRuntime;
        CHECK(!unavailableRuntime.SubmitItem("item-a", 1).accepted);

        Backend backend;
        Ready(backend, {"grant_item"});

        // Runtime readiness alone is not enough; the item catalog must be published.
        auto result = backend.SubmitItem("item-a", 1);
        CHECK(!result.accepted && result.message == "物品列表尚未准备好");

        backend.PublishItems({}, true, "空物品列表");
        CHECK(backend.GetSnapshot().itemsReady);
        CHECK(backend.GetSnapshot().items.empty());
        result = backend.SubmitItem("item-a", 1);
        CHECK(!result.accepted && result.message == "所选物品已失效，请重新选择");

        backend.PublishItems({TestItem("item-a", 999, false)}, true, "物品已加载");
        result = backend.SubmitItem("item-a", 1);
        CHECK(!result.accepted && result.message == "该物品不可获取");

        backend.PublishItems({TestItem("item-a")}, true, "物品已加载");
        for (const int quantity : {0, -1, 1000}) {
            result = backend.SubmitItem("item-a", quantity);
            CHECK(!result.accepted);
        }

        backend.PublishItems({TestItem("single-use", 1)}, true, "物品已加载");
        result = backend.SubmitItem("single-use", 2);
        CHECK(!result.accepted && result.message == "数量超过该物品单次获取上限");
        result = backend.SubmitItem("single-use", 1);
        CHECK(result.accepted);
    }

    void TestDirectSubmitCannotBypassItemCatalogValidation()
    {
        Backend backend;
        Ready(backend, {"grant_item"});
        backend.PublishItems({TestItem("available"), TestItem("locked", 999, false), TestItem("single", 1)},
            true, "物品已加载");

        CHECK(!backend.Submit("grant_item", {"missing", "1"}).accepted);
        CHECK(!backend.Submit("grant_item", {"locked", "1"}).accepted);
        CHECK(!backend.Submit("grant_item", {"single", "2"}).accepted);
        CHECK(!backend.Submit("grant_item", {"available", "0"}).accepted);

        const auto direct = backend.Submit("grant_item", {"available", "3"});
        CHECK(direct.accepted);
        const auto snapshot = backend.GetSnapshot();
        const auto* record = FindRecord(snapshot, direct.requestId);
        CHECK(record && record->request.arguments == std::vector<std::string>({"available", "3"}));
    }

    void TestItemQuantityInputRejectsOverflowAndPartialNumbers()
    {
        for (const int expected : {1, 999, 1000, 1000000, MaxItemQuantity}) {
            int parsed{};
            CHECK(ParseItemQuantity(std::to_string(expected), parsed));
            CHECK(parsed == expected);
        }
        for (const std::string_view text : {"", "0", "-1", "+1", "1.5", "1e3", "1x", " 1", "1 ",
                "2147483648", "9999999999999999999999999999999999"}) {
            int parsed = 42;
            CHECK(!ParseItemQuantity(text, parsed));
            CHECK(parsed == 42);
        }
        CHECK(ItemQuantityFitsInventory(0, MaxItemQuantity));
        CHECK(ItemQuantityFitsInventory(MaxItemQuantity - 1, 1));
        CHECK(!ItemQuantityFitsInventory(MaxItemQuantity, 1));
        CHECK(!ItemQuantityFitsInventory(1, MaxItemQuantity));
        CHECK(!ItemQuantityFitsInventory(-1, 1));
        CHECK(!ItemQuantityFitsInventory(static_cast<std::int64_t>(MaxItemQuantity) + 1, 1));
        CHECK(!ItemQuantityFitsInventory(0, 0));
        CHECK(!ItemQuantityFitsInventory(0, -1));
    }

    void TestDustAcceptsLargeQuantitiesWithoutChangingOrdinaryItemLimits()
    {
        constexpr std::string_view dustKey = "resource:consciousness_dust";
        for (const int quantity : {1000, 1000000, MaxItemQuantity}) {
            Backend backend;
            Ready(backend, {"grant_item"});
            backend.PublishItems({TestItem(std::string(dustKey), MaxItemQuantity)}, true, "资源已加载");
            const auto result = backend.SubmitItem(dustKey, quantity);
            CHECK(result.accepted);
            std::vector<std::string> arguments;
            CHECK(backend.Pump([&](const Request& request) {
                arguments = request.arguments;
                return ExecutionResult{RequestState::Dispatched, "测试执行器已接收"};
            }, 1) == 1);
            CHECK(arguments == std::vector<std::string>({std::string(dustKey), std::to_string(quantity)}));
        }

        Backend backend;
        Ready(backend, {"grant_item"});
        auto dust = TestItem(std::string(dustKey), MaxItemQuantity);
        dust.owned = 25555901;
        backend.PublishItems({dust, TestItem("ordinary"), TestItem("unique", 1)}, true, "物品已加载");
        const int remaining = MaxItemQuantity - static_cast<int>(dust.owned);
        CHECK(backend.SubmitItem(dustKey, remaining).accepted);
        const auto overflow = backend.SubmitItem(dustKey, remaining + 1);
        CHECK(!overflow.accepted && overflow.message == "当前数量加请求数量会溢出，未执行");
        CHECK(!backend.SubmitItem(dustKey, MaxItemQuantity).accepted);
        CHECK(!backend.Submit("grant_item", {std::string(dustKey), "2147483648"}).accepted);
        CHECK(!backend.Submit("grant_item", {std::string(dustKey), "1000x"}).accepted);
        CHECK(backend.SubmitItem("ordinary", 999).accepted);
        const auto ordinaryTooLarge = backend.SubmitItem("ordinary", 1000);
        CHECK(!ordinaryTooLarge.accepted && ordinaryTooLarge.message == "数量超过该物品单次获取上限");
        CHECK(!backend.SubmitItem("unique", 2).accepted);
    }

    void TestItemRequestsAndCatalogAreInvalidatedOnSessionChange()
    {
        Backend backend;
        Ready(backend, {"grant_item"});
        backend.PublishItems({TestItem("session-item")}, true, "会话 A 物品");
        const auto queued = backend.SubmitItem("session-item", 4);
        CHECK(queued.accepted);

        backend.SetRuntimeState(true, "new world", "session-b");
        const auto snapshot = backend.GetSnapshot();
        CHECK(snapshot.items.empty());
        CHECK(!snapshot.itemsReady);
        CHECK(snapshot.pendingCount == 0);
        const auto* cancelled = FindRecord(snapshot, queued.requestId);
        CHECK(cancelled && cancelled->state == RequestState::Cancelled);
        CHECK(!backend.SubmitItem("session-item", 1).accepted);
    }

    void TestSameSessionRebindRequiresFreshItemPublication()
    {
        Backend backend;
        backend.SetRuntimeState(true, "ready", "same-session");
        backend.SetCapability("grant_item", true, "verified");
        backend.PublishItems({TestItem("same-item")}, true, "verified catalog");
        CHECK(backend.ItemsReady());
        backend.SetRuntimeState(false, "rebind", "same-session");
        backend.SetRuntimeState(true, "ready again", "same-session");
        backend.SetCapability("grant_item", true, "verified again");
        CHECK(!backend.ItemsReady());
        CHECK(!backend.SubmitItem("same-item", 1).accepted);
        backend.PublishItems({TestItem("same-item")}, false, "cached preview");
        CHECK(!backend.ItemsReady());
        CHECK(!backend.SubmitItem("same-item", 1).accepted);
        backend.PublishItems({TestItem("same-item")}, true, "fresh readback");
        CHECK(backend.ItemsReady());
        CHECK(backend.SubmitItem("same-item", 1).accepted);
    }

    void TestItemDispatchIsNotReportedAsVerifiedWithoutReadback()
    {
        Backend backend;
        Ready(backend, {"grant_item"});
        backend.PublishItems({TestItem("unverified-item")}, true, "物品已加载");
        const auto submitted = backend.SubmitItem("unverified-item", 2);
        CHECK(submitted.accepted);

        std::vector<std::string> captured;
        CHECK(backend.Pump([&](const Request& request) {
            captured = request.arguments;
            return ExecutionResult{RequestState::Dispatched, "调用返回，未回读验证"};
        }, 1) == 1);
        CHECK(captured == std::vector<std::string>({"unverified-item", "2"}));
        const auto snapshot = backend.GetSnapshot();
        const auto* record = FindRecord(snapshot, submitted.requestId);
        CHECK(record && record->state == RequestState::Dispatched);
        CHECK(record && record->state != RequestState::Verified);
    }

    void TestExpirationFailureAndExceptionsNeverRetry()
    {
        Backend backend;
        Ready(backend, {"set_max_health"});
        const auto now = Clock::time_point{} + std::chrono::hours(1);
        const auto expired = backend.Submit("set_max_health", {"25"}, std::chrono::milliseconds(10), now);
        CHECK(expired.accepted);
        int calls{};
        CHECK(backend.Pump([&](const Request&) {
            ++calls;
            return ExecutionResult{RequestState::Dispatched, {}};
        }, 1, now + std::chrono::milliseconds(11)) == 0);
        CHECK(calls == 0);
        const auto expiredSnapshot = backend.GetSnapshot();
        const auto* expiredRecord = FindRecord(expiredSnapshot, expired.requestId);
        CHECK(expiredRecord && expiredRecord->state == RequestState::Expired);

        const auto failed = backend.Submit("set_max_health", {"25"});
        CHECK(failed.accepted);
        CHECK(backend.Pump([&](const Request&) {
            ++calls;
            return ExecutionResult{RequestState::Failed, "guard rejected"};
        }, 1) == 1);
        const auto failedSnapshot = backend.GetSnapshot();
        const auto* failedRecord = FindRecord(failedSnapshot, failed.requestId);
        CHECK(failedRecord && failedRecord->state == RequestState::Failed);
        CHECK(calls == 1);

        const auto thrown = backend.Submit("set_max_health", {"25"});
        CHECK(thrown.accepted);
        CHECK(backend.Pump([&](const Request&) -> ExecutionResult {
            ++calls;
            throw std::runtime_error("test exception");
        }, 1) == 1);
        const auto thrownSnapshot = backend.GetSnapshot();
        const auto* thrownRecord = FindRecord(thrownSnapshot, thrown.requestId);
        CHECK(thrownRecord && thrownRecord->state == RequestState::Indeterminate);
        CHECK(calls == 2); // Each request is considered once and never retried.
        CHECK(backend.Pump([&](const Request&) {
            ++calls;
            return ExecutionResult{RequestState::Dispatched, {}};
        }, 1) == 0);
        CHECK(calls == 2);
    }

    void TestSessionChangeCancelsQueuedAndMarksRunningIndeterminate()
    {
        Backend backend;
        Ready(backend, {"set_max_health"});
        const auto current = backend.Submit("set_max_health", {"30"});
        const auto queued = backend.Submit("set_max_health", {"40"});
        CHECK(current.accepted && queued.accepted);

        CHECK(backend.Pump([&](const Request& request) {
            CHECK(request.requestId == current.requestId);
            backend.SetRuntimeState(true, "new world", "session-b");
            return ExecutionResult{RequestState::Dispatched, "call returned after session changed"};
        }, 2) == 1);

        const auto snapshot = backend.GetSnapshot();
        const auto* running = FindRecord(snapshot, current.requestId);
        const auto* pending = FindRecord(snapshot, queued.requestId);
        CHECK(running && running->state == RequestState::Indeterminate);
        CHECK(pending && pending->state == RequestState::Cancelled);
        CHECK(snapshot.pendingCount == 0);
        CHECK(snapshot.generation > 1);
    }

    void TestConcurrentPumpIsSerializedAndReentrantSubmitWaitsForNextPump()
    {
        Backend backend;
        Ready(backend, {"set_max_health"});
        const auto first = backend.Submit("set_max_health", {"50"});
        CHECK(first.accepted);

        std::mutex mutex;
        std::condition_variable cv;
        bool entered{};
        bool release{};
        bool nestedAccepted{};
        std::atomic<int> calls{};
        std::size_t outerCount{};
        std::thread worker([&] {
            outerCount = backend.Pump([&](const Request&) {
                ++calls;
                const auto nested = backend.Submit("set_max_health", {"60"});
                {
                    std::lock_guard lock(mutex);
                    nestedAccepted = nested.accepted;
                    entered = true;
                }
                cv.notify_one();
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return release; });
                return ExecutionResult{RequestState::Dispatched, "done"};
            }, 2);
        });

        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return entered; });
        }
        CHECK(backend.Pump([&](const Request&) {
            ++calls;
            return ExecutionResult{RequestState::Dispatched, {}};
        }, 2) == 0); // Concurrent pump is ignored.
        {
            std::lock_guard lock(mutex);
            release = true;
        }
        cv.notify_one();
        worker.join();

        CHECK(nestedAccepted);
        CHECK(outerCount == 1); // Requests added while pumping wait for a later tick.
        CHECK(calls == 1);
        CHECK(backend.GetSnapshot().pendingCount == 1);
        CHECK(backend.Pump([&](const Request&) {
            ++calls;
            return ExecutionResult{RequestState::Dispatched, "second tick"};
        }, 1) == 1);
        CHECK(calls == 2);
        const auto snapshot = backend.GetSnapshot();
        CHECK(FindRecord(snapshot, first.requestId)->state == RequestState::Dispatched);
    }

    void TestHistoryRetentionAndBadExecutorState()
    {
        Backend backend(1, 1); // Effective history holds at least queueCapacity + 1.
        Ready(backend, {"set_max_health"});
        std::vector<std::uint64_t> ids;
        for (int i = 0; i < 5; ++i) {
            const auto result = backend.Submit("set_max_health", {std::to_string(i + 1)});
            CHECK(result.accepted);
            ids.push_back(result.requestId);
            backend.Pump([](const Request&) {
                return ExecutionResult{RequestState::Pending, "invalid executor state"};
            }, 1);
        }
        const auto snapshot = backend.GetSnapshot();
        CHECK(snapshot.requests.size() <= 2);
        CHECK(!FindRecord(snapshot, ids.front()));
        if (!snapshot.requests.empty())
            CHECK(snapshot.requests.back().state == RequestState::Indeterminate);
    }

    std::filesystem::path SettingsFile(std::string_view name)
    {
        const auto directory = settingsRoot / name;
        std::filesystem::create_directories(directory);
        return directory / "features.ini";
    }

    std::string ReadSettings(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), {}};
    }

    void WriteSettings(const std::filesystem::path& file, std::string_view text)
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream << text;
        CHECK(stream.good());
    }

    void SeedEnabled(const std::filesystem::path& file)
    {
        auto data = toggle_settings::Defaults();
        data.at("invulnerable") = true;
        std::string error;
        CHECK(toggle_settings::Save(file, data, error));
        CHECK(error.empty());
    }

    TogglePreference Preference(const Backend& backend, std::string_view id = "invulnerable")
    {
        const auto snapshot = backend.GetSnapshot();
        for (const auto& preference : snapshot.togglePreferences)
            if (preference.id == id) return preference;
        CHECK(false);
        return {};
    }

    void TestSettingsDefaultsVerifiedRoundTripAndActionsNeverRestore()
    {
        const auto file = SettingsFile("round-trip");
        Backend backend;
        std::string error;
        CHECK(backend.InitializeSettings(file, &error));
        CHECK(error.empty());
        CHECK(!Preference(backend).enabled);
        CHECK(backend.FlushSettings(&error));
        CHECK(!std::filesystem::exists(file)); // Opening the UI must not write defaults.
        Ready(backend, {"state_invulnerable_on", "full_heal"});
        CHECK(backend.Submit("state_invulnerable_on").accepted);
        CHECK(backend.Pump([](const Request&) {
            return ExecutionResult{RequestState::Verified, "tag matched"};
        }) == 1);
        CHECK(Preference(backend).enabled);
        CHECK(backend.Submit("full_heal").accepted);
        CHECK(backend.Pump([](const Request&) {
            return ExecutionResult{RequestState::Verified, "healed"};
        }) == 1);
        CHECK(backend.FlushSettings(&error));
        const auto saved = ReadSettings(file);
        CHECK(saved.find("invulnerable=1") != std::string::npos);
        CHECK(saved.find("full_heal") == std::string::npos);

        Backend restarted;
        CHECK(restarted.InitializeSettings(file, &error));
        CHECK(Preference(restarted).enabled && Preference(restarted).restorePending);
        CHECK(restarted.GetSnapshot().toggleStates.empty()); // Preference is never a fake read-back.
        Ready(restarted, {"state_invulnerable_on", "full_heal"});
        restarted.PublishToggleStates({{"invulnerable", true, false, "off"}});
        CHECK(restarted.Pump([](const Request& request) {
            CHECK(request.restoration && request.featureId == "state_invulnerable_on");
            return ExecutionResult{RequestState::Verified, "restored"};
        }) == 1);
        CHECK(!Preference(restarted).restorePending);
        CHECK(restarted.Pump([](const Request&) {
            CHECK(false); return ExecutionResult{};
        }) == 0);
    }

    void TestSettingsEveryCatalogToggleRestoresWithItsOwnFixedArguments()
    {
        const auto file = SettingsFile("all-toggles");
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        backend.SetRuntimeState(true, "ready", "all-toggles-a");
        for (const auto& toggle : ToggleCatalog()) {
            backend.SetCapability(toggle.onFeatureId, true);
            CHECK(backend.Submit(toggle.onFeatureId).accepted);
            CHECK(backend.Pump([](const Request&) {
                return ExecutionResult{RequestState::Verified, "verified"};
            }, 1) == 1);
        }
        CHECK(backend.FlushSettings());
        Backend restarted;
        CHECK(restarted.InitializeSettings(file));
        restarted.SetRuntimeState(true, "ready", "all-toggles-b");
        std::vector<ToggleState> states;
        for (const auto& toggle : ToggleCatalog()) {
            restarted.SetCapability(toggle.onFeatureId, true);
            states.push_back({toggle.id, true, false, "off"});
        }
        restarted.PublishToggleStates(std::move(states));
        std::map<std::string, int> restored;
        for (std::size_t i = 0; i < ToggleCatalog().size(); ++i) {
            CHECK(restarted.Pump([&](const Request& request) {
                CHECK(request.restoration);
                const auto* feature = FindFeature(request.featureId);
                auto expected = feature ? feature->fixedArguments : std::vector<std::string>{};
                if (feature) {
                    const auto parameters = DefaultParameters(*feature);
                    expected.insert(expected.end(), parameters.begin(), parameters.end());
                }
                CHECK(feature && expected == request.arguments);
                ++restored[request.featureId];
                return ExecutionResult{RequestState::Verified, "restored"};
            }, 1) == 1);
        }
        CHECK(restored.size() == ToggleCatalog().size());
        for (const auto& toggle : ToggleCatalog()) CHECK(restored[toggle.onFeatureId] == 1);
    }

    void TestSettingsWaitForReadbackAndDoNotRetryExecutedFailures()
    {
        const auto file = SettingsFile("wait-and-failure");
        SeedEnabled(file);
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        int calls{};
        const auto execute = [&](const Request&) { ++calls; return ExecutionResult{RequestState::Failed, "rejected"}; };
        CHECK(backend.Pump(execute) == 0); // No player.
        Ready(backend, {"state_invulnerable_on"});
        CHECK(backend.Pump(execute) == 0); // No state read-back.
        backend.PublishToggleStates({{"invulnerable", false, false, "unknown"}});
        CHECK(backend.Pump(execute) == 0);
        backend.PublishToggleStates({{"invulnerable", true, false, "off"}});
        backend.SetCapability("state_invulnerable_on", false);
        CHECK(backend.Pump(execute) == 0);
        backend.SetCapability("state_invulnerable_on", true);
        CHECK(backend.Pump(execute) == 1);
        CHECK(backend.Pump(execute) == 0);
        CHECK(calls == 1 && Preference(backend).enabled && !Preference(backend).restorePending);

        backend.SetRuntimeState(true, "new world", "session-b");
        backend.SetCapability("state_invulnerable_on", true);
        backend.PublishToggleStates({{"invulnerable", true, false, "new world off"}});
        CHECK(backend.Pump(execute) == 1); // Fresh world can restore the preserved intent once.
        CHECK(backend.Pump(execute) == 0);
        CHECK(calls == 2);
        CHECK(backend.FlushSettings());
        CHECK(ReadSettings(file).find("invulnerable=1") != std::string::npos);
    }

    void TestSettingsAlreadyEnabledDoesNotIssueAnotherEnable()
    {
        const auto file = SettingsFile("already-enabled");
        SeedEnabled(file);
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        Ready(backend, {"state_invulnerable_on"});
        backend.PublishToggleStates({{"invulnerable", true, true, "active"}});
        CHECK(backend.Pump([](const Request&) {
            CHECK(false); return ExecutionResult{};
        }) == 0);
        CHECK(!Preference(backend).restorePending);
    }

    void TestSettingsDisableCancelsWhileUnreadyAndFailedDisableStaysOff()
    {
        const auto file = SettingsFile("disable-unready");
        SeedEnabled(file);
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        const auto cancelled = backend.Submit("state_invulnerable_off");
        CHECK(cancelled.accepted && cancelled.requestId == 0);
        CHECK(!Preference(backend).enabled);
        CHECK(backend.FlushSettings());
        Ready(backend, {"state_invulnerable_on", "state_invulnerable_off"});
        backend.PublishToggleStates({{"invulnerable", true, false, "off"}});
        CHECK(backend.Pump([](const Request&) { CHECK(false); return ExecutionResult{}; }) == 0);

        const auto failedFile = SettingsFile("failed-disable");
        SeedEnabled(failedFile);
        Backend failing;
        CHECK(failing.InitializeSettings(failedFile));
        Ready(failing, {"state_invulnerable_on", "state_invulnerable_off"});
        failing.PublishToggleStates({{"invulnerable", true, true, "on"}});
        CHECK(failing.Submit("state_invulnerable_off").accepted);
        CHECK(failing.Pump([](const Request& request) {
            CHECK(request.featureId == "state_invulnerable_off");
            return ExecutionResult{RequestState::Failed, "disable failed"};
        }) == 1);
        CHECK(!Preference(failing).enabled);
        CHECK(failing.FlushSettings());
        Backend restarted;
        CHECK(restarted.InitializeSettings(failedFile));
        CHECK(!Preference(restarted).enabled && !Preference(restarted).restorePending);
    }

    void TestSettingsDisableSupersedesRunningRestore()
    {
        const auto file = SettingsFile("cancel-running-restore");
        SeedEnabled(file);
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        Ready(backend, {"state_invulnerable_on", "state_invulnerable_off"});
        backend.PublishToggleStates({{"invulnerable", true, false, "off"}});
        CHECK(backend.Pump([&](const Request& request) {
            CHECK(request.restoration && request.featureId == "state_invulnerable_on");
            CHECK(backend.Submit("state_invulnerable_off").accepted);
            backend.PublishToggleStates({{"invulnerable", true, true, "late enable"}});
            return ExecutionResult{RequestState::Verified, "old enable completed"};
        }) == 1);
        CHECK(!Preference(backend).enabled);
        CHECK(backend.Pump([&](const Request& request) {
            CHECK(request.featureId == "state_invulnerable_off");
            backend.PublishToggleStates({{"invulnerable", true, false, "off"}});
            return ExecutionResult{RequestState::Verified, "compensated"};
        }) == 1);
        CHECK(!Preference(backend).enabled && !Preference(backend).restorePending);
        CHECK(backend.FlushSettings());
        CHECK(ReadSettings(file).find("invulnerable=0") != std::string::npos);
    }

    void TestSettingsUnreadyDisableExecutesAfterReadyIfStillEnabled()
    {
        const auto file = SettingsFile("unready-disable-still-active");
        SeedEnabled(file);
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        const auto disabled = backend.Submit("state_invulnerable_off");
        CHECK(disabled.accepted && disabled.requestId == 0);
        CHECK(!Preference(backend).enabled && Preference(backend).restorePending);
        CHECK(backend.FlushSettings());

        Ready(backend, {"state_invulnerable_on", "state_invulnerable_off"});
        // Native switches can survive a pause/loading interval even though the
        // backend was unable to dispatch the user's accepted close at the time.
        backend.PublishToggleStates({{"invulnerable", true, true, "still active"}});
        CHECK(Preference(backend).restorePending);
        CHECK(backend.Pump([&](const Request& request) {
            CHECK(request.restoration && request.featureId == "state_invulnerable_off");
            backend.PublishToggleStates({{"invulnerable", true, false, "off"}});
            return ExecutionResult{RequestState::Verified, "closed after ready"};
        }, 1) == 1);
        CHECK(!Preference(backend).enabled && !Preference(backend).restorePending);
        CHECK(backend.Pump([](const Request&) { CHECK(false); return ExecutionResult{}; }) == 0);
        CHECK(ReadSettings(file).find("invulnerable=0") != std::string::npos);
    }

    void TestSettingsQueuedDisableCancelledBySessionChangeIsReissued()
    {
        const auto file = SettingsFile("queued-disable-session-change");
        SeedEnabled(file);
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        Ready(backend, {"state_invulnerable_on", "state_invulnerable_off"});
        backend.PublishToggleStates({{"invulnerable", true, true, "active"}});
        const auto disabled = backend.Submit("state_invulnerable_off");
        CHECK(disabled.accepted && disabled.requestId != 0);

        backend.SetRuntimeState(false, "loading", "session-a");
        const auto loading = backend.GetSnapshot();
        const auto* cancelled = FindRecord(loading, disabled.requestId);
        CHECK(cancelled && cancelled->state == RequestState::Cancelled);
        CHECK(loading.pendingCount == 0);
        CHECK(!Preference(backend).enabled && Preference(backend).restorePending);

        backend.SetRuntimeState(true, "new player ready", "session-b");
        backend.SetCapability("state_invulnerable_on", true);
        backend.SetCapability("state_invulnerable_off", true);
        backend.PublishToggleStates({{"invulnerable", true, true, "native switch survived"}});
        CHECK(Preference(backend).restorePending);
        CHECK(backend.Pump([&](const Request& request) {
            CHECK(request.requestId != disabled.requestId && request.restoration);
            CHECK(request.featureId == "state_invulnerable_off");
            CHECK(request.generation == backend.GetSnapshot().generation);
            backend.PublishToggleStates({{"invulnerable", true, false, "off"}});
            return ExecutionResult{RequestState::Verified, "reissued close"};
        }, 1) == 1);
        CHECK(!Preference(backend).enabled && !Preference(backend).restorePending);
        CHECK(backend.Pump([](const Request&) { CHECK(false); return ExecutionResult{}; }) == 0);
        CHECK(backend.FlushSettings());
        CHECK(ReadSettings(file).find("invulnerable=0") != std::string::npos);
    }

    void TestSettingsFailedAndUnverifiedUserEnablesAreNotSaved()
    {
        const auto file = SettingsFile("unverified-enable");
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        Ready(backend, {"state_invulnerable_on"});
        for (const auto state : {RequestState::Failed, RequestState::Dispatched, RequestState::Indeterminate}) {
            CHECK(backend.Submit("state_invulnerable_on").accepted);
            CHECK(backend.Pump([&](const Request&) { return ExecutionResult{state, "not verified"}; }) == 1);
            CHECK(!Preference(backend).enabled);
        }
        CHECK(backend.FlushSettings());
        CHECK(!std::filesystem::exists(file));
    }

    void TestSettingsMalformedVersionsAndUnknownActions()
    {
        const auto file = SettingsFile("format-validation");
        WriteSettings(file, "[meta]\nversion=9\n[toggles]\ninvulnerable=1\n");
        const auto original = ReadSettings(file);
        Backend invalid;
        std::string error;
        CHECK(!invalid.InitializeSettings(file, &error));
        CHECK(!error.empty() && !Preference(invalid).enabled);
        CHECK(invalid.FlushSettings());
        CHECK(ReadSettings(file) == original);

        WriteSettings(file, "\xef\xbb\xbf[meta]\r\nversion=1\r\n[toggles]\r\n"
            "invulnerable = 1\r\ninfinite_shachi=invalid\r\nfull_heal=1\r\ntravel=1\r\n");
        toggle_settings::Data data;
        CHECK(toggle_settings::Load(file, data, error));
        CHECK(error.empty());
        CHECK(data.at("invulnerable") && !data.at("infinite_shachi"));
        CHECK(!data.contains("full_heal") && !data.contains("travel"));
        CHECK(toggle_settings::Save(file, data, error));
        CHECK(ReadSettings(file).find("full_heal") == std::string::npos);
    }

    void TestRemovedUpgradeRequirementToggleCannotRestoreOrSubmit()
    {
        constexpr auto removed = "ignore_upgrade_requirements";
        for (const auto id : {removed, "ignore_upgrade_requirements_on", "ignore_upgrade_requirements_off"})
            CHECK(FindFeature(id) == nullptr);
        for (const auto& toggle : ToggleCatalog()) CHECK(toggle.id != removed);
        // Removing the failed bypass must preserve the separate native upgrade actions.
        for (const auto id : {"upgrade_main_weapon", "max_main_weapon", "max_all_main_weapons",
            "upgrade_shachi_weapon", "max_shachi_weapon", "max_all_shachi_weapons", "upgrade_medicine",
            "max_medicine", "max_all_medicines", "upgrade_accessory"}) CHECK(FindFeature(id) != nullptr);
        const auto file = SettingsFile("removed-upgrade-toggle");
        for (const auto version : {"1", "2"}) {
            WriteSettings(file, std::string("[meta]\nversion=") + version + "\n[toggles]\n" + removed +
                "=1\n[parameters.ignore_upgrade_requirements]\nvalue=invalid-old-value\n");
            toggle_settings::Data data;
            std::string error;
            CHECK(toggle_settings::Load(file, data, error));
            CHECK(error.empty() && !data.contains(removed));
            Backend backend;
            CHECK(backend.InitializeSettings(file));
            // Old capability announcements must not resurrect a deleted action.
            Ready(backend, {"ignore_upgrade_requirements_on", "ignore_upgrade_requirements_off"});
            for (const auto id : {removed, "ignore_upgrade_requirements_on", "ignore_upgrade_requirements_off"}) {
                const auto submitted = backend.Submit(id);
                CHECK(!submitted.accepted && submitted.message == "未知功能");
            }
            const auto snapshot = backend.GetSnapshot();
            for (const auto& preference : snapshot.togglePreferences) CHECK(preference.id != removed);
            for (const auto& capability : snapshot.capabilities)
                CHECK(capability.featureId != "ignore_upgrade_requirements_on" &&
                    capability.featureId != "ignore_upgrade_requirements_off");
            CHECK(snapshot.pendingCount == 0 && snapshot.requests.empty());
            CHECK(backend.Pump([](const Request&) { CHECK(false); return ExecutionResult{}; }) == 0);
            // Normal persistence drops obsolete sections, even if supplied in memory.
            data[removed] = true;
            CHECK(toggle_settings::Save(file, data, error));
            CHECK(ReadSettings(file).find(removed) == std::string::npos);
        }
    }

    void TestSettingsSaveFailurePreservesOriginalAndRetriesDirtyData()
    {
        const auto file = SettingsFile("save-failure");
        SeedEnabled(file);
        const auto original = ReadSettings(file);
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        CHECK(backend.Submit("state_invulnerable_off").accepted);
        const auto handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(handle != INVALID_HANDLE_VALUE);
        if (handle == INVALID_HANDLE_VALUE) return;
        std::string error;
        CHECK(!backend.FlushSettings(&error));
        CHECK(!error.empty());
        CHECK(ReadSettings(file) == original);
        CHECK(CloseHandle(handle));
        CHECK(backend.FlushSettings(&error));
        CHECK(error.empty());
        CHECK(ReadSettings(file).find("invulnerable=0") != std::string::npos);
    }

    void TestParameterizedToggleRoundTripRestorationAndSessionChanges()
    {
        const auto file = SettingsFile("parameterized-round-trip");
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        Ready(backend, {"damage_on", "damage_off"});
        CHECK(Preference(backend, "damage").parameters == std::vector<std::string>{"1"});
        CHECK(backend.Submit("damage_on", {"2.5"}).accepted);
        CHECK(backend.Pump([](const Request& request) {
            CHECK(request.arguments == std::vector<std::string>({"damage", "on", "2.5"}));
            return ExecutionResult{RequestState::Verified, "read back 2.5"};
        }) == 1);
        CHECK(Preference(backend, "damage").enabled);
        CHECK(Preference(backend, "damage").parameters == std::vector<std::string>{"2.5"});
        CHECK(backend.FlushSettings());
        CHECK(ReadSettings(file).find("version=2") != std::string::npos);
        CHECK(ReadSettings(file).find("[parameters.damage]\nvalue=2.5") != std::string::npos);

        Backend restarted;
        CHECK(restarted.InitializeSettings(file));
        CHECK(Preference(restarted, "damage").parameters == std::vector<std::string>{"2.5"});
        Ready(restarted, {"damage_on", "damage_off"});
        restarted.PublishToggleStates({{"damage", true, true, "different multiplier", {"1"}}});
        CHECK(restarted.Pump([](const Request& request) {
            CHECK(request.restoration && request.featureId == "damage_on");
            CHECK(request.arguments == std::vector<std::string>({"damage", "on", "2.5"}));
            return ExecutionResult{RequestState::Verified, "restored parameter"};
        }) == 1);
        CHECK(!Preference(restarted, "damage").restorePending);

        restarted.SetRuntimeState(true, "next session", "damage-b");
        restarted.SetCapability("damage_on", true);
        restarted.PublishToggleStates({{"damage", true, true, "same multiplier", {"2.500000"}}});
        CHECK(restarted.Pump([](const Request&) { CHECK(false); return ExecutionResult{}; }) == 0);
        CHECK(!Preference(restarted, "damage").restorePending);

        restarted.SetRuntimeState(true, "another session", "damage-c");
        restarted.SetCapability("damage_on", true);
        restarted.PublishToggleStates({{"damage", false, false, "unknown"}});
        CHECK(restarted.Pump([](const Request&) { CHECK(false); return ExecutionResult{}; }) == 0);
        restarted.PublishToggleStates({{"damage", true, false, "off"}});
        CHECK(restarted.Pump([](const Request& request) {
            CHECK(request.arguments.back() == "2.5");
            return ExecutionResult{RequestState::Failed, "failed restore"};
        }) == 1);
        CHECK(restarted.Pump([](const Request&) { CHECK(false); return ExecutionResult{}; }) == 0);
    }

    void TestFailedParameterEditsPreserveVerifiedIntentAndCloseIgnoresDraft()
    {
        const auto file = SettingsFile("failed-parameter-edit");
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        Ready(backend, {"damage_on", "damage_off"});
        CHECK(backend.Submit("damage_on", {"2"}).accepted);
        CHECK(backend.Pump([](const Request&) { return ExecutionResult{RequestState::Verified, "ok"}; }) == 1);
        CHECK(backend.FlushSettings());
        const auto original = ReadSettings(file);
        for (const auto state : {RequestState::Failed, RequestState::Dispatched, RequestState::Indeterminate}) {
            CHECK(backend.Submit("damage_on", {"3"}).accepted);
            CHECK(backend.Pump([&](const Request&) { return ExecutionResult{state, "not verified"}; }) == 1);
            CHECK(Preference(backend, "damage").parameters == std::vector<std::string>{"2"});
            CHECK(backend.FlushSettings());
            CHECK(ReadSettings(file) == original);
        }
        CHECK(!backend.Submit("damage_on", {"nan"}).accepted);
        CHECK(!backend.Submit("damage_on", {"-1"}).accepted);
        CHECK(!backend.Submit("damage_on", {"2garbage"}).accepted);
        const auto closed = backend.Submit("damage_off", {"invalid-draft"});
        CHECK(closed.accepted);
        CHECK(!Preference(backend, "damage").enabled);
        CHECK(backend.Pump([](const Request& request) {
            CHECK(request.arguments == std::vector<std::string>({"damage", "off"}));
            return ExecutionResult{RequestState::Verified, "off"};
        }) == 1);
        CHECK(Preference(backend, "damage").parameters == std::vector<std::string>{"2"});
        CHECK(backend.FlushSettings());
        Backend restarted;
        CHECK(restarted.InitializeSettings(file));
        CHECK(!Preference(restarted, "damage").enabled);
        CHECK(Preference(restarted, "damage").parameters == std::vector<std::string>{"2"});
    }

    void TestParameterSettingsLegacyVersionMalformedValuesAndUnknownIds()
    {
        const auto file = SettingsFile("parameter-config-validation");
        WriteSettings(file, "[meta]\nversion=1\n[toggles]\ninvulnerable=1\ndamage=1\n"
            "[parameters.damage]\nvalue=99\n");
        Backend legacy;
        CHECK(legacy.InitializeSettings(file));
        CHECK(Preference(legacy).enabled);
        CHECK(Preference(legacy, "damage").parameters == std::vector<std::string>{"1"});
        CHECK(legacy.FlushSettings());
        CHECK(ReadSettings(file).find("version=1") != std::string::npos);

        WriteSettings(file, "[meta]\nversion=2\n[toggles]\ndamage=1\nfull_heal=1\n"
            "[parameters.damage]\nvalue=4\nunknown=100\n[parameters.unknown]\nvalue=1\n");
        toggle_settings::Data data;
        std::string error;
        CHECK(toggle_settings::Load(file, data, error));
        CHECK(data.at("damage").enabled);
        CHECK(data.at("damage").parameters == std::vector<std::string>{"4"});
        CHECK(!data.contains("full_heal") && !data.contains("unknown"));
        for (const auto value : {"nan", "inf", "0", "101", "2junk"}) {
            WriteSettings(file, std::string("[meta]\nversion=2\n[toggles]\ninvulnerable=1\ndamage=1\n") +
                "[parameters.damage]\nvalue=" + value + "\n");
            const auto original = ReadSettings(file);
            Backend invalid;
            CHECK(!invalid.InitializeSettings(file, &error));
            CHECK(!error.empty());
            CHECK(!Preference(invalid).enabled && !Preference(invalid, "damage").enabled);
            CHECK(Preference(invalid, "damage").parameters == std::vector<std::string>{"1"});
            CHECK(invalid.FlushSettings());
            CHECK(ReadSettings(file) == original);
        }
    }

    void TestExtendedActionsNeverBecomeRestorationRequests()
    {
        const auto file = SettingsFile("extended-actions");
        Backend backend;
        CHECK(backend.InitializeSettings(file));
        Ready(backend, {"remaining_days", "unlock_66_days", "flight_up", "flight_down"});
        CHECK(!backend.Submit("remaining_days", {"67"}).accepted);
        for (const auto id : {"remaining_days", "unlock_66_days", "flight_up", "flight_down"}) {
            CHECK(backend.Submit(id).accepted);
            CHECK(backend.Pump([](const Request& request) {
                CHECK(!request.restoration);
                return ExecutionResult{RequestState::Verified, "ok"};
            }) == 1);
        }
        CHECK(backend.FlushSettings());
        CHECK(!std::filesystem::exists(file));
        backend.SetRuntimeState(true, "new session", "actions-b");
        CHECK(backend.Pump([](const Request&) { CHECK(false); return ExecutionResult{}; }) == 0);
    }
}

void TestItemSnapshotsShareImmutablePublication()
{
    using namespace gloss::gameplay;
    Backend backend;
    CHECK(!backend.IsReady());
    backend.SetRuntimeState(true, "ready", "session");
    CHECK(backend.IsReady());
    ItemEntry original;
    original.key = "item";
    original.name = "old";
    original.description = std::string(65536, 'x');
    backend.PublishItems({original}, true, "loaded");
    const auto first = backend.GetSnapshot();
    const auto second = backend.GetSnapshot();
    CHECK(first.items.data() == second.items.data());
    original.name = "new";
    backend.PublishItems({original}, true, "updated");
    const auto third = backend.GetSnapshot();
    CHECK(first.items.data() != third.items.data());
    CHECK(first.items[0].name == "old");
    CHECK(third.items[0].name == "new");
    backend.SetRuntimeState(false, "gone", "other");
    CHECK(!backend.IsReady());
    CHECK(backend.GetSnapshot().items.empty());
    CHECK(first.items[0].description.size() == 65536);
}
int main(int argc, char** argv)
{
    settingsRoot = std::filesystem::absolute(argc > 1 ? argv[1] : "output/pbz-basics-20261029") /
        ("gameplay-settings-" + std::to_string(GetCurrentProcessId()));
    TestItemSnapshotsShareImmutablePublication();
    TestCatalogMatchesRecoveredNativeContracts();
    TestCatalogParameterValidationAndFixedArguments();
    TestToggleCatalogHasUniqueReversibleStatePairs();
    TestPublishedToggleStatesAreVisibleAndClearedOnSessionChange();
    TestRemovedClearFeaturesCannotBeSubmitted();
    TestSubmissionValidationQueueCapacityAndFixedPayload();
    TestItemSubmissionRequiresPreparedAvailableCatalogAndValidQuantity();
    TestDirectSubmitCannotBypassItemCatalogValidation();
    TestItemQuantityInputRejectsOverflowAndPartialNumbers();
    TestDustAcceptsLargeQuantitiesWithoutChangingOrdinaryItemLimits();
    TestItemRequestsAndCatalogAreInvalidatedOnSessionChange();
      TestSameSessionRebindRequiresFreshItemPublication();
    TestItemDispatchIsNotReportedAsVerifiedWithoutReadback();
    TestExpirationFailureAndExceptionsNeverRetry();
    TestSessionChangeCancelsQueuedAndMarksRunningIndeterminate();
    TestConcurrentPumpIsSerializedAndReentrantSubmitWaitsForNextPump();
    TestHistoryRetentionAndBadExecutorState();
    TestSettingsDefaultsVerifiedRoundTripAndActionsNeverRestore();
    TestSettingsEveryCatalogToggleRestoresWithItsOwnFixedArguments();
    TestSettingsWaitForReadbackAndDoNotRetryExecutedFailures();
    TestSettingsAlreadyEnabledDoesNotIssueAnotherEnable();
    TestSettingsDisableCancelsWhileUnreadyAndFailedDisableStaysOff();
    TestSettingsDisableSupersedesRunningRestore();
    TestSettingsUnreadyDisableExecutesAfterReadyIfStillEnabled();
    TestSettingsQueuedDisableCancelledBySessionChangeIsReissued();
    TestSettingsFailedAndUnverifiedUserEnablesAreNotSaved();
    TestSettingsMalformedVersionsAndUnknownActions();
    TestRemovedUpgradeRequirementToggleCannotRestoreOrSubmit();
    TestSettingsSaveFailurePreservesOriginalAndRetriesDirtyData();
    TestParameterizedToggleRoundTripRestorationAndSessionChanges();
    TestFailedParameterEditsPreserveVerifiedIntentAndCloseIgnoresDraft();
    TestParameterSettingsLegacyVersionMalformedValuesAndUnknownIds();
    TestExtendedActionsNeverBecomeRestorationRequests();
    if (failures)
        return 1;
    std::cout << "GlossTrainer PBZ gameplay tests passed (0 failures)\n";
    return 0;
}
