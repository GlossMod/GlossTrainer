#include "../src/gameplay/extended_rules.hpp"
#include "../src/gameplay/medicine_rules.hpp"
#include <iostream>
#include <limits>

namespace {
    using namespace gloss::gameplay;
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)
    void TestMedicineRefillRules() {
        CHECK(MedicineMaximumInflated(2147083647));
        CHECK(!MedicineMaximumInflated(2)); CHECK(!MedicineMaximumInflated(3));
        CHECK(ValidMedicineDefault(2)); CHECK(!ValidMedicineDefault(0)); CHECK(!ValidMedicineDefault(100));
        CHECK(ValidMedicineTotal(3)); CHECK(!ValidMedicineTotal(0)); CHECK(!ValidMedicineTotal(InflatedMedicineMaximum));
    }
    void TestPlayerDirection() {
        CHECK(PlayerDamageDirection(false, false) == DamageDirection::Unrelated);
        CHECK(PlayerDamageDirection(true, true) == DamageDirection::Unrelated);
        CHECK(PlayerDamageDirection(true, false) == DamageDirection::Outgoing);
        CHECK(PlayerDamageDirection(false, true) == DamageDirection::Incoming);
    }
    void TestDamageAndDefense() {
        CHECK(ExtendedDamageDelta(-20, true, false, 3, 4, false, 100) == -60);
        CHECK(ExtendedDamageDelta(-20, false, true, 3, 4, false, 100) == -5);
        CHECK(ExtendedDamageDelta(-20, false, false, 3, 4, true, 100) == -20);
        CHECK(ExtendedDamageDelta(-20, true, true, 3, 4, true, 100) == -20);
        CHECK(ExtendedDamageDelta(20, true, false, 3, 4, true, 100) == 20);
        CHECK(ExtendedDamageDelta(0, true, false, 3, 4, true, 100) == 0);
        CHECK(ExtendedDamageDelta(-20, false, true, 1, .1f, false, 100) == -200);
        CHECK(ExtendedDamageDelta(-10000000, true, false, 100, 1, false, 100) == -MaximumDamageDelta);
        CHECK(ExtendedDamageDelta(-20, true, false, 1, 1, false, 100) == -20);
    }
    void TestLethalAndBreak() {
        CHECK(ExtendedDamageDelta(-20, true, false, 2, 1, true, 100) == -101);
        CHECK(ExtendedDamageDelta(-20, true, false, 10, 1, true, 100) == -200);
        CHECK(ExtendedDamageDelta(-20, true, false, 1, 1, true, 0) == -20);
        CHECK(ExtendedDamageDelta(-.25f, true, false, 1, 1, true, 0) == -1);
        CHECK(ExtendedDamageDelta(-20, false, true, 1, 2, true, 100) == -10);
        CHECK(ExtendedDamageDelta(-1, true, false, 1, 1, true, 99999992) == -99999992);
    }
    void TestInvalidAndBoundaryInputs() {
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        const auto inf = std::numeric_limits<float>::infinity();
        for (const auto invalid : {nan, inf, -inf, 0.f, -.1f, .09f, 100.1f}) {
            CHECK(!ValidDamageMultiplier(invalid));
            CHECK(!ExtendedDamageDelta(-20, true, false, invalid, 1, false, 100));
            CHECK(!ExtendedDamageDelta(-20, false, true, 1, invalid, false, 100));
        }
        CHECK(ValidDamageMultiplier(.1f)); CHECK(ValidDamageMultiplier(100));
        for (const auto invalid : {nan, inf, -inf, -MaximumDamageDelta * 2, MaximumDamageDelta * 2})
            CHECK(!ExtendedDamageDelta(invalid, true, false, 1, 1, false, 100));
        for (const auto invalid : {nan, inf, -1.f, MaximumDamageDelta})
            CHECK(!ExtendedDamageDelta(-20, true, false, 1, 1, true, invalid));
        // An unreadable target value is irrelevant for ordinary scaling and healing.
        CHECK(ExtendedDamageDelta(-20, true, false, 2, 1, false, nan) == -40);
        CHECK(ExtendedDamageDelta(20, true, false, 2, 1, true, nan) == 20);
        CHECK(ExtendedDamageDelta(-20, false, true, 1, 2, true, nan) == -10);
    }
}
int main() {
    TestPlayerDirection(); TestDamageAndDefense(); TestLethalAndBreak(); TestInvalidAndBoundaryInputs();
    TestMedicineRefillRules();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
