#include "core/VisibilityEvaluation.h"
#include "core/CandidateSelection.h"

#include <array>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    bool Check(bool a_condition, std::string_view a_message)
    {
        if (!a_condition) {
            std::cerr << "FAILED: " << a_message << '\n';
        }
        return a_condition;
    }

    ssc::core::VisibilityPointResult Point(
        std::size_t a_participant,
        std::uint32_t a_id,
        ssc::core::VisibilityPoint a_point,
        ssc::core::VisibilityPointStatus a_status)
    {
        return {
            a_participant,
            a_id,
            a_point,
            a_status,
            { 10.0F, 20.0F, 30.0F },
            { 40.0F, 50.0F, 60.0F },
            a_status == ssc::core::VisibilityPointStatus::kObstructed ?
                std::optional{ ssc::core::Vec3{ 20.0F, 30.0F, 40.0F } } : std::nullopt,
            a_status == ssc::core::VisibilityPointStatus::kObstructed ?
                std::optional{ ssc::core::Vec3{ 0.0F, 1.0F, 0.0F } } : std::nullopt,
            0.5F,
            0x1234,
            "fixture",
        };
    }
}

int main()
{
    using namespace ssc::core;

    VisibilityEvaluator evaluator;
    const std::array participantIDs{ std::uint32_t{ 0x14 }, std::uint32_t{ 0x100 } };
    const CameraPose pose{
        { 10.0F, 20.0F, 30.0F },
        {
            { 0.0F, 1.0F, 0.0F },
            { 0.0F, 0.0F, 1.0F },
            { 1.0F, 0.0F, 0.0F },
        },
    };

    std::vector<VisibilityPointResult> usablePoints{
        Point(0, participantIDs[0], VisibilityPoint::kFace, VisibilityPointStatus::kVisible),
        Point(0, participantIDs[0], VisibilityPoint::kChest, VisibilityPointStatus::kObstructed),
        Point(0, participantIDs[0], VisibilityPoint::kWaist, VisibilityPointStatus::kUnavailable),
        Point(1, participantIDs[1], VisibilityPoint::kFace, VisibilityPointStatus::kObstructed),
        Point(1, participantIDs[1], VisibilityPoint::kChest, VisibilityPointStatus::kVisible),
        Point(1, participantIDs[1], VisibilityPoint::kWaist, VisibilityPointStatus::kVisible),
    };

    const auto usable = evaluator.Evaluate("usable", pose, participantIDs, usablePoints);
    bool passed = true;
    passed &= Check(usable.usable, "one visible point makes each participant visible");
    passed &= Check(usable.failureReason == CandidateFailureReason::kNone,
        "usable candidate has no failure reason");
    passed &= Check(usable.visibleParticipantCount == 2,
        "quality reports every visible participant");
    passed &= Check(usable.visiblePointCount == 3,
        "quality counts every visible evaluation point");
    passed &= Check(usable.availablePointCount == 5,
        "unavailable nodes are excluded from available point quality");
    passed &= Check(usable.participants[0].visiblePointMask == 0x01,
        "face visibility is retained in the quality mask");
    passed &= Check(usable.participants[1].visiblePointMask == 0x06,
        "chest and waist visibility are retained in the quality mask");
    passed &= Check(usable.points[1].hitPosition.has_value() &&
        usable.points[1].hitNormal.has_value() && usable.points[1].hitObject == "fixture",
        "stored ray result retains hit diagnostics for drawing and logging");

    auto rejectedPoints = usablePoints;
    for (auto& point : rejectedPoints) {
        if (point.participantIndex == 1) {
            point.status = VisibilityPointStatus::kObstructed;
        }
    }
    const auto rejected = evaluator.Evaluate("rejected", pose, participantIDs, rejectedPoints);
    passed &= Check(!rejected.usable,
        "a candidate is rejected when any participant has no visible point");
    passed &= Check(rejected.visibleParticipantCount == 1,
        "rejected candidate reports the participant visibility count");
    passed &= Check(rejected.failureReason == CandidateFailureReason::kParticipantNotVisible,
        "rejected candidate explains the participant visibility failure");

    auto insidePoints = usablePoints;
    insidePoints[0].status = VisibilityPointStatus::kStartsInsideCollision;
    const auto startsInside = evaluator.Evaluate("inside", pose, participantIDs, insidePoints);
    passed &= Check(!startsInside.participants[0].visible,
        "a ray that starts inside collision is not treated as visible");
    passed &= Check(startsInside.availablePointCount == 5,
        "a start-inside result is a completed query, not an unavailable point");

    const auto missingPose = evaluator.Evaluate("missing-pose", std::nullopt, participantIDs, {});
    passed &= Check(!missingPose.usable &&
        missingPose.failureReason == CandidateFailureReason::kPoseUnavailable,
        "a candidate without a world pose is rejected with its own reason");

    const std::array<std::uint32_t, 0> noParticipants{};
    const auto empty = evaluator.Evaluate("empty", pose, noParticipants, {});
    passed &= Check(!empty.usable,
        "a candidate cannot become usable without scene participants");

    CandidateSelector selector;
    std::array<CameraCandidateVisibility, 4> candidates{};
    candidates[0].presetID = "blocked";
    candidates[1].presetID = "good-first";
    candidates[1].usable = true;
    candidates[2].presetID = "lower-preferred";
    candidates[2].usable = true;
    candidates[3].presetID = "good-tie";
    candidates[3].usable = true;
    passed &= Check(
        selector.Step(candidates, "good-first", 1) ==
            std::optional<std::string>{ "lower-preferred" },
        "forward selection advances through usable candidates");
    passed &= Check(
        selector.Step(candidates, "good-first", -1) ==
            std::optional<std::string>{ "good-tie" },
        "backward selection wraps and skips unusable candidates");

    return passed ? 0 : 1;
}
