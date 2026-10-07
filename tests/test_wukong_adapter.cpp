#include <gtest/gtest.h>
#include "wukong_adapter.hpp"
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
    EXPECT_EQ(hit.hit_collider_handle, handle);
    EXPECT_EQ(hit.hit_entity, EntityId::None);
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

    adapter.registerEntity(EntityId{101}, &enemy);
    // Canonical MC: Y = left. UE5: Y = right (left-handed), so the sign flips.
    adapter.applyLinearImpulse(EntityId{101}, Vec3{0.0f, 200.0f, 300.0f});

    EXPECT_NEAR(enemy.Velocity.Y, -200.0f, 1e-2f);
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

    adapter.registerEntity(EntityId{201}, &boss);

    EXPECT_FLOAT_EQ(adapter.getMaxHealth(EntityId{201}), 1000.0f);

    HitIntent hit{};
    hit.attacker_id = EntityId::None;
    hit.victim_id = EntityId{201};
    hit.damage = 150.0f;
    hit.knockback_vector = Vec3{0.0f, 1.0f, 0.0f};
    hit.knockback_force = 4.0f;

    bool hit_result = adapter.processHit(hit);
    EXPECT_TRUE(hit_result);
    EXPECT_FLOAT_EQ(boss.Health, 850.0f);
    EXPECT_EQ(boss.LastStaggerLevel, 0); // reaction is owned by CombatEngine::executeHit

    // Percentage hit for boss balance
    HitIntent percent_hit{};
    percent_hit.attacker_id = EntityId::None;
    percent_hit.victim_id = EntityId{201};
    percent_hit.max_hp_percent = 0.2f; // 20% of 1000 = 200
    EXPECT_TRUE(adapter.processHit(percent_hit));
    EXPECT_FLOAT_EQ(boss.Health, 650.0f);

    // Lethal hit
    HitIntent lethal_hit{};
    lethal_hit.victim_id = EntityId{201};
    lethal_hit.damage = 700.0f;
    EXPECT_TRUE(adapter.processHit(lethal_hit));
    EXPECT_FLOAT_EQ(boss.Health, 0.0f);
    EXPECT_TRUE(boss.bIsDead);

    // Hitting dead unit should fail
    EXPECT_FALSE(adapter.processHit(hit));
}

TEST(WukongAdapterTest, ProcessHitDoesNotReactAndExecuteHitKnocksBackOnce) {
    WukongAdapter adapter;
    CombatEngine combat(adapter);
    ABGUCharacter enemy;
    enemy.Health = 1000.0f;
    enemy.MaxHealth = 1000.0f;
    adapter.registerEntity(EntityId{5}, &enemy);

    HitIntent hit{};
    hit.victim_id = EntityId{5};
    hit.damage = 100.0f;
    hit.knockback_vector = Vec3{1.0f, 0.0f, 0.0f};
    hit.knockback_force = 300.0f; // cm/s, same unit as projectile speeds

    EXPECT_TRUE(adapter.processHit(hit));
    EXPECT_EQ(enemy.LastStaggerLevel, 0);
    EXPECT_FLOAT_EQ(enemy.Velocity.Size(), 0.0f);

    EXPECT_TRUE(combat.executeHit(hit));
    EXPECT_NEAR(enemy.Velocity.X, 300.0f, 1e-2f); // applied once (not 600), and not scaled by 100
    EXPECT_GT(enemy.LastStaggerLevel, 0);
}

