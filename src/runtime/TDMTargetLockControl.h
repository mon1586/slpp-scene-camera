#pragma once

#include "runtime/ITargetLockControl.h"

namespace TDM_API { class IVTDM5; }

namespace ssc::runtime
{
    class TDMTargetLockControl final : public ITargetLockControl
    {
    public:
        static TDMTargetLockControl* GetSingleton() noexcept;
        void RequestAPI();
        void ConnectAPI(TDM_API::IVTDM5* a_api, SKSE::PluginHandle a_handle) noexcept;
        [[nodiscard]] bool RequestUnlock() noexcept override;
        [[nodiscard]] bool FinishUnlock(bool a_cancel) noexcept override;

    private:
        std::atomic<TDM_API::IVTDM5*> api_{ nullptr };
        SKSE::PluginHandle pluginHandle_{ SKSE::kInvalidPluginHandle };
        std::atomic_bool ownsDisable_{ false };
        std::atomic_bool releaseThreadWarningLogged_{ false };
    };
}
