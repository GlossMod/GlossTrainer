#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace gloss::gameplay {
    struct CameraVector { double x{}, y{}, z{}; };
    static_assert(sizeof(CameraVector) == 24);

    inline bool CameraValueClose(double observed, double expected) {
        return std::isfinite(observed) && std::isfinite(expected) &&
            std::abs(observed - expected) <= 0.00001 * (std::max)(1.0, std::abs(expected));
    }
    inline bool CameraVectorValid(CameraVector value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
            std::abs(value.x) <= 100000 && std::abs(value.y) <= 100000 && std::abs(value.z) <= 100000;
    }
    inline bool CameraVectorClose(CameraVector observed, CameraVector expected) {
        return CameraValueClose(observed.x, expected.x) && CameraValueClose(observed.y, expected.y) &&
            CameraValueClose(observed.z, expected.z);
    }
    inline std::optional<CameraVector> CameraHeightTarget(CameraVector original, float offset) {
        if (!CameraVectorValid(original) || !std::isfinite(offset) || offset < -300 || offset > 300) return {};
        original.z += offset;
        return CameraVectorValid(original) ? std::optional(original) : std::nullopt;
    }
    inline bool CameraDistanceAllowed(float value, float minimum, float maximum) {
        return std::isfinite(value) && std::isfinite(minimum) && std::isfinite(maximum) &&
            minimum >= 0 && maximum >= minimum && maximum <= 100000 && value >= minimum && value <= maximum;
    }

    struct CameraIdentity {
        std::uint64_t world{}, pawn{}, camera{}, location{};
        std::uint32_t worldIndex{}, pawnIndex{}, cameraIndex{}, locationIndex{};
        bool operator==(const CameraIdentity&) const = default;
        bool Valid() const { return world && pawn && camera && location; }
    };
    struct CameraValues {
        float distance{}, actualDistance{}, additiveDistance{};
        CameraVector height{}, heightTarget{};
        float heightInterpSpeed{};
        bool Valid() const {
            return std::isfinite(distance) && distance >= 0 && distance <= 100000 &&
                std::isfinite(actualDistance) && actualDistance >= 0 && actualDistance <= 100000 &&
                std::isfinite(additiveDistance) && std::abs(additiveDistance) <= 100000 &&
                CameraVectorValid(height) && CameraVectorValid(heightTarget) &&
                std::isfinite(heightInterpSpeed) && heightInterpSpeed >= 0 && heightInterpSpeed <= 100000;
        }
    };
    struct CameraBaseline {
        CameraIdentity identity;
        CameraValues original;
        bool captured{};
        bool Capture(CameraIdentity next, const CameraValues& values) {
            if (!next.Valid() || !values.Valid()) return false;
            if (captured) return identity == next;
            identity = next; original = values; captured = true;
            return true;
        }
        bool CanRestore(CameraIdentity next) const { return captured && identity == next; }
    };
}
