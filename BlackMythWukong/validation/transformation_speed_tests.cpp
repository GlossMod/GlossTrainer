#include "../src/gameplay/transformation_speed.hpp"
#include <iostream>
#include <limits>

namespace speed = gloss::gameplay::transformation::speed;

namespace
{
    int failures{};
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; ++failures; } } while (false)

    void TestRoundTripAndNativeDefaults()
    {
        const speed::Config source{
            {"/game/yangjian#", {1.8f, 4.2f}},
            {"/game/wolf#", {1.f, 2.5f}},
            {"/game/auto#", {1.f, 1.f}},
        };
        speed::Config parsed;
        CHECK(speed::Parse(speed::Serialize(source), parsed).success);
        CHECK(parsed == source);
        CHECK(speed::Get(parsed, "/game/yangjian#") == speed::Setting{1.8f, 4.2f});
        CHECK(speed::Get(parsed, "missing") == speed::Setting{1.f, 1.f});
        CHECK(speed::Multiplier(source.at("/game/yangjian#"), false) == 1.8f);
        CHECK(speed::Multiplier(source.at("/game/yangjian#"), true) == 4.2f);
    }

    void TestBoundsAndMalformedInputPreserveOutput()
    {
        CHECK(speed::ValidValue(1.f));
        CHECK(speed::ValidValue(speed::MinimumMultiplier));
        CHECK(speed::ValidValue(speed::MaximumMultiplier));
        CHECK(!speed::ValidValue(0.f));
        CHECK(!speed::ValidValue(speed::MaximumMultiplier + 1.f));
        CHECK(!speed::ValidValue(std::numeric_limits<float>::infinity()));
        CHECK(!speed::ValidValue(std::numeric_limits<float>::quiet_NaN()));
        CHECK(!speed::Valid({1.f, 0.f}));

        const speed::Config original{{"keep", {1.2f, 3.6f}}};
        for (const auto bad : {
            "WRONG\n",
            "GLOSS_TRANSFORMATION_SPEED_MULTIPLIERS_1\n\"a\" 0.09 2\n",
            "GLOSS_TRANSFORMATION_SPEED_MULTIPLIERS_1\n\"a\" 1 5.1\n",
            "GLOSS_TRANSFORMATION_SPEED_MULTIPLIERS_1\n\"a\" nan 1\n",
            "GLOSS_TRANSFORMATION_SPEED_MULTIPLIERS_1\n\"a\" 1\n",
            "GLOSS_TRANSFORMATION_SPEED_MULTIPLIERS_1\n\"a\" 1 2\n\"a\" 3 4\n",
            "GLOSS_TRANSFORMATION_SPEED_MULTIPLIERS_1\n\"\" 1 2\n",
            "GLOSS_TRANSFORMATION_SPEED_MULTIPLIERS_1\n\"a\" 1 2 trailing\n",
        })
        {
            auto parsed = original;
            CHECK(!speed::Parse(bad, parsed).success);
            CHECK(parsed == original);
        }
        auto oversized = original;
        CHECK(!speed::Parse(std::string(speed::MaximumFileBytes + 1, 'x'), oversized).success);
        CHECK(oversized == original);
    }
}

int main()
{
    TestRoundTripAndNativeDefaults();
    TestBoundsAndMalformedInputPreserveOutput();
    if (!failures) std::cout << "PASS per-character normal/sprint speed config, bounds and corrupt input rollback\n";
    return failures ? 1 : 0;
}
