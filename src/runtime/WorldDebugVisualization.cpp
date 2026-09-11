#include "runtime/WorldDebugVisualization.h"

#include "runtime/EditHotkeySettings.h"

#include <SKSEMenuFramework.h>

namespace ssc::runtime
{
    namespace
    {
        constexpr float kMinimumMenuFrameworkVersion = 3.4F;
        constexpr float kProjectionTolerance = 9.99999975e-06F;
        constexpr float kLineThickness = 2.0F;
        constexpr float kPointRadius = 5.0F;
        constexpr float kCandidateRadius = 7.0F;
        constexpr float kAnchorLength = 32.0F;
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
            switch (a_point) {
            case core::VisibilityPoint::kBodyCenter:
            case core::VisibilityPoint::kAnchor:
            case core::VisibilityPoint::kAnchorTopLeft:
            case core::VisibilityPoint::kAnchorTopRight:
            case core::VisibilityPoint::kAnchorBottomLeft:
            case core::VisibilityPoint::kAnchorBottomRight:
                ImGuiMCP::ImDrawListManager::AddCircle(
                    a_drawList, a_position, kPointRadius, a_color, 12, kLineThickness);
                break;
            }
            if (a_point != core::VisibilityPoint::kBodyCenter &&
                a_point != core::VisibilityPoint::kAnchor) {
                ImGuiMCP::ImDrawListManager::AddText(
                    a_drawList, { a_position.x + 7.0F, a_position.y - 14.0F }, a_color,
                    core::VisibilityPointName(a_point).data());
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

        [[nodiscard]] core::Vec3 ToCore(const Vec3& a_value) noexcept
        {
            return { a_value.x, a_value.y, a_value.z };
        }
    }

    WorldDebugVisualization* WorldDebugVisualization::GetSingleton() noexcept
    {
        static WorldDebugVisualization singleton;
        return std::addressof(singleton);
    }

    bool WorldDebugVisualization::Register()
    {
        auto* self = GetSingleton();
        self->registered_.store(false, std::memory_order_release);
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::info("SKSE Menu Framework not installed; debug overlay disabled");
            return false;
        }
        const auto version = SKSEMenuFramework::GetMenuFrameworkVersion();
        if (version < kMinimumMenuFrameworkVersion) {
            logger::warn("Debug overlay disabled: SKSE Menu Framework is unavailable");
            return false;
        }
        const auto registerHudElement = GetMenuFrameworkFunction<
            SKSEMenuFramework::Model::RegisterHudElementFuction>("RegisterHudElement");
        if (!registerHudElement) {
            logger::error("Debug overlay disabled: HUD registration API is unavailable");
            return false;
        }
        static auto* hudElement = SKSEMenuFramework::AddHudElement(RenderVisibility);
        if (!hudElement) {
            logger::error("Debug overlay registration failed");
            return false;
        }
        self->registered_.store(true, std::memory_order_release);
        logger::info("Debug overlay registered");
        return true;
    }

    bool WorldDebugVisualization::Available() const noexcept
    {
        return registered_.load(std::memory_order_acquire);
    }

    bool WorldDebugVisualization::ShowAnchor(
        const Vec3& a_position,
        const Vec3& a_forward,
        const Vec3& a_targetPosition) noexcept
    {
        if (!Enabled()) {
            return true;
        }
        std::scoped_lock lock{ anchorMutex_ };
        anchor_ = AnchorDisplay{ a_position, a_forward, a_targetPosition };
        return true;
    }

    void WorldDebugVisualization::Update() noexcept
    {}

    void WorldDebugVisualization::HideAnchor() noexcept
    {
        std::scoped_lock lock{ anchorMutex_ };
        anchor_.reset();
    }

