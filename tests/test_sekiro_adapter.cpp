#include <gtest/gtest.h>
#include "sekiro_adapter.hpp"
#include "mc/voxel_world.hpp"
#include "mc/combat.hpp"
#include "mc/animator.hpp"
#include "mc/session.hpp"
#include "mc/elytra.hpp"
#include "mc/hud.hpp"

#include <array>
#include <cmath>

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
    EXPECT_EQ(hit_top.hit_collider_handle, handle);
    EXPECT_EQ(hit_top.hit_entity, EntityId::None);
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
    EXPECT_EQ(hit_side.hit_collider_handle, handle);
    EXPECT_EQ(hit_side.hit_entity, EntityId::None);
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

    adapter.registerEntity(EntityId{102}, &enemy);
    // Canonical MC (X=forward 150, Y=left 250, Z=up 350)
    // Dantelion (X=right, Y=up, Z=forward): right = -left, up = up, forward = forward
    adapter.applyLinearImpulse(EntityId{102}, Vec3{150.0f, 250.0f, 350.0f});

    EXPECT_NEAR(enemy.Velocity.X, -250.0f, 1e-2f);
    EXPECT_NEAR(enemy.Velocity.Y, 350.0f, 1e-2f);
    EXPECT_NEAR(enemy.Velocity.Z, 150.0f, 1e-2f);
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

    adapter.registerEntity(EntityId{202}, &boss);

    EXPECT_FLOAT_EQ(adapter.getMaxHealth(EntityId{202}), 1000.0f);

    HitIntent hit{};
    hit.attacker_id = EntityId::None;
    hit.victim_id = EntityId{202};
    hit.damage = 100.0f;
    hit.knockback_vector = Vec3{0.0f, 1.0f, 0.0f};
    hit.knockback_force = 3.0f;

    bool hit_result = adapter.processHit(hit);
    EXPECT_TRUE(hit_result);
    EXPECT_FLOAT_EQ(boss.Health, 900.0f);
    EXPECT_GT(boss.Posture, 0.0f); // Posture accumulated
    // Reaction (stagger/knockback) is owned by CombatEngine::executeHit, never by processHit
    EXPECT_EQ(boss.StaggerLevel, 0);
    EXPECT_FLOAT_EQ(boss.Velocity.Length(), 0.0f);

    // Percentage hit for boss dynamic balance
    HitIntent percent_hit{};
    percent_hit.attacker_id = EntityId::None;
    percent_hit.victim_id = EntityId{202};
    percent_hit.max_hp_percent = 0.25f; // 25% of 1000 = 250
    EXPECT_TRUE(adapter.processHit(percent_hit));
    EXPECT_FLOAT_EQ(boss.Health, 650.0f);

    // Lethal posture breaking hit (fills Posture past MaxPosture)
    HitIntent posture_break_hit{};
    posture_break_hit.victim_id = EntityId{202};
    posture_break_hit.damage = 500.0f;
    posture_break_hit.knockback_force = 10.0f;
    EXPECT_TRUE(adapter.processHit(posture_break_hit));
    EXPECT_TRUE(boss.bDeathblowReady); // 忍杀红点触发

    // Lethal health finishing hit
    HitIntent lethal_hit{};
    lethal_hit.victim_id = EntityId{202};
    lethal_hit.damage = 500.0f;
    EXPECT_TRUE(adapter.processHit(lethal_hit));
    EXPECT_FLOAT_EQ(boss.Health, 0.0f);
    EXPECT_TRUE(boss.bIsDead);

    // Hitting dead unit should fail
    EXPECT_FALSE(adapter.processHit(hit));
}

