#pragma once

#include "core/CameraTypes.h"
#include "runtime/RuntimeTypes.h"

namespace ssc::runtime
{
    [[nodiscard]] CameraPose ToRuntimeCameraPose(
        const core::CameraPose& a_pose,
        float a_fovOffsetDegrees = 0.0F) noexcept;
}
