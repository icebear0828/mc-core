#include <gtest/gtest.h>

#include "eldenring_virtual_summon.hpp"
#include "eldenring_mobs.hpp"

#include <cmath>
#include <vector>
#include <thread>
#include <atomic>

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

TEST(VirtualSummonTest, CapacityRollingEvictionEnsuresSpawningNeverFails) {
    VirtualSummonManager mgr;
    const auto p = makePlayer();
    std::vector<uint64_t> ids;
    for (size_t i = 0; i < VirtualSummonManager::kMaxVirtualSummons; ++i) {
        uint64_t id = mgr.spawnMob("zombie", p, 2.0f);
        EXPECT_NE(id, 0u);
        ids.push_back(id);
    }
    EXPECT_EQ(mgr.activeCount(), VirtualSummonManager::kMaxVirtualSummons);

    // 17th spawn should NOT fail/reject; it should evict the oldest mob and successfully spawn!
    uint64_t new_id = mgr.spawnMob("creeper", p, 2.0f);
    EXPECT_NE(new_id, 0u);
    EXPECT_EQ(mgr.activeCount(), VirtualSummonManager::kMaxVirtualSummons);
    // Oldest mob must have been evicted cleanly
    EXPECT_EQ(mgr.findMob(ids[0]), nullptr);
    EXPECT_NE(mgr.findMob(new_id), nullptr);
}

TEST(VirtualSummonTest, CreeperAutonomousDetonationPipeline) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t creeper_id = mgr.spawnMob("creeper", p, 2.0f);
    ASSERT_NE(creeper_id, 0u);

    // Hostile enemy at (0, 0, 0) is 2.0m away from creeper at (0, 0, -2) (within 3.5m ignite range)
    std::vector<EnemyTarget> enemies = {
        makeEnemy(0xABCD, 0.0f, 0.0f, 0.0f, 500, 500)
    };

    // Step 1: Creeper detects enemy and enters primed state
    mgr.update(0.1f, p, enemies);
    const auto* mob = mgr.findMob(creeper_id);
    ASSERT_NE(mob, nullptr);
    EXPECT_TRUE(mob->is_primed);
    EXPECT_GT(mob->scale, 1.0f); // Starts swelling

    // Step 2: Step 1.45s (total 1.55s >= 1.5s fuse) -> Detonation!
    mgr.update(1.45f, p, enemies);

    // Drain explosion events
    auto exp_events = mgr.drainExplosionEvents();
    ASSERT_EQ(exp_events.size(), 1u);
    EXPECT_EQ(exp_events[0].source_mob_id, creeper_id);
    EXPECT_GT(exp_events[0].radius, 5.0f);
    EXPECT_GT(exp_events[0].damage, 1000.0f);

    // Creeper is now destroyed by its own explosion
    EXPECT_EQ(mgr.findMob(creeper_id), nullptr);
    EXPECT_EQ(mgr.activeCount(), 0u);
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

TEST(VirtualSummonTest, NeverTeleportsWhenPlayerMovesTooFarSmoothFollowOnly) {
    VirtualSummonManager mgr;
    auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t z_id = mgr.spawnMob("zombie", p, 2.0f);
    uint64_t c_id = mgr.spawnMob("creeper", p, 2.0f);

    // Player suddenly moves 100 metres away (sprint, torrent, warp)
    p = makePlayer(100.0f, 0.0f, 200.0f, 0.0f);
    std::vector<EnemyTarget> no_enemies;
    mgr.update(0.1f, p, no_enemies);

    const auto* mob_z = mgr.findMob(z_id);
    const auto* mob_c = mgr.findMob(c_id);
    ASSERT_NE(mob_z, nullptr);
    ASSERT_NE(mob_c, nullptr);

    float z_dist = std::sqrt((mob_z->pos[0] - p.pos[0]) * (mob_z->pos[0] - p.pos[0]) +
                             (mob_z->pos[2] - p.pos[2]) * (mob_z->pos[2] - p.pos[2]));
    float c_dist = std::sqrt((mob_c->pos[0] - p.pos[0]) * (mob_c->pos[0] - p.pos[0]) +
                             (mob_c->pos[2] - p.pos[2]) * (mob_c->pos[2] - p.pos[2]));

    // Neither Zombie nor Creeper should ever teleport snap! They walk smoothly toward the player.
    EXPECT_GT(z_dist, 50.0f);
    EXPECT_GT(c_dist, 50.0f);
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

TEST(VirtualSummonTest, CreeperNeverGeneratesMeleeDamageIntents) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t creeper_id = mgr.spawnMob("creeper", p, 1.0f);
    ASSERT_NE(creeper_id, 0u);

    // Enemy within 1.0m
    std::vector<EnemyTarget> enemies = {
        makeEnemy(0xDEAD, 0.0f, 0.0f, 0.0f, 500, 500)
    };

    // Update creeper: even within attack range, creeper must NEVER generate DamageIntent (only zombie/melee mobs do)
    mgr.update(0.1f, p, enemies);
    auto intents = mgr.drainDamageIntents();
    EXPECT_TRUE(intents.empty());
}

