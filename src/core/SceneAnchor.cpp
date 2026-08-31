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

    std::optional<SceneAnchor> SceneAnchorCalculator::Evaluate(
        const SceneAnchorInput& a_input) const noexcept
    {
        if (a_input.participantPelvisPositions.empty() ||
            !a_input.playerPelvisPosition ||
            !IsFinite(*a_input.playerPelvisPosition)) {
            return std::nullopt;
        }

        double sumX = 0.0;
        double sumY = 0.0;
        double sumZ = 0.0;
        for (const auto& pelvis : a_input.participantPelvisPositions) {
            if (!IsFinite(pelvis)) {
                return std::nullopt;
            }
            sumX += static_cast<double>(pelvis.x);
            sumY += static_cast<double>(pelvis.y);
            sumZ += static_cast<double>(pelvis.z);
        }

        const auto inverseParticipantCount =
            1.0 / static_cast<double>(a_input.participantPelvisPositions.size());
        const Vec3 position{
            static_cast<float>(sumX * inverseParticipantCount),
            static_cast<float>(sumY * inverseParticipantCount),
            static_cast<float>(sumZ * inverseParticipantCount),
        };
        if (!IsFinite(position)) {
            return std::nullopt;
        }

        if (a_input.participantPelvisPositions.size() > 1) {
            if (const auto forward = NormalizeHorizontal({
                    a_input.playerPelvisPosition->x - position.x,
                    a_input.playerPelvisPosition->y - position.y,
                    0.0F });
                forward) {
                return SceneAnchor{ position, *forward };
            }
        }

        if (!a_input.playerForward || !IsFinite(*a_input.playerForward)) {
            return std::nullopt;
        }
        const auto fallbackForward = NormalizeHorizontal({
            -a_input.playerForward->x,
            -a_input.playerForward->y,
            0.0F });
        if (!fallbackForward) {
            return std::nullopt;
        }

        return SceneAnchor{ position, *fallbackForward };
    }
}
