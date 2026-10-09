#pragma once

// Minecraft's combat particles (critical hit stars, damage indicator hearts, the sweep attack arc) as simple world-space
// sprites, and the hurt camera tilt. Pure logic, unit-tested; the overlay projects and draws them (src/overlay_d3d12.cpp).
// Positions are game metres (Y up).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eldenring::fx {

enum class Kind : uint8_t { Crit, Damage, Sweep };

struct Particle {
    Kind kind{Kind::Crit};
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
    static constexpr float kSweepSec = 0.4f;  // 8 frames
    static constexpr size_t kMax = 256;

    void spawnCrit(const float at[3], Rng& rng) {
        for (int i = 0; i < kCritCount && particles_.size() < kMax; ++i) {
            Particle p;
            p.kind = Kind::Crit;
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
            p.kind = Kind::Damage;
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

    void spawnSweep(const float at[3]) {
        if (particles_.size() >= kMax) return;
        Particle p;
        p.kind = Kind::Sweep;
        std::copy(at, at + 3, p.pos);
        p.life = kSweepSec;
        p.size = 1.4f;
        particles_.push_back(p);
    }

    void update(float dt) {
        for (Particle& p : particles_) {
            p.age += dt;
            if (p.kind == Kind::Crit) p.vel[1] -= 5.f * dt; // crit stars fall a little
            for (int a = 0; a < 3; ++a) p.pos[a] += p.vel[a] * dt;
            if (p.kind == Kind::Sweep) p.frame = std::min(7, static_cast<int>(8.f * p.age / p.life));
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

} // namespace eldenring::fx
