#pragma once

#include <array>
#include <optional>
#include <span>

#include "core/VisibilityEvaluation.h"

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
        float fovOffsetDegrees{ 0.0F };
    };

    struct SceneAnchorSamples
    {
        Vec3 bodyCenter;
        Vec3 playerActorForward;
    };

    struct SceneControlState
    {
        bool movementEnabled{ false };
        bool paused{ false };
    };

    struct VisibilityTarget
    {
        std::size_t participantIndex{ 0 };
        std::uint32_t participantID{ 0 };
        core::VisibilityPoint point{ core::VisibilityPoint::kBodyCenter };
        std::optional<Vec3> position;
        // Omitted for ordinary visibility rays that start at the candidate camera center.
        std::optional<Vec3> rayOrigin;
    };

    struct SceneVisibilitySamples
    {
        static constexpr std::size_t kPointsPerParticipant = 1;

        std::span<const std::uint32_t> participantIDs;
        std::span<const VisibilityTarget> targets;
    };
}
