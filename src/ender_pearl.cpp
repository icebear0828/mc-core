#include "mc/ender_pearl.hpp"

namespace mc {

namespace {

// RandomSource.triangle(0, spread) = (nextFloat() - nextFloat()) * spread.
float triangle(const float jitter[2], float spread) { return (jitter[0] - jitter[1]) * spread; }

} // namespace

EnderPearl::EnderPearl(const EnderPearlConfig& config) : config_(config) {}

Vec3 EnderPearl::launchVelocity(const Vec3& look, float throw_speed, float spread, const float jitter[6]) {
    const Vec3 dir = look.normalized();
    const Vec3 jittered{dir.x + triangle(jitter + 0, spread),
                        dir.y + triangle(jitter + 2, spread),
                        dir.z + triangle(jitter + 4, spread)};
    return jittered * throw_speed;
}

bool EnderPearl::launch(const EnderPearlLaunch& launch, const float jitter[6]) {
    if (launch.look.lengthSq() < 1e-12f) return false;
    state_ = {};
    state_.active = true;
    state_.position = launch.origin;
    state_.prev_position = launch.origin;
    Vec3 v = launchVelocity(launch.look, config_.throw_speed, config_.jitter_spread, jitter);
    // Projectile.shootFromRotation: inherit the owner's known movement; y only while airborne.
    v += Vec3{launch.owner_velocity.x, launch.owner_on_ground ? 0.f : launch.owner_velocity.y, launch.owner_velocity.z};
    state_.velocity = v;
    return true;
}

void EnderPearl::beginTick(bool in_water) {
    if (!state_.active) return;
    state_.prev_position = state_.position;
    state_.velocity.y -= config_.gravity;
    state_.velocity = state_.velocity * (in_water ? 0.8f : config_.drag);
    ++state_.ticks;
    if (state_.ticks >= config_.max_ticks) discard();
}

void EnderPearl::commit(const Vec3& resolved_position) {
    if (!state_.active) return;
    state_.position = resolved_position;
}

void EnderPearl::updateTick(bool in_water) {
    if (!state_.active) return;
    beginTick(in_water);
    if (!state_.active) return; // the safety cap discarded it
    commit(state_.position + state_.velocity);
}

void EnderPearl::discard() {
    state_.active = false;
}

} // namespace mc
