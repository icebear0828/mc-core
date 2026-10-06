#include <gtest/gtest.h>
#include "sekiro_adapter.hpp"
#include "sekiro_native.hpp"
#include "mc/animator.hpp"
#include "mc/voxel_world.hpp"
#include "mc/combat.hpp"

namespace {

TEST(SekiroIntegrationTest, NativeWolfHiddenWhileCapsuleActive) {
    sekiro::native::ChrIns wolf{};
    wolf.Handle = 1001;
    wolf.Name = "Wolf";
    wolf.ModelAlpha = 1.0f;
    wolf.bModelHidden = false;
    wolf.bCapsulePhysicsActive = true;

    mc::adapter::SekiroAdapter adapter(&wolf, nullptr);

    // Initial state: native wolf is visible
    EXPECT_FLOAT_EQ(wolf.ModelAlpha, 1.0f);
    EXPECT_FALSE(wolf.bModelHidden);
    EXPECT_TRUE(wolf.bCapsulePhysicsActive);

    // Activate Steve Mode: Wolf model must be hidden, capsule remains active
    adapter.setNativePlayerVisible(false);
    EXPECT_FLOAT_EQ(wolf.ModelAlpha, 0.0f);
    EXPECT_TRUE(wolf.bModelHidden);
    EXPECT_TRUE(wolf.bCapsulePhysicsActive);

    // Spawn Steve parts
    EXPECT_TRUE(adapter.spawnSteveParts());
    for (size_t i = 0; i < mc::SteveAnimator::kPartCount; ++i) {
        auto part = adapter.getStevePartVisual(static_cast<mc::StevePart>(i));
        ASSERT_NE(part, nullptr);
        EXPECT_TRUE(part->bVisible);
        EXPECT_FLOAT_EQ(part->Alpha, 1.0f);
    }

    // Deactivate Steve Mode: restore Wolf
    adapter.setNativePlayerVisible(true);
    EXPECT_FLOAT_EQ(wolf.ModelAlpha, 1.0f);
    EXPECT_FALSE(wolf.bModelHidden);
}

TEST(SekiroIntegrationTest, FirstPersonRaycastAndVoxelPlacement) {
    sekiro::native::ChrIns wolf{};
    wolf.Position = {1000.0f, 6400.0f, 1000.0f}; // Dantelion cm coordinates
    sekiro::native::ChrCam cam{};
    cam.Position = {1000.0f, 6400.0f, 1000.0f};
    cam.Forward = {0.0f, 0.0f, 1.0f}; // Looking forward along Z

    mc::adapter::SekiroAdapter adapter(&wolf, &cam);
    mc::VoxelWorld world(adapter, adapter);

    // Mock world hit 300cm ahead (surface with normal pointing back at player)
    adapter.setCustomRaycastHandler([](
        const sekiro::native::FVector3& start,
        const sekiro::native::FVector3& end,
        sekiro::native::HavokHitResult& outHit,
        uint64_t ignoreEntity
    ) -> bool {
        (void)ignoreEntity;
        (void)start; (void)end;
        outHit.bHit = true;
        outHit.HitPoint = {1000.0f, 6400.0f, 1300.0f}; // 3m away
        outHit.HitNormal = {0.0f, 0.0f, -1.0f};       // Facing player
        outHit.HitColliderHandle = 999;
        outHit.bIsStaticBlock = false;
        return true;
    });

    mc::RaycastResult hit = adapter.raycastWorld({1000.f, 1000.f, 6400.f}, {1000.f, 1500.f, 6400.f});
    EXPECT_TRUE(hit.has_hit);

    // Calculate placement target: should snap to 100cm integer grid
    auto candidate = mc::VoxelWorld::calculatePlacementTarget(hit);
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->x, 10);
    EXPECT_EQ(candidate->y, 13);
    EXPECT_EQ(candidate->z, 64);

    // Place Stone block
    bool placed = world.placeBlock(*candidate, mc::BlockId::Stone, adapter.getPlayerPosition());
    EXPECT_TRUE(placed);
    EXPECT_EQ(world.getActiveBlockCount(), 1u);

    // Verify crack stages 0..9 monotonic progression (break_time is 0.3s for Stone with pickaxe)
    for (int stage = 0; stage < 8; ++stage) {
        world.mineBlock(*candidate, mc::ItemId::DiamondPickaxe, 0.025f);
        const auto* blk = world.getBlock(*candidate);
        ASSERT_NE(blk, nullptr);
        EXPECT_GE(blk->mining_stage, 0);
        EXPECT_LE(blk->mining_stage, 9);
    }

    // Complete mining
    bool destroyed = false;
    for (int i = 0; i < 10; ++i) {
        if (world.mineBlock(*candidate, mc::ItemId::DiamondPickaxe, 0.05f)) {
            destroyed = true;
            break;
        }
    }
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(world.getActiveBlockCount(), 0u);
}

TEST(SekiroIntegrationTest, CombatCriticalHitAndDeathblowTrigger) {
    sekiro::native::ChrIns wolf{};
    sekiro::native::ChrIns boss{};
    boss.Handle = 2002;
    boss.Name = "Genichiro";
    boss.Health = 100.0f;
    boss.MaxHealth = 100.0f;
    boss.Posture = 0.0f;
    boss.MaxPosture = 100.0f;
    boss.bDeathblowReady = false;

    mc::adapter::SekiroAdapter adapter(&wolf, nullptr);
    adapter.registerEntity(boss.Handle, &boss);

    mc::CombatEngine combat(adapter);

    // 1. Ground attack: standard damage + posture damage
    mc::HitIntent ground_hit = combat.calculateMeleeHit(
        0, boss.Handle, mc::ItemId::DiamondSword, 1.0f, false, true, {0,0,0}, {0,0,1}
    );
    combat.executeHit(ground_hit);
    EXPECT_LT(boss.Health, 100.0f);
    EXPECT_GT(boss.Posture, 0.0f);
    EXPECT_FALSE(boss.bDeathblowReady);

    // 2. Heavy critical attacks to break posture
    for (int i = 0; i < 5; ++i) {
        mc::HitIntent crit_hit = combat.calculateMeleeHit(
            0, boss.Handle, mc::ItemId::DiamondSword, 1.0f, true, false, {0,0,0}, {0,0,1}
        );
        combat.executeHit(crit_hit);
    }

    // Posture broken -> Deathblow ready!
    EXPECT_GE(boss.Posture, boss.MaxPosture);
    EXPECT_TRUE(boss.bDeathblowReady);
}

} // namespace
