#include "mc/thrown_trident.hpp"

namespace mc {

namespace {

float triangle(const float jitter[2], float spread) { return (jitter[0] - jitter[1]) * spread; }

} // namespace

ThrownTrident::ThrownTrident(const ThrownTridentConfig& config) : config_(config) {}

Vec3 ThrownTrident::launchVelocity(const Vec3& look, float speed, float spread, const float jitter[6]) {
    const Vec3 dir = look.normalized();
    const Vec3 jittered{dir.x + triangle(jitter + 0, spread), dir.y + triangle(jitter + 2, spread),
                        dir.z + triangle(jitter + 4, spread)};
    return jittered * speed;
}

bool ThrownTrident::launch(const Vec3& origin, const Vec3& look, const Vec3& owner_velocity, bool owner_on_ground,
                           const float jitter[6]) {
    if (look.lengthSq() < 1e-12f) return false;
    state_ = {};
    state_.active = true;
    state_.position = origin;
    Vec3 v = launchVelocity(look, config_.throw_speed, config_.jitter_spread, jitter);
    v += Vec3{owner_velocity.x, owner_on_ground ? 0.f : owner_velocity.y, owner_velocity.z};
    state_.velocity = v;
    return true;
}

void ThrownTrident::beginTick() {
    if (!state_.active) return;
    state_.velocity.y -= config_.gravity;
    state_.velocity = state_.velocity * config_.drag;
    if (++state_.ticks >= config_.max_ticks) discard();
}

void ThrownTrident::commit(const Vec3& resolved_position) {
    if (!state_.active) return;
    state_.position = resolved_position;
}

void ThrownTrident::updateTick() {
    if (!state_.active) return;
    beginTick();
    if (!state_.active) return;
    commit(state_.position + state_.velocity);
}

void ThrownTrident::discard() {
    state_.active = false;
}

} // namespace mc
