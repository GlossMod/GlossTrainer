#include "../src/gameplay/tag_rules.hpp"
#include <array>
#include <iostream>

namespace {
    using namespace gloss::gameplay;
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)

    void TestContributionPreservesPreexistingCountsAndAddsOnlyOne() {
        LooseTagContribution tag;
        CHECK(tag.BeginAdd(3));
        CHECK(!tag.BeginAdd(3));
        CHECK(tag.ObserveAdd(4));
        CHECK(tag.baseline == 3 && tag.owned && tag.confirmed);
        CHECK(tag.ShouldRemove(4));
        CHECK(tag.BeginRemove(4));
        CHECK(!tag.ShouldRemove(4));
        CHECK(tag.ObserveRemoval(3));
        CHECK(!tag.owned);
        CHECK(!tag.ShouldRemove(3));
        CHECK(tag.ObserveNoContribution(3));
    }
    void TestGameRemovedContributionNeverConsumesAnotherSource() {
        LooseTagContribution tag;
        CHECK(tag.BeginAdd(2) && tag.ObserveAdd(3));
        CHECK(!tag.ShouldRemove(2));
        CHECK(tag.ObserveNoContribution(2));
        CHECK(!tag.owned);
        CHECK(!tag.BeginRemove(2));
        CHECK(tag.BeginAdd(0) && tag.ObserveAdd(1));
        CHECK(tag.ObserveNoContribution(0));
        CHECK(!tag.owned);
    }
    void TestIndeterminateRemovalIsNotReplayed() {
        LooseTagContribution tag;
        CHECK(tag.BeginAdd(2) && tag.ObserveAdd(3));
        CHECK(tag.BeginRemove(3));
        CHECK(!tag.ObserveRemoval({}));
        CHECK(tag.owned && tag.removalPending);
        CHECK(!tag.BeginRemove(3));
        CHECK(!tag.ShouldRemove(3));
        CHECK(!tag.ObserveRemoval(3));
        CHECK(tag.ObserveRemoval(2));
        CHECK(!tag.owned);
    }
    void TestPartialEnableCanRollbackWithoutLosingUnconfirmedWrites() {
        std::array<LooseTagContribution, 5> tags;
        CHECK(tags[0].BeginAdd(0) && tags[0].ObserveAdd(1));
        CHECK(tags[1].BeginAdd(3) && tags[1].ObserveAdd(4));
        CHECK(tags[2].BeginAdd(0));
        CHECK(!tags[2].ObserveAdd({}));
        CHECK(tags[2].owned && !tags[2].confirmed);
        CHECK(tags[2].BeginRemove(1));
        CHECK(tags[2].ObserveRemoval(0));
        CHECK(tags[1].BeginRemove(4));
        CHECK(tags[1].ObserveRemoval(3));
        CHECK(tags[0].BeginRemove(1));
        CHECK(tags[0].ObserveRemoval(0));
        for (const auto& tag : tags) CHECK(!tag.owned);
    }
    void TestFailedAddAndAdditionalExternalCountsStayConservative() {
        LooseTagContribution tag;
        CHECK(!tag.BeginAdd(-1));
        CHECK(!tag.BeginAdd(100001));
        CHECK(tag.BeginAdd(2));
        CHECK(!tag.ObserveAdd(2));
        CHECK(!tag.owned);
        CHECK(tag.BeginAdd(2));
        CHECK(!tag.ObserveAdd(4)); // Concurrent unrelated source, not exact +1.
        CHECK(tag.owned && !tag.confirmed);
        CHECK(tag.BeginRemove(4));
        CHECK(tag.ObserveRemoval(3)); // Exactly one removed; external count kept.
        CHECK(!tag.owned);
        CHECK(tag.BeginAdd(0) && tag.ObserveAdd(1));
        CHECK(tag.BeginRemove(1));
        tag.CancelUnexecutedRemoval();
        CHECK(tag.ShouldRemove(1));
    }
    void TestTargetIdentityIncludesObjectGeneration() {
        const TagTargetIdentity identity{1, 2, 3, 10, 20, 30};
        CHECK(identity.Valid());
        CHECK(!TagTargetIdentity{}.Valid());
        for (int field = 0; field < 6; ++field) {
            auto next = identity;
            switch (field) {
            case 0: ++next.world; break; case 1: ++next.pawn; break;
            case 2: ++next.component; break; case 3: ++next.worldIndex; break;
            case 4: ++next.pawnIndex; break; case 5: ++next.componentIndex; break;
            }
            CHECK(next != identity);
        }
    }
}
int main() {
    TestContributionPreservesPreexistingCountsAndAddsOnlyOne();
    TestGameRemovedContributionNeverConsumesAnotherSource();
    TestIndeterminateRemovalIsNotReplayed();
    TestPartialEnableCanRollbackWithoutLosingUnconfirmedWrites();
    TestFailedAddAndAdditionalExternalCountsStayConservative();
    TestTargetIdentityIncludesObjectGeneration();
    std::cout << checks << " tag checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
