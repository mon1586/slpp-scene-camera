#include "ui/PresetEditorMenu.h"

#include "ui/PresetEditorPolicy.h"

#include "runtime/EditHotkeySettings.h"
#include "runtime/PresetPreviewService.h"
#include "runtime/PresetRepository.h"
#include "runtime/PresetFilterEvaluator.h"
#include "SceneCamera.h"
#include "runtime/WorldDebugVisualization.h"

#include <SKSEMenuFramework.h>

#include <array>
#include <cstdio>

namespace ssc::ui
{
    namespace
    {
        constexpr float kMinimumFrameworkVersion = 3.4F;
        enum class PendingAction
        {
            kNone,
            kSelect,
            kNew,
            kNewFromCurrent,
            kReload,
            kClose,
        };

        struct EditorState
        {
            SKSEMenuFramework::Model::WindowInterface* window{ nullptr };
            SKSEMenuFramework::Model::WindowInterface* toolbarWindow{ nullptr };
            SKSEMenuFramework::Model::WindowInterface* mainWindow{ nullptr };
            SKSEMenuFramework::Model::Event* event{ nullptr };
            SKSEMenuFramework::Model::InputEvent* hotkeyInput{ nullptr };
            SKSEMenuFramework::Model::HudElement* toolbarVisibility{ nullptr };
            std::array<char, 128> idBuffer{};
            std::array<char, 128> nameBuffer{};
            std::string savedName;
            std::array<char, 512> animationNameRegex{};
            std::array<char, 512> animationTagRegex{};
            std::string savedNameRegex, savedTagRegex, filterError;
            std::string selectedID;
            std::string pendingID;
            runtime::PresetTransform savedTransform{};
            runtime::PresetTransform draftTransform{};
            std::uint64_t draftRevision{ 0 };
            std::string message;
            PendingAction pendingAction{ PendingAction::kNone };
            std::atomic_bool editHotkeyRequested{ false };
            std::atomic_bool awaitingEditHotkey{ false };
            std::atomic_uint32_t capturedEditHotkey{ 0 };
            std::atomic_uint32_t ownedEditHotkey{ 0 };
            bool creating{ false };
            bool dirty{ false };
            bool deleteConfirmation{ false };
            std::string hotkeyMessage;
        };

        EditorState state;

        void ApplyCapturedEditHotkey();

        void CancelEditHotkeyAssignment() noexcept
        {
            state.awaitingEditHotkey.store(false, std::memory_order_release);
            state.capturedEditHotkey.store(0, std::memory_order_release);
        }

        [[nodiscard]] bool SameTransform(
            const runtime::PresetTransform& a_left,
            const runtime::PresetTransform& a_right) noexcept
        {
            return a_left.framingOffset.right == a_right.framingOffset.right &&
                   a_left.framingOffset.up == a_right.framingOffset.up &&
                   a_left.orbit.yawDegrees == a_right.orbit.yawDegrees &&
                   a_left.orbit.pitchDegrees == a_right.orbit.pitchDegrees &&
                   a_left.orbit.distance == a_right.orbit.distance &&
                   a_left.fovOffsetDegrees == a_right.fovOffsetDegrees;
        }

        void SetIDBuffer(std::string_view a_id)
        {
            state.idBuffer.fill('\0');
            const auto count = std::min(a_id.size(), state.idBuffer.size() - 1);
            std::copy_n(a_id.data(), count, state.idBuffer.data());
        }

        [[nodiscard]] std::string DraftID()
        {
            return state.creating ? std::string{ state.idBuffer.data() } : state.selectedID;
        }

        void SetNameBuffer(std::string_view a_name)
        {
            state.nameBuffer.fill('\0');
            std::copy_n(a_name.data(), std::min(a_name.size(), state.nameBuffer.size() - 1),
                state.nameBuffer.data());
        }

        void SetFilters(std::string_view name, std::string_view tag)
        {
            state.animationNameRegex.fill('\0'); state.animationTagRegex.fill('\0');
            std::copy_n(name.data(), std::min(name.size(), state.animationNameRegex.size() - 1), state.animationNameRegex.data());
            std::copy_n(tag.data(), std::min(tag.size(), state.animationTagRegex.size() - 1), state.animationTagRegex.data());
            state.filterError.clear();
        }

        bool FiltersDirty()
        {
            return state.savedNameRegex != state.animationNameRegex.data() ||
                state.savedTagRegex != state.animationTagRegex.data();
        }

        [[nodiscard]] std::string PresetName(std::string_view a_id)
        {
            const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
            const auto preset = std::ranges::find(*snapshot, a_id, &runtime::CameraPreset::id);
            return preset != snapshot->end() ? preset->name : std::string{ a_id };
        }

        void PublishDraft()
        {
            if (const auto error = runtime::ValidatePresetTransform(state.draftTransform);
                !error.empty()) {
                state.message = error;
                return;
            }
            state.draftRevision = runtime::PresetPreviewService::GetSingleton()->SetPreview(
                state.draftTransform,
                DraftID());
            state.message = "Preview pending";
        }

