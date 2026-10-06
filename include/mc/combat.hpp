#pragma once

#include "mc/types.hpp"

namespace mc {

class ICombatAdapter;

struct HitIntent {
    uint64_t attacker_id{0};
    uint64_t victim_id{0};
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
        uint64_t attacker_id,
        uint64_t victim_id,
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
