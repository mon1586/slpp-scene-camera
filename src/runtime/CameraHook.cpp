#include "runtime/CameraHook.h"

#include <REX/W32/KERNEL32.h>

namespace ssc::runtime
{
    namespace
    {
        constexpr std::size_t kUpdateSlot = 0x03;

        struct HookCandidate
        {
            std::uintptr_t vtable{ 0 };
            std::uintptr_t slot{ 0 };
            std::uintptr_t original{ 0 };
        };

        class CameraStateSink final : public RE::BSTEventSink<SKSE::CameraEvent>
        {
        public:
            [[nodiscard]] static CameraStateSink* GetSingleton() noexcept
            {
                static CameraStateSink singleton;
                return std::addressof(singleton);
            }

            RE::BSEventNotifyControl ProcessEvent(
                const SKSE::CameraEvent* a_event,
                RE::BSTEventSource<SKSE::CameraEvent>*) override
            {
                if (!a_event) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const auto* newState = a_event->newState;
                if (!newState || (newState->id != RE::CameraState::kThirdPerson &&
                                     newState->id != RE::CameraState::kAnimated)) {
                    CameraHook::QueueReset("camera changed to an unsupported state");
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void CameraHook::Configure(
        IRuntimeClient& a_client,
        ISceneSource& a_sceneSource) noexcept
    {
        client_ = std::addressof(a_client);
        sceneSource_ = std::addressof(a_sceneSource);
    }

    bool CameraHook::RegisterCameraStateSink() noexcept
    {
        try {
            auto* source = SKSE::GetCameraEventSource();
            if (!source) {
                logger::error("Cannot register camera-state observer: SKSE event source is unavailable");
                return false;
            }
            source->AddEventSink(CameraStateSink::GetSingleton());
            logger::info("Camera-state observer registered");
            return true;
        } catch (...) {
            HandleBoundaryFailure("camera-state observer registration"sv);
            return false;
        }
    }

    void CameraHook::InvalidatePendingEvents() noexcept
    {
        eventGeneration_.fetch_add(1, std::memory_order_acq_rel);
    }

    void CameraHook::QueueReset(std::string_view a_reason) noexcept
    {
        auto* client = client_;
        if (!client) {
            return;
        }
        client->RequestReset();

        bool expected = false;
        if (!resetTaskQueued_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            return;
        }

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            resetTaskQueued_.store(false, std::memory_order_release);
            client->RequestReset();
            return;
        }

        try {
            tasks->AddTask([reason = std::string{ a_reason }] {
                resetTaskQueued_.store(false, std::memory_order_release);
                if (auto* runtimeClient = client_) {
                    static_cast<void>(runtimeClient->ProcessPendingReset(reason));
                }
            });
        } catch (...) {
            resetTaskQueued_.store(false, std::memory_order_release);
            client->RequestReset();
            HandleBoundaryFailure("camera reset scheduling"sv);
        }
    }

    void CameraHook::SubmitEvent(SceneEvent a_event)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::error(
                "Ignoring scene event {} {:08X}/{}: SKSE task interface is unavailable",
                SceneEventTypeName(a_event.type),
                a_event.key.sourceID,
                a_event.key.instanceID);
            return;
        }

