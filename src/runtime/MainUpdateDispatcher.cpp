#include "runtime/MainUpdateDispatcher.h"

namespace ssc::runtime
{
    void MainUpdateDispatcher::Post(std::function<void()> a_work)
    {
        std::scoped_lock lock{ mutex_ };
        pending_.push_back(std::move(a_work));
    }

    void MainUpdateDispatcher::Tick(IRuntimeClient& a_client)
    {
        // Retry even without camera updates or new events. Taking the batch
        // afterward keeps queued notifications intact if reset throws.
        static_cast<void>(a_client.ProcessPendingReset("main update reset"sv));
        std::vector<std::function<void()>> batch;
        {
            std::scoped_lock lock{ mutex_ };
            batch.swap(pending_);
        }
        for (auto& work : batch) {
            work();
        }
    }
}
