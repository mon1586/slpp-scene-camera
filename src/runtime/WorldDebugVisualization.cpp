#include "runtime/WorldDebugVisualization.h"

#if defined(SSC_ENABLE_VISIBILITY_DEBUG)
#include <SKSEMenuFramework.h>
#endif

namespace ssc::runtime
{
    namespace
    {
        constexpr auto kArrowModel = "marker_arrow.nif"sv;
        constexpr float kArrowLength = 32.0F;
        constexpr float kEffectLifetime = 24.0F * 60.0F * 60.0F;
        constexpr float kMinimumModelDiameter = 0.001F;

#if defined(SSC_ENABLE_VISIBILITY_DEBUG)
        constexpr float kMinimumMenuFrameworkVersion = 3.4F;
        constexpr float kProjectionTolerance = 9.99999975e-06F;
        constexpr float kLineThickness = 2.0F;
        constexpr float kPointRadius = 5.0F;
        constexpr float kNormalLength = 24.0F;

        [[nodiscard]] constexpr ImGuiMCP::ImU32 Color(
            std::uint8_t a_red,
            std::uint8_t a_green,
            std::uint8_t a_blue,
            std::uint8_t a_alpha = 255) noexcept
        {
            return static_cast<ImGuiMCP::ImU32>(a_red) |
                   (static_cast<ImGuiMCP::ImU32>(a_green) << 8) |
                   (static_cast<ImGuiMCP::ImU32>(a_blue) << 16) |
                   (static_cast<ImGuiMCP::ImU32>(a_alpha) << 24);
        }

