#include <gtest/gtest.h>

#include "eldenring_virtual_summon.hpp"
#include "eldenring_mobs.hpp"

#include <cmath>
#include <vector>

using namespace eldenring::summon;

namespace {

PlayerPose makePlayer(float x = 0.0f, float y = 10.0f, float z = 0.0f, float yaw = 0.0f) {
    PlayerPose p;
    p.pos[0] = x;
    p.pos[1] = y;
    p.pos[2] = z;
    p.yaw = yaw;
    return p;
}

EnemyTarget makeEnemy(uintptr_t chr_ins, float x, float y, float z, int hp = 100, int max_hp = 100) {
    EnemyTarget e;
    e.chr_ins = chr_ins;
    e.pos[0] = x;
    e.pos[1] = y;
    e.pos[2] = z;
    e.hp = hp;
    e.max_hp = max_hp;
    e.is_hostile = true;
    return e;
}

} // namespace

TEST(VirtualSummonTest, SpawnCreatesEntityWithUniqueIdAndInitialState) {
    VirtualSummonManager mgr;
    EXPECT_EQ(mgr.activeCount(), 0u);

    const auto p = makePlayer(10.0f, 5.0f, 20.0f, 0.0f);
    uint64_t id1 = mgr.spawnMob("zombie", p, 2.5f);
    EXPECT_NE(id1, 0u);
    EXPECT_EQ(mgr.activeCount(), 1u);

    const auto* mob1 = mgr.findMob(id1);
    ASSERT_NE(mob1, nullptr);
    EXPECT_EQ(mob1->id, id1);
    EXPECT_EQ(mob1->model_type, "zombie");
    EXPECT_EQ(mob1->hp, 20);
    EXPECT_EQ(mob1->max_hp, 20);
    EXPECT_FALSE(mob1->is_dying);

    // Second mob gets unique ID
    uint64_t id2 = mgr.spawnMob("skeleton", p, 2.5f);
    EXPECT_NE(id2, 0u);
    EXPECT_NE(id1, id2);
    EXPECT_EQ(mgr.activeCount(), 2u);
}

TEST(VirtualSummonTest, CapacityClampedToPreventAbuse) {
    VirtualSummonManager mgr;
    const auto p = makePlayer();
    for (size_t i = 0; i < VirtualSummonManager::kMaxVirtualSummons; ++i) {
        EXPECT_NE(mgr.spawnMob("zombie", p, 2.0f), 0u);
    }
    EXPECT_EQ(mgr.activeCount(), VirtualSummonManager::kMaxVirtualSummons);

    // Exceeding capacity is cleanly rejected without throwing or crashing
    uint64_t rejected = mgr.spawnMob("zombie", p, 2.0f);
    EXPECT_EQ(rejected, 0u);
    EXPECT_EQ(mgr.activeCount(), VirtualSummonManager::kMaxVirtualSummons);
}

TEST(VirtualSummonTest, FollowsPlayerWhenBeyondIdleRadius) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    // Mob spawned at (0, 0, -8.0)
    uint64_t id = mgr.spawnMob("zombie", p, 8.0f);
    const auto* mob = mgr.findMob(id);
    ASSERT_NE(mob, nullptr);
    float init_dist = std::sqrt(mob->pos[0] * mob->pos[0] + mob->pos[2] * mob->pos[2]);
    EXPECT_NEAR(init_dist, 8.0f, 0.1f);

    // Advance 1 second with player remaining at (0, 0, 0)
    std::vector<EnemyTarget> no_enemies;
    mgr.update(1.0f, p, no_enemies);

    float new_dist = std::sqrt(mob->pos[0] * mob->pos[0] + mob->pos[2] * mob->pos[2]);
    // Mob should move closer to the player (standard MC move speed ~3.8 m/s)
    EXPECT_LT(new_dist, init_dist);
    EXPECT_GT(new_dist, 2.0f); // Still heading toward idle radius (~2.5m)
}

TEST(VirtualSummonTest, TeleportsWhenPlayerMovesTooFar) {
    VirtualSummonManager mgr;
    auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t id = mgr.spawnMob("zombie", p, 2.0f);

    // Player suddenly moves 100 metres away (fast travel, sprint or warp)
    p = makePlayer(100.0f, 0.0f, 200.0f, 0.0f);
    std::vector<EnemyTarget> no_enemies;
    mgr.update(0.1f, p, no_enemies);

    const auto* mob = mgr.findMob(id);
    ASSERT_NE(mob, nullptr);
    float dx = mob->pos[0] - p.pos[0];
    float dz = mob->pos[2] - p.pos[2];
    float dist = std::sqrt(dx * dx + dz * dz);
    // Should have teleported close to the player (< 5.0m)
    EXPECT_LE(dist, 5.0f);
}

