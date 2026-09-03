#pragma once

#include "runtime/IPresetProvider.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>

namespace ssc::runtime
{
    struct PresetPreviewRequest
    {
        std::optional<CameraPreset> preset;
    };

    struct PresetPreviewFeedback
    {
        bool sceneActive{ false };
        bool previewApplied{ false };
        std::optional<PresetOffset> currentOffset;
        std::string message{ "No active player scene" };
    };

    class PresetPreviewService
    {
    public:
        static PresetPreviewService* GetSingleton() noexcept;

        void SetPreview(const CameraPreset& a_preset);
        void ClearPreview() noexcept;
        [[nodiscard]] std::shared_ptr<const PresetPreviewRequest> Request() const noexcept;

        void PublishFeedback(PresetPreviewFeedback a_feedback) noexcept;
        [[nodiscard]] std::shared_ptr<const PresetPreviewFeedback> Feedback() const noexcept;

    private:
        std::atomic<std::shared_ptr<const PresetPreviewRequest>> request_{ nullptr };
        std::atomic<std::shared_ptr<const PresetPreviewFeedback>> feedback_{
            std::make_shared<const PresetPreviewFeedback>() };
    };
}
