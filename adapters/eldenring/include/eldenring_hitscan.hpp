#pragma once

// Shared hitscan primitives for the first-person weapon modes (CS2 bullets and
// Overwatch strikes). Pure logic only: cone spread, the ray/body-cylinder test,
// range falloff and the damage -> ER-damage conversion. No game memory and no
// Windows headers, so every piece is unit-tested on any host.
//
// The mode-specific halves live in cs_ballistics.hpp (CS2 bullets) and
// eldenring_ow_combat.hpp (Doomfist strikes); both feed the same DamageQueue,
// which already applies native HP loss, stagger, aggro and kill credit.

#include "eldenring_bullet.hpp"
#include "eldenring_world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eldenring::hitscan {

inline constexpr float kPi = 3.14159265358979323846f;

struct ShotParams {
    float damage{36.f};          // damage in the source game's scale
    float spread_deg{0.45f};     // cone half-angle in degrees
    float max_range_m{300.f};    // hits stop here
    float falloff_start{0.5f};   // fraction of max_range where damage starts to drop
    float min_damage_scale{0.5f};// damage multiplier at max_range
    // The victim is a vertical cylinder from feet+bottom to feet+top. It covers
    // the head as well as the torso: a torso-only sphere misses a 2 m enemy's
    // head, which reads as "the crosshair was on him".
    float body_bottom_m{0.1f};
    float body_top_m{2.6f};      // tall humanoids have heads well above 2 m
    float hit_radius_m{0.6f};
};

// What one shot hit: an index into the enemy list and the distance along the ray.
struct ShotTrace {
    int index{-1};
    float distance{0.f};
};

// Deterministic xorshift32. The shot RNG is seeded per process and advanced per
// shot; the pure functions take the two uniforms explicitly so tests can pin them.
class ShotRng {
public:
    explicit ShotRng(uint32_t seed = 0x9E3779B9u) : state_(seed != 0 ? seed : 0x9E3779B9u) {}

    uint32_t next() {
        uint32_t x = state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state_ = x;
        return x;
    }

    // [0, 1)
    float next01() { return static_cast<float>(next() >> 8) * (1.f / 16777216.f); }

private:
    uint32_t state_;
};

inline void normalize3(float v[3]) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-8f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

// An orthonormal basis with `forward` as one axis. Picks the world axis least
// aligned with forward, so the basis never degenerates when looking straight
// up or down.
inline void orthonormalBasis(const float forward[3], float right[3], float up[3]) {
    const float ax = std::fabs(forward[0]), ay = std::fabs(forward[1]), az = std::fabs(forward[2]);
    float helper[3] = {0.f, 0.f, 1.f};
    if (ax <= ay && ax <= az) {
        helper[0] = 1.f; helper[1] = 0.f; helper[2] = 0.f;
    } else if (ay <= ax && ay <= az) {
        helper[0] = 0.f; helper[1] = 1.f; helper[2] = 0.f;
    }
    right[0] = forward[1] * helper[2] - forward[2] * helper[1];
    right[1] = forward[2] * helper[0] - forward[0] * helper[2];
    right[2] = forward[0] * helper[1] - forward[1] * helper[0];
    normalize3(right);
    up[0] = right[1] * forward[2] - right[2] * forward[1];
    up[1] = right[2] * forward[0] - right[0] * forward[2];
    up[2] = right[0] * forward[1] - right[1] * forward[0];
    normalize3(up);
}

// A unit direction uniformly distributed inside the cone of half-angle
// spread_deg around forward. u1, u2 are uniforms in [0, 1); spread 0 returns
// forward unchanged.
inline void spreadDirection(const float forward[3], float spread_deg, float u1, float u2, float out[3]) {
    out[0] = forward[0];
    out[1] = forward[1];
    out[2] = forward[2];
    normalize3(out);
    if (!(spread_deg > 0.f)) return;

    const float half = spread_deg * kPi / 180.f;
    const float cos_max = std::cos(std::min(half, kPi));
    const float cos_theta = 1.f - std::clamp(u1, 0.f, 1.f) * (1.f - cos_max);
    const float sin_theta = std::sqrt(std::max(0.f, 1.f - cos_theta * cos_theta));
    const float phi = 2.f * kPi * u2;

    float right[3], up[3];
    orthonormalBasis(out, right, up);
    for (int i = 0; i < 3; ++i) {
        out[i] = out[i] * cos_theta + (right[i] * std::cos(phi) + up[i] * std::sin(phi)) * sin_theta;
    }
    normalize3(out);
}

