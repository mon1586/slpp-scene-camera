#pragma once

#include "runtime/IPresetProvider.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace ssc::runtime
{
    struct PresetLoadResult
    {
        bool succeeded{ false };
        std::size_t presetCount{ 0 };
        std::string error;
    };

    struct PresetOperationResult
    {
        bool succeeded{ false };
        std::string error;
    };

    [[nodiscard]] std::string ValidateCameraPreset(const CameraPreset& a_preset);

    class PresetRepository final : public IPresetProvider
    {
    public:
        PresetRepository() = default;

        static PresetRepository* GetSingleton() noexcept;

        [[nodiscard]] PresetLoadResult LoadFromFile(const std::filesystem::path& a_path);
        [[nodiscard]] PresetLoadResult Reload();
        [[nodiscard]] PresetOperationResult Create(const CameraPreset& a_preset);
        [[nodiscard]] PresetOperationResult Update(
            std::string_view a_id,
            const PresetOffset& a_offset);
        [[nodiscard]] PresetOperationResult Delete(std::string_view a_id);
        [[nodiscard]] std::shared_ptr<const CameraPresetSnapshot> Snapshot() const noexcept override;

    private:
        [[nodiscard]] PresetOperationResult PersistAndPublishLocked(
            CameraPresetSnapshot a_snapshot);

        std::mutex mutex_;
        std::filesystem::path storagePath_;
        CameraPresetSnapshot persistedSnapshot_;
        bool loaded_{ false };
        std::atomic<std::shared_ptr<const CameraPresetSnapshot>> snapshot_{
            std::make_shared<const CameraPresetSnapshot>() };
    };
}
