#pragma once

#include "runtime/PresetPreviewService.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ssc::ui
{
    inline constexpr bool kEditorPausesGame = true;

    inline constexpr runtime::PresetTransform kNewPresetTransform{
        { 0.0F, 60.0F },
        { 0.0F, 0.0F, 200.0F },
        0.0F,
    };

    inline constexpr float kMinimumYawDegrees = -180.0F;
    inline constexpr float kMaximumYawDegrees = 180.0F;
    inline constexpr float kMinimumPitchDegrees = -89.9F;
    inline constexpr float kMaximumPitchDegrees = 89.9F;
    inline constexpr float kMinimumDistance = 0.1F;
    inline constexpr float kMaximumDistance = 100000.0F;
    inline constexpr float kMinimumFOVOffsetDegrees = -160.0F;
    inline constexpr float kMaximumFOVOffsetDegrees = 160.0F;

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

    [[nodiscard]] inline std::optional<std::string_view> CurrentPresetID(
        const runtime::PresetPreviewFeedback* a_feedback) noexcept
    {
        if (!a_feedback || !a_feedback->visibilityEvaluation ||
            !a_feedback->visibilityEvaluation->selectedPresetID) {
            return std::nullopt;
        }
        return *a_feedback->visibilityEvaluation->selectedPresetID;
    }

    [[nodiscard]] constexpr bool ShouldShowSceneToolbar(
        const runtime::PresetPreviewFeedback* a_feedback,
        bool a_previewSessionActive,
        bool a_blockingWindowOpen) noexcept
    {
        return a_feedback && a_feedback->sceneActive &&
               !a_previewSessionActive && !a_blockingWindowOpen;
    }

    [[nodiscard]] inline bool CanEditCurrentPreset(
        const runtime::PresetPreviewFeedback* a_feedback) noexcept
    {
        return CanStartPresetPreview(a_feedback) && CurrentPresetID(a_feedback).has_value();
    }

    [[nodiscard]] constexpr bool CanStartDashboardPreview(
        const runtime::PresetPreviewFeedback* a_feedback,
        bool a_awaitingHotkeyAssignment) noexcept
    {
        return !a_awaitingHotkeyAssignment && CanStartPresetPreview(a_feedback);
    }

    [[nodiscard]] inline bool CanOpenCurrentPresetEditor(
        const runtime::PresetPreviewFeedback* a_feedback,
        bool a_blockingWindowOpen) noexcept
    {
        return !a_blockingWindowOpen && CanEditCurrentPreset(a_feedback);
    }

    [[nodiscard]] inline bool ShouldHandleEditHotkey(
        std::uint32_t a_pressedKey,
        std::uint32_t a_editHotkey,
        bool a_editorOpen,
        bool a_blockingWindowOpen,
        const runtime::PresetPreviewFeedback* a_feedback) noexcept
    {
        return a_pressedKey == a_editHotkey &&
               (a_editorOpen ||
                   CanOpenCurrentPresetEditor(a_feedback, a_blockingWindowOpen));
    }

    enum class EditHotkeyButtonPhase
    {
        kDown,
        kHeld,
        kUp,
    };

    struct EditHotkeyInputDecision
    {
        bool consume{ false };
        bool toggleEditor{ false };
        bool finishAssignment{ false };
        bool cancelAssignment{ false };
        std::uint32_t capturedKey{ 0 };
        std::uint32_t ownedKey{ 0 };
    };

    [[nodiscard]] constexpr EditHotkeyInputDecision DecideEditHotkeyInput(
        EditHotkeyButtonPhase a_phase,
        std::uint32_t a_keyCode,
        std::uint32_t a_editHotkey,
        std::uint32_t a_escapeKey,
        std::uint32_t a_ownedKey,
        bool a_awaitingAssignment,
        bool a_canToggleEditor) noexcept
    {
        if (a_ownedKey == a_keyCode) {
            if (a_phase == EditHotkeyButtonPhase::kUp) {
                return { true, false, false, false, 0, 0 };
            }
            if (a_phase == EditHotkeyButtonPhase::kHeld) {
                return { true, false, false, false, 0, a_ownedKey };
            }
        } else if (a_phase != EditHotkeyButtonPhase::kDown) {
            return {};
        }

        if (a_phase != EditHotkeyButtonPhase::kDown) {
            return {};
        }
        if (a_awaitingAssignment) {
            const auto cancelled = a_keyCode == a_escapeKey;
            return {
                true,
                false,
                true,
                cancelled,
                cancelled ? 0U : a_keyCode,
                a_keyCode,
            };
        }
        if (a_keyCode == a_editHotkey && a_canToggleEditor) {
            return { true, true, false, false, 0, a_keyCode };
        }
        return {};
    }
}