    void WorldDebugVisualization::ShowVisibility(
        std::shared_ptr<const core::VisibilityEvaluationSnapshot> a_evaluation) noexcept
    {
        const auto previous = evaluation_.load(std::memory_order_acquire);
        const auto previousIndex = selectedCandidate_.load(std::memory_order_acquire);
        evaluation_.store(std::move(a_evaluation), std::memory_order_release);
        const auto snapshot = evaluation_.load(std::memory_order_acquire);
        const auto count = snapshot ? snapshot->candidates.size() : 0;
        if (count == 0) {
            selectedCandidate_.store(0, std::memory_order_release);
            return;
        }
        if (snapshot->anchorLOS && previous && previousIndex < previous->candidates.size()) {
            const auto& previousID = previous->candidates[previousIndex].presetID;
            const auto retained = std::ranges::find(
                snapshot->candidates, previousID, &core::CameraCandidateVisibility::presetID);
            selectedCandidate_.store(
                retained != snapshot->candidates.end() ?
                    static_cast<std::size_t>(std::distance(snapshot->candidates.begin(), retained)) : 0,
                std::memory_order_release);
            return;
        }
        const auto selected = snapshot->selectedPresetID ?
            std::ranges::find_if(snapshot->candidates, [&](const auto& a_candidate) {
                return a_candidate.presetID == *snapshot->selectedPresetID;
            }) :
            std::ranges::find_if(
                snapshot->candidates,
                [](const auto& a_candidate) { return a_candidate.usable; });
        selectedCandidate_.store(
            selected != snapshot->candidates.end() ?
                static_cast<std::size_t>(
                    std::distance(snapshot->candidates.begin(), selected)) : 0,
            std::memory_order_release);
    }

    void WorldDebugVisualization::HideVisibility() noexcept
    {
        evaluation_.store(nullptr, std::memory_order_release);
        selectedCandidate_.store(0, std::memory_order_release);
    }

    bool WorldDebugVisualization::Enabled() const noexcept
    {
        return Available() && EditHotkeySettings::GetSingleton()->DebugMode();
    }

    void WorldDebugVisualization::StepCandidate(int a_direction) noexcept
    {
        const auto snapshot = evaluation_.load(std::memory_order_acquire);
        if (!snapshot || snapshot->candidates.empty() || a_direction == 0) {
            return;
        }
        const auto count = snapshot->candidates.size();
        const auto current = std::min(
            selectedCandidate_.load(std::memory_order_acquire), count - 1);
        const auto next = a_direction > 0 ?
            (current + 1) % count : (current == 0 ? count - 1 : current - 1);
        selectedCandidate_.store(next, std::memory_order_release);
        logger::info("Debug preset selected: '{}' ({}/{})",
            snapshot->candidates[next].presetID,
            next + 1,
            count);
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
        if (snapshot->anchorLOS) {
            const auto cornersVisible = std::ranges::count_if(candidate.points, [](const auto& point) {
                return point.point != core::VisibilityPoint::kAnchor &&
                    point.status == core::VisibilityPointStatus::kVisible;
            });
            const auto unavailable = std::ranges::count_if(candidate.points, [](const auto& point) {
                return point.status == core::VisibilityPointStatus::kUnavailable;
            });
            return fmt::format("{} ({}/{}, center: {}, corners: {}/4, unavailable: {})",
                candidate.presetID, selected + 1, snapshot->candidates.size(),
                candidate.points.empty() ? "unavailable" :
                    core::VisibilityPointStatusName(candidate.points.front().status),
                cornersVisible, unavailable);
        }
        return fmt::format(
            "{} ({}/{}, {}/{} centers, {})",
            candidate.presetID,
            selected + 1,
            snapshot->candidates.size(),
            candidate.visiblePointCount,
            candidate.availablePointCount,
            core::CandidateFailureReasonName(candidate.failureReason));
    }

