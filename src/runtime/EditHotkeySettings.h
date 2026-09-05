#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace ssc::runtime
{
    inline constexpr std::uint32_t kDefaultEditHotkey = 0x42;
    inline constexpr std::uint32_t kEscapeKeyboardKey = 0x01;

    struct EditHotkeySettingsResult
    {
        bool succeeded{ false };
        std::string error;
    };

    [[nodiscard]] bool IsValidEditHotkey(std::uint32_t a_keyCode) noexcept;
    [[nodiscard]] std::string EditHotkeyName(std::uint32_t a_keyCode);

    class EditHotkeySettings
    {
    public:
        static EditHotkeySettings* GetSingleton() noexcept;

        [[nodiscard]] EditHotkeySettingsResult LoadFromFile(
            const std::filesystem::path& a_path);
        [[nodiscard]] EditHotkeySettingsResult SetEditHotkey(std::uint32_t a_keyCode);
        [[nodiscard]] std::uint32_t EditHotkey() const noexcept;

    private:
        std::mutex mutex_;
        std::filesystem::path storagePath_;
        bool loaded_{ false };
        std::atomic_uint32_t editHotkey_{ kDefaultEditHotkey };
    };
}
