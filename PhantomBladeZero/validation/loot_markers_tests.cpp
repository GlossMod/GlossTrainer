#include "../src/gameplay/loot_markers.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <string>

namespace {
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)
}
int main() {
    using namespace gloss::gameplay;
    ClearLootMarkers();
    CHECK(GetLootMarkers().empty());
    CHECK(PublishLootMarkersJson(R"([{"x":120,"y":350,"label":"Loot","distance":270}])"));
    auto copied = GetLootMarkers();
    CHECK(copied.size() == 1 && copied[0].x == 120 && copied[0].y == 350 && copied[0].label == "Loot" && copied[0].distance == 270);
    copied.clear();
    CHECK(GetLootMarkers().size() == 1);
    for (const auto* invalid : {"null", "{}", "[{}]", "[1]", "[", "[{\"x\":1,\"y\":2,\"label\":\"NPC\",\"distance\":3}]",
        "[{\"x\":1,\"y\":2,\"label\":\"Loot\",\"distance\":1001}]", "[{\"x\":-1,\"y\":2,\"label\":\"Loot\",\"distance\":3}]",
        "[{\"x\":1e100,\"y\":2,\"label\":\"Loot\",\"distance\":3}]"}) {
        CHECK(!PublishLootMarkersJson(invalid));
        CHECK(GetLootMarkers().empty());
    }
    CHECK(!PublishLootMarkersJson(std::string(16385, ' ')));
    CHECK(PublishLootMarkersJson(R"([{"x":0,"y":0,"label":"Chest","distance":0},{"x":100000,"y":100000,"label":"Note","distance":1000}])"));
    CHECK(GetLootMarkers().size() == 2);
    ClearLootMarkers();
    CHECK(GetLootMarkers().empty());
    CHECK(PublishLootMarkersJson(R"([{"x":1,"y":2,"label":"Loot","distance":3}])"));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(GetLootMarkers().empty());
    std::string tooMany = "[";
    for (int i = 0; i < 33; ++i) {
        if (i) tooMany += ',';
        tooMany += R"({"x":1,"y":2,"label":"Loot","distance":3})";
    }
    tooMany += ']';
    CHECK(!PublishLootMarkersJson(tooMany));
    CHECK(GetLootMarkers().empty());
    CHECK(PublishLootMarkersJson("[]"));
    CHECK(GetLootMarkers().empty());
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
