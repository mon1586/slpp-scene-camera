#include "ui/PresetEditorMenu.h"

#include "ui/PresetEditorPolicy.h"

#include "runtime/PresetPreviewService.h"
#include "runtime/PresetRepository.h"

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
            SKSEMenuFramework::Model::WindowInterface* mainWindow{ nullptr };
            SKSEMenuFramework::Model::Event* event{ nullptr };
            std::array<char, 128> idBuffer{};
            std::string selectedID;
            std::string pendingID;
            runtime::PresetTransform savedTransform{};
            runtime::PresetTransform draftTransform{};
            std::uint64_t draftRevision{ 0 };
            std::string message;
            PendingAction pendingAction{ PendingAction::kNone };
            bool creating{ false };
            bool dirty{ false };
            bool deleteConfirmation{ false };
        };

        EditorState state;

        [[nodiscard]] bool SameTransform(
            const runtime::PresetTransform& a_left,
            const runtime::PresetTransform& a_right) noexcept
        {
            return a_left.framingOffset.right == a_right.framingOffset.right &&
                   a_left.framingOffset.up == a_right.framingOffset.up &&
                   a_left.orbit.yawDegrees == a_right.orbit.yawDegrees &&
                   a_left.orbit.pitchDegrees == a_right.orbit.pitchDegrees &&
                   a_left.orbit.distance == a_right.orbit.distance;
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

        void PublishDraft()
        {
            if (const auto error = runtime::ValidatePresetTransform(state.draftTransform);
                !error.empty()) {
                state.message = error;
                return;
            }
            state.draftRevision = runtime::PresetPreviewService::GetSingleton()->SetPreview(
                state.draftTransform);
            state.message = "Preview pending";
        }

        void SelectPreset(const runtime::CameraPreset& a_preset)
        {
            state.selectedID = a_preset.id;
            SetIDBuffer(a_preset.id);
            state.savedTransform = a_preset.transform;
            state.draftTransform = a_preset.transform;
            state.creating = false;
            state.dirty = false;
            state.message.clear();
            PublishDraft();
        }

        void SelectFirstPreset()
        {
            const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
            if (snapshot && !snapshot->empty()) {
                SelectPreset(snapshot->front());
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
                    SelectPreset(*iterator);
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
            const runtime::CameraPreset candidate{ id, state.draftTransform };
            if (const auto error = runtime::ValidateCameraPreset(candidate); !error.empty()) {
                state.message = error;
                return false;
            }

            const auto result = state.creating ?
                runtime::PresetRepository::GetSingleton()->Create(candidate) :
                runtime::PresetRepository::GetSingleton()->Update(
                    state.selectedID,
                    state.draftTransform);
            if (!result.succeeded) {
                state.message = result.error;
                return false;
            }

            state.selectedID = id;
            state.savedTransform = state.draftTransform;
            state.creating = false;
            state.dirty = false;
            state.message = "Saved";
            logger::info("Camera preset '{}' saved", id);
            return true;
        }

        void CancelDraft()
        {
            if (state.creating) {
                SelectFirstPreset();
                return;
            }
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
            auto* previewService = runtime::PresetPreviewService::GetSingleton();
            previewService->EndPreviewSession();
            previewService->ClearPreview();
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
                CancelDraft();
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
            ImGuiMCP::Text("Delete preset '%s'?", state.selectedID.c_str());
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

        void OpenEditor()
        {
            if (!state.window) {
                return;
            }
            auto* previewService = runtime::PresetPreviewService::GetSingleton();
            const auto feedback = previewService->Feedback();
            const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
            const auto noPresets = !snapshot || snapshot->empty();
            const auto canRecoverEmpty = noPresets && feedback && feedback->sceneActive &&
                feedback->previewPossible && !feedback->previewApplied;
            if (!CanOpenPresetEditor(feedback.get(), noPresets)) {
                return;
            }

            previewService->BeginPreviewSession();
            if (canRecoverEmpty) {
                if (state.creating) {
                    PublishDraft();
                } else {
                    BeginNew();
                }
            } else if (state.selectedID.empty() && !state.creating) {
                SelectFirstPreset();
            } else {
                PublishDraft();
            }
            state.window->IsOpen.store(true, std::memory_order_release);
            if (state.mainWindow) {
                state.mainWindow->IsOpen.store(false, std::memory_order_release);
            }
            logger::info("Preset editor opened");
        }

        void __stdcall RenderSection()
        {
            try {
                ImGuiMCP::TextWrapped(
                    "Create and edit scene-relative camera presets. Clearance is not evaluated yet.");
                const auto feedback = runtime::PresetPreviewService::GetSingleton()->Feedback();
                const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
                const auto noPresets = !snapshot || snapshot->empty();
                const auto canOpen = CanOpenPresetEditor(feedback.get(), noPresets);
                ImGuiMCP::BeginDisabled(!canOpen);
                if (ImGuiMCP::Button("Open preset editor")) {
                    OpenEditor();
                }
                ImGuiMCP::EndDisabled();
                if (feedback) {
                    ImGuiMCP::TextDisabled("Camera: %s", feedback->message.c_str());
                }
                if (!canOpen) {
                    ImGuiMCP::TextDisabled(
                        "The editor requires an active scene camera with preview control.");
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
                    (state.selectedID.empty() ? "<none>" : state.selectedID.c_str());
                if (ImGuiMCP::BeginCombo("Preset", previewLabel)) {
                    if (snapshot) {
                        for (const auto& preset : *snapshot) {
                            const auto selected = !state.creating && preset.id == state.selectedID;
                            if (ImGuiMCP::Selectable(preset.id.c_str(), selected) && !selected) {
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
                ImGuiMCP::BeginDisabled(!state.creating);
                if (ImGuiMCP::InputText("ID", state.idBuffer.data(), state.idBuffer.size())) {
                    state.dirty = true;
                }
                ImGuiMCP::EndDisabled();

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
                if (transformChanged) {
                    state.dirty = state.creating ||
                        !SameTransform(state.draftTransform, state.savedTransform);
                    PublishDraft();
                }

                ImGuiMCP::Text("State: %s", state.dirty ? "unsaved" : "saved");
                if (feedback) {
                    ImGuiMCP::TextDisabled("Camera: %s", feedback->message.c_str());
                }
                if (!state.message.empty()) {
                    ImGuiMCP::TextWrapped("%s", state.message.c_str());
                }

                const auto hasDraft = state.creating || !state.selectedID.empty();
                ImGuiMCP::BeginDisabled(!CanSavePreset(
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
                previewService->ClearPreview();
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
        state.event = SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0F);
        if (!state.mainWindow || !state.window || !state.event) {
            logger::error("SKSE Menu Framework preset editor registration failed");
            return false;
        }
        logger::info("SKSE Menu Framework preset editor registered (version {:.2f})", version);
        return true;
    }

    void PresetEditorMenu::CloseForLifecycle() noexcept
    {
        if (state.window) {
            state.window->IsOpen.store(false, std::memory_order_release);
        }
        auto* previewService = runtime::PresetPreviewService::GetSingleton();
        previewService->EndPreviewSession();
        previewService->ClearPreview();
    }
}
