#pragma once

#include "runtime/SceneKey.h"

namespace ssc::runtime
{
    class SexLabPSceneSource;

    enum class SceneEventType
    {
        kAnimationStarting,
        kAnimationStart,
        kAnimationChange,
        kAnimationEnding,
        kAnimationEnd,
    };

    class SceneParticipantSnapshot
    {
    public:
        static constexpr std::size_t kCapacity = 32;

        [[nodiscard]] std::size_t Count() const noexcept { return count_; }
        [[nodiscard]] bool ContainsPlayer() const noexcept { return containsPlayer_; }
        [[nodiscard]] bool WasTruncated() const noexcept { return truncated_; }

    private:
        struct Storage;

        std::shared_ptr<const Storage> storage_;
        std::size_t count_{ 0 };
        bool containsPlayer_{ false };
        bool truncated_{ false };

        friend class SexLabPSceneSource;
    };

    struct SceneEvent
    {
        SceneEventType type{ SceneEventType::kAnimationStarting };
        SceneKey key;
        SceneParticipantSnapshot participants;
    };
}
