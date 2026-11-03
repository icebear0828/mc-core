// Validates the custom Blockbench recreations of the elytra wings and the trident (issues #38 / #39).
//
// The data lives in assets/source/models/entities/{trident,elytra_wings}.geo.json. It is a custom
// recreation extracted from the bundled Blockbench preview
// (re_data_cache/model_data.js), not a Mojang asset. These tests only check the geometry tables; the
// renderer attachment, atlas regions and in-game screenshots are still open (see the draft PR).

#include "mc/entity_model.hpp"
#include "mc/elytra_models.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace mc::model {
namespace {

std::string readAsset(const std::string& relative) {
    std::ifstream file(std::string(MC_ASSET_DIR) + relative, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

struct Bounds {
    Vec3 min{1e9f, 1e9f, 1e9f};
    Vec3 max{-1e9f, -1e9f, -1e9f};
};

Bounds boundsOf(const Bone& bone) {
    Bounds b;
    for (const Cube& c : bone.cubes) {
        b.min.x = std::min(b.min.x, c.origin.x);
        b.min.y = std::min(b.min.y, c.origin.y);
        b.min.z = std::min(b.min.z, c.origin.z);
        b.max.x = std::max(b.max.x, c.origin.x + c.size.x);
        b.max.y = std::max(b.max.y, c.origin.y + c.size.y);
        b.max.z = std::max(b.max.z, c.origin.z + c.size.z);
    }
    return b;
}

rig::HostBasis identityBasis() {
    return {.forward = {1.f, 0.f, 0.f}, .left = {0.f, 1.f, 0.f}, .up = {0.f, 0.f, 1.f}, .units_per_cm = 1.0f};
}

} // namespace

TEST(ElytraTridentModels, TridentFileParsesWithAnchorAndPropBones) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/trident.geo.json"));
    ASSERT_TRUE(opt.has_value());
    const EntityModel& model = *opt;

    EXPECT_EQ(model.identifier, "geometry.trident");
    ASSERT_EQ(model.bones.size(), 2u);

    const Bone* arm = model.findBone("rightArm");
    ASSERT_NE(arm, nullptr);
    EXPECT_TRUE(arm->cubes.empty());
    EXPECT_FLOAT_EQ(arm->pivot.x, -5.0f);
    EXPECT_FLOAT_EQ(arm->pivot.y, 22.0f);

    const Bone* trident = model.findBone("trident");
    ASSERT_NE(trident, nullptr);
    EXPECT_EQ(trident->parent, "rightArm");
    EXPECT_FLOAT_EQ(trident->pivot.x, -6.0f);
    EXPECT_FLOAT_EQ(trident->pivot.y, 13.0f);
    EXPECT_FLOAT_EQ(trident->pivot.z, 0.0f);
    ASSERT_EQ(trident->cubes.size(), 5u);
}

TEST(ElytraTridentModels, TridentShaftAndProngsMatchTheExtractedData) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/trident.geo.json"));
    ASSERT_TRUE(opt.has_value());
    const Bone* trident = opt->findBone("trident");
    ASSERT_NE(trident, nullptr);

    // Vanilla TridentModel: pole addBox(-0.5,2,-0.5,1,25,1) at the grip pivot (-6,13,0) (Java y down, so
    // Bedrock y = -java y; the pole hangs below the hand, the spikes point up).
    const Cube& shaft = trident->cubes[0];
    EXPECT_FLOAT_EQ(shaft.origin.x, -6.5f);
    EXPECT_FLOAT_EQ(shaft.origin.y, 0.5f);
    EXPECT_FLOAT_EQ(shaft.origin.z, -0.5f);
    EXPECT_FLOAT_EQ(shaft.size.x, 1.0f);
    EXPECT_FLOAT_EQ(shaft.size.y, 25.0f);
    EXPECT_FLOAT_EQ(shaft.size.z, 1.0f);

