#pragma once

// Minecraft 1.21.8 trident Riptide, per 20 Hz tick, y-up, blocks/tick.
//
// Source: the decompiled TridentItem.releaseUsing / Player.startAutoSpinAttack / LivingEntity.travelInAir
// and data/minecraft/enchantment/riptide.json:
//   - 10 ticks of charge (THROW_THRESHOLD_TIME).
//   - spin attack strength 1.5 at level I, +0.75 per level (II 2.25, III 3.0).
//   - push(look * strength) then startAutoSpinAttack(20 ticks, 8.0 damage); on the ground, move +1.2 m up first.
//   - normal air travel: horizontal drag 0.91, vertical drag 0.98, gravity 0.08.
// The adapter integrates the position; this class only owns the velocity recurrence.

#include "mc/types.hpp"

namespace mc {

struct RiptideConfig {
    float gravity{0.08f};            // blocks/tick^2
    float horizontal_drag{0.91f};    // travelInAir
    float vertical_drag{0.98f};
    int spin_ticks{20};              // Player.startAutoSpinAttack(20, ...)
    float damage{8.f};               // autoSpinAttackDmg
    int charge_ticks{10};            // TridentItem.THROW_THRESHOLD_TIME
    float ground_hop{1.2f};          // move(0, 1.2, 0) when released on the ground
    float base_strength{1.5f};       // Riptide I
    float strength_per_level{0.75f}; // each level above I
};

struct RiptideState {
    bool active{false};
    int ticks_left{0};
    Vec3 velocity{}; // blocks/tick
};

class RiptideModel {
public:
    explicit RiptideModel(const RiptideConfig& config = {});

    [[nodiscard]] static float strengthForLevel(int level, const RiptideConfig& config = {});
    [[nodiscard]] static bool canUse(bool in_water_or_rain, int level, const RiptideConfig& config = {});
    // push(normalize(look) * strength).
    [[nodiscard]] static Vec3 launchVelocity(const Vec3& look, int level, const RiptideConfig& config = {});

    // Vanilla release: add the launch to the current velocity; on the ground also add the 1.2 m hop.
    bool start(int level, const Vec3& look, bool on_ground, const Vec3& current_velocity);

    // After a tick's movement: v.y -= gravity, v *= (0.91, 0.98, 0.91), then count the spin down.
    void endTick();
    void stop();

    [[nodiscard]] bool active() const { return state_.active; }
    [[nodiscard]] int ticksLeft() const { return state_.ticks_left; }
    [[nodiscard]] const RiptideState& state() const { return state_; }
    [[nodiscard]] const RiptideConfig& config() const { return config_; }
    void setState(const RiptideState& state) { state_ = state; }

private:
    RiptideConfig config_;
    RiptideState state_{};
};

} // namespace mc
