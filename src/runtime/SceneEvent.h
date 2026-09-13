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
        kStageStart,
        kActorsRelocated,
        kAnimationEnding,
        kAnimationEnd,
    };

    [[nodiscard]] constexpr std::string_view SceneEventTypeName(
        SceneEventType a_type) noexcept
    {
        switch (a_type) {
        case SceneEventType::kAnimationStarting:
            return "AnimationStarting"sv;
        case SceneEventType::kAnimationStart:
            return "AnimationStart"sv;
        case SceneEventType::kAnimationChange:
            return "AnimationChange"sv;
        case SceneEventType::kStageStart:
            return "StageStart"sv;
        case SceneEventType::kActorsRelocated:
            return "ActorsRelocated"sv;
        case SceneEventType::kAnimationEnding:
            return "AnimationEnding"sv;
        case SceneEventType::kAnimationEnd:
            return "AnimationEnd"sv;
        default:
            return "Unknown"sv;
        }
    }

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
        std::size_t playerIndex_{ kCapacity };
        bool containsPlayer_{ false };
        bool truncated_{ false };

        friend class SexLabPSceneSource;
    };

    struct SceneEvent
    {
        SceneEventType type{ SceneEventType::kAnimationStarting };
        SceneKey key;
        SceneParticipantSnapshot participants;
        std::uint64_t receipt{ 0 };
    };
}
