#include "runtime/TDMTargetLockControl.h"

#include <REX/W32/KERNEL32.h>
#include <TrueDirectionalMovementAPI.h>

namespace ssc::runtime
{
    namespace
    {
        template <class... Args>
        void Log(fmt::format_string<Args...> a_format, Args&&... a_args) noexcept
        {
            try {
                spdlog::info(a_format, std::forward<Args>(a_args)...);
            } catch (...) {
            }
        }

        std::string_view ResultName(TDM_API::APIResult a_result) noexcept
        {
            switch (a_result) {
            case TDM_API::APIResult::OK: return "OK";
            case TDM_API::APIResult::NotOwner: return "NotOwner";
            case TDM_API::APIResult::MustKeep: return "MustKeep";
            case TDM_API::APIResult::AlreadyGiven: return "AlreadyGiven";
            case TDM_API::APIResult::AlreadyTaken: return "AlreadyTaken";
            case TDM_API::APIResult::BadThread: return "BadThread";
            default: return "unknown";
            }
        }
    }

    TDMTargetLockControl* TDMTargetLockControl::GetSingleton() noexcept
    {
        static TDMTargetLockControl singleton;
        return std::addressof(singleton);
    }

    void TDMTargetLockControl::RequestAPI()
    {
        auto* api = static_cast<TDM_API::IVTDM5*>(
            TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V5));
        Log("TDM target lock integration: {}",
            api ? "V5 connected" : "unavailable (TDM absent or API V5 unsupported)");
        ConnectAPI(api, SKSE::GetPluginHandle());
    }

    void TDMTargetLockControl::ConnectAPI(
        TDM_API::IVTDM5* a_api, SKSE::PluginHandle a_handle) noexcept
    {
        pluginHandle_ = a_handle;
        api_.store(a_api, std::memory_order_release);
        Log("TDM connection: currentThread={} apiThread={}; unlock is deferred to camera update",
            REX::W32::GetCurrentThreadId(), a_api ? a_api->GetTDMThreadId() : 0);
    }

    bool TDMTargetLockControl::RequestUnlock() noexcept
    {
        auto* api = api_.load(std::memory_order_acquire);
        if (!api) {
            Log("TDM unlock skipped: API unavailable");
            return false;
        }
        const auto current = REX::W32::GetCurrentThreadId();
        const auto expected = api->GetTDMThreadId();
        // The camera update supplies the execution context. This comparison
        // checks it; neither the API ID nor an SKSE task moves execution there.
        if (current != expected) {
            Log("TDM unlock skipped: thread mismatch (currentThread={} apiThread={})", current, expected);
            return false;
        }
        if (ownsDisable_.load(std::memory_order_acquire)) {
            return true;
        }
        if (!api->GetTargetLockState()) {
            Log("TDM unlock skipped: no target locked (currentThread={} apiThread={})", current, expected);
            return false;
        }
        const auto result = api->RequestDisableTargetLock(pluginHandle_);
        const auto acquired = result == TDM_API::APIResult::OK ||
                              result == TDM_API::APIResult::AlreadyGiven;
        ownsDisable_.store(acquired, std::memory_order_release);
        Log("TDM RequestDisableTargetLock -> {} (owner={} plugin={} currentThread={} apiThread={})",
            ResultName(result), api->GetDisableTargetLockOwner(), pluginHandle_, current, expected);
        return acquired;
    }

    bool TDMTargetLockControl::FinishUnlock(bool a_cancel) noexcept
    {
        if (!ownsDisable_.load(std::memory_order_acquire)) {
            return true;
        }
        auto* api = api_.load(std::memory_order_acquire);
        if (!api) {
            return false;
        }
        const auto current = REX::W32::GetCurrentThreadId();
        const auto expected = api->GetTDMThreadId();
        if (current != expected) {
            if (!releaseThreadWarningLogged_.exchange(true, std::memory_order_relaxed)) {
                Log("TDM release pending: thread mismatch (currentThread={} apiThread={})", current, expected);
            }
            return false;
        }
        releaseThreadWarningLogged_.store(false, std::memory_order_relaxed);
        if (api->GetDisableTargetLockOwner() != pluginHandle_) {
            Log("TDM unlock finished: SSC no longer owns the disable");
            ownsDisable_.store(false, std::memory_order_release);
            return true;
        }
        if (!a_cancel && api->GetTargetLockState()) {
            return false;
        }
        const auto result = api->ReleaseDisableTargetLock(pluginHandle_);
        Log("TDM ReleaseDisableTargetLock -> {} ({}, currentThread={} apiThread={})",
            ResultName(result), a_cancel ? "cancel or timeout" : "target unlocked", current, expected);
        if (result != TDM_API::APIResult::OK && result != TDM_API::APIResult::NotOwner) {
            return false;
        }
        ownsDisable_.store(false, std::memory_order_release);
        return true;
    }
}