TEST(VirtualSummonTest, TargetsNearbyHostileAndGeneratesDamageIntent) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t id = mgr.spawnMob("zombie", p, 2.0f);

    // Enemy placed 5 metres away
    std::vector<EnemyTarget> enemies = {
        makeEnemy(0xDEADBEEF, 0.0f, 0.0f, 5.0f, 100, 100)
    };

    // Update 1: mob should detect enemy and start moving toward it
    mgr.update(0.5f, p, enemies);
    const auto* mob = mgr.findMob(id);
    ASSERT_NE(mob, nullptr);
    EXPECT_EQ(mob->target_chr_ins, 0xDEADBEEF);

    // Update 2: step mob close to enemy (within 1.5m attack range)
    for (int i = 0; i < 10; ++i) {
        mgr.update(0.2f, p, enemies);
    }

    // Now in range, drain generated attack intents
    auto intents = mgr.drainDamageIntents();
    ASSERT_FALSE(intents.empty());
    EXPECT_EQ(intents[0].target_chr_ins, 0xDEADBEEF);
    EXPECT_GT(intents[0].damage_amount, 0.0f);

    // Cooldown prevents spamming damage every frame
    auto rapid_intents = mgr.drainDamageIntents();
    EXPECT_TRUE(rapid_intents.empty());
}

TEST(VirtualSummonTest, DamageAndDeathLifecycleWithoutCrash) {
    VirtualSummonManager mgr;
    const auto p = makePlayer();
    uint64_t id = mgr.spawnMob("zombie", p, 2.0f);

    // Non-lethal damage
    bool survived = mgr.applyDamage(id, 5);
    EXPECT_TRUE(survived);
    const auto* mob = mgr.findMob(id);
    ASSERT_NE(mob, nullptr);
    EXPECT_EQ(mob->hp, 15);
    EXPECT_GT(mob->hurt_timer, 0.0f);

    // Lethal damage
    survived = mgr.applyDamage(id, 20);
    EXPECT_FALSE(survived);
    EXPECT_EQ(mob->hp, 0);
    EXPECT_TRUE(mob->is_dying);

    // During death timer, mob remains visible for death animation
    std::vector<EnemyTarget> no_enemies;
    mgr.update(0.5f, p, no_enemies);
    EXPECT_EQ(mgr.activeCount(), 1u);

    // After death timer expires (> 1.0s), mob is completely pruned safely
    mgr.update(0.6f, p, no_enemies);
    EXPECT_EQ(mgr.activeCount(), 0u);
    EXPECT_EQ(mgr.findMob(id), nullptr);
}

TEST(VirtualSummonTest, GeneratesMobSnapshotsForDirectRenderCompatibility) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(10.0f, 2.0f, 30.0f, 1.57f);
    mgr.spawnMob("zombie", p, 3.0f);

    auto snapshots = mgr.generateSnapshots();
    ASSERT_EQ(snapshots.size(), 1u);
    EXPECT_NE(snapshots[0].id, 0u);
    EXPECT_EQ(snapshots[0].model_type, "zombie");
    EXPECT_EQ(snapshots[0].hp, 20);
    EXPECT_EQ(snapshots[0].max_hp, 20);
    EXPECT_FLOAT_EQ(snapshots[0].scale, 1.0f);

    // Feeding to existing MobRegistry must not throw or fail
    eldenring::mobs::MobRegistry registry;
    const auto draws = registry.update(1.0f / 60.0f, snapshots);
    ASSERT_EQ(draws.size(), 1u);
    EXPECT_EQ(draws[0].id, snapshots[0].id);
    EXPECT_EQ(draws[0].model_type, "zombie");
}

TEST(VirtualSummonTest, DismissAllSafelyClearsAllVirtualSummonsWithoutHook) {
    VirtualSummonManager mgr;
    const auto p = makePlayer();
    mgr.spawnMob("zombie", p, 2.0f);
    mgr.spawnMob("skeleton", p, 3.0f);
    EXPECT_EQ(mgr.activeCount(), 2u);

    // Dismiss is instant and internal, does not call or hook any FromSoftware memory
    mgr.dismissAll();
    EXPECT_EQ(mgr.activeCount(), 0u);
    EXPECT_TRUE(mgr.generateSnapshots().empty());
}
