#include "runtime/SexLabPSceneSource.h"
#include "runtime/SceneParticipantSnapshotStorage.h"

namespace ssc::runtime
{
    namespace
    {
        const RE::BSFixedString kPelvisNodeName{ "NPC Pelvis [Pelv]" };

        [[nodiscard]] Vec3 ToRuntime(const RE::NiPoint3& a_point) noexcept
        {
            return { a_point.x, a_point.y, a_point.z };
        }

        [[nodiscard]] Vec3 ForwardFromYaw(float a_yaw) noexcept
        {
            // Skyrim actors use +Y as their zero-yaw forward direction.
            return { std::sin(a_yaw), std::cos(a_yaw), 0.0F };
        }

        [[nodiscard]] std::optional<SceneEventType> ParseEvent(std::string_view a_name) noexcept
        {
            if (a_name == "AnimationStarting"sv) {
                return SceneEventType::kAnimationStarting;
            }
            if (a_name == "AnimationStart"sv) {
                return SceneEventType::kAnimationStart;
            }
            if (a_name == "AnimationChange"sv) {
                return SceneEventType::kAnimationChange;
            }
            if (a_name == "AnimationEnding"sv) {
                return SceneEventType::kAnimationEnding;
            }
            if (a_name == "AnimationEnd"sv) {
                return SceneEventType::kAnimationEnd;
            }
            if (a_name == "HookAnimationStart"sv) {
                return SceneEventType::kAnimationStart;
            }
            if (a_name == "HookAnimationChange"sv) {
                return SceneEventType::kAnimationChange;
            }
            if (a_name == "HookAnimationEnd"sv) {
                return SceneEventType::kAnimationEnd;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::int32_t> ParseInstanceID(
            const SKSE::ModCallbackEvent& a_event) noexcept
        {
            // SexLab P+ calls SendModEvent(HookEvent, thread_id). SKSE's second
            // parameter is strArg, so Papyrus coerces the integer thread ID to text.
            if (const auto* text = a_event.strArg.c_str(); text && *text) {
                const std::string_view value{ text };
                std::int32_t parsed = 0;
                const auto [end, error] = std::from_chars(
                    value.data(), value.data() + value.size(), parsed);
                if (error == std::errc{} && end == value.data() + value.size()) {
                    return parsed;
                }
            }

            // Compatibility fallback for senders that put the ID in numArg.
            if (!std::isfinite(a_event.numArg)) {
                return std::nullopt;
            }
            constexpr auto minInstance = static_cast<float>(std::numeric_limits<std::int32_t>::min());
            constexpr auto maxInstance = static_cast<float>(std::numeric_limits<std::int32_t>::max());
            if (a_event.numArg < minInstance || a_event.numArg > maxInstance) {
                return std::nullopt;
            }
            return static_cast<std::int32_t>(std::lround(a_event.numArg));
        }

        [[nodiscard]] bool IsSexLabSender(const RE::TESForm* a_sender) noexcept
        {
            const auto* quest = a_sender ? skyrim_cast<const RE::TESQuest*>(a_sender) : nullptr;
            const auto* definingFile = quest ? quest->GetFile(0) : nullptr;
            return definingFile && definingFile->GetFilename() == "SexLab.esm"sv;
        }
    }

    SexLabPSceneSource* SexLabPSceneSource::GetSingleton() noexcept
    {
        static SexLabPSceneSource singleton;
        return std::addressof(singleton);
    }

    bool SexLabPSceneSource::Register(SceneEventHandler a_handler)
    {
        if (!a_handler) {
            logger::error("Cannot register SexLab P+ scene source without an event handler");
            return false;
        }
        if (registered_) {
            return handler_ == a_handler;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || !dataHandler->LookupModByName("SexLab.esm"sv)) {
            logger::warn("SexLab.esm is not loaded; scene source will not be registered");
            return false;
        }

        auto* source = SKSE::GetModCallbackEventSource();
        if (!source) {
            logger::error("SKSE ModCallbackEvent source is unavailable");
            return false;
        }

        handler_ = a_handler;
        source->AddEventSink(this);
        registered_ = true;
        logger::info("SexLab P+ scene source registered");
        return true;
    }

    SceneParticipantSnapshot SexLabPSceneSource::CollectParticipants(
        const SceneKey& a_key) const
    {
        SceneParticipantSnapshot result;
        auto storage = std::make_shared<SceneParticipantSnapshot::Storage>();
        result.storage_ = storage;
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_key.sourceID);
        if (!quest) {
            logger::warn("SexLab scene source {:08X} is not a live quest", a_key.sourceID);
            return result;
        }

        const auto* player = RE::PlayerCharacter::GetSingleton();
        const RE::BSReadLockGuard lock{ quest->aliasAccessLock };

        for (auto* baseAlias : quest->aliases) {
            auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(baseAlias);
            auto* actor = refAlias ? refAlias->GetActorReference() : nullptr;
            if (!actor) {
                continue;
            }

            auto handle = actor->GetHandle();
            if (!handle) {
                continue;
            }

            const auto isPlayer = actor == player;
            bool duplicate = false;
            for (std::size_t index = 0; index < result.count_; ++index) {
                if (storage->handles[index].native_handle() == handle.native_handle()) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                result.containsPlayer_ = result.containsPlayer_ || isPlayer;
                continue;
            }

            if (result.count_ < storage->handles.size()) {
                storage->handles[result.count_++] = handle;
                result.containsPlayer_ = result.containsPlayer_ || isPlayer;
            } else {
                result.truncated_ = true;
                if (isPlayer && !result.containsPlayer_) {
                    storage->handles.back() = handle;
                    result.containsPlayer_ = true;
                }
            }
        }

        return result;
    }

