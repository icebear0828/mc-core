#include <gtest/gtest.h>
#include "eldenring_creeper.hpp"
#include "eldenring_mobs.hpp"

using namespace eldenring::creeper;

TEST(EldenRingCreeper, CreeperStartsIdleWithFullFuse) {
    CreeperManager mgr;
    CreeperTarget summon;
    summon.id = 0x1234;
    summon.feet[0] = 10.0f;
    summon.feet[1] = 0.0f;
    summon.feet[2] = 20.0f;
    summon.hp = 100;
    summon.max_hp = 100;
    summon.is_creeper = true;

    std::vector<CreeperTarget> summons = {summon};
    std::vector<CreeperEnemyTarget> enemies; // no enemies

    auto events = mgr.update(0.05f, summons, enemies);
    EXPECT_TRUE(events.empty());
    EXPECT_FALSE(mgr.isPrimed(0x1234));
    EXPECT_FLOAT_EQ(mgr.getSwell(0x1234), 0.0f);
    EXPECT_FLOAT_EQ(mgr.getScale(0x1234), 1.0f);
}

TEST(EldenRingCreeper, ProximityToHostileEnemyStartsFuseAndHiss) {
    CreeperManager mgr;
    CreeperTarget summon;
    summon.id = 0x1000;
    summon.feet[0] = 0.0f;
    summon.feet[1] = 0.0f;
    summon.feet[2] = 0.0f;
    summon.hp = 100;
    summon.max_hp = 100;
    summon.is_creeper = true;

    CreeperEnemyTarget enemy;
    enemy.chr = 0x2000;
    enemy.feet[0] = 2.0f; // within 3.5m ignite range
    enemy.feet[1] = 0.0f;
    enemy.feet[2] = 0.0f;
    enemy.hostile = true;
    enemy.hp = 500;

    std::vector<CreeperTarget> summons = {summon};
    std::vector<CreeperEnemyTarget> enemies = {enemy};

    auto events = mgr.update(0.05f, summons, enemies);
    EXPECT_TRUE(mgr.isPrimed(0x1000));
    EXPECT_GT(mgr.getSwell(0x1000), 0.0f);
    EXPECT_GT(mgr.getScale(0x1000), 1.0f);
    // Should produce a Hiss event
    bool found_hiss = false;
    for (const auto& ev : events) {
        if (ev.kind == CreeperEventKind::Hiss && ev.creeper_id == 0x1000) {
            found_hiss = true;
        }
    }
    EXPECT_TRUE(found_hiss);
}

TEST(EldenRingCreeper, EnemyMovingAwayCancelsFuseIfNotManual) {
    CreeperManager mgr;
    CreeperTarget summon;
    summon.id = 0x1000;
    summon.feet[0] = 0.0f;
    summon.feet[1] = 0.0f;
    summon.feet[2] = 0.0f;
    summon.hp = 100;
    summon.max_hp = 100;
    summon.is_creeper = true;

    CreeperEnemyTarget enemy;
    enemy.chr = 0x2000;
    enemy.feet[0] = 2.0f;
    enemy.feet[1] = 0.0f;
    enemy.feet[2] = 0.0f;
    enemy.hostile = true;
    enemy.hp = 500;

    mgr.update(0.1f, {summon}, {enemy});
    EXPECT_TRUE(mgr.isPrimed(0x1000));

    // Enemy flees to 15m away (beyond defuse threshold 7m)
    enemy.feet[0] = 15.0f;
    mgr.update(0.1f, {summon}, {enemy});
    EXPECT_FALSE(mgr.isPrimed(0x1000));
    EXPECT_FLOAT_EQ(mgr.getSwell(0x1000), 0.0f);
}

