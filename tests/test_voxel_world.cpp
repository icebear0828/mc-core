#include <gtest/gtest.h>
#include "mc/voxel_world.hpp"
#include "mc/contracts/physics_adapter.hpp"
#include "mc/contracts/render_adapter.hpp"

namespace {

class MockVoxelPhysicsAdapter : public mc::IPhysicsAdapter {
public:
    mc::RaycastResult raycastWorld(const mc::Vec3&, const mc::Vec3&, mc::EntityId) override {
        return mc::RaycastResult{};
    }
    uint64_t createBlockCollider(const mc::GridPos&, mc::BlockId, const mc::Vec3&) override {
        return ++collider_count_;
    }
    void destroyBlockCollider(uint64_t) override {
        --collider_count_;
    }
    void applyLinearImpulse(mc::EntityId, const mc::Vec3&) override {}
    void setLinearVelocity(mc::EntityId, const mc::Vec3&) override {}

    uint64_t collider_count_{0};
};

class MockVoxelRenderAdapter : public mc::IRenderAdapter {
public:
    void setNativePlayerVisible(bool) override {}
    bool spawnSteveParts() override { return true; }
    void destroySteveParts() override {}
    void setSteveRoot(const mc::Vec3&, float) override {}
    void updateStevePartTransforms(const mc::SteveAnimator::PartTransforms&) override {}
    void setHeldItemVisual(mc::ItemId, bool) override {}
    uint64_t spawnBlockVisual(const mc::GridPos&, mc::BlockId, const mc::Vec3&) override {
        return ++visual_count_;
    }
    void setBlockCrackStage(uint64_t, int stage) override {
        last_stage_ = stage;
    }
    void destroyBlockVisual(uint64_t) override {
        --visual_count_;
    }

    uint64_t visual_count_{0};
    int last_stage_{-1};
};

} // namespace

TEST(VoxelWorldTest, GridQuantization) {
    mc::Vec3 pos1{49.9f, -149.0f, 201.2f};
    mc::GridPos grid = mc::VoxelWorld::worldToGrid(pos1);

    EXPECT_EQ(grid.x, 0);
    EXPECT_EQ(grid.y, -1);
    EXPECT_EQ(grid.z, 2);

    mc::Vec3 world_back = mc::VoxelWorld::gridToWorld(grid);
    EXPECT_FLOAT_EQ(world_back.x, 0.0f);
    EXPECT_FLOAT_EQ(world_back.y, -100.0f);
    EXPECT_FLOAT_EQ(world_back.z, 200.0f);
}

TEST(VoxelWorldTest, PlaceAndMineCycle) {
    MockVoxelPhysicsAdapter physics;
    MockVoxelRenderAdapter render;
    mc::VoxelWorld world(physics, render);

    mc::GridPos target{1, 2, 0};
    mc::Vec3 player_pos{500.f, 500.f, 0.f}; // Far enough from target

    EXPECT_TRUE(world.placeBlock(target, mc::BlockId::Stone, player_pos));
    EXPECT_EQ(world.getActiveBlockCount(), 1u);
    EXPECT_EQ(physics.collider_count_, 1u);
    EXPECT_EQ(render.visual_count_, 1u);

    // Mine stone with Diamond Pickaxe (takes 0.3s)
    bool broken = world.mineBlock(target, mc::ItemId::DiamondPickaxe, 0.15f);
    EXPECT_FALSE(broken);
    EXPECT_GE(render.last_stage_, 4); // ~50% progress, stage >= 4

    broken = world.mineBlock(target, mc::ItemId::DiamondPickaxe, 0.2f);
    EXPECT_TRUE(broken);
    EXPECT_EQ(world.getActiveBlockCount(), 0u);
    EXPECT_EQ(physics.collider_count_, 0u);
    EXPECT_EQ(render.visual_count_, 0u);
}
