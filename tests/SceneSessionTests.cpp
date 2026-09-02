#include "SceneSession.h"
#include "core/CameraPose.h"
#include "core/SceneAnchor.h"
#include "runtime/CameraPoseAdapter.h"
#include "runtime/PresetRepository.h"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace
{
    bool Check(bool a_condition, std::string_view a_message)
    {
        if (!a_condition) {
            std::cerr << "FAILED: " << a_message << '\n';
        }
        return a_condition;
    }

    bool CheckNear(float a_actual, float a_expected, std::string_view a_message)
    {
        return Check(std::abs(a_actual - a_expected) < 0.0001F, a_message);
    }

    std::filesystem::path NextTemporaryPath()
    {
        static std::uint64_t counter = 0;
        const auto suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(++counter);
        return std::filesystem::temp_directory_path() / ("ssc-presets-" + suffix + ".json");
    }

    bool WriteText(const std::filesystem::path& a_path, std::string_view a_text)
    {
        std::ofstream stream{ a_path, std::ios::binary };
        stream.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
        return stream.good();
    }

    void RemoveFile(const std::filesystem::path& a_path)
    {
        std::error_code error;
        static_cast<void>(std::filesystem::remove(a_path, error));
    }

    float Length(const ssc::core::Vec3& a_value)
    {
        return std::sqrt(
            a_value.x * a_value.x + a_value.y * a_value.y + a_value.z * a_value.z);
    }

    float Dot(const ssc::core::Vec3& a_left, const ssc::core::Vec3& a_right)
    {
        return a_left.x * a_right.x + a_left.y * a_right.y + a_left.z * a_right.z;
    }

    float Determinant(const ssc::core::CameraBasis& a_basis)
    {
        return a_basis.viewForward.x *
                   (a_basis.up.y * a_basis.right.z - a_basis.up.z * a_basis.right.y) -
               a_basis.up.x *
                   (a_basis.viewForward.y * a_basis.right.z -
                       a_basis.viewForward.z * a_basis.right.y) +
               a_basis.right.x *
                   (a_basis.viewForward.y * a_basis.up.z -
                       a_basis.viewForward.z * a_basis.up.y);
    }
}

