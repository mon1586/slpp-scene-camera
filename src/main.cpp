#include "SceneCamera.h"
#include "runtime/CameraHook.h"
#include "runtime/CameraInput.h"
#include "runtime/HavokVisibilityProbe.h"
#include "runtime/PresetRepository.h"
#include "runtime/PresetPreviewService.h"
#include "runtime/PluginRuntime.h"
#include "runtime/SexLabPSceneSource.h"
#include "runtime/SmoothCamCameraControl.h"
#include "runtime/TDMTargetLockControl.h"
#include "runtime/WorldDebugVisualization.h"

#include <REX/W32/KERNEL32.h>

namespace
{
    void ReportLoadFailure(const char* a_detail) noexcept
    {
        try {
            logger::critical("Sexlab Scene Camera failed to load: {}",
                a_detail ? a_detail : "unknown exception");
        } catch (...) {
        }

        REX::W32::OutputDebugStringA("Sexlab Scene Camera failed to load: ");
        REX::W32::OutputDebugStringA(a_detail ? a_detail : "unknown exception");
        REX::W32::OutputDebugStringA("\n");
    }

}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    try {
        ssc::runtime::PluginRuntime::InitializeLog();
        logger::info("Sexlab Scene Camera POC 0.1.0 loading");
        logger::info("MoveScene control v1 enabled: yield on movement unlock; resume after relock settles");

        SKSE::Init(a_skse, false);
        if (REL::Module::IsVR()) {
            logger::critical("Skyrim VR is not supported by this POC");
            return false;
        }

        auto* sceneCamera = ssc::SceneCamera::GetSingleton();
        sceneCamera->Configure(
            *ssc::runtime::SexLabPSceneSource::GetSingleton(),
            *ssc::runtime::PresetRepository::GetSingleton(),
            *ssc::runtime::PresetPreviewService::GetSingleton(),
            *ssc::runtime::SmoothCamCameraControl::GetSingleton(),
            *ssc::runtime::HavokVisibilityProbe::GetSingleton(),
            *ssc::runtime::WorldDebugVisualization::GetSingleton(),
            ssc::runtime::TDMTargetLockControl::GetSingleton());
        ssc::runtime::CameraHook::Configure(
            *sceneCamera,
            *ssc::runtime::SexLabPSceneSource::GetSingleton());
        ssc::runtime::CameraInput::Configure(*sceneCamera);

        if (!ssc::runtime::PluginRuntime::RegisterLifecycle(*sceneCamera)) {
            return false;
        }

        logger::info("Sexlab Scene Camera POC loaded");
        return true;
    } catch (const std::exception& exception) {
        ReportLoadFailure(exception.what());
        return false;
    } catch (...) {
        ReportLoadFailure(nullptr);
        return false;
    }
}
