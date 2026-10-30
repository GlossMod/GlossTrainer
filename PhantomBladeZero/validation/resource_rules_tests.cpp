#include "../src/gameplay/resource_rules.hpp"
#include <iostream>
#include <limits>

namespace {
    using namespace gloss::gameplay;
    int failures{}, checks{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)

    void TestResourceProtectionDoesNotInterceptChargeConversion() {
        CHECK(ProtectedResourceValue(80, 100, 30, ProtectedResourcePool::Health) == 80);
        CHECK(ProtectedResourceValue(80, 100, 95, ProtectedResourcePool::Health) == 95);
        CHECK(ProtectedResourceValue(120, 100, 30, ProtectedResourcePool::Health) == 100);
        CHECK(ProtectedResourceValue(2, 3, 1, ProtectedResourcePool::PureShachiSegments) == 2);
        CHECK(ProtectedResourceValue(2, 3, 3, ProtectedResourcePool::PureShachiSegments) == 3);
        CHECK(ProtectedResourceValue(100, 100, 0, ProtectedResourcePool::ChargeProgress) == 0);
        CHECK(ProtectedResourceValue(50, 100, 60, ProtectedResourcePool::ChargeProgress) == 60);
        CHECK(ProtectedResourceValue(2, 0, 1, ProtectedResourcePool::PureShachiSegments) == 1);
        CHECK(ProtectedResourceValue(-1, 100, 10, ProtectedResourcePool::Health) == 10);
        CHECK(ProtectedResourceValue(80, -1, 10, ProtectedResourcePool::Health) == 10);
        CHECK(ProtectedResourceValue(80, 100001000, 10, ProtectedResourcePool::Health) == 10);
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(ProtectedResourceValue(nan, 100, 10, ProtectedResourcePool::Health) == 10);
        CHECK(ProtectedResourceValue(80, nan, 10, ProtectedResourcePool::Health) == 10);
        CHECK(std::isnan(ProtectedResourceValue(80, 100, nan, ProtectedResourcePool::Health)));
    }

    void TestGainCapturesOriginalValuesAndRejectsReusedObjects() {
        const ResourceIdentity first{1, 2, 3, 4, 10, 20, 30, 40};
        ResourceGainBaseline baseline;
        CHECK(baseline.Capture(first, 1, 1.2f));
        CHECK(baseline.Target(baseline.base, 5) == 5);
        CHECK(baseline.Target(baseline.current, 5) == 6);
        CHECK(baseline.Capture(first, 5, 6));
        CHECK(baseline.base == 1 && baseline.current == 1.2f);
        CHECK(baseline.Target(baseline.base, 2) == 2); // Edits do not compound.
        CHECK(baseline.Target(baseline.current, 0) == 0);
        CHECK(baseline.CanRestore(first));
        for (int field = 0; field < 8; ++field) {
            auto next = first;
            switch (field) {
            case 0: ++next.world; break; case 1: ++next.pawn; break;
            case 2: ++next.component; break; case 3: ++next.attributes; break;
            case 4: ++next.worldIndex; break; case 5: ++next.pawnIndex; break;
            case 6: ++next.componentIndex; break; case 7: ++next.attributeIndex; break;
            }
            CHECK(!baseline.CanRestore(next));
            CHECK(!baseline.Capture(next, 1, 1));
        }
        CHECK(!baseline.Target(baseline.base, -1));
        CHECK(!baseline.Target(baseline.base, 101));
        CHECK(!baseline.Target(baseline.base, std::numeric_limits<float>::infinity()));
        baseline.Clear();
        CHECK(!baseline.captured && !baseline.CanRestore(first));
        CHECK(!baseline.Capture({}, 1, 1));
        CHECK(!baseline.Capture(first, -1, 1));
        CHECK(!baseline.Capture(first, 1, std::numeric_limits<float>::quiet_NaN()));
        CHECK(!baseline.Capture(first, 1000001, 1));
    }

    void TestFailedWritesAndSessionChangesPreserveSuccessfulPreference() {
        ResourceGainPreference preference;
        CHECK(preference.Commit(true, 2, true, true));
        CHECK(preference.enabled && preference.multiplier == 2);
        CHECK(!preference.Commit(true, 3, true, false)); // Failed write/readback.
        CHECK(preference.enabled && preference.multiplier == 2);
        CHECK(!preference.Commit(true, 4, false, true)); // Changed player/index.
        CHECK(preference.enabled && preference.multiplier == 2);
        CHECK(!preference.Commit(false, 1, true, false)); // Failed restore.
        CHECK(preference.enabled && preference.multiplier == 2);
        CHECK(preference.Commit(false, 1, true, true));
        CHECK(!preference.enabled && preference.multiplier == 2);
        CHECK(!preference.Commit(true, 101, true, true));
        CHECK(!preference.enabled && preference.multiplier == 2);
        CHECK(!ResourceValueClose(std::numeric_limits<float>::quiet_NaN(), 1));
        CHECK(!ResourceValueClose(1.1f, 1));
        CHECK(ResourceValueClose(1.0000001f, 1));
    }

