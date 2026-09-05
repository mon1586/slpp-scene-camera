#pragma once

#include <string_view>

namespace ssc::runtime
{
    namespace detail
    {
        [[nodiscard]] constexpr char FoldAsciiCase(char a_character) noexcept
        {
            return a_character >= 'A' && a_character <= 'Z' ?
                static_cast<char>(a_character + ('a' - 'A')) : a_character;
        }
    }

    [[nodiscard]] constexpr bool PluginFilenameEquals(
        std::string_view a_left,
        std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) {
            return false;
        }

        for (std::size_t index = 0; index < a_left.size(); ++index) {
            if (detail::FoldAsciiCase(a_left[index]) !=
                detail::FoldAsciiCase(a_right[index])) {
                return false;
            }
        }
        return true;
    }
}
