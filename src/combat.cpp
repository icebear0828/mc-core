#include "mc/combat.hpp"
#include "mc/contracts/combat_adapter.hpp"

namespace mc {

CombatEngine::CombatEngine(ICombatAdapter& combat)
    : combat_(combat) {}

HitIntent CombatEngine::calculateMeleeHit(
    EntityId attacker_id,
    EntityId victim_id,
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
    switch (weapon) {
        case ItemId::WoodenSword: base_damage = 4.0f; break;
        case ItemId::StoneSword: base_damage = 5.0f; break;
        case ItemId::IronSword: base_damage = 6.0f; break;
        case ItemId::DiamondSword: base_damage = 7.0f; break;
        case ItemId::NetheriteSword: base_damage = 8.0f; break;
        case ItemId::WoodenPickaxe: base_damage = 2.0f; break;
        case ItemId::StonePickaxe: base_damage = 3.0f; break;
        case ItemId::IronPickaxe: base_damage = 4.0f; break;
        case ItemId::GoldenPickaxe: base_damage = 2.0f; break;
        case ItemId::DiamondPickaxe: base_damage = 5.0f; break;
        case ItemId::NetheritePickaxe: base_damage = 6.0f; break;
        case ItemId::Trident: base_damage = 9.0f; break;
        default: break;
    }

    // Cooldown scaling
    const float cooldown_factor = 0.2f + 0.8f * (attack_cooldown * attack_cooldown);
    float damage = base_damage * cooldown_factor;

    // Critical hit condition: player falling and cooldown >= 84.8%
    if (is_falling && attack_cooldown > 0.848f) {
        intent.is_critical = true;
        damage *= 1.5f;
    }

    const bool sword = weapon == ItemId::WoodenSword || weapon == ItemId::StoneSword || weapon == ItemId::IronSword ||
                       weapon == ItemId::DiamondSword || weapon == ItemId::NetheriteSword;
    // Sweeping edge condition: a sword, on ground, full cooldown
    if (sword && is_on_ground && attack_cooldown > 0.9f && !intent.is_critical) {
        intent.is_sweeping = true;
    }

    intent.damage = damage;
    // Percentage damage for robust boss scaling, following the sword ladder (diamond stays at 5%).
    switch (weapon) {
        case ItemId::WoodenSword: intent.max_hp_percent = 0.03f; break;
        case ItemId::StoneSword: intent.max_hp_percent = 0.035f; break;
        case ItemId::IronSword: intent.max_hp_percent = 0.04f; break;
        case ItemId::DiamondSword: intent.max_hp_percent = 0.05f; break;
        case ItemId::NetheriteSword: intent.max_hp_percent = 0.06f; break;
        default: intent.max_hp_percent = 0.02f; break;
    }
    intent.knockback_force = (intent.is_critical ? 1200.f : 800.f) * cooldown_factor;

    return intent;
}

bool CombatEngine::executeHit(const HitIntent& intent) {
    if (intent.victim_id == EntityId::None) {
        return false;
    }

    const bool processed = combat_.processHit(intent);
    if (processed && intent.knockback_force > 0.f) {
        combat_.triggerStaggerOrRagdoll(intent.victim_id, intent.knockback_vector, intent.knockback_force);
    }
    return processed;
}

} // namespace mc
