#pragma once
#include "transformation.hpp"
#include "extensions.hpp"
#include <algorithm>
#include <cmath>

namespace gloss::gameplay::transformation::adapter
{
    // Reflected UCharacterMovementComponent modes, not character-name hints.
    enum class Motion { Stationary, Ground, Volume };
    struct Movement
    {
        std::uint8_t mode{}, custom{}, defaultLand{}, defaultWater{};
        float walkSpeed{}, flySpeed{}, swimSpeed{}, gravity{};
    };
    struct Decision
    {
        bool allowed{};
        Motion motion{Motion::Stationary};
        std::string reason;
    };
    inline Decision Evaluate(const Movement &value, bool classDefault = false, bool temporarilyStationary = false)
    {
        for (const auto speed : {value.walkSpeed, value.flySpeed, value.swimSpeed})
            if (!std::isfinite(speed) || speed < 0.f || speed > 100000.f)
                return {false, {}, "角色移动速度配置无效"};
        if (!std::isfinite(value.gravity) || std::abs(value.gravity) > 100.f)
            return {false, {}, "角色重力配置无效"};
        if (value.mode > 6 || value.defaultLand > 6 || value.defaultWater > 6 || value.custom != 0)
            return {false, {}, "该形态使用自定义移动模式，缺少专用驱动"};
        auto mode = value.mode;
        if (mode == 0 && classDefault) mode = value.defaultLand;
        if (mode == 6) return {false, {}, "该形态使用自定义移动模式，缺少专用驱动"};
        if (mode == 0 && temporarilyStationary) return {true, Motion::Stationary, "技能或受控状态暂时接管移动"};
        if (mode == 1 || mode == 2 || mode == 3)
        {
            if (value.walkSpeed == 0.f && temporarilyStationary) return {true, Motion::Stationary, "技能或受控状态暂时停止移动"};
            return {value.walkSpeed > 0.f, Motion::Ground, value.walkSpeed > 0.f ? "地面代理适配" : "角色没有有效的地面移动速度"};
        }
        if (mode == 4 || mode == 5)
        {
            const auto speed = mode == 4 ? value.swimSpeed : value.flySpeed;
            if (speed == 0.f && temporarilyStationary) return {true, Motion::Stationary, "技能或受控状态暂时停止移动"};
            return {speed > 0.f, Motion::Volume, speed > 0.f ? (mode == 4 ? "游泳代理适配" : "飞行代理适配") : "角色没有有效的三维移动速度"};
        }
        return {false, {}, "角色移动模式未初始化"};
    }
    struct Steering
    {
        bool valid{};
        extensions::Vec3 direction{};
        float scale{};
    };
    inline Steering Steer(Motion motion, double yaw, double pitch, float forward, float right)
    {
        if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(forward) || !std::isfinite(right)) return {};
        if (motion == Motion::Stationary) return {true};
        constexpr double radians = .017453292519943295;
        yaw = std::remainder(yaw, 360.) * radians;
        pitch = motion == Motion::Volume ? std::remainder(pitch, 360.) * radians : 0.;
        const auto f = std::clamp(static_cast<double>(forward), -1., 1.);
        const auto r = std::clamp(static_cast<double>(right), -1., 1.);
        extensions::Vec3 direction{std::cos(yaw) * std::cos(pitch) * f - std::sin(yaw) * r,
            std::sin(yaw) * std::cos(pitch) * f + std::cos(yaw) * r, std::sin(pitch) * f};
        const auto magnitude = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (magnitude <= .001) return {true};
        direction.x /= magnitude; direction.y /= magnitude; direction.z /= magnitude;
        return {true, direction, static_cast<float>((std::min)(magnitude, 1.))};
    }
    struct LocomotionIntent
    {
        bool valid{}, moving{};
        extensions::Vec3 direction{}, destination{}, origin{};
        double refreshDistance{};
        std::uint8_t speedType{}, moveAIType{}, motionMatchingState{};
        bool inputFacing{};
    };
    inline LocomotionIntent Locomotion(const Movement &movement, const Steering &steering, const extensions::Vec3 &origin, bool locked, bool sprint = false)
    {
        if (!steering.valid || !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return {};
        if (steering.scale <= .001f) return {true};
        const auto speed = movement.mode == 5 ? movement.flySpeed : movement.mode == 4 ? movement.swimSpeed : movement.walkSpeed;
        if (!std::isfinite(speed) || speed <= 0.f) return {};
        const auto distance = std::clamp(static_cast<double>(speed) * 2., 300., 4000.);
        // This is a destination for the character's own AI mover, never a
        // location write. Native JOG/RUN/SPRINT select this character's configured
        // gait/speed/animation; no player animation or numeric override is used.
        return {true, true, steering.direction,
            {origin.x + steering.direction.x * distance, origin.y + steering.direction.y * distance, origin.z + steering.direction.z * distance},
            origin, std::clamp(static_cast<double>(speed) * .35, 75., 500.),
            static_cast<std::uint8_t>(sprint ? 2 : steering.scale < .6f ? 0 : 1), static_cast<std::uint8_t>(locked ? 1 : 3)};
    }
    struct LocomotionRequests
    {
        int requestId{};
        std::uint64_t issuedAt{};
        LocomotionIntent previous;
        bool issued{};
        bool Due(const LocomotionIntent &intent, std::uint64_t now) const
        {
            if (!intent.valid || !intent.moving) return false;
            if (!issued || now < issuedAt) return true;
            const auto elapsed = now - issuedAt;
            const auto dot = previous.direction.x * intent.direction.x + previous.direction.y * intent.direction.y + previous.direction.z * intent.direction.z;
            const bool changed = dot < .98 || previous.speedType != intent.speedType || previous.moveAIType != intent.moveAIType ||
                previous.motionMatchingState != intent.motionMatchingState || previous.inputFacing != intent.inputFacing;
            if (!requestId) return elapsed >= 250 || (changed && elapsed >= 33);
            const auto dx = previous.destination.x - intent.origin.x, dy = previous.destination.y - intent.origin.y, dz = previous.destination.z - intent.origin.z;
            const bool nearDestination = dx * dx + dy * dy + dz * dz <= previous.refreshDistance * previous.refreshDistance;
            // Re-requesting the same active mover calls its OnEnd/Reset. Keep
            // straight motion continuous until the old goal is nearly reached;
            // only changed intent or a bounded stalled-request retry replans.
            return (elapsed >= 33 && (changed || nearDestination)) || elapsed >= 2000;
        }
        void Result(const LocomotionIntent &intent, std::uint64_t now, int id)
        {
            previous = intent;
            issuedAt = now;
            requestId = id > 0 ? id : 0;
            issued = true;
        }
        void Reset() { *this = {}; }
    };
}
