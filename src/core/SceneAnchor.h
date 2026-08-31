#pragma once

#include "core/CameraTypes.h"

#include <optional>
#include <span>

namespace ssc::core
{
    struct SceneAnchorInput
    {
        std::span<const Vec3> participantPelvisPositions;
        std::optional<Vec3> playerPelvisPosition;
        std::optional<Vec3> playerForward;
    };

    struct SceneAnchor
    {
        Vec3 position;
        Vec3 forward;
    };

    class SceneAnchorCalculator
    {
    public:
        [[nodiscard]] std::optional<SceneAnchor> Evaluate(
            const SceneAnchorInput& a_input) const noexcept;
    };
}
