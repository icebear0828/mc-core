#pragma once

#include "mc/combat.hpp"
#include "mc/inventory.hpp"
#include "mc/melee.hpp"
#include "mc/session.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <cmath>

namespace eldenring::live {

using mc::MeleeController;
using mc::JumpTracker;
using mc::HitFeedback;

// Elden Ring hit points for a landed MC hit:
// Hybrid scaling combines a fixed base damage (so low-HP enemies like soldiers and wolves do not suffer sponge-scaling)
// and a percentage of max health (so bosses scale fairly and smoothly).
// base_scale: damage multiplier for flat portion (default 20.0f, so 7.0 sword damage = 140 base damage).
// hp_percent_override: if >= 0.0f, overrides intent.max_hp_percent (default -1.0f uses intent's percent).
inline int erDamage(const mc::HitIntent& intent, int victim_max_hp, float base_scale = 20.0f, float hp_percent_override = -1.0f) {
    if (intent.damage <= 0.0f) return 0;
    constexpr float kReferenceDamage = 7.0f;
    const float base_dmg = intent.damage * base_scale;
    float percent_dmg = 0.0f;
    if (victim_max_hp > 0) {
        const float percent = (hp_percent_override >= 0.0f) ? hp_percent_override : intent.max_hp_percent;
        percent_dmg = static_cast<float>(victim_max_hp) * percent * (intent.damage / kReferenceDamage);
    }
    const float total = base_dmg + percent_dmg;
    if (total <= 0.0f) return 0;
    return std::max(1, static_cast<int>(std::lround(total)));
}

} // namespace eldenring::live
