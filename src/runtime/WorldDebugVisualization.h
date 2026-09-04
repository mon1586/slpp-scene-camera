#pragma once

#include "runtime/IDebugVisualization.h"

namespace ssc::runtime
{
    class WorldDebugVisualization final : public IDebugVisualization
    {
    public:
        static WorldDebugVisualization* GetSingleton() noexcept;
        [[nodiscard]] static bool Register();

        [[nodiscard]] bool ShowAnchor(
            const Vec3& a_position,
            const Vec3& a_forward) noexcept override;
        void Update() noexcept override;
        void HideAnchor() noexcept override;
        void ShowVisibility(
            std::shared_ptr<const core::VisibilityEvaluationSnapshot> a_evaluation) noexcept override;
        void HideVisibility() noexcept override;

        void SetOccludedSegmentsVisible(bool a_visible) noexcept;
        [[nodiscard]] bool OccludedSegmentsVisible() const noexcept;
        [[nodiscard]] std::string SelectedCandidateLabel() const;

    private:
        [[nodiscard]] bool CreateMarker(
            const Vec3& a_position,
            const Vec3& a_forward) noexcept;
        [[nodiscard]] bool UpdateMarker(
            const Vec3& a_position,
            const Vec3& a_forward) noexcept;
        void DestroyMarker() noexcept;
        static void __stdcall RenderVisibility();

        RE::NiPointer<RE::BSTempEffectParticle> arrow_;
        bool arrowPrepared_{ false };
        std::atomic<std::shared_ptr<const core::VisibilityEvaluationSnapshot>> evaluation_;
        std::atomic_size_t selectedCandidate_{ 0 };
        std::atomic_bool showOccludedSegments_{ true };
    };
}
