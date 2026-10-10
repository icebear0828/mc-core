#include <gtest/gtest.h>
#include "mc/animator.hpp"
#include <algorithm>
#include <cmath>

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
    EXPECT_NE(right_leg.rot.y, left_leg.rot.y);
    EXPECT_NEAR(right_leg.rot.y, -left_leg.rot.y, 0.01f);
}

TEST(SteveAnimatorTest, HeadFollowsLookAngles) {
    mc::SteveAnimator animator;
    mc::SteveAnimInput input{};
    input.look_pitch = 0.5f;
    input.look_yaw = -0.3f;

    animator.update(0.05f, input);
    const auto& transforms = animator.getTransforms();
    const auto head = transforms[static_cast<size_t>(mc::StevePart::Head)];

    EXPECT_NE(head.rot.y, 0.0f); // pitch
    EXPECT_NE(head.rot.z, 0.0f); // yaw
}

TEST(SteveAnimatorTest, ZombieArmsPointStraightForwardAndKeepSwayingWhileWalking) {
    mc::SteveAnimator plain, zombie;
    mc::SteveAnimInput in{};
    in.forward_speed = 4.317f;
    mc::SteveAnimInput zin = in;
    zin.arms_forward = true;
    for (int i = 0; i < 10; ++i) {
        plain.update(0.05f, in);
        zombie.update(0.05f, zin);
    }
    const size_t ra = static_cast<size_t>(mc::StevePart::RightArm), la = static_cast<size_t>(mc::StevePart::LeftArm);
    const size_t rs = static_cast<size_t>(mc::StevePart::RightSleeve);
    // the arms are rotated far from the plain walking swing (about -90 degrees of pitch: w = cos(45 deg) at most)
    EXPECT_LT(zombie.getTransforms()[ra].rot.w, 0.75f);
    EXPECT_LT(zombie.getTransforms()[la].rot.w, 0.75f);
    EXPECT_GT(plain.getTransforms()[ra].rot.w, 0.85f);
    // both arms the same pitch (they do not swing against each other like a walking player's)
    EXPECT_NEAR(zombie.getTransforms()[ra].rot.y, zombie.getTransforms()[la].rot.y, 0.05f); // pitch is the y component
    // the sleeve follows its arm
    EXPECT_NEAR(zombie.getTransforms()[rs].rot.y, zombie.getTransforms()[ra].rot.y, 1e-5f);
    // the legs still walk
    const auto rl = zombie.getTransforms()[static_cast<size_t>(mc::StevePart::RightLeg)].rot.y;
    const auto ll = zombie.getTransforms()[static_cast<size_t>(mc::StevePart::LeftLeg)].rot.y;
    EXPECT_LT(rl * ll, 0.f);
}

TEST(SteveAnimatorTest, SwayOnlyArmsDoNotSwingWithTheStrideAndStayNearTheirRestRotation) {
    // for models whose file already holds the arms out (the zombie's rest rotation): the animator only adds a slow sway on top of it
    mc::SteveAnimator animator;
    mc::SteveAnimInput in{};
    in.forward_speed = 4.317f;
    in.arms_sway_only = true;
    float widest = 0.f;
    for (int i = 0; i < 40; ++i) {
        animator.update(0.05f, in);
        const auto& t = animator.getTransforms();
        widest = std::max(widest, std::fabs(t[static_cast<size_t>(mc::StevePart::RightArm)].rot.y));
        widest = std::max(widest, std::fabs(t[static_cast<size_t>(mc::StevePart::LeftArm)].rot.y));
    }
    EXPECT_LT(widest, 0.06f); // a walking player's arm swing reaches ~0.4 here (sin of half 0.8 rad)
    // the legs still walk
    bool legs_move = false;
    mc::SteveAnimator w2;
    for (int i = 0; i < 12; ++i) {
        w2.update(0.05f, in);
        legs_move = legs_move || std::fabs(w2.getTransforms()[static_cast<size_t>(mc::StevePart::RightLeg)].rot.y) > 0.1f;
    }
    EXPECT_TRUE(legs_move);
}
