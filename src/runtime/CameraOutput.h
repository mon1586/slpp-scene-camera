#pragma once

#include "runtime/ICameraControl.h"

namespace ssc::runtime
{
    class CameraOutput
    {
    public:
        [[nodiscard]] CameraApplyResult Apply(const CameraPose& a_pose);

    private:
        [[nodiscard]] static RE::NiCamera* FindNiCamera(RE::NiAVObject* a_object) noexcept;

    };
}