        const auto generation = eventGeneration_.load(std::memory_order_acquire);
        logger::info(
            "Scene event queued: type={} key={:08X}/{} generation={}",
            SceneEventTypeName(a_event.type),
            a_event.key.sourceID,
            a_event.key.instanceID,
            generation);
        tasks->AddTask([a_event, generation] {
            try {
                if (generation != eventGeneration_.load(std::memory_order_acquire)) {
                    logger::info(
                        "Discarding queued scene event {} {:08X}/{}: lifecycle generation changed from {} to {}",
                        SceneEventTypeName(a_event.type),
                        a_event.key.sourceID,
                        a_event.key.instanceID,
                        generation,
                        eventGeneration_.load(std::memory_order_acquire));
                    return;
                }

                logger::info(
                    "Scene event task started: type={} key={:08X}/{} generation={} currentThread={}",
                    SceneEventTypeName(a_event.type),
                    a_event.key.sourceID,
                    a_event.key.instanceID,
                    generation, REX::W32::GetCurrentThreadId());
                auto* client = client_;
                if (!client) {
                    logger::error(
                        "Ignoring scene event {} {:08X}/{}: runtime client is unavailable",
                        SceneEventTypeName(a_event.type),
                        a_event.key.sourceID,
                        a_event.key.instanceID);
                    return;
                }
                const auto isStartEvent = a_event.type == SceneEventType::kAnimationStarting ||
                                          a_event.type == SceneEventType::kAnimationStart;
                auto preparedEvent = a_event;
                if (isStartEvent) {
                    auto* sceneSource = sceneSource_;
                    if (!sceneSource) {
                        logger::error("Ignoring scene start: scene source is unavailable");
                        return;
                    }
                    preparedEvent.participants = sceneSource->CollectParticipants(preparedEvent.key);
                    logger::info(
                        "Scene participants collected for {:08X}/{}: count={}, player={}, truncated={}",
                        preparedEvent.key.sourceID,
                        preparedEvent.key.instanceID,
                        preparedEvent.participants.Count(),
                        preparedEvent.participants.ContainsPlayer() ? "yes" : "no",
                        preparedEvent.participants.WasTruncated() ? "yes" : "no");
                }

                const auto installState = installState_.load(std::memory_order_acquire);
                if (installState == InstallState::kFailed) {
                    logger::error(
                        "Ignoring scene event {} {:08X}/{}: camera update hook installation previously failed",
                        SceneEventTypeName(preparedEvent.type),
                        preparedEvent.key.sourceID,
                        preparedEvent.key.instanceID);
                    return;
                }
                if (installState == InstallState::kNotInstalled) {
                    if (!isStartEvent) {
                        logger::info(
                            "Ignoring scene event {} {:08X}/{} before camera update hook installation",
                            SceneEventTypeName(preparedEvent.type),
                            preparedEvent.key.sourceID,
                            preparedEvent.key.instanceID);
                        return;
                    }
                    if (!preparedEvent.participants.ContainsPlayer()) {
                        logger::info(
                            "Ignoring scene {:08X}/{} before hook installation: player is not a participant",
                            preparedEvent.key.sourceID,
                            preparedEvent.key.instanceID);
                        return;
                    }
                    if (!InstallOnce(preparedEvent.key)) {
                        logger::error("Ignoring scene: camera-state update hook is unavailable");
                        return;
                    }
                }

                if (generation != eventGeneration_.load(std::memory_order_acquire)) {
                    logger::info(
                        "Discarding prepared scene event {} {:08X}/{}: lifecycle generation changed",
                        SceneEventTypeName(preparedEvent.type),
                        preparedEvent.key.sourceID,
                        preparedEvent.key.instanceID);
                    return;
                }
                logger::info(
                    "Scene event delivered to procedure layer: type={} key={:08X}/{}",
                    SceneEventTypeName(preparedEvent.type),
                    preparedEvent.key.sourceID,
                    preparedEvent.key.instanceID);
                client->HandleSceneEvent(preparedEvent);
            } catch (const std::exception& exception) {
                try {
                    logger::critical("Scene event task failed: {}", exception.what());
                } catch (...) {
                }
                if (auto* client = client_) {
                    client->EmergencyReset();
                }
            } catch (...) {
                HandleBoundaryFailure("scene event task"sv);
            }
        });
    }

    bool CameraHook::InstallOnce(const SceneKey& a_key)
    {
        const auto state = installState_.load(std::memory_order_acquire);
        if (state != InstallState::kNotInstalled) {
            return state == InstallState::kInstalled;
        }

        SKSE::stl::scope_exit markFailed{ []() noexcept {
            installState_.store(InstallState::kFailed, std::memory_order_release);
        } };

        logger::info(
            "Installing camera-state Update hook for scene {:08X}/{}",
            a_key.sourceID,
            a_key.instanceID);

        if (!client_ || !sceneSource_) {
            logger::error("Cannot install camera-state updates: runtime dependencies are unavailable");
            installState_.store(InstallState::kFailed, std::memory_order_release);
            return false;
        }

        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!camera) {
            logger::error("Cannot hook camera-state updates: PlayerCamera is unavailable");
            installState_.store(InstallState::kFailed, std::memory_order_release);
            return false;
        }