TEST(WukongAdapterTest, InputStateQueries) {
    APlayerController controller;
    APlayerCameraManager camera;
    ABGUPlayerCharacter player;

    camera.CameraLocation = FVector{10.0f, 20.0f, 180.0f};
    camera.CameraRotation = FRotator{0.0f, 90.0f, 0.0f}; // UE yaw +90 = turned to the right (+Y)
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
    EXPECT_FLOAT_EQ(cam_pos.y, -20.0f); // UE +Y (right) is MC -Y (left)
    EXPECT_FLOAT_EQ(cam_pos.z, 180.0f);

    Vec3 cam_fwd = adapter.getCameraForward();
    EXPECT_NEAR(cam_fwd.x, 0.0f, 1e-4f);
    EXPECT_NEAR(cam_fwd.y, -1.0f, 1e-4f); // turned right = MC -Y

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

// ---------------------------------------------------------------------------
// Canonical coordinate mapping (UE5: X=forward, Y=right, Z=up, left-handed  <->  MC: X=forward, Y=left, Z=up)
// ---------------------------------------------------------------------------

TEST(WukongCoordinateTest, YAxisIsMirroredToCanonicalMcSpace) {
    Vec3 right = WukongAdapter::toMc(FVector{0.f, 1.f, 0.f});
    EXPECT_FLOAT_EQ(right.x, 0.f);
    EXPECT_FLOAT_EQ(right.y, -1.f);
    EXPECT_FLOAT_EQ(right.z, 0.f);

    Vec3 fwd = WukongAdapter::toMc(FVector{1.f, 0.f, 0.f});
    EXPECT_FLOAT_EQ(fwd.x, 1.f);
    Vec3 up = WukongAdapter::toMc(FVector{0.f, 0.f, 1.f});
    EXPECT_FLOAT_EQ(up.z, 1.f);

    const Vec3 v{12.f, -34.f, 56.f};
    const Vec3 back = WukongAdapter::toMc(WukongAdapter::toNative(v));
    EXPECT_FLOAT_EQ(back.x, v.x);
    EXPECT_FLOAT_EQ(back.y, v.y);
    EXPECT_FLOAT_EQ(back.z, v.z);
}

namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;

std::array<float, 3> rotateByQuat(float qx, float qy, float qz, float qw, const std::array<float, 3>& v) {
    const float tx = 2.f * (qy * v[2] - qz * v[1]);
    const float ty = 2.f * (qz * v[0] - qx * v[2]);
    const float tz = 2.f * (qx * v[1] - qy * v[0]);
    return {v[0] + qw * tx + (qy * tz - qz * ty),
            v[1] + qw * ty + (qz * tx - qx * tz),
            v[2] + qw * tz + (qx * ty - qy * tx)};
}

// UE5 FRotator::Quaternion() reference formula
std::array<float, 4> ueRotatorToQuat(const FRotator& r) {
    const float sp = std::sin(r.Pitch * kDegToRad * 0.5f), cp = std::cos(r.Pitch * kDegToRad * 0.5f);
    const float sy = std::sin(r.Yaw * kDegToRad * 0.5f), cy = std::cos(r.Yaw * kDegToRad * 0.5f);
    const float sr = std::sin(r.Roll * kDegToRad * 0.5f), cr = std::cos(r.Roll * kDegToRad * 0.5f);
    return {cr * sp * sy - sr * cp * cy,
            -cr * sp * cy - sr * cp * sy,
            cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy};
}

} // namespace

TEST(WukongCoordinateTest, YawLeftInMcIsNegativeUeYaw) {
    const float half = 30.f * kDegToRad * 0.5f;
    const FRotator r = WukongAdapter::toNativeRotator(Quat{0.f, 0.f, std::sin(half), std::cos(half)});
    EXPECT_NEAR(r.Yaw, -30.f, 1e-3f);
    EXPECT_NEAR(r.Pitch, 0.f, 1e-3f);
    EXPECT_NEAR(r.Roll, 0.f, 1e-3f);
}

TEST(WukongCoordinateTest, LookingDownInMcIsNegativeUePitch) {
    // RH rotation about +Y (left) by +20deg tips the forward vector downward.
    const float half = 20.f * kDegToRad * 0.5f;
    const FRotator r = WukongAdapter::toNativeRotator(Quat{0.f, std::sin(half), 0.f, std::cos(half)});
    EXPECT_NEAR(r.Pitch, -20.f, 1e-3f);
    EXPECT_NEAR(r.Yaw, 0.f, 1e-3f);
}

