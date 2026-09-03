#include "SceneCamera.h"

#include "runtime/CameraPoseAdapter.h"

namespace ssc
{
    namespace
    {
        constexpr auto kMaximumSceneDuration = std::chrono::minutes{ 30 };
        constexpr auto kAnimationChangeDelay = std::chrono::seconds{ 1 };

        [[nodiscard]] core::Vec3 ToCore(const runtime::Vec3& a_value) noexcept
        {
            return { a_value.x, a_value.y, a_value.z };
        }

        [[nodiscard]] runtime::Vec3 ToRuntime(const core::Vec3& a_value) noexcept
        {
            return { a_value.x, a_value.y, a_value.z };
        }

        [[nodiscard]] std::string_view ApplyResultName(
            runtime::CameraApplyResult a_result) noexcept
        {
            switch (a_result) {
            case runtime::CameraApplyResult::kApplied:
                return "applied"sv;
            case runtime::CameraApplyResult::kUnsupportedState:
                return "unsupported camera state"sv;
            case runtime::CameraApplyResult::kMissingCamera:
                return "missing camera"sv;
            case runtime::CameraApplyResult::kNotOwner:
                return "camera ownership lost"sv;
            case runtime::CameraApplyResult::kWrongThread:
                return "wrong SmoothCam API thread"sv;
            case runtime::CameraApplyResult::kUpdatePathUnavailable:
                return "camera update hook unavailable"sv;
            default:
                return "unknown failure"sv;
            }
        }
    }

    SceneCamera* SceneCamera::GetSingleton() noexcept
    {
        static SceneCamera singleton;
        return std::addressof(singleton);
    }

    void SceneCamera::Configure(
        runtime::ISceneSource& a_sceneSource,
        runtime::IPresetProvider& a_presetProvider,
        runtime::PresetPreviewService& a_previewService,
        runtime::ICameraControl& a_cameraControl,
        runtime::IDebugVisualization& a_debugVisualization) noexcept
    {
        sceneSource_ = std::addressof(a_sceneSource);
        presetProvider_ = std::addressof(a_presetProvider);
        previewService_ = std::addressof(a_previewService);
        cameraControl_ = std::addressof(a_cameraControl);
        debugVisualization_ = std::addressof(a_debugVisualization);
    }

    bool SceneCamera::NeedsUpdate() const noexcept
    {
        const auto previewChanged = previewService_ &&
            previewService_->Request() != appliedPreviewRequest_;
        return anchorCapturePending_.load(std::memory_order_acquire) ||
               cameraPoseActive_.load(std::memory_order_acquire) ||
               resetRequested_.load(std::memory_order_acquire) || previewChanged;
    }

    void SceneCamera::HandleSceneEvent(const runtime::SceneEvent& a_event)
    {
        switch (a_event.type) {
        case runtime::SceneEventType::kAnimationStarting:
            OnAnimationStarting(a_event);
            break;
        case runtime::SceneEventType::kAnimationStart:
            OnAnimationStart(a_event);
            break;
        case runtime::SceneEventType::kAnimationChange:
            OnAnimationChange(a_event);
            break;
        case runtime::SceneEventType::kAnimationEnding:
            OnAnimationEnding(a_event);
            break;
        case runtime::SceneEventType::kAnimationEnd:
            OnAnimationEnd(a_event);
            break;
        }
    }

    void SceneCamera::Prepare(
        const runtime::SceneKey& a_key,
        const runtime::SceneParticipantSnapshot& a_participants)
    {
        if (session_.IsActive()) {
            if (!session_.Matches(a_key)) {
                logger::info("Ignoring overlapping scene {:08X}/{} while another player scene is active",
                    a_key.sourceID, a_key.instanceID);
            }
            return;
        }

        if (!a_participants.ContainsPlayer()) {
            logger::info("Ignoring scene {:08X}/{}: player is not a participant",
                a_key.sourceID, a_key.instanceID);
            return;
        }

        if (!session_.Prepare(a_key)) {
            logger::info("Ignoring overlapping scene {:08X}/{} while another scene is pending",
                a_key.sourceID, a_key.instanceID);
            return;
        }
        participants_ = a_participants;
        logger::info("Scene {:08X}/{} prepared with {} participant(s)",
            a_key.sourceID, a_key.instanceID, participants_.Count());
        if (participants_.WasTruncated()) {
            logger::warn("Scene participant list exceeded {}; extra actors were ignored",
                runtime::SceneParticipantSnapshot::kCapacity);
        }
    }

