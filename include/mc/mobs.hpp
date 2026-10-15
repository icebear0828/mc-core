#pragma once

#include "mc/death.hpp"
#include "mc/entity_model.hpp"
#include "mc/rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

inline constexpr size_t kMaxMobs = 16;
inline constexpr int kMissedFramesBeforeDrop = 30;
inline constexpr float kHurtSeconds = 0.5f;  // Minecraft: 10 ticks of hurt time (the red flash)

inline mc::rig::Mat4 scaleAboutPoint(const mc::Vec3& p, float s) {
    mc::rig::Mat4 m;
    m.m[0] = s;
    m.m[5] = s;
    m.m[10] = s;
    m.m[12] = p.x * (1.0f - s);
    m.m[13] = p.y * (1.0f - s);
    m.m[14] = p.z * (1.0f - s);
    return m;
}

// Universal state machine tracking an individual mob's damage flash, death state, and corpse expiry.
class MobLifecycleTracker {
public:
    void updateHp(int hp, int max_hp, float dt) {
        if (max_hp <= 0) return;

        bool just_hurt = false;
        bool just_died = false;

        if (have_hp_ && hp < last_hp_ && hp > 0) {
            just_hurt = true;
        }
        if (hp <= 0 && dying_sec_ < 0.f) {
            dying_sec_ = 0.f;
            just_died = true;
        }

        last_hp_ = hp;
        have_hp_ = true;

        if (just_hurt) {
            hurt_left_sec_ = kHurtSeconds;
        } else if (!just_died) {
            hurt_left_sec_ = std::max(0.f, hurt_left_sec_ - dt);
        }

        if (dying_sec_ >= 0.f && !just_died) {
            dying_sec_ += dt;
        }
    }

    void tick(float dt) {
        if (hurt_left_sec_ > 0.f && dying_sec_ < 0.f) {
            hurt_left_sec_ = std::max(0.f, hurt_left_sec_ - dt);
        }
        if (dying_sec_ >= 0.f) {
            dying_sec_ += dt;
        }
    }

    [[nodiscard]] bool isHurt() const {
        return (dying_sec_ >= 0.f) || (hurt_left_sec_ > 0.f);
    }

    [[nodiscard]] bool isDying() const {
        return dying_sec_ >= 0.f;
    }

    [[nodiscard]] bool isCorpseExpired() const {
        return dying_sec_ > kDeathSeconds;
    }

    [[nodiscard]] float hurtFraction() const {
        if (dying_sec_ >= 0.f) return 1.f;
        return std::clamp(hurt_left_sec_ / kHurtSeconds, 0.f, 1.f);
    }

    [[nodiscard]] float deathFraction() const {
        return deathFlipFraction(dying_sec_);
    }

    [[nodiscard]] float dyingSec() const {
        return dying_sec_;
    }

    void reset() {
        last_hp_ = 0;
        have_hp_ = false;
        hurt_left_sec_ = 0.f;
        dying_sec_ = -1.f;
    }

private:
    int last_hp_{0};
    bool have_hp_{false};
    float hurt_left_sec_{0.f};
    float dying_sec_{-1.f}; // < 0: alive, >= 0: seconds since hit points ran out
};

} // namespace mc
