#include "../src/gameplay/item_scan_schedule.hpp"
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
    std::cout << "Item scan scheduling: " << failures << " failures\n";
    return failures ? 1 : 0;
}
