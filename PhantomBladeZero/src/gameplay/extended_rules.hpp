#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace gloss::gameplay {
    inline constexpr float MaximumDamageDelta = 100000000.0f;
    inline bool ValidDamageMultiplier(float value) noexcept {
        return std::isfinite(value) && value >= 0.1f && value <= 100.0f;
    }
    enum class DamageDirection { Unrelated, Outgoing, Incoming };
    inline DamageDirection PlayerDamageDirection(bool attackerIsPlayer, bool targetIsPlayer) noexcept {
        if (attackerIsPlayer == targetIsPlayer) return DamageDirection::Unrelated;
        return attackerIsPlayer ? DamageDirection::Outgoing : DamageDirection::Incoming;
    }
    // Work on a single already-calculated damage delta, never on the attribute
    // maximum or on healing. Defense is reciprocal incoming damage scaling.
    inline std::optional<float> ExtendedDamageDelta(float delta, bool outgoing,
        bool incoming, float damage, float defense, bool lethal, float current) noexcept {
        if (!std::isfinite(delta) || std::abs(delta) > MaximumDamageDelta ||
            !ValidDamageMultiplier(damage) || !ValidDamageMultiplier(defense)) return {};
        // Self effects and NPC interactions are never a player attack/defense edit.
        if (delta >= 0 || outgoing == incoming) return delta;
        double result = delta;
        if (outgoing) result *= damage;
        if (incoming) result /= defense;
        if (outgoing && lethal) {
            if (!std::isfinite(current) || current < 0 || current >= MaximumDamageDelta) return {};
            result = (std::min)(result, -static_cast<double>(current) - 1.0);
        }
        if (!std::isfinite(result)) return {};
        result = (std::max)(result, -static_cast<double>(MaximumDamageDelta));
        return static_cast<float>(result);
    }
}
