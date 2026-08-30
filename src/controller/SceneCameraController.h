#pragma once

#include "controller/CameraOutput.h"
#include "controller/SceneEventMailbox.h"
#include "controller/SceneSession.h"
#include "controller/SmoothCamAdapter.h"
#include "core/HighAltitudeRig.h"

namespace ssc::controller
{
    class SceneCameraController
    {
    public:
        static SceneCameraController* GetSingleton() noexcept;

        void SetSmoothCamInterface(void* a_interface, SmoothCamAPI::InterfaceVersion a_version);

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
        [[nodiscard]] SceneParticipantSnapshot CollectParticipants(RE::FormID a_senderID) const;
        void Prepare(const SceneKey& a_key, const SceneParticipantSnapshot& a_participants);
        void Restore(std::string_view a_reason);
        void ApplyRequestedReset();
        void Clear() noexcept;

        SceneSession session_;
        SceneParticipantSnapshot participants_;
        std::chrono::steady_clock::time_point activeSince_{};
        std::atomic_bool active_{ false };
        std::atomic_bool resetRequested_{ false };
        SmoothCamAdapter smoothCam_;
        CameraOutput output_;
        core::HighAltitudeRig rig_;
    };
}
