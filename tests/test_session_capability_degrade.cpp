// Graceful degradation for physics capabilities.
//
// A host adapter declares what its physics port can really do through
// IPhysicsAdapter::supportedPhysicsFeatures(). The Session wraps that port in mc::DegradingPhysicsPort,
// which forwards supported calls and turns unsupported ones into safe no-ops, so a partial port can
// never crash a rule engine. docs/REVERSE_INTERFACES.md lists every PhysicsFeature.

#include <gtest/gtest.h>

#include "mc/contracts/combat_adapter.hpp"
#include "mc/contracts/input_adapter.hpp"
#include "mc/contracts/physics_adapter.hpp"
#include "mc/contracts/render_adapter.hpp"
#include "mc/degrading_physics_port.hpp"
#include "mc/elytra.hpp"
#include "mc/hud.hpp"
#include "mc/session.hpp"
#include "mc/voxel_world.hpp"

#include <memory>
#include <vector>

namespace {

class CapabilityPhysics : public mc::IPhysicsAdapter {
public:
    uint32_t supportedPhysicsFeatures() const override { return features; }

    void setFeatures(uint32_t f) { features = f; }

    int raycast_calls{0};
    int collider_creates{0};
    int collider_destroys{0};
    int impulse_calls{0};
    int velocity_writes{0};
    mc::RaycastResult next_hit{};
    mc::EntityId last_velocity_target{mc::EntityId::None};
    mc::Vec3 last_velocity{};
    uint32_t features{mc::kAllPhysicsFeatures};

    mc::RaycastResult raycastWorld(const mc::Vec3&, const mc::Vec3&, mc::EntityId) override {
        ++raycast_calls;
        return next_hit;
    }
    uint64_t createBlockCollider(const mc::GridPos&, mc::BlockId, const mc::Vec3&) override {
        return static_cast<uint64_t>(++collider_creates);
    }
    void destroyBlockCollider(uint64_t) override { ++collider_destroys; }
    void applyLinearImpulse(mc::EntityId, const mc::Vec3&) override { ++impulse_calls; }
    void setLinearVelocity(mc::EntityId id, const mc::Vec3& v) override {
        ++velocity_writes;
        last_velocity_target = id;
        last_velocity = v;
    }
};

class CapabilityRender : public mc::IRenderAdapter {
public:
    void setNativePlayerVisible(bool) override {}
    bool spawnSteveParts() override { return true; }
    void destroySteveParts() override {}
    void setSteveRoot(const mc::Vec3&, float) override {}
    void updateStevePartTransforms(const mc::SteveAnimator::PartTransforms&) override {}
    void setHeldItemVisual(mc::ItemId, bool) override {}
    uint64_t spawnBlockVisual(const mc::GridPos&, mc::BlockId, const mc::Vec3&) override {
        return static_cast<uint64_t>(++visuals);
    }
    void setBlockCrackStage(uint64_t, int) override {}
    void destroyBlockVisual(uint64_t) override {}

    int visuals{0};
};

class CapabilityCombat : public mc::ICombatAdapter {
public:
    bool processHit(const mc::HitIntent& i) override {
        hits.push_back(i);
        return true;
    }
    float getMaxHealth(mc::EntityId) override { return 100.f; }
    void triggerStaggerOrRagdoll(mc::EntityId, const mc::Vec3&, float) override {}

    std::vector<mc::HitIntent> hits;
};

class CapabilityInput : public mc::IInputAdapter {
public:
    mc::Vec3 getEyePosition() const override { return cam_pos; }
    bool getPlayerFacingYaw(float&) const override { return false; }
    mc::ItemId getEquippedMainHand() const override { return mc::ItemId::None; }
    mc::ItemId getEquippedOffHand() const override { return mc::ItemId::None; }
    mc::Vec3 getCameraPosition() const override { return cam_pos; }
    mc::Vec3 getCameraForward() const override { return cam_fwd; }
    mc::Vec3 getPlayerPosition() const override { return player_pos; }
    mc::Vec3 getPlayerVelocity() const override { return player_vel; }

    mc::Vec3 cam_pos{0.f, 0.f, 160.f};
    mc::Vec3 cam_fwd{1.f, 0.f, 0.f};
    mc::Vec3 player_pos{0.f, 0.f, 0.f};
    mc::Vec3 player_vel{0.f, 0.f, 0.f};
};

constexpr uint32_t kBit(mc::PhysicsFeature f) { return static_cast<uint32_t>(f); }

mc::RaycastResult surfaceHit() {
    mc::RaycastResult r;
    r.has_hit = true;
    r.point = {250.f, 0.f, 100.f};
    r.normal = {-1.f, 0.f, 0.f};
    r.is_block = true;
    return r;
}

} // namespace

// ------------------------------------------------------------------ the decorator itself

