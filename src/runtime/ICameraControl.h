#pragma once

#include "runtime/RuntimeTypes.h"

namespace ssc::runtime
{
    enum class CameraApplyResult
    {
        kApplied,
        kUnsupportedState,
        kMissingCamera,
        kNotOwner,
        kWrongThread,
        kUpdatePathUnavailable,
    };

    enum class CameraReleaseResult
    {
        kReleased,
        kNoOwnership,
        kWrongThread,
        kFailed,
    };

    class ICameraControl
    {
    public:
        virtual ~ICameraControl() = default;

        [[nodiscard]] virtual bool CanAcquire() const noexcept = 0;
        [[nodiscard]] virtual bool Acquire() = 0;
        [[nodiscard]] virtual bool StillOwnsCamera() const noexcept = 0;
        [[nodiscard]] virtual bool OwnsCamera() const noexcept = 0;
        [[nodiscard]] virtual CameraApplyResult Apply(const CameraPose& a_pose) = 0;
        [[nodiscard]] virtual CameraReleaseResult Release() = 0;
        [[nodiscard]] virtual bool EmergencyRelease() noexcept = 0;
    };
}
