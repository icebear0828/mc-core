#pragma once

#include "mc/combat.hpp"
#include "mc/types.hpp"

namespace mc {

class ICombatAdapter {
public:
    virtual ~ICombatAdapter() = default;

    // Convert MC HitIntent to native damage / stagger / reaction
    virtual bool processHit(const HitIntent& intent) = 0;

    // Retrieve entity maximum health for percentage-based boss balancing
    virtual float getMaxHealth(uint64_t entity_id) = 0;

    // Trigger physical stagger, hit flinch, or ragdoll
    virtual void triggerStaggerOrRagdoll(uint64_t entity_id, const Vec3& direction, float force) = 0;
};

} // namespace mc