TEST(EldenRingCreeper, FlintAndSteelManualIgnitionForcesDetonation) {
    CreeperManager mgr;
    CreeperTarget summon;
    summon.id = 0x1000;
    summon.feet[0] = 0.0f;
    summon.feet[1] = 0.0f;
    summon.feet[2] = 0.0f;
    summon.hp = 100;
    summon.max_hp = 100;
    summon.is_creeper = true;

    // Manually ignite with Flint and Steel
    bool ignited = mgr.ignite(0x1000);
    EXPECT_TRUE(ignited);
    EXPECT_TRUE(mgr.isPrimed(0x1000));

    // Even with zero enemies, it remains primed and counts down
    std::vector<CreeperEnemyTarget> no_enemies;
    mgr.update(0.5f, {summon}, no_enemies);
    EXPECT_TRUE(mgr.isPrimed(0x1000));
    EXPECT_GT(mgr.getSwell(0x1000), 0.2f);
}

TEST(EldenRingCreeper, DetonationAtEndOfFuseYieldsExplosionAndKillsCreeper) {
    CreeperManager mgr;
    CreeperTarget summon;
    summon.id = 0x1000;
    summon.feet[0] = 5.0f;
    summon.feet[1] = 1.0f;
    summon.feet[2] = 5.0f;
    summon.hp = 100;
    summon.max_hp = 100;
    summon.is_creeper = true;

    mgr.ignite(0x1000); // 1.5s fuse

    // Advance 1.4s -> still not exploded
    auto ev1 = mgr.update(1.4f, {summon}, {});
    bool exploded_early = false;
    for (const auto& ev : ev1) {
        if (ev.kind == CreeperEventKind::Explosion) exploded_early = true;
    }
    EXPECT_FALSE(exploded_early);

    // Advance another 0.15s (total 1.55s >= 1.5s) -> Detonation!
    auto ev2 = mgr.update(0.15f, {summon}, {});
    const CreeperEvent* exp = nullptr;
    for (const auto& ev : ev2) {
        if (ev.kind == CreeperEventKind::Explosion) exp = &ev;
    }
    ASSERT_NE(exp, nullptr);
    EXPECT_EQ(exp->creeper_id, 0x1000);
    EXPECT_NEAR(exp->pos[0], 5.0f, 1e-3f);
    EXPECT_NEAR(exp->pos[1], 1.0f, 1e-3f);
    EXPECT_NEAR(exp->pos[2], 5.0f, 1e-3f);
    EXPECT_GT(exp->radius, 5.0f);
    EXPECT_GT(exp->damage, 1000.0f);
    EXPECT_TRUE(exp->kill_creeper);
}

TEST(EldenRingCreeper, DeathBeforeFuseExpiryCancelsExplosion) {
    CreeperManager mgr;
    CreeperTarget summon;
    summon.id = 0x1000;
    summon.feet[0] = 0.0f;
    summon.feet[1] = 0.0f;
    summon.feet[2] = 0.0f;
    summon.hp = 100;
    summon.max_hp = 100;
    summon.is_creeper = true;

    mgr.ignite(0x1000);
    mgr.update(0.5f, {summon}, {});
    EXPECT_TRUE(mgr.isPrimed(0x1000));

    // Creeper killed by enemy (hp drops to 0)
    summon.hp = 0;
    auto ev = mgr.update(1.5f, {summon}, {});
    bool exploded = false;
    for (const auto& e : ev) {
        if (e.kind == CreeperEventKind::Explosion) exploded = true;
    }
    EXPECT_FALSE(exploded);
    EXPECT_FALSE(mgr.isPrimed(0x1000));
}

TEST(EldenRingCreeper, WhiteFlashTogglesPeriodicallyDuringFuse) {
    CreeperManager mgr;
    CreeperTarget summon;
    summon.id = 0x1000;
    summon.hp = 100;
    summon.max_hp = 100;
    summon.is_creeper = true;

    mgr.ignite(0x1000);
    // Period is 0.2s: white flash is active for half the period
    mgr.update(0.05f, {summon}, {});
    bool f1 = mgr.isWhiteFlash(0x1000);
    mgr.update(0.10f, {summon}, {});
    bool f2 = mgr.isWhiteFlash(0x1000);
    EXPECT_NE(f1, f2);
}