        [[nodiscard]] RE::NiCamera* FindNiCamera(RE::NiAVObject* a_object) noexcept
        {
            if (!a_object) {
                return nullptr;
            }
            if (auto* camera = netimmerse_cast<RE::NiCamera*>(a_object)) {
                return camera;
            }
            auto* node = a_object->AsNode();
            if (!node) {
                return nullptr;
            }
            for (auto& child : node->GetChildren()) {
                if (auto* camera = FindNiCamera(child.get())) {
                    return camera;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<ImGuiMCP::ImVec2> Project(
            RE::NiCamera& a_camera,
            const core::Vec3& a_point) noexcept
        {
            float x = 0.0F;
            float y = 0.0F;
            float depth = 0.0F;
            if (!a_camera.WorldPtToScreenPt3(
                    { a_point.x, a_point.y, a_point.z },
                    x,
                    y,
                    depth,
                    kProjectionTolerance)) {
                return std::nullopt;
            }
            const auto screen = RE::BSGraphics::Renderer::GetScreenSize();
            if (screen.width == 0 || screen.height == 0) {
                return std::nullopt;
            }
            return ImGuiMCP::ImVec2{
                x * static_cast<float>(screen.width),
                (1.0F - y) * static_cast<float>(screen.height),
            };
        }

        void DrawLine(
            ImGuiMCP::ImDrawList* a_drawList,
            RE::NiCamera& a_camera,
            const core::Vec3& a_start,
            const core::Vec3& a_end,
            ImGuiMCP::ImU32 a_color) noexcept
        {
            const auto start = Project(a_camera, a_start);
            const auto end = Project(a_camera, a_end);
            if (start && end) {
                ImGuiMCP::ImDrawListManager::AddLine(
                    a_drawList, *start, *end, a_color, kLineThickness);
            }
        }

        void DrawPointMarker(
            ImGuiMCP::ImDrawList* a_drawList,
            const ImGuiMCP::ImVec2& a_position,
            core::VisibilityPoint a_point,
            ImGuiMCP::ImU32 a_color) noexcept
        {
            const ImGuiMCP::ImVec2 top{ a_position.x, a_position.y - kPointRadius };
            const ImGuiMCP::ImVec2 bottomLeft{
                a_position.x - kPointRadius, a_position.y + kPointRadius };
            const ImGuiMCP::ImVec2 bottomRight{
                a_position.x + kPointRadius, a_position.y + kPointRadius };
            switch (a_point) {
            case core::VisibilityPoint::kFace:
                ImGuiMCP::ImDrawListManager::AddCircle(
                    a_drawList, a_position, kPointRadius, a_color, 12, kLineThickness);
                break;
            case core::VisibilityPoint::kChest:
                ImGuiMCP::ImDrawListManager::AddRect(
                    a_drawList,
                    { a_position.x - kPointRadius, a_position.y - kPointRadius },
                    { a_position.x + kPointRadius, a_position.y + kPointRadius },
                    a_color,
                    0.0F,
                    ImGuiMCP::ImDrawFlags_None,
                    kLineThickness);
                break;
            case core::VisibilityPoint::kWaist:
                ImGuiMCP::ImDrawListManager::AddTriangle(
                    a_drawList, top, bottomRight, bottomLeft, a_color, kLineThickness);
                break;
            }
        }

        void DrawHitMarker(
            ImGuiMCP::ImDrawList* a_drawList,
            const ImGuiMCP::ImVec2& a_position) noexcept
        {
            constexpr auto red = Color(235, 65, 65);
            ImGuiMCP::ImDrawListManager::AddLine(
                a_drawList,
                { a_position.x - kPointRadius, a_position.y - kPointRadius },
                { a_position.x + kPointRadius, a_position.y + kPointRadius },
                red,
                kLineThickness);
            ImGuiMCP::ImDrawListManager::AddLine(
                a_drawList,
                { a_position.x - kPointRadius, a_position.y + kPointRadius },
                { a_position.x + kPointRadius, a_position.y - kPointRadius },
                red,
                kLineThickness);
        }
#endif

        [[nodiscard]] float ScaleModelToDiameter(
            std::string_view a_model,
            float a_targetDiameter,
            float a_fallbackScale) noexcept
        {
            try {
                RE::NiPointer<RE::NiNode> model;
                const RE::BSModelDB::DBTraits::ArgsType loadArguments;
                const auto result = RE::BSModelDB::Demand(
                    a_model.data(), model, loadArguments);
                if (result != RE::BSResource::ErrorCode::kNone || !model) {
                    logger::warn("Cannot inspect debug marker model '{}' (error {})",
                        a_model,
                        std::to_underlying(result));
                    return a_fallbackScale;
                }

                RE::NiUpdateData updateData{
                    RE::Main::QFrameAnimTime(),
                    RE::NiUpdateData::Flag::kDirty,
                };
                model->Update(updateData);
                const auto modelDiameter = model->worldBound.radius * 2.0F;
                if (!std::isfinite(modelDiameter) || modelDiameter < kMinimumModelDiameter) {
                    logger::warn("Debug marker model '{}' has no usable bound", a_model);
                    return a_fallbackScale;
                }

                return a_targetDiameter / modelDiameter;
            } catch (...) {
                return a_fallbackScale;
            }
        }

        void ExpireEffect(RE::NiPointer<RE::BSTempEffectParticle>& a_effect) noexcept
        {
            if (!a_effect) {
                return;
            }

            try {
                a_effect->lifetime = 0.0F;
                a_effect->age = 0.0F;
                a_effect->Detach();
            } catch (...) {
            }
            a_effect.reset();
        }

        [[nodiscard]] bool PrepareLoadedEffect(RE::BSTempEffectParticle* a_effect) noexcept
        {
            try {
                if (!a_effect || !a_effect->particleObject) {
                    return false;
                }
                a_effect->particleObject->CullNode(false);
                a_effect->particleObject->SetCollisionLayer(RE::COL_LAYER::kNonCollidable);
                return true;
            } catch (...) {
                return false;
            }
        }
    }

    WorldDebugVisualization* WorldDebugVisualization::GetSingleton() noexcept
    {
        static WorldDebugVisualization singleton;
        return std::addressof(singleton);
    }

    bool WorldDebugVisualization::Register()
    {
#if defined(SSC_ENABLE_VISIBILITY_DEBUG)
        const auto version = SKSEMenuFramework::GetMenuFrameworkVersion();
        if (version < kMinimumMenuFrameworkVersion) {
            logger::warn("Visibility debug overlay disabled: SKSE Menu Framework is unavailable");
            return false;
        }
        const auto registerHudElement = GetMenuFrameworkFunction<
            SKSEMenuFramework::Model::RegisterHudElementFuction>("RegisterHudElement");
        if (!registerHudElement) {
            logger::error("Visibility debug overlay disabled: HUD registration API is unavailable");
            return false;
        }
        static auto* hudElement = SKSEMenuFramework::AddHudElement(RenderVisibility);
        if (!hudElement) {
            logger::error("Visibility debug overlay registration failed");
            return false;
        }
        logger::info("Visibility debug overlay registered");
#endif
        return true;
    }

    bool WorldDebugVisualization::CreateMarker(
        const Vec3& a_position,
        const Vec3& a_forward) noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        try {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            if (!cell) {
                logger::warn("Cannot create debug anchor marker: player cell is unavailable");
                return false;
            }

            const RE::NiPoint3 position{ a_position.x, a_position.y, a_position.z };
            RE::NiMatrix3 arrowRotation;
            arrowRotation.MakeZRotation(std::atan2(a_forward.x, a_forward.y));

            const auto arrowScale = ScaleModelToDiameter(kArrowModel, kArrowLength, 0.25F);

            arrow_ = RE::NiPointer<RE::BSTempEffectParticle>{ cell->PlaceParticleEffect(
                kEffectLifetime,
                kArrowModel.data(),
                arrowRotation,
                position,
                arrowScale,
                0,
                nullptr) };

            if (!arrow_) {
                logger::warn("Cannot create debug anchor arrow effect");
                DestroyMarker();
                return false;
            }

            arrowPrepared_ = PrepareLoadedEffect(arrow_.get());
            logger::info(
                "Debug anchor marker created at ({:.2f}, {:.2f}, {:.2f}), forward ({:.3f}, {:.3f}, {:.3f})",
                a_position.x,
                a_position.y,
                a_position.z,
                a_forward.x,
                a_forward.y,
                a_forward.z);
            return true;
        } catch (const std::exception& exception) {
            try {
                logger::warn("Cannot create debug anchor marker: {}", exception.what());
            } catch (...) {
            }
            DestroyMarker();
            return false;
        } catch (...) {
            DestroyMarker();
            return false;
        }
#else
        static_cast<void>(a_position);
        static_cast<void>(a_forward);
        return true;
#endif
    }

    bool WorldDebugVisualization::UpdateMarker(
        const Vec3& a_position,
        const Vec3& a_forward) noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        try {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            if (!arrow_ || !cell || arrow_->cell != cell) {
                return false;
            }

            RE::NiMatrix3 arrowRotation;
            arrowRotation.MakeZRotation(std::atan2(a_forward.x, a_forward.y));
            arrow_->particleEffectTransform.translate = {
                a_position.x, a_position.y, a_position.z };
            arrow_->particleEffectTransform.rotate = arrowRotation;

            if (arrow_->particleObject) {
                arrow_->particleObject->local = arrow_->particleEffectTransform;
                RE::NiUpdateData updateData{
                    RE::Main::QFrameAnimTime(),
                    RE::NiUpdateData::Flag::kDirty,
                };
                arrow_->particleObject->Update(updateData);
            }
            arrowPrepared_ = PrepareLoadedEffect(arrow_.get());

            logger::info(
                "Debug anchor marker moved to ({:.2f}, {:.2f}, {:.2f}), forward ({:.3f}, {:.3f}, {:.3f})",
                a_position.x,
                a_position.y,
                a_position.z,
                a_forward.x,
                a_forward.y,
                a_forward.z);
            return true;
        } catch (const std::exception& exception) {
            try {
                logger::warn("Cannot update debug anchor marker: {}", exception.what());
            } catch (...) {
            }
            return false;
        } catch (...) {
            return false;
        }
#else
        static_cast<void>(a_position);
        static_cast<void>(a_forward);
        return true;
#endif
    }

