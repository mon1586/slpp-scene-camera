#pragma once

#include "controller/ICameraController.h"
#include "controller/ISceneController.h"
#include "controller/SceneSession.h"
#include "core/HighAltitudeRig.h"

namespace ssc::controller
{
    class SceneCameraCoordinator
    {
    public:
        static SceneCameraCoordinator* GetSingleton() noexcept;

        void Configure(
            ISceneController& a_sceneController,
            ICameraController& a_cameraController) noexcept;

        [[nodiscard]] bool PrepareStartEvent(SceneEvent& a_event) const;
        [[nodiscard]] bool NeedsUpdate() const noexcept;

        void OnAnimationStarting(const SceneEvent& a_event);
        void OnAnimationStart(const SceneEvent& a_event);
        void OnAnimationEnding(const SceneEvent& a_event);
        void OnAnimationEnd(const SceneEvent& a_event);

        void Update(RE::PlayerCamera* a_camera);
        void Reset(std::string_view a_reason);
        void RequestReset() noexcept;
        void EmergencyReset() noexcept;

    private:
        void Prepare(const SceneKey& a_key, const SceneParticipantSnapshot& a_participants);
        void Restore(std::string_view a_reason);
        void ApplyRequestedReset();
        void Clear() noexcept;

        ISceneController* sceneController_{ nullptr };
        ICameraController* cameraController_{ nullptr };
        SceneSession session_;
        SceneParticipantSnapshot participants_;
        std::chrono::steady_clock::time_point activeSince_{};
        std::atomic_bool active_{ false };
        std::atomic_bool resetRequested_{ false };
        core::HighAltitudeRig rig_;
    };
}
