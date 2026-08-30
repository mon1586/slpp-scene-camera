#include "core/HighAltitudeRig.h"

#include <cmath>

namespace ssc::core
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const Vec3& a_value) noexcept
        {
            return std::isfinite(a_value.x) && std::isfinite(a_value.y) && std::isfinite(a_value.z);
        }
    }

    std::optional<CameraPose> HighAltitudeRig::Evaluate(
        const CameraFrameInput& a_input) const noexcept
    {
        Vec3 center{};
        std::size_t validSubjectCount = 0;

        for (const auto& subject : a_input.subjects) {
            if (!IsFinite(subject)) {
                continue;
            }

            center.x += subject.x;
            center.y += subject.y;
            center.z += subject.z;
            ++validSubjectCount;
        }

        if (validSubjectCount > 0) {
            const auto inverseCount = 1.0F / static_cast<float>(validSubjectCount);
            center.x *= inverseCount;
            center.y *= inverseCount;
            center.z *= inverseCount;
        } else if (a_input.fallbackCenter && IsFinite(*a_input.fallbackCenter)) {
            center = *a_input.fallbackCenter;
        } else {
            return std::nullopt;
        }

        return CameraPose{
            .position = { center.x, center.y, center.z + kAltitude },
            .rotation = {
                .entries = {{
                    {{ 0.0F, 0.0F, 1.0F }},
                    {{ 1.0F, 0.0F, 0.0F }},
                    {{ 0.0F, 1.0F, 0.0F }},
                }},
            },
        };
    }
}