    void SceneCamera::OnAnimationStarting(const runtime::SceneEvent& a_event)
    {
        ApplyRequestedReset();
        logger::info("AnimationStarting {:08X}/{}", a_event.key.sourceID, a_event.key.instanceID);
        Prepare(a_event.key, a_event.participants);
    }

    void SceneCamera::OnAnimationStart(const runtime::SceneEvent& a_event)
    {
        ApplyRequestedReset();
        logger::info("AnimationStart {:08X}/{}", a_event.key.sourceID, a_event.key.instanceID);

        if (session_.IsIdle()) {
            Prepare(a_event.key, a_event.participants);
        }
        if (!session_.IsPreparing() || !session_.Matches(a_event.key)) {
            return;
        }

        if (!a_event.participants.ContainsPlayer()) {
            logger::warn("Prepared scene no longer contains the player; abandoning camera switch");
            Clear();
            return;
        }
        participants_ = a_event.participants;

        if (!session_.Activate(a_event.key)) {
            return;
        }
        anchorCaptureReadyAt_ = std::chrono::steady_clock::now();
        anchorCapturePending_.store(true, std::memory_order_release);
        activeSince_ = anchorCaptureReadyAt_;
        logger::info("Scene anchor capture pending for {:08X}/{}",
            a_event.key.sourceID, a_event.key.instanceID);
    }

    void SceneCamera::OnAnimationChange(const runtime::SceneEvent& a_event)
    {
        ApplyRequestedReset();
        logger::info("AnimationChange {:08X}/{}", a_event.key.sourceID, a_event.key.instanceID);

        if (!session_.IsActive() || !session_.Matches(a_event.key)) {
            logger::info("Ignoring stale AnimationChange {:08X}/{}",
                a_event.key.sourceID, a_event.key.instanceID);
            return;
        }

        anchorCaptureReadyAt_ = std::chrono::steady_clock::now() + kAnimationChangeDelay;
        anchorCapturePending_.store(true, std::memory_order_release);
        logger::info("Scene anchor recapture scheduled in {} ms for {:08X}/{}",
            std::chrono::duration_cast<std::chrono::milliseconds>(kAnimationChangeDelay).count(),
            a_event.key.sourceID,
            a_event.key.instanceID);
    }

    void SceneCamera::OnAnimationEnding(const runtime::SceneEvent& a_event)
    {
        ApplyRequestedReset();
        logger::info("AnimationEnding {:08X}/{}", a_event.key.sourceID, a_event.key.instanceID);
        if (session_.Matches(a_event.key)) {
            Restore("matching AnimationEnding"sv);
        }
    }

    void SceneCamera::OnAnimationEnd(const runtime::SceneEvent& a_event)
    {
        ApplyRequestedReset();
        logger::info("AnimationEnd {:08X}/{}", a_event.key.sourceID, a_event.key.instanceID);
        if (!session_.Matches(a_event.key)) {
            logger::info("Ignoring stale AnimationEnd {:08X}/{}",
                a_event.key.sourceID, a_event.key.instanceID);
            return;
        }
        Restore("matching AnimationEnd"sv);
    }