// Entry distance of the ray (o + t*d, t >= 0) into the vertical cylinder at
// (cx, cz) between y=bottom and y=top. False when the XZ projection misses the
// circle or the entry point is outside the height range.
inline bool rayVerticalCylinder(const float o[3], const float d[3], float cx, float cz,
                                float bottom, float top, float radius, float& out_t) {
    const float ox = o[0] - cx, oz = o[2] - cz;
    const float dx = d[0], dz = d[2];
    const float a = dx * dx + dz * dz;
    float t = 0.f;
    if (a < 1e-8f) {
        // Looking straight up or down: the ray is already inside the circle or it never enters it.
        if (ox * ox + oz * oz > radius * radius) return false;
    } else {
        const float b = ox * dx + oz * dz;
        const float c = ox * ox + oz * oz - radius * radius;
        const float disc = b * b - a * c;
        if (disc < 0.f) return false;
        const float sq = std::sqrt(disc);
        const float t0 = (-b - sq) / a;
        const float t1 = (-b + sq) / a;
        t = (t0 >= 0.f) ? t0 : t1;
        if (t < 0.f) return false;
    }
    const float y = o[1] + t * d[1];
    if (y < bottom || y > top) return false;
    out_t = t;
    return true;
}

// Nearest living hostile whose body cylinder the ray (cam_rel, dir) enters
// within max_range_m. Hostile-only: neutral animals are not shot.
inline ShotTrace traceTargets(const float cam_rel[3], const float dir[3],
                              const std::vector<live::EnemyInfo>& enemies,
                              const ShotParams& p) {
    ShotTrace best;
    float best_t = 1e30f;
    for (size_t i = 0; i < enemies.size(); ++i) {
        const live::EnemyInfo& e = enemies[i];
        if (e.hp <= 0 || !e.hostile) continue;
        float t = 0.f;
        if (!rayVerticalCylinder(cam_rel, dir, e.rel_x, e.rel_z, e.rel_y + p.body_bottom_m,
                                 e.rel_y + p.body_top_m, p.hit_radius_m, t)) {
            continue;
        }
        if (t > p.max_range_m) continue;
        if (t < best_t) {
            best_t = t;
            best.index = static_cast<int>(i);
            best.distance = t;
        }
    }
    return best;
}

// 1 at the muzzle, linearly down to min_damage_scale at max_range.
inline float damageScaleAtRange(float distance, const ShotParams& p) {
    if (!(p.max_range_m > 0.f)) return 1.f;
    const float start = std::clamp(p.falloff_start, 0.f, 1.f) * p.max_range_m;
    if (distance <= start) return 1.f;
    if (distance >= p.max_range_m) return p.min_damage_scale;
    const float u = (distance - start) / (p.max_range_m - start);
    return 1.f + (p.min_damage_scale - 1.f) * u;
}

// Source-game damage -> ER damage through the same conversion the MC bow uses:
// a 7-damage diamond sword hit is 5% of the victim's maximum health, so a
// 36-damage AK round is ~25% and a 115-damage AWP round ~82%.
inline int damageToEr(int victim_max_hp, float damage, float distance, const ShotParams& p) {
    if (victim_max_hp <= 0 || damage <= 0.f) return 0;
    const float scaled = damage * damageScaleAtRange(distance, p);
    return eldenring::bullet::boltDamageEr(victim_max_hp, scaled);
}

// Same conversion without a distance term (melee-range strikes).
inline int damageToEr(int victim_max_hp, float damage) {
    if (victim_max_hp <= 0 || damage <= 0.f) return 0;
    return eldenring::bullet::boltDamageEr(victim_max_hp, damage);
}

} // namespace eldenring::hitscan
