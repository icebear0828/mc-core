#pragma once

// Minecraft 1.21.8 ender pearl, per 20 Hz tick, y-up, blocks/tick.
//
// Source of truth: docs/MC_ELYTRA_REVERSE.md section 6 (official mappings plus the decompiled
// EnderpearlItem.use / Projectile.shootFromRotation / ThrowableProjectile.tick / ThrownEnderpearl.onHit).
// The adapter integrates the position and does the collision raycast; this class only owns the vanilla
// velocity recurrence and the oldPosition() the teleport uses.

#include "mc/types.hpp"

namespace mc {

struct EnderPearlConfig {
    float throw_speed{1.5f};       // Projectile.shootFromRotation power
    float jitter_spread{0.0172275f}; // RandomSource.triangle(0, 0.0172275 * uncertainty), uncertainty = 1
    float gravity{0.03f};          // ThrowableProjectile.getDefaultGravity, blocks/tick^2
    float drag{0.99f};             // applyInertia, per tick (0.8 in water)
    float damage{5.f};             // ThrownEnderpearl.onHit, MC damage
    int max_ticks{1200};           // adapter safety cap; vanilla has no lifetime cap
};

struct EnderPearlState {
    bool active{false};
    Vec3 position{};      // blocks, y-up, the pearl's centre
    Vec3 prev_position{}; // start-of-tick position: vanilla's oldPosition(), the teleport target
    Vec3 velocity{};      // blocks/tick
    int ticks{0};
};

struct EnderPearlLaunch {
    Vec3 origin{};          // owner eye position minus 0.1
    Vec3 look{};            // unit view vector, y-up
    Vec3 owner_velocity{};  // blocks/tick
    bool owner_on_ground{false};
};

class EnderPearl {
public:
    explicit EnderPearl(const EnderPearlConfig& config = {});

    // Projectile.getMovementToShoot: normalize(look) + triangle jitter per axis, scaled by throw_speed.
    // `jitter` is six uniform [0,1) samples (two per axis) so tests are deterministic.
    static Vec3 launchVelocity(const Vec3& look, float throw_speed, float uncertainty, const float jitter[6]);

    // Vanilla spawnProjectileFromRotation: the launch velocity plus the owner's known movement
    // (x/z always, y only while airborne).
    bool launch(const EnderPearlLaunch& launch, const float jitter[6]);

    // ThrowableProjectile.tick order: gravity, then inertia, then move by the resulting velocity.
    // With no collision the pearl simply advances; the adapter uses beginTick()/moveVector()/commit()
    // when it needs to resolve the move against the world.
    void updateTick(bool in_water = false);

    // Adapter path: beginTick applies gravity and drag, moveVector is the displacement to raycast,
    // commit stores the resolved position (hit point or full move).
    void beginTick(bool in_water = false);
    [[nodiscard]] Vec3 moveVector() const { return state_.velocity; }
    void commit(const Vec3& resolved_position);

    // The vanilla teleport target is oldPosition(), not the hit point.
    [[nodiscard]] Vec3 teleportTarget() const { return state_.prev_position; }
    void discard();

    [[nodiscard]] bool active() const { return state_.active; }
    [[nodiscard]] const EnderPearlState& state() const { return state_; }
    [[nodiscard]] const EnderPearlConfig& config() const { return config_; }
    void setState(const EnderPearlState& state) { state_ = state; }

private:
    EnderPearlConfig config_;
    EnderPearlState state_{};
};

} // namespace mc
