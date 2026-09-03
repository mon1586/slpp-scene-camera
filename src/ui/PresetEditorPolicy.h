#pragma once

#include "runtime/PresetPreviewService.h"

#include <cstdint>

namespace ssc::ui
{
    inline constexpr bool kEditorPausesGame = true;

    inline constexpr runtime::PresetTransform kNewPresetTransform{
        { 0.0F, 60.0F },
        { 0.0F, 0.0F, 200.0F },
    };

    inline constexpr float kMinimumYawDegrees = -180.0F;
    inline constexpr float kMaximumYawDegrees = 180.0F;
    inline constexpr float kMinimumPitchDegrees = -89.9F;
    inline constexpr float kMaximumPitchDegrees = 89.9F;
    inline constexpr float kMinimumDistance = 0.1F;
    inline constexpr float kMaximumDistance = 100000.0F;

    [[nodiscard]] constexpr bool CanOpenPresetEditor(
        const runtime::PresetPreviewFeedback* a_feedback,
        bool a_noPresets) noexcept
    {
        return a_feedback && (a_feedback->previewApplied ||
            (a_noPresets && a_feedback->sceneActive && a_feedback->previewPossible));
    }

    [[nodiscard]] constexpr bool CanEditPreset(
        const runtime::PresetPreviewFeedback* a_feedback,
        bool a_previewSessionActive) noexcept
    {
        return a_previewSessionActive && a_feedback && a_feedback->previewApplied;
    }

    [[nodiscard]] constexpr bool IsLatestPreviewApplied(
        const runtime::PresetPreviewFeedback* a_feedback,
        std::uint64_t a_draftRevision) noexcept
    {
        return a_draftRevision != 0 && a_feedback && a_feedback->previewApplied &&
               a_feedback->appliedRevision == a_draftRevision;
    }

    [[nodiscard]] constexpr bool CanSavePreset(
        bool a_hasDraft,
        bool a_dirty,
        const runtime::PresetPreviewFeedback* a_feedback,
        std::uint64_t a_draftRevision) noexcept
    {
        return a_hasDraft && a_dirty &&
               IsLatestPreviewApplied(a_feedback, a_draftRevision);
    }
}
