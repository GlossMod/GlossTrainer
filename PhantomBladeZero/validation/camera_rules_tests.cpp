#include "../src/gameplay/camera_rules.hpp"
#include <iostream>
#include <limits>

namespace {
    using namespace gloss::gameplay;
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)

    void TestHeightKeepsSignedBaselineAndOtherAxes() {
        const CameraVector original{12, -24, -40};
        const auto first = CameraHeightTarget(original, 100);
        CHECK(first && first->x == 12 && first->y == -24 && first->z == 60);
        const auto edited = CameraHeightTarget(original, -50);
        CHECK(edited && edited->x == 12 && edited->y == -24 && edited->z == -90);
        CHECK(CameraHeightTarget(original, 0)->z == -40);
        CHECK(!CameraHeightTarget(original, 301));
        CHECK(!CameraHeightTarget(original, -301));
        CHECK(!CameraHeightTarget(original, std::numeric_limits<float>::quiet_NaN()));
        CHECK(!CameraHeightTarget({0, 0, 99999}, 300));
        CHECK(!CameraHeightTarget({0, std::numeric_limits<double>::infinity(), 0}, 0));
    }
    void TestDistanceHonorsModeBoundsAndFiniteValues() {
        CHECK(CameraDistanceAllowed(100, 100, 1000));
        CHECK(CameraDistanceAllowed(1000, 100, 1000));
        CHECK(!CameraDistanceAllowed(99, 100, 1000));
        CHECK(!CameraDistanceAllowed(1001, 100, 1000));
        CHECK(!CameraDistanceAllowed(500, 1000, 100));
        CHECK(!CameraDistanceAllowed(500, -1, 1000));
        CHECK(!CameraDistanceAllowed(500, 0, std::numeric_limits<float>::infinity()));
        CHECK(!CameraDistanceAllowed(std::numeric_limits<float>::quiet_NaN(), 100, 1000));
    }
    void TestEditsDoNotReplaceOriginalAndReusedObjectsCannotRestore() {
        CameraIdentity identity{1, 2, 3, 4, 11, 12, 13, 14};
        CameraValues before{600, 800, 200, {1, -2, -3}, {1, -2, -3}, 5};
        CameraBaseline baseline;
        CHECK(baseline.Capture(identity, before));
        auto edited = before; edited.distance = 900; edited.heightTarget.z = 200;
        CHECK(baseline.Capture(identity, edited));
        CHECK(baseline.original.distance == 600 && baseline.original.additiveDistance == 200);
        CHECK(baseline.original.heightTarget.z == -3 && baseline.original.heightInterpSpeed == 5);
        CHECK(baseline.CanRestore(identity));
        auto reused = identity; ++reused.locationIndex;
        CHECK(!baseline.CanRestore(reused)); CHECK(!baseline.Capture(reused, before));
        auto newWorld = identity; ++newWorld.worldIndex;
        CHECK(!baseline.CanRestore(newWorld));
        auto invalid = before; invalid.heightInterpSpeed = std::numeric_limits<float>::quiet_NaN();
        CHECK(!CameraBaseline{}.Capture(identity, invalid));
        identity.location = 0;
        CHECK(!CameraBaseline{}.Capture(identity, before));
    }
}

int main() {
    TestHeightKeepsSignedBaselineAndOtherAxes();
    TestDistanceHonorsModeBoundsAndFiniteValues();
    TestEditsDoNotReplaceOriginalAndReusedObjectsCannotRestore();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
