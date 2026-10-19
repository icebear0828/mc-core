#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "cs_ballistics.hpp"

namespace {

using eldenring::cs::CsShotParams;
using eldenring::cs::CsShotRng;
using eldenring::cs::CsShotTrace;
using eldenring::live::EnemyInfo;

EnemyInfo MakeEnemy(float x, float y, float z, int hp = 100, bool hostile = true) {
    EnemyInfo e;
    e.chr = 0x1000u;
    e.npc_id = 1000;
    e.hp = hp;
    e.max_hp = 100;
    e.rel_x = x;
    e.rel_y = y;
    e.rel_z = z;
    e.hostile = hostile;
    return e;
}

float Length(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

} // namespace

TEST(CsBallisticsTest, ZeroSpreadKeepsForwardDirection) {
    const float forward[3] = {0.f, 0.f, 1.f};
    float out[3];
    eldenring::cs::spreadDirection(forward, 0.f, 0.7f, 0.2f, out);
    EXPECT_FLOAT_EQ(out[0], 0.f);
    EXPECT_FLOAT_EQ(out[1], 0.f);
    EXPECT_FLOAT_EQ(out[2], 1.f);
}

TEST(CsBallisticsTest, SpreadStaysInsideTheConeAndStaysUnitLength) {
    const float forward[3] = {0.f, 0.f, 1.f};
    const float spread = 1.5f;
    const float cos_limit = std::cos(spread * eldenring::cs::kCsPi / 180.f);
    CsShotRng rng(1234u);
    for (int i = 0; i < 2000; ++i) {
        float out[3];
        eldenring::cs::spreadDirection(forward, spread, rng.next01(), rng.next01(), out);
        EXPECT_NEAR(Length(out), 1.f, 1e-4f);
        const float dot = out[0] * forward[0] + out[1] * forward[1] + out[2] * forward[2];
        EXPECT_GE(dot, cos_limit - 1e-5f);
    }
}

TEST(CsBallisticsTest, SpreadBasisDoesNotDegenerateLookingStraightUp) {
    const float forward[3] = {0.f, 1.f, 0.f};
    float out[3];
    eldenring::cs::spreadDirection(forward, 2.f, 0.5f, 0.5f, out);
    EXPECT_TRUE(std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]));
    EXPECT_NEAR(Length(out), 1.f, 1e-4f);
}

TEST(CsBallisticsTest, RngIsDeterministicAndAdvances) {
    CsShotRng a(42u), b(42u);
    EXPECT_FLOAT_EQ(a.next01(), b.next01());
    EXPECT_FLOAT_EQ(a.next01(), b.next01());
    CsShotRng c(42u);
    const float first = c.next01();
    const float second = c.next01();
    EXPECT_NE(first, second);
}

TEST(CsBallisticsTest, TracePicksTheNearestHostileOnTheRay) {
    std::vector<EnemyInfo> enemies = {
        MakeEnemy(0.f, 0.f, 20.f),
        MakeEnemy(0.f, 0.f, 10.f),
        MakeEnemy(0.f, 0.f, 5.f, 100, false), // neutral: not shot
    };
    const float cam_rel[3] = {0.f, 1.4f, 0.f};
    const float dir[3] = {0.f, 0.f, 1.f};
    const CsShotTrace t = eldenring::cs::traceTargets(cam_rel, dir, enemies, CsShotParams{});
    ASSERT_EQ(t.index, 1);
    EXPECT_NEAR(t.distance, 9.4f, 0.01f); // body radius 0.6 m
}

TEST(CsBallisticsTest, TraceCoversTheHeadNotJustTheTorso) {
    std::vector<EnemyInfo> enemies = {MakeEnemy(0.f, 0.f, 10.f)};
    const float cam_rel[3] = {0.f, 1.4f, 0.f};

    // Eye at 1.4 m, aim at a 2.0 m head: the old 1.0 m torso sphere missed this.
    float head[3] = {0.f, 2.0f - 1.4f, 10.f};
    eldenring::cs::normalize3(head);
    EXPECT_EQ(eldenring::cs::traceTargets(cam_rel, head, enemies, CsShotParams{}).index, 0);

    // Well above the 2.2 m body top: still a miss.
    float high[3] = {0.f, 3.0f - 1.4f, 10.f};
    eldenring::cs::normalize3(high);
    EXPECT_EQ(eldenring::cs::traceTargets(cam_rel, high, enemies, CsShotParams{}).index, -1);
}

TEST(CsBallisticsTest, TraceIgnoresDeadOffRayAndOutOfRangeTargets) {
    std::vector<EnemyInfo> enemies = {
        MakeEnemy(0.f, 0.f, 10.f, 0),      // dead
        MakeEnemy(3.f, 0.f, 10.f),         // off the ray
        MakeEnemy(0.f, 0.f, 400.f),        // beyond max range
    };
    const float cam_rel[3] = {0.f, 1.4f, 0.f};
    const float dir[3] = {0.f, 0.f, 1.f};
    CsShotParams p;
    p.max_range_m = 300.f;
    const CsShotTrace t = eldenring::cs::traceTargets(cam_rel, dir, enemies, p);
    EXPECT_EQ(t.index, -1);
}