    bool WorldDebugVisualization::ShowAnchor(
        const Vec3& a_position,
        const Vec3& a_forward) noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        if (UpdateMarker(a_position, a_forward)) {
            return true;
        }
        DestroyMarker();
        return CreateMarker(a_position, a_forward);
#else
        static_cast<void>(a_position);
        static_cast<void>(a_forward);
        return true;
#endif
    }

    void WorldDebugVisualization::Update() noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        if (arrow_ && !arrowPrepared_) {
            arrowPrepared_ = PrepareLoadedEffect(arrow_.get());
        }
#endif
    }

    void WorldDebugVisualization::HideAnchor() noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        DestroyMarker();
#endif
    }

    void WorldDebugVisualization::ShowVisibility(
        std::shared_ptr<const core::VisibilityEvaluationSnapshot> a_evaluation) noexcept
    {
        evaluation_.store(std::move(a_evaluation), std::memory_order_release);
        const auto snapshot = evaluation_.load(std::memory_order_acquire);
        const auto count = snapshot ? snapshot->candidates.size() : 0;
        if (count == 0) {
            selectedCandidate_.store(0, std::memory_order_release);
            return;
        }
        const auto selected = std::ranges::find_if(
            snapshot->candidates,
            [](const auto& a_candidate) { return a_candidate.usable; });
        if (selected != snapshot->candidates.end()) {
            selectedCandidate_.store(
                static_cast<std::size_t>(
                    std::distance(snapshot->candidates.begin(), selected)),
                std::memory_order_release);
        } else {
            selectedCandidate_.store(0, std::memory_order_release);
        }
    }

    void WorldDebugVisualization::HideVisibility() noexcept
    {
        evaluation_.store(nullptr, std::memory_order_release);
        selectedCandidate_.store(0, std::memory_order_release);
    }

    void WorldDebugVisualization::SetOccludedSegmentsVisible(bool a_visible) noexcept
    {
        showOccludedSegments_.store(a_visible, std::memory_order_release);
    }

    bool WorldDebugVisualization::OccludedSegmentsVisible() const noexcept
    {
        return showOccludedSegments_.load(std::memory_order_acquire);
    }

    std::string WorldDebugVisualization::SelectedCandidateLabel() const
    {
        const auto snapshot = evaluation_.load(std::memory_order_acquire);
        if (!snapshot || snapshot->candidates.empty()) {
            return "No visibility evaluation";
        }
        const auto selected = std::min(
            selectedCandidate_.load(std::memory_order_acquire),
            snapshot->candidates.size() - 1);
        const auto& candidate = snapshot->candidates[selected];
        return fmt::format(
            "{} ({}/{}, {}/{} points, {})",
            candidate.presetID,
            selected + 1,
            snapshot->candidates.size(),
            candidate.visiblePointCount,
            candidate.availablePointCount,
            core::CandidateFailureReasonName(candidate.failureReason));
    }

    void __stdcall WorldDebugVisualization::RenderVisibility()
    {
#if defined(SSC_ENABLE_VISIBILITY_DEBUG)
        auto* self = GetSingleton();
        const auto snapshot = self->evaluation_.load(std::memory_order_acquire);
        if (!snapshot || snapshot->candidates.empty()) {
            return;
        }

        auto* playerCamera = RE::PlayerCamera::GetSingleton();
        auto* niCamera = playerCamera ? FindNiCamera(playerCamera->cameraRoot.get()) : nullptr;
        auto* drawList = ImGuiMCP::GetBackgroundDrawList();
        if (!niCamera || !drawList) {
            return;
        }

        const auto selected = std::min(
            self->selectedCandidate_.load(std::memory_order_acquire),
            snapshot->candidates.size() - 1);
        const auto& candidate = snapshot->candidates[selected];
        const auto showOccludedSegments =
            self->showOccludedSegments_.load(std::memory_order_acquire);
        constexpr auto green = Color(70, 220, 90, 230);
        constexpr auto red = Color(235, 65, 65, 230);
        constexpr auto gray = Color(155, 155, 155, 200);
        constexpr auto yellow = Color(245, 210, 65, 230);

        for (const auto& point : candidate.points) {
            if (point.status == core::VisibilityPointStatus::kUnavailable) {
                continue;
            }

            const auto targetScreen = Project(*niCamera, point.target);
            if (point.status == core::VisibilityPointStatus::kVisible) {
                DrawLine(drawList, *niCamera, point.rayStart, point.target, green);
                if (targetScreen) {
                    DrawPointMarker(drawList, *targetScreen, point.point, green);
                }
                continue;
            }

            if (!point.hitPosition) {
                continue;
            }
            DrawLine(drawList, *niCamera, point.rayStart, *point.hitPosition, red);
            if (const auto hitScreen = Project(*niCamera, *point.hitPosition)) {
                DrawHitMarker(drawList, *hitScreen);
            }
            if (showOccludedSegments) {
                DrawLine(drawList, *niCamera, *point.hitPosition, point.target, gray);
                if (targetScreen) {
                    DrawPointMarker(drawList, *targetScreen, point.point, gray);
                }
            }
            if (point.hitNormal) {
                const core::Vec3 normalEnd{
                    point.hitPosition->x + point.hitNormal->x * kNormalLength,
                    point.hitPosition->y + point.hitNormal->y * kNormalLength,
                    point.hitPosition->z + point.hitNormal->z * kNormalLength,
                };
                DrawLine(drawList, *niCamera, *point.hitPosition, normalEnd, yellow);
            }
        }
#endif
    }

    void WorldDebugVisualization::DestroyMarker() noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        const auto hadMarker = static_cast<bool>(arrow_);
        ExpireEffect(arrow_);
        arrowPrepared_ = false;
        if (hadMarker) {
            try {
                logger::info("Debug anchor marker removed");
            } catch (...) {
            }
        }
#endif
    }
}
