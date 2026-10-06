#include <gtest/gtest.h>
#include "sekiro_adapter.hpp"
#include "mc/voxel_world.hpp"
#include "mc/combat.hpp"
#include "mc/animator.hpp"

using namespace mc;
using namespace mc::adapter;
using namespace sekiro::native;

TEST(SekiroAdapterTest, BlockColliderCreationAndRaycast) {
    SekiroAdapter adapter;
    GridPos pos{2, 3, 4};
    Vec3 world_pos{200.0f, 300.0f, 400.0f};

    uint64_t handle = adapter.createBlockCollider(pos, BlockId::Stone, world_pos);
    EXPECT_GT(handle, 0u);
    EXPECT_EQ(adapter.getBlockColliderCount(), 1u);

    // 1. Raycast hitting top face: from (200, 300, 600) straight down to (200, 300, 300)
    Vec3 ray_start_top{200.0f, 300.0f, 600.0f};
    Vec3 ray_end_top{200.0f, 300.0f, 300.0f};
    RaycastResult hit_top = adapter.raycastWorld(ray_start_top, ray_end_top);

    EXPECT_TRUE(hit_top.has_hit);
    EXPECT_TRUE(hit_top.is_block);
    EXPECT_EQ(hit_top.hit_entity_id, handle);
    EXPECT_NEAR(hit_top.point.x, 200.0f, 1e-2f);
    EXPECT_NEAR(hit_top.point.y, 300.0f, 1e-2f);
    EXPECT_NEAR(hit_top.point.z, 450.0f, 1e-2f); // top face at 400 + 50
    EXPECT_NEAR(hit_top.normal.z, 1.0f, 1e-2f);

    // 2. Raycast hitting side face (+X side): from (400, 300, 400) left to (100, 300, 400)
    Vec3 ray_start_side{400.0f, 300.0f, 400.0f};
    Vec3 ray_end_side{100.0f, 300.0f, 400.0f};
    RaycastResult hit_side = adapter.raycastWorld(ray_start_side, ray_end_side);

    EXPECT_TRUE(hit_side.has_hit);
    EXPECT_TRUE(hit_side.is_block);
    EXPECT_EQ(hit_side.hit_entity_id, handle);
    EXPECT_NEAR(hit_side.point.x, 250.0f, 1e-2f); // +X face at 200 + 50
    EXPECT_NEAR(hit_side.point.y, 300.0f, 1e-2f);
    EXPECT_NEAR(hit_side.point.z, 400.0f, 1e-2f);
    EXPECT_NEAR(hit_side.normal.x, 1.0f, 1e-2f);

    // 3. Destroy collider
    adapter.destroyBlockCollider(handle);
    EXPECT_EQ(adapter.getBlockColliderCount(), 0u);

    // Raycast should now miss cleanly
    RaycastResult miss = adapter.raycastWorld(ray_start_top, ray_end_top);
    EXPECT_FALSE(miss.has_hit);
}

TEST(SekiroAdapterTest, PhysicsLinearImpulse) {
    SekiroAdapter adapter;
    ChrIns enemy;
    enemy.Velocity = FVector3{0.0f, 0.0f, 0.0f};

    adapter.registerEntity(102, &enemy);
    // Apply impulse: MC (X=lateral 150, Y=depth 250, Z=up 350)
    // Dantelion mapping: X=lateral 150, Y=up 350, Z=depth 250
    adapter.applyLinearImpulse(102, Vec3{150.0f, 250.0f, 350.0f});

    EXPECT_NEAR(enemy.Velocity.X, 150.0f, 1e-2f);
    EXPECT_NEAR(enemy.Velocity.Y, 350.0f, 1e-2f); // vertical up in Dantelion
    EXPECT_NEAR(enemy.Velocity.Z, 250.0f, 1e-2f); // forward in Dantelion
}

