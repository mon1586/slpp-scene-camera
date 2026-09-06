#include "SceneCamera.h"

#include "runtime/CameraPoseAdapter.h"

namespace ssc
{
    namespace
    {
        constexpr auto kMaximumSceneDuration = std::chrono::minutes{ 30 };
        constexpr auto kAnimationChangeDelay = std::chrono::seconds{ 1 };
        constexpr auto kDebugResumeInterval = std::chrono::milliseconds{ 500 };
        constexpr auto kDebugResumeTimeout = std::chrono::seconds{ 2 };
        constexpr unsigned kDebugResumeAttempts = 3;

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
            case runtime::CameraApplyResult::kInvalidFOV:
                return "invalid camera FOV"sv;
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
        return session_.IsActive() ||
               sceneEvaluationPending_.load(std::memory_order_acquire) ||
               cameraPoseActive_.load(std::memory_order_acquire) ||
               resetRequested_.load(std::memory_order_acquire) ||
               presetStepRequested_.load(std::memory_order_acquire) != 0 || previewChanged;
    }

    bool SceneCamera::AllowsUpdateWhilePaused() const noexcept
    {
        const auto debugMode = debugVisualization_ && debugVisualization_->Enabled();
        return (previewService_ && previewService_->PreviewSessionActive()) ||
               debugResumePending_.load(std::memory_order_acquire) ||
               debugMode != debugModeObserved_;
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
            sceneEvaluationPending_.store(false, std::memory_order_release);
            sceneEvaluationReadyAt_ = {};
            Clear();
            return;
        }
        participants_ = a_event.participants;

