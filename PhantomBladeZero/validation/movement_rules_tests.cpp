#include "../src/gameplay/movement_rules.hpp"
#include <iostream>
#include <limits>

namespace {
    using namespace gloss::gameplay;
    int failures{}, checks{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)

    void TestTargetsUseOriginalBaselineAndValidateBounds() {
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(MovementTarget(600, 2, MovementValueKind::Multiplier, 0.1f, 5) == 1200);
        CHECK(MovementTarget(700, 4, MovementValueKind::JumpHeight, 0.1f, 10) == 1400);
        CHECK(MovementTarget(2.2f, 0, MovementValueKind::Multiplier, 0, 5) == 0);
        CHECK(MovementTarget(90, 120, MovementValueKind::Absolute, 30, 150) == 120);
        CHECK(!MovementTarget(nan, 2, MovementValueKind::Multiplier, 0.1f, 5));
        CHECK(!MovementTarget(600, nan, MovementValueKind::Multiplier, 0.1f, 5));
        CHECK(!MovementTarget(600, 0, MovementValueKind::Multiplier, 0.1f, 5));
        CHECK(!MovementTarget(600, 6, MovementValueKind::Multiplier, 0.1f, 5));
        CHECK(!MovementTarget(600, -1, MovementValueKind::JumpHeight, 0.1f, 10));
        CHECK(!MovementTarget(100000000, 5, MovementValueKind::Multiplier, 0.1f, 5));
        CHECK(!MovementValueClose(nan, 1));
        CHECK(MovementValueClose(1.0000001f, 1));
        CHECK(!MovementValueClose(1.001f, 1));
    }

    void TestIdentityAndBaselineNeverFollowReusedObjects() {
        const MovementIdentity first{1, 2, 3, 10, 20, 30};
        MovementBaseline baseline;
        const float original[]{600, 300};
        CHECK(baseline.Capture(first, original, 2));
        const float modified[]{1200, 600};
        CHECK(baseline.Capture(first, modified, 2));
        CHECK(baseline.original[0] == 600 && baseline.original[1] == 300);
        CHECK(baseline.CanRestore(first));
        auto reusedPawn = first;
        ++reusedPawn.pawnIndex;
        CHECK(!baseline.CanRestore(reusedPawn));
        CHECK(!baseline.Capture(reusedPawn, modified, 2));
        auto reusedComponent = first;
        ++reusedComponent.componentIndex;
        CHECK(!baseline.CanRestore(reusedComponent));
        auto nextWorld = first;
        ++nextWorld.worldIndex;
        CHECK(!baseline.CanRestore(nextWorld));
        CHECK(!baseline.Capture(first, modified, 1));
        baseline.Clear();
        CHECK(!baseline.captured && !baseline.CanRestore(first));
        CHECK(baseline.Capture(reusedPawn, modified, 2));
        CHECK(baseline.original[0] == 1200);
    }

    void TestInvalidCapturesAndUnsafeFlightExitNeverBecomeRestorable() {
        MovementBaseline baseline;
        const float values[]{600};
        CHECK(!baseline.Capture({}, values, 1));
        CHECK(!baseline.Capture({1, 2, 3}, nullptr, 1));
        CHECK(!baseline.Capture({1, 2, 3}, values, 0));
        CHECK(!baseline.Capture({1, 2, 3}, values, 6));
        const float invalid[]{std::numeric_limits<float>::infinity()};
        CHECK(!baseline.Capture({1, 2, 3}, invalid, 1));
        CHECK(!baseline.captured);
        CHECK(!SafeFlightExit(false, true, true));
        CHECK(!SafeFlightExit(true, false, true));
        CHECK(!SafeFlightExit(true, true, false));
        CHECK(SafeFlightExit(true, true, true));
    }
    void TestAiOwnershipRequiresBidirectionalControllerAndExcludesPlayer() {
        const AiMovementOwnership owned{10, 20, 10, 20, true, false, true};
        CHECK(IsOwnedAiPawn(owned));
        auto local = owned;
        local.localPawn = true;
        CHECK(!IsOwnedAiPawn(local));
        auto wrongController = owned;
        wrongController.aiController = false;
        CHECK(!IsOwnedAiPawn(wrongController));
        auto stalePawn = owned;
        stalePawn.controllerPawn = 30;
        CHECK(!IsOwnedAiPawn(stalePawn));
        auto staleController = owned;
        staleController.pawnController = 30;
        CHECK(!IsOwnedAiPawn(staleController));
        auto destroyed = owned;
        destroyed.objectsValid = false;
        CHECK(!IsOwnedAiPawn(destroyed));
        CHECK(!IsOwnedAiPawn({}));
    }
    void TestPossessChangeRestoresLivePreviousObjectsAndPreservesUnavailableBaselines() {
        CHECK(DecideMovementCleanup(true, true, true) == MovementCleanupDecision::RestoreCurrent);
        CHECK(DecideMovementCleanup(false, true, true) == MovementCleanupDecision::RestorePrevious);
        CHECK(DecideMovementCleanup(false, true, false) == MovementCleanupDecision::PreserveUnavailable);
        CHECK(DecideMovementCleanup(false, false, false) == MovementCleanupDecision::DiscardExpired);
        MovementBaseline baseline;
        const MovementIdentity previous{1, 2, 3, 10, 20, 30};
        const float value[]{1};
        CHECK(baseline.Capture(previous, value, 1));
        CHECK(DecideMovementCleanup(false, true, true) != MovementCleanupDecision::DiscardExpired);
        CHECK(baseline.CanRestore(previous)); // Returning to A still has A's original value.
    }
    void TestPlayerSpeedMaintenanceStopsAfterFailureAndNeverTouchesChangedObjects() {
        CHECK(DecideMovementMaintenance(true, false, true, 1, 1.2f) == MovementMaintenanceDecision::Rewrite);
        CHECK(DecideMovementMaintenance(true, false, true, 1.2f, 1.2f) == MovementMaintenanceDecision::None);
        CHECK(DecideMovementMaintenance(true, false, false, 1, 1.2f) == MovementMaintenanceDecision::Stop);
        CHECK(DecideMovementMaintenance(true, true, true, 1, 1.2f) == MovementMaintenanceDecision::None);
        CHECK(DecideMovementMaintenance(false, false, true, 1, 1.2f) == MovementMaintenanceDecision::None);
        CHECK(DecideMovementMaintenance(true, false, true, std::numeric_limits<float>::quiet_NaN(), 1.2f) == MovementMaintenanceDecision::Stop);
    }
}

int main() {
    TestTargetsUseOriginalBaselineAndValidateBounds();
    TestIdentityAndBaselineNeverFollowReusedObjects();
    TestInvalidCapturesAndUnsafeFlightExitNeverBecomeRestorable();
    TestAiOwnershipRequiresBidirectionalControllerAndExcludesPlayer();
    TestPossessChangeRestoresLivePreviousObjectsAndPreservesUnavailableBaselines();
    TestPlayerSpeedMaintenanceStopsAfterFailureAndNeverTouchesChangedObjects();
    std::cout << checks << " movement checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