TEST(DegradingPhysicsPortTest, DefaultAdapterSupportsEveryPhysicsFeature) {
    CapabilityPhysics physics;
    mc::DegradingPhysicsPort port{physics};

    EXPECT_EQ(port.supported(), mc::kAllPhysicsFeatures);
    EXPECT_EQ(port.missing(), 0u);
    EXPECT_TRUE(port.supports(mc::PhysicsFeature::RaycastWorld));
    EXPECT_TRUE(port.supports(mc::PhysicsFeature::LinearVelocityWrite));
}

TEST(DegradingPhysicsPortTest, ForwardsEverySupportedCallUnchanged) {
    CapabilityPhysics physics;
    mc::DegradingPhysicsPort port{physics};

    physics.next_hit = surfaceHit();
    const mc::RaycastResult hit = port.raycastWorld({0.f, 0.f, 0.f}, {100.f, 0.f, 0.f});
    EXPECT_TRUE(hit.has_hit);
    EXPECT_EQ(physics.raycast_calls, 1);

    EXPECT_EQ(port.createBlockCollider({1, 2, 3}, mc::BlockId::Stone, {100.f, 200.f, 300.f}), 1u);
    EXPECT_EQ(physics.collider_creates, 1);
    port.destroyBlockCollider(1);
    EXPECT_EQ(physics.collider_destroys, 1);
    port.applyLinearImpulse(mc::EntityId{5}, {1.f, 0.f, 0.f});
    EXPECT_EQ(physics.impulse_calls, 1);
    port.setLinearVelocity(mc::EntityId::LocalPlayer, {0.f, 0.f, 300.f});
    EXPECT_EQ(physics.velocity_writes, 1);
}

TEST(DegradingPhysicsPortTest, UnsupportedCallsBecomeNoOpsAndNeverReachTheAdapter) {
    CapabilityPhysics physics;
    physics.setFeatures(0u);
    mc::DegradingPhysicsPort port{physics};

    EXPECT_EQ(port.missing(), mc::kAllPhysicsFeatures);

    physics.next_hit = surfaceHit();
    EXPECT_FALSE(port.raycastWorld({0.f, 0.f, 0.f}, {100.f, 0.f, 0.f}).has_hit);
    EXPECT_EQ(port.createBlockCollider({0, 0, 0}, mc::BlockId::Stone, {}), 0u);
    port.destroyBlockCollider(7);
    port.applyLinearImpulse(mc::EntityId{5}, {1.f, 0.f, 0.f});
    port.setLinearVelocity(mc::EntityId::LocalPlayer, {0.f, 0.f, 300.f});

    EXPECT_EQ(physics.raycast_calls, 0);
    EXPECT_EQ(physics.collider_creates, 0);
    EXPECT_EQ(physics.collider_destroys, 0);
    EXPECT_EQ(physics.impulse_calls, 0);
    EXPECT_EQ(physics.velocity_writes, 0);
}

TEST(DegradingPhysicsPortTest, MissingReportsExactlyTheUnsupportedBits) {
    CapabilityPhysics physics;
    physics.setFeatures(kBit(mc::PhysicsFeature::RaycastWorld) | kBit(mc::PhysicsFeature::BlockCollision));
    mc::DegradingPhysicsPort port{physics};

    EXPECT_EQ(port.missing(), mc::kAllPhysicsFeatures & ~(kBit(mc::PhysicsFeature::RaycastWorld) |
                                                          kBit(mc::PhysicsFeature::BlockCollision)));
    EXPECT_TRUE(port.supports(mc::PhysicsFeature::RaycastWorld));
    EXPECT_FALSE(port.supports(mc::PhysicsFeature::LinearVelocityWrite));
}

TEST(PhysicsFeatureTest, EveryFeatureHasADistinctNameAndBit) {
    const mc::PhysicsFeature all[] = {
        mc::PhysicsFeature::RaycastWorld,       mc::PhysicsFeature::BlockCollision,
        mc::PhysicsFeature::LinearImpulse,      mc::PhysicsFeature::LinearVelocityWrite,
        mc::PhysicsFeature::JumpImpulse,        mc::PhysicsFeature::FallControl,
    };
    uint32_t seen = 0;
    for (auto f : all) {
        const uint32_t bit = static_cast<uint32_t>(f);
        EXPECT_EQ(bit & (bit - 1), 0u) << "single bit";
        EXPECT_EQ(seen & bit, 0u) << "distinct";
        seen |= bit;
        EXPECT_STRNE(mc::physicsFeatureName(f), "Unknown");
    }
    EXPECT_EQ(seen, mc::kAllPhysicsFeatures);
}

// ------------------------------------------------------------------ the Session over a partial port

class SessionCapabilityTest : public ::testing::Test {
protected:
    void makeSession(uint32_t features) {
        physics.setFeatures(features);
        session = std::make_unique<mc::Session>(mc::Ports{physics, render, combat, input});
        session->hud().setSlot(0, mc::ItemId::DiamondSword, 1);
        session->hud().setSlot(1, mc::ItemId::BlockStone, 64);
        session->hud().selectSlot(0);
    }

