#pragma once

#include "core/SceneAnchor.h"

#include <optional>

namespace ssc::core
{
    struct CameraOffset
    {
        float right{ 0.0F };
        float forward{ 0.0F };
        float up{ 0.0F };
    };

    class CameraPoseCalculator
    {
    public:
        [[nodiscard]] std::optional<CameraPose> Evaluate(
            const SceneAnchor& a_anchor,
            const CameraOffset& a_offset) const noexcept;
        [[nodiscard]] std::optional<CameraOffset> ExtractOffset(
            const SceneAnchor& a_anchor,
            const Vec3& a_cameraPosition) const noexcept;
    };
}
