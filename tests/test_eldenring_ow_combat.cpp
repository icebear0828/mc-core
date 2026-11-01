#include <gtest/gtest.h>

#include "eldenring_ow_combat.hpp"

#include <vector>

using eldenring::live::EnemyInfo;
using eldenring::ow::OwStrikeKind;
using eldenring::ow::OwStrikeParams;
using eldenring::ow::collectOwAoeHits;
using eldenring::ow::owStrikeParamsFor;
using eldenring::ow::traceOwStrike;

namespace {

EnemyInfo makeEnemy(float x, float y, float z, int hp = 219, bool hostile = true) {
    EnemyInfo e{};
    e.chr = static_cast<uintptr_t>(0x1000 + static_cast<int>(x * 10.f));
    e.rel_x = x;
    e.rel_y = y;
    e.rel_z = z;
    e.hp = hp;
    e.max_hp = hp;
    e.hostile = hostile;
    return e;
}

} // namespace

TEST(OwCombatTest, StrikeParamsMatchTheAuthoredAbilities) {
    const OwStrikeParams cannon = owStrikeParamsFor(OwStrikeKind::HandCannon);
    EXPECT_GT(cannon.shot.max_range_m, 10.f);
    EXPECT_GT(cannon.shot.damage, 0.f);
    EXPECT_EQ(cannon.aoe_radius_m, 0.f);

    const OwStrikeParams punch = owStrikeParamsFor(OwStrikeKind::RocketPunch);
    EXPECT_LT(punch.shot.max_range_m, cannon.shot.max_range_m);
    EXPECT_GT(punch.shot.damage, cannon.shot.damage);

    const OwStrikeParams uppercut = owStrikeParamsFor(OwStrikeKind::Uppercut);
    EXPECT_LT(uppercut.shot.damage, punch.shot.damage);

    const OwStrikeParams slam = owStrikeParamsFor(OwStrikeKind::Slam);
    EXPECT_GT(slam.aoe_radius_m, 0.f);
    EXPECT_GT(slam.shot.damage, 0.f);
}

TEST(OwCombatTest, HandCannonHitsTheHostileUnderTheCrosshair) {
    const std::vector<EnemyInfo> enemies{makeEnemy(0.f, 0.f, 8.f)};
    const float cam_rel[3] = {0.f, 1.6f, 0.f};
    const float dir[3] = {0.f, 0.f, 1.f};
    const auto hit = traceOwStrike(cam_rel, dir, enemies, owStrikeParamsFor(OwStrikeKind::HandCannon));
    EXPECT_EQ(hit.index, 0);
    // The ray enters the 0.7 m body cylinder at 8 - 0.7 = 7.3 m.
    EXPECT_NEAR(hit.distance, 7.3f, 0.05f);
    EXPECT_GT(hit.er_damage, 0);
}

TEST(OwCombatTest, StrikeIgnoresTargetsBehindOutOfRangeAndNeutral) {
    const float cam_rel[3] = {0.f, 1.6f, 0.f};
    const float dir[3] = {0.f, 0.f, 1.f};
    const OwStrikeParams punch = owStrikeParamsFor(OwStrikeKind::RocketPunch);

    const std::vector<EnemyInfo> behind{makeEnemy(0.f, 0.f, -3.f)};
    EXPECT_EQ(traceOwStrike(cam_rel, dir, behind, punch).index, -1);

    const std::vector<EnemyInfo> out_of_range{makeEnemy(0.f, 0.f, 20.f)};
    EXPECT_EQ(traceOwStrike(cam_rel, dir, out_of_range, punch).index, -1);

    const std::vector<EnemyInfo> neutral{makeEnemy(0.f, 0.f, 3.f, 219, false)};
    EXPECT_EQ(traceOwStrike(cam_rel, dir, neutral, punch).index, -1);
}

TEST(OwCombatTest, RocketPunchHitsAtMeleeRange) {
    const std::vector<EnemyInfo> enemies{makeEnemy(0.f, 0.f, 3.f)};
    const float cam_rel[3] = {0.f, 1.6f, 0.f};
    const float dir[3] = {0.f, 0.f, 1.f};
    const auto hit = traceOwStrike(cam_rel, dir, enemies, owStrikeParamsFor(OwStrikeKind::RocketPunch));
    EXPECT_EQ(hit.index, 0);
    EXPECT_GT(hit.er_damage, 0);
}

TEST(OwCombatTest, SlamHitsEveryHostileInsideTheRadiusWithFalloff) {
    const std::vector<EnemyInfo> enemies{
        makeEnemy(1.f, 0.f, 1.f),    // ~1.4 m, near the centre
        makeEnemy(5.f, 0.f, 0.f),    // 5 m, inside the 7 m radius
        makeEnemy(12.f, 0.f, 0.f),   // outside
        makeEnemy(2.f, 0.f, 2.f, 219, false), // neutral
    };
    const auto hits = collectOwAoeHits(enemies, owStrikeParamsFor(OwStrikeKind::Slam));
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].index, 0);
    EXPECT_EQ(hits[1].index, 1);
    EXPECT_GT(hits[0].er_damage, hits[1].er_damage); // falloff with distance
}

TEST(OwCombatTest, DamageConversionScalesWithVictimHealth) {
    const OwStrikeParams punch = owStrikeParamsFor(OwStrikeKind::RocketPunch);
    const std::vector<EnemyInfo> weak{makeEnemy(0.f, 0.f, 3.f, 100)};
    const std::vector<EnemyInfo> strong{makeEnemy(0.f, 0.f, 3.f, 1000)};
    const float cam_rel[3] = {0.f, 1.6f, 0.f};
    const float dir[3] = {0.f, 0.f, 1.f};
    const int weak_damage = traceOwStrike(cam_rel, dir, weak, punch).er_damage;
    const int strong_damage = traceOwStrike(cam_rel, dir, strong, punch).er_damage;
    EXPECT_GT(weak_damage, 0);
    EXPECT_GT(strong_damage, weak_damage);
}
