#pragma once

#include "controller/CameraOutput.h"
#include "controller/SmoothCamAdapter.h"
#include "core/HighAltitudeRig.h"

namespace ssc::controller
{
    class SceneCameraController
    {
    public:
        static SceneCameraController* GetSingleton() noexcept;

        void SetSmoothCamInterface(void* a_interface, SmoothCamAPI::InterfaceVersion a_version) noexcept;

        void OnAnimationStarting(RE::FormID a_senderID, std::int32_t a_threadID) noexcept;
        void OnAnimationStart(RE::FormID a_senderID, std::int32_t a_threadID) noexcept;
        void OnAnimationEnding(RE::FormID a_senderID, std::int32_t a_threadID) noexcept;
        void OnAnimationEnd(RE::FormID a_senderID, std::int32_t a_threadID) noexcept;

        void Update(RE::PlayerCamera* a_camera) noexcept;
        void Reset(std::string_view a_reason) noexcept;

    private:
        enum class State
        {
            kIdle,
            kPreparing,
            kActive,
            kRestoring,
        };

        struct SceneKey
        {
            RE::FormID senderID{ 0 };
            std::int32_t threadID{ -1 };

            [[nodiscard]] friend bool operator==(const SceneKey&, const SceneKey&) = default;
        };

        struct Participants
        {
            std::vector<RE::ActorHandle> handles;
            bool containsPlayer{ false };
        };

        [[nodiscard]] Participants CollectParticipants(RE::FormID a_senderID) const noexcept;
        [[nodiscard]] bool Matches(const SceneKey& a_key) const noexcept;
        void Prepare(const SceneKey& a_key) noexcept;
        void Restore(std::string_view a_reason) noexcept;
        void Clear() noexcept;

        State state_{ State::kIdle };
        std::optional<SceneKey> scene_;
        std::vector<RE::ActorHandle> participants_;
        SmoothCamAdapter smoothCam_;
        CameraOutput output_;
        core::HighAltitudeRig rig_;
    };
}

