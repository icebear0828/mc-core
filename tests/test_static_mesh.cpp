#include <gtest/gtest.h>

#include "mc/static_mesh.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace {

void putU32(std::string& out, uint32_t v) {
    const char bytes[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                           static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
    out.append(bytes, 4);
}

void putU16(std::string& out, uint16_t v) {
    const char bytes[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
    out.append(bytes, 2);
}

void putF32(std::string& out, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    putU32(out, bits);
}

void putString(std::string& out, std::string_view s) {
    putU32(out, static_cast<uint32_t>(s.size()));
    out.append(s);
}

std::string makeMesh(uint32_t vertex_count = 3, uint32_t index_count = 3, uint32_t material_count = 1) {
    std::string out;
    putU32(out, mc::model::StaticMesh::kMagic);
    putU32(out, mc::model::StaticMesh::kVersion);
    putU32(out, vertex_count);
    putU32(out, index_count);
    putU32(out, material_count);
    putString(out, "darkrai");
    putF32(out, 0.5f); // yaw_offset
    for (uint32_t m = 0; m < material_count; ++m) {
        putString(out, "body_a");
        putString(out, "darkrai.png");
        putU32(out, 0);
        putU32(out, index_count);
    }
    for (uint32_t i = 0; i < vertex_count; ++i) {
        putF32(out, static_cast<float>(i));
        putF32(out, 1.0f);
        putF32(out, 2.0f);
        putF32(out, 0.25f);
        putF32(out, 0.75f);
    }
    for (uint32_t i = 0; i < index_count; ++i) putU16(out, static_cast<uint16_t>(i % vertex_count));
    return out;
}

} // namespace

TEST(StaticMesh, ParsesAValidFile) {
    const std::string bytes = makeMesh();
    const auto mesh = mc::model::StaticMesh::fromBytes(bytes);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->identifier, "darkrai");
    EXPECT_FLOAT_EQ(mesh->yaw_offset, 0.5f);
    ASSERT_EQ(mesh->vertices.size(), 3u);
    ASSERT_EQ(mesh->indices.size(), 3u);
    ASSERT_EQ(mesh->materials.size(), 1u);
    EXPECT_EQ(mesh->materials[0].name, "body_a");
    EXPECT_EQ(mesh->materials[0].texture, "darkrai.png");
    EXPECT_EQ(mesh->materials[0].first_index, 0u);
    EXPECT_EQ(mesh->materials[0].index_count, 3u);
    EXPECT_FLOAT_EQ(mesh->vertices[2].x, 2.0f);
    EXPECT_FLOAT_EQ(mesh->vertices[0].u, 0.25f);
    EXPECT_FLOAT_EQ(mesh->vertices[0].v, 0.75f);
}

TEST(StaticMesh, RejectsABadMagic) {
    std::string bytes = makeMesh();
    bytes[0] = 'X';
    EXPECT_FALSE(mc::model::StaticMesh::fromBytes(bytes).has_value());
}

TEST(StaticMesh, RejectsAnUnknownVersion) {
    std::string bytes = makeMesh();
    bytes[4] = static_cast<char>(99);
    EXPECT_FALSE(mc::model::StaticMesh::fromBytes(bytes).has_value());
}

TEST(StaticMesh, RejectsTruncatedData) {
    const std::string bytes = makeMesh();
    EXPECT_FALSE(mc::model::StaticMesh::fromBytes(bytes.substr(0, bytes.size() - 1)).has_value());
    EXPECT_FALSE(mc::model::StaticMesh::fromBytes(bytes.substr(0, 10)).has_value());
}

TEST(StaticMesh, RejectsAnOutOfRangeMaterialRange) {
    std::string bytes = makeMesh();
    // the material's first_index sits right after its two strings; rewrite it to 2 with count 3
    const size_t material_pos = 4 + 4 + 4 + 4 + 4 + 4 + std::strlen("darkrai") + 4 + 4 + std::strlen("body_a") + 4 +
                                std::strlen("darkrai.png");
    bytes[material_pos] = 2; // first_index = 2
    EXPECT_FALSE(mc::model::StaticMesh::fromBytes(bytes).has_value());
}

TEST(StaticMesh, RejectsAnIndexPastTheVertexArray) {
    std::string bytes = makeMesh();
    const size_t indices_pos = bytes.size() - 3 * 2;
    bytes[indices_pos] = 5; // vertex_count is 3
    EXPECT_FALSE(mc::model::StaticMesh::fromBytes(bytes).has_value());
}

TEST(StaticMesh, RejectsMoreVerticesThanA16BitIndexCanAddress) {
    const std::string bytes = makeMesh(65536, 3, 1);
    EXPECT_FALSE(mc::model::StaticMesh::fromBytes(bytes).has_value());
}
