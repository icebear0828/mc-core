#pragma once

#include "mc/types.hpp"

namespace mc {

class IPhysicsAdapter {
public:
    virtual ~IPhysicsAdapter() = default;

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
