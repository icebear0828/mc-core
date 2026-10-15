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

TEST(SteveAnimatorTest, TwoHandedBowDrawingPosesBothArmsAuthentically) {
    const size_t ra = static_cast<size_t>(mc::StevePart::RightArm), la = static_cast<size_t>(mc::StevePart::LeftArm);
    const size_t rs = static_cast<size_t>(mc::StevePart::RightSleeve), ls = static_cast<size_t>(mc::StevePart::LeftSleeve);
    mc::SteveAnimator idle, main_bow, off_bow;
    mc::SteveAnimInput in{};
    mc::SteveAnimInput main_in = in, off_in = in;
    main_in.bow_charge = 0.8f;
    off_in.bow_charge = 0.8f;
    off_in.bow_offhand = true;
    for (int i = 0; i < 10; ++i) {
        idle.update(0.05f, in);
        main_bow.update(0.05f, main_in);
        off_bow.update(0.05f, off_in);
    }

    // Main hand bow draw:
    // Right arm holds bow (pitch -90 deg, yaw -0.1), Left arm draws string (pitch -90 deg, yaw +0.5 across chest)
    EXPECT_LT(main_bow.getTransforms()[ra].rot.w, 0.75f) << "Right arm holding bow must be raised";
    EXPECT_LT(main_bow.getTransforms()[la].rot.w, 0.75f) << "Left arm drawing string must also be raised";
    // Pitch angles match
    EXPECT_NEAR(main_bow.getTransforms()[ra].rot.y, main_bow.getTransforms()[la].rot.y, 0.05f);
    // Sleeves follow arms
    EXPECT_NEAR(main_bow.getTransforms()[rs].rot.w, main_bow.getTransforms()[ra].rot.w, 1e-4f);
    EXPECT_NEAR(main_bow.getTransforms()[ls].rot.w, main_bow.getTransforms()[la].rot.w, 1e-4f);

    // Offhand bow draw:
    // Left arm holds bow (pitch -90 deg, yaw +0.1), Right arm draws string (pitch -90 deg, yaw -0.5 across chest)
    EXPECT_LT(off_bow.getTransforms()[la].rot.w, 0.75f) << "Left arm holding offhand bow must be raised";
    EXPECT_LT(off_bow.getTransforms()[ra].rot.w, 0.75f) << "Right arm drawing string must also be raised";
    EXPECT_NEAR(off_bow.getTransforms()[la].rot.y, off_bow.getTransforms()[ra].rot.y, 0.05f);

    // When idle, arms hang down
    EXPECT_GT(idle.getTransforms()[ra].rot.w, 0.95f);
    EXPECT_GT(idle.getTransforms()[la].rot.w, 0.95f);
}

TEST(SteveAnimatorTest, OnlyTheArmThatHoldsTheRaisedShieldMoves) {
    const size_t ra = static_cast<size_t>(mc::StevePart::RightArm), la = static_cast<size_t>(mc::StevePart::LeftArm);
    mc::SteveAnimator idle, main_shield, off_shield;
    mc::SteveAnimInput in{};
    mc::SteveAnimInput main_in = in, off_in = in;
    main_in.is_blocking = true;
    off_in.is_blocking = true;
    off_in.block_offhand = true;
    for (int i = 0; i < 10; ++i) {
        idle.update(0.05f, in);
        main_shield.update(0.05f, main_in);
        off_shield.update(0.05f, off_in);
    }
    EXPECT_GT(idle.getTransforms()[ra].rot.w, 0.97f);
    EXPECT_GT(idle.getTransforms()[la].rot.w, 0.97f);
    // shield in the main hand: the right arm is raised (Minecraft's ArmPose.BLOCK), the left one is not
    EXPECT_LT(main_shield.getTransforms()[ra].rot.w, 0.92f);
    EXPECT_GT(main_shield.getTransforms()[la].rot.w, 0.97f);
    // shield in the offhand: the left arm is raised, the right one is not
    EXPECT_LT(off_shield.getTransforms()[la].rot.w, 0.92f);
    EXPECT_GT(off_shield.getTransforms()[ra].rot.w, 0.97f);
}
