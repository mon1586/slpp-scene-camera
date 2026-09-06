#include "runtime/SexLabPSceneSource.h"
#include "runtime/PluginIdentity.h"
#include "runtime/SceneParticipantSnapshotStorage.h"

#include <numeric>

namespace ssc::runtime
{
    namespace
    {
        [[nodiscard]] Vec3 ToRuntime(const RE::NiPoint3& a_point) noexcept
        {
            return { a_point.x, a_point.y, a_point.z };
        }

        [[nodiscard]] Vec3 ForwardFromYaw(float a_yaw) noexcept
        {
            // Skyrim actors use +Y as their zero-yaw forward direction.
            return { std::sin(a_yaw), std::cos(a_yaw), 0.0F };
        }

        [[nodiscard]] bool IsFinite(const RE::NiPoint3& a_point) noexcept
        {
            return std::isfinite(a_point.x) &&
                   std::isfinite(a_point.y) &&
                   std::isfinite(a_point.z);
        }

        [[nodiscard]] std::optional<Vec3> TorsoCenter(const RE::Actor* a_actor) noexcept
        {
            auto* root = a_actor ? a_actor->Get3D() : nullptr;
            if (!root) {
                return std::nullopt;
            }

            static const RE::BSFixedString pelvisNodeName{ "NPC Pelvis [Pelv]" };
            static const RE::BSFixedString chestNodeName{ "NPC Spine2 [Spn2]" };
            const auto* pelvis = root->GetObjectByName(pelvisNodeName);
            const auto* chest = root->GetObjectByName(chestNodeName);
            if (!pelvis || !chest ||
                !IsFinite(pelvis->world.translate) || !IsFinite(chest->world.translate)) {
                return std::nullopt;
            }

            const auto& waistPosition = pelvis->world.translate;
            const auto& chestPosition = chest->world.translate;
            return Vec3{
                std::midpoint(waistPosition.x, chestPosition.x),
                std::midpoint(waistPosition.y, chestPosition.y),
                std::midpoint(waistPosition.z, chestPosition.z),
            };
        }

        [[nodiscard]] std::optional<Vec3> BodyCenter(const RE::Actor* a_actor) noexcept
        {
            auto* root = a_actor ? a_actor->Get3D() : nullptr;
            if (!root || !IsFinite(root->worldBound.center) ||
                !std::isfinite(root->worldBound.radius) || root->worldBound.radius <= 0.0F) {
                return std::nullopt;
            }
            return ToRuntime(root->worldBound.center);
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

        [[nodiscard]] const RE::TESQuest* AsQuest(const RE::TESForm* a_sender) noexcept
        {
            return a_sender ? skyrim_cast<const RE::TESQuest*>(a_sender) : nullptr;
        }

        [[nodiscard]] std::string_view DefiningFileName(const RE::TESQuest* a_quest) noexcept
        {
            const auto* definingFile = a_quest ? a_quest->GetFile(0) : nullptr;
            return definingFile ? definingFile->GetFilename() : "<none>"sv;
        }

        [[nodiscard]] bool LooksLikeSceneEvent(std::string_view a_name) noexcept
        {
            return a_name.find("Animation"sv) != std::string_view::npos;
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
                    if (isPlayer) {
                        result.playerIndex_ = index;
                    }
                    break;
                }
            }
            if (duplicate) {
                result.containsPlayer_ = result.containsPlayer_ || isPlayer;
                continue;
            }

            if (result.count_ < storage->handles.size()) {
                if (isPlayer) {
                    result.playerIndex_ = result.count_;
                }
                storage->handles[result.count_++] = handle;
                result.containsPlayer_ = result.containsPlayer_ || isPlayer;
            } else {
                result.truncated_ = true;
                if (isPlayer && !result.containsPlayer_) {
                    storage->handles.back() = handle;
                    result.playerIndex_ = storage->handles.size() - 1;
                    result.containsPlayer_ = true;
                }
            }
        }

