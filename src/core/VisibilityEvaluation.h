#pragma once

#include "core/CameraTypes.h"

#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ssc::core
{
    enum class VisibilityPoint
    {
        kBodyCenter,
        kAnchor,
        kAnchorTopLeft,
        kAnchorTopRight,
        kAnchorBottomLeft,
        kAnchorBottomRight,
    };

    inline constexpr float kAnchorLOSWidth = 32.0F;
    inline constexpr float kAnchorLOSHeight = 18.0F;
    inline constexpr std::array kAnchorLOSPoints{
        VisibilityPoint::kAnchor, VisibilityPoint::kAnchorTopLeft,
        VisibilityPoint::kAnchorTopRight, VisibilityPoint::kAnchorBottomLeft,
        VisibilityPoint::kAnchorBottomRight,
    };
    [[nodiscard]] std::array<Vec3, 5> AnchorLOSRayOrigins(const CameraPose& a_camera) noexcept;

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
        VisibilityPoint point{ VisibilityPoint::kBodyCenter };
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
        kAnchorUnavailable,
        kAnchorCenterObstructed,
        kAnchorCornersObstructed,
        kAnimationFilter,
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

    struct AnchorLOSMetrics
    {
        float intervalSeconds{ 0.5F };
        std::uint64_t sampleCount{ 0 };
        double lastMilliseconds{ 0.0 };
        double averageMilliseconds{ 0.0 };
        double maximumMilliseconds{ 0.0 };
        double traceMilliseconds{ 0.0 };
        std::size_t rayQueryCount{ 0 };
    };

    void ApplyAnchorLOSRule(CameraCandidateVisibility& a_candidate) noexcept;

    struct VisibilityEvaluationSnapshot
    {
        std::vector<CameraCandidateVisibility> candidates;
        std::optional<std::string> selectedPresetID;
        std::optional<AnchorLOSMetrics> anchorLOS;
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
