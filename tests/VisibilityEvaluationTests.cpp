#include "core/VisibilityEvaluation.h"
#include "core/CandidateSelection.h"

#include <array>
#include <cmath>
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
        Point(0, participantIDs[0], VisibilityPoint::kBodyCenter, VisibilityPointStatus::kVisible),
        Point(1, participantIDs[1], VisibilityPoint::kBodyCenter, VisibilityPointStatus::kVisible),
    };

    const auto usable = evaluator.Evaluate("usable", pose, participantIDs, usablePoints);
    bool passed = true;
    const Vec3 rectangleCenter{ 10.0F, 20.0F, 30.0F };
    const auto rectangle = AnchorLOSRayOrigins(pose);
    passed &= Check(rectangle[0].x == 10.0F && rectangle[0].y == 20.0F &&
        rectangle[0].z == 30.0F && rectangle[1].x == -6.0F &&
        rectangle[1].z == 39.0F && rectangle[4].x == 26.0F && rectangle[4].z == 21.0F,
        "LOS ray origins are centered on the camera with fixed-size screen-aligned corners");
    const CameraBasis rotatedBasis{
        { 0.8F, 0.0F, 0.6F }, { -0.6F, 0.0F, 0.8F }, { 0.0F, 1.0F, 0.0F } };
    const auto rotatedRectangle = AnchorLOSRayOrigins({ rectangleCenter, rotatedBasis });
    const auto distance = [](const Vec3& a, const Vec3& b) {
        return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
    };
    passed &= Check(std::abs(distance(rotatedRectangle[1], rotatedRectangle[2]) - 32.0F) < 0.001F &&
        std::abs(distance(rotatedRectangle[1], rotatedRectangle[3]) - 18.0F) < 0.001F,
        "yaw and pitch rotate the rectangle without changing its 16:9 dimensions");
    passed &= Check(std::abs(rotatedRectangle[1].x - 4.6F) < 0.001F &&
        std::abs(rotatedRectangle[1].y - 4.0F) < 0.001F &&
        std::abs(rotatedRectangle[1].z - 37.2F) < 0.001F,
        "top-left follows camera up and left even for a pitched camera");
    passed &= Check(usable.usable, "one visible point makes each participant visible");
    passed &= Check(usable.failureReason == CandidateFailureReason::kNone,
        "usable candidate has no failure reason");
    passed &= Check(usable.visibleParticipantCount == 2,
        "quality reports every visible participant");
    passed &= Check(usable.visiblePointCount == 2,
        "quality counts each visible body center");
    passed &= Check(usable.availablePointCount == 2,
        "each participant contributes one available body center");
    passed &= Check(usable.participants[0].visiblePointMask == 0x01,
        "body-center visibility is retained in the quality mask");
    passed &= Check(usable.participants[1].visiblePointMask == 0x01,
        "each participant uses the same body-center mask");
    auto rejectedPoints = usablePoints;
    rejectedPoints[1] = Point(
        1,
        participantIDs[1],
        VisibilityPoint::kBodyCenter,
        VisibilityPointStatus::kObstructed);
    const auto rejected = evaluator.Evaluate("rejected", pose, participantIDs, rejectedPoints);
    passed &= Check(!rejected.usable,
        "a candidate is rejected when any participant has no visible point");
    passed &= Check(rejected.visibleParticipantCount == 1,
        "rejected candidate reports the participant visibility count");
    passed &= Check(rejected.failureReason == CandidateFailureReason::kParticipantNotVisible,
        "rejected candidate explains the participant visibility failure");
    passed &= Check(rejected.points[1].hitPosition.has_value() &&
        rejected.points[1].hitNormal.has_value() && rejected.points[1].hitObject == "fixture",
        "stored body-center ray result retains hit diagnostics for drawing and logging");

    auto insidePoints = usablePoints;
    insidePoints[0].status = VisibilityPointStatus::kStartsInsideCollision;
    const auto startsInside = evaluator.Evaluate("inside", pose, participantIDs, insidePoints);
    passed &= Check(!startsInside.participants[0].visible,
        "a ray that starts inside collision is not treated as visible");
    passed &= Check(startsInside.availablePointCount == 2,
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
        selector.SelectInitial(candidates) ==
            std::optional<std::string>{ "good-first" },
        "initial selection uses the first usable candidate without a preference");
    passed &= Check(
        selector.SelectInitial(candidates, "lower-preferred") ==
            std::optional<std::string>{ "lower-preferred" },
        "selection keeps an edited preset when it remains usable");
    passed &= Check(
        selector.SelectInitial(candidates, "blocked") ==
            std::optional<std::string>{ "blocked" },
        "an explicitly edited preset remains selected when it is unusable");
    passed &= Check(
        selector.SelectInitial(candidates, "missing") ==
            std::optional<std::string>{ "good-first" },
        "selection falls back only when the edited preset no longer exists");
    passed &= Check(
        selector.Step(candidates, "good-first", 1) ==
            std::optional<std::string>{ "lower-preferred" },
        "forward selection advances through usable candidates");
    passed &= Check(
        selector.Step(candidates, "good-first", -1) ==
            std::optional<std::string>{ "good-tie" },
        "backward selection wraps and skips unusable candidates");

    // Exhaust every center/corner visibility combination.
    for (unsigned mask = 0; mask < 32; ++mask) {
        CameraCandidateVisibility candidate;
        candidate.pose = pose;
        unsigned visibleCorners = 0;
        for (std::size_t index = 0; index < kAnchorLOSPoints.size(); ++index) {
            const bool visible = (mask & (1U << index)) != 0;
            candidate.points.push_back(Point(0, 0x14, kAnchorLOSPoints[index],
                visible ? VisibilityPointStatus::kVisible : VisibilityPointStatus::kObstructed));
            if (index > 0 && visible) {
                ++visibleCorners;
            }
        }
        ApplyAnchorLOSRule(candidate);
        passed &= Check(candidate.usable == ((mask & 1) != 0 && visibleCorners >= 3),
            "anchor eligibility requires center and at least three corners");
    }
    for (std::size_t missing = 0; missing < 5; ++missing) {
        CameraCandidateVisibility candidate;
        candidate.pose = pose;
        for (std::size_t index = 0; index < 5; ++index) {
            candidate.points.push_back(Point(0, 0x14, kAnchorLOSPoints[index],
                index == missing ? VisibilityPointStatus::kUnavailable : VisibilityPointStatus::kVisible));
        }
        ApplyAnchorLOSRule(candidate);
        passed &= Check(!candidate.usable &&
            candidate.failureReason == CandidateFailureReason::kAnchorUnavailable,
            "any unavailable ray prevents selection");
        candidate.points[missing].status = VisibilityPointStatus::kStartsInsideCollision;
        ApplyAnchorLOSRule(candidate);
        passed &= Check(candidate.usable == (missing != 0),
            "start-inside is blocked and a single blocked corner is tolerated");
        candidate.points.erase(candidate.points.begin() + missing);
        ApplyAnchorLOSRule(candidate);
        passed &= Check(!candidate.usable, "an absent ray prevents selection");
    }
    passed &= Check(selector.Step(candidates, "", 1) == std::optional<std::string>{ "good-first" } &&
        selector.Step(candidates, "", -1) == std::optional<std::string>{ "good-tie" },
        "without a current selection D starts first and A starts last");

    return passed ? 0 : 1;
}
