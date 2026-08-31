#include "runtime/CameraHook.h"

namespace ssc::runtime
{
    namespace
    {
        constexpr std::size_t kUpdateSlot = 0x03;

        class CameraEventSink final : public RE::BSTEventSink<SKSE::CameraEvent>
        {
        public:
            static CameraEventSink* GetSingleton() noexcept
            {
                static CameraEventSink singleton;
                return std::addressof(singleton);
            }

            RE::BSEventNotifyControl ProcessEvent(
                const SKSE::CameraEvent*,
                RE::BSTEventSource<SKSE::CameraEvent>*) override
            {
                try {
                    auto* mailbox = SceneEventMailbox::GetSingleton();
                    auto* client = CameraHook::GetClient();
                    if (client && (client->NeedsUpdate() || mailbox->HasPending())) {
                        CameraHook::QueueRefresh();
                    }
                } catch (...) {
                    if (auto* client = CameraHook::GetClient()) {
                        client->RequestReset();
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        struct HookCandidate
        {
            std::uintptr_t vtable{ 0 };
            std::uintptr_t slot{ 0 };
            std::uintptr_t original{ 0 };
        };
    }

    void CameraHook::Configure(IRuntimeClient& a_client) noexcept
    {
        client_ = std::addressof(a_client);
    }

    bool CameraHook::IsInstalled() noexcept
    {
        return installed_.load(std::memory_order_acquire);
    }

    void CameraHook::SubmitEvent(SceneEvent a_event)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::error("Ignoring scene event: SKSE task interface is unavailable");
            return;
        }

        const auto generation = SceneEventMailbox::GetSingleton()->CurrentGeneration();
        tasks->AddTask([a_event, generation] {
            try {
                auto* mailbox = SceneEventMailbox::GetSingleton();
                if (generation != mailbox->CurrentGeneration()) {
                    return;
                }

                auto* client = client_;
                if (!client) {
                    logger::error("Ignoring scene event: runtime client is unavailable");
                    return;
                }
                const auto isStartEvent = a_event.type == SceneEventType::kAnimationStarting ||
                                          a_event.type == SceneEventType::kAnimationStart;
                auto preparedEvent = a_event;
                const auto eligible = !isStartEvent || client->PrepareStartEvent(preparedEvent);
                if (!IsInstalled()) {
                    if (!isStartEvent || !eligible) {
                        return;
                    }
                    if (!InstallOrRefresh()) {
                        logger::error("Ignoring scene: camera-state update hook is unavailable");
                        return;
                    }
                } else if (isStartEvent && eligible) {
                    if (!InstallOrRefresh()) {
                        logger::warn("Camera-state hook refresh was incomplete; existing hooks remain active");
                    }
                }

                static_cast<void>(mailbox->Enqueue(preparedEvent, generation));
            } catch (const std::exception& exception) {
                try {
                    logger::critical("Scene event task failed: {}", exception.what());
                } catch (...) {
                }
                if (auto* client = client_) {
                    client->RequestReset();
                }
            } catch (...) {
                HandleBoundaryFailure("scene event task"sv);
            }
        });
    }

    void CameraHook::QueueRefresh()
    {
        if (!IsInstalled() || refreshQueued_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            refreshQueued_.store(false, std::memory_order_release);
            return;
        }

        try {
            tasks->AddTask([] {
                refreshQueued_.store(false, std::memory_order_release);
                try {
                    if (!InstallOrRefresh()) {
                        logger::warn("Camera-state hook refresh failed");
                    }
                } catch (...) {
                    HandleBoundaryFailure("camera-state hook refresh"sv);
                }
            });
        } catch (...) {
            refreshQueued_.store(false, std::memory_order_release);
            throw;
        }
    }

    bool CameraHook::InstallOrRefresh()
    {
        std::scoped_lock lock{ installMutex_ };

        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!camera) {
            logger::error("Cannot hook camera-state updates: PlayerCamera is unavailable");
            return false;
        }

