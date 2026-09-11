#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ssc::runtime
{
    struct PresetFramingOffset
    {
        float right{ 0.0F };
        float up{ 0.0F };
    };

    struct PresetOrbit
    {
        float yawDegrees{ 0.0F };
        float pitchDegrees{ 0.0F };
        float distance{ 0.0F };
    };

    struct PresetTransform
    {
        PresetFramingOffset framingOffset;
        PresetOrbit orbit;
        float fovOffsetDegrees{ 0.0F };
    };

    struct CameraPreset
    {
        std::string id;
        PresetTransform transform;
        std::string name;
    };

    using CameraPresetSnapshot = std::vector<CameraPreset>;

    class IPresetProvider
    {
    public:
        virtual ~IPresetProvider() = default;

        [[nodiscard]] virtual std::shared_ptr<const CameraPresetSnapshot> Snapshot() const noexcept = 0;
    };
}
