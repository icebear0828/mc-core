#pragma once

// Minecraft survival rules on top of Elden Ring's hit points: regeneration and absorption (golden apple, totem), the totem of
// undying, and the scale between Minecraft's 20 HP and the character's maximum HP. Pure logic, unit-tested; the loader does
// the game calls (damage clamp in the ProcessDamageContext hook, healing on the game thread).

#include "mc/consumables.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace eldenring::live {

inline constexpr float kMcPlayerHealth = 20.f; // 10 hearts

// Elden Ring hit points for `mc_hp` Minecraft hit points of a character with `max_hp`.
inline int scaleToEr(float mc_hp, int max_hp) { return static_cast<int>(std::lround(mc_hp * static_cast<float>(max_hp) / kMcPlayerHealth)); }

struct TotemOutcome {
    int damage{0};
    bool popped{false};
};

// A hit that would kill leaves the character on 1 HP and uses the totem; anything else passes unchanged.
inline TotemOutcome totemClamp(int damage, int hp, bool has_totem) {
    if (has_totem && hp > 0 && damage >= hp) return {std::max(0, hp - 1), true};
    return {damage, false};
}

class SurvivalState {
public:
    // Regeneration heals 1 HP every 50 >> (level - 1) ticks (Minecraft, 20 ticks per second); absorption gives 4 HP per level
    // that soaks up damage first and runs out with its duration.
    void apply(const std::vector<mc::ActiveEffect>& effects) {
        for (const mc::ActiveEffect& e : effects) {
            if (e.type == mc::EffectType::Regeneration) {
                regen_level_ = std::max(regen_level_, e.level);
                regen_left_ = std::max(regen_left_, e.duration);
            } else if (e.type == mc::EffectType::Absorption) {
                absorb_mc_ = std::max(absorb_mc_, 4.f * static_cast<float>(e.level));
                absorb_left_ = std::max(absorb_left_, e.duration);
            }
        }
    }

    // Advances the timers; returns the hit points to give back now (Elden Ring hit points).
    int tick(float dt, int max_hp) {
        int healed = 0;
        if (regen_left_ > 0.f && regen_level_ > 0) {
            const float interval = 2.5f / static_cast<float>(1 << std::min(regen_level_ - 1, 8));
            const float active = std::min(dt, regen_left_);
            regen_timer_ += active;
            while (regen_timer_ >= interval) {
                regen_timer_ -= interval;
                regen_carry_ += static_cast<float>(max_hp) / kMcPlayerHealth;
            }
            regen_left_ -= dt;
            if (regen_left_ <= 0.f) {
                regen_left_ = 0.f;
                regen_level_ = 0;
                regen_timer_ = 0.f;
            }
            healed = static_cast<int>(regen_carry_);
            regen_carry_ -= static_cast<float>(healed);
        }
        if (absorb_left_ > 0.f) {
            absorb_left_ -= dt;
            if (absorb_left_ <= 0.f) {
                absorb_left_ = 0.f;
                absorb_mc_ = 0.f;
            }
        }
        return healed;
    }

    // Soaks up `damage` (Elden Ring hit points); returns what is left to be dealt.
    int absorb(int damage, int max_hp) {
        if (damage <= 0 || absorb_mc_ <= 0.f) return damage;
        const float pool = absorb_mc_ * static_cast<float>(max_hp) / kMcPlayerHealth;
        const float taken = std::min(pool, static_cast<float>(damage));
        absorb_mc_ -= taken * kMcPlayerHealth / static_cast<float>(std::max(1, max_hp));
        if (absorb_mc_ < 1e-4f) absorb_mc_ = 0.f;
        return damage - static_cast<int>(std::lround(taken));
    }

    [[nodiscard]] float absorptionMc() const { return absorb_mc_; } // Minecraft hit points (4 per golden-apple level)
    [[nodiscard]] bool regenerating() const { return regen_left_ > 0.f; }

private:
    int regen_level_{0};
    float regen_left_{0.f};
    float regen_timer_{0.f};
    float regen_carry_{0.f};
    float absorb_mc_{0.f};
    float absorb_left_{0.f};
};

// What a finished meal restores right away, in Elden Ring hit points (cooked beef, bread; the golden apple works through effects).
inline int mealHeal(const mc::EatingEvent& e, int max_hp) { return e.completed ? scaleToEr(e.health_restored, max_hp) : 0; }

} // namespace eldenring::live
