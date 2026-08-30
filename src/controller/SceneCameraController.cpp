#include "controller/SceneCameraController.h"

namespace ssc::controller
{
    namespace
    {
        constexpr std::size_t kMaxSubjectsPerFrame = 32;

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
        SmoothCamAPI::InterfaceVersion a_version) noexcept
    {
        smoothCam_.SetInterface(a_interface, a_version);
    }

    SceneCameraController::Participants SceneCameraController::CollectParticipants(
        RE::FormID a_senderID) const noexcept
    {
        Participants result;
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_senderID);
        if (!quest) {
            logger::warn("SexLab event sender {:08X} is not a live quest", a_senderID);
            return result;
        }

        const auto* player = RE::PlayerCharacter::GetSingleton();
        const RE::BSReadLockGuard lock{ quest->aliasAccessLock };
        result.handles.reserve(quest->aliases.size());

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

            const auto duplicate = std::ranges::any_of(
                result.handles,
                [nativeHandle = handle.native_handle()](const RE::ActorHandle& a_existing) {
                    return a_existing.native_handle() == nativeHandle;
                });
            if (duplicate) {
                continue;
            }

            result.containsPlayer = result.containsPlayer || actor == player;
            result.handles.push_back(handle);
        }

        return result;
    }

    bool SceneCameraController::Matches(const SceneKey& a_key) const noexcept
    {
        return scene_.has_value() && scene_.value() == a_key;
    }

    void SceneCameraController::Prepare(const SceneKey& a_key) noexcept
    {
        if (state_ == State::kActive) {
            if (!Matches(a_key)) {
                logger::info("Ignoring overlapping scene {:08X}/{} while another player scene is active",
                    a_key.senderID, a_key.threadID);
            }
            return;
        }

        auto participants = CollectParticipants(a_key.senderID);
        if (!participants.containsPlayer) {
            logger::info("Ignoring scene {:08X}/{}: player is not a participant",
                a_key.senderID, a_key.threadID);
            return;
        }

        scene_ = a_key;
        participants_ = std::move(participants.handles);
        state_ = State::kPreparing;
        logger::info("Scene {:08X}/{} prepared with {} participant(s)",
            a_key.senderID, a_key.threadID, participants_.size());
    }

    void SceneCameraController::OnAnimationStarting(
        RE::FormID a_senderID,
        std::int32_t a_threadID) noexcept
    {
        const SceneKey key{ a_senderID, a_threadID };
        logger::info("AnimationStarting {:08X}/{}", a_senderID, a_threadID);
        Prepare(key);
    }

    void SceneCameraController::OnAnimationStart(
        RE::FormID a_senderID,
        std::int32_t a_threadID) noexcept
    {
        const SceneKey key{ a_senderID, a_threadID };
        logger::info("AnimationStart {:08X}/{}", a_senderID, a_threadID);

        if (state_ == State::kIdle) {
            Prepare(key);
        }
        if (state_ != State::kPreparing || !Matches(key)) {
            return;
        }

        // Refresh after actor synchronization: aliases can be more complete here than
        // at AnimationStarting.
        auto participants = CollectParticipants(a_senderID);
        if (!participants.containsPlayer) {
            logger::warn("Prepared scene no longer contains the player; abandoning camera switch");
            Clear();
            return;
        }
        participants_ = std::move(participants.handles);

        if (!smoothCam_.Acquire()) {
            logger::warn("Scene camera activation failed; SmoothCam remains in control");
            Clear();
            return;
        }

        state_ = State::kActive;
        logger::info("Scene camera ACTIVE for {:08X}/{}", a_senderID, a_threadID);
    }

    void SceneCameraController::OnAnimationEnding(
        RE::FormID a_senderID,
        std::int32_t a_threadID) noexcept
    {
        const SceneKey key{ a_senderID, a_threadID };
        logger::info("AnimationEnding {:08X}/{}{}", a_senderID, a_threadID,
            Matches(key) ? " (active scene)" : "");
    }

    void SceneCameraController::OnAnimationEnd(
        RE::FormID a_senderID,
        std::int32_t a_threadID) noexcept
    {
        const SceneKey key{ a_senderID, a_threadID };
        logger::info("AnimationEnd {:08X}/{}", a_senderID, a_threadID);
        if (!Matches(key)) {
            logger::info("Ignoring stale AnimationEnd {:08X}/{}", a_senderID, a_threadID);
            return;
        }
        Restore("matching AnimationEnd"sv);
    }

    void SceneCameraController::Update(RE::PlayerCamera* a_camera) noexcept
    {
        if (state_ != State::kActive || !smoothCam_.OwnsCamera()) {
            return;
        }

        std::array<core::Vec3, kMaxSubjectsPerFrame> subjectPositions{};
        std::size_t subjectCount = 0;
        for (const auto& handle : participants_) {
            if (subjectCount == subjectPositions.size()) {
                break;
            }
            if (const auto actor = handle.get()) {
                subjectPositions[subjectCount++] = ToCore(actor->GetPosition());
            }
        }

        core::Vec3 fallback{};
        if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
            fallback = ToCore(player->GetPosition());
        }

        const auto pose = rig_.Evaluate({
            std::span<const core::Vec3>{ subjectPositions.data(), subjectCount },
            fallback,
        });
        output_.Apply(a_camera, pose);
    }

    void SceneCameraController::Restore(std::string_view a_reason) noexcept
    {
        if (state_ == State::kIdle) {
            return;
        }

        state_ = State::kRestoring;
        logger::info("Restoring camera: {}", a_reason);
        smoothCam_.Release(RE::PlayerCharacter::GetSingleton());
        Clear();
        logger::info("Scene camera IDLE");
    }

    void SceneCameraController::Reset(std::string_view a_reason) noexcept
    {
        Restore(a_reason);
    }

    void SceneCameraController::Clear() noexcept
    {
        participants_.clear();
        scene_.reset();
        state_ = State::kIdle;
    }
}