TEST(SekiroAdapterTest, ExecuteHitAppliesKnockbackExactlyOnce) {
    SekiroAdapter adapter;
    CombatEngine combat(adapter);
    ChrIns boss;
    boss.Health = 1000.0f;
    boss.MaxHealth = 1000.0f;
    adapter.registerEntity(EntityId{202}, &boss);

    HitIntent hit{};
    hit.victim_id = EntityId{202};
    hit.damage = 100.0f;
    hit.knockback_vector = Vec3{1.0f, 0.0f, 0.0f}; // MC forward == Dantelion +Z
    hit.knockback_force = 3.0f;

    EXPECT_TRUE(combat.executeHit(hit));
    EXPECT_NEAR(boss.Velocity.Z, 300.0f, 1e-2f); // force * 100, applied once (not 600)
    EXPECT_GT(boss.StaggerLevel, 0);
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

    // Canonical MC space: X=forward, Y=left, Z=up
    Vec3 cam_pos = adapter.getCameraPosition();
    EXPECT_FLOAT_EQ(cam_pos.x, 20.0f);   // native forward (Z)
    EXPECT_FLOAT_EQ(cam_pos.y, -10.0f);  // native right (X) negated
    EXPECT_FLOAT_EQ(cam_pos.z, 180.0f);  // native up (Y)

    Vec3 cam_fwd = adapter.getCameraForward();
    EXPECT_FLOAT_EQ(cam_fwd.x, 1.0f);
    EXPECT_FLOAT_EQ(cam_fwd.y, 0.0f);
    EXPECT_FLOAT_EQ(cam_fwd.z, 0.0f);

    Vec3 player_pos = adapter.getPlayerPosition();
    EXPECT_FLOAT_EQ(player_pos.x, 20.0f);
    EXPECT_FLOAT_EQ(player_pos.y, -10.0f);
    EXPECT_FLOAT_EQ(player_pos.z, 0.0f);

    Vec3 player_vel = adapter.getPlayerVelocity();
    EXPECT_FLOAT_EQ(player_vel.y, -400.0f); // native +X (right) is MC -Y
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
void SekiroMod_Tick(float delta_time, const mc::InputSnapshot* input);
void SekiroMod_UpdatePointers(sekiro::native::ChrIns* player, sekiro::native::ChrCam* camera);
bool SekiroMod_RegisterEntity(uint64_t entity_id, sekiro::native::ChrIns* entity);
void SekiroMod_UnregisterEntity(uint64_t entity_id);
mc::Session* SekiroMod_GetSession();
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
    SekiroMod_Tick(0.05f, nullptr);

    // Exit Steve Mode
    SekiroMod_SetSteveMode(false);
    EXPECT_FALSE(SekiroMod_IsSteveModeActive());
    EXPECT_FALSE(player.bModelHidden);

    SekiroMod_Shutdown();
}

TEST(SekiroAdapterTest, DynamicPlayerPointerBindingHidesNativeModel) {
    // 1. Initialize with nullptr (simulate game boot / title screen)
    SekiroMod_Initialize(nullptr, nullptr);
    EXPECT_FALSE(SekiroMod_IsSteveModeActive());

    // 2. Activate Steve mode while player is still null
    SekiroMod_SetSteveMode(true);
    EXPECT_TRUE(SekiroMod_IsSteveModeActive());

    // 3. Late-bind real player character (e.g. loaded into game world from WorldChrMan)
    ChrIns live_player;
    live_player.ModelAlpha = 1.0f;
    live_player.bModelHidden = false;
    ChrCam live_camera;

    SekiroMod_UpdatePointers(&live_player, &live_camera);

    // Live player model must be immediately hidden because Steve mode is active!
    EXPECT_TRUE(live_player.bModelHidden);
    EXPECT_FLOAT_EQ(live_player.ModelAlpha, 0.0f);

    // 4. Exit Steve mode restores native player
    SekiroMod_SetSteveMode(false);
    EXPECT_FALSE(live_player.bModelHidden);
    EXPECT_FLOAT_EQ(live_player.ModelAlpha, 1.0f);

    SekiroMod_Shutdown();
}

// ---------------------------------------------------------------------------
// Canonical coordinate mapping (Dantelion: X=right, Y=up, Z=forward  <->  MC: X=forward, Y=left, Z=up)
// ---------------------------------------------------------------------------

