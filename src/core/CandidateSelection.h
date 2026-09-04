#pragma once

#include "core/VisibilityEvaluation.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ssc::core
{
    class CandidateSelector
    {
    public:
        [[nodiscard]] std::optional<std::string> Step(
            std::span<const CameraCandidateVisibility> a_candidates,
            std::string_view a_currentPresetID,
            int a_direction) const;
    };
}