TEST(CsBallisticsTest, TraceHonoursTheTorsoSphereRadius) {
    std::vector<EnemyInfo> enemies = {MakeEnemy(0.9f, 0.f, 10.f)};
    const float cam_rel[3] = {0.f, 1.4f, 0.f};
    const float dir[3] = {0.f, 0.f, 1.f};

    CsShotParams wide;
    wide.hit_radius_m = 1.0f;
    EXPECT_EQ(eldenring::cs::traceTargets(cam_rel, dir, enemies, wide).index, 0);

    CsShotParams tight;
    tight.hit_radius_m = 0.5f;
    EXPECT_EQ(eldenring::cs::traceTargets(cam_rel, dir, enemies, tight).index, -1);
}

TEST(CsBallisticsTest, DamageFalloffIsFullInsideTheStartAndMinimumAtMaxRange) {
    CsShotParams p;
    p.max_range_m = 100.f;
    p.falloff_start = 0.5f;
    p.min_damage_scale = 0.5f;
    EXPECT_FLOAT_EQ(eldenring::cs::damageScaleAtRange(0.f, p), 1.f);
    EXPECT_FLOAT_EQ(eldenring::cs::damageScaleAtRange(50.f, p), 1.f);
    EXPECT_FLOAT_EQ(eldenring::cs::damageScaleAtRange(100.f, p), 0.5f);
    EXPECT_FLOAT_EQ(eldenring::cs::damageScaleAtRange(75.f, p), 0.75f);
    // Monotonic decrease after the falloff point.
    EXPECT_LT(eldenring::cs::damageScaleAtRange(80.f, p), eldenring::cs::damageScaleAtRange(70.f, p));
}

TEST(CsBallisticsTest, DamageToErUsesTheSameConversionAsTheMcBow) {
    CsShotParams p;
    p.damage = 36.f;
    // 219 HP * 5% * 36/7 = 56.31 -> 56, exactly boltDamageEr(219, 36).
    EXPECT_EQ(eldenring::cs::damageToEr(219, 36.f, 0.f, p), 56);
    EXPECT_EQ(eldenring::cs::damageToEr(219, 36.f, 0.f, p), eldenring::bullet::boltDamageEr(219, 36.f));
}

TEST(CsBallisticsTest, DamageToErAppliesRangeFalloff) {
    CsShotParams p;
    p.damage = 36.f;
    p.max_range_m = 100.f;
    p.falloff_start = 0.5f;
    p.min_damage_scale = 0.5f;
    const int near_damage = eldenring::cs::damageToEr(219, 36.f, 10.f, p);
    const int far_damage = eldenring::cs::damageToEr(219, 36.f, 100.f, p);
    EXPECT_GT(near_damage, far_damage);
    EXPECT_EQ(far_damage, eldenring::bullet::boltDamageEr(219, 18.f));
}

TEST(CsBallisticsTest, TracerFrequencyMatchesTheCs2WeaponData) {
    // AK-47/M4A1: m_nTracerFrequency = 3, AWP: 1, 0 disables tracers.
    EXPECT_FALSE(eldenring::cs::shouldSpawnTracer(0u, 3u));
    EXPECT_FALSE(eldenring::cs::shouldSpawnTracer(1u, 3u));
    EXPECT_TRUE(eldenring::cs::shouldSpawnTracer(2u, 3u));
    EXPECT_TRUE(eldenring::cs::shouldSpawnTracer(3u, 1u));
    EXPECT_FALSE(eldenring::cs::shouldSpawnTracer(0u, 0u));
}

TEST(CsBallisticsTest, TracerLifetimeScalesWithDistanceAndIsClamped) {
    EXPECT_FLOAT_EQ(eldenring::cs::tracerLifetime(0.f), eldenring::cs::kCsTracerMinLifeS);
    EXPECT_FLOAT_EQ(eldenring::cs::tracerLifetime(8.f), 0.02f);   // 8 m / 400 m/s
    EXPECT_FLOAT_EQ(eldenring::cs::tracerLifetime(40.f), 0.10f);  // 40 m / 400 m/s
    EXPECT_FLOAT_EQ(eldenring::cs::tracerLifetime(1000.f), eldenring::cs::kCsTracerMaxLifeS);
}

TEST(CsBallisticsTest, DamageToErRejectsInvalidInput) {
    CsShotParams p;
    EXPECT_EQ(eldenring::cs::damageToEr(0, 36.f, 0.f, p), 0);
    EXPECT_EQ(eldenring::cs::damageToEr(219, 0.f, 0.f, p), 0);
    EXPECT_EQ(eldenring::cs::damageToEr(-5, 36.f, 0.f, p), 0);
}
