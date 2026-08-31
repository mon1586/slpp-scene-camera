#pragma once

#include "controller/CameraOutput.h"
#include "controller/ICameraController.h"

#include <SmoothCamAPI.h>

namespace ssc::controller
{
    class SmoothCamCameraController final : public ICameraController
    {
    public:
        static SmoothCamCameraController* GetSingleton() noexcept;

        [[nodiscard]] bool RegisterAPIListener(const SKSE::MessagingInterface* a_messaging);
        [[nodiscard]] bool RequestAPI(const SKSE::MessagingInterface* a_messaging);
        void SetInterface(void* a_interface, SmoothCamAPI::InterfaceVersion a_version);

        [[nodiscard]] bool CanAcquire() const noexcept override;
        [[nodiscard]] bool Acquire() override;
        [[nodiscard]] bool StillOwnsCamera() const noexcept override;
        [[nodiscard]] bool OwnsCamera() const noexcept override;
        [[nodiscard]] CameraApplyResult Apply(
            RE::PlayerCamera* a_camera,
            const core::CameraPose& a_pose) override;
        void Release(const RE::Actor* a_player) override;
        [[nodiscard]] bool EmergencyRelease() noexcept override;

    private:
        std::atomic<SmoothCamAPI::IVSmoothCam2*> api_{ nullptr };
        std::atomic_bool ownsCamera_{ false };
        CameraOutput output_;
    };
}
