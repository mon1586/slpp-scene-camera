#pragma once

namespace ssc::controller
{
    class CameraHook
    {
    public:
        [[nodiscard]] static bool Install() noexcept;

    private:
        using UpdateFunction = void (*)(
            RE::TESCameraState*,
            RE::BSTSmartPointer<RE::TESCameraState>&);

        struct HookEntry
        {
            std::uintptr_t vtable{ 0 };
            std::uintptr_t original{ 0 };
        };

        static void Thunk(
            RE::TESCameraState* a_state,
            RE::BSTSmartPointer<RE::TESCameraState>& a_nextState);

        static inline constexpr std::size_t kMaxHookedVtables = 16;
        static inline std::array<HookEntry, kMaxHookedVtables> entries_{};
        static inline std::atomic<std::size_t> entryCount_{ 0 };
        static inline std::atomic_bool installed_{ false };
        static inline std::mutex installMutex_;
    };
}
