#pragma once
#include "SceneCamera.h"

#include "runtime/IDebugVisualization.h"
#include "runtime/IVisibilityProbe.h"
#include "runtime/MainUpdateDispatcher.h"

#include <cmath>
#include <iostream>
#include <thread>


namespace ssc::runtime
{
    class SexLabPSceneSource
    {
    public:
        [[nodiscard]] static SceneParticipantSnapshot MakeTestSnapshot()
        {
            SceneParticipantSnapshot snapshot;
            snapshot.count_ = 1;
            snapshot.containsPlayer_ = true;
            return snapshot;
        }
    };
}

namespace ssc::tests
{
    inline bool Check(bool a_condition, std::string_view a_message)
    {
        if (!a_condition) {
            std::cerr << "FAILED: " << a_message << '\n';
        }
        return a_condition;
    }

    class TestSceneSource final : public ssc::runtime::ISceneSource
    {
    public:
        [[nodiscard]] std::optional<ssc::runtime::SceneControlState> CollectControlState() const override
        {
            return controlState_;
        }
        std::optional<ssc::runtime::SceneControlState> controlState_{ ssc::runtime::SceneControlState{} };

        [[nodiscard]] bool Register(ssc::runtime::SceneEventHandler) override
        {
            return true;
        }

        [[nodiscard]] ssc::runtime::SceneParticipantSnapshot CollectParticipants(
            const ssc::runtime::SceneKey&) const override
        {
            return ssc::runtime::SexLabPSceneSource::MakeTestSnapshot();
        }

        [[nodiscard]] std::optional<ssc::runtime::SceneAnchorSamples> CollectAnchorInput(
            const ssc::runtime::SceneParticipantSnapshot&) const override
        {
            ++anchorCollectionCount_;
            if (!anchorAvailable_) {
                return std::nullopt;
            }
            return ssc::runtime::SceneAnchorSamples{
                bodyCenter_,
                actorForward_,
            };
        }

        [[nodiscard]] std::optional<ssc::runtime::SceneVisibilitySamples>
        CollectVisibilityInput(
            const ssc::runtime::SceneParticipantSnapshot&,
            std::span<std::uint32_t> a_participantIDStorage,
            std::span<ssc::runtime::VisibilityTarget> a_targetStorage) const override
        {
            if (a_participantIDStorage.empty() || a_targetStorage.empty()) {
                return std::nullopt;
            }
            constexpr std::uint32_t participantID = 0x14;
            a_participantIDStorage[0] = participantID;
            a_targetStorage[0] = {
                0,
                participantID,
                ssc::core::VisibilityPoint::kBodyCenter,
                bodyCenter_,
            };
            return ssc::runtime::SceneVisibilitySamples{
                std::span<const std::uint32_t>{ a_participantIDStorage.data(), 1 },
                std::span<const ssc::runtime::VisibilityTarget>{ a_targetStorage.data(), 1 },
            };
        }

        ssc::runtime::Vec3 bodyCenter_{};
        ssc::runtime::Vec3 actorForward_{ 0.0F, -1.0F, 0.0F };
        bool anchorAvailable_{ true };
        mutable std::size_t anchorCollectionCount_{ 0 };
    };

    class TestPresetProvider final : public ssc::runtime::IPresetProvider
    {
    public:
        explicit TestPresetProvider(ssc::runtime::CameraPresetSnapshot a_presets) :
            snapshot_(std::make_shared<const ssc::runtime::CameraPresetSnapshot>(
                std::move(a_presets)))
        {}

        [[nodiscard]] std::shared_ptr<const ssc::runtime::CameraPresetSnapshot> Snapshot()
            const noexcept override
        {
            return snapshot_;
        }

        void Update(std::string_view a_id, const ssc::runtime::PresetTransform& a_transform)
        {
            auto updated = *snapshot_;
            const auto preset = std::ranges::find(updated, a_id, &ssc::runtime::CameraPreset::id);
            if (preset != updated.end()) {
                preset->transform = a_transform;
            }
            snapshot_ = std::make_shared<const ssc::runtime::CameraPresetSnapshot>(
                std::move(updated));
        }

        void Remove(std::string_view a_id)
        {
            auto updated = *snapshot_;
            std::erase_if(updated, [&](const auto& a_preset) { return a_preset.id == a_id; });
            snapshot_ = std::make_shared<const ssc::runtime::CameraPresetSnapshot>(std::move(updated));
        }

    private:
        std::shared_ptr<const ssc::runtime::CameraPresetSnapshot> snapshot_;
    };