    // base addBox(-1.5,0,-0.5,3,2,1), then left/middle/right spikes addBox(...,1,4,1)
    const Cube& base = trident->cubes[1];
    EXPECT_FLOAT_EQ(base.origin.y, 25.5f);
    EXPECT_FLOAT_EQ(base.size.x, 3.0f);
    const Cube& middle = trident->cubes[3];
    EXPECT_FLOAT_EQ(middle.origin.y, 27.5f);
    EXPECT_FLOAT_EQ(trident->cubes[2].origin.x, -8.5f);
    EXPECT_FLOAT_EQ(trident->cubes[4].origin.x, -4.5f);
    EXPECT_TRUE(trident->cubes[4].mirror);

    // overall bounding box: spikes x -8.5..-3.5, pole y -14..17, the flat 1px slab z -0.5..0.5
    const Bounds b = boundsOf(*trident);
    EXPECT_FLOAT_EQ(b.min.x, -8.5f);
    EXPECT_FLOAT_EQ(b.max.x, -3.5f);
    EXPECT_FLOAT_EQ(b.min.y, 0.5f);
    EXPECT_FLOAT_EQ(b.max.y, 31.5f);
    EXPECT_FLOAT_EQ(b.min.z, -0.5f);
    EXPECT_FLOAT_EQ(b.max.z, 0.5f);
}

TEST(ElytraTridentModels, ElytraFileHasTwoWingsHangingFromTheUpperBody) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/elytra_wings.geo.json"));
    ASSERT_TRUE(opt.has_value());
    const EntityModel& model = *opt;

    EXPECT_EQ(model.identifier, "geometry.elytra_wings");
    ASSERT_EQ(model.bones.size(), 3u);

    const Bone* upper = model.findBone("upper");
    ASSERT_NE(upper, nullptr);
    EXPECT_TRUE(upper->cubes.empty());
    EXPECT_FLOAT_EQ(upper->pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(upper->pivot.y, 24.0f);
    EXPECT_FLOAT_EQ(upper->pivot.z, 0.0f);

    // Vanilla ElytraModel: left_wing at (5,0,0) and right_wing at (-5,0,0),
    // both one 10x20x2 slab. Java y runs down, so Bedrock y = 24 - java y.
    // The pose rotations are applied at runtime, so the files carry none.
    const Bone* right = model.findBone("rightWing");
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->parent, "upper");
    EXPECT_FLOAT_EQ(right->pivot.x, -5.0f);
    EXPECT_FLOAT_EQ(right->pivot.y, 24.0f);
    EXPECT_FLOAT_EQ(right->pivot.z, 2.0f);
    ASSERT_EQ(right->cubes.size(), 1u);

    const Bone* left = model.findBone("leftWing");
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->parent, "upper");
    EXPECT_FLOAT_EQ(left->pivot.x, 5.0f);
    EXPECT_FLOAT_EQ(left->pivot.y, 24.0f);
    EXPECT_FLOAT_EQ(left->pivot.z, 2.0f);
    ASSERT_EQ(left->cubes.size(), 1u);

    for (const Bone* wing : {left, right}) {
        EXPECT_FLOAT_EQ(wing->rotation.x, 0.0f);
        EXPECT_FLOAT_EQ(wing->rotation.y, 0.0f);
        EXPECT_FLOAT_EQ(wing->rotation.z, 0.0f);
    }
}

