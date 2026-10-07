#include <gtest/gtest.h>

#include "mc/ballistics.hpp"
#include "mc/combat.hpp"
#include "mc/consumables.hpp"
#include "mc/contracts/combat_adapter.hpp"
#include "mc/contracts/input_adapter.hpp"
#include "mc/contracts/physics_adapter.hpp"
#include "mc/contracts/render_adapter.hpp"
#include "mc/elytra.hpp"
#include "mc/hud.hpp"
#include "mc/contracts/host_gameplay.hpp"
#include "mc/session.hpp"
#include "mc/voxel_world.hpp"

#include <cmath>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979f;

class MockPhysics : public mc::IPhysicsAdapter {
public:
    mc::RaycastResult next_hit{};
    int raycast_calls{0};
    mc::Vec3 last_start{};
    mc::Vec3 last_end{};
    int colliders{0};
    int velocity_writes{0};
    mc::EntityId last_velocity_target{mc::EntityId::None};
    mc::Vec3 last_velocity{};

    mc::EntityId last_ignore{mc::EntityId::None};

    mc::RaycastResult raycastWorld(const mc::Vec3& s, const mc::Vec3& e, mc::EntityId ignore) override {
        ++raycast_calls;
        last_ignore = ignore;
        last_start = s;
        last_end = e;
        return next_hit;
    }
    uint64_t createBlockCollider(const mc::GridPos&, mc::BlockId, const mc::Vec3&) override {
        return static_cast<uint64_t>(++colliders);
    }
    void destroyBlockCollider(uint64_t) override { --colliders; }
    void applyLinearImpulse(mc::EntityId, const mc::Vec3&) override {}
    void setLinearVelocity(mc::EntityId id, const mc::Vec3& v) override {
        ++velocity_writes;
        last_velocity_target = id;
        last_velocity = v;
    }
};

class MockRender : public mc::IRenderAdapter {
public:
    std::vector<bool> native_visible_calls;
    int spawn_steve_calls{0};
    int destroy_steve_calls{0};
    int transform_updates{0};
    int root_updates{0};
    mc::Vec3 root_pos{};
    float root_yaw{0.f};
    mc::ItemId held{mc::ItemId::None};
    int last_crack_stage{-1};
    int visuals{0};

    void setNativePlayerVisible(bool v) override { native_visible_calls.push_back(v); }
    bool spawnSteveParts() override { ++spawn_steve_calls; return true; }
    void destroySteveParts() override { ++destroy_steve_calls; }
    void setSteveRoot(const mc::Vec3& p, float yaw) override {
        ++root_updates;
        root_pos = p;
        root_yaw = yaw;
    }
    void updateStevePartTransforms(const mc::SteveAnimator::PartTransforms&) override { ++transform_updates; }
    void setHeldItemVisual(mc::ItemId item, bool offhand) override {
        if (!offhand) held = item;
    }
    uint64_t spawnBlockVisual(const mc::GridPos&, mc::BlockId, const mc::Vec3&) override {
        return static_cast<uint64_t>(++visuals);
    }
    void setBlockCrackStage(uint64_t, int stage) override { last_crack_stage = stage; }
    void destroyBlockVisual(uint64_t) override { --visuals; }
};

class MockCombat : public mc::ICombatAdapter {
public:
    std::vector<mc::HitIntent> hits;
    int stagger_calls{0};

    bool processHit(const mc::HitIntent& i) override { hits.push_back(i); return true; }
    float getMaxHealth(mc::EntityId) override { return 100.f; }
    void triggerStaggerOrRagdoll(mc::EntityId, const mc::Vec3&, float) override { ++stagger_calls; }
};

class MockInput : public mc::IInputAdapter {
public:
    mc::Vec3 player_pos{0.f, 0.f, 0.f};
    mc::Vec3 player_vel{0.f, 0.f, 0.f};
    mc::Vec3 cam_pos{0.f, 0.f, 160.f};
    mc::Vec3 cam_fwd{1.f, 0.f, 0.f};
    bool has_facing{false};
    float facing_yaw{0.f};
    bool has_eye{false};
    mc::Vec3 eye{};
    mc::Vec3 getEyePosition() const override { return has_eye ? eye : cam_pos; }

    bool getPlayerFacingYaw(float& out) const override {
        if (has_facing) out = facing_yaw;
        return has_facing;
    }
    mc::ItemId getEquippedMainHand() const override { return mc::ItemId::None; }
    mc::ItemId getEquippedOffHand() const override { return mc::ItemId::None; }
    mc::Vec3 getCameraPosition() const override { return cam_pos; }
    mc::Vec3 getCameraForward() const override { return cam_fwd; }
    mc::Vec3 getPlayerPosition() const override { return player_pos; }
    mc::Vec3 getPlayerVelocity() const override { return player_vel; }
};

class SessionTest : public ::testing::Test {
protected:
    MockPhysics physics;
    MockRender render;
    MockCombat combat;
    MockInput input;
    mc::Session session{mc::Ports{physics, render, combat, input}};

    void SetUp() override {
        session.hud().setSlot(0, mc::ItemId::DiamondSword, 1);
        session.hud().setSlot(1, mc::ItemId::DiamondPickaxe, 1);
        session.hud().setSlot(2, mc::ItemId::BlockStone, 64);
        session.hud().setSlot(3, mc::ItemId::Bread, 2);
        session.hud().setSlot(4, mc::ItemId::Bow, 1);
        session.hud().setSlot(5, mc::ItemId::FireworkRocket, 3);
        session.hud().selectSlot(0);
    }

