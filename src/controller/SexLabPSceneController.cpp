#include "controller/SexLabPSceneController.h"

namespace ssc::controller
{
    namespace
    {
        [[nodiscard]] std::optional<SceneEventType> ParseEvent(std::string_view a_name) noexcept
        {
            if (a_name == "AnimationStarting"sv) {
                return SceneEventType::kAnimationStarting;
            }
            if (a_name == "AnimationStart"sv) {
                return SceneEventType::kAnimationStart;
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

    SexLabPSceneController* SexLabPSceneController::GetSingleton() noexcept
    {
        static SexLabPSceneController singleton;
        return std::addressof(singleton);
    }

    bool SexLabPSceneController::Register(SceneEventHandler a_handler)
    {
        if (!a_handler) {
            logger::error("Cannot register SexLab P+ scene controller without an event handler");
            return false;
        }
        if (registered_) {
            return handler_ == a_handler;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || !dataHandler->LookupModByName("SexLab.esm"sv)) {
            logger::warn("SexLab.esm is not loaded; scene controller will not be registered");
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
        logger::info("SexLab P+ scene controller registered");
        return true;
    }

    SceneParticipantSnapshot SexLabPSceneController::CollectParticipants(
        const SceneKey& a_key) const
    {
        SceneParticipantSnapshot result;
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
            for (std::size_t index = 0; index < result.count; ++index) {
                if (result.handles[index].native_handle() == handle.native_handle()) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                result.containsPlayer = result.containsPlayer || isPlayer;
                continue;
            }

            if (result.count < result.handles.size()) {
                result.handles[result.count++] = handle;
                result.containsPlayer = result.containsPlayer || isPlayer;
            } else {
                result.truncated = true;
                if (isPlayer && !result.containsPlayer) {
                    result.handles.back() = handle;
                    result.containsPlayer = true;
                }
            }
        }

        return result;
    }

    RE::BSEventNotifyControl SexLabPSceneController::ProcessEvent(
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
