#pragma once
#include "runtime/AnimationMetadataStore.h"
#include "runtime/SelectionBoundary.h"
#include "runtime/SceneKey.h"
#include <chrono>
#include <functional>
#include <map>
#include <optional>

namespace ssc::runtime
{
    struct MetadataReply
    {
        std::mutex mutex;
        bool complete{ false };
        const char* status{ "pending" };
        std::string text;
        std::vector<std::string> tags;
    };

    class AnimationUpdateCoordinator
    {
    public:
        using Clock = std::chrono::steady_clock;
        enum class Event { Starting, Start, Change, Stage, Relocated, End };
        using Reply = std::shared_ptr<MetadataReply>;
        using ReadID = std::function<Reply(const SceneKey&)>;
        using ReadMetadata = std::function<Reply(const std::string&, bool)>;
        void Configure(ReadID a_id, ReadMetadata a_metadata);
        bool Enabled() const noexcept { return static_cast<bool>(readID_); }
        std::uint64_t Receive(Event a_event, SceneKey a_key, Clock::time_point a_now);
        void Observe(Event a_event, SceneKey a_key, bool a_player,
            std::uint64_t a_receipt, Clock::time_point a_now);
        void Tick(Clock::time_point a_now);
        void Invalidate();
        void ClearActive();
        bool Pending() const;
        bool IsLiveReceipt(std::uint64_t a_receipt) const;
        std::uint64_t PositionLocked() const { return position_; }
        std::shared_ptr<const AnimationMetadata> Ready() const;
        std::shared_ptr<const AnimationMetadata> Published() const;
        // Caller holds SelectionBoundary for the entire candidate publication/selection.
        bool ValidLocked(const std::shared_ptr<const AnimationMetadata>& a_value) const;
        void CommitLocked(const std::shared_ptr<const AnimationMetadata>& a_value);

    private:
        using Key = std::pair<std::uint32_t, std::int32_t>;
        static Key ToKey(SceneKey a_key) { return { a_key.sourceID, a_key.instanceID }; }
        struct Receipt { std::uint64_t number; Event event; Clock::time_point at; };
        std::map<Key, Receipt> receipts_;
        std::optional<SceneKey> active_;
        std::uint64_t serial_{ 0 };
        std::uint64_t position_{ 0 };
        std::uint64_t invalidatedThrough_{ 0 };
        std::uint64_t working_{ 0 };
        std::uint64_t handled_{ 0 };
        Clock::time_point deadline_{};
        bool pending_{ false };
        bool needID_{ false };
        Reply idReply_, nameReply_, tagsReply_;
        std::string workingID_;
        std::shared_ptr<const AnimationMetadata> ready_;
        AnimationMetadataStore store_;
        ReadID readID_;
        ReadMetadata readMetadata_;
    };
}
