#include "core/SceneAnchor.h"

#include <cmath>

namespace ssc::core
{
    namespace
    {
        constexpr float kDirectionEpsilonSquared = 0.0001F;

        [[nodiscard]] bool IsFinite(const Vec3& a_value) noexcept
        {
            return std::isfinite(a_value.x) && std::isfinite(a_value.y) && std::isfinite(a_value.z);
        }

        [[nodiscard]] std::optional<Vec3> NormalizeHorizontal(Vec3 a_value) noexcept
        {
            a_value.z = 0.0F;
            const auto lengthSquared = a_value.x * a_value.x + a_value.y * a_value.y;
            if (!std::isfinite(lengthSquared) || lengthSquared <= kDirectionEpsilonSquared) {
                return std::nullopt;
            }

            const auto inverseLength = 1.0F / std::sqrt(lengthSquared);
            return Vec3{ a_value.x * inverseLength, a_value.y * inverseLength, 0.0F };
        }
    }

    Vec3 SmoothAnchorPosition(
        const Vec3& a_previous, const Vec3& a_target, float a_deltaSeconds) noexcept
    {
        if (!IsFinite(a_previous) || !IsFinite(a_target) ||
            !std::isfinite(a_deltaSeconds) || a_deltaSeconds <= 0.0F) {
            return a_previous;
        }
        constexpr float kPositionHalfLifeSeconds = 0.15F;
        const auto weight = -std::expm1(
            -std::log(2.0F) * a_deltaSeconds / kPositionHalfLifeSeconds);
        return {
            std::lerp(a_previous.x, a_target.x, weight),
            std::lerp(a_previous.y, a_target.y, weight),
            std::lerp(a_previous.z, a_target.z, weight),
        };
    }

    std::optional<SceneAnchor> SceneAnchorCalculator::Evaluate(
        const SceneAnchorInput& a_input) const noexcept
    {
        if (!IsFinite(a_input.bodyCenter) || !IsFinite(a_input.playerActorForward)) {
            return std::nullopt;
        }

        const auto playerForward = NormalizeHorizontal(a_input.playerActorForward);
        if (!playerForward) {
            return std::nullopt;
        }

        return SceneAnchor{
            a_input.bodyCenter,
            { -playerForward->x, -playerForward->y, 0.0F },
        };
    }
}
