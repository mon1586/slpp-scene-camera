#pragma once

#include "runtime/IRuntimeClient.h"
#include "runtime/SceneEvent.h"

namespace ssc::runtime
{
    class SceneEventMailbox
    {
    public:
        static SceneEventMailbox* GetSingleton() noexcept;

        [[nodiscard]] std::uint64_t CurrentGeneration() const noexcept;
        void BeginNewGeneration();
        [[nodiscard]] bool HasPending() const noexcept;
        [[nodiscard]] bool Enqueue(SceneEvent a_event, std::uint64_t a_generation);
        void DispatchPending(IRuntimeClient& a_client);

    private:
        static constexpr std::size_t kCapacity = 32;

        struct QueuedSceneEvent
        {
            SceneEvent event;
            std::uint64_t generation{ 0 };
        };

        std::mutex mutex_;
        std::array<QueuedSceneEvent, kCapacity> events_{};
        std::size_t head_{ 0 };
        std::size_t size_{ 0 };
        std::atomic<std::uint64_t> generation_{ 1 };
        std::atomic_bool hasPending_{ false };
        std::atomic_bool emergencyReset_{ false };
        std::once_flag dispatchThreadLogged_;
    };
}
