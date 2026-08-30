#pragma once

#include <span>

namespace ssc::core
{
    struct Vec3
    {
        float x{ 0.0F };
        float y{ 0.0F };
        float z{ 0.0F };
    };

    struct CameraFrameInput
    {
        std::span<const Vec3> subjects;
        Vec3 fallbackCenter;
    };

    struct CameraPose
    {
        Vec3 position;
        Vec3 target;
    };

    class HighAltitudeRig
    {
    public:
        static constexpr float kAltitude = 3000.0F;

        [[nodiscard]] CameraPose Evaluate(const CameraFrameInput& a_input) const noexcept;
    };
}