    void select(int slot) {
        mc::InputSnapshot in;
        in.hotbar_select = slot;
        session.tick(0.f, in);
    }

    // Surface hit on the -X face of the voxel at grid (3,0,1).
    static mc::RaycastResult faceHit() {
        mc::RaycastResult r;
        r.has_hit = true;
        r.point = {250.f, 0.f, 100.f};
        r.normal = {-1.f, 0.f, 0.f};
        r.is_block = true;
        return r;
    }

    static mc::RaycastResult entityHit(uint64_t id) {
        mc::RaycastResult r;
        r.has_hit = true;
        r.point = {250.f, 0.f, 150.f};
        r.normal = {-1.f, 0.f, 0.f};
        r.hit_entity = mc::EntityId{id};
        r.is_block = false;
        return r;
    }
};

} // namespace

// ---------------------------------------------------------------- lifecycle

TEST_F(SessionTest, InactiveTickDoesNothing) {
    mc::InputSnapshot in;
    in.use_pressed = true;
    in.attack_pressed = true;
    session.tick(0.1f, in);

    EXPECT_FALSE(session.isActive());
    EXPECT_EQ(render.transform_updates, 0);
    EXPECT_EQ(physics.raycast_calls, 0);
    EXPECT_TRUE(combat.hits.empty());
}

TEST_F(SessionTest, ActivateHidesNativeAndSpawnsSteve_DeactivateRestores) {
    session.setActive(true);
    EXPECT_TRUE(session.isActive());
    ASSERT_EQ(render.native_visible_calls.size(), 1u);
    EXPECT_FALSE(render.native_visible_calls.back());
    EXPECT_EQ(render.spawn_steve_calls, 1);

    session.setActive(true); // idempotent
    EXPECT_EQ(render.spawn_steve_calls, 1);

    session.setActive(false);
    EXPECT_FALSE(session.isActive());
    EXPECT_TRUE(render.native_visible_calls.back());
    EXPECT_EQ(render.destroy_steve_calls, 1);
}

TEST(SessionLifetime, DestructorWhileActiveRestoresNativePlayer) {
    MockPhysics physics;
    MockRender render;
    MockCombat combat;
    MockInput input;
    {
        mc::Session s{mc::Ports{physics, render, combat, input}};
        s.setActive(true);
    }
    EXPECT_TRUE(render.native_visible_calls.back());
    EXPECT_EQ(render.destroy_steve_calls, 1);
}

// ---------------------------------------------------------------- animation

TEST_F(SessionTest, ActiveTickPushesTransformsAndCanonicalAnimInput) {
    session.setActive(true);
    input.cam_fwd = {0.f, 1.f, 0.f}; // looking +Y => yaw = +90deg
    input.player_vel = {0.f, 200.f, 0.f}; // 2 m/s along view
    session.tick(0.05f, {});

    EXPECT_EQ(render.transform_updates, 1);
    const auto& a = session.lastAnimInput();
    EXPECT_NEAR(render.root_yaw, kPi / 2.f, 1e-4f); // the body takes the camera's heading on the first tick
    EXPECT_NEAR(a.look_yaw, 0.f, 1e-4f);            // head yaw is relative to the body
    EXPECT_NEAR(a.look_pitch, 0.f, 1e-4f);
    EXPECT_NEAR(a.forward_speed, 2.f, 1e-3f);
    EXPECT_NEAR(a.strafe_speed, 0.f, 1e-3f);
}

TEST_F(SessionTest, StrafeSpeedIsPositiveToTheRightOfView) {
    session.setActive(true);
    input.cam_fwd = {1.f, 0.f, 0.f};
    input.player_vel = {0.f, -100.f, 0.f}; // -Y is right when facing +X (Z-up, RH)
    session.tick(0.05f, {});
    EXPECT_NEAR(session.lastAnimInput().strafe_speed, 1.f, 1e-3f);
    EXPECT_NEAR(session.lastAnimInput().forward_speed, 0.f, 1e-3f);
}

TEST_F(SessionTest, AttackPressedDrivesSwingProgressThenIdles) {
    session.setActive(true);
    mc::InputSnapshot in;
    in.attack_pressed = true;
    session.tick(0.1f, in);
    EXPECT_NEAR(session.lastAnimInput().swing_progress, 0.1f / mc::Session::kSwingDurationSec, 0.01f);

    session.tick(0.1f, {});
    EXPECT_GT(session.lastAnimInput().swing_progress, 0.5f);

    session.tick(0.3f, {});
    EXPECT_FLOAT_EQ(session.lastAnimInput().swing_progress, 0.f);
}

// ---------------------------------------------------------------- hotbar

TEST_F(SessionTest, HotbarSelectAndScrollSyncHudAndHeldVisual) {
    session.setActive(true);
    select(2);
    EXPECT_EQ(session.hud().getSelectedSlot(), 2);
    EXPECT_EQ(render.held, mc::ItemId::BlockStone);

    mc::InputSnapshot in;
    in.scroll = 1;
    session.tick(0.f, in);
    EXPECT_EQ(session.hud().getSelectedSlot(), 3);
    EXPECT_EQ(render.held, mc::ItemId::Bread);
}

// ---------------------------------------------------------------- voxel