        void SelectPreset(const runtime::CameraPreset& a_preset, bool a_publishPreview)
        {
            state.selectedID = a_preset.id;
            SetIDBuffer(a_preset.id);
            state.savedName = a_preset.name;
            state.savedNameRegex = a_preset.animationNameRegex;
            state.savedTagRegex = a_preset.animationTagRegex;
            SetFilters(state.savedNameRegex, state.savedTagRegex);
            SetNameBuffer(a_preset.name);
            state.savedTransform = a_preset.transform;
            state.draftTransform = a_preset.transform;
            state.creating = false;
            state.dirty = false;
            state.message.clear();
            if (a_publishPreview) {
                PublishDraft();
            }
        }

        void SelectFirstPreset(bool a_publishPreview = true)
        {
            const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
            if (snapshot && !snapshot->empty()) {
                SelectPreset(snapshot->front(), a_publishPreview);
                return;
            }
            state.selectedID.clear();
            SetIDBuffer({});
            state.savedTransform = {};
            state.draftTransform = {};
            state.creating = false;
            state.dirty = false;
            runtime::PresetPreviewService::GetSingleton()->ClearPreview();
        }

        void SelectPresetByID(std::string_view a_id)
        {
            const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
            if (snapshot) {
                const auto iterator = std::ranges::find(
                    *snapshot,
                    a_id,
                    &runtime::CameraPreset::id);
                if (iterator != snapshot->end()) {
                    SelectPreset(*iterator, true);
                    return;
                }
            }
            SelectFirstPreset();
        }

        [[nodiscard]] std::string NextPresetID()
        {
            const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
            const auto exists = [&](std::string_view a_id) {
                return snapshot && std::ranges::any_of(*snapshot, [&](const auto& a_preset) {
                    return a_preset.id == a_id;
                });
            };
            if (!exists("new-preset"sv)) {
                return "new-preset";
            }
            for (std::uint32_t suffix = 2; suffix < 10000; ++suffix) {
                auto candidate = "new-preset-" + std::to_string(suffix);
                if (!exists(candidate)) {
                    return candidate;
                }
            }
            return "new-preset-copy";
        }

        void BeginNew()
        {
            state.selectedID.clear();
            SetIDBuffer(NextPresetID());
            state.savedName.clear();
            state.savedNameRegex.clear(); state.savedTagRegex.clear(); SetFilters({}, {});
            SetNameBuffer("New preset");
            state.savedTransform = kNewPresetTransform;
            state.draftTransform = kNewPresetTransform;
            state.creating = true;
            state.dirty = true;
            PublishDraft();
        }

        void BeginNewFromCurrent()
        {
            const auto feedback = runtime::PresetPreviewService::GetSingleton()->Feedback();
            if (!feedback || !feedback->currentTransform) {
                state.message = "An active scene camera is required";
                return;
            }
            state.selectedID.clear();
            SetIDBuffer(NextPresetID());
            state.savedName.clear();
            state.savedNameRegex.clear(); state.savedTagRegex.clear(); SetFilters({}, {});
            SetNameBuffer("New preset");
            state.savedTransform = *feedback->currentTransform;
            state.draftTransform = *feedback->currentTransform;
            state.creating = true;
            state.dirty = true;
            PublishDraft();
        }

        [[nodiscard]] bool SaveDraft()
        {
            const auto feedback = runtime::PresetPreviewService::GetSingleton()->Feedback();
            if (!IsLatestPreviewApplied(feedback.get(), state.draftRevision)) {
                state.message = "Wait until the current values are visible in the preview";
                return false;
            }
            const auto id = DraftID();
            const runtime::CameraPreset candidate{ id, state.draftTransform, state.nameBuffer.data(),
                state.animationNameRegex.data(), state.animationTagRegex.data() };
            if (const auto error = runtime::ValidateCameraPreset(candidate); !error.empty()) {
                state.message = error;
                return false;
            }

            const auto result = state.creating ?
                runtime::PresetRepository::GetSingleton()->Create(candidate) :
                runtime::PresetRepository::GetSingleton()->Update(
                    state.selectedID,
                    state.draftTransform,
                    candidate.name, candidate.animationNameRegex, candidate.animationTagRegex);
            if (!result.succeeded) {
                state.message = result.error;
                return false;
            }

            state.selectedID = id;
            state.savedName = candidate.name;
            state.savedNameRegex = candidate.animationNameRegex; state.savedTagRegex = candidate.animationTagRegex;
            state.savedTransform = state.draftTransform;
            state.creating = false;
            state.dirty = false;
            state.message = "Saved";
            logger::info("Camera preset '{}' saved", id);
            return true;
        }

        void CancelDraft(bool a_closing = false)
        {
            if (state.creating) {
                if (a_closing) {
                    state.selectedID.clear();
                    SetIDBuffer({});
                    state.savedTransform = {};
                    state.draftTransform = {};
                    state.draftRevision = 0;
                    state.creating = false;
                    state.dirty = false;
                    return;
                }
                SelectFirstPreset();
                return;
            }
            SetNameBuffer(state.savedName);
            SetFilters(state.savedNameRegex, state.savedTagRegex);
            state.draftTransform = state.savedTransform;
            state.dirty = false;
            state.message = "Changes discarded";
            PublishDraft();
        }