TEST(WukongCoordinateTest, RotatorConversionPreservesPhysicalRotation) {
    const Quat cases[] = {
        {0.f, 0.f, std::sin(0.7f), std::cos(0.7f)},
        {std::sin(0.4f), 0.f, 0.f, std::cos(0.4f)},
        {0.f, std::sin(0.3f), 0.f, std::cos(0.3f)},
        {0.18257419f, 0.36514837f, 0.54772256f, 0.73029674f},
    };
    const Vec3 vs[] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {3.f, -2.f, 5.f}};

    for (const Quat& q : cases) {
        const FRotator rot = WukongAdapter::toNativeRotator(q);
        const auto uq = ueRotatorToQuat(rot);
        for (const Vec3& v : vs) {
            const auto mc_out = rotateByQuat(q.x, q.y, q.z, q.w, {v.x, v.y, v.z});
            const FVector expected = WukongAdapter::toNative(Vec3{mc_out[0], mc_out[1], mc_out[2]});
            const FVector nv = WukongAdapter::toNative(v);
            const auto got = rotateByQuat(uq[0], uq[1], uq[2], uq[3], {nv.X, nv.Y, nv.Z});
            EXPECT_NEAR(got[0], expected.X, 1e-3f);
            EXPECT_NEAR(got[1], expected.Y, 1e-3f);
            EXPECT_NEAR(got[2], expected.Z, 1e-3f);
        }
    }
}

TEST(WukongCoordinateTest, SteveParts_ReceiveConvertedRotator) {
    APlayerController controller;
    ABGUPlayerCharacter player;
    USceneComponent root;
    player.RootComponent = &root;
    controller.ControlledPawn = &player;
    WukongAdapter adapter(&controller);
    ASSERT_TRUE(adapter.spawnSteveParts());

    SteveAnimator::PartTransforms transforms{};
    const float half = 30.f * kDegToRad * 0.5f;
    const Quat q{0.f, 0.f, std::sin(half), std::cos(half)};
    transforms[static_cast<size_t>(StevePart::Head)].rot = q;
    transforms[static_cast<size_t>(StevePart::Head)].pos = Vec3{0.f, 10.f, 170.f};
    adapter.updateStevePartTransforms(transforms);

    const auto* head = adapter.getStevePartComponent(StevePart::Head);
    ASSERT_NE(head, nullptr);
    EXPECT_NEAR(head->RelativeRotation.Yaw, -30.f, 1e-3f);
    EXPECT_NEAR(head->RelativeLocation.Y, -10.f, 1e-3f);
}

TEST(WukongAdapterTest, OnGroundHeuristicUsesVerticalVelocity) {
    APlayerController controller;
    ABGUPlayerCharacter player;
    controller.ControlledPawn = &player;
    WukongAdapter adapter(&controller);

    player.Velocity = FVector{300.f, 300.f, 0.f};
    EXPECT_TRUE(adapter.isPlayerOnGround());
    player.Velocity = FVector{0.f, 0.f, 250.f};
    EXPECT_FALSE(adapter.isPlayerOnGround());
    player.Velocity = FVector{0.f, 0.f, -250.f};
    EXPECT_FALSE(adapter.isPlayerOnGround());
    EXPECT_TRUE(WukongAdapter().isPlayerOnGround());
}

// ---------------------------------------------------------------------------
// Session wired through the real WukongAdapter
// ---------------------------------------------------------------------------

extern "C" {
void WukongMod_Initialize(b1::native::APlayerController* controller);
void WukongMod_Shutdown();
void WukongMod_SetSteveMode(bool active);
bool WukongMod_IsSteveModeActive();
void WukongMod_Tick(float delta_time, const mc::InputSnapshot* input);
bool WukongMod_RegisterEntity(uint64_t entity_id, b1::native::ABGUCharacter* entity);
void WukongMod_UnregisterEntity(uint64_t entity_id);
mc::Session* WukongMod_GetSession();
}

namespace {

struct TraceScope {
    ~TraceScope() { UKismetSystemLibrary::CustomLineTrace = nullptr; }
};

struct WukongRig {
    APlayerController controller;
    APlayerCameraManager camera;
    ABGUPlayerCharacter player;
    USceneComponent root;
    USceneComponent mesh;

