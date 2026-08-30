#include "controller/SceneCameraController.h"
#include "controller/SceneEventMailbox.h"
#include "controller/SexLabEventSink.h"

#include <SmoothCamAPI.h>

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
        auto* mailbox = ssc::controller::SceneEventMailbox::GetSingleton();
        mailbox->BeginNewGeneration();
        auto* controller = ssc::controller::SceneCameraController::GetSingleton();
        controller->RequestReset();
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
            case SKSE::MessagingInterface::kPostLoad: {
                const auto registered = SmoothCamAPI::RegisterInterfaceLoaderCallback(
                    messaging,
                    [](void* a_interface, SmoothCamAPI::InterfaceVersion a_version) {
                        try {
                            ssc::controller::SceneCameraController::GetSingleton()->SetSmoothCamInterface(
                                a_interface, a_version);
                        } catch (...) {
                            ssc::controller::SceneCameraController::GetSingleton()->RequestReset();
                        }
                    });
                logger::info("SmoothCam interface callback registration: {}", registered ? "OK" : "FAILED");
                break;
            }
            case SKSE::MessagingInterface::kPostPostLoad: {
                const auto requested = SmoothCamAPI::RequestInterface(
                    messaging, SmoothCamAPI::InterfaceVersion::V2);
                logger::info("SmoothCam V2 interface request dispatched: {}", requested ? "yes" : "no listener");
                break;
            }
            case SKSE::MessagingInterface::kDataLoaded:
                ssc::controller::SexLabEventSink::Register();
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
            ssc::controller::SceneCameraController::GetSingleton()->RequestReset();
        } catch (...) {
            ssc::controller::SceneCameraController::GetSingleton()->RequestReset();
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

    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageHandler)) {
        logger::critical("Failed to register the SKSE messaging listener");
        return false;
    }

    logger::info("Sexlab Scene Camera POC loaded");
    return true;
}
