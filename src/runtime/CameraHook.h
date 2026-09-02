#pragma once

#include "runtime/ISceneSource.h"
#include "runtime/SceneEventMailbox.h"

namespace ssc::runtime
{
    class CameraHook
    {
    public:
        static void Configure(IRuntimeClient& a_client, ISceneSource& a_sceneSource) noexcept;
        static void SubmitEvent(SceneEvent a_event);

    private:
        enum class InstallState : std::uint8_t
        {
            kNotInstalled,
            kInstalled,
            kFailed,
        };

        using UpdateFunction = void (*)(
            RE::TESCameraState*,
            RE::BSTSmartPointer<RE::TESCameraState>&);

        struct HookEntry
        {
            std::uintptr_t vtable{ 0 };
            std::uintptr_t slot{ 0 };
        };

        template <std::size_t Index>
        static void Thunk(
            RE::TESCameraState* a_state,
            RE::BSTSmartPointer<RE::TESCameraState>& a_nextState);

        template <std::size_t... Indices>
        [[nodiscard]] static constexpr auto MakeThunkTable(
            std::index_sequence<Indices...>) noexcept;

        [[nodiscard]] static bool InstallOnce(const SceneKey& a_key);
        [[nodiscard]] static std::uintptr_t GetThunkAddress(std::size_t a_index) noexcept;
        static void HandleBoundaryFailure(std::string_view a_context) noexcept;
        static void HandleUpdateFailure(std::string_view a_context) noexcept;

        static inline constexpr std::size_t kMaxHookedVtables = RE::CameraStates::kTotal;
        static inline std::array<HookEntry, kMaxHookedVtables> entries_{};
        static inline std::array<std::atomic<UpdateFunction>, kMaxHookedVtables> originals_{};
        static inline std::size_t entryCount_{ 0 };
        static inline std::atomic<InstallState> installState_{ InstallState::kNotInstalled };
        static inline std::atomic_bool firstThunkObserved_{ false };
        static inline IRuntimeClient* client_{ nullptr };
        static inline ISceneSource* sceneSource_{ nullptr };
        static inline thread_local std::size_t thunkDepth_{ 0 };
    };
}
