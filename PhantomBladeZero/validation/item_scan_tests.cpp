#include "../src/gameplay/item_scan_schedule.hpp"
#include "../src/gameplay/item_loading_budget.hpp"
#include "../src/gameplay/item_catalog_progress.hpp"
#include <iostream>
using namespace gloss::gameplay;
int main() {
    int failures{};
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << "FAIL " << __LINE__ << '\n'; } } while (0)
    using namespace std::chrono;
    ItemScanSchedule scan;
    const auto start = ItemScanSchedule::Clock::time_point{};
    CHECK(scan.Begin(start, 1000));
    CHECK(scan.End(1000) == 512);
    scan.cursor = 512; scan.Finish(start, 1000);
    CHECK(!scan.Begin(start + milliseconds(49), 1000));
    CHECK(scan.Begin(start + milliseconds(50), 1000));
    CHECK(scan.cursor == 512 && scan.End(1000) == 1000);
    scan.cursor = 1000; scan.Finish(start + milliseconds(50), 1000);
    CHECK(!scan.Begin(start + seconds(30), 1000));
    CHECK(!scan.Begin(start + seconds(60), 1000));
    CHECK(scan.Begin(start + seconds(60), 1200));
    CHECK(scan.cursor == 1000 && scan.End(1200) == 1200);
    scan.cursor = 1200;
    CHECK(!scan.Begin(start + seconds(61), 100));
    CHECK(!scan.Begin(start + seconds(62), 0));
    // A warm 790-row catalog must no longer take 790 separate Ticks. A
    // deterministic no-engine workload completes within the 32-row cap.
    std::size_t catalogRows{}, ticks{};
    while (catalogRows < 790) {
        ItemLoadingBudget budget(start, milliseconds(2), 32);
        while (catalogRows < 790 && budget.Continue(start)) { ++catalogRows; budget.Consume(); }
        ++ticks;
    }
    CHECK(ticks == 25);
    ItemLoadingBudget slowName(start, milliseconds(2), 32);
    CHECK(slowName.Continue(start));
    slowName.Consume();
    CHECK(!slowName.Continue(start + milliseconds(3))); // One slow engine call stops further work.
    ItemLoadingBudget descriptions(start, milliseconds(1), 8);
    for (unsigned i = 0; i < 8; ++i) { CHECK(descriptions.Continue(start)); descriptions.Consume(); }
    CHECK(!descriptions.Continue(start)); // Empty/cached work is bounded too.
    ItemLoadingBudget late(start, milliseconds(1), 8);
    CHECK(late.Continue(start + seconds(1))); // Always make one unit of progress.
    late.Consume();
    CHECK(!late.Continue(start + seconds(1)));
    // An unfinished table cannot publish a catalog; a reset discards its slot,
    // allocated count and all later phases rather than resuming stale pointers.
    ItemCatalogProgress progress;
    CHECK(!progress.Complete());
    progress.tableReady = true;
    progress.slot = 640;
    progress.allocated = 640;
    progress.FinishTable(5);
    CHECK(progress.table == 1 && progress.slot == 0 && progress.allocated == 0 && !progress.tableReady);
    CHECK(progress.phase == ItemCatalogProgress::Phase::Tables && !progress.Complete());
    progress = {};
    CHECK(progress.table == 0 && progress.row == 0 && progress.slot == 0 && !progress.tableReady);
    for (unsigned table = 0; table < 5; ++table) progress.FinishTable(5);
    CHECK(progress.phase == ItemCatalogProgress::Phase::ParentIndex && !progress.Complete());
    progress.row = 789;
    progress.FinishRows(790);
    CHECK(progress.phase == ItemCatalogProgress::Phase::ParentIndex); // Interrupted join stays private.
    progress.row = 790;
    progress.FinishRows(790);
    CHECK(progress.phase == ItemCatalogProgress::Phase::ParentLinks && progress.row == 0);
    progress.row = 790;
    progress.FinishRows(790);
    CHECK(progress.phase == ItemCatalogProgress::Phase::VerifyTables && progress.table == 0);
    for (unsigned table = 0; table < 4; ++table) {
        progress.FinishVerification(5);
        CHECK(!progress.Complete());
    }
    // A reload during final verification resets the entire prefix too.
    auto failedBuild = progress;
    failedBuild = {};
    CHECK(failedBuild.phase == ItemCatalogProgress::Phase::Tables && failedBuild.table == 0 && !failedBuild.Complete());
    progress.FinishVerification(5);
    CHECK(progress.Complete());
    // Catalog setup, empty sparse slots, links and checks must each consume the
    // shared cap; reaching a deadline cannot immediately start the name phase.
    ItemLoadingBudget catalogBuild(start, milliseconds(1), 32);
    unsigned work{};
    while (catalogBuild.Continue(start)) { ++work; catalogBuild.Consume(); }
    CHECK(work == 32 && !catalogBuild.Continue(start));
    // A slow UObject-discovery batch resumes at the next unread slot, within
    // the existing 50ms throttle, rather than rescanning its completed prefix.
    ItemScanSchedule inputDiscovery;
    CHECK(inputDiscovery.Begin(start, 2000));
    ItemLoadingBudget discoveryBudget(start, microseconds(500), 512);
    CHECK(discoveryBudget.Continue(start));
    ++inputDiscovery.cursor;
    discoveryBudget.Consume();
    CHECK(!discoveryBudget.Continue(start + milliseconds(1)));
    inputDiscovery.Finish(start, 2000);
    CHECK(inputDiscovery.Begin(start + milliseconds(50), 2000));
    CHECK(inputDiscovery.cursor == 1 && inputDiscovery.End(2000) == 513);
    std::cout << "Item scan scheduling: " << failures << " failures\n";
    return failures ? 1 : 0;
}