TEST(VirtualSummonTest, FlintAndSteelIgniteStartsFuseImmediately) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t creeper_id = mgr.spawnMob("creeper", p, 2.0f);
    ASSERT_NE(creeper_id, 0u);

    const auto* mob_before = mgr.findMob(creeper_id);
    ASSERT_NE(mob_before, nullptr);
    EXPECT_FALSE(mob_before->is_primed);

    // Manually ignite via flint and steel
    bool ignited = mgr.igniteCreeper(creeper_id);
    EXPECT_TRUE(ignited);

    const auto* mob_after = mgr.findMob(creeper_id);
    ASSERT_NE(mob_after, nullptr);
    EXPECT_TRUE(mob_after->is_primed);
    EXPECT_FLOAT_EQ(mob_after->fuse_timer, mob_after->fuse_duration);

    // Advance 1.55s -> Detonates even with 0 enemies nearby!
    std::vector<EnemyTarget> no_enemies;
    mgr.update(1.55f, p, no_enemies);

    auto exp = mgr.drainExplosionEvents();
    ASSERT_EQ(exp.size(), 1u);
    EXPECT_EQ(exp[0].source_mob_id, creeper_id);
    EXPECT_EQ(mgr.activeCount(), 0u);
}

TEST(VirtualSummonTest, CreeperExplosionDamagesNearbySummonsChainDetonation) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t c1 = mgr.spawnMob("creeper", p, 1.0f); // at (0, 0, -1)
    uint64_t c2 = mgr.spawnMob("creeper", p, 3.0f); // at (0, 0, -3), 2.0m away from c1
    ASSERT_NE(c1, 0u);
    ASSERT_NE(c2, 0u);
    EXPECT_EQ(mgr.activeCount(), 2u);

    // Igniting c1 manually
    EXPECT_TRUE(mgr.igniteCreeper(c1));

    // Wait until c1 explodes (1.55s)
    std::vector<EnemyTarget> no_enemies;
    mgr.update(1.55f, p, no_enemies);

    auto exp = mgr.drainExplosionEvents();
    ASSERT_EQ(exp.size(), 1u);
    EXPECT_EQ(exp[0].source_mob_id, c1);

    // c1 is destroyed by its own explosion
    EXPECT_EQ(mgr.findMob(c1), nullptr);

    // c2 was in range (2.0m <= 7.0m radius, 1500 damage >> 20 HP) -> c2 must have taken lethal damage!
    const auto* mob2 = mgr.findMob(c2);
    // Either c2 died immediately or is in dying state
    if (mob2) {
        EXPECT_TRUE(mob2->is_dying);
        EXPECT_EQ(mob2->hp, 0);
    }
}

TEST(VirtualSummonTest, AntiClippingPushoutSeparatesSummonsFromOverlapping) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    // Spawn two mobs at virtually identical positions (e.g. 0.05m apart)
    uint64_t m1 = mgr.spawnMob("creeper", p, 2.0f);
    uint64_t m2 = mgr.spawnMob("creeper", p, 2.05f);

    std::vector<EnemyTarget> no_enemies;
    // Step a couple frames
    mgr.update(0.1f, p, no_enemies);

    const auto* mob1 = mgr.findMob(m1);
    const auto* mob2 = mgr.findMob(m2);
    ASSERT_NE(mob1, nullptr);
    ASSERT_NE(mob2, nullptr);

    float dx = mob1->pos[0] - mob2->pos[0];
    float dz = mob1->pos[2] - mob2->pos[2];
    float dist = std::sqrt(dx * dx + dz * dz);

    // Two mobs must have pushed each other apart to prevent clipping (min separation >= 0.5m)
    EXPECT_GE(dist, 0.5f);
}

