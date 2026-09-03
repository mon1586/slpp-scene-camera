#pragma once

#include "core/SceneAnchor.h"

#include <optional>

namespace ssc::core
{
    struct CameraFramingOffset
    {
        float right{ 0.0F };
        float up{ 0.0F };
    };

    struct CameraOrbit
    {
        float yawDegrees{ 0.0F };
        float pitchDegrees{ 0.0F };
        float distance{ 0.0F };
    };

    struct CameraRig
    {
        CameraFramingOffset framingOffset;
        CameraOrbit orbit;
    };

    class CameraPoseCalculator
    {
    public:
        [[nodiscard]] std::optional<CameraPose> Evaluate(
            const SceneAnchor& a_anchor,
            const CameraRig& a_rig) const noexcept;
        [[nodiscard]] std::optional<CameraRig> ExtractRig(
            const SceneAnchor& a_anchor,
            const Vec3& a_cameraPosition) const noexcept;
    };
}
