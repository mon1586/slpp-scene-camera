#pragma once

namespace ssc::controller
{
    enum class SceneEventType
    {
        kAnimationStarting,
        kAnimationStart,
        kAnimationEnding,
        kAnimationEnd,
        kResetPreLoadGame,
        kResetNewGame,
    };

    struct SceneEvent
    {
        SceneEventType type{ SceneEventType::kAnimationStarting };
        RE::FormID senderID{ 0 };
        std::int32_t threadID{ -1 };
    };

    class SceneEventMailbox
    {
    public:
        static SceneEventMailbox* GetSingleton() noexcept;

        void Enqueue(SceneEvent a_event) noexcept;
        void DispatchPending() noexcept;

    private:
        static constexpr std::size_t kCapacity = 32;

        std::mutex mutex_;
        std::array<SceneEvent, kCapacity> events_{};
        std::size_t head_{ 0 };
        std::size_t size_{ 0 };
        std::atomic_bool emergencyReset_{ false };
        std::once_flag dispatchThreadLogged_;
    };
}
