#include "../src/gameplay/item_action_index.hpp"
#include <iostream>
int main() {
    using namespace gloss::gameplay;
    int failures{};
    const auto check = [&](bool ok) { if (!ok) ++failures; };
    std::vector<std::uint64_t> snapshot{1,2,3,0, 100,7,9,11,1, 101,7,9,12,1, 102,7,10,13,1, 103,7,9,14,0};
    const auto index = BuildItemActionIndex(snapshot, 4);
    check(index.size() == 2);
    check(index.at((9ull << 32) | 7) == std::vector<std::uint32_t>({11,12}));
    check(index.at((10ull << 32) | 7) == std::vector<std::uint32_t>({13}));
    check(BuildItemActionIndex(snapshot, 100).empty());
    auto previous = snapshot;
    check(previous == snapshot);
    snapshot[3] = 1; check(previous != snapshot); // device changes
    snapshot = previous; snapshot[7] = 99; check(previous != snapshot); // in-place remap
    snapshot = previous; snapshot[1] = 200; check(previous != snapshot); // new input object
    std::cout << "Item action index: " << failures << " failures\n";
    return failures ? 1 : 0;
}
