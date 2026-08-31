#pragma once

#include "controller/ISceneController.h"

namespace ssc::controller
{
    class SexLabPSceneController final :
        public ISceneController,
        public RE::BSTEventSink<SKSE::ModCallbackEvent>
    {
    public:
        static SexLabPSceneController* GetSingleton() noexcept;

        [[nodiscard]] bool Register(SceneEventHandler a_handler) override;
        [[nodiscard]] SceneParticipantSnapshot CollectParticipants(
            const SceneKey& a_key) const override;

        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::ModCallbackEvent* a_event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>* a_source) override;

    private:
        SceneEventHandler handler_{ nullptr };
        bool registered_{ false };
    };
}
