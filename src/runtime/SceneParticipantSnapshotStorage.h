#pragma once

#include "runtime/SceneEvent.h"

namespace ssc::runtime
{
    struct SceneParticipantSnapshot::Storage
    {
        std::array<RE::ActorHandle, kCapacity> handles{};
    };
}