TEST(VirtualSummonTest, AntiClippingStopsMobFromPenetratingTargetAabb) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    // Mob spawned at (0, 0, 2.0)
    uint64_t id = mgr.spawnMob("creeper", p, 2.0f);

    // Enemy placed at (0, 0, 2.5), distance is 0.5m (less than collision separation radius 0.65m)
    std::vector<EnemyTarget> enemies = {
        makeEnemy(0xEE11, 0.0f, 0.0f, 2.5f, 100, 100)
    };

    // Update for 0.5 second of pursuit
    mgr.update(0.5f, p, enemies);

    const auto* mob = mgr.findMob(id);
    ASSERT_NE(mob, nullptr);

    float dx = mob->pos[0] - 0.0f;
    float dz = mob->pos[2] - 2.5f;
    float dist = std::sqrt(dx * dx + dz * dz);

    // Sum of radiuses: creeper ~0.3m + target ~0.35m = ~0.65m.
    // Pushout must separate them so dist >= 0.65m!
    EXPECT_GE(dist, 0.65f);
}

TEST(VirtualSummonTest, ArchetypeRegistryHasDefaultsAndAllowsCustom) {
    VirtualSummonManager mgr;

    // Verify default archetypes
    const auto* skel_arch = mgr.findArchetype("skeleton");
    ASSERT_NE(skel_arch, nullptr);
    EXPECT_EQ(skel_arch->id, "skeleton");
    EXPECT_EQ(skel_arch->attack_style, MobAttackStyle::Ranged);
    EXPECT_EQ(skel_arch->max_hp, 20);
    EXPECT_FLOAT_EQ(skel_arch->attack_range, 14.0f);
    EXPECT_FLOAT_EQ(skel_arch->attack_min_range, 4.0f);
    EXPECT_FLOAT_EQ(skel_arch->damage, 7.0f);

    const auto* golem_arch = mgr.findArchetype("iron_golem");
    ASSERT_NE(golem_arch, nullptr);
    EXPECT_EQ(golem_arch->id, "iron_golem");
    EXPECT_EQ(golem_arch->attack_style, MobAttackStyle::Melee);
    EXPECT_EQ(golem_arch->max_hp, 100);
    EXPECT_FLOAT_EQ(golem_arch->damage, 21.0f);
    EXPECT_FLOAT_EQ(golem_arch->scale, 1.2f);

    // Register custom archetype
    MobArchetype custom;
    custom.id = "snow_golem";
    custom.max_hp = 10;
    custom.speed = 3.0f;
    custom.attack_style = MobAttackStyle::Ranged;
    custom.attack_range = 10.0f;
    custom.attack_min_range = 3.0f;
    custom.damage = 1.0f;
    custom.attack_cooldown = 0.8f;
    mgr.registerArchetype(custom);

    const auto* registered = mgr.findArchetype("snow_golem");
    ASSERT_NE(registered, nullptr);
    EXPECT_EQ(registered->max_hp, 10);

    // Spawn custom mob and verify properties propagated
    const auto p = makePlayer();
    uint64_t snow_id = mgr.spawnMob("snow_golem", p, 2.0f);
    const auto* snow_mob = mgr.findMob(snow_id);
    ASSERT_NE(snow_mob, nullptr);
    EXPECT_EQ(snow_mob->hp, 10);
    EXPECT_EQ(snow_mob->max_hp, 10);
    EXPECT_FLOAT_EQ(snow_mob->speed, 3.0f);
}

