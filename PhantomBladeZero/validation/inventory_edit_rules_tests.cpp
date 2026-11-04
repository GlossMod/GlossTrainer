#include "../src/gameplay/inventory_edit_rules.hpp"
#include <iostream>

using namespace gloss::gameplay;
static int failures{};
#define CHECK(value) do { if (!(value)) { std::cerr << "CHECK failed at " << __LINE__ << ": " #value "\n"; ++failures; } } while (false)

static void TestTargetParsing() {
    std::int32_t value = -7;
    CHECK(ParseInventoryTarget("0", value) && value == 0);
    CHECK(ParseInventoryTarget("2147483647", value) && value == 2147483647);
    for (const auto text : {"", "-1", "+1", " 0", "1 ", "1.0", "2147483648", "1x"}) {
        value = -7;
        CHECK(!ParseInventoryTarget(text, value) && value == -7);
    }
}
static void TestReductionBounds() {
    CHECK(InventoryRemovalAmount(5, 0) == 5);
    CHECK(InventoryRemovalAmount(5, 3) == 2);
    CHECK(InventoryRemovalAmount(5, 5) == 0);
    CHECK(InventoryRemovalAmount(2147483647, 0) == 2147483647);
    CHECK(!InventoryRemovalAmount(5, 6));
    CHECK(!InventoryRemovalAmount(-1, 0));
    CHECK(!InventoryRemovalAmount(2147483648ll, 0));
    CHECK(!InventoryRemovalAmount(5, -1));
}
static void TestConsumptionWhitelist() {
    InventoryRowPolicy charm{0, 1, 0};
    CHECK(!InventoryConsumableAllowed("Item_Traversing", {}));
    CHECK(!InventoryOrdinaryReduction({}));
    CHECK(!InventoryConsumableAllowed("", charm));
    CHECK(InventoryConsumableAllowed("Item_Traversing", charm));
    CHECK(!InventoryConsumableAllowed("Quest_Key", charm));
    CHECK(!InventoryConsumableAllowed("Item_001", charm));
    for (std::uint8_t type = 1; type <= 8; ++type) {
        auto row = charm; row.materialType = type;
        CHECK(!InventoryConsumableAllowed("Item_Traversing", row));
    }
    for (const auto flag : {0, 1, 2, 3, 4}) {
        auto row = charm;
        if (flag == 0) row.attributeEffect = true;
        if (flag == 1) row.integration = true;
        if (flag == 2) row.flow = true;
        if (flag == 3) row.dialogue = true;
        if (flag == 4) row.ability = true;
        CHECK(!InventoryConsumableAllowed("Item_Traversing", row));
        CHECK(!InventoryOrdinaryReduction(row));
    }
    CHECK(InventoryOrdinaryReduction({0, 1, 3})); // Intentional manual removal of crafting materials.
    CHECK(!InventoryOrdinaryReduction({0, 2, 0}));
    CHECK(!InventoryOrdinaryReduction({0, 5, 0}));
    CHECK(!InventoryOrdinaryReduction({4, 0, 0}));
    CHECK(!InventoryOrdinaryReduction({5, 0, 0}));
    CHECK(!InventoryOrdinaryReduction({0, 255, 0}));
}
int main() {
    TestTargetParsing(); TestReductionBounds(); TestConsumptionWhitelist();
    return failures ? 1 : 0;
}
