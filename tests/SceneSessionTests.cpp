#include "SceneSession.h"
#include "core/SceneAnchor.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    bool Check(bool a_condition, std::string_view a_message)
    {
        if (!a_condition) {
            std::cerr << "FAILED: " << a_message << '\n';
        }
        return a_condition;
    }

    bool CheckNear(float a_actual, float a_expected, std::string_view a_message)
    {
        return Check(std::abs(a_actual - a_expected) < 0.0001F, a_message);
    }
}

int main()
{
    using ssc::SceneSession;
    using ssc::runtime::SceneKey;

    const SceneKey first{ 0x01001234, 7 };
    const SceneKey second{ 0x02005678, 8 };
    SceneSession session;
    bool passed = true;

    passed &= Check(session.IsIdle(), "new session is idle");
    passed &= Check(session.Prepare(first), "idle session accepts preparation");
    passed &= Check(session.IsPreparing(), "prepared session reports Preparing");
    passed &= Check(session.Matches(first), "prepared session retains its scene key");
    passed &= Check(!session.Activate(second), "wrong scene key cannot activate");
    passed &= Check(session.IsPreparing(), "failed activation preserves Preparing");
    passed &= Check(session.Activate(first), "matching scene key activates");
    passed &= Check(session.IsActive(), "activated session reports Active");
    passed &= Check(!session.Prepare(second), "overlapping scene cannot replace Active");
    passed &= Check(session.Matches(first), "overlap preserves the active scene key");

    session.BeginRestore();
    passed &= Check(
        session.GetState() == SceneSession::State::kRestoring,
        "active session enters Restoring");
    passed &= Check(session.Matches(first), "Restoring retains the scene key");

    session.Clear();
    passed &= Check(session.IsIdle(), "clear returns the session to Idle");
    passed &= Check(!session.Matches(first), "clear removes the old scene key");
    passed &= Check(session.Prepare(second), "a new scene can prepare after clear");

    using ssc::core::SceneAnchorCalculator;
    using ssc::core::Vec3;
    SceneAnchorCalculator anchorCalculator;

    const std::array<Vec3, 2> twoParticipants{{ { 0.0F, 0.0F, 10.0F }, { 10.0F, 0.0F, 20.0F } }};
    const auto twoPersonAnchor = anchorCalculator.Evaluate({
        twoParticipants,
        Vec3{ 0.0F, 0.0F, 10.0F },
        Vec3{ 0.0F, 1.0F, 0.0F },
    });
    passed &= Check(twoPersonAnchor.has_value(), "two participants produce an anchor");
    if (twoPersonAnchor) {
        passed &= CheckNear(twoPersonAnchor->position.x, 5.0F, "anchor averages Pelvis X");
        passed &= CheckNear(twoPersonAnchor->position.y, 0.0F, "anchor averages Pelvis Y");
        passed &= CheckNear(twoPersonAnchor->position.z, 15.0F, "anchor averages Pelvis Z");
        passed &= CheckNear(twoPersonAnchor->forward.x, -1.0F, "multi-person forward points from anchor to player");
        passed &= CheckNear(twoPersonAnchor->forward.y, 0.0F, "multi-person forward is horizontal");
    }

    const std::array<Vec3, 1> oneParticipant{{ { 4.0F, 5.0F, 6.0F } }};
    const auto onePersonAnchor = anchorCalculator.Evaluate({
        oneParticipant,
        oneParticipant.front(),
        Vec3{ 0.0F, 3.0F, 7.0F },
    });
    passed &= Check(onePersonAnchor.has_value(), "one participant uses player-forward fallback");
    if (onePersonAnchor) {
        passed &= CheckNear(onePersonAnchor->forward.x, 0.0F, "one-person fallback removes X drift");
        passed &= CheckNear(onePersonAnchor->forward.y, -1.0F, "one-person fallback reverses player forward");
        passed &= CheckNear(onePersonAnchor->forward.z, 0.0F, "one-person fallback removes vertical forward");
    }

    const std::array<Vec3, 2> degenerateParticipants{{ { 3.0F, 4.0F, 1.0F }, { 3.0F, 4.0F, 9.0F } }};
    const auto degenerateAnchor = anchorCalculator.Evaluate({
        degenerateParticipants,
        Vec3{ 3.0F, 4.0F, 2.0F },
        Vec3{ 1.0F, 0.0F, 0.0F },
    });
    passed &= Check(degenerateAnchor.has_value(), "degenerate multi-person layout uses fallback");
    if (degenerateAnchor) {
        passed &= CheckNear(degenerateAnchor->forward.x, -1.0F, "degenerate fallback reverses player forward");
    }

    const auto maximum = std::numeric_limits<float>::max();
    const std::array<Vec3, 2> extremeParticipants{{
        { maximum, maximum, maximum },
        { maximum, maximum, maximum },
    }};
    const auto extremeAnchor = anchorCalculator.Evaluate({
        extremeParticipants,
        extremeParticipants.front(),
        Vec3{ 0.0F, 1.0F, 0.0F },
    });
    passed &= Check(extremeAnchor.has_value(), "finite extreme coordinates do not overflow the average");
    if (extremeAnchor) {
        passed &= Check(std::isfinite(extremeAnchor->position.x), "extreme anchor position remains finite");
    }

    return passed ? 0 : 1;
}
