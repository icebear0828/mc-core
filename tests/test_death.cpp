#include <gtest/gtest.h>
#include "mc/death.hpp"
#include "mc/animator.hpp"
#include "mc/rig.hpp"

#include <cmath>

namespace {
constexpr float kTolerance = 1e-5f;
} // namespace

TEST(DeathKinematicsTest, AuthenticMinecraftCurveMatchesLivingEntityRenderer) {
    // LivingEntityRenderer.setupRotations:
    // fraction = sqrt((deathTime - 1) / 20 * 1.6), clamped to [0, 1]
    EXPECT_FLOAT_EQ(mc::deathFlipFraction(0.f), 0.f);
    EXPECT_FLOAT_EQ(mc::deathFlipFraction(-0.5f), 0.f);

    // Tick 1 (0.05s): (1 - 1) / 20 * 1.6 = 0
    EXPECT_NEAR(mc::deathFlipFraction(0.05f), 0.f, 1e-4f);

    // Tick 5 (0.25s): sqrt((5 - 1) / 20 * 1.6) = sqrt(0.32) ~ 0.565685
    const float expected_tick_5 = std::sqrt(4.f / 20.f * 1.6f);
    EXPECT_NEAR(mc::deathFlipFraction(0.25f), expected_tick_5, kTolerance);

    // Tick 10 (0.50s): sqrt((10 - 1) / 20 * 1.6) = sqrt(0.72) ~ 0.848528
    const float expected_tick_10 = std::sqrt(9.f / 20.f * 1.6f);
    EXPECT_NEAR(mc::deathFlipFraction(0.50f), expected_tick_10, kTolerance);

    // At approx tick 13.5 (~0.675s), curve reaches 1.0. Beyond that it clamps to 1.0
    EXPECT_FLOAT_EQ(mc::deathFlipFraction(0.70f), 1.0f);
    EXPECT_FLOAT_EQ(mc::deathFlipFraction(1.00f), 1.0f);
    EXPECT_FLOAT_EQ(mc::deathFlipFraction(5.00f), 1.0f);

    // Strictly monotonic progression
    float prev = 0.f;
    for (int tick = 1; tick <= 20; ++tick) {
        float cur = mc::deathFlipFraction(tick * 0.05f);
        EXPECT_GE(cur, prev);
        prev = cur;
    }
}

TEST(DeathKinematicsTest, DeathFallMatrixRotatesModelNinetyDegreesOntoItsSide) {
    const mc::Vec3 feet{5.0f, 2.0f, -3.0f};
    const float yaw = 0.785398f; // 45 degrees
    const mc::Vec3 head{5.0f, 3.8f, -3.0f}; // 1.8m straight above feet

    // Fraction 0: Model is vertical, head is at original position
    const mc::rig::Mat4 m_zero = mc::rig::deathFallMatrix(feet, yaw, 0.0f);
    const mc::Vec3 head_zero = mc::rig::transformPoint(m_zero, head);
    EXPECT_NEAR(head_zero.x, head.x, 1e-4f);
    EXPECT_NEAR(head_zero.y, head.y, 1e-4f);
    EXPECT_NEAR(head_zero.z, head.z, 1e-4f);

    // Fraction 1: Model is 90 degrees turned onto side
    const mc::rig::Mat4 m_full = mc::rig::deathFallMatrix(feet, yaw, 1.0f);
    const mc::Vec3 head_lying = mc::rig::transformPoint(m_full, head);

    // Height should now match feet height
    EXPECT_NEAR(head_lying.y, feet.y, 1e-3f);

    // Distance to feet should remain 1.8m (rigid rotation about feet)
    const float dx = head_lying.x - feet.x;
    const float dz = head_lying.z - feet.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    EXPECT_NEAR(dist, 1.8f, 1e-3f);

    // Displaced side is perpendicular to heading forward vector
    const float fwd_x = std::sin(yaw);
    const float fwd_z = std::cos(yaw);
    const float dot_forward = dx * fwd_x + dz * fwd_z;
    EXPECT_NEAR(dot_forward, 0.0f, 1e-3f);
}

