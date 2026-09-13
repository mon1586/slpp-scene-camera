#include "runtime/AnimationUpdateCoordinator.h"

namespace ssc::runtime
{
    namespace
    {
        template <class Read>
        AnimationUpdateCoordinator::Reply TryRead(Read&& read)
        {
            try {
                if (auto reply = read()) { return reply; }
            } catch (const std::exception&) {}
            auto failed = std::make_shared<MetadataReply>();
            failed->complete = true; failed->status = "reader_failed";
            return failed;
        }
    }

    void AnimationUpdateCoordinator::Configure(ReadID a_id, ReadMetadata a_metadata)
    {
        readID_ = std::move(a_id);
        readMetadata_ = std::move(a_metadata);
    }

    std::uint64_t AnimationUpdateCoordinator::Receive(Event a_event, SceneKey a_key, Clock::time_point a_now)
    {
        std::scoped_lock lock{ SelectionBoundary() };
        const auto number = ++serial_;
        const auto key = ToKey(a_key);
        if (receipts_.size() >= 128 && !receipts_.contains(key)) {
            for (auto it = receipts_.begin(); it != receipts_.end(); ++it) {
                if (!active_ || it->first != ToKey(*active_)) { receipts_.erase(it); break; }
            }
        }
        if (a_event == Event::Relocated && active_ && *active_ == a_key) { ++position_; }
        // Relocation invalidates candidates, not in-flight animation metadata.
        if (a_event != Event::Relocated) { receipts_[key] = { number, a_event, a_now }; }
        return number;
    }

    void AnimationUpdateCoordinator::Observe(Event a_event, SceneKey a_key, bool a_player,
        std::uint64_t a_receipt, Clock::time_point a_now)
    {
        if (!Enabled()) { return; }
        if (a_receipt == 0) { a_receipt = Receive(a_event, a_key, a_now); }
        std::scoped_lock lock{ SelectionBoundary() };
        if (a_receipt <= invalidatedThrough_ || !receipts_.contains(ToKey(a_key))) { return; }
        if (!active_) {
            if (!a_player || (a_event != Event::Starting && a_event != Event::Start)) { return; }
            active_ = a_key;
            store_.current.reset();
            handled_ = 0;
        }
        if (*active_ != a_key) { return; }
        if (a_event == Event::End) {
            active_.reset(); ready_.reset(); store_.current.reset(); pending_ = false;
            idReply_.reset(); nameReply_.reset(); tagsReply_.reset();
            return;
        }
        const auto it = receipts_.find(ToKey(a_key));
        if (it == receipts_.end() || it->second.number != a_receipt) { return; }
        // Repeated delivery of one receipt cannot restart a completed update.
        if (a_receipt <= handled_) { return; }
        handled_ = working_ = a_receipt;
        deadline_ = it->second.at + std::chrono::seconds(5);
        pending_ = true;
        needID_ = a_event == Event::Start || a_event == Event::Stage;
        ready_.reset(); workingID_.clear();
        idReply_.reset(); nameReply_.reset(); tagsReply_.reset();
    }