TEST(SekiroCoordinateTest, AxesMapToCanonicalMcSpace) {
    using FV = sekiro::native::FVector3;
    Vec3 fwd = SekiroAdapter::toMc(FV{0.f, 0.f, 1.f});
    EXPECT_FLOAT_EQ(fwd.x, 1.f);
    EXPECT_FLOAT_EQ(fwd.y, 0.f);
    EXPECT_FLOAT_EQ(fwd.z, 0.f);

    Vec3 up = SekiroAdapter::toMc(FV{0.f, 1.f, 0.f});
    EXPECT_FLOAT_EQ(up.z, 1.f);

    Vec3 right = SekiroAdapter::toMc(FV{1.f, 0.f, 0.f});
    EXPECT_FLOAT_EQ(right.y, -1.f); // right-hand side is -Y in MC (Z-up, RH)
}

TEST(SekiroCoordinateTest, ToNativeInvertsToMc) {
    const Vec3 v{12.f, -34.f, 56.f};
    const Vec3 back = SekiroAdapter::toMc(SekiroAdapter::toNative(v));
    EXPECT_FLOAT_EQ(back.x, v.x);
    EXPECT_FLOAT_EQ(back.y, v.y);
    EXPECT_FLOAT_EQ(back.z, v.z);
}

namespace {
// Hamilton rotation v' = q v q* for any 3-vector given as {x,y,z}
std::array<float, 3> rotate(float qx, float qy, float qz, float qw, const std::array<float, 3>& v) {
    // t = 2 * cross(q.xyz, v); v' = v + w*t + cross(q.xyz, t)
    const float tx = 2.f * (qy * v[2] - qz * v[1]);
    const float ty = 2.f * (qz * v[0] - qx * v[2]);
    const float tz = 2.f * (qx * v[1] - qy * v[0]);
    return {v[0] + qw * tx + (qy * tz - qz * ty),
            v[1] + qw * ty + (qz * tx - qx * tz),
            v[2] + qw * tz + (qx * ty - qy * tx)};
}
} // namespace

TEST(SekiroCoordinateTest, QuaternionConversionPreservesPhysicalRotation) {
    // The same physical rotation expressed in MC and in Dantelion must move the
    // same physical point to the same place: rotate_native(q', N(v)) == N(rotate_mc(q, v)).
    const Quat cases[] = {
        {0.f, 0.f, std::sin(0.7f), std::cos(0.7f)},                       // yaw
        {std::sin(0.4f), 0.f, 0.f, std::cos(0.4f)},                       // roll about forward
        {0.f, std::sin(0.3f), 0.f, std::cos(0.3f)},                       // pitch
        {0.18257419f, 0.36514837f, 0.54772256f, 0.73029674f},             // generic (unit)
    };
    const Vec3 vs[] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {3.f, -2.f, 5.f}};

    for (const Quat& q : cases) {
        const sekiro::native::FQuat nq = SekiroAdapter::toNativeQuat(q);
        for (const Vec3& v : vs) {
            const auto mc_out = rotate(q.x, q.y, q.z, q.w, {v.x, v.y, v.z});
            const sekiro::native::FVector3 expected =
                SekiroAdapter::toNative(Vec3{mc_out[0], mc_out[1], mc_out[2]});
            const sekiro::native::FVector3 nv = SekiroAdapter::toNative(v);
            const auto got = rotate(nq.X, nq.Y, nq.Z, nq.W, {nv.X, nv.Y, nv.Z});
            EXPECT_NEAR(got[0], expected.X, 1e-4f);
            EXPECT_NEAR(got[1], expected.Y, 1e-4f);
            EXPECT_NEAR(got[2], expected.Z, 1e-4f);
        }
    }
}

TEST(SekiroCoordinateTest, SteveParts_CarryConvertedQuaternion) {
    SekiroAdapter adapter;
    ASSERT_TRUE(adapter.spawnSteveParts());

    SteveAnimator::PartTransforms transforms{};
    const Quat q{0.f, 0.f, std::sin(0.7f), std::cos(0.7f)};
    transforms[static_cast<size_t>(StevePart::Head)].rot = q;
    adapter.updateStevePartTransforms(transforms);

    const auto* head = adapter.getStevePartVisual(StevePart::Head);
    ASSERT_NE(head, nullptr);
    const sekiro::native::FQuat expected = SekiroAdapter::toNativeQuat(q);
    EXPECT_NEAR(head->RotationQuat.X, expected.X, 1e-6f);
    EXPECT_NEAR(head->RotationQuat.Y, expected.Y, 1e-6f);
    EXPECT_NEAR(head->RotationQuat.Z, expected.Z, 1e-6f);
    EXPECT_NEAR(head->RotationQuat.W, expected.W, 1e-6f);
}

