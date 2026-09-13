#include "support/SceneCameraDoubles.h"
#include "runtime/PresetRepository.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>

namespace ssc::tests
{
    namespace
    {
        using Event = runtime::SceneEventType;
        using Reply = runtime::AnimationUpdateCoordinator::Reply;
        const runtime::PresetTransform front{ {}, { 0, 0, 100 } };
        const runtime::PresetTransform side{ {}, { 30, 0, 100 } };
        constexpr auto sitting = R"(^(?=.*\bsitting\b)(?!.*\bstanding\b).*$)";

        void Require(bool condition, const std::string& message)
        {
            if (!condition) { throw std::runtime_error(message); }
        }

        class TemporaryPresets
        {
        public:
            TemporaryPresets()
            {
                static std::atomic_uint64_t sequence{ 0 };
                const auto root = std::filesystem::temp_directory_path();
                for (;;) {
                    directory_ = root / ("ssc-filter-test-" +
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                        "-" + std::to_string(++sequence));
                    if (std::filesystem::create_directory(directory_)) { break; }
                }
            }
            ~TemporaryPresets()
            {
                // Only files created in this unique directory; no recursive deletion.
                std::error_code error;
                for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) {
                    std::filesystem::remove(entry.path(), error);
                }
                std::filesystem::remove(directory_, error);
            }
            std::filesystem::path Path() const { return directory_ / "presets.json"; }
            nlohmann::json Read() const
            {
                std::ifstream stream{ Path() };
                return nlohmann::json::parse(stream);
            }
            void Write(const nlohmann::json& document) const
            {
                std::ofstream stream{ Path(), std::ios::trunc };
                stream << document.dump(2);
                stream.close();
                Require(!stream.fail(), "write test preset file");
            }
        private:
            std::filesystem::path directory_;
        };

        struct DeferredMetadata
        {
            std::vector<Reply> ids, names, tags;
            void Connect(runtime::AnimationUpdateCoordinator& coordinator)
            {
                coordinator.Configure(
                    [&](const auto&) { return ids.emplace_back(std::make_shared<runtime::MetadataReply>()); },
                    [&](const auto&, bool isTags) {
                        return (isTags ? tags : names).emplace_back(std::make_shared<runtime::MetadataReply>());
                    });
            }
            static void Complete(const Reply& reply, std::string value,
                std::vector<std::string> values = {}, const char* status = "ok")
            {
                std::scoped_lock lock{ reply->mutex };
                reply->text = std::move(value); reply->tags = std::move(values);
                reply->status = status; reply->complete = true;
            }
        };

        struct FilterScene
        {
            TemporaryPresets files;
            runtime::PresetRepository repository;
            TestSceneSource source;
            runtime::PresetPreviewService preview;
            TestCameraControl output;
            TestVisibilityProbe probe;
            TestDebugVisualization debug;
            DeferredMetadata metadata;
            SceneCamera::Clock::time_point now{};
            SceneCamera camera{ [&] { return now; } };
            runtime::MainUpdateDispatcher dispatcher;
            const runtime::SceneKey key{ 0x01000001, 1 };

