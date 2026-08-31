#include "controller/SceneCameraCoordinator.h"

namespace ssc::controller
{
    namespace
    {
        constexpr auto kMaximumSceneDuration = std::chrono::minutes{ 30 };

        [[nodiscard]] core::Vec3 ToCore(const RE::NiPoint3& a_point) noexcept
        {
            return { a_point.x, a_point.y, a_point.z };
        }
    }

    SceneCameraCoordinator* SceneCameraCoordinator::GetSingleton() noexcept
    {
        static SceneCameraCoordinator singleton;
        return std::addressof(singleton);
    }

    void SceneCameraCoordinator::Configure(
        ISceneController& a_sceneController,
        ICameraController& a_cameraController) noexcept
    {
        sceneController_ = std::addressof(a_sceneController);
        cameraController_ = std::addressof(a_cameraController);
    }

    bool SceneCameraCoordinator::PrepareStartEvent(SceneEvent& a_event) const
    {
        if (!sceneController_ || !cameraController_ || !cameraController_->CanAcquire()) {
            return false;
        }
        a_event.participants = sceneController_->CollectParticipants(a_event.key);
        return a_event.participants.containsPlayer;
    }

    bool SceneCameraCoordinator::NeedsUpdate() const noexcept
    {
        return active_.load(std::memory_order_acquire) ||
               resetRequested_.load(std::memory_order_acquire);
    }

    void SceneCameraCoordinator::Prepare(
        const SceneKey& a_key,
        const SceneParticipantSnapshot& a_participants)
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
                SceneParticipantSnapshot::kCapacity);
        }
    }

    void SceneCameraCoordinator::OnAnimationStarting(const SceneEvent& a_event)
    {
        ApplyRequestedReset();
        logger::info("AnimationStarting {:08X}/{}", a_event.key.sourceID, a_event.key.instanceID);
        Prepare(a_event.key, a_event.participants);
    }

    void SceneCameraCoordinator::OnAnimationStart(const SceneEvent& a_event)
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

        if (!cameraController_ || !cameraController_->Acquire()) {
            logger::warn("Scene camera activation failed; configured camera controller remains in control");
            Clear();
            return;
        }

        static_cast<void>(session_.Activate(a_event.key));
        active_.store(true, std::memory_order_release);
        activeSince_ = std::chrono::steady_clock::now();
        logger::info("Scene camera ACTIVE for {:08X}/{}",
            a_event.key.sourceID, a_event.key.instanceID);
    }

    void SceneCameraCoordinator::OnAnimationEnding(const SceneEvent& a_event)
    {
        ApplyRequestedReset();
        logger::info("AnimationEnding {:08X}/{}", a_event.key.sourceID, a_event.key.instanceID);
        if (session_.Matches(a_event.key)) {
            Restore("matching AnimationEnding"sv);
        }
    }

    void SceneCameraCoordinator::OnAnimationEnd(const SceneEvent& a_event)
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

    void SceneCameraCoordinator::Update(RE::PlayerCamera* a_camera)
    {
        ApplyRequestedReset();
        if (!session_.IsActive()) {
            return;
        }
        if (!cameraController_ || !cameraController_->StillOwnsCamera()) {
            logger::warn("Scene camera ownership was lost; clearing local state without releasing another owner");
            Clear();
            return;
        }
        if (std::chrono::steady_clock::now() - activeSince_ > kMaximumSceneDuration) {
            Restore("scene watchdog timeout"sv);
            return;
        }
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            return;
        }

        const auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            Restore("player is unavailable"sv);
            return;
        }

        std::array<core::Vec3, SceneParticipantSnapshot::kCapacity> subjectPositions{};
        std::size_t subjectCount = 0;
        bool playerResolved = false;
        for (std::size_t index = 0; index < participants_.count; ++index) {
            const auto& handle = participants_.handles[index];
            if (const auto actor = handle.get()) {
                playerResolved = playerResolved || actor.get() == player;
                subjectPositions[subjectCount++] = ToCore(actor->GetPosition());
            }
        }
        if (!playerResolved) {
            Restore("player participant handle became invalid"sv);
            return;
        }

        const auto pose = rig_.Evaluate({
            std::span<const core::Vec3>{ subjectPositions.data(), subjectCount },
            ToCore(player->GetPosition()),
        });
        if (!pose) {
            Restore("no valid camera subject position"sv);
            return;
        }

        switch (cameraController_->Apply(a_camera, *pose)) {
        case CameraApplyResult::kApplied:
            break;
        case CameraApplyResult::kUnsupportedState:
            Restore("camera entered an unsupported state"sv);
            break;
        case CameraApplyResult::kMissingCamera:
            Restore("player camera node is unavailable"sv);
            break;
        }
    }

    void SceneCameraCoordinator::Restore(std::string_view a_reason)
    {
        if (session_.IsIdle()) {
            return;
        }

        session_.BeginRestore();
        logger::info("Restoring camera: {}", a_reason);
        if (cameraController_) {
            cameraController_->Release(RE::PlayerCharacter::GetSingleton());
        }
        Clear();
        logger::info("Scene camera IDLE");
    }

    void SceneCameraCoordinator::Reset(std::string_view a_reason)
    {
        resetRequested_.store(false, std::memory_order_release);
        Restore(a_reason);
    }

    void SceneCameraCoordinator::RequestReset() noexcept
    {
        resetRequested_.store(true, std::memory_order_release);
        if (cameraController_) {
            static_cast<void>(cameraController_->EmergencyRelease());
        }
    }

    void SceneCameraCoordinator::EmergencyReset() noexcept
    {
        if (cameraController_) {
            static_cast<void>(cameraController_->EmergencyRelease());
        }
        resetRequested_.store(false, std::memory_order_release);
        Clear();
    }

    void SceneCameraCoordinator::ApplyRequestedReset()
    {
        if (!resetRequested_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (cameraController_ && cameraController_->OwnsCamera()) {
            Restore("lifecycle reset"sv);
        } else {
            Clear();
        }
    }

    void SceneCameraCoordinator::Clear() noexcept
    {
        participants_ = {};
        session_.Clear();
        active_.store(false, std::memory_order_release);
    }
}
