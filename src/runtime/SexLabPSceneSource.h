#pragma once

#include "runtime/ISceneSource.h"

namespace ssc::runtime
{
    class SexLabPSceneSource final :
        public ISceneSource,
        public RE::BSTEventSink<SKSE::ModCallbackEvent>
    {
    public:
        static SexLabPSceneSource* GetSingleton() noexcept;

        [[nodiscard]] bool Register(SceneEventHandler a_handler) override;
        [[nodiscard]] std::optional<SceneControlState> CollectControlState() const override;
        [[nodiscard]] SceneParticipantSnapshot CollectParticipants(
            const SceneKey& a_key) const override;
        [[nodiscard]] std::optional<SceneAnchorSamples> CollectAnchorInput(
            const SceneParticipantSnapshot& a_participants) const override;
        [[nodiscard]] std::optional<SceneVisibilitySamples> CollectVisibilityInput(
            const SceneParticipantSnapshot& a_participants,
            std::span<std::uint32_t> a_participantIDStorage,
            std::span<VisibilityTarget> a_targetStorage) const override;

        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::ModCallbackEvent* a_event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>* a_source) override;

    private:
        SceneEventHandler handler_{ nullptr };
        bool registered_{ false };
    };
}