TEST(VirtualSummonTest, SkeletonRangedCombatMaintainsStandoffAndFires) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    // Skeleton spawned at (0, 0, -2.0)
    uint64_t skel_id = mgr.spawnMob("skeleton", p, 2.0f);
    ASSERT_NE(skel_id, 0u);

    // Enemy placed at (0, 0, 12.0) -> distance is 10.0m
    // (within attack_range 14.0m and > attack_min_range 4.0m)
    std::vector<EnemyTarget> enemies = {
        makeEnemy(0xAAAA, 0.0f, 0.0f, 12.0f, 200, 200)
    };

    // Step 0.1s
    mgr.update(0.1f, p, enemies);

    const auto* skel = mgr.findMob(skel_id);
    ASSERT_NE(skel, nullptr);

    // Skeleton should face the enemy (+Z direction has yaw = 0.0 in Elden Ring basis)
    EXPECT_NEAR(skel->yaw, 0.0f, 0.1f);

    // Skeleton should NOT charge blindly into melee range; it maintains standoff
    EXPECT_NEAR(skel->pos[2], 2.0f, 0.5f);

    // Fired projectile and damage intent
    auto dmg_intents = mgr.drainDamageIntents();
    ASSERT_EQ(dmg_intents.size(), 1u);
    EXPECT_EQ(dmg_intents[0].target_chr_ins, 0xAAAAu);
    EXPECT_FLOAT_EQ(dmg_intents[0].damage_amount, 7.0f);
    EXPECT_EQ(dmg_intents[0].source_mob_id, skel_id);

    auto proj_intents = mgr.drainProjectileIntents();
    ASSERT_EQ(proj_intents.size(), 1u);
    EXPECT_EQ(proj_intents[0].target_chr_ins, 0xAAAAu);
    EXPECT_FLOAT_EQ(proj_intents[0].damage, 7.0f);
    EXPECT_FLOAT_EQ(proj_intents[0].speed, 22.0f);

    // Cooldown in effect: step another 0.5s (0.6s total < 1.5s cooldown) -> no new intents
    mgr.update(0.5f, p, enemies);
    EXPECT_TRUE(mgr.drainDamageIntents().empty());
    EXPECT_TRUE(mgr.drainProjectileIntents().empty());

    // Step 1.0s more (total 1.6s > 1.5s cooldown) -> fires second arrow!
    mgr.update(1.0f, p, enemies);
    auto second_shots = mgr.drainDamageIntents();
    EXPECT_EQ(second_shots.size(), 1u);
}

TEST(VirtualSummonTest, SkeletonKitingWhenEnemyRushesClose) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    // Skeleton spawned at (0, 0, 2.0) ahead of player
    uint64_t skel_id = mgr.spawnMob("skeleton", p, 2.0f);
    ASSERT_NE(skel_id, 0u);

    // Enemy placed at (0, 0, 3.0) -> distance is 1.0m, which is LESS than attack_min_range (4.0m)
    std::vector<EnemyTarget> enemies = {
        makeEnemy(0xBBBB, 0.0f, 0.0f, 3.0f, 200, 200)
    };

    float initial_z = mgr.findMob(skel_id)->pos[2]; // 2.0f

    // Step 0.5s: skeleton should back up (kite) away from enemy at (0, 0, 3.0)
    // Moving backwards away from enemy means Z decreases (e.g. from 2.0 to 1.5)
    mgr.update(0.5f, p, enemies);

    const auto* skel = mgr.findMob(skel_id);
    ASSERT_NE(skel, nullptr);
    EXPECT_LT(skel->pos[2], initial_z); // Kited backwards away from enemy!
}

TEST(VirtualSummonTest, IronGolemHeavyMeleeDamageAndTankStats) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t golem_id = mgr.spawnMob("iron_golem", p, 2.0f);
    ASSERT_NE(golem_id, 0u);

    const auto* golem = mgr.findMob(golem_id);
    ASSERT_NE(golem, nullptr);
    EXPECT_EQ(golem->hp, 100);
    EXPECT_EQ(golem->max_hp, 100);
    EXPECT_FLOAT_EQ(golem->scale, 1.2f);

    // Enemy placed at 2.0m (within attack_range 2.5m)
    std::vector<EnemyTarget> enemies = {
        makeEnemy(0xCCCC, 0.0f, 0.0f, 0.0f, 500, 500)
    };

    mgr.update(0.1f, p, enemies);
    auto intents = mgr.drainDamageIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_FLOAT_EQ(intents[0].damage_amount, 21.0f); // Massive iron golem slap

    // Tankiness: taking 30 damage doesn't kill it
    bool alive = mgr.applyDamage(golem_id, 30);
    EXPECT_TRUE(alive);
    EXPECT_EQ(mgr.findMob(golem_id)->hp, 70);
}

