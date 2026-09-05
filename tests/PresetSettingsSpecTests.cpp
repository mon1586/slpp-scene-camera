#include "core/CameraPose.h"
#include "runtime/CameraFOV.h"
#include "runtime/EditHotkeySettings.h"
#include "runtime/PresetPreviewService.h"
#include "runtime/PresetRepository.h"
#include "ui/PresetEditorPolicy.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
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
        return Check(std::abs(a_actual - a_expected) < 0.001F, a_message);
    }

    float Dot(const ssc::core::Vec3& a_left, const ssc::core::Vec3& a_right)
    {
        return a_left.x * a_right.x + a_left.y * a_right.y + a_left.z * a_right.z;
    }

    float Distance(const ssc::core::Vec3& a_left, const ssc::core::Vec3& a_right)
    {
        return std::hypot(
            a_left.x - a_right.x,
            a_left.y - a_right.y,
            a_left.z - a_right.z);
    }

    ssc::core::CameraRig ToRig(const ssc::runtime::PresetTransform& a_transform)
    {
        return {
            {
                a_transform.framingOffset.right,
                a_transform.framingOffset.up,
            },
            {
                a_transform.orbit.yawDegrees,
                a_transform.orbit.pitchDegrees,
                a_transform.orbit.distance,
            },
        };
    }

    std::filesystem::path TemporaryPresetPath()
    {
        const auto suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        return std::filesystem::temp_directory_path() /
               ("ssc-settings-spec-" + suffix + ".json");
    }
}

