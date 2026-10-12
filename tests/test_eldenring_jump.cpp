#include <gtest/gtest.h>

#include "eldenring_jump.hpp"

using namespace eldenring::live;
using namespace eldenring::jump;

TEST(McJump, MinecraftJumpPeaksAtAboutOnePointTwoFiveMetres) {
    // 0.42 blocks per tick up, 0.08 per tick^2 down, with the 0.98 drag: about 1.25 blocks
    EXPECT_NEAR(mcJumpPeakHeight(), 1.25f, 0.06f);
}

TEST(McJump, WithoutDragTheSameSpeedGivesALittleLessHeight) {
    EXPECT_NEAR(kMcJumpSpeed * kMcJumpSpeed / (2.f * kMcGravity), 1.10f, 0.01f);
    const float v_for_peak = std::sqrt(2.f * kMcGravity * mcJumpPeakHeight());
    EXPECT_GT(v_for_peak, kMcJumpSpeed);
    EXPECT_NEAR(v_for_peak, 8.95f, 0.15f);
}

TEST(McJump, TraceReportsThePeakAndTheAirTime) {
    JumpTrace t;
    t.start(10.0f);
    t.sample(10.5f, 0.05f);
    t.sample(11.2f, 0.05f);
    t.sample(11.0f, 0.05f);
    t.sample(10.0f, 0.05f);
    EXPECT_NEAR(t.peak(), 1.2f, 1e-5f);
    EXPECT_NEAR(t.seconds(), 0.2f, 1e-5f);
    EXPECT_NEAR(t.estimatedGravity(), 0.f, 100.f);
}

TEST(McJump, GravityIsEstimatedFromTheRiseAndFall) {
    JumpTrace t;
    t.start(0.f);
    for (int i = 1; i <= 10; ++i) {
        const float s = 0.05f * static_cast<float>(i);
        t.sample(8.4f * s - 16.f * s * s, 0.05f, 8.4f - 32.f * s);
    }
    EXPECT_NEAR(t.estimatedGravity(), 32.f, 2.f);
}

TEST(McJump, NoSamplesNoGravity) {
    JumpTrace t;
    t.start(1.f);
    EXPECT_FLOAT_EQ(t.estimatedGravity(), 0.f);
}

// ---- New Minecraft Jump Controller tests ----------------------------------------------------

TEST(McJump, CanTakeOffAllowsJumpingOnPlacedBlocksAndTerrain) {
    // Standing on a placed block (native ground = 0, supported_by_block = true) -> MUST be allowed!
    EXPECT_TRUE(canTakeOff(false, true, false, false));

    // Standing on native ground (native ground = 1, supported_by_block = false) -> MUST be allowed!
    EXPECT_TRUE(canTakeOff(true, false, false, false));

    // Atomic ground fallback flag -> MUST be allowed!
    EXPECT_TRUE(canTakeOff(false, false, true, false));
}

TEST(McJump, CanTakeOffRejectsWhenInAirOrDead) {
    // Falling in mid-air (both ground and blocks false) -> CANNOT jump!
    EXPECT_FALSE(canTakeOff(false, false, false, false));

    // Player dead -> CANNOT jump!
    EXPECT_FALSE(canTakeOff(true, true, true, true));
}

TEST(McJump, SprintJumpBoostsAndPreservesHorizontalSpeed) {
    MinecraftJumpController controller;
    const float sprint_speed = 5.612f;

    // Start a sprint jump moving along +Z
    controller.start(10.0f, 0.0f, sprint_speed, true);
    EXPECT_TRUE(controller.isActive());
    // Sprint jump gives horizontal boost:
    EXPECT_GE(controller.vz(), sprint_speed);

    // Simulate 10 frames of air movement with forward key held
    for (int i = 0; i < 10; ++i) {
        const auto res = controller.update(0.05f, 0.0f, 1.0f, sprint_speed, true);
        EXPECT_TRUE(res.active);
        // Horizontal speed should remain high and never stall
        EXPECT_GE(res.vz, sprint_speed * 0.95f);
    }
}

TEST(McJump, JumpReachesMinecraftOnePointTwoFiveMeterPeak) {
    MinecraftJumpController controller;
    controller.start(0.0f, 0.0f, 0.0f, false);

    // Step until velocity turns negative (reached peak)
    for (int i = 0; i < 20; ++i) {
        controller.update(0.05f, 0.0f, 0.0f, 0.0f, false);
        if (controller.vy() <= 0.0f) break;
    }

    // Peak should be approximately 1.25 meters (matching authentic Minecraft 1.25 blocks)
    EXPECT_NEAR(controller.peak(), 1.25f, 0.10f);
}

TEST(McJump, AirSteeringMaintainsKinematics) {
    MinecraftJumpController controller;
    const float walk_speed = 4.317f;

    // Moving +Z, then steering +X in the air
    controller.start(0.0f, 0.0f, walk_speed, false);

    // Update 5 frames steering towards +X
    for (int i = 0; i < 5; ++i) {
        controller.update(0.05f, 1.0f, 0.0f, walk_speed, true);
    }

    // Velocity should now have gained significant +X component without halting:
    EXPECT_GT(controller.vx(), 1.0f);
}

TEST(McJump, LandingSnapsPositionAndResetsActiveState) {
    MinecraftJumpController controller;
    controller.start(5.0f, 0.0f, 0.0f, false);
    EXPECT_TRUE(controller.isActive());

    controller.land(5.0f);
    EXPECT_FALSE(controller.isActive());
    EXPECT_FLOAT_EQ(controller.vy(), 0.0f);
    EXPECT_FLOAT_EQ(controller.currentY(), 5.0f);
}

TEST(McJump, CeilingCollisionStopsAscentAndBeginsDescent) {
    MinecraftJumpController controller;
    controller.start(0.0f, 0.0f, 0.0f, false);
    // Ascend 1 tick
    controller.update(0.05f, 0.0f, 0.0f, 0.0f, false);
    EXPECT_GT(controller.vy(), 0.0f);

    // Hit ceiling at y = 0.5f
    controller.hitCeiling(0.5f);
    EXPECT_FLOAT_EQ(controller.currentY(), 0.5f);
    EXPECT_FLOAT_EQ(controller.vy(), 0.0f);
    EXPECT_TRUE(controller.isActive());

    // Next tick gravity turns velocity negative
    controller.update(0.05f, 0.0f, 0.0f, 0.0f, false);
    EXPECT_LT(controller.vy(), 0.0f);

    // Second tick position moves downward
    controller.update(0.05f, 0.0f, 0.0f, 0.0f, false);
    EXPECT_LT(controller.currentY(), 0.5f);
}

TEST(McJump, LandingOnBlockPreservesAirborneFlagForHorizontalVelocityDrive) {
    const auto flags = computeLandingFlags(true);
    EXPECT_TRUE(flags.on_ground_flag);
    EXPECT_TRUE(flags.airborne_flag);
    EXPECT_TRUE(flags.support_flag);
}

TEST(McJump, LandingOnTerrainClearsAirborneFlagForNativeRootMotion) {
    const auto flags = computeLandingFlags(false);
    EXPECT_TRUE(flags.on_ground_flag);
    EXPECT_FALSE(flags.airborne_flag);
    EXPECT_TRUE(flags.support_flag);
}

