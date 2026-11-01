#include <gtest/gtest.h>

#include "mc/trident.hpp"

using mc::TridentController;

namespace {

// One tick per call, so the tick counts in the tests read like Minecraft's.
constexpr float kDt = 0.05f;

// `frames` updates with the button held, after the fresh press that starts the charge.
void hold(TridentController& t, int frames) {
    for (int i = 0; i < frames; ++i) t.update(kDt, true, true);
}

} // namespace

TEST(TridentControllerTest, AFreshPressStartsTheCharge) {
    TridentController t;
    const mc::TridentUpdate u = t.update(kDt, true, true);
    EXPECT_TRUE(u.started);
    EXPECT_TRUE(t.charging());
    EXPECT_FLOAT_EQ(t.power(), 0.f);
}

TEST(TridentControllerTest, ReleasingBeforeTenTicksCancelsTheThrow) {
    TridentController t;
    t.update(kDt, true, true);
    hold(t, 8); // 9 ticks
    const mc::TridentUpdate u = t.update(kDt, false, true);
    EXPECT_TRUE(u.cancelled);
    EXPECT_FALSE(u.released);
    EXPECT_FALSE(t.charging());
}

TEST(TridentControllerTest, ReleasingAtTenTicksThrows) {
    TridentController t;
    t.update(kDt, true, true);
    hold(t, 9); // 10 ticks
    const mc::TridentUpdate u = t.update(kDt, false, true);
    EXPECT_TRUE(u.released);
    EXPECT_FALSE(u.cancelled);
    EXPECT_NEAR(u.power, mc::powerForTicks(mc::kTridentMinTicks), 1e-5f);
}

TEST(TridentControllerTest, AFullChargeReachesFullPower) {
    TridentController t;
    t.update(kDt, true, true);
    hold(t, 24); // 25 ticks
    EXPECT_FLOAT_EQ(t.power(), 1.f);
    const mc::TridentUpdate u = t.update(kDt, false, true);
    EXPECT_TRUE(u.released);
    EXPECT_FLOAT_EQ(u.power, 1.f);
}

TEST(TridentControllerTest, LosingTheTridentCancelsTheCharge) {
    TridentController t;
    t.update(kDt, true, true);
    hold(t, 5);
    const mc::TridentUpdate u = t.update(kDt, true, false); // the slot changed: no trident any more
    EXPECT_TRUE(u.cancelled);
    EXPECT_FALSE(u.released);
    EXPECT_FALSE(t.charging());
}

TEST(TridentControllerTest, AButtonHeldFromBeforeTheTridentIsEquippedDoesNotCharge) {
    TridentController t;
    t.update(kDt, true, false); // right button already down, nothing usable in the hand
    const mc::TridentUpdate u = t.update(kDt, true, true); // trident equipped, button still down
    EXPECT_FALSE(u.started);
    EXPECT_FALSE(t.charging());
}

TEST(TridentControllerTest, ReleasingWithoutChargingDoesNothing) {
    TridentController t;
    const mc::TridentUpdate u = t.update(kDt, false, true);
    EXPECT_FALSE(u.released);
    EXPECT_FALSE(u.cancelled);
}
