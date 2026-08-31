#include "runtime/WorldDebugVisualization.h"

namespace ssc::runtime
{
    namespace
    {
        constexpr auto kArrowModel = "marker_arrow.nif"sv;
        constexpr float kArrowLength = 32.0F;
        constexpr float kEffectLifetime = 24.0F * 60.0F * 60.0F;
        constexpr float kMinimumModelDiameter = 0.001F;

        [[nodiscard]] float ScaleModelToDiameter(
            std::string_view a_model,
            float a_targetDiameter,
            float a_fallbackScale) noexcept
        {
            try {
                RE::NiPointer<RE::NiNode> model;
                const RE::BSModelDB::DBTraits::ArgsType loadArguments;
                const auto result = RE::BSModelDB::Demand(
                    a_model.data(), model, loadArguments);
                if (result != RE::BSResource::ErrorCode::kNone || !model) {
                    logger::warn("Cannot inspect debug marker model '{}' (error {})",
                        a_model,
                        std::to_underlying(result));
                    return a_fallbackScale;
                }

                RE::NiUpdateData updateData{
                    RE::Main::QFrameAnimTime(),
                    RE::NiUpdateData::Flag::kDirty,
                };
                model->Update(updateData);
                const auto modelDiameter = model->worldBound.radius * 2.0F;
                if (!std::isfinite(modelDiameter) || modelDiameter < kMinimumModelDiameter) {
                    logger::warn("Debug marker model '{}' has no usable bound", a_model);
                    return a_fallbackScale;
                }

                return a_targetDiameter / modelDiameter;
            } catch (...) {
                return a_fallbackScale;
            }
        }

        void ExpireEffect(RE::NiPointer<RE::BSTempEffectParticle>& a_effect) noexcept
        {
            if (!a_effect) {
                return;
            }

            try {
                a_effect->lifetime = 0.0F;
                a_effect->age = 0.0F;
                a_effect->Detach();
            } catch (...) {
            }
            a_effect.reset();
        }

        [[nodiscard]] bool PrepareLoadedEffect(RE::BSTempEffectParticle* a_effect) noexcept
        {
            try {
                if (!a_effect || !a_effect->particleObject) {
                    return false;
                }
                a_effect->particleObject->CullNode(false);
                a_effect->particleObject->SetCollisionLayer(RE::COL_LAYER::kNonCollidable);
                return true;
            } catch (...) {
                return false;
            }
        }
    }

    WorldDebugVisualization* WorldDebugVisualization::GetSingleton() noexcept
    {
        static WorldDebugVisualization singleton;
        return std::addressof(singleton);
    }

    bool WorldDebugVisualization::CreateMarker(
        const Vec3& a_position,
        const Vec3& a_forward) noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        try {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            if (!cell) {
                logger::warn("Cannot create debug anchor marker: player cell is unavailable");
                return false;
            }

            const RE::NiPoint3 position{ a_position.x, a_position.y, a_position.z };
            RE::NiMatrix3 arrowRotation;
            arrowRotation.MakeZRotation(std::atan2(a_forward.x, a_forward.y));

            const auto arrowScale = ScaleModelToDiameter(kArrowModel, kArrowLength, 0.25F);

            arrow_ = RE::NiPointer<RE::BSTempEffectParticle>{ cell->PlaceParticleEffect(
                kEffectLifetime,
                kArrowModel.data(),
                arrowRotation,
                position,
                arrowScale,
                0,
                nullptr) };

            if (!arrow_) {
                logger::warn("Cannot create debug anchor arrow effect");
                DestroyMarker();
                return false;
            }

            arrowPrepared_ = PrepareLoadedEffect(arrow_.get());
            logger::info(
                "Debug anchor marker created at ({:.2f}, {:.2f}, {:.2f}), forward ({:.3f}, {:.3f}, {:.3f})",
                a_position.x,
                a_position.y,
                a_position.z,
                a_forward.x,
                a_forward.y,
                a_forward.z);
            return true;
        } catch (const std::exception& exception) {
            try {
                logger::warn("Cannot create debug anchor marker: {}", exception.what());
            } catch (...) {
            }
            DestroyMarker();
            return false;
        } catch (...) {
            DestroyMarker();
            return false;
        }
#else
        static_cast<void>(a_position);
        static_cast<void>(a_forward);
        return true;
#endif
    }

    bool WorldDebugVisualization::UpdateMarker(
        const Vec3& a_position,
        const Vec3& a_forward) noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        try {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            if (!arrow_ || !cell || arrow_->cell != cell) {
                return false;
            }

            RE::NiMatrix3 arrowRotation;
            arrowRotation.MakeZRotation(std::atan2(a_forward.x, a_forward.y));
            arrow_->particleEffectTransform.translate = {
                a_position.x, a_position.y, a_position.z };
            arrow_->particleEffectTransform.rotate = arrowRotation;

            if (arrow_->particleObject) {
                arrow_->particleObject->local = arrow_->particleEffectTransform;
                RE::NiUpdateData updateData{
                    RE::Main::QFrameAnimTime(),
                    RE::NiUpdateData::Flag::kDirty,
                };
                arrow_->particleObject->Update(updateData);
            }
            arrowPrepared_ = PrepareLoadedEffect(arrow_.get());

            logger::info(
                "Debug anchor marker moved to ({:.2f}, {:.2f}, {:.2f}), forward ({:.3f}, {:.3f}, {:.3f})",
                a_position.x,
                a_position.y,
                a_position.z,
                a_forward.x,
                a_forward.y,
                a_forward.z);
            return true;
        } catch (const std::exception& exception) {
            try {
                logger::warn("Cannot update debug anchor marker: {}", exception.what());
            } catch (...) {
            }
            return false;
        } catch (...) {
            return false;
        }
#else
        static_cast<void>(a_position);
        static_cast<void>(a_forward);
        return true;
#endif
    }

    bool WorldDebugVisualization::ShowAnchor(
        const Vec3& a_position,
        const Vec3& a_forward) noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        if (UpdateMarker(a_position, a_forward)) {
            return true;
        }
        DestroyMarker();
        return CreateMarker(a_position, a_forward);
#else
        static_cast<void>(a_position);
        static_cast<void>(a_forward);
        return true;
#endif
    }

    void WorldDebugVisualization::Update() noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        if (arrow_ && !arrowPrepared_) {
            arrowPrepared_ = PrepareLoadedEffect(arrow_.get());
        }
#endif
    }

    void WorldDebugVisualization::HideAnchor() noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        DestroyMarker();
#endif
    }

    void WorldDebugVisualization::DestroyMarker() noexcept
    {
#if defined(SSC_ENABLE_DEBUG_ANCHOR)
        const auto hadMarker = static_cast<bool>(arrow_);
        ExpireEffect(arrow_);
        arrowPrepared_ = false;
        if (hadMarker) {
            try {
                logger::info("Debug anchor marker removed");
            } catch (...) {
            }
        }
#endif
    }
}