TEST_F(SessionTest, UsePlacesSelectedBlockAtRaycastSurfaceAndConsumesOne) {
    session.setActive(true);
    select(2);
    physics.next_hit = faceHit();
    mc::InputSnapshot in;
    in.use_pressed = true;
    session.tick(0.05f, in);

    // Ray comes from the camera along its forward vector, limited to reach.
    EXPECT_FLOAT_EQ(physics.last_start.z, 160.f);
    EXPECT_NEAR(physics.last_end.x, mc::Session::kReachCm, 1e-2f);

    // point(250) + normal(-1) * 50 = 200 -> grid x = 2
    const mc::VoxelBlock* b = session.voxel().getBlock({2, 0, 1});
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->id, mc::BlockId::Stone);
    EXPECT_EQ(session.hud().getSlot(2).count, 63u);
}

TEST_F(SessionTest, UseWithNoHitOrEmptyStackPlacesNothing) {
    session.setActive(true);
    select(2);
    mc::InputSnapshot in;
    in.use_pressed = true;

    physics.next_hit = {}; // miss
    session.tick(0.05f, in);
    EXPECT_EQ(session.voxel().getActiveBlockCount(), 0u);
    EXPECT_EQ(session.hud().getSlot(2).count, 64u);

    physics.next_hit = faceHit();
    session.hud().setSlot(2, mc::ItemId::BlockStone, 0);
    session.tick(0.05f, in);
    EXPECT_EQ(session.voxel().getActiveBlockCount(), 0u);
}

TEST_F(SessionTest, PlacementRejectedInsidePlayerDoesNotConsumeItem) {
    session.setActive(true);
    select(2);
    input.player_pos = {200.f, 0.f, 100.f}; // standing inside target voxel (2,0,1)
    physics.next_hit = faceHit();
    mc::InputSnapshot in;
    in.use_pressed = true;
    session.tick(0.05f, in);

    EXPECT_EQ(session.voxel().getActiveBlockCount(), 0u);
    EXPECT_EQ(session.hud().getSlot(2).count, 64u);
}

TEST_F(SessionTest, HoldingAttackMinesVoxelUntilBroken) {
    session.setActive(true);
    ASSERT_TRUE(session.voxel().placeBlock({3, 0, 1}, mc::BlockId::Dirt, input.player_pos));
    select(1); // pickaxe
    physics.next_hit = faceHit(); // point(250) - normal(-1)*50 = 300 -> grid x = 3

    mc::InputSnapshot in;
    in.attack_held = true;
    session.tick(0.1f, in);
    EXPECT_GE(render.last_crack_stage, 0);
    EXPECT_NE(session.voxel().getBlock({3, 0, 1}), nullptr);

    for (int i = 0; i < 8; ++i) {
        session.tick(0.1f, in); // dirt = 0.75s
    }
    EXPECT_EQ(session.voxel().getBlock({3, 0, 1}), nullptr);
}

// ---------------------------------------------------------------- combat

TEST_F(SessionTest, AttackOnEntityEmitsFullChargeSwordHit) {
    session.setActive(true);
    physics.next_hit = entityHit(7);
    mc::InputSnapshot in;
    in.attack_pressed = true;
    session.tick(0.05f, in);

    ASSERT_EQ(combat.hits.size(), 1u);
    EXPECT_EQ(combat.hits[0].victim_id, mc::EntityId{7});
    EXPECT_EQ(combat.hits[0].attacker_id, mc::EntityId::LocalPlayer);
    EXPECT_FLOAT_EQ(combat.hits[0].damage, 7.f);
    EXPECT_EQ(combat.stagger_calls, 1);
}

TEST_F(SessionTest, SpamAttackIsScaledByCooldownAndRechargesOverTime) {
    session.setActive(true);
    physics.next_hit = entityHit(7);
    mc::InputSnapshot in;
    in.attack_pressed = true;
    session.tick(0.01f, in);
    session.tick(0.01f, in);
    ASSERT_EQ(combat.hits.size(), 2u);
    EXPECT_LT(combat.hits[1].damage, 3.f);

    session.tick(mc::Session::kAttackRechargeSec + 0.05f, {});
    session.tick(0.01f, in);
    ASSERT_EQ(combat.hits.size(), 3u);
    EXPECT_FLOAT_EQ(combat.hits[2].damage, 7.f);
}

TEST_F(SessionTest, FallingAttackIsCritical) {
    session.setActive(true);
    physics.next_hit = entityHit(7);
    input.player_vel = {0.f, 0.f, -300.f};
    mc::InputSnapshot in;
    in.attack_pressed = true;
    in.on_ground = false;
    session.tick(0.05f, in);

    ASSERT_EQ(combat.hits.size(), 1u);
    EXPECT_TRUE(combat.hits[0].is_critical);
    EXPECT_FLOAT_EQ(combat.hits[0].damage, 10.5f);
}

TEST_F(SessionTest, AttackOnOwnVoxelNeverReachesCombat) {
    session.setActive(true);
    ASSERT_TRUE(session.voxel().placeBlock({3, 0, 1}, mc::BlockId::Dirt, input.player_pos));
    physics.next_hit = faceHit();
    mc::InputSnapshot in;
    in.attack_pressed = true;
    session.tick(0.05f, in);
    EXPECT_TRUE(combat.hits.empty());
}

// ---------------------------------------------------------------- consumables

TEST_F(SessionTest, UseOnFoodStartsEatingAndCompletionConsumesAndHeals) {
    session.setActive(true);
    session.hud().setHealth(10.f);
    select(3); // bread x2
    mc::InputSnapshot in;
    in.use_pressed = true;
    session.tick(0.05f, in);
    EXPECT_TRUE(session.consumables().isEating());
    EXPECT_GT(session.lastAnimInput().eating_progress, 0.f);

    session.tick(1.7f, {});
    EXPECT_FALSE(session.consumables().isEating());
    EXPECT_EQ(session.hud().getSlot(3).count, 1u);
    EXPECT_FLOAT_EQ(session.hud().getHealth(), 15.f);
}

