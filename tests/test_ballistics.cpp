#include <gtest/gtest.h>
#include "mc/ballistics.hpp"
#include "mc/contracts/physics_adapter.hpp"

namespace {

class MockBallisticsPhysics : public mc::IPhysicsAdapter {
public:
    mc::RaycastResult raycastWorld(const mc::Vec3&, const mc::Vec3&, mc::EntityId) override {
        // No obstacle hit in this test
        return mc::RaycastResult{};
    }
    uint64_t createBlockCollider(const mc::GridPos&, mc::BlockId, const mc::Vec3&) override { return 0; }
    void destroyBlockCollider(uint64_t) override {}
    void applyLinearImpulse(mc::EntityId, const mc::Vec3&) override {}
    void setLinearVelocity(mc::EntityId, const mc::Vec3&) override {}
};

} // namespace

TEST(BallisticsEngineTest, ArrowTrajectoryDropsWithGravity) {
    MockBallisticsPhysics physics;
    mc::BallisticsEngine engine(physics);

    const mc::Vec3 origin{0.f, 0.f, 200.f};
    const mc::Vec3 forward{1.f, 0.f, 0.f}; // horizontal launch
    engine.launch(mc::ProjectileType::Arrow, origin, forward, 1.0f);

    EXPECT_EQ(engine.getActiveProjectiles().size(), 1u);

    // Update 0.5s
    engine.update(0.5f, {0, 0, 0});

    const auto& p = engine.getActiveProjectiles()[0];
    // Z coordinate must have dropped due to gravity
    EXPECT_LT(p.position.z, origin.z);
    // X coordinate must have traveled forward
    EXPECT_GT(p.position.x, origin.x);
}
