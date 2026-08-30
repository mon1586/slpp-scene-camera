#pragma once

#include <SmoothCamAPI.h>

namespace ssc::controller
{
    class SmoothCamAdapter
    {
    public:
        void SetInterface(void* a_interface, SmoothCamAPI::InterfaceVersion a_version) noexcept;

        [[nodiscard]] bool Acquire() noexcept;
        void Release(const RE::Actor* a_player) noexcept;

        [[nodiscard]] bool OwnsCamera() const noexcept { return ownsCamera_; }

    private:
        SmoothCamAPI::IVSmoothCam2* api_{ nullptr };
        bool ownsCamera_{ false };
    };
}

