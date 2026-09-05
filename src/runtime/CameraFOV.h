#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace ssc::runtime
{
    inline constexpr float kMinimumFOVDegrees = 10.0F;
    inline constexpr float kMaximumFOVDegrees = 170.0F;

    [[nodiscard]] inline std::optional<float> ResolveFOVOffset(
        float a_baseFOVDegrees,
        float a_requestedOffsetDegrees) noexcept
    {
        if (!std::isfinite(a_baseFOVDegrees) ||
            !std::isfinite(a_requestedOffsetDegrees)) {
            return std::nullopt;
        }

        return std::clamp(
            a_requestedOffsetDegrees,
            kMinimumFOVDegrees - a_baseFOVDegrees,
            kMaximumFOVDegrees - a_baseFOVDegrees);
    }
}
