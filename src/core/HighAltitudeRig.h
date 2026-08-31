#pragma once

#include "core/CameraTypes.h"

#include <optional>
#include <span>

namespace ssc::core
{
    struct CameraFrameInput
    {
        std::span<const Vec3> subjects;
        std::optional<Vec3> fallbackCenter;
    };

    class HighAltitudeRig
    {
    public:
        static constexpr float kAltitude = 3000.0F;

        [[nodiscard]] std::optional<CameraPose> Evaluate(
            const CameraFrameInput& a_input) const noexcept;
    };
}