TEST(ElytraTridentModels, WingSlabsAreTenByTwentyPixelsAndMirrored) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/elytra_wings.geo.json"));
    ASSERT_TRUE(opt.has_value());

    const Bone* right = opt->findBone("rightWing");
    const Bone* left = opt->findBone("leftWing");
    ASSERT_NE(right, nullptr);
    ASSERT_NE(left, nullptr);

    // Vanilla left_wing addBox(-10,0,0,10,20,2) at (5,0,0) and right_wing
    // addBox(0,0,0,10,20,2).mirror() at (-5,0,0). In Bedrock space both cubes
    // are the same slab at x -5..5; only the right one is mirrored in UV.
    for (const Bone* wing : {left, right}) {
        EXPECT_FLOAT_EQ(wing->cubes[0].origin.x, -5.0f);
        EXPECT_FLOAT_EQ(wing->cubes[0].origin.y, 4.0f);
        EXPECT_FLOAT_EQ(wing->cubes[0].origin.z, 2.0f);
        EXPECT_FLOAT_EQ(wing->cubes[0].size.x, 10.0f);
        EXPECT_FLOAT_EQ(wing->cubes[0].size.y, 20.0f);
        EXPECT_FLOAT_EQ(wing->cubes[0].size.z, 2.0f);
        EXPECT_FLOAT_EQ(wing->cubes[0].inflate, 1.0f);
    }
    EXPECT_FALSE(left->cubes[0].mirror);
    EXPECT_TRUE(right->cubes[0].mirror);

    const Bounds r = boundsOf(*right);
    EXPECT_FLOAT_EQ(r.min.x, -5.0f);
    EXPECT_FLOAT_EQ(r.max.x, 5.0f);
    EXPECT_FLOAT_EQ(r.min.y, 4.0f);
    EXPECT_FLOAT_EQ(r.max.y, 24.0f);
    const Bounds l = boundsOf(*left);
    EXPECT_FLOAT_EQ(l.min.x, -5.0f);
    EXPECT_FLOAT_EQ(l.max.x, 5.0f);
    EXPECT_FLOAT_EQ(l.min.y, 4.0f);
    EXPECT_FLOAT_EQ(l.max.y, 24.0f);
}

TEST(ElytraTridentModels, CrouchFileDropsTheWingsThreePixels) {
    // Vanilla ElytraModel.setupAnim sets leftWing.y = isCrouching ? 3 : 0, so only crouching drops the wings.
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/elytra_wings_crouch.geo.json"));
    ASSERT_TRUE(opt.has_value());
    for (const char* name : {"leftWing", "rightWing"}) {
        const Bone* wing = opt->findBone(name);
        ASSERT_NE(wing, nullptr) << name;
        EXPECT_FLOAT_EQ(wing->pivot.y, 21.0f) << name;
        EXPECT_FLOAT_EQ(wing->cubes[0].origin.y, 1.0f) << name;
        EXPECT_FLOAT_EQ(wing->rotation.x, 0.0f) << name;
    }
    EXPECT_TRUE(opt->findBone("rightWing")->cubes[0].mirror);
}

TEST(ElytraTridentModels, WingPosesMatchTheVanillaElytraAnimationState) {
    // PlayerElytraAnimationState.tick() targets, in degrees.
    const WingPose standing = elytraWingPose(false, 1.f, false);
    EXPECT_FLOAT_EQ(standing.x_rot, 15.0f);
    EXPECT_FLOAT_EQ(standing.y_rot, 0.0f);
    EXPECT_FLOAT_EQ(standing.z_rot, -15.0f);

    const WingPose crouching = elytraWingPose(false, 1.f, true);
    EXPECT_FLOAT_EQ(crouching.x_rot, 40.0f);
    EXPECT_FLOAT_EQ(crouching.y_rot, 5.0f);
    EXPECT_FLOAT_EQ(crouching.z_rot, -45.0f);

    // Level flight (f = 1): fully spread. A steep dive (f = 0): folded back to the standing angle.
    const WingPose level = elytraWingPose(true, 1.f, false);
    EXPECT_FLOAT_EQ(level.x_rot, 20.0f);
    EXPECT_FLOAT_EQ(level.z_rot, -90.0f);
    const WingPose dive = elytraWingPose(true, 0.f, false);
    EXPECT_FLOAT_EQ(dive.x_rot, 15.0f);
    EXPECT_FLOAT_EQ(dive.z_rot, -15.0f);
    const WingPose half = elytraWingPose(true, 0.5f, false);
    EXPECT_FLOAT_EQ(half.x_rot, 17.5f);
    EXPECT_FLOAT_EQ(half.z_rot, -52.5f);

    // Out-of-range blends clamp like vanilla's velocity ratio.
    EXPECT_FLOAT_EQ(elytraWingPose(true, 2.f, false).z_rot, -90.0f);
    EXPECT_FLOAT_EQ(elytraWingPose(true, -1.f, false).z_rot, -15.0f);
}