TEST(SekiroAdapterTest, OnGroundHeuristicUsesVerticalVelocity) {
    ChrIns player;
    SekiroAdapter adapter(&player, nullptr);
    player.Velocity = FVector3{300.f, 0.f, 300.f};
    EXPECT_TRUE(adapter.isPlayerOnGround());
    player.Velocity = FVector3{0.f, 250.f, 0.f};
    EXPECT_FALSE(adapter.isPlayerOnGround());
    player.Velocity = FVector3{0.f, -250.f, 0.f};
    EXPECT_FALSE(adapter.isPlayerOnGround());
    EXPECT_TRUE(SekiroAdapter().isPlayerOnGround()); // no player bound
}

// ---------------------------------------------------------------------------
// Session wired through the real SekiroAdapter (no loader / D3D involved)
// ---------------------------------------------------------------------------

namespace {

struct RaycastScope {
    ~RaycastScope() { sekiro::native::DantelionEngineContext::CustomRaycast = nullptr; }
};

} // namespace

TEST(SekiroSessionTest, UsePlacesBlockAlongNativeForwardAndRaysUseNativeAxes) {
    ChrIns player;
    player.Handle = 321;
    ChrCam camera;
    camera.Position = FVector3{0.f, 160.f, 0.f};
    camera.Forward = FVector3{0.f, 0.f, 1.f};
    SekiroMod_Initialize(&player, &camera);
    ASSERT_NE(SekiroMod_GetSession(), nullptr);

    FVector3 seen_start{}, seen_end{};
    uint64_t seen_ignore = 0;
    RaycastScope scope;
    DantelionEngineContext::CustomRaycast = [&](const FVector3& s, const FVector3& e, HavokHitResult& out, uint64_t ignore) {
        seen_start = s;
        seen_end = e;
        seen_ignore = ignore;
        out.bHit = true;
        out.HitPoint = SekiroAdapter::toNative(Vec3{250.f, 0.f, 100.f});
        out.HitNormal = SekiroAdapter::toNative(Vec3{-1.f, 0.f, 0.f});
        return true;
    };

    SekiroMod_SetSteveMode(true);
    InputSnapshot in;
    in.hotbar_select = 2; // seeded: 64x dirt block
    in.use_pressed = true;
    SekiroMod_Tick(0.05f, &in);

    EXPECT_NEAR(seen_start.Y, 160.f, 1e-3f);                          // camera height = native up
    EXPECT_NEAR(seen_end.Z, Session::kReachCm, 1e-2f);                // reach goes along native forward
    EXPECT_NEAR(seen_end.X, 0.f, 1e-3f);
    EXPECT_EQ(seen_ignore, 321u); // Session ignores the player: mapped to its Havok handle, not an internal id

    const VoxelBlock* b = SekiroMod_GetSession()->voxel().getBlock({2, 0, 1});
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->id, BlockId::Dirt);
    EXPECT_EQ(SekiroMod_GetSession()->hud().getSlot(2).count, 63u);

    SekiroMod_Shutdown();
}

