#pragma once
#include <cmath>

namespace gloss::gameplay {
    struct SwitchCooldownResult {
        float remaining{};
        bool bypassed{};
    };
    // The engine result already combines the generic and per-weapon timers.
    // Preserve invalid/sentinel results and every query outside the local binding.
    inline SwitchCooldownResult ApplySwitchCooldown(float original, bool enabled,
        bool currentLocalPlayer, bool knownSlot) noexcept {
        if (!enabled || !currentLocalPlayer || !knownSlot ||
            !std::isfinite(original) || original <= 0) return {original, false};
        return {0, true};
    }
}
