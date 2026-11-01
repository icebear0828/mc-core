#include <gtest/gtest.h>

#include "eldenring_ow_feel.hpp"

using namespace eldenring::ow;

TEST(EldenRingOwFeel, RecoilStartsAtTheConfiguredKickAndDecays) {
    OwFeelConfig cfg;
    cfg.recoil_kick = 0.05f;
    cfg.recoil_recover = 0.12f;

    EXPECT_NEAR(owRecoilOffset(0.f, cfg), -0.05f, 1e-5f);
    EXPECT_NEAR(owRecoilOffset(0.12f, cfg), -0.05f * std::exp(-1.f), 1e-5f);
    EXPECT_LT(std::fabs(owRecoilOffset(0.36f, cfg)), 0.003f);
    EXPECT_LT(std::fabs(owRecoilOffset(1.f, cfg)), 1e-4f);
}

TEST(EldenRingOwFeel, RecoilIsZeroBeforeTheShotAndForNonPositiveRecovery) {
    OwFeelConfig cfg;
    EXPECT_FLOAT_EQ(owRecoilOffset(-0.01f, cfg), 0.f);
    cfg.recoil_recover = 0.f;
    EXPECT_FLOAT_EQ(owRecoilOffset(0.f, cfg), 0.f);
}

TEST(EldenRingOwFeel, FovPulseMirrorsTheRecoilCurve) {
    OwFeelConfig cfg;
    cfg.fov_pulse = 2.f;
    cfg.recoil_recover = 0.1f;
    EXPECT_NEAR(owFovPulse(0.f, cfg), 2.f, 1e-5f);
    EXPECT_NEAR(owFovPulse(0.1f, cfg), 2.f * std::exp(-1.f), 1e-5f);
    EXPECT_NEAR(owFovPulse(0.5f, cfg), 2.f * std::exp(-5.f), 1e-5f);
}

TEST(EldenRingOwFeel, PunchTailTrimsTheAuthoredClip) {
    // Release at 2.25 s of a 3.57 s clip: 0.35 s tail ends at 2.60 s.
    EXPECT_NEAR(owTrimmedEndTime(2.25f, 0.35f, 3.567f), 2.60f, 1e-4f);
    // A tail longer than the clip clamps to the clip duration.
    EXPECT_FLOAT_EQ(owTrimmedEndTime(2.25f, 5.f, 3.567f), 3.567f);
    // Without a loaded clip the dispatcher still gets a finite end.
    EXPECT_FLOAT_EQ(owTrimmedEndTime(2.25f, 0.35f, 0.f), 2.60f);
    // Negative tails never shorten below the release frame.
    EXPECT_FLOAT_EQ(owTrimmedEndTime(2.25f, -1.f, 3.567f), 2.25f);
}

TEST(EldenRingOwFeel, ReadyIsFalseWhileTheCooldownRuns) {
    EXPECT_TRUE(owReady(0.f));
    EXPECT_TRUE(owReady(-0.1f));
    EXPECT_FALSE(owReady(0.01f));
}
