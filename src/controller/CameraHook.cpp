#include "controller/CameraHook.h"

#include "controller/SceneCameraController.h"
#include "controller/SceneEventMailbox.h"

namespace ssc::controller
{
    void CameraHook::Install()
    {
        static std::once_flag installed;
        std::call_once(installed, [] {
            REL::Relocation<std::uintptr_t> playerCameraVtable{ RE::VTABLE_PlayerCamera[0] };
            original_ = playerCameraVtable.write_vfunc(0x02, Thunk);
            logger::info("PlayerCamera::Update hook installed");
        });
    }

    void CameraHook::Thunk(RE::PlayerCamera* a_camera)
    {
        original_(a_camera);
        SceneEventMailbox::GetSingleton()->DispatchPending();
        SceneCameraController::GetSingleton()->Update(a_camera);
    }
}