    WukongRig() {
        player.RootComponent = &root;
        player.MeshComponent = &mesh;
        camera.CameraLocation = FVector{0.f, 0.f, 160.f};
        camera.CameraRotation = FRotator{0.f, 0.f, 0.f};
        controller.PlayerCameraManager = &camera;
        controller.ControlledPawn = &player;
    }
};

} // namespace

TEST(WukongSessionTest, LifecycleHidesAndRestoresNativeMesh) {
    WukongRig rig;
    WukongMod_Initialize(&rig.controller);
    EXPECT_FALSE(WukongMod_IsSteveModeActive());

    WukongMod_SetSteveMode(true);
    EXPECT_TRUE(WukongMod_IsSteveModeActive());
    EXPECT_TRUE(rig.mesh.bHiddenInGame);

    WukongMod_Tick(0.05f, nullptr);

    WukongMod_Shutdown(); // active session must restore the native mesh on teardown
    EXPECT_FALSE(rig.mesh.bHiddenInGame);
    EXPECT_EQ(WukongMod_GetSession(), nullptr);
}

TEST(WukongSessionTest, UsePlacesBlockAndTraceFollowsMirroredYAxis) {
    WukongRig rig;
    rig.camera.CameraRotation = FRotator{0.f, 90.f, 0.f}; // turned right => MC forward = -Y
    WukongMod_Initialize(&rig.controller);
    ASSERT_NE(WukongMod_GetSession(), nullptr);

    FVector seen_start{}, seen_end{};
    TraceScope scope;
    UKismetSystemLibrary::CustomLineTrace = [&](UObject*, const FVector& s, const FVector& e, ETraceTypeQuery, bool,
                                                const std::vector<AActor*>&, EDrawDebugTrace, FHitResult& out, bool) {
        seen_start = s;
        seen_end = e;
        out.bBlockingHit = true;
        // MC surface point (0,-250,100) facing +Y(MC) toward the player => UE (0, 250, 100), normal UE (0,-1,0)
        out.ImpactPoint = WukongAdapter::toNative(Vec3{0.f, -250.f, 100.f});
        out.ImpactNormal = WukongAdapter::toNative(Vec3{0.f, 1.f, 0.f});
        return true;
    };

    WukongMod_SetSteveMode(true);
    InputSnapshot in;
    in.hotbar_select = 2; // default loadout: 64x dirt
    in.use_pressed = true;
    WukongMod_Tick(0.05f, &in);

    EXPECT_NEAR(seen_start.Z, 160.f, 1e-3f);
    EXPECT_NEAR(seen_end.Y, Session::kReachCm, 1e-2f); // UE +Y, because the camera turned right
    EXPECT_NEAR(seen_end.X, 0.f, 1e-2f);

    // point(-250) + normal(+1)*50 = -200 -> grid y = -2
    const VoxelBlock* b = WukongMod_GetSession()->voxel().getBlock({0, -2, 1});
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->id, BlockId::Dirt);

    WukongMod_Shutdown();
}

TEST(WukongSessionTest, AttackDamagesRegisteredEnemy) {
    WukongRig rig;
    ABGUCharacter enemy;
    enemy.Health = 1000.f;
    enemy.MaxHealth = 1000.f;

    WukongMod_Initialize(&rig.controller);
    EXPECT_TRUE(WukongMod_RegisterEntity(7, &enemy));

    TraceScope scope;
    UKismetSystemLibrary::CustomLineTrace = [&](UObject*, const FVector&, const FVector&, ETraceTypeQuery, bool,
                                                const std::vector<AActor*>&, EDrawDebugTrace, FHitResult& out, bool) {
        out.bBlockingHit = true;
        out.Actor = &enemy;
        out.ImpactPoint = FVector{250.f, 0.f, 150.f};
        out.ImpactNormal = FVector{-1.f, 0.f, 0.f};
        return true;
    };

    WukongMod_SetSteveMode(true);
    InputSnapshot in;
    in.attack_pressed = true; // default loadout slot 0 = diamond sword, cooldown starts full
    WukongMod_Tick(0.05f, &in);

    EXPECT_FLOAT_EQ(enemy.Health, 950.f);
    EXPECT_NEAR(enemy.Velocity.X, 800.f, 1e-2f); // sword knockback 800 cm/s, one impulse along view (UE +X)
    EXPECT_GT(WukongMod_GetSession()->lastAnimInput().swing_progress, 0.f);

    WukongMod_UnregisterEntity(7);
    WukongMod_Shutdown();
}

