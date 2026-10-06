#include <gtest/gtest.h>
#include "mc/elytra.hpp"
#include "mc/animator.hpp"
#include "mc/contracts/physics_adapter.hpp"

namespace {

class MockElytraPhysics : public mc::IPhysicsAdapter {
public:
    mc::RaycastResult mock_raycast_result{};

    mc::RaycastResult raycastWorld(const mc::Vec3&, const mc::Vec3&, uint64_t) override {
        return mock_raycast_result;
    }
    uint64_t createBlockCollider(const mc::GridPos&, mc::BlockId, const mc::Vec3&) override { return 0; }
    void destroyBlockCollider(uint64_t) override {}
    void applyLinearImpulse(uint64_t, const mc::Vec3&) override {}
};

} // namespace

TEST(ElytraFlightTest, CannotStartGlidingWhileOnGround) {
    MockElytraPhysics physics;
    mc::ElytraFlight flight(physics);

    const bool started = flight.startGliding(true, {0.f, 0.f, 0.f});
    EXPECT_FALSE(started);
    EXPECT_FALSE(flight.getState().is_gliding);
}

TEST(ElytraFlightTest, StartGlidingInAirAndDivingAccelerates) {
    MockElytraPhysics physics;
    mc::ElytraFlight flight(physics);

    const mc::Vec3 initial_vel{500.f, 0.f, 0.f};
    const bool started = flight.startGliding(false, initial_vel);
    EXPECT_TRUE(started);
    EXPECT_TRUE(flight.getState().is_gliding);

    const float initial_speed = flight.getState().velocity.length();

    // Dive down: pitch = -0.5 radians (looking downward)
    const auto result = flight.update(0.1f, {0.f, 0.f, 1000.f}, -0.5f, 0.f, false);
    EXPECT_FALSE(result.crashed);
    EXPECT_FALSE(result.landed);

    const float dived_speed = flight.getState().velocity.length();
    // Diving accelerates kinetic speed
    EXPECT_GT(dived_speed, initial_speed);
}

TEST(ElytraFlightTest, ClimbingGeneratesLiftAndDeceleratesHorizontalSpeed) {
    MockElytraPhysics physics;
    mc::ElytraFlight flight(physics);

    flight.startGliding(false, {2000.f, 0.f, 0.f});

    // Pitch up: pitch = 0.5 radians (looking upward)
    flight.update(0.1f, {0.f, 0.f, 1000.f}, 0.5f, 0.f, false);

    const auto state = flight.getState();
    // Positive upward vertical velocity generated
    EXPECT_GT(state.velocity.z, 0.f);
    // Horizontal speed decreases due to climb drag conversion
    EXPECT_LT(state.velocity.x, 2000.f);
}

TEST(ElytraFlightTest, FireworkBoostAddsThrustAndExpires) {
    MockElytraPhysics physics;
    mc::ElytraFlight flight(physics);

    // Cannot boost while not gliding
    EXPECT_FALSE(flight.useFireworkBoost(1.0f));

    flight.startGliding(false, {500.f, 0.f, 0.f});
    EXPECT_TRUE(flight.useFireworkBoost(1.0f));
    EXPECT_TRUE(flight.getState().is_boosting);

    // Update 0.2s with look straight ahead
    flight.update(0.2f, {0.f, 0.f, 1000.f}, 0.f, 0.f, false);
    EXPECT_TRUE(flight.getState().is_boosting);
    EXPECT_GT(flight.getState().velocity.x, 700.f);

    // Advance remaining boost duration past 1.0s
    flight.update(0.9f, {0.f, 0.f, 1000.f}, 0.f, 0.f, false);
    EXPECT_FALSE(flight.getState().is_boosting);
}

TEST(ElytraFlightTest, HighSpeedCollisionTriggersKineticDamageAndStopsGliding) {
    MockElytraPhysics physics;
    mc::ElytraFlight flight(physics);

    flight.startGliding(false, {2500.f, 0.f, 0.f});

    // Setup mock obstacle hit facing oncoming flight
    physics.mock_raycast_result.has_hit = true;
    physics.mock_raycast_result.point = {50.f, 0.f, 1000.f};
    physics.mock_raycast_result.normal = {-1.f, 0.f, 0.f}; // wall facing +X

    const auto result = flight.update(0.05f, {0.f, 0.f, 1000.f}, 0.f, 0.f, false);
    EXPECT_TRUE(result.crashed);
    EXPECT_GT(result.kinetic_damage, 0.f);
    EXPECT_FLOAT_EQ(result.crash_normal.x, -1.f);
    EXPECT_FALSE(flight.getState().is_gliding);
}

TEST(ElytraFlightTest, LowSpeedGroundContactLandsSafely) {
    MockElytraPhysics physics;
    mc::ElytraFlight flight(physics);

    flight.startGliding(false, {200.f, 0.f, -50.f});

    const auto result = flight.update(0.05f, {0.f, 0.f, 10.f}, 0.f, 0.f, true);
    EXPECT_FALSE(result.crashed);
    EXPECT_TRUE(result.landed);
    EXPECT_FLOAT_EQ(result.kinetic_damage, 0.f);
    EXPECT_FALSE(flight.getState().is_gliding);
}

TEST(ElytraFlightTest, SteveAnimatorGlidingPoseDiffersFromWalking) {
    mc::SteveAnimator animator;

    mc::SteveAnimInput walking_input;
    walking_input.forward_speed = 4.0f;
    animator.update(0.1f, walking_input);
    const auto walking_transforms = animator.getTransforms();

    mc::SteveAnimInput gliding_input;
    gliding_input.is_gliding = true;
    gliding_input.look_pitch = -0.3f;
    gliding_input.roll_angle = 0.2f;
    animator.update(0.1f, gliding_input);
    const auto gliding_transforms = animator.getTransforms();

    // Body orientation must reflect gliding pose
    EXPECT_NE(walking_transforms[static_cast<size_t>(mc::StevePart::Body)].rot.w,
              gliding_transforms[static_cast<size_t>(mc::StevePart::Body)].rot.w);
}
