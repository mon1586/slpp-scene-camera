#include "runtime/CameraOutput.h"

namespace ssc::runtime
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
            const RotationMatrix& a_rotation) noexcept
        {
            RE::NiMatrix3 rotation;
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 3; ++column) {
                    rotation.entry[row][column] = a_rotation.entries[row][column];
                }
            }
            return rotation;
        }

        [[nodiscard]] RE::NiMatrix3 ToCameraRootRotation(
            const RE::NiMatrix3& a_cameraRotation) noexcept
        {
            RE::NiMatrix3 rootRotation;
            for (std::size_t row = 0; row < 3; ++row) {
                // Skyrim's camera root uses (right, forward, up), while
                // NiCamera uses (view-forward, up, right).
                rootRotation.entry[row][0] = a_cameraRotation.entry[row][2];
                rootRotation.entry[row][1] = a_cameraRotation.entry[row][0];
                rootRotation.entry[row][2] = a_cameraRotation.entry[row][1];
            }
            return rootRotation;
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

    CameraApplyResult CameraOutput::Apply(const CameraPose& a_pose)
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!camera || !camera->currentState) {
            return CameraApplyResult::kMissingCamera;
        }

        const auto stateID = camera->currentState->id;
        if (stateID != RE::CameraState::kThirdPerson && stateID != RE::CameraState::kAnimated) {
            return CameraApplyResult::kUnsupportedState;
        }

        if (!camera->cameraRoot) {
            return CameraApplyResult::kMissingCamera;
        }

        auto* niCamera = FindNiCamera(camera->cameraRoot.get());
        if (!niCamera) {
            return CameraApplyResult::kMissingCamera;
        }

        const RE::NiPoint3 position{ a_pose.position.x, a_pose.position.y, a_pose.position.z };

        camera->cameraRoot->local.translate = position;
        camera->cameraRoot->world.translate = position;
        niCamera->world.translate = position;

        const auto cameraRotation = ToGameRotation(a_pose.rotation);
        const auto rootRotation = ToCameraRootRotation(cameraRotation);
        camera->cameraRoot->local.rotate = rootRotation;
        camera->cameraRoot->world.rotate = rootRotation;
        niCamera->world.rotate = cameraRotation;

        if (auto* thirdPerson = skyrim_cast<RE::ThirdPersonState*>(camera->currentState.get())) {
            thirdPerson->translation = position;
        }

        UpdateWorldToScreen(niCamera);
        return CameraApplyResult::kApplied;
    }
}