TEST(PlayerDeathTrackerTest, LifeCycleModelVisibilityAndCorpseExpiry) {
    mc::PlayerDeathTracker tracker;

    // Initially alive
    EXPECT_FALSE(tracker.isDead());
    EXPECT_FALSE(tracker.isDying());
    EXPECT_FALSE(tracker.isCorpseExpired());
    EXPECT_TRUE(tracker.shouldDrawModel());
    EXPECT_FLOAT_EQ(tracker.redTintAlpha(), 0.0f);

    // Tick 1: Player dies
    tracker.update(0.05f, /*is_dead=*/true);
    EXPECT_TRUE(tracker.isDead());
    EXPECT_TRUE(tracker.justDied());
    EXPECT_TRUE(tracker.isDying());
    EXPECT_FALSE(tracker.isCorpseExpired());
    EXPECT_TRUE(tracker.shouldDrawModel()); // still visible during death flip
    EXPECT_FLOAT_EQ(tracker.redTintAlpha(), 1.0f);

    // Tick 2: Subsequent dying tick
    tracker.update(0.05f, /*is_dead=*/true);
    EXPECT_FALSE(tracker.justDied());
    EXPECT_TRUE(tracker.isDying());
    EXPECT_TRUE(tracker.shouldDrawModel());

    // Advance to 0.95s (19 ticks): Still dying, corpse not yet expired, model still visible
    for (int i = 0; i < 17; ++i) {
        tracker.update(0.05f, /*is_dead=*/true);
        EXPECT_TRUE(tracker.shouldDrawModel());
        EXPECT_FALSE(tracker.isCorpseExpired());
    }

    // Tick 20 (1.00s == kDeathSeconds, 20 ticks): Corpse expires! Smoke event triggers, model MUST DISAPPEAR
    tracker.update(0.05f, /*is_dead=*/true);
    EXPECT_NEAR(tracker.deathSeconds(), 1.0f, 1e-4f);
    EXPECT_TRUE(tracker.isDead());
    EXPECT_FALSE(tracker.isDying());
    EXPECT_TRUE(tracker.isCorpseExpired());
    EXPECT_TRUE(tracker.justExpired()) << "Death smoke/particles trigger at tick 20";
    EXPECT_FALSE(tracker.shouldDrawModel()) << "Player model MUST disappear after corpse expires!";

    // Tick 21 (> 1.0s): Continues to be expired and hidden, justExpired returns to false
    tracker.update(0.05f, /*is_dead=*/true);
    EXPECT_FALSE(tracker.justExpired());
    EXPECT_TRUE(tracker.isCorpseExpired());
    EXPECT_FALSE(tracker.shouldDrawModel());
}

TEST(PlayerDeathTrackerTest, AtomicRespawnResetRestoresVisibilityAndClearsKinematics) {
    mc::PlayerDeathTracker tracker;
    mc::SteveAnimator animator;

    // Cause some animation and then die
    mc::SteveAnimInput in{};
    in.forward_speed = 4.317f;
    animator.update(0.1f, in);
    EXPECT_NE(animator.getTransforms()[static_cast<size_t>(mc::StevePart::RightLeg)].rot.y, 0.0f);

    // Die for 2.0s so corpse expires
    for (int i = 0; i < 40; ++i) {
        tracker.update(0.05f, /*is_dead=*/true);
    }
    EXPECT_TRUE(tracker.isCorpseExpired());
    EXPECT_FALSE(tracker.shouldDrawModel());

    // Player respawns (is_dead becomes false)
    tracker.update(0.05f, /*is_dead=*/false);
    EXPECT_TRUE(tracker.justRespawned());
    EXPECT_FALSE(tracker.isDead());
    EXPECT_FALSE(tracker.isDying());
    EXPECT_FALSE(tracker.isCorpseExpired());
    EXPECT_TRUE(tracker.shouldDrawModel()) << "Model must be visible again upon respawn";
    EXPECT_FLOAT_EQ(tracker.deathSeconds(), 0.0f);
    EXPECT_FLOAT_EQ(tracker.fallFraction(), 0.0f);

    // Reset animator on respawn
    animator.reset();
    for (const auto& t : animator.getTransforms()) {
        EXPECT_FLOAT_EQ(t.rot.x, 0.0f);
        EXPECT_FLOAT_EQ(t.rot.y, 0.0f);
        EXPECT_FLOAT_EQ(t.rot.z, 0.0f);
        EXPECT_FLOAT_EQ(t.rot.w, 1.0f);
        EXPECT_FLOAT_EQ(t.pos.x, 0.0f);
        EXPECT_FLOAT_EQ(t.pos.y, 0.0f);
        EXPECT_FLOAT_EQ(t.pos.z, 0.0f);
    }
}
