#include <gtest/gtest.h>
#include "wukong_adapter.hpp"
#include "mc/voxel_world.hpp"
#include "mc/combat.hpp"
#include "mc/animator.hpp"

using namespace mc;
using namespace mc::adapter;
using namespace b1::native;

TEST(WukongAdapterTest, BlockColliderCreationAndRaycast) {
    WukongAdapter adapter;
    GridPos pos{1, 2, 3};
    Vec3 world_pos{100.0f, 200.0f, 300.0f};

    uint64_t handle = adapter.createBlockCollider(pos, BlockId::Stone, world_pos);
    EXPECT_GT(handle, 0u);
    EXPECT_EQ(adapter.getBlockColliderCount(), 1u);

    // Raycast hitting top of the block: from (100, 200, 500) straight down to (100, 200, 200)
    Vec3 ray_start{100.0f, 200.0f, 500.0f};
    Vec3 ray_end{100.0f, 200.0f, 200.0f};
    RaycastResult hit = adapter.raycastWorld(ray_start, ray_end);

    EXPECT_TRUE(hit.has_hit);
    EXPECT_TRUE(hit.is_block);
    EXPECT_EQ(hit.hit_entity_id, handle);
    EXPECT_NEAR(hit.point.x, 100.0f, 1e-2f);
    EXPECT_NEAR(hit.point.y, 200.0f, 1e-2f);
    EXPECT_NEAR(hit.point.z, 350.0f, 1e-2f); // top face at 300 + 50
    EXPECT_NEAR(hit.normal.z, 1.0f, 1e-2f);

    // Destroy collider
    adapter.destroyBlockCollider(handle);
    EXPECT_EQ(adapter.getBlockColliderCount(), 0u);

    // Raycast should now miss
    RaycastResult miss = adapter.raycastWorld(ray_start, ray_end);
    EXPECT_FALSE(miss.has_hit);
}

TEST(WukongAdapterTest, PhysicsLinearImpulse) {
    WukongAdapter adapter;
    ABGUCharacter enemy;
    enemy.Velocity = FVector{0.0f, 0.0f, 0.0f};

    adapter.registerEntity(101, &enemy);
    adapter.applyLinearImpulse(101, Vec3{0.0f, 200.0f, 300.0f});

    EXPECT_NEAR(enemy.Velocity.Y, 200.0f, 1e-2f);
    EXPECT_NEAR(enemy.Velocity.Z, 300.0f, 1e-2f);
}

TEST(WukongAdapterTest, RenderPlayerVisibilityAndSteveParts) {
    APlayerController controller;
    ABGUPlayerCharacter player;
    UCapsuleComponent capsule;
    USceneComponent mesh;
    USceneComponent root;

    player.RootComponent = &root;
    player.CapsuleComponent = &capsule;
    player.MeshComponent = &mesh;
    controller.ControlledPawn = &player;

    WukongAdapter adapter(&controller);

    // Hide native player mesh
    adapter.setNativePlayerVisible(false);
    EXPECT_TRUE(adapter.isNativePlayerHidden());
    EXPECT_TRUE(mesh.bHiddenInGame);
    // Physics capsule must remain active and visible to physics
    EXPECT_FALSE(capsule.bHiddenInGame);

    // Spawn 12 Steve parts
    EXPECT_TRUE(adapter.spawnSteveParts());
    EXPECT_TRUE(adapter.isSteveSpawned());

    // Update transforms via SteveAnimator
    SteveAnimator animator;
    SteveAnimInput input{};
    input.forward_speed = 4.3f;
    animator.update(0.1f, input);
    adapter.updateStevePartTransforms(animator.getTransforms());

    // Clean up Steve parts & restore player
    adapter.destroySteveParts();
    EXPECT_FALSE(adapter.isSteveSpawned());

    adapter.setNativePlayerVisible(true);
    EXPECT_FALSE(adapter.isNativePlayerHidden());
    EXPECT_FALSE(mesh.bHiddenInGame);
}

TEST(WukongAdapterTest, RenderBlockVisualAndCrackStages) {
    WukongAdapter adapter;
    GridPos pos{0, 0, 1};
    Vec3 world_pos{0.0f, 0.0f, 100.0f};

    uint64_t handle = adapter.spawnBlockVisual(pos, BlockId::Dirt, world_pos);
    EXPECT_GT(handle, 0u);
    EXPECT_EQ(adapter.getBlockVisualCount(), 1u);

    // Progress crack stages 0..9
    for (int stage = 0; stage <= 9; ++stage) {
        adapter.setBlockCrackStage(handle, stage);
    }

    adapter.destroyBlockVisual(handle);
    EXPECT_EQ(adapter.getBlockVisualCount(), 0u);
}

