#pragma once

#include "runtime/IRuntimeClient.h"

namespace ssc::runtime
{
    class PluginRuntime
    {
    public:
        static void InitializeLog();
        [[nodiscard]] static bool RegisterLifecycle(IRuntimeClient& a_client);

    private:
        static void QueueLifecycleReset(std::string_view a_reason);
        static void MessageHandler(SKSE::MessagingInterface::Message* a_message);

        static inline IRuntimeClient* client_{ nullptr };
    };
}
