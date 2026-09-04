#pragma once

#include "runtime/PresetPreviewService.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

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

    enum class PresetSceneStatus
    {
        kNotEvaluated,
        kUsable,
        kBlocked,
    };

    struct PresetSceneSummary
    {
        PresetSceneStatus status{ PresetSceneStatus::kNotEvaluated };
        std::size_t visibleParticipants{ 0 };
        std::size_t totalParticipants{ 0 };
        std::size_t visiblePoints{ 0 };
        std::size_t availablePoints{ 0 };
        core::CandidateFailureReason failureReason{ core::CandidateFailureReason::kNone };
    };

    [[nodiscard]] inline PresetSceneSummary SummarizePresetForScene(
        const core::VisibilityEvaluationSnapshot* a_evaluation,
        std::string_view a_presetID) noexcept
    {
        if (!a_evaluation) {
            return {};
        }
        const auto candidate = std::ranges::find(
            a_evaluation->candidates,
            a_presetID,
            &core::CameraCandidateVisibility::presetID);
        if (candidate == a_evaluation->candidates.end()) {
            return {};
        }
        return {
            candidate->usable ? PresetSceneStatus::kUsable : PresetSceneStatus::kBlocked,
            candidate->visibleParticipantCount,
            candidate->participants.size(),
            candidate->visiblePointCount,
            candidate->availablePointCount,
            candidate->failureReason,
        };
    }

    [[nodiscard]] inline std::size_t CountUsablePresets(
        const core::VisibilityEvaluationSnapshot* a_evaluation) noexcept
    {
        return a_evaluation ? static_cast<std::size_t>(std::ranges::count_if(
            a_evaluation->candidates,
            [](const auto& a_candidate) { return a_candidate.usable; })) : 0;
    }

    [[nodiscard]] constexpr bool CanStartPresetPreview(
        const runtime::PresetPreviewFeedback* a_feedback) noexcept
    {
        return a_feedback && a_feedback->sceneActive &&
               (a_feedback->previewApplied || a_feedback->previewPossible);
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
