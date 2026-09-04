#pragma once

#include "core/CameraTypes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ssc::core
{
    enum class VisibilityPoint
    {
        kFace,
        kChest,
        kWaist,
    };

    enum class VisibilityPointStatus
    {
        kVisible,
        kObstructed,
        kUnavailable,
        kStartsInsideCollision,
    };

    struct VisibilityPointResult
    {
        std::size_t participantIndex{ 0 };
        std::uint32_t participantID{ 0 };
        VisibilityPoint point{ VisibilityPoint::kFace };
        VisibilityPointStatus status{ VisibilityPointStatus::kUnavailable };
        Vec3 rayStart;
        Vec3 target;
        std::optional<Vec3> hitPosition;
        std::optional<Vec3> hitNormal;
        float hitFraction{ 1.0F };
        std::uint32_t hitObjectID{ 0 };
        std::string hitObject;
    };

    struct ParticipantVisibility
    {
        std::size_t participantIndex{ 0 };
        std::uint32_t participantID{ 0 };
        std::uint8_t visiblePointMask{ 0 };
        std::size_t visiblePointCount{ 0 };
        bool visible{ false };
    };

    enum class CandidateFailureReason
    {
        kNone,
        kPoseUnavailable,
        kParticipantNotVisible,
    };

    struct CameraCandidateVisibility
    {
        std::string presetID;
        std::optional<CameraPose> pose;
        std::vector<VisibilityPointResult> points;
        std::vector<ParticipantVisibility> participants;
        std::size_t visiblePointCount{ 0 };
        std::size_t availablePointCount{ 0 };
        std::size_t visibleParticipantCount{ 0 };
        CandidateFailureReason failureReason{ CandidateFailureReason::kNone };
        bool usable{ false };
    };

    struct VisibilityEvaluationSnapshot
    {
        std::vector<CameraCandidateVisibility> candidates;
        std::optional<std::string> selectedPresetID;
    };

    class VisibilityEvaluator
    {
    public:
        [[nodiscard]] CameraCandidateVisibility Evaluate(
            std::string a_presetID,
            std::optional<CameraPose> a_pose,
            std::span<const std::uint32_t> a_participantIDs,
            std::vector<VisibilityPointResult> a_points) const;
    };

    [[nodiscard]] std::string_view VisibilityPointName(VisibilityPoint a_point) noexcept;
    [[nodiscard]] std::string_view VisibilityPointStatusName(
        VisibilityPointStatus a_status) noexcept;
    [[nodiscard]] std::string_view CandidateFailureReasonName(
        CandidateFailureReason a_reason) noexcept;
}
