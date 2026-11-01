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

TEST(EldenRingParticles, SparkBurstIsShortLivedAndFalls) {
    ParticleSystem ps;
    Rng rng(11);
    const float at[3] = {4.f, 2.f, 1.f};
    ps.spawnSpark(at, rng);
    ASSERT_EQ(ps.alive().size(), 5u);
    for (const Particle& p : ps.alive()) {
        EXPECT_EQ(p.kind, Kind::Spark);
        EXPECT_GE(p.life, 0.08f);
        EXPECT_LE(p.life, 0.18f);
    }
    ps.update(0.19f);
    EXPECT_TRUE(ps.alive().empty());
}

TEST(EldenRingParticles, DustBurstDriftsUpAndFades) {
    ParticleSystem ps;
    Rng rng(13);
    const float at[3] = {1.f, 0.f, 1.f};
    ps.spawnDust(at, rng);
    ASSERT_EQ(ps.alive().size(), 6u);
    for (const Particle& p : ps.alive()) {
        EXPECT_EQ(p.kind, Kind::Dust);
        EXPECT_GE(p.life, 0.35f);
        EXPECT_LE(p.life, 0.7f);
    }
    const float y0 = ps.alive()[0].pos[1];
    ps.update(0.2f);
    EXPECT_GT(ps.alive()[0].pos[1], y0 + 0.02f);
    EXPECT_LT(ParticleSystem::fade(ps.alive()[0]), 1.f);
    ps.update(1.0f);
    EXPECT_TRUE(ps.alive().empty());
}

TEST(EldenRingParticles, ThePoolIsBounded) {
    ParticleSystem ps;
    Rng rng(1);
    const float at[3] = {};
    for (int i = 0; i < 100; ++i) ps.spawnCrit(at, rng);
    EXPECT_LE(ps.alive().size(), ParticleSystem::kMax);
}

TEST(EldenRingParticles, FireworkSparksStreamBehindTheRocketAndFall) {
    ParticleSystem ps;
    Rng rng(21);
    const float at[3] = {1.f, 2.f, 3.f};
    const float dir[3] = {0.f, 0.f, 1.f}; // the rocket travels towards +Z
    ps.spawnFirework(at, dir, rng);
    ASSERT_EQ(ps.alive().size(), 10u);
    for (const Particle& p : ps.alive()) {
        EXPECT_EQ(p.kind, Kind::Firework);
        EXPECT_GE(p.life, 0.35f);
        EXPECT_LE(p.life, 0.7f);
        EXPECT_GE(p.size, 0.06f);
        EXPECT_LE(p.size, 0.12f);
        // the trail is emitted opposite the direction of travel
        const float along = p.vel[0] * dir[0] + p.vel[1] * dir[1] + p.vel[2] * dir[2];
        EXPECT_LT(along, 0.f);
    }
    const float vy0 = ps.alive()[0].vel[1];
    ps.update(0.1f);
    EXPECT_LT(ps.alive()[0].vel[1], vy0); // gravity
    ps.update(1.0f);
    EXPECT_TRUE(ps.alive().empty());
}

TEST(EldenRingParticles, FireworkWithoutADirectionStillSpawnsUpwardSparks) {
    ParticleSystem ps;
    Rng rng(22);
    const float at[3] = {0.f, 1.f, 0.f};
    const float zero[3] = {0.f, 0.f, 0.f};
    ps.spawnFirework(at, zero, rng);
    ASSERT_EQ(ps.alive().size(), 10u);
    for (const Particle& p : ps.alive()) {
        EXPECT_EQ(p.kind, Kind::Firework);
    }
}

TEST(EldenRingParticles, RiptideDropletsFormARingAroundThePlayer) {
    ParticleSystem ps;
    Rng rng(23);
    const float at[3] = {4.f, 1.f, -2.f};
    ps.spawnRiptide(at, 0.f, rng);
    ASSERT_EQ(ps.alive().size(), 8u);
    for (const Particle& p : ps.alive()) {
        EXPECT_EQ(p.kind, Kind::Riptide);
        EXPECT_GE(p.life, 0.3f);
        EXPECT_LE(p.life, 0.6f);
        EXPECT_GE(p.size, 0.08f);
        EXPECT_LE(p.size, 0.15f);
        const float dx = p.pos[0] - at[0];
        const float dz = p.pos[2] - at[2];
        const float radius = std::sqrt(dx * dx + dz * dz);
        EXPECT_NEAR(radius, 0.5f, 0.02f);
        // velocity is mostly tangential, not radial
        const float radial = (p.vel[0] * dx + p.vel[2] * dz) / radius;
        EXPECT_LT(std::abs(radial), 0.35f);
    }
    ps.update(0.7f);
    EXPECT_TRUE(ps.alive().empty());
}

TEST(EldenRingParticles, RiptidePhaseRotatesTheRing) {
    ParticleSystem a;
    ParticleSystem b;
    Rng ra(31);
    Rng rb(31); // same seed, only the phase differs
    const float at[3] = {0.f, 0.f, 0.f};
    a.spawnRiptide(at, 0.f, ra);
    b.spawnRiptide(at, 0.5f, rb);
    ASSERT_EQ(a.alive().size(), 8u);
    ASSERT_EQ(b.alive().size(), 8u);
    // half a turn: the first droplet moves to the opposite side of the ring
    EXPECT_NEAR(a.alive()[0].pos[0], -b.alive()[0].pos[0], 0.15f);
    EXPECT_NEAR(a.alive()[0].pos[2], -b.alive()[0].pos[2], 0.15f);
}

TEST(EldenRingParticles, ThePoolStillCapsWithTheNewKinds) {
    ParticleSystem ps;
    Rng rng(41);
    const float at[3] = {};
    const float dir[3] = {0.f, 1.f, 0.f};
    for (int i = 0; i < 100; ++i) {
        ps.spawnFirework(at, dir, rng);
        ps.spawnRiptide(at, static_cast<float>(i), rng);
    }
    EXPECT_LE(ps.alive().size(), ParticleSystem::kMax);
}

TEST(EldenRingFx, HurtTiltPeaksEarlyAndVanishes) {
    EXPECT_FLOAT_EQ(hurtTiltRadians(0.f, 1.f), 0.f);
    EXPECT_NEAR(hurtTiltRadians(1.f, 1.f), 0.f, 1e-6f);        // sin(pi) at the very start is 0: the tilt builds up
    EXPECT_NEAR(hurtTiltRadians(0.8409f, 1.f), 14.f * 0.01745329252f, 1e-3f); // t^4 = 0.5: the peak
    EXPECT_NEAR(hurtTiltRadians(0.8409f, -1.f), -14.f * 0.01745329252f, 1e-3f);
    EXPECT_FLOAT_EQ(hurtTiltRadians(-1.f, 1.f), 0.f);
}
