#include "runtime/PresetPreviewService.h"

namespace ssc::runtime
{
    PresetPreviewService* PresetPreviewService::GetSingleton() noexcept
    {
        static PresetPreviewService singleton;
        return std::addressof(singleton);
    }

    std::uint64_t PresetPreviewService::SetPreview(
        const PresetTransform& a_transform,
        std::string a_presetID)
    {
        const auto revision = NextRevision();
        request_.store(
            std::make_shared<const PresetPreviewRequest>(
                PresetPreviewRequest{ revision, a_transform, std::move(a_presetID) }),
            std::memory_order_release);
        return revision;
    }

    void PresetPreviewService::ClearPreview() noexcept
    {
        auto current = request_.load(std::memory_order_acquire);
        while (current && current->transform) {
            std::shared_ptr<const PresetPreviewRequest> cleared;
            try {
                cleared = std::make_shared<const PresetPreviewRequest>(
                    PresetPreviewRequest{ NextRevision(), std::nullopt, {} });
            } catch (...) {
                std::shared_ptr<const PresetPreviewRequest> empty;
                static_cast<void>(request_.compare_exchange_strong(
                    current,
                    empty,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire));
                return;
            }

            if (request_.compare_exchange_weak(
                    current,
                    std::move(cleared),
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return;
            }
        }
    }

    std::shared_ptr<const PresetPreviewRequest> PresetPreviewService::Request() const noexcept
    {
        return request_.load(std::memory_order_acquire);
    }

    void PresetPreviewService::BeginPreviewSession() noexcept
    {
        previewSessionActive_.store(true, std::memory_order_release);
    }

    void PresetPreviewService::EndPreviewSession() noexcept
    {
        previewSessionActive_.store(false, std::memory_order_release);
    }

    bool PresetPreviewService::PreviewSessionActive() const noexcept
    {
        return previewSessionActive_.load(std::memory_order_acquire);
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

    std::uint64_t PresetPreviewService::NextRevision() noexcept
    {
        return revision_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
}
