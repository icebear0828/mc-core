#include "mc/riptide.hpp"

namespace mc {

RiptideModel::RiptideModel(const RiptideConfig& config) : config_(config) {}

float RiptideModel::strengthForLevel(int level, const RiptideConfig& config) {
    if (level <= 0) return 0.f;
    return config.base_strength + config.strength_per_level * static_cast<float>(level - 1);
}

bool RiptideModel::canUse(bool in_water_or_rain, int level, const RiptideConfig& config) {
    return in_water_or_rain && strengthForLevel(level, config) > 0.f;
}

Vec3 RiptideModel::launchVelocity(const Vec3& look, int level, const RiptideConfig& config) {
    const float strength = strengthForLevel(level, config);
    if (strength <= 0.f) return {};
    return look.normalized() * strength;
}

bool RiptideModel::start(int level, const Vec3& look, bool on_ground, const Vec3& current_velocity) {
    const float strength = strengthForLevel(level, config_);
    if (strength <= 0.f || look.lengthSq() < 1e-12f) return false;
    state_ = {};
    state_.active = true;
    state_.ticks_left = config_.spin_ticks;
    state_.velocity = current_velocity + look.normalized() * strength;
    if (on_ground) state_.velocity.y += config_.ground_hop;
    return true;
}

void RiptideModel::endTick() {
    if (!state_.active) return;
    state_.velocity.y -= config_.gravity;
    state_.velocity.x *= config_.horizontal_drag;
    state_.velocity.z *= config_.horizontal_drag;
    state_.velocity.y *= config_.vertical_drag;
    if (--state_.ticks_left <= 0) state_.active = false;
}

void RiptideModel::stop() {
    state_.active = false;
    state_.ticks_left = 0;
}

} // namespace mc
