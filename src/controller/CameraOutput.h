#pragma once

#include "core/HighAltitudeRig.h"

namespace ssc::controller
{
    enum class CameraApplyResult
    {
        kApplied,
        kUnsupportedState,
        kMissingCamera,
    };

    class CameraOutput
    {
    public:
        [[nodiscard]] CameraApplyResult Apply(
            RE::PlayerCamera* a_camera,
            const core::CameraPose& a_pose);

    private:
        [[nodiscard]] static RE::NiCamera* FindNiCamera(RE::NiAVObject* a_object) noexcept;

    };
}
