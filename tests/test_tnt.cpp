#include <gtest/gtest.h>
#include "mc/tnt.hpp"
#include "mc/types.hpp"

#include <vector>
#include <cmath>

namespace mc {

TEST(TntTest, FuseMonotonicallyDecreasesAndTriggersExplosionAtZero) {
    PrimedTnt tnt(1, Vec3(0.0f, 100.0f, 0.0f), 4.0f);
    EXPECT_FLOAT_EQ(tnt.getFuseSeconds(), 4.0f);
    EXPECT_FALSE(tnt.hasExploded());

    float prev_fuse = tnt.getFuseSeconds();
    // Step 1.0 second in 0.1s increments
    for (int i = 0; i < 10; ++i) {
        tnt.update(0.1f, 0.0f); // dt = 0.1s, ground_y = 0.0f
        EXPECT_LT(tnt.getFuseSeconds(), prev_fuse); // strictly monotonic decrease
        prev_fuse = tnt.getFuseSeconds();
        EXPECT_FALSE(tnt.hasExploded());
    }
    EXPECT_NEAR(tnt.getFuseSeconds(), 3.0f, 1e-4f);

    // Step remaining 3.0 seconds
    tnt.update(2.9f, 0.0f);
    EXPECT_FALSE(tnt.hasExploded());
    EXPECT_GT(tnt.getFuseSeconds(), 0.0f);

    tnt.update(0.15f, 0.0f);
    EXPECT_LE(tnt.getFuseSeconds(), 0.0f);
    EXPECT_TRUE(tnt.hasExploded());
}

TEST(TntTest, PrimedTntGravityAndBounceKinematics) {
    // Initial velocity: +150 cm/s Y (1.5 m/s)
    PrimedTnt tnt(2, Vec3(0.0f, 100.0f, 0.0f), 4.0f, Vec3(0.0f, 150.0f, 0.0f));

    // Initially rises
    tnt.update(0.05f, 0.0f);
    EXPECT_GT(tnt.getPosition().y, 100.0f);

    // After peaking, vertical velocity turns downward under gravity (-1600 cm/s^2) while still airborne
    for (int i = 0; i < 2; ++i) {
        tnt.update(0.05f, 0.0f);
    }
    EXPECT_LT(tnt.getVelocity().y, 0.0f);
    EXPECT_GT(tnt.getPosition().y, 50.0f);

    // Step until it hits ground (ground_y = 50.0f)
    for (int i = 0; i < 40; ++i) {
        tnt.update(0.05f, 50.0f);
    }
    // Clamped at ground and vertical velocity stopped
    EXPECT_NEAR(tnt.getPosition().y, 50.0f, 1e-2f);
    EXPECT_NEAR(tnt.getVelocity().y, 0.0f, 1e-2f);
}

TEST(TntTest, WhiteFlashFrequencyIncreasesAsFuseRunsOut) {
    PrimedTnt tnt(3, Vec3(0.0f, 0.0f, 0.0f), 4.0f);

    // Early stage (fuse = 3.5s): low frequency flash (~2 Hz)
    tnt.update(0.5f, 0.0f);
    float freq_early = tnt.getFlashFrequency();

    // Mid stage (fuse = 1.5s): medium frequency (~4 Hz)
    tnt.update(2.0f, 0.0f);
    float freq_mid = tnt.getFlashFrequency();
    EXPECT_GT(freq_mid, freq_early);

    // Final stage (fuse = 0.3s): urgent high frequency (~8-10 Hz)
    tnt.update(1.2f, 0.0f);
    float freq_late = tnt.getFlashFrequency();
    EXPECT_GT(freq_late, freq_mid);
}

TEST(TntTest, ExplosionDestroysDirtAndStoneByBlastResistance) {
    // Blast resistance ladder:
    EXPECT_FLOAT_EQ(BlockBlastResistance(BlockId::Air), 0.0f);
    EXPECT_FLOAT_EQ(BlockBlastResistance(BlockId::Tnt), 0.0f);
    EXPECT_LT(BlockBlastResistance(BlockId::Dirt), BlockBlastResistance(BlockId::Stone));
    EXPECT_GT(BlockBlastResistance(BlockId::Obsidian), 1000.0f);

    // Mock block world with Dirt at (2, 0, 0), Stone at (3, 0, 0), Obsidian at (1, 0, 0)
    std::vector<std::pair<GridPos, BlockId>> test_blocks = {
        {{1, 0, 0}, BlockId::Obsidian}, // right next to blast
        {{2, 0, 0}, BlockId::Dirt},
        {{3, 0, 0}, BlockId::Stone},
        {{4, 0, 0}, BlockId::Dirt}
    };

    auto lookup_block = [&](const GridPos& p) -> BlockId {
        for (const auto& [pos, id] : test_blocks) {
            if (pos == p) return id;
        }
        return BlockId::Air;
    };

    Vec3 blast_center(0.0f, 0.0f, 0.0f); // in grid units or block coordinates
    ExplosionConfig config;
    config.power = 4.0f; // Standard MC TNT power
    config.radius_blocks = 4.5f;

    ExplosionResult result = CalculateVoxelExplosion(blast_center, config, lookup_block, {});

    // Obsidian must NOT be destroyed
    bool obsidian_destroyed = false;
    bool dirt_destroyed = false;
    for (const auto& pos : result.destroyed_blocks) {
        if (pos == GridPos{1, 0, 0}) obsidian_destroyed = true;
        if (pos == GridPos{2, 0, 0}) dirt_destroyed = true;
    }
    EXPECT_FALSE(obsidian_destroyed);
    EXPECT_TRUE(dirt_destroyed);
}

TEST(TntTest, ChainReactionIgnitesNearbyTntWithReducedFuse) {
    std::vector<std::pair<GridPos, BlockId>> test_blocks = {
        {{2, 0, 0}, BlockId::Tnt}, // Adjacent TNT block
        {{1, 1, 0}, BlockId::Dirt}
    };

    auto lookup_block = [&](const GridPos& p) -> BlockId {
        for (const auto& [pos, id] : test_blocks) {
            if (pos == p) return id;
        }
        return BlockId::Air;
    };

    Vec3 blast_center(0.0f, 0.0f, 0.0f);
    ExplosionConfig config;
    config.power = 4.0f;
    config.radius_blocks = 4.5f;

    ExplosionResult result = CalculateVoxelExplosion(blast_center, config, lookup_block, {});

    // Adjacent TNT must be reported in ignited_tnt_blocks with a short fuse
    ASSERT_EQ(result.ignited_tnt_blocks.size(), 1u);
    EXPECT_EQ(result.ignited_tnt_blocks[0], (GridPos{2, 0, 0}));
}

TEST(TntTest, EntityImpulseAndDamageDecaysInverselyWithDistance) {
    Vec3 blast_center(0.0f, 0.0f, 0.0f);
    ExplosionConfig config;
    config.power = 4.0f;
    config.radius_blocks = 8.0f; // Max damage radius in blocks/meters
    config.max_damage = 65.0f;
    config.max_impulse = 20.0f;

    std::vector<ExplosionEntityTarget> targets = {
        {EntityId{2}, Vec3(1.0f, 0.0f, 0.0f)}, // 1 block away
        {EntityId{3}, Vec3(4.0f, 0.0f, 0.0f)}, // 4 blocks away
        {EntityId{4}, Vec3(9.0f, 0.0f, 0.0f)}  // Outside 8 blocks radius
    };

    auto empty_blocks = [](const GridPos&) { return BlockId::Air; };
    ExplosionResult result = CalculateVoxelExplosion(blast_center, config, empty_blocks, targets);

    ASSERT_EQ(result.entity_impacts.size(), 2u); // Target 4 is out of range

    const auto& near_target = result.entity_impacts[0];
    const auto& mid_target = result.entity_impacts[1];

    EXPECT_EQ(near_target.entity_id, EntityId{2});
    EXPECT_EQ(mid_target.entity_id, EntityId{3});

    // Monotonic decay of damage with distance
    EXPECT_GT(near_target.damage, mid_target.damage);
    EXPECT_GT(mid_target.damage, 0.0f);

    // Monotonic decay of impulse magnitude
    EXPECT_GT(near_target.impulse.length(), mid_target.impulse.length());

    // Impulse direction must point directly away from blast center (along +X)
    EXPECT_GT(near_target.impulse.x, 0.0f);
    EXPECT_NEAR(near_target.impulse.y, 0.0f, 1e-4f);
    EXPECT_NEAR(near_target.impulse.z, 0.0f, 1e-4f);
}

} // namespace mc