            FilterScene()
            {
                Require(repository.LoadFromFile(files.Path()).succeeded, "initialize real repository");
                Require(repository.Create({ "specific", front, "Specific", "Billy", sitting }).succeeded, "create specific");
                Require(repository.Create({ "generic", side, "Generic" }).succeeded, "create generic");
                camera.Configure(source, repository, preview, output, probe, debug);
                metadata.Connect(camera.AnimationUpdates());
            }
            void Queue(Event type)
            {
                runtime::SceneEvent event{ type, key, runtime::SexLabPSceneSource::MakeTestSnapshot() };
                camera.ReceiveSceneEvent(event);
                dispatcher.Post([this, event] { camera.HandleSceneEvent(event); });
            }
            void Tick()
            {
                dispatcher.Tick(camera);
                camera.Update(0.016F);
            }
            void Start()
            {
                Queue(Event::kAnimationStart); Tick();
                Require(output.acquireCount_ == 0, "must hold initial camera while metadata pending");
            }
            void Resolve(std::string id, std::string name, std::vector<std::string> tags)
            {
                Require(!metadata.ids.empty(), "expected ID request");
                const auto count = metadata.names.size();
                DeferredMetadata::Complete(metadata.ids.back(), std::move(id)); Tick();
                if (metadata.names.size() != count) {
                    DeferredMetadata::Complete(metadata.names.back(), std::move(name));
                    DeferredMetadata::Complete(metadata.tags.back(), {}, std::move(tags)); Tick();
                }
            }
            void Selected(std::string_view expected)
            {
                const auto actual = debug.snapshot_ ? debug.snapshot_->selectedPresetID.value_or("<none>") : "<no evaluation>";
                Require(actual == expected, "selected: expected " + std::string{ expected } + ", got " + actual);
                Require(output.OwnsCamera() && output.LastPose().has_value(), "selected preset must reach camera output");
            }
            void Candidates(const std::vector<std::string>& expected)
            {
                Require(static_cast<bool>(debug.snapshot_), "expected published candidates");
                std::vector<std::string> actual;
                for (const auto& candidate : debug.snapshot_->candidates) {
                    if (candidate.usable) { actual.push_back(candidate.presetID); }
                }
                Require(actual == expected, "eligible candidates differ from expected saved-order list");
            }
            void Step(int direction) { camera.RequestPresetStep(direction); Tick(); }
        };

        void FilterMatrix()
        {
            struct Case { const char* label; std::string name; std::vector<std::string> tags; bool matches; };
            const std::vector<Case> cases{
                { "sitting only", "Billyy", { "Sitting" }, true },
                { "standing only", "Billyy", { "Standing" }, false },
                { "positive before negative", "Billyy", { "Sitting", "Standing" }, false },
                { "negative before positive", "Billyy", { "Standing", "Sitting" }, false },
                { "neither", "Billyy", { "Other" }, false },
                { "empty tags", "Billyy", {}, false },
                { "name AND tags", "Other", { "Sitting" }, false },
                { "case and partial name", "Pack bIlLyY Scene", { "Other", "sItTiNg", "Feet" }, true },
            };
            for (const auto& test : cases) {
                try {
                    FilterScene scene; scene.Start(); scene.Resolve("id", test.name, test.tags);
                    scene.Selected(test.matches ? "specific" : "generic");
                    scene.Candidates(test.matches ? std::vector<std::string>{ "specific", "generic" } :
                        std::vector<std::string>{ "generic" });
                    scene.Step(1); scene.Selected("generic");
                    scene.Step(1); scene.Selected(test.matches ? "specific" : "generic");
                    scene.Step(-1); scene.Selected("generic");
                } catch (const std::exception& error) {
                    throw std::runtime_error(std::string{ test.label } + ": " + error.what());
                }
            }
        }

        void SavedOrderAndVisibility()
        {
            FilterScene scene;
            auto data = scene.files.Read();
            std::swap(data["presets"][0], data["presets"][1]);
            scene.files.Write(data);
            Require(scene.repository.Reload().succeeded, "reload reordered presets");
            scene.Start(); scene.Resolve("id", "Billyy", { "Sitting" });
            scene.Selected("generic"); scene.Step(1); scene.Selected("specific");
            Require(scene.repository.Update("specific", { {}, { 90, 0, 100 } }, "Specific", "Billy", sitting).succeeded,
                "save obstructed transform");
            scene.Tick(); scene.Selected("specific"); scene.Candidates({ "generic" });
            scene.Step(1); scene.Selected("generic");
        }

