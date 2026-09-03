#include "core/CameraPose.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace ssc::core
{
    namespace
    {
        constexpr double kDirectionEpsilon = 1.0e-6;
        constexpr double kDegreesToRadians = std::numbers::pi_v<double> / 180.0;
        constexpr double kRadiansToDegrees = 180.0 / std::numbers::pi_v<double>;

        struct Vec3d
        {
            double x;
            double y;
            double z;
        };

        [[nodiscard]] bool IsFinite(const Vec3& a_value) noexcept
        {
            return std::isfinite(a_value.x) && std::isfinite(a_value.y) &&
                   std::isfinite(a_value.z);
        }

        [[nodiscard]] bool IsFinite(const Vec3d& a_value) noexcept
        {
            return std::isfinite(a_value.x) && std::isfinite(a_value.y) &&
                   std::isfinite(a_value.z);
        }

        [[nodiscard]] Vec3d Cross(const Vec3d& a_left, const Vec3d& a_right) noexcept
        {
            return {
                a_left.y * a_right.z - a_left.z * a_right.y,
                a_left.z * a_right.x - a_left.x * a_right.z,
                a_left.x * a_right.y - a_left.y * a_right.x,
            };
        }

        [[nodiscard]] std::optional<Vec3d> Normalize(const Vec3d& a_value) noexcept
        {
            if (!IsFinite(a_value)) {
                return std::nullopt;
            }

            const auto length = std::hypot(a_value.x, a_value.y, a_value.z);
            if (!std::isfinite(length) || length <= kDirectionEpsilon) {
                return std::nullopt;
            }

            return Vec3d{ a_value.x / length, a_value.y / length, a_value.z / length };
        }

        [[nodiscard]] std::optional<float> ToFiniteFloat(double a_value) noexcept
        {
            constexpr auto maximum = static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(a_value) || a_value < -maximum || a_value > maximum) {
                return std::nullopt;
            }

            const auto result = static_cast<float>(a_value);
            return std::isfinite(result) ? std::optional{ result } : std::nullopt;
        }

        [[nodiscard]] Vec3 ToVec3(const Vec3d& a_value) noexcept
        {
            return {
                static_cast<float>(a_value.x),
                static_cast<float>(a_value.y),
                static_cast<float>(a_value.z),
            };
        }
    }

    std::optional<CameraPose> CameraPoseCalculator::Evaluate(
        const SceneAnchor& a_anchor,
        const CameraRig& a_rig) const noexcept
    {
        const auto& framingOffset = a_rig.framingOffset;
        const auto& orbit = a_rig.orbit;
        if (!IsFinite(a_anchor.position) || !IsFinite(a_anchor.forward) ||
            !std::isfinite(framingOffset.right) || !std::isfinite(framingOffset.up) ||
            !std::isfinite(orbit.yawDegrees) ||
            !std::isfinite(orbit.pitchDegrees) || !std::isfinite(orbit.distance) ||
            orbit.distance <= kDirectionEpsilon) {
            return std::nullopt;
        }

        const auto anchorForward = Normalize({
            static_cast<double>(a_anchor.forward.x),
            static_cast<double>(a_anchor.forward.y),
            0.0,
        });
        if (!anchorForward) {
            return std::nullopt;
        }

        constexpr Vec3d worldUp{ 0.0, 0.0, 1.0 };
        const auto anchorRight = Cross(*anchorForward, worldUp);
        const auto yaw = static_cast<double>(orbit.yawDegrees) * kDegreesToRadians;
        const auto pitch = static_cast<double>(orbit.pitchDegrees) * kDegreesToRadians;
        const auto sineYaw = std::sin(yaw);
        const auto cosineYaw = std::cos(yaw);
        const auto sinePitch = std::sin(pitch);
        const auto cosinePitch = std::cos(pitch);
        const Vec3d viewForward{
            anchorRight.x * (-sineYaw * cosinePitch) +
                anchorForward->x * (cosineYaw * cosinePitch),
            anchorRight.y * (-sineYaw * cosinePitch) +
                anchorForward->y * (cosineYaw * cosinePitch),
            -sinePitch,
        };
        const Vec3d cameraRight{
            anchorRight.x * cosineYaw + anchorForward->x * sineYaw,
            anchorRight.y * cosineYaw + anchorForward->y * sineYaw,
            0.0,
        };
        const auto cameraUp = Cross(cameraRight, viewForward);
        const Vec3d framingCenter{
            static_cast<double>(a_anchor.position.x) +
                cameraRight.x * static_cast<double>(framingOffset.right) +
                cameraUp.x * static_cast<double>(framingOffset.up),
            static_cast<double>(a_anchor.position.y) +
                cameraRight.y * static_cast<double>(framingOffset.right) +
                cameraUp.y * static_cast<double>(framingOffset.up),
            static_cast<double>(a_anchor.position.z) +
                cameraUp.z * static_cast<double>(framingOffset.up),
        };
        const Vec3d cameraPositionDouble{
            framingCenter.x - viewForward.x * static_cast<double>(orbit.distance),
            framingCenter.y - viewForward.y * static_cast<double>(orbit.distance),
            framingCenter.z - viewForward.z * static_cast<double>(orbit.distance),
        };

        const auto positionX = ToFiniteFloat(cameraPositionDouble.x);
        const auto positionY = ToFiniteFloat(cameraPositionDouble.y);
        const auto positionZ = ToFiniteFloat(cameraPositionDouble.z);
        if (!positionX || !positionY || !positionZ) {
            return std::nullopt;
        }
        const Vec3 cameraPosition{ *positionX, *positionY, *positionZ };

        const auto normalizedViewForward = Normalize({
            framingCenter.x - cameraPosition.x,
            framingCenter.y - cameraPosition.y,
            framingCenter.z - cameraPosition.z,
        });
        if (!normalizedViewForward) {
            return std::nullopt;
        }

        auto normalizedCameraRight = Normalize(Cross(*normalizedViewForward, worldUp));
        if (!normalizedCameraRight) {
            normalizedCameraRight = Normalize(cameraRight);
        }
        if (!normalizedCameraRight) {
            return std::nullopt;
        }

        const auto normalizedCameraUp = Normalize(Cross(
            *normalizedCameraRight,
            *normalizedViewForward));
        if (!normalizedCameraUp) {
            return std::nullopt;
        }

        const CameraBasis basis{
            ToVec3(*normalizedViewForward),
            ToVec3(*normalizedCameraUp),
            ToVec3(*normalizedCameraRight),
        };
        if (!IsFinite(basis.viewForward) || !IsFinite(basis.up) || !IsFinite(basis.right)) {
            return std::nullopt;
        }

        return CameraPose{ cameraPosition, basis };
    }

    std::optional<CameraRig> CameraPoseCalculator::ExtractRig(
        const SceneAnchor& a_anchor,
        const Vec3& a_cameraPosition) const noexcept
    {
        if (!IsFinite(a_anchor.position) || !IsFinite(a_anchor.forward) ||
            !IsFinite(a_cameraPosition)) {
            return std::nullopt;
        }

        const auto anchorForward = Normalize({
            static_cast<double>(a_anchor.forward.x),
            static_cast<double>(a_anchor.forward.y),
            0.0,
        });
        if (!anchorForward) {
            return std::nullopt;
        }

        constexpr Vec3d worldUp{ 0.0, 0.0, 1.0 };
        const auto anchorRight = Cross(*anchorForward, worldUp);
        const Vec3d difference{
            static_cast<double>(a_cameraPosition.x) - a_anchor.position.x,
            static_cast<double>(a_cameraPosition.y) - a_anchor.position.y,
            static_cast<double>(a_cameraPosition.z) - a_anchor.position.z,
        };
        const auto right = ToFiniteFloat(
            difference.x * anchorRight.x + difference.y * anchorRight.y);
        const auto forward = ToFiniteFloat(
            difference.x * anchorForward->x + difference.y * anchorForward->y);
        const auto up = ToFiniteFloat(difference.z);
        if (!right || !forward || !up) {
            return std::nullopt;
        }
        const auto distance = std::hypot(
            static_cast<double>(*right),
            static_cast<double>(*forward),
            static_cast<double>(*up));
        if (!std::isfinite(distance) || distance <= kDirectionEpsilon) {
            return std::nullopt;
        }
        const auto yaw = std::atan2(
            static_cast<double>(*right),
            -static_cast<double>(*forward)) * kRadiansToDegrees;
        const auto pitch = std::asin(std::clamp(
            static_cast<double>(*up) / distance,
            -1.0,
            1.0)) * kRadiansToDegrees;
        const auto yawFloat = ToFiniteFloat(yaw);
        const auto pitchFloat = ToFiniteFloat(pitch);
        const auto distanceFloat = ToFiniteFloat(distance);
        if (!yawFloat || !pitchFloat || !distanceFloat) {
            return std::nullopt;
        }
        return CameraRig{
            {},
            { *yawFloat, *pitchFloat, *distanceFloat },
        };
    }
}
