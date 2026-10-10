#include "mc/entity_model.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace mc::model {
namespace {

constexpr std::string_view kCreeper112 = R"({
    "format_version": "1.12.0",
    "minecraft:geometry": [
        {
            "description": {
                "identifier": "geometry.creeper",
                "texture_width": 64,
                "texture_height": 32
            },
            "bones": [
                {
                    "name": "head",
                    "pivot": [0, 18, 0],
                    "cubes": [
                        {"origin": [-4, 18, -4], "size": [8, 8, 8], "uv": [0, 0]}
                    ]
                },
                {
                    "name": "body",
                    "pivot": [0, 18, 0],
                    "cubes": [
                        {"origin": [-4, 6, -2], "size": [8, 12, 4], "uv": [16, 16], "inflate": 0.25}
                    ]
                },
                {
                    "name": "leg0",
                    "parent": "body",
                    "pivot": [-2, 6, 4],
                    "rotation": [0, 15, 0],
                    "cubes": [
                        {"origin": [-4, 0, 2], "size": [4, 6, 4], "uv": [0, 16], "mirror": true}
                    ]
                }
            ]
        }
    ]
})";

constexpr std::string_view kZombie18 = R"({
    "format_version": "1.8.0",
    "geometry.zombie": {
        "texturewidth": 64,
        "textureheight": 64,
        "bones": [
            {
                "name": "head",
                "pivot": [0, 24, 0],
                "cubes": [
                    {"origin": [-4, 24, -4], "size": [8, 8, 8], "uv": [0, 0]}
                ]
            }
        ]
    }
})";

TEST(EntityModel, ParsesBedrock112Format) {
    const auto opt = EntityModel::fromJson(kCreeper112);
    ASSERT_TRUE(opt.has_value());
    const auto& model = *opt;

    EXPECT_EQ(model.identifier, "geometry.creeper");
    EXPECT_FLOAT_EQ(model.texture_width, 64.0f);
    EXPECT_FLOAT_EQ(model.texture_height, 32.0f);
    ASSERT_EQ(model.bones.size(), 3u);

    const auto* head = model.findBone("head");
    ASSERT_NE(head, nullptr);
    EXPECT_FLOAT_EQ(head->pivot.y, 18.0f);
    ASSERT_EQ(head->cubes.size(), 1u);
    EXPECT_FLOAT_EQ(head->cubes[0].origin.x, -4.0f);
    EXPECT_FLOAT_EQ(head->cubes[0].origin.y, 18.0f);
    EXPECT_FLOAT_EQ(head->cubes[0].origin.z, -4.0f);
    EXPECT_FLOAT_EQ(head->cubes[0].size.x, 8.0f);
    EXPECT_FLOAT_EQ(head->cubes[0].size.y, 8.0f);
    EXPECT_FLOAT_EQ(head->cubes[0].size.z, 8.0f);
    EXPECT_FLOAT_EQ(head->cubes[0].inflate, 0.0f);
    EXPECT_FALSE(head->cubes[0].mirror);

    const auto* body = model.findBone("body");
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(body->cubes[0].inflate, 0.25f);

    const auto* leg0 = model.findBone("leg0");
    ASSERT_NE(leg0, nullptr);
    EXPECT_EQ(leg0->parent, "body");
    EXPECT_FLOAT_EQ(leg0->rotation.y, 15.0f);
    EXPECT_TRUE(leg0->cubes[0].mirror);
}

TEST(EntityModel, ParsesBedrock18Format) {
    const auto opt = EntityModel::fromJson(kZombie18);
    ASSERT_TRUE(opt.has_value());
    const auto& model = *opt;

    EXPECT_EQ(model.identifier, "geometry.zombie");
    EXPECT_FLOAT_EQ(model.texture_width, 64.0f);
    EXPECT_FLOAT_EQ(model.texture_height, 64.0f);
    ASSERT_EQ(model.bones.size(), 1u);
    EXPECT_NE(model.findBone("head"), nullptr);
}

TEST(EntityModel, FindBoneReturnsNullptrWhenNotFound) {
    const auto opt = EntityModel::fromJson(kZombie18);
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->findBone("non_existent_bone"), nullptr);
}