int main()
{
    using ssc::SceneSession;
    using ssc::runtime::SceneKey;

    const SceneKey first{ 0x01001234, 7 };
    const SceneKey second{ 0x02005678, 8 };
    SceneSession session;
    bool passed = true;

    passed &= Check(session.IsIdle(), "new session is idle");
    passed &= Check(session.Prepare(first), "idle session accepts preparation");
    passed &= Check(session.IsPreparing(), "prepared session reports Preparing");
    passed &= Check(session.Matches(first), "prepared session retains its scene key");
    passed &= Check(!session.Prepare(second), "overlapping scene cannot replace Preparing");
    passed &= Check(session.Matches(first), "Preparing retains the original scene key");
    passed &= Check(!session.Activate(second), "wrong scene key cannot activate");
    passed &= Check(session.IsPreparing(), "failed activation preserves Preparing");
    passed &= Check(session.Activate(first), "matching scene key activates");
    passed &= Check(session.IsActive(), "activated session reports Active");
    passed &= Check(!session.Prepare(second), "overlapping scene cannot replace Active");
    passed &= Check(session.Matches(first), "overlap preserves the active scene key");

    session.BeginRestore();
    passed &= Check(
        session.GetState() == SceneSession::State::kRestoring,
        "active session enters Restoring");
    passed &= Check(session.Matches(first), "Restoring retains the scene key");
    passed &= Check(!session.Prepare(second), "Restoring cannot be replaced by a new scene");

    session.Clear();
    passed &= Check(session.IsIdle(), "clear returns the session to Idle");
    passed &= Check(!session.Matches(first), "clear removes the old scene key");
    passed &= Check(session.Prepare(second), "a new scene can prepare after clear");

    using ssc::core::SceneAnchorCalculator;
    using ssc::core::Vec3;
    SceneAnchorCalculator anchorCalculator;

    const std::array<Vec3, 2> twoParticipants{{ { 0.0F, 0.0F, 10.0F }, { 10.0F, 0.0F, 20.0F } }};
    const auto twoPersonAnchor = anchorCalculator.Evaluate({
        twoParticipants,
        Vec3{ 0.0F, 0.0F, 10.0F },
        Vec3{ 0.0F, 1.0F, 0.0F },
    });
    passed &= Check(twoPersonAnchor.has_value(), "two participants produce an anchor");
    if (twoPersonAnchor) {
        passed &= CheckNear(twoPersonAnchor->position.x, 5.0F, "anchor averages Pelvis X");
        passed &= CheckNear(twoPersonAnchor->position.y, 0.0F, "anchor averages Pelvis Y");
        passed &= CheckNear(twoPersonAnchor->position.z, 15.0F, "anchor averages Pelvis Z");
        passed &= CheckNear(twoPersonAnchor->forward.x, -1.0F, "multi-person forward points from anchor to player");
        passed &= CheckNear(twoPersonAnchor->forward.y, 0.0F, "multi-person forward is horizontal");
    }

    const std::array<Vec3, 1> oneParticipant{{ { 4.0F, 5.0F, 6.0F } }};
    const auto onePersonAnchor = anchorCalculator.Evaluate({
        oneParticipant,
        oneParticipant.front(),
        Vec3{ 0.0F, 3.0F, 7.0F },
    });
    passed &= Check(onePersonAnchor.has_value(), "one participant uses player-forward fallback");
    if (onePersonAnchor) {
        passed &= CheckNear(onePersonAnchor->forward.x, 0.0F, "one-person fallback removes X drift");
        passed &= CheckNear(onePersonAnchor->forward.y, -1.0F, "one-person fallback reverses player forward");
        passed &= CheckNear(onePersonAnchor->forward.z, 0.0F, "one-person fallback removes vertical forward");
    }

    const std::array<Vec3, 2> degenerateParticipants{{ { 3.0F, 4.0F, 1.0F }, { 3.0F, 4.0F, 9.0F } }};
    const auto degenerateAnchor = anchorCalculator.Evaluate({
        degenerateParticipants,
        Vec3{ 3.0F, 4.0F, 2.0F },
        Vec3{ 1.0F, 0.0F, 0.0F },
    });
    passed &= Check(degenerateAnchor.has_value(), "degenerate multi-person layout uses fallback");
    if (degenerateAnchor) {
        passed &= CheckNear(degenerateAnchor->forward.x, -1.0F, "degenerate fallback reverses player forward");
    }

    const auto maximum = std::numeric_limits<float>::max();
    const std::array<Vec3, 2> extremeParticipants{{
        { maximum, maximum, maximum },
        { maximum, maximum, maximum },
    }};
    const auto extremeAnchor = anchorCalculator.Evaluate({
        extremeParticipants,
        extremeParticipants.front(),
        Vec3{ 0.0F, 1.0F, 0.0F },
    });
    passed &= Check(extremeAnchor.has_value(), "finite extreme coordinates do not overflow the average");
    if (extremeAnchor) {
        passed &= Check(std::isfinite(extremeAnchor->position.x), "extreme anchor position remains finite");
    }

    using ssc::core::CameraOffset;
    using ssc::core::CameraPoseCalculator;
    using ssc::core::SceneAnchor;
    CameraPoseCalculator poseCalculator;

    const auto forwardYPose = poseCalculator.Evaluate(
        SceneAnchor{ { 10.0F, 20.0F, 30.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraOffset{ 5.0F, -200.0F, 60.0F });
    passed &= Check(forwardYPose.has_value(), "+Y anchor produces a camera pose");
    if (forwardYPose) {
        passed &= CheckNear(forwardYPose->position.x, 15.0F, "right offset follows F cross U");
        passed &= CheckNear(forwardYPose->position.y, -180.0F, "forward offset follows anchor forward");
        passed &= CheckNear(forwardYPose->position.z, 90.0F, "up offset follows world up");

        const auto toAnchorX = 10.0F - forwardYPose->position.x;
        const auto toAnchorY = 20.0F - forwardYPose->position.y;
        const auto toAnchorZ = 30.0F - forwardYPose->position.z;
        const auto toAnchorLength = std::sqrt(
            toAnchorX * toAnchorX + toAnchorY * toAnchorY + toAnchorZ * toAnchorZ);
        passed &= CheckNear(
            forwardYPose->basis.viewForward.x,
            toAnchorX / toAnchorLength,
            "rotation view column points toward anchor X");
        passed &= CheckNear(
            forwardYPose->basis.viewForward.y,
            toAnchorY / toAnchorLength,
            "rotation view column points toward anchor Y");
        passed &= CheckNear(
            forwardYPose->basis.viewForward.z,
            toAnchorZ / toAnchorLength,
            "rotation view column points toward anchor Z");
        passed &= CheckNear(Length(forwardYPose->basis.viewForward), 1.0F,
            "camera view direction is unit length");
        passed &= CheckNear(Length(forwardYPose->basis.up), 1.0F,
            "camera up direction is unit length");
        passed &= CheckNear(Length(forwardYPose->basis.right), 1.0F,
            "camera right direction is unit length");
        passed &= CheckNear(Dot(forwardYPose->basis.viewForward, forwardYPose->basis.up), 0.0F,
            "camera view and up directions are orthogonal");
        passed &= CheckNear(Dot(forwardYPose->basis.viewForward, forwardYPose->basis.right), 0.0F,
            "camera view and right directions are orthogonal");
        passed &= CheckNear(Dot(forwardYPose->basis.up, forwardYPose->basis.right), 0.0F,
            "camera up and right directions are orthogonal");
        passed &= CheckNear(Determinant(forwardYPose->basis), 1.0F,
            "camera basis is right-handed");

        const auto runtimePose = ssc::runtime::ToRuntimeCameraPose(*forwardYPose);
        passed &= CheckNear(runtimePose.rotation.entries[0][0], forwardYPose->basis.viewForward.x,
            "runtime rotation column 0 stores view-forward X");
        passed &= CheckNear(runtimePose.rotation.entries[1][1], forwardYPose->basis.up.y,
            "runtime rotation column 1 stores camera-up Y");
        passed &= CheckNear(runtimePose.rotation.entries[2][2], forwardYPose->basis.right.z,
            "runtime rotation column 2 stores camera-right Z");
    }

    const auto forwardXPose = poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } },
        CameraOffset{ 5.0F, -200.0F, 60.0F });
    passed &= Check(forwardXPose.has_value(), "rotated anchor produces a camera pose");
    if (forwardXPose) {
        passed &= CheckNear(forwardXPose->position.x, -200.0F,
            "rotated anchor preserves local forward composition");
        passed &= CheckNear(forwardXPose->position.y, -5.0F,
            "rotated anchor preserves local right composition");
        passed &= CheckNear(forwardXPose->position.z, 60.0F,
            "rotated anchor preserves local up composition");
    }

    const auto directlyAbove = poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraOffset{ 0.0F, 0.0F, 50.0F });
    passed &= Check(directlyAbove.has_value(), "camera directly above anchor uses fallback axis");
    if (directlyAbove) {
        passed &= CheckNear(directlyAbove->basis.viewForward.z, -1.0F,
            "camera above anchor looks straight down");
    }

    const auto directlyBelow = poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraOffset{ 0.0F, 0.0F, -50.0F });
    passed &= Check(directlyBelow.has_value(), "camera directly below anchor uses fallback axis");
    if (directlyBelow) {
        passed &= CheckNear(directlyBelow->basis.viewForward.z, 1.0F,
            "camera below anchor looks straight up");
    }

    passed &= Check(!poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraOffset{}), "camera and anchor at the same point are rejected");
    passed &= Check(!poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } },
        CameraOffset{ 0.0F, -10.0F, 0.0F }), "degenerate anchor forward is rejected");
    passed &= Check(!poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraOffset{ std::numeric_limits<float>::infinity(), -10.0F, 0.0F }),
        "non-finite camera offset is rejected");

    using ssc::runtime::PresetRepository;
    PresetRepository repository;
    const auto validPath = NextTemporaryPath();
    const auto validJson = R"json({
        "schemaVersion": 1,
        "futureTopLevel": true,
        "presets": [
            {
                "id": "default",
                "offset": { "right": 1, "forward": -200.5, "up": 60 },
                "futurePresetField": "ignored"
            },
            {
                "id": "close",
                "offset": { "right": -10, "forward": -80, "up": 20, "futureOffset": 4 }
            }
        ]
    })json";
    passed &= Check(WriteText(validPath, validJson), "valid preset fixture can be written");
    const auto validLoad = repository.LoadFromFile(validPath);
    passed &= Check(validLoad.succeeded, "valid schema loads successfully");
    passed &= Check(validLoad.presetCount == 2, "loader retains every preset");
    const auto validSnapshot = repository.Snapshot();
    passed &= Check(validSnapshot && validSnapshot->size() == 2,
        "valid immutable snapshot exposes every preset");
    if (validSnapshot && validSnapshot->size() == 2) {
        passed &= Check((*validSnapshot)[0].id == "default", "preset order is retained");
        passed &= CheckNear((*validSnapshot)[0].offset.forward, -200.5F,
            "numeric preset offset is retained");
        passed &= Check((*validSnapshot)[1].id == "close", "second preset is retained");
    }
    RemoveFile(validPath);

    const auto missingPath = NextTemporaryPath();
    const auto missingLoad = repository.LoadFromFile(missingPath);
    passed &= Check(!missingLoad.succeeded, "missing preset file is rejected");
    passed &= Check(repository.Snapshot()->empty(), "failed load publishes an empty snapshot");

    const std::array<std::pair<std::string_view, std::string_view>, 8> invalidDocuments{{
        { "broken JSON", R"json({ "schemaVersion": 1, "presets": [)json" },
        { "unknown schema version", R"json({ "schemaVersion": 2, "presets": [] })json" },
        { "missing required field", R"json({ "schemaVersion": 1, "presets": [
            { "id": "x", "offset": { "right": 0, "forward": -10 } }
        ] })json" },
        { "wrong member type", R"json({ "schemaVersion": 1, "presets": [
            { "id": "x", "offset": { "right": "zero", "forward": -10, "up": 0 } }
        ] })json" },
        { "non-finite numeric result", R"json({ "schemaVersion": 1, "presets": [
            { "id": "x", "offset": { "right": 1e9999, "forward": -10, "up": 0 } }
        ] })json" },
        { "duplicate id", R"json({ "schemaVersion": 1, "presets": [
            { "id": "x", "offset": { "right": 0, "forward": -10, "up": 0 } },
            { "id": "x", "offset": { "right": 1, "forward": -20, "up": 1 } }
        ] })json" },
        { "empty id", R"json({ "schemaVersion": 1, "presets": [
            { "id": "", "offset": { "right": 0, "forward": -10, "up": 0 } }
        ] })json" },
        { "non-array presets", R"json({ "schemaVersion": 1, "presets": {} })json" },
    }};
    for (const auto& [description, document] : invalidDocuments) {
        const auto path = NextTemporaryPath();
        passed &= Check(WriteText(path, document), "invalid preset fixture can be written");
        PresetRepository invalidRepository;
        const auto result = invalidRepository.LoadFromFile(path);
        passed &= Check(!result.succeeded, description);
        passed &= Check(invalidRepository.Snapshot()->empty(),
            "invalid document leaves an empty snapshot");
        RemoveFile(path);
    }

    const auto emptyPath = NextTemporaryPath();
    passed &= Check(WriteText(emptyPath, R"json({ "schemaVersion": 1, "presets": [] })json"),
        "empty preset fixture can be written");
    PresetRepository emptyRepository;
    const auto emptyLoad = emptyRepository.LoadFromFile(emptyPath);
    passed &= Check(emptyLoad.succeeded && emptyLoad.presetCount == 0,
        "empty preset array is a valid empty snapshot");
    RemoveFile(emptyPath);

    return passed ? 0 : 1;
}