int main()
{
    using ssc::core::CameraPoseCalculator;
    using ssc::core::SceneAnchor;
    using ssc::core::Vec3;
    using ssc::runtime::PresetPreviewFeedback;
    using ssc::runtime::PresetTransform;

    bool passed = true;

    passed &= Check(ssc::ui::kEditorPausesGame,
        "preset editor pauses game time while it owns input");
    const auto defaults = ssc::ui::kNewPresetTransform;
    passed &= CheckNear(defaults.framingOffset.right, 0.0F,
        "New preset starts with Pan Right 0");
    passed &= CheckNear(defaults.framingOffset.up, 60.0F,
        "New preset starts with Pan Up 60");
    passed &= CheckNear(defaults.orbit.yawDegrees, 0.0F,
        "New preset starts with Yaw 0");
    passed &= CheckNear(defaults.orbit.pitchDegrees, 0.0F,
        "New preset starts with Pitch 0");
    passed &= CheckNear(defaults.orbit.distance, 200.0F,
        "New preset starts with Distance 200");
    passed &= CheckNear(defaults.fovOffsetDegrees, 0.0F,
        "New preset starts with FOV Offset 0");
    passed &= CheckNear(ssc::ui::kMinimumYawDegrees, -180.0F,
        "editor yaw minimum is -180 degrees");
    passed &= CheckNear(ssc::ui::kMaximumYawDegrees, 180.0F,
        "editor yaw maximum is 180 degrees");
    passed &= CheckNear(ssc::ui::kMinimumPitchDegrees, -89.9F,
        "editor pitch minimum avoids the lower pole");
    passed &= CheckNear(ssc::ui::kMaximumPitchDegrees, 89.9F,
        "editor pitch maximum avoids the upper pole");
    passed &= CheckNear(ssc::ui::kMinimumDistance, 0.1F,
        "editor distance minimum is 0.1 Skyrim unit");
    passed &= CheckNear(ssc::ui::kMaximumDistance, 100000.0F,
        "editor distance maximum is 100000 Skyrim units");
    passed &= CheckNear(ssc::ui::kMinimumFOVOffsetDegrees, -160.0F,
        "editor FOV offset minimum is -160 degrees");
    passed &= CheckNear(ssc::ui::kMaximumFOVOffsetDegrees, 160.0F,
        "editor FOV offset maximum is 160 degrees");
    passed &= Check(ssc::runtime::ValidatePresetTransform(defaults).empty(),
        "New preset defaults are valid preview values");
    auto invalidFOV = defaults;
    invalidFOV.fovOffsetDegrees = 160.1F;
    passed &= Check(!ssc::runtime::ValidatePresetTransform(invalidFOV).empty(),
        "FOV offsets outside the editor range are rejected");
    invalidFOV.fovOffsetDegrees = std::numeric_limits<float>::quiet_NaN();
    passed &= Check(!ssc::runtime::ValidatePresetTransform(invalidFOV).empty(),
        "non-finite FOV offsets are rejected");
    const auto unchangedFOVOffset = ssc::runtime::ResolveFOVOffset(70.0F, -15.0F);
    passed &= Check(unchangedFOVOffset.has_value(),
        "a finite FOV offset resolves against the normal FOV");
    if (unchangedFOVOffset) {
        passed &= CheckNear(*unchangedFOVOffset, -15.0F,
            "an in-range requested FOV offset is retained");
    }
    const auto minimumFOVOffset = ssc::runtime::ResolveFOVOffset(70.0F, -100.0F);
    passed &= Check(minimumFOVOffset.has_value(),
        "an excessively narrow FOV offset resolves");
    if (minimumFOVOffset) {
        passed &= CheckNear(*minimumFOVOffset, -60.0F,
            "the applied FOV cannot fall below 10 degrees");
    }
    const auto maximumFOVOffset = ssc::runtime::ResolveFOVOffset(70.0F, 120.0F);
    passed &= Check(maximumFOVOffset.has_value(),
        "an excessively wide FOV offset resolves");
    if (maximumFOVOffset) {
        passed &= CheckNear(*maximumFOVOffset, 100.0F,
            "the applied FOV cannot exceed 170 degrees");
    }
    passed &= Check(!ssc::runtime::ResolveFOVOffset(
        std::numeric_limits<float>::infinity(), 0.0F),
        "a non-finite normal FOV is rejected");

    const SceneAnchor anchor{ { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } };
    CameraPoseCalculator calculator;

    const auto defaultPose = calculator.Evaluate(anchor, ToRig(defaults));
    passed &= Check(defaultPose.has_value(), "New preset defaults produce a camera pose");
    if (defaultPose) {
        passed &= CheckNear(defaultPose->position.x, 0.0F,
            "Yaw 0 stays centered horizontally");
        passed &= CheckNear(defaultPose->position.y, -200.0F,
            "Yaw 0 places the camera behind anchor forward");
        passed &= CheckNear(defaultPose->position.z, 60.0F,
            "default Pan Up moves camera and framing center upward");
    }

    PresetTransform panRight{ { 35.0F, 0.0F }, { 0.0F, 0.0F, 200.0F } };
    const auto rightPose = calculator.Evaluate(anchor, ToRig(panRight));
    passed &= Check(rightPose.has_value(), "Pan Right produces a camera pose");
    if (rightPose) {
        const Vec3 cameraToAnchor{
            -rightPose->position.x,
            -rightPose->position.y,
            -rightPose->position.z,
        };
        passed &= Check(rightPose->position.x > 0.0F,
            "increasing Pan Right moves the camera to screen right");
        passed &= CheckNear(Dot(cameraToAnchor, rightPose->basis.right), -35.0F,
            "increasing Pan Right places the anchor left on screen");
    }

    PresetTransform panUp{ { 0.0F, 45.0F }, { 0.0F, 0.0F, 200.0F } };
    const auto upPose = calculator.Evaluate(anchor, ToRig(panUp));
    passed &= Check(upPose.has_value(), "Pan Up produces a camera pose");
    if (upPose) {
        const Vec3 cameraToAnchor{
            -upPose->position.x,
            -upPose->position.y,
            -upPose->position.z,
        };
        passed &= Check(upPose->position.z > 0.0F,
            "increasing Pan Up moves the camera upward");
        passed &= CheckNear(Dot(cameraToAnchor, upPose->basis.up), -45.0F,
            "increasing Pan Up places the anchor lower on screen");
    }

    PresetTransform positiveYaw{ {}, { 90.0F, 0.0F, 200.0F } };
    const auto yawPose = calculator.Evaluate(anchor, ToRig(positiveYaw));
    passed &= Check(yawPose.has_value(), "positive Yaw produces a camera pose");
    if (yawPose) {
        passed &= CheckNear(yawPose->position.x, 200.0F,
            "positive Yaw orbits toward anchor right");
        passed &= CheckNear(yawPose->position.y, 0.0F,
            "90 degree Yaw completes a quarter orbit");
    }

    PresetTransform positivePitch{ {}, { 0.0F, 30.0F, 200.0F } };
    const auto highPose = calculator.Evaluate(anchor, ToRig(positivePitch));
    passed &= Check(highPose.has_value(), "positive Pitch produces a camera pose");
    if (highPose) {
        passed &= Check(highPose->position.z > anchor.position.z,
            "positive Pitch places the camera above the framing center");
    }
    PresetTransform negativePitch{ {}, { 0.0F, -30.0F, 200.0F } };
    const auto lowPose = calculator.Evaluate(anchor, ToRig(negativePitch));
    passed &= Check(lowPose.has_value(), "negative Pitch produces a camera pose");
    if (lowPose) {
        passed &= Check(lowPose->position.z < anchor.position.z,
            "negative Pitch places the camera below the framing center");
    }

    PresetTransform nearTransform{ {}, { 0.0F, 0.0F, 100.0F } };
    PresetTransform farTransform{ {}, { 0.0F, 0.0F, 350.0F } };
    const auto nearPose = calculator.Evaluate(anchor, ToRig(nearTransform));
    const auto farPose = calculator.Evaluate(anchor, ToRig(farTransform));
    passed &= Check(nearPose && farPose, "near and far Distance values produce camera poses");
    if (nearPose && farPose) {
        passed &= CheckNear(Distance(nearPose->position, anchor.position), 100.0F,
            "smaller Distance moves toward the framing center");
        passed &= CheckNear(Distance(farPose->position, anchor.position), 350.0F,
            "larger Distance moves away from the framing center");
    }

    PresetTransform combined{
        { 25.0F, 40.0F }, { 55.0F, 25.0F, 250.0F }, -12.5F };
    const auto combinedPose = calculator.Evaluate(anchor, ToRig(combined));
    passed &= Check(combinedPose.has_value(),
        "Pan, Yaw, Pitch, and Distance combine into a camera pose");
    if (combinedPose) {
        const Vec3 cameraToAnchor{
            -combinedPose->position.x,
            -combinedPose->position.y,
            -combinedPose->position.z,
        };
        passed &= CheckNear(Dot(cameraToAnchor, combinedPose->basis.right), -25.0F,
            "Yaw and Pitch preserve Pan Right as a screen-space offset");
        passed &= CheckNear(Dot(cameraToAnchor, combinedPose->basis.up), -40.0F,
            "Yaw and Pitch preserve Pan Up as a screen-space offset");
        passed &= CheckNear(Dot(cameraToAnchor, combinedPose->basis.viewForward), 250.0F,
            "Pan values do not replace Distance with a forward control");
    }

    const PresetPreviewFeedback unavailable{};
    passed &= Check(!ssc::ui::CanStartPresetPreview(&unavailable),
        "dashboard cannot start preview without an active scene");
    const PresetPreviewFeedback emptyRecovery{
        true, false, true, 0, std::nullopt, "Recovery preview available" };
    passed &= Check(ssc::ui::CanStartPresetPreview(&emptyRecovery),
        "dashboard can start a preview when camera acquisition is possible");
    const PresetPreviewFeedback activePreview{
        true, true, true, 7, defaults, "Preview active" };
    passed &= Check(ssc::ui::CanStartPresetPreview(&activePreview),
        "an already-applied scene camera can enter preview editing");
    passed &= Check(ssc::ui::CanEditPreset(&activePreview, true),
        "editing is available while the scene preview session is open");
    passed &= Check(!ssc::ui::CanEditPreset(&activePreview, false),
        "editing stops when the preview session closes");
    const PresetPreviewFeedback ownershipLost{
        false, false, false, 0, defaults, "Camera ownership lost" };
    passed &= Check(!ssc::ui::CanEditPreset(&ownershipLost, true),
        "camera ownership loss immediately locks editing");
    passed &= Check(!ssc::ui::CanStartPresetPreview(&ownershipLost),
        "an unavailable camera cannot start preview editing");

    ssc::core::VisibilityEvaluationSnapshot visibilitySnapshot;
    ssc::core::CameraCandidateVisibility usableCandidate;
    usableCandidate.presetID = "usable";
    usableCandidate.usable = true;
    usableCandidate.visibleParticipantCount = 2;
    usableCandidate.visiblePointCount = 5;
    usableCandidate.availablePointCount = 6;
    usableCandidate.participants.resize(2);
    visibilitySnapshot.candidates.push_back(usableCandidate);
    ssc::core::CameraCandidateVisibility blockedCandidate;
    blockedCandidate.presetID = "blocked";
    blockedCandidate.visibleParticipantCount = 1;
    blockedCandidate.visiblePointCount = 3;
    blockedCandidate.availablePointCount = 5;
    blockedCandidate.failureReason =
        ssc::core::CandidateFailureReason::kParticipantNotVisible;
    blockedCandidate.participants.resize(2);
    visibilitySnapshot.candidates.push_back(blockedCandidate);
    visibilitySnapshot.selectedPresetID = "usable";

    auto toolbarEvaluation =
        std::make_shared<const ssc::core::VisibilityEvaluationSnapshot>(visibilitySnapshot);
    const PresetPreviewFeedback toolbarFeedback{
        true, true, true, 0, defaults, "Scene camera active", toolbarEvaluation };
    const auto currentPresetID = ssc::ui::CurrentPresetID(&toolbarFeedback);
    passed &= Check(currentPresetID && *currentPresetID == "usable",
        "scene toolbar identifies the preset currently applied to the camera");
    passed &= Check(ssc::ui::ShouldShowSceneToolbar(&toolbarFeedback, false, false),
        "scene toolbar remains visible during an active player scene");
    passed &= Check(!ssc::ui::ShouldShowSceneToolbar(&toolbarFeedback, true, false),
        "scene toolbar hides while the preset editor owns the preview session");
    passed &= Check(!ssc::ui::ShouldShowSceneToolbar(&toolbarFeedback, false, true),
        "scene toolbar hides behind a blocking menu");
    passed &= Check(ssc::ui::CanEditCurrentPreset(&toolbarFeedback),
        "scene toolbar can edit the preset currently applied to the camera");
    passed &= Check(ssc::ui::CanStartDashboardPreview(&toolbarFeedback, false),
        "dashboard preview can start outside hotkey assignment");
    passed &= Check(!ssc::ui::CanStartDashboardPreview(&toolbarFeedback, true),
        "dashboard preview cannot start during hotkey assignment");
    passed &= Check(ssc::ui::ShouldHandleEditHotkey(
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kDefaultEditHotkey,
        false,
        false,
        &toolbarFeedback),
        "the edit hotkey is captured when the current preset can be edited");
    passed &= Check(!ssc::ui::ShouldHandleEditHotkey(
        0x41,
        ssc::runtime::kDefaultEditHotkey,
        false,
        false,
        &toolbarFeedback),
        "unrelated keyboard input is not captured by the edit hotkey");
    passed &= Check(!ssc::ui::ShouldHandleEditHotkey(
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kDefaultEditHotkey,
        false,
        true,
        &toolbarFeedback),
        "the edit hotkey is not captured behind another blocking window");
    passed &= Check(!ssc::ui::CanOpenCurrentPresetEditor(&toolbarFeedback, true),
        "a queued edit request cannot open behind another blocking window");

    const auto assignedDown = ssc::ui::DecideEditHotkeyInput(
        ssc::ui::EditHotkeyButtonPhase::kDown,
        0x41,
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kEscapeKeyboardKey,
        0,
        true,
        false);
    passed &= Check(assignedDown.consume && assignedDown.finishAssignment &&
        !assignedDown.cancelAssignment && assignedDown.capturedKey == 0x41 &&
        assignedDown.ownedKey == 0x41,
        "hotkey assignment captures the next key-down as one owned gesture");
    const auto assignedHeld = ssc::ui::DecideEditHotkeyInput(
        ssc::ui::EditHotkeyButtonPhase::kHeld,
        0x41,
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kEscapeKeyboardKey,
        assignedDown.ownedKey,
        false,
        false);
    passed &= Check(assignedHeld.consume && !assignedHeld.toggleEditor &&
        assignedHeld.ownedKey == 0x41,
        "the held phase of an assigned key remains captured");
    const auto assignedUp = ssc::ui::DecideEditHotkeyInput(
        ssc::ui::EditHotkeyButtonPhase::kUp,
        0x41,
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kEscapeKeyboardKey,
        assignedHeld.ownedKey,
        false,
        false);
    passed &= Check(assignedUp.consume && assignedUp.ownedKey == 0,
        "the release phase is captured and then releases gesture ownership");
    const auto escapeAssignment = ssc::ui::DecideEditHotkeyInput(
        ssc::ui::EditHotkeyButtonPhase::kDown,
        ssc::runtime::kEscapeKeyboardKey,
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kEscapeKeyboardKey,
        0,
        true,
        false);
    passed &= Check(escapeAssignment.consume && escapeAssignment.finishAssignment &&
        escapeAssignment.cancelAssignment && escapeAssignment.capturedKey == 0,
        "Escape cancels hotkey assignment without assigning itself");
    const auto editDown = ssc::ui::DecideEditHotkeyInput(
        ssc::ui::EditHotkeyButtonPhase::kDown,
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kEscapeKeyboardKey,
        0,
        false,
        true);
    passed &= Check(editDown.consume && editDown.toggleEditor &&
        editDown.ownedKey == ssc::runtime::kDefaultEditHotkey,
        "the edit hotkey toggles once and owns its complete gesture");

    auto noSelectionEvaluation = visibilitySnapshot;
    noSelectionEvaluation.selectedPresetID.reset();
    auto noSelectionSnapshot =
        std::make_shared<const ssc::core::VisibilityEvaluationSnapshot>(noSelectionEvaluation);
    const PresetPreviewFeedback noSelectionFeedback{
        true, false, true, 0, std::nullopt, "No usable preset", noSelectionSnapshot };
    passed &= Check(ssc::ui::ShouldShowSceneToolbar(
        &noSelectionFeedback, false, false),
        "scene toolbar reports an active scene even when no preset is selected");
    passed &= Check(!ssc::ui::CanEditCurrentPreset(&noSelectionFeedback),
        "scene toolbar disables Edit when no preset is applied");
    passed &= Check(!ssc::ui::ShouldHandleEditHotkey(
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kDefaultEditHotkey,
        false,
        false,
        &noSelectionFeedback),
        "the edit hotkey passes through when no preset is applied");
    passed &= Check(ssc::ui::ShouldHandleEditHotkey(
        ssc::runtime::kDefaultEditHotkey,
        ssc::runtime::kDefaultEditHotkey,
        true,
        true,
        nullptr),
        "the edit hotkey remains available to close the editor");

    const auto usableSummary = ssc::ui::SummarizePresetForScene(
        &visibilitySnapshot, "usable");
    passed &= Check(usableSummary.status == ssc::ui::PresetSceneStatus::kUsable &&
        usableSummary.visibleParticipants == 2 && usableSummary.totalParticipants == 2 &&
        usableSummary.visiblePoints == 5 && usableSummary.availablePoints == 6,
        "dashboard reports participant and point quality for a usable preset");
    const auto blockedSummary = ssc::ui::SummarizePresetForScene(
        &visibilitySnapshot, "blocked");
    passed &= Check(blockedSummary.status == ssc::ui::PresetSceneStatus::kBlocked &&
        blockedSummary.visibleParticipants == 1 && blockedSummary.totalParticipants == 2 &&
        blockedSummary.visiblePoints == 3 && blockedSummary.availablePoints == 5 &&
        blockedSummary.failureReason ==
            ssc::core::CandidateFailureReason::kParticipantNotVisible,
        "dashboard reports quality and failure reason for a blocked preset");
    const auto missingSummary = ssc::ui::SummarizePresetForScene(
        &visibilitySnapshot, "not-evaluated");
    passed &= Check(missingSummary.status == ssc::ui::PresetSceneStatus::kNotEvaluated,
        "dashboard distinguishes a preset that was not evaluated");
    passed &= Check(ssc::ui::CountUsablePresets(&visibilitySnapshot) == 1,
        "dashboard counts only usable presets");

    auto* previewService = ssc::runtime::PresetPreviewService::GetSingleton();
    previewService->EndPreviewSession();
    previewService->BeginPreviewSession();
    const auto oldRevision = previewService->SetPreview(defaults, "default");
    const auto currentRevision = previewService->SetPreview(combined, "combined");
    passed &= Check(currentRevision > oldRevision,
        "each realtime edit receives a newer revision");
    const auto currentRequest = previewService->Request();
    passed &= Check(currentRequest && currentRequest->presetID == "combined",
        "preview request identifies the dashboard preset being edited");
    previewService->PublishFeedback({
        true, true, true, oldRevision, defaults, "Old preview applied" });
    auto feedback = previewService->Feedback();
    passed &= Check(!ssc::ui::CanSavePreset(
        true, true, feedback.get(), currentRevision),
        "Save remains disabled while only an older edit is visible");
    previewService->PublishFeedback({
        true, true, true, currentRevision, combined, "Current preview applied" });
    feedback = previewService->Feedback();
    passed &= Check(ssc::ui::CanSavePreset(
        true, true, feedback.get(), currentRevision),
        "Save is enabled after the latest edit is visible");
    passed &= Check(!ssc::ui::CanSavePreset(
        true, false, feedback.get(), currentRevision),
        "Save is disabled when there are no unsaved changes");
    passed &= Check(!ssc::ui::CanSavePreset(
        false, true, feedback.get(), currentRevision),
        "Save is disabled without a draft");
    previewService->EndPreviewSession();
    previewService->ClearPreview();
    const auto firstClearRequest = previewService->Request();
    previewService->ClearPreview();
    const auto repeatedClearRequest = previewService->Request();
    passed &= Check(firstClearRequest && !firstClearRequest->transform &&
        repeatedClearRequest == firstClearRequest,
        "repeated editor close notifications publish only one preview reset");

    const auto presetPath = TemporaryPresetPath();
    std::error_code ignored;
    static_cast<void>(std::filesystem::remove(presetPath, ignored));
    ssc::runtime::PresetRepository repository;
    passed &= Check(repository.LoadFromFile(presetPath).succeeded,
        "a missing preset file starts as an empty repository");
    passed &= Check(repository.Create({ "only", defaults }).succeeded,
        "a preset can be created from the empty state");
    passed &= Check(repository.Delete("only").succeeded && repository.Snapshot()->empty(),
        "deleting the final preset leaves a valid zero-preset state");
    passed &= Check(repository.Create({ "recovered", combined }).succeeded,
        "New preset recovers after every preset was deleted");
    passed &= Check(repository.Snapshot()->size() == 1 &&
        repository.Snapshot()->front().id == "recovered",
        "the recovered preset becomes the saved selection source");
    static_cast<void>(std::filesystem::remove(presetPath, ignored));

    const auto hotkeyPath = TemporaryPresetPath();
    static_cast<void>(std::filesystem::remove(hotkeyPath, ignored));
    ssc::runtime::EditHotkeySettings hotkeySettings;
    passed &= Check(hotkeySettings.LoadFromFile(hotkeyPath).succeeded &&
        hotkeySettings.EditHotkey() == ssc::runtime::kDefaultEditHotkey,
        "a missing settings file uses F8 as the edit hotkey");
    constexpr std::uint32_t alternateHotkey = 0x41;
    passed &= Check(hotkeySettings.SetEditHotkey(alternateHotkey).succeeded &&
        hotkeySettings.EditHotkey() == alternateHotkey,
        "a changed edit hotkey is saved and becomes active");
    ssc::runtime::EditHotkeySettings reloadedHotkeySettings;
    passed &= Check(reloadedHotkeySettings.LoadFromFile(hotkeyPath).succeeded &&
        reloadedHotkeySettings.EditHotkey() == alternateHotkey,
        "the changed edit hotkey survives a settings reload");
    passed &= Check(!reloadedHotkeySettings.SetEditHotkey(
        ssc::runtime::kEscapeKeyboardKey).succeeded &&
        reloadedHotkeySettings.EditHotkey() == alternateHotkey,
        "Escape cancels assignment instead of replacing the edit hotkey");
    passed &= Check(!ssc::runtime::EditHotkeyName(
        ssc::runtime::kDefaultEditHotkey).empty(),
        "the toolbar can display the assigned edit hotkey name");
    static_cast<void>(std::filesystem::remove(hotkeyPath, ignored));

    if (!passed) {
        return 1;
    }
    std::cout << "Preset settings specification tests passed\n";
    return 0;
}
