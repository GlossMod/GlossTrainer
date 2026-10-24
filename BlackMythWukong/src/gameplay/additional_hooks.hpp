#pragma once
#include "../features.hpp"
#include <cstdint>

namespace gloss::gameplay::extra
{
    void Initialize();
    bool Supported(features::Id id);
    bool Enabled(features::Id id) noexcept;
    features::Result SetEnabled(features::Id id, bool enabled);
    // A fresh canonical native pair is joined to the independently captured
    // managed player before any inventory callback may alter its result.
    void CaptureNativePlayer(std::uintptr_t pawn, std::uintptr_t controller) noexcept;
    // One exact, expiring allowance for the explicitly requested removal. This is
    // shared across threads because the GM request may execute asynchronously.
    void PermitRemoval(int id, int count);
    // Displayed cycle is one-based, matching the game's stored value. Applied
    // once by a natural, verified live-player getter; expires after 30 seconds.
    features::Result SetCycle(int displayedCycle);
}
