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
    };
}