TEST(VirtualSummonTest, NonCreeperMobsNeverExplodeOrIgniteUnderEnemyProximity) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    uint64_t skel_id = mgr.spawnMob("skeleton", p, 2.0f);
    uint64_t golem_id = mgr.spawnMob("iron_golem", p, 3.0f);
    ASSERT_NE(skel_id, 0u);
    ASSERT_NE(golem_id, 0u);

    // Flint and steel ignition MUST be rejected for non-creepers!
    EXPECT_FALSE(mgr.igniteCreeper(skel_id));
    EXPECT_FALSE(mgr.igniteCreeper(golem_id));

    // Enemy placed right next to them at 1.5m
    std::vector<EnemyTarget> enemies = {
        makeEnemy(0xEEEE, 0.0f, 0.0f, -2.5f, 200, 200)
    };

    // Update for 2.0s (longer than creeper fuse 1.5s)
    mgr.update(2.0f, p, enemies);

    // Zero explosion events should occur!
    auto exp = mgr.drainExplosionEvents();
    EXPECT_TRUE(exp.empty());

    // Both mobs must still be active and alive!
    EXPECT_EQ(mgr.activeCount(), 2u);
    const auto* skel = mgr.findMob(skel_id);
    const auto* golem = mgr.findMob(golem_id);
    ASSERT_NE(skel, nullptr);
    ASSERT_NE(golem, nullptr);
    EXPECT_FALSE(skel->is_primed);
    EXPECT_FALSE(golem->is_primed);
    EXPECT_GT(skel->hp, 0);
    EXPECT_GT(golem->hp, 0);

    // Snapshots generated must not have white flash
    auto snaps = mgr.generateSnapshots();
    for (const auto& s : snaps) {
        EXPECT_FLOAT_EQ(s.white_flash, 0.0f);
    }
}

TEST(VirtualSummonTest, ConcurrentMultiThreadedSafety) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 0.0f, 0.0f, 0.0f);
    std::vector<EnemyTarget> enemies = {
        makeEnemy(0x1000, 5.0f, 0.0f, 5.0f, 100, 100),
        makeEnemy(0x2000, -5.0f, 0.0f, -5.0f, 100, 100)
    };

    std::atomic<bool> running{true};
    std::atomic<int> update_ticks{0};
    std::atomic<int> snapshot_ticks{0};
    std::atomic<int> spawn_ticks{0};

    // Thread 1: Game thread physics / AI update
    std::thread game_thread([&]() {
        while (running.load(std::memory_order_relaxed)) {
            mgr.update(0.016f, p, enemies);
            auto dmg = mgr.drainDamageIntents();
            auto proj = mgr.drainProjectileIntents();
            auto exp = mgr.drainExplosionEvents();
            (void)dmg; (void)proj; (void)exp;
            ++update_ticks;
            std::this_thread::yield();
        }
    });

    // Thread 2: Render thread snapshot generation
    std::thread render_thread([&]() {
        while (running.load(std::memory_order_relaxed)) {
            auto snaps = mgr.generateSnapshots();
            (void)snaps;
            ++snapshot_ticks;
            std::this_thread::yield();
        }
    });

    // Thread 3: Key / Input thread spawning & interactions
    std::thread input_thread([&]() {
        const char* types[] = {"creeper", "skeleton", "zombie", "iron_golem", "wolf"};
        for (int i = 0; i < 50; ++i) {
            uint64_t id = mgr.spawnMob(types[i % 5], p, 2.0f);
            if (i % 3 == 0) {
                mgr.igniteCreeper(id);
            }
            if (i % 4 == 0) {
                mgr.applyDamage(id, 10);
            }
            ++spawn_ticks;
            std::this_thread::yield();
        }
    });

    input_thread.join();
    running.store(false, std::memory_order_relaxed);
    game_thread.join();
    render_thread.join();

    EXPECT_GT(update_ticks.load(), 0);
    EXPECT_GT(snapshot_ticks.load(), 0);
    EXPECT_EQ(spawn_ticks.load(), 50);
}

TEST(VirtualSummonTest, CreeperDecoupledFromPlayerFlyHeight) {
    VirtualSummonManager mgr;
    const auto p_spawn = makePlayer(0.0f, 10.0f, 0.0f, 0.0f);
    uint64_t creeper_id = mgr.spawnMob("creeper", p_spawn, 2.0f);
    ASSERT_NE(creeper_id, 0u);

    // Initial position is at spawn height (10.0m)
    const auto* mob = mgr.findMob(creeper_id);
    ASSERT_NE(mob, nullptr);
    EXPECT_FLOAT_EQ(mob->pos[1], 10.0f);

    // Player takes off in creative flight to 50.0m in the sky
    const auto p_flying = makePlayer(0.0f, 50.0f, 0.0f, 0.0f);
    std::vector<EnemyTarget> no_enemies;

    // Update for 1 second (60 ticks)
    for (int i = 0; i < 60; ++i) {
        mgr.update(0.016f, p_flying, no_enemies);
    }

    // Creeper MUST NOT be dragged up to 50.0m! It must stay at its ground level (~10.0m).
    mob = mgr.findMob(creeper_id);
    ASSERT_NE(mob, nullptr);
    EXPECT_NEAR(mob->pos[1], 10.0f, 0.5f);
    EXPECT_LT(mob->pos[1], 15.0f);
}

