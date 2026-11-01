#pragma once

// Overwatch (Doomfist) strikes: the hand cannon, rocket punch, rising uppercut
// and seismic slam. Pure logic only — which enemies a strike reaches and how
// much ER damage each one takes. The game-facing half is
// CombatService::fireOwStrike, which reads the camera/enemy cache and feeds the
// existing DamageQueue.
//
// This header depends on the shared eldenring_hitscan.hpp, not on anything from
// the CS2 subsystem: an OW-only build compiles and tests it on its own.

#include "eldenring_hitscan.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace eldenring::ow {

// DamageQueue tag bit: this hit came from a Doomfist strike. drainDamageQueue
// uses it to skip the Minecraft melee hit sound, exactly like a CS bullet.
inline constexpr uint32_t kOwStrikeTag = 16u;

// Values match ModeConfig::ow_state (0 idle, 2 cannon, 3 punch, 4 uppercut,
// 5 slam); the dispatcher passes the same numbers through HotkeyCallbacks.
enum class OwStrikeKind : uint8_t {
    HandCannon = 0,
    RocketPunch = 1,
    Uppercut = 2,
    Slam = 3,
};

struct OwStrikeParams {
    hitscan::ShotParams shot{};
    // > 0: radial strike around the player's feet instead of a camera ray.
    float aoe_radius_m{0.f};
};

// Overwatch damage numbers mapped through the same 5%-per-7-damage conversion
// as the MC bow and CS bullets: 40 (hand cannon) ~= 29% of a 219 HP enemy,
// 100 (rocket punch) ~= 71%, 90 (slam) ~= 64%.
inline OwStrikeParams owStrikeParamsFor(OwStrikeKind kind) {
    OwStrikeParams p;
    switch (kind) {
        case OwStrikeKind::HandCannon:
            p.shot.damage = 40.f;
            p.shot.max_range_m = 60.f;
            p.shot.hit_radius_m = 0.8f;
            p.shot.falloff_start = 0.55f;
            p.shot.min_damage_scale = 0.45f;
            break;
        case OwStrikeKind::RocketPunch:
            p.shot.damage = 100.f;
            p.shot.max_range_m = 5.f;
            p.shot.hit_radius_m = 1.0f;
            p.shot.falloff_start = 0.8f;
            p.shot.min_damage_scale = 0.6f;
            break;
        case OwStrikeKind::Uppercut:
            p.shot.damage = 60.f;
            p.shot.max_range_m = 4.f;
            p.shot.hit_radius_m = 1.0f;
            p.shot.falloff_start = 0.8f;
            p.shot.min_damage_scale = 0.6f;
            break;
        case OwStrikeKind::Slam:
            p.shot.damage = 90.f;
            p.shot.max_range_m = 0.f;
            p.aoe_radius_m = 7.f;
            break;
    }
    return p;
}

struct OwStrikeHit {
    int index{-1};      // index into the enemy list
    float distance{0.f};
    int er_damage{0};
};

// One camera-ray strike (hand cannon, rocket punch, uppercut). `cam_rel` is the
// camera position relative to the player's feet, `dir` the unit aim direction.
inline OwStrikeHit traceOwStrike(const float cam_rel[3], const float dir[3],
                                 const std::vector<live::EnemyInfo>& enemies,
                                 const OwStrikeParams& p) {
    OwStrikeHit out;
    const hitscan::ShotTrace trace = hitscan::traceTargets(cam_rel, dir, enemies, p.shot);
    if (trace.index < 0) return out;
    const live::EnemyInfo& victim = enemies[static_cast<size_t>(trace.index)];
    out.index = trace.index;
    out.distance = trace.distance;
    out.er_damage = hitscan::damageToEr(victim.max_hp, p.shot.damage, trace.distance, p.shot);
    return out;
}

// Radial strike (seismic slam): every living hostile within aoe_radius_m of the
// player's feet, with a linear falloff from full damage at the centre to
// min_damage_scale at the edge.
inline std::vector<OwStrikeHit> collectOwAoeHits(const std::vector<live::EnemyInfo>& enemies,
                                                 const OwStrikeParams& p) {
    std::vector<OwStrikeHit> hits;
    if (!(p.aoe_radius_m > 0.f) || !(p.shot.damage > 0.f)) return hits;
    const float min_scale = std::clamp(p.shot.min_damage_scale, 0.f, 1.f);
    for (size_t i = 0; i < enemies.size(); ++i) {
        const live::EnemyInfo& e = enemies[i];
        if (e.hp <= 0 || !e.hostile) continue;
        const float d = std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z);
        if (d > p.aoe_radius_m) continue;
        const float u = std::clamp(d / p.aoe_radius_m, 0.f, 1.f);
        const float scale = 1.f + (min_scale - 1.f) * u;
        const int er_damage = hitscan::damageToEr(e.max_hp, p.shot.damage * scale);
        if (er_damage <= 0) continue;
        hits.push_back(OwStrikeHit{static_cast<int>(i), d, er_damage});
    }
    return hits;
}

} // namespace eldenring::ow
