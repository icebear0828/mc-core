#include <gtest/gtest.h>

#include "eldenring_sprint.hpp"

using namespace eldenring::sprint;

TEST(EldenRingSprintTest, InitialStateIsIdleWithDefaultFovAndAnim) {
    SprintController ctrl;
    EXPECT_FALSE(ctrl.isSprinting());
    EXPECT_FLOAT_EQ(ctrl.fovMultiplier(), 1.0f);
    EXPECT_FLOAT_EQ(ctrl.animSpeedMultiplier(), 1.0f);
}

TEST(EldenRingSprintTest, DoubleTapWWithinThresholdActivatesSprint) {
    SprintController ctrl;
    ctrl.enable_double_tap = true;

    // First tap of W
    ctrl.update(/*dt=*/0.016f, /*w_down=*/true, /*s_down=*/false, /*ctrl_down=*/false, /*food_level=*/20);
    EXPECT_FALSE(ctrl.isSprinting());

    // Release W for 100ms
    ctrl.update(0.100f, /*w_down=*/false, false, false, 20);
    EXPECT_FALSE(ctrl.isSprinting());

    // Second tap of W (within 300ms window)
    ctrl.update(0.016f, /*w_down=*/true, false, false, 20);
    EXPECT_TRUE(ctrl.isSprinting());

    // Holding W keeps sprint active
    ctrl.update(0.500f, /*w_down=*/true, false, false, 20);
    EXPECT_TRUE(ctrl.isSprinting());
}

TEST(EldenRingSprintTest, DoubleTapExceedingTimeoutFailsToSprint) {
    SprintController ctrl;
    ctrl.enable_double_tap = true;

    // First tap of W
    ctrl.update(0.016f, /*w_down=*/true, false, false, 20);

    // Release W for 350ms (> 300ms timeout)
    ctrl.update(0.350f, /*w_down=*/false, false, false, 20);

    // Second tap of W
    ctrl.update(0.016f, /*w_down=*/true, false, false, 20);
    EXPECT_FALSE(ctrl.isSprinting());
}

TEST(EldenRingSprintTest, CtrlKeyWithForwardImmediatelyActivatesSprint) {
    SprintController ctrl;

    // Pressing W without Ctrl -> Walking
    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/false, 20);
    EXPECT_FALSE(ctrl.isSprinting());

    // Pressing Ctrl while W is down -> Instantly Sprinting
    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, 20);
    EXPECT_TRUE(ctrl.isSprinting());
}

TEST(EldenRingSprintTest, ReleasingForwardOrPressingBackwardCancelsSprint) {
    SprintController ctrl;

    // Activate sprint via Ctrl
    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, 20);
    EXPECT_TRUE(ctrl.isSprinting());

    // Releasing forward (W=false) cancels sprint
    ctrl.update(0.016f, /*w_down=*/false, false, false, 20);
    EXPECT_FALSE(ctrl.isSprinting());

    // Reactivate and press backward (S=true) -> Cancels sprint
    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, 20);
    EXPECT_TRUE(ctrl.isSprinting());

    ctrl.update(0.016f, /*w_down=*/true, /*s_down=*/true, false, 20);
    EXPECT_FALSE(ctrl.isSprinting());
}

TEST(EldenRingSprintTest, HungerThresholdBlocksAndCancelsSprint) {
    SprintController ctrl;

    // In Minecraft, food_level <= 6 (3 shanks or fewer) blocks sprint initiation
    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, /*food_level=*/6);
    EXPECT_FALSE(ctrl.isSprinting());

    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, /*food_level=*/3);
    EXPECT_FALSE(ctrl.isSprinting());

    // Food level 7 allows sprint
    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, /*food_level=*/7);
    EXPECT_TRUE(ctrl.isSprinting());

    // If food level drops to 6 or below while sprinting, sprint drops immediately
    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, /*food_level=*/6);
    EXPECT_FALSE(ctrl.isSprinting());
}

TEST(EldenRingSprintTest, FovSmoothlyTransitionsToSprintMultiplier) {
    SprintController ctrl;
    EXPECT_FLOAT_EQ(ctrl.fovMultiplier(), 1.0f);

    // Enter sprint
    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, 20);
    EXPECT_TRUE(ctrl.isSprinting());

    // FOV smoothly increases towards 1.15
    for (int i = 0; i < 5; ++i) {
        ctrl.update(0.030f, true, false, true, 20);
    }
    EXPECT_GT(ctrl.fovMultiplier(), 1.05f);
    EXPECT_LE(ctrl.fovMultiplier(), 1.15f);

    // After ~200ms total, FOV reaches near 1.15
    for (int i = 0; i < 10; ++i) {
        ctrl.update(0.020f, true, false, true, 20);
    }
    EXPECT_NEAR(ctrl.fovMultiplier(), 1.15f, 0.005f);

    // Release sprint
    ctrl.update(0.016f, /*w_down=*/false, false, false, 20);
    EXPECT_FALSE(ctrl.isSprinting());

    // FOV smoothly returns to 1.0
    for (int i = 0; i < 15; ++i) {
        ctrl.update(0.020f, false, false, false, 20);
    }
    EXPECT_NEAR(ctrl.fovMultiplier(), 1.0f, 0.005f);
}

TEST(EldenRingSprintTest, AnimSpeedScalesDuringSprint) {
    SprintController ctrl;
    EXPECT_FLOAT_EQ(ctrl.animSpeedMultiplier(), 1.0f);

    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, 20);
    EXPECT_TRUE(ctrl.isSprinting());
    EXPECT_FLOAT_EQ(ctrl.animSpeedMultiplier(), 1.4f);

    ctrl.update(0.016f, /*w_down=*/false, false, false, 20);
    EXPECT_FALSE(ctrl.isSprinting());
    EXPECT_FLOAT_EQ(ctrl.animSpeedMultiplier(), 1.0f);
}

TEST(EldenRingSprintTest, BoostSpeedReturnsActiveSpeedOnSprint) {
    SprintController ctrl;
    EXPECT_FLOAT_EQ(ctrl.boostSpeed(), 0.0f);

    ctrl.update(0.016f, /*w_down=*/true, false, /*ctrl_down=*/true, 20);
    EXPECT_TRUE(ctrl.isSprinting());
    EXPECT_FLOAT_EQ(ctrl.boostSpeed(), 2.5f);

    ctrl.update(0.016f, /*w_down=*/false, false, false, 20);
    EXPECT_FALSE(ctrl.isSprinting());
    EXPECT_FLOAT_EQ(ctrl.boostSpeed(), 0.0f);
}
