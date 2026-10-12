#include <gtest/gtest.h>
#include "mc/animation_library.hpp"

#include <fstream>
#include <sstream>

using namespace mc;
using namespace mc::anim;

TEST(AnimationLibraryTest, SampleKeyframesReturnsInterpolatedValues) {
    std::vector<VectorKeyframe> frames = {
        {0.0f, {0.0f, 0.0f, 0.0f}, "linear"},
        {1.0f, {10.0f, 20.0f, -30.0f}, "linear"},
        {2.0f, {20.0f, 0.0f, 0.0f}, "linear"}
    };

    // Before start
    auto v_before = sampleKeyframes(frames, -0.5f);
    EXPECT_FLOAT_EQ(v_before.x, 0.0f);
    EXPECT_FLOAT_EQ(v_before.y, 0.0f);

    // Midpoint at t = 0.5s
    auto v_mid = sampleKeyframes(frames, 0.5f);
    EXPECT_NEAR(v_mid.x, 5.0f, 1e-4f);
    EXPECT_NEAR(v_mid.y, 10.0f, 1e-4f);
    EXPECT_NEAR(v_mid.z, -15.0f, 1e-4f);

    // Beyond end
    auto v_after = sampleKeyframes(frames, 2.5f);
    EXPECT_FLOAT_EQ(v_after.x, 20.0f);
    EXPECT_FLOAT_EQ(v_after.y, 0.0f);
}

TEST(AnimationLibraryTest, ParseSteveBbmodelExtractsClipsAndTracks) {
    std::ifstream file("D:/steve.bbmodel");
    ASSERT_TRUE(file.is_open()) << "D:/steve.bbmodel should exist";

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    auto lib_opt = AnimationLibrary::fromBbmodelJson(content);
    ASSERT_TRUE(lib_opt.has_value());
    const auto& lib = *lib_opt;

    // Check known animations created in user's model
    EXPECT_GT(lib.clipCount(), 20u);
    EXPECT_TRUE(lib.hasClip("idle"));
    EXPECT_TRUE(lib.hasClip("walk"));
    EXPECT_TRUE(lib.hasClip("sprint"));
    EXPECT_TRUE(lib.hasClip("jump"));
    EXPECT_TRUE(lib.hasClip("sneak"));
    EXPECT_TRUE(lib.hasClip("block_shield"));

    const auto* walk = lib.findClip("walk");
    ASSERT_NE(walk, nullptr);
    EXPECT_FLOAT_EQ(walk->length, 1.0f);
    EXPECT_TRUE(walk->loop);

    // Check mapped bones in walk clip
    const auto* right_arm_track = walk->findTrack("rightArm");
    ASSERT_NE(right_arm_track, nullptr);
    EXPECT_FALSE(right_arm_track->rotation_frames.empty());

    const auto* left_leg_track = walk->findTrack("leftLeg");
    ASSERT_NE(left_leg_track, nullptr);
    EXPECT_FALSE(left_leg_track->rotation_frames.empty());
}

TEST(AnimationLibraryTest, ClipPlayerSamplesAndBlendsSmoothly) {
    std::ifstream file("D:/steve.bbmodel");
    ASSERT_TRUE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();

    auto lib_opt = AnimationLibrary::fromBbmodelJson(buffer.str());
    ASSERT_TRUE(lib_opt.has_value());
    const auto& lib = *lib_opt;

    ClipPlayer player(lib);
    EXPECT_TRUE(player.play("idle", 0.0f));
    EXPECT_EQ(player.currentClip(), "idle");

    // Advance idle
    player.update(0.1f);
    auto idle_pose = player.sampleBone("rightArm");
    EXPECT_FALSE(std::isnan(idle_pose.rotation.x));
    EXPECT_FALSE(std::isnan(idle_pose.rotation.w));

    // Transition from idle to walk with 0.2s blend
    EXPECT_TRUE(player.play("walk", 0.2f));
    EXPECT_EQ(player.currentClip(), "walk");

    // Sample during transition
    player.update(0.1f); // 50% blend
    auto blend_pose = player.sampleBone("rightArm");
    EXPECT_FALSE(std::isnan(blend_pose.rotation.x));
    EXPECT_FALSE(std::isnan(blend_pose.rotation.w));

    // Finish transition
    player.update(0.2f);
    auto walk_pose = player.sampleBone("rightArm");
    EXPECT_FALSE(std::isnan(walk_pose.rotation.x));
}

TEST(AnimationLibraryTest, SteveAnimationStateMachineTransitionsByInput) {
    std::ifstream file("D:/steve.bbmodel");
    ASSERT_TRUE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();

    auto lib_opt = AnimationLibrary::fromBbmodelJson(buffer.str());
    ASSERT_TRUE(lib_opt.has_value());
    const auto& lib = *lib_opt;

    SteveAnimationStateMachine sm(lib);
    EXPECT_EQ(sm.activeClip(), "idle");

    // 1. Walk input
    SteveAnimInput input{};
    input.forward_speed = 2.0f;
    sm.update(0.05f, input);
    EXPECT_EQ(sm.activeClip(), "walk");

    // Sample transforms during walk
    auto transforms = sm.getTransforms();
    EXPECT_FALSE(std::isnan(transforms[static_cast<size_t>(StevePart::RightLeg)].rot.x));
    EXPECT_FALSE(std::isnan(transforms[static_cast<size_t>(StevePart::LeftLeg)].rot.x));

    // 2. Sprint input
    input.forward_speed = 5.5f;
    sm.update(0.05f, input);
    EXPECT_EQ(sm.activeClip(), "sprint");

    // 3. Sneak input
    input.is_crouching = true;
    sm.update(0.05f, input);
    EXPECT_EQ(sm.activeClip(), "sneak");

    // 4. Shield blocking input
    input = SteveAnimInput{};
    input.is_blocking = true;
    sm.update(0.05f, input);
    EXPECT_EQ(sm.activeClip(), "block_shield");

    // 5. Attack swing input
    input = SteveAnimInput{};
    input.swing_progress = 0.5f;
    sm.update(0.05f, input);
    EXPECT_EQ(sm.activeClip(), "attack");

    // In attack swing, right arm pitches forward (positive in Blockbench -> negative in canonical pitch)
    auto attack_transforms = sm.getTransforms();
    const auto& right_arm_rot = attack_transforms[static_cast<size_t>(StevePart::RightArm)].rot;
    // Canonical pitch rotates about Y; negative pitch gives positive y in quaternion when pitched forward
    EXPECT_GT(right_arm_rot.y, 0.0f);

    // 6. Back to idle
    input = SteveAnimInput{};
    sm.update(0.05f, input);
    EXPECT_EQ(sm.activeClip(), "idle");
}
