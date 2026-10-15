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

TEST(SteveAnimatorTest, ShieldBlockCombatStanceAndCrouchBracing) {
    const size_t ra = static_cast<size_t>(mc::StevePart::RightArm);
    const size_t la = static_cast<size_t>(mc::StevePart::LeftArm);

    mc::SteveAnimator anim_stand_block, anim_crouch_block, anim_walk_block, anim_walk_plain;
    mc::SteveAnimInput in_stand{}, in_crouch{}, in_wb{}, in_wp{};

    // 1. Standing block vs Crouching block
    in_stand.is_blocking = true;
    in_stand.block_offhand = true;

    in_crouch.is_blocking = true;
    in_crouch.block_offhand = true;
    in_crouch.is_crouching = true;

    anim_stand_block.update(0.05f, in_stand);
    anim_crouch_block.update(0.05f, in_crouch);

    const float stand_pitch = anim_stand_block.getTransforms()[la].rot.y;
    const float crouch_pitch = anim_crouch_block.getTransforms()[la].rot.y;

    // In vanilla Minecraft, crouching adds +0.4f pitch to the arms to align with the forward tilted torso
    EXPECT_NE(stand_pitch, crouch_pitch) << "Crouching must tilt shield arm to align with torso";
    EXPECT_GT(crouch_pitch, stand_pitch) << "Crouch pitch must be more forward/downward";

    // 2. Weapon arm swing is dampened while offhand blocks on the move
    in_wp.forward_speed = 4.317f;
    in_wb.forward_speed = 4.317f;
    in_wb.is_blocking = true;
    in_wb.block_offhand = true;

    for (int i = 0; i < 5; ++i) {
        anim_walk_plain.update(0.05f, in_wp);
        anim_walk_block.update(0.05f, in_wb);
    }

    const float plain_right_pitch = anim_walk_plain.getTransforms()[ra].rot.y;
    const float block_right_pitch = anim_walk_block.getTransforms()[ra].rot.y;

    // Plain walk swing reaches large angles (~0.4-0.8 rad); while blocking, right arm has dampened half-swing
    EXPECT_LT(std::fabs(block_right_pitch), std::fabs(plain_right_pitch) + 0.1f);
}


TEST(SteveAnimatorTest, SwordSwingPosesFollowsAuthenticArcAndHeadPitch) {
    const size_t ra = static_cast<size_t>(mc::StevePart::RightArm);
    const size_t la = static_cast<size_t>(mc::StevePart::LeftArm);
    const size_t body = static_cast<size_t>(mc::StevePart::Body);

    mc::SteveAnimator anim_level, anim_down, anim_up;
    mc::SteveAnimInput in_level{}, in_down{}, in_up{};

    in_level.swing_progress = 0.5f;

    in_down.swing_progress = 0.5f;
    in_down.look_pitch = 0.8f; // looking down (Minecraft: positive pitch is down)

    in_up.swing_progress = 0.5f;
    in_up.look_pitch = -0.8f; // looking up

    anim_level.update(0.05f, in_level);
    anim_down.update(0.05f, in_down);
    anim_up.update(0.05f, in_up);

    // 1. Torso twists during swing
    EXPECT_NE(anim_level.getTransforms()[body].rot.z, 0.0f) << "Body must twist during swing";

    // 2. Left arm reacts to torso twist
    EXPECT_NE(anim_level.getTransforms()[la].rot.z, 0.0f) << "Left arm must react to torso twist during swing";

    // 3. Head pitch modifies swing arc (looking down slashes lower than looking up)
    const float pitch_down = anim_down.getTransforms()[ra].rot.y;
    const float pitch_up = anim_up.getTransforms()[ra].rot.y;
    EXPECT_NE(pitch_down, pitch_up) << "Swing trajectory must track vertical look pitch";
    EXPECT_GT(std::fabs(pitch_down - pitch_up), 0.15f) << "Significant vertical swing angle difference";
}

