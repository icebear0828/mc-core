#include "mc/elytra_flight.hpp"

#include <algorithm>
#include <cmath>

namespace mc {

namespace {

float horizontalDistance(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

} // namespace

ElytraFlightModel::ElytraFlightModel(const ElytraFlightConfig& config) : config_(config) {}

bool ElytraFlightModel::canGlide(bool on_ground, bool passenger, bool levitation, bool has_glider, bool creative_flying) {
    return !on_ground && !passenger && !levitation && has_glider && !creative_flying;
}

bool ElytraFlightModel::startGliding(bool can_glide, bool in_water, const Vec3& initial_velocity) {
    if (!can_glide || in_water) return false;
    state_.is_gliding = true;
    state_.fall_fly_ticks = 0;
    state_.firework_ticks_left = 0;
    state_.velocity = initial_velocity;
    return true;
}

void ElytraFlightModel::stopGliding() {
    state_.is_gliding = false;
    state_.firework_ticks_left = 0;
}

bool ElytraFlightModel::useFireworkBoost(int ticks) {
    if (!state_.is_gliding || ticks <= 0) return false;
    state_.firework_ticks_left = ticks;
    return true;
}

Vec3 ElytraFlightModel::fireworkLerp(const Vec3& velocity, const Vec3& look) {
    return {velocity.x + look.x * 0.1f + (look.x * 1.5f - velocity.x) * 0.5f,
            velocity.y + look.y * 0.1f + (look.y * 1.5f - velocity.y) * 0.5f,
            velocity.z + look.z * 0.1f + (look.z * 1.5f - velocity.z) * 0.5f};
}

Vec3 ElytraFlightModel::updateMovement(const Vec3& velocity, const Vec3& look, float gravity,
                                       const ElytraFlightConfig& config) {
    // Vanilla getLookAngle() is unit length: look.y = -sin(xRot).
    const float pitch = -std::asin(std::clamp(look.y, -1.f, 1.f));
    const float h_look = std::sqrt(look.x * look.x + look.z * look.z);
    const float h_vel = horizontalDistance(velocity);
    const float cos2 = std::cos(pitch) * std::cos(pitch);

    Vec3 out = velocity;

    // 1. Gravity: only 25% remains in level flight.
    out.y += gravity * (-1.f + cos2 * 0.75f);

    // 2. Descending speed is converted into speed along the look direction.
    if (out.y < 0.f && h_look > 0.f) {
        const float d = out.y * -0.1f * cos2;
        out.x += look.x * d / h_look;
        out.y += d;
        out.z += look.z * d / h_look;
    }

    // 3. Looking up (xRot < 0) generates lift from the current horizontal speed.
    if (pitch < 0.f && h_look > 0.f) {
        const float d = h_vel * -std::sin(pitch) * 0.04f;
        out.x += -look.x * d / h_look;
        out.y += d * 3.2f;
        out.z += -look.z * d / h_look;
    }

    // 4. Horizontal velocity turns toward the look direction at 10% per tick.
    if (h_look > 0.f) {
        out.x += (look.x / h_look * h_vel - out.x) * 0.1f;
        out.z += (look.z / h_look * h_vel - out.z) * 0.1f;
    }

    // 5. Drag: horizontal 0.99, vertical 0.98.
    out.x *= config.horizontal_drag;
    out.y *= config.vertical_drag;
    out.z *= config.horizontal_drag;
    return out;
}

Vec3 ElytraFlightModel::updateTick(const Vec3& look_in, bool slow_falling) {
    if (!state_.is_gliding) return state_.velocity;

    Vec3 look = look_in;
    const float len = look.length();
    if (len > 1e-6f) {
        look = look * (1.f / len);
    } else {
        look = {0.f, 0.f, 1.f};
    }

    const float gravity = (slow_falling && state_.velocity.y <= 0.f)
                              ? std::min(config_.gravity, config_.slow_falling_gravity)
                              : config_.gravity;

    Vec3 v = updateMovement(state_.velocity, look, gravity, config_);

    // The vanilla rocket ticks after the player, so its lerp affects the next
    // tick's movement.
    if (state_.firework_ticks_left > 0) {
        v = fireworkLerp(v, look);
        --state_.firework_ticks_left;
    }

    state_.velocity = v;
    ++state_.fall_fly_ticks;
    return v;
}

float ElytraFlightModel::collisionDamage(const Vec3& before, const Vec3& after) const {
    const float lost = horizontalDistance(before) - horizontalDistance(after);
    const float damage = lost * config_.collision_damage_scale - config_.collision_damage_offset;
    return damage > 0.f ? damage : 0.f;
}

Vec3 ElytraFlightModel::afterHorizontalCollision(const Vec3& velocity, bool hit_x, bool hit_z) {
    Vec3 out = velocity;
    if (hit_x) out.x = 0.f;
    if (hit_z) out.z = 0.f;
    return out;
}

} // namespace mc
