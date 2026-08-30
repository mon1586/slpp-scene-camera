#include "controller/SmoothCamAdapter.h"

namespace ssc::controller
{
    namespace
    {
        [[nodiscard]] std::string_view ResultName(SmoothCamAPI::APIResult a_result) noexcept
        {
            using enum SmoothCamAPI::APIResult;
            switch (a_result) {
            case OK:
                return "OK"sv;
            case NotOwner:
                return "NotOwner"sv;
            case MustKeep:
                return "MustKeep"sv;
            case AlreadyGiven:
                return "AlreadyGiven"sv;
            case AlreadyTaken:
                return "AlreadyTaken"sv;
            case BadThread:
                return "BadThread"sv;
            default:
                return "Unknown"sv;
            }
        }
    }

    void SmoothCamAdapter::SetInterface(
        void* a_interface,
        SmoothCamAPI::InterfaceVersion a_version)
    {
        if (!a_interface || a_version < SmoothCamAPI::InterfaceVersion::V2) {
            api_.store(nullptr, std::memory_order_release);
            logger::error("SmoothCam API V2+ is unavailable (reported version {})",
                static_cast<std::uint32_t>(a_version));
            return;
        }

        api_.store(static_cast<SmoothCamAPI::IVSmoothCam2*>(a_interface), std::memory_order_release);
        const auto rawVersion = static_cast<std::uint32_t>(a_version);
        logger::info("SmoothCam API connected (V{}, enum value {})", rawVersion + 1, rawVersion);
    }

    bool SmoothCamAdapter::CanAcquire() const noexcept
    {
        const auto* api = api_.load(std::memory_order_acquire);
        return api && api->IsCameraEnabled();
    }

    bool SmoothCamAdapter::Acquire()
    {
        auto* api = api_.load(std::memory_order_acquire);
        if (!api) {
            logger::error("Cannot acquire camera: SmoothCam API is unavailable");
            return false;
        }
        if (!api->IsCameraEnabled()) {
            logger::warn("Cannot acquire camera: SmoothCam is disabled");
            return false;
        }

        const auto pluginHandle = SKSE::GetPluginHandle();
        const auto acquireResult = api->RequestCameraControl(pluginHandle);
        logger::info("SmoothCam RequestCameraControl -> {}", ResultName(acquireResult));
        if (acquireResult != SmoothCamAPI::APIResult::OK &&
            acquireResult != SmoothCamAPI::APIResult::AlreadyGiven) {
            return false;
        }
        if (api->GetCameraOwner() != pluginHandle) {
            logger::error("SmoothCam granted camera control without reporting this plugin as owner");
            return false;
        }

        ownsCamera_.store(true, std::memory_order_release);
        const auto updateResult = api->RequestInterpolatorUpdates(pluginHandle, true);
        logger::info("SmoothCam RequestInterpolatorUpdates(true) -> {}", ResultName(updateResult));
        if (updateResult == SmoothCamAPI::APIResult::OK) {
            return true;
        }

        const auto releaseResult = api->ReleaseCameraControl(pluginHandle);
        logger::warn("Interpolator updates were refused; ReleaseCameraControl -> {}", ResultName(releaseResult));
        ownsCamera_.store(false, std::memory_order_release);
        return false;
    }

    bool SmoothCamAdapter::StillOwnsCamera() const noexcept
    {
        if (!ownsCamera_.load(std::memory_order_acquire)) {
            return false;
        }

        const auto* api = api_.load(std::memory_order_acquire);
        return api && api->GetCameraOwner() == SKSE::GetPluginHandle();
    }

    void SmoothCamAdapter::Release(const RE::Actor* a_player)
    {
        if (!ownsCamera_.load(std::memory_order_acquire)) {
            return;
        }
        auto* api = api_.load(std::memory_order_acquire);
        if (!api) {
            logger::error("SmoothCam interface disappeared while camera ownership was active");
            ownsCamera_.store(false, std::memory_order_release);
            return;
        }

        const auto pluginHandle = SKSE::GetPluginHandle();
        const auto owner = api->GetCameraOwner();
        if (owner != pluginHandle) {
            logger::warn("SmoothCam reports another camera owner while restoring (owner={})", owner);
            ownsCamera_.store(false, std::memory_order_release);
            return;
        }

        const auto goalResult = api->SendToGoalPosition(pluginHandle, true, false, a_player);
        logger::info("SmoothCam SendToGoalPosition(true) -> {}", ResultName(goalResult));

        const auto releaseResult = api->ReleaseCameraControl(pluginHandle);
        logger::info("SmoothCam ReleaseCameraControl -> {}", ResultName(releaseResult));
        ownsCamera_.store(false, std::memory_order_release);
    }

    bool SmoothCamAdapter::EmergencyRelease() noexcept
    {
        if (!ownsCamera_.load(std::memory_order_acquire)) {
            return true;
        }

        auto* api = api_.load(std::memory_order_acquire);
        const auto pluginHandle = SKSE::GetPluginHandle();
        if (!api || api->GetCameraOwner() != pluginHandle) {
            ownsCamera_.store(false, std::memory_order_release);
            return true;
        }

        const auto result = api->ReleaseCameraControl(pluginHandle);
        if (result == SmoothCamAPI::APIResult::OK || result == SmoothCamAPI::APIResult::NotOwner) {
            ownsCamera_.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }
}
