#pragma once

#include "mc/combat.hpp"
#include "mc/types.hpp"

namespace mc {

class ICombatAdapter {
public:
    virtual ~ICombatAdapter() = default;

    // Apply the state part of a HitIntent: damage, death, posture and similar host bookkeeping.
    // Must NOT trigger knockback or stagger: CombatEngine::executeHit calls
    // triggerStaggerOrRagdoll() once after this returns true. Return false when the hit is
    // rejected (unknown or dead target); no reaction is triggered then.
    virtual bool processHit(const HitIntent& intent) = 0;

    // Retrieve entity maximum health for percentage-based boss balancing
    virtual float getMaxHealth(EntityId entity_id) = 0;

    // Trigger physical stagger, hit flinch, or ragdoll. Called by the core only (once per
    // accepted hit with non-zero knockback); adapters must not call it from processHit.
    virtual void triggerStaggerOrRagdoll(EntityId entity_id, const Vec3& direction, float force) = 0;
};

} // namespace mc
