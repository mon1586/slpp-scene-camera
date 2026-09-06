#pragma once

#include "core/VisibilityEvaluation.h"
#include "runtime/IPresetProvider.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>

namespace ssc::runtime
{
    struct PresetPreviewRequest
    {
        std::uint64_t revision{ 0 };
        std::optional<PresetTransform> transform;
        std::string presetID;
    };

    struct PresetPreviewFeedback
    {
        bool sceneActive{ false };
        bool previewApplied{ false };
        bool previewPossible{ false };
        std::uint64_t appliedRevision{ 0 };
        std::optional<PresetTransform> currentTransform;
        std::string message{ "No active player scene" };
        std::shared_ptr<const core::VisibilityEvaluationSnapshot> visibilityEvaluation;
    };

    class PresetPreviewService
    {
    public:
        static PresetPreviewService* GetSingleton() noexcept;

        [[nodiscard]] std::uint64_t SetPreview(
            const PresetTransform& a_transform,
            std::string a_presetID = {});
        void ClearPreview(std::string a_resumePresetID = {}) noexcept;
        [[nodiscard]] std::shared_ptr<const PresetPreviewRequest> Request() const noexcept;

        void BeginPreviewSession() noexcept;
        void EndPreviewSession() noexcept;
        void InvalidatePreviewSession() noexcept;
        [[nodiscard]] bool PreviewSessionActive() const noexcept;

        void PublishFeedback(PresetPreviewFeedback a_feedback) noexcept;
        [[nodiscard]] std::shared_ptr<const PresetPreviewFeedback> Feedback() const noexcept;

    private:
        [[nodiscard]] std::uint64_t NextRevision() noexcept;

        std::atomic<std::shared_ptr<const PresetPreviewRequest>> request_{ nullptr };
        std::atomic_uint64_t revision_{ 0 };
        std::atomic_bool previewSessionActive_{ false };
        std::atomic<std::shared_ptr<const PresetPreviewFeedback>> feedback_{
            std::make_shared<const PresetPreviewFeedback>() };
    };
}
