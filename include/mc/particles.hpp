#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mc {

enum class ParticleKind : uint8_t { Crit, Damage, Sweep, Spark, Dust, Firework, Riptide };
using Kind = ParticleKind;

struct Particle {
    ParticleKind kind{ParticleKind::Crit};
    float pos[3]{};
    float vel[3]{};
    float age{0.f};
    float life{0.4f};
    float size{0.12f}; // metres
    int frame{0};      // sweep: 0..7
};

// A small deterministic generator so tests do not depend on the platform's rand().
class Rng {
public:
    explicit Rng(uint32_t seed = 12345u) : s_(seed ? seed : 1u) {}
    float next() { // 0..1
        s_ = s_ * 1664525u + 1013904223u;
        return static_cast<float>((s_ >> 8) & 0xFFFFFF) / 16777216.f;
    }
    float range(float a, float b) { return a + (b - a) * next(); }

private:
    uint32_t s_;
};

class ParticleSystem {
public:
    static constexpr int kCritCount = 16;     // Player.crit(): a burst of 16
    static constexpr int kFireworkCount = 10; // elytra rocket boost trail, per tick
    static constexpr int kRiptideCount = 8;   // riptide water helix, per tick
    static constexpr float kSweepSec = 0.4f;  // 8 frames
    static constexpr size_t kMax = 256;

    void spawnCrit(const float at[3], Rng& rng) {
        for (int i = 0; i < kCritCount && particles_.size() < kMax; ++i) {
            Particle p;
            p.kind = ParticleKind::Crit;
            std::copy(at, at + 3, p.pos);
            // random direction in a sphere, 1.5..3.5 m/s, slightly upward
            const float yaw = rng.range(0.f, 6.2831853f), pitch = rng.range(-0.4f, 1.2f), speed = rng.range(1.5f, 3.5f);
            p.vel[0] = std::cos(yaw) * std::cos(pitch) * speed;
            p.vel[1] = std::sin(pitch) * speed;
            p.vel[2] = std::sin(yaw) * std::cos(pitch) * speed;
            p.life = rng.range(0.25f, 0.45f);
            p.size = 0.14f;
            particles_.push_back(p);
        }
    }

    // `hearts`: vanilla spawns floor(damage * 0.5) of them (7 damage -> 3), they drift up and fade.
    void spawnDamage(const float at[3], int hearts, Rng& rng) {
        for (int i = 0; i < hearts && particles_.size() < kMax; ++i) {
            Particle p;
            p.kind = ParticleKind::Damage;
            std::copy(at, at + 3, p.pos);
            p.pos[0] += rng.range(-0.25f, 0.25f);
            p.pos[1] += rng.range(0.f, 0.3f);
            p.pos[2] += rng.range(-0.25f, 0.25f);
            p.vel[0] = rng.range(-0.15f, 0.15f);
            p.vel[1] = rng.range(0.8f, 1.4f);
            p.vel[2] = rng.range(-0.15f, 0.15f);
            p.life = rng.range(0.7f, 1.0f);
            p.size = 0.18f;
            particles_.push_back(p);
        }
    }

    // A few bright fragments for a bullet impact (world space, game metres).
    void spawnSpark(const float at[3], Rng& rng) {
        for (int i = 0; i < 5 && particles_.size() < kMax; ++i) {
            Particle p;
            p.kind = ParticleKind::Spark;
            std::copy(at, at + 3, p.pos);
            const float yaw = rng.range(0.f, 6.2831853f), pitch = rng.range(-0.8f, 0.8f), speed = rng.range(1.0f, 2.8f);
            p.vel[0] = std::cos(yaw) * std::cos(pitch) * speed;
            p.vel[1] = std::sin(pitch) * speed;
            p.vel[2] = std::sin(yaw) * std::cos(pitch) * speed;
            p.life = rng.range(0.08f, 0.18f);
            p.size = 0.05f;
            particles_.push_back(p);
        }
    }

    // A small grey dust puff for a bullet hitting a floor or wall. It drifts up and fades; no gravity.
    void spawnDust(const float at[3], Rng& rng) {
        for (int i = 0; i < 6 && particles_.size() < kMax; ++i) {
            Particle p;
            p.kind = ParticleKind::Dust;
            std::copy(at, at + 3, p.pos);
            p.pos[0] += rng.range(-0.06f, 0.06f);
            p.pos[1] += rng.range(0.f, 0.04f);
            p.pos[2] += rng.range(-0.06f, 0.06f);
            const float yaw = rng.range(0.f, 6.2831853f);
            const float speed = rng.range(0.15f, 0.45f);
            p.vel[0] = std::cos(yaw) * speed;
            p.vel[1] = rng.range(0.35f, 0.8f);
            p.vel[2] = std::sin(yaw) * speed;
            p.life = rng.range(0.35f, 0.7f);
            p.size = rng.range(0.10f, 0.18f);
            particles_.push_back(p);
        }
    }

