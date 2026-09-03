#include "runtime/PresetRepository.h"

#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace ssc::runtime
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] PresetOperationResult Failure(std::string a_error)
        {
            return { false, std::move(a_error) };
        }

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

                CameraPreset parsedPreset{
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
                    }
                };
                if (const auto error = ValidateCameraPreset(parsedPreset); !error.empty()) {
                    throw std::runtime_error(context + "." + error);
                }
                snapshot.push_back(std::move(parsedPreset));
            }
            return snapshot;
        }

        [[nodiscard]] Json SerializeSnapshot(const CameraPresetSnapshot& a_snapshot)
        {
            Json presets = Json::array();
            for (const auto& preset : a_snapshot) {
                presets.push_back({
                    { "id", preset.id },
                    { "offset", {
                        { "right", preset.offset.right },
                        { "forward", preset.offset.forward },
                        { "up", preset.offset.up },
                    } },
                });
            }
            return Json{
                { "schemaVersion", 1 },
                { "presets", std::move(presets) },
            };
        }

        void ReplaceFileTransactionally(
            const std::filesystem::path& a_path,
            const CameraPresetSnapshot& a_snapshot)
        {
            static std::atomic_uint64_t counter{ 0 };
            auto temporaryPath = a_path;
            temporaryPath += ".tmp-" + std::to_string(::GetCurrentProcessId()) + "-" +
                             std::to_string(counter.fetch_add(1, std::memory_order_relaxed));

            try {
                std::ofstream stream{ temporaryPath, std::ios::binary | std::ios::trunc };
                if (!stream.is_open()) {
                    throw std::runtime_error("temporary preset file could not be opened");
                }
                stream << SerializeSnapshot(a_snapshot).dump(2) << '\n';
                stream.flush();
                if (!stream.good()) {
                    throw std::runtime_error("temporary preset file could not be written");
                }
                stream.close();
                if (!stream.good()) {
                    throw std::runtime_error("temporary preset file could not be closed");
                }

                if (!::MoveFileExW(
                        temporaryPath.c_str(),
                        a_path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    const std::error_code error{
                        static_cast<int>(::GetLastError()),
                        std::system_category() };
                    throw std::runtime_error(
                        "temporary preset file could not replace the destination: " +
                        error.message());
                }
            } catch (...) {
                std::error_code ignored;
                static_cast<void>(std::filesystem::remove(temporaryPath, ignored));
                throw;
            }
        }
    }

    std::string ValidateCameraPreset(const CameraPreset& a_preset)
    {
        constexpr double kMinimumCameraDistance = 1.0e-6;
        if (a_preset.id.empty()) {
            return "preset id must not be empty";
        }
        const auto& offset = a_preset.offset;
        if (!std::isfinite(offset.right) || !std::isfinite(offset.forward) ||
            !std::isfinite(offset.up)) {
            return "preset offsets must be finite";
        }
        if (std::hypot(
                static_cast<double>(offset.right),
                static_cast<double>(offset.forward),
                static_cast<double>(offset.up)) <= kMinimumCameraDistance) {
            return "preset offset must keep the camera away from the scene anchor";
        }
        return {};
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
            const auto count = parsed.size();
            std::scoped_lock lock{ mutex_ };
            storagePath_ = a_path;
            persistedSnapshot_ = std::move(parsed);
            loaded_ = true;
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(persistedSnapshot_),
                std::memory_order_release);
            return { true, count, {} };
        } catch (const std::exception& exception) {
            std::scoped_lock lock{ mutex_ };
            storagePath_ = a_path;
            persistedSnapshot_.clear();
            loaded_ = false;
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(),
                std::memory_order_release);
            return { false, 0, exception.what() };
        } catch (...) {
            std::scoped_lock lock{ mutex_ };
            storagePath_ = a_path;
            persistedSnapshot_.clear();
            loaded_ = false;
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(),
                std::memory_order_release);
            return { false, 0, "unknown loader failure" };
        }
    }

    PresetLoadResult PresetRepository::Reload()
    {
        std::filesystem::path path;
        {
            std::scoped_lock lock{ mutex_ };
            path = storagePath_;
        }
        if (path.empty()) {
            return { false, 0, "repository has no storage path" };
        }

        try {
            std::ifstream stream{ path, std::ios::binary };
            if (!stream.is_open()) {
                throw std::runtime_error("file could not be opened");
            }

            auto parsed = ParseSnapshot(stream);
            const auto count = parsed.size();
            std::scoped_lock lock{ mutex_ };
            persistedSnapshot_ = std::move(parsed);
            loaded_ = true;
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(persistedSnapshot_),
                std::memory_order_release);
            return { true, count, {} };
        } catch (const std::exception& exception) {
            return { false, 0, exception.what() };
        } catch (...) {
            return { false, 0, "unknown loader failure" };
        }
    }

    PresetOperationResult PresetRepository::Create(const CameraPreset& a_preset)
    {
        if (const auto error = ValidateCameraPreset(a_preset); !error.empty()) {
            return Failure(error);
        }

        std::scoped_lock lock{ mutex_ };
        if (!loaded_) {
            return Failure("repository is not loaded");
        }
        if (std::ranges::any_of(persistedSnapshot_, [&](const auto& a_existing) {
                return a_existing.id == a_preset.id;
            })) {
            return Failure("preset id already exists");
        }

        auto next = persistedSnapshot_;
        next.push_back(a_preset);
        return PersistAndPublishLocked(std::move(next));
    }

    PresetOperationResult PresetRepository::Update(
        std::string_view a_id,
        const PresetOffset& a_offset)
    {
        CameraPreset candidate{ std::string{ a_id }, a_offset };
        if (const auto error = ValidateCameraPreset(candidate); !error.empty()) {
            return Failure(error);
        }

        std::scoped_lock lock{ mutex_ };
        if (!loaded_) {
            return Failure("repository is not loaded");
        }
        auto next = persistedSnapshot_;
        const auto iterator = std::ranges::find(next, a_id, &CameraPreset::id);
        if (iterator == next.end()) {
            return Failure("preset id was not found");
        }
        iterator->offset = a_offset;
        return PersistAndPublishLocked(std::move(next));
    }

    PresetOperationResult PresetRepository::Delete(std::string_view a_id)
    {
        std::scoped_lock lock{ mutex_ };
        if (!loaded_) {
            return Failure("repository is not loaded");
        }
        auto next = persistedSnapshot_;
        const auto iterator = std::ranges::find(next, a_id, &CameraPreset::id);
        if (iterator == next.end()) {
            return Failure("preset id was not found");
        }
        next.erase(iterator);
        return PersistAndPublishLocked(std::move(next));
    }

    PresetOperationResult PresetRepository::PersistAndPublishLocked(
        CameraPresetSnapshot a_snapshot)
    {
        try {
            ReplaceFileTransactionally(storagePath_, a_snapshot);
            persistedSnapshot_ = std::move(a_snapshot);
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(persistedSnapshot_),
                std::memory_order_release);
            return { true, {} };
        } catch (const std::exception& exception) {
            return Failure(exception.what());
        } catch (...) {
            return Failure("unknown preset persistence failure");
        }
    }

    std::shared_ptr<const CameraPresetSnapshot> PresetRepository::Snapshot() const noexcept
    {
        return snapshot_.load(std::memory_order_acquire);
    }
}
