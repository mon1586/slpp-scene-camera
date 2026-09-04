#pragma once

#include "runtime/IVisibilityProbe.h"

namespace ssc::runtime
{
    class HavokVisibilityProbe final : public IVisibilityProbe
    {
    public:
        static HavokVisibilityProbe* GetSingleton() noexcept;

        [[nodiscard]] VisibilityRayHit Trace(
            const Vec3& a_start,
            const Vec3& a_target,
            std::uint32_t a_targetActorID) noexcept override;
    };
}
