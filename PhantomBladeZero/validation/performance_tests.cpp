#include "../src/gameplay/backend.hpp"
#include "../src/gameplay/image_ranges.hpp"
#include "../src/gameplay/missing_name_cache.hpp"
#include "../src/gameplay/reflection_cache.hpp"
#include "../src/gameplay/status_schedule.hpp"
#include <iostream>

using namespace gloss::gameplay;

namespace {
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)

    void TestExecutableImageRanges() {
        ExecutableImageRanges ranges;
        CHECK(!ranges.Contains(reinterpret_cast<std::uintptr_t>(&TestExecutableImageRanges)));
        CHECK(!ranges.Capture(0));
        CHECK(ranges.Capture(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))));
        CHECK(ranges.Contains(reinterpret_cast<std::uintptr_t>(&TestExecutableImageRanges)));
        static const int data{};
        CHECK(!ranges.Contains(reinterpret_cast<std::uintptr_t>(&data))); // Read-only data is not code.
        int local{};
        CHECK(!ranges.Contains(reinterpret_cast<std::uintptr_t>(&local)));
        CHECK(!ranges.Contains(0));
    }
    void TestReflectionCacheRevalidatesOwner() {
        ReflectionCache cache;
        ReflectionCache::Hit hit;
        CHECK(!cache.Find(0x1000, 7, "Health", 0, hit));
        cache.Remember(0x1000, 7, "Health", 0x2000, 0);
        CHECK(cache.Find(0x1000, 7, "Health", 999999, hit) && hit.member == 0x2000); // Found members do not expire.
        CHECK(!cache.Find(0x1000, 8, "Health", 0, hit)); // Address reused by another object.
        CHECK(!cache.Find(0x1008, 7, "Health", 0, hit));
        CHECK(!cache.Find(0x1000, 7, "Health_Max", 0, hit));
        cache.Clear();
        CHECK(!cache.Find(0x1000, 7, "Health", 0, hit));
    }
    void TestReflectionCacheMissesExpire() {
        ReflectionCache cache;
        ReflectionCache::Hit hit{1};
        cache.Remember(0x1000, 7, "Late", 0, 100);
        CHECK(cache.Find(0x1000, 7, "Late", 100 + ReflectionCache::MissLifetimeMs - 1, hit) && hit.member == 0);
        CHECK(!cache.Find(0x1000, 7, "Late", 100 + ReflectionCache::MissLifetimeMs, hit)); // The type may have finished linking.
    }
    void TestStatusScheduleSpreadsOneSlicePerFrame() {
        using namespace std::chrono_literals;
        StatusSchedule schedule;
        const Clock::time_point start{1h};
        for (std::size_t slice = 0; slice < 3; ++slice) {
            CHECK(!schedule.Finished(3));
            CHECK(schedule.Next(start, false, 3) == slice);
        }
        CHECK(schedule.Finished(3));
        schedule.Complete(start);
        CHECK(schedule.Next(start + StatusSchedule::IdleInterval - 1ms, false, 3) == 3); // Idle: no work between passes.
        CHECK(schedule.Next(start + 1ms, true, 3) == 0); // The menu or a pending restore cannot wait.
        schedule.Restart();
        CHECK(schedule.Next(start, false, 3) == 0); // A new session reads status at once.
    }
    void TestMissingNameCacheResumes() {
        MissingNameCache missing;
        CHECK(missing.Scanned("tag") == 0);
        missing.Remember("tag", 50);
        CHECK(missing.Scanned("tag") == 50);
        CHECK(missing.Scanned("other") == 0);
    }
    void TestBackendRevisionTracksOnlyChanges() {
        Backend backend;
        const auto execute = [](const Request&) { return ExecutionResult{RequestState::Verified, "ok"}; };
        auto revision = backend.Revision();
        CHECK(!backend.IsReady());
        backend.SetRuntimeState(true, "ready", "session");
        CHECK(backend.IsReady());
        CHECK(backend.Revision() != revision);
        revision = backend.Revision();
        backend.SetRuntimeState(true, "ready", "session");
        CHECK(backend.Revision() == revision);
        backend.PublishStatistics({{"id", "label", "1"}});
        CHECK(backend.Revision() != revision);
        revision = backend.Revision();
        backend.PublishStatistics({{"id", "label", "1"}});
        CHECK(backend.Revision() == revision);
        backend.PublishToggleStates({{"toggle", true, false, "reason"}});
        CHECK(backend.Revision() != revision);
        revision = backend.Revision();
        backend.PublishToggleStates({{"toggle", true, false, "reason"}});
        CHECK(backend.Revision() == revision);
        CHECK(backend.Pump(execute) == 0); // Idle pump publishes nothing.
        CHECK(backend.Revision() == revision);
        const auto& feature = Catalog().front();
        backend.SetCapability(feature.id, true, "ok");
        CHECK(backend.Revision() != revision);
        revision = backend.Revision();
        backend.SetCapability(feature.id, true, "ok");
        CHECK(backend.Revision() == revision);
    }
    void TestPumpStillRunsQueuedWorkAfterIdle() {
        Backend backend;
        unsigned executed{};
        const auto execute = [&](const Request&) { ++executed; return ExecutionResult{RequestState::Verified, "ok"}; };
        backend.SetRuntimeState(true, "ready", "session");
        CHECK(backend.Pump(execute) == 0);
        const FeatureDef* feature{};
        for (const auto& candidate : Catalog())
            if (!candidate.functionName.empty() && candidate.params.empty() && candidate.id != "grant_item") { feature = &candidate; break; }
        CHECK(feature != nullptr);
        if (!feature) return;
        backend.SetCapability(feature->id, true, "ok");
        CHECK(backend.Submit(feature->id).accepted);
        CHECK(backend.Pump(execute) == 1);
        CHECK(executed == 1);
        CHECK(backend.Pump(execute) == 0); // Back to the idle path.
        CHECK(executed == 1);
    }
    void TestSnapshotFromOldRevisionIsRefetched() {
        Backend backend;
        const auto before = backend.Revision();
        const auto snapshot = backend.GetSnapshot();
        backend.SetRuntimeState(false, "changed");
        CHECK(backend.Revision() != before); // A reader holding `before` copies again.
        CHECK(snapshot.status != backend.GetSnapshot().status);
    }
    void TestItemListIdentityFollowsPublication() {
        ItemList first(std::vector<ItemEntry>{{"key"}});
        const auto shared = first;
        CHECK(shared.Identity() == first.Identity());
        ItemList second(std::vector<ItemEntry>{{"key"}});
        CHECK(second.Identity() != first.Identity());
    }
}

int main() {
    TestExecutableImageRanges();
    TestReflectionCacheRevalidatesOwner();
    TestReflectionCacheMissesExpire();
    TestStatusScheduleSpreadsOneSlicePerFrame();
    TestMissingNameCacheResumes();
    TestBackendRevisionTracksOnlyChanges();
    TestPumpStillRunsQueuedWorkAfterIdle();
    TestSnapshotFromOldRevisionIsRefetched();
    TestItemListIdentityFollowsPublication();
    std::cout << "Performance: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
