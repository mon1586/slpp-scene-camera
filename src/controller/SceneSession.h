#pragma once

#include <cstdint>
#include <optional>

namespace ssc::controller
{
    struct SceneKey
    {
        std::uint32_t senderID{ 0 };
        std::int32_t threadID{ -1 };

        [[nodiscard]] friend bool operator==(const SceneKey&, const SceneKey&) = default;
    };

    class SceneSession
    {
    public:
        enum class State
        {
            kIdle,
            kPreparing,
            kActive,
            kRestoring,
        };

        [[nodiscard]] State GetState() const noexcept { return state_; }
        [[nodiscard]] bool IsIdle() const noexcept { return state_ == State::kIdle; }
        [[nodiscard]] bool IsPreparing() const noexcept { return state_ == State::kPreparing; }
        [[nodiscard]] bool IsActive() const noexcept { return state_ == State::kActive; }
        [[nodiscard]] bool Matches(const SceneKey& a_key) const noexcept
        {
            return key_.has_value() && *key_ == a_key;
        }

        [[nodiscard]] bool Prepare(const SceneKey& a_key) noexcept
        {
            if (IsActive()) {
                return false;
            }
            key_ = a_key;
            state_ = State::kPreparing;
            return true;
        }

        [[nodiscard]] bool Activate(const SceneKey& a_key) noexcept
        {
            if (!IsPreparing() || !Matches(a_key)) {
                return false;
            }
            state_ = State::kActive;
            return true;
        }

        void BeginRestore() noexcept
        {
            if (!IsIdle()) {
                state_ = State::kRestoring;
            }
        }

        void Clear() noexcept
        {
            key_.reset();
            state_ = State::kIdle;
        }

    private:
        State state_{ State::kIdle };
        std::optional<SceneKey> key_;
    };
}