    class TestCameraControl final : public ssc::runtime::ICameraControl
    {
    public:
        [[nodiscard]] bool CanAcquire() const noexcept override { return true; }
        [[nodiscard]] std::string_view UnavailableReason() const noexcept override { return {}; }
        [[nodiscard]] bool Acquire() override
        {
            ++acquireCount_;
            if (acquireFailures_ > 0) {
                --acquireFailures_;
                return false;
            }
            owns_ = true;
            return true;
        }
        [[nodiscard]] bool StillOwnsCamera() const noexcept override { return owns_; }
        [[nodiscard]] bool OwnsCamera() const noexcept override { return owns_; }
        [[nodiscard]] ssc::runtime::CameraApplyResult Apply(
            const ssc::runtime::CameraPose& a_pose) override
        {
            if (!owns_) {
                return ssc::runtime::CameraApplyResult::kNotOwner;
            }
            ++applyCount_;
            lastPose_ = a_pose;
            return ssc::runtime::CameraApplyResult::kApplied;
        }
        [[nodiscard]] ssc::runtime::CameraReleaseResult Release() override
        {
            ++releaseCount_;
            if (loseOwnershipOnRelease_) {
                owns_ = false;
                return ssc::runtime::CameraReleaseResult::kNoOwnership;
            }
            if (releaseFailures_ > 0) {
                --releaseFailures_;
                return releaseFailureResult_;
            }
            if (failNextRelease_) {
                failNextRelease_ = false;
                return ssc::runtime::CameraReleaseResult::kFailed;
            }
            owns_ = false;
            return ssc::runtime::CameraReleaseResult::kReleased;
        }
        [[nodiscard]] bool EmergencyRelease() noexcept override
        {
            if (failEmergencyRelease_) {
                return false;
            }
            owns_ = false;
            return true;
        }

        [[nodiscard]] const std::optional<ssc::runtime::CameraPose>& LastPose() const noexcept
        {
            return lastPose_;
        }

        [[nodiscard]] std::size_t ApplyCount() const noexcept { return applyCount_; }

        bool failNextRelease_{ false };
        bool loseOwnershipOnRelease_{ false };
        bool failEmergencyRelease_{ false };
        unsigned releaseFailures_{ 0 };
        unsigned releaseCount_{ 0 };
        ssc::runtime::CameraReleaseResult releaseFailureResult_{
            ssc::runtime::CameraReleaseResult::kFailed };
        unsigned acquireFailures_{ 0 };
        unsigned acquireCount_{ 0 };

    private:
        bool owns_{ false };
        std::optional<ssc::runtime::CameraPose> lastPose_;
        std::size_t applyCount_{ 0 };
    };

    class TestVisibilityProbe final : public ssc::runtime::IVisibilityProbe
    {
    public:
        [[nodiscard]] ssc::runtime::VisibilityRayHit Trace(
            const ssc::runtime::Vec3& a_start,
            const ssc::runtime::Vec3& a_target,
            std::uint32_t a_targetActorID) noexcept override
        {
            if (onTrace_) { auto callback = std::move(onTrace_); callback(); }
            ++traceCount_;
            lastStart_ = a_start;
            lastTarget_ = a_target;
            lastTargetActorID_ = a_targetActorID;
            const auto visible = !forceBlocked_ && std::abs(a_start.x) < 80.0F &&
                (!blockedAboveZ_ || a_start.z <= *blockedAboveZ_);
            return {
                querySucceeded_,
                visible,
                false,
                queriesPerTrace_,
                std::nullopt,
                std::nullopt,
                visible ? 1.0F : 0.5F,
                0,
                visible ? std::string{} : std::string{ "test obstruction" },
            };
        }

        std::function<void()> onTrace_;
        std::size_t traceCount_{ 0 };
        bool forceBlocked_{ false };
        std::optional<float> blockedAboveZ_;
        bool querySucceeded_{ true };
        std::size_t queriesPerTrace_{ 1 };
        ssc::runtime::Vec3 lastTarget_{};
        ssc::runtime::Vec3 lastStart_{};
        std::uint32_t lastTargetActorID_{ 0 };
    };

    class TestTargetLockControl final : public ssc::runtime::ITargetLockControl
    {
    public:
        bool RequestUnlock() noexcept override { ++requests; return available && locked; }
        bool FinishUnlock(bool a_cancel) noexcept override
        {
            ++finishAttempts;
            if (!canFinish || (!a_cancel && locked)) {
                return false;
            }
            ++releases;
            lastCancelled = a_cancel;
            return true;
        }
        bool available{ true };
        bool locked{ true };
        bool canFinish{ true };
        bool lastCancelled{ false };
        unsigned requests{ 0 };
        unsigned finishAttempts{ 0 };
        unsigned releases{ 0 };
    };

    class TestDebugVisualization final : public ssc::runtime::IDebugVisualization
    {
    public:
        [[nodiscard]] bool ShowAnchor(
            const ssc::runtime::Vec3& a_position,
            const ssc::runtime::Vec3&,
            const ssc::runtime::Vec3& a_targetPosition) noexcept override
        {
            ++anchorShowCount_;
            anchorPosition_ = a_position;
            targetPosition_ = a_targetPosition;
            return true;
        }
        void Update() noexcept override {}
        void HideAnchor() noexcept override {}
        void ShowVisibility(
            std::shared_ptr<const ssc::core::VisibilityEvaluationSnapshot> a_snapshot) noexcept override
        {
            snapshot_ = std::move(a_snapshot);
        }
        void HideVisibility() noexcept override { snapshot_.reset(); }
        [[nodiscard]] bool Enabled() const noexcept override { return enabled_; }
        void StepCandidate(int a_direction) noexcept override
        {
            if (a_direction != 0) {
                ++stepCount_;
            }
        }

        bool enabled_{ false };
        std::shared_ptr<const ssc::core::VisibilityEvaluationSnapshot> snapshot_;
        ssc::runtime::Vec3 anchorPosition_{};
        ssc::runtime::Vec3 targetPosition_{};
        std::size_t anchorShowCount_{ 0 };
        std::size_t stepCount_{ 0 };
    };
}