        void ReloadPresets()
        {
            const auto result = runtime::PresetRepository::GetSingleton()->Reload();
            if (!result.succeeded) {
                state.message = result.error;
                return;
            }
            SelectFirstPreset();
            state.message = "Reloaded";
            logger::info("Camera presets reloaded ({} preset(s))", result.presetCount);
        }

        void CloseEditor()
        {
            CancelEditHotkeyAssignment();
            auto* previewService = runtime::PresetPreviewService::GetSingleton();
            previewService->EndPreviewSession();
            previewService->ClearPreview(state.selectedID);
            if (state.window) {
                state.window->IsOpen.store(false, std::memory_order_release);
            }
            state.pendingAction = PendingAction::kNone;
            logger::info("Preset editor closed");
        }

        void RunPendingAction()
        {
            const auto action = std::exchange(state.pendingAction, PendingAction::kNone);
            switch (action) {
            case PendingAction::kSelect:
                SelectPresetByID(state.pendingID);
                break;
            case PendingAction::kNew:
                BeginNew();
                break;
            case PendingAction::kNewFromCurrent:
                BeginNewFromCurrent();
                break;
            case PendingAction::kReload:
                ReloadPresets();
                break;
            case PendingAction::kClose:
                CloseEditor();
                break;
            default:
                break;
            }
            state.pendingID.clear();
        }

        void RequestAction(PendingAction a_action, std::string a_id = {})
        {
            if (!state.dirty) {
                state.pendingAction = a_action;
                state.pendingID = std::move(a_id);
                RunPendingAction();
                return;
            }
            state.pendingAction = a_action;
            state.pendingID = std::move(a_id);
            ImGuiMCP::OpenPopup("Unsaved changes##SSC");
        }

