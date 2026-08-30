#include "controller/SexLabEventSink.h"

#include "controller/SceneEventMailbox.h"

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
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::int32_t> ParseThreadID(
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
            constexpr auto minThread = static_cast<float>(std::numeric_limits<std::int32_t>::min());
            constexpr auto maxThread = static_cast<float>(std::numeric_limits<std::int32_t>::max());
            if (a_event.numArg < minThread || a_event.numArg > maxThread) {
                return std::nullopt;
            }
            return static_cast<std::int32_t>(std::lround(a_event.numArg));
        }
    }

    SexLabEventSink* SexLabEventSink::GetSingleton() noexcept
    {
        static SexLabEventSink singleton;
        return std::addressof(singleton);
    }

    void SexLabEventSink::Register()
    {
        auto* singleton = GetSingleton();
        if (singleton->registered_) {
            return;
        }

        auto* source = SKSE::GetModCallbackEventSource();
        if (!source) {
            logger::error("SKSE ModCallbackEvent source is unavailable");
            return;
        }

        source->AddEventSink(singleton);
        singleton->registered_ = true;
        logger::info("SexLab ModCallbackEvent sink registered");
    }

    RE::BSEventNotifyControl SexLabEventSink::ProcessEvent(
        const SKSE::ModCallbackEvent* a_event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*)
    {
        if (!a_event || !a_event->sender) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto eventKind = ParseEvent(a_event->eventName.c_str());
        if (!eventKind) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto threadID = ParseThreadID(*a_event);
        if (!threadID) {
            logger::warn("Ignoring SexLab event with invalid thread ID payload (strArg='{}', numArg={})",
                a_event->strArg.c_str(), a_event->numArg);
            return RE::BSEventNotifyControl::kContinue;
        }

        SceneEventMailbox::GetSingleton()->Enqueue({
            *eventKind,
            a_event->sender->GetFormID(),
            *threadID,
        });

        return RE::BSEventNotifyControl::kContinue;
    }
}
