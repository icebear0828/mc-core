#pragma once

#include "mc/types.hpp"

namespace mc {

class IPhysicsAdapter {
public:
    virtual ~IPhysicsAdapter() = default;

    // Raycast probe in the host world
    virtual RaycastResult raycastWorld(const Vec3& start, const Vec3& end, uint64_t ignore_entity = 0) = 0;

    // Spawn 1x1x1m physical box collider in host physics system
    virtual uint64_t createBlockCollider(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) = 0;
    virtual void destroyBlockCollider(uint64_t collider_handle) = 0;

    // Apply linear impulse / knockback / ragdoll force to entities
    virtual void applyLinearImpulse(uint64_t entity_id, const Vec3& impulse) = 0;
};

} // namespace mc