        void PersistenceAndFailures()
        {
            FilterScene scene; scene.Start(); scene.Resolve("id", "Billyy", { "Sitting", "Standing" });
            scene.Selected("generic");
            Require(scene.repository.Update("specific", front, "Specific", "Billy", "sitting|standing").succeeded, "save OR filter");
            runtime::PresetRepository reopened;
            Require(reopened.LoadFromFile(scene.files.Path()).succeeded &&
                reopened.Snapshot()->front().animationTagRegex == "sitting|standing", "fresh repository reads saved filter");
            Require(scene.repository.Reload().succeeded, "reload saved filters");
            scene.Tick(); scene.Step(1); scene.Selected("specific");
            const auto before = scene.repository.Snapshot();
            Require(!scene.repository.Update("specific", front, "Specific", "[", "").succeeded, "invalid regex rejected");
            Require(scene.repository.Snapshot() == before, "invalid save retains snapshot");
            scene.Tick(); scene.Candidates({ "specific", "generic" });
            auto valid = scene.files.Read(); auto invalid = valid;
            invalid["presets"][0]["animationTagRegex"] = 123;
            scene.files.Write(invalid);
            Require(!scene.repository.Reload().succeeded && scene.repository.Snapshot() == before, "bad reload retains snapshot");
            scene.Tick(); scene.Step(1); scene.Selected("generic");
            // External changes are not observed until Reload.
            valid["presets"][0]["animationTagRegex"] = "a^"; scene.files.Write(valid);
            scene.Tick(); scene.Candidates({ "specific", "generic" });
            Require(scene.repository.Reload().succeeded, "reload external filter change");
            scene.Tick(); scene.Candidates({ "generic" });
            auto legacy = valid;
            legacy["presets"][0].erase("animationNameRegex"); legacy["presets"][0].erase("animationTagRegex");
            scene.files.Write(legacy); Require(scene.repository.Reload().succeeded, "load legacy version5");
            scene.Tick(); scene.Step(1); scene.Selected("specific");
        }

        void DeferredChangesAndReset()
        {
            FilterScene scene; scene.Start(); scene.Resolve("A", "Billyy", { "Sitting" });
            scene.Selected("specific");
            scene.Queue(Event::kAnimationChange);
            scene.camera.RequestPresetStep(1); // Already received, not delivered.
            scene.Tick(); scene.Selected("specific");
            scene.Queue(Event::kStageStart); scene.Tick(); const auto old = scene.metadata.ids.back();
            scene.Queue(Event::kAnimationChange); scene.Queue(Event::kStageStart); scene.Tick();
            scene.Resolve("B", "Billyy", { "Standing" });
            DeferredMetadata::Complete(old, "A"); scene.Tick();
            scene.Selected("specific"); scene.Candidates({ "generic" });
            scene.Step(1); scene.Selected("generic");
            scene.Queue(Event::kStageStart); scene.Tick(); const auto late = scene.metadata.ids.back();
            scene.camera.RequestReset(); scene.Tick();
            Require(!scene.output.OwnsCamera() && !scene.camera.AnimationUpdates().Published(), "reset releases and clears metadata");
            scene.Queue(Event::kAnimationStart); scene.Tick();
            DeferredMetadata::Complete(late, "B"); scene.Tick();
            Require(scene.camera.AnimationUpdates().Pending(), "old lifetime cannot resolve new acquisition");
            scene.Resolve("C", "Billyy", { "Sitting" }); scene.Selected("specific");
        }

        void TimeoutAndMovement()
        {
            FilterScene scene; scene.Start();
            scene.now += std::chrono::milliseconds(4999); scene.Tick();
            Require(!scene.output.OwnsCamera(), "initial selection held before deadline");
            scene.now += std::chrono::milliseconds(1); scene.Tick();
            scene.Selected("generic"); scene.Candidates({ "generic" });
            scene.Queue(Event::kStageStart); scene.Tick(); scene.Resolve("A", "Billyy", { "Sitting" });
            scene.Step(1); scene.Selected("specific");
            scene.Queue(Event::kAnimationChange); scene.Tick();
            scene.source.controlState_->movementEnabled = true; scene.Tick();
            Require(!scene.output.OwnsCamera(), "movement returns camera while metadata pending");
            const auto count = scene.metadata.ids.size();
            scene.source.controlState_->movementEnabled = false;
            scene.Queue(Event::kActorsRelocated); scene.Tick();
            Require(!scene.output.OwnsCamera() && scene.metadata.ids.size() == count, "relocation neither resumes nor requests metadata");
            scene.now += std::chrono::seconds(5); scene.Tick(); scene.Selected("generic");
            scene.Queue(Event::kStageStart); scene.Tick();
            DeferredMetadata::Complete(scene.metadata.ids.back(), {}, {}, "dispatch_failed"); scene.Tick();
            scene.Candidates({ "generic" });
        }