TEST(SekiroSessionTest, AttackDamagesRegisteredEnemyThroughSession) {
    ChrIns player;
    ChrCam camera;
    camera.Position = FVector3{0.f, 160.f, 0.f};
    camera.Forward = FVector3{0.f, 0.f, 1.f};
    ChrIns enemy;
    enemy.Handle = 900;
    enemy.Health = 1000.f;
    enemy.MaxHealth = 1000.f;

    SekiroMod_Initialize(&player, &camera);
    EXPECT_TRUE(SekiroMod_RegisterEntity(7, &enemy));

    RaycastScope scope;
    DantelionEngineContext::CustomRaycast = [&](const FVector3&, const FVector3&, HavokHitResult& out, uint64_t) {
        out.bHit = true;
        out.HitEntityHandle = 900;
        out.HitPoint = SekiroAdapter::toNative(Vec3{250.f, 0.f, 150.f});
        out.HitNormal = SekiroAdapter::toNative(Vec3{-1.f, 0.f, 0.f});
        return true;
    };

    SekiroMod_SetSteveMode(true);
    InputSnapshot in;
    in.attack_pressed = true; // seeded slot 0 = diamond sword, cooldown starts full
    SekiroMod_Tick(0.05f, &in);

    EXPECT_FLOAT_EQ(enemy.Health, 950.f); // 5% max-hp balance applied by adapter
    EXPECT_NEAR(enemy.Velocity.Z, 800.f * 100.f, 1.f); // sword knockback 800, one impulse along view
    EXPECT_GT(SekiroMod_GetSession()->lastAnimInput().swing_progress, 0.f);

    SekiroMod_UnregisterEntity(7);
    SekiroMod_Shutdown();
    EXPECT_EQ(SekiroMod_GetSession(), nullptr);
}

// ---------------------------------------------------------------------------
// Entity identity
// ---------------------------------------------------------------------------

TEST(SekiroEntityTest, PlayerIsRegisteredAsLocalPlayerAndCanBeHit) {
    ChrIns player;
    player.Health = 100.f;
    player.MaxHealth = 100.f;
    SekiroAdapter adapter(&player, nullptr);
    EXPECT_EQ(adapter.getRegisteredEntity(EntityId::LocalPlayer), &player);
    EXPECT_EQ(adapter.getRegisteredEntity(EntityId::None), nullptr);

    HitIntent fall{};
    fall.victim_id = EntityId::LocalPlayer;
    fall.damage = 10.f;
    EXPECT_TRUE(adapter.processHit(fall));
    EXPECT_FLOAT_EQ(player.Health, 90.f);
}

TEST(SekiroEntityTest, RegisterRejectsNoEntity) {
    SekiroAdapter adapter;
    ChrIns e;
    EXPECT_FALSE(adapter.registerEntity(EntityId::None, &e));
    EXPECT_TRUE(adapter.registerEntity(EntityId{5}, &e));
}

TEST(SekiroEntityTest, IgnoreEntityIsMappedToHavokHandle) {
    ChrIns player;
    player.Handle = 555;
    SekiroAdapter adapter(&player, nullptr);

    uint64_t seen = 12345;
    RaycastScope scope;
    DantelionEngineContext::CustomRaycast = [&](const FVector3&, const FVector3&, HavokHitResult&, uint64_t ignore) {
        seen = ignore;
        return false;
    };

    adapter.raycastWorld(Vec3{}, Vec3{100.f, 0.f, 0.f}, EntityId::LocalPlayer);
    EXPECT_EQ(seen, 555u);
    adapter.raycastWorld(Vec3{}, Vec3{100.f, 0.f, 0.f});
    EXPECT_EQ(seen, 0u); // default: ignore nothing
}

TEST(SekiroEntityTest, RaycastKeepsEntitiesAndBlockColliderSeparate) {
    ChrIns enemy;
    enemy.Handle = 900;
    SekiroAdapter adapter;
    adapter.registerEntity(EntityId{7}, &enemy);
    const uint64_t collider = adapter.createBlockCollider({0, 0, 0}, BlockId::Stone, Vec3{});

    RaycastScope scope;
    DantelionEngineContext::CustomRaycast = [&](const FVector3&, const FVector3&, HavokHitResult& out, uint64_t) {
        out.bHit = true;
        out.HitEntityHandle = 900;
        return true;
    };
    RaycastResult on_entity = adapter.raycastWorld(Vec3{}, Vec3{100.f, 0.f, 0.f});
    EXPECT_EQ(on_entity.hit_entity, EntityId{7});
    EXPECT_EQ(on_entity.hit_collider_handle, 0u);
    EXPECT_FALSE(on_entity.is_block);

    DantelionEngineContext::CustomRaycast = [&](const FVector3&, const FVector3&, HavokHitResult& out, uint64_t) {
        out.bHit = true;
        out.HitColliderHandle = collider;
        return true;
    };
    RaycastResult on_block = adapter.raycastWorld(Vec3{}, Vec3{100.f, 0.f, 0.f});
    EXPECT_EQ(on_block.hit_entity, EntityId::None);
    EXPECT_EQ(on_block.hit_collider_handle, collider);
    EXPECT_TRUE(on_block.is_block);

    DantelionEngineContext::CustomRaycast = [&](const FVector3&, const FVector3&, HavokHitResult& out, uint64_t) {
        out.bHit = true;
        out.HitEntityHandle = 31337; // not registered
        return true;
    };
    RaycastResult unknown = adapter.raycastWorld(Vec3{}, Vec3{100.f, 0.f, 0.f});
    EXPECT_EQ(unknown.hit_entity, EntityId::None); // never leak raw host handles as ids
}

