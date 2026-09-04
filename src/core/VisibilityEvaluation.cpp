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

    std::string_view VisibilityPointName(VisibilityPoint a_point) noexcept
    {
        switch (a_point) {
        case VisibilityPoint::kFace:
            return "face";
        case VisibilityPoint::kChest:
            return "chest";
        case VisibilityPoint::kWaist:
            return "waist";
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
        default:
            return "unknown";
        }
    }
}
