#include "runtime/PresetPreviewService.h"

namespace ssc::runtime
{
    PresetPreviewService* PresetPreviewService::GetSingleton() noexcept
    {
        static PresetPreviewService singleton;
        return std::addressof(singleton);
    }

    void PresetPreviewService::SetPreview(const CameraPreset& a_preset)
    {
        request_.store(
            std::make_shared<const PresetPreviewRequest>(PresetPreviewRequest{ a_preset }),
            std::memory_order_release);
    }

    void PresetPreviewService::ClearPreview() noexcept
    {
        request_.store(nullptr, std::memory_order_release);
    }

    std::shared_ptr<const PresetPreviewRequest> PresetPreviewService::Request() const noexcept
    {
        return request_.load(std::memory_order_acquire);
    }

    void PresetPreviewService::PublishFeedback(PresetPreviewFeedback a_feedback) noexcept
    {
        try {
            feedback_.store(
                std::make_shared<const PresetPreviewFeedback>(std::move(a_feedback)),
                std::memory_order_release);
        } catch (...) {
        }
    }

    std::shared_ptr<const PresetPreviewFeedback> PresetPreviewService::Feedback() const noexcept
    {
        return feedback_.load(std::memory_order_acquire);
    }
}
