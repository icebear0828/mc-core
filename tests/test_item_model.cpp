#include <gtest/gtest.h>

#include "mc/item_model.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace mc::rig;
using mc::ItemId;
using mc::Vec3;

namespace {

// Same host as Sekiro: Y up, Z forward, X right, metres, left-handed.
const HostBasis kSekiro{{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}, 0.01f};
const HostBasis kCanonical{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 1.f};

struct Atlas {
    int w, h;
    std::vector<uint8_t> rgba;
    Atlas(int w_, int h_) : w(w_), h(h_), rgba(static_cast<size_t>(w_) * h_ * 4, 0) {}
    void set(int x, int y, uint8_t a = 255) {
        const size_t i = (static_cast<size_t>(y) * w + x) * 4;
        rgba[i] = 200;
        rgba[i + 1] = 100;
        rgba[i + 2] = 50;
        rgba[i + 3] = a;
    }
    ItemSprite sprite(int x, int y, int cw, int ch) const { return {rgba.data(), w, h, x, y, cw, ch}; }
};

size_t quadCount(const RigMesh& m) { return m.indices.size() / 6; }

} // namespace

TEST(ItemModelTest, ASingleOpaquePixelIsAClosedBoxOfSixQuads) {
    Atlas a(32, 32);
    a.set(5, 7);
    const RigMesh m = buildHeldItemMesh(a.sprite(4, 6, 16, 16), HeldItemStyle::Generated, kCanonical);
    EXPECT_EQ(quadCount(m), 6u);
    EXPECT_EQ(m.vertices.size(), 24u);
    for (uint16_t i : m.indices) EXPECT_LT(i, m.vertices.size());
}

TEST(ItemModelTest, SideFacesAppearOnlyWhereTheNeighbourIsTransparent) {
    Atlas a(16, 16);
    for (int y = 3; y < 5; ++y) {
        for (int x = 3; x < 5; ++x) a.set(x, y); // a 2x2 block of pixels
    }
    const RigMesh m = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Generated, kCanonical);
    // 4 front + 4 back + the 8 boundary edges of the 2x2 square; the 4 shared inner edges get no side quad
    EXPECT_EQ(quadCount(m), 4u + 4u + 8u);
}

TEST(ItemModelTest, TransparentPixelsAndAlphaBelowHalfAreSkippedAndAnEmptySpriteIsEmpty) {
    Atlas a(16, 16);
    a.set(1, 1, 255);
    a.set(2, 1, 100); // translucent: cut out like the shader does (alpha < 0.5)
    EXPECT_EQ(quadCount(buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Generated, kCanonical)), 6u);
    Atlas empty(16, 16);
    const RigMesh m = buildHeldItemMesh(empty.sprite(0, 0, 16, 16), HeldItemStyle::Generated, kCanonical);
    EXPECT_TRUE(m.vertices.empty());
    EXPECT_TRUE(m.indices.empty());
}

TEST(ItemModelTest, UvPointsAtTheCentreOfTheSourcePixelInsideTheAtlas) {
    Atlas a(64, 64);
    a.set(20 + 3, 30 + 9);
    const RigMesh m = buildHeldItemMesh(a.sprite(20, 30, 16, 16), HeldItemStyle::Handheld, kCanonical);
    ASSERT_FALSE(m.vertices.empty());
    for (const auto& v : m.vertices) {
        EXPECT_NEAR(v.u, (23.f + 0.5f) / 64.f, 1e-6f);
        EXPECT_NEAR(v.v, (39.f + 0.5f) / 64.f, 1e-6f);
    }
}

TEST(ItemModelTest, TheThicknessIsOneSpritePixelScaledByTheDisplayScale) {
    Atlas a(16, 16);
    a.set(8, 8);
    // Generated items are shown at scale 0.55 of a 16-pixel block; one sprite pixel is 1 model pixel * 0.55.
    const RigMesh m = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Generated, kCanonical);
    float lo = 1e9f, hi = -1e9f;
    for (const auto& v : m.vertices) {
        const float d = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    const float px = kCmPerModelPixel * 0.55f; // cm per sprite pixel
    // the box has edge `px`: its diagonal spread is at most sqrt(3) * px
    float xmin = 1e9f, xmax = -1e9f;
    for (const auto& v : m.vertices) { xmin = std::min(xmin, v.x); xmax = std::max(xmax, v.x); }
    EXPECT_GT(hi - lo, 0.f);
    EXPECT_LE(xmax - xmin, px * 1.75f);
}

// ---- where it ends up: MC's ItemInHandLayer + the model's display transform ----------------------

