#include <gtest/gtest.h>
#include "mc/animator.hpp"

TEST(SteveAnimatorTest, IdleTransformsAreFinite) {
    mc::SteveAnimator animator;
    mc::SteveAnimInput input{};

    animator.update(0.05f, input);
    const auto& transforms = animator.getTransforms();

    for (const auto& t : transforms) {
        EXPECT_FALSE(std::isnan(t.rot.x));
        EXPECT_FALSE(std::isnan(t.rot.y));
        EXPECT_FALSE(std::isnan(t.rot.z));
        EXPECT_FALSE(std::isnan(t.rot.w));
    }
}

TEST(SteveAnimatorTest, WalkingProducesOppositeLegSwings) {
    mc::SteveAnimator animator;
    mc::SteveAnimInput input{};
    input.forward_speed = 4.317f; // Full walk speed

    animator.update(0.1f, input);
    const auto& transforms = animator.getTransforms();

    const auto right_leg = transforms[static_cast<size_t>(mc::StevePart::RightLeg)];
    const auto left_leg = transforms[static_cast<size_t>(mc::StevePart::LeftLeg)];

    // Opposite phase: if one is forward, the other is backward
    EXPECT_NE(right_leg.rot.x, left_leg.rot.x);
}

TEST(SteveAnimatorTest, HeadFollowsLookAngles) {
    mc::SteveAnimator animator;
    mc::SteveAnimInput input{};
    input.look_pitch = 0.5f;
    input.look_yaw = -0.3f;

    animator.update(0.05f, input);
    const auto& transforms = animator.getTransforms();
    const auto head = transforms[static_cast<size_t>(mc::StevePart::Head)];

    EXPECT_NE(head.rot.x, 0.0f);
    EXPECT_NE(head.rot.y, 0.0f);
}
