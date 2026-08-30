#include "controller/SceneCameraController.h"

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

    SceneCameraController* SceneCameraController::GetSingleton() noexcept
    {
        static SceneCameraController singleton;
        return std::addressof(singleton);
    }

    void SceneCameraController::SetSmoothCamInterface(
        void* a_interface,
        SmoothCamAPI::InterfaceVersion a_version)
    {
        smoothCam_.SetInterface(a_interface, a_version);
    }

    SceneParticipantSnapshot SceneCameraController::CollectParticipants(
        RE::FormID a_senderID) const
    {
        SceneParticipantSnapshot result;
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_senderID);
        if (!quest) {
            logger::warn("SexLab event sender {:08X} is not a live quest", a_senderID);
            return result;
        }

        const auto* player = RE::PlayerCharacter::GetSingleton();
        const RE::BSReadLockGuard lock{ quest->aliasAccessLock };

        for (auto* baseAlias : quest->aliases) {
            auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(baseAlias);
            auto* actor = refAlias ? refAlias->GetActorReference() : nullptr;
            if (!actor) {
                continue;
            }

            auto handle = actor->GetHandle();
            if (!handle) {
                continue;
            }

            const auto isPlayer = actor == player;
            bool duplicate = false;
            for (std::size_t index = 0; index < result.count; ++index) {
                if (result.handles[index].native_handle() == handle.native_handle()) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                result.containsPlayer = result.containsPlayer || isPlayer;
                continue;
            }

            if (result.count < result.handles.size()) {
                result.handles[result.count++] = handle;
                result.containsPlayer = result.containsPlayer || isPlayer;
            } else {
                result.truncated = true;
                if (isPlayer && !result.containsPlayer) {
                    result.handles.back() = handle;
                    result.containsPlayer = true;
                }
            }
        }

        return result;
    }

    bool SceneCameraController::PrepareStartEvent(SceneEvent& a_event) const
    {
        if (!smoothCam_.CanAcquire()) {
            return false;
        }
        a_event.participants = CollectParticipants(a_event.senderID);
        return a_event.participants.containsPlayer;
    }

    bool SceneCameraController::NeedsUpdate() const noexcept
    {
        return active_.load(std::memory_order_acquire) ||
               resetRequested_.load(std::memory_order_acquire);
    }

    void SceneCameraController::Prepare(
        const SceneKey& a_key,
        const SceneParticipantSnapshot& a_participants)
    {
        if (session_.IsActive()) {
            if (!session_.Matches(a_key)) {
                logger::info("Ignoring overlapping scene {:08X}/{} while another player scene is active",
                    a_key.senderID, a_key.threadID);
            }
            return;
        }

        if (!a_participants.containsPlayer) {
            logger::info("Ignoring scene {:08X}/{}: player is not a participant",
                a_key.senderID, a_key.threadID);
            return;
        }

        static_cast<void>(session_.Prepare(a_key));
        participants_ = a_participants;
        logger::info("Scene {:08X}/{} prepared with {} participant(s)",
            a_key.senderID, a_key.threadID, participants_.count);
        if (participants_.truncated) {
            logger::warn("Scene participant list exceeded {}; extra actors were ignored",
                SceneParticipantSnapshot::kCapacity);
        }
    }

    void SceneCameraController::OnAnimationStarting(const SceneEvent& a_event)
    {
        ApplyRequestedReset();
        const SceneKey key{ a_event.senderID, a_event.threadID };
        logger::info("AnimationStarting {:08X}/{}", a_event.senderID, a_event.threadID);
        Prepare(key, a_event.participants);
    }

    void SceneCameraController::OnAnimationStart(const SceneEvent& a_event)
    {
        ApplyRequestedReset();
        const SceneKey key{ a_event.senderID, a_event.threadID };
        logger::info("AnimationStart {:08X}/{}", a_event.senderID, a_event.threadID);

        if (session_.IsIdle()) {
            Prepare(key, a_event.participants);
        }
        if (!session_.IsPreparing() || !session_.Matches(key)) {
            return;
        }

        if (!a_event.participants.containsPlayer) {
            logger::warn("Prepared scene no longer contains the player; abandoning camera switch");
            Clear();
            return;
        }
        participants_ = a_event.participants;

        if (!smoothCam_.Acquire()) {
            logger::warn("Scene camera activation failed; SmoothCam remains in control");
            Clear();
            return;
        }

        static_cast<void>(session_.Activate(key));
        active_.store(true, std::memory_order_release);
        activeSince_ = std::chrono::steady_clock::now();
        logger::info("Scene camera ACTIVE for {:08X}/{}", a_event.senderID, a_event.threadID);
    }

    void SceneCameraController::OnAnimationEnding(const SceneEvent& a_event)
    {
        ApplyRequestedReset();
        const SceneKey key{ a_event.senderID, a_event.threadID };
        logger::info("AnimationEnding {:08X}/{}", a_event.senderID, a_event.threadID);
        if (session_.Matches(key)) {
            Restore("matching AnimationEnding"sv);
        }
    }

    void SceneCameraController::OnAnimationEnd(const SceneEvent& a_event)
    {
        ApplyRequestedReset();
        const SceneKey key{ a_event.senderID, a_event.threadID };
        logger::info("AnimationEnd {:08X}/{}", a_event.senderID, a_event.threadID);
        if (!session_.Matches(key)) {
            logger::info("Ignoring stale AnimationEnd {:08X}/{}", a_event.senderID, a_event.threadID);
            return;
        }
        Restore("matching AnimationEnd"sv);
    }

    void SceneCameraController::Update(RE::PlayerCamera* a_camera)
    {
        ApplyRequestedReset();
        if (!session_.IsActive()) {
            return;
        }
        if (!smoothCam_.StillOwnsCamera()) {
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

        switch (output_.Apply(a_camera, *pose)) {
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

    void SceneCameraController::Restore(std::string_view a_reason)
    {
        if (session_.IsIdle()) {
            return;
        }

        session_.BeginRestore();
        logger::info("Restoring camera: {}", a_reason);
        smoothCam_.Release(RE::PlayerCharacter::GetSingleton());
        Clear();
        logger::info("Scene camera IDLE");
    }

    void SceneCameraController::Reset(std::string_view a_reason)
    {
        resetRequested_.store(false, std::memory_order_release);
        Restore(a_reason);
    }

    void SceneCameraController::RequestReset() noexcept
    {
        resetRequested_.store(true, std::memory_order_release);
        static_cast<void>(smoothCam_.EmergencyRelease());
    }

    void SceneCameraController::EmergencyReset() noexcept
    {
        static_cast<void>(smoothCam_.EmergencyRelease());
        resetRequested_.store(false, std::memory_order_release);
        Clear();
    }

    void SceneCameraController::ApplyRequestedReset()
    {
        if (!resetRequested_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (smoothCam_.OwnsCamera()) {
            Restore("lifecycle reset"sv);
        } else {
            Clear();
        }
    }

    void SceneCameraController::Clear() noexcept
    {
        participants_ = {};
        session_.Clear();
        active_.store(false, std::memory_order_release);
    }
}