// ---------------------------------------------------------------------------
// Entity identity
// ---------------------------------------------------------------------------

TEST(WukongEntityTest, PawnIsRegisteredAsLocalPlayerAndCanBeHit) {
    APlayerController controller;
    ABGUPlayerCharacter player;
    player.Health = 100.f;
    player.MaxHealth = 100.f;
    controller.ControlledPawn = &player;
    WukongAdapter adapter(&controller);

    EXPECT_FLOAT_EQ(adapter.getMaxHealth(EntityId::LocalPlayer), 100.f);
    HitIntent fall{};
    fall.victim_id = EntityId::LocalPlayer;
    fall.damage = 10.f;
    EXPECT_TRUE(adapter.processHit(fall));
    EXPECT_FLOAT_EQ(player.Health, 90.f);
}

TEST(WukongEntityTest, RegisterRejectsNoEntity) {
    WukongAdapter adapter;
    ABGUCharacter e;
    EXPECT_FALSE(adapter.registerEntity(EntityId::None, &e));
    EXPECT_TRUE(adapter.registerEntity(EntityId{5}, &e));
}

TEST(WukongEntityTest, IgnoreEntityMapsToTraceIgnoreList) {
    APlayerController controller;
    ABGUPlayerCharacter player;
    controller.ControlledPawn = &player;
    WukongAdapter adapter(&controller);

    size_t ignored = 99;
    AActor* first_ignored = nullptr;
    TraceScope scope;
    UKismetSystemLibrary::CustomLineTrace = [&](UObject*, const FVector&, const FVector&, ETraceTypeQuery, bool,
                                                const std::vector<AActor*>& ignore, EDrawDebugTrace, FHitResult&, bool) {
        ignored = ignore.size();
        first_ignored = ignore.empty() ? nullptr : ignore.front();
        return false;
    };
    adapter.raycastWorld(Vec3{}, Vec3{100.f, 0.f, 0.f}, EntityId::LocalPlayer);
    EXPECT_EQ(ignored, 1u);
    EXPECT_EQ(first_ignored, &player);
    adapter.raycastWorld(Vec3{}, Vec3{100.f, 0.f, 0.f});
    EXPECT_EQ(ignored, 0u);
}

TEST(WukongEntityTest, RaycastKeepsEntitiesAndBlockColliderSeparate) {
    ABGUCharacter enemy;
    ABGUCharacter stranger;
    WukongAdapter adapter;
    adapter.registerEntity(EntityId{7}, &enemy);

    TraceScope scope;
    AActor* hit_actor = &enemy;
    UKismetSystemLibrary::CustomLineTrace = [&](UObject*, const FVector&, const FVector&, ETraceTypeQuery, bool,
                                                const std::vector<AActor*>&, EDrawDebugTrace, FHitResult& out, bool) {
        out.bBlockingHit = true;
        out.Actor = hit_actor;
        return true;
    };
    RaycastResult on_entity = adapter.raycastWorld(Vec3{}, Vec3{100.f, 0.f, 0.f});
    EXPECT_EQ(on_entity.hit_entity, EntityId{7});
    EXPECT_EQ(on_entity.hit_collider_handle, 0u);

    hit_actor = &stranger; // unregistered actor: must not leak its pointer as an id
    RaycastResult unknown = adapter.raycastWorld(Vec3{}, Vec3{100.f, 0.f, 0.f});
    EXPECT_EQ(unknown.hit_entity, EntityId::None);
    EXPECT_TRUE(unknown.has_hit);
}

TEST(WukongEntityTest, PluginRejectsReservedEntityIds) {
    WukongRig rig;
    ABGUCharacter enemy;
    WukongMod_Initialize(&rig.controller);
    EXPECT_FALSE(WukongMod_RegisterEntity(0, &enemy));
    EXPECT_FALSE(WukongMod_RegisterEntity(1, &enemy));
    EXPECT_TRUE(WukongMod_RegisterEntity(2, &enemy));
    WukongMod_Shutdown();
}

