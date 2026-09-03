#include "ui/PresetEditorMenu.h"

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
        constexpr runtime::PresetOffset kDefaultPresetOffset{
            0.0F,
            -200.0F,
            60.0F,
        };

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
            runtime::PresetOffset savedOffset{};
            runtime::PresetOffset draftOffset{};
            std::string message;
            PendingAction pendingAction{ PendingAction::kNone };
            bool creating{ false };
            bool dirty{ false };
            bool deleteConfirmation{ false };
        };

        EditorState state;

        [[nodiscard]] bool SameOffset(
            const runtime::PresetOffset& a_left,
            const runtime::PresetOffset& a_right) noexcept
        {
            return a_left.right == a_right.right &&
                   a_left.forward == a_right.forward &&
                   a_left.up == a_right.up;
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
            runtime::CameraPreset candidate{ DraftID(), state.draftOffset };
            if (const auto error = runtime::ValidateCameraPreset(candidate); !error.empty()) {
                state.message = error;
                return;
            }
            runtime::PresetPreviewService::GetSingleton()->SetPreview(candidate);
            state.message = "Preview requested";
        }

        void SelectPreset(const runtime::CameraPreset& a_preset)
        {
            state.selectedID = a_preset.id;
            SetIDBuffer(a_preset.id);
            state.savedOffset = a_preset.offset;
            state.draftOffset = a_preset.offset;
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
            state.savedOffset = {};
            state.draftOffset = {};
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
            state.savedOffset = kDefaultPresetOffset;
            state.draftOffset = kDefaultPresetOffset;
            state.creating = true;
            state.dirty = true;
            PublishDraft();
        }

        void BeginNewFromCurrent()
        {
            const auto feedback = runtime::PresetPreviewService::GetSingleton()->Feedback();
            if (!feedback || !feedback->currentOffset) {
                state.message = "An active scene camera is required";
                return;
            }
            state.selectedID.clear();
            SetIDBuffer(NextPresetID());
            state.savedOffset = *feedback->currentOffset;
            state.draftOffset = *feedback->currentOffset;
            state.creating = true;
            state.dirty = true;
            PublishDraft();
        }

        [[nodiscard]] bool SaveDraft()
        {
            const auto id = DraftID();
            const runtime::CameraPreset candidate{ id, state.draftOffset };
            if (const auto error = runtime::ValidateCameraPreset(candidate); !error.empty()) {
                state.message = error;
                return false;
            }

            const auto result = state.creating ?
                runtime::PresetRepository::GetSingleton()->Create(candidate) :
                runtime::PresetRepository::GetSingleton()->Update(
                    state.selectedID,
                    state.draftOffset);
            if (!result.succeeded) {
                state.message = result.error;
                return false;
            }

            state.selectedID = id;
            state.savedOffset = state.draftOffset;
            state.creating = false;
            state.dirty = false;
            state.message = "Saved";
            PublishDraft();
            logger::info("Camera preset '{}' saved", id);
            return true;
        }

        void CancelDraft()
        {
            if (state.creating) {
                SelectFirstPreset();
                return;
            }
            state.draftOffset = state.savedOffset;
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
            runtime::PresetPreviewService::GetSingleton()->ClearPreview();
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
            if (state.selectedID.empty() && !state.creating) {
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

        void KeepGameRunningForLivePreview() noexcept
        {
            if (auto* main = RE::Main::GetSingleton()) {
                main->GetRuntimeData().freezeTime = false;
            }
        }

        void __stdcall RenderSection()
        {
            try {
                ImGuiMCP::TextWrapped(
                    "Create and edit scene-relative camera presets. Clearance is not evaluated yet.");
                if (ImGuiMCP::Button("Open preset editor")) {
                    OpenEditor();
                }
                const auto feedback = runtime::PresetPreviewService::GetSingleton()->Feedback();
                if (feedback) {
                    ImGuiMCP::TextDisabled("Camera: %s", feedback->message.c_str());
                }
            } catch (...) {
                logger::error("SKSE Menu Framework preset page failed");
            }
        }

        void __stdcall RenderEditor()
        {
            try {
                // SKSE Menu Framework only feeds input to blocking windows. Keep this
                // window blocking for input capture, but undo its time freeze so the
                // camera update path can consume live-preview requests.
                KeepGameRunningForLivePreview();
                ImGuiMCP::SetNextWindowSize(
                    { 620.0F, 420.0F },
                    ImGuiMCP::ImGuiCond_FirstUseEver);
                if (!ImGuiMCP::Begin("Scene Camera Preset Editor##SSC")) {
                    ImGuiMCP::End();
                    return;
                }

                const auto snapshot = runtime::PresetRepository::GetSingleton()->Snapshot();
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

                const auto feedback = runtime::PresetPreviewService::GetSingleton()->Feedback();
                const auto canCapture = feedback && feedback->currentOffset.has_value();
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
                    PublishDraft();
                }
                ImGuiMCP::EndDisabled();

                bool offsetChanged = false;
                offsetChanged |= ImGuiMCP::DragFloat("Right", &state.draftOffset.right, 1.0F);
                offsetChanged |= ImGuiMCP::DragFloat("Forward", &state.draftOffset.forward, 1.0F);
                offsetChanged |= ImGuiMCP::DragFloat("Up", &state.draftOffset.up, 1.0F);
                if (offsetChanged) {
                    state.dirty = state.creating || !SameOffset(state.draftOffset, state.savedOffset);
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
                ImGuiMCP::BeginDisabled(!hasDraft || !state.dirty);
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
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Close")) {
                    RequestAction(PendingAction::kClose);
                }

                RenderUnsavedPopup();
                RenderDeletePopup();
                ImGuiMCP::End();
            } catch (const std::exception& exception) {
                logger::error("SKSE Menu Framework preset editor failed: {}", exception.what());
            } catch (...) {
                logger::error("SKSE Menu Framework preset editor failed");
            }
        }

        void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_type)
        {
            if (a_type == SKSEMenuFramework::Model::EventType::kCloseMenu &&
                (!state.window || !state.window->IsOpen.load(std::memory_order_acquire))) {
                runtime::PresetPreviewService::GetSingleton()->ClearPreview();
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
        state.window = SKSEMenuFramework::AddWindow(RenderEditor, true);
        state.event = SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0F);
        if (!state.mainWindow || !state.window || !state.event) {
            logger::error("SKSE Menu Framework preset editor registration failed");
            return false;
        }
        logger::info("SKSE Menu Framework preset editor registered (version {:.2f})", version);
        return true;
    }
}
