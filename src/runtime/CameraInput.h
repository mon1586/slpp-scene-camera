#pragma once

#include "runtime/IRuntimeClient.h"

namespace ssc::runtime
{
    class CameraInput final : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static CameraInput* GetSingleton() noexcept;

        static void Configure(IRuntimeClient& a_client) noexcept;
        [[nodiscard]] static bool Register() noexcept;

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* a_events,
            RE::BSTEventSource<RE::InputEvent*>*) override;

    private:
        static inline IRuntimeClient* client_{ nullptr };
    };
}
