#include "core/HighAltitudeRig.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

namespace
{
    [[nodiscard]] bool Near(float a_lhs, float a_rhs) noexcept
    {
        return std::fabs(a_lhs - a_rhs) < 0.001F;
    }
}

int main()
{
    const ssc::core::HighAltitudeRig rig;

    {
        constexpr std::array subjects{
            ssc::core::Vec3{ 10.0F, 20.0F, 30.0F },
            ssc::core::Vec3{ 30.0F, 40.0F, 50.0F },
        };
        const auto pose = rig.Evaluate({ subjects, {} });

        assert(Near(pose.target.x, 20.0F));
        assert(Near(pose.target.y, 30.0F));
        assert(Near(pose.target.z, 40.0F));
        assert(Near(pose.position.z, 40.0F + ssc::core::HighAltitudeRig::kAltitude));
    }

    {
        constexpr ssc::core::Vec3 fallback{ 1.0F, 2.0F, 3.0F };
        const auto pose = rig.Evaluate({ {}, fallback });

        assert(Near(pose.target.x, fallback.x));
        assert(Near(pose.target.y, fallback.y));
        assert(Near(pose.target.z, fallback.z));
    }

    {
        constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
        constexpr std::array subjects{
            ssc::core::Vec3{ nan, 0.0F, 0.0F },
            ssc::core::Vec3{ 7.0F, 8.0F, 9.0F },
        };
        const auto pose = rig.Evaluate({ subjects, {} });

        assert(Near(pose.target.x, 7.0F));
        assert(Near(pose.target.y, 8.0F));
        assert(Near(pose.target.z, 9.0F));
    }

    return 0;
}

