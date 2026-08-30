#pragma once

#include "core/HighAltitudeRig.h"

namespace ssc::controller
{
    class CameraOutput
    {
    public:
        void Apply(RE::PlayerCamera* a_camera, const core::CameraPose& a_pose) noexcept;

    private:
        [[nodiscard]] static RE::NiCamera* FindNiCamera(RE::NiAVObject* a_object) noexcept;

        bool loggedMissingCamera_{ false };
    };
}

