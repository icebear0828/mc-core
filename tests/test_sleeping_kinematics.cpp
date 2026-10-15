#include <gtest/gtest.h>

#include "mc/animator.hpp"
#include "mc/rig.hpp"
#include "mc/sleep.hpp"

#include <cmath>
#include <vector>

namespace mc {

TEST(SleepingKinematicsTest, TransitionCurveIsSmoothMonotonicAndCapped) {
    constexpr float duration = 0.8f;
    EXPECT_FLOAT_EQ(sleepTransitionFraction(-0.5f, duration), 0.0f);
    EXPECT_FLOAT_EQ(sleepTransitionFraction(0.0f, duration), 0.0f);
    EXPECT_FLOAT_EQ(sleepTransitionFraction(duration, duration), 1.0f);
    EXPECT_FLOAT_EQ(sleepTransitionFraction(duration + 1.0f, duration), 1.0f);

    float prev = -1.0f;
    for (int i = 0; i <= 80; ++i) {
        const float t = i * 0.01f;
        const float frac = sleepTransitionFraction(t, duration);
        EXPECT_GE(frac, 0.0f);
        EXPECT_LE(frac, 1.0f);
        if (prev >= 0.0f) {
            EXPECT_GE(frac, prev); // Monotonically increasing
        }
        prev = frac;
    }

    // Smooth derivatives: slope at t=0 and t=duration must be approximately 0 (smoothstep)
    const float dt = 1e-4f;
    const float slope_start = (sleepTransitionFraction(dt, duration) - sleepTransitionFraction(0.0f, duration)) / dt;
    const float slope_end = (sleepTransitionFraction(duration, duration) - sleepTransitionFraction(duration - dt, duration)) / dt;
    EXPECT_NEAR(slope_start, 0.0f, 0.01f);
    EXPECT_NEAR(slope_end, 0.0f, 0.01f);
}

TEST(SleepingKinematicsTest, SleepPoseMatrixLaysModelFlatOnBackWithGroundOffset) {
    const Vec3 feet{10.0f, 2.0f, 15.0f};
    const float yaw = 0.0f; // Facing +Z
    constexpr float ground_offset = 0.1125f;

    // At fraction 0, it must be the identity transform relative to feet
    const rig::Mat4 m0 = rig::sleepPoseMatrix(feet, yaw, 0.0f, ground_offset);
    const Vec3 p0 = rig::transformPoint(m0, feet);
    EXPECT_NEAR(p0.x, feet.x, 1e-4f);
    EXPECT_NEAR(p0.y, feet.y, 1e-4f);
    EXPECT_NEAR(p0.z, feet.z, 1e-4f);

    // At fraction 1, the model lies flat on its back:
    // Standing: feet at (10, 2, 15), head was at (10, 2 + 1.8, 15), chest pointed +Z, back pointed -Z
    // When sleeping flat on back:
    // Head rotates backwards to -Z: head Z should be < feet.z
    // Chest (normal +Z) rotates to face upwards toward sky (+Y)
    // Back (normal -Z) rotates to face downwards toward ground (-Y)
    const rig::Mat4 m1 = rig::sleepPoseMatrix(feet, yaw, 1.0f, ground_offset);

    // Transform head point in standing pose: (feet.x, feet.y + 1.8f, feet.z)
    const Vec3 standing_head{feet.x, feet.y + 1.8f, feet.z};
    const Vec3 sleeping_head = rig::transformPoint(m1, standing_head);

    // Head is now horizontally extended backwards along -Z (behind feet)
    EXPECT_NEAR(sleeping_head.x, feet.x, 1e-3f);
    EXPECT_LT(sleeping_head.z, feet.z - 1.5f); // 1.8m behind feet
    // Head Y should be approximately ground level + offset
    EXPECT_NEAR(sleeping_head.y, feet.y + ground_offset, 0.05f);

    // Transform chest point: (feet.x, feet.y + 1.0f, feet.z + 0.2f)
    const Vec3 chest_point{feet.x, feet.y + 1.0f, feet.z + 0.2f};
    const Vec3 sleeping_chest = rig::transformPoint(m1, chest_point);
    // Chest must be facing upwards (+Y higher than back)
    EXPECT_GT(sleeping_chest.y, feet.y + ground_offset);
}

TEST(SleepingKinematicsTest, SleepPoseMatrixSupportsArbitraryYawHeadings) {
    const Vec3 feet{0.0f, 0.0f, 0.0f};
    constexpr float ground_offset = 0.1125f;

    // Test across all 4 cardinal yaw directions: 0 (+Z), pi/2 (+X), pi (-Z), -pi/2 (-X)
    const std::vector<float> yaws = {0.0f, 1.5707963f, 3.14159265f, -1.5707963f};
    for (float yaw : yaws) {
        const rig::Mat4 m = rig::sleepPoseMatrix(feet, yaw, 1.0f, ground_offset);
        const Vec3 standing_head{0.0f, 1.8f, 0.0f};
        const Vec3 sleeping_head = rig::transformPoint(m, standing_head);

        // In all headings, sleeping head Y must stay at ground_offset
        EXPECT_NEAR(sleeping_head.y, ground_offset, 0.05f);

        // Distance from feet origin must equal standing height (1.8m)
        const float dist_sq = sleeping_head.x * sleeping_head.x + sleeping_head.z * sleeping_head.z;
        EXPECT_NEAR(std::sqrt(dist_sq), 1.8f, 0.05f);

        // Head must lie in the direction opposite to facing forward vector:
        const Vec3 forward{std::sin(yaw), 0.0f, std::cos(yaw)};
        const float dot = sleeping_head.x * forward.x + sleeping_head.z * forward.z;
        EXPECT_LT(dot, -1.5f); // Opposite to forward vector
    }
}

TEST(SleepingKinematicsTest, PlayerSleepTrackerLifecycleAndRespiration) {
    PlayerSleepTracker tracker;
    EXPECT_FALSE(tracker.isSleeping());
    EXPECT_FALSE(tracker.isTransitioning());
    EXPECT_FLOAT_EQ(tracker.sleepFraction(), 0.0f);

    // Start sleeping
    tracker.update(0.1f, true);
    EXPECT_TRUE(tracker.isTransitioning());
    EXPECT_GT(tracker.sleepFraction(), 0.0f);
    EXPECT_LT(tracker.sleepFraction(), 1.0f);

    // Complete transition (duration = 0.8s)
    for (int i = 0; i < 10; ++i) {
        tracker.update(0.1f, true);
    }
    EXPECT_TRUE(tracker.isSleeping());
    EXPECT_FALSE(tracker.isTransitioning());
    EXPECT_FLOAT_EQ(tracker.sleepFraction(), 1.0f);

    // Respiration oscillation during sleep
    const float breath1 = tracker.breathingAngle();
    EXPECT_LE(std::abs(breath1), 0.05f);
    tracker.update(0.75f, true); // Quarter cycle of 3.0s respiration
    const float breath2 = tracker.breathingAngle();
    EXPECT_NE(breath1, breath2); // Periodic oscillation
    EXPECT_LE(std::abs(breath2), 0.05f);

    // Wake up
    tracker.update(0.1f, false);
    EXPECT_TRUE(tracker.isTransitioning());
    EXPECT_LT(tracker.sleepFraction(), 1.0f);

    for (int i = 0; i < 10; ++i) {
        tracker.update(0.1f, false);
    }
    EXPECT_FALSE(tracker.isSleeping());
    EXPECT_FALSE(tracker.isTransitioning());
    EXPECT_FLOAT_EQ(tracker.sleepFraction(), 0.0f);
    EXPECT_FLOAT_EQ(tracker.breathingAngle(), 0.0f);

    // Reset test
    tracker.update(1.0f, true);
    EXPECT_TRUE(tracker.isSleeping());
    tracker.reset();
    EXPECT_FALSE(tracker.isSleeping());
    EXPECT_FLOAT_EQ(tracker.sleepFraction(), 0.0f);
}

TEST(SleepingKinematicsTest, AnimatorPutsLimbsIntoVanillaSleepingPose) {
    SteveAnimator animator;
    SteveAnimInput in{};
    in.forward_speed = 4.317f; // Walking speed before sleep
    in.look_yaw = 0.5f;
    in.look_pitch = -0.3f;
    in.swing_progress = 0.4f;
    in.is_sleeping = true;

    animator.update(0.05f, in);
    const auto& transforms = animator.getTransforms();

    const auto head = transforms[static_cast<size_t>(StevePart::Head)];
    const auto right_arm = transforms[static_cast<size_t>(StevePart::RightArm)];
    const auto left_arm = transforms[static_cast<size_t>(StevePart::LeftArm)];
    const auto right_leg = transforms[static_cast<size_t>(StevePart::RightLeg)];
    const auto left_leg = transforms[static_cast<size_t>(StevePart::LeftLeg)];

    // Legs must be parallel and straight (pitch 0, roll 0, yaw 0)
    EXPECT_NEAR(right_leg.rot.y, 0.0f, 1e-3f);
    EXPECT_NEAR(left_leg.rot.y, 0.0f, 1e-3f);
    EXPECT_NEAR(right_leg.rot.z, 0.0f, 1e-3f);
    EXPECT_NEAR(left_leg.rot.z, 0.0f, 1e-3f);

    // Head orientation must be locked to sleeping resting angle, not following look_yaw
    EXPECT_NEAR(head.rot.z, 0.0f, 1e-3f); // Head yaw locked

    // Arms straightened along sides with slight breathing vibration
    EXPECT_NEAR(right_arm.rot.z, 0.0f, 1e-3f); // Arm yaw straight
    EXPECT_NEAR(left_arm.rot.z, 0.0f, 1e-3f);
}

} // namespace mc
