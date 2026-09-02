#include "runtime/PresetRepository.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace ssc::runtime
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] float ReadFiniteFloat(const Json& a_value, std::string_view a_field)
        {
            if (!a_value.is_number()) {
                throw std::runtime_error(std::string{ a_field } + " must be a number");
            }

            const auto value = a_value.get<double>();
            constexpr auto maximum = static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(value) || value < -maximum || value > maximum) {
                throw std::runtime_error(std::string{ a_field } + " must be a finite float");
            }
            return static_cast<float>(value);
        }

        [[nodiscard]] const Json& RequireMember(
            const Json& a_object,
            std::string_view a_name,
            std::string_view a_context)
        {
            const auto iterator = a_object.find(a_name);
            if (iterator == a_object.end()) {
                throw std::runtime_error(
                    std::string{ a_context } + " is missing required member '" +
                    std::string{ a_name } + "'");
            }
            return *iterator;
        }

        [[nodiscard]] CameraPresetSnapshot ParseSnapshot(std::istream& a_stream)
        {
            Json document;
            a_stream >> document;
            if (!document.is_object()) {
                throw std::runtime_error("top level must be an object");
            }

            const auto& schemaVersion = RequireMember(document, "schemaVersion", "top level");
            if (!schemaVersion.is_number_integer() || schemaVersion.get<std::int64_t>() != 1) {
                throw std::runtime_error("schemaVersion must be integer 1");
            }

            const auto& presets = RequireMember(document, "presets", "top level");
            if (!presets.is_array()) {
                throw std::runtime_error("presets must be an array");
            }

            CameraPresetSnapshot snapshot;
            snapshot.reserve(presets.size());
            std::unordered_set<std::string> ids;
            ids.reserve(presets.size());
            for (std::size_t index = 0; index < presets.size(); ++index) {
                const auto& preset = presets[index];
                const auto context = "presets[" + std::to_string(index) + "]";
                if (!preset.is_object()) {
                    throw std::runtime_error(context + " must be an object");
                }

                const auto& idValue = RequireMember(preset, "id", context);
                if (!idValue.is_string()) {
                    throw std::runtime_error(context + ".id must be a string");
                }
                auto id = idValue.get<std::string>();
                if (id.empty()) {
                    throw std::runtime_error(context + ".id must not be empty");
                }
                if (!ids.emplace(id).second) {
                    throw std::runtime_error("duplicate preset id '" + id + "'");
                }

                const auto& offset = RequireMember(preset, "offset", context);
                if (!offset.is_object()) {
                    throw std::runtime_error(context + ".offset must be an object");
                }

                snapshot.push_back({
                    std::move(id),
                    {
                        ReadFiniteFloat(
                            RequireMember(offset, "right", context + ".offset"),
                            context + ".offset.right"),
                        ReadFiniteFloat(
                            RequireMember(offset, "forward", context + ".offset"),
                            context + ".offset.forward"),
                        ReadFiniteFloat(
                            RequireMember(offset, "up", context + ".offset"),
                            context + ".offset.up"),
                    },
                });
            }
            return snapshot;
        }
    }

    PresetRepository* PresetRepository::GetSingleton() noexcept
    {
        static PresetRepository singleton;
        return std::addressof(singleton);
    }

    PresetLoadResult PresetRepository::LoadFromFile(const std::filesystem::path& a_path)
    {
        try {
            std::ifstream stream{ a_path, std::ios::binary };
            if (!stream.is_open()) {
                throw std::runtime_error("file could not be opened");
            }

            auto parsed = ParseSnapshot(stream);
            auto nextSnapshot = std::make_shared<const CameraPresetSnapshot>(std::move(parsed));
            const auto count = nextSnapshot->size();
            snapshot_.store(std::move(nextSnapshot), std::memory_order_release);
            return { true, count, {} };
        } catch (const std::exception& exception) {
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(),
                std::memory_order_release);
            return { false, 0, exception.what() };
        } catch (...) {
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(),
                std::memory_order_release);
            return { false, 0, "unknown loader failure" };
        }
    }

    std::shared_ptr<const CameraPresetSnapshot> PresetRepository::Snapshot() const noexcept
    {
        return snapshot_.load(std::memory_order_acquire);
    }
}
