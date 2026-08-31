#pragma once

#include "runtime/SceneEvent.h"

namespace ssc::runtime
{
    class IRuntimeClient
    {
    public:
        virtual ~IRuntimeClient() = default;

        [[nodiscard]] virtual bool NeedsUpdate() const noexcept = 0;
        virtual void HandleSceneEvent(const SceneEvent& a_event) = 0;
        virtual void Update() = 0;
        virtual void Reset(std::string_view a_reason) = 0;
        virtual void RequestReset() noexcept = 0;
        virtual void EmergencyReset() noexcept = 0;
    };
}
