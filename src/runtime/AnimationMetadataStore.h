#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace ssc::runtime
{
    struct AnimationMetadata
    {
        std::uint64_t revision{ 0 };
        bool known{ false };
        std::string id;
        std::string name;
        std::vector<std::string> tags;
    };

    struct AnimationMetadataStore
    {
        // Protected by SelectionBoundary; published with the matching candidates.
        std::shared_ptr<const AnimationMetadata> current;
    };
}
