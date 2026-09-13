#pragma once
#include <mutex>

namespace ssc::runtime
{
    // Only receipt stamps, immutable snapshot publication and selection decisions.
    // Never hold across VM, engine calls, regex evaluation or disk I/O.
    inline std::mutex& SelectionBoundary()
    {
        static std::mutex mutex;
        return mutex;
    }
}
