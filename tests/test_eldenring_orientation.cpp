#include <gtest/gtest.h>

#include "eldenring_orientation.hpp"
#include <cmath>

using namespace eldenring::motion;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEps = 1e-4f;

TEST(EldenRingOrientation, QuaternionFromYawRoundTripsCardinalHeadings) {
    // 0 rad (+Z forward)
    {
        const auto q = OrientationQuat::fromYaw(0.f);
        EXPECT_NEAR(q.x, 0.f, kEps);
        EXPECT_NEAR(q.y, 0.f, kEps);
        EXPECT_NEAR(q.z, 0.f, kEps);
        EXPECT_NEAR(q.w, 1.f, kEps);
        EXPECT_NEAR(q.toYaw(), 0.f, kEps);
    }
    // +90 deg (+X right)
    {
        const auto q = OrientationQuat::fromYaw(kPi * 0.5f);
        EXPECT_NEAR(q.x, 0.f, kEps);
        EXPECT_NEAR(q.y, std::sin(kPi * 0.25f), kEps);
        EXPECT_NEAR(q.z, 0.f, kEps);
        EXPECT_NEAR(q.w, std::cos(kPi * 0.25f), kEps);
        EXPECT_NEAR(q.toYaw(), kPi * 0.5f, kEps);
    }
    // 180 deg (-Z backward)
    {
        const auto q = OrientationQuat::fromYaw(kPi);
        EXPECT_NEAR(q.x, 0.f, kEps);
        EXPECT_NEAR(q.y, 1.f, kEps);
        EXPECT_NEAR(q.z, 0.f, kEps);
        EXPECT_NEAR(q.w, 0.f, kEps);
        EXPECT_NEAR(std::abs(q.toYaw()), kPi, kEps);
    }
    // -90 deg (-X left)
    {
        const auto q = OrientationQuat::fromYaw(-kPi * 0.5f);
        EXPECT_NEAR(q.x, 0.f, kEps);
        EXPECT_NEAR(q.y, -std::sin(kPi * 0.25f), kEps);
        EXPECT_NEAR(q.z, 0.f, kEps);
        EXPECT_NEAR(q.w, std::cos(kPi * 0.25f), kEps);
        EXPECT_NEAR(q.toYaw(), -kPi * 0.5f, kEps);
    }
}

TEST(EldenRingOrientation, InputDirectionMapsCameraAndKeys) {
    const float cam_fwd[3] = {0.f, 0.f, 1.f};
    const float cam_right[3] = {1.f, 0.f, 0.f};
    float dir[2] = {0.f, 0.f};

    // No keys -> neutral
    EXPECT_FALSE(computeInputDirection(0.f, 0.f, cam_fwd, cam_right, dir));

    // W pressed -> +Z
    EXPECT_TRUE(computeInputDirection(1.f, 0.f, cam_fwd, cam_right, dir));
    EXPECT_NEAR(dir[0], 0.f, kEps);
    EXPECT_NEAR(dir[1], 1.f, kEps);

    // S pressed -> -Z
    EXPECT_TRUE(computeInputDirection(-1.f, 0.f, cam_fwd, cam_right, dir));
    EXPECT_NEAR(dir[0], 0.f, kEps);
    EXPECT_NEAR(dir[1], -1.f, kEps);

    // D pressed -> +X
    EXPECT_TRUE(computeInputDirection(0.f, 1.f, cam_fwd, cam_right, dir));
    EXPECT_NEAR(dir[0], 1.f, kEps);
    EXPECT_NEAR(dir[1], 0.f, kEps);

    // A pressed -> -X
    EXPECT_TRUE(computeInputDirection(0.f, -1.f, cam_fwd, cam_right, dir));
    EXPECT_NEAR(dir[0], -1.f, kEps);
    EXPECT_NEAR(dir[1], 0.f, kEps);

    // W + D pressed -> diagonal normalized
    EXPECT_TRUE(computeInputDirection(1.f, 1.f, cam_fwd, cam_right, dir));
    const float expected_diag = 1.f / std::sqrt(2.f);
    EXPECT_NEAR(dir[0], expected_diag, kEps);
    EXPECT_NEAR(dir[1], expected_diag, kEps);
}

TEST(EldenRingOrientation, InstantTurnSnapsImmediatelyWithZeroLag) {
    const float current_yaw = 0.f;
    const float target_yaw = kPi; // 180 deg reverse
    const float dt = 0.0166667f;
    const float turn_speed = 5.f; // 5 rad/s

    const float next_yaw = stepYaw(current_yaw, target_yaw, dt, turn_speed, /*instant_turn=*/true);
    EXPECT_NEAR(std::abs(next_yaw), kPi, kEps);
}

TEST(EldenRingOrientation, SmoothTurnClampsAngularStepAndWrapsShortestPath) {
    const float dt = 0.02f;
    const float turn_speed = 10.f; // max step = 0.2 rad
    
    // Normal step from 0 towards 1.0 rad
    float yaw = stepYaw(0.f, 1.0f, dt, turn_speed, /*instant_turn=*/false);
    EXPECT_NEAR(yaw, 0.2f, kEps);

    // Near boundary wrap: from +3.10 rad to -3.10 rad (shortest path is across +-pi, distance 0.083 rad)
    const float curr = 3.10f;
    const float target = -3.10f;
    yaw = stepYaw(curr, target, dt, turn_speed, /*instant_turn=*/false);
    // Difference is ~ +0.083 rad, so it reaches target within max_step 0.2 rad!
    EXPECT_NEAR(yaw, target, kEps);
}

} // namespace
