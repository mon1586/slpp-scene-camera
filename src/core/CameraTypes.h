#pragma once

#include <array>

namespace ssc::core
{
    struct Vec3
    {
        float x{ 0.0F };
        float y{ 0.0F };
        float z{ 0.0F };
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
}
