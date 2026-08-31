#pragma once

#include "runtime/CameraOutput.h"
#include "runtime/ICameraControl.h"

#include <SmoothCamAPI.h>

namespace ssc::runtime
{
    class SmoothCamCameraControl final : public ICameraControl
    {
    public:
        static SmoothCamCameraControl* GetSingleton() noexcept;

        [[nodiscard]] bool RegisterAPIListener(const SKSE::MessagingInterface* a_messaging);
        [[nodiscard]] bool RequestAPI(const SKSE::MessagingInterface* a_messaging);
        void SetInterface(void* a_interface, SmoothCamAPI::InterfaceVersion a_version);

        [[nodiscard]] bool CanAcquire() const noexcept override;
        [[nodiscard]] bool Acquire() override;
        [[nodiscard]] bool StillOwnsCamera() const noexcept override;
        [[nodiscard]] bool OwnsCamera() const noexcept override;
        [[nodiscard]] CameraApplyResult Apply(const CameraPose& a_pose) override;
        void Release() override;
        [[nodiscard]] bool EmergencyRelease() noexcept override;

    private:
        std::atomic<SmoothCamAPI::IVSmoothCam2*> api_{ nullptr };
        std::atomic_bool ownsCamera_{ false };
        CameraOutput output_;
    };
}
