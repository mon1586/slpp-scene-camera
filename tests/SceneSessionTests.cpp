#include "SceneSession.h"
#include "core/CameraPose.h"
#include "core/SceneAnchor.h"
#include "runtime/CameraPoseAdapter.h"
#include "runtime/PluginIdentity.h"
#include "runtime/PresetPreviewService.h"
#include "runtime/PresetRepository.h"
#include "runtime/SceneInstanceID.h"

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

    using ssc::runtime::ParseSceneInstanceID;
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();
    struct InstanceIDCase
    {
        std::string_view text;
        float number;
        std::optional<std::int32_t> expected;
        std::string_view description;
    };
    const std::array instanceIDCases{
        InstanceIDCase{ "7", 8.0F, 7, "valid string wins over a different numeric ID" },
        InstanceIDCase{ "7", nan, 7, "valid string does not depend on the fallback" },
        InstanceIDCase{ "2147483647", 0.0F, 2147483647, "string accepts int32 maximum exactly" },
        InstanceIDCase{ "-2147483648", 0.0F, std::numeric_limits<std::int32_t>::min(),
            "string accepts int32 minimum exactly" },
        InstanceIDCase{ "2147483648", 9.0F, 9, "out-of-range string uses numeric fallback" },
        InstanceIDCase{ "-2147483649", nan, std::nullopt, "both out-of-range string and invalid number are rejected" },
        InstanceIDCase{ "", 0.0F, 0, "empty text permits numeric zero" },
        InstanceIDCase{ "bad", 3.0F, 3, "malformed string preserves compatibility fallback" },
        InstanceIDCase{ "7tail", nan, std::nullopt, "integer prefix alone is not a valid string ID" },
        InstanceIDCase{ " 7", nan, std::nullopt, "leading whitespace is not accepted as an integer string" },
        InstanceIDCase{ "+7", nan, std::nullopt, "leading plus is not accepted as an integer string" },
        InstanceIDCase{ "", 1.49F, 1, "numeric fraction rounds to nearest integer" },
        InstanceIDCase{ "", 1.5F, 2, "positive half rounds away from zero" },
        InstanceIDCase{ "", -1.5F, -2, "negative half rounds away from zero" },
        InstanceIDCase{ "", -2147483648.0F, std::numeric_limits<std::int32_t>::min(),
            "numeric int32 minimum is accepted" },
        InstanceIDCase{ "", std::nextafter(2147483648.0F, 0.0F), 2147483520,
            "largest representable float below int32 upper boundary is accepted" },
        InstanceIDCase{ "", 2147483648.0F, std::nullopt,
            "numeric 2^31 is rejected before integer conversion" },
        InstanceIDCase{ "", std::nextafter(-2147483648.0F, -infinity), std::nullopt,
            "float immediately below int32 minimum is rejected" },
        InstanceIDCase{ "", nan, std::nullopt, "NaN fallback is rejected" },
        InstanceIDCase{ "", infinity, std::nullopt, "positive infinity fallback is rejected" },
        InstanceIDCase{ "", -infinity, std::nullopt, "negative infinity fallback is rejected" },
    };
    for (const auto& test : instanceIDCases) {
        passed &= Check(ParseSceneInstanceID(test.text, test.number) == test.expected, test.description);
    }

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
    const auto previewRevision = previewService->SetPreview(previewTransform, "edited");
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
    previewService->ClearPreview("edited");
    const auto clearRequest = previewService->Request();
    passed &= Check(clearRequest && clearRequest->revision > previewRevision &&
        !clearRequest->transform.has_value() && clearRequest->presetID == "edited",
        "clearing preview publishes a newer request that retains the edited preset");
    previewService->ClearPreview();
    passed &= Check(previewService->Request() == clearRequest &&
        previewService->Request()->presetID == "edited",
        "a duplicate close notification does not erase the preset to resume");
    previewService->EndPreviewSession();
    passed &= Check(!previewService->PreviewSessionActive(),
        "preset preview session can be closed");

    using ssc::core::SceneAnchorCalculator;
    using ssc::core::Vec3;
    SceneAnchorCalculator anchorCalculator;

    using ssc::core::SmoothAnchorPosition;
    const Vec3 smoothingTarget{ 100.0F, -40.0F, 20.0F };
    const auto halfWay = SmoothAnchorPosition({}, smoothingTarget, 0.15F);
    passed &= CheckNear(halfWay.x, 50.0F, "anchor closes half the gap in 0.15 seconds");
    passed &= CheckNear(halfWay.y, -20.0F, "negative coordinates smooth toward the target");
    passed &= CheckNear(halfWay.z, 10.0F, "anchor height uses the same response");
    Vec3 at30FPS{};
    Vec3 at144FPS{};
    for (int frame = 0; frame < 30; ++frame) {
        at30FPS = SmoothAnchorPosition(at30FPS, smoothingTarget, 1.0F / 30.0F);
    }
    for (int frame = 0; frame < 144; ++frame) {
        at144FPS = SmoothAnchorPosition(at144FPS, smoothingTarget, 1.0F / 144.0F);
    }
    passed &= Check(std::abs(at30FPS.x - at144FPS.x) < 0.001F &&
        std::abs(at30FPS.y - at144FPS.y) < 0.001F &&
        std::abs(at30FPS.z - at144FPS.z) < 0.001F,
        "30 FPS and 144 FPS converge equally over one second");
    passed &= Check(at30FPS.x > halfWay.x && at30FPS.x <= smoothingTarget.x,
        "tracking converges without overshooting");
    for (const auto invalidDelta : { 0.0F, -1.0F,
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity() }) {
        const auto held = SmoothAnchorPosition(halfWay, smoothingTarget, invalidDelta);
        passed &= Check(held.x == halfWay.x && held.y == halfWay.y && held.z == halfWay.z,
            "paused or invalid elapsed time preserves the anchor");
    }

    const auto bodyCenterAnchor = anchorCalculator.Evaluate({
        Vec3{ 4.0F, 5.0F, 6.0F },
        Vec3{ 1.0F, 0.0F, 0.0F },
    });
    passed &= Check(bodyCenterAnchor.has_value(), "player body center produces an anchor");
    if (bodyCenterAnchor) {
        passed &= CheckNear(bodyCenterAnchor->position.x, 4.0F, "anchor uses body center X");
        passed &= CheckNear(bodyCenterAnchor->position.y, 5.0F, "anchor uses body center Y");
        passed &= CheckNear(bodyCenterAnchor->position.z, 6.0F, "anchor uses body center Z");
        passed &= CheckNear(bodyCenterAnchor->forward.x, -1.0F,
            "anchor forward reverses the Actor horizontal forward");
        passed &= CheckNear(bodyCenterAnchor->forward.y, 0.0F,
            "anchor forward preserves the Actor horizontal orientation");
    }

    const auto degenerateAnchor = anchorCalculator.Evaluate({
        Vec3{ 3.0F, 4.0F, 5.0F },
        Vec3{ 0.0F, 0.0F, 1.0F },
    });
    passed &= Check(!degenerateAnchor.has_value(), "vertical Actor forward is rejected");

    const auto maximum = std::numeric_limits<float>::max();
    const auto extremeAnchor = anchorCalculator.Evaluate({
        Vec3{ maximum, maximum, maximum },
        Vec3{ 0.0F, 1.0F, 0.0F },
    });
    passed &= Check(extremeAnchor.has_value(), "finite extreme coordinates produce an anchor");
    if (extremeAnchor) {
        passed &= Check(std::isfinite(extremeAnchor->position.x), "extreme anchor position remains finite");
    }

    const auto invalidPositionAnchor = anchorCalculator.Evaluate({
        Vec3{ std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F },
        Vec3{ 0.0F, 1.0F, 0.0F },
    });
    passed &= Check(!invalidPositionAnchor.has_value(), "non-finite body center is rejected");

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

        const auto runtimePose = ssc::runtime::ToRuntimeCameraPose(*forwardYPose, -12.5F);
        passed &= CheckNear(runtimePose.rotation.entries[0][0], forwardYPose->basis.viewForward.x,
            "runtime rotation column 0 stores view-forward X");
        passed &= CheckNear(runtimePose.rotation.entries[1][1], forwardYPose->basis.up.y,
            "runtime rotation column 1 stores camera-up Y");
        passed &= CheckNear(runtimePose.rotation.entries[2][2], forwardYPose->basis.right.z,
            "runtime rotation column 2 stores camera-right Z");
        passed &= CheckNear(runtimePose.fovOffsetDegrees, -12.5F,
            "runtime camera pose carries the preset FOV offset");

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
        "schemaVersion": 5,
        "futureTopLevel": true,
        "presets": [
            {
                "id": "default", "name": "Default",
                "framingOffset": { "right": 1, "up": 60 },
                "orbit": { "yawDegrees": 0, "pitchDegrees": 10, "distance": 200 },
                "fovOffsetDegrees": -15,
                "futurePresetField": "ignored"
            },
            {
                "id": "close", "name": "Close",
                "framingOffset": { "right": -10, "up": 20 },
                "orbit": { "yawDegrees": 0, "pitchDegrees": 0, "distance": 80 },
                "fovOffsetDegrees": 0
            }
        ]
    })json";
    passed &= Check(WriteText(validPath, validJson), "valid preset fixture can be written");
    const auto validLoad = repository.LoadFromFile(validPath);
    passed &= Check(validLoad.succeeded && validLoad.presetCount == 2,
        "schema 5 loads all presets");
    const auto validSnapshot = repository.Snapshot();
    if (validSnapshot->size() == 2) {
        passed &= Check(validSnapshot->front().id == "default" &&
            validSnapshot->front().name == "Default", "ID and display name load independently");
        passed &= CheckNear(validSnapshot->front().transform.fovOffsetDegrees, -15.0F,
            "schema 5 retains FOV offset");
    }

    passed &= Check(repository.Create({
        "wide",
        { { 25.0F, 100.0F }, { 30.0F, 10.0F, 350.0F } },
        "wide",
    }).succeeded, "create persists a new preset");
    passed &= Check(repository.Snapshot()->size() == 3,
        "create publishes all presets");
    passed &= Check(!repository.Create({
        "wide",
        { {}, { 0.0F, 0.0F, 100.0F } },
        "wide",
    }).succeeded, "create rejects a duplicate id");
    passed &= Check(!repository.Create({
        "zero-distance",
        { {}, { 0.0F, 0.0F, 0.0F } },
        "zero-distance",
    }).succeeded, "create rejects zero orbit distance");
    passed &= Check(!repository.Create({
        "too-close",
        { {}, { 0.0F, 0.0F, 1.0e-8F } },
        "too-close",
    }).succeeded, "create rejects an orbit distance too close to derive a view direction");

    passed &= Check(repository.Update(
        "default",
        { { 20.0F, 80.0F }, { 45.0F, -15.0F, 240.0F }, 20.0F }, "Renamed").succeeded,
        "update persists an existing preset");
    passed &= CheckNear(repository.Snapshot()->front().transform.framingOffset.right, 20.0F,
        "update publishes the changed framing offset");
    passed &= CheckNear(repository.Snapshot()->front().transform.orbit.yawDegrees, 45.0F,
        "update publishes the changed orbit");
    passed &= CheckNear(repository.Snapshot()->front().transform.fovOffsetDegrees, 20.0F,
        "update publishes the changed FOV offset");
    passed &= Check(!repository.Update(
        "missing",
        { {}, { 0.0F, 0.0F, 100.0F } }, "Missing").succeeded,
        "update rejects an unknown id");

    passed &= Check(repository.Snapshot()->front().id == "default" &&
        repository.Snapshot()->front().name == "Renamed",
        "rename changes the display name while preserving identity and order");

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
        passed &= Check((*persistedReader.Snapshot())[0].name == "Renamed",
            "renamed display name survives reload");
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
        passed &= CheckNear(
            (*persistedReader.Snapshot())[0].transform.fovOffsetDegrees,
            20.0F,
            "updated FOV offset survives reload");
        passed &= Check((*persistedReader.Snapshot())[1].id == "wide",
            "created preset survives reload");
    }
    const auto reloadResult = repository.Reload();
    passed &= Check(reloadResult.succeeded && reloadResult.presetCount == 2,
        "repository reloads its storage path");

    passed &= Check(WriteText(validPath, R"json({ "schemaVersion": 5, "presets": [)json"),
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
        "recovered-missing",
    }).succeeded, "missing preset file can be recovered by creating a preset");
    RemoveFile(missingPath);

    const auto corruptRecoveryPath = NextTemporaryPath();
    passed &= Check(WriteText(
        corruptRecoveryPath,
        R"json({ "schemaVersion": 5, "presets": [)json"),
        "corrupt recovery fixture can be written");
    PresetRepository corruptRecoveryRepository;
    passed &= Check(!corruptRecoveryRepository.LoadFromFile(corruptRecoveryPath).succeeded,
        "corrupt initial preset file is reported");
    passed &= Check(corruptRecoveryRepository.Create({
        "recovered-corrupt",
        { {}, { 0.0F, 0.0F, 200.0F } },
        "recovered-corrupt",
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

    const std::array<std::pair<std::string_view, std::string_view>, 19> invalidDocuments{{
        { "broken JSON", R"json({ "schemaVersion": 5, "presets": [)json" },
        { "unknown schema version", R"json({"schemaVersion":6,"presets":[]})json" },
        { "old schema version", R"json({"schemaVersion":4,"presets":[{"id":"x","name":"Name","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "non-array presets", R"json({"schemaVersion":5,"presets":{}})json" },
        { "missing name", R"json({"schemaVersion":5,"presets":[{"id":"x","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "empty name", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "name wrong type", R"json({"schemaVersion":5,"presets":[{"id":"x","name":7,"framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "name too long", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "empty id", R"json({"schemaVersion":5,"presets":[{"id":"","name":"Name","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "missing framing member", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "wrong framing type", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":"zero","up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "missing orbit", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":0,"up":0},"fovOffsetDegrees":0}]})json" },
        { "yaw outside range", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":181,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "zero distance", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":0},"fovOffsetDegrees":0}]})json" },
        { "missing FOV", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100}}]})json" },
        { "FOV outside range", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":161}]})json" },
        { "FOV wrong type", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":"wide"}]})json" },
        { "duplicate id", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0},{"id":"x","name":"Name","framingOffset":{"right":0,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
        { "non-finite numeric result", R"json({"schemaVersion":5,"presets":[{"id":"x","name":"Name","framingOffset":{"right":1e9999,"up":0},"orbit":{"yawDegrees":0,"pitchDegrees":0,"distance":100},"fovOffsetDegrees":0}]})json" },
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
    passed &= Check(WriteText(emptyPath, R"json({ "schemaVersion": 5, "presets": [] })json"),
        "empty preset fixture can be written");
    PresetRepository emptyRepository;
    const auto emptyLoad = emptyRepository.LoadFromFile(emptyPath);
    passed &= Check(emptyLoad.succeeded && emptyLoad.presetCount == 0,
        "empty preset array is a valid empty snapshot");
    RemoveFile(emptyPath);

    return passed ? 0 : 1;
}
