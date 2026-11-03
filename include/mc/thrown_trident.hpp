#pragma once

// Minecraft 1.21.8 thrown trident: an arrow-like projectile.
//
// Source: TridentItem.releaseUsing (Projectile.spawnProjectileFromRotation with power 2.5, uncertainty 1.0) and
// the AbstractArrow tick (gravity 0.05, drag 0.99). The adapter integrates the position and does the raycast.

#include "mc/types.hpp"

namespace mc {

struct ThrownTridentConfig {
    float throw_speed{2.5f};        // TridentItem.PROJECTILE_SHOOT_POWER
    float jitter_spread{0.0172275f}; // Projectile.shootFromRotation, uncertainty 1.0
    float gravity{0.05f};           // blocks/tick^2 (AbstractArrow)
    float drag{0.99f};              // blocks/tick
    float damage{8.f};              // TridentItem.BASE_DAMAGE
    int max_ticks{1200};            // adapter safety cap
};

struct ThrownTridentState {
    bool active{false};
    Vec3 position{};
    Vec3 velocity{}; // blocks/tick
    int ticks{0};
};

class ThrownTrident {
public:
    explicit ThrownTrident(const ThrownTridentConfig& config = {});

    // Projectile.getMovementToShoot: normalize(look) + triangle jitter per axis, scaled by throw_speed.
    static Vec3 launchVelocity(const Vec3& look, float speed, float spread, const float jitter[6]);

    // The launch velocity plus the owner's known movement (x/z always, y only while airborne).
    bool launch(const Vec3& origin, const Vec3& look, const Vec3& owner_velocity, bool owner_on_ground,
                const float jitter[6]);

    void beginTick();
    [[nodiscard]] Vec3 moveVector() const { return state_.velocity; }
    void commit(const Vec3& resolved_position);
    void updateTick(); // no collision: gravity, drag, then move
    void discard();

    [[nodiscard]] bool active() const { return state_.active; }
    [[nodiscard]] const ThrownTridentState& state() const { return state_; }
    [[nodiscard]] const ThrownTridentConfig& config() const { return config_; }
    void setState(const ThrownTridentState& state) { state_ = state; }

private:
    ThrownTridentConfig config_;
    ThrownTridentState state_{};
};

} // namespace mc
