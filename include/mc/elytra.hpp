#pragma once

#include "mc/types.hpp"

namespace mc {

class IPhysicsAdapter;

struct ElytraConfig {
    float gravity{980.f};                   // cm/s^2 (downward)
    float base_drag{0.99f};                 // Air resistance drag factor per 50ms tick
    float max_speed{3500.f};                // cm/s (~35 m/s terminal velocity cap)
    float boost_acceleration{2200.f};       // cm/s^2 forward thrust when firework rocket active
    float kinetic_damage_threshold{1000.f}; // cm/s (~10 m/s minimum impact velocity to trigger damage)
    float kinetic_damage_factor{0.02f};     // Damage points per cm/s exceeding threshold
};

struct ElytraState {
    bool is_gliding{false};
    bool is_boosting{false};
    float boost_time_remaining{0.f};
    Vec3 velocity{0.f, 0.f, 0.f}; // cm/s
    float wing_flutter{0.f};      // Wing oscillation amplitude
    float bank_roll{0.f};         // Banking roll angle (radians)
};

struct FlightStepResult {
    bool crashed{false};
    float kinetic_damage{0.f};
    Vec3 crash_normal{0.f, 0.f, 0.f};
    bool landed{false};
    Vec3 new_position{0.f, 0.f, 0.f};
};

class ElytraFlight {
public:
    explicit ElytraFlight(IPhysicsAdapter& physics, const ElytraConfig& config = {});

    // Try starting glide (fails if player is currently on ground)
    bool startGliding(bool on_ground, const Vec3& initial_velocity);

    // Cancel or end gliding (e.g. landed safely, touched water, unequipped)
    void stopGliding();

    // Trigger firework rocket boost (fails if not currently gliding)
    bool useFireworkBoost(float duration_sec = 2.0f);

    // Advance flight physics simulation by dt seconds
    // look_pitch: radians (+up, -down)
    // look_yaw: radians
    // on_ground: whether player is contacting ground this frame
    FlightStepResult update(float dt, const Vec3& current_pos, float look_pitch, float look_yaw, bool on_ground);

    [[nodiscard]] const ElytraState& getState() const { return state_; }
    void setState(const ElytraState& state) { state_ = state; }
    [[nodiscard]] const ElytraConfig& getConfig() const { return config_; }

private:
    IPhysicsAdapter& physics_;
    ElytraConfig config_;
    ElytraState state_{};
    float sim_time_{0.f};
    float prev_yaw_{0.f};
};

} // namespace mc