    void spawnSweep(const float at[3]) {
        if (particles_.size() >= kMax) return;
        Particle p;
        p.kind = ParticleKind::Sweep;
        std::copy(at, at + 3, p.pos);
        p.life = kSweepSec;
        p.size = 1.4f;
        particles_.push_back(p);
    }

    // Elytra firework boost (#38): a jet of upward sparks and a trail streaming opposite the rocket's
    // direction of travel. `dir` may be all zeros, which is treated as straight up.
    void spawnFirework(const float at[3], const float dir[3], Rng& rng) {
        float f[3] = {dir[0], dir[1], dir[2]};
        float len = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (len < 1e-4f) {
            f[0] = 0.f;
            f[1] = 1.f;
            f[2] = 0.f;
            len = 1.f;
        }
        for (int i = 0; i < kFireworkCount && particles_.size() < kMax; ++i) {
            Particle p;
            p.kind = ParticleKind::Firework;
            p.pos[0] = at[0] + rng.range(-0.12f, 0.12f);
            p.pos[1] = at[1] + rng.range(-0.12f, 0.12f);
            p.pos[2] = at[2] + rng.range(-0.12f, 0.12f);
            const float back = rng.range(2.f, 5.f);
            p.vel[0] = -f[0] / len * back + rng.range(-1.f, 1.f);
            p.vel[1] = -f[1] / len * back + rng.range(0.6f, 2.2f); // sparks drift upward as they fall behind
            p.vel[2] = -f[2] / len * back + rng.range(-1.f, 1.f);
            p.life = rng.range(0.35f, 0.7f);
            p.size = rng.range(0.06f, 0.12f);
            particles_.push_back(p);
        }
    }

    // Riptide dash (#39): a spinning ring of water droplets around the player. `phase` turns the whole
    // ring (0..1 = one full turn), so a caller that advances it each tick gets a helix. Deterministic.
    void spawnRiptide(const float at[3], float phase, Rng& rng) {
        constexpr float kRadius = 0.5f; // metres
        constexpr float kTwoPi = 6.2831853f;
        for (int i = 0; i < kRiptideCount && particles_.size() < kMax; ++i) {
            Particle p;
            p.kind = ParticleKind::Riptide;
            const float angle = phase * kTwoPi + static_cast<float>(i) * (kTwoPi / static_cast<float>(kRiptideCount)) + rng.range(-0.08f, 0.08f);
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            p.pos[0] = at[0] + c * kRadius;
            p.pos[1] = at[1] + rng.range(0.f, 1.2f);
            p.pos[2] = at[2] + s * kRadius;
            const float speed = rng.range(1.2f, 2.2f);
            // tangential direction: d/d(angle) (cos, sin) = (-sin, cos)
            p.vel[0] = -s * speed + rng.range(-0.2f, 0.2f);
            p.vel[1] = rng.range(0.4f, 1.f);
            p.vel[2] = c * speed + rng.range(-0.2f, 0.2f);
            p.life = rng.range(0.3f, 0.6f);
            p.size = rng.range(0.08f, 0.15f);
            particles_.push_back(p);
        }
    }

    void update(float dt) {
        for (Particle& p : particles_) {
            p.age += dt;
            if (p.kind == ParticleKind::Crit || p.kind == ParticleKind::Spark || p.kind == ParticleKind::Firework ||
                p.kind == ParticleKind::Riptide)
                p.vel[1] -= 5.f * dt;
            for (int a = 0; a < 3; ++a) p.pos[a] += p.vel[a] * dt;
            if (p.kind == ParticleKind::Sweep) p.frame = std::min(7, static_cast<int>(8.f * p.age / p.life));
        }
        particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.age >= p.life; }), particles_.end());
    }

    [[nodiscard]] const std::vector<Particle>& alive() const { return particles_; }
    // 1 when new, 0 when it dies.
    [[nodiscard]] static float fade(const Particle& p) { return std::clamp(1.f - p.age / p.life, 0.f, 1.f); }

private:
    std::vector<Particle> particles_;
};

// Minecraft's hurt camera tilt: `t` runs from 1 (just hurt) to 0 (10 ticks later); the roll peaks at 14 degrees, sin(t^4 * pi)
// (GameRenderer.bobHurt). `side` is +1 or -1: which way the camera rolls.
inline float hurtTiltRadians(float t, float side) {
    t = std::clamp(t, 0.f, 1.f);
    return side * 14.f * 0.01745329252f * std::sin(t * t * t * t * 3.14159265f);
}

} // namespace mc