TEST_F(SessionTest, SwitchingSlotCancelsEating) {
    session.setActive(true);
    select(3);
    mc::InputSnapshot in;
    in.use_pressed = true;
    session.tick(0.05f, in);
    ASSERT_TRUE(session.consumables().isEating());

    select(0);
    EXPECT_FALSE(session.consumables().isEating());
    EXPECT_EQ(session.hud().getSlot(3).count, 2u);
}

// ---------------------------------------------------------------- ballistics

TEST_F(SessionTest, UseWithBowLaunchesArrowAlongCameraForward) {
    session.setActive(true);
    select(4);
    input.cam_fwd = {0.f, 1.f, 0.f};
    mc::InputSnapshot in;
    in.use_pressed = true;
    session.tick(0.05f, in);

    const auto& ps = session.ballistics().getActiveProjectiles();
    ASSERT_EQ(ps.size(), 1u);
    EXPECT_EQ(ps[0].type, mc::ProjectileType::Arrow);
    EXPECT_GT(ps[0].velocity.y, 0.f);
    EXPECT_NEAR(ps[0].velocity.x, 0.f, 1e-3f);
}

// ---------------------------------------------------------------- elytra

TEST_F(SessionTest, GlideToggleOnlyWorksAirborne) {
    session.setActive(true);
    mc::InputSnapshot in;
    in.glide_toggle = true;
    in.on_ground = true;
    session.tick(0.05f, in);
    EXPECT_FALSE(session.elytra().getState().is_gliding);

    in.on_ground = false;
    session.tick(0.05f, in);
    EXPECT_TRUE(session.elytra().getState().is_gliding);
    EXPECT_TRUE(session.lastAnimInput().is_gliding);
}

TEST_F(SessionTest, GlideEndsOnSlowLanding) {
    session.setActive(true);
    mc::InputSnapshot in;
    in.glide_toggle = true;
    in.on_ground = false;
    session.tick(0.05f, in);
    ASSERT_TRUE(session.elytra().getState().is_gliding);

    in = {};
    in.on_ground = true;
    session.tick(0.05f, in);
    EXPECT_FALSE(session.elytra().getState().is_gliding);
}

TEST_F(SessionTest, FireworkBoostsOnlyWhileGlidingAndConsumesRocket) {
    session.setActive(true);
    select(5);
    mc::InputSnapshot use;
    use.use_pressed = true;
    use.on_ground = false;

    session.tick(0.05f, use); // not gliding yet
    EXPECT_FALSE(session.elytra().getState().is_boosting);
    EXPECT_EQ(session.hud().getSlot(5).count, 3u);

    mc::InputSnapshot glide;
    glide.glide_toggle = true;
    glide.on_ground = false;
    session.tick(0.05f, glide);
    ASSERT_TRUE(session.elytra().getState().is_gliding);

    session.tick(0.05f, use);
    EXPECT_TRUE(session.elytra().getState().is_boosting);
    EXPECT_EQ(session.hud().getSlot(5).count, 2u);
}

// ---------------------------------------------------------------- default loadout

TEST_F(SessionTest, LoadDefaultHotbarSeedsStandardLoadoutAndSyncsHeldItem) {
    session.setActive(true);
    session.loadDefaultHotbar();

    EXPECT_EQ(session.hud().getSlot(0).item, mc::ItemId::DiamondSword);
    EXPECT_EQ(session.hud().getSlot(1).item, mc::ItemId::DiamondPickaxe);
    EXPECT_EQ(session.hud().getSlot(2).item, mc::ItemId::BlockDirt);
    EXPECT_EQ(session.hud().getSlot(2).count, 64u);
    EXPECT_EQ(session.hud().getSlot(8).item, mc::ItemId::TotemOfUndying);
    EXPECT_EQ(session.hud().getSelectedSlot(), 0);
    EXPECT_EQ(render.held, mc::ItemId::DiamondSword);
}

// ---------------------------------------------------------------- entity identity

TEST_F(SessionTest, TargetProbeIgnoresLocalPlayer) {
    session.setActive(true);
    mc::InputSnapshot in;
    in.attack_pressed = true;
    session.tick(0.05f, in);
    EXPECT_EQ(physics.last_ignore, mc::EntityId::LocalPlayer);
}

TEST_F(SessionTest, AttackNeverTargetsSelfOrUnidentifiedHits) {
    session.setActive(true);
    mc::InputSnapshot in;
    in.attack_pressed = true;

    physics.next_hit = entityHit(static_cast<uint64_t>(mc::EntityId::LocalPlayer));
    session.tick(0.05f, in);
    session.tick(1.f, {});
    physics.next_hit = entityHit(0); // host geometry / unregistered actor: no identity
    session.tick(0.05f, in);

    EXPECT_TRUE(combat.hits.empty());
}

