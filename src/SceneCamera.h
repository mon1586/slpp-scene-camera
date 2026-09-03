#pragma once

#include "runtime/ICameraControl.h"
#include "runtime/IDebugVisualization.h"
#include "runtime/IPresetProvider.h"
#include "runtime/PresetPreviewService.h"
#include "runtime/IRuntimeClient.h"
#include "runtime/ISceneSource.h"
#include "SceneSession.h"
#include "core/CameraPose.h"
#include "core/SceneAnchor.h"

namespace ssc
{
    class SceneCamera final : public runtime::IRuntimeClient
    {
    public:
        static SceneCamera* GetSingleton() noexcept;

        void Configure(
            runtime::ISceneSource& a_sceneSource,
            runtime::IPresetProvider& a_presetProvider,
            runtime::PresetPreviewService& a_previewService,
            runtime::ICameraControl& a_cameraControl,
            runtime::IDebugVisualization& a_debugVisualization) noexcept;

        [[nodiscard]] bool NeedsUpdate() const noexcept override;
        void HandleSceneEvent(const runtime::SceneEvent& a_event) override;

        void Update() override;
        void Reset(std::string_view a_reason) override;
        void RequestReset() noexcept override;
        void EmergencyReset() noexcept override;

    private:
        void OnAnimationStarting(const runtime::SceneEvent& a_event);
        void OnAnimationStart(const runtime::SceneEvent& a_event);
        void OnAnimationChange(const runtime::SceneEvent& a_event);
        void OnAnimationEnding(const runtime::SceneEvent& a_event);
        void OnAnimationEnd(const runtime::SceneEvent& a_event);
        void Prepare(
            const runtime::SceneKey& a_key,
            const runtime::SceneParticipantSnapshot& a_participants);
        void Restore(std::string_view a_reason);
        void ApplyRequestedReset();
        [[nodiscard]] std::optional<runtime::CameraPreset> ResolvePreset(
            const std::shared_ptr<const runtime::PresetPreviewRequest>& a_request) const;
        [[nodiscard]] bool ApplyPreset(
            const runtime::CameraPreset& a_preset,
            bool a_liveEdit);
        [[nodiscard]] bool ApplyRequestedPreset(
            const std::shared_ptr<const runtime::PresetPreviewRequest>& a_request,
            bool a_liveEdit);
        void PublishPreviewFeedback(
            bool a_applied,
            std::string a_message,
            std::optional<runtime::PresetOffset> a_offset = std::nullopt);
        void Clear() noexcept;

        runtime::ISceneSource* sceneSource_{ nullptr };
        runtime::IPresetProvider* presetProvider_{ nullptr };
        runtime::PresetPreviewService* previewService_{ nullptr };
        runtime::ICameraControl* cameraControl_{ nullptr };
        runtime::IDebugVisualization* debugVisualization_{ nullptr };
        SceneSession session_;
        runtime::SceneParticipantSnapshot participants_;
        std::chrono::steady_clock::time_point activeSince_{};
        std::chrono::steady_clock::time_point anchorCaptureReadyAt_{};
        std::atomic_bool resetRequested_{ false };
        std::atomic_bool anchorCapturePending_{ false };
        std::atomic_bool cameraPoseActive_{ false };
        std::optional<core::SceneAnchor> anchor_;
        std::optional<runtime::CameraPose> cameraPose_;
        std::shared_ptr<const runtime::PresetPreviewRequest> appliedPreviewRequest_;
        core::SceneAnchorCalculator anchorCalculator_;
        core::CameraPoseCalculator poseCalculator_;
    };
}
