#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace ssc::runtime
{
    [[nodiscard]] inline std::optional<std::int32_t> ParseSceneInstanceID(
        std::string_view a_text, float a_number) noexcept
    {
        if (!a_text.empty()) {
            std::int32_t parsed = 0;
            const auto [end, error] = std::from_chars(
                a_text.data(), a_text.data() + a_text.size(), parsed);
            if (error == std::errc{} && end == a_text.data() + a_text.size()) {
                return parsed;
            }
        }

        // Keep the limits exact: float(INT32_MAX) rounds up to 2147483648.
        const auto number = static_cast<double>(a_number);
        constexpr auto minimum = static_cast<double>(std::numeric_limits<std::int32_t>::min());
        constexpr auto maximum = static_cast<double>(std::numeric_limits<std::int32_t>::max());
        if (!std::isfinite(number) || number < minimum || number > maximum) {
            return std::nullopt;
        }
        const auto rounded = std::round(number);
        if (rounded < minimum || rounded > maximum) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(rounded);
    }
}
