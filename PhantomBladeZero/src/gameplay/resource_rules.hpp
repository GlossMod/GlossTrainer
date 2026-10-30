#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace gloss::gameplay {
    enum class ProtectedResourcePool { Health, PureShachiSegments, ChargeProgress };

    inline float ProtectedResourceValue(float current, float maximum, float proposed, ProtectedResourcePool pool) noexcept {
        // Progress resetting to zero can grant a completed segment. Only protect
        // segment consumption; preserving progress would duplicate that grant.
        if (pool == ProtectedResourcePool::ChargeProgress || !std::isfinite(current) ||
            !std::isfinite(maximum) || !std::isfinite(proposed) || current < 0 ||
            maximum <= 0 || maximum > 100000000) return proposed;
        return (std::max)(proposed, (std::min)(current, maximum));
    }

    struct ResourceIdentity {
        std::uint64_t world{}, pawn{}, component{}, attributes{};
        std::uint32_t worldIndex{}, pawnIndex{}, componentIndex{}, attributeIndex{};
        bool operator==(const ResourceIdentity&) const = default;
        bool Valid() const { return world && pawn && component && attributes; }
    };

    inline bool ResourceValueClose(float actual, float expected) noexcept {
        return std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(actual - expected) <= 0.00001f * (std::max)(1.0f, std::abs(expected));
    }

    struct ResourceGainBaseline {
        ResourceIdentity identity;
        float base{}, current{};
        bool captured{};

        bool Capture(ResourceIdentity next, float nextBase, float nextCurrent) {
            if (!next.Valid() || !std::isfinite(nextBase) || !std::isfinite(nextCurrent) ||
                nextBase < 0 || nextCurrent < 0 || nextBase > 1000000 || nextCurrent > 1000000) return false;
            if (captured) return identity == next;
            identity = next;
            base = nextBase;
            current = nextCurrent;
            captured = true;
            return true;
        }
        bool CanRestore(ResourceIdentity next) const { return captured && identity == next; }
        std::optional<float> Target(float original, float multiplier) const {
            if (!captured || !std::isfinite(multiplier) || multiplier < 0 || multiplier > 100) return {};
            const double result = static_cast<double>(original) * multiplier;
            if (!std::isfinite(result) || result < 0 || result > 100000000) return {};
            return static_cast<float>(result);
        }
        void Clear() { *this = {}; }
    };

    struct ResourceGainPreference {
        bool enabled{};
        float multiplier{1};
        // Native writes and rollback are separate from successful user intent.
        bool Commit(bool nextEnabled, float nextMultiplier, bool identityCurrent, bool readbackMatches) {
            if (!identityCurrent || !readbackMatches || !std::isfinite(nextMultiplier) ||
                nextMultiplier < 0 || nextMultiplier > 100) return false;
            enabled = nextEnabled;
            if (nextEnabled) multiplier = nextMultiplier;
            return true;
        }
    };

    enum class ShachiCostSource { HeavyAttackGetter, InitialAbilityProperty, ConstantAbilityProperty, Damage, Other };

    inline std::optional<double> ScaledShachiCost(double original, double multiplier,
        ShachiCostSource source, bool localAbility) noexcept {
        if (!localAbility || (source != ShachiCostSource::HeavyAttackGetter &&
            source != ShachiCostSource::InitialAbilityProperty && source != ShachiCostSource::ConstantAbilityProperty) ||
            !std::isfinite(original) || original < 0 || original > 100000000 ||
            !std::isfinite(multiplier) || multiplier < 0 || multiplier > 10) return {};
        const auto result = original * multiplier;
        if (!std::isfinite(result) || result > 100000000) return {};
        return result;
    }

    // VM reference evaluation can return an alias to the actual ability field.
    // Only a separate output buffer may be changed; ability defaults stay intact.
    inline bool ShachiCostTemporaryOutput(std::uint64_t output, std::uint64_t object,
        std::uint32_t objectSize) noexcept {
        if (output < 0x10000 || object < 0x10000 || objectSize < 0x28 || objectSize > 0x100000 ||
            output > UINT64_MAX - sizeof(double) || object > UINT64_MAX - objectSize) return false;
        return output + sizeof(double) <= object || output >= object + objectSize;
    }

    struct ShachiCostPreference {
        ResourceIdentity identity;
        double multiplier{1};
        std::uint64_t generation{};
        bool enabled{}, uncertain{};

        bool Commit(ResourceIdentity next, double value, bool hooksOwned, bool identityCurrent) noexcept {
            if (!next.Valid() || !hooksOwned || !identityCurrent || !std::isfinite(value) || value < 0 || value > 10)
                return false;
            identity = next;
            multiplier = value;
            enabled = true;
            uncertain = false;
            ++generation;
            return true;
        }
        bool Current(ResourceIdentity observed, std::uint64_t epoch) const noexcept {
            return enabled && !uncertain && epoch == generation && identity == observed;
        }
        void Stop() noexcept {
            enabled = false;
            uncertain = false;
            identity = {};
            ++generation;
        }
    };
}