        if (!session_.Activate(a_event.key)) {
            return;
        }
        sceneEvaluationReadyAt_ = now_();
        sceneEvaluationPending_.store(true, std::memory_order_release);
        activeSince_ = sceneEvaluationReadyAt_;
        logger::info("Initial scene evaluation pending for {:08X}/{}",
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
        sceneEvaluationReadyAt_ = now_() + kAnimationChangeDelay;
        sceneEvaluationPending_.store(true, std::memory_order_release);
        presetSwitchEnabled_.store(false, std::memory_order_release);
        presetStepRequested_.store(0, std::memory_order_release);
        logger::info("Scene visibility reevaluation scheduled in {} ms for {:08X}/{}",
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

    void SceneCamera::Update(float a_deltaSeconds)
    {
        ApplyRequestedReset();
        const auto debugMode = debugVisualization_ && debugVisualization_->Enabled();
        const auto debugModeChanged = debugMode != debugModeObserved_;
        if (debugModeChanged) {
            debugResumePending_.store(false, std::memory_order_release);
            anchorLOSTimeSeconds_ = 0.0F;
            anchorLOSMetrics_ = {};
            logger::info("Scene camera debug mode {}", debugMode ? "enabled" : "disabled");
            if (session_.IsActive()) {
                sceneEvaluationReadyAt_ = now_();
                sceneEvaluationPending_.store(true, std::memory_order_release);
            }
            if (debugMode && !ReleaseCamera("debug mode enabled"sv, false)) {
                return;
            }
            debugModeObserved_ = debugMode;
            if (!debugMode && session_.IsActive()) {
                debugResumeNextAttempt_ = now_();
                debugResumeDeadline_ = debugResumeNextAttempt_ + kDebugResumeTimeout;
                debugResumeAttemptsLeft_ = kDebugResumeAttempts;
                debugResumePending_.store(true, std::memory_order_release);
                presetSwitchEnabled_.store(false, std::memory_order_release);
                presetStepRequested_.store(0, std::memory_order_release);
            }
            if (debugMode) {
                PublishPreviewFeedback(
                    false,
                    "Debug mode active; SmoothCam remains in control",
                    std::nullopt,
                    false);
            }
        }
        const auto previewRequest = previewService_ ? previewService_->Request() : nullptr;
        const auto previewSessionActive = previewService_ &&
            previewService_->PreviewSessionActive();
        if (debugModeChanged && !debugMode && !previewSessionActive) {
            // The explicit resume owns this request, including any old editor-close request.
            appliedPreviewRequest_ = previewRequest;
        }
        if (!session_.IsActive() && !cameraPose_) {
            appliedPreviewRequest_ = previewRequest;
            return;
        }
        if (debugVisualization_) {
            debugVisualization_->Update();
        }
        const auto now = now_();
        if (debugResumePending_.load(std::memory_order_acquire) &&
            (previewSessionActive || now >= debugResumeDeadline_)) {
            debugResumePending_.store(false, std::memory_order_release);
            if (!previewSessionActive) {
                sceneEvaluationPending_.store(false, std::memory_order_release);
                sceneEvaluationReadyAt_ = {};
                PublishPreviewFeedback(false,
                    "Could not resume camera; toggle Debug mode on/off to retry");
            }
        }
        if (session_.IsActive() && now - activeSince_ > kMaximumSceneDuration) {
            Restore("scene watchdog timeout"sv);
            return;
        }

        const auto sceneEvaluationPending =
            sceneEvaluationPending_.load(std::memory_order_acquire);
        const auto sceneEvaluationReady =
            sceneEvaluationPending && now >= sceneEvaluationReadyAt_;
        const auto shouldTrackAnchor =
            session_.IsActive() && (anchor_.has_value() || sceneEvaluationReady);
        bool anchorRefreshed = false;
        bool poseAppliedThisUpdate = false;
        bool evaluatedThisUpdate = false;
        if (shouldTrackAnchor) {
            const auto samples = sceneSource_ ?
                sceneSource_->CollectAnchorInput(participants_) : std::nullopt;
            if (samples) {
                auto updatedAnchor = anchorCalculator_.Evaluate({
                    ToCore(samples->bodyCenter),
                    ToCore(samples->playerActorForward),
                });
                if (updatedAnchor) {
                    const auto firstAnchor = !anchor_.has_value();
                    const auto targetPosition = updatedAnchor->position;
                    if (anchor_) {
                        updatedAnchor->position = core::SmoothAnchorPosition(
                            anchor_->position, targetPosition, a_deltaSeconds);
                        if (!std::isfinite(a_deltaSeconds) || a_deltaSeconds <= 0.0F) {
                            updatedAnchor->forward = anchor_->forward;
                        }
                    }
                    anchor_ = std::move(updatedAnchor);
                    anchorRefreshed = true;
                    if (debugVisualization_ && !debugVisualization_->ShowAnchor(
                            ToRuntime(anchor_->position), ToRuntime(anchor_->forward),
                            ToRuntime(targetPosition)) &&
                        firstAnchor) {
                        logger::warn("Scene anchor debug marker could not be displayed");
                    }
                    if (firstAnchor) {
                        logger::info(
                            "Scene anchor acquired at torso center (waist/chest midpoint) ({:.2f}, {:.2f}, {:.2f}), forward ({:.3f}, {:.3f}, {:.3f})",
                            anchor_->position.x,
                            anchor_->position.y,
                            anchor_->position.z,
                            anchor_->forward.x,
                            anchor_->forward.y,
                            anchor_->forward.z);
                    }
                }
            }

            if (!anchorRefreshed && !anchor_) {
                return;
            }
        }

        const auto previewEnded = anchor_ && !previewSessionActive &&
            previewRequest != appliedPreviewRequest_ && previewRequest &&
            !previewRequest->transform && appliedPreviewRequest_ &&
            appliedPreviewRequest_->transform;
        if (!anchorRefreshed) {
            anchorInputUnavailable_ = true;
            anchorLOSTimeSeconds_ = 0.0F;
        }
        if (sceneEvaluationReady && anchorRefreshed) {
            sceneEvaluationPending_.store(false, std::memory_order_release);
            sceneEvaluationReadyAt_ = {};
            if (!EvaluateVisibility(previewEnded ? previewRequest->presetID : "")) {
                return;
            }
            evaluatedThisUpdate = true;
            anchorInputUnavailable_ = false;
            if (!debugMode && !debugResumePending_.load(std::memory_order_acquire)) {
                bool requestedPoseApplied = false;
                if (!ApplyRequestedTransform(previewRequest, false, requestedPoseApplied)) {
                    return;
                }
                poseAppliedThisUpdate = requestedPoseApplied;
            }
        }

        if (debugResumePending_.load(std::memory_order_acquire) &&
            !sceneEvaluationPending_.load(std::memory_order_acquire) &&
            anchorRefreshed && now >= debugResumeNextAttempt_) {
            if (!ResolveTransform(previewRequest)) {
                debugResumePending_.store(false, std::memory_order_release);
                bool unusedPoseApplied = false;
                static_cast<void>(ApplyRequestedTransform(previewRequest, false, unusedPoseApplied));
            } else {
                --debugResumeAttemptsLeft_;
                debugResumeNextAttempt_ = now + kDebugResumeInterval;
                bool resumedPoseApplied = false;
                if (!ApplyRequestedTransform(previewRequest, false, resumedPoseApplied)) {
                    if (session_.IsActive() && debugResumeAttemptsLeft_ == 0) {
                        debugResumePending_.store(false, std::memory_order_release);
                        PublishPreviewFeedback(false,
                            "Could not resume camera; toggle Debug mode on/off to retry");
                    }
                    return;
                }
                debugResumePending_.store(false, std::memory_order_release);
                poseAppliedThisUpdate = poseAppliedThisUpdate || resumedPoseApplied;
            }
        }

        const auto runningTime = std::isfinite(a_deltaSeconds) && a_deltaSeconds > 0.0F;
        if (!evaluatedThisUpdate && !previewSessionActive && !previewEnded &&
            !sceneEvaluationPending_.load(std::memory_order_acquire) && anchorRefreshed && runningTime) {
            anchorLOSTimeSeconds_ += a_deltaSeconds;
            if (anchorInputUnavailable_ || anchorLOSTimeSeconds_ >= anchorLOSMetrics_.intervalSeconds) {
                MeasureAnchorLOS();
                evaluatedThisUpdate = true;
                anchorInputUnavailable_ = false;
            }
        }
        const auto canStep = anchorRefreshed && !anchorInputUnavailable_ &&
            !debugResumePending_.load(std::memory_order_acquire) &&
            !previewSessionActive && !sceneEvaluationPending_.load(std::memory_order_acquire);
        presetSwitchEnabled_.store(canStep, std::memory_order_release);
        if (!canStep) {
            presetStepRequested_.store(0, std::memory_order_release);
        }
        if (debugMode) {
            const auto requestedStep =
                presetStepRequested_.exchange(0, std::memory_order_acq_rel);
            if (requestedStep != 0 && debugVisualization_) {
                debugVisualization_->StepCandidate(requestedStep);
            }
            if (sceneEvaluationReady) {
                PublishPreviewFeedback(
                    false,
                    "Debug mode active; SmoothCam remains in control",
                    std::nullopt,
                    false);
            }
            return;
        }

        if (previewEnded && !evaluatedThisUpdate &&
            !EvaluateVisibility(previewRequest->presetID)) {
            return;
        }

        if (cameraPose_ && !previewSessionActive && !ResolveTransform(previewRequest)) {
            PublishPreviewFeedback(false, "No visible camera preset is available");
            static_cast<void>(ReleaseCamera("preset editor closed with no visible camera preset"sv));
            return;
        }
        if (anchor_ && !debugResumePending_.load(std::memory_order_acquire) &&
            previewRequest != appliedPreviewRequest_) {
            bool requestedPoseApplied = false;
            if (!ApplyRequestedTransform(
                    previewRequest, previewSessionActive, requestedPoseApplied)) {
                return;
            }
            poseAppliedThisUpdate = poseAppliedThisUpdate || requestedPoseApplied;
        }

        if (previewSessionActive) {
            presetSwitchEnabled_.store(false, std::memory_order_release);
            presetStepRequested_.store(0, std::memory_order_release);
        } else {
            const auto requestedStep = presetStepRequested_.exchange(0, std::memory_order_acq_rel);
            if (requestedStep != 0) {
                bool stepPoseApplied = false;
                if (!SelectPresetStep(requestedStep, stepPoseApplied)) {
                    return;
                }
                poseAppliedThisUpdate = poseAppliedThisUpdate || stepPoseApplied;
            }
        }

        if (cameraPose_ && anchor_ && !poseAppliedThisUpdate) {
            if (const auto trackedTransform = ResolveTransform(previewRequest)) {
                const auto trackedPose = poseCalculator_.Evaluate(
                    *anchor_,
                    {
                        {
                            trackedTransform->framingOffset.right,
                            trackedTransform->framingOffset.up,
                        },
                        {
                            trackedTransform->orbit.yawDegrees,
                            trackedTransform->orbit.pitchDegrees,
                            trackedTransform->orbit.distance,
                        },
                    });
                if (!trackedPose) {
                    logger::error("Could not update the camera pose from the tracked scene anchor");
                    Restore("tracked camera pose generation failed"sv);
                    return;
                }
                cameraPose_ = runtime::ToRuntimeCameraPose(
                    *trackedPose,
                    trackedTransform->fovOffsetDegrees);
            }
        }

        if (cameraPose_ && !poseAppliedThisUpdate) {
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
        bool a_liveEdit,
        bool& a_poseApplied)
    {
        a_poseApplied = false;
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
                    "No camera preset meets the anchor LOS conditions" :
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
        a_poseApplied = true;
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
        std::size_t& a_rayCount,
        double* a_traceMilliseconds)
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

        // The player reference and all character layers are ignored by the probe.
        constexpr std::array<std::uint32_t, 1> participantIDs{ 0x14 };
        std::array<runtime::VisibilityTarget, 5> targets{};
        const auto origins = pose ? core::AnchorLOSRayOrigins(*pose) :
            std::array<core::Vec3, 5>{};
        for (std::size_t index = 0; index < targets.size(); ++index) {
            targets[index] = { 0, participantIDs.front(), core::kAnchorLOSPoints[index],
                ToRuntime(anchor_->position), ToRuntime(origins[index]) };
        }
        auto candidate = EvaluateVisibilityAtPose(
            std::move(a_presetID), std::move(pose), { participantIDs, targets },
            a_rayCount, a_traceMilliseconds);
        core::ApplyAnchorLOSRule(candidate);
        return candidate;
    }

    core::CameraCandidateVisibility SceneCamera::EvaluateVisibilityAtPose(
        std::string a_presetID,
        std::optional<core::CameraPose> pose,
        const runtime::SceneVisibilitySamples& a_samples,
        std::size_t& a_rayCount,
        double* a_traceMilliseconds)
    {
        std::vector<core::VisibilityPointResult> pointResults;
        pointResults.reserve(a_samples.targets.size());
        for (const auto& target : a_samples.targets) {
            core::VisibilityPointResult point;
            point.participantIndex = target.participantIndex;
            point.participantID = target.participantID;
            point.point = target.point;
            point.target = target.position ? ToCore(*target.position) : core::Vec3{};
            point.rayStart = target.rayOrigin ? ToCore(*target.rayOrigin) :
                (pose ? pose->position : core::Vec3{});

            if (!pose) {
                point.hitObject = "camera pose unavailable";
            } else if (!target.position) {
                point.hitObject = "participant body center unavailable";
            } else if (!visibilityProbe_) {
                point.hitObject = "visibility probe unavailable";
            } else {
                const auto traceStartedAt = a_traceMilliseconds ?
                    std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                const auto hit = visibilityProbe_->Trace(
                    ToRuntime(point.rayStart),
                    *target.position,
                    target.participantID);
                if (a_traceMilliseconds) {
                    *a_traceMilliseconds += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - traceStartedAt).count();
                }
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

    void SceneCamera::MeasureAnchorLOS()
    {
        if (!anchor_) {
            return;
        }
        const auto startedAt = std::chrono::steady_clock::now();
        const auto presets = presetProvider_ ? presetProvider_->Snapshot() : nullptr;
        auto evaluation = std::make_shared<core::VisibilityEvaluationSnapshot>();
        std::size_t rayCount = 0;
        std::size_t visibleCenters = 0;
        std::size_t visibleCorners = 0;
        double traceMilliseconds = 0.0;
        if (presets) {
            evaluation->candidates.reserve(presets->size());
            for (const auto& preset : *presets) {
                auto candidate = EvaluateVisibilityCandidate(
                    preset.id, preset.transform, rayCount, &traceMilliseconds);
                for (const auto& point : candidate.points) {
                    if (point.status == core::VisibilityPointStatus::kVisible) {
                        if (point.point == core::VisibilityPoint::kAnchor) {
                            ++visibleCenters;
                        } else {
                            ++visibleCorners;
                        }
                    }
                }
                evaluation->candidates.push_back(std::move(candidate));
            }
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - startedAt).count();
        ++anchorLOSMetrics_.sampleCount;
        anchorLOSMetrics_.lastMilliseconds = elapsed;
        anchorLOSMetrics_.averageMilliseconds +=
            (elapsed - anchorLOSMetrics_.averageMilliseconds) /
            static_cast<double>(anchorLOSMetrics_.sampleCount);
        anchorLOSMetrics_.maximumMilliseconds =
            std::max(anchorLOSMetrics_.maximumMilliseconds, elapsed);
        anchorLOSMetrics_.traceMilliseconds = traceMilliseconds;
        anchorLOSMetrics_.rayQueryCount = rayCount;
        evaluation->anchorLOS = anchorLOSMetrics_;
        evaluation->selectedPresetID = activePresetID_;
        visibilityEvaluation_ = std::move(evaluation);
        anchorLOSTimeSeconds_ = 0.0F;
        if (debugVisualization_) {
            debugVisualization_->ShowVisibility(visibilityEvaluation_);
        }
        if (previewService_) {
            const auto previous = previewService_->Feedback();
            auto feedback = previous ? *previous : runtime::PresetPreviewFeedback{};
            feedback.visibilityEvaluation = visibilityEvaluation_;
            previewService_->PublishFeedback(std::move(feedback));
        }
        if (debugModeObserved_ &&
            (anchorLOSMetrics_.sampleCount == 1 || anchorLOSMetrics_.sampleCount % 10 == 0)) {
            logger::info(
                "Anchor LOS benchmark (camera-side 32x18, 5 rays/preset): sample={}, interval={:.2f}s, "
                "candidates={}, visibleCenters={}, visibleCorners={}, "
                "queries={}, batch={:.4f}ms, LOS={:.4f}ms, avg={:.4f}ms, max={:.4f}ms, "
                "estimated={:.4f}ms/s (excludes logging/HUD)",
                anchorLOSMetrics_.sampleCount, anchorLOSMetrics_.intervalSeconds,
                presets ? presets->size() : 0, visibleCenters, visibleCorners, rayCount, elapsed,
                traceMilliseconds, anchorLOSMetrics_.averageMilliseconds,
                anchorLOSMetrics_.maximumMilliseconds,
                anchorLOSMetrics_.averageMilliseconds / anchorLOSMetrics_.intervalSeconds);
        }
    }

    bool SceneCamera::EvaluatePreviewVisibility(
        const runtime::PresetPreviewRequest& a_request)
    {
        if (!anchor_ || !a_request.transform) {
            return false;
        }

        auto evaluation = std::make_shared<core::VisibilityEvaluationSnapshot>();
        std::size_t rayCount = 0;
        auto candidate = EvaluateVisibilityCandidate(
            a_request.presetID.empty() ? "<new preset>" : a_request.presetID,
            *a_request.transform,
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
        evaluation->selectedPresetID = evaluation->candidates.front().presetID;
        visibilityEvaluation_ = std::move(evaluation);
        if (debugVisualization_) {
            debugVisualization_->ShowVisibility(visibilityEvaluation_);
        }
        return true;
    }

    bool SceneCamera::EvaluateVisibility(std::string_view a_preferredPresetID)
    {
        if (!anchor_) {
            return false;
        }
        MeasureAnchorLOS();
        if (!initialPresetSelectionDone_) {
            activePresetID_ = candidateSelector_.SelectInitial(visibilityEvaluation_->candidates);
            initialPresetSelectionDone_ = true;
        }
        const auto exists = [&](std::string_view a_id) {
            return std::ranges::any_of(visibilityEvaluation_->candidates,
                [&](const auto& a_candidate) { return a_candidate.presetID == a_id; });
        };
        if (!a_preferredPresetID.empty() && exists(a_preferredPresetID)) {
            activePresetID_ = a_preferredPresetID;
        } else if (activePresetID_ && !exists(*activePresetID_)) {
            activePresetID_.reset();
        }
        auto evaluation = std::make_shared<core::VisibilityEvaluationSnapshot>(*visibilityEvaluation_);
        evaluation->selectedPresetID = activePresetID_;
        visibilityEvaluation_ = std::move(evaluation);
        if (debugVisualization_) {
            debugVisualization_->ShowVisibility(visibilityEvaluation_);
        }
        return true;
    }

    bool SceneCamera::SelectPresetStep(int a_direction, bool& a_poseApplied)
    {
        a_poseApplied = false;
        if (!presetSwitchEnabled_.load(std::memory_order_acquire) ||
            !visibilityEvaluation_ || a_direction == 0) {
            return true;
        }

        const auto nextPresetID = candidateSelector_.Step(
            visibilityEvaluation_->candidates, activePresetID_.value_or(""), a_direction);
        if (!nextPresetID || nextPresetID == activePresetID_) {
            return true;
        }

        const auto presetSnapshot = presetProvider_ ? presetProvider_->Snapshot() : nullptr;
        if (!presetSnapshot) {
            return true;
        }
        const auto preset = std::ranges::find_if(*presetSnapshot, [&](const auto& a_preset) {
            return a_preset.id == *nextPresetID;
        });
        if (preset == presetSnapshot->end()) {
            return true;
        }

        activePresetID_ = *nextPresetID;
        auto updatedEvaluation = std::make_shared<core::VisibilityEvaluationSnapshot>(
            *visibilityEvaluation_);
        updatedEvaluation->selectedPresetID = activePresetID_;
        visibilityEvaluation_ = std::move(updatedEvaluation);
        if (debugVisualization_) {
            debugVisualization_->ShowVisibility(visibilityEvaluation_);
        }
        logger::info("Camera preset cut to '{}'", *nextPresetID);
        if (!ApplyTransform(preset->transform, false, 0)) {
            return false;
        }
        a_poseApplied = true;
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
            logger::info("Scene camera acquisition preflight passed; requesting SmoothCam control");
            if (!cameraControl_->Acquire()) {
                logger::warn("SmoothCam camera-control acquisition failed");
                PublishPreviewFeedback(false, "SmoothCam camera-control acquisition failed");
                return false;
            }
        }

        const auto runtimePose = runtime::ToRuntimeCameraPose(
            *corePose,
            a_transform.fovOffsetDegrees);
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
        presetSwitchEnabled_.store(!a_liveEdit, std::memory_order_release);
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

    bool SceneCamera::ReleaseCamera(
        std::string_view a_reason,
        bool a_requestResetOnFailure)
    {
        if (cameraControl_ && cameraControl_->OwnsCamera()) {
            const auto releaseResult = cameraControl_->Release();
            if (releaseResult == runtime::CameraReleaseResult::kWrongThread) {
                if (a_requestResetOnFailure) {
                    resetRequested_.store(true, std::memory_order_release);
                }
                return false;
            }
            if (releaseResult == runtime::CameraReleaseResult::kFailed) {
                logger::error("Camera release did not complete; release will be retried");
                if (a_requestResetOnFailure) {
                    resetRequested_.store(true, std::memory_order_release);
                }
                return false;
            }
        }

        const auto hadPose = cameraPose_.has_value();
        presetSwitchEnabled_.store(false, std::memory_order_release);
        presetStepRequested_.store(0, std::memory_order_release);
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

    void SceneCamera::RequestPresetStep(int a_direction) noexcept
    {
        if (!session_.IsActive() ||
            !presetSwitchEnabled_.load(std::memory_order_acquire) ||
            a_direction == 0) {
            return;
        }
        presetStepRequested_.store(a_direction > 0 ? 1 : -1, std::memory_order_release);
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
        sceneEvaluationPending_.store(false, std::memory_order_release);
        cameraPoseActive_.store(false, std::memory_order_release);
        presetSwitchEnabled_.store(false, std::memory_order_release);
        presetStepRequested_.store(0, std::memory_order_release);
        sceneEvaluationReadyAt_ = {};
        activeSince_ = {};
        debugResumePending_.store(false, std::memory_order_release);
        debugResumeAttemptsLeft_ = 0;
        debugResumeDeadline_ = {};
        debugResumeNextAttempt_ = {};
        anchorLOSTimeSeconds_ = 0.0F;
        anchorLOSMetrics_ = {};
        if (debugVisualization_) {
            debugVisualization_->HideAnchor();
            debugVisualization_->HideVisibility();
        }
        cameraPose_.reset();
        appliedPreviewRequest_.reset();
        activePresetID_.reset();
        visibilityEvaluation_.reset();
        anchor_.reset();
        initialPresetSelectionDone_ = false;
        anchorInputUnavailable_ = false;
        session_.Clear();
        PublishPreviewFeedback(false, "No active player scene");
    }
}
