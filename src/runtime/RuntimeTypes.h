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
    };

    struct SceneAnchorSamples
    {
        std::span<const Vec3> participantPelvisPositions;
        std::optional<Vec3> playerPelvisForward;
        std::optional<Vec3> playerActorForward;
    };

    struct VisibilityTarget
    {
        std::size_t participantIndex{ 0 };
        std::uint32_t participantID{ 0 };
        core::VisibilityPoint point{ core::VisibilityPoint::kFace };
        std::optional<Vec3> position;
    };

    struct SceneVisibilitySamples
    {
        static constexpr std::size_t kPointsPerParticipant = 3;

        std::span<const std::uint32_t> participantIDs;
        std::span<const VisibilityTarget> targets;
    };
}
