#include "mc/entity_model.hpp"

#include <fstream>
#include <gtest/gtest.h>

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