TEST(SteveAnimatorTest, SneakCrouchingPosesTorsoAndArmsAuthentically) {
    const size_t body = static_cast<size_t>(mc::StevePart::Body);
    const size_t jacket = static_cast<size_t>(mc::StevePart::Jacket);
    const size_t ra = static_cast<size_t>(mc::StevePart::RightArm);
    const size_t la = static_cast<size_t>(mc::StevePart::LeftArm);
    const size_t head = static_cast<size_t>(mc::StevePart::Head);

    mc::SteveAnimator anim_stand, anim_sneak;
    mc::SteveAnimInput in_stand{}, in_sneak{};

    in_sneak.is_crouching = true;
    in_sneak.look_pitch = 0.2f;

    anim_stand.update(0.05f, in_stand);
    anim_sneak.update(0.05f, in_sneak);

    const auto& stand_t = anim_stand.getTransforms();
    const auto& sneak_t = anim_sneak.getTransforms();

    // 1. Body tilts forward when crouching (+0.5 rad pitch)
    EXPECT_GT(sneak_t[body].rot.y, 0.2f) << "Torso must pitch forward when crouching";
    EXPECT_NEAR(sneak_t[jacket].rot.y, sneak_t[body].rot.y, 1e-4f) << "Jacket must follow Body";

    // 2. Both arms tilt backward (+0.4 rad pitch) to hang along tilted torso
    EXPECT_GT(sneak_t[ra].rot.y, stand_t[ra].rot.y) << "Right arm must tilt backward along torso";
    EXPECT_GT(sneak_t[la].rot.y, stand_t[la].rot.y) << "Left arm must tilt backward along torso";

    // 3. Head follows independent look angles
    EXPECT_NEAR(sneak_t[head].rot.y, std::sin(0.2f * 0.5f), 0.05f) << "Head pitch must match look pitch";
}

TEST(SteveAnimatorTest, SneakCrouchingCompensatesLegsAndHeadPositionsAuthentically) {
    const size_t rl = static_cast<size_t>(mc::StevePart::RightLeg);
    const size_t ll = static_cast<size_t>(mc::StevePart::LeftLeg);
    const size_t rp = static_cast<size_t>(mc::StevePart::RightPants);
    const size_t lp = static_cast<size_t>(mc::StevePart::LeftPants);
    const size_t head = static_cast<size_t>(mc::StevePart::Head);
    const size_t hat = static_cast<size_t>(mc::StevePart::Hat);

    mc::SteveAnimator anim_stand, anim_sneak;
    mc::SteveAnimInput in_stand{}, in_sneak{};
    in_sneak.is_crouching = true;

    anim_stand.update(0.05f, in_stand);
    anim_sneak.update(0.05f, in_sneak);

    const auto& stand_t = anim_stand.getTransforms();
    const auto& sneak_t = anim_sneak.getTransforms();

    // Stand pose has zero translation offset
    EXPECT_FLOAT_EQ(stand_t[rl].pos.x, 0.f);
    EXPECT_FLOAT_EQ(stand_t[rl].pos.z, 0.f);
    EXPECT_FLOAT_EQ(stand_t[head].pos.z, 0.f);

    // Sneak crouch:
    // Legs move backward along canonical X (-22.5 cm, 4 pixels) to align with forward-leaning torso bottom (pelvis)
    EXPECT_NEAR(sneak_t[rl].pos.x, -22.5f, 1e-2f);
    EXPECT_NEAR(sneak_t[ll].pos.x, -22.5f, 1e-2f);
    EXPECT_NEAR(sneak_t[rp].pos.x, -22.5f, 1e-2f);
    EXPECT_NEAR(sneak_t[lp].pos.x, -22.5f, 1e-2f);

    // Legs move upward along canonical Z (+16.875 cm, 3 pixels) to clamp into pelvis socket
    EXPECT_NEAR(sneak_t[rl].pos.z, 16.875f, 1e-2f);
    EXPECT_NEAR(sneak_t[ll].pos.z, 16.875f, 1e-2f);
    EXPECT_NEAR(sneak_t[rp].pos.z, 16.875f, 1e-2f);
    EXPECT_NEAR(sneak_t[lp].pos.z, 16.875f, 1e-2f);

    // Head lowers along canonical Z (-5.625 cm, 1 pixel) to rest on sloped neck
    EXPECT_NEAR(sneak_t[head].pos.z, -5.625f, 1e-2f);
    EXPECT_NEAR(sneak_t[hat].pos.z, -5.625f, 1e-2f);
}