TEST(EntityModel, BuildBoneMeshGeneratesCorrectGeometry) {
    const auto opt = EntityModel::fromJson(kCreeper112);
    ASSERT_TRUE(opt.has_value());

    const rig::HostBasis basis{
        .forward = {1.f, 0.f, 0.f},
        .left = {0.f, 1.f, 0.f},
        .up = {0.f, 0.f, 1.f},
        .units_per_cm = 1.0f,
    };

    const rig::RigMesh head_mesh = opt->buildBoneMesh("head", basis);
    // 1 cube = 6 faces * 4 vertices = 24
    EXPECT_EQ(head_mesh.vertices.size(), 24u);
    // 6 faces * 2 triangles * 3 = 36 indices
    EXPECT_EQ(head_mesh.indices.size(), 36u);

    for (const auto& v : head_mesh.vertices) {
        EXPECT_GE(v.u, 0.0f);
        EXPECT_LE(v.u, 1.0f);
        EXPECT_GE(v.v, 0.0f);
        EXPECT_LE(v.v, 1.0f);
    }
}

TEST(EntityModel, BuildCombinedMeshContainsAllBones) {
    const auto opt = EntityModel::fromJson(kCreeper112);
    ASSERT_TRUE(opt.has_value());

    const rig::HostBasis basis{
        .forward = {1.f, 0.f, 0.f},
        .left = {0.f, 1.f, 0.f},
        .up = {0.f, 0.f, 1.f},
        .units_per_cm = 1.0f,
    };

    const rig::RigMesh combined = opt->buildCombinedMesh(basis);
    // 3 cubes total = 3 * 24 = 72 vertices, 3 * 36 = 108 indices
    EXPECT_EQ(combined.vertices.size(), 72u);
    EXPECT_EQ(combined.indices.size(), 108u);
}

TEST(EntityModel, RejectsMalformedJson) {
    EXPECT_FALSE(EntityModel::fromJson("not a json").has_value());
    EXPECT_FALSE(EntityModel::fromJson("{").has_value());
    EXPECT_FALSE(EntityModel::fromJson(R"({"format_version": "1.12.0"})").has_value());
}

namespace {
rig::HostBasis identityBasis() { return {.forward = {1.f, 0.f, 0.f}, .left = {0.f, 1.f, 0.f}, .up = {0.f, 0.f, 1.f}, .units_per_cm = 1.0f}; }

EntityModel oneCube(bool mirror) {
    EntityModel m;
    m.texture_width = 64.f;
    m.texture_height = 64.f;
    Bone b;
    b.name = "leg";
    Cube c;
    c.origin = {0.f, 0.f, 0.f};
    c.size = {4.f, 12.f, 4.f};
    c.u = 0.f;
    c.v = 16.f;
    c.mirror = mirror;
    b.cubes.push_back(c);
    m.bones.push_back(b);
    return m;
}

// min and max U (in pixels) over the four vertices of face `face` (0 north, 1 south, 2 west, 3 east, 4 top, 5 bottom)
std::pair<float, float> faceURange(const rig::RigMesh& mesh, int face) {
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 4; ++i) {
        const float u = mesh.vertices[static_cast<size_t>(face * 4 + i)].u * 64.f;
        lo = std::min(lo, u);
        hi = std::max(hi, u);
    }
    return {lo, hi};
}
} // namespace

TEST(EntityModel, MirroredCubeFlipsEveryFaceHorizontallyAndSwapsTheSideFaces) {
    const rig::RigMesh plain = oneCube(false).buildBoneMesh("leg", identityBasis());
    const rig::RigMesh mirrored = oneCube(true).buildBoneMesh("leg", identityBasis());
    ASSERT_EQ(plain.vertices.size(), 24u);
    ASSERT_EQ(mirrored.vertices.size(), 24u);
    // same rectangles on the skin for the front/back/top/bottom faces, but running the other way
    for (int face : {0, 1, 4, 5}) {
        EXPECT_EQ(faceURange(plain, face), faceURange(mirrored, face)) << face;
        EXPECT_NEAR(plain.vertices[static_cast<size_t>(face * 4)].u, mirrored.vertices[static_cast<size_t>(face * 4 + 1)].u, 1e-6f) << face;
        EXPECT_NEAR(plain.vertices[static_cast<size_t>(face * 4 + 1)].u, mirrored.vertices[static_cast<size_t>(face * 4)].u, 1e-6f) << face;
    }
    // the west and east faces take each other's rectangle (u 0..4 and 8..12 for u=0, depth 4, width 4)
    EXPECT_EQ(faceURange(plain, 2), (std::pair<float, float>{0.f, 4.f}));
    EXPECT_EQ(faceURange(plain, 3), (std::pair<float, float>{8.f, 12.f}));
    EXPECT_EQ(faceURange(mirrored, 2), (std::pair<float, float>{8.f, 12.f}));
    EXPECT_EQ(faceURange(mirrored, 3), (std::pair<float, float>{0.f, 4.f}));
    // and are flipped too: the first vertex of the west face was on the left of its rectangle, now it is on the right
    EXPECT_NEAR(plain.vertices[8].u * 64.f, 0.f, 1e-4f);
    EXPECT_NEAR(mirrored.vertices[8].u * 64.f, 12.f, 1e-4f);
}

