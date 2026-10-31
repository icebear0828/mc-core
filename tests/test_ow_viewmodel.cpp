#include <gtest/gtest.h>

#include "ow_viewmodel.hpp"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

using eldenring::ow::OwAnimator;
using eldenring::ow::OwModel;
using eldenring::ow::parseOwModel;

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + 4);
}
void appendI32(std::vector<uint8_t>& b, int32_t v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + 4);
}
void appendFloats(std::vector<uint8_t>& b, std::initializer_list<float> values) {
    for (float f : values) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&f);
        b.insert(b.end(), p, p + 4);
    }
}

// One triangle, one rigid submesh, CSVM v1.
std::vector<uint8_t> makeV1() {
    std::vector<uint8_t> b;
    b.insert(b.end(), {'C', 'S', 'V', 'M'});
    appendU32(b, 1); // version
    appendU32(b, 1); // submeshes
    appendU32(b, 3); // vertices
    appendU32(b, 3); // indices
    appendU32(b, 1);
    b.push_back('a');
    appendU32(b, 0); // bone index
    appendU32(b, 0); // first index
    appendI32(b, 0); // base vertex
    appendU32(b, 3); // index count
    for (int v = 0; v < 3; ++v) {
        appendFloats(b, {static_cast<float>(v), 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f});
    }
    appendU32(b, 0);
    appendU32(b, 1);
    appendU32(b, 2);
    return b;
}

TEST(OwViewmodelTest, ParsesRigidCsvmV1) {
    const std::vector<uint8_t> bytes = makeV1();
    OwModel model;
    ASSERT_TRUE(parseOwModel(bytes.data(), bytes.size(), model));
    EXPECT_EQ(model.vertices.size(), 3u);
    EXPECT_EQ(model.indices.size(), 3u);
    ASSERT_EQ(model.submeshes.size(), 1u);
    EXPECT_EQ(model.submeshes[0].index_count, 3u);
    EXPECT_FLOAT_EQ(model.vertices[2].pos[0], 2.f);
    EXPECT_FLOAT_EQ(model.vertices[0].normal[2], 1.f);
}

TEST(OwViewmodelTest, RejectsBadMagicAndTruncatedInput) {
    std::vector<uint8_t> bytes = makeV1();
    bytes[0] = 'X';
    OwModel model;
    EXPECT_FALSE(parseOwModel(bytes.data(), bytes.size(), model));

    bytes = makeV1();
    EXPECT_FALSE(parseOwModel(bytes.data(), 10, model));
    EXPECT_FALSE(parseOwModel(nullptr, 0, model));
}

TEST(OwViewmodelTest, RejectsSkinnedV4Submesh) {
    std::vector<uint8_t> b;
    b.insert(b.end(), {'C', 'S', 'V', 'M'});
    appendU32(b, 4); // version
    appendU32(b, 1); // submeshes
    appendU32(b, 0); // vertices
    appendU32(b, 0); // indices
    appendU32(b, 1);
    b.push_back('a');
    appendU32(b, 0); // bone index
    appendU32(b, 0); // first index
    appendI32(b, 0); // base vertex
    appendU32(b, 0); // index count
    appendU32(b, 1); // flags: bit 0 = skinned
    OwModel model;
    EXPECT_FALSE(parseOwModel(b.data(), b.size(), model));
}

TEST(OwViewmodelTest, PrimaryAttackThrustsForwardAndReturns) {
    OwAnimator anim;
    anim.tick(0.016f, 0, 0.0f);
    const float idle_z = anim.pose().base.m[11];

    anim.tick(0.016f, 2, 0.15f);
    EXPECT_LT(anim.pose().base.m[11] - idle_z, -0.10f) << "jab must thrust forward by at least 10 cm";

    anim.tick(0.016f, 2, 0.40f);
    EXPECT_NEAR(anim.pose().base.m[11], idle_z, 0.01f) << "gauntlet must return to rest after the jab";
}

TEST(OwViewmodelTest, RocketPunchWindsUpThenHeaves) {
    OwAnimator anim;
    anim.tick(0.016f, 0, 0.0f);
    const float idle_y = anim.pose().base.m[7];
    const float idle_z = anim.pose().base.m[11];

    anim.tick(0.016f, 3, 0.10f);
    EXPECT_GT(anim.pose().base.m[11] - idle_z, 0.02f) << "rocket punch must wind up and pull back";

    anim.tick(0.016f, 3, 0.40f);
    EXPECT_LE(anim.pose().base.m[11] - idle_z, -0.20f) << "heavy strike must thrust forward at least 20 cm";
    EXPECT_GE(anim.pose().base.m[7] - idle_y, 0.05f) << "heavy strike must heave upward at least 5 cm";

    anim.tick(0.016f, 3, 0.80f);
    EXPECT_NEAR(anim.pose().base.m[7], idle_y, 0.015f);
    EXPECT_NEAR(anim.pose().base.m[11], idle_z, 0.015f);
}

TEST(OwViewmodelTest, ProjectionUsesItsOwnFov) {
    eldenring::ow::OwViewmodelConfig wide;
    wide.fov_deg = 90.f;
    eldenring::ow::OwViewmodelConfig narrow;
    narrow.fov_deg = 60.f;
    const mc::rig::Mat4 a = eldenring::ow::computeOwProjection(1920, 1080, wide);
    const mc::rig::Mat4 b = eldenring::ow::computeOwProjection(1920, 1080, narrow);
    EXPECT_NE(a.m[0], b.m[0]);
}

} // namespace