        void SaveDuringAcquisitionAndEvaluation()
        {
            FilterScene scene; scene.Start();
            Require(scene.repository.Update("specific", front, "Specific", "Other", sitting).succeeded, "save during acquisition");
            scene.Resolve("A", "Billyy", { "Sitting" }); scene.Selected("generic");
            scene.Queue(Event::kStageStart); scene.Tick();
            bool saved = false;
            scene.probe.onTrace_ = [&] {
                saved = scene.repository.Update("specific", front, "Specific", "Billy", sitting).succeeded;
            };
            scene.Resolve("B", "Billyy", { "Sitting" });
            Require(saved, "save at LOS boundary ran successfully");
            Require(!scene.camera.AnimationUpdates().Published(), "stale pre-save evaluation must not publish metadata");
            scene.Tick(); scene.Candidates({ "specific", "generic" });
            scene.Step(1); scene.Selected("specific");
            scene.Queue(Event::kStageStart); scene.Tick();
            bool removed = false;
            scene.probe.onTrace_ = [&] { removed = scene.repository.Delete("specific").succeeded; };
            scene.Resolve("C", "Billyy", { "Sitting" });
            Require(!scene.camera.AnimationUpdates().Published(), "stale pre-delete evaluation must not publish metadata");
            scene.Tick();
            Require(removed, "delete at LOS boundary ran successfully");
            scene.Candidates({ "generic" });
            Require(!scene.output.OwnsCamera() && !scene.debug.snapshot_->selectedPresetID,
                "deleting selection returns camera without automatically selecting a replacement");
            scene.Tick(); // Resume normal input eligibility after the release update.
            scene.Step(1); scene.Selected("generic");
        }

        void ConcurrentReceiptDuringEvaluation()
        {
            FilterScene scene; scene.Start();
            std::promise<void> evaluating, received;
            auto entered = evaluating.get_future(); auto completed = received.get_future();
            bool synchronized = false;
            std::exception_ptr producerError;
            // One producer, preserving production event order. Only overlap receipt with LOS.
            std::jthread producer([&] {
                try {
                    if (entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
                        scene.Queue(Event::kAnimationChange);
                    }
                } catch (...) { producerError = std::current_exception(); }
                received.set_value();
            });
            scene.probe.onTrace_ = [&] {
                evaluating.set_value();
                synchronized = completed.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
            };
            scene.Resolve("A", "Billyy", { "Sitting" });
            producer.join();
            if (producerError) { std::rethrow_exception(producerError); }
            Require(synchronized, "producer must receive event before LOS resumes");
            Require(!scene.output.OwnsCamera() && !scene.camera.AnimationUpdates().Published(),
                "receipt during evaluation must prevent initial publication/acquisition");
            scene.Tick();
            scene.Queue(Event::kStageStart); scene.Tick(); scene.Resolve("B", "Billyy", { "Standing" });
            scene.Selected("generic"); scene.Candidates({ "generic" });
        }
    }
}

bool RunAnimationFilterIntegrationTests()
{
    using namespace ssc::tests;
    struct Case { const char* name; void (*run)(); };
    const Case cases[]{
        { "FILTER-FLOW-01 matrix", FilterMatrix },
        { "FILTER-FLOW-02 saved order and visibility", SavedOrderAndVisibility },
        { "FILTER-FLOW-03 persistence and failures", PersistenceAndFailures },
        { "FILTER-FLOW-04 deferred changes and reset", DeferredChangesAndReset },
        { "FILTER-FLOW-05 timeout and movement", TimeoutAndMovement },
        { "FILTER-FLOW-06 real saves during acquisition/evaluation", SaveDuringAcquisitionAndEvaluation },
        { "FILTER-FLOW-07 concurrent receipt during evaluation", ConcurrentReceiptDuringEvaluation },
    };
    bool passed = true;
    for (const auto& test : cases) {
        try { test.run(); std::cout << "PASS: " << test.name << '\n'; }
        catch (const std::exception& error) {
            passed = false; std::cerr << "FAILED: " << test.name << ": " << error.what() << '\n';
        }
    }
    return passed;
}
