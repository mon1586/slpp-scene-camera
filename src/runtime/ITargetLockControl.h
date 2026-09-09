#pragma once

namespace ssc::runtime
{
    class ITargetLockControl
    {
    public:
        virtual ~ITargetLockControl() = default;
        // Called from the camera update. True requires a later FinishUnlock.
        [[nodiscard]] virtual bool RequestUnlock() noexcept = 0;
        // False retains the obligation to release, including on a wrong thread.
        [[nodiscard]] virtual bool FinishUnlock(bool a_cancel) noexcept = 0;
    };
}