        std::array<HookCandidate, kMaxHookedVtables> candidates{};
        std::size_t candidateCount = 0;
        std::size_t liveStateCount = 0;
        auto& cameraStates = camera->GetRuntimeData().cameraStates;
        constexpr std::array targetStates{
            RE::CameraState::kAnimated,
            RE::CameraState::kThirdPerson,
        };
        for (const auto stateID : targetStates) {
            auto& statePointer = cameraStates[stateID];
            auto* cameraState = statePointer.get();
            if (!cameraState) {
                continue;
            }
            ++liveStateCount;

            auto* vtable = *reinterpret_cast<std::uintptr_t**>(cameraState);
            if (!vtable) {
                logger::error("Cannot hook camera state: vtable is null");
                return false;
            }
            const auto vtableAddress = reinterpret_cast<std::uintptr_t>(vtable);
            const auto current = vtable[kUpdateSlot];
            if (!current) {
                logger::error("Cannot hook camera state: Update pointer is null");
                return false;
            }

            bool duplicate = false;
            for (std::size_t index = 0; index < candidateCount; ++index) {
                if (candidates[index].vtable == vtableAddress) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            if (candidateCount == candidates.size()) {
                logger::error("Cannot hook camera-state updates: candidate capacity exceeded");
                return false;
            }
            candidates[candidateCount++] = {
                vtableAddress,
                vtableAddress + sizeof(std::uintptr_t) * kUpdateSlot,
                current,
            };
        }

        if (candidateCount == 0) {
            logger::error("Cannot hook camera-state updates: no live camera state vtables were found");
            installState_.store(InstallState::kFailed, std::memory_order_release);
            return false;
        }

        logger::info(
            "Collected {} supported camera state(s) across {} unique vtable(s)",
            liveStateCount,
            candidateCount);

        for (std::size_t index = 0; index < candidateCount; ++index) {
            const auto& candidate = candidates[index];
            const auto original = SKSE::stl::unrestricted_cast<UpdateFunction>(candidate.original);
            const auto thunkAddress = GetThunkAddress(index);
            if (!original || !thunkAddress) {
                logger::error("Cannot publish camera-state hook entry {}", index);
                for (std::size_t clearIndex = 0; clearIndex <= index; ++clearIndex) {
                    originals_[clearIndex].store(nullptr, std::memory_order_release);
                    entries_[clearIndex] = {};
                }
                installState_.store(InstallState::kFailed, std::memory_order_release);
                return false;
            }
            entries_[index] = { candidate.vtable, candidate.slot };
            originals_[index].store(original, std::memory_order_release);
        }

        std::size_t patchedCount = 0;
        for (std::size_t index = 0; index < candidateCount; ++index) {
            const auto& candidate = candidates[index];
            const auto thunkAddress = GetThunkAddress(index);
            if (!REL::safe_write(
                    candidate.slot,
                    std::addressof(thunkAddress),
                    sizeof(thunkAddress),
                    std::addressof(candidate.original),
                    sizeof(candidate.original))) {
                logger::error("Camera-state update hook verification failed for vtable {:X}",
                    candidate.vtable);

                bool rollbackComplete = true;
                std::size_t retainedCount = 0;
                for (std::size_t rollbackOffset = patchedCount; rollbackOffset > 0; --rollbackOffset) {
                    const auto rollbackIndex = rollbackOffset - 1;
                    const auto rollbackThunk = GetThunkAddress(rollbackIndex);
                    const auto rollbackOriginal = SKSE::stl::unrestricted_cast<std::uintptr_t>(
                        originals_[rollbackIndex].load(std::memory_order_acquire));
                    if (!REL::safe_write(
                            entries_[rollbackIndex].slot,
                            std::addressof(rollbackOriginal),
                            sizeof(rollbackOriginal),
                            std::addressof(rollbackThunk),
                            sizeof(rollbackThunk))) {
                        rollbackComplete = false;
                        ++retainedCount;
                    } else {
                        originals_[rollbackIndex].store(nullptr, std::memory_order_release);
                        entries_[rollbackIndex] = {};
                    }
                }

                for (std::size_t clearIndex = patchedCount; clearIndex < candidateCount; ++clearIndex) {
                    originals_[clearIndex].store(nullptr, std::memory_order_release);
                    entries_[clearIndex] = {};
                }

                entryCount_ = retainedCount;
                installState_.store(InstallState::kFailed, std::memory_order_release);
                if (rollbackComplete) {
                    logger::warn("Camera-state hook batch was rolled back; hook installation is disabled");
                } else {
                    logger::critical(
                        "Camera-state hook rollback was incomplete; {} original-only thunk(s) remain",
                        retainedCount);
                }
                return false;
            }
            ++patchedCount;
        }

        entryCount_ = candidateCount;
        installState_.store(InstallState::kInstalled, std::memory_order_release);
        logger::info("Camera-state Update hook installed once on {} unique vtable(s)", entryCount_);
        markFailed.release();
        return true;
    }

