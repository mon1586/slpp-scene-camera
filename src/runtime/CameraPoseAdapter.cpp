#include "runtime/CameraPoseAdapter.h"

namespace ssc::runtime
{
    CameraPose ToRuntimeCameraPose(
        const core::CameraPose& a_pose,
        float a_fovOffsetDegrees) noexcept
    {
        const auto& basis = a_pose.basis;
        CameraPose result;
        result.position = { a_pose.position.x, a_pose.position.y, a_pose.position.z };
        result.rotation.entries = {{
            { basis.viewForward.x, basis.up.x, basis.right.x },
            { basis.viewForward.y, basis.up.y, basis.right.y },
            { basis.viewForward.z, basis.up.z, basis.right.z },
        }};
        result.fovOffsetDegrees = a_fovOffsetDegrees;
        return result;
    }
}
