#include <gtest/gtest.h>

#include "mc/ender_pearl.hpp"

namespace {

constexpr float kNoJitter[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

mc::EnderPearlLaunch forwardLaunch() {
    mc::EnderPearlLaunch launch;
    launch.origin = {1.f, 2.f, 3.f};
    launch.look = {0.f, 0.f, 1.f};
    return launch;
}

} // namespace

TEST(EnderPearlTest, LaunchVelocityIsLookTimesOnePointFiveWithoutJitter) {
    const mc::Vec3 v = mc::EnderPearl::launchVelocity({0.f, 0.f, 1.f}, 1.5f, 0.0172275f, kNoJitter);
    EXPECT_NEAR(v.x, 0.f, 1e-6f);
    EXPECT_NEAR(v.y, 0.f, 1e-6f);
    EXPECT_NEAR(v.z, 1.5f, 1e-6f);
}

TEST(EnderPearlTest, LaunchVelocityAddsTheTriangleJitterBeforeScaling) {
    // axis 0: (1 - 0) * spread = +spread; the other axes stay at the look value.
    const float jitter[6] = {1.f, 0.f, 0.5f, 0.5f, 0.5f, 0.5f};
    const mc::Vec3 v = mc::EnderPearl::launchVelocity({0.f, 0.f, 1.f}, 1.5f, 0.01f, jitter);
    EXPECT_NEAR(v.x, 0.01f * 1.5f, 1e-6f);
    EXPECT_NEAR(v.y, 0.f, 1e-6f);
    EXPECT_NEAR(v.z, 1.5f, 1e-6f);
}

TEST(EnderPearlTest, LaunchInheritsOwnerVelocityXZAlwaysAndYOnlyWhileAirborne) {
    mc::EnderPearlLaunch launch = forwardLaunch();
    launch.owner_velocity = {0.2f, -0.4f, 0.1f};

    mc::EnderPearl airborne;
    ASSERT_TRUE(airborne.launch(launch, kNoJitter));
    EXPECT_NEAR(airborne.state().velocity.x, 0.2f, 1e-6f);
    EXPECT_NEAR(airborne.state().velocity.y, -0.4f, 1e-6f);
    EXPECT_NEAR(airborne.state().velocity.z, 1.6f, 1e-6f);

    mc::EnderPearl grounded;
    launch.owner_on_ground = true;
    ASSERT_TRUE(grounded.launch(launch, kNoJitter));
    EXPECT_NEAR(grounded.state().velocity.y, 0.f, 1e-6f);
}

TEST(EnderPearlTest, TickAppliesGravityThenDragThenMoves) {
    mc::EnderPearl pearl;
    ASSERT_TRUE(pearl.launch(forwardLaunch(), kNoJitter));
    pearl.updateTick();

    const auto& s = pearl.state();
    EXPECT_NEAR(s.velocity.x, 0.f, 1e-6f);
    EXPECT_NEAR(s.velocity.y, -0.03f * 0.99f, 1e-6f);
    EXPECT_NEAR(s.velocity.z, 1.5f * 0.99f, 1e-6f);
    EXPECT_NEAR(s.position.x, 1.f, 1e-6f);
    EXPECT_NEAR(s.position.y, 2.f - 0.03f * 0.99f, 1e-6f);
    EXPECT_NEAR(s.position.z, 3.f + 1.5f * 0.99f, 1e-6f);
}

TEST(EnderPearlTest, TeleportTargetIsTheStartOfTheTickPosition) {
    mc::EnderPearl pearl;
    ASSERT_TRUE(pearl.launch(forwardLaunch(), kNoJitter));
    const mc::Vec3 start = pearl.state().position;
    pearl.updateTick();
    // oldPosition() is where the pearl was when the tick began, not the hit point.
    EXPECT_NEAR(pearl.teleportTarget().x, start.x, 1e-6f);
    EXPECT_NEAR(pearl.teleportTarget().y, start.y, 1e-6f);
    EXPECT_NEAR(pearl.teleportTarget().z, start.z, 1e-6f);
}

TEST(EnderPearlTest, AdapterPathCommitsAResolvedHitPosition) {
    mc::EnderPearl pearl;
    ASSERT_TRUE(pearl.launch(forwardLaunch(), kNoJitter));
    const mc::Vec3 start = pearl.state().position;
    pearl.beginTick();
    const mc::Vec3 move = pearl.moveVector();
    EXPECT_NEAR(move.z, 1.485f, 1e-6f);
    pearl.commit(start + move * 0.25f); // a wall 25% of the way through the tick
    EXPECT_NEAR(pearl.state().position.z, start.z + 1.485f * 0.25f, 1e-6f);
    EXPECT_NEAR(pearl.teleportTarget().z, start.z, 1e-6f);
}

TEST(EnderPearlTest, WaterUsesPointEightDrag) {
    mc::EnderPearl pearl;
    ASSERT_TRUE(pearl.launch(forwardLaunch(), kNoJitter));
    pearl.updateTick(true);
    EXPECT_NEAR(pearl.state().velocity.z, 1.5f * 0.8f, 1e-6f);
}

TEST(EnderPearlTest, SafetyCapDiscardsAndDiscardDeactivates) {
    mc::EnderPearlConfig config;
    config.max_ticks = 3;
    mc::EnderPearl pearl(config);
    ASSERT_TRUE(pearl.launch(forwardLaunch(), kNoJitter));
    pearl.updateTick();
    pearl.updateTick();
    EXPECT_TRUE(pearl.active());
    pearl.updateTick();
    EXPECT_FALSE(pearl.active());

    mc::EnderPearl other;
    ASSERT_TRUE(other.launch(forwardLaunch(), kNoJitter));
    other.discard();
    EXPECT_FALSE(other.active());
}

TEST(EnderPearlTest, RejectsADegenerateLookVector) {
    mc::EnderPearlLaunch launch = forwardLaunch();
    launch.look = {0.f, 0.f, 0.f};
    mc::EnderPearl pearl;
    EXPECT_FALSE(pearl.launch(launch, kNoJitter));
    EXPECT_FALSE(pearl.active());
}
