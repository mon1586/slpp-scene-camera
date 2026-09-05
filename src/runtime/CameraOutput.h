#pragma once

#include "runtime/ICameraControl.h"

namespace ssc::runtime
{
    class CameraOutput
    {
    public:
        [[nodiscard]] CameraApplyResult Apply(const CameraPose& a_pose);
        void ResetFOVOffset();

    private:
        [[nodiscard]] static RE::NiCamera* FindNiCamera(RE::NiAVObject* a_object) noexcept;
        [[nodiscard]] static bool ApplyFOVOffset(float a_offsetDegrees);
    };
}