    void SceneCamera::Update()
    {
        ApplyRequestedReset();
        if (!session_.IsActive()) {
            appliedPreviewRequest_ = previewService_ ? previewService_->Request() : nullptr;
            return;
        }
        if (debugVisualization_) {
            debugVisualization_->Update();
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - activeSince_ > kMaximumSceneDuration) {
            Restore("scene watchdog timeout"sv);
            return;
        }

        const auto previewRequest = previewService_ ? previewService_->Request() : nullptr;
        if (anchor_ && previewRequest != appliedPreviewRequest_) {
            if (!ApplyRequestedPreset(previewRequest, true)) {
                return;
            }
        }

        if (cameraPose_) {
            if (!cameraControl_ || !cameraControl_->StillOwnsCamera()) {
                logger::warn("SmoothCam camera ownership was lost; preset pose was discarded");
                Restore("camera ownership check failed"sv);
                return;
            } else {
                const auto applyResult = cameraControl_->Apply(*cameraPose_);
                if (applyResult != runtime::CameraApplyResult::kApplied) {
                    logger::error("Could not maintain preset camera pose: {}",
                        ApplyResultName(applyResult));
                    Restore("preset camera pose could not be maintained"sv);
                    return;
                }
            }
        }
        if (!anchorCapturePending_.load(std::memory_order_acquire)) {
            return;
        }
        if (now < anchorCaptureReadyAt_) {
            return;
        }

        std::array<runtime::Vec3, runtime::SceneParticipantSnapshot::kCapacity> runtimePelvisPositions{};
        const auto samples = sceneSource_ ?
            sceneSource_->CollectAnchorInput(participants_, runtimePelvisPositions) : std::nullopt;
        if (!samples) {
            Restore("scene participant Pelvis nodes are unavailable"sv);
            return;
        }

        std::array<core::Vec3, runtime::SceneParticipantSnapshot::kCapacity> corePelvisPositions{};
        for (std::size_t index = 0; index < samples->participantPelvisPositions.size(); ++index) {
            corePelvisPositions[index] = ToCore(samples->participantPelvisPositions[index]);
        }
        const auto playerPelvis = samples->playerPelvisPosition ?
            std::optional{ ToCore(*samples->playerPelvisPosition) } : std::nullopt;
        const auto playerForward = samples->playerForward ?
            std::optional{ ToCore(*samples->playerForward) } : std::nullopt;

        anchor_ = anchorCalculator_.Evaluate({
            std::span<const core::Vec3>{
                corePelvisPositions.data(), samples->participantPelvisPositions.size() },
            playerPelvis,
            playerForward,
        });
        if (!anchor_) {
            Restore("could not derive a stable scene anchor"sv);
            return;
        }

        anchorCapturePending_.store(false, std::memory_order_release);
        anchorCaptureReadyAt_ = {};
        if (debugVisualization_ && !debugVisualization_->ShowAnchor(
                ToRuntime(anchor_->position), ToRuntime(anchor_->forward))) {
            logger::warn("Scene anchor debug marker could not be displayed");
        }
        logger::info(
            "Scene anchor fixed at ({:.2f}, {:.2f}, {:.2f}), forward ({:.3f}, {:.3f}, {:.3f})",
            anchor_->position.x,
            anchor_->position.y,
            anchor_->position.z,
            anchor_->forward.x,
            anchor_->forward.y,
            anchor_->forward.z);

        const auto request = previewService_ ? previewService_->Request() : nullptr;
        static_cast<void>(ApplyRequestedPreset(request, false));
    }

    std::optional<runtime::CameraPreset> SceneCamera::ResolvePreset(
        const std::shared_ptr<const runtime::PresetPreviewRequest>& a_request) const
    {
        if (a_request && a_request->preset) {
            return a_request->preset;
        }
        const auto snapshot = presetProvider_ ? presetProvider_->Snapshot() : nullptr;
        if (!snapshot || snapshot->empty()) {
            return std::nullopt;
        }
        return snapshot->front();
    }

    bool SceneCamera::ApplyRequestedPreset(
        const std::shared_ptr<const runtime::PresetPreviewRequest>& a_request,
        bool a_liveEdit)
    {
        const auto preset = ResolvePreset(a_request);
        if (!preset) {
            appliedPreviewRequest_ = a_request;
            PublishPreviewFeedback(false, "No camera preset is available");
            if (cameraControl_ && cameraControl_->OwnsCamera()) {
                Restore("no camera preset remains"sv);
                return false;
            }
            if (!a_liveEdit) {
                logger::warn("No valid camera preset is available; SmoothCam remains in control");
            }
            return true;
        }

        if (!ApplyPreset(*preset, a_liveEdit)) {
            appliedPreviewRequest_ = previewService_ ? previewService_->Request() : a_request;
            return false;
        }
        appliedPreviewRequest_ = a_request;
        return true;
    }