TEST_F(SessionTest, ElytraCrashDamagesLocalPlayer) {
    session.setActive(true);
    input.player_vel = {0.f, 0.f, -3000.f}; // diving at 30 m/s
    mc::RaycastResult wall;
    wall.has_hit = true;
    wall.point = {0.f, 0.f, 0.f};
    wall.normal = {0.f, 0.f, 1.f};
    wall.is_block = true;
    physics.next_hit = wall;

    mc::InputSnapshot in;
    in.on_ground = false;
    in.glide_toggle = true;
    session.tick(0.05f, in);

    ASSERT_EQ(combat.hits.size(), 1u);
    EXPECT_EQ(combat.hits[0].victim_id, mc::EntityId::LocalPlayer);
    EXPECT_EQ(combat.hits[0].attacker_id, mc::EntityId::None);
    EXPECT_GT(combat.hits[0].damage, 0.f);
    EXPECT_FLOAT_EQ(combat.hits[0].max_hp_percent, 0.f); // flat damage, no boss balancing
    EXPECT_FLOAT_EQ(combat.hits[0].knockback_force, 0.f);
    EXPECT_FALSE(session.elytra().getState().is_gliding);
}

// ---------------------------------------------------------------- driving the host player

TEST_F(SessionTest, GlidingDrivesLocalPlayerVelocityEveryTick) {
    session.setActive(true);
    mc::InputSnapshot in;
    in.on_ground = false;
    in.glide_toggle = true;
    session.tick(0.05f, in);

    ASSERT_TRUE(session.elytra().getState().is_gliding);
    EXPECT_EQ(physics.velocity_writes, 1);
    EXPECT_EQ(physics.last_velocity_target, mc::EntityId::LocalPlayer);
    const mc::Vec3 v1 = session.elytra().getState().velocity;
    EXPECT_FLOAT_EQ(physics.last_velocity.x, v1.x);
    EXPECT_FLOAT_EQ(physics.last_velocity.y, v1.y);
    EXPECT_FLOAT_EQ(physics.last_velocity.z, v1.z);

    in = {};
    in.on_ground = false;
    session.tick(0.05f, in);
    EXPECT_EQ(physics.velocity_writes, 2);
    EXPECT_FLOAT_EQ(physics.last_velocity.z, session.elytra().getState().velocity.z);
}

TEST_F(SessionTest, NoVelocityWritesUnlessGliding) {
    session.setActive(true);
    mc::InputSnapshot in;
    in.use_pressed = true;
    in.attack_pressed = true;
    session.tick(0.05f, in);
    session.tick(0.05f, {});
    EXPECT_EQ(physics.velocity_writes, 0);
}

TEST_F(SessionTest, VelocityWritesStopWhenGlideEnds) {
    session.setActive(true);
    mc::InputSnapshot glide;
    glide.on_ground = false;
    glide.glide_toggle = true;
    session.tick(0.05f, glide);
    ASSERT_EQ(physics.velocity_writes, 1);

    mc::InputSnapshot land;
    land.on_ground = true;
    session.tick(0.05f, land);
    ASSERT_FALSE(session.elytra().getState().is_gliding);
    EXPECT_EQ(physics.velocity_writes, 1); // no write on the landing tick

    session.tick(0.05f, {});
    EXPECT_EQ(physics.velocity_writes, 1);
}

TEST_F(SessionTest, ToggleOffAndDeactivateHandControlBackWithoutWrites) {
    session.setActive(true);
    mc::InputSnapshot glide;
    glide.on_ground = false;
    glide.glide_toggle = true;
    session.tick(0.05f, glide);
    ASSERT_EQ(physics.velocity_writes, 1);

    session.tick(0.05f, glide); // toggle again: stop
    EXPECT_FALSE(session.elytra().getState().is_gliding);
    EXPECT_EQ(physics.velocity_writes, 1);

    session.tick(0.05f, glide);
    ASSERT_TRUE(session.elytra().getState().is_gliding);
    const int before = physics.velocity_writes;
    session.setActive(false);
    EXPECT_EQ(physics.velocity_writes, before);
    EXPECT_FALSE(session.elytra().getState().is_gliding);
}

TEST_F(SessionTest, CrashTickWritesNoVelocity) {
    session.setActive(true);
    input.player_vel = {0.f, 0.f, -3000.f};
    mc::RaycastResult wall;
    wall.has_hit = true;
    wall.normal = {0.f, 0.f, 1.f};
    wall.is_block = true;
    physics.next_hit = wall;
    mc::InputSnapshot in;
    in.on_ground = false;
    in.glide_toggle = true;
    session.tick(0.05f, in);

    EXPECT_FALSE(session.elytra().getState().is_gliding);
    EXPECT_EQ(physics.velocity_writes, 0);
}

TEST_F(SessionTest, EstimatedGroundDoesNotCancelGlide_ButTrustedGroundDoes) {
    session.setActive(true);
    mc::InputSnapshot glide;
    glide.on_ground = false;
    glide.glide_toggle = true;
    session.tick(0.05f, glide);
    ASSERT_TRUE(session.elytra().getState().is_gliding);

    // Our own velocity writes make a velocity-based ground estimate read "grounded": ignore it.
    mc::InputSnapshot estimated;
    estimated.on_ground = true;
    estimated.on_ground_is_estimate = true;
    session.tick(0.05f, estimated);
    EXPECT_TRUE(session.elytra().getState().is_gliding);

    mc::InputSnapshot trusted;
    trusted.on_ground = true;
    session.tick(0.05f, trusted);
    EXPECT_FALSE(session.elytra().getState().is_gliding);
}

// ---------------------------------------------------------------- rig root and head/body split