    void select(int slot) {
        mc::InputSnapshot in;
        in.hotbar_select = slot;
        session->tick(0.f, in);
    }

    CapabilityPhysics physics;
    CapabilityRender render;
    CapabilityCombat combat;
    CapabilityInput input;
    std::unique_ptr<mc::Session> session;
};

TEST_F(SessionCapabilityTest, ReportsNoMissingPhysicsFeaturesWhenThePortImplementsEverything) {
    makeSession(mc::kAllPhysicsFeatures);
    EXPECT_EQ(session->missingPhysicsFeatures(), 0u);
}

TEST_F(SessionCapabilityTest, ReportsThePhysicsFeaturesThePortStillNeeds) {
    makeSession(kBit(mc::PhysicsFeature::RaycastWorld) | kBit(mc::PhysicsFeature::LinearVelocityWrite));
    EXPECT_EQ(session->missingPhysicsFeatures(),
              mc::kAllPhysicsFeatures & ~(kBit(mc::PhysicsFeature::RaycastWorld) |
                                          kBit(mc::PhysicsFeature::LinearVelocityWrite)));
}

TEST_F(SessionCapabilityTest, DefaultPortStillDrivesTargetingAndGliding) {
    makeSession(mc::kAllPhysicsFeatures);
    session->setActive(true);

    mc::InputSnapshot click;
    click.attack_pressed = true;
    session->tick(0.05f, click);
    EXPECT_EQ(physics.raycast_calls, 1);

    mc::InputSnapshot glide;
    glide.on_ground = false;
    glide.glide_toggle = true;
    session->tick(0.05f, glide);
    ASSERT_TRUE(session->elytra().getState().is_gliding);
    EXPECT_EQ(physics.velocity_writes, 1);
    EXPECT_EQ(physics.last_velocity_target, mc::EntityId::LocalPlayer);
}

TEST_F(SessionCapabilityTest, APortWithoutAnyCapabilityTicksWithoutCallsOrCrashes) {
    makeSession(0u);
    session->setActive(true);

    mc::InputSnapshot in;
    in.attack_pressed = true;
    in.attack_held = true;
    in.use_pressed = true;
    in.on_ground = false;
    in.glide_toggle = true;
    session->tick(0.05f, in);
    session->tick(0.05f, {});
    session->setActive(false);

    EXPECT_EQ(physics.raycast_calls, 0);
    EXPECT_EQ(physics.velocity_writes, 0);
    EXPECT_EQ(physics.impulse_calls, 0);
    EXPECT_TRUE(combat.hits.empty());
}

TEST_F(SessionCapabilityTest, WithoutRaycastWorldTheTargetProbeIsSkipped) {
    makeSession(mc::kAllPhysicsFeatures & ~kBit(mc::PhysicsFeature::RaycastWorld));
    session->setActive(true);
    physics.next_hit = surfaceHit();

    mc::InputSnapshot in;
    in.attack_pressed = true;
    in.use_pressed = true;
    session->tick(0.05f, in);

    EXPECT_EQ(physics.raycast_calls, 0);
    EXPECT_TRUE(combat.hits.empty());
    EXPECT_EQ(session->voxel().getActiveBlockCount(), 0u); // no hit to place against, but no crash either
}

TEST_F(SessionCapabilityTest, WithoutBlockCollisionBlocksStayLocalAndHaveNoCollider) {
    makeSession(mc::kAllPhysicsFeatures & ~kBit(mc::PhysicsFeature::BlockCollision));
    session->setActive(true);
    select(1); // stone block
    physics.next_hit = surfaceHit();

    mc::InputSnapshot in;
    in.use_pressed = true;
    session->tick(0.05f, in);

    EXPECT_EQ(physics.raycast_calls, 1); // raycast still works
    EXPECT_EQ(physics.collider_creates, 0);
    ASSERT_EQ(session->voxel().getActiveBlockCount(), 1u);
    const mc::VoxelBlock* block = session->voxel().getBlock({2, 0, 1});
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->collider_handle, 0u);
    EXPECT_EQ(session->hud().getSlot(1).count, 63u);
}

TEST_F(SessionCapabilityTest, WithoutLinearVelocityWriteGlidingNeverWritesTheHost) {
    makeSession(mc::kAllPhysicsFeatures & ~kBit(mc::PhysicsFeature::LinearVelocityWrite));
    session->setActive(true);

    mc::InputSnapshot glide;
    glide.on_ground = false;
    glide.glide_toggle = true;
    session->tick(0.05f, glide);
    glide.glide_toggle = false;
    session->tick(0.05f, glide);

    EXPECT_TRUE(session->elytra().getState().is_gliding);
    EXPECT_EQ(physics.velocity_writes, 0);
    EXPECT_NE(session->missingPhysicsFeatures() & kBit(mc::PhysicsFeature::LinearVelocityWrite), 0u);
}
