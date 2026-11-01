#include <gtest/gtest.h>

#include "eldenring_ow_dash.hpp"

#include <cmath>

namespace {

using eldenring::ow::RocketPunchDashConfig;
using eldenring::ow::rocketPunchDirection;
using eldenring::ow::rocketPunchDistance;
using eldenring::ow::rocketPunchSpeed;
using eldenring::ow::rocketPunchStateEnd;

TEST(OwDashTest, SpeedIsZeroOutsideTheThrustWindow) {
    const RocketPunchDashConfig cfg{};
    EXPECT_FLOAT_EQ(rocketPunchSpeed(0.0f, cfg), 0.f);
    EXPECT_FLOAT_EQ(rocketPunchSpeed(2.25f, cfg), 0.f);
    EXPECT_FLOAT_EQ(rocketPunchSpeed(3.05f, cfg), 0.f);
    EXPECT_FLOAT_EQ(rocketPunchSpeed(3.50f, cfg), 0.f);
    EXPECT_GT(rocketPunchSpeed(2.65f, cfg), 0.f);
}

TEST(OwDashTest, PeakSpeedMatchesTheSinProfile) {
    const RocketPunchDashConfig cfg{};
    const float span = cfg.end_time - cfg.start_time;
    const float expected_peak = cfg.distance_m * 3.14159265358979f / (2.f * span);
    EXPECT_NEAR(rocketPunchSpeed(2.65f, cfg), expected_peak, 1e-3f);
}

TEST(OwDashTest, IntegratesToTheConfiguredDistance) {
    const RocketPunchDashConfig cfg{};
    const float span = cfg.end_time - cfg.start_time;
    const int steps = 20000;
    const float dt = span / static_cast<float>(steps);
    double total = 0.0;
    for (int i = 0; i < steps; ++i) {
        const float t = cfg.start_time + (static_cast<float>(i) + 0.5f) * dt;
        total += rocketPunchSpeed(t, cfg) * dt;
    }
    EXPECT_NEAR(total, cfg.distance_m, 1e-3);
}

TEST(OwDashTest, DistanceIsMonotonicAndClamped) {
    const RocketPunchDashConfig cfg{};
    EXPECT_FLOAT_EQ(rocketPunchDistance(0.0f, cfg), 0.f);
    EXPECT_FLOAT_EQ(rocketPunchDistance(cfg.end_time, cfg), cfg.distance_m);
    EXPECT_FLOAT_EQ(rocketPunchDistance(3.5f, cfg), cfg.distance_m);
    EXPECT_NEAR(rocketPunchDistance(2.65f, cfg), cfg.distance_m * 0.5f, 1e-3f);
    float prev = 0.f;
    for (float t = cfg.start_time; t <= cfg.end_time; t += 0.01f) {
        const float d = rocketPunchDistance(t, cfg);
        EXPECT_GE(d, prev);
        prev = d;
    }
}

TEST(OwDashTest, DirectionProjectsToTheHorizontalPlane) {
    float x = 0.f, z = 0.f;
    const float forward[3] = {0.f, -0.8f, 2.f}; // looking down and north
    ASSERT_TRUE(rocketPunchDirection(forward, x, z));
    EXPECT_NEAR(x, 0.f, 1e-6f);
    EXPECT_NEAR(z, 1.f, 1e-6f);

    const float vertical[3] = {0.f, -1.f, 0.f};
    EXPECT_FALSE(rocketPunchDirection(vertical, x, z));
}

TEST(OwDashTest, StateEndKeepsTheWholeThrustWindow) {
    // The tail trim must not cut the 2.25-3.05 s window: that would shrink the
    // integrated distance to ~40% of ow_dash_distance.
    EXPECT_NEAR(rocketPunchStateEnd(2.25f, 0.35f, 3.567f), 3.05f, 1e-4f);
    EXPECT_NEAR(rocketPunchStateEnd(2.25f, 0.f, 3.567f), 3.05f, 1e-4f);
    // A tail past the window still trims, clamped to the clip.
    EXPECT_NEAR(rocketPunchStateEnd(2.25f, 2.f, 3.567f), 3.567f, 1e-4f);
    // Without a loaded clip the window still closes at 3.05.
    EXPECT_NEAR(rocketPunchStateEnd(2.25f, 0.35f, 0.f), 3.05f, 1e-4f);
}

} // namespace