TEST(SekiroAdapterTest, RenderPlayerVisibilityAndSteveParts) {
    ChrIns player;
    player.Name = "Wolf";
    player.ModelAlpha = 1.0f;
    player.bModelHidden = false;
    player.bCapsulePhysicsActive = true;

    ChrCam camera;
    camera.Position = FVector3{0.f, 160.f, 0.f};

    SekiroAdapter adapter(&player, &camera);

    // Hide native player mesh
    adapter.setNativePlayerVisible(false);
    EXPECT_TRUE(adapter.isNativePlayerHidden());
    EXPECT_TRUE(player.bModelHidden);
    EXPECT_FLOAT_EQ(player.ModelAlpha, 0.0f);
    // Havok physics capsule must remain active for terrain and environment collision
    EXPECT_TRUE(player.bCapsulePhysicsActive);

    // Spawn 12 Steve parts
    EXPECT_TRUE(adapter.spawnSteveParts());
    EXPECT_TRUE(adapter.isSteveSpawned());

    // Update transforms via SteveAnimator
    SteveAnimator animator;
    SteveAnimInput input{};
    input.forward_speed = 4.317f; // MC walking speed
    animator.update(0.1f, input);
    adapter.updateStevePartTransforms(animator.getTransforms());

    // Verify Steve parts are created with correct model names and non-zero positions
    const auto* head_visual = adapter.getStevePartVisual(StevePart::Head);
    ASSERT_NE(head_visual, nullptr);
    EXPECT_EQ(head_visual->ModelName, "steve_head");
    EXPECT_TRUE(head_visual->bVisible);

    const auto* left_arm_visual = adapter.getStevePartVisual(StevePart::LeftArm);
    ASSERT_NE(left_arm_visual, nullptr);
    EXPECT_EQ(left_arm_visual->ModelName, "steve_left_arm");

    // Clean up Steve parts & restore player
    adapter.destroySteveParts();
    EXPECT_FALSE(adapter.isSteveSpawned());

    adapter.setNativePlayerVisible(true);
    EXPECT_FALSE(adapter.isNativePlayerHidden());
    EXPECT_FALSE(player.bModelHidden);
    EXPECT_GT(player.ModelAlpha, 0.0f);
}

TEST(SekiroAdapterTest, RenderBlockVisualAndCrackStages) {
    SekiroAdapter adapter;
    GridPos pos{0, 1, 2};
    Vec3 world_pos{0.0f, 100.0f, 200.0f};

    uint64_t handle = adapter.spawnBlockVisual(pos, BlockId::Stone, world_pos);
    EXPECT_GT(handle, 0u);
    EXPECT_EQ(adapter.getBlockVisualCount(), 1u);

    // Progress crack stages 0..9
    for (int stage = 0; stage <= 9; ++stage) {
        adapter.setBlockCrackStage(handle, stage);
    }

    adapter.destroyBlockVisual(handle);
    EXPECT_EQ(adapter.getBlockVisualCount(), 0u);
}

TEST(SekiroAdapterTest, CombatProcessHitAndDeathblow) {
    SekiroAdapter adapter;
    ChrIns boss;
    boss.Name = "Genichiro";
    boss.Health = 1000.0f;
    boss.MaxHealth = 1000.0f;
    boss.Posture = 0.0f;
    boss.MaxPosture = 200.0f;
    boss.TeamId = 1; // Enemy team

    adapter.registerEntity(202, &boss);

    EXPECT_FLOAT_EQ(adapter.getMaxHealth(202), 1000.0f);

    HitIntent hit{};
    hit.attacker_id = 0;
    hit.victim_id = 202;
    hit.damage = 100.0f;
    hit.knockback_vector = Vec3{0.0f, 1.0f, 0.0f};
    hit.knockback_force = 3.0f;

    bool hit_result = adapter.processHit(hit);
    EXPECT_TRUE(hit_result);
    EXPECT_FLOAT_EQ(boss.Health, 900.0f);
    EXPECT_GT(boss.Posture, 0.0f); // Posture accumulated
    EXPECT_GT(boss.StaggerLevel, 0);

    // Percentage hit for boss dynamic balance
    HitIntent percent_hit{};
    percent_hit.attacker_id = 0;
    percent_hit.victim_id = 202;
    percent_hit.max_hp_percent = 0.25f; // 25% of 1000 = 250
    EXPECT_TRUE(adapter.processHit(percent_hit));
    EXPECT_FLOAT_EQ(boss.Health, 650.0f);

    // Lethal posture breaking hit (fills Posture past MaxPosture)
    HitIntent posture_break_hit{};
    posture_break_hit.victim_id = 202;
    posture_break_hit.damage = 500.0f;
    posture_break_hit.knockback_force = 10.0f;
    EXPECT_TRUE(adapter.processHit(posture_break_hit));
    EXPECT_TRUE(boss.bDeathblowReady); // 忍杀红点触发

    // Lethal health finishing hit
    HitIntent lethal_hit{};
    lethal_hit.victim_id = 202;
    lethal_hit.damage = 500.0f;
    EXPECT_TRUE(adapter.processHit(lethal_hit));
    EXPECT_FLOAT_EQ(boss.Health, 0.0f);
    EXPECT_TRUE(boss.bIsDead);

    // Hitting dead unit should fail
    EXPECT_FALSE(adapter.processHit(hit));
}

