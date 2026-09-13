#include "support/SceneCameraDoubles.h"

using namespace ssc::tests;
bool RunTDMTargetLockControlTests();
bool RunAnimationFilterIntegrationTests();

int main()
{
    const ssc::runtime::PresetTransform defaultTransform{
        {}, { 0.0F, 0.0F, 100.0F } };
    const ssc::runtime::PresetTransform alternateTransform{
        {}, { 30.0F, 0.0F, 100.0F } };
    const ssc::runtime::PresetTransform editedBlockedTransform{
        {}, { 90.0F, 0.0F, 100.0F } };

    TestSceneSource sceneSource;
    TestPresetProvider presetProvider({
        { "default", defaultTransform },
        { "edited", alternateTransform },
    });
    ssc::runtime::PresetPreviewService previewService;
    TestCameraControl cameraControl;
    TestVisibilityProbe visibilityProbe;
    TestDebugVisualization debugVisualization;
    ssc::SceneCamera camera;
    camera.Configure(
        sceneSource,
        presetProvider,
        previewService,
        cameraControl,
        visibilityProbe,
        debugVisualization);

    const auto participants = ssc::runtime::SexLabPSceneSource::MakeTestSnapshot();
    const ssc::runtime::SceneKey sceneKey{ 0x01000001, 1 };
    camera.HandleSceneEvent({
        ssc::runtime::SceneEventType::kAnimationStart,
        sceneKey,
        participants,
    });
    camera.Update(1.0F / 60.0F);

    bool passed = RunTDMTargetLockControlTests();
    auto feedback = previewService.Feedback();
    passed &= Check(feedback && feedback->visibilityEvaluation &&
        feedback->visibilityEvaluation->selectedPresetID ==
            std::optional<std::string>{ "default" },
        "scene start uses the body center and selects the first usable preset");
    passed &= Check(visibilityProbe.traceCount_ == 10,
        "initial visibility evaluates five anchor rays per preset");
    passed &= Check(cameraControl.ApplyCount() == 1,
        "initial camera acquisition applies the selected pose once");

    const auto initialPose = cameraControl.LastPose();
    sceneSource.bodyCenter_ = { 10.0F, 20.0F, 30.0F };
    camera.Update(0.15F);
    const auto trackedPose = cameraControl.LastPose();
    passed &= Check(initialPose && trackedPose &&
        std::abs((trackedPose->position.x - initialPose->position.x) - 5.0F) < 0.001F &&
        std::abs((trackedPose->position.y - initialPose->position.y) - 10.0F) < 0.001F &&
        std::abs((trackedPose->position.z - initialPose->position.z) - 15.0F) < 0.001F,
        "camera closes half the body-center gap in 0.15 seconds");
    passed &= Check(std::abs(debugVisualization.anchorPosition_.x - 5.0F) < 0.001F &&
        debugVisualization.targetPosition_.x == 10.0F,
        "debug markers distinguish the filtered anchor from the raw torso target");
    passed &= Check(visibilityProbe.traceCount_ == 10,
        "continuous anchor tracking does not rerun visibility rays");
    passed &= Check(cameraControl.ApplyCount() == 2,
        "body-center tracking applies the updated pose once per update");

    sceneSource.actorForward_ = { 1.0F, 0.0F, 0.0F };
    camera.Update(0.15F);
    const auto rotatedPose = cameraControl.LastPose();
    passed &= Check(rotatedPose &&
        std::abs(rotatedPose->position.x - 107.5F) < 0.001F &&
        std::abs(rotatedPose->position.y - 15.0F) < 0.001F &&
        std::abs(rotatedPose->position.z - 22.5F) < 0.001F,
        "Actor rotation remains immediate while position continues smoothing");
    passed &= Check(visibilityProbe.traceCount_ == 10,
        "orientation tracking does not rerun visibility rays");
    passed &= Check(cameraControl.ApplyCount() == 3,
        "orientation tracking applies the updated pose once per update");

    sceneSource.anchorAvailable_ = false;
    camera.Update(1.0F / 60.0F);
    const auto retainedPose = cameraControl.LastPose();
    passed &= Check(rotatedPose && retainedPose &&
        std::abs(retainedPose->position.x - rotatedPose->position.x) < 0.001F &&
        std::abs(retainedPose->position.y - rotatedPose->position.y) < 0.001F &&
        std::abs(retainedPose->position.z - rotatedPose->position.z) < 0.001F &&
        cameraControl.OwnsCamera(),
        "temporary body-center loss retains the previous pose and camera ownership");
    sceneSource.anchorAvailable_ = true;
    camera.Update(0.0F);
    passed &= Check(cameraControl.LastPose() && retainedPose &&
        std::abs(cameraControl.LastPose()->position.x - retainedPose->position.x) < 0.001F,
        "paused updates preserve the filtered anchor even when samples recover");
    camera.Update(0.15F);
    passed &= Check(cameraControl.LastPose() &&
        std::abs(cameraControl.LastPose()->position.x - 108.75F) < 0.001F,
        "sample recovery resumes smoothing without accumulating missing time");
    sceneSource.actorForward_ = { 0.0F, -1.0F, 0.0F };

    const auto applyCountBeforeDebug = cameraControl.ApplyCount();
    const auto rayCountBeforeDebug = visibilityProbe.traceCount_;
    debugVisualization.enabled_ = true;
    passed &= Check(camera.AllowsUpdateWhilePaused(),
        "a debug-mode transition can release the camera while paused");
    camera.Update(1.0F / 60.0F);
    passed &= Check(!cameraControl.OwnsCamera() &&
        cameraControl.ApplyCount() == applyCountBeforeDebug &&
        visibilityProbe.traceCount_ == rayCountBeforeDebug + 10,
        "enabling debug mode releases the camera and refreshes every candidate ray");
    passed &= Check(!camera.AllowsUpdateWhilePaused(),
        "steady debug mode does not track or evaluate while paused");
    camera.RequestPresetStep(1);
    camera.Update(1.0F / 60.0F);
    passed &= Check(debugVisualization.stepCount_ == 1 &&
        !cameraControl.OwnsCamera() &&
        cameraControl.ApplyCount() == applyCountBeforeDebug,
        "A/D changes only the displayed debug preset without applying a camera pose");
    debugVisualization.enabled_ = false;
    passed &= Check(camera.AllowsUpdateWhilePaused(),
        "leaving debug mode can restore normal camera control while paused");
    camera.Update(1.0F / 60.0F);
    passed &= Check(cameraControl.OwnsCamera() &&
        cameraControl.ApplyCount() == applyCountBeforeDebug + 1 &&
        visibilityProbe.traceCount_ == rayCountBeforeDebug + 20,
        "disabling debug mode reevaluates candidates and restores scene camera control");

    camera.RequestPresetStep(1);
    camera.Update(1.0F / 60.0F);
    feedback = previewService.Feedback();
    passed &= Check(feedback && feedback->visibilityEvaluation &&
        feedback->visibilityEvaluation->selectedPresetID ==
            std::optional<std::string>{ "edited" },
        "manual preset stepping selects the alternate preset");

    previewService.BeginPreviewSession();
    const auto revision = previewService.SetPreview(editedBlockedTransform, "edited");
    camera.Update(1.0F / 60.0F);
    feedback = previewService.Feedback();
    passed &= Check(feedback && feedback->previewApplied &&
        feedback->appliedRevision == revision,
        "editor preview applies the changed preset before save");

    presetProvider.Update("edited", editedBlockedTransform);
    previewService.EndPreviewSession();
    previewService.ClearPreview("edited");
    camera.Update(1.0F / 60.0F);
    feedback = previewService.Feedback();
    passed &= Check(feedback && feedback->visibilityEvaluation &&
        feedback->visibilityEvaluation->selectedPresetID ==
            std::optional<std::string>{ "edited" },
        "closing the editor keeps the explicitly edited preset selected");
    if (feedback && feedback->visibilityEvaluation) {
        const auto edited = std::ranges::find(
            feedback->visibilityEvaluation->candidates,
            "edited",
            &ssc::core::CameraCandidateVisibility::presetID);
        passed &= Check(edited != feedback->visibilityEvaluation->candidates.end() &&
            !edited->usable,
            "the explicit edited selection is retained even when visibility marks it blocked");
    }
    passed &= Check(feedback && feedback->currentTransform &&
        feedback->currentTransform->orbit.yawDegrees == 90.0F,
        "normal scene camera applies the saved edited transform after editor close");

    camera.Reset("test complete");

    camera.Update(1.0F / 60.0F);
    passed &= Check(!camera.NeedsUpdate(),
        "the reset camera reaches an idle update state");
    debugVisualization.enabled_ = true;
    camera.RequestPresetStep(1);
    passed &= Check(!camera.NeedsUpdate(),
        "A/D outside an active scene does not leave a pending debug step");
    debugVisualization.enabled_ = false;

    TestSceneSource releaseRetrySceneSource;
    TestPresetProvider releaseRetryPresetProvider({ { "default", defaultTransform } });
    ssc::runtime::PresetPreviewService releaseRetryPreviewService;
    TestCameraControl releaseRetryCameraControl;
    TestVisibilityProbe releaseRetryVisibilityProbe;
    TestDebugVisualization releaseRetryDebugVisualization;
    ssc::SceneCamera releaseRetryCamera;
    releaseRetryCamera.Configure(
        releaseRetrySceneSource,
        releaseRetryPresetProvider,
        releaseRetryPreviewService,
        releaseRetryCameraControl,
        releaseRetryVisibilityProbe,
        releaseRetryDebugVisualization);
    releaseRetryCamera.HandleSceneEvent({
        ssc::runtime::SceneEventType::kAnimationStart,
        sceneKey,
        participants,
    });
    releaseRetryCamera.Update(1.0F / 60.0F);
    releaseRetryCameraControl.failNextRelease_ = true;
    releaseRetryDebugVisualization.enabled_ = true;
    releaseRetryCamera.Update(1.0F / 60.0F);
    passed &= Check(releaseRetryCameraControl.OwnsCamera() &&
        releaseRetryCamera.AllowsUpdateWhilePaused(),
        "a failed debug-mode release remains pending while paused");
    releaseRetryCamera.Update(1.0F / 60.0F);
    passed &= Check(!releaseRetryCameraControl.OwnsCamera() &&
        !releaseRetryCamera.AllowsUpdateWhilePaused() &&
        releaseRetryCamera.NeedsUpdate() &&
        releaseRetryVisibilityProbe.traceCount_ == 10,
        "the next paused update completes release and keeps scene diagnostics active");
    releaseRetryCamera.RequestPresetStep(1);
    releaseRetryCamera.Update(1.0F / 60.0F);
    passed &= Check(releaseRetryDebugVisualization.stepCount_ == 1,
        "debug preset input remains available after a release retry");
    releaseRetryCamera.Reset("release retry test complete");

    TestSceneSource unavailableSceneSource;
    unavailableSceneSource.anchorAvailable_ = false;
    TestPresetProvider unavailablePresetProvider({ { "default", defaultTransform } });
    ssc::runtime::PresetPreviewService unavailablePreviewService;
    TestCameraControl unavailableCameraControl;
    TestVisibilityProbe unavailableVisibilityProbe;
    TestDebugVisualization unavailableDebugVisualization;
    ssc::SceneCamera unavailableCamera;
    unavailableCamera.Configure(
        unavailableSceneSource,
        unavailablePresetProvider,
        unavailablePreviewService,
        unavailableCameraControl,
        unavailableVisibilityProbe,
        unavailableDebugVisualization);
    unavailableCamera.HandleSceneEvent({
        ssc::runtime::SceneEventType::kAnimationStart,
        sceneKey,
        participants,
    });
    unavailableCamera.Update(1.0F / 60.0F);
    passed &= Check(!unavailableCameraControl.OwnsCamera() &&
        unavailableVisibilityProbe.traceCount_ == 0,
        "an unavailable initial body center does not evaluate presets or acquire the camera");
    unavailableSceneSource.anchorAvailable_ = true;
    unavailableSceneSource.bodyCenter_ = { 10.0F, 20.0F, 30.0F };
    unavailableCamera.Update(1.0F / 60.0F);
    passed &= Check(unavailableCameraControl.OwnsCamera() &&
        unavailableVisibilityProbe.traceCount_ == 5,
        "initial scene evaluation retries and acquires after the body center becomes available");
    passed &= Check(unavailableDebugVisualization.anchorPosition_.x == 10.0F &&
        unavailableDebugVisualization.anchorPosition_.z == 30.0F,
        "first valid anchor starts at the torso instead of smoothing from the origin");
    unavailableCamera.Reset("retry test complete");
    unavailableSceneSource.bodyCenter_ = { -20.0F, -30.0F, 15.0F };
    unavailableCamera.HandleSceneEvent({
        ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
    unavailableCamera.Update(1.0F / 60.0F);
    passed &= Check(unavailableDebugVisualization.anchorPosition_.x == -20.0F &&
        unavailableDebugVisualization.anchorPosition_.z == 15.0F,
        "a new scene after reset does not inherit the previous filtered position");
    unavailableCamera.Reset("smoothing reset test complete");

    TestSceneSource noPresetSceneSource;
    TestPresetProvider noPresetProvider(ssc::runtime::CameraPresetSnapshot{});
    ssc::runtime::PresetPreviewService noPresetPreviewService;
    TestCameraControl noPresetCameraControl;
    TestVisibilityProbe noPresetVisibilityProbe;
    TestDebugVisualization noPresetDebugVisualization;
    ssc::SceneCamera noPresetCamera;
    noPresetCamera.Configure(
        noPresetSceneSource,
        noPresetProvider,
        noPresetPreviewService,
        noPresetCameraControl,
        noPresetVisibilityProbe,
        noPresetDebugVisualization);
    noPresetCamera.HandleSceneEvent({
        ssc::runtime::SceneEventType::kAnimationStart,
        sceneKey,
        participants,
    });
    noPresetCamera.Update(1.0F / 60.0F);
    passed &= Check(noPresetCamera.NeedsUpdate() &&
        !noPresetCameraControl.OwnsCamera() &&
        noPresetVisibilityProbe.traceCount_ == 0,
        "an active scene keeps updating without packaged presets or camera ownership");
    const auto noPresetCollectionCount = noPresetSceneSource.anchorCollectionCount_;
    noPresetSceneSource.bodyCenter_ = { 5.0F, 6.0F, 7.0F };
    noPresetCamera.Update(1.0F / 60.0F);
    passed &= Check(
        noPresetSceneSource.anchorCollectionCount_ == noPresetCollectionCount + 1,
        "body-center tracking continues while an active scene has no presets");
    noPresetCamera.Reset("no-preset tracking test complete");

    TestSceneSource losSceneSource;
    TestPresetProvider losPresets({
        { "default", defaultTransform }, { "alternate", alternateTransform } });
    ssc::runtime::PresetPreviewService losPreview;
    TestCameraControl losControl;
    TestVisibilityProbe losProbe;
    TestDebugVisualization losDebug;
    losDebug.enabled_ = true;
    ssc::SceneCamera losCamera;
    losCamera.Configure(losSceneSource, losPresets, losPreview, losControl, losProbe, losDebug);
    losCamera.HandleSceneEvent({
        ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
    losCamera.Update(0.0F);
    const auto originalEvaluation = losPreview.Feedback()->visibilityEvaluation;
    const auto initialQueries = losProbe.traceCount_;
    losCamera.Update(0.25F);
    passed &= Check(losProbe.traceCount_ == initialQueries,
        "periodic anchor LOS does not run before the interval");
    losSceneSource.bodyCenter_ = { 20.0F, 30.0F, 40.0F };
    losProbe.queriesPerTrace_ = 3;
    losCamera.Update(0.25F);
    passed &= Check(losProbe.traceCount_ == initialQueries + 10 && losDebug.snapshot_ &&
        losDebug.snapshot_->anchorLOS && losDebug.snapshot_->anchorLOS->sampleCount == 2 &&
        losDebug.snapshot_->anchorLOS->rayQueryCount == 30,
        "each interval measures all presets and counts physical queries including traversal");
    passed &= Check(losProbe.lastTargetActorID_ == 0x14,
        "all five LOS rays retain the player identity for self filtering");
    if (losDebug.snapshot_ && losDebug.snapshot_->anchorLOS) {
        const auto& first = losDebug.snapshot_->candidates.front();
        passed &= Check(first.pose && first.points.size() == 5 &&
            first.points.front().point == ssc::core::VisibilityPoint::kAnchor &&
            std::abs(first.points.front().target.x - losDebug.anchorPosition_.x) < 0.001F &&
            std::abs(first.points.front().target.x - losSceneSource.bodyCenter_.x) > 0.1F &&
            std::abs(first.pose->position.x - losDebug.anchorPosition_.x) < 0.001F &&
            std::abs(first.pose->position.y - (losDebug.anchorPosition_.y - 100.0F)) < 0.001F,
            "candidate pose and LOS target use the same current anchor");
        passed &= Check(first.points.size() == 5 &&
            std::abs(first.points[2].rayStart.x - first.points[1].rayStart.x - 32.0F) < 0.001F &&
            std::abs(first.points[1].rayStart.z - first.points[3].rayStart.z - 18.0F) < 0.001F,
            "diagnostic rays start at the camera-side corners of a fixed 32 by 18 rectangle");
        for (const auto& candidate : losDebug.snapshot_->candidates) {
            for (const auto& point : candidate.points) {
                passed &= Check(std::abs(point.target.x - losDebug.anchorPosition_.x) < 0.001F &&
                    std::abs(point.target.y - losDebug.anchorPosition_.y) < 0.001F &&
                    std::abs(point.target.z - losDebug.anchorPosition_.z) < 0.001F,
                    "every camera-side ray converges on the same smoothed anchor");
            }
        }
        const auto& lastPoint = losDebug.snapshot_->candidates.back().points.back();
        passed &= Check(std::abs(losProbe.lastStart_.x - lastPoint.rayStart.x) < 0.001F &&
            std::abs(losProbe.lastStart_.y - lastPoint.rayStart.y) < 0.001F &&
            std::abs(losProbe.lastStart_.z - lastPoint.rayStart.z) < 0.001F &&
            std::abs(losProbe.lastTarget_.x - lastPoint.target.x) < 0.001F &&
            std::abs(losProbe.lastTarget_.y - lastPoint.target.y) < 0.001F &&
            std::abs(losProbe.lastTarget_.z - lastPoint.target.z) < 0.001F,
            "the physics query uses the displayed corner origin and anchor endpoint");
        const auto& metrics = *losDebug.snapshot_->anchorLOS;
        passed &= Check(metrics.lastMilliseconds >= metrics.traceMilliseconds &&
            metrics.maximumMilliseconds >= metrics.lastMilliseconds &&
            metrics.averageMilliseconds <= metrics.maximumMilliseconds,
            "initial timing reports total and LOS costs consistently");
    }
    losSceneSource.bodyCenter_ = losDebug.anchorPosition_;
    losProbe.blockedAboveZ_ = losSceneSource.bodyCenter_.z + 1.0F;
    losCamera.Update(0.5F);
    if (losDebug.snapshot_ && losDebug.snapshot_->anchorLOS) {
        const auto& points = losDebug.snapshot_->candidates.front().points;
        using Status = ssc::core::VisibilityPointStatus;
        passed &= Check(points.size() == 5 && points[0].status == Status::kVisible &&
            points[1].status == Status::kObstructed && points[2].status == Status::kObstructed &&
            points[3].status == Status::kVisible && points[4].status == Status::kVisible,
            "center and four corner LOS results remain independent under partial occlusion");
    }
    losProbe.blockedAboveZ_.reset();
    losProbe.forceBlocked_ = true;
    losCamera.Update(0.5F);
    passed &= Check(losDebug.snapshot_ && losDebug.snapshot_->anchorLOS &&
        !losDebug.snapshot_->candidates.front().usable &&
        !losControl.OwnsCamera() && losControl.ApplyCount() == 0 &&
        losPreview.Feedback()->visibilityEvaluation != originalEvaluation &&
        losPreview.Feedback()->visibilityEvaluation->selectedPresetID == originalEvaluation->selectedPresetID,
        "blocked periodic LOS updates choices while retaining the selected preset and camera control");
    losProbe.querySucceeded_ = false;
    const auto queriesBeforeStall = losProbe.traceCount_;
    losCamera.Update(2.0F);
    passed &= Check(losProbe.traceCount_ == queriesBeforeStall + 10 &&
        losDebug.snapshot_->candidates.front().points.front().status ==
            ssc::core::VisibilityPointStatus::kUnavailable,
        "a long update runs one batch and reports failed queries as unavailable");
    const auto queriesBeforePause = losProbe.traceCount_;
    losCamera.Update(0.0F);
    losCamera.Update(std::numeric_limits<float>::quiet_NaN());
    losSceneSource.anchorAvailable_ = false;
    losCamera.Update(1.0F);
    losSceneSource.anchorAvailable_ = true;
    losCamera.Update(0.25F);
    passed &= Check(losProbe.traceCount_ == queriesBeforePause + 10,
        "anchor recovery immediately refreshes choices without accumulating pause or missing time");
    losProbe.forceBlocked_ = false;
    losProbe.querySucceeded_ = true;
    losDebug.enabled_ = false;
    losCamera.Update(0.0F);
    const auto queriesAfterDisable = losProbe.traceCount_;
    losCamera.Update(1.0F);
    passed &= Check(losProbe.traceCount_ == queriesAfterDisable + 10 &&
        losControl.OwnsCamera() && losDebug.snapshot_ && losDebug.snapshot_->anchorLOS,
        "normal mode also reevaluates choices periodically");
    losDebug.enabled_ = true;
    losCamera.Update(0.5F);
    passed &= Check(losDebug.snapshot_ && losDebug.snapshot_->anchorLOS &&
        losDebug.snapshot_->anchorLOS->sampleCount == 1,
        "a new debug session starts fresh timing statistics");
    losCamera.Reset("anchor LOS benchmark test complete");
    const auto queriesAfterReset = losProbe.traceCount_;
    losCamera.Update(1.0F);
    passed &= Check(!losDebug.snapshot_ && losProbe.traceCount_ == queriesAfterReset,
        "reset clears benchmark display and prevents scene-external queries");

    TestSceneSource dynamicSource;
    TestPresetProvider dynamicPresets({
        { "first", defaultTransform }, { "middle", alternateTransform },
        { "last", defaultTransform } });
    ssc::runtime::PresetPreviewService dynamicPreview;
    TestCameraControl dynamicControl;
    TestVisibilityProbe dynamicProbe;
    TestDebugVisualization dynamicDebug;
    ssc::SceneCamera dynamicCamera;
    dynamicCamera.Configure(dynamicSource, dynamicPresets, dynamicPreview,
        dynamicControl, dynamicProbe, dynamicDebug);
    const auto selected = [&]() { return dynamicPreview.Feedback()->visibilityEvaluation->selectedPresetID; };
    const auto startDynamic = [&]() {
        dynamicCamera.HandleSceneEvent({
            ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
        dynamicCamera.Update(0.016F);
    };
    startDynamic();
    dynamicPresets.Update("middle", editedBlockedTransform);
    dynamicCamera.Update(0.5F);
    passed &= Check(selected() == "first" && dynamicControl.OwnsCamera(),
        "periodic blocking only changes choices");
    dynamicCamera.RequestPresetStep(1);
    dynamicCamera.Update(0.016F);
    passed &= Check(selected() == "last", "D skips a newly blocked middle preset");
    dynamicCamera.RequestPresetStep(-1);
    dynamicCamera.Update(0.016F);
    passed &= Check(selected() == "first", "A skips a newly blocked middle preset");
    dynamicProbe.forceBlocked_ = true;
    dynamicCamera.Update(0.5F);
    passed &= Check(selected() == "first" && dynamicControl.OwnsCamera() &&
        !dynamicPreview.Feedback()->visibilityEvaluation->candidates[0].usable,
        "all blocked retains the current camera and selection");
    dynamicCamera.RequestPresetStep(1);
    dynamicCamera.Update(0.016F);
    passed &= Check(selected() == "first" && dynamicControl.OwnsCamera(),
        "A/D with no choices keeps the active camera");
    dynamicProbe.forceBlocked_ = false;
    dynamicPresets.Update("middle", alternateTransform);
    dynamicCamera.Update(0.5F);
    passed &= Check(selected() == "first", "recovery never switches the current camera");
    dynamicCamera.RequestPresetStep(1);
    dynamicCamera.Update(0.016F);
    passed &= Check(selected() == "middle", "recovered candidates return to A/D choices");
    dynamicSource.anchorAvailable_ = false;
    const auto beforeMissing = dynamicProbe.traceCount_;
    dynamicCamera.RequestPresetStep(1);
    dynamicCamera.Update(0.5F);
    passed &= Check(selected() == "middle" && dynamicProbe.traceCount_ == beforeMissing,
        "missing anchor rejects pending A/D without casting stale rays");
    dynamicSource.anchorAvailable_ = true;
    dynamicCamera.Update(0.016F);
    passed &= Check(dynamicProbe.traceCount_ == beforeMissing + 15,
        "anchor recovery immediately refreshes A/D choices");
    dynamicPreview.BeginPreviewSession();
    const auto beforeEditor = dynamicProbe.traceCount_;
    dynamicCamera.Update(1.0F);
    passed &= Check(dynamicProbe.traceCount_ == beforeEditor, "editor suspends periodic evaluation");
    dynamicPreview.EndPreviewSession();
    dynamicCamera.Reset("dynamic choice test complete");
    dynamicProbe.forceBlocked_ = true;
    startDynamic();
    passed &= Check(!selected() && !dynamicControl.OwnsCamera(),
        "all blocked at scene start leaves normal camera active");
    dynamicProbe.forceBlocked_ = false;
    dynamicCamera.Update(0.5F);
    passed &= Check(!selected() && !dynamicControl.OwnsCamera(),
        "first candidate recovery does not automatically acquire the camera");
    dynamicCamera.RequestPresetStep(1);
    dynamicCamera.Update(0.016F);
    passed &= Check(selected() == "first" && dynamicControl.OwnsCamera(),
        "D explicitly acquires the first recovered candidate");
    dynamicCamera.Reset("dynamic acquisition test complete");

    // R1-01: retry an explicit debug-off request using real elapsed time, even while paused.
    TestSceneSource resumeSource;
    TestPresetProvider resumePresets({ { "only", defaultTransform } });
    ssc::runtime::PresetPreviewService resumePreview;
    TestCameraControl resumeControl;
    TestVisibilityProbe resumeProbe;
    TestDebugVisualization resumeDebug;
    auto resumeNow = ssc::SceneCamera::Clock::time_point{};
    ssc::SceneCamera resumeCamera([&] { return resumeNow; });
    resumeCamera.Configure(resumeSource, resumePresets, resumePreview,
        resumeControl, resumeProbe, resumeDebug);
    resumeCamera.HandleSceneEvent({
        ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
    resumeCamera.Update(0.0F);
    const auto refuseResume = [&](unsigned a_failures) {
        resumeDebug.enabled_ = true;
        resumeCamera.Update(0.0F);
        resumeControl.acquireFailures_ = a_failures;
        resumeDebug.enabled_ = false;
        resumeCamera.Update(0.0F);
    };
    refuseResume(1);
    const auto afterRefusal = resumeControl.acquireCount_;
    const auto afterResumeEvaluation = resumeProbe.traceCount_;
    passed &= Check(!resumeControl.OwnsCamera() && resumeCamera.AllowsUpdateWhilePaused(),
        "R1-01 transient refusal keeps the explicit resume pending while paused");
    resumeNow += std::chrono::milliseconds{ 499 };
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.acquireCount_ == afterRefusal,
        "resume attempts are not repeated on every paused frame");
    resumeNow += std::chrono::milliseconds{ 1 };
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.OwnsCamera() && !resumeCamera.AllowsUpdateWhilePaused() &&
        resumeControl.acquireCount_ == afterRefusal + 1 && resumeProbe.traceCount_ == afterResumeEvaluation,
        "a single preset resumes after refusal clears without another LOS batch");

    refuseResume(20);
    const auto firstPersistentAttempt = resumeControl.acquireCount_;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        resumeNow += std::chrono::milliseconds{ 500 };
        resumeCamera.Update(0.0F);
    }
    passed &= Check(resumeControl.acquireCount_ == firstPersistentAttempt + 2 &&
        !resumeControl.OwnsCamera() && !resumeCamera.AllowsUpdateWhilePaused() &&
        resumePreview.Feedback()->message.find("Could not resume") != std::string::npos,
        "persistent refusal stops after three total attempts and reports failure");
    resumeNow += std::chrono::seconds{ 5 };
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.acquireCount_ == firstPersistentAttempt + 2,
        "the exhausted request never retries on later updates");
    refuseResume(0);
    passed &= Check(resumeControl.OwnsCamera(), "another explicit toggle can resume after exhaustion");

    refuseResume(20);
    const auto beforeDeadline = resumeControl.acquireCount_;
    resumeSource.anchorAvailable_ = false;
    resumeNow += std::chrono::seconds{ 2 };
    resumeCamera.Update(0.0F);
    resumeSource.anchorAvailable_ = true;
    resumeCamera.Update(0.016F);
    passed &= Check(resumeControl.acquireCount_ == beforeDeadline &&
        !resumeControl.OwnsCamera() && !resumeCamera.AllowsUpdateWhilePaused(),
        "deadline cancels resume even when anchor samples disappear, without delayed acquisition");
    refuseResume(20);
    const auto beforeCancel = resumeControl.acquireCount_;
    resumeDebug.enabled_ = true;
    resumeCamera.Update(0.0F);
    resumeNow += std::chrono::seconds{ 1 };
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.acquireCount_ == beforeCancel &&
        !resumeCamera.AllowsUpdateWhilePaused(), "debug on cancels pending acquisition");
    resumeDebug.enabled_ = false;
    resumeCamera.Update(0.0F);
    resumePreview.BeginPreviewSession();
    const auto beforeEditorCancel = resumeControl.acquireCount_;
    resumeCamera.Update(0.0F);
    resumePreview.EndPreviewSession();
    resumeNow += std::chrono::seconds{ 1 };
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.acquireCount_ == beforeEditorCancel &&
        !resumeCamera.AllowsUpdateWhilePaused(), "editing cancels a pending debug resume");
    refuseResume(20);
    const auto beforeResetCancel = resumeControl.acquireCount_;
    resumeCamera.Reset("R1-01 cancellation test");
    resumeNow += std::chrono::seconds{ 1 };
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.acquireCount_ == beforeResetCancel &&
        !resumeCamera.NeedsUpdate() && !resumeCamera.AllowsUpdateWhilePaused(),
        "reset cancels pending resume and leaves no retry work");

    resumeProbe.forceBlocked_ = true;
    resumeDebug.enabled_ = true;
    resumeCamera.HandleSceneEvent({
        ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
    resumeCamera.Update(0.0F);
    resumeDebug.enabled_ = false;
    const auto beforeUnselectedResume = resumeControl.acquireCount_;
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.acquireCount_ == beforeUnselectedResume &&
        !resumeCamera.AllowsUpdateWhilePaused() &&
        resumePreview.Feedback()->message == "No camera preset meets the anchor LOS conditions",
        "debug off with no selection leaves normal camera and reports current LOS conditions");
    resumeCamera.Reset("R1-01 unselected resume test");

    // R1-01-C1: an old non-null editor-close request cannot bypass the retry budget.
    resumeProbe.forceBlocked_ = false;
    resumeControl.acquireFailures_ = 0;
    resumeCamera.HandleSceneEvent({
        ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
    resumeCamera.Update(0.0F);
    resumePreview.BeginPreviewSession();
    static_cast<void>(resumePreview.SetPreview(defaultTransform, "only"));
    resumeCamera.Update(0.0F);
    resumePreview.EndPreviewSession();
    resumePreview.ClearPreview("only");
    resumeCamera.Update(0.0F);
    passed &= Check(resumePreview.Request() && !resumePreview.Request()->transform,
        "retry bypass regression fixture retains a non-null editor-close request");
    resumeDebug.enabled_ = true;
    resumeCamera.Update(0.0F);
    resumeSource.anchorAvailable_ = false;
    resumeDebug.enabled_ = false;
    resumeControl.acquireFailures_ = 20;
    const auto beforeMissingResume = resumeControl.acquireCount_;
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.acquireCount_ == beforeMissingResume,
        "old editor-close request cannot acquire while resume waits for anchor input");
    resumeSource.anchorAvailable_ = true;
    resumeNow += std::chrono::milliseconds{ 1 };
    resumeCamera.Update(0.0F);
    resumeNow += std::chrono::milliseconds{ 499 };
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.acquireCount_ == beforeMissingResume + 1,
        "all acquisition paths respect spacing after the first recovered-anchor attempt");
    resumeNow += std::chrono::milliseconds{ 1 };
    resumeCamera.Update(0.0F);
    resumeNow += std::chrono::milliseconds{ 500 };
    resumeCamera.Update(0.0F);
    passed &= Check(resumeControl.acquireCount_ == beforeMissingResume + 3 &&
        !resumeCamera.AllowsUpdateWhilePaused(),
        "old preview request does not add a fourth acquisition attempt");
    resumeDebug.enabled_ = true;
    resumeCamera.Update(0.0F);
    resumeSource.anchorAvailable_ = false;
    resumeDebug.enabled_ = false;
    const auto beforeNoAttemptTimeout = resumeControl.acquireCount_;
    resumeCamera.Update(0.0F);
    resumeNow += std::chrono::seconds{ 2 };
    resumeCamera.Update(0.0F);
    resumeSource.anchorAvailable_ = true;
    resumeCamera.Update(0.016F);
    passed &= Check(resumeControl.acquireCount_ == beforeNoAttemptTimeout &&
        !resumeCamera.AllowsUpdateWhilePaused() && !resumeControl.OwnsCamera(),
        "expiry before the first attempt cannot revive an old editor-close request");
    resumeCamera.Reset("R1-01-C1 regression test");

    // R1-02: discard a new preview and restore the pre-editor selection, even if blocked.
    dynamicProbe.forceBlocked_ = false;
    startDynamic();
    dynamicPresets.Update("first", editedBlockedTransform);
    dynamicCamera.Update(0.5F);
    dynamicPreview.BeginPreviewSession();
    static_cast<void>(dynamicPreview.SetPreview(defaultTransform));
    dynamicCamera.Update(0.0F);
    dynamicPreview.EndPreviewSession();
    dynamicPreview.ClearPreview();
    dynamicCamera.Update(0.016F);
    passed &= Check(selected() == "first" && dynamicControl.OwnsCamera() &&
        dynamicPreview.Feedback()->currentTransform->orbit.yawDegrees == 90.0F &&
        !dynamicPreview.Feedback()->visibilityEvaluation->candidates[0].usable,
        "R1-02 discarding a new preview restores the blocked pre-editor preset");
    dynamicPreview.BeginPreviewSession();
    static_cast<void>(dynamicPreview.SetPreview(defaultTransform));
    dynamicCamera.Update(0.0F);
    dynamicPresets.Remove("first");
    dynamicPreview.EndPreviewSession();
    dynamicPreview.ClearPreview("first");
    dynamicCamera.Update(0.016F);
    passed &= Check(!selected() && !dynamicControl.OwnsCamera(),
        "a deleted pre-editor preset does not select another usable candidate");
    dynamicCamera.Reset("R1-02 restore test");
    dynamicProbe.forceBlocked_ = true;
    startDynamic();
    dynamicPreview.BeginPreviewSession();
    static_cast<void>(dynamicPreview.SetPreview(defaultTransform));
    dynamicCamera.Update(0.0F);
    dynamicProbe.forceBlocked_ = false;
    dynamicPreview.EndPreviewSession();
    dynamicPreview.ClearPreview();
    dynamicCamera.Update(0.016F);
    dynamicCamera.Update(0.5F);
    passed &= Check(!selected() && !dynamicControl.OwnsCamera(),
        "no pre-editor selection restores normal camera despite newly usable presets");
    dynamicCamera.Reset("R1-02 no-selection test");

    // T35/T37: failed termination must only release, including paused updates.
    for (const auto failure : { ssc::runtime::CameraReleaseResult::kFailed,
             ssc::runtime::CameraReleaseResult::kWrongThread }) {
        for (unsigned termination = 0; termination < 3; ++termination) {
            TestSceneSource stopSource;
            TestPresetProvider stopPresets({ { "default", defaultTransform } });
            ssc::runtime::PresetPreviewService stopPreview;
            TestCameraControl stopControl;
            TestVisibilityProbe stopProbe;
            TestDebugVisualization stopDebug;
            ssc::SceneCamera stopCamera;
            stopCamera.Configure(stopSource, stopPresets, stopPreview,
                stopControl, stopProbe, stopDebug);
            stopCamera.HandleSceneEvent({
                ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
            stopCamera.Update(0.016F);
            stopPreview.BeginPreviewSession();
            static_cast<void>(stopPreview.SetPreview(alternateTransform, "draft"));
            stopCamera.Update(0.0F);
            stopCamera.HandleSceneEvent({
                ssc::runtime::SceneEventType::kAnimationChange, sceneKey, participants });

            stopControl.releaseFailureResult_ = failure;
            stopControl.releaseFailures_ = termination == 2 ? 3 : 4;
            stopControl.failEmergencyRelease_ = termination == 2;
            if (termination == 0) {
                stopCamera.HandleSceneEvent({
                    ssc::runtime::SceneEventType::kAnimationEnd, sceneKey, participants });
            } else if (termination == 1) {
                stopCamera.Reset("failed reset regression");
            } else {
                stopCamera.EmergencyReset();
            }
            passed &= Check(stopControl.OwnsCamera() && stopCamera.NeedsUpdate() &&
                stopCamera.AllowsUpdateWhilePaused() &&
                !stopPreview.Feedback()->sceneActive &&
                !stopPreview.Feedback()->previewApplied &&
                !stopPreview.PreviewSessionActive() && !stopPreview.Request(),
                "failed termination locks editing, cancels preview, and keeps release pending");

            const auto stopApplies = stopControl.ApplyCount();
            const auto stopAcquires = stopControl.acquireCount_;
            const auto stopTraces = stopProbe.traceCount_;
            const auto stopAnchors = stopSource.anchorCollectionCount_;
            const ssc::runtime::SceneKey nextKey{ sceneKey.sourceID, sceneKey.instanceID + 1 };
            stopCamera.HandleSceneEvent({
                ssc::runtime::SceneEventType::kAnimationChange, sceneKey, participants });
            stopCamera.HandleSceneEvent({
                ssc::runtime::SceneEventType::kAnimationStart, nextKey, participants });
            stopCamera.RequestPresetStep(1);
            stopCamera.Update(0.0F);
            passed &= Check(stopControl.OwnsCamera() &&
                stopControl.ApplyCount() == stopApplies &&
                stopControl.acquireCount_ == stopAcquires &&
                stopProbe.traceCount_ == stopTraces &&
                stopSource.anchorCollectionCount_ == stopAnchors &&
                !stopPreview.Feedback()->sceneActive,
                "release failures cannot evaluate, acquire, apply, or accept a new scene");

            stopCamera.Update(0.0F);
            passed &= Check(!stopControl.OwnsCamera() && !stopCamera.NeedsUpdate() &&
                !stopCamera.AllowsUpdateWhilePaused() &&
                stopControl.releaseCount_ == (termination == 2 ? 4U : 5U),
                "paused release retry reaches idle without reviving canceled work");
            stopCamera.HandleSceneEvent({
                ssc::runtime::SceneEventType::kAnimationStart, nextKey, participants });
            stopCamera.Update(0.016F);
            passed &= Check(stopControl.OwnsCamera() &&
                stopPreview.Feedback()->sceneActive &&
                stopPreview.Feedback()->currentTransform &&
                stopPreview.Feedback()->currentTransform->orbit.yawDegrees ==
                    defaultTransform.orbit.yawDegrees &&
                !stopPreview.PreviewSessionActive(),
                "a fresh start uses saved presets without applying the old draft");
            stopCamera.Reset("termination regression cleanup");
        }
    }

    // T36: a queued reset wakes the consumer; it must not issue another reset.
    {
        TestSceneSource resetSource;
        TestPresetProvider resetPresets({ { "default", defaultTransform } });
        ssc::runtime::PresetPreviewService resetPreview;
        TestCameraControl resetControl;
        TestVisibilityProbe resetProbe;
        TestDebugVisualization resetDebug;
        ssc::SceneCamera resetCamera;
        resetCamera.Configure(resetSource, resetPresets, resetPreview,
            resetControl, resetProbe, resetDebug);
        resetCamera.HandleSceneEvent({
            ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
        resetCamera.Update(0.016F);
        resetCamera.RequestReset();
        passed &= Check(resetCamera.AllowsUpdateWhilePaused(),
            "a reset request alone permits paused release processing");
        const ssc::runtime::SceneKey nextKey{ sceneKey.sourceID, sceneKey.instanceID + 1 };
        resetCamera.HandleSceneEvent({
            ssc::runtime::SceneEventType::kAnimationStart, nextKey, participants });
        resetCamera.Update(0.016F);
        const auto releasedBeforeWakeup = resetControl.releaseCount_;
        const auto appliedBeforeWakeup = resetControl.ApplyCount();
        ssc::runtime::IRuntimeClient& runtimeClient = resetCamera;
        passed &= Check(runtimeClient.ProcessPendingReset("delayed reset task") &&
            runtimeClient.ProcessPendingReset("duplicate reset task") &&
            resetControl.OwnsCamera() && resetPreview.Feedback()->sceneActive &&
            resetControl.releaseCount_ == releasedBeforeWakeup &&
            resetControl.ApplyCount() == appliedBeforeWakeup,
            "an already consumed reset cannot terminate the next scene");
        resetCamera.RequestReset();
        passed &= Check(runtimeClient.ProcessPendingReset("new reset task") &&
            !resetControl.OwnsCamera() && !resetCamera.NeedsUpdate(),
            "a new reset remains effective after a stale wakeup");

        // An editor-close request must not cross the scene boundary either.
        resetCamera.HandleSceneEvent({
            ssc::runtime::SceneEventType::kAnimationStart, nextKey, participants });
        resetCamera.Update(0.016F);
        resetPreview.BeginPreviewSession();
        static_cast<void>(resetPreview.SetPreview(alternateTransform, "old"));
        resetCamera.Update(0.0F);
        resetPreview.EndPreviewSession();
        resetPreview.ClearPreview("missing-old-preset");
        resetCamera.Reset("reset with pending editor close");
        passed &= Check(!resetPreview.Request() && !resetPreview.PreviewSessionActive(),
            "reset discards both preview and editor-close requests");
        resetCamera.HandleSceneEvent({
            ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
        resetCamera.Update(0.016F);
        passed &= Check(resetControl.OwnsCamera() &&
            resetPreview.Feedback()->visibilityEvaluation->selectedPresetID ==
                std::optional<std::string>{ "default" },
            "an old editor close cannot change selection or release the next scene");
        resetCamera.Reset("reset wakeup regression cleanup");
    }

    {
        TestSceneSource source;
        ssc::runtime::PresetPreviewService preview;
        TestCameraControl output;
        TestVisibilityProbe probe;
        TestDebugVisualization debug;
        TestTargetLockControl target;
        auto now = ssc::SceneCamera::Clock::time_point{};
        ssc::SceneCamera startup([&] { return now; });
        startup.Configure(source, presetProvider, preview, output, probe, debug, &target);
        using Event = ssc::runtime::SceneEventType;
        const ssc::runtime::SceneKey otherKey{ 0x01000002, 2 };
        const auto start = [&] { startup.HandleSceneEvent({ Event::kAnimationStart, sceneKey, participants }); };

        startup.HandleSceneEvent({ Event::kAnimationStarting, sceneKey, participants });
        startup.Update(0.016F);
        passed &= Check(target.requests == 0, "Starting alone does not unlock");
        start();
        start();
        startup.HandleSceneEvent({ Event::kAnimationStart, otherKey, participants });
        passed &= Check(target.requests == 0 && target.finishAttempts == 0,
            "start event only publishes intent; duplicates and another scene do not call API");
        startup.Update(0.016F);
        passed &= Check(target.requests == 1 && target.releases == 0 && source.anchorCollectionCount_ == 0,
            "first camera update requests unlock before sampling initial anchor");
        passed &= Check(startup.NeedsUpdate() && startup.AllowsUpdateWhilePaused(),
            "pending unlock remains serviceable, including paused cleanup");
        startup.Update(0.016F);
        passed &= Check(target.requests == 1 && output.ApplyCount() == 0 && target.releases == 0,
            "camera waits while TDM still reports a locked target");
        target.locked = false;
        startup.Update(0.016F);
        passed &= Check(target.releases == 1 && !target.lastCancelled && source.anchorCollectionCount_ == 1,
            "unlock confirmation releases disable and allows first anchor evaluation");
        target.locked = true;  // a later manual lock must be left alone
        startup.HandleSceneEvent({ Event::kAnimationChange, sceneKey, {} });
        start();
        startup.Update(0.016F);
        passed &= Check(target.requests == 1 && target.releases == 1,
            "change, duplicate start and manual relock do not request another unlock");
        startup.Reset("next test");

        start();
        startup.HandleSceneEvent({ Event::kAnimationEnd, sceneKey, {} });
        startup.Update(0.016F);
        passed &= Check(target.requests == 1, "end before camera update discards queued unlock");
        start();
        startup.RequestReset();
        startup.Update(0.016F);
        passed &= Check(target.requests == 1, "load/reset before update discards queued unlock");
        startup.HandleSceneEvent({ Event::kAnimationStart, sceneKey, {} });
        startup.Update(0.016F);
        passed &= Check(target.requests == 1, "non-player start does not unlock");

        start();
        startup.Update(0.016F);
        target.canFinish = false;  // models a reset delivered on a worker
        startup.Reset("worker reset");
        passed &= Check(startup.NeedsUpdate() && startup.AllowsUpdateWhilePaused() && target.releases == 1,
            "unavailable cancellation retains pending cleanup");
        start();
        passed &= Check(target.requests == 2, "restoration cannot start another unlock");
        target.canFinish = true;
        startup.Update(0.0F);
        passed &= Check(target.releases == 2 && target.lastCancelled && !startup.NeedsUpdate(),
            "next camera update completes cancellation while paused");

        start();
        startup.Update(0.016F);
        now += std::chrono::milliseconds{ 1999 };
        startup.Update(0.016F);
        passed &= Check(target.releases == 2, "temporary disable remains before timeout");
        now += std::chrono::milliseconds{ 1 };
        startup.Update(0.0F);
        passed &= Check(target.releases == 3 && target.lastCancelled,
            "two-second timeout releases disable even when paused and still locked");
        startup.Reset("timeout cleanup");

        target.available = false;
        const auto samplesBeforeUnavailable = source.anchorCollectionCount_;
        start();
        startup.Update(0.016F);
        passed &= Check(source.anchorCollectionCount_ > samplesBeforeUnavailable && target.releases == 3,
            "absent or rejected unlock does not block initial scene evaluation");
        startup.Reset("unavailable cleanup");

        target.available = true;
        start();
        startup.Update(0.016F);
        target.canFinish = false;
        startup.EmergencyReset();
        passed &= Check(startup.NeedsUpdate(), "emergency reset retains failed cancellation");
        target.canFinish = true;
        startup.Update(0.0F);
        passed &= Check(target.releases == 4 && !startup.NeedsUpdate(),
            "emergency cancellation is retried at next camera update");
    }

    // R1/R2: real worker producers, main consumer, no camera updates during cleanup.
    {
        TestSceneSource source;
        TestPresetProvider presets({ { "default", defaultTransform } });
        ssc::runtime::PresetPreviewService preview;
        TestCameraControl output;
        TestVisibilityProbe probe;
        TestDebugVisualization debug;
        ssc::SceneCamera scene;
        scene.Configure(source, presets, preview, output, probe, debug);
        ssc::runtime::MainUpdateDispatcher dispatcher;
        const auto mainThread = std::this_thread::get_id();
        std::thread::id collectionThread;
        std::vector<int> order;
        std::thread producer([&] {
            dispatcher.Post([&] {
                collectionThread = std::this_thread::get_id();
                const auto collected = source.CollectParticipants(sceneKey);
                scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationStart, sceneKey, collected });
                order.push_back(1);
                dispatcher.Post([&] { order.push_back(3); });
            });
            dispatcher.Post([&] { order.push_back(2); });
        });
        producer.join();
        passed &= Check(collectionThread == std::thread::id{} && order.empty() && !scene.NeedsUpdate(),
            "R2 worker only publishes notifications, without collection or scene mutation");
        dispatcher.Tick(scene);
        passed &= Check(collectionThread == mainThread && order == std::vector<int>{ 1, 2 } && scene.NeedsUpdate(),
            "R2 collection and scene delivery run in order on main consumer; reentrant posts wait");
        dispatcher.Tick(scene);
        passed &= Check(order == std::vector<int>{ 1, 2, 3 }, "new notifications run on following main update");

        scene.Update(0.016F); // acquire once; no camera-state Update after this line
        passed &= Check(output.OwnsCamera(), "R1 camera ownership acquired before losing camera updates");
        const auto applies = output.ApplyCount();
        const auto anchors = source.anchorCollectionCount_;
        output.releaseFailureResult_ = ssc::runtime::CameraReleaseResult::kWrongThread;
        output.releaseFailures_ = 1;
        std::thread resetProducer([&] { scene.RequestReset(); });
        resetProducer.join();
        dispatcher.Tick(scene);
        passed &= Check(output.OwnsCamera() && scene.NeedsUpdate(), "R1 failed release remains pending");
        dispatcher.Tick(scene);
        passed &= Check(!output.OwnsCamera() && !scene.NeedsUpdate() &&
            output.ApplyCount() == applies && source.anchorCollectionCount_ == anchors,
            "R1 main update alone retries and releases without camera update, pose or anchor work");
        const auto releases = output.releaseCount_;
        dispatcher.Tick(scene);
        passed &= Check(output.releaseCount_ == releases, "idle main update does not release twice");

        TestTargetLockControl target;
        scene.Configure(source, presets, preview, output, probe, debug, &target);
        scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
        scene.Update(0.016F); // acquire disable, then stop camera-state updates
        target.canFinish = false;
        std::thread endProducer([&] {
            dispatcher.Post([&] {
                scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationEnd, sceneKey, {} });
            });
        });
        endProducer.join();
        dispatcher.Tick(scene);
        passed &= Check(target.requests == 1 && target.releases == 0 && scene.NeedsUpdate(),
            "R1 end on main consumer retains failed target-disable return");
        target.canFinish = true;
        dispatcher.Tick(scene);
        passed &= Check(target.releases == 1 && target.lastCancelled && !scene.NeedsUpdate(),
            "R1 main update returns TDM disable with no subsequent camera update");
    }

    {
        TestSceneSource source;
        TestPresetProvider presets({ { "default", defaultTransform } });
        ssc::runtime::PresetPreviewService preview;
        TestCameraControl output;
        TestVisibilityProbe probe;
        TestDebugVisualization debug;
        const auto now = ssc::SceneCamera::Clock::time_point{};
        ssc::SceneCamera scene([&] { return now; });
        scene.Configure(source, presets, preview, output, probe, debug);
        using Event = ssc::runtime::SceneEventType;
        scene.HandleSceneEvent({ Event::kAnimationStart, sceneKey, participants });
        scene.Update(0.016F);
        for (const auto event : { Event::kAnimationChange, Event::kActorsRelocated }) {
            const auto rays = probe.traceCount_;
            scene.HandleSceneEvent({ event, sceneKey, {} });
            scene.HandleSceneEvent({ event, sceneKey, {} });
            scene.Update(0.016F);
            const auto evaluatedRays = probe.traceCount_;
            passed &= Check(evaluatedRays > rays && output.OwnsCamera(),
                "animation change and relocation reevaluate without advancing the clock");
            scene.Update(0.016F);
            passed &= Check(probe.traceCount_ == evaluatedRays,
                "duplicate notifications coalesce without repeating evaluation next frame");
            source.anchorAvailable_ = false;
            scene.HandleSceneEvent({ event, sceneKey, {} });
            scene.Update(0.016F);
            passed &= Check(probe.traceCount_ == evaluatedRays,
                "immediate reevaluation still waits for valid body input");
            source.anchorAvailable_ = true;
            scene.Update(0.016F);
            passed &= Check(probe.traceCount_ > evaluatedRays,
                "pending reevaluation runs when input returns without a timed delay");
        }
        scene.Reset("immediate reevaluation regression cleanup");
    }

    for (const bool endWhileFree : { false, true }) {
        TestSceneSource source;
        TestPresetProvider presets({ { "default", defaultTransform } });
        ssc::runtime::PresetPreviewService preview;
        TestCameraControl output;
        TestVisibilityProbe probe;
        TestDebugVisualization debug;
        auto now = ssc::SceneCamera::Clock::time_point{};
        ssc::SceneCamera scene([&] { return now; });
        scene.Configure(source, presets, preview, output, probe, debug);
        ssc::runtime::MainUpdateDispatcher dispatcher;
        using Event = ssc::runtime::SceneEventType;
        scene.HandleSceneEvent({ Event::kAnimationStart, sceneKey, participants });
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        passed &= Check(output.OwnsCamera(), "free camera regression starts with scene ownership");
        for (int cycle = 0; cycle < 2; ++cycle) {
            const auto applies = output.ApplyCount();
            const auto anchors = source.anchorCollectionCount_;
            source.controlState_->freeCamera = true;
            source.controlState_->sceneCameraSupported = false;
            output.failNextRelease_ = true;
            dispatcher.Tick(scene);
            scene.Update(0.016F);
            dispatcher.Tick(scene);
            passed &= Check(!output.OwnsCamera() && scene.NeedsUpdate(),
                "Free retries failed release on main update while retaining the scene");
            now += std::chrono::seconds{ 5 };
            dispatcher.Tick(scene);
            scene.Update(0.016F);
            passed &= Check(output.ApplyCount() == applies && source.anchorCollectionCount_ == anchors,
                "Free with movement locked suspends pose output and anchor evaluation");
            if (endWhileFree) {
                scene.HandleSceneEvent({ Event::kAnimationEnd, sceneKey, {} });
            }
            source.controlState_->freeCamera = false;
            source.controlState_->sceneCameraSupported = true;
            dispatcher.Tick(scene);
            scene.Update(0.016F);
            if (endWhileFree) {
                passed &= Check(!output.OwnsCamera() && !scene.NeedsUpdate(),
                    "ending during Free prevents automatic restart on return");
                break;
            }
            passed &= Check(output.OwnsCamera() && source.anchorCollectionCount_ > anchors,
                "return from Free automatically resumes with fresh anchor, including repeated toggles");
        }
        scene.Reset("free camera regression cleanup");
    }

    // Move Scene: short hotkey and long timeout paths share the same control
    // transitions. Camera updates alone cannot release or resume the suspension.
    for (const auto duration : { std::chrono::seconds{ 4 }, std::chrono::seconds{ 39 } }) {
        TestSceneSource source;
        TestPresetProvider presets({ { "default", defaultTransform }, { "other", alternateTransform } });
        ssc::runtime::PresetPreviewService preview;
        TestCameraControl output;
        TestVisibilityProbe probe;
        TestDebugVisualization debug;
        auto now = ssc::SceneCamera::Clock::time_point{};
        ssc::SceneCamera scene([&] { return now; });
        scene.Configure(source, presets, preview, output, probe, debug);
        ssc::runtime::MainUpdateDispatcher dispatcher;
        scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        const auto oldPose = *output.LastPose();
        const auto oldApplies = output.ApplyCount();
        const auto oldAnchors = source.anchorCollectionCount_;
        source.controlState_->movementEnabled = true;
        dispatcher.Tick(scene); // no camera update is needed to return control
        passed &= Check(!output.OwnsCamera() && output.releaseCount_ == 1 && scene.NeedsUpdate(),
            "movement unlock returns the camera on main update and keeps the scene");
        scene.RequestPresetStep(1);
        // Move along the probe's open corridor (the fake blocks |x| >= 80).
        source.bodyCenter_ = { 20.0F, 1000.0F, 30.0F };
        now += duration;
        scene.Update(0.016F);
        dispatcher.Tick(scene);
        passed &= Check(output.ApplyCount() == oldApplies && source.anchorCollectionCount_ == oldAnchors &&
            output.acquireCount_ == 1 && output.releaseCount_ == 1,
            "movement keeps camera, A/D and anchor work suspended without duplicate external release");

        source.controlState_->movementEnabled = false;
        dispatcher.Tick(scene);
        scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kActorsRelocated, sceneKey, {} });
        dispatcher.Tick(scene);
        passed &= Check(!output.OwnsCamera(), "main update queues resume without applying a camera pose");
        scene.Update(0.016F);
        passed &= Check(output.OwnsCamera() && output.acquireCount_ == 2 &&
            std::abs(output.LastPose()->position.x - oldPose.position.x - 20.0F) < 0.01F &&
            std::abs(output.LastPose()->position.y - oldPose.position.y - 1000.0F) < 0.01F,
            "hotkey and timeout resume with a fresh destination anchor, without smoothing across the move");

        // A second move without ActorsRelocated models cancellation's missing event.
        source.controlState_->movementEnabled = true;
        dispatcher.Tick(scene);
        source.controlState_->movementEnabled = false;
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        passed &= Check(output.OwnsCamera() && output.acquireCount_ == 3,
            "a repeated move resumes after relock even without a relocation callback");
    }

    // Return failures, unknown input and paused confirmation dialogs cannot
    // turn into a successful resume merely because time passes.
    {
        TestSceneSource source;
        TestPresetProvider presets({ { "default", defaultTransform } });
        ssc::runtime::PresetPreviewService preview;
        TestCameraControl output;
        TestVisibilityProbe probe;
        TestDebugVisualization debug;
        auto now = ssc::SceneCamera::Clock::time_point{};
        ssc::SceneCamera scene([&] { return now; });
        scene.Configure(source, presets, preview, output, probe, debug);
        ssc::runtime::MainUpdateDispatcher dispatcher;
        scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
        scene.Update(0.016F);
        preview.BeginPreviewSession();
        static_cast<void>(preview.SetPreview(alternateTransform, "default"));
        source.controlState_->movementEnabled = true;
        output.releaseFailureResult_ = ssc::runtime::CameraReleaseResult::kWrongThread;
        output.releaseFailures_ = 2;
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        passed &= Check(output.OwnsCamera() && !preview.PreviewSessionActive() && !preview.Request() &&
            !preview.Feedback()->previewApplied && !preview.Feedback()->previewPossible,
            "failed movement return locks preview immediately and preserves the release obligation");
        source.controlState_->movementEnabled = false;
        now += std::chrono::seconds{ 3 };
        dispatcher.Tick(scene);
        passed &= Check(output.OwnsCamera() && output.acquireCount_ == 1,
            "relocked input does not reacquire while a camera return is pending");
        source.controlState_.reset();
        dispatcher.Tick(scene);
        passed &= Check(!output.OwnsCamera() && output.releaseCount_ == 3,
            "main updates retry the failed return even without camera updates");
        now += std::chrono::seconds{ 3 };
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        passed &= Check(!output.OwnsCamera(), "unavailable input is not interpreted as relock");
        source.controlState_ = ssc::runtime::SceneControlState{ false, true };
        now += std::chrono::seconds{ 3 };
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        passed &= Check(!output.OwnsCamera(), "paused dialog cannot complete movement resume");
        source.controlState_->paused = false;
        dispatcher.Tick(scene);
        // Missing bodies must not reuse the cached pre-move anchor.
        source.anchorAvailable_ = false;
        scene.Update(0.016F);
        passed &= Check(!output.OwnsCamera(), "resume waits for a fresh valid body sample");
        source.anchorAvailable_ = true;
        output.acquireFailures_ = 1;
        scene.Update(0.016F);
        const auto acquisitions = output.acquireCount_;
        for (int i = 0; i < 5; ++i) {
            dispatcher.Tick(scene);
            scene.Update(0.016F);
        }
        passed &= Check(!output.OwnsCamera() && output.acquireCount_ == acquisitions,
            "resume acquisition refusal follows the existing policy without a per-frame retry loop");
    }

    for (const bool debugMode : { false, true }) {
        for (const bool endEvent : { false, true }) {
            TestSceneSource source;
            TestPresetProvider presets({ { "default", defaultTransform } });
            ssc::runtime::PresetPreviewService preview;
            TestCameraControl output;
            TestVisibilityProbe probe;
            TestDebugVisualization debug;
            debug.enabled_ = debugMode;
            auto now = ssc::SceneCamera::Clock::time_point{};
            ssc::SceneCamera scene([&] { return now; });
            scene.Configure(source, presets, preview, output, probe, debug);
            ssc::runtime::MainUpdateDispatcher dispatcher;
            scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
            scene.Update(0.016F);
            source.controlState_->movementEnabled = true;
            dispatcher.Tick(scene);
            source.controlState_->movementEnabled = false;
            dispatcher.Tick(scene);
            now += std::chrono::seconds{ 1 };
            dispatcher.Tick(scene);
            scene.Update(0.016F);
            passed &= Check(output.OwnsCamera() == !debugMode,
                "movement resume respects Debug mode");
            source.controlState_->movementEnabled = true;
            dispatcher.Tick(scene);
            if (endEvent) {
                scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationEnd, sceneKey, {} });
            } else {
                scene.RequestReset(); // shared lifecycle reset path
            }
            dispatcher.Tick(scene);
            source.controlState_->movementEnabled = false;
            scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kActorsRelocated, sceneKey, {} });
            now += std::chrono::seconds{ 5 };
            dispatcher.Tick(scene);
            scene.Update(0.016F);
            passed &= Check(!output.OwnsCamera() && !scene.NeedsUpdate(),
                "end/reset cancels movement resume; a late relocation cannot resurrect the scene");
        }
    }

    {
        TestSceneSource source;
        TestPresetProvider presets({ { "default", defaultTransform } });
        ssc::runtime::PresetPreviewService preview;
        TestCameraControl output;
        TestVisibilityProbe probe;
        TestDebugVisualization debug;
        TestTargetLockControl target;
        auto now = ssc::SceneCamera::Clock::time_point{};
        ssc::SceneCamera scene([&] { return now; });
        scene.Configure(source, presets, preview, output, probe, debug, &target);
        ssc::runtime::MainUpdateDispatcher dispatcher;
        scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
        scene.Update(0.016F); // TDM temporary disable is pending, camera not acquired yet.
        target.canFinish = false;
        source.controlState_->movementEnabled = true;
        dispatcher.Tick(scene);
        source.controlState_->movementEnabled = false;
        now += std::chrono::seconds{ 3 };
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        passed &= Check(!output.OwnsCamera() && target.releases == 0,
            "movement suspension cannot resume while the startup target disable is still owned");
        target.canFinish = true;
        dispatcher.Tick(scene);
        passed &= Check(target.releases == 1 && target.lastCancelled,
            "main update cancels and returns the startup target disable during movement suspension");
        now += std::chrono::milliseconds{ 900 };
        const ssc::runtime::SceneKey otherKey{ sceneKey.sourceID, sceneKey.instanceID + 1 };
        scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kActorsRelocated, otherKey, {} });
        now += std::chrono::milliseconds{ 100 };
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        passed &= Check(output.OwnsCamera() && target.requests == 1,
            "unrelated relocation does not postpone resume or repeat initial TDM unlock");
        source.controlState_->movementEnabled = true;
        dispatcher.Tick(scene);
        now += std::chrono::minutes{ 31 };
        dispatcher.Tick(scene);
        passed &= Check(!output.OwnsCamera() && !scene.NeedsUpdate(),
            "main update expires the original watchdog during suspension without camera updates");
    }

    // COR-1: loss of recorded ownership at movement entry is a terminal
    // boundary, unlike another mod using the camera after a successful return.
    for (const bool lossBeforeReturn : { true, false }) {
        TestSceneSource source;
        TestPresetProvider presets({ { "default", defaultTransform } });
        ssc::runtime::PresetPreviewService preview;
        TestCameraControl output;
        TestVisibilityProbe probe;
        TestDebugVisualization debug;
        auto now = ssc::SceneCamera::Clock::time_point{};
        ssc::SceneCamera scene([&] { return now; });
        scene.Configure(source, presets, preview, output, probe, debug);
        ssc::runtime::MainUpdateDispatcher dispatcher;
        scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        output.loseOwnershipOnRelease_ = lossBeforeReturn;
        source.controlState_->movementEnabled = true;
        dispatcher.Tick(scene);
        const auto releases = output.releaseCount_;
        // In the normal handoff case, there is now no recorded SSC ownership:
        // a later owner's presence must not be mistaken for ownership loss.
        output.loseOwnershipOnRelease_ = true;
        dispatcher.Tick(scene);
        passed &= Check(!output.OwnsCamera() && output.releaseCount_ == releases,
            "COR-1 no release of another owner's camera after either handoff outcome");
        source.controlState_->movementEnabled = false;
        scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kActorsRelocated, sceneKey, {} });
        dispatcher.Tick(scene);
        now += std::chrono::seconds{ 2 };
        dispatcher.Tick(scene);
        scene.Update(0.016F);
        if (lossBeforeReturn) {
            passed &= Check(!output.OwnsCamera() && output.acquireCount_ == 1 &&
                !scene.NeedsUpdate() && !preview.Feedback()->sceneActive,
                "COR-1 lost ownership discards the scene; relock and late relocation cannot reacquire");
            scene.HandleSceneEvent({ ssc::runtime::SceneEventType::kAnimationStart, sceneKey, participants });
            dispatcher.Tick(scene);
            scene.Update(0.016F);
            passed &= Check(output.OwnsCamera() && output.acquireCount_ == 2,
                "COR-1 a genuinely new start can acquire after ownership-loss cleanup");
        } else {
            passed &= Check(output.OwnsCamera() && output.acquireCount_ == 2,
                "COR-1 normal temporary return still permits automatic resume");
        }
    }


    // Real scene procedure with controllable asynchronous metadata and event ingress.
    {
        using Event = ssc::runtime::SceneEventType;
        using Reply = ssc::runtime::AnimationUpdateCoordinator::Reply;
        auto now = ssc::SceneCamera::Clock::time_point{};
        ssc::SceneCamera scene{ [&] { return now; } };
        TestSceneSource source;
        TestPresetProvider presets({
            { "wrong", defaultTransform, "Wrong", "^other$", "" },
            { "matched", defaultTransform, "Matched", "^current$", "^feet$" },
            { "generic", alternateTransform, "Generic", "", "" },
        });
        ssc::runtime::PresetPreviewService preview;
        TestCameraControl output;
        TestVisibilityProbe probe;
        TestDebugVisualization debug;
        scene.Configure(source, presets, preview, output, probe, debug);
        std::vector<Reply> ids, names, tags;
        scene.AnimationUpdates().Configure(
            [&](const auto&) { return ids.emplace_back(std::make_shared<ssc::runtime::MetadataReply>()); },
            [&](const auto&, bool tag) {
                return (tag ? tags : names).emplace_back(std::make_shared<ssc::runtime::MetadataReply>());
            });
        auto receive = [&](Event type) {
            ssc::runtime::SceneEvent event{ type, sceneKey, participants };
            scene.ReceiveSceneEvent(event);
            return event;
        };
        auto send = [&](Event type) { scene.HandleSceneEvent(receive(type)); };
        auto tick = [&] { scene.ProcessMainUpdate(); scene.Update(0.016F); };
        auto complete = [](Reply reply, std::string value, std::vector<std::string> values = {}) {
            std::scoped_lock lock{ reply->mutex };
            reply->status = "ok"; reply->text = std::move(value); reply->tags = std::move(values); reply->complete = true;
        };
        auto finish = [&](const std::string& id, const std::string& name) {
            complete(ids.back(), id); tick();
            complete(names.back(), name); complete(tags.back(), {}, { "Feet" }); tick();
        };
        send(Event::kAnimationStart); tick();
        passed &= Check(output.acquireCount_ == 0, "metadata: no initial selection before async response");
        finish("id1", "current");
        passed &= Check(output.OwnsCamera() && debug.snapshot_ &&
            debug.snapshot_->selectedPresetID == "matched" && !debug.snapshot_->candidates.front().usable,
            "metadata: initial selection skips a visible but mismatched preset");
        scene.RequestPresetStep(1); scene.Update(0.016F);
        passed &= Check(debug.snapshot_->selectedPresetID == "generic", "metadata: generic candidates have normal saved-order priority");
        auto change = receive(Event::kAnimationChange);
        scene.RequestPresetStep(1); scene.Update(0.016F);
        passed &= Check(debug.snapshot_->selectedPresetID == "generic", "metadata: undelivered Change blocks A/D at ingress");
        scene.HandleSceneEvent(change); tick();
        const auto readsBefore = ids.size();
        send(Event::kActorsRelocated); tick();
        passed &= Check(ids.size() == readsBefore, "metadata: relocation does not issue ID requests or release metadata hold");
        send(Event::kStageStart); tick(); finish("id2", "other");
        passed &= Check(debug.snapshot_->selectedPresetID == "generic", "metadata: changed filters retain selected camera");
        scene.RequestPresetStep(1); scene.Update(0.016F);
        passed &= Check(debug.snapshot_->selectedPresetID == "wrong", "metadata: A/D uses new animation conditions");
        // New receipt during LOS must invalidate the complete result before publication.
        send(Event::kStageStart); tick(); complete(ids.back(), "id2");
        auto before = scene.AnimationUpdates().Published();
        std::optional<ssc::runtime::SceneEvent> duringTrace;
        probe.onTrace_ = [&] { duringTrace = receive(Event::kAnimationChange); };
        tick();
        passed &= Check(duringTrace.has_value() && !scene.AnimationUpdates().Published(),
            "metadata: receipt during candidate calculation prevents store/candidate commit");
        scene.HandleSceneEvent(*duringTrace); tick();
        now += std::chrono::seconds(5); tick();
        passed &= Check(scene.AnimationUpdates().Published() && !scene.AnimationUpdates().Published()->known &&
            debug.snapshot_->selectedPresetID == "wrong", "metadata: timeout to unknown retains current selection");
        scene.RequestPresetStep(1); scene.Update(0.016F);
        passed &= Check(debug.snapshot_->selectedPresetID == "generic", "metadata: timeout leaves only generic A/D candidates");
        send(Event::kStageStart); tick();
        presets.Remove("wrong"); finish("id3", "current");
        passed &= Check(debug.snapshot_->candidates.size() == 2, "metadata: edits during acquisition use latest preset snapshot");
        send(Event::kStageStart); tick(); complete(ids.back(), "id3");
        probe.onTrace_ = [&] { presets.Remove("matched"); };
        tick(); tick();
        passed &= Check(debug.snapshot_->candidates.size() == 1 && debug.snapshot_->candidates.front().presetID == "generic",
            "metadata: edit during LOS invalidates stale candidates and retries latest presets");
        send(Event::kStageStart); tick(); auto previousLife = ids.back();
        auto end = receive(Event::kAnimationEnd); auto restart = receive(Event::kAnimationStart);
        scene.HandleSceneEvent(end); scene.HandleSceneEvent(restart); tick();
        complete(previousLife, "old"); tick();
        passed &= Check(scene.AnimationUpdates().Pending(), "metadata: same-batch end/restart rejects old lifetime response");
        finish("id4", "current");
        passed &= Check(output.OwnsCamera(), "metadata: same-batch restart is not discarded by end cleanup");
        send(Event::kAnimationChange); tick();
        source.controlState_->movementEnabled = true; tick();
        source.controlState_->movementEnabled = false; send(Event::kActorsRelocated); tick();
        const auto acquisitions = output.acquireCount_;
        passed &= Check(!output.OwnsCamera(), "metadata: relocation cannot resume while update is pending");
        now += std::chrono::seconds(5); tick();
        passed &= Check(output.OwnsCamera() && output.acquireCount_ == acquisitions + 1,
            "metadata: movement resumes with generic candidate after timeout");
    }

    passed &= RunAnimationFilterIntegrationTests();
    return passed ? 0 : 1;
}
