#pragma once

#include "core/CameraTypes.h"

#include <optional>

namespace ssc::core
{
    [[nodiscard]] Vec3 SmoothAnchorPosition(
        const Vec3& a_previous, const Vec3& a_target, float a_deltaSeconds) noexcept;

    struct SceneAnchorInput
    {
        Vec3 bodyCenter;
        Vec3 playerActorForward;
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
