#pragma once

#include "runtime/ICameraControl.h"
#include "runtime/IDebugVisualization.h"
#include "runtime/IRuntimeClient.h"
#include "runtime/ISceneSource.h"
#include "SceneSession.h"
#include "core/SceneAnchor.h"

namespace ssc
{
    class SceneCamera final : public runtime::IRuntimeClient
    {
    public:
        static SceneCamera* GetSingleton() noexcept;

        void Configure(
            runtime::ISceneSource& a_sceneSource,
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
        void Clear() noexcept;

        runtime::ISceneSource* sceneSource_{ nullptr };
        runtime::ICameraControl* cameraControl_{ nullptr };
        runtime::IDebugVisualization* debugVisualization_{ nullptr };
        SceneSession session_;
        runtime::SceneParticipantSnapshot participants_;
        std::chrono::steady_clock::time_point activeSince_{};
        std::chrono::steady_clock::time_point anchorCaptureReadyAt_{};
        std::atomic_bool resetRequested_{ false };
        std::atomic_bool sessionActive_{ false };
        std::atomic_bool anchorCapturePending_{ false };
        std::optional<core::SceneAnchor> anchor_;
        core::SceneAnchorCalculator anchorCalculator_;
    };
}
