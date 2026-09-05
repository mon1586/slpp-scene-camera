#include "SceneCamera.h"

#include "runtime/IDebugVisualization.h"
#include "runtime/IVisibilityProbe.h"

#include <cmath>
#include <iostream>

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

namespace
{
    bool Check(bool a_condition, std::string_view a_message)
    {
        if (!a_condition) {
            std::cerr << "FAILED: " << a_message << '\n';
        }
        return a_condition;
    }

    class TestSceneSource final : public ssc::runtime::ISceneSource
    {
    public:
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
            const ssc::runtime::SceneParticipantSnapshot&,
            std::span<ssc::runtime::Vec3> a_pelvisStorage) const override
        {
            if (a_pelvisStorage.empty()) {
                return std::nullopt;
            }
            a_pelvisStorage[0] = { 0.0F, 0.0F, 0.0F };
            return ssc::runtime::SceneAnchorSamples{
                std::span<const ssc::runtime::Vec3>{ a_pelvisStorage.data(), 1 },
                ssc::runtime::Vec3{ 0.0F, -1.0F, 0.0F },
                std::nullopt,
            };
        }

        [[nodiscard]] std::optional<ssc::runtime::SceneVisibilitySamples>
        CollectVisibilityInput(
            const ssc::runtime::SceneParticipantSnapshot&,
            std::span<std::uint32_t> a_participantIDStorage,
            std::span<ssc::runtime::VisibilityTarget> a_targetStorage) const override
        {
            if (a_participantIDStorage.empty() || a_targetStorage.size() < 3) {
                return std::nullopt;
            }
            constexpr std::uint32_t participantID = 0x14;
            a_participantIDStorage[0] = participantID;
            for (std::size_t index = 0; index < 3; ++index) {
                a_targetStorage[index] = {
                    0,
                    participantID,
                    static_cast<ssc::core::VisibilityPoint>(index),
                    ssc::runtime::Vec3{ 0.0F, 0.0F, 60.0F - 20.0F * index },
                };
            }
            return ssc::runtime::SceneVisibilitySamples{
                std::span<const std::uint32_t>{ a_participantIDStorage.data(), 1 },
                std::span<const ssc::runtime::VisibilityTarget>{ a_targetStorage.data(), 3 },
            };
        }
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
            lastPose_ = a_pose;
            return ssc::runtime::CameraApplyResult::kApplied;
        }
        [[nodiscard]] ssc::runtime::CameraReleaseResult Release() override
        {
            owns_ = false;
            return ssc::runtime::CameraReleaseResult::kReleased;
        }
        [[nodiscard]] bool EmergencyRelease() noexcept override
        {
            owns_ = false;
            return true;
        }

    private:
        bool owns_{ false };
        std::optional<ssc::runtime::CameraPose> lastPose_;
    };

    class TestVisibilityProbe final : public ssc::runtime::IVisibilityProbe
    {
    public:
        [[nodiscard]] ssc::runtime::VisibilityRayHit Trace(
            const ssc::runtime::Vec3& a_start,
            const ssc::runtime::Vec3&,
            std::uint32_t) noexcept override
        {
            const auto visible = std::abs(a_start.x) < 80.0F;
            return {
                true,
                visible,
                false,
                1,
                std::nullopt,
                std::nullopt,
                visible ? 1.0F : 0.5F,
                0,
                visible ? std::string{} : std::string{ "test obstruction" },
            };
        }
    };

    class TestDebugVisualization final : public ssc::runtime::IDebugVisualization
    {
    public:
        [[nodiscard]] bool ShowAnchor(
            const ssc::runtime::Vec3&,
            const ssc::runtime::Vec3&) noexcept override
        {
            return true;
        }
        void Update() noexcept override {}
        void HideAnchor() noexcept override {}
        void ShowVisibility(
            std::shared_ptr<const ssc::core::VisibilityEvaluationSnapshot>) noexcept override
        {}
        void HideVisibility() noexcept override {}
    };
}

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
    camera.Update();

    bool passed = true;
    auto feedback = previewService.Feedback();
    passed &= Check(feedback && feedback->visibilityEvaluation &&
        feedback->visibilityEvaluation->selectedPresetID ==
            std::optional<std::string>{ "default" },
        "scene start selects the first automatically usable preset");

    camera.RequestPresetStep(1);
    camera.Update();
    feedback = previewService.Feedback();
    passed &= Check(feedback && feedback->visibilityEvaluation &&
        feedback->visibilityEvaluation->selectedPresetID ==
            std::optional<std::string>{ "edited" },
        "manual preset stepping selects the alternate preset");

    previewService.BeginPreviewSession();
    const auto revision = previewService.SetPreview(editedBlockedTransform, "edited");
    camera.Update();
    feedback = previewService.Feedback();
    passed &= Check(feedback && feedback->previewApplied &&
        feedback->appliedRevision == revision,
        "editor preview applies the changed preset before save");

    presetProvider.Update("edited", editedBlockedTransform);
    previewService.EndPreviewSession();
    previewService.ClearPreview("edited");
    camera.Update();
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
    return passed ? 0 : 1;
}
