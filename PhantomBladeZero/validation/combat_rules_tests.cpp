#include "../src/gameplay/combat_rules.hpp"
#include <iostream>
#include <limits>
using namespace gloss::gameplay;
int main() {
    int failures{}, checks{};
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; } } while (0)
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(ProtectedShachi(400, 400, 0) == 400);
    CHECK(ProtectedShachi(200, 400, 150) == 200);
    CHECK(ProtectedShachi(200, 400, 210) == 210);
    CHECK(ProtectedShachi(400, 300, 299) == 300);
    CHECK(ProtectedShachi(0, 400, 0) == 0);
    CHECK(ProtectedShachi(-1, 400, 5) == 5);
    CHECK(ProtectedShachi(400, 0, 5) == 5);
    CHECK(ProtectedShachi(nan, 400, 5) == 5);
    CHECK(ProtectedShachi(400, nan, 5) == 5);
    CHECK(std::isnan(ProtectedShachi(400, 400, nan)));
    CHECK(!ShouldAttemptPerfectDodge(-1, 10, 10, true, -1, 0));
    CHECK(!ShouldAttemptPerfectDodge(1, 10.5, 10, false, -1, 0));
    CHECK(ShouldAttemptPerfectDodge(1, 10.03, 10, false, -1, 0));
    CHECK(ShouldAttemptPerfectDodge(1, 10.5, 10, true, -1, 0));
    CHECK(!ShouldAttemptPerfectDodge(1, 10.03, 10, false, 1, 10.03));
    CHECK(ShouldAttemptPerfectDodge(1, 20.03, 20, false, 1, 10.03));
    CHECK(!ShouldAttemptPerfectDodge(1, nan, 10, true, -1, 0));
    CHECK(!ShouldAttemptPerfectDodge(1, 10, nan, true, -1, 0));
    CHECK(!ShouldAttemptPerfectDodge(1, 10, -1, true, -1, 0));
    std::cout << checks << " combat rule checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