        std::array<HookCandidate, RE::CameraStates::kTotal> candidates{};
        std::size_t candidateCount = 0;
        auto& cameraStates = camera->GetRuntimeData().cameraStates;
        for (auto& statePointer : cameraStates) {
            auto* state = statePointer.get();
            if (!state) {
                continue;
            }

            auto* vtable = *reinterpret_cast<std::uintptr_t**>(state);
            const auto vtableAddress = reinterpret_cast<std::uintptr_t>(vtable);
            const auto current = vtable[kUpdateSlot];
            if (!current || IsThunkAddress(current)) {
                continue;
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

            candidates[candidateCount++] = {
                vtableAddress,
                vtableAddress + sizeof(std::uintptr_t) * kUpdateSlot,
                current,
            };
        }

        if (candidateCount == 0) {
            return IsInstalled();
        }
        if (nextIndex_ + candidateCount > kMaxHookedVtables) {
            logger::error("Cannot hook camera-state updates: vtable capacity exhausted");
            return false;
        }

        const auto firstIndex = nextIndex_;
        std::size_t patchedCount = 0;
        for (std::size_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex) {
            const auto hookIndex = firstIndex + candidateIndex;
            const auto& candidate = candidates[candidateIndex];
            const auto thunkAddress = GetThunkAddress(hookIndex);
            const auto original = SKSE::stl::unrestricted_cast<UpdateFunction>(candidate.original);

            entries_[hookIndex] = { candidate.vtable, candidate.slot };
            originals_[hookIndex].store(original, std::memory_order_release);
            if (!REL::safe_write(
                    candidate.slot,
                    std::addressof(thunkAddress),
                    sizeof(thunkAddress),
                    std::addressof(candidate.original),
                    sizeof(candidate.original))) {
                originals_[hookIndex].store(nullptr, std::memory_order_release);
                entries_[hookIndex] = {};
                logger::error("Camera-state update hook verification failed for vtable {:X}",
                    candidate.vtable);

                bool rollbackComplete = true;
                for (std::size_t rollbackOffset = patchedCount; rollbackOffset > 0; --rollbackOffset) {
                    const auto rollbackIndex = firstIndex + rollbackOffset - 1;
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
                    }
                }

                nextIndex_ = firstIndex + patchedCount;
                if (rollbackComplete) {
                    logger::warn("Camera-state hook batch was rolled back; {} published chain slot(s) remain reserved",
                        patchedCount);
                } else {
                    installed_.store(true, std::memory_order_release);
                    logger::critical("Camera-state hook rollback was incomplete; retained safe chains for {} slot(s)",
                        patchedCount);
                }
                return false;
            }
            ++patchedCount;
        }

        nextIndex_ += candidateCount;
        installed_.store(true, std::memory_order_release);

        if (!cameraEventSinkRegistered_) {
            if (auto* source = SKSE::GetCameraEventSource()) {
                source->AddEventSink(CameraEventSink::GetSingleton());
                cameraEventSinkRegistered_ = true;
            } else {
                logger::warn("SKSE camera event source is unavailable; hooks will refresh at scene start only");
            }
        }

        logger::info("Camera-state Update hook installed/refreshed on {} vtable(s) ({} chain(s) retained)",
            candidateCount, nextIndex_);
        return true;
    }

    template <std::size_t Index>
    void CameraHook::Thunk(
        RE::TESCameraState* a_state,
        RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
    {
        const auto original = originals_[Index].load(std::memory_order_acquire);
        if (!original) {
            HandleBoundaryFailure("camera-state hook missing original"sv);
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

        // A later camera mod may chain a new hook to an older SSC thunk. Only the
        // outermost SSC thunk performs post-update work, after that whole chain.
        if (entryDepth != 0) {
            return;
        }

        auto* mailbox = SceneEventMailbox::GetSingleton();
        auto* client = client_;
        if (!client || (!mailbox->HasPending() && !client->NeedsUpdate())) {
            return;
        }

        try {
            mailbox->DispatchPending(*client);
            if (auto* ui = RE::UI::GetSingleton(); !ui || !ui->GameIsPaused()) {
                client->Update();
            }
        } catch (const std::exception& exception) {
            try {
                logger::critical("Camera-state update failed: {}", exception.what());
            } catch (...) {
            }
            client->EmergencyReset();
        } catch (...) {
            HandleBoundaryFailure("camera-state update"sv);
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

    bool CameraHook::IsThunkAddress(std::uintptr_t a_address) noexcept
    {
        for (std::size_t index = 0; index < kMaxHookedVtables; ++index) {
            if (GetThunkAddress(index) == a_address) {
                return true;
            }
        }
        return false;
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
}