TEST(ElytraTridentModels, BedrockEulerQuatMatchesTheRestRotation) {
    // The runtime pose must produce exactly the matrix a per-pose file would.
    const mc::rig::HostBasis bases[] = {
        identityBasis(),
        {.forward = {0.f, 0.f, 1.f}, .left = {-1.f, 0.f, 0.f}, .up = {0.f, 1.f, 0.f}, .units_per_cm = 0.01f}, // Elden Ring
    };
    const Vec3 angles[] = {{15.f, 0.f, -15.f}, {20.f, 0.f, -90.f}, {40.f, 5.f, -45.f}, {-30.f, 70.f, 120.f}};
    for (const mc::rig::HostBasis& basis : bases) {
        for (const Vec3& a : angles) {
            Bone bone;
            bone.rotation = a;
            const mc::rig::Mat4 expected = boneRestRotation(bone, basis);
            const mc::rig::Mat4 actual = mc::rig::rotationFromQuat(bedrockEulerQuat(a.x, a.y, a.z, basis));
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    EXPECT_NEAR(expected.at(r, c), actual.at(r, c), 1e-5f)
                        << "angles " << a.x << "," << a.y << "," << a.z << " at " << r << "," << c;
                }
            }
        }
    }
}

TEST(ElytraTridentModels, BothModelsBuildMeshesWithoutTouchingTheSteveRig) {
    const auto trident = EntityModel::fromJson(readAsset("/models/entities/trident.geo.json"));
    const auto wings = EntityModel::fromJson(readAsset("/models/entities/elytra_wings.geo.json"));
    ASSERT_TRUE(trident.has_value());
    ASSERT_TRUE(wings.has_value());

    // 5 cubes -> 5 * 24 vertices / 5 * 36 indices; 2 wing cubes -> 48 / 72.
    EXPECT_EQ(trident->buildCombinedMesh(identityBasis()).vertices.size(), 5u * 24u);
    EXPECT_EQ(trident->buildCombinedMesh(identityBasis()).indices.size(), 5u * 36u);
    EXPECT_EQ(wings->buildCombinedMesh(identityBasis()).vertices.size(), 2u * 24u);
    EXPECT_EQ(wings->buildCombinedMesh(identityBasis()).indices.size(), 2u * 36u);

    // The prop models are separate data: the 12-part Steve rig is untouched.
    EXPECT_EQ(static_cast<size_t>(StevePart::Count), 12u);
}

TEST(ElytraTridentModels, EveryRegisteredWingPoseHasItsOwnModelFile) {
    // Regression: the loader only registered "elytra_wings", so the crouch pose had no model and the
    // wings disappeared the moment the player crouched. Every name the renderer can pick
    // must resolve to a parseable file with both wings.
    ASSERT_EQ(std::size(mc::model::kElytraWingPoses), 2u);
    for (const char* pose : mc::model::kElytraWingPoses) {
        const std::string path = std::string("/models/entities/") + pose + ".geo.json";
        const auto opt = EntityModel::fromJson(readAsset(path));
        ASSERT_TRUE(opt.has_value()) << path;
        EXPECT_NE(opt->findBone("leftWing"), nullptr) << path;
        EXPECT_NE(opt->findBone("rightWing"), nullptr) << path;
        EXPECT_EQ(opt->buildCombinedMesh(identityBasis()).vertices.size(), 2u * 24u) << path;
    }
}

} // namespace mc::model
