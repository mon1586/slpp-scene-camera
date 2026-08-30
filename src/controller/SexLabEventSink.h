#pragma once

namespace ssc::controller
{
    class SexLabEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
    {
    public:
        static SexLabEventSink* GetSingleton() noexcept;
        static void Register();

        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::ModCallbackEvent* a_event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>* a_source) override;

    private:
        bool registered_{ false };
    };
}