TEST(SteveAnimatorTest, WalkAndSprintKinematicsDynamics) {
    const size_t body = static_cast<size_t>(mc::StevePart::Body);
    const size_t head = static_cast<size_t>(mc::StevePart::Head);
    const size_t ra = static_cast<size_t>(mc::StevePart::RightArm);
    const size_t rl = static_cast<size_t>(mc::StevePart::RightLeg);
    const size_t ll = static_cast<size_t>(mc::StevePart::LeftLeg);

    mc::SteveAnimator anim_stand, anim_walk, anim_sprint, anim_sneak_walk;
    mc::SteveAnimInput in_stand{}, in_walk{}, in_sprint{}, in_sneak_walk{};

    // Stand: 0 m/s
    anim_stand.update(0.05f, in_stand);

    // Walk: 4.317 m/s (standard Minecraft walking speed)
    in_walk.forward_speed = 4.317f;

    // Sprint: 5.612 m/s (Minecraft sprint speed) with is_sprinting = true
    in_sprint.forward_speed = 5.612f;
    in_sprint.is_sprinting = true;

    // Sneak walk: 1.295 m/s (Minecraft sneak speed) with is_crouching = true
    in_sneak_walk.forward_speed = 1.295f;
    in_sneak_walk.is_crouching = true;

    // Step several frames to develop stride
    for (int i = 0; i < 10; ++i) {
        anim_walk.update(0.05f, in_walk);
        anim_sprint.update(0.05f, in_sprint);
        anim_sneak_walk.update(0.05f, in_sneak_walk);
    }

    const auto& t_stand = anim_stand.getTransforms();
    const auto& t_walk = anim_walk.getTransforms();
    const auto& t_sprint = anim_sprint.getTransforms();
    const auto& t_sneak_walk = anim_sneak_walk.getTransforms();

    // Verify stand pose is finite and neutral
    EXPECT_FLOAT_EQ(t_stand[body].rot.y, 0.0f);
    EXPECT_NEAR(t_sneak_walk[body].rot.y, std::sin(0.25f), 1e-4f); // crouch torso pitch 0.5 rad
    EXPECT_NE(t_sprint[ll].rot.y, 0.0f);

    // 1. Sprinting Torso Pitch Lean (冲刺俯冲前倾角):
    // In sprinting, torso pitches forward by ~0.15 rad (~8.5 deg) compared to standing/walking
    EXPECT_NEAR(t_walk[body].rot.y, 0.0f, 1e-3f) << "Walking torso stays upright";
    EXPECT_NEAR(t_sprint[body].rot.y, std::sin(0.075f), 1e-4f) << "Sprinting torso must pitch forward 0.15 rad";

    // 2. Sprinting Head Pitch Compensation (冲刺平视补偿):
    // Head pitch compensates against torso forward lean so eye-level gaze remains horizontal
    EXPECT_LT(t_sprint[head].rot.y, 0.0f) << "Head must tilt backward to compensate torso forward pitch and keep gaze level";

    // 3. Sprinting Stride and Arm swing amplitude (大步幅冲刺动力学):
    // Sprinting has larger amplitude than standard walk (1.25 vs 1.0)
    // Over the cycles, sprint arm rotation magnitude should exceed standard walk
    float max_walk_arm = 0.f, max_sprint_arm = 0.f;
    float max_walk_leg = 0.f, max_sprint_leg = 0.f;
    for (int i = 0; i < 40; ++i) {
        anim_walk.update(0.05f, in_walk);
        anim_sprint.update(0.05f, in_sprint);
        max_walk_arm = std::max(max_walk_arm, std::abs(anim_walk.getTransforms()[ra].rot.y));
        max_sprint_arm = std::max(max_sprint_arm, std::abs(anim_sprint.getTransforms()[ra].rot.y));
        max_walk_leg = std::max(max_walk_leg, std::abs(anim_walk.getTransforms()[rl].rot.y));
        max_sprint_leg = std::max(max_sprint_leg, std::abs(anim_sprint.getTransforms()[rl].rot.y));
    }
    EXPECT_GT(max_sprint_leg, max_walk_leg) << "Sprint stride leg swing amplitude must exceed walking amplitude";
    EXPECT_GT(max_sprint_arm, max_walk_arm) << "Sprint arm swing amplitude must exceed walking amplitude";

    // 4. Sneak walking dampening (潜行轻步挪移，杜绝大跨步穿帮):
    float max_sneak_leg = 0.f;
    for (int i = 0; i < 40; ++i) {
        anim_sneak_walk.update(0.05f, in_sneak_walk);
        max_sneak_leg = std::max(max_sneak_leg, std::abs(anim_sneak_walk.getTransforms()[rl].rot.y));
    }
    EXPECT_LT(max_sneak_leg, max_walk_leg * 0.7f) << "Sneak walking leg swing must be significantly dampened";
}

