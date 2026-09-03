#include "core/CameraPose.h"

#include <cmath>
#include <limits>

namespace ssc::core
{
    namespace
    {
        constexpr double kDirectionEpsilon = 1.0e-6;

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
        const CameraOffset& a_offset) const noexcept
    {
        if (!IsFinite(a_anchor.position) || !IsFinite(a_anchor.forward) ||
            !std::isfinite(a_offset.right) || !std::isfinite(a_offset.forward) ||
            !std::isfinite(a_offset.up)) {
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
        const Vec3d cameraPositionDouble{
            static_cast<double>(a_anchor.position.x) +
                anchorRight.x * static_cast<double>(a_offset.right) +
                anchorForward->x * static_cast<double>(a_offset.forward),
            static_cast<double>(a_anchor.position.y) +
                anchorRight.y * static_cast<double>(a_offset.right) +
                anchorForward->y * static_cast<double>(a_offset.forward),
            static_cast<double>(a_anchor.position.z) + static_cast<double>(a_offset.up),
        };

        const auto positionX = ToFiniteFloat(cameraPositionDouble.x);
        const auto positionY = ToFiniteFloat(cameraPositionDouble.y);
        const auto positionZ = ToFiniteFloat(cameraPositionDouble.z);
        if (!positionX || !positionY || !positionZ) {
            return std::nullopt;
        }
        const Vec3 cameraPosition{ *positionX, *positionY, *positionZ };

        const auto viewForward = Normalize({
            static_cast<double>(a_anchor.position.x) - cameraPosition.x,
            static_cast<double>(a_anchor.position.y) - cameraPosition.y,
            static_cast<double>(a_anchor.position.z) - cameraPosition.z,
        });
        if (!viewForward) {
            return std::nullopt;
        }

        auto cameraRight = Normalize(Cross(*viewForward, worldUp));
        if (!cameraRight) {
            cameraRight = Normalize(anchorRight);
        }
        if (!cameraRight) {
            return std::nullopt;
        }

        const auto cameraUp = Normalize(Cross(*cameraRight, *viewForward));
        if (!cameraUp) {
            return std::nullopt;
        }

        const CameraBasis basis{
            ToVec3(*viewForward),
            ToVec3(*cameraUp),
            ToVec3(*cameraRight),
        };
        if (!IsFinite(basis.viewForward) || !IsFinite(basis.up) || !IsFinite(basis.right)) {
            return std::nullopt;
        }

        return CameraPose{ cameraPosition, basis };
    }

    std::optional<CameraOffset> CameraPoseCalculator::ExtractOffset(
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
        return CameraOffset{ *right, *forward, *up };
    }
}
