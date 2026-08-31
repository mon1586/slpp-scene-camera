#include "SceneCamera.h"
#include "runtime/CameraHook.h"
#include "runtime/SceneEventMailbox.h"
#include "runtime/SexLabPSceneSource.h"
#include "runtime/SmoothCamCameraControl.h"

namespace
{
    void InitializeLog()
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

    void QueueLifecycleReset(std::string_view a_reason)
    {
        auto* mailbox = ssc::runtime::SceneEventMailbox::GetSingleton();
        mailbox->BeginNewGeneration();
        auto* sceneCamera = ssc::SceneCamera::GetSingleton();
        sceneCamera->RequestReset();
        logger::info("Camera reset requested: {}", a_reason);
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        try {
            const auto* messaging = SKSE::GetMessagingInterface();
            switch (a_message->type) {
            case SKSE::MessagingInterface::kPostLoad:
                static_cast<void>(
                    ssc::runtime::SmoothCamCameraControl::GetSingleton()->RegisterAPIListener(messaging));
                break;
            case SKSE::MessagingInterface::kPostPostLoad:
                static_cast<void>(
                    ssc::runtime::SmoothCamCameraControl::GetSingleton()->RequestAPI(messaging));
                break;
            case SKSE::MessagingInterface::kDataLoaded:
                if (!ssc::runtime::SexLabPSceneSource::GetSingleton()->Register(
                        ssc::runtime::CameraHook::SubmitEvent)) {
                    logger::warn("No scene source was registered");
                }
                break;
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
            ssc::SceneCamera::GetSingleton()->RequestReset();
        } catch (...) {
            ssc::SceneCamera::GetSingleton()->RequestReset();
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    InitializeLog();
    logger::info("Sexlab Scene Camera POC 0.1.0 loading");

    SKSE::Init(a_skse, false);
    if (REL::Module::IsVR()) {
        logger::critical("Skyrim VR is not supported by this POC");
        return false;
    }

    auto* sceneCamera = ssc::SceneCamera::GetSingleton();
    sceneCamera->Configure(
        *ssc::runtime::SexLabPSceneSource::GetSingleton(),
        *ssc::runtime::SmoothCamCameraControl::GetSingleton());
    ssc::runtime::CameraHook::Configure(*sceneCamera);

    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageHandler)) {
        logger::critical("Failed to register the SKSE messaging listener");
        return false;
    }

    logger::info("Sexlab Scene Camera POC loaded");
    return true;
}
