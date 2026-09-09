#pragma once

#include "runtime/IRuntimeClient.h"

#include <vector>
#include <functional>
#include <mutex>

namespace ssc::runtime
{
    // Producers only publish work. Tick is called exclusively by the game's
    // main-update hook, even when no supported camera state is updating.
    class MainUpdateDispatcher
    {
    public:
        void Post(std::function<void()> a_work);
        void Tick(IRuntimeClient& a_client);

    private:
        std::mutex mutex_;
        std::vector<std::function<void()>> pending_;
    };
}
