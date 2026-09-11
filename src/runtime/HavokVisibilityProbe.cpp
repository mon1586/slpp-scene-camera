#include "runtime/HavokVisibilityProbe.h"

namespace ssc::runtime
{
    namespace
    {
        constexpr float kStartHitFraction = 1.0e-4F;
        constexpr float kCharacterAdvanceDistance = 0.5F;
        constexpr std::size_t kMaximumCharacterPasses = 256;

        [[nodiscard]] bool IsFinite(const Vec3& a_value) noexcept
        {
            return std::isfinite(a_value.x) &&
                   std::isfinite(a_value.y) &&
                   std::isfinite(a_value.z);
        }

        [[nodiscard]] Vec3 Interpolate(
            const Vec3& a_start,
            const Vec3& a_target,
            float a_fraction) noexcept
        {
            return {
                std::lerp(a_start.x, a_target.x, a_fraction),
                std::lerp(a_start.y, a_target.y, a_fraction),
                std::lerp(a_start.z, a_target.z, a_fraction),
            };
        }

        [[nodiscard]] bool IsCharacterLayer(RE::COL_LAYER a_layer) noexcept
        {
            switch (a_layer) {
            case RE::COL_LAYER::kBiped:
            case RE::COL_LAYER::kCharController:
            case RE::COL_LAYER::kDeadBip:
            case RE::COL_LAYER::kBipedNoCC:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool IsCharacterHit(
            const RE::hkpWorldRayCastOutput& a_output,
            const RE::TESObjectREFR* a_reference,
            std::uint32_t a_targetActorID) noexcept
        {
            if (a_reference &&
                (a_reference->IsActor() || a_reference->GetFormID() == a_targetActorID)) {
                return true;
            }
            return a_output.rootCollidable &&
                IsCharacterLayer(a_output.rootCollidable->GetCollisionLayer());
        }

    }

    HavokVisibilityProbe* HavokVisibilityProbe::GetSingleton() noexcept
    {
        static HavokVisibilityProbe singleton;
        return std::addressof(singleton);
    }

    VisibilityRayHit HavokVisibilityProbe::Trace(
        const Vec3& a_start,
        const Vec3& a_target,
        std::uint32_t a_targetActorID) noexcept
    {
        // Cast from the anchor so a camera below terrain is approached from
        // the front of the ground surface. No second cast is needed.
        auto result = TraceDirection(a_target, a_start, a_targetActorID);
        if (result.querySucceeded && !result.reachesTarget) {
            result.fraction = 1.0F - result.fraction;
        }
        // The physical cast starts at the anchor, not the camera. Preserve its
        // world-space hit/normal but do not label an anchor hit as camera-inside.
        result.startsInsideCollision = false;
        return result;
    }

    VisibilityRayHit HavokVisibilityProbe::TraceDirection(
        const Vec3& a_start,
        const Vec3& a_target,
        std::uint32_t a_targetActorID) noexcept
    {
        VisibilityRayHit result;
        if (!IsFinite(a_start) || !IsFinite(a_target)) {
            result.object = "invalid ray coordinates";
            return result;
        }

        try {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            auto* bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
            auto* world = bhkWorld ? bhkWorld->GetWorld1() : nullptr;
            if (!world) {
                result.object = "physics world unavailable";
                return result;
            }

            const auto worldScale = RE::bhkWorld::GetWorldScale();
            if (!std::isfinite(worldScale) || worldScale <= 0.0F) {
                result.object = "invalid Havok world scale";
                return result;
            }

            const auto deltaX = a_target.x - a_start.x;
            const auto deltaY = a_target.y - a_start.y;
            const auto deltaZ = a_target.z - a_start.z;
            const auto rayLength = std::sqrt(
                deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            if (!std::isfinite(rayLength)) {
                result.object = "invalid ray length";
                return result;
            }
            if (rayLength <= std::numeric_limits<float>::epsilon()) {
                result.querySucceeded = true;
                result.reachesTarget = true;
                return result;
            }

            const auto advanceFraction = kCharacterAdvanceDistance / rayLength;
            auto castStartFraction = 0.0F;
            for (std::size_t pass = 0; pass < kMaximumCharacterPasses; ++pass) {
                const auto castStart = Interpolate(a_start, a_target, castStartFraction);
                RE::hkpWorldRayCastInput input{};
                input.from = RE::hkVector4{
                    castStart.x * worldScale,
                    castStart.y * worldScale,
                    castStart.z * worldScale,
                    0.0F };
                input.to = RE::hkVector4{
                    a_target.x * worldScale,
                    a_target.y * worldScale,
                    a_target.z * worldScale,
                    0.0F };
                input.enableShapeCollectionFilter = true;
                input.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLineOfSight);

                RE::hkpWorldRayCastOutput output{};
                output.Reset();
                ++result.queryCount;
                world->CastRay(input, output);
                result.querySucceeded = true;

                if (!output.HasHit()) {
                    result.reachesTarget = true;
                    result.fraction = 1.0F;
                    result.position.reset();
                    result.normal.reset();
                    result.objectID = 0;
                    result.object.clear();
                    return result;
                }

                const auto localFraction = std::clamp(output.hitFraction, 0.0F, 1.0F);
                const auto hitFraction = std::lerp(castStartFraction, 1.0F, localFraction);
                auto* hitReference = output.rootCollidable ?
                    RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable) : nullptr;

                if (IsCharacterHit(output, hitReference, a_targetActorID)) {
                    const auto nextStart = std::max(
                        hitFraction + advanceFraction,
                        castStartFraction + advanceFraction);
                    if (!std::isfinite(nextStart) || nextStart >= 1.0F) {
                        result.reachesTarget = true;
                        result.fraction = 1.0F;
                        result.position.reset();
                        result.normal.reset();
                        result.objectID = 0;
                        result.object.clear();
                        return result;
                    }
                    castStartFraction = nextStart;
                    continue;
                }

                result.fraction = hitFraction;
                result.position = Interpolate(a_start, a_target, result.fraction);
                result.normal = Vec3{
                    output.normal.quad.m128_f32[0],
                    output.normal.quad.m128_f32[1],
                    output.normal.quad.m128_f32[2],
                };
                result.startsInsideCollision =
                    castStartFraction <= kStartHitFraction &&
                    localFraction <= kStartHitFraction;

                if (hitReference) {
                    result.objectID = hitReference->GetFormID();
                    result.object = fmt::format(
                        "{:08X} ({})",
                        result.objectID,
                        output.rootCollidable->GetCollisionLayer());
                } else if (output.rootCollidable) {
                    result.object = fmt::format(
                        "world ({})",
                        output.rootCollidable->GetCollisionLayer());
                } else {
                    result.object = "world";
                }
                return result;
            }

            result.querySucceeded = false;
            result.reachesTarget = false;
            result.object = "character collision traversal limit reached";
            return result;
        } catch (const std::exception& exception) {
            try {
                result.object = fmt::format("raycast exception: {}", exception.what());
            } catch (...) {
                result.object = "raycast exception";
            }
            return result;
        } catch (...) {
            result.object = "unknown raycast exception";
            return result;
        }
    }
}
