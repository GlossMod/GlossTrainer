#include "../src/gameplay/transformation_adapter.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace adapter = gloss::gameplay::transformation::adapter;

namespace
{
    int failures{};

#define CHECK(...)                                                                                         \
    do                                                                                                     \
    {                                                                                                      \
        if (!(__VA_ARGS__))                                                                                \
        {                                                                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #__VA_ARGS__ "\n";              \
            ++failures;                                                                                   \
        }                                                                                                  \
    } while (false)

    adapter::Movement Ground()
    {
        return {3, 0, 1, 4, 600.f, 600.f, 600.f, 1.f};
    }

    void TestGroundModesAndClassDefaultFallback()
    {
        constexpr std::array<std::uint8_t, 3> modes{1, 2, 3};
        for (const auto mode : modes)
        {
            auto movement = Ground();
            movement.mode = mode;
            const auto decision = adapter::Evaluate(movement);
            CHECK(decision.allowed);
            CHECK(decision.motion == adapter::Motion::Ground);
        }

        auto classDefault = Ground();
        classDefault.mode = 0;
        CHECK(adapter::Evaluate(classDefault, true).allowed);
        CHECK(adapter::Evaluate(classDefault, true).motion == adapter::Motion::Ground);
        CHECK(!adapter::Evaluate(classDefault).allowed);
        CHECK(adapter::Evaluate(classDefault, false, true).allowed);
        CHECK(adapter::Evaluate(classDefault, false, true).motion == adapter::Motion::Stationary);
    }

    void TestVolumeModesRequireTheirMatchingSpeeds()
    {
        auto swimming = Ground();
        swimming.mode = 4;
        CHECK(adapter::Evaluate(swimming).allowed);
        CHECK(adapter::Evaluate(swimming).motion == adapter::Motion::Volume);
        swimming.swimSpeed = 0.f;
        CHECK(!adapter::Evaluate(swimming).allowed);

        auto flying = Ground();
        flying.mode = 5;
        CHECK(adapter::Evaluate(flying).allowed);
        CHECK(adapter::Evaluate(flying).motion == adapter::Motion::Volume);
        flying.flySpeed = 0.f;
        CHECK(!adapter::Evaluate(flying).allowed);
    }

    void TestDedicatedAndInvalidModesAreRejected()
    {
        constexpr std::array<std::uint8_t, 3> modes{6, 7, 255};
        for (const auto mode : modes)
        {
            auto movement = Ground();
            movement.mode = mode;
            CHECK(!adapter::Evaluate(movement).allowed);
        }
        auto custom = Ground();
        custom.custom = 1;
        CHECK(!adapter::Evaluate(custom).allowed);

        auto noWalk = Ground();
        noWalk.walkSpeed = 0.f;
        CHECK(!adapter::Evaluate(noWalk).allowed);

        auto badSpeed = Ground();
        badSpeed.swimSpeed = std::numeric_limits<float>::quiet_NaN();
        CHECK(!adapter::Evaluate(badSpeed).allowed);
        badSpeed = Ground();
        badSpeed.flySpeed = -1.f;
        CHECK(!adapter::Evaluate(badSpeed).allowed);
        badSpeed = Ground();
        badSpeed.walkSpeed = 100001.f;
        CHECK(!adapter::Evaluate(badSpeed).allowed);
        badSpeed = Ground();
        badSpeed.gravity = std::numeric_limits<float>::infinity();
        CHECK(!adapter::Evaluate(badSpeed).allowed);
        badSpeed.gravity = -101.f;
        CHECK(!adapter::Evaluate(badSpeed).allowed);
    }

    void TestSteeringRotatesNormalizesAndClampsGroundInput()
    {
        const auto diagonal = adapter::Steer(adapter::Motion::Ground, 90., 45., 1.f, 1.f);
        CHECK(diagonal.valid);
        CHECK(std::abs(diagonal.direction.x + std::sqrt(.5)) < 1e-5);
        CHECK(std::abs(diagonal.direction.y - std::sqrt(.5)) < 1e-5);
        CHECK(std::abs(diagonal.direction.z) < 1e-5); // Ground motion ignores camera pitch.
        CHECK(std::abs(diagonal.scale - 1.f) < 1e-5);

        const auto partial = adapter::Steer(adapter::Motion::Ground, 0., 0., .5f, 0.f);
        CHECK(partial.valid);
        CHECK(std::abs(partial.direction.x - 1.) < 1e-5);
        CHECK(std::abs(partial.direction.y) < 1e-5);
        CHECK(std::abs(partial.scale - .5f) < 1e-5);

        const auto clamped = adapter::Steer(adapter::Motion::Ground, 0., 0., 4.f, 0.f);
        CHECK(clamped.valid);
        CHECK(std::abs(clamped.direction.x - 1.) < 1e-5);
        CHECK(std::abs(clamped.scale - 1.f) < 1e-5);

        const auto wrappedYaw = adapter::Steer(adapter::Motion::Ground, 450., 0., 1.f, 0.f);
        CHECK(wrappedYaw.valid);
        CHECK(std::abs(wrappedYaw.direction.x) < 1e-5);
        CHECK(std::abs(wrappedYaw.direction.y - 1.) < 1e-5);
    }

