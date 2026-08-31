#pragma once

#include <array>
#include <optional>
#include <span>

namespace ssc::runtime
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

    struct SceneAnchorSamples
    {
        std::span<const Vec3> participantPelvisPositions;
        std::optional<Vec3> playerPelvisPosition;
        std::optional<Vec3> playerForward;
    };
}
