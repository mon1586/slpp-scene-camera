#include "runtime/AnimationUpdateCoordinator.h"
#include "runtime/PresetFilterEvaluator.h"
#include <stdexcept>

namespace
{
    using Coordinator = ssc::runtime::AnimationUpdateCoordinator;
    using E = Coordinator::Event;
    void Check(bool value, const char* message)
    {
        if (!value) { throw std::runtime_error(message); }
    }
    void Complete(const Coordinator::Reply& reply, std::string text,
        std::vector<std::string> tags = {}, const char* status = "ok")
    {
        std::scoped_lock lock{ reply->mutex };
        reply->text = std::move(text); reply->tags = std::move(tags);
        reply->status = status; reply->complete = true;
    }
}

bool RunAnimationUpdateCoordinatorTests()
{
    Coordinator c;
    std::vector<Coordinator::Reply> ids, names, tags;
    c.Configure([&](const auto&) { return ids.emplace_back(std::make_shared<ssc::runtime::MetadataReply>()); },
        [&](const auto&, bool tag) {
            return (tag ? tags : names).emplace_back(std::make_shared<ssc::runtime::MetadataReply>());
        });
    auto now = Coordinator::Clock::time_point{};
    const ssc::runtime::SceneKey key{ 42, 1 }, other{ 43, 1 };
    auto event = [&](E type, bool player = false) {
        auto receipt = c.Receive(type, key, now);
        c.Observe(type, key, player, receipt, now);
    };
    event(E::Starting, true); c.Tick(now);
    Check(ids.empty() && c.Pending(), "Starting must wait for a usable start boundary");
    event(E::Start, true); c.Tick(now);
    Check(ids.size() == 1 && c.Pending(), "initial metadata is pending");
    const auto oldReply = ids.back();
    event(E::Change); Complete(oldReply, "old"); c.Tick(now);
    Check(names.empty() && c.Pending(), "Change cannot commit an old ID or initiate a read");
    event(E::Stage); c.Tick(now); Complete(ids.back(), "new"); c.Tick(now);
    Complete(names.back(), "New scene"); c.Tick(now);
    Check(c.Pending(), "a name alone must not publish partial metadata");
    Complete(tags.back(), {}, { "TagOne", "TagTwo" }); c.Tick(now);
    auto ready = c.Ready(); Check(ready && ready->known && ready->id == "new", "new metadata ready");
    Check(!c.Published(), "store stays unpublished until candidate commit");
    {
        std::scoped_lock lock{ ssc::runtime::SelectionBoundary() };
        Check(c.ValidLocked(ready), "candidate generation valid"); c.CommitLocked(ready);
    }
    Check(c.Published() == ready, "store committed");
    c.Receive(E::Change, other, now);
    Check(c.Published() == ready, "foreign notifications cannot invalidate current scene");
    // Receipt precedes queued processing: it must block a ready candidate immediately.
    auto next = c.Receive(E::Stage, key, now);
    Check(c.Pending() && !c.Published(), "receipt must invalidate before main-queue delivery");
    {
        std::scoped_lock lock{ ssc::runtime::SelectionBoundary() };
        Check(!c.ValidLocked(ready), "cannot commit after newer receipt");
    }
    c.Observe(E::Stage, key, false, next, now); c.Tick(now); Complete(ids.back(), "new"); c.Tick(now);
    Check(c.Ready() && names.size() == 1, "same ID reuses committed name and tags");
    event(E::Stage); c.Tick(now); const auto replyA = ids.back();
    event(E::Stage); c.Tick(now); const auto replyB = ids.back();
    Complete(replyB, "B"); c.Tick(now);
    Complete(names.back(), "B name"); Complete(tags.back(), {}, {}); c.Tick(now);
    Complete(replyA, "A"); c.Tick(now);
    Check(c.Ready() && c.Ready()->id == "B" && c.Ready()->known, "reverse callbacks cannot overwrite B; empty tags valid");
    event(E::Change); now += std::chrono::seconds(5); c.Tick(now);
    Check(c.Ready() && !c.Ready()->known, "missing StageStart times out to unknown");
    event(E::Stage); c.Tick(now); auto late = ids.back();
    now += std::chrono::seconds(5); c.Tick(now); Complete(late, "late"); c.Tick(now);
    Check(c.Ready() && !c.Ready()->known, "late success cannot replace timeout");
    event(E::Stage); c.Tick(now); late = ids.back(); c.Invalidate();
    event(E::Start, true); c.Tick(now); Complete(late, "previous lifetime"); c.Tick(now);
    Check(c.Pending(), "same key after load must reject previous-lifetime responses");
    event(E::End); Complete(ids.back(), "ended"); c.Tick(now);
    Check(!c.Ready() && !c.Published(), "end clears metadata and late response");

    Coordinator throwing;
    throwing.Configure([](const auto&) -> Coordinator::Reply { throw std::runtime_error("VM unavailable"); },
        [](const auto&, bool) -> Coordinator::Reply { return {}; });
    throwing.Observe(E::Start, key, true, 0, now); throwing.Tick(now);
    Check(throwing.Ready() && !throwing.Ready()->known, "reader exceptions resolve to unknown without resetting camera");

    using ssc::runtime::PresetFilterEvaluator;
    const ssc::runtime::AnimationMetadata metadata{ 1, true, "id", "Billyy Scene", { "Cowgirl", "Feet" } };
    Check(PresetFilterEvaluator("billyy", R"(\bfeet\b)").Matches(metadata), "ASCII-insensitive search across the tag list");
    Check(!PresetFilterEvaluator("other", "Feet").Matches(metadata), "name and tags require AND");
    Check(PresetFilterEvaluator("", "^Cowgirl Feet$").Matches(metadata), "tags are joined by one space without padding");
    Check(!PresetFilterEvaluator("", "^Feet$").Matches(metadata), "anchors refer to the whole tag list");
    const PresetFilterEvaluator sittingOnly("Billy", R"(^(?=.*\bsitting\b)(?!.*\bstanding\b).*$)");
    Check(sittingOnly.Matches({ 1, true, "id", "Billyy", { "Other", "Sitting" } }), "required tag and no excluded tag");
    Check(!sittingOnly.Matches({ 1, true, "id", "Billyy", { "Sitting", "Standing" } }), "later excluded tag rejects earlier positive match");
    Check(!sittingOnly.Matches({ 1, true, "id", "Billyy", { "Standing", "Sitting" } }), "exclusion is independent of tag order");
    Check(!sittingOnly.Matches({ 1, true, "id", "Billyy", { "Other" } }), "required tag absent");
    Check(!sittingOnly.Matches({ 1, true, "id", "Billyy", {} }), "empty tags do not satisfy positive condition");
    Check(PresetFilterEvaluator("", R"(^(?!.*\bstanding\b).*$)").Matches({ 1, true, "id", "Billyy", {} }), "known empty list satisfies absence");
    Check(!PresetFilterEvaluator("", R"(^(?!.*\bstanding\b).*$)").Matches({}), "unknown metadata cannot satisfy absence");
    Check(!PresetFilterEvaluator("[", "").Error().empty(), "invalid regex rejected");
    Check(!PresetFilterEvaluator(std::string(512, 'a'), "").Error().empty(), "length limit");
    Check(PresetFilterEvaluator("", "").Matches({}), "unrestricted is eligible for unknown metadata");
    Check(!PresetFilterEvaluator(".*", "").Matches({}), "conditional fails for unknown metadata");
    return true;
}
