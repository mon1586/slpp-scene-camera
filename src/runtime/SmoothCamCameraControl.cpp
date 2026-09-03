#include "runtime/SmoothCamCameraControl.h"

#include "runtime/CameraHook.h"

#include <REX/W32/KERNEL32.h>

namespace ssc::runtime
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

        [[nodiscard]] bool IsAPIThread(const SmoothCamAPI::IVSmoothCam2& a_api) noexcept
        {
            return a_api.GetSmoothCamThreadId() == REX::W32::GetCurrentThreadId();
        }
    }

    SmoothCamCameraControl* SmoothCamCameraControl::GetSingleton() noexcept
    {
        static SmoothCamCameraControl singleton;
        return std::addressof(singleton);
    }

    bool SmoothCamCameraControl::RegisterAPIListener(
        const SKSE::MessagingInterface* a_messaging)
    {
        const auto improvedCameraLoaded = IsImprovedCameraLoaded();
        improvedCameraDetected_.store(improvedCameraLoaded, std::memory_order_release);
        if (improvedCameraLoaded) {
            logger::warn(
                "Improved Camera detected; this configuration is unsupported and scene camera control is disabled");
        }
        if (!a_messaging) {
            logger::error("Cannot register SmoothCam API listener: messaging interface is unavailable");
            return false;
        }
        const auto registered = SmoothCamAPI::RegisterInterfaceLoaderCallback(
            a_messaging,
            [](void* a_interface, SmoothCamAPI::InterfaceVersion a_version) {
                try {
                    GetSingleton()->SetInterface(a_interface, a_version);
                } catch (...) {
                    try {
                        logger::critical("SmoothCam API callback failed");
                    } catch (...) {
                    }
                }
            });
        logger::info("SmoothCam interface callback registration: {}", registered ? "OK" : "FAILED");
        return registered;
    }

    bool SmoothCamCameraControl::RequestAPI(const SKSE::MessagingInterface* a_messaging)
    {
        if (!a_messaging) {
            logger::error("Cannot request SmoothCam API: messaging interface is unavailable");
            return false;
        }
        const auto requested = SmoothCamAPI::RequestInterface(
            a_messaging, SmoothCamAPI::InterfaceVersion::V2);
        logger::info("SmoothCam V2 interface request dispatched: {}", requested ? "yes" : "no listener");
        return requested;
    }

    void SmoothCamCameraControl::SetInterface(
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

    bool SmoothCamCameraControl::CanAcquire() const noexcept
    {
        if (improvedCameraDetected_.load(std::memory_order_acquire)) {
            return false;
        }
        const auto* api = api_.load(std::memory_order_acquire);
        return api && IsAPIThread(*api) && api->IsCameraEnabled() &&
               CameraHook::IsUpdateHookHealthy();
    }

    std::string_view SmoothCamCameraControl::UnavailableReason() const noexcept
    {
        if (improvedCameraDetected_.load(std::memory_order_acquire)) {
            return "Improved Camera is installed and unsupported"sv;
        }
        const auto* api = api_.load(std::memory_order_acquire);
        if (!api) {
            return "SmoothCam API is unavailable"sv;
        }
        if (!IsAPIThread(*api)) {
            return "Wrong SmoothCam API thread"sv;
        }
        if (!api->IsCameraEnabled()) {
            return "SmoothCam is disabled"sv;
        }
        if (!CameraHook::IsUpdateHookHealthy()) {
            return "Camera update hook is unavailable"sv;
        }
        return "SmoothCam cannot currently yield camera control"sv;
    }

    bool SmoothCamCameraControl::Acquire()
    {
        if (improvedCameraDetected_.load(std::memory_order_acquire)) {
            logger::warn("Cannot acquire camera: Improved Camera is installed and unsupported");
            return false;
        }
        auto* api = api_.load(std::memory_order_acquire);
        if (!api) {
            logger::error("Cannot acquire camera: SmoothCam API is unavailable");
            return false;
        }
        if (!api->IsCameraEnabled()) {
            logger::warn("Cannot acquire camera: SmoothCam is disabled");
            return false;
        }
        if (!IsAPIThread(*api)) {
            logger::error("Cannot acquire camera: call is not on SmoothCam's API thread");
            return false;
        }
        if (!CameraHook::IsUpdateHookHealthy()) {
            logger::error("Cannot acquire camera: camera update hook is unavailable");
            return false;
        }

        if (ownsCamera_.load(std::memory_order_acquire)) {
            return StillOwnsCamera();
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
        return true;
    }

    bool SmoothCamCameraControl::StillOwnsCamera() const noexcept
    {
        if (!ownsCamera_.load(std::memory_order_acquire)) {
            return false;
        }

        const auto* api = api_.load(std::memory_order_acquire);
        return api && IsAPIThread(*api) &&
               api->GetCameraOwner() == SKSE::GetPluginHandle();
    }

    bool SmoothCamCameraControl::OwnsCamera() const noexcept
    {
        return ownsCamera_.load(std::memory_order_acquire);
    }

    CameraApplyResult SmoothCamCameraControl::Apply(const CameraPose& a_pose)
    {
        auto* api = api_.load(std::memory_order_acquire);
        if (!api || !ownsCamera_.load(std::memory_order_acquire)) {
            ownsCamera_.store(false, std::memory_order_release);
            return CameraApplyResult::kNotOwner;
        }
        if (!IsAPIThread(*api)) {
            return CameraApplyResult::kWrongThread;
        }
        if (api->GetCameraOwner() != SKSE::GetPluginHandle()) {
            ownsCamera_.store(false, std::memory_order_release);
            return CameraApplyResult::kNotOwner;
        }
        if (!CameraHook::IsUpdateHookHealthy()) {
            return CameraApplyResult::kUpdatePathUnavailable;
        }
        return output_.Apply(a_pose);
    }

    CameraReleaseResult SmoothCamCameraControl::Release()
    {
        if (!ownsCamera_.load(std::memory_order_acquire)) {
            return CameraReleaseResult::kNoOwnership;
        }
        auto* api = api_.load(std::memory_order_acquire);
        if (!api) {
            logger::error("SmoothCam interface disappeared while camera ownership was active");
            ownsCamera_.store(false, std::memory_order_release);
            return CameraReleaseResult::kNoOwnership;
        }
        if (!IsAPIThread(*api)) {
            return CameraReleaseResult::kWrongThread;
        }

        const auto pluginHandle = SKSE::GetPluginHandle();
        const auto owner = api->GetCameraOwner();
        if (owner != pluginHandle) {
            logger::warn("SmoothCam reports another camera owner while restoring (owner={})", owner);
            ownsCamera_.store(false, std::memory_order_release);
            return CameraReleaseResult::kNoOwnership;
        }

        const auto goalResult = api->SendToGoalPosition(
            pluginHandle, true, false, RE::PlayerCharacter::GetSingleton());
        logger::info("SmoothCam SendToGoalPosition(true) -> {}", ResultName(goalResult));

        const auto releaseResult = api->ReleaseCameraControl(pluginHandle);
        logger::info("SmoothCam ReleaseCameraControl -> {}", ResultName(releaseResult));
        if (releaseResult == SmoothCamAPI::APIResult::OK ||
            releaseResult == SmoothCamAPI::APIResult::NotOwner) {
            ownsCamera_.store(false, std::memory_order_release);
            return releaseResult == SmoothCamAPI::APIResult::OK ?
                CameraReleaseResult::kReleased : CameraReleaseResult::kNoOwnership;
        }
        return CameraReleaseResult::kFailed;
    }

    bool SmoothCamCameraControl::EmergencyRelease() noexcept
    {
        if (!ownsCamera_.load(std::memory_order_acquire)) {
            return true;
        }

        auto* api = api_.load(std::memory_order_acquire);
        const auto pluginHandle = SKSE::GetPluginHandle();
        if (!api) {
            ownsCamera_.store(false, std::memory_order_release);
            return true;
        }
        if (!IsAPIThread(*api)) {
            return false;
        }
        if (api->GetCameraOwner() != pluginHandle) {
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

    bool SmoothCamCameraControl::IsImprovedCameraLoaded() noexcept
    {
        return REX::W32::GetModuleHandleW(L"ImprovedCameraSE.dll") != nullptr ||
               REX::W32::GetModuleHandleW(L"ImprovedCameraSE-NG.dll") != nullptr;
    }
}
