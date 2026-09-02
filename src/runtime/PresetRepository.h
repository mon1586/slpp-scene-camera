#pragma once

#include "runtime/IPresetProvider.h"

#include <atomic>
#include <filesystem>
#include <string>

namespace ssc::runtime
{
    struct PresetLoadResult
    {
        bool succeeded{ false };
        std::size_t presetCount{ 0 };
        std::string error;
    };

    class PresetRepository final : public IPresetProvider
    {
    public:
        PresetRepository() = default;

        static PresetRepository* GetSingleton() noexcept;

        [[nodiscard]] PresetLoadResult LoadFromFile(const std::filesystem::path& a_path);
        [[nodiscard]] std::shared_ptr<const CameraPresetSnapshot> Snapshot() const noexcept override;

    private:
        std::atomic<std::shared_ptr<const CameraPresetSnapshot>> snapshot_{
            std::make_shared<const CameraPresetSnapshot>() };
    };
}
