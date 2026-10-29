#pragma once
#include <cmath>
#include <optional>
namespace gloss::gameplay {
    // First compensate an additive modifier, then use two engine observations
    // to account for a multiplier. The caller bounds the number of attempts and
    // must verify the actual result; clamps/override effects may be unsolvable.
    inline std::optional<float> CorrectedAttributeBase(float base, float actual, float target,
        std::optional<float> previousBase = {}, std::optional<float> previousActual = {}) noexcept {
        if (!std::isfinite(base) || !std::isfinite(actual) || !std::isfinite(target) ||
            base < 0 || base > 100000000 || actual <= 0 || target < 1 || target > 100000000) return {};
        double slope = 1;
        if (previousBase && previousActual) {
            if (!std::isfinite(*previousBase) || !std::isfinite(*previousActual) || std::abs(base - *previousBase) < 0.001f)
                return {};
            slope = (static_cast<double>(actual) - *previousActual) / (static_cast<double>(base) - *previousBase);
            if (!std::isfinite(slope) || slope < 0.000001 || slope > 1000000) return {};
        }
        const double corrected = base + (static_cast<double>(target) - actual) / slope;
        if (!std::isfinite(corrected) || corrected < 0 || corrected > 100000000) return {};
        const auto result = static_cast<float>(corrected);
        return result != base ? std::optional(result) : std::nullopt;
    }
}