        void RenderUnsavedPopup()
        {
            if (!ImGuiMCP::BeginPopupModal(
                    "Unsaved changes##SSC",
                    nullptr,
                    ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
                return;
            }
            ImGuiMCP::TextWrapped("Save or discard the current changes before continuing.");
            if (ImGuiMCP::Button("Save and continue")) {
                if (SaveDraft()) {
                    RunPendingAction();
                    ImGuiMCP::CloseCurrentPopup();
                }
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Discard and continue")) {
                CancelDraft(state.pendingAction == PendingAction::kClose);
                RunPendingAction();
                ImGuiMCP::CloseCurrentPopup();
            }
            if (ImGuiMCP::Button("Keep editing")) {
                state.pendingAction = PendingAction::kNone;
                state.pendingID.clear();
                ImGuiMCP::CloseCurrentPopup();
            }
            ImGuiMCP::EndPopup();
        }

        void RenderDeletePopup()
        {
            if (state.deleteConfirmation) {
                ImGuiMCP::OpenPopup("Delete preset##SSC");
                state.deleteConfirmation = false;
            }
            if (!ImGuiMCP::BeginPopupModal(
                    "Delete preset##SSC",
                    nullptr,
                    ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
                return;
            }
            ImGuiMCP::Text("Delete preset '%s'?", state.savedName.c_str());
            if (ImGuiMCP::Button("Delete")) {
                const auto result = runtime::PresetRepository::GetSingleton()->Delete(state.selectedID);
                if (result.succeeded) {
                    const auto deletedID = state.selectedID;
                    SelectFirstPreset();
                    state.message = "Deleted";
                    logger::info("Camera preset '{}' deleted", deletedID);
                } else {
                    state.message = result.error;
                }
                ImGuiMCP::CloseCurrentPopup();
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Cancel")) {
                ImGuiMCP::CloseCurrentPopup();
            }
            ImGuiMCP::EndPopup();
        }

        void OpenEditor(std::string_view a_requestedID = {})
        {
            if (!state.window) {
                return;
            }
            auto* previewService = runtime::PresetPreviewService::GetSingleton();
            const auto feedback = previewService->Feedback();
            const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
            const auto noPresets = !snapshot || snapshot->empty();
            if (!CanStartPresetPreview(feedback.get())) {
                return;
            }

            CancelEditHotkeyAssignment();
            previewService->BeginPreviewSession();
            if (noPresets) {
                if (state.creating) {
                    PublishDraft();
                } else {
                    BeginNew();
                }
            } else {
                const auto requestedID = a_requestedID.empty() ?
                    std::string_view{ state.selectedID } : a_requestedID;
                const auto selected = std::ranges::find(
                    *snapshot,
                    requestedID,
                    &runtime::CameraPreset::id);
                if (selected == snapshot->end()) {
                    SelectFirstPreset();
                } else {
                    SelectPreset(*selected, true);
                }
            }
            state.window->IsOpen.store(true, std::memory_order_release);
            if (state.mainWindow) {
                state.mainWindow->IsOpen.store(false, std::memory_order_release);
            }
            logger::info("Preset editor opened");
        }

        void __stdcall UpdateSceneToolbarVisibility()
        {
            try {
                ApplyCapturedEditHotkey();
                if (!state.toolbarWindow) {
                    return;
                }
                auto* previewService = runtime::PresetPreviewService::GetSingleton();
                const auto feedback = previewService->Feedback();
                const auto blockingWindowOpen = SKSEMenuFramework::IsAnyBlockingWindowOpened();
                const auto showToolbar = ShouldShowSceneToolbar(
                    feedback.get(),
                    previewService->PreviewSessionActive(),
                    blockingWindowOpen);
                state.toolbarWindow->IsOpen.store(
                    showToolbar,
                    std::memory_order_release);

                const auto editorOpen = state.window &&
                    state.window->IsOpen.load(std::memory_order_acquire);
                if (!editorOpen &&
                    state.editHotkeyRequested.exchange(false, std::memory_order_acq_rel)) {
                    if (const auto currentPresetID = CurrentPresetID(feedback.get());
                        !runtime::WorldDebugVisualization::GetSingleton()->Enabled() &&
                        CanOpenCurrentPresetEditor(feedback.get(), blockingWindowOpen) &&
                        currentPresetID) {
                        OpenEditor(*currentPresetID);
                    }
                }
            } catch (...) {
                logger::error("SKSE Menu Framework scene toolbar visibility update failed");
            }
        }

        void __stdcall RenderSceneToolbar()
        {
            try {
                auto* previewService = runtime::PresetPreviewService::GetSingleton();
                const auto feedback = previewService->Feedback();
                if (!ShouldShowSceneToolbar(
                        feedback.get(),
                        previewService->PreviewSessionActive(),
                        SKSEMenuFramework::IsAnyBlockingWindowOpened())) {
                    return;
                }

                const auto* viewport = ImGuiMCP::GetMainViewport();
                if (!viewport) {
                    return;
                }
                ImGuiMCP::SetNextWindowPos(
                    { viewport->WorkPos.x + viewport->WorkSize.x * 0.5F,
                        viewport->WorkPos.y + 20.0F },
                    ImGuiMCP::ImGuiCond_Always,
                    { 0.5F, 0.0F });
                ImGuiMCP::SetNextWindowBgAlpha(0.78F);
                constexpr auto flags = ImGuiMCP::ImGuiWindowFlags_NoDecoration |
                    ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiMCP::ImGuiWindowFlags_NoMove |
                    ImGuiMCP::ImGuiWindowFlags_NoSavedSettings |
                    ImGuiMCP::ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiMCP::ImGuiWindowFlags_NoInputs;
                if (!ImGuiMCP::Begin("Scene Camera Preset##SSC-toolbar", nullptr, flags)) {
                    ImGuiMCP::End();
                    return;
                }

                if (runtime::WorldDebugVisualization::GetSingleton()->Enabled()) {
                    const auto label =
                        runtime::WorldDebugVisualization::GetSingleton()->SelectedCandidateLabel();
                    ImGuiMCP::Text("Debug preset: %s", label.c_str());
                    ImGuiMCP::TextDisabled("[A/D] Inspect    SmoothCam in control");
                } else if (const auto currentPresetID = CurrentPresetID(feedback.get())) {
                    ImGuiMCP::Text("Preset: %s", PresetName(*currentPresetID).c_str());
                    const auto keyName = runtime::EditHotkeyName(
                        runtime::EditHotkeySettings::GetSingleton()->EditHotkey());
                    ImGuiMCP::TextDisabled(
                        "[A/D] Change    Press [%s] to edit this preset",
                        keyName.c_str());
                } else {
                    ImGuiMCP::TextDisabled("No active camera preset");
                }
                ImGuiMCP::End();
            } catch (const std::exception& exception) {
                logger::error("SKSE Menu Framework scene toolbar failed: {}", exception.what());
            } catch (...) {
                logger::error("SKSE Menu Framework scene toolbar failed");
            }
        }

        bool __stdcall OnEditHotkeyInput(RE::InputEvent* a_event)
        {
            try {
                if (!a_event || a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton ||
                    a_event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                    return false;
                }
                const auto* button = a_event->AsButtonEvent();
                if (!button) {
                    return false;
                }

                const auto keyCode = button->GetIDCode();
                EditHotkeyButtonPhase phase;
                if (button->IsDown()) {
                    phase = EditHotkeyButtonPhase::kDown;
                } else if (button->IsHeld()) {
                    phase = EditHotkeyButtonPhase::kHeld;
                } else if (button->IsUp()) {
                    phase = EditHotkeyButtonPhase::kUp;
                } else {
                    return false;
                }

                const auto editorOpen = state.window &&
                    state.window->IsOpen.load(std::memory_order_acquire);
                const auto feedback = runtime::PresetPreviewService::GetSingleton()->Feedback();
                const auto* settings = runtime::EditHotkeySettings::GetSingleton();
                const auto editHotkey = settings->EditHotkey();
                const auto canToggle = (editorOpen ||
                    !runtime::WorldDebugVisualization::GetSingleton()->Enabled()) &&
                    ShouldHandleEditHotkey(
                    keyCode,
                    editHotkey,
                    editorOpen,
                    !editorOpen && SKSEMenuFramework::IsAnyBlockingWindowOpened(),
                    feedback.get());
                const auto decision = DecideEditHotkeyInput(
                    phase,
                    keyCode,
                    editHotkey,
                    runtime::kEscapeKeyboardKey,
                    state.ownedEditHotkey.load(std::memory_order_acquire),
                    state.awaitingEditHotkey.load(std::memory_order_acquire),
                    canToggle);
                state.ownedEditHotkey.store(decision.ownedKey, std::memory_order_release);
                if (decision.finishAssignment) {
                    state.awaitingEditHotkey.store(false, std::memory_order_release);
                    state.capturedEditHotkey.store(
                        decision.capturedKey,
                        std::memory_order_release);
                }
                if (decision.toggleEditor) {
                    state.editHotkeyRequested.store(true, std::memory_order_release);
                }
                return decision.consume;
            } catch (...) {
                try {
                    logger::error("Preset edit hotkey input failed");
                } catch (...) {
                }
                return false;
            }
        }

        void ApplyCapturedEditHotkey()
        {
            const auto captured =
                state.capturedEditHotkey.exchange(0, std::memory_order_acq_rel);
            if (captured == 0) {
                return;
            }
            state.awaitingEditHotkey.store(false, std::memory_order_release);
            const auto result =
                runtime::EditHotkeySettings::GetSingleton()->SetEditHotkey(captured);
            if (result.succeeded) {
                const auto keyName = runtime::EditHotkeyName(captured);
                state.hotkeyMessage = "Edit hotkey changed to " + keyName;
                logger::info("Preset edit hotkey changed to {} ({:#04x})", keyName, captured);
            } else {
                state.hotkeyMessage = result.error;
            }
        }

        void __stdcall RenderSection()
        {
            try {
                ApplyCapturedEditHotkey();
                ImGuiMCP::TextWrapped(
                    "Stored presets and their visibility in the current scene. Selecting a row does not move the camera.");
                const auto feedback = runtime::PresetPreviewService::GetSingleton()->Feedback();
                const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
                const auto noPresets = !snapshot || snapshot->empty();

                if (feedback) {
                    ImGuiMCP::Text("Camera: %s", feedback->message.c_str());
                }
                const auto* evaluation = feedback && feedback->visibilityEvaluation ?
                    feedback->visibilityEvaluation.get() : nullptr;
                const auto presetCount = snapshot ? snapshot->size() : 0;
                const auto usableCount = CountUsablePresets(evaluation);
                if (feedback && feedback->sceneActive) {
                    ImGuiMCP::Text("Usable presets: %zu / %zu", usableCount, presetCount);
                } else {
                    ImGuiMCP::TextDisabled("Current scene: not evaluated");
                }

                if (snapshot && !snapshot->empty()) {
                    const auto selectedStillExists = std::ranges::any_of(
                        *snapshot,
                        [](const auto& a_preset) { return a_preset.id == state.selectedID; });
                    if (!selectedStillExists) {
                        auto usablePreset = snapshot->end();
                        if (evaluation) {
                            const auto usableCandidate = std::ranges::find_if(
                                evaluation->candidates,
                                [](const auto& a_candidate) { return a_candidate.usable; });
                            if (usableCandidate != evaluation->candidates.end()) {
                                usablePreset = std::ranges::find(
                                    *snapshot,
                                    usableCandidate->presetID,
                                    &runtime::CameraPreset::id);
                            }
                        }
                        if (usablePreset != snapshot->end()) {
                            SelectPreset(*usablePreset, false);
                        } else {
                            SelectFirstPreset(false);
                        }
                    }
                    ImGuiMCP::Separator();
                    for (const auto& preset : *snapshot) {
                        const auto summary = SummarizePresetForScene(evaluation, preset.id);
                        ImGuiMCP::ImVec4 color{ 0.68F, 0.68F, 0.68F, 1.0F };
                        std::string status = "[--] not evaluated";
                        if (summary.status == PresetSceneStatus::kUsable) {
                            color = { 0.30F, 0.90F, 0.38F, 1.0F };
                            status = fmt::format(
                                "[OK] center {}, corners {}/4",
                                core::VisibilityPointStatusName(summary.centerStatus),
                                summary.visibleCorners);
                        } else if (summary.status == PresetSceneStatus::kBlocked) {
                            color = { 0.95F, 0.32F, 0.30F, 1.0F };
                            status = fmt::format(
                                "[BLOCKED] center {}, corners {}/4: {}",
                                core::VisibilityPointStatusName(summary.centerStatus),
                                summary.visibleCorners,
                                core::CandidateFailureReasonName(summary.failureReason));
                        }
                        const auto label = fmt::format(
                            "{} - {}###dashboard-{}",
                            preset.name,
                            status,
                            preset.id);
                        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, color);
                        const auto selected = preset.id == state.selectedID;
                        if (ImGuiMCP::Selectable(label.c_str(), selected) && !selected) {
                            SelectPreset(preset, false);
                        }
                        ImGuiMCP::PopStyleColor();
                    }
                } else {
                    ImGuiMCP::Separator();
                    ImGuiMCP::TextDisabled("No camera presets have been created.");
                }

                ImGuiMCP::Separator();
                const auto awaitingHotkey =
                    state.awaitingEditHotkey.load(std::memory_order_acquire);
                auto* settings = runtime::EditHotkeySettings::GetSingleton();
                auto debugMode = settings->DebugMode();
                const auto debugModeActive = debugMode &&
                    runtime::WorldDebugVisualization::GetSingleton()->Available();
                const auto canPreview = !debugModeActive &&
                    CanStartDashboardPreview(feedback.get(), awaitingHotkey);
                ImGuiMCP::BeginDisabled(!canPreview);
                if (ImGuiMCP::Button(
                        noPresets ? "Create & preview preset" : "Preview & edit selected")) {
                    OpenEditor();
                }
                ImGuiMCP::EndDisabled();
                if (awaitingHotkey) {
                    ImGuiMCP::TextDisabled(
                        "Finish or cancel the edit hotkey change first.");
                } else if (!canPreview) {
                    ImGuiMCP::TextDisabled("Preview unavailable: %s",
                        feedback ? feedback->message.c_str() : "camera state unavailable");
                }
                ImGuiMCP::Separator();
                const auto editHotkey = settings->EditHotkey();
                const auto editHotkeyName = runtime::EditHotkeyName(editHotkey);
                ImGuiMCP::Text("Edit hotkey: %s", editHotkeyName.c_str());
                if (ImGuiMCP::Button(awaitingHotkey ?
                        "Cancel hotkey change" : "Change edit hotkey")) {
                    state.awaitingEditHotkey.store(!awaitingHotkey, std::memory_order_release);
                    state.capturedEditHotkey.store(0, std::memory_order_release);
                    state.hotkeyMessage.clear();
                }
                if (state.awaitingEditHotkey.load(std::memory_order_acquire)) {
                    ImGuiMCP::TextDisabled("Press a keyboard key. Escape cancels.");
                }
                if (!state.hotkeyMessage.empty()) {
                    ImGuiMCP::TextWrapped("%s", state.hotkeyMessage.c_str());
                }
                ImGuiMCP::Separator();
                auto* debug = runtime::WorldDebugVisualization::GetSingleton();
                const auto debugAvailable = debug->Available();
                ImGuiMCP::BeginDisabled(!debugAvailable);
                const auto debugChanged = ImGuiMCP::Checkbox("Debug mode", &debugMode);
                ImGuiMCP::EndDisabled();
                if (debugChanged) {
                    const auto result = settings->SetDebugMode(debugMode);
                    if (result.succeeded) {
                        spdlog::set_level(debugMode ?
                            spdlog::level::debug : spdlog::level::info);
                        state.hotkeyMessage = debugMode ?
                            "Debug mode enabled; SmoothCam remains in control." :
                            "Debug mode disabled; scene camera restored.";
                    } else {
                        debugMode = settings->DebugMode();
                        state.hotkeyMessage = result.error;
                    }
                }
                if (!debugAvailable) {
                    ImGuiMCP::TextDisabled("Debug overlay is unavailable.");
                }
                if (debugMode && debugAvailable) {
                    const auto label = debug->SelectedCandidateLabel();
                    ImGuiMCP::Text("Debug preset: %s", label.c_str());
                    ImGuiMCP::TextDisabled("Use A / D to cycle through currently usable presets.");
                    auto showOccludedSegments = debug->OccludedSegmentsVisible();
                    if (ImGuiMCP::Checkbox(
                            "Show occluded segments", &showOccludedSegments)) {
                        debug->SetOccludedSegmentsVisible(showOccludedSegments);
                    }
                }
            } catch (...) {
                logger::error("SKSE Menu Framework preset page failed");
            }
        }

        void __stdcall RenderEditor()
        {
            try {
                // A blocking framework window owns input and pause state. The camera
                // update hook explicitly permits preview updates while this session is open.
                ImGuiMCP::SetNextWindowSize(
                    { 620.0F, 420.0F },
                    ImGuiMCP::ImGuiCond_FirstUseEver);
                if (!ImGuiMCP::Begin("Scene Camera Preset Editor##SSC")) {
                    ImGuiMCP::End();
                    return;
                }

                if (state.editHotkeyRequested.exchange(false, std::memory_order_acq_rel)) {
                    RequestAction(PendingAction::kClose);
                    if (!state.window->IsOpen.load(std::memory_order_acquire)) {
                        ImGuiMCP::End();
                        return;
                    }
                }
                const auto editHotkeyName = runtime::EditHotkeyName(
                    runtime::EditHotkeySettings::GetSingleton()->EditHotkey());
                ImGuiMCP::TextDisabled("Press [%s] to close", editHotkeyName.c_str());

                const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
                auto* previewService = runtime::PresetPreviewService::GetSingleton();
                const auto feedback = previewService->Feedback();
                const auto canEdit = CanEditPreset(
                    feedback.get(), previewService->PreviewSessionActive());
                if (!canEdit) {
                    ImGuiMCP::TextWrapped(
                        "Editing is locked until the scene camera can apply a live preview.");
                }

                ImGuiMCP::BeginDisabled(!canEdit);
                const auto previewLabel = state.creating ? "<new preset>" :
                    (state.selectedID.empty() ? "<none>" : state.savedName.c_str());
                if (ImGuiMCP::BeginCombo("Preset", previewLabel)) {
                    if (snapshot) {
                        for (const auto& preset : *snapshot) {
                            const auto selected = !state.creating && preset.id == state.selectedID;
                            const auto label = fmt::format("{}###preset-{}", preset.name, preset.id);
                            if (ImGuiMCP::Selectable(label.c_str(), selected) && !selected) {
                                RequestAction(PendingAction::kSelect, preset.id);
                            }
                        }
                    }
                    ImGuiMCP::EndCombo();
                }

                if (!snapshot || snapshot->empty()) {
                    ImGuiMCP::TextDisabled(
                        "No presets. Create one from the standard position or an active camera.");
                }

                const auto canCapture = feedback && feedback->currentTransform.has_value();
                if (ImGuiMCP::Button("New preset")) {
                    RequestAction(PendingAction::kNew);
                }
                ImGuiMCP::SameLine();
                ImGuiMCP::BeginDisabled(!canCapture);
                if (ImGuiMCP::Button("New from current camera")) {
                    RequestAction(PendingAction::kNewFromCurrent);
                }
                ImGuiMCP::EndDisabled();
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Reload")) {
                    RequestAction(PendingAction::kReload);
                }

                ImGuiMCP::Separator();
                ImGuiMCP::TextDisabled("ID: %s", DraftID().c_str());
                if (ImGuiMCP::InputText("Name", state.nameBuffer.data(), state.nameBuffer.size())) {
                    state.dirty = state.creating || FiltersDirty() || std::string_view{ state.nameBuffer.data() } != state.savedName ||
                        !SameTransform(state.draftTransform, state.savedTransform);
                    PublishDraft();
                }

                ImGuiMCP::Separator();
                ImGuiMCP::Text("Animation filters");
                ImGuiMCP::TextWrapped("Choose when this preset is eligible for initial selection and A/D switching.");
                bool filterChanged = ImGuiMCP::InputText("Name filter (regex)",
                    state.animationNameRegex.data(), state.animationNameRegex.size());
                filterChanged |= ImGuiMCP::InputText("Tag filter (regex)",
                    state.animationTagRegex.data(), state.animationTagRegex.size());
                if (filterChanged) {
                    const runtime::PresetFilterEvaluator filter{
                        state.animationNameRegex.data(), state.animationTagRegex.data() };
                    state.filterError = filter.Error();
                    state.dirty = state.creating || FiltersDirty() ||
                        std::string_view{ state.nameBuffer.data() } != state.savedName ||
                        !SameTransform(state.draftTransform, state.savedTransform);
                }
                ImGuiMCP::TextWrapped("Leave blank for no restriction. Both filters must match. Tag filter searches the whole space-separated tag list. Case-insensitive.");
                if (!state.filterError.empty()) {
                    ImGuiMCP::TextWrapped("Regex error: %s", state.filterError.c_str());
                }
                const auto animation = SceneCamera::GetSingleton()->AnimationUpdates().Published();
                if (!animation) {
                    ImGuiMCP::TextDisabled("Animation: awaiting current metadata / no active scene");
                } else if (!animation->known) {
                    ImGuiMCP::TextDisabled("Animation: metadata unavailable; only unrestricted presets are eligible");
                } else {
                    ImGuiMCP::TextWrapped("Animation: %s", animation->name.c_str());
                    std::string tags;
                    for (const auto& tag : animation->tags) { if (!tags.empty()) { tags += ", "; } tags += tag; }
                    ImGuiMCP::TextWrapped("Tags: %s", tags.c_str());
                }
                ImGuiMCP::Separator();
                ImGuiMCP::TextDisabled("Screen-relative framing");
                bool transformChanged = false;
                transformChanged |= ImGuiMCP::DragFloat(
                    "Pan Right", &state.draftTransform.framingOffset.right, 1.0F);
                transformChanged |= ImGuiMCP::DragFloat(
                    "Pan Up", &state.draftTransform.framingOffset.up, 1.0F);

                ImGuiMCP::TextDisabled("Orbit around the framing center");
                transformChanged |= ImGuiMCP::DragFloat(
                    "Yaw", &state.draftTransform.orbit.yawDegrees,
                    0.25F, kMinimumYawDegrees, kMaximumYawDegrees, "%.1f deg",
                    ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
                transformChanged |= ImGuiMCP::DragFloat(
                    "Pitch", &state.draftTransform.orbit.pitchDegrees,
                    0.25F, kMinimumPitchDegrees, kMaximumPitchDegrees, "%.1f deg",
                    ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
                transformChanged |= ImGuiMCP::DragFloat(
                    "Distance", &state.draftTransform.orbit.distance,
                    1.0F, kMinimumDistance, kMaximumDistance, "%.1f",
                    ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
                ImGuiMCP::TextDisabled("Relative to the normal third-person FOV");
                transformChanged |= ImGuiMCP::DragFloat(
                    "FOV Offset", &state.draftTransform.fovOffsetDegrees,
                    0.25F, kMinimumFOVOffsetDegrees, kMaximumFOVOffsetDegrees, "%.1f deg",
                    ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
                if (transformChanged) {
                    state.dirty = state.creating || FiltersDirty() || std::string_view{ state.nameBuffer.data() } != state.savedName ||
                        !SameTransform(state.draftTransform, state.savedTransform);
                    PublishDraft();
                }

                ImGuiMCP::Text("State: %s", state.dirty ? "unsaved" : "saved");
                if (feedback) {
                    ImGuiMCP::TextDisabled("Camera: %s", feedback->message.c_str());
                }
                const auto summary = SummarizeEditorPreview(
                    feedback.get(), state.draftRevision, DraftID());
                if (summary.status != PresetSceneStatus::kNotEvaluated) {
                    if (summary.status == PresetSceneStatus::kUsable) {
                        ImGuiMCP::TextColored(
                            { 0.30F, 0.90F, 0.38F, 1.0F },
                            "Visibility: usable (center %s, corners %zu/4)",
                            core::VisibilityPointStatusName(summary.centerStatus).data(),
                            summary.visibleCorners);
                    } else {
                        ImGuiMCP::TextColored(
                            { 0.95F, 0.32F, 0.30F, 1.0F },
                            "Visibility: blocked (center %s, corners %zu/4: %s)",
                            core::VisibilityPointStatusName(summary.centerStatus).data(),
                            summary.visibleCorners,
                            core::CandidateFailureReasonName(summary.failureReason).data());
                    }
                } else {
                    ImGuiMCP::TextDisabled("Visibility: awaiting current preview evaluation");
                }
                if (!state.message.empty()) {
                    ImGuiMCP::TextWrapped("%s", state.message.c_str());
                }

                const auto hasDraft = state.creating || !state.selectedID.empty();
                ImGuiMCP::BeginDisabled(!state.filterError.empty() || !CanSavePreset(
                    hasDraft, state.dirty, feedback.get(), state.draftRevision));
                if (ImGuiMCP::Button("Save")) {
                    static_cast<void>(SaveDraft());
                }
                ImGuiMCP::EndDisabled();
                ImGuiMCP::SameLine();
                ImGuiMCP::BeginDisabled(!hasDraft || !state.dirty);
                if (ImGuiMCP::Button("Cancel changes")) {
                    CancelDraft();
                }
                ImGuiMCP::EndDisabled();
                ImGuiMCP::SameLine();
                ImGuiMCP::BeginDisabled(state.creating || state.selectedID.empty());
                if (ImGuiMCP::Button("Delete")) {
                    state.deleteConfirmation = true;
                }
                ImGuiMCP::EndDisabled();
                ImGuiMCP::EndDisabled();
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Close")) {
                    if (canEdit) {
                        RequestAction(PendingAction::kClose);
                    } else {
                        CloseEditor();
                    }
                }

                if (canEdit) {
                    RenderUnsavedPopup();
                    RenderDeletePopup();
                } else {
                    state.pendingAction = PendingAction::kNone;
                    state.pendingID.clear();
                    state.deleteConfirmation = false;
                }
                ImGuiMCP::End();
            } catch (const std::exception& exception) {
                logger::error("SKSE Menu Framework preset editor failed: {}", exception.what());
            } catch (...) {
                logger::error("SKSE Menu Framework preset editor failed");
            }
        }

        void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_type)
        {
            if (a_type == SKSEMenuFramework::Model::EventType::kCloseMenu) {
                if (state.window) {
                    state.window->IsOpen.store(false, std::memory_order_release);
                }
                auto* previewService = runtime::PresetPreviewService::GetSingleton();
                previewService->EndPreviewSession();
                previewService->ClearPreview(state.selectedID);
                CancelEditHotkeyAssignment();
            }
        }
    }

    bool PresetEditorMenu::Register()
    {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::info("SKSE Menu Framework not installed; preset editor disabled");
            return false;
        }
        const auto version = SKSEMenuFramework::GetMenuFrameworkVersion();
        if (version < kMinimumFrameworkVersion) {
            logger::warn(
                "SKSE Menu Framework {:.2f} is older than required {:.2f}; preset editor disabled",
                version,
                kMinimumFrameworkVersion);
            return false;
        }

        SKSEMenuFramework::SetSection("Sexlab Scene Camera");
        SKSEMenuFramework::AddSectionItem("Camera Presets", RenderSection);
        state.mainWindow = SKSEMenuFramework::GetMainWindow();
        state.window = SKSEMenuFramework::AddWindow(RenderEditor, kEditorPausesGame);
        state.toolbarWindow = SKSEMenuFramework::AddWindow(RenderSceneToolbar, false);
        state.toolbarVisibility = SKSEMenuFramework::AddHudElement(UpdateSceneToolbarVisibility);
        state.hotkeyInput = SKSEMenuFramework::AddInputEvent(OnEditHotkeyInput);
        state.event = SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0F);
        if (!state.mainWindow || !state.window || !state.toolbarWindow ||
            !state.toolbarVisibility || !state.hotkeyInput || !state.event) {
            logger::error("SKSE Menu Framework preset editor registration failed");
            return false;
        }
        state.toolbarWindow->IsOpen.store(false, std::memory_order_release);
        logger::info("SKSE Menu Framework preset editor registered (version {:.2f})", version);
        return true;
    }

    void PresetEditorMenu::CloseForLifecycle() noexcept
    {
        if (state.window) {
            state.window->IsOpen.store(false, std::memory_order_release);
        }
        if (state.toolbarWindow) {
            state.toolbarWindow->IsOpen.store(false, std::memory_order_release);
        }
        CancelEditHotkeyAssignment();
        state.ownedEditHotkey.store(0, std::memory_order_release);
        state.editHotkeyRequested.store(false, std::memory_order_release);
        auto* previewService = runtime::PresetPreviewService::GetSingleton();
        previewService->EndPreviewSession();
        previewService->ClearPreview();
    }
}
