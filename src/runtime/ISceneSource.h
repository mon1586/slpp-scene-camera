#pragma once

#include "runtime/SceneEvent.h"
#include "runtime/RuntimeTypes.h"

namespace ssc::runtime
{
    using SceneEventHandler = void (*)(SceneEvent);

    class ISceneSource
    {
    public:
        virtual ~ISceneSource() = default;

        [[nodiscard]] virtual bool Register(SceneEventHandler a_handler) = 0;
        [[nodiscard]] virtual SceneParticipantSnapshot CollectParticipants(
            const SceneKey& a_key) const = 0;
        [[nodiscard]] virtual std::optional<SceneAnchorSamples> CollectAnchorInput(
            const SceneParticipantSnapshot& a_participants) const = 0;
        [[nodiscard]] virtual std::optional<SceneVisibilitySamples> CollectVisibilityInput(
            const SceneParticipantSnapshot& a_participants,
            std::span<std::uint32_t> a_participantIDStorage,
            std::span<VisibilityTarget> a_targetStorage) const = 0;
    };
}