    void AnimationUpdateCoordinator::Tick(Clock::time_point a_now)
    {
        SceneKey key;
        std::uint64_t revision;
        bool requestID = false;
        {
            std::scoped_lock lock{ SelectionBoundary() };
            if (!active_ || !pending_) { return; }
            const auto receipt = receipts_.find(ToKey(*active_));
            if (receipt == receipts_.end() || receipt->second.number != working_) { return; }
            if (a_now >= deadline_) {
                ready_ = std::make_shared<AnimationMetadata>(AnimationMetadata{ working_ });
                pending_ = false;
                idReply_.reset(); nameReply_.reset(); tagsReply_.reset();
                return;
            }
            key = *active_; revision = working_;
            requestID = needID_ && !idReply_;
        }
        // Engine calls and response copying are outside the selection boundary.
        if (requestID) { idReply_ = TryRead([&] { return readID_(key); }); }
        if (!idReply_) { return; }
        bool failed = false;
        {
            std::scoped_lock lock{ idReply_->mutex };
            if (!idReply_->complete) { return; }
            failed = std::string_view{ idReply_->status } != "ok" || idReply_->text.empty();
            workingID_ = idReply_->text;
        }
        auto result = std::make_shared<AnimationMetadata>();
        result->revision = revision;
        if (!failed) {
            std::shared_ptr<const AnimationMetadata> cached;
            { std::scoped_lock lock{ SelectionBoundary() }; cached = store_.current; }
            if (cached && cached->known && cached->id == workingID_) {
                *result = *cached; result->revision = revision;
            } else {
                if (!nameReply_) { nameReply_ = TryRead([&] { return readMetadata_(workingID_, false); }); }
                if (!tagsReply_) { tagsReply_ = TryRead([&] { return readMetadata_(workingID_, true); }); }
                if (!nameReply_ || !tagsReply_) { failed = true; }
                else {
                    std::scoped_lock lock{ nameReply_->mutex, tagsReply_->mutex };
                    failed = (nameReply_->complete && std::string_view{ nameReply_->status } != "ok") ||
                        (tagsReply_->complete && std::string_view{ tagsReply_->status } != "ok");
                    if (!failed && (!nameReply_->complete || !tagsReply_->complete)) { return; }
                    if (!failed) {
                        result->known = true; result->id = workingID_;
                        result->name = nameReply_->text; result->tags = tagsReply_->tags;
                    }
                }
            }
        }
        std::scoped_lock lock{ SelectionBoundary() };
        if (active_ && *active_ == key && working_ == revision &&
            receipts_.at(ToKey(key)).number == revision) {
            ready_ = std::move(result); pending_ = false;
            idReply_.reset(); nameReply_.reset(); tagsReply_.reset();
        }
    }

    void AnimationUpdateCoordinator::ClearActive()
    {
        std::scoped_lock lock{ SelectionBoundary() };
        active_.reset(); ready_.reset(); store_.current.reset(); pending_ = false;
    }

    void AnimationUpdateCoordinator::Invalidate()
    {
        std::scoped_lock lock{ SelectionBoundary() };
        invalidatedThrough_ = ++serial_; receipts_.clear(); active_.reset(); ready_.reset();
        store_.current.reset(); pending_ = false;
        // Requests are main-thread-owned; callbacks retain only private mailboxes.
    }

    bool AnimationUpdateCoordinator::ValidLocked(const std::shared_ptr<const AnimationMetadata>& a_value) const
    {
        if (!Enabled()) { return true; }
        if (!active_ || !a_value) { return false; }
        const auto it = receipts_.find(ToKey(*active_));
        return it != receipts_.end() && it->second.number == a_value->revision &&
            ready_ == a_value && !pending_;
    }
    std::shared_ptr<const AnimationMetadata> AnimationUpdateCoordinator::Ready() const
    {
        std::scoped_lock lock{ SelectionBoundary() };
        return ValidLocked(ready_) ? ready_ : nullptr;
    }
    bool AnimationUpdateCoordinator::IsLiveReceipt(std::uint64_t a_receipt) const
    {
        std::scoped_lock lock{ SelectionBoundary() };
        return a_receipt > invalidatedThrough_;
    }
    bool AnimationUpdateCoordinator::Pending() const
    {
        return Enabled() && !Ready();
    }
    void AnimationUpdateCoordinator::CommitLocked(const std::shared_ptr<const AnimationMetadata>& a_value)
    {
        store_.current = a_value;
    }
    std::shared_ptr<const AnimationMetadata> AnimationUpdateCoordinator::Published() const
    {
        std::scoped_lock lock{ SelectionBoundary() };
        return ValidLocked(store_.current) ? store_.current : nullptr;
    }
}
