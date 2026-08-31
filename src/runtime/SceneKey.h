#pragma once

#include <cstdint>

namespace ssc::runtime
{
    struct SceneKey
    {
        std::uint32_t sourceID{ 0 };
        std::int32_t instanceID{ -1 };

        [[nodiscard]] friend bool operator==(const SceneKey&, const SceneKey&) = default;
    };
}
