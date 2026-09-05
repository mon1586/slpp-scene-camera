#include "core/CandidateSelection.h"

#include <algorithm>

namespace ssc::core
{
    std::optional<std::string> CandidateSelector::SelectInitial(
        std::span<const CameraCandidateVisibility> a_candidates,
        std::string_view a_preferredPresetID) const
    {
        if (!a_preferredPresetID.empty()) {
            const auto preferred = std::ranges::find_if(
                a_candidates,
                [&](const auto& a_candidate) {
                    return a_candidate.presetID == a_preferredPresetID;
                });
            if (preferred != a_candidates.end()) {
                return preferred->presetID;
            }
        }

        const auto firstUsable = std::ranges::find_if(
            a_candidates,
            [](const auto& a_candidate) { return a_candidate.usable; });
        return firstUsable != a_candidates.end() ?
            std::optional{ firstUsable->presetID } : std::nullopt;
    }

    std::optional<std::string> CandidateSelector::Step(
        std::span<const CameraCandidateVisibility> a_candidates,
        std::string_view a_currentPresetID,
        int a_direction) const
    {
        if (a_candidates.empty() || a_direction == 0) {
            return std::nullopt;
        }

        std::size_t currentIndex = 0;
        for (std::size_t index = 0; index < a_candidates.size(); ++index) {
            if (a_candidates[index].presetID == a_currentPresetID) {
                currentIndex = index;
                break;
            }
        }

        const auto count = a_candidates.size();
        for (std::size_t offset = 1; offset <= count; ++offset) {
            const auto index = a_direction > 0 ?
                (currentIndex + offset) % count :
                (currentIndex + count - (offset % count)) % count;
            if (a_candidates[index].usable) {
                return a_candidates[index].presetID;
            }
        }
        return std::nullopt;
    }
}
