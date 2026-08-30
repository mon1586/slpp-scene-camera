#pragma once

#include <SmoothCamAPI.h>

namespace ssc::controller
{
    class SmoothCamAdapter
    {
    public:
        void SetInterface(void* a_interface, SmoothCamAPI::InterfaceVersion a_version);

        [[nodiscard]] bool CanAcquire() const noexcept;
        [[nodiscard]] bool Acquire();
        [[nodiscard]] bool StillOwnsCamera() const noexcept;
        void Release(const RE::Actor* a_player);
        [[nodiscard]] bool EmergencyRelease() noexcept;

        [[nodiscard]] bool OwnsCamera() const noexcept
        {
            return ownsCamera_.load(std::memory_order_acquire);
        }

    private:
        std::atomic<SmoothCamAPI::IVSmoothCam2*> api_{ nullptr };
        std::atomic_bool ownsCamera_{ false };
    };
}
