#include <gtest/gtest.h>

#include "eldenring_fp_camera.hpp"

#include <cmath>

using namespace eldenring::fpcam;

namespace {

constexpr float kTolerance = 1e-4f;

void expectVecNear(const float actual[3], float x, float y, float z) {
    EXPECT_NEAR(actual[0], x, kTolerance);
    EXPECT_NEAR(actual[1], y, kTolerance);
    EXPECT_NEAR(actual[2], z, kTolerance);
}

float length(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

} // namespace

TEST(EldenRingFpCamera, DownPitchIsZeroLevelAndNinetyStraightDown) {
    const float level[3] = {0.f, 0.f, 1.f};
    const float down[3] = {0.f, -1.f, 0.f};
    const float up[3] = {0.f, 1.f, 0.f};
    const float thirty[3] = {0.f, -0.5f, 0.8660254f};

    EXPECT_NEAR(downPitchDeg(level), 0.f, kTolerance);
    EXPECT_NEAR(downPitchDeg(down), 90.f, kTolerance);
    EXPECT_NEAR(downPitchDeg(up), -90.f, kTolerance);
    EXPECT_NEAR(downPitchDeg(thirty), 30.f, 0.01f);
}

TEST(EldenRingFpCamera, RotateDownTurnsForwardDownAndKeepsUnitLength) {
    float up[3] = {0.f, 1.f, 0.f};
    float forward[3] = {0.f, 0.f, 1.f};

    rotateDown(up, forward, 30.f);

    expectVecNear(forward, 0.f, -0.5f, 0.8660254f);
    expectVecNear(up, 0.f, 0.8660254f, 0.5f);
    EXPECT_NEAR(length(forward), 1.f, kTolerance);
    EXPECT_NEAR(length(up), 1.f, kTolerance);
}

TEST(EldenRingFpCamera, RotateDownRoundTrips) {
    float up[3] = {0.f, 1.f, 0.f};
    float forward[3] = {0.f, 0.f, 1.f};

    rotateDown(up, forward, 47.f);
    rotateDown(up, forward, -47.f);

    expectVecNear(forward, 0.f, 0.f, 1.f);
    expectVecNear(up, 0.f, 1.f, 0.f);
}

TEST(EldenRingFpCamera, PitchOffsetSeedsFromTheGameWithoutMoving) {
    PitchOffset pitch;

    EXPECT_FLOAT_EQ(pitch.update(0.f, 10.f), 10.f);
    EXPECT_FLOAT_EQ(pitch.offsetDeg(), 0.f);
    EXPECT_TRUE(pitch.seeded());
}

TEST(EldenRingFpCamera, PitchOffsetFollowsTheGameInsideItsRange) {
    PitchOffset pitch;
    pitch.update(0.f, 10.f); // seed

    // The game answers a 10-count mouse move with 1 degree: k = 0.1.
    const float effective = pitch.update(10.f, 11.f);

    EXPECT_NEAR(pitch.degPerCount(), 0.1f, kTolerance);
    EXPECT_NEAR(effective, 11.f, kTolerance);
    EXPECT_NEAR(pitch.offsetDeg(), 0.f, kTolerance);
}

TEST(EldenRingFpCamera, PitchOffsetExtendsPastTheDownClampToTheLimit) {
    PitchOffset pitch;
    pitch.update(0.f, 50.f);  // seed
    pitch.update(10.f, 51.f); // calibrate k = 0.1

    // Ramp to the game's clamp at 55 degrees: one degree per 10-count mouse step, offset stays at zero.
    float effective = 51.f;
    for (float game_down = 52.f; game_down <= 55.f; game_down += 1.f) {
        effective = pitch.update(10.f, game_down);
    }
    ASSERT_NEAR(effective, 55.f, kTolerance);
    ASSERT_NEAR(pitch.offsetDeg(), 0.f, 0.01f);

    // Keep pushing down: the game no longer moves, so the offset takes over.
    for (int i = 0; i < 40; ++i) {
        effective = pitch.update(10.f, 55.f);
    }

    EXPECT_NEAR(effective, 89.9f, kTolerance);
    EXPECT_NEAR(pitch.offsetDeg(), 34.9f, kTolerance);
}

TEST(EldenRingFpCamera, PitchOffsetReturnsAtNormalSpeedWhenTheGameUnclamps) {
    PitchOffset pitch;
    pitch.update(0.f, 50.f);
    pitch.update(10.f, 51.f);

    float effective = 51.f;
    for (float game_down = 52.f; game_down <= 55.f; game_down += 1.f) {
        effective = pitch.update(10.f, game_down);
    }
    for (int i = 0; i < 40; ++i) {
        effective = pitch.update(10.f, 55.f);
    }
    ASSERT_NEAR(effective, 89.9f, kTolerance);

    // Move the mouse up 10 degrees: the game leaves the clamp by 10 degrees and the view follows.
    effective = pitch.update(-100.f, 45.f);
    EXPECT_NEAR(effective, 79.9f, 0.01f);
    EXPECT_NEAR(pitch.offsetDeg(), 34.9f, 0.01f);
}

TEST(EldenRingFpCamera, PitchOffsetHandlesInvertedY) {
    PitchOffset pitch;
    pitch.update(0.f, 10.f); // seed

    // Inverted Y: a mouse move up (-10 counts) increases the game's down pitch by 1 degree.
    const float effective = pitch.update(-10.f, 11.f);

    EXPECT_NEAR(pitch.degPerCount(), -0.1f, kTolerance);
    EXPECT_NEAR(effective, 11.f, kTolerance);
    EXPECT_NEAR(pitch.offsetDeg(), 0.f, kTolerance);
}

TEST(EldenRingFpCamera, PitchOffsetFollowsTheGameWhenItMovesWithoutMouseInput) {
    PitchOffset pitch;
    pitch.update(0.f, 10.f);
    pitch.update(10.f, 11.f);

    // A lock-on or cutscene moves the pitch with no mouse input: the view follows, no offset is kept.
    const float effective = pitch.update(0.f, 25.f);

    EXPECT_NEAR(effective, 25.f, kTolerance);
    EXPECT_NEAR(pitch.offsetDeg(), 0.f, kTolerance);
}

TEST(EldenRingFpCamera, PitchOffsetClampsUpwardToo) {
    PitchOffset pitch;
    pitch.setMaxPitchDeg(90.f);
    pitch.update(0.f, 0.f);
    pitch.update(-10.f, -1.f); // calibrate k = 0.1 with normal Y

    // Ramp to the game's up clamp at -60 degrees.
    float effective = -1.f;
    for (float game_down = -2.f; game_down >= -60.f; game_down -= 1.f) {
        effective = pitch.update(-10.f, game_down);
    }
    ASSERT_NEAR(effective, -60.f, kTolerance);
    ASSERT_NEAR(pitch.offsetDeg(), 0.f, 0.01f);

    for (int i = 0; i < 40; ++i) {
        effective = pitch.update(-10.f, -60.f);
    }

    EXPECT_NEAR(effective, -89.9f, kTolerance);
}

TEST(EldenRingFpCamera, PitchOffsetHonoursAFixedSensitivity) {
    PitchOffset pitch;
    pitch.setDegPerCount(0.2f);
    pitch.update(0.f, 10.f);

    const float effective = pitch.update(10.f, 12.f); // game answered 2 degrees; fixed k also says 2

    EXPECT_FLOAT_EQ(pitch.degPerCount(), 0.2f);
    EXPECT_NEAR(effective, 12.f, kTolerance);
    EXPECT_NEAR(pitch.offsetDeg(), 0.f, kTolerance);
}

TEST(EldenRingFpCamera, PitchOffsetDiscardsADiscontinuity) {
    PitchOffset pitch;
    pitch.update(0.f, 10.f);

    const float effective = pitch.update(5000.f, 10.f);

    EXPECT_NEAR(effective, 10.f, kTolerance);
    EXPECT_NEAR(pitch.offsetDeg(), 0.f, kTolerance);
}

TEST(EldenRingFpCamera, ResetForgetsTheOffsetAndTheSeed) {
    PitchOffset pitch;
    pitch.update(0.f, 10.f);
    pitch.update(10.f, 11.f);

    pitch.reset();

    EXPECT_FALSE(pitch.seeded());
    EXPECT_FLOAT_EQ(pitch.offsetDeg(), 0.f);
}
