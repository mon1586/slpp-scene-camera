#pragma once

#include "runtime/RuntimeTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ssc::runtime
{
    struct VisibilityRayHit
    {
        bool querySucceeded{ false };
        bool reachesTarget{ false };
        bool startsInsideCollision{ false };
        std::size_t queryCount{ 0 };
        std::optional<Vec3> position;
        std::optional<Vec3> normal;
        float fraction{ 1.0F };
        std::uint32_t objectID{ 0 };
        std::string object;
    };

    class IVisibilityProbe
    {
    public:
        virtual ~IVisibilityProbe() = default;

        [[nodiscard]] virtual VisibilityRayHit Trace(
            const Vec3& a_start,
            const Vec3& a_target,
            std::uint32_t a_targetActorID) noexcept = 0;
    };
}
