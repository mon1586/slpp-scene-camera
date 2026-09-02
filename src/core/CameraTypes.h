#pragma once

namespace ssc::core
{
    struct Vec3
    {
        float x{ 0.0F };
        float y{ 0.0F };
        float z{ 0.0F };
    };

    struct CameraBasis
    {
        Vec3 viewForward;
        Vec3 up;
        Vec3 right;
    };

    struct CameraPose
    {
        Vec3 position;
        CameraBasis basis;
    };
}