    void TestSteeringSupportsPitchForVolumeAndRejectsNonFiniteInput()
    {
        const auto volume = adapter::Steer(adapter::Motion::Volume, 90., 30., 1.f, 0.f);
        CHECK(volume.valid);
        CHECK(std::abs(volume.direction.x) < 1e-5);
        CHECK(std::abs(volume.direction.y - std::cos(30. * .017453292519943295)) < 1e-5);
        CHECK(std::abs(volume.direction.z - .5) < 1e-5);
        CHECK(std::abs(volume.scale - 1.f) < 1e-5);

        const auto stationary = adapter::Steer(adapter::Motion::Stationary, 0., 0., 1.f, 1.f);
        CHECK(stationary.valid);
        CHECK(stationary.direction.x == 0. && stationary.direction.y == 0. && stationary.direction.z == 0.);
        CHECK(stationary.scale == 0.f);

        CHECK(!adapter::Steer(adapter::Motion::Ground, std::numeric_limits<double>::quiet_NaN(), 0., 1.f, 0.f).valid);
        CHECK(!adapter::Steer(adapter::Motion::Volume, 0., 0., 0.f, std::numeric_limits<float>::infinity()).valid);
    }

    void TestNativeLocomotionPlansOwnGaitsAndAvoidsRepeatedMoverResets()
    {
        const auto steering = adapter::Steer(adapter::Motion::Ground, 90., 0., 1.f, 0.f);
        const auto plan = adapter::Locomotion(Ground(), steering, {10., 20., 30.}, false);
        CHECK(plan.valid && plan.moving && plan.speedType == 1 && plan.moveAIType == 3);
        CHECK(std::abs(plan.destination.x - 10.) < 1e-5 && std::abs(plan.destination.y - 1220.) < 1e-5 && plan.destination.z == 30.);
        const auto jog = adapter::Locomotion(Ground(), adapter::Steer(adapter::Motion::Ground, 0., 0., .5f, 0.f), {}, true);
        CHECK(jog.valid && jog.speedType == 0 && jog.moveAIType == 1);
        const auto idle = adapter::Locomotion(Ground(), adapter::Steer(adapter::Motion::Stationary, 0., 0., 1.f, 0.f), {}, false);
        CHECK(idle.valid && !idle.moving);
        CHECK(!adapter::Locomotion(Ground(), steering, {std::numeric_limits<double>::infinity(), 0., 0.}, false).valid);
        adapter::LocomotionRequests requests;
        CHECK(requests.Due(plan, 1000));
        requests.Result(plan, 1000, 17);
        CHECK(requests.requestId == 17);
        CHECK(!requests.Due(plan, 1001) && !requests.Due(plan, 1100) && !requests.Due(plan, 2000));
        CHECK(!requests.Due(jog, 1032) && requests.Due(jog, 1033));
        auto changedAnimation = plan;
        changedAnimation.motionMatchingState = 4;
        CHECK(!requests.Due(changedAnimation, 1032) && requests.Due(changedAnimation, 1033));
        auto near = plan;
        near.origin = {plan.destination.x, plan.destination.y - 50., plan.destination.z};
        CHECK(requests.Due(near, 1100));
        CHECK(requests.Due(plan, 3000)); // Bounded retry if a native mover stopped unexpectedly.
        requests.Result(plan, 4000, 0);
        CHECK(!requests.Due(plan, 4249) && requests.Due(plan, 4250));
        CHECK(!requests.Due(idle, 5000));
        requests.Reset();
        CHECK(!requests.requestId && requests.Due(plan, 6000));
    }
    void TestNativeSprintChangesGaitWithoutChangingDirectionOrBaseAttributes()
    {
        const auto movement = Ground();
        const auto steering = adapter::Steer(adapter::Motion::Ground, -45., 0., .4f, .3f);
        const auto walk = adapter::Locomotion(movement, steering, {}, false);
        const auto sprint = adapter::Locomotion(movement, steering, {}, false, true);
        CHECK(walk.speedType == 0 && sprint.speedType == 2);
        CHECK(sprint.direction.x == walk.direction.x && sprint.direction.y == walk.direction.y);
        CHECK(sprint.destination.x == walk.destination.x && sprint.destination.y == walk.destination.y);
        CHECK(!adapter::Locomotion(movement, {true}, {}, false, true).moving);
        adapter::LocomotionRequests requests;
        requests.Result(walk, 1000, 17);
        CHECK(requests.Due(sprint, 1033));
        requests.Result(sprint, 1033, 18);
        CHECK(!requests.Due(sprint, 1100) && requests.Due(walk, 1100));
    }
}

int main()
{
    TestGroundModesAndClassDefaultFallback();
    TestVolumeModesRequireTheirMatchingSpeeds();
    TestDedicatedAndInvalidModesAreRejected();
    TestSteeringRotatesNormalizesAndClampsGroundInput();
    TestSteeringSupportsPitchForVolumeAndRejectsNonFiniteInput();
    TestNativeLocomotionPlansOwnGaitsAndAvoidsRepeatedMoverResets();
    TestNativeSprintChangesGaitWithoutChangingDirectionOrBaseAttributes();
    if (failures)
    {
        std::cerr << failures << " transformation adapter assertion(s) failed\n";
        return 1;
    }
    std::cout << "GlossTrainer transformation adapter tests passed (0 failures)\n";
    return 0;
}
