#include "mc/elytra.hpp"
#include "mc/contracts/physics_adapter.hpp"
#include <algorithm>
#include <cmath>

namespace mc {

ElytraFlight::ElytraFlight(IPhysicsAdapter& physics, const ElytraConfig& config)
    : physics_(physics), config_(config) {}

bool ElytraFlight::startGliding(bool on_ground, const Vec3& initial_velocity) {
    if (on_ground) {
        return false;
    }
    state_.is_gliding = true;
    state_.is_boosting = false;
    state_.boost_time_remaining = 0.f;
    state_.velocity = initial_velocity;
    if (state_.velocity.lengthSq() < 100.f * 100.f) {
        state_.velocity = {300.f, 0.f, 0.f};
    }
    state_.wing_flutter = 0.f;
    state_.bank_roll = 0.f;
    return true;
}

void ElytraFlight::stopGliding() {
    state_.is_gliding = false;
    state_.is_boosting = false;
    state_.boost_time_remaining = 0.f;
    state_.velocity = {0.f, 0.f, 0.f};
}

bool ElytraFlight::useFireworkBoost(float duration_sec) {
    if (!state_.is_gliding) {
        return false;
    }
    state_.is_boosting = true;
    state_.boost_time_remaining = duration_sec;
    return true;
}

FlightStepResult ElytraFlight::update(float dt, const Vec3& current_pos, float look_pitch, float look_yaw, bool on_ground) {
    FlightStepResult result;
    if (!state_.is_gliding) {
        return result;
    }

    sim_time_ += dt;

    if (on_ground) {
        const float speed = state_.velocity.length();
        if (speed < config_.kinetic_damage_threshold) {
            result.landed = true;
            stopGliding();
            return result;
        }
    }

    const float cos_p = std::cos(look_pitch);
    const float sin_p = std::sin(look_pitch);
    const float cos_y = std::cos(look_yaw);
    const float sin_y = std::sin(look_yaw);

    const Vec3 forward_dir{cos_p * cos_y, cos_p * sin_y, sin_p};

    // Calculate banking roll angle from yaw change
    const float yaw_delta = look_yaw - prev_yaw_;
    prev_yaw_ = look_yaw;
    state_.bank_roll = std::clamp(yaw_delta * 5.0f, -0.6f, 0.6f);

    // 1. Firework rocket propulsion thrust
    if (state_.is_boosting) {
        state_.velocity += forward_dir * (config_.boost_acceleration * dt);
        state_.boost_time_remaining -= dt;
        if (state_.boost_time_remaining <= 0.f) {
            state_.is_boosting = false;
            state_.boost_time_remaining = 0.f;
        }
    }

    // 2. Gliding lift vs Gravity
    const float speed_h = std::sqrt(state_.velocity.x * state_.velocity.x + state_.velocity.y * state_.velocity.y);
    const float lift_ratio = std::clamp(cos_p, 0.0f, 1.0f);
    const float effective_gravity = config_.gravity * (1.0f - lift_ratio * 0.75f);
    state_.velocity.z -= effective_gravity * dt;

    // Dive acceleration or climbing lift
    if (look_pitch < 0.f) {
        // Diving: pitch < 0, forward direction accelerates
        const float dive_factor = -sin_p;
        state_.velocity += forward_dir * (config_.gravity * dive_factor * 1.5f * dt);
    } else if (look_pitch > 0.f) {
        // Climbing: pitch > 0, horizontal speed trades for vertical lift
        const float climb_lift = sin_p * speed_h * 1.2f * dt;
        state_.velocity.z += climb_lift;
        const float induced_drag = std::max(0.1f, 1.0f - (sin_p * 0.5f * dt));
        state_.velocity.x *= induced_drag;
        state_.velocity.y *= induced_drag;
    }

    // 3. Air resistance drag
    const float drag = std::pow(config_.base_drag, dt / 0.05f);
    state_.velocity = state_.velocity * drag;

    // 4. Cap max speed
    const float current_speed = state_.velocity.length();
    if (current_speed > config_.max_speed) {
        state_.velocity = state_.velocity.normalized() * config_.max_speed;
    }

    state_.wing_flutter = std::sin(sim_time_ * (current_speed * 0.015f)) * 0.15f;

    // 5. Collision swept raycast
    const Vec3 next_pos = current_pos + state_.velocity * dt;
    result.new_position = next_pos;

    RaycastResult hit = physics_.raycastWorld(current_pos, next_pos);
    if (hit.has_hit) {
        result.new_position = hit.point;
        result.crash_normal = hit.normal;

        const float impact_speed = -(state_.velocity.x * hit.normal.x +
                                     state_.velocity.y * hit.normal.y +
                                     state_.velocity.z * hit.normal.z);

        if (impact_speed > config_.kinetic_damage_threshold) {
            result.crashed = true;
            result.kinetic_damage = (impact_speed - config_.kinetic_damage_threshold) * config_.kinetic_damage_factor;
        } else {
            result.landed = true;
        }

        stopGliding();
        return result;
    }

    if (on_ground) {
        result.landed = true;
        stopGliding();
    }

    return result;
}

} // namespace mc
