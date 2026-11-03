#include <gtest/gtest.h>

#include "mc/riptide.hpp"

TEST(RiptideModelTest, StrengthMatchesTheRiptideEnchantment) {
    EXPECT_FLOAT_EQ(mc::RiptideModel::strengthForLevel(0), 0.f);
    EXPECT_FLOAT_EQ(mc::RiptideModel::strengthForLevel(1), 1.5f);
    EXPECT_FLOAT_EQ(mc::RiptideModel::strengthForLevel(2), 2.25f);
    EXPECT_FLOAT_EQ(mc::RiptideModel::strengthForLevel(3), 3.0f);
}

TEST(RiptideModelTest, CanUseNeedsWaterOrRainAndALevel) {
    EXPECT_TRUE(mc::RiptideModel::canUse(true, 1));
    EXPECT_FALSE(mc::RiptideModel::canUse(false, 1));
    EXPECT_FALSE(mc::RiptideModel::canUse(true, 0));
}

TEST(RiptideModelTest, LaunchVelocityIsTheNormalizedLookTimesStrength) {
    const mc::Vec3 v = mc::RiptideModel::launchVelocity({0.f, 0.f, 2.f}, 2);
    EXPECT_NEAR(v.x, 0.f, 1e-6f);
    EXPECT_NEAR(v.y, 0.f, 1e-6f);
    EXPECT_NEAR(v.z, 2.25f, 1e-6f);
}

TEST(RiptideModelTest, StartAddsTheLaunchToTheCurrentVelocityAndStartsTwentyTicks) {
    mc::RiptideModel model;
    ASSERT_TRUE(model.start(1, {0.f, 0.f, 1.f}, false, {0.2f, 0.f, 0.1f}));
    EXPECT_TRUE(model.active());
    EXPECT_EQ(model.ticksLeft(), 20);
    EXPECT_NEAR(model.state().velocity.x, 0.2f, 1e-6f);
    EXPECT_NEAR(model.state().velocity.y, 0.f, 1e-6f);
    EXPECT_NEAR(model.state().velocity.z, 1.6f, 1e-6f);
}

TEST(RiptideModelTest, GroundReleaseAddsTheOnePointTwoMetreHop) {
    mc::RiptideModel model;
    ASSERT_TRUE(model.start(1, {0.f, 0.f, 1.f}, true, {}));
    EXPECT_NEAR(model.state().velocity.y, 1.2f, 1e-6f);
}

TEST(RiptideModelTest, EndTickAppliesGravityThenTheAirDrag) {
    mc::RiptideModel model;
    ASSERT_TRUE(model.start(1, {0.f, 0.f, 1.f}, false, {}));
    model.endTick();
    EXPECT_EQ(model.ticksLeft(), 19);
    EXPECT_NEAR(model.state().velocity.x, 0.f, 1e-6f);
    EXPECT_NEAR(model.state().velocity.y, -0.08f * 0.98f, 1e-6f);
    EXPECT_NEAR(model.state().velocity.z, 1.5f * 0.91f, 1e-6f);
}

TEST(RiptideModelTest, TheSpinEndsAfterTwentyTicks) {
    mc::RiptideModel model;
    ASSERT_TRUE(model.start(1, {0.f, 0.f, 1.f}, false, {}));
    for (int i = 0; i < 19; ++i) model.endTick();
    EXPECT_TRUE(model.active());
    model.endTick();
    EXPECT_FALSE(model.active());
}

TEST(RiptideModelTest, RejectsLevelZeroAndADegenerateLook) {
    mc::RiptideModel model;
    EXPECT_FALSE(model.start(0, {0.f, 0.f, 1.f}, false, {}));
    EXPECT_FALSE(model.start(1, {0.f, 0.f, 0.f}, false, {}));
    EXPECT_FALSE(model.active());
}

TEST(RiptideModelTest, StopEndsTheSpin) {
    mc::RiptideModel model;
    ASSERT_TRUE(model.start(3, {0.f, 1.f, 0.f}, false, {}));
    model.stop();
    EXPECT_FALSE(model.active());
}