    bool SceneCamera::ApplyPreset(
        const runtime::CameraPreset& a_preset,
        bool a_liveEdit)
    {
        if (!anchor_) {
            PublishPreviewFeedback(false, "Scene anchor is not available");
            return false;
        }
        const auto corePose = poseCalculator_.Evaluate(
            *anchor_,
            {
                a_preset.offset.right,
                a_preset.offset.forward,
                a_preset.offset.up,
            });
        if (!corePose) {
            PublishPreviewFeedback(false, "Preset cannot produce a camera pose");
            if (!a_liveEdit) {
                logger::error("Camera pose generation failed for preset '{}'", a_preset.id);
                Restore("camera pose generation failed"sv);
            }
            return false;
        }

        if (!cameraControl_) {
            logger::error("Camera control is unavailable; SmoothCam remains in control");
            PublishPreviewFeedback(false, "Camera control is unavailable");
            return false;
        }
        if (cameraControl_->OwnsCamera() && !cameraControl_->StillOwnsCamera()) {
            logger::warn("SmoothCam camera ownership was lost before pose replacement");
            PublishPreviewFeedback(false, "SmoothCam camera ownership was lost");
            Restore("camera ownership check failed before pose replacement"sv);
            return false;
        }

        const auto acquiredNow = !cameraControl_->OwnsCamera();
        if (acquiredNow) {
            if (!cameraControl_->CanAcquire()) {
                logger::warn("SmoothCam cannot currently yield camera control");
                PublishPreviewFeedback(false, "SmoothCam cannot currently yield camera control");
                return false;
            }
            if (!cameraControl_->Acquire()) {
                logger::warn("SmoothCam camera-control acquisition failed");
                PublishPreviewFeedback(false, "SmoothCam camera-control acquisition failed");
                return false;
            }
        }

        const auto runtimePose = runtime::ToRuntimeCameraPose(*corePose);
        const auto applyResult = cameraControl_->Apply(runtimePose);
        if (applyResult != runtime::CameraApplyResult::kApplied) {
            logger::error("Could not apply camera preset '{}': {}",
                a_preset.id,
                ApplyResultName(applyResult));
            PublishPreviewFeedback(false, std::string{ ApplyResultName(applyResult) });
            Restore(acquiredNow ?
                "initial camera pose apply failed"sv :
                "replacement camera pose apply failed"sv);
            return false;
        }

        cameraPose_ = runtimePose;
        cameraPoseActive_.store(true, std::memory_order_release);
        const auto extracted = poseCalculator_.ExtractOffset(
            *anchor_,
            ToCore(runtimePose.position));
        PublishPreviewFeedback(
            true,
            a_liveEdit ? "Live preview" : "Preset active",
            extracted ? std::optional{ runtime::PresetOffset{
                extracted->right,
                extracted->forward,
                extracted->up } } : std::nullopt);
        if (a_liveEdit) {
            logger::debug("Camera preset '{}' live preview applied", a_preset.id);
        } else {
            logger::info(
                "Camera preset '{}' applied at ({:.2f}, {:.2f}, {:.2f})",
                a_preset.id,
                runtimePose.position.x,
                runtimePose.position.y,
                runtimePose.position.z);
        }
        return true;
    }

    void SceneCamera::PublishPreviewFeedback(
        bool a_applied,
        std::string a_message,
        std::optional<runtime::PresetOffset> a_offset)
    {
        if (!previewService_) {
            return;
        }
        previewService_->PublishFeedback({
            session_.IsActive(),
            a_applied,
            std::move(a_offset),
            std::move(a_message),
        });
    }

    void SceneCamera::Restore(std::string_view a_reason)
    {
        const auto hadSession = !session_.IsIdle();
        const auto hadCameraOwnership = cameraControl_ && cameraControl_->OwnsCamera();
        if (!hadSession && !hadCameraOwnership) {
            return;
        }

        if (hadCameraOwnership) {
            const auto releaseResult = cameraControl_->Release();
            if (releaseResult == runtime::CameraReleaseResult::kWrongThread) {
                resetRequested_.store(true, std::memory_order_release);
                return;
            }
            if (releaseResult == runtime::CameraReleaseResult::kFailed) {
                logger::error("Camera release did not complete; reset will be retried");
                resetRequested_.store(true, std::memory_order_release);
                return;
            }
        }
        if (hadSession) {
            session_.BeginRestore();
            logger::info("Discarding scene anchor: {}", a_reason);
        }
        Clear();
        logger::info("Scene camera IDLE");
    }

    void SceneCamera::Reset(std::string_view a_reason)
    {
        resetRequested_.store(false, std::memory_order_release);
        Restore(a_reason);
    }

    void SceneCamera::RequestReset() noexcept
    {
        resetRequested_.store(true, std::memory_order_release);
    }

    void SceneCamera::EmergencyReset() noexcept
    {
        if (cameraControl_ && !cameraControl_->EmergencyRelease()) {
            resetRequested_.store(true, std::memory_order_release);
            return;
        }
        resetRequested_.store(false, std::memory_order_release);
        Clear();
    }

    void SceneCamera::ApplyRequestedReset()
    {
        if (!resetRequested_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (cameraControl_ && cameraControl_->OwnsCamera()) {
            Restore("lifecycle reset"sv);
        } else {
            Clear();
        }
    }

    void SceneCamera::Clear() noexcept
    {
        participants_ = {};
        anchorCapturePending_.store(false, std::memory_order_release);
        cameraPoseActive_.store(false, std::memory_order_release);
        anchorCaptureReadyAt_ = {};
        if (debugVisualization_) {
            debugVisualization_->HideAnchor();
        }
        cameraPose_.reset();
        appliedPreviewRequest_.reset();
        anchor_.reset();
        session_.Clear();
        if (previewService_) {
            previewService_->ClearPreview();
        }
        PublishPreviewFeedback(false, "No active player scene");
    }
}