TEST(ItemModelTest, ASwordSitsInTheRightHandWithTheBladePointingForward) {
    // A diagonal "sword": handle bottom-left, tip top-right of a 16x16 sprite.
    Atlas a(16, 16);
    for (int i = 0; i < 14; ++i) a.set(1 + i, 14 - i);
    const RigMesh m = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Handheld, kCanonical);
    ASSERT_FALSE(m.vertices.empty());

    float fwd_lo = 1e9f, fwd_hi = -1e9f, up_lo = 1e9f, up_hi = -1e9f, left_lo = 1e9f, left_hi = -1e9f;
    for (const auto& v : m.vertices) {
        fwd_lo = std::min(fwd_lo, v.x); fwd_hi = std::max(fwd_hi, v.x);
        left_lo = std::min(left_lo, v.y); left_hi = std::max(left_hi, v.y);
        up_lo = std::min(up_lo, v.z); up_hi = std::max(up_hi, v.z);
    }
    // canonical rig frame: X forward, Y left, Z up, feet at the origin, cm. The right hand hangs about
    // 24 - 2 - 10 = 12 model pixels above the ground and 5 px to the character's right.
    const float hand_up = (24.f - 2.f - 10.f) * kCmPerModelPixel;
    EXPECT_GT(fwd_hi, 10.f * kCmPerModelPixel * 0.8f) << "the blade reaches out in front of the character";
    EXPECT_NEAR(up_hi, hand_up, 5.f * kCmPerModelPixel) << "level with the hand";
    EXPECT_LT(left_lo, 0.f) << "on the character's right side";
    EXPECT_LT(left_hi - left_lo, 6.f * kCmPerModelPixel) << "a flat blade, thin sideways";
}

TEST(ItemModelTest, TheSameItemInAnotherHostBasisIsTheSameShapeInThatHostsAxes) {
    Atlas a(16, 16);
    for (int i = 0; i < 10; ++i) a.set(2 + i, 12 - i);
    const RigMesh canonical = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Handheld, kCanonical);
    const RigMesh sekiro = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Handheld, kSekiro);
    ASSERT_EQ(canonical.vertices.size(), sekiro.vertices.size());
    for (size_t i = 0; i < canonical.vertices.size(); ++i) {
        const Vec3 c{canonical.vertices[i].x, canonical.vertices[i].y, canonical.vertices[i].z};
        const Vec3 expected = kSekiro.fromCanonical(c);
        EXPECT_NEAR(sekiro.vertices[i].x, expected.x, 1e-3f);
        EXPECT_NEAR(sekiro.vertices[i].y, expected.y, 1e-3f);
        EXPECT_NEAR(sekiro.vertices[i].z, expected.z, 1e-3f);
    }
}

TEST(ItemModelTest, TrianglesAreWoundOutwardFromTheirOwnQuadInAnyBasis) {
    Atlas a(16, 16);
    a.set(4, 4);
    a.set(5, 4);
    for (const HostBasis& basis : {kCanonical, kSekiro}) {
        const RigMesh m = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Generated, basis);
        Vec3 centre{};
        for (const auto& v : m.vertices) centre += Vec3{v.x, v.y, v.z};
        centre = centre * (1.f / static_cast<float>(m.vertices.size()));
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            const auto& p0 = m.vertices[m.indices[i]];
            const auto& p1 = m.vertices[m.indices[i + 1]];
            const auto& p2 = m.vertices[m.indices[i + 2]];
            const Vec3 e1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z}, e2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
            const Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            EXPECT_GT(n.x * (p0.x - centre.x) + n.y * (p0.y - centre.y) + n.z * (p0.z - centre.z), 0.f) << "triangle " << i / 3;
        }
    }
}

TEST(ItemModelTest, WhichItemsAreHeldAsFlatSpritesAndWithWhatStyle) {
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::DiamondSword));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::DiamondPickaxe));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::Bow));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::GoldenApple));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::TotemOfUndying));
    EXPECT_FALSE(isHeldAsFlatSprite(ItemId::None));
    EXPECT_FALSE(isHeldAsFlatSprite(ItemId::BlockDirt)) << "block icons in the atlas are isometric renders, not sprites";
    EXPECT_FALSE(isHeldAsFlatSprite(ItemId::BlockTnt));
    EXPECT_FALSE(isHeldAsFlatSprite(ItemId::Elytra)) << "worn, not held";
    EXPECT_EQ(heldItemStyle(ItemId::DiamondSword), HeldItemStyle::Handheld);
    EXPECT_EQ(heldItemStyle(ItemId::DiamondPickaxe), HeldItemStyle::Handheld);
    EXPECT_EQ(heldItemStyle(ItemId::Bow), HeldItemStyle::Bow);
    EXPECT_EQ(heldItemStyle(ItemId::GoldenApple), HeldItemStyle::Generated);
}

TEST(ItemModelTest, BadSpritesNeverCrash) {
    Atlas a(16, 16);
    a.set(1, 1);
    EXPECT_TRUE(buildHeldItemMesh({nullptr, 16, 16, 0, 0, 16, 16}, HeldItemStyle::Generated, kCanonical).vertices.empty());
    EXPECT_TRUE(buildHeldItemMesh(a.sprite(0, 0, 0, 16), HeldItemStyle::Generated, kCanonical).vertices.empty());
    EXPECT_TRUE(buildHeldItemMesh(a.sprite(10, 10, 16, 16), HeldItemStyle::Generated, kCanonical).vertices.empty()); // outside the atlas
    EXPECT_TRUE(buildHeldItemMesh(a.sprite(-1, 0, 8, 8), HeldItemStyle::Generated, kCanonical).vertices.empty());
}
