#include <gtest/gtest.h>

#include "eldenring_jump.hpp"

using namespace eldenring::live;

TEST(McJump, MinecraftJumpPeaksAtAboutOnePointTwoFiveMetres) {
    // 0.42 blocks per tick up, 0.08 per tick^2 down, with the 0.98 drag: about 1.25 blocks
    EXPECT_NEAR(mcJumpPeakHeight(), 1.25f, 0.06f);
}

TEST(McJump, WithoutDragTheSameSpeedGivesALittleLessHeight) {
    // v^2 / (2 g) with g = 32 m/s^2 (0.08 * 20 * 20): 1.10 m. Minecraft's 0.98 drag per tick makes its real jump 1.25 m, so a jump that
    // uses plain gravity needs a somewhat higher start speed to match.
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
    EXPECT_NEAR(t.estimatedGravity(), 0.f, 100.f); // computed, see below
}

TEST(McJump, GravityIsEstimatedFromTheRiseAndFall) {
    // y = v t - g t^2 / 2 sampled every 0.05 s with v = 8.4, g = 32
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