TEST_F(SessionTest, RootFollowsThePlayerAndOnlyWhileActive) {
    input.player_pos = {120.f, -40.f, 5.f};
    session.tick(0.05f, {});
    EXPECT_EQ(render.root_updates, 0);

    session.setActive(true);
    session.tick(0.05f, {});
    EXPECT_EQ(render.root_updates, 1);
    EXPECT_FLOAT_EQ(render.root_pos.x, 120.f);
    EXPECT_FLOAT_EQ(render.root_pos.y, -40.f);
    EXPECT_FLOAT_EQ(render.root_pos.z, 5.f);
}

TEST_F(SessionTest, HeadTurnsRelativeToTheBodyUpToFiftyDegreesThenTheBodyFollows) {
    session.setActive(true);
    auto look = [&](float deg) {
        input.cam_fwd = {std::cos(deg * kPi / 180.f), std::sin(deg * kPi / 180.f), 0.f};
        session.tick(0.05f, {});
    };
    look(0.f);
    EXPECT_NEAR(render.root_yaw, 0.f, 1e-4f);

    look(30.f);  // within the limit: only the head turns
    EXPECT_NEAR(render.root_yaw, 0.f, 1e-4f);
    EXPECT_NEAR(session.lastAnimInput().look_yaw, 30.f * kPi / 180.f, 1e-4f);

    look(90.f);  // beyond 50 degrees: the body follows, the head stays at the limit
    EXPECT_NEAR(render.root_yaw, 40.f * kPi / 180.f, 1e-4f);
    EXPECT_NEAR(session.lastAnimInput().look_yaw, 50.f * kPi / 180.f, 1e-4f);
}

TEST_F(SessionTest, HeadYawIsWrappedSoTurningAcrossThePiBoundaryTakesTheShortWay) {
    session.setActive(true);
    auto look = [&](float deg) {
        input.cam_fwd = {std::cos(deg * kPi / 180.f), std::sin(deg * kPi / 180.f), 0.f};
        session.tick(0.05f, {});
    };
    look(170.f);
    look(-170.f); // only 20 degrees away through +/-180
    EXPECT_NEAR(session.lastAnimInput().look_yaw, 20.f * kPi / 180.f, 1e-3f);
    EXPECT_NEAR(std::cos(render.root_yaw), std::cos(170.f * kPi / 180.f), 1e-3f);
}

TEST_F(SessionTest, PitchUsesTheMinecraftConventionPositiveIsLookingDown) {
    session.setActive(true);
    input.cam_fwd = {std::cos(kPi / 6.f), 0.f, std::sin(kPi / 6.f)}; // looking up 30 degrees
    session.tick(0.05f, {});
    EXPECT_NEAR(session.lastAnimInput().look_pitch, -kPi / 6.f, 1e-4f);

    input.cam_fwd = {std::cos(kPi / 4.f), 0.f, -std::sin(kPi / 4.f)}; // looking down 45 degrees
    session.tick(0.05f, {});
    EXPECT_NEAR(session.lastAnimInput().look_pitch, kPi / 4.f, 1e-4f);
}

TEST_F(SessionTest, ReactivatingReseedsTheBodyYawFromTheCamera) {
    session.setActive(true);
    input.cam_fwd = {1.f, 0.f, 0.f};
    session.tick(0.05f, {});
    session.setActive(false);

    input.cam_fwd = {0.f, 1.f, 0.f};
    session.setActive(true);
    session.tick(0.05f, {});
    EXPECT_NEAR(render.root_yaw, kPi / 2.f, 1e-4f); // not stuck at the old heading
}

// ---------------------------------------------------------------------------------------------
// Host-owned body facing (the native character turns where it walks; Steve must follow that, not the camera)
// ---------------------------------------------------------------------------------------------
TEST_F(SessionTest, BodyFollowsTheHostFacingNotTheCameraWhenTheHostReportsOne) {
    input.has_facing = true;
    input.facing_yaw = kPi / 2.f;
    session.setActive(true);
    input.cam_fwd = {1.f, 0.f, 0.f}; // camera looks along +X, the character faces +Y
    session.tick(0.05f, {});
    EXPECT_NEAR(render.root_yaw, kPi / 2.f, 1e-4f);
    EXPECT_NEAR(session.lastAnimInput().look_yaw, -50.f * kPi / 180.f, 1e-4f); // 90 degrees off: clamped
}

TEST_F(SessionTest, HeadIsClampedToFiftyDegreesAndTheHostBodyIsNeverDragged) {
    input.has_facing = true;
    input.facing_yaw = 0.f;
    session.setActive(true);
    input.cam_fwd = {std::cos(2.f), std::sin(2.f), 0.f}; // 115 degrees off the body
    session.tick(0.05f, {});
    EXPECT_NEAR(render.root_yaw, 0.f, 1e-4f);                  // the body stays where the host put it
    EXPECT_NEAR(session.lastAnimInput().look_yaw, 50.f * kPi / 180.f, 1e-4f);
}

TEST_F(SessionTest, BodyYawTracksTheHostWhenItTurnsAcrossThePiBoundary) {
    input.has_facing = true;
    session.setActive(true);
    input.facing_yaw = 3.0f;
    session.tick(0.05f, {});
    input.facing_yaw = -3.0f; // 0.28 rad further round, through +/-pi
    session.tick(0.05f, {});
    EXPECT_NEAR(std::cos(render.root_yaw), std::cos(-3.0f), 1e-4f);
    EXPECT_NEAR(std::sin(render.root_yaw), std::sin(-3.0f), 1e-4f);
}

