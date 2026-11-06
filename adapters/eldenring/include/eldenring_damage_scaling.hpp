#pragma once

// Elden Ring wiring of mc::DamageScalingConfig and mc::HealthMirror.
//
// The ER damage model is a hybrid: a flat term (so low-HP enemies are not sponge-scaled) plus a
// percentage of the victim's max HP (so bosses scale fairly). This header keeps the percentage
// term unchanged and routes the flat term through mc::scaleMcToHost, so one configurable rule
// (mc_to_host_base + mc_to_host_per_level * host level) now decides MC weapon damage in ER.
//
// Defaults reproduce the previous hardcoded behaviour exactly: base 20 turns a 7-damage diamond
// sword into 140, and hp_percent 0.02 keeps the boss term. `enabled == false` reproduces the old
// erDamage() formula bit for bit, so an existing install can fall back with one ini key.
//
// The heart row is host-authoritative: ER HP is the real bar and the Minecraft hearts mirror its
// fraction, which is exactly mc::HealthMirror::Authority::Host.

#include "mc/combat.hpp"
#include "mc/damage_scaling.hpp"

#include <algorithm>
#include <cmath>

namespace eldenring::live {

struct ErDamageConfig {
    mc::DamageScalingConfig scaling{};
    float hp_percent{0.02f};
    bool enabled{true};
};

// ER's historical defaults: 7 MC damage -> 140 flat damage, 2% of max HP, no level term.
inline ErDamageConfig defaultErDamageConfig() {
    ErDamageConfig config;
    config.scaling.mc_to_host_base = 20.0f;
    config.scaling.mc_to_host_per_level = 0.0f;
    config.scaling.host_to_mc_divisor = 5.0f;
    return config;
}

// Damage in ER hit points for a landed MC hit.
// host_level is the victim's level when one is known; ER does not expose one, so callers pass 0.
inline int erDamageScaled(const mc::HitIntent& intent, int victim_max_hp, int host_level,
                          const ErDamageConfig& config) {
    if (!(intent.damage > 0.0f)) return 0;
    constexpr float kReferenceDamage = 7.0f;

    const float flat = config.enabled
                           ? mc::scaleMcToHost(intent.damage, host_level, config.scaling)
                           : intent.damage * defaultErDamageConfig().scaling.mc_to_host_base;

    float percent_damage = 0.0f;
    if (victim_max_hp > 0) {
        percent_damage = static_cast<float>(victim_max_hp) * config.hp_percent * (intent.damage / kReferenceDamage);
    }

    const float total = flat + percent_damage;
    if (!(total > 0.0f)) return 0;
    return std::max(1, static_cast<int>(std::lround(total)));
}

// The heart row the HUD draws: 20 half-hearts for a full bar, ceil so any damage shows.
// This is mc::HealthMirror with ER as the authoritative side.
inline int heartHalvesFromVitals(int hp, int max_hp) {
    if (max_hp <= 0) return 0;
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Host, 20.0f);
    const float fraction = static_cast<float>(hp) / static_cast<float>(max_hp);
    mirror.observeRemoteHp(fraction);
    return static_cast<int>(std::ceil(mirror.localFraction() * 20.0f));
}

} // namespace eldenring::live