TEST(SekiroEntityTest, PluginRejectsReservedEntityIds) {
    ChrIns player;
    ChrCam camera;
    ChrIns enemy;
    SekiroMod_Initialize(&player, &camera);
    EXPECT_FALSE(SekiroMod_RegisterEntity(0, &enemy));
    EXPECT_FALSE(SekiroMod_RegisterEntity(1, &enemy)); // reserved for the local player
    EXPECT_TRUE(SekiroMod_RegisterEntity(2, &enemy));
    SekiroMod_Shutdown();
}

// ---------------------------------------------------------------------------
// setLinearVelocity / elytra driving the player
// ---------------------------------------------------------------------------

TEST(SekiroAdapterTest, SetLinearVelocityReplacesInsteadOfAdding) {
    SekiroAdapter adapter;
    ChrIns e;
    e.Velocity = FVector3{1.f, 1.f, 1.f};
    adapter.registerEntity(EntityId{9}, &e);

    adapter.setLinearVelocity(EntityId{9}, Vec3{150.f, 250.f, 350.f});
    EXPECT_NEAR(e.Velocity.X, -250.f, 1e-3f); // MC left 250 = Dantelion right -250
    EXPECT_NEAR(e.Velocity.Y, 350.f, 1e-3f);
    EXPECT_NEAR(e.Velocity.Z, 150.f, 1e-3f);

    adapter.setLinearVelocity(EntityId{404}, Vec3{1.f, 2.f, 3.f}); // unknown: no-op, no crash
    adapter.setLinearVelocity(EntityId::None, Vec3{1.f, 2.f, 3.f});
    EXPECT_NEAR(e.Velocity.Z, 150.f, 1e-3f);
}

TEST(SekiroSessionTest, GlidingWritesVelocityToPlayerAndEstimatedGroundDoesNotCancelIt) {
    ChrIns player;
    player.Velocity = FVector3{0.f, -300.f, 400.f}; // falling (airborne by the vertical-speed estimate), moving forward
    ChrCam camera;
    camera.Position = FVector3{0.f, 160.f, 0.f};
    camera.Forward = FVector3{0.f, 0.f, 1.f};
    SekiroMod_Initialize(&player, &camera);
    SekiroMod_SetSteveMode(true);

    InputSnapshot jump;
    jump.glide_toggle = true;
    SekiroMod_Tick(0.05f, &jump);
    ASSERT_TRUE(SekiroMod_GetSession()->elytra().getState().is_gliding);

    const Vec3 mc_v = SekiroMod_GetSession()->elytra().getState().velocity;
    const FVector3 expected = SekiroAdapter::toNative(mc_v);
    EXPECT_NEAR(player.Velocity.X, expected.X, 1e-2f);
    EXPECT_NEAR(player.Velocity.Y, expected.Y, 1e-2f);
    EXPECT_NEAR(player.Velocity.Z, expected.Z, 1e-2f);

    // Elytra levels out: vertical speed is now tiny, so the heuristic reports "on ground".
    // The glide must survive (landing is detected by the swept raycast instead).
    player.Velocity.Y = 0.f;
    SekiroMod_Tick(0.05f, nullptr);
    EXPECT_TRUE(SekiroMod_GetSession()->elytra().getState().is_gliding);

    SekiroMod_Shutdown();
}
