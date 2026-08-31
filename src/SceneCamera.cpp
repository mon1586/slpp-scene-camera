#include "SceneCamera.h"

namespace ssc
{
    namespace
    {
        constexpr auto kMaximumSceneDuration = std::chrono::minutes{ 30 };

        [[nodiscard]] core::Vec3 ToCore(const runtime::Vec3& a_value) noexcept
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
        runtime::ICameraControl& a_cameraControl) noexcept
    {
        sceneSource_ = std::addressof(a_sceneSource);
        cameraControl_ = std::addressof(a_cameraControl);
    }

    bool SceneCamera::PrepareStartEvent(runtime::SceneEvent& a_event) const
    {
        if (!sceneSource_) {
            return false;
        }
        a_event.participants = sceneSource_->CollectParticipants(a_event.key);
        return a_event.participants.containsPlayer;
    }

    bool SceneCamera::NeedsUpdate() const noexcept
    {
        return anchorCapturePending_.load(std::memory_order_acquire) ||
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

        if (!a_participants.containsPlayer) {
            logger::info("Ignoring scene {:08X}/{}: player is not a participant",
                a_key.sourceID, a_key.instanceID);
            return;
        }

        static_cast<void>(session_.Prepare(a_key));
        participants_ = a_participants;
        logger::info("Scene {:08X}/{} prepared with {} participant(s)",
            a_key.sourceID, a_key.instanceID, participants_.count);
        if (participants_.truncated) {
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

        if (!a_event.participants.containsPlayer) {
            logger::warn("Prepared scene no longer contains the player; abandoning camera switch");
            Clear();
            return;
        }
        participants_ = a_event.participants;

        static_cast<void>(session_.Activate(a_event.key));
        anchorCapturePending_.store(true, std::memory_order_release);
        activeSince_ = std::chrono::steady_clock::now();
        logger::info("Scene anchor capture pending for {:08X}/{}",
            a_event.key.sourceID, a_event.key.instanceID);
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
        if (std::chrono::steady_clock::now() - activeSince_ > kMaximumSceneDuration) {
            Restore("scene watchdog timeout"sv);
            return;
        }
        if (!anchorCapturePending_.load(std::memory_order_acquire)) {
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
        if (cameraControl_) {
            static_cast<void>(cameraControl_->EmergencyRelease());
        }
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
        participants_ = {};
        anchorCapturePending_.store(false, std::memory_order_release);
        anchor_.reset();
        session_.Clear();
    }
}
