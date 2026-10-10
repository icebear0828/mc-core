#include <gtest/gtest.h>

#include "eldenring_flight.hpp"

#include <algorithm>
#include <cmath>

using namespace eldenring::flight;

namespace {
Heading northHeading() {
    Heading h; // forward +z, right +x
    const float f[3] = {0.f, 0.f, 1.f}, r[3] = {1.f, 0.f, 0.f};
    EXPECT_TRUE(headingFromCamera(f, r, h));
    return h;
}
} // namespace

TEST(EldenRingFlight, DoubleTapNeedsTwoPressesInTime) {
    DoubleTap t;
    EXPECT_FALSE(t.update(true, 1000));  // first press
    EXPECT_FALSE(t.update(false, 1050)); // release
    EXPECT_TRUE(t.update(true, 1200));   // second press 200 ms after the first
    EXPECT_FALSE(t.update(false, 1250));
    EXPECT_FALSE(t.update(true, 1300));  // a third press begins a new pair, it does not toggle again
}

TEST(EldenRingFlight, DoubleTapIgnoresSlowPressesAndHeldKeys) {
    DoubleTap t;
    EXPECT_FALSE(t.update(true, 1000));
    EXPECT_FALSE(t.update(false, 1100));
    EXPECT_FALSE(t.update(true, 1000 + kDoubleTapMs + 1)); // too slow: only a new first press
    DoubleTap held;
    EXPECT_FALSE(held.update(true, 0));
    EXPECT_FALSE(held.update(true, 50)); // holding the key is not a second press
    EXPECT_FALSE(held.update(true, 100));
}

TEST(EldenRingFlight, DoubleTapResetForgetsThePair) {
    DoubleTap t;
    t.update(true, 1000);
    t.update(false, 1010);
    t.reset();
    EXPECT_FALSE(t.update(true, 1100));
}

TEST(EldenRingFlight, HeadingIgnoresPitchAndRejectsStraightUpDown) {
    Heading h;
    const float up_look[3] = {0.f, 0.9f, 0.436f}, right[3] = {1.f, 0.f, 0.f}; // looking steeply up, still facing +z
    ASSERT_TRUE(headingFromCamera(up_look, right, h));
    EXPECT_NEAR(h.fwd[0], 0.f, 1e-5f);
    EXPECT_NEAR(h.fwd[1], 1.f, 1e-5f);
    const float straight_up[3] = {0.f, 1.f, 0.f};
    EXPECT_FALSE(headingFromCamera(straight_up, right, h));
}

TEST(EldenRingFlight, ForwardMovesAlongTheHeadingAtFlySpeed) {
    float pos[3] = {0.f, 10.f, 0.f};
    Keys k;
    k.forward = true;
    EXPECT_TRUE(step(pos, k, northHeading(), 0.02f));
    EXPECT_NEAR(pos[2], kHorizontalSpeed * 0.02f, 1e-5f);
    EXPECT_NEAR(pos[0], 0.f, 1e-6f);
    EXPECT_FLOAT_EQ(pos[1], 10.f);
}

TEST(EldenRingFlight, DiagonalIsNotFasterThanStraight) {
    float pos[3] = {0.f, 0.f, 0.f};
    Keys k;
    k.forward = k.right = true;
    step(pos, k, northHeading(), 0.02f);
    EXPECT_NEAR(std::sqrt(pos[0] * pos[0] + pos[2] * pos[2]), kHorizontalSpeed * 0.02f, 1e-5f);
}

TEST(EldenRingFlight, OppositeKeysCancelAndVerticalIsIndependent) {
    float pos[3] = {1.f, 1.f, 1.f};
    Keys k;
    k.forward = k.back = true;
    EXPECT_FALSE(step(pos, k, northHeading(), 0.02f));
    EXPECT_FLOAT_EQ(pos[2], 1.f);
    k.up = true;
    EXPECT_TRUE(step(pos, k, northHeading(), 0.02f));
    EXPECT_NEAR(pos[1], 1.f + kVerticalSpeed * 0.02f, 1e-5f);
    k.up = false;
    k.down = true;
    step(pos, k, northHeading(), 0.02f);
    EXPECT_NEAR(pos[1], 1.f, 1e-5f);
}

TEST(EldenRingFlight, VerticalAddsToHorizontalWithoutSlowingIt) {
    float pos[3] = {0.f, 0.f, 0.f};
    Keys k;
    k.forward = k.up = true;
    step(pos, k, northHeading(), 0.02f);
    EXPECT_NEAR(pos[2], kHorizontalSpeed * 0.02f, 1e-5f);
    EXPECT_NEAR(pos[1], kVerticalSpeed * 0.02f, 1e-5f);
}

TEST(EldenRingFlight, ALongHitchIsClampedAndZeroOrNegativeTimeDoesNothing) {
    float pos[3] = {0.f, 0.f, 0.f};
    Keys k;
    k.forward = true;
    step(pos, k, northHeading(), 5.f); // 5 s hitch
    EXPECT_NEAR(pos[2], kHorizontalSpeed * kMaxStep, 1e-5f);
    float still[3] = {0.f, 0.f, 0.f};
    EXPECT_FALSE(step(still, k, northHeading(), 0.f));
    EXPECT_FALSE(step(still, k, northHeading(), -0.1f));
    EXPECT_FLOAT_EQ(still[2], 0.f);
}

TEST(EldenRingFlight, TheGameDoesNotSeeMovementJumpOrCrouchKeys) {
    for (int dik : {0x11, 0x1E, 0x1F, 0x20, 0x39, 0x2A}) {
        EXPECT_NE(std::find(std::begin(kHiddenKeys), std::end(kHiddenKeys), dik), std::end(kHiddenKeys)) << dik;
    }
}