namespace {
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

EntityModel zombieModel() {
    const auto opt = EntityModel::fromJson(readFile(std::string(MC_ASSET_DIR) + "/models/entities/zombie.geo.json"));
    EXPECT_TRUE(opt.has_value());
    return opt.value_or(EntityModel{});
}

size_t boneIndex(const EntityModel& m, std::string_view name) {
    for (size_t i = 0; i < m.bones.size(); ++i) {
        if (m.bones[i].name == name) return i;
    }
    ADD_FAILURE() << "no bone " << name;
    return 0;
}

// The host basis of Elden Ring (eldenring_steve.hpp kBasis): left-handed (a reflection), forward = +Z, left = -X, up = +Y, metres.
rig::HostBasis gameLikeBasis() { return {.forward = {0.f, 0.f, 1.f}, .left = {-1.f, 0.f, 0.f}, .up = {0.f, 1.f, 0.f}, .units_per_cm = 0.01f}; }
} // namespace

TEST(EntityPose, BonePivotIsTheModelPivotInHostUnits) {
    const EntityModel z = zombieModel();
    const auto basis = identityBasis();
    const Vec3 p = bonePivotHost(z.bones[boneIndex(z, "right_arm")], basis); // bedrock (-5, 22, 0)
    EXPECT_NEAR(p.x, 0.f, 1e-4f);                                            // forward = -z
    EXPECT_NEAR(p.y, -5.f * rig::kCmPerModelPixel, 1e-3f);                   // left = +x: the right arm is on the right
    EXPECT_NEAR(p.z, 22.f * rig::kCmPerModelPixel, 1e-3f);
}

TEST(EntityPose, TheZombiesRestRotationHoldsTheArmsStraightOutInFront) {
    const EntityModel z = zombieModel();
    for (const auto& basis : {identityBasis(), gameLikeBasis()}) {
        const auto mats = boneMatrices(z, {}, {0.f, 0.f, 0.f}, 0.f, basis);
        const size_t arm = boneIndex(z, "right_arm");
        // the hand: the bottom of the arm (bedrock y = 12) in the middle of its box
        const Vec3 hand_rest = basis.fromCanonical({0.f, -6.f * rig::kCmPerModelPixel, 12.f * rig::kCmPerModelPixel});
        const Vec3 pivot = bonePivotHost(z.bones[arm], basis);
        const Vec3 hand = rig::transformPoint(mats[arm], hand_rest);
        const Vec3 arm_dir = (hand - pivot).normalized();
        const Vec3 forward = basis.forward.normalized();
        EXPECT_GT(arm_dir.x * forward.x + arm_dir.y * forward.y + arm_dir.z * forward.z, 0.95f) << "reflection=" << basis.isReflection();
        // and the length of the arm is kept
        // (the hand point is 1 px to the side of the pivot and 10 px below it: sqrt(101) px)
        EXPECT_NEAR((hand - pivot).length(), std::sqrt(101.f) * rig::kCmPerModelPixel * basis.units_per_cm, 1e-3f * basis.units_per_cm * 100.f);
    }
}

TEST(EntityPose, ABoneWithoutRestRotationStaysWhereTheMeshIs) {
    const EntityModel z = zombieModel();
    const auto basis = identityBasis();
    const auto mats = boneMatrices(z, {}, {0.f, 0.f, 0.f}, 0.f, basis);
    const size_t body = boneIndex(z, "body");
    const Vec3 p = basis.fromCanonical({1.f, 2.f, 3.f});
    const Vec3 q = rig::transformPoint(mats[body], p);
    EXPECT_NEAR(q.x, p.x, 1e-4f);
    EXPECT_NEAR(q.y, p.y, 1e-4f);
    EXPECT_NEAR(q.z, p.z, 1e-4f);
}

