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

TEST(McJump, ArcStartsAtZeroAndRisesThroughMinecraftsFirstTick) {
    McJumpArc a;
    EXPECT_FALSE(a.active());
    a.start();
    EXPECT_TRUE(a.active());
    EXPECT_FLOAT_EQ(a.advance(0.f), 0.f);
    // after one full 50 ms tick the feet are 0.42 m up (vy 0.42 blocks per tick)
    EXPECT_NEAR(a.advance(0.05f), 0.42f, 1e-4f);
}

TEST(McJump, ArcPeaksAtTheMinecraftHeightWhateverTheFrameRate) {
    for (const float dt : {1.f / 30.f, 1.f / 60.f, 1.f / 144.f}) {
        McJumpArc a;
        a.start();
        float peak = 0.f;
        for (int i = 0; i < 400; ++i) {
            peak = std::max(peak, a.advance(dt));
            if (a.descending() && a.height() < 0.f) break;
        }
        EXPECT_NEAR(peak, mcJumpPeakHeight(), 0.04f) << dt;
    }
}

TEST(McJump, ArcFallsBackBelowTheTakeoffAndReportsDescending) {
    McJumpArc a;
    a.start();
    EXPECT_FALSE(a.descending());
    float h = 0.f;
    int frames = 0;
    while (h > -0.5f && frames++ < 1000) h = a.advance(1.f / 60.f);
    EXPECT_TRUE(a.descending());
    EXPECT_LT(h, 0.f); // with no ground it keeps falling
    EXPECT_LT(frames, 120); // about 0.6 s up and down plus the drop
}

TEST(McJump, ArcIgnoresAHitchLongerThanAFewTicks) {
    McJumpArc a, b;
    a.start();
    b.start();
    a.advance(5.f);                 // a 5 s hitch is clamped (loading screen, alt-tab)
    for (int i = 0; i < 6; ++i) b.advance(1.f / 60.f); // 0.1 s
    EXPECT_NEAR(a.height(), b.height(), 0.2f);
}

TEST(McJump, EndingTheJumpStopsTheArc) {
    McJumpArc a;
    a.start();
    a.advance(0.1f);
    a.end();
    EXPECT_FALSE(a.active());
}
