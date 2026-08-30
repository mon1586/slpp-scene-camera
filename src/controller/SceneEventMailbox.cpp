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

    void SceneEventMailbox::Enqueue(SceneEvent a_event) noexcept
    {
        std::scoped_lock lock{ mutex_ };
        if (emergencyReset_.load(std::memory_order_acquire)) {
            return;
        }
        if (size_ == events_.size()) {
            emergencyReset_.store(true, std::memory_order_release);
            head_ = 0;
            size_ = 0;
            logger::error("Scene event mailbox overflow; camera will be reset on the next camera update");
            return;
        }

        const auto tail = (head_ + size_) % events_.size();
        events_[tail] = a_event;
        ++size_;
    }

    void SceneEventMailbox::DispatchPending() noexcept
    {
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
        }

        auto* controller = SceneCameraController::GetSingleton();
        if (emergencyReset) {
            controller->Reset("scene event mailbox overflow"sv);
            return;
        }

        for (std::size_t index = 0; index < pendingCount; ++index) {
            const auto& event = pending[index];
            switch (event.type) {
            case SceneEventType::kAnimationStarting:
                controller->OnAnimationStarting(event.senderID, event.threadID);
                break;
            case SceneEventType::kAnimationStart:
                controller->OnAnimationStart(event.senderID, event.threadID);
                break;
            case SceneEventType::kAnimationEnding:
                controller->OnAnimationEnding(event.senderID, event.threadID);
                break;
            case SceneEventType::kAnimationEnd:
                controller->OnAnimationEnd(event.senderID, event.threadID);
                break;
            case SceneEventType::kResetPreLoadGame:
                controller->Reset("pre-load game"sv);
                break;
            case SceneEventType::kResetNewGame:
                controller->Reset("new game"sv);
                break;
            }
        }
    }
}
