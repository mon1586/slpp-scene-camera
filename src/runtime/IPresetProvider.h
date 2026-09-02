#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ssc::runtime
{
    struct PresetOffset
    {
        float right{ 0.0F };
        float forward{ 0.0F };
        float up{ 0.0F };
    };

    struct CameraPreset
    {
        std::string id;
        PresetOffset offset;
    };

    using CameraPresetSnapshot = std::vector<CameraPreset>;

    class IPresetProvider
    {
    public:
        virtual ~IPresetProvider() = default;

        [[nodiscard]] virtual std::shared_ptr<const CameraPresetSnapshot> Snapshot() const noexcept = 0;
    };
}
