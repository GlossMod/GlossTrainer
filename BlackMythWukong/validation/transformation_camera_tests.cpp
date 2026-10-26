#include "../src/gameplay/transformation_camera.hpp"
#include <iostream>
#include <limits>

namespace camera = gloss::gameplay::transformation::camera;
namespace
{
    int failures{};
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; ++failures; } } while (false)

    void TestCharacterAndFormIdentityRoundTrip()
    {
        const camera::Config source{{"/game/杨戬#phase1", 12.5f}, {"/game/杨戬#phase2", 60.f}, {"/game/wolf#", 2.25f}};
        camera::Config parsed;
        CHECK(camera::Parse(camera::Serialize(source), parsed).success);
        CHECK(parsed == source);
        CHECK(camera::Distance(parsed, "/game/杨戬#phase1") == 12.5f);
        CHECK(camera::Distance(parsed, "/game/杨戬#phase2") == 60.f);
        CHECK(camera::Distance(parsed, "unconfigured") == 0.f);
        CHECK(camera::ArmLength(900.f, 0.f) == 900.f);
        CHECK(camera::ArmLength(900.f, 12.5f) == 1250.f);
    }
    void TestInvalidConfigPreservesPreviousState()
    {
        const camera::Config original{{"keep", 15.f}};
        for (const auto bad : {
            "WRONG\n", "GLOSS_TRANSFORMATION_CAMERA_1\n\"a\" -1\n", "GLOSS_TRANSFORMATION_CAMERA_1\n\"a\" 0.5\n",
            "GLOSS_TRANSFORMATION_CAMERA_1\n\"a\" 201\n", "GLOSS_TRANSFORMATION_CAMERA_1\n\"a\" nan\n",
            "GLOSS_TRANSFORMATION_CAMERA_1\n\"a\" 10\n\"a\" 12\n", "GLOSS_TRANSFORMATION_CAMERA_1\n\"a\" 10 garbage\n",
            "GLOSS_TRANSFORMATION_CAMERA_1\n\"\" 10\n", "GLOSS_TRANSFORMATION_CAMERA_1\n\"a\"\n",
            "GLOSS_TRANSFORMATION_CAMERA_1\n\"a\n\" 10\n"})
        {
            auto parsed = original;
            CHECK(!camera::Parse(bad, parsed).success);
            CHECK(parsed == original);
        }
        auto parsed = original;
        CHECK(!camera::Parse(std::string(camera::MaximumFileBytes + 1, 'x'), parsed).success);
        CHECK(parsed == original);
        CHECK(!camera::Valid(std::numeric_limits<float>::infinity()));
        CHECK(!camera::Valid(std::numeric_limits<float>::quiet_NaN()));
        CHECK(camera::Valid(0.f) && camera::Valid(1.f) && camera::Valid(200.f));
    }
}
int main()
{
    TestCharacterAndFormIdentityRoundTrip();
    TestInvalidConfigPreservesPreviousState();
    if (!failures) std::cout << "PASS per-character camera config, bounds, corrupt input rollback and units\n";
    return failures ? 1 : 0;
}