TEST_F(SessionTest, HostFacingAppearingOrVanishingMidSessionIsHandled) {
    session.setActive(true);
    input.cam_fwd = {1.f, 0.f, 0.f};
    session.tick(0.05f, {});
    input.has_facing = true;
    input.facing_yaw = 1.0f;
    session.tick(0.05f, {});
    EXPECT_NEAR(render.root_yaw, 1.0f, 1e-4f);
    input.has_facing = false; // sample went invalid: carry on from the last body yaw (head limit rules), no jump
    session.tick(0.05f, {});
    EXPECT_NEAR(render.root_yaw, 1.0f, 0.2f);
}

TEST_F(SessionTest, HeadDoesNotFlipSidesWhenTheCameraSwingsPastTheBackOfTheBody) {
    input.has_facing = true;
    input.facing_yaw = 0.f;
    session.setActive(true);
    auto look = [&](float deg) {
        input.cam_fwd = {std::cos(deg * kPi / 180.f), std::sin(deg * kPi / 180.f), 0.f};
        session.tick(0.016f, {});
        return session.lastAnimInput().look_yaw;
    };
    look(100.f); // head parked at +50 degrees on the left
    EXPECT_NEAR(look(170.f), 50.f * kPi / 180.f, 1e-4f);
    EXPECT_NEAR(look(179.f), 50.f * kPi / 180.f, 1e-4f);
    EXPECT_NEAR(look(-179.f), 50.f * kPi / 180.f, 1e-4f); // directly behind: no snap to the other side
    EXPECT_NEAR(look(-170.f), 50.f * kPi / 180.f, 1e-4f);
    EXPECT_NEAR(look(-100.f), -50.f * kPi / 180.f, 1e-4f); // clearly on the right again: now it may switch
}

TEST_F(SessionTest, WalkAnimationIsMeasuredAlongTheHostBodyNotTheCamera) {
    input.has_facing = true;
    input.facing_yaw = kPi / 2.f;         // body faces +Y
    input.cam_fwd = {1.f, 0.f, 0.f};      // camera looks along +X
    session.setActive(true);
    for (int i = 0; i < 60; ++i) {
        input.player_vel = {0.f, 432.f, 0.f}; // 4.32 m/s along the body
        session.tick(0.016f, {});
    }
    EXPECT_NEAR(session.lastAnimInput().forward_speed, 4.32f, 0.05f);
    EXPECT_NEAR(session.lastAnimInput().strafe_speed, 0.f, 0.05f);
}

TEST_F(SessionTest, StepwisePositionUpdatesDoNotMakeTheLimbsFlutter) {
    // The game moves its character in fixed steps while frames come faster: differenced velocity alternates
    // between 0 and twice the real speed. The animation must see roughly the real speed.
    input.has_facing = true;
    input.facing_yaw = 0.f;
    session.setActive(true);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 120; ++i) {
        input.player_vel = {(i % 2 == 0) ? 0.f : 864.f, 0.f, 0.f}; // mean 4.32 m/s
        session.tick(0.008f, {});
        if (i > 60) {
            lo = std::min(lo, session.lastAnimInput().forward_speed);
            hi = std::max(hi, session.lastAnimInput().forward_speed);
        }
    }
    EXPECT_GT(lo, 3.4f);
    EXPECT_LT(hi, 5.2f);
}

TEST_F(SessionTest, AnimationSpeedStartsFromTheCurrentVelocityAfterReactivation) {
    input.has_facing = true;
    session.setActive(true);
    input.player_vel = {432.f, 0.f, 0.f};
    for (int i = 0; i < 30; ++i) session.tick(0.016f, {});
    session.setActive(false);
    input.player_vel = {0.f, 0.f, 0.f};
    session.setActive(true);
    session.tick(0.016f, {});
    EXPECT_NEAR(session.lastAnimInput().forward_speed, 0.f, 0.05f); // no stale run carried over
}

// ---------------------------------------------------------------------------------------------
// Host gameplay port (docs/REVERSE_INTERFACES.md): optional, everything degrades without it
// ---------------------------------------------------------------------------------------------
namespace {
class MockGameplay : public mc::IHostGameplay {
public:
    uint32_t features{0};
    bool vitals_ok{false};
    mc::HostVitals vitals{};
    int suppress_calls{0};
    bool suppressed{false};

    uint32_t supportedFeatures() const override { return features; }
    bool getPlayerVitals(mc::HostVitals& out) const override {
        if (vitals_ok) out = vitals;
        return vitals_ok;
    }
    void setNativeCombatInputSuppressed(bool s) override {
        ++suppress_calls;
        suppressed = s;
    }
};
} // namespace

class SessionGameplayTest : public SessionTest {
protected:
    MockGameplay gameplay;
    mc::Session with_gameplay{mc::Ports{physics, render, combat, input, &gameplay}};
};

TEST_F(SessionGameplayTest, NativeCombatInputIsSuppressedExactlyWhileActive) {
    EXPECT_FALSE(gameplay.suppressed);
    with_gameplay.setActive(true);
    EXPECT_TRUE(gameplay.suppressed);
    with_gameplay.setActive(false);
    EXPECT_FALSE(gameplay.suppressed);
}

TEST_F(SessionGameplayTest, DestroyingAnActiveSessionReleasesTheInput) {
    {
        mc::Session temp{mc::Ports{physics, render, combat, input, &gameplay}};
        temp.setActive(true);
        ASSERT_TRUE(gameplay.suppressed);
    }
    EXPECT_FALSE(gameplay.suppressed);
}

TEST_F(SessionGameplayTest, SettingTheSameActiveStateTwiceDoesNotSpamTheHost) {
    with_gameplay.setActive(true);
    with_gameplay.setActive(true);
    EXPECT_EQ(gameplay.suppress_calls, 1);
}