TEST(WukongAdapterTest, CombatProcessHitAndMaxHealth) {
    WukongAdapter adapter;
    ABGUCharacter boss;
    boss.Health = 1000.0f;
    boss.MaxHealth = 1000.0f;
    boss.TeamId = 2; // Enemy team

    adapter.registerEntity(201, &boss);

    EXPECT_FLOAT_EQ(adapter.getMaxHealth(201), 1000.0f);

    HitIntent hit{};
    hit.attacker_id = 0;
    hit.victim_id = 201;
    hit.damage = 150.0f;
    hit.knockback_vector = Vec3{0.0f, 1.0f, 0.0f};
    hit.knockback_force = 4.0f;

    bool hit_result = adapter.processHit(hit);
    EXPECT_TRUE(hit_result);
    EXPECT_FLOAT_EQ(boss.Health, 850.0f);
    EXPECT_GT(boss.LastStaggerLevel, 0);

    // Percentage hit for boss balance
    HitIntent percent_hit{};
    percent_hit.attacker_id = 0;
    percent_hit.victim_id = 201;
    percent_hit.max_hp_percent = 0.2f; // 20% of 1000 = 200
    EXPECT_TRUE(adapter.processHit(percent_hit));
    EXPECT_FLOAT_EQ(boss.Health, 650.0f);

    // Lethal hit
    HitIntent lethal_hit{};
    lethal_hit.victim_id = 201;
    lethal_hit.damage = 700.0f;
    EXPECT_TRUE(adapter.processHit(lethal_hit));
    EXPECT_FLOAT_EQ(boss.Health, 0.0f);
    EXPECT_TRUE(boss.bIsDead);

    // Hitting dead unit should fail
    EXPECT_FALSE(adapter.processHit(hit));
}

TEST(WukongAdapterTest, InputStateQueries) {
    APlayerController controller;
    APlayerCameraManager camera;
    ABGUPlayerCharacter player;

    camera.CameraLocation = FVector{10.0f, 20.0f, 180.0f};
    camera.CameraRotation = FRotator{0.0f, 90.0f, 0.0f}; // Looking along Y axis
    player.ActorLocation = FVector{10.0f, 20.0f, 0.0f};
    player.Velocity = FVector{500.0f, 0.0f, 0.0f};

    controller.PlayerCameraManager = &camera;
    controller.ControlledPawn = &player;

    WukongAdapter adapter(&controller);
    adapter.setEquippedItems(ItemId::DiamondSword, ItemId::TotemOfUndying);

    EXPECT_EQ(adapter.getEquippedMainHand(), ItemId::DiamondSword);
    EXPECT_EQ(adapter.getEquippedOffHand(), ItemId::TotemOfUndying);

    Vec3 cam_pos = adapter.getCameraPosition();
    EXPECT_FLOAT_EQ(cam_pos.x, 10.0f);
    EXPECT_FLOAT_EQ(cam_pos.y, 20.0f);
    EXPECT_FLOAT_EQ(cam_pos.z, 180.0f);

    Vec3 cam_fwd = adapter.getCameraForward();
    EXPECT_NEAR(cam_fwd.x, 0.0f, 1e-4f);
    EXPECT_NEAR(cam_fwd.y, 1.0f, 1e-4f);

    Vec3 player_pos = adapter.getPlayerPosition();
    EXPECT_FLOAT_EQ(player_pos.x, 10.0f);
    EXPECT_FLOAT_EQ(player_pos.z, 0.0f);

    Vec3 player_vel = adapter.getPlayerVelocity();
    EXPECT_FLOAT_EQ(player_vel.x, 500.0f);
}

TEST(WukongAdapterTest, VoxelWorldIntegrationCycle) {
    WukongAdapter adapter;
    VoxelWorld world(adapter, adapter);

    Vec3 player_pos{500.0f, 500.0f, 0.0f};
    GridPos target_grid{1, 1, 0};

    // Place stone block
    bool placed = world.placeBlock(target_grid, BlockId::Stone, player_pos);
    EXPECT_TRUE(placed);
    EXPECT_EQ(adapter.getBlockColliderCount(), 1u);
    EXPECT_EQ(adapter.getBlockVisualCount(), 1u);

    // Mine stone block with diamond pickaxe (break_time = 0.3s)
    // First step: dt = 0.15s (progress = 50%, crack stage 5) -> returns false (not broken yet)
    bool in_progress = world.mineBlock(target_grid, ItemId::DiamondPickaxe, 0.15f);
    EXPECT_FALSE(in_progress);
    EXPECT_EQ(adapter.getBlockColliderCount(), 1u);
    EXPECT_EQ(adapter.getBlockVisualCount(), 1u);

    // Second step: dt = 0.20s (cumulative = 0.35s >= 0.3s) -> returns true (broken)
    bool mined = world.mineBlock(target_grid, ItemId::DiamondPickaxe, 0.20f);
    EXPECT_TRUE(mined);

    // Block destroyed: both collider and visual removed from engine
    EXPECT_EQ(adapter.getBlockColliderCount(), 0u);
    EXPECT_EQ(adapter.getBlockVisualCount(), 0u);
    EXPECT_EQ(world.getActiveBlockCount(), 0u);
}
