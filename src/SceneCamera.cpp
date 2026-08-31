#include "SceneCamera.h"

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
    }

    SceneCamera* SceneCamera::GetSingleton() noexcept
    {
        static SceneCamera singleton;
        return std::addressof(singleton);
    }

    void SceneCamera::Configure(
        runtime::ISceneSource& a_sceneSource,
        runtime::ICameraControl& a_cameraControl,
        runtime::IDebugVisualization& a_debugVisualization) noexcept
    {
        sceneSource_ = std::addressof(a_sceneSource);
        cameraControl_ = std::addressof(a_cameraControl);
        debugVisualization_ = std::addressof(a_debugVisualization);
    }

    bool SceneCamera::NeedsUpdate() const noexcept
    {
        return sessionActive_.load(std::memory_order_acquire) ||
               anchorCapturePending_.load(std::memory_order_acquire) ||
               resetRequested_.load(std::memory_order_acquire);
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

        static_cast<void>(session_.Prepare(a_key));
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
        sessionActive_.store(true, std::memory_order_release);
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

        // No preset/raycast camera pose exists yet. Do not place the camera at the
        // anchor (which is inside the participants) or invent an arbitrary offset.
        // Leave camera control unchanged until pose selection is added.
    }

    void SceneCamera::Restore(std::string_view a_reason)
    {
        if (session_.IsIdle()) {
            return;
        }

        session_.BeginRestore();
        logger::info("Discarding scene anchor: {}", a_reason);
        if (cameraControl_ && cameraControl_->OwnsCamera()) {
            cameraControl_->Release();
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
        if (cameraControl_) {
            static_cast<void>(cameraControl_->EmergencyRelease());
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
        sessionActive_.store(false, std::memory_order_release);
        participants_ = {};
        anchorCapturePending_.store(false, std::memory_order_release);
        anchorCaptureReadyAt_ = {};
        if (debugVisualization_) {
            debugVisualization_->HideAnchor();
        }
        anchor_.reset();
        session_.Clear();
    }
}
