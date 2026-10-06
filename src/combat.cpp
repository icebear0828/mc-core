#include "mc/combat.hpp"
#include "mc/contracts/combat_adapter.hpp"

namespace mc {

CombatEngine::CombatEngine(ICombatAdapter& combat)
    : combat_(combat) {}

HitIntent CombatEngine::calculateMeleeHit(
    uint64_t attacker_id,
    uint64_t victim_id,
    ItemId weapon,
    float attack_cooldown,
    bool is_falling,
    bool is_on_ground,
    const Vec3& hit_location,
    const Vec3& attack_direction
) {
    HitIntent intent;
    intent.attacker_id = attacker_id;
    intent.victim_id = victim_id;
    intent.hit_location = hit_location;
    intent.knockback_vector = attack_direction.normalized();

    // Base damage according to MC Java weapon table
    float base_damage = 1.0f; // Bare hand
    if (weapon == ItemId::DiamondSword) {
        base_damage = 7.0f;
    } else if (weapon == ItemId::DiamondPickaxe) {
        base_damage = 5.0f;
    } else if (weapon == ItemId::Trident) {
        base_damage = 9.0f;
    }

    // Cooldown scaling
    const float cooldown_factor = 0.2f + 0.8f * (attack_cooldown * attack_cooldown);
    float damage = base_damage * cooldown_factor;

    // Critical hit condition: player falling and cooldown >= 84.8%
    if (is_falling && attack_cooldown > 0.848f) {
        intent.is_critical = true;
        damage *= 1.5f;
    }

    // Sweeping edge condition: diamond sword, on ground, full cooldown
    if (weapon == ItemId::DiamondSword && is_on_ground && attack_cooldown > 0.9f && !intent.is_critical) {
        intent.is_sweeping = true;
    }

    intent.damage = damage;
    // Percentage damage for robust boss scaling (e.g., 7% per diamond sword hit)
    intent.max_hp_percent = (weapon == ItemId::DiamondSword) ? 0.05f : 0.02f;
    intent.knockback_force = (intent.is_critical ? 1200.f : 800.f) * cooldown_factor;

    return intent;
}

bool CombatEngine::executeHit(const HitIntent& intent) {
    if (intent.victim_id == 0) {
        return false;
    }

    const bool processed = combat_.processHit(intent);
    if (processed && intent.knockback_force > 0.f) {
        combat_.triggerStaggerOrRagdoll(intent.victim_id, intent.knockback_vector, intent.knockback_force);
    }
    return processed;
}

} // namespace mc
