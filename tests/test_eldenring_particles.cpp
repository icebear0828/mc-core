#include <gtest/gtest.h>

#include "eldenring_particles.hpp"

using namespace eldenring::fx;

TEST(EldenRingParticles, ACritBurstHasSixteenStarsAndDiesOut) {
    ParticleSystem ps;
    Rng rng(7);
    const float at[3] = {1.f, 2.f, 3.f};
    ps.spawnCrit(at, rng);
    ASSERT_EQ(ps.alive().size(), 16u);
    for (const Particle& p : ps.alive()) {
        EXPECT_EQ(p.kind, Kind::Crit);
        EXPECT_GE(p.life, 0.25f);
        EXPECT_LE(p.life, 0.45f);
    }
    ps.update(0.1f);
    EXPECT_EQ(ps.alive().size(), 16u);
    ps.update(0.5f);
    EXPECT_TRUE(ps.alive().empty());
}

TEST(EldenRingParticles, CritStarsFlyOutwardFromTheTarget) {
    ParticleSystem ps;
    Rng rng(3);
    const float at[3] = {0.f, 0.f, 0.f};
    ps.spawnCrit(at, rng);
    ps.update(0.1f);
    for (const Particle& p : ps.alive()) {
        const float d = std::sqrt(p.pos[0] * p.pos[0] + p.pos[1] * p.pos[1] + p.pos[2] * p.pos[2]);
        EXPECT_GT(d, 0.05f);
        EXPECT_LT(d, 0.6f);
    }
}

TEST(EldenRingParticles, DamageHeartsRiseAndFade) {
    ParticleSystem ps;
    Rng rng(5);
    const float at[3] = {0.f, 1.f, 0.f};
    ps.spawnDamage(at, 3, rng);
    ASSERT_EQ(ps.alive().size(), 3u);
    const float y0 = ps.alive()[0].pos[1];
    ps.update(0.3f);
    EXPECT_GT(ps.alive()[0].pos[1], y0 + 0.2f);
    EXPECT_LT(ParticleSystem::fade(ps.alive()[0]), 1.f);
    ps.update(2.f);
    EXPECT_TRUE(ps.alive().empty());
    ParticleSystem none;
    none.spawnDamage(at, 0, rng);
    EXPECT_TRUE(none.alive().empty());
}

TEST(EldenRingParticles, SweepRunsThroughEightFramesInFourTenthsOfASecond) {
    ParticleSystem ps;
    const float at[3] = {0.f, 1.f, 0.f};
    ps.spawnSweep(at);
    ASSERT_EQ(ps.alive().size(), 1u);
    EXPECT_EQ(ps.alive()[0].frame, 0);
    ps.update(0.21f);
    EXPECT_EQ(ps.alive()[0].frame, 4);
    ps.update(0.18f);
    EXPECT_EQ(ps.alive()[0].frame, 7);
    ps.update(0.05f);
    EXPECT_TRUE(ps.alive().empty());
}

TEST(EldenRingParticles, ThePoolIsBounded) {
    ParticleSystem ps;
    Rng rng(1);
    const float at[3] = {};
    for (int i = 0; i < 100; ++i) ps.spawnCrit(at, rng);
    EXPECT_LE(ps.alive().size(), ParticleSystem::kMax);
}

TEST(EldenRingFx, HurtTiltPeaksEarlyAndVanishes) {
    EXPECT_FLOAT_EQ(hurtTiltRadians(0.f, 1.f), 0.f);
    EXPECT_NEAR(hurtTiltRadians(1.f, 1.f), 0.f, 1e-6f);        // sin(pi) at the very start is 0: the tilt builds up
    EXPECT_NEAR(hurtTiltRadians(0.8409f, 1.f), 14.f * 0.01745329252f, 1e-3f); // t^4 = 0.5: the peak
    EXPECT_NEAR(hurtTiltRadians(0.8409f, -1.f), -14.f * 0.01745329252f, 1e-3f);
    EXPECT_FLOAT_EQ(hurtTiltRadians(-1.f, 1.f), 0.f);
}
