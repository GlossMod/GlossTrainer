#include "../src/gameplay/item_scan_schedule.hpp"
#include "../src/gameplay/item_loading_budget.hpp"
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
    std::cout << "Item scan scheduling: " << failures << " failures\n";
    return failures ? 1 : 0;
}
