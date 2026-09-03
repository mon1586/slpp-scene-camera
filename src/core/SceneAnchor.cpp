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
        if (a_input.participantPelvisPositions.empty()) {
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

        std::optional<Vec3> playerForward;
        if (a_input.playerPelvisForward && IsFinite(*a_input.playerPelvisForward)) {
            playerForward = NormalizeHorizontal(*a_input.playerPelvisForward);
        }
        if (!playerForward && a_input.playerActorForward &&
            IsFinite(*a_input.playerActorForward)) {
            playerForward = NormalizeHorizontal(*a_input.playerActorForward);
        }
        if (!playerForward) {
            return std::nullopt;
        }

        return SceneAnchor{
            position,
            { -playerForward->x, -playerForward->y, 0.0F },
        };
    }
}
