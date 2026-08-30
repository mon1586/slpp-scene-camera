#include "controller/CameraOutput.h"

namespace ssc::controller
{
    namespace
    {
        void UpdateWorldToScreen(RE::NiCamera* a_camera)
        {
            using UpdateWorldToScreen_t = void (*)(RE::NiCamera*);
            static REL::Relocation<UpdateWorldToScreen_t> updateWorldToScreen{
                RELOCATION_ID(70641, 69271)
            };
            updateWorldToScreen(a_camera);
        }

        [[nodiscard]] RE::NiMatrix3 TopDownCameraRotation() noexcept
        {
            RE::NiMatrix3 rotation;
            rotation.entry[0][0] = 0.0F;
            rotation.entry[0][1] = 0.0F;
            rotation.entry[0][2] = 1.0F;
            rotation.entry[1][0] = 1.0F;
            rotation.entry[1][1] = 0.0F;
            rotation.entry[1][2] = 0.0F;
            rotation.entry[2][0] = 0.0F;
            rotation.entry[2][1] = 1.0F;
            rotation.entry[2][2] = 0.0F;
            return rotation;
        }
    }

    RE::NiCamera* CameraOutput::FindNiCamera(RE::NiAVObject* a_object) noexcept
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

    void CameraOutput::Apply(RE::PlayerCamera* a_camera, const core::CameraPose& a_pose) noexcept
    {
        if (!a_camera || !a_camera->cameraRoot) {
            return;
        }

        auto* niCamera = FindNiCamera(a_camera->cameraRoot.get());
        if (!niCamera) {
            if (!loggedMissingCamera_) {
                logger::error("Player camera NiCamera node was not found; POC pose cannot be applied");
                loggedMissingCamera_ = true;
            }
            return;
        }

        loggedMissingCamera_ = false;
        const RE::NiPoint3 position{ a_pose.position.x, a_pose.position.y, a_pose.position.z };

        a_camera->cameraRoot->local.translate = position;
        a_camera->cameraRoot->world.translate = position;
        niCamera->world.translate = position;

        // This fixed transform is intentionally POC-only: the pose is directly above
        // its target, so the camera always looks vertically down.
        a_camera->cameraRoot->local.rotate = RE::NiMatrix3{};
        a_camera->cameraRoot->world.rotate = RE::NiMatrix3{};
        niCamera->world.rotate = TopDownCameraRotation();

        if (auto* thirdPerson = skyrim_cast<RE::ThirdPersonState*>(a_camera->currentState.get())) {
            thirdPerson->translation = position;
        }

        UpdateWorldToScreen(niCamera);
    }
}