    std::optional<SceneAnchorSamples> SexLabPSceneSource::CollectAnchorInput(
        const SceneParticipantSnapshot& a_participants,
        std::span<Vec3> a_pelvisStorage) const
    {
        if (!a_participants.storage_ ||
            a_participants.count_ == 0 ||
            a_participants.count_ > a_pelvisStorage.size()) {
            return std::nullopt;
        }

        const auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return std::nullopt;
        }

        std::optional<Vec3> playerPelvis;
        for (std::size_t index = 0; index < a_participants.count_; ++index) {
            const auto actor = a_participants.storage_->handles[index].get();
            auto* root = actor ? actor->Get3D() : nullptr;
            auto* pelvis = root ? root->GetObjectByName(kPelvisNodeName) : nullptr;
            if (!pelvis) {
                logger::debug("Cannot capture scene anchor: participant {} has no Pelvis node", index);
                return std::nullopt;
            }

            const auto pelvisPosition = ToRuntime(pelvis->world.translate);
            a_pelvisStorage[index] = pelvisPosition;
            if (actor.get() == player) {
                playerPelvis = pelvisPosition;
            }
        }

        if (!playerPelvis) {
            return std::nullopt;
        }

        return SceneAnchorSamples{
            std::span<const Vec3>{ a_pelvisStorage.data(), a_participants.count_ },
            playerPelvis,
            ForwardFromYaw(player->GetAngleZ()),
        };
    }

    RE::BSEventNotifyControl SexLabPSceneSource::ProcessEvent(
        const SKSE::ModCallbackEvent* a_event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        try {
            const auto eventKind = ParseEvent(a_event->eventName.c_str());
            if (!eventKind) {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (!IsSexLabSender(a_event->sender)) {
                logger::debug("Ignoring shared ModCallbackEvent '{}': sender is not a SexLab.esm quest",
                    a_event->eventName.c_str());
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto sourceID = a_event->sender->GetFormID();
            logger::debug("SexLab callback '{}' source={:08X} strArg='{}' numArg={}",
                a_event->eventName.c_str(), sourceID, a_event->strArg.c_str(), a_event->numArg);

            const auto instanceID = ParseInstanceID(*a_event);
            if (!instanceID) {
                logger::warn("Ignoring SexLab event with invalid instance ID payload (strArg='{}', numArg={})",
                    a_event->strArg.c_str(), a_event->numArg);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (handler_) {
                handler_({ *eventKind, { sourceID, *instanceID }, {} });
            }
        } catch (const std::exception& exception) {
            try {
                logger::critical("SexLab P+ scene adapter failed: {}", exception.what());
            } catch (...) {
            }
        } catch (...) {
            try {
                logger::critical("SexLab P+ scene adapter failed with an unknown exception");
            } catch (...) {
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