TEST(EntityPose, ChildBonesFollowTheirParentsRotation) {
    const EntityModel z = zombieModel();
    const auto basis = identityBasis();
    std::vector<Quat> extra(z.bones.size());
    // turn the head 90 degrees about the vertical axis (canonical +Z, identity basis: host +Z)
    const float half = 0.5f * 1.5707963f;
    extra[boneIndex(z, "head")] = {0.f, 0.f, std::sin(half), std::cos(half)};
    const auto mats = boneMatrices(z, extra, {0.f, 0.f, 0.f}, 0.f, basis);
    const Vec3 on_hat = basis.fromCanonical({0.f, 10.f, 28.f * rig::kCmPerModelPixel}); // a point above the head
    const Vec3 hat_moved = rig::transformPoint(mats[boneIndex(z, "hat")], on_hat);
    const Vec3 head_moved = rig::transformPoint(mats[boneIndex(z, "head")], on_hat);
    EXPECT_NEAR(hat_moved.x, head_moved.x, 1e-3f); // the hat turns exactly like the head
    EXPECT_NEAR(hat_moved.y, head_moved.y, 1e-3f);
    EXPECT_GT(std::fabs(hat_moved.x - on_hat.x), 1.f); // and the head really moved
    // the body is not a child of the head
    const Vec3 body_point = rig::transformPoint(mats[boneIndex(z, "body")], on_hat);
    EXPECT_NEAR(body_point.x, on_hat.x, 1e-4f);
}

TEST(EntityPose, TheRootMovesAndTurnsTheWholeFigure) {
    const EntityModel z = zombieModel();
    const auto basis = identityBasis();
    const auto mats = boneMatrices(z, {}, {100.f, 0.f, 0.f}, 0.f, basis);
    const Vec3 q = rig::transformPoint(mats[boneIndex(z, "body")], {0.f, 0.f, 0.f});
    EXPECT_NEAR(q.x, 100.f, 1e-3f);
    const auto turned = boneMatrices(z, {}, {0.f, 0.f, 0.f}, 3.14159265f, basis); // half a turn about up
    const Vec3 r = rig::transformPoint(turned[boneIndex(z, "body")], {10.f, 0.f, 0.f});
    EXPECT_NEAR(r.x, -10.f, 1e-3f);
}

TEST(EntityPose, HumanoidBonesMapToTheStevePartsTheAnimatorDrives) {
    EXPECT_EQ(humanoidPartForBone("head"), StevePart::Head);
    EXPECT_EQ(humanoidPartForBone("left_leg"), StevePart::LeftLeg);
    EXPECT_EQ(humanoidPartForBone("right_sleeve"), StevePart::RightSleeve);
    EXPECT_EQ(humanoidPartForBone("jacket"), StevePart::Jacket);
    EXPECT_FALSE(humanoidPartForBone("tail").has_value());
}

TEST(EntityModel, ParsesActualZombieGeoJsonFile) {
    const std::string path = std::string(MC_ASSET_DIR) + "/models/entities/zombie.geo.json";
    std::ifstream file(path);
    ASSERT_TRUE(file.is_open()) << "Failed to open " << path;
    std::stringstream ss;
    ss << file.rdbuf();

    const auto opt = EntityModel::fromJson(ss.str());
    ASSERT_TRUE(opt.has_value());
    const auto& model = *opt;

    EXPECT_EQ(model.identifier, "geometry.zombie");
    EXPECT_FLOAT_EQ(model.texture_width, 64.0f);
    EXPECT_FLOAT_EQ(model.texture_height, 64.0f);
    ASSERT_EQ(model.bones.size(), 12u);

    const auto* head = model.findBone("head");
    ASSERT_NE(head, nullptr);
    const auto* right_arm = model.findBone("right_arm");
    ASSERT_NE(right_arm, nullptr);
    EXPECT_FLOAT_EQ(right_arm->rotation.x, -90.0f);

    const rig::HostBasis basis{
        .forward = {1.f, 0.f, 0.f},
        .left = {0.f, 1.f, 0.f},
        .up = {0.f, 0.f, 1.f},
        .units_per_cm = 1.0f,
    };
    const rig::RigMesh mesh = model.buildCombinedMesh(basis);
    EXPECT_GT(mesh.vertices.size(), 0u);
    EXPECT_GT(mesh.indices.size(), 0u);
}

TEST(EntityModel, ParsesActualCreeperAndSkeletonGeoJsonFiles) {
    for (const auto& [mob_id, filename] : {std::pair{"geometry.creeper", "creeper.geo.json"}, {"geometry.skeleton", "skeleton.geo.json"}}) {
        const std::string path = std::string(MC_ASSET_DIR) + "/models/entities/" + filename;
        std::ifstream file(path);
        ASSERT_TRUE(file.is_open()) << "Failed to open " << path;
        std::stringstream ss;
        ss << file.rdbuf();

        const auto opt = EntityModel::fromJson(ss.str());
        ASSERT_TRUE(opt.has_value());
        EXPECT_EQ(opt->identifier, mob_id);
    }
}

} // namespace
} // namespace mc::model

