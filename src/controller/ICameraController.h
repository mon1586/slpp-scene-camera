#pragma once

#include "core/CameraTypes.h"

namespace ssc::controller
{
    enum class CameraApplyResult
    {
        kApplied,
        kUnsupportedState,
        kMissingCamera,
    };

    class ICameraController
    {
    public:
        virtual ~ICameraController() = default;

        [[nodiscard]] virtual bool CanAcquire() const noexcept = 0;
        [[nodiscard]] virtual bool Acquire() = 0;
        [[nodiscard]] virtual bool StillOwnsCamera() const noexcept = 0;
        [[nodiscard]] virtual bool OwnsCamera() const noexcept = 0;
        [[nodiscard]] virtual CameraApplyResult Apply(
            RE::PlayerCamera* a_camera,
            const core::CameraPose& a_pose) = 0;
        virtual void Release(const RE::Actor* a_player) = 0;
        [[nodiscard]] virtual bool EmergencyRelease() noexcept = 0;
    };
}