    void TestCostOnlyChangesActiveSkillOutputs() {
        for (const auto source : {ShachiCostSource::HeavyAttackGetter, ShachiCostSource::InitialAbilityProperty,
            ShachiCostSource::ConstantAbilityProperty}) {
            CHECK(ScaledShachiCost(20, 0, source, true) == 0);
            CHECK(ScaledShachiCost(20, 0.5, source, true) == 10);
            CHECK(ScaledShachiCost(20, 10, source, true) == 200);
            CHECK(!ScaledShachiCost(20, 0, source, false)); // Enemy/default/nonlocal ability.
            CHECK(!ScaledShachiCost(-20, 0.5, source, true)); // Never adjust a generic negative delta.
        }
        CHECK(!ScaledShachiCost(20, 0, ShachiCostSource::Damage, true));
        CHECK(!ScaledShachiCost(20, 0, ShachiCostSource::Other, true));
        CHECK(!ScaledShachiCost(20, 0, static_cast<ShachiCostSource>(99), true));
        CHECK(!ScaledShachiCost(20, -1, ShachiCostSource::HeavyAttackGetter, true));
        CHECK(!ScaledShachiCost(20, 10.1, ShachiCostSource::HeavyAttackGetter, true));
        CHECK(!ScaledShachiCost(std::numeric_limits<double>::quiet_NaN(), 1, ShachiCostSource::HeavyAttackGetter, true));
        CHECK(!ScaledShachiCost(20, std::numeric_limits<double>::infinity(), ShachiCostSource::HeavyAttackGetter, true));
        CHECK(!ScaledShachiCost(100000000, 10, ShachiCostSource::HeavyAttackGetter, true));
        // Each VM call scales a fresh game result; editing the multiplier never compounds a stored ability field.
        const double abilityCost = 20;
        CHECK(ScaledShachiCost(abilityCost, 2, ShachiCostSource::InitialAbilityProperty, true) == 40);
        CHECK(ScaledShachiCost(abilityCost, 3, ShachiCostSource::InitialAbilityProperty, true) == 60);
        CHECK(abilityCost == 20);
    }

    void TestCostReferenceAliasesAndOverlapAreRejected() {
        constexpr std::uint64_t object = 0x100000;
        CHECK(!ShachiCostTemporaryOutput(0, object, 0x800)); // Reference-only VM evaluation.
        CHECK(!ShachiCostTemporaryOutput(object + 0x650, object, 0x800));
        CHECK(!ShachiCostTemporaryOutput(object + 0x658, object, 0x800));
        CHECK(!ShachiCostTemporaryOutput(object + 0x720, object, 0x800));
        CHECK(!ShachiCostTemporaryOutput(object - 4, object, 0x800)); // Partial overlap.
        CHECK(ShachiCostTemporaryOutput(object - 8, object, 0x800));
        CHECK(ShachiCostTemporaryOutput(object + 0x800, object, 0x800));
        CHECK(!ShachiCostTemporaryOutput(object + 0x1000, object, 0));
        CHECK(!ShachiCostTemporaryOutput(object + 0x1000, object, 0x100001));
        CHECK(!ShachiCostTemporaryOutput(UINT64_MAX - 4, object, 0x800));
        CHECK(!ShachiCostTemporaryOutput(object, UINT64_MAX - 4, 0x800));
    }

    void TestCostFailureAndChangedIdentityDoNotCommitOrReplay() {
        const ResourceIdentity first{1, 2, 3, 4, 10, 20, 30, 40};
        ShachiCostPreference preference;
        CHECK(!preference.Commit(first, 2, false, true));
        CHECK(!preference.enabled && preference.multiplier == 1);
        CHECK(preference.Commit(first, 2, true, true));
        const auto epoch = preference.generation;
        CHECK(preference.Current(first, epoch));
        CHECK(!preference.Commit(first, 3, true, false));
        CHECK(preference.Current(first, epoch) && preference.multiplier == 2);
        CHECK(!preference.Commit(first, 11, true, true));
        CHECK(preference.multiplier == 2);
        for (int field = 0; field < 8; ++field) {
            auto changed = first;
            switch (field) {
            case 0: ++changed.world; break; case 1: ++changed.pawn; break;
            case 2: ++changed.component; break; case 3: ++changed.attributes; break;
            case 4: ++changed.worldIndex; break; case 5: ++changed.pawnIndex; break;
            case 6: ++changed.componentIndex; break; case 7: ++changed.attributeIndex; break;
            }
            CHECK(!preference.Current(changed, epoch));
        }
        preference.uncertain = true;
        CHECK(!preference.Current(first, epoch)); // A failed output is not automatically retried.
        preference.Stop();
        CHECK(!preference.enabled && !preference.uncertain && !preference.Current(first, epoch));
        CHECK(preference.multiplier == 2);
        CHECK(preference.Commit(first, 3, true, true));
        CHECK(!preference.Current(first, epoch)); // A call entered before off/re-enable cannot change output.
        CHECK(preference.Current(first, preference.generation));
        CHECK(preference.multiplier == 3);
        preference.Stop();
        CHECK(!preference.Current(first, preference.generation)); // New pawn/session never replays a cost operation.
    }
}

int main() {
    TestResourceProtectionDoesNotInterceptChargeConversion();
    TestGainCapturesOriginalValuesAndRejectsReusedObjects();
    TestFailedWritesAndSessionChangesPreserveSuccessfulPreference();
    TestCostOnlyChangesActiveSkillOutputs();
    TestCostReferenceAliasesAndOverlapAreRejected();
    TestCostFailureAndChangedIdentityDoNotCommitOrReplay();
    std::cout << checks << " resource checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
