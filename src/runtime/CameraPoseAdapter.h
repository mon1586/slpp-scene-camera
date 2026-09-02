#pragma once

#include "core/CameraTypes.h"
#include "runtime/RuntimeTypes.h"

namespace ssc::runtime
{
    [[nodiscard]] CameraPose ToRuntimeCameraPose(const core::CameraPose& a_pose) noexcept;
}
