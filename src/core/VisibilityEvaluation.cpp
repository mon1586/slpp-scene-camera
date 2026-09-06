#include "core/VisibilityEvaluation.h"

namespace ssc::core
{
    namespace
    {
        [[nodiscard]] constexpr std::uint8_t PointBit(VisibilityPoint a_point) noexcept
        {
            return static_cast<std::uint8_t>(1U << std::to_underlying(a_point));
        }
    }

    std::array<Vec3, 5> AnchorLOSRayOrigins(const CameraPose& a_camera) noexcept
    {
        const auto& a_center = a_camera.position;
        const auto& a_basis = a_camera.basis;
        const auto corner = [&](float a_right, float a_up) {
            return Vec3{
                a_center.x + a_basis.right.x * a_right + a_basis.up.x * a_up,
                a_center.y + a_basis.right.y * a_right + a_basis.up.y * a_up,
                a_center.z + a_basis.right.z * a_right + a_basis.up.z * a_up,
            };
        };
        constexpr auto right = kAnchorLOSWidth * 0.5F;
        constexpr auto up = kAnchorLOSHeight * 0.5F;
        return { a_center, corner(-right, up), corner(right, up),
            corner(-right, -up), corner(right, -up) };
    }

    CameraCandidateVisibility VisibilityEvaluator::Evaluate(
        std::string a_presetID,
        std::optional<CameraPose> a_pose,
        std::span<const std::uint32_t> a_participantIDs,
        std::vector<VisibilityPointResult> a_points) const
    {
        CameraCandidateVisibility result;
        result.presetID = std::move(a_presetID);
        result.pose = std::move(a_pose);
        result.points = std::move(a_points);
        result.participants.reserve(a_participantIDs.size());

        for (std::size_t index = 0; index < a_participantIDs.size(); ++index) {
            result.participants.push_back({ index, a_participantIDs[index] });
        }

        for (const auto& point : result.points) {
            if (point.participantIndex >= result.participants.size()) {
                continue;
            }

            if (point.status != VisibilityPointStatus::kUnavailable) {
                ++result.availablePointCount;
            }
            if (point.status != VisibilityPointStatus::kVisible) {
                continue;
            }

            ++result.visiblePointCount;
            auto& participant = result.participants[point.participantIndex];
            const auto bit = PointBit(point.point);
            if ((participant.visiblePointMask & bit) == 0) {
                participant.visiblePointMask |= bit;
                ++participant.visiblePointCount;
            }
        }

        for (auto& participant : result.participants) {
            participant.visible = participant.visiblePointCount != 0;
            if (participant.visible) {
                ++result.visibleParticipantCount;
            }
        }

        if (!result.pose) {
            result.failureReason = CandidateFailureReason::kPoseUnavailable;
            return result;
        }
        if (result.participants.empty() ||
            result.visibleParticipantCount != result.participants.size()) {
            result.failureReason = CandidateFailureReason::kParticipantNotVisible;
            return result;
        }

        result.failureReason = CandidateFailureReason::kNone;
        result.usable = true;
        return result;
    }

    void ApplyAnchorLOSRule(CameraCandidateVisibility& a_candidate) noexcept
    {
        a_candidate.usable = false;
        if (!a_candidate.pose) {
            a_candidate.failureReason = CandidateFailureReason::kPoseUnavailable;
            return;
        }
        std::array<VisibilityPointStatus, 5> statuses{};
        for (std::size_t index = 0; index < kAnchorLOSPoints.size(); ++index) {
            std::size_t matches = 0;
            for (const auto& point : a_candidate.points) {
                if (point.point == kAnchorLOSPoints[index]) {
                    statuses[index] = point.status;
                    ++matches;
                }
            }
            if (matches != 1 || statuses[index] == VisibilityPointStatus::kUnavailable) {
                a_candidate.failureReason = CandidateFailureReason::kAnchorUnavailable;
                return;
            }
        }
        if (statuses[0] != VisibilityPointStatus::kVisible) {
            a_candidate.failureReason = CandidateFailureReason::kAnchorCenterObstructed;
            return;
        }
        std::size_t visibleCorners = 0;
        for (std::size_t index = 1; index < statuses.size(); ++index) {
            visibleCorners += statuses[index] == VisibilityPointStatus::kVisible ? 1 : 0;
        }
        a_candidate.usable = visibleCorners >= 3;
        a_candidate.failureReason = a_candidate.usable ? CandidateFailureReason::kNone :
            CandidateFailureReason::kAnchorCornersObstructed;
    }

    std::string_view VisibilityPointName(VisibilityPoint a_point) noexcept
    {
        switch (a_point) {
        case VisibilityPoint::kBodyCenter:
            return "body center";
        case VisibilityPoint::kAnchor:
            return "center";
        case VisibilityPoint::kAnchorTopLeft:
            return "top left";
        case VisibilityPoint::kAnchorTopRight:
            return "top right";
        case VisibilityPoint::kAnchorBottomLeft:
            return "bottom left";
        case VisibilityPoint::kAnchorBottomRight:
            return "bottom right";
        default:
            return "unknown";
        }
    }

    std::string_view VisibilityPointStatusName(VisibilityPointStatus a_status) noexcept
    {
        switch (a_status) {
        case VisibilityPointStatus::kVisible:
            return "visible";
        case VisibilityPointStatus::kObstructed:
            return "obstructed";
        case VisibilityPointStatus::kUnavailable:
            return "unavailable";
        case VisibilityPointStatus::kStartsInsideCollision:
            return "starts inside collision";
        default:
            return "unknown";
        }
    }

    std::string_view CandidateFailureReasonName(CandidateFailureReason a_reason) noexcept
    {
        switch (a_reason) {
        case CandidateFailureReason::kNone:
            return "usable";
        case CandidateFailureReason::kPoseUnavailable:
            return "camera pose unavailable";
        case CandidateFailureReason::kParticipantNotVisible:
            return "one or more participants are not visible";
        case CandidateFailureReason::kAnchorUnavailable:
            return "anchor LOS unavailable";
        case CandidateFailureReason::kAnchorCenterObstructed:
            return "center obstructed";
        case CandidateFailureReason::kAnchorCornersObstructed:
            return "two or more corners obstructed";
        default:
            return "unknown";
        }
    }
}
