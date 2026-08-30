#pragma once

#include "controller/SceneEventMailbox.h"

namespace ssc::controller
{
    class CameraHook
    {
    public:
        [[nodiscard]] static bool IsInstalled() noexcept;
        static void SubmitEvent(SceneEvent a_event);
        static void QueueRefresh();

    private:
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

        [[nodiscard]] static bool InstallOrRefresh();
        [[nodiscard]] static std::uintptr_t GetThunkAddress(std::size_t a_index) noexcept;
        [[nodiscard]] static bool IsThunkAddress(std::uintptr_t a_address) noexcept;
        static void HandleBoundaryFailure(std::string_view a_context) noexcept;

        static inline constexpr std::size_t kMaxHookedVtables = 64;
        static inline std::array<HookEntry, kMaxHookedVtables> entries_{};
        static inline std::array<std::atomic<UpdateFunction>, kMaxHookedVtables> originals_{};
        static inline std::size_t nextIndex_{ 0 };
        static inline std::atomic_bool installed_{ false };
        static inline std::atomic_bool refreshQueued_{ false };
        static inline bool cameraEventSinkRegistered_{ false };
        static inline std::mutex installMutex_;
        static inline thread_local std::size_t thunkDepth_{ 0 };
    };
}
