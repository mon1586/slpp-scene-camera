#include "controller/CameraOutput.h"

namespace ssc::controller
{
    namespace
    {
        void UpdateWorldToScreen(RE::NiCamera* a_camera)
        {
            using UpdateWorldToScreen_t = void (*)(RE::NiCamera*);
            static REL::Relocation<UpdateWorldToScreen_t> updateWorldToScreen{
                // CommonLib expects IDs in (SE, AE) order. SmoothCam uses the same
                // 69271/70641 pair for this function.
                RELOCATION_ID(69271, 70641)
            };
            updateWorldToScreen(a_camera);
        }

        [[nodiscard]] RE::NiMatrix3 ToGameRotation(
            const core::RotationMatrix& a_rotation) noexcept
        {
            RE::NiMatrix3 rotation;
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 3; ++column) {
                    rotation.entry[row][column] = a_rotation.entries[row][column];
                }
            }
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

    CameraApplyResult CameraOutput::Apply(
        RE::PlayerCamera* a_camera,
        const core::CameraPose& a_pose)
    {
        if (!a_camera || !a_camera->currentState) {
            return CameraApplyResult::kMissingCamera;
        }

        const auto stateID = a_camera->currentState->id;
        if (stateID != RE::CameraState::kThirdPerson && stateID != RE::CameraState::kAnimated) {
            return CameraApplyResult::kUnsupportedState;
        }

        if (!a_camera->cameraRoot) {
            return CameraApplyResult::kMissingCamera;
        }

        auto* niCamera = FindNiCamera(a_camera->cameraRoot.get());
        if (!niCamera) {
            return CameraApplyResult::kMissingCamera;
        }

        const RE::NiPoint3 position{ a_pose.position.x, a_pose.position.y, a_pose.position.z };

        a_camera->cameraRoot->local.translate = position;
        a_camera->cameraRoot->world.translate = position;
        niCamera->world.translate = position;

        a_camera->cameraRoot->local.rotate = RE::NiMatrix3{};
        a_camera->cameraRoot->world.rotate = RE::NiMatrix3{};
        niCamera->world.rotate = ToGameRotation(a_pose.rotation);

        if (auto* thirdPerson = skyrim_cast<RE::ThirdPersonState*>(a_camera->currentState.get())) {
            thirdPerson->translation = position;
        }

        UpdateWorldToScreen(niCamera);
        return CameraApplyResult::kApplied;
    }
}
