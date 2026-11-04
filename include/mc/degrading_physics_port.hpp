#pragma once

#include "mc/contracts/physics_adapter.hpp"

#include <cstdint>

namespace mc {

// Decorator over IPhysicsAdapter for hosts that cannot implement every physics capability.
//
// Supported calls are forwarded unchanged. Unsupported ones degrade to the same harmless defaults the
// rule engines already tolerate, so no engine needs a per-capability branch:
//   RaycastWorld missing        -> raycastWorld reports no hit
//   BlockCollision missing      -> createBlockCollider returns 0, destroyBlockCollider does nothing
//   LinearImpulse missing       -> applyLinearImpulse does nothing
//   LinearVelocityWrite missing -> setLinearVelocity does nothing
//
// `missing()` reports the unsupported bits; Session::missingPhysicsFeatures() exposes them to the host.
class DegradingPhysicsPort final : public IPhysicsAdapter {
public:
    explicit DegradingPhysicsPort(IPhysicsAdapter& inner)
        : inner_(inner), supported_(inner.supportedPhysicsFeatures() & kAllPhysicsFeatures) {}

    [[nodiscard]] uint32_t supportedPhysicsFeatures() const override { return supported_; }
    [[nodiscard]] uint32_t supported() const { return supported_; }
    [[nodiscard]] uint32_t missing() const { return kAllPhysicsFeatures & ~supported_; }
    [[nodiscard]] bool supports(PhysicsFeature feature) const {
        return (supported_ & static_cast<uint32_t>(feature)) != 0u;
    }

    RaycastResult raycastWorld(const Vec3& start, const Vec3& end,
                               EntityId ignore_entity = EntityId::None) override {
        if (!supports(PhysicsFeature::RaycastWorld)) {
            return {};
        }
        return inner_.raycastWorld(start, end, ignore_entity);
    }

    uint64_t createBlockCollider(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) override {
        if (!supports(PhysicsFeature::BlockCollision)) {
            return 0;
        }
        return inner_.createBlockCollider(grid_pos, block_id, world_pos);
    }

    void destroyBlockCollider(uint64_t collider_handle) override {
        if (!supports(PhysicsFeature::BlockCollision)) {
            return;
        }
        inner_.destroyBlockCollider(collider_handle);
    }

    void applyLinearImpulse(EntityId entity_id, const Vec3& impulse) override {
        if (!supports(PhysicsFeature::LinearImpulse)) {
            return;
        }
        inner_.applyLinearImpulse(entity_id, impulse);
    }

    void setLinearVelocity(EntityId entity_id, const Vec3& velocity) override {
        if (!supports(PhysicsFeature::LinearVelocityWrite)) {
            return;
        }
        inner_.setLinearVelocity(entity_id, velocity);
    }

private:
    IPhysicsAdapter& inner_;
    uint32_t supported_;
};

} // namespace mc
