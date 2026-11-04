#pragma once

#include "mc/types.hpp"

#include <cstdint>

namespace mc {

// Physics capabilities a host adapter may or may not implement.
//
// Every method below stays on IPhysicsAdapter (the rule engines call it unconditionally), but a port
// that cannot do one of them says so in supportedPhysicsFeatures(). mc::DegradingPhysicsPort then turns
// the unsupported calls into safe no-ops, and Session::missingPhysicsFeatures() reports what is left.
// docs/REVERSE_INTERFACES.md has the per-feature table: what each one enables and how to verify it.
enum class PhysicsFeature : uint32_t {
    RaycastWorld = 1u << 0,        // raycastWorld: targeting, block placement, projectile collision
    BlockCollision = 1u << 1,      // createBlockCollider/destroyBlockCollider: placed blocks stop bodies
    LinearImpulse = 1u << 2,       // applyLinearImpulse: knockback / stagger pushes
    LinearVelocityWrite = 1u << 3, // setLinearVelocity: the host player is driven while gliding
    JumpImpulse = 1u << 4,         // host-side jump impulse (no IPhysicsAdapter method yet)
    FallControl = 1u << 5,         // host fall velocity/damage can be controlled (no IPhysicsAdapter method yet)
};

inline constexpr uint32_t kAllPhysicsFeatures = (1u << 6) - 1;

[[nodiscard]] inline const char* physicsFeatureName(PhysicsFeature f) {
    switch (f) {
        case PhysicsFeature::RaycastWorld: return "RaycastWorld";
        case PhysicsFeature::BlockCollision: return "BlockCollision";
        case PhysicsFeature::LinearImpulse: return "LinearImpulse";
        case PhysicsFeature::LinearVelocityWrite: return "LinearVelocityWrite";
        case PhysicsFeature::JumpImpulse: return "JumpImpulse";
        case PhysicsFeature::FallControl: return "FallControl";
    }
    return "Unknown";
}

class IPhysicsAdapter {
public:
    virtual ~IPhysicsAdapter() = default;

    // Bitmask of PhysicsFeature values this adapter really implements. Defaults to all of them so
    // existing adapters keep their previous behaviour; a partial port overrides it.
    [[nodiscard]] virtual uint32_t supportedPhysicsFeatures() const { return kAllPhysicsFeatures; }

    // Raycast probe in the host world
    virtual RaycastResult raycastWorld(const Vec3& start, const Vec3& end, EntityId ignore_entity = EntityId::None) = 0;

    // Spawn 1x1x1m physical box collider in host physics system
    virtual uint64_t createBlockCollider(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) = 0;
    virtual void destroyBlockCollider(uint64_t collider_handle) = 0;

    // Apply linear impulse / knockback / ragdoll force to entities
    virtual void applyLinearImpulse(EntityId entity_id, const Vec3& impulse) = 0;

    // Replace (not add to) an entity's velocity, in canonical MC space (cm/s). Used to drive the
    // local player while gliding. A no-op for unknown entities.
    virtual void setLinearVelocity(EntityId entity_id, const Vec3& velocity) = 0;
};

} // namespace mc
