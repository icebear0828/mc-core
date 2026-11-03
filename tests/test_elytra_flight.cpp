#include <gtest/gtest.h>

#include "mc/elytra_flight.hpp"

#include <cmath>

namespace {

const mc::Vec3 kLookForward{0.f, 0.f, 1.f};

mc::Vec3 lookForPitchDegrees(float pitch_down_degrees) {
    const float r = pitch_down_degrees * 3.14159265358979f / 180.f;
    return {0.f, -std::sin(r), std::cos(r)};
}

} // namespace

TEST(ElytraFlightModelTest, CanGlideRequiresAirborneGliderAndNoCreativeFlight) {
    EXPECT_TRUE(mc::ElytraFlightModel::canGlide(false, false, false, true, false));
    EXPECT_FALSE(mc::ElytraFlightModel::canGlide(true, false, false, true, false));
    EXPECT_FALSE(mc::ElytraFlightModel::canGlide(false, true, false, true, false));
    EXPECT_FALSE(mc::ElytraFlightModel::canGlide(false, false, true, true, false));
    EXPECT_FALSE(mc::ElytraFlightModel::canGlide(false, false, false, false, false));
    EXPECT_FALSE(mc::ElytraFlightModel::canGlide(false, false, false, true, true));
}

TEST(ElytraFlightModelTest, StartFailsOnGroundOrInWater) {
    mc::ElytraFlightModel flight;
    EXPECT_FALSE(flight.startGliding(false, false, {}));
    EXPECT_FALSE(flight.startGliding(true, true, {}));
    EXPECT_TRUE(flight.startGliding(true, false, {}));
    EXPECT_TRUE(flight.state().is_gliding);
}

TEST(ElytraFlightModelTest, LevelGlideConvergesToVanillaTerminal) {
    mc::ElytraFlightModel flight;
    ASSERT_TRUE(flight.startGliding(true, false, {}));
    for (int i = 0; i < 2000; ++i) flight.updateTick(kLookForward, false);

    const auto& v = flight.state().velocity;
    EXPECT_NEAR(v.x, 0.f, 1e-3f);
    EXPECT_NEAR(v.y, -0.14949f, 1e-3f);
    EXPECT_NEAR(v.z, 1.51017f, 1e-3f);
}

TEST(ElytraFlightModelTest, Dive45ConvergesToVanillaTerminal) {
    mc::ElytraFlightModel flight;
    ASSERT_TRUE(flight.startGliding(true, false, {}));
    const auto look = lookForPitchDegrees(45.f);
    for (int i = 0; i < 2000; ++i) flight.updateTick(look, false);

    const auto& v = flight.state().velocity;
    EXPECT_NEAR(v.x, 0.f, 1e-3f);
    EXPECT_NEAR(v.y, -0.67464f, 2e-3f);
    EXPECT_NEAR(v.z, 3.22826f, 5e-3f);
}

TEST(ElytraFlightModelTest, VerticalDiveConvergesToVanillaTerminal) {
    mc::ElytraFlightModel flight;
    ASSERT_TRUE(flight.startGliding(true, false, {}));
    const mc::Vec3 look{0.f, -1.f, 0.f};
    for (int i = 0; i < 2000; ++i) flight.updateTick(look, false);

    const auto& v = flight.state().velocity;
    EXPECT_NEAR(v.x, 0.f, 1e-3f);
    EXPECT_NEAR(v.y, -3.92f, 1e-3f);
    EXPECT_NEAR(v.z, 0.f, 1e-3f);
}

TEST(ElytraFlightModelTest, FireworkLerpConvergesTo1Point7Look) {
    mc::Vec3 v{};
    for (int i = 0; i < 100; ++i) v = mc::ElytraFlightModel::fireworkLerp(v, kLookForward);
    EXPECT_NEAR(v.x, 0.f, 1e-5f);
    EXPECT_NEAR(v.y, 0.f, 1e-5f);
    EXPECT_NEAR(v.z, 1.7f, 1e-4f);
}

TEST(ElytraFlightModelTest, CollisionDamageUsesLostHorizontalSpeed) {
    mc::ElytraFlightModel flight;
    EXPECT_NEAR(flight.collisionDamage({1.5f, 0.f, 0.f}, {0.f, 0.f, 0.f}), 12.f, 1e-4f);
    EXPECT_NEAR(flight.collisionDamage({0.2f, 0.f, 0.f}, {0.f, 0.f, 0.f}), 0.f, 1e-4f);
    EXPECT_NEAR(flight.collisionDamage({1.f, 0.f, 1.f}, {0.f, 0.f, 1.f}), 1.1421f, 1e-3f);
}

TEST(ElytraFlightModelTest, AfterHorizontalCollisionZerosOnlyTheHitAxis) {
    const mc::Vec3 v{1.f, 2.f, 3.f};
    const auto x = mc::ElytraFlightModel::afterHorizontalCollision(v, true, false);
    EXPECT_FLOAT_EQ(x.x, 0.f);
    EXPECT_FLOAT_EQ(x.y, 2.f);
    EXPECT_FLOAT_EQ(x.z, 3.f);

    const auto z = mc::ElytraFlightModel::afterHorizontalCollision(v, false, true);
    EXPECT_FLOAT_EQ(z.x, 1.f);
    EXPECT_FLOAT_EQ(z.z, 0.f);
}

TEST(ElytraFlightModelTest, SlowFallingUsesSmallGravity) {
    mc::ElytraFlightModel normal;
    mc::ElytraFlightModel slow;
    ASSERT_TRUE(normal.startGliding(true, false, {0.f, -1.f, 0.f}));
    ASSERT_TRUE(slow.startGliding(true, false, {0.f, -1.f, 0.f}));

    normal.updateTick(kLookForward, false);
    slow.updateTick(kLookForward, true);
    EXPECT_GT(slow.state().velocity.y, normal.state().velocity.y);
}

TEST(ElytraFlightModelTest, StopGlidingClearsFireworkAndTicks) {
    mc::ElytraFlightModel flight;
    ASSERT_TRUE(flight.startGliding(true, false, {}));
    ASSERT_TRUE(flight.useFireworkBoost(30));
    flight.updateTick(kLookForward, false);
    EXPECT_EQ(flight.state().firework_ticks_left, 29);
    EXPECT_EQ(flight.state().fall_fly_ticks, 1);

    flight.stopGliding();
    EXPECT_FALSE(flight.state().is_gliding);
    EXPECT_EQ(flight.state().firework_ticks_left, 0);
}

TEST(ElytraFlightModelTest, FireworkBoostRequiresGliding) {
    mc::ElytraFlightModel flight;
    EXPECT_FALSE(flight.useFireworkBoost(20));
    ASSERT_TRUE(flight.startGliding(true, false, {}));
    EXPECT_TRUE(flight.useFireworkBoost(20));
    EXPECT_FALSE(flight.useFireworkBoost(0));
}
