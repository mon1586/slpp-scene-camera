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
        SmoothCamAPI::InterfaceVersion a_version) noexcept
    {
        if (!a_interface || a_version < SmoothCamAPI::InterfaceVersion::V2) {
            api_ = nullptr;
            logger::error("SmoothCam API V2+ is unavailable (reported version {})",
                static_cast<std::uint32_t>(a_version));
            return;
        }

        api_ = static_cast<SmoothCamAPI::IVSmoothCam2*>(a_interface);
        const auto rawVersion = static_cast<std::uint32_t>(a_version);
        logger::info("SmoothCam API connected (V{}, enum value {})", rawVersion + 1, rawVersion);
    }

    bool SmoothCamAdapter::Acquire() noexcept
    {
        if (!api_) {
            logger::error("Cannot acquire camera: SmoothCam API is unavailable");
            return false;
        }
        if (!api_->IsCameraEnabled()) {
            logger::warn("Cannot acquire camera: SmoothCam is disabled");
            return false;
        }

        const auto pluginHandle = SKSE::GetPluginHandle();
        const auto acquireResult = api_->RequestCameraControl(pluginHandle);
        logger::info("SmoothCam RequestCameraControl -> {}", ResultName(acquireResult));
        if (acquireResult != SmoothCamAPI::APIResult::OK &&
            acquireResult != SmoothCamAPI::APIResult::AlreadyGiven) {
            return false;
        }

        ownsCamera_ = true;
        const auto updateResult = api_->RequestInterpolatorUpdates(pluginHandle, true);
        logger::info("SmoothCam RequestInterpolatorUpdates(true) -> {}", ResultName(updateResult));
        if (updateResult == SmoothCamAPI::APIResult::OK) {
            return true;
        }

        const auto releaseResult = api_->ReleaseCameraControl(pluginHandle);
        logger::warn("Interpolator updates were refused; ReleaseCameraControl -> {}", ResultName(releaseResult));
        ownsCamera_ = false;
        return false;
    }

    void SmoothCamAdapter::Release(const RE::Actor* a_player) noexcept
    {
        if (!ownsCamera_) {
            return;
        }
        if (!api_) {
            logger::error("SmoothCam interface disappeared while camera ownership was active");
            ownsCamera_ = false;
            return;
        }

        const auto pluginHandle = SKSE::GetPluginHandle();
        const auto owner = api_->GetCameraOwner();
        if (owner != pluginHandle) {
            logger::warn("SmoothCam reports another camera owner while restoring (owner={})", owner);
            ownsCamera_ = false;
            return;
        }

        const auto goalResult = api_->SendToGoalPosition(pluginHandle, true, false, a_player);
        logger::info("SmoothCam SendToGoalPosition(true) -> {}", ResultName(goalResult));

        const auto releaseResult = api_->ReleaseCameraControl(pluginHandle);
        logger::info("SmoothCam ReleaseCameraControl -> {}", ResultName(releaseResult));
        ownsCamera_ = false;
    }
}
