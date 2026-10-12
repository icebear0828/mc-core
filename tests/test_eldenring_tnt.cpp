#include <gtest/gtest.h>
#include "eldenring_blocks.hpp"
#include "eldenring_tnt.hpp"

using namespace eldenring::blocks;
using namespace eldenring::tnt;

TEST(EldenRingTnt, IgnitesTntBlockAndRemovesFromGrid) {
    BlockGrid grid;
    grid.place({10, 5, 20}, mc::BlockId::Tnt);
    EXPECT_EQ(grid.get({10, 5, 20}), mc::BlockId::Tnt);

    ErTntManager tnt_mgr;
    bool ignited = tnt_mgr.tryIgniteBlock(grid, {10, 5, 20});
    EXPECT_TRUE(ignited);

    // Block removed from static block grid
    EXPECT_EQ(grid.get({10, 5, 20}), mc::BlockId::Air);
    EXPECT_EQ(tnt_mgr.activeCount(), 1u);

    const auto* primed = tnt_mgr.getTnt(0);
    ASSERT_NE(primed, nullptr);
    // Centered in block
    EXPECT_NEAR(primed->pos[0], 10.5f, 1e-3f);
    EXPECT_NEAR(primed->pos[1], 5.0f, 1e-3f);
    EXPECT_NEAR(primed->pos[2], 20.5f, 1e-3f);
    EXPECT_FLOAT_EQ(primed->fuse, 4.0f);
}

TEST(EldenRingTnt, NonTntBlockCannotBeIgnited) {
    BlockGrid grid;
    grid.place({1, 0, 0}, mc::BlockId::Stone);

    ErTntManager tnt_mgr;
    EXPECT_FALSE(tnt_mgr.tryIgniteBlock(grid, {1, 0, 0}));
    EXPECT_EQ(grid.get({1, 0, 0}), mc::BlockId::Stone);
    EXPECT_EQ(tnt_mgr.activeCount(), 0u);
}

TEST(EldenRingTnt, ExplosionDestroysNearbyBlocksInGrid) {
    BlockGrid grid;
    // Exploding at (0, 0, 0)
    grid.place({1, 0, 0}, mc::BlockId::Dirt);
    grid.place({2, 0, 0}, mc::BlockId::Dirt);
    grid.place({0, 1, 0}, mc::BlockId::Stone);
    grid.place({10, 0, 0}, mc::BlockId::Dirt); // far away

    ErTntManager tnt_mgr;
    float center[3] = {0.5f, 0.5f, 0.5f};
    auto explosion = tnt_mgr.detonateAt(grid, center, 4.0f);

    // Nearby blocks destroyed
    EXPECT_EQ(grid.get({1, 0, 0}), mc::BlockId::Air);
    EXPECT_EQ(grid.get({0, 1, 0}), mc::BlockId::Air);
    // Distant block intact
    EXPECT_EQ(grid.get({10, 0, 0}), mc::BlockId::Dirt);
    EXPECT_GT(explosion.destroyed_cells.size(), 0u);
}

TEST(EldenRingTnt, ChainReactionIgnitesAdjacentTntWithShortFuse) {
    BlockGrid grid;
    grid.place({2, 0, 0}, mc::BlockId::Tnt);

    ErTntManager tnt_mgr;
    float center[3] = {0.5f, 0.5f, 0.5f};
    auto explosion = tnt_mgr.detonateAt(grid, center, 4.0f);

    // Adjacent TNT was ignited into primed TNT entity
    EXPECT_EQ(grid.get({2, 0, 0}), mc::BlockId::Air);
    ASSERT_EQ(tnt_mgr.activeCount(), 1u);
    const auto* chain_tnt = tnt_mgr.getTnt(0);
    ASSERT_NE(chain_tnt, nullptr);
    // Short fuse (<= 1.5s instead of 4.0s)
    EXPECT_LE(chain_tnt->fuse, 1.5f);
    EXPECT_GE(chain_tnt->fuse, 0.5f);
    // Pushed outward away from blast center (+X velocity)
    EXPECT_GT(chain_tnt->vel[0], 0.0f);
}

TEST(EldenRingTnt, ExplosionCalculatesDamageAndKnockbackToEnemies) {
    ErTntManager tnt_mgr;
    float blast_center[3] = {0.0f, 0.0f, 0.0f};

    std::vector<ErTargetMock> targets = {
        {101, {1.0f, 0.0f, 0.0f}}, // 1m away
        {102, {4.0f, 0.0f, 0.0f}}, // 4m away
        {103, {12.0f, 0.0f, 0.0f}} // 12m away (out of range)
    };

    auto hits = tnt_mgr.calculateEnemyHits(blast_center, targets, 8.0f, 1500.0f, 25.0f);
    ASSERT_EQ(hits.size(), 2u);

    EXPECT_EQ(hits[0].target_id, 101);
    EXPECT_EQ(hits[1].target_id, 102);

    // Damage falls off with distance
    EXPECT_GT(hits[0].damage, hits[1].damage);
    // Knockback falls off with distance
    EXPECT_GT(hits[0].knockback, hits[1].knockback);
    // Direction points away from explosion (+X)
    EXPECT_GT(hits[0].impulse_dir[0], 0.9f);
}