// ---------------------------------------------------------------------------
// setLinearVelocity / elytra driving the player
// ---------------------------------------------------------------------------

TEST(WukongAdapterTest, SetLinearVelocityReplacesInsteadOfAdding) {
    WukongAdapter adapter;
    ABGUCharacter e;
    e.Velocity = FVector{1.f, 1.f, 1.f};
    adapter.registerEntity(EntityId{9}, &e);

    adapter.setLinearVelocity(EntityId{9}, Vec3{150.f, 250.f, 350.f});
    EXPECT_NEAR(e.Velocity.X, 150.f, 1e-3f);
    EXPECT_NEAR(e.Velocity.Y, -250.f, 1e-3f); // MC left 250 = UE right -250
    EXPECT_NEAR(e.Velocity.Z, 350.f, 1e-3f);

    adapter.setLinearVelocity(EntityId{404}, Vec3{1.f, 2.f, 3.f});
    adapter.setLinearVelocity(EntityId::None, Vec3{1.f, 2.f, 3.f});
    EXPECT_NEAR(e.Velocity.X, 150.f, 1e-3f);
}

TEST(WukongAdapterTest, ImpulseStillAddsWhileLaunchOverrideReplaces) {
    ABGUCharacter c;
    c.Velocity = FVector{10.f, 10.f, 10.f};
    c.LaunchCharacter(FVector{5.f, 5.f, 5.f}, false, false);
    EXPECT_FLOAT_EQ(c.Velocity.X, 15.f);
    c.LaunchCharacter(FVector{1.f, 2.f, 3.f}, true, true);
    EXPECT_FLOAT_EQ(c.Velocity.X, 1.f);
    EXPECT_FLOAT_EQ(c.Velocity.Y, 2.f);
    EXPECT_FLOAT_EQ(c.Velocity.Z, 3.f);
    c.LaunchCharacter(FVector{9.f, 9.f, 9.f}, true, false); // XY replaced, Z added
    EXPECT_FLOAT_EQ(c.Velocity.X, 9.f);
    EXPECT_FLOAT_EQ(c.Velocity.Z, 12.f);
}

TEST(WukongSessionTest, GlidingWritesVelocityToPlayerAndEstimatedGroundDoesNotCancelIt) {
    WukongRig rig;
    rig.player.Velocity = FVector{400.f, 0.f, -300.f};
    WukongMod_Initialize(&rig.controller);
    WukongMod_SetSteveMode(true);

    InputSnapshot jump;
    jump.glide_toggle = true;
    WukongMod_Tick(0.05f, &jump);
    ASSERT_TRUE(WukongMod_GetSession()->elytra().getState().is_gliding);

    const FVector expected = WukongAdapter::toNative(WukongMod_GetSession()->elytra().getState().velocity);
    EXPECT_NEAR(rig.player.Velocity.X, expected.X, 1e-2f);
    EXPECT_NEAR(rig.player.Velocity.Y, expected.Y, 1e-2f);
    EXPECT_NEAR(rig.player.Velocity.Z, expected.Z, 1e-2f);

    rig.player.Velocity.Z = 0.f; // heuristic now says "grounded"
    WukongMod_Tick(0.05f, nullptr);
    EXPECT_TRUE(WukongMod_GetSession()->elytra().getState().is_gliding);

    WukongMod_Shutdown();
}

TEST(WukongAdapterTest, SteveRootIsCentimetresInUeSpaceAndYawIsPositiveToTheRight) {
    WukongAdapter adapter;
    adapter.setSteveRoot(Vec3{1000.f, -200.f, 0.f}, 3.14159265f * 0.5f); // 2 m to the right, facing left
    const auto& root = adapter.getSteveRoot();
    EXPECT_NEAR(root.position.X, 1000.f, 1e-3f);
    EXPECT_NEAR(root.position.Y, 200.f, 1e-3f);
    EXPECT_NEAR(root.yaw_degrees, -90.f, 1e-3f);
}
