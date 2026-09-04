#pragma once

#include "runtime/RuntimeTypes.h"

#include <memory>

namespace ssc::runtime
{
    class IDebugVisualization
    {
    public:
        virtual ~IDebugVisualization() = default;

        [[nodiscard]] virtual bool ShowAnchor(
            const Vec3& a_position,
            const Vec3& a_forward) noexcept = 0;
        virtual void Update() noexcept = 0;
        virtual void HideAnchor() noexcept = 0;
        virtual void ShowVisibility(
            std::shared_ptr<const core::VisibilityEvaluationSnapshot> a_evaluation) noexcept = 0;
        virtual void HideVisibility() noexcept = 0;
    };
}
