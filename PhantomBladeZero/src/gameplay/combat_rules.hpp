#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace gloss::gameplay {
    inline float ProtectedShachi(float current, float maximum, float proposed) noexcept {
        if (!std::isfinite(current) || !std::isfinite(maximum) || !std::isfinite(proposed) ||
            current < 0 || maximum <= 0 || maximum > 100000000) return proposed;
        return (std::max)(proposed, (std::min)(current, maximum));
    }
    inline bool ShouldAttemptPerfectDodge(std::int32_t handle, double hitTime, double gameTime,
        bool hasHit, std::int32_t previousHandle, double previousHitTime) noexcept {
        return handle >= 0 && std::isfinite(hitTime) && std::isfinite(gameTime) && gameTime >= 0 &&
            (hasHit || hitTime <= gameTime + 0.04) &&
            !(previousHandle == handle && previousHitTime == hitTime);
    }
}