TEST(VirtualSummonTest, GravityAndGroundSnapping) {
    VirtualSummonManager mgr;
    // Spawn mob at y = 15.0m
    const auto p = makePlayer(0.0f, 15.0f, 0.0f, 0.0f);
    uint64_t mob_id = mgr.spawnMob("creeper", p, 2.0f);
    ASSERT_NE(mob_id, 0u);

    // Provide ground sampler returning terrain at y = 10.0m
    mgr.setGroundSampler([](float, float, float) -> std::optional<float> {
        return 10.0f;
    });

    std::vector<EnemyTarget> no_enemies;

    // In mid-air, falling under gravity
    mgr.update(0.1f, p, no_enemies);
    const auto* mob = mgr.findMob(mob_id);
    ASSERT_NE(mob, nullptr);
    EXPECT_LT(mob->pos[1], 15.0f); // Falling down
    EXPECT_GT(mob->pos[1], 10.0f); // Not yet on ground

    // After 1 second, must land firmly on ground at y = 10.0m
    for (int i = 0; i < 60; ++i) {
        mgr.update(0.016f, p, no_enemies);
    }
    mob = mgr.findMob(mob_id);
    ASSERT_NE(mob, nullptr);
    EXPECT_NEAR(mob->pos[1], 10.0f, 1e-3f);
    EXPECT_NEAR(mob->vel_y, 0.0f, 1e-3f);
    EXPECT_TRUE(mob->on_ground);
}

TEST(VirtualSummonTest, StepUpOneBlock) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 10.0f, 0.0f, 0.0f);
    uint64_t mob_id = mgr.spawnMob("creeper", p, 2.0f);
    ASSERT_NE(mob_id, 0u);

    // Initial ground at 10.0m
    float ground_level = 10.0f;
    mgr.setGroundSampler([&](float, float, float) -> std::optional<float> {
        return ground_level;
    });

    std::vector<EnemyTarget> no_enemies;
    mgr.update(0.016f, p, no_enemies);
    EXPECT_NEAR(mgr.findMob(mob_id)->pos[1], 10.0f, 1e-3f);

    // Ground elevation steps up by 1 block (to 11.0m)
    ground_level = 11.0f;
    mgr.update(0.016f, p, no_enemies);

    // Mob should step up to 11.0m
    const auto* mob = mgr.findMob(mob_id);
    ASSERT_NE(mob, nullptr);
    EXPECT_NEAR(mob->pos[1], 11.0f, 1e-3f);
    EXPECT_TRUE(mob->on_ground);
}

TEST(VirtualSummonTest, ShiftTranslatesAllMobsAndProjectiles) {
    VirtualSummonManager mgr;
    const auto p = makePlayer(0.0f, 5.0f, -20.0f, 0.0f);
    uint64_t creeper_id = mgr.spawnMob("creeper", p, 2.0f);
    uint64_t skeleton_id = mgr.spawnMob("skeleton", p, 2.0f);

    const auto* c_before = mgr.findMob(creeper_id);
    const auto* s_before = mgr.findMob(skeleton_id);
    ASSERT_NE(c_before, nullptr);
    ASSERT_NE(s_before, nullptr);
    const float c_x0 = c_before->pos[0], c_y0 = c_before->pos[1], c_z0 = c_before->pos[2];
    const float s_x0 = s_before->pos[0], s_y0 = s_before->pos[1], s_z0 = s_before->pos[2];

    // Engine origin rebase by (0, 0, +32)
    mgr.shift(0.0f, 0.0f, 32.0f);

    const auto* c_after = mgr.findMob(creeper_id);
    const auto* s_after = mgr.findMob(skeleton_id);
    ASSERT_NE(c_after, nullptr);
    ASSERT_NE(s_after, nullptr);

    EXPECT_FLOAT_EQ(c_after->pos[0], c_x0);
    EXPECT_FLOAT_EQ(c_after->pos[1], c_y0);
    EXPECT_FLOAT_EQ(c_after->pos[2], c_z0 + 32.0f);

    EXPECT_FLOAT_EQ(s_after->pos[0], s_x0);
    EXPECT_FLOAT_EQ(s_after->pos[1], s_y0);
    EXPECT_FLOAT_EQ(s_after->pos[2], s_z0 + 32.0f);
}

