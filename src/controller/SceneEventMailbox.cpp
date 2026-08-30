#include "controller/SceneEventMailbox.h"

#include "controller/SceneCameraController.h"

#include <REX/W32/KERNEL32.h>

namespace ssc::controller
{
    SceneEventMailbox* SceneEventMailbox::GetSingleton() noexcept
    {
        static SceneEventMailbox singleton;
        return std::addressof(singleton);
    }

    std::uint64_t SceneEventMailbox::CurrentGeneration() const noexcept
    {
        return generation_.load(std::memory_order_acquire);
    }

    void SceneEventMailbox::BeginNewGeneration()
    {
        std::scoped_lock lock{ mutex_ };
        generation_.fetch_add(1, std::memory_order_acq_rel);
        head_ = 0;
        size_ = 0;
        emergencyReset_.store(false, std::memory_order_release);
        hasPending_.store(false, std::memory_order_release);
    }

    bool SceneEventMailbox::HasPending() const noexcept
    {
        return hasPending_.load(std::memory_order_acquire);
    }

    bool SceneEventMailbox::Enqueue(SceneEvent a_event)
    {
        if (a_event.generation != CurrentGeneration()) {
            return false;
        }

        std::scoped_lock lock{ mutex_ };
        if (a_event.generation != generation_.load(std::memory_order_relaxed)) {
            return false;
        }
        if (emergencyReset_.load(std::memory_order_acquire)) {
            return false;
        }
        if (size_ == events_.size()) {
            emergencyReset_.store(true, std::memory_order_release);
            head_ = 0;
            size_ = 0;
            hasPending_.store(true, std::memory_order_release);
            logger::error("Scene event mailbox overflow; camera will be reset on the next camera update");
            return false;
        }

        const auto tail = (head_ + size_) % events_.size();
        events_[tail] = a_event;
        ++size_;
        hasPending_.store(true, std::memory_order_release);
        return true;
    }

    void SceneEventMailbox::DispatchPending()
    {
        if (!HasPending()) {
            return;
        }

        std::call_once(dispatchThreadLogged_, [] {
            logger::info("Scene event mailbox dispatch thread is {}", REX::W32::GetCurrentThreadId());
        });

        std::array<SceneEvent, kCapacity> pending{};
        std::size_t pendingCount = 0;
        bool emergencyReset = false;
        {
            std::scoped_lock lock{ mutex_ };
            emergencyReset = emergencyReset_.exchange(false, std::memory_order_acq_rel);
            if (!emergencyReset) {
                pendingCount = size_;
                for (std::size_t index = 0; index < pendingCount; ++index) {
                    pending[index] = events_[(head_ + index) % events_.size()];
                }
            }
            head_ = 0;
            size_ = 0;
            hasPending_.store(false, std::memory_order_release);
        }

        auto* controller = SceneCameraController::GetSingleton();
        if (emergencyReset) {
            controller->Reset("scene event mailbox overflow"sv);
            return;
        }

        for (std::size_t index = 0; index < pendingCount; ++index) {
            const auto& event = pending[index];
            if (event.generation != CurrentGeneration()) {
                continue;
            }
            switch (event.type) {
            case SceneEventType::kAnimationStarting:
                controller->OnAnimationStarting(event);
                break;
            case SceneEventType::kAnimationStart:
                controller->OnAnimationStart(event);
                break;
            case SceneEventType::kAnimationEnding:
                controller->OnAnimationEnding(event);
                break;
            case SceneEventType::kAnimationEnd:
                controller->OnAnimationEnd(event);
                break;
            }
        }
    }
}
