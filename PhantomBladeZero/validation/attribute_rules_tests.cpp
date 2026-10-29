#include "../src/gameplay/attribute_rules.hpp"
#include <iostream>
#include <limits>
using namespace gloss::gameplay;
int main() {
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << '\n'; } } while(0)
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(CorrectedAttributeBase(1000, 1042.5f, 1000) == 957.5f);
    CHECK(CorrectedAttributeBase(1000, 1200, 1000) == 800);
    const auto multiplier = CorrectedAttributeBase(800, 960, 1000, 1000, 1200);
    CHECK(multiplier && std::abs(*multiplier * 1.2f - 1000) < 0.01f);
    CHECK(CorrectedAttributeBase(500, 542.5f, 500) == 457.5f);
    CHECK(!CorrectedAttributeBase(1000, 1000, 1000));
    CHECK(!CorrectedAttributeBase(nan, 1000, 500));
    CHECK(!CorrectedAttributeBase(1000, nan, 500));
    CHECK(!CorrectedAttributeBase(1000, 1000, nan));
    CHECK(!CorrectedAttributeBase(0, 42.5f, 1));
    CHECK(!CorrectedAttributeBase(1000, 500, 100000000));
    CHECK(!CorrectedAttributeBase(1000, 1000, 0));
    CHECK(!CorrectedAttributeBase(900, 1000, 500, 1000, 1000)); // Override/cap: no response to base.
    CHECK(!CorrectedAttributeBase(900, 1000, 500, 900, 1100)); // Zero base delta.
    CHECK(!CorrectedAttributeBase(900, 1000, 500, 1000, 900)); // Invalid negative slope.
    CHECK(!CorrectedAttributeBase(900, 1000, 500, nan, 1000));
    std::cout << checks << " attribute checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
