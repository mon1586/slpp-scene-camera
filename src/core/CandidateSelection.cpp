#include "core/CandidateSelection.h"

namespace ssc::core
{
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
