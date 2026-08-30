#include "controller/CameraHook.h"

#include "controller/SceneCameraController.h"
#include "controller/SceneEventMailbox.h"

namespace ssc::controller
{
    namespace
    {
        constexpr std::size_t kUpdateSlot = 0x03;
    }

    bool CameraHook::Install() noexcept
    {
        if (installed_.load(std::memory_order_acquire)) {
            return true;
        }

        std::scoped_lock lock{ installMutex_ };
        if (installed_.load(std::memory_order_relaxed)) {
            return true;
        }

        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!camera) {
            logger::error("Cannot hook camera-state updates: PlayerCamera is unavailable");
            return false;
        }

        auto count = entryCount_.load(std::memory_order_relaxed);
        bool failed = false;
        const auto thunkAddress = SKSE::stl::unrestricted_cast<std::uintptr_t>(Thunk);
        auto& cameraStates = camera->GetRuntimeData().cameraStates;

        for (auto& statePointer : cameraStates) {
            auto* state = statePointer.get();
            if (!state) {
                continue;
            }

            auto* vtable = *reinterpret_cast<std::uintptr_t**>(state);
            const auto vtableAddress = reinterpret_cast<std::uintptr_t>(vtable);
            bool alreadyHooked = false;
            for (std::size_t index = 0; index < count; ++index) {
                if (entries_[index].vtable == vtableAddress) {
                    alreadyHooked = true;
                    break;
                }
            }
            if (alreadyHooked) {
                continue;
            }

            if (count == entries_.size()) {
                logger::error("Cannot hook camera-state updates: vtable capacity exhausted");
                failed = true;
                break;
            }

            const auto slotAddress = vtableAddress + sizeof(std::uintptr_t) * kUpdateSlot;
            const auto originalAddress = vtable[kUpdateSlot];
            if (!originalAddress || originalAddress == thunkAddress) {
                logger::error("Cannot hook camera-state update vtable {:X}: invalid original {:X}",
                    vtableAddress, originalAddress);
                failed = true;
                continue;
            }

            entries_[count] = { vtableAddress, originalAddress };
            entryCount_.store(count + 1, std::memory_order_release);
            if (!REL::safe_write(
                    slotAddress,
                    std::addressof(thunkAddress),
                    sizeof(thunkAddress),
                    std::addressof(originalAddress),
                    sizeof(originalAddress))) {
                entryCount_.store(count, std::memory_order_release);
                entries_[count] = {};
                logger::error("Camera-state update hook verification failed for vtable {:X}", vtableAddress);
                failed = true;
                continue;
            }
            ++count;
        }

        if (failed || count == 0) {
            logger::error("Camera-state update hook installation failed ({} vtable(s) hooked)", count);
            return false;
        }

        installed_.store(true, std::memory_order_release);
        logger::info("Camera-state Update hook installed on {} unique vtable(s)", count);
        return true;
    }

    void CameraHook::Thunk(
        RE::TESCameraState* a_state,
        RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
    {
        const auto vtableAddress = a_state ?
            reinterpret_cast<std::uintptr_t>(*reinterpret_cast<std::uintptr_t**>(a_state)) : 0;
        const auto count = entryCount_.load(std::memory_order_acquire);
        UpdateFunction original = nullptr;
        for (std::size_t index = 0; index < count; ++index) {
            if (entries_[index].vtable == vtableAddress) {
                original = SKSE::stl::unrestricted_cast<UpdateFunction>(entries_[index].original);
                break;
            }
        }

        if (!original) {
            static std::once_flag missingOriginalLogged;
            std::call_once(missingOriginalLogged, [vtableAddress] {
                logger::critical("Camera-state hook cannot find original for vtable {:X}", vtableAddress);
            });
            return;
        }

        original(a_state, a_nextState);
        SceneEventMailbox::GetSingleton()->DispatchPending();
        SceneCameraController::GetSingleton()->Update(RE::PlayerCamera::GetSingleton());
    }
}