TEST(EldenRingTnt, StepReturnsExplosionResultsAndRemovesDetonatedTnt) {
    BlockGrid grid;
    grid.place({0, 5, 0}, mc::BlockId::Tnt);

    ErTntManager tnt_mgr;
    ASSERT_TRUE(tnt_mgr.tryIgniteBlock(grid, {0, 5, 0}, 1.0f));
    EXPECT_EQ(tnt_mgr.activeCount(), 1u);

    // Step 0.5s: fuse not expired
    auto explosions_half = tnt_mgr.step(0.5f, grid);
    EXPECT_TRUE(explosions_half.empty());
    EXPECT_EQ(tnt_mgr.activeCount(), 1u);

    // Step 0.6s: fuse expires, detonates and returns explosion event
    auto explosions_det = tnt_mgr.step(0.6f, grid);
    ASSERT_EQ(explosions_det.size(), 1u);
    EXPECT_NEAR(explosions_det[0].center[0], 0.5f, 1e-2f);
    EXPECT_EQ(tnt_mgr.activeCount(), 0u);
}

TEST(EldenRingTnt, StepHandlesChainReactionWithoutMemoryCorruptionOrIteratorInvalidation) {
    BlockGrid grid;
    // Primary TNT
    grid.place({0, 5, 0}, mc::BlockId::Tnt);
    // Nearby TNT blocks in blast range (will chain ignite)
    grid.place({1, 5, 0}, mc::BlockId::Tnt);
    grid.place({0, 5, 1}, mc::BlockId::Tnt);
    grid.place({-1, 5, 0}, mc::BlockId::Tnt);

    ErTntManager tnt_mgr;
    ASSERT_TRUE(tnt_mgr.tryIgniteBlock(grid, {0, 5, 0}, 0.2f));
    EXPECT_EQ(tnt_mgr.activeCount(), 1u);

    // Step 0.25s: primary TNT explodes and chains into 3 adjacent TNT blocks
    auto explosions = tnt_mgr.step(0.25f, grid);
    ASSERT_EQ(explosions.size(), 1u);
    EXPECT_EQ(explosions[0].chain_tnt_cells.size(), 3u);

    // Primary TNT removed, but 3 newly primed chain TNT entities are active
    EXPECT_EQ(tnt_mgr.activeCount(), 3u);
    for (size_t i = 0; i < tnt_mgr.activeCount(); ++i) {
        const auto* ct = tnt_mgr.getTnt(i);
        ASSERT_NE(ct, nullptr);
        EXPECT_GT(ct->fuse, 0.0f);
        EXPECT_LE(ct->fuse, 1.5f);
    }
}

TEST(EldenRingTnt, ExplosionDamageIntentQueueDrainsAndComputesDamageCorrectly) {
    ErExplosionIntentQueue intent_queue;
    EXPECT_EQ(intent_queue.pendingCount(), 0u);

    // Push two explosion intents
    const float c1[3] = {0.0f, 0.0f, 0.0f};
    const float c2[3] = {10.0f, 0.0f, 0.0f};
    intent_queue.enqueue(c1, 8.0f, 1500.0f);
    intent_queue.enqueue(c2, 4.0f, 800.0f);
    EXPECT_EQ(intent_queue.pendingCount(), 2u);

    auto drained = intent_queue.drain();
    EXPECT_EQ(drained.size(), 2u);
    EXPECT_EQ(intent_queue.pendingCount(), 0u);

    // Verify distance and damage calculation helper
    // Target 2m away from blast at (0,0,0) with radius 8m, max 1500
    float target_pos[3] = {2.0f, 0.0f, 0.0f};
    int dmg = computeTntExplosionDamage(drained[0].center, target_pos, drained[0].radius, drained[0].max_damage);
    // exposure = 1.0 - 2/8 = 0.75 -> 1500 * 0.75 = 1125
    EXPECT_EQ(dmg, 1125);

    // Target 8m or further away takes 0 damage
    float far_pos[3] = {8.5f, 0.0f, 0.0f};
    int far_dmg = computeTntExplosionDamage(drained[0].center, far_pos, drained[0].radius, drained[0].max_damage);
    EXPECT_EQ(far_dmg, 0);
}