    template <std::size_t Index>
    void CameraHook::Thunk(
        RE::TESCameraState* a_state,
        RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
    {
        const auto original = originals_[Index].load(std::memory_order_acquire);
        if (!original) {
            HandleUpdateFailure("camera-state hook missing original"sv);
            return;
        }

        const auto entryDepth = thunkDepth_++;
        try {
            original(a_state, a_nextState);
        } catch (...) {
            --thunkDepth_;
            throw;
        }
        --thunkDepth_;

        if (entryDepth != 0) {
            return;
        }

        if (installState_.load(std::memory_order_acquire) != InstallState::kInstalled) {
            return;
        }

        updateThunkActive_ = true;
        SKSE::stl::scope_exit clearThunkActive{ []() noexcept {
            updateThunkActive_ = false;
        } };

        if (!firstThunkObserved_.load(std::memory_order_relaxed)) {
            bool expected = false;
            if (firstThunkObserved_.compare_exchange_strong(
                    expected, true, std::memory_order_relaxed)) {
                try {
                    logger::info("Camera-state Update hook reached its first update (currentThread={})",
                        REX::W32::GetCurrentThreadId());
                } catch (...) {
                }
            }
        }

        auto* client = client_;
        if (!client || !client->NeedsUpdate()) {
            return;
        }

        try {
            if (!IsUpdateHookHealthy()) {
                client->EmergencyReset();
                return;
            }
            auto* ui = RE::UI::GetSingleton();
            const auto paused = ui && ui->GameIsPaused();
            if (!paused || client->AllowsUpdateWhilePaused()) {
                const auto* timer = RE::BSTimer::GetSingleton();
                client->Update(!paused && timer ? timer->realTimeDelta : 0.0F);
            }
        } catch (const std::exception& exception) {
            try {
                logger::critical("Camera-state update failed: {}", exception.what());
            } catch (...) {
            }
            client->EmergencyReset();
        } catch (...) {
            HandleUpdateFailure("camera-state update"sv);
        }
    }

    template <std::size_t... Indices>
    constexpr auto CameraHook::MakeThunkTable(std::index_sequence<Indices...>) noexcept
    {
        return std::array<UpdateFunction, sizeof...(Indices)>{ Thunk<Indices>... };
    }

    std::uintptr_t CameraHook::GetThunkAddress(std::size_t a_index) noexcept
    {
        static constexpr auto thunks = MakeThunkTable(
            std::make_index_sequence<kMaxHookedVtables>{});
        return a_index < thunks.size() ?
            SKSE::stl::unrestricted_cast<std::uintptr_t>(thunks[a_index]) : 0;
    }

    bool CameraHook::IsUpdateHookHealthy() noexcept
    {
        if (installState_.load(std::memory_order_acquire) != InstallState::kInstalled) {
            return false;
        }
        // Reaching our thunk proves that a later vtable hook preserved us in its
        // original-call chain even when it now occupies the visible slot.
        if (updateThunkActive_) {
            return true;
        }

        for (std::size_t index = 0; index < entryCount_; ++index) {
            const auto slot = entries_[index].slot;
            const auto expected = GetThunkAddress(index);
            if (!slot || !expected || *reinterpret_cast<const std::uintptr_t*>(slot) != expected) {
                bool wasReported = hookLossReported_.exchange(true, std::memory_order_acq_rel);
                if (!wasReported) {
                    try {
                        logger::error("Camera-state Update hook was replaced; camera control is disabled");
                    } catch (...) {
                    }
                }
                return false;
            }
        }
        return entryCount_ != 0;
    }

    void CameraHook::HandleBoundaryFailure(std::string_view a_context) noexcept
    {
        try {
            logger::critical("Unhandled exception at {} boundary; camera ownership will be released", a_context);
        } catch (...) {
        }
        if (auto* client = client_) {
            client->RequestReset();
        }
    }

    void CameraHook::HandleUpdateFailure(std::string_view a_context) noexcept
    {
        try {
            logger::critical("Unhandled exception at {} boundary; camera ownership will be released", a_context);
        } catch (...) {
        }
        if (auto* client = client_) {
            client->EmergencyReset();
        }
    }
}
