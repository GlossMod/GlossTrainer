#pragma once

#include <cstdint>
#include <optional>

namespace gloss::gameplay {
    enum class TagTargetStatus { Current, Expired, Unavailable };
    struct TagTargetIdentity {
        std::uint64_t world{}, pawn{}, component{};
        std::uint32_t worldIndex{}, pawnIndex{}, componentIndex{};
        bool operator==(const TagTargetIdentity&) const = default;
        bool Valid() const { return world && pawn && component; }
    };

    // One ledger entry owns at most one loose-tag count. A removal whose result
    // could not be read must never be blindly replayed and consume another source.
    struct LooseTagContribution {
        std::int32_t baseline{};
        bool owned{}, confirmed{}, removalPending{};
        std::int32_t removalTarget{};

        bool BeginAdd(std::int32_t observed) {
            if (owned || observed < 0 || observed > 100000) return false;
            baseline = observed;
            owned = true;
            confirmed = false;
            return true;
        }
        bool ObserveAdd(std::optional<std::int32_t> observed) {
            if (!owned || !observed || *observed < 0) return false;
            if (*observed <= baseline) { Clear(); return false; }
            confirmed = *observed == baseline + 1;
            return confirmed;
        }
        bool ShouldRemove(std::int32_t observed) const {
            return owned && !removalPending && observed > baseline;
        }
        bool ObserveNoContribution(std::int32_t observed) {
            if (owned && !removalPending && observed >= 0 && observed <= baseline) { Clear(); return true; }
            return !owned;
        }
        bool BeginRemove(std::int32_t observed) {
            if (!ShouldRemove(observed)) return false;
            removalPending = true;
            removalTarget = observed - 1;
            return true;
        }
        bool ObserveRemoval(std::optional<std::int32_t> observed) {
            if (!owned) return true;
            if (!removalPending || !observed || *observed < 0 || *observed > removalTarget) return false;
            Clear();
            return true;
        }
        void CancelUnexecutedRemoval() { removalPending = false; }
        void Clear() { *this = {}; }
    };
}
