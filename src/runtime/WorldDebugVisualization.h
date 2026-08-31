#pragma once

#include "runtime/IDebugVisualization.h"

namespace ssc::runtime
{
    class WorldDebugVisualization final : public IDebugVisualization
    {
    public:
        static WorldDebugVisualization* GetSingleton() noexcept;

        [[nodiscard]] bool ShowAnchor(
            const Vec3& a_position,
            const Vec3& a_forward) noexcept override;
        void Update() noexcept override;
        void HideAnchor() noexcept override;

    private:
        [[nodiscard]] bool CreateMarker(
            const Vec3& a_position,
            const Vec3& a_forward) noexcept;
        [[nodiscard]] bool UpdateMarker(
            const Vec3& a_position,
            const Vec3& a_forward) noexcept;
        void DestroyMarker() noexcept;

        RE::NiPointer<RE::BSTempEffectParticle> arrow_;
        bool arrowPrepared_{ false };
    };
}
