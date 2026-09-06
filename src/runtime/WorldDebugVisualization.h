#pragma once

#include "runtime/IDebugVisualization.h"

#include <mutex>
#include <optional>

namespace ssc::runtime
{
    class WorldDebugVisualization final : public IDebugVisualization
    {
    public:
        static WorldDebugVisualization* GetSingleton() noexcept;
        [[nodiscard]] static bool Register();
        [[nodiscard]] bool Available() const noexcept;

        [[nodiscard]] bool ShowAnchor(
            const Vec3& a_position,
            const Vec3& a_forward,
            const Vec3& a_targetPosition) noexcept override;
        void Update() noexcept override;
        void HideAnchor() noexcept override;
        void ShowVisibility(
            std::shared_ptr<const core::VisibilityEvaluationSnapshot> a_evaluation) noexcept override;
        void HideVisibility() noexcept override;
        [[nodiscard]] bool Enabled() const noexcept override;
        void StepCandidate(int a_direction) noexcept override;

        void SetOccludedSegmentsVisible(bool a_visible) noexcept;
        [[nodiscard]] bool OccludedSegmentsVisible() const noexcept;
        [[nodiscard]] std::string SelectedCandidateLabel() const;

    private:
        struct AnchorDisplay
        {
            Vec3 position;
            Vec3 forward;
            Vec3 targetPosition;
        };

        static void __stdcall RenderVisibility();

        mutable std::mutex anchorMutex_;
        std::optional<AnchorDisplay> anchor_;
        std::atomic<std::shared_ptr<const core::VisibilityEvaluationSnapshot>> evaluation_;
        std::atomic_size_t selectedCandidate_{ 0 };
        std::atomic_bool showOccludedSegments_{ true };
        std::atomic_bool registered_{ false };
    };
}