        return result;
    }

    std::optional<SceneAnchorSamples> SexLabPSceneSource::CollectAnchorInput(
        const SceneParticipantSnapshot& a_participants) const
    {
        if (!a_participants.storage_ ||
            a_participants.count_ == 0 ||
            !a_participants.containsPlayer_ ||
            a_participants.playerIndex_ >= a_participants.count_) {
            return std::nullopt;
        }

        const auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return std::nullopt;
        }

        const auto actor =
            a_participants.storage_->handles[a_participants.playerIndex_].get();
        if (actor.get() != player) {
            return std::nullopt;
        }

        const auto bodyCenter = TorsoCenter(actor.get());
        if (!bodyCenter) {
            return std::nullopt;
        }
        return SceneAnchorSamples{
            *bodyCenter,
            ForwardFromYaw(player->GetAngleZ()),
        };
    }

    std::optional<SceneVisibilitySamples> SexLabPSceneSource::CollectVisibilityInput(
        const SceneParticipantSnapshot& a_participants,
        std::span<std::uint32_t> a_participantIDStorage,
        std::span<VisibilityTarget> a_targetStorage) const
    {
        const auto targetCount = a_participants.count_ *
            SceneVisibilitySamples::kPointsPerParticipant;
        if (!a_participants.storage_ ||
            a_participants.count_ == 0 ||
            a_participants.count_ > a_participantIDStorage.size() ||
            targetCount > a_targetStorage.size()) {
            return std::nullopt;
        }

        for (std::size_t participantIndex = 0;
             participantIndex < a_participants.count_;
            ++participantIndex) {
            const auto actor = a_participants.storage_->handles[participantIndex].get();
            const auto actorID = actor ? actor->GetFormID() : 0;
            a_participantIDStorage[participantIndex] = actorID;

            auto& target = a_targetStorage[participantIndex];
            target = {
                participantIndex,
                actorID,
                core::VisibilityPoint::kBodyCenter,
                BodyCenter(actor.get()),
            };
            if (!target.position) {
                logger::debug(
                    "Body center is unavailable for participant {:08X}",
                    actorID);
            }
        }

        return SceneVisibilitySamples{
            std::span<const std::uint32_t>{
                a_participantIDStorage.data(), a_participants.count_ },
            std::span<const VisibilityTarget>{ a_targetStorage.data(), targetCount },
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
            const auto* rawEventName = a_event->eventName.c_str();
            const auto eventName = rawEventName ? std::string_view{ rawEventName } : std::string_view{};
            const auto* quest = AsQuest(a_event->sender);
            const auto senderID = a_event->sender ? a_event->sender->GetFormID() : 0;
            const auto definingFile = DefiningFileName(quest);
            const auto* rawStrArg = a_event->strArg.c_str();
            const auto strArg = rawStrArg ? std::string_view{ rawStrArg } : std::string_view{};

            static std::atomic_bool firstCallbackObserved{ false };
            if (!firstCallbackObserved.exchange(true, std::memory_order_relaxed)) {
                logger::info(
                    "First SKSE ModCallbackEvent observed: event='{}' sender={:08X} file='{}'",
                    eventName,
                    senderID,
                    definingFile);
            }

            const auto eventKind = ParseEvent(eventName);
            if (!eventKind) {
                if (LooksLikeSceneEvent(eventName)) {
                    logger::warn(
                        "Ignoring unrecognized animation callback '{}' sender={:08X} file='{}' strArg='{}' numArg={}",
                        eventName,
                        senderID,
                        definingFile,
                        strArg,
                        a_event->numArg);
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            logger::info(
                "Scene callback received: event='{}' sender={:08X} file='{}' strArg='{}' numArg={}",
                eventName,
                senderID,
                definingFile,
                strArg,
                a_event->numArg);

            if (!quest || !PluginFilenameEquals(definingFile, "SexLab.esm"sv)) {
                logger::warn(
                    "Ignoring scene callback '{}': sender {:08X} is not a quest defined by SexLab.esm (file='{}')",
                    eventName,
                    senderID,
                    definingFile);
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto sourceID = quest->GetFormID();

            const auto instanceID = ParseInstanceID(*a_event);
            if (!instanceID) {
                logger::warn("Ignoring SexLab event with invalid instance ID payload (strArg='{}', numArg={})",
                    strArg, a_event->numArg);
                return RE::BSEventNotifyControl::kContinue;
            }

            if (handler_) {
                logger::info(
                    "Scene callback accepted: type={} key={:08X}/{}; forwarding to game task",
                    SceneEventTypeName(*eventKind),
                    sourceID,
                    *instanceID);
                handler_({ *eventKind, { sourceID, *instanceID }, {} });
            } else {
                logger::error(
                    "Ignoring accepted scene callback {:08X}/{}: event handler is unavailable",
                    sourceID,
                    *instanceID);
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