TEST_F(SessionGameplayTest, HostHealthDrivesTheHeartsAsAFractionOfTwentyHalfHearts) {
    gameplay.vitals_ok = true;
    gameplay.vitals = {300.f, 600.f};
    with_gameplay.setActive(true);
    with_gameplay.tick(0.016f, {});
    EXPECT_FLOAT_EQ(with_gameplay.hud().getMaxHealth(), 20.f);
    EXPECT_FLOAT_EQ(with_gameplay.hud().getHealth(), 10.f);

    gameplay.vitals = {30.f, 600.f}; // took a beating
    with_gameplay.tick(0.016f, {});
    EXPECT_FLOAT_EQ(with_gameplay.hud().getHealth(), 1.f);
}

TEST_F(SessionGameplayTest, UnreadableOrNonsenseVitalsLeaveTheHeartsAlone) {
    with_gameplay.hud().setHealth(14.f);
    with_gameplay.setActive(true);
    gameplay.vitals_ok = false;
    with_gameplay.tick(0.016f, {});
    EXPECT_FLOAT_EQ(with_gameplay.hud().getHealth(), 14.f);

    gameplay.vitals_ok = true;
    for (mc::HostVitals bad : {mc::HostVitals{10.f, 0.f}, mc::HostVitals{10.f, -5.f},
                               mc::HostVitals{std::nanf(""), 100.f}, mc::HostVitals{10.f, std::nanf("")}}) {
        gameplay.vitals = bad;
        with_gameplay.tick(0.016f, {});
        EXPECT_FLOAT_EQ(with_gameplay.hud().getHealth(), 14.f);
    }

    gameplay.vitals = {500.f, 100.f}; // over max: clamped to full
    with_gameplay.tick(0.016f, {});
    EXPECT_FLOAT_EQ(with_gameplay.hud().getHealth(), 20.f);
}

TEST_F(SessionGameplayTest, ReportsWhichHostFeaturesAreStillMissing) {
    EXPECT_EQ(with_gameplay.missingHostFeatures(), mc::kAllHostFeatures);
    gameplay.features = static_cast<uint32_t>(mc::HostFeature::PlayerVitals) | static_cast<uint32_t>(mc::HostFeature::InputSuppression);
    EXPECT_EQ(with_gameplay.missingHostFeatures(),
              mc::kAllHostFeatures & ~(static_cast<uint32_t>(mc::HostFeature::PlayerVitals) | static_cast<uint32_t>(mc::HostFeature::InputSuppression)));
}

TEST_F(SessionTest, WithoutAGameplayPortEverythingStillWorksAndAllFeaturesAreMissing) {
    EXPECT_EQ(session.missingHostFeatures(), mc::kAllHostFeatures);
    session.setActive(true);
    session.tick(0.016f, {});
    session.setActive(false);
}

TEST(HostFeatureTest, EveryFeatureHasADistinctNameAndBit) {
    const mc::HostFeature all[] = {mc::HostFeature::PlayerVitals,      mc::HostFeature::EntityEnumeration,
                                   mc::HostFeature::InputSuppression, mc::HostFeature::IncomingDamageEvents,
                                   mc::HostFeature::FirstPersonCamera, mc::HostFeature::GroundedFlag};
    uint32_t seen = 0;
    for (auto f : all) {
        const uint32_t bit = static_cast<uint32_t>(f);
        EXPECT_EQ(bit & (bit - 1), 0u) << "single bit";
        EXPECT_EQ(seen & bit, 0u) << "distinct";
        seen |= bit;
        EXPECT_STRNE(mc::hostFeatureName(f), "Unknown");
    }
    EXPECT_EQ(seen, mc::kAllHostFeatures);
}


// ---------------------------------------------------------------------------------------------
// Third person: the camera floats behind the character, but vanilla Minecraft aims and reaches from the EYES.
// ---------------------------------------------------------------------------------------------
TEST_F(SessionTest, TheTargetRayStartsAtTheEyesNotAtACameraFloatingBehindTheCharacter) {
    session.setActive(true);
    input.cam_pos = {-500.f, 0.f, 220.f}; // 5 m behind and above
    input.cam_fwd = {1.f, 0.f, 0.f};
    input.has_eye = true;
    input.eye = {0.f, 0.f, 162.f};
    mc::InputSnapshot click;
    click.attack_pressed = true;
    session.tick(0.016f, click);

    ASSERT_EQ(physics.raycast_calls, 1);
    EXPECT_NEAR(physics.last_start.x, 0.f, 1e-3f);
    EXPECT_NEAR(physics.last_start.z, 162.f, 1e-3f);
    EXPECT_NEAR(physics.last_end.x, mc::Session::kReachCm, 1e-2f); // reach is measured from the eyes
    EXPECT_EQ(physics.last_ignore, mc::EntityId::LocalPlayer);
}

TEST_F(SessionTest, HostsWithoutAnEyePositionKeepUsingTheCamera) {
    session.setActive(true);
    input.cam_pos = {10.f, 20.f, 160.f};
    input.cam_fwd = {1.f, 0.f, 0.f};
    mc::InputSnapshot click;
    click.attack_pressed = true;
    session.tick(0.016f, click);
    ASSERT_EQ(physics.raycast_calls, 1);
    EXPECT_NEAR(physics.last_start.x, 10.f, 1e-3f);
    EXPECT_NEAR(physics.last_start.y, 20.f, 1e-3f);
}
