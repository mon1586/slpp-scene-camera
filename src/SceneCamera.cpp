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
        runtime::IVisibilityProbe& a_visibilityProbe,
        runtime::IDebugVisualization& a_debugVisualization) noexcept
    {
        sceneSource_ = std::addressof(a_sceneSource);
        presetProvider_ = std::addressof(a_presetProvider);
        previewService_ = std::addressof(a_previewService);
        cameraControl_ = std::addressof(a_cameraControl);
        visibilityProbe_ = std::addressof(a_visibilityProbe);
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

    bool SceneCamera::AllowsUpdateWhilePaused() const noexcept
    {
        return previewService_ && previewService_->PreviewSessionActive();
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
            session_.Clear();
            participants_ = {};
            anchorCapturePending_.store(false, std::memory_order_release);
            anchorCaptureReadyAt_ = {};
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
        const auto previewRequest = previewService_ ? previewService_->Request() : nullptr;
        const auto previewSessionActive = previewService_ &&
            previewService_->PreviewSessionActive();
        if (!session_.IsActive() && !cameraPose_) {
            appliedPreviewRequest_ = previewRequest;
            return;
        }
        if (debugVisualization_) {
            debugVisualization_->Update();
        }
        const auto now = std::chrono::steady_clock::now();
        if (session_.IsActive() && now - activeSince_ > kMaximumSceneDuration) {
            Restore("scene watchdog timeout"sv);
            return;
        }

        const auto previewEnded = anchor_ && !previewSessionActive &&
            previewRequest != appliedPreviewRequest_ && previewRequest &&
            !previewRequest->transform && appliedPreviewRequest_ &&
            appliedPreviewRequest_->transform;
        if (previewEnded && !EvaluateVisibility()) {
            return;
        }

        if (cameraPose_ && !previewSessionActive && !ResolveTransform(previewRequest)) {
            PublishPreviewFeedback(false, "No visible camera preset is available");
            static_cast<void>(ReleaseCamera("preset editor closed with no visible camera preset"sv));
            return;
        }
        if (anchor_ && previewRequest != appliedPreviewRequest_) {
            if (!ApplyRequestedTransform(previewRequest, true)) {
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
        if (!session_.IsActive()) {
            return;
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
        const auto playerPelvisForward = samples->playerPelvisForward ?
            std::optional{ ToCore(*samples->playerPelvisForward) } : std::nullopt;
        const auto playerActorForward = samples->playerActorForward ?
            std::optional{ ToCore(*samples->playerActorForward) } : std::nullopt;

        anchor_ = anchorCalculator_.Evaluate({
            std::span<const core::Vec3>{
                corePelvisPositions.data(), samples->participantPelvisPositions.size() },
            playerPelvisForward,
            playerActorForward,
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

        if (!EvaluateVisibility()) {
            return;
        }

        const auto request = previewService_ ? previewService_->Request() : nullptr;
        static_cast<void>(ApplyRequestedTransform(request, false));
    }

    std::optional<runtime::PresetTransform> SceneCamera::ResolveTransform(
        const std::shared_ptr<const runtime::PresetPreviewRequest>& a_request) const
    {
        if (a_request && a_request->transform) {
            return a_request->transform;
        }
        const auto snapshot = presetProvider_ ? presetProvider_->Snapshot() : nullptr;
        if (!snapshot || snapshot->empty()) {
            return std::nullopt;
        }
        if (!activePresetID_) {
            return std::nullopt;
        }
        const auto preset = std::ranges::find_if(*snapshot, [&](const auto& a_preset) {
            return a_preset.id == *activePresetID_;
        });
        return preset != snapshot->end() ?
            std::optional{ preset->transform } : std::nullopt;
    }

    bool SceneCamera::ApplyRequestedTransform(
        const std::shared_ptr<const runtime::PresetPreviewRequest>& a_request,
        bool a_liveEdit)
    {
        const auto transform = ResolveTransform(a_request);
        if (!transform) {
            appliedPreviewRequest_ = a_request;
            if (cameraControl_ && cameraControl_->OwnsCamera()) {
                if (previewService_ && previewService_->PreviewSessionActive() && cameraPose_) {
                    const auto feedback = previewService_->Feedback();
                    PublishPreviewFeedback(
                        true,
                        "No preset selected; camera held by preset editor",
                        feedback ? feedback->currentTransform : std::nullopt,
                        true,
                        feedback ? feedback->appliedRevision : 0);
                    return true;
                }
                PublishPreviewFeedback(false, "No visible camera preset is available");
                static_cast<void>(ReleaseCamera("no visible camera preset remains"sv));
                return false;
            }
            const auto previewPossible = anchor_.has_value() && cameraControl_ &&
                cameraControl_->CanAcquire();
            const auto message = cameraControl_ && !previewPossible ?
                std::string{ cameraControl_->UnavailableReason() } :
                std::string{ visibilityEvaluation_ && !visibilityEvaluation_->candidates.empty() ?
                    "No camera preset can show every participant" :
                    "No camera preset is available" };
            PublishPreviewFeedback(
                false,
                message,
                std::nullopt,
                previewPossible);
            if (!a_liveEdit) {
                logger::warn("No valid camera preset is available; SmoothCam remains in control");
            }
            return true;
        }

        const auto revision = a_request && a_request->transform ? a_request->revision : 0;
        if (!ApplyTransform(*transform, a_liveEdit, revision)) {
            appliedPreviewRequest_ = previewService_ ? previewService_->Request() : a_request;
            return false;
        }
        if (a_liveEdit && a_request && a_request->transform) {
            static_cast<void>(EvaluatePreviewVisibility(*a_request));
            PublishPreviewFeedback(
                true,
                "Live preview",
                *transform,
                true,
                revision);
        }
        appliedPreviewRequest_ = a_request;
        return true;
    }

    core::CameraCandidateVisibility SceneCamera::EvaluateVisibilityCandidate(
        std::string a_presetID,
        const runtime::PresetTransform& a_transform,
        const runtime::SceneVisibilitySamples& a_samples,
        std::size_t& a_rayCount)
    {
        auto pose = poseCalculator_.Evaluate(
            *anchor_,
            {
                {
                    a_transform.framingOffset.right,
                    a_transform.framingOffset.up,
                },
                {
                    a_transform.orbit.yawDegrees,
                    a_transform.orbit.pitchDegrees,
                    a_transform.orbit.distance,
                },
            });

        std::vector<core::VisibilityPointResult> pointResults;
        pointResults.reserve(a_samples.targets.size());
        for (const auto& target : a_samples.targets) {
            core::VisibilityPointResult point;
            point.participantIndex = target.participantIndex;
            point.participantID = target.participantID;
            point.point = target.point;
            point.target = target.position ? ToCore(*target.position) : core::Vec3{};
            point.rayStart = pose ? pose->position : core::Vec3{};

            if (!pose) {
                point.hitObject = "camera pose unavailable";
            } else if (!target.position) {
                point.hitObject = "participant node unavailable";
            } else if (!visibilityProbe_) {
                point.hitObject = "visibility probe unavailable";
            } else {
                const auto hit = visibilityProbe_->Trace(
                    ToRuntime(pose->position),
                    *target.position,
                    target.participantID);
                a_rayCount += hit.queryCount;
                point.hitFraction = hit.fraction;
                point.hitPosition = hit.position ?
                    std::optional{ ToCore(*hit.position) } : std::nullopt;
                point.hitNormal = hit.normal ?
                    std::optional{ ToCore(*hit.normal) } : std::nullopt;
                point.hitObjectID = hit.objectID;
                point.hitObject = hit.object;

                if (!hit.querySucceeded) {
                    point.status = core::VisibilityPointStatus::kUnavailable;
                } else if (hit.startsInsideCollision) {
                    point.status = core::VisibilityPointStatus::kStartsInsideCollision;
                } else if (hit.reachesTarget) {
                    point.status = core::VisibilityPointStatus::kVisible;
                } else {
                    point.status = core::VisibilityPointStatus::kObstructed;
                }
            }
            pointResults.push_back(std::move(point));
        }

        return visibilityEvaluator_.Evaluate(
            std::move(a_presetID),
            std::move(pose),
            a_samples.participantIDs,
            std::move(pointResults));
    }

    bool SceneCamera::EvaluatePreviewVisibility(
        const runtime::PresetPreviewRequest& a_request)
    {
        if (!anchor_ || !a_request.transform) {
            return false;
        }

        constexpr auto targetCapacity = runtime::SceneParticipantSnapshot::kCapacity *
            runtime::SceneVisibilitySamples::kPointsPerParticipant;
        std::array<std::uint32_t, runtime::SceneParticipantSnapshot::kCapacity> participantIDs{};
        std::array<runtime::VisibilityTarget, targetCapacity> targets{};
        const auto samples = sceneSource_ ?
            sceneSource_->CollectVisibilityInput(participants_, participantIDs, targets) :
            std::nullopt;
        auto evaluation = std::make_shared<core::VisibilityEvaluationSnapshot>();
        if (!samples) {
            visibilityEvaluation_ = std::move(evaluation);
            if (debugVisualization_) {
                debugVisualization_->ShowVisibility(visibilityEvaluation_);
            }
            return false;
        }

        std::size_t rayCount = 0;
        auto candidate = EvaluateVisibilityCandidate(
            a_request.presetID.empty() ? "<new preset>" : a_request.presetID,
            *a_request.transform,
            *samples,
            rayCount);
        logger::debug(
            "Preview visibility '{}': {}, participants {}/{}, points {}/{}, {} physics ray(s)",
            candidate.presetID,
            core::CandidateFailureReasonName(candidate.failureReason),
            candidate.visibleParticipantCount,
            candidate.participants.size(),
            candidate.visiblePointCount,
            candidate.availablePointCount,
            rayCount);
        evaluation->candidates.push_back(std::move(candidate));
        visibilityEvaluation_ = std::move(evaluation);
        if (debugVisualization_) {
            debugVisualization_->ShowVisibility(visibilityEvaluation_);
        }
        return true;
    }

    bool SceneCamera::EvaluateVisibility()
    {
        activePresetID_.reset();

        auto evaluation = std::make_shared<core::VisibilityEvaluationSnapshot>();
        const auto presetSnapshot = presetProvider_ ? presetProvider_->Snapshot() : nullptr;
        if (!anchor_ || !presetSnapshot || presetSnapshot->empty()) {
            visibilityEvaluation_ = evaluation;
            if (debugVisualization_) {
                debugVisualization_->ShowVisibility(visibilityEvaluation_);
            }
            return true;
        }

        constexpr auto targetCapacity = runtime::SceneParticipantSnapshot::kCapacity *
            runtime::SceneVisibilitySamples::kPointsPerParticipant;
        std::array<std::uint32_t, runtime::SceneParticipantSnapshot::kCapacity> participantIDs{};
        std::array<runtime::VisibilityTarget, targetCapacity> targets{};
        const auto visibilitySamples = sceneSource_ ?
            sceneSource_->CollectVisibilityInput(participants_, participantIDs, targets) :
            std::nullopt;
        if (!visibilitySamples) {
            logger::error("Could not collect participant visibility points");
            visibilityEvaluation_ = evaluation;
            if (debugVisualization_) {
                debugVisualization_->ShowVisibility(visibilityEvaluation_);
            }
            static_cast<void>(ReleaseCamera("participant visibility points are unavailable"sv));
            PublishPreviewFeedback(false, "Participant visibility points are unavailable");
            return false;
        }

        evaluation->candidates.reserve(presetSnapshot->size());
        const auto startedAt = std::chrono::steady_clock::now();
        std::size_t rayCount = 0;
        for (const auto& preset : *presetSnapshot) {
            auto candidate = EvaluateVisibilityCandidate(
                preset.id,
                preset.transform,
                *visibilitySamples,
                rayCount);
            logger::info(
                "Visibility candidate '{}': {}, participants {}/{}, points {}/{}",
                candidate.presetID,
                core::CandidateFailureReasonName(candidate.failureReason),
                candidate.visibleParticipantCount,
                candidate.participants.size(),
                candidate.visiblePointCount,
                candidate.availablePointCount);
            for (const auto& point : candidate.points) {
                logger::debug(
                    "Visibility '{}' participant {:08X} {}: {}, hit={}, fraction={:.5f}, position=({}, {}, {})",
                    candidate.presetID,
                    point.participantID,
                    core::VisibilityPointName(point.point),
                    core::VisibilityPointStatusName(point.status),
                    point.hitObject.empty() ? "none" : point.hitObject,
                    point.hitFraction,
                    point.hitPosition ? point.hitPosition->x : 0.0F,
                    point.hitPosition ? point.hitPosition->y : 0.0F,
                    point.hitPosition ? point.hitPosition->z : 0.0F);
            }

            if (!activePresetID_ && candidate.usable) {
                activePresetID_ = candidate.presetID;
            }
            evaluation->candidates.push_back(std::move(candidate));
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - startedAt);
        logger::info(
            "Visibility evaluation completed: {} preset(s), {} physics ray(s), {:.3f} ms, selected={}",
            evaluation->candidates.size(),
            rayCount,
            static_cast<double>(elapsed.count()) / 1000.0,
            activePresetID_ ? *activePresetID_ : "none");

        visibilityEvaluation_ = std::move(evaluation);
        if (debugVisualization_) {
            debugVisualization_->ShowVisibility(visibilityEvaluation_);
        }

        if (!activePresetID_) {
            static_cast<void>(ReleaseCamera("no preset can show every participant"sv));
            PublishPreviewFeedback(false, "No camera preset can show every participant");
        }
        return true;
    }

    bool SceneCamera::ApplyTransform(
        const runtime::PresetTransform& a_transform,
        bool a_liveEdit,
        std::uint64_t a_revision)
    {
        if (!anchor_) {
            PublishPreviewFeedback(false, "Scene anchor is not available");
            return false;
        }
        const auto corePose = poseCalculator_.Evaluate(
            *anchor_,
            {
                {
                    a_transform.framingOffset.right,
                    a_transform.framingOffset.up,
                },
                {
                    a_transform.orbit.yawDegrees,
                    a_transform.orbit.pitchDegrees,
                    a_transform.orbit.distance,
                },
            });
        if (!corePose) {
            PublishPreviewFeedback(false, "Preset cannot produce a camera pose");
            if (!a_liveEdit) {
                logger::error("Camera pose generation failed for the selected transform");
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
                const auto reason = cameraControl_->UnavailableReason();
                logger::warn("Cannot acquire scene camera: {}", reason);
                PublishPreviewFeedback(false, std::string{ reason });
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
            logger::error("Could not apply camera transform: {}", ApplyResultName(applyResult));
            PublishPreviewFeedback(false, std::string{ ApplyResultName(applyResult) });
            Restore(acquiredNow ?
                "initial camera pose apply failed"sv :
                "replacement camera pose apply failed"sv);
            return false;
        }

        cameraPose_ = runtimePose;
        cameraPoseActive_.store(true, std::memory_order_release);
        PublishPreviewFeedback(
            true,
            a_liveEdit ? "Live preview" : "Preset active",
            a_transform,
            true,
            a_revision);
        if (a_liveEdit) {
            logger::debug("Camera transform revision {} live preview applied", a_revision);
        } else {
            logger::info(
                "Camera transform applied at ({:.2f}, {:.2f}, {:.2f})",
                runtimePose.position.x,
                runtimePose.position.y,
                runtimePose.position.z);
        }
        return true;
    }

    void SceneCamera::PublishPreviewFeedback(
        bool a_applied,
        std::string a_message,
        std::optional<runtime::PresetTransform> a_transform,
        bool a_previewPossible,
        std::uint64_t a_appliedRevision)
    {
        if (!previewService_) {
            return;
        }
        previewService_->PublishFeedback({
            session_.IsActive(),
            a_applied,
            a_previewPossible,
            a_appliedRevision,
            std::move(a_transform),
            std::move(a_message),
            visibilityEvaluation_,
        });
    }

    bool SceneCamera::ReleaseCamera(std::string_view a_reason)
    {
        if (cameraControl_ && cameraControl_->OwnsCamera()) {
            const auto releaseResult = cameraControl_->Release();
            if (releaseResult == runtime::CameraReleaseResult::kWrongThread) {
                resetRequested_.store(true, std::memory_order_release);
                return false;
            }
            if (releaseResult == runtime::CameraReleaseResult::kFailed) {
                logger::error("Camera release did not complete; reset will be retried");
                resetRequested_.store(true, std::memory_order_release);
                return false;
            }
        }

        const auto hadPose = cameraPose_.has_value();
        cameraPose_.reset();
        cameraPoseActive_.store(false, std::memory_order_release);
        appliedPreviewRequest_.reset();
        if (hadPose) {
            logger::info("Scene camera released: {}", a_reason);
        }
        return true;
    }

    void SceneCamera::Restore(std::string_view a_reason)
    {
        const auto hadSession = !session_.IsIdle();
        const auto hadCameraOwnership = cameraControl_ && cameraControl_->OwnsCamera();
        const auto hadRuntimeState = hadSession || hadCameraOwnership ||
            cameraPose_.has_value() || anchor_.has_value();
        if (!hadRuntimeState) {
            return;
        }

        if (hadCameraOwnership && !ReleaseCamera(a_reason)) {
            return;
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
        activeSince_ = {};
        if (debugVisualization_) {
            debugVisualization_->HideAnchor();
            debugVisualization_->HideVisibility();
        }
        cameraPose_.reset();
        appliedPreviewRequest_.reset();
        activePresetID_.reset();
        visibilityEvaluation_.reset();
        anchor_.reset();
        session_.Clear();
        PublishPreviewFeedback(false, "No active player scene");
    }
}
