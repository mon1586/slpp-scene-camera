#include "runtime/TDMTargetLockControl.h"

#include <REX/W32/KERNEL32.h>
#include <TrueDirectionalMovementAPI.h>
#include <iostream>
#include <thread>

namespace
{
    using Result = TDM_API::APIResult;
    using Handle = SKSE::PluginHandle;

    class TestTDM final : public TDM_API::IVTDM5
    {
    public:
        unsigned long GetTDMThreadId() const noexcept override { return thread; }
        bool GetDirectionalMovementState() const noexcept override { ++unrelated; return false; }
        bool GetTargetLockState() const noexcept override { ++reads; return locked; }
        RE::ActorHandle GetCurrentTarget() const noexcept override { ++unrelated; return {}; }
        Result RequestDisableDirectionalMovement(Handle) noexcept override { ++unrelated; return Result::MustKeep; }
        Result RequestDisableHeadtracking(Handle) noexcept override { ++unrelated; return Result::MustKeep; }
        Handle GetDisableDirectionalMovementOwner() const noexcept override { ++unrelated; return SKSE::kInvalidPluginHandle; }
        Handle GetDisableHeadtrackingOwner() const noexcept override { ++unrelated; return SKSE::kInvalidPluginHandle; }
        Result ReleaseDisableDirectionalMovement(Handle) noexcept override { ++unrelated; return Result::NotOwner; }
        Result ReleaseDisableHeadtracking(Handle) noexcept override { ++unrelated; return Result::NotOwner; }
        Result RequestYawControl(Handle, float) noexcept override { ++unrelated; return Result::MustKeep; }
        Result SetPlayerYaw(Handle, float) noexcept override { ++unrelated; return Result::NotOwner; }
        Result ReleaseYawControl(Handle) noexcept override { ++unrelated; return Result::NotOwner; }
        TDM_API::DirectionalMovementMode GetDirectionalMovementMode() const noexcept override { ++unrelated; return TDM_API::DirectionalMovementMode::kDisabled; }
        RE::NiPoint2 GetActualMovementInput() const noexcept override { ++unrelated; return {}; }
        bool IsTargetLockBehindTarget() const noexcept override { ++unrelated; return false; }
        Result RequestDisableTargetLock(Handle a_handle) noexcept override
        {
            ++requests;
            if (requestResult == Result::OK || requestResult == Result::AlreadyGiven) {
                owner = a_handle;
            }
            return requestResult;
        }
        Result ReleaseDisableTargetLock(Handle a_handle) noexcept override
        {
            ++releases;
            if (owner != a_handle) {
                return Result::NotOwner;
            }
            if (releaseResult == Result::OK) {
                owner = SKSE::kInvalidPluginHandle;
            }
            return releaseResult;
        }
        Handle GetDisableTargetLockOwner() const noexcept override { ++reads; return owner; }

        unsigned long thread{ REX::W32::GetCurrentThreadId() };
        Handle owner{ SKSE::kInvalidPluginHandle };
        bool locked{ true };
        Result requestResult{ Result::OK };
        Result releaseResult{ Result::OK };
        unsigned requests{ 0 };
        unsigned releases{ 0 };
        mutable unsigned reads{ 0 };
        mutable unsigned unrelated{ 0 };
    };
}

bool RunTDMTargetLockControlTests()
{
    bool passed = true;
    const auto check = [&](bool a_condition, const char* a_message) {
        if (!a_condition) {
            std::cerr << "FAILED: " << a_message << '\n';
            passed = false;
        }
    };
    TestTDM api;
    ssc::runtime::TDMTargetLockControl control;
    control.ConnectAPI(&api, 42);

    bool workerRequested = true;
    std::thread worker([&] { workerRequested = control.RequestUnlock(); });
    worker.join();
    check(!workerRequested && api.reads == 0 && api.requests == 0,
        "worker cannot read or write TDM target state");
    api.locked = false;
    check(!control.RequestUnlock() && api.requests == 0,
        "no target requires no disable acquisition");
    api.locked = true;
    api.owner = 7;
    api.requestResult = Result::AlreadyTaken;
    check(!control.RequestUnlock() && control.FinishUnlock(true) && api.owner == 7 && api.releases == 0,
        "other plugin ownership is preserved");
    api.owner = SKSE::kInvalidPluginHandle;
    api.requestResult = Result::OK;
    check(control.RequestUnlock() && api.owner == 42,
        "camera thread can acquire target disable");
    check(!control.FinishUnlock(false) && api.releases == 0,
        "disable remains until TDM consumes it");
    const auto readsBeforeCancel = api.reads;
    bool workerReleased = true;
    std::thread cancellation([&] { workerReleased = control.FinishUnlock(true); });
    cancellation.join();
    check(!workerReleased && api.reads == readsBeforeCancel && api.releases == 0,
        "off-thread cancellation stays pending without touching target state");
    api.locked = false;
    check(control.FinishUnlock(false) && api.releases == 1 && api.owner == SKSE::kInvalidPluginHandle,
        "observed unlock releases ownership on API thread");
    check(control.FinishUnlock(true) && api.releases == 1,
        "finished request is not released twice");

    api.locked = true;
    check(control.RequestUnlock(), "next request acquires again");
    api.releaseResult = Result::BadThread;
    check(!control.FinishUnlock(true), "failed release retains obligation");
    api.releaseResult = Result::OK;
    check(control.FinishUnlock(true) && api.owner == SKSE::kInvalidPluginHandle,
        "cancellation retries a failed release even with target still locked");
    check(control.RequestUnlock(), "ownership-loss case acquired");
    api.owner = 9;
    const auto releasesBeforeLoss = api.releases;
    check(control.FinishUnlock(true) && api.releases == releasesBeforeLoss && api.owner == 9,
        "lost ownership does not release another plugin's disable");
    check(api.unrelated == 0, "unlock never changes movement, yaw or headtracking");
    ssc::runtime::TDMTargetLockControl missing;
    check(!missing.RequestUnlock() && missing.FinishUnlock(true),
        "absent API is optional");
    return passed;
}
