#include "runtime/EditHotkeySettings.h"

#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace ssc::runtime
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] EditHotkeySettingsResult Failure(std::string a_error)
        {
            return { false, std::move(a_error) };
        }

        void ReplaceFileTransactionally(
            const std::filesystem::path& a_path,
            std::uint32_t a_keyCode,
            bool a_debugMode)
        {
            const auto parent = a_path.parent_path();
            if (!parent.empty()) {
                std::error_code directoryError;
                std::filesystem::create_directories(parent, directoryError);
                if (directoryError) {
                    throw std::runtime_error(
                        "hotkey settings directory could not be created: " +
                        directoryError.message());
                }
            }

            static std::atomic_uint64_t counter{ 0 };
            auto temporaryPath = a_path;
            temporaryPath += ".tmp-" + std::to_string(::GetCurrentProcessId()) + "-" +
                             std::to_string(counter.fetch_add(1, std::memory_order_relaxed));

            try {
                std::ofstream stream{ temporaryPath, std::ios::binary | std::ios::trunc };
                if (!stream.is_open()) {
                    throw std::runtime_error("temporary hotkey settings file could not be opened");
                }
                stream << Json{
                    { "editHotkey", a_keyCode },
                    { "debugMode", a_debugMode },
                }.dump(2) << '\n';
                stream.flush();
                if (!stream.good()) {
                    throw std::runtime_error("temporary hotkey settings file could not be written");
                }
                stream.close();
                if (!stream.good()) {
                    throw std::runtime_error("temporary hotkey settings file could not be closed");
                }

                if (!::MoveFileExW(
                        temporaryPath.c_str(),
                        a_path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    const std::error_code error{
                        static_cast<int>(::GetLastError()),
                        std::system_category() };
                    throw std::runtime_error(
                        "temporary hotkey settings file could not replace the destination: " +
                        error.message());
                }
            } catch (...) {
                std::error_code ignored;
                static_cast<void>(std::filesystem::remove(temporaryPath, ignored));
                throw;
            }
        }
    }

    bool IsValidEditHotkey(std::uint32_t a_keyCode) noexcept
    {
        return a_keyCode > 0 && a_keyCode <= std::numeric_limits<std::uint8_t>::max() &&
               a_keyCode != kEscapeKeyboardKey;
    }

    std::string EditHotkeyName(std::uint32_t a_keyCode)
    {
        if (!IsValidEditHotkey(a_keyCode)) {
            return "Unassigned";
        }

        const auto scanCode = static_cast<LONG>((a_keyCode & 0x7FU) << 16U) |
            ((a_keyCode & 0x80U) != 0 ? (1L << 24U) : 0L);
        char buffer[64]{};
        if (::GetKeyNameTextA(scanCode, buffer, static_cast<int>(std::size(buffer))) > 0) {
            return buffer;
        }

        char fallback[16]{};
        std::snprintf(fallback, std::size(fallback), "Key 0x%02X", a_keyCode);
        return fallback;
    }

    EditHotkeySettings* EditHotkeySettings::GetSingleton() noexcept
    {
        static EditHotkeySettings singleton;
        return std::addressof(singleton);
    }

    EditHotkeySettingsResult EditHotkeySettings::LoadFromFile(
        const std::filesystem::path& a_path)
    {
        std::scoped_lock lock{ mutex_ };
        storagePath_ = a_path;
        loaded_ = true;
        editHotkey_.store(kDefaultEditHotkey, std::memory_order_release);
        debugMode_.store(false, std::memory_order_release);

        try {
            std::error_code existenceError;
            const auto exists = std::filesystem::exists(a_path, existenceError);
            if (existenceError) {
                return Failure("hotkey settings existence check failed: " +
                               existenceError.message());
            }
            if (!exists) {
                return { true, {} };
            }

            std::ifstream stream{ a_path, std::ios::binary };
            if (!stream.is_open()) {
                return Failure("hotkey settings file could not be opened");
            }
            Json document;
            stream >> document;
            if (!document.is_object()) {
                return Failure("hotkey settings top level must be an object");
            }
            const auto iterator = document.find("editHotkey");
            if (iterator == document.end() || !iterator->is_number_integer()) {
                return Failure("editHotkey must be an integer");
            }
            const auto value = iterator->get<std::int64_t>();
            if (value < 0 || value > std::numeric_limits<std::uint32_t>::max() ||
                !IsValidEditHotkey(static_cast<std::uint32_t>(value))) {
                return Failure("editHotkey is not an assignable keyboard key");
            }
            const auto debugIterator = document.find("debugMode");
            if (debugIterator != document.end() && !debugIterator->is_boolean()) {
                return Failure("debugMode must be a boolean");
            }
            editHotkey_.store(static_cast<std::uint32_t>(value), std::memory_order_release);
            debugMode_.store(
                debugIterator != document.end() && debugIterator->get<bool>(),
                std::memory_order_release);
            return { true, {} };
        } catch (const std::exception& exception) {
            return Failure(std::string{ "hotkey settings could not be loaded: " } +
                           exception.what());
        }
    }

    EditHotkeySettingsResult EditHotkeySettings::SetEditHotkey(std::uint32_t a_keyCode)
    {
        if (!IsValidEditHotkey(a_keyCode)) {
            return Failure("key cannot be assigned to preset editing");
        }

        std::scoped_lock lock{ mutex_ };
        if (!loaded_ || storagePath_.empty()) {
            return Failure("hotkey settings are not ready");
        }
        try {
            ReplaceFileTransactionally(
                storagePath_,
                a_keyCode,
                debugMode_.load(std::memory_order_acquire));
            editHotkey_.store(a_keyCode, std::memory_order_release);
            return { true, {} };
        } catch (const std::exception& exception) {
            return Failure(std::string{ "hotkey settings could not be saved: " } +
                           exception.what());
        }
    }

    EditHotkeySettingsResult EditHotkeySettings::SetDebugMode(bool a_enabled)
    {
        std::scoped_lock lock{ mutex_ };
        if (!loaded_ || storagePath_.empty()) {
            return Failure("settings are not ready");
        }
        try {
            ReplaceFileTransactionally(
                storagePath_,
                editHotkey_.load(std::memory_order_acquire),
                a_enabled);
            debugMode_.store(a_enabled, std::memory_order_release);
            return { true, {} };
        } catch (const std::exception& exception) {
            return Failure(std::string{ "debug mode could not be saved: " } +
                           exception.what());
        }
    }

    std::uint32_t EditHotkeySettings::EditHotkey() const noexcept
    {
        return editHotkey_.load(std::memory_order_acquire);
    }

    bool EditHotkeySettings::DebugMode() const noexcept
    {
        return debugMode_.load(std::memory_order_acquire);
    }
}