TEST(SteveAnimatorTest, JumpAndAirStanceKinematicsDynamics) {
    const size_t rl = static_cast<size_t>(mc::StevePart::RightLeg);
    const size_t ll = static_cast<size_t>(mc::StevePart::LeftLeg);
    const size_t ra = static_cast<size_t>(mc::StevePart::RightArm);
    const size_t la = static_cast<size_t>(mc::StevePart::LeftArm);

    mc::SteveAnimator anim_vert_jump, anim_vert_fall, anim_air_move;
    mc::SteveAnimInput in_vert_jump{}, in_vert_fall{}, in_air_move{};

    // 1. Pure vertical jump / fall (No horizontal keys pressed: forward_speed = 0, strafe_speed = 0)
    // Vanilla Minecraft: limbs remain completely straight, motionless and neutral (no bicycle, no artificial splay)
    in_vert_jump.forward_speed = 0.f;
    in_vert_jump.strafe_speed = 0.f;
    in_vert_jump.on_ground = false;
    in_vert_jump.vertical_speed = 4.5f;

    in_vert_fall.forward_speed = 0.f;
    in_vert_fall.strafe_speed = 0.f;
    in_vert_fall.on_ground = false;
    in_vert_fall.vertical_speed = -6.0f;

    // 2. Air movement / Running jump (WASD held in mid-air: forward_speed > 0)
    // Vanilla Minecraft: limbSwing is driven by horizontal displacement. Limbs continue their
    // natural rhythmic swinging in the air ("moonwalk / space-stepping" effect).
    in_air_move.forward_speed = 4.317f;
    in_air_move.on_ground = false;
    in_air_move.vertical_speed = 3.0f;

    for (int i = 0; i < 10; ++i) {
        anim_vert_jump.update(0.05f, in_vert_jump);
        anim_vert_fall.update(0.05f, in_vert_fall);
        anim_air_move.update(0.05f, in_air_move);
    }

    const auto& t_vert_jump = anim_vert_jump.getTransforms();
    const auto& t_vert_fall = anim_vert_fall.getTransforms();
    const auto& t_air_move = anim_air_move.getTransforms();

    // 1. Vertical Jump & Fall: Legs and arms remain completely straight and parallel
    EXPECT_FLOAT_EQ(t_vert_jump[rl].rot.y, 0.0f) << "Vertical jump legs stay completely straight (pitch == 0)";
    EXPECT_FLOAT_EQ(t_vert_jump[ll].rot.y, 0.0f) << "Vertical jump legs stay completely straight (pitch == 0)";
    EXPECT_FLOAT_EQ(t_vert_jump[rl].rot.x, 0.0f) << "Vertical jump legs do not artificially splay outward";
    EXPECT_FLOAT_EQ(t_vert_jump[ll].rot.x, 0.0f) << "Vertical jump legs do not artificially splay outward";

    EXPECT_FLOAT_EQ(t_vert_fall[rl].rot.y, 0.0f) << "Vertical fall legs stay completely straight (pitch == 0)";
    EXPECT_FLOAT_EQ(t_vert_fall[ll].rot.y, 0.0f) << "Vertical fall legs stay completely straight (pitch == 0)";

    // 2. Air Movement with Horizontal Speed: limbs swing rhythmically (authentic vanilla space-step)
    EXPECT_NE(t_air_move[rl].rot.y, 0.0f) << "Legs swing when moving horizontally in mid-air";
    EXPECT_NE(t_air_move[ll].rot.y, 0.0f) << "Legs swing when moving horizontally in mid-air";
    EXPECT_NE(t_air_move[ra].rot.y, 0.0f) << "Arms swing when moving horizontally in mid-air";
    EXPECT_NE(t_air_move[la].rot.y, 0.0f) << "Arms swing when moving horizontally in mid-air";
}



