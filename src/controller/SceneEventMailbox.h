#pragma once

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
        RE::FormID senderID{ 0 };
        std::int32_t threadID{ -1 };
        std::uint64_t generation{ 0 };
        SceneParticipantSnapshot participants;
    };

    class SceneEventMailbox
    {
    public:
        static SceneEventMailbox* GetSingleton() noexcept;

        [[nodiscard]] std::uint64_t CurrentGeneration() const noexcept;
        void BeginNewGeneration();
        [[nodiscard]] bool HasPending() const noexcept;
        [[nodiscard]] bool Enqueue(SceneEvent a_event);
        void DispatchPending();

    private:
        static constexpr std::size_t kCapacity = 32;

        std::mutex mutex_;
        std::array<SceneEvent, kCapacity> events_{};
        std::size_t head_{ 0 };
        std::size_t size_{ 0 };
        std::atomic<std::uint64_t> generation_{ 1 };
        std::atomic_bool hasPending_{ false };
        std::atomic_bool emergencyReset_{ false };
        std::once_flag dispatchThreadLogged_;
    };
}
