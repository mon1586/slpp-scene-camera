#pragma once

#include "controller/SceneKey.h"

namespace ssc::controller
{
    enum class SceneEventType
    {
        kAnimationStarting,
        kAnimationStart,
        kAnimationEnding,
        kAnimationEnd,
    };

    struct SceneParticipantSnapshot
    {
        static constexpr std::size_t kCapacity = 32;

        std::array<RE::ActorHandle, kCapacity> handles{};
        std::size_t count{ 0 };
        bool containsPlayer{ false };
        bool truncated{ false };
    };

    struct SceneEvent
    {
        SceneEventType type{ SceneEventType::kAnimationStarting };
        SceneKey key;
        SceneParticipantSnapshot participants;
    };
}
