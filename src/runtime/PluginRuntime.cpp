#include "runtime/PluginRuntime.h"

#include "runtime/CameraHook.h"
#include "runtime/PresetRepository.h"
#include "runtime/SexLabPSceneSource.h"
#include "runtime/SmoothCamCameraControl.h"

namespace ssc::runtime
{
    namespace
    {
        constexpr auto kPresetRelativePath =
            "Data/SKSE/Plugins/SexlabSceneCamera/presets.json"sv;
    }

    void PluginRuntime::InitializeLog()
    {
        auto logDirectory = SKSE::log::log_directory();
        if (!logDirectory) {
            SKSE::stl::report_and_fail("Unable to find the SKSE log directory");
        }

        *logDirectory /= "SexlabSceneCamera.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logDirectory->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::warn);
    }

    bool PluginRuntime::RegisterLifecycle(IRuntimeClient& a_client)
    {
        client_ = std::addressof(a_client);
        const auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging || !messaging->RegisterListener(MessageHandler)) {
            logger::critical("Failed to register the SKSE messaging listener");
            client_ = nullptr;
            return false;
        }
        return true;
    }

    void PluginRuntime::QueueLifecycleReset(std::string_view a_reason)
    {
        CameraHook::InvalidatePendingEvents();
        CameraHook::QueueReset(a_reason);
        logger::info("Camera reset requested: {}", a_reason);
    }

    void PluginRuntime::MessageHandler(SKSE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        try {
            const auto* messaging = SKSE::GetMessagingInterface();
            switch (a_message->type) {
            case SKSE::MessagingInterface::kPostLoad:
                static_cast<void>(
                    SmoothCamCameraControl::GetSingleton()->RegisterAPIListener(messaging));
                break;
            case SKSE::MessagingInterface::kPostPostLoad:
                static_cast<void>(
                    SmoothCamCameraControl::GetSingleton()->RequestAPI(messaging));
                break;
            case SKSE::MessagingInterface::kDataLoaded:
            {
                const auto gameDirectory =
                    std::filesystem::path{ REL::Module::get().filePath() }.parent_path();
                const auto presetPath = gameDirectory / kPresetRelativePath;
                const auto loadResult =
                    PresetRepository::GetSingleton()->LoadFromFile(presetPath);
                if (loadResult.succeeded) {
                    logger::info("Loaded {} camera preset(s) from {}",
                        loadResult.presetCount,
                        presetPath.string());
                } else {
                    logger::error("Could not load camera presets from {}: {}",
                        presetPath.string(),
                        loadResult.error);
                }
                if (!CameraHook::RegisterCameraStateSink()) {
                    logger::warn("Camera-state observer is unavailable");
                }
                if (!SexLabPSceneSource::GetSingleton()->Register(CameraHook::SubmitEvent)) {
                    logger::warn("No scene source was registered");
                }
                break;
            }
            case SKSE::MessagingInterface::kPreLoadGame:
                QueueLifecycleReset("pre-load game"sv);
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
                QueueLifecycleReset("post-load game"sv);
                break;
            case SKSE::MessagingInterface::kNewGame:
                QueueLifecycleReset("new game"sv);
                break;
            default:
                break;
            }
        } catch (const std::exception& exception) {
            try {
                logger::critical("SKSE message handler failed: {}", exception.what());
            } catch (...) {
            }
            CameraHook::QueueReset("SKSE message handler failure");
        } catch (...) {
            CameraHook::QueueReset("unknown SKSE message handler failure");
        }
    }
}
