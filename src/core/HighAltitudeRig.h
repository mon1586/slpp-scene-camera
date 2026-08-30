#pragma once

#include <array>
#include <optional>
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
        std::optional<Vec3> fallbackCenter;
    };

    struct RotationMatrix
    {
        std::array<std::array<float, 3>, 3> entries{};
    };

    struct CameraPose
    {
        Vec3 position;
        RotationMatrix rotation;
    };

    class HighAltitudeRig
    {
    public:
        static constexpr float kAltitude = 3000.0F;

        [[nodiscard]] std::optional<CameraPose> Evaluate(
            const CameraFrameInput& a_input) const noexcept;
    };
}
