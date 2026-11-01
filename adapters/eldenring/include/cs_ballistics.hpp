#pragma once

// Hitscan ballistics for the CS2 arsenal in Elden Ring. The shared geometry and
// damage conversion live in eldenring_hitscan.hpp (also used by the Overwatch
// strikes); this header keeps the CS2 names and the tracer policy.
//
// The game-facing half lives in CombatService::fireCsShot: it reads the camera
// and the enemy cache, calls this header, and enqueues the hit in the existing
// DamageQueue (vfunc[7]), which already applies native HP loss, stagger, aggro,
// sound and kill credit.

#include "eldenring_hitscan.hpp"

#include <algorithm>
#include <cstdint>

namespace eldenring::cs {

inline constexpr float kCsPi = hitscan::kPi;
// DamageQueue tag bit: this hit came from a CS bullet. drainDamageQueue uses it
// for the hitmarker instead of the Minecraft melee sounds.
inline constexpr uint32_t kCsShotTag = 8u;

struct CsShotParams : hitscan::ShotParams {
    // CS2 m_nTracerFrequency: a tracer every N rounds (AK/M4 = 3, AWP = 1, 0 = never).
    uint32_t tracer_frequency{1};
    uint8_t weapon_slot{0}; // 0..7, only used to keep one shot counter per weapon
};

using CsShotTrace = hitscan::ShotTrace;
using CsShotRng = hitscan::ShotRng;
using hitscan::damageScaleAtRange;
using hitscan::damageToEr;
using hitscan::normalize3;
using hitscan::orthonormalBasis;
using hitscan::rayVerticalCylinder;
using hitscan::spreadDirection;

inline CsShotTrace traceTargets(const float cam_rel[3], const float dir[3],
                                const std::vector<live::EnemyInfo>& enemies,
                                const CsShotParams& p) {
    return hitscan::traceTargets(cam_rel, dir, enemies, p);
}

// CS2 tracers are short streaks, not a laser from muzzle to target. The head
// travels at this speed; the overlay draws a few metres of tail behind it.
//
// The lifetime is a *visual* floor, not just distance / speed. A 10 m shot at
// 400 m/s is 25 ms, which is shorter than one frame at 30 fps, so the overlay
// can erase the event before it is ever drawn. Keep every tracer alive for at
// least 120 ms (about four frames at 30 fps) so close-range shots read on screen.
inline constexpr float kCsTracerSpeedMps = 400.f;
inline constexpr float kCsTracerMinLifeS = 0.12f;
inline constexpr float kCsTracerMaxLifeS = 0.30f;

// True when round `shot_index` (0-based, per weapon) should show a tracer.
// Frequency 3 means the 3rd, 6th, 9th... round, matching CS2's "one tracer
// every N rounds".
inline bool shouldSpawnTracer(uint32_t shot_index, uint32_t frequency) {
    return frequency > 0u && ((shot_index + 1u) % frequency) == 0u;
}

inline float tracerLifetime(float distance_m) {
    return std::clamp(distance_m / kCsTracerSpeedMps, kCsTracerMinLifeS, kCsTracerMaxLifeS);
}

} // namespace eldenring::cs
