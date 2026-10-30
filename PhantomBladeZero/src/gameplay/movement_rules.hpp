#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gloss::gameplay {
    struct MovementIdentity {
        std::uint64_t world{}, pawn{}, component{};
        std::uint32_t worldIndex{}, pawnIndex{}, componentIndex{};
        bool operator==(const MovementIdentity&) const = default;
        bool Valid() const { return world && pawn && component; }
    };

    enum class MovementCleanupDecision { RestoreCurrent, RestorePrevious, DiscardExpired, PreserveUnavailable };
    inline MovementCleanupDecision DecideMovementCleanup(bool matchesCurrent, bool previousAlive, bool previousAccessible) {
        if (matchesCurrent) return MovementCleanupDecision::RestoreCurrent;
        if (!previousAlive) return MovementCleanupDecision::DiscardExpired;
        return previousAccessible ? MovementCleanupDecision::RestorePrevious : MovementCleanupDecision::PreserveUnavailable;
    }

    enum class MovementMaintenanceDecision { None, Rewrite, Stop };
    inline MovementMaintenanceDecision DecideMovementMaintenance(bool enabled, bool uncertain, bool identityCurrent,
        float observed, float target) {
        if (!enabled || uncertain) return MovementMaintenanceDecision::None;
        if (!identityCurrent || !std::isfinite(observed) || !std::isfinite(target)) return MovementMaintenanceDecision::Stop;
        return std::abs(observed - target) <= 0.00001f * (std::max)(1.0f, std::abs(target)) ?
            MovementMaintenanceDecision::None : MovementMaintenanceDecision::Rewrite;
    }

    enum class MovementValueKind { Multiplier, JumpHeight, Absolute };

    inline bool MovementValueClose(float observed, float expected) {
        return std::isfinite(observed) && std::isfinite(expected) &&
            std::abs(observed - expected) <= 0.00001f * (std::max)(1.0f, std::abs(expected));
    }

    inline std::optional<float> MovementTarget(float baseline, float value, MovementValueKind kind,
        float minimum, float maximum) {
        if (!std::isfinite(baseline) || !std::isfinite(value) || baseline < 0 ||
            value < minimum || value > maximum) return {};
        const double target = kind == MovementValueKind::Absolute ? value :
            static_cast<double>(baseline) * (kind == MovementValueKind::JumpHeight ? std::sqrt(value) : value);
        if (!std::isfinite(target) || target < 0 || target > 100000000) return {};
        return static_cast<float>(target);
    }

    // A baseline is captured once per object identity, before any writes. Editing
    // the multiplier uses that baseline instead of multiplying an earlier edit.
    struct MovementBaseline {
        MovementIdentity identity;
        std::array<float, 5> original{};
        std::size_t count{};
        bool captured{};

        bool Capture(MovementIdentity next, const float* values, std::size_t size) {
            if (!next.Valid() || !values || !size || size > original.size()) return false;
            if (captured) return identity == next && count == size;
            for (std::size_t i = 0; i < size; ++i)
                if (!std::isfinite(values[i]) || values[i] < 0 || values[i] > 100000000) return false;
            identity = next;
            count = size;
            for (std::size_t i = 0; i < size; ++i) original[i] = values[i];
            captured = true;
            return true;
        }
        bool CanRestore(MovementIdentity current) const { return captured && identity == current; }
        void Clear() { *this = {}; }
    };

    // Returning from flight requires a verified free capsule position. A ground
    // mode must not be forced while the pawn overlaps world geometry.
    inline bool SafeFlightExit(bool identityCurrent, bool collisionContract, bool freeCapsule) {
        return identityCurrent && collisionContract && freeCapsule;
    }

    struct AiMovementOwnership {
        std::uint64_t pawn{}, controller{}, controllerPawn{}, pawnController{};
        bool aiController{}, localPawn{}, objectsValid{};
    };
    inline bool IsOwnedAiPawn(const AiMovementOwnership& ownership) {
        return ownership.objectsValid && ownership.aiController && !ownership.localPawn &&
            ownership.pawn && ownership.controller && ownership.controllerPawn == ownership.pawn &&
            ownership.pawnController == ownership.controller;
    }
}
