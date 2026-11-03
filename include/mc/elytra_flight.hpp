#pragma once

// Minecraft 1.21.8 elytra flight, per 20 Hz tick, y-up, blocks/tick.
//
// This is the exact model used by the Elden Ring adapter. The older
// mc::ElytraFlight (z-up, cm/s) remains for the generic Session/host path.
//
// Source of truth: mc-core/docs/MC_ELYTRA_REVERSE.md (official 1.21.8 mappings
// + decompiled LivingEntity.travelFallFlying / updateFallFlyingMovement and
// FireworkRocketEntity.tick). The adapter integrates position; this class only
// produces velocity and collision damage.

#include "mc/types.hpp"

namespace mc {

struct ElytraFlightConfig {
    float gravity{0.08f};              // blocks/tick^2 (GRAVITY attribute)
    float slow_falling_gravity{0.01f}; // min(gravity, 0.01) while descending
    float horizontal_drag{0.99f};
    float vertical_drag{0.98f};
    float collision_damage_scale{10.f};
    float collision_damage_offset{3.f};
};

struct ElytraFlightState {
    bool is_gliding{false};
    int fall_fly_ticks{0};
    int firework_ticks_left{0};
    Vec3 velocity{0.f, 0.f, 0.f}; // blocks/tick, y up
};

class ElytraFlightModel {
public:
    explicit ElytraFlightModel(const ElytraFlightConfig& config = {});

    // Vanilla canGlide(): airborne, not a passenger, no levitation, a glider is
    // equipped, and not creative-flying.
    static bool canGlide(bool on_ground, bool passenger, bool levitation, bool has_glider, bool creative_flying);

    bool startGliding(bool can_glide, bool in_water, const Vec3& initial_velocity);
    void stopGliding();

    // Attached firework: the boost runs for `ticks` ticks. The caller derives
    // the vanilla lifetime 10*(1+flight_duration)+rand(6)+rand(7).
    bool useFireworkBoost(int ticks);

    // One 20 Hz tick. `look` is the unit view vector (y up). Applies the
    // elytra movement, then the attached-firework velocity lerp, so the boost
    // affects the next tick like the vanilla rocket (which ticks after the
    // player).
    Vec3 updateTick(const Vec3& look, bool slow_falling);

    // (h_before - h_after) * 10 - 3, clamped at 0. `after` is the velocity after
    // the horizontal collision has zeroed the hit axes.
    float collisionDamage(const Vec3& before, const Vec3& after) const;

    // Vanilla Entity.move(): zero the velocity components whose axis collided.
    static Vec3 afterHorizontalCollision(const Vec3& velocity, bool hit_x, bool hit_z);

    // Exposed for tests: v' = 0.5v + 0.85 look.
    static Vec3 fireworkLerp(const Vec3& velocity, const Vec3& look);

    // Exposed for tests: the exact 1.21.8 updateFallFlyingMovement recurrence.
    static Vec3 updateMovement(const Vec3& velocity, const Vec3& look, float gravity, const ElytraFlightConfig& config);

    [[nodiscard]] const ElytraFlightState& state() const { return state_; }
    [[nodiscard]] const ElytraFlightConfig& config() const { return config_; }
    void setState(const ElytraFlightState& state) { state_ = state; }

private:
    ElytraFlightConfig config_;
    ElytraFlightState state_{};
};

} // namespace mc
