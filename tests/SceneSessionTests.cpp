#include "SceneSession.h"
#include "core/CameraPose.h"
#include "core/SceneAnchor.h"
#include "runtime/CameraPoseAdapter.h"
#include "runtime/PluginIdentity.h"
#include "runtime/PresetPreviewService.h"
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
    using ssc::runtime::PluginFilenameEquals;
    using ssc::runtime::SceneKey;

    const SceneKey first{ 0x01001234, 7 };
    const SceneKey second{ 0x02005678, 8 };
    SceneSession session;
    bool passed = true;

    passed &= Check(
        PluginFilenameEquals("SexLab.esm", "SexLab.esm"),
        "plugin filename identity accepts exact spelling");
    passed &= Check(
        PluginFilenameEquals("sexlab.esm", "SexLab.esm"),
        "plugin filename identity ignores ASCII case");
    passed &= Check(
        PluginFilenameEquals("SEXLAB.ESM", "SexLab.esm"),
        "plugin filename identity accepts uppercase spelling");
    passed &= Check(
        !PluginFilenameEquals("SexLab.esp", "SexLab.esm"),
        "plugin filename identity rejects a different extension");
    passed &= Check(
        !PluginFilenameEquals("Other.esm", "SexLab.esm"),
        "plugin filename identity rejects a different plugin");

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

    auto* previewService = ssc::runtime::PresetPreviewService::GetSingleton();
    previewService->EndPreviewSession();
    passed &= Check(!previewService->PreviewSessionActive(),
        "preset preview session begins inactive");
    previewService->BeginPreviewSession();
    passed &= Check(previewService->PreviewSessionActive(),
        "preset preview session can be opened independently of a scene");
    const ssc::runtime::PresetTransform previewTransform{
        { 10.0F, 20.0F }, { 30.0F, 5.0F, 200.0F }
    };
    const auto previewRevision = previewService->SetPreview(previewTransform);
    const auto previewRequest = previewService->Request();
    passed &= Check(previewRequest && previewRequest->revision == previewRevision &&
        previewRequest->transform.has_value(),
        "preset preview request carries only transform values and a revision");
    previewService->PublishFeedback({
        true, true, true, previewRevision, previewTransform, "Preview active" });
    const auto previewFeedback = previewService->Feedback();
    passed &= Check(previewFeedback && previewFeedback->sceneActive &&
        previewFeedback->previewApplied && previewFeedback->previewPossible &&
        previewFeedback->appliedRevision == previewRevision,
        "preset preview feedback acknowledges the applied revision");
    previewService->ClearPreview();
    const auto clearRequest = previewService->Request();
    passed &= Check(clearRequest && clearRequest->revision > previewRevision &&
        !clearRequest->transform.has_value(),
        "clearing preview publishes a newer request without scene data");
    previewService->EndPreviewSession();
    passed &= Check(!previewService->PreviewSessionActive(),
        "preset preview session can be closed");

    using ssc::core::SceneAnchorCalculator;
    using ssc::core::Vec3;
    SceneAnchorCalculator anchorCalculator;

    const std::array<Vec3, 2> twoParticipants{{ { 0.0F, 0.0F, 10.0F }, { 10.0F, 0.0F, 20.0F } }};
    const auto twoPersonAnchor = anchorCalculator.Evaluate({
        twoParticipants,
        Vec3{ 0.0F, 1.0F, 0.0F },
        Vec3{ 1.0F, 0.0F, 0.0F },
    });
    passed &= Check(twoPersonAnchor.has_value(), "two participants produce an anchor");
    if (twoPersonAnchor) {
        passed &= CheckNear(twoPersonAnchor->position.x, 5.0F, "anchor averages Pelvis X");
        passed &= CheckNear(twoPersonAnchor->position.y, 0.0F, "anchor averages Pelvis Y");
        passed &= CheckNear(twoPersonAnchor->position.z, 15.0F, "anchor averages Pelvis Z");
        passed &= CheckNear(twoPersonAnchor->forward.x, 0.0F,
            "multi-person forward ignores the player's position around the anchor");
        passed &= CheckNear(twoPersonAnchor->forward.y, -1.0F,
            "multi-person forward reverses the player Pelvis forward");
    }

    const std::array<Vec3, 1> oneParticipant{{ { 4.0F, 5.0F, 6.0F } }};
    const auto onePersonAnchor = anchorCalculator.Evaluate({
        oneParticipant,
        Vec3{ 4.0F, 0.0F, 7.0F },
        Vec3{ 0.0F, 1.0F, 0.0F },
    });
    passed &= Check(onePersonAnchor.has_value(), "one participant uses Pelvis forward");
    if (onePersonAnchor) {
        passed &= CheckNear(onePersonAnchor->forward.x, -1.0F,
            "one-person forward reverses Pelvis forward");
        passed &= CheckNear(onePersonAnchor->forward.y, 0.0F,
            "one-person forward does not use actor yaw when Pelvis forward is valid");
        passed &= CheckNear(onePersonAnchor->forward.z, 0.0F,
            "one-person forward removes vertical Pelvis tilt");
    }

    const std::array<Vec3, 2> degenerateParticipants{{ { 3.0F, 4.0F, 1.0F }, { 3.0F, 4.0F, 9.0F } }};
    const auto degenerateAnchor = anchorCalculator.Evaluate({
        degenerateParticipants,
        Vec3{ 0.0F, 0.0F, 1.0F },
        Vec3{ 1.0F, 0.0F, 0.0F },
    });
    passed &= Check(degenerateAnchor.has_value(), "vertical Pelvis forward uses actor fallback");
    if (degenerateAnchor) {
        passed &= CheckNear(degenerateAnchor->forward.x, -1.0F,
            "actor fallback is reversed for anchor forward");
    }

    const auto maximum = std::numeric_limits<float>::max();
    const std::array<Vec3, 2> extremeParticipants{{
        { maximum, maximum, maximum },
        { maximum, maximum, maximum },
    }};
    const auto extremeAnchor = anchorCalculator.Evaluate({
        extremeParticipants,
        Vec3{ 0.0F, 1.0F, 0.0F },
        std::nullopt,
    });
    passed &= Check(extremeAnchor.has_value(), "finite extreme coordinates do not overflow the average");
    if (extremeAnchor) {
        passed &= Check(std::isfinite(extremeAnchor->position.x), "extreme anchor position remains finite");
    }

    using ssc::core::CameraPoseCalculator;
    using ssc::core::CameraRig;
    using ssc::core::SceneAnchor;
    CameraPoseCalculator poseCalculator;

    const auto forwardYPose = poseCalculator.Evaluate(
        SceneAnchor{ { 10.0F, 20.0F, 30.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraRig{ { 5.0F, 20.0F }, { 0.0F, 0.0F, 200.0F } });
    passed &= Check(forwardYPose.has_value(), "+Y anchor produces a camera pose");
    if (forwardYPose) {
        passed &= CheckNear(forwardYPose->position.x, 15.0F, "pivot right follows F cross U");
        passed &= CheckNear(forwardYPose->position.y, -180.0F, "zero yaw places camera behind framing center");
        passed &= CheckNear(forwardYPose->position.z, 50.0F, "pan up follows camera up");

        const auto toPivotX = 15.0F - forwardYPose->position.x;
        const auto toPivotY = 20.0F - forwardYPose->position.y;
        const auto toPivotZ = 50.0F - forwardYPose->position.z;
        const auto toPivotLength = std::sqrt(
            toPivotX * toPivotX + toPivotY * toPivotY + toPivotZ * toPivotZ);
        passed &= CheckNear(
            forwardYPose->basis.viewForward.x,
            toPivotX / toPivotLength,
            "rotation view column points toward pivot X");
        passed &= CheckNear(
            forwardYPose->basis.viewForward.y,
            toPivotY / toPivotLength,
            "rotation view column points toward pivot Y");
        passed &= CheckNear(
            forwardYPose->basis.viewForward.z,
            toPivotZ / toPivotLength,
            "rotation view column points toward pivot Z");
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

    const CameraRig orbitRig{ {}, { 35.0F, 20.0F, 250.0F } };
    const auto orbitPose = poseCalculator.Evaluate(
        SceneAnchor{ { 10.0F, 20.0F, 30.0F }, { 0.0F, 1.0F, 0.0F } },
        orbitRig);
    passed &= Check(orbitPose.has_value(), "yaw and pitch produce an orbit pose");
    if (orbitPose) {
        const auto extracted = poseCalculator.ExtractRig(
            SceneAnchor{ { 10.0F, 20.0F, 30.0F }, { 0.0F, 1.0F, 0.0F } },
            orbitPose->position);
        passed &= Check(extracted.has_value(), "world camera position converts back to an orbit");
        if (extracted) {
            passed &= CheckNear(extracted->orbit.yawDegrees, 35.0F,
                "inverse conversion restores yaw");
            passed &= CheckNear(extracted->orbit.pitchDegrees, 20.0F,
                "inverse conversion restores pitch");
            passed &= CheckNear(extracted->orbit.distance, 250.0F,
                "inverse conversion restores distance");
        }
    }

    const CameraRig framedOrbitRig{ { 25.0F, 40.0F }, { 35.0F, 20.0F, 250.0F } };
    const auto framedOrbitPose = poseCalculator.Evaluate(
        SceneAnchor{ { 10.0F, 20.0F, 30.0F }, { 0.0F, 1.0F, 0.0F } },
        framedOrbitRig);
    passed &= Check(framedOrbitPose.has_value(), "screen-relative framing produces an orbit pose");
    if (framedOrbitPose) {
        const Vec3 cameraToAnchor{
            10.0F - framedOrbitPose->position.x,
            20.0F - framedOrbitPose->position.y,
            30.0F - framedOrbitPose->position.z,
        };
        passed &= CheckNear(Dot(cameraToAnchor, framedOrbitPose->basis.right), -25.0F,
            "pan right remains the anchor's screen-horizontal offset after yaw");
        passed &= CheckNear(Dot(cameraToAnchor, framedOrbitPose->basis.up), -40.0F,
            "pan up remains the anchor's screen-vertical offset after pitch");
        passed &= CheckNear(Dot(cameraToAnchor, framedOrbitPose->basis.viewForward), 250.0F,
            "framing offsets do not alter orbit distance along the view axis");
    }

    const auto forwardXPose = poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } },
        CameraRig{ { 5.0F, 20.0F }, { 0.0F, 0.0F, 200.0F } });
    passed &= Check(forwardXPose.has_value(), "rotated anchor produces a camera pose");
    if (forwardXPose) {
        passed &= CheckNear(forwardXPose->position.x, -200.0F,
            "rotated anchor preserves orbit distance");
        passed &= CheckNear(forwardXPose->position.y, -5.0F,
            "rotated anchor preserves pivot right composition");
        passed &= CheckNear(forwardXPose->position.z, 20.0F,
            "rotated anchor preserves pivot up composition");
    }

    const auto directlyAbove = poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraRig{ {}, { 0.0F, 90.0F, 50.0F } });
    passed &= Check(directlyAbove.has_value(), "camera directly above anchor uses fallback axis");
    if (directlyAbove) {
        passed &= CheckNear(directlyAbove->basis.viewForward.z, -1.0F,
            "camera above anchor looks straight down");
    }

    const auto directlyBelow = poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraRig{ {}, { 0.0F, -90.0F, 50.0F } });
    passed &= Check(directlyBelow.has_value(), "camera directly below anchor uses fallback axis");
    if (directlyBelow) {
        passed &= CheckNear(directlyBelow->basis.viewForward.z, 1.0F,
            "camera below anchor looks straight up");
    }

    passed &= Check(!poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraRig{}), "zero orbit distance is rejected");
    passed &= Check(!poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } },
        CameraRig{ {}, { 0.0F, 0.0F, 10.0F } }), "degenerate anchor forward is rejected");
    passed &= Check(!poseCalculator.Evaluate(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        CameraRig{
            { std::numeric_limits<float>::infinity(), 0.0F },
            { 0.0F, 0.0F, 10.0F } }),
        "non-finite framing offset is rejected");
    passed &= Check(!poseCalculator.ExtractRig(
        SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } },
        Vec3{ 0.0F, -10.0F, 0.0F }), "degenerate anchor rejects orbit extraction");

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
        const auto expectedDistance = static_cast<float>(std::hypot(1.0, 200.5, 60.0));
        passed &= CheckNear((*validSnapshot)[0].transform.framingOffset.right, 0.0F,
            "legacy offset migration keeps horizontal framing centered");
        passed &= CheckNear((*validSnapshot)[0].transform.framingOffset.up, 0.0F,
            "legacy offset migration keeps vertical framing centered");
        passed &= CheckNear((*validSnapshot)[0].transform.orbit.distance, expectedDistance,
            "legacy offset migration retains camera distance");
        const auto migratedPose = poseCalculator.Evaluate(
            SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
            CameraRig{
                {
                    (*validSnapshot)[0].transform.framingOffset.right,
                    (*validSnapshot)[0].transform.framingOffset.up,
                },
                {
                    (*validSnapshot)[0].transform.orbit.yawDegrees,
                    (*validSnapshot)[0].transform.orbit.pitchDegrees,
                    (*validSnapshot)[0].transform.orbit.distance,
                },
            });
        passed &= Check(migratedPose.has_value(), "migrated legacy preset evaluates");
        if (migratedPose) {
            passed &= CheckNear(migratedPose->position.x, 1.0F,
                "legacy migration retains right position");
            passed &= CheckNear(migratedPose->position.y, -200.5F,
                "legacy migration retains forward position");
            passed &= CheckNear(migratedPose->position.z, 60.0F,
                "legacy migration retains up position");
        }
        passed &= Check((*validSnapshot)[1].id == "close", "second preset is retained");
    }

    const auto version2Path = NextTemporaryPath();
    passed &= Check(WriteText(version2Path, R"json({
        "schemaVersion": 2,
        "presets": [
            {
                "id": "version-2",
                "pivotOffset": { "right": 10, "forward": 20, "up": 30 },
                "orbit": { "yawDegrees": 90, "pitchDegrees": 0, "distance": 100 }
            }
        ]
    })json"), "version 2 migration fixture can be written");
    PresetRepository version2Repository;
    const auto version2Load = version2Repository.LoadFromFile(version2Path);
    passed &= Check(version2Load.succeeded && version2Load.presetCount == 1,
        "version 2 schema migrates successfully");
    if (version2Load.succeeded) {
        const auto& migrated = version2Repository.Snapshot()->front().transform;
        passed &= CheckNear(migrated.framingOffset.right, 20.0F,
            "version 2 pivot offset migrates along rotated screen right");
        passed &= CheckNear(migrated.framingOffset.up, 30.0F,
            "version 2 pivot up migrates to screen up");
        passed &= CheckNear(migrated.orbit.distance, 110.0F,
            "version 2 view-axis pivot component folds into orbit distance");
        const auto migratedVersion2Pose = poseCalculator.Evaluate(
            SceneAnchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
            CameraRig{
                { migrated.framingOffset.right, migrated.framingOffset.up },
                {
                    migrated.orbit.yawDegrees,
                    migrated.orbit.pitchDegrees,
                    migrated.orbit.distance,
                },
            });
        passed &= Check(migratedVersion2Pose.has_value(),
            "migrated version 2 preset evaluates");
        if (migratedVersion2Pose) {
            passed &= CheckNear(migratedVersion2Pose->position.x, 110.0F,
                "version 2 migration retains camera X");
            passed &= CheckNear(migratedVersion2Pose->position.y, 20.0F,
                "version 2 migration retains camera Y");
            passed &= CheckNear(migratedVersion2Pose->position.z, 30.0F,
                "version 2 migration retains camera Z");
        }
    }
    RemoveFile(version2Path);

    passed &= Check(repository.Create({
        "wide",
        { { 25.0F, 100.0F }, { 30.0F, 10.0F, 350.0F } },
    }).succeeded, "create persists a new preset");
    passed &= Check(repository.Snapshot()->size() == 3,
        "create publishes all presets");
    passed &= Check(!repository.Create({
        "wide",
        { {}, { 0.0F, 0.0F, 100.0F } },
    }).succeeded, "create rejects a duplicate id");
    passed &= Check(!repository.Create({
        "zero-distance",
        { {}, { 0.0F, 0.0F, 0.0F } },
    }).succeeded, "create rejects zero orbit distance");
    passed &= Check(!repository.Create({
        "too-close",
        { {}, { 0.0F, 0.0F, 1.0e-8F } },
    }).succeeded, "create rejects an orbit distance too close to derive a view direction");

    passed &= Check(repository.Update(
        "default",
        { { 20.0F, 80.0F }, { 45.0F, -15.0F, 240.0F } }).succeeded,
        "update persists an existing preset");
    passed &= CheckNear(repository.Snapshot()->front().transform.framingOffset.right, 20.0F,
        "update publishes the changed framing offset");
    passed &= CheckNear(repository.Snapshot()->front().transform.orbit.yawDegrees, 45.0F,
        "update publishes the changed orbit");
    passed &= Check(!repository.Update(
        "missing",
        { {}, { 0.0F, 0.0F, 100.0F } }).succeeded,
        "update rejects an unknown id");

    passed &= Check(repository.Delete("close").succeeded,
        "delete removes an existing preset");
    passed &= Check(repository.Snapshot()->size() == 2,
        "delete publishes the remaining presets");
    passed &= Check(!repository.Delete("close").succeeded,
        "delete rejects an unknown id");

    PresetRepository persistedReader;
    const auto persistedLoad = persistedReader.LoadFromFile(validPath);
    passed &= Check(persistedLoad.succeeded && persistedLoad.presetCount == 2,
        "CRUD result can be loaded from disk");
    if (persistedReader.Snapshot()->size() == 2) {
        passed &= Check((*persistedReader.Snapshot())[0].id == "default",
            "update retains preset order");
        passed &= CheckNear(
            (*persistedReader.Snapshot())[0].transform.framingOffset.right,
            20.0F,
            "updated framing offset survives reload");
        passed &= CheckNear(
            (*persistedReader.Snapshot())[0].transform.orbit.yawDegrees,
            45.0F,
            "updated orbit survives reload");
        passed &= Check((*persistedReader.Snapshot())[1].id == "wide",
            "created preset survives reload");
    }
    const auto reloadResult = repository.Reload();
    passed &= Check(reloadResult.succeeded && reloadResult.presetCount == 2,
        "repository reloads its storage path");

    passed &= Check(WriteText(validPath, R"json({ "schemaVersion": 1, "presets": [)json"),
        "broken reload fixture can be written");
    const auto brokenReload = repository.Reload();
    passed &= Check(!brokenReload.succeeded,
        "repository rejects a broken external reload");
    passed &= Check(repository.Snapshot()->size() == 2,
        "failed reload retains the last valid snapshot");
    passed &= Check((*repository.Snapshot())[0].id == "default",
        "failed reload retains the last valid preset values");
    RemoveFile(validPath);

    const auto missingPath = NextTemporaryPath();
    const auto missingLoad = repository.LoadFromFile(missingPath);
    passed &= Check(missingLoad.succeeded && missingLoad.presetCount == 0,
        "missing preset file initializes a valid empty repository");
    passed &= Check(repository.Snapshot()->empty(),
        "missing preset file publishes an empty snapshot");
    passed &= Check(repository.Create({
        "recovered-missing",
        { {}, { 0.0F, 0.0F, 200.0F } },
    }).succeeded, "missing preset file can be recovered by creating a preset");
    RemoveFile(missingPath);

    const auto corruptRecoveryPath = NextTemporaryPath();
    passed &= Check(WriteText(
        corruptRecoveryPath,
        R"json({ "schemaVersion": 3, "presets": [)json"),
        "corrupt recovery fixture can be written");
    PresetRepository corruptRecoveryRepository;
    passed &= Check(!corruptRecoveryRepository.LoadFromFile(corruptRecoveryPath).succeeded,
        "corrupt initial preset file is reported");
    passed &= Check(corruptRecoveryRepository.Create({
        "recovered-corrupt",
        { {}, { 0.0F, 0.0F, 200.0F } },
    }).succeeded, "corrupt preset file can be recovered by creating a preset");
    auto corruptBackupPath = corruptRecoveryPath;
    corruptBackupPath += ".invalid.bak";
    passed &= Check(std::filesystem::exists(corruptBackupPath),
        "corrupt preset file is backed up before recovery save");
    PresetRepository corruptRecoveryReader;
    passed &= Check(corruptRecoveryReader.LoadFromFile(corruptRecoveryPath).succeeded &&
        corruptRecoveryReader.Snapshot()->size() == 1,
        "recovered corrupt preset file can be loaded");
    RemoveFile(corruptRecoveryPath);
    RemoveFile(corruptBackupPath);

    const std::array<std::pair<std::string_view, std::string_view>, 13> invalidDocuments{{
        { "broken JSON", R"json({ "schemaVersion": 1, "presets": [)json" },
        { "unknown schema version", R"json({ "schemaVersion": 4, "presets": [] })json" },
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
        { "camera at anchor", R"json({ "schemaVersion": 1, "presets": [
            { "id": "x", "offset": { "right": 0, "forward": 0, "up": 0 } }
        ] })json" },
        { "non-array presets", R"json({ "schemaVersion": 1, "presets": {} })json" },
        { "missing v2 orbit", R"json({ "schemaVersion": 2, "presets": [
            { "id": "x", "pivotOffset": { "right": 0, "forward": 0, "up": 0 } }
        ] })json" },
        { "v2 yaw outside range", R"json({ "schemaVersion": 2, "presets": [
            { "id": "x", "pivotOffset": { "right": 0, "forward": 0, "up": 0 },
              "orbit": { "yawDegrees": 181, "pitchDegrees": 0, "distance": 100 } }
        ] })json" },
        { "v2 zero distance", R"json({ "schemaVersion": 2, "presets": [
            { "id": "x", "pivotOffset": { "right": 0, "forward": 0, "up": 0 },
              "orbit": { "yawDegrees": 0, "pitchDegrees": 0, "distance": 0 } }
        ] })json" },
        { "missing v3 framing member", R"json({ "schemaVersion": 3, "presets": [
            { "id": "x", "framingOffset": { "right": 0 },
              "orbit": { "yawDegrees": 0, "pitchDegrees": 0, "distance": 100 } }
        ] })json" },
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
