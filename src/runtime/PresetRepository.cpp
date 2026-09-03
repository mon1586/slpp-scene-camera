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
#include <numbers>
#include <unordered_set>

namespace ssc::runtime
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr double kDegreesToRadians = std::numbers::pi_v<double> / 180.0;
        constexpr double kRadiansToDegrees = 180.0 / std::numbers::pi_v<double>;

        struct LegacyOffset
        {
            float right;
            float forward;
            float up;
        };

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

        [[nodiscard]] LegacyOffset ParseLegacyOffset(
            const Json& a_value,
            std::string_view a_context)
        {
            if (!a_value.is_object()) {
                throw std::runtime_error(std::string{ a_context } + " must be an object");
            }
            return {
                ReadFiniteFloat(
                    RequireMember(a_value, "right", a_context),
                    std::string{ a_context } + ".right"),
                ReadFiniteFloat(
                    RequireMember(a_value, "forward", a_context),
                    std::string{ a_context } + ".forward"),
                ReadFiniteFloat(
                    RequireMember(a_value, "up", a_context),
                    std::string{ a_context } + ".up"),
            };
        }

        [[nodiscard]] PresetFramingOffset ParseFramingOffset(
            const Json& a_value,
            std::string_view a_context)
        {
            if (!a_value.is_object()) {
                throw std::runtime_error(std::string{ a_context } + " must be an object");
            }
            return {
                ReadFiniteFloat(
                    RequireMember(a_value, "right", a_context),
                    std::string{ a_context } + ".right"),
                ReadFiniteFloat(
                    RequireMember(a_value, "up", a_context),
                    std::string{ a_context } + ".up"),
            };
        }

        [[nodiscard]] PresetOrbit ParseOrbit(
            const Json& a_value,
            std::string_view a_context)
        {
            if (!a_value.is_object()) {
                throw std::runtime_error(std::string{ a_context } + " must be an object");
            }
            return {
                ReadFiniteFloat(
                    RequireMember(a_value, "yawDegrees", a_context),
                    std::string{ a_context } + ".yawDegrees"),
                ReadFiniteFloat(
                    RequireMember(a_value, "pitchDegrees", a_context),
                    std::string{ a_context } + ".pitchDegrees"),
                ReadFiniteFloat(
                    RequireMember(a_value, "distance", a_context),
                    std::string{ a_context } + ".distance"),
            };
        }

        [[nodiscard]] PresetTransform MigrateVersion1Offset(const LegacyOffset& a_offset)
        {
            const auto right = static_cast<double>(a_offset.right);
            const auto forward = static_cast<double>(a_offset.forward);
            const auto up = static_cast<double>(a_offset.up);
            const auto distance = std::hypot(right, forward, up);
            if (!std::isfinite(distance) || distance <= 1.0e-6) {
                throw std::runtime_error("legacy offset must keep the camera away from the anchor");
            }
            const auto yaw = std::atan2(right, -forward) * kRadiansToDegrees;
            const auto pitch = std::asin(std::clamp(up / distance, -1.0, 1.0)) *
                               kRadiansToDegrees;
            return {
                {},
                {
                    static_cast<float>(yaw),
                    static_cast<float>(pitch),
                    static_cast<float>(distance),
                },
            };
        }

        [[nodiscard]] PresetTransform MigrateVersion2Transform(
            const LegacyOffset& a_pivotOffset,
            const PresetOrbit& a_orbit)
        {
            const auto yaw = static_cast<double>(a_orbit.yawDegrees) * kDegreesToRadians;
            const auto pitch = static_cast<double>(a_orbit.pitchDegrees) * kDegreesToRadians;
            const auto sineYaw = std::sin(yaw);
            const auto cosineYaw = std::cos(yaw);
            const auto sinePitch = std::sin(pitch);
            const auto cosinePitch = std::cos(pitch);
            const auto right = static_cast<double>(a_pivotOffset.right);
            const auto forward = static_cast<double>(a_pivotOffset.forward);
            const auto up = static_cast<double>(a_pivotOffset.up);

            const auto panRight = right * cosineYaw + forward * sineYaw;
            const auto panUp = -right * sineYaw * sinePitch +
                               forward * cosineYaw * sinePitch +
                               up * cosinePitch;
            const auto pivotDepth = -right * sineYaw * cosinePitch +
                                    forward * cosineYaw * cosinePitch -
                                    up * sinePitch;
            const auto distance = static_cast<double>(a_orbit.distance) - pivotDepth;
            if (std::isfinite(distance) && distance > 1.0e-6) {
                return {
                    {
                        static_cast<float>(panRight),
                        static_cast<float>(panUp),
                    },
                    {
                        a_orbit.yawDegrees,
                        a_orbit.pitchDegrees,
                        static_cast<float>(distance),
                    },
                };
            }

            // A version 2 target behind its camera cannot retain both position and
            // viewing direction without a signed distance. Preserve its position
            // and fall back to looking at the scene anchor.
            const auto viewRight = -sineYaw * cosinePitch;
            const auto viewForward = cosineYaw * cosinePitch;
            const auto viewUp = -sinePitch;
            return MigrateVersion1Offset({
                static_cast<float>(right - viewRight * a_orbit.distance),
                static_cast<float>(forward - viewForward * a_orbit.distance),
                static_cast<float>(up - viewUp * a_orbit.distance),
            });
        }

        [[nodiscard]] CameraPresetSnapshot ParseSnapshot(std::istream& a_stream)
        {
            Json document;
            a_stream >> document;
            if (!document.is_object()) {
                throw std::runtime_error("top level must be an object");
            }

            const auto& schemaVersion = RequireMember(document, "schemaVersion", "top level");
            if (!schemaVersion.is_number_integer()) {
                throw std::runtime_error("schemaVersion must be an integer");
            }
            const auto version = schemaVersion.get<std::int64_t>();
            if (version != 1 && version != 2 && version != 3) {
                throw std::runtime_error("schemaVersion must be integer 1, 2, or 3");
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

                PresetTransform transform;
                if (version == 1) {
                    transform = MigrateVersion1Offset(ParseLegacyOffset(
                        RequireMember(preset, "offset", context),
                        context + ".offset"));
                } else if (version == 2) {
                    const auto pivotOffset = ParseLegacyOffset(
                        RequireMember(preset, "pivotOffset", context),
                        context + ".pivotOffset");
                    const auto orbit = ParseOrbit(
                        RequireMember(preset, "orbit", context),
                        context + ".orbit");
                    const CameraPreset legacyPreset{
                        id,
                        { {}, orbit },
                    };
                    if (const auto error = ValidateCameraPreset(legacyPreset); !error.empty()) {
                        throw std::runtime_error(context + "." + error);
                    }
                    transform = MigrateVersion2Transform(pivotOffset, orbit);
                } else {
                    transform.framingOffset = ParseFramingOffset(
                        RequireMember(preset, "framingOffset", context),
                        context + ".framingOffset");
                    transform.orbit = ParseOrbit(
                        RequireMember(preset, "orbit", context),
                        context + ".orbit");
                }

                CameraPreset parsedPreset{ std::move(id), transform };
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
                    { "framingOffset", {
                        { "right", preset.transform.framingOffset.right },
                        { "up", preset.transform.framingOffset.up },
                    } },
                    { "orbit", {
                        { "yawDegrees", preset.transform.orbit.yawDegrees },
                        { "pitchDegrees", preset.transform.orbit.pitchDegrees },
                        { "distance", preset.transform.orbit.distance },
                    } },
                });
            }
            return Json{
                { "schemaVersion", 3 },
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

        void BackupUnreadableFile(const std::filesystem::path& a_path)
        {
            std::error_code error;
            if (!std::filesystem::exists(a_path, error)) {
                if (error) {
                    throw std::runtime_error(
                        "preset file existence check failed: " + error.message());
                }
                return;
            }

            auto backupPath = a_path;
            backupPath += ".invalid.bak";
            for (std::uint32_t suffix = 2; std::filesystem::exists(backupPath); ++suffix) {
                backupPath = a_path;
                backupPath += ".invalid-" + std::to_string(suffix) + ".bak";
            }
            std::filesystem::copy_file(a_path, backupPath, error);
            if (error) {
                throw std::runtime_error(
                    "unreadable preset file could not be backed up: " + error.message());
            }
        }
    }

    std::string ValidatePresetTransform(const PresetTransform& a_transform)
    {
        constexpr double kMinimumCameraDistance = 1.0e-6;
        const auto& framing = a_transform.framingOffset;
        const auto& orbit = a_transform.orbit;
        if (!std::isfinite(framing.right) || !std::isfinite(framing.up)) {
            return "preset framing offsets must be finite";
        }
        if (!std::isfinite(orbit.yawDegrees) || !std::isfinite(orbit.pitchDegrees) ||
            !std::isfinite(orbit.distance)) {
            return "preset orbit values must be finite";
        }
        if (orbit.yawDegrees < -180.0F || orbit.yawDegrees > 180.0F) {
            return "preset yaw must be between -180 and 180 degrees";
        }
        if (orbit.pitchDegrees < -90.0F || orbit.pitchDegrees > 90.0F) {
            return "preset pitch must be between -90 and 90 degrees";
        }
        if (orbit.distance <= kMinimumCameraDistance) {
            return "preset orbit distance must be greater than zero";
        }
        return {};
    }

    std::string ValidateCameraPreset(const CameraPreset& a_preset)
    {
        if (a_preset.id.empty()) {
            return "preset id must not be empty";
        }
        return ValidatePresetTransform(a_preset.transform);
    }

    PresetRepository* PresetRepository::GetSingleton() noexcept
    {
        static PresetRepository singleton;
        return std::addressof(singleton);
    }

    PresetLoadResult PresetRepository::LoadFromFile(const std::filesystem::path& a_path)
    {
        try {
            std::error_code existsError;
            const auto exists = std::filesystem::exists(a_path, existsError);
            if (existsError) {
                throw std::runtime_error(
                    "preset file existence check failed: " + existsError.message());
            }
            if (!exists) {
                std::scoped_lock lock{ mutex_ };
                storagePath_ = a_path;
                persistedSnapshot_.clear();
                loaded_ = true;
                backupBeforeNextWrite_ = false;
                snapshot_.store(
                    std::make_shared<const CameraPresetSnapshot>(),
                    std::memory_order_release);
                return { true, 0, {} };
            }
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
            backupBeforeNextWrite_ = false;
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(persistedSnapshot_),
                std::memory_order_release);
            return { true, count, {} };
        } catch (const std::exception& exception) {
            std::scoped_lock lock{ mutex_ };
            storagePath_ = a_path;
            persistedSnapshot_.clear();
            loaded_ = false;
            std::error_code ignored;
            backupBeforeNextWrite_ = std::filesystem::exists(a_path, ignored);
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(),
                std::memory_order_release);
            return { false, 0, exception.what() };
        } catch (...) {
            std::scoped_lock lock{ mutex_ };
            storagePath_ = a_path;
            persistedSnapshot_.clear();
            loaded_ = false;
            std::error_code ignored;
            backupBeforeNextWrite_ = std::filesystem::exists(a_path, ignored);
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
            backupBeforeNextWrite_ = false;
            snapshot_.store(
                std::make_shared<const CameraPresetSnapshot>(persistedSnapshot_),
                std::memory_order_release);
            return { true, count, {} };
        } catch (const std::exception& exception) {
            std::scoped_lock lock{ mutex_ };
            std::error_code ignored;
            backupBeforeNextWrite_ = std::filesystem::exists(path, ignored);
            return { false, 0, exception.what() };
        } catch (...) {
            std::scoped_lock lock{ mutex_ };
            std::error_code ignored;
            backupBeforeNextWrite_ = std::filesystem::exists(path, ignored);
            return { false, 0, "unknown loader failure" };
        }
    }

    PresetOperationResult PresetRepository::Create(const CameraPreset& a_preset)
    {
        if (const auto error = ValidateCameraPreset(a_preset); !error.empty()) {
            return Failure(error);
        }

        std::scoped_lock lock{ mutex_ };
        if (storagePath_.empty()) {
            return Failure("repository has no storage path");
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
        const PresetTransform& a_transform)
    {
        CameraPreset candidate{ std::string{ a_id }, a_transform };
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
        iterator->transform = a_transform;
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
            if (backupBeforeNextWrite_) {
                BackupUnreadableFile(storagePath_);
                backupBeforeNextWrite_ = false;
            }
            ReplaceFileTransactionally(storagePath_, a_snapshot);
            persistedSnapshot_ = std::move(a_snapshot);
            loaded_ = true;
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
