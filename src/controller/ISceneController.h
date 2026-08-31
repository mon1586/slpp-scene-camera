#pragma once

#include "controller/SceneEvent.h"

namespace ssc::controller
{
    using SceneEventHandler = void (*)(SceneEvent);

    class ISceneController
    {
    public:
        virtual ~ISceneController() = default;

        [[nodiscard]] virtual bool Register(SceneEventHandler a_handler) = 0;
        [[nodiscard]] virtual SceneParticipantSnapshot CollectParticipants(
            const SceneKey& a_key) const = 0;
    };
}
