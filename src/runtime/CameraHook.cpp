#include "runtime/CameraHook.h"

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
    }

    void CameraHook::Configure(
        IRuntimeClient& a_client,
        ISceneSource& a_sceneSource) noexcept
    {
        client_ = std::addressof(a_client);
        sceneSource_ = std::addressof(a_sceneSource);
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
                if (isStartEvent) {
                    auto* sceneSource = sceneSource_;
                    if (!sceneSource) {
                        logger::error("Ignoring scene start: scene source is unavailable");
                        return;
                    }
                    preparedEvent.participants = sceneSource->CollectParticipants(preparedEvent.key);
                }

                const auto installState = installState_.load(std::memory_order_acquire);
                if (installState == InstallState::kFailed) {
                    return;
                }
                if (installState == InstallState::kNotInstalled) {
                    if (!isStartEvent) {
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

        std::array<HookCandidate, RE::CameraStates::kTotal> candidates{};
        std::size_t candidateCount = 0;
        std::size_t liveStateCount = 0;
        auto& cameraStates = camera->GetRuntimeData().cameraStates;
        for (auto& statePointer : cameraStates) {
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
            "Collected {} camera state(s) across {} unique vtable(s)",
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

        if (!firstThunkObserved_.load(std::memory_order_relaxed)) {
            bool expected = false;
            if (firstThunkObserved_.compare_exchange_strong(
                    expected, true, std::memory_order_relaxed)) {
                try {
                    logger::info("Camera-state Update hook reached its first update");
                } catch (...) {
                }
            }
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