    void __stdcall WorldDebugVisualization::RenderVisibility()
    {
        auto* self = GetSingleton();
        if (!self->Enabled()) {
            return;
        }

        auto* playerCamera = RE::PlayerCamera::GetSingleton();
        auto* niCamera = playerCamera ? FindNiCamera(playerCamera->cameraRoot.get()) : nullptr;
        auto* drawList = ImGuiMCP::GetBackgroundDrawList();
        if (!niCamera || !drawList) {
            return;
        }

        std::optional<AnchorDisplay> anchor;
        {
            std::scoped_lock lock{ self->anchorMutex_ };
            anchor = self->anchor_;
        }
        if (anchor) {
            constexpr auto orange = Color(255, 165, 50, 240);
            if (const auto screen = Project(*niCamera, ToCore(anchor->targetPosition))) {
                ImGuiMCP::ImDrawListManager::AddCircleFilled(
                    drawList, *screen, kPointRadius, orange, 12);
                ImGuiMCP::ImDrawListManager::AddText(
                    drawList, { screen->x + 8.0F, screen->y - 18.0F }, orange, "Torso target");
            }
            constexpr auto cyan = Color(55, 220, 245, 240);
            const auto anchorPosition = ToCore(anchor->position);
            const core::Vec3 forwardEnd{
                anchorPosition.x + anchor->forward.x * kAnchorLength,
                anchorPosition.y + anchor->forward.y * kAnchorLength,
                anchorPosition.z + anchor->forward.z * kAnchorLength,
            };
            DrawLine(drawList, *niCamera, anchorPosition, forwardEnd, cyan);
            if (const auto screen = Project(*niCamera, anchorPosition)) {
                ImGuiMCP::ImDrawListManager::AddCircleFilled(
                    drawList, *screen, kPointRadius, cyan, 12);
                ImGuiMCP::ImDrawListManager::AddText(
                    drawList, { screen->x + 8.0F, screen->y + 5.0F }, cyan, "Anchor");
            }
        }

        const auto snapshot = self->evaluation_.load(std::memory_order_acquire);
        if (snapshot && snapshot->anchorLOS) {
            const auto& metrics = *snapshot->anchorLOS;
            const auto label = fmt::format(
                "Anchor LOS camera-side 32x18 (5 rays/preset) / {:.2f}s / sample {} / {} presets / {} physics rays\n"
                "Batch {:.4f} ms | LOS {:.4f} ms | avg {:.4f} ms | max {:.4f} ms\n"
                "Estimated {:.4f} ms/s (excludes logging/HUD)\n{}",
                metrics.intervalSeconds, metrics.sampleCount, snapshot->candidates.size(),
                metrics.rayQueryCount, metrics.lastMilliseconds, metrics.traceMilliseconds,
                metrics.averageMilliseconds, metrics.maximumMilliseconds,
                metrics.averageMilliseconds / metrics.intervalSeconds,
                self->SelectedCandidateLabel());
            ImGuiMCP::ImDrawListManager::AddText(
                drawList, { 20.0F, 80.0F }, Color(240, 240, 240, 255), label.c_str());
        }
        if (!snapshot || snapshot->candidates.empty()) {
            return;
        }
        const auto selected = std::min(
            self->selectedCandidate_.load(std::memory_order_acquire),
            snapshot->candidates.size() - 1);
        const auto& candidate = snapshot->candidates[selected];
        constexpr auto magenta = Color(235, 90, 235, 240);
        if (candidate.pose) {
            if (const auto screen = Project(*niCamera, candidate.pose->position)) {
                ImGuiMCP::ImDrawListManager::AddCircleFilled(
                    drawList, *screen, kCandidateRadius, magenta, 16);
                ImGuiMCP::ImDrawListManager::AddText(
                    drawList,
                    { screen->x + 10.0F, screen->y + 6.0F },
                    magenta,
                    candidate.presetID.c_str());
            }
        }

        const auto showOccludedSegments =
            self->showOccludedSegments_.load(std::memory_order_acquire);
        constexpr auto green = Color(70, 220, 90, 230);
        constexpr auto red = Color(235, 65, 65, 230);
        constexpr auto gray = Color(155, 155, 155, 200);
        constexpr auto yellow = Color(245, 210, 65, 230);

        for (const auto& point : candidate.points) {
            if (snapshot->anchorLOS && candidate.pose) {
                if (const auto screen = Project(*niCamera, point.rayStart)) {
                    const auto color = point.status == core::VisibilityPointStatus::kVisible ? green :
                        (point.status == core::VisibilityPointStatus::kUnavailable ? gray : red);
                    DrawPointMarker(drawList, *screen, point.point, color);
                }
            }
            if (point.status == core::VisibilityPointStatus::kUnavailable) {
                if (snapshot->anchorLOS && candidate.pose) {
                    if (const auto screen = Project(*niCamera, point.rayStart)) {
                        ImGuiMCP::ImDrawListManager::AddText(
                            drawList, { screen->x + 7.0F, screen->y + 4.0F }, gray, "unavailable");
                    }
                }
                continue;
            }

            const auto targetScreen = Project(*niCamera, point.target);
            if (point.status == core::VisibilityPointStatus::kVisible) {
                DrawLine(drawList, *niCamera, point.rayStart, point.target, green);
                if (targetScreen && !snapshot->anchorLOS) {
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
                if (targetScreen && !snapshot->anchorLOS) {
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
    }
}