TEST(SekiroAdapterTest, InputStateQueries) {
    ChrIns player;
    ChrCam camera;

    // Dantelion camera: X=10, Y=180(up), Z=20(forward)
    camera.Position = FVector3{10.0f, 180.0f, 20.0f};
    camera.Forward = FVector3{0.0f, 0.0f, 1.0f}; // Looking forward along Z in Dantelion
    player.Position = FVector3{10.0f, 0.0f, 20.0f};
    player.Velocity = FVector3{400.0f, 0.0f, 0.0f};

    SekiroAdapter adapter(&player, &camera);
    adapter.setEquippedItems(ItemId::DiamondSword, ItemId::TotemOfUndying);

    EXPECT_EQ(adapter.getEquippedMainHand(), ItemId::DiamondSword);
    EXPECT_EQ(adapter.getEquippedOffHand(), ItemId::TotemOfUndying);

    // Check camera position in MC space (X=lateral, Y=depth, Z=up)
    Vec3 cam_pos = adapter.getCameraPosition();
    EXPECT_FLOAT_EQ(cam_pos.x, 10.0f);
    EXPECT_FLOAT_EQ(cam_pos.y, 20.0f);
    EXPECT_FLOAT_EQ(cam_pos.z, 180.0f);

    Vec3 cam_fwd = adapter.getCameraForward();
    EXPECT_FLOAT_EQ(cam_fwd.x, 0.0f);
    EXPECT_FLOAT_EQ(cam_fwd.y, 1.0f);
    EXPECT_FLOAT_EQ(cam_fwd.z, 0.0f);

    Vec3 player_pos = adapter.getPlayerPosition();
    EXPECT_FLOAT_EQ(player_pos.x, 10.0f);
    EXPECT_FLOAT_EQ(player_pos.y, 20.0f);
    EXPECT_FLOAT_EQ(player_pos.z, 0.0f);

    Vec3 player_vel = adapter.getPlayerVelocity();
    EXPECT_FLOAT_EQ(player_vel.x, 400.0f);
}

TEST(SekiroAdapterTest, VoxelWorldIntegrationCycle) {
    SekiroAdapter adapter;
    VoxelWorld world(adapter, adapter);

    Vec3 player_pos{500.0f, 500.0f, 0.0f};
    GridPos target_grid{1, 1, 0};

    // Place stone block
    bool placed = world.placeBlock(target_grid, BlockId::Stone, player_pos);
    EXPECT_TRUE(placed);
    EXPECT_EQ(adapter.getBlockColliderCount(), 1u);
    EXPECT_EQ(adapter.getBlockVisualCount(), 1u);

    // Mine stone block with diamond pickaxe (break_time = 0.3s)
    // Step 1: dt = 0.15s (progress = 50%, crack stage 5) -> returns false (not broken)
    bool in_progress = world.mineBlock(target_grid, ItemId::DiamondPickaxe, 0.15f);
    EXPECT_FALSE(in_progress);
    EXPECT_EQ(adapter.getBlockColliderCount(), 1u);
    EXPECT_EQ(adapter.getBlockVisualCount(), 1u);

    // Step 2: dt = 0.20s (cumulative = 0.35s >= 0.3s) -> returns true (broken)
    bool mined = world.mineBlock(target_grid, ItemId::DiamondPickaxe, 0.20f);
    EXPECT_TRUE(mined);

    // Both collider and visual must be cleaned up
    EXPECT_EQ(adapter.getBlockColliderCount(), 0u);
    EXPECT_EQ(adapter.getBlockVisualCount(), 0u);
    EXPECT_EQ(world.getActiveBlockCount(), 0u);
}

extern "C" {
void SekiroMod_Initialize(sekiro::native::ChrIns* player, sekiro::native::ChrCam* camera);
void SekiroMod_Shutdown();
void SekiroMod_SetSteveMode(bool active);
bool SekiroMod_IsSteveModeActive();
void SekiroMod_Tick(float delta_time);
}

TEST(SekiroAdapterTest, PluginLifecycleAndTick) {
    ChrIns player;
    ChrCam camera;

    SekiroMod_Initialize(&player, &camera);
    EXPECT_FALSE(SekiroMod_IsSteveModeActive());

    // Enter Steve Mode
    SekiroMod_SetSteveMode(true);
    EXPECT_TRUE(SekiroMod_IsSteveModeActive());
    EXPECT_TRUE(player.bModelHidden);

    // Tick frame
    SekiroMod_Tick(0.05f);

    // Exit Steve Mode
    SekiroMod_SetSteveMode(false);
    EXPECT_FALSE(SekiroMod_IsSteveModeActive());
    EXPECT_FALSE(player.bModelHidden);

    SekiroMod_Shutdown();
}
