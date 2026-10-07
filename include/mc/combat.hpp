#pragma once

#include "mc/types.hpp"

namespace mc {

class ICombatAdapter;

struct HitIntent {
    EntityId attacker_id{EntityId::None};
    EntityId victim_id{EntityId::None};
    float damage{0.f};
    float max_hp_percent{0.f}; // Dynamic balance for Bosses
    Vec3 hit_location{};
    Vec3 knockback_vector{};
    float knockback_force{0.f};
    bool is_critical{false};
    bool is_sweeping{false};
};

class CombatEngine {
public:
    explicit CombatEngine(ICombatAdapter& combat);

    HitIntent calculateMeleeHit(
        EntityId attacker_id,
        EntityId victim_id,
        ItemId weapon,
        float attack_cooldown, // [0, 1] 1 = fully charged
        bool is_falling,       // critical condition
        bool is_on_ground,     // sweeping condition
        const Vec3& hit_location,
        const Vec3& attack_direction
    );

    bool executeHit(const HitIntent& intent);

private:
    ICombatAdapter& combat_;
};

} // namespace mc
