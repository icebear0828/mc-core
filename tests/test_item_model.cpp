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
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::WoodenPickaxe));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::StonePickaxe));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::IronPickaxe));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::GoldenPickaxe));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::NetheritePickaxe));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::Bow));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::GoldenApple));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::TotemOfUndying));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::Shield));
    EXPECT_FALSE(isHeldAsFlatSprite(ItemId::None));
    EXPECT_FALSE(isHeldAsFlatSprite(ItemId::BlockDirt)) << "block icons in the atlas are isometric renders, not sprites";
    EXPECT_FALSE(isHeldAsFlatSprite(ItemId::BlockTnt));
    EXPECT_FALSE(isHeldAsFlatSprite(ItemId::Elytra)) << "worn, not held";
    EXPECT_EQ(heldItemStyle(ItemId::DiamondSword), HeldItemStyle::Handheld);
    EXPECT_EQ(heldItemStyle(ItemId::DiamondPickaxe), HeldItemStyle::Handheld);
    EXPECT_EQ(heldItemStyle(ItemId::WoodenPickaxe), HeldItemStyle::Handheld);
    EXPECT_EQ(heldItemStyle(ItemId::StonePickaxe), HeldItemStyle::Handheld);
    EXPECT_EQ(heldItemStyle(ItemId::IronPickaxe), HeldItemStyle::Handheld);
    EXPECT_EQ(heldItemStyle(ItemId::GoldenPickaxe), HeldItemStyle::Handheld);
    EXPECT_EQ(heldItemStyle(ItemId::NetheritePickaxe), HeldItemStyle::Handheld);
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

TEST(ItemModel, FlatMeshIsCentredOneSixteenthThickAndAddressesTheSpritePixels) {
    // a 4x4 sprite cell at (2, 3) of an 8x8 atlas, with only the pixel (1, 2) of the cell opaque
    uint8_t atlas[8 * 8 * 4] = {};
    const int cell_x = 2, cell_y = 3, w = 4, h = 4;
    atlas[((cell_y + 2) * 8 + (cell_x + 1)) * 4 + 3] = 255;
    mc::rig::ItemSprite sprite{atlas, 8, 8, cell_x, cell_y, w, h};
    const mc::rig::RigMesh mesh = mc::rig::buildFlatItemMesh(sprite);
    ASSERT_FALSE(mesh.vertices.empty());
    EXPECT_EQ(mesh.vertices.size(), 6u * 4u); // front, back and four walls
    float min_z = 1e9f, max_z = -1e9f, min_x = 1e9f, max_x = -1e9f;
    for (const auto& v : mesh.vertices) {
        min_z = std::min(min_z, v.z);
        max_z = std::max(max_z, v.z);
        min_x = std::min(min_x, v.x);
        max_x = std::max(max_x, v.x);
        EXPECT_NEAR(v.u, (cell_x + 1 + 0.5f) / 8.f, 1e-6f); // the centre of that pixel
        EXPECT_NEAR(v.v, (cell_y + 2 + 0.5f) / 8.f, 1e-6f);
    }
    EXPECT_NEAR(max_z - min_z, 1.f / 16.f, 1e-6f);
    EXPECT_NEAR(max_x - min_x, 1.f / 4.f, 1e-6f); // one pixel of a 4 pixel wide sprite = a quarter block
    EXPECT_NEAR((min_x + max_x) * 0.5f, (1.5f - 2.f) * 0.25f, 1e-6f); // pixel column 1 of 4, relative to the centre
    mc::rig::ItemSprite empty{atlas, 8, 8, 0, 0, 2, 2};
    EXPECT_TRUE(mc::rig::buildFlatItemMesh(empty).vertices.empty());
    EXPECT_TRUE(mc::rig::buildFlatItemMesh({nullptr, 8, 8, 0, 0, 2, 2}).vertices.empty());
}

TEST(ItemModelTest, HeldBlockIsATexturedCubeWithTheFaceCellsOfTheBlock) {
    const UvRect top{0.10f, 0.10f, 0.20f, 0.20f}, side{0.30f, 0.30f, 0.40f, 0.40f}, bottom{0.50f, 0.50f, 0.60f, 0.60f};
    const RigMesh mesh = buildHeldBlockMesh(top, side, bottom, kSekiro);
    EXPECT_EQ(mesh.vertices.size(), 24u);
    EXPECT_EQ(mesh.indices.size(), 36u);
    auto in = [](const UvRect& r, const RigVertex& v) { return v.u >= r.u0 && v.u <= r.u1 && v.v >= r.v0 && v.v <= r.v1; };
    int n_top = 0, n_side = 0, n_bottom = 0;
    for (const RigVertex& v : mesh.vertices) {
        if (in(top, v)) ++n_top;
        else if (in(side, v)) ++n_side;
        else if (in(bottom, v)) ++n_bottom;
    }
    EXPECT_EQ(n_top, 4);
    EXPECT_EQ(n_side, 16);
    EXPECT_EQ(n_bottom, 4);
}

TEST(ItemModelTest, HeldBlockIsSmallAndNearTheRightHand) {
    const UvRect r{0.f, 0.f, 1.f, 1.f};
    const RigMesh mesh = buildHeldBlockMesh(r, r, r, kSekiro);
    ASSERT_FALSE(mesh.vertices.empty());
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (const RigVertex& v : mesh.vertices) {
        const float p[3] = {v.x, v.y, v.z};
        for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], p[i]);
            hi[i] = std::max(hi[i], p[i]);
        }
    }
    // host units are metres; the model block is 0.9 m, scaled by 0.375 and rotated: well under a metre along any axis, and not a point
    for (int i = 0; i < 3; ++i) EXPECT_LT(hi[i] - lo[i], 0.7f);
    EXPECT_GT(hi[0] - lo[0], 0.2f);
}

TEST(ItemModel, TheDrawnBowSpritesAreHeldAsFlatSpritesLikeTheBow) {
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::Bow));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::BowPulling0));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::BowPulling1));
    EXPECT_TRUE(isHeldAsFlatSprite(ItemId::BowPulling2));
}

TEST(ItemModelTest, ShieldItemStyleIsShield) {
    EXPECT_EQ(heldItemStyle(ItemId::Shield), HeldItemStyle::Shield);
}

TEST(ItemModelTest, BowPullingSpritesHaveBowStyle) {
    EXPECT_EQ(heldItemStyle(ItemId::BowPulling0), HeldItemStyle::Bow);
    EXPECT_EQ(heldItemStyle(ItemId::BowPulling1), HeldItemStyle::Bow);
    EXPECT_EQ(heldItemStyle(ItemId::BowPulling2), HeldItemStyle::Bow);
}

TEST(ItemModelTest, BowStyleHasDistinctOrientationFromGeneratedStyle) {
    // A diagonal bow arc: from (2, 2) to (14, 14)
    Atlas a(16, 16);
    for (int i = 0; i < 13; ++i) a.set(2 + i, 2 + i);
    const RigMesh bow_mesh = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Bow, kCanonical);
    const RigMesh gen_mesh = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Generated, kCanonical);
    ASSERT_FALSE(bow_mesh.vertices.empty());
    ASSERT_FALSE(gen_mesh.vertices.empty());

    // Compute bounding boxes in canonical rig space (X forward, Y left, Z up)
    auto bbox = [](const RigMesh& m) {
        float xmin = 1e9f, xmax = -1e9f, ymin = 1e9f, ymax = -1e9f, zmin = 1e9f, zmax = -1e9f;
        for (const auto& v : m.vertices) {
            xmin = std::min(xmin, v.x); xmax = std::max(xmax, v.x);
            ymin = std::min(ymin, v.y); ymax = std::max(ymax, v.y);
            zmin = std::min(zmin, v.z); zmax = std::max(zmax, v.z);
        }
        return std::array<float, 3>{xmax - xmin, ymax - ymin, zmax - zmin};
    };

    const auto bow_span = bbox(bow_mesh);
    const auto gen_span = bbox(gen_mesh);

    // In Generated style, the item is unrotated [0,0,0], so its span along X and Y is completely different
    // from Bow style [-80, 260, -40], which turns the bow forward and tilts it.
    EXPECT_GT(std::fabs(bow_span[0] - gen_span[0]), 4.0f * kCmPerModelPixel) << "Bow style must have different forward span than Generated";
    EXPECT_GT(std::fabs(bow_span[2] - gen_span[2]), 4.0f * kCmPerModelPixel) << "Bow style must have different vertical span than Generated";

    // All pulling stages produce meshes consistent with Bow style, not Generated
    for (ItemId pull_id : {ItemId::BowPulling0, ItemId::BowPulling1, ItemId::BowPulling2}) {
        const RigMesh pull_mesh = buildHeldItemMesh(a.sprite(0, 0, 16, 16), heldItemStyle(pull_id), kCanonical);
        const auto pull_span = bbox(pull_mesh);
        EXPECT_NEAR(pull_span[0], bow_span[0], 1e-4f);
        EXPECT_NEAR(pull_span[1], bow_span[1], 1e-4f);
        EXPECT_NEAR(pull_span[2], bow_span[2], 1e-4f);
    }
}



TEST(ItemModelTest, LeftHandItemsMirrorAcrossTheBody) {
    Atlas a(16, 16);
    for (int y = 2; y < 14; ++y) {
        for (int x = 2; x < 14; ++x) a.set(x, y);
    }
    const RigMesh right = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Generated, kCanonical, Hand::Right);
    const RigMesh left = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Generated, kCanonical, Hand::Left);
    ASSERT_FALSE(right.vertices.empty());
    ASSERT_FALSE(left.vertices.empty());
    ASSERT_EQ(right.vertices.size(), left.vertices.size());

    // In canonical frame (X forward, Y left, Z up):
    // Right arm is at negative Y (around -20 cm); Left arm is at positive Y (around +20 cm).
    float r_y_sum = 0.f, l_y_sum = 0.f;
    for (const auto& v : right.vertices) r_y_sum += v.y;
    for (const auto& v : left.vertices) l_y_sum += v.y;
    const float r_y_avg = r_y_sum / static_cast<float>(right.vertices.size());
    const float l_y_avg = l_y_sum / static_cast<float>(left.vertices.size());
    EXPECT_LT(r_y_avg, -10.f) << "Right hand item should be on character's right side (negative Y)";
    EXPECT_GT(l_y_avg, 10.f) << "Left hand item should be on character's left side (positive Y)";
    EXPECT_NEAR(r_y_avg, -l_y_avg, 1.0f) << "Left and right hands should be symmetrical across Y = 0";
}

TEST(ItemModelTest, LeftHandShieldFitsArmForearmWith3DExtrusion) {
    Atlas a(16, 16);
    // Draw solid shield sprite with rim
    for (int y = 2; y < 14; ++y) {
        for (int x = 3; x < 13; ++x) a.set(x, y);
    }
    const RigMesh shield = buildHeldItemMesh(a.sprite(0, 0, 16, 16), HeldItemStyle::Shield, kCanonical, Hand::Left);
    ASSERT_FALSE(shield.vertices.empty());
    EXPECT_GE(shield.indices.size(), 36u);

    // Verify 3D bounding box
    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;
    float min_z = 1e9f, max_z = -1e9f;
    for (const auto& v : shield.vertices) {
        min_x = std::min(min_x, v.x); max_x = std::max(max_x, v.x);
        min_y = std::min(min_y, v.y); max_y = std::max(max_y, v.y);
        min_z = std::min(min_z, v.z); max_z = std::max(max_z, v.z);
    }

    // Shield should have realistic 3D thickness (non-zero depth along normal)
    EXPECT_GT(max_x - min_x, 0.5f) << "Shield must have 3D extruded depth";
    // Should be on character's left side (+Y in canonical)
    EXPECT_GT(min_y, 10.f) << "Shield must sit on the left forearm";
    // Height coverage (Z) should cover forearm (~15-35 cm above ground in rest pose)
    EXPECT_GT(max_z - min_z, 20.f) << "Shield must be tall enough to cover the arm";
}

namespace {

float signedVolume(const RigMesh& m) {
    float vol = 0.f;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const RigVertex &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &c = m.vertices[m.indices[i + 2]];
        vol += (a.x * (b.y * c.z - b.z * c.y) - a.y * (b.x * c.z - b.z * c.x) + a.z * (b.x * c.y - b.y * c.x)) / 6.f;
    }
    return vol;
}

// Does some face of the mesh cover exactly this texel rectangle of the 64x64 sheet (which fills the whole atlas in these tests)?
bool hasFaceCovering(const RigMesh& m, float u0, float v0, float u1, float v1) {
    for (size_t q = 0; q + 3 < m.vertices.size(); q += 4) {
        float lo_u = 2.f, hi_u = -1.f, lo_v = 2.f, hi_v = -1.f;
        for (size_t k = 0; k < 4; ++k) {
            lo_u = std::min(lo_u, m.vertices[q + k].u); hi_u = std::max(hi_u, m.vertices[q + k].u);
            lo_v = std::min(lo_v, m.vertices[q + k].v); hi_v = std::max(hi_v, m.vertices[q + k].v);
        }
        if (std::abs(lo_u - u0 / 64.f) < 1e-5f && std::abs(hi_u - u1 / 64.f) < 1e-5f && std::abs(lo_v - v0 / 64.f) < 1e-5f && std::abs(hi_v - v1 / 64.f) < 1e-5f) return true;
    }
    return false;
}

const UvRect kWholeSheet{0.f, 0.f, 1.f, 1.f};

} // namespace

TEST(ShieldModel, IsAPlateAndAHandleMappedOntoTheEntityTextureLikeTheGame) {
    const RigMesh m = buildShieldMesh(kWholeSheet, kCanonical, Hand::Left);
    EXPECT_EQ(m.vertices.size(), 48u); // two boxes, six faces each
    EXPECT_EQ(m.indices.size(), 72u);
    // plate box texOffs (0, 0), 12x22x1: the front (rivets) at (1, 1), the back at (14, 1), the edge strips around them
    EXPECT_TRUE(hasFaceCovering(m, 1, 1, 13, 23));
    EXPECT_TRUE(hasFaceCovering(m, 14, 1, 26, 23));
    EXPECT_TRUE(hasFaceCovering(m, 1, 0, 13, 1));   // top strip
    EXPECT_TRUE(hasFaceCovering(m, 13, 0, 25, 1));  // bottom strip
    EXPECT_TRUE(hasFaceCovering(m, 0, 1, 1, 23));   // one side
    EXPECT_TRUE(hasFaceCovering(m, 13, 1, 14, 23)); // the other side
    // handle box texOffs (26, 0), 2x6x6: front/back 2x6 at (32, 6) and (40, 6), sides 6x6 at (26, 6) and (34, 6), top/bottom 2x6
    EXPECT_TRUE(hasFaceCovering(m, 32, 6, 34, 12));
    EXPECT_TRUE(hasFaceCovering(m, 40, 6, 42, 12));
    EXPECT_TRUE(hasFaceCovering(m, 26, 6, 32, 12));
    EXPECT_TRUE(hasFaceCovering(m, 34, 6, 40, 12));
    EXPECT_TRUE(hasFaceCovering(m, 32, 0, 34, 6));
    EXPECT_TRUE(hasFaceCovering(m, 34, 0, 36, 6));
}

TEST(ShieldModel, UvsAddressTheGivenCellOfTheAtlas) {
    const UvRect cell{0.25f, 0.5f, 0.375f, 0.625f};
    const RigMesh m = buildShieldMesh(cell, kCanonical, Hand::Right);
    ASSERT_FALSE(m.vertices.empty());
    for (const RigVertex& v : m.vertices) {
        EXPECT_GE(v.u, cell.u0 - 1e-6f); EXPECT_LE(v.u, cell.u1 + 1e-6f);
        EXPECT_GE(v.v, cell.v0 - 1e-6f); EXPECT_LE(v.v, cell.v1 + 1e-6f);
    }
}

TEST(ShieldModel, EveryFaceIsWoundOutwardForBothHandsBlockingOrNotAndBothHostHandednesses) {
    const float vol_px = 12.f * 22.f * 1.f + 2.f * 6.f * 6.f; // plate + handle, vanilla display scale 1
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        for (const bool blocking : {false, true}) {
            const RigMesh m = buildShieldMesh(kWholeSheet, kCanonical, hand, blocking);
            const float k = kCmPerModelPixel;
            EXPECT_NEAR(signedVolume(m), vol_px * k * k * k, 1e-3f * vol_px * k * k * k);
        }
    }
    // a reflecting host (Sekiro's): the rule is "the cross product of the host coordinates points outward", so the volume is positive there too
    const RigMesh s = buildShieldMesh(kWholeSheet, kSekiro, Hand::Left);
    const float expected = vol_px * std::pow(kCmPerModelPixel * 0.01f, 3.f);
    EXPECT_NEAR(signedVolume(s), expected, 0.01f * expected);
}

namespace {

// Minecraft model space (x towards the character's left, y down, z back; pixels) to the canonical host the tests use.
Vec3 canonicalOfModel(float x, float y, float z) { return {-z * kCmPerModelPixel, x * kCmPerModelPixel, (24.f - y) * kCmPerModelPixel}; }

struct FaceFrame {
    Vec3 front_centre{}, back_centre{};
    bool found{false};
};

// The centres of the plate's front (rivets) and back faces, found by the texels they show.
FaceFrame plateFaces(const RigMesh& m) {
    FaceFrame f;
    int found = 0;
    for (size_t q = 0; q + 3 < m.vertices.size(); q += 4) {
        const RigMesh one{{m.vertices[q], m.vertices[q + 1], m.vertices[q + 2], m.vertices[q + 3]}, {}};
        const bool front = hasFaceCovering(one, 1, 1, 13, 23), back = hasFaceCovering(one, 14, 1, 26, 23);
        if (!front && !back) continue;
        Vec3 c{0, 0, 0};
        for (const RigVertex& v : one.vertices) { c.x += v.x / 4.f; c.y += v.y / 4.f; c.z += v.z / 4.f; }
        (front ? f.front_centre : f.back_centre) = c;
        ++found;
    }
    f.found = found == 2;
    return f;
}

} // namespace

TEST(ShieldModel, ThirdPersonPlacementIsMinecraftsItemInHandChainWithTheJarsDisplayTransforms) {
    // models/item/shield.json and shield_blocking.json (1.21.8) through ItemInHandLayer on the rest pose of the arm; the numbers were computed
    // independently (centre of the plate in model pixels, outward normal of the front face).
    struct Case { Hand hand; bool blocking; float cx, cy, cz, nx, ny, nz; };
    const Case cases[] = {
        {Hand::Right, false, -9.500f, 8.000f, 0.000f, -1.000f, 0.000f, 0.000f},
        {Hand::Right, true, -7.013f, 13.101f, -0.787f, -0.423f, 0.641f, -0.641f},
        {Hand::Left, false, 9.500f, 8.000f, 0.000f, 1.000f, 0.000f, 0.000f},
        {Hand::Left, true, 7.513f, 13.382f, -1.568f, 0.423f, 0.641f, -0.641f},
    };
    for (const Case& c : cases) {
        const FaceFrame f = plateFaces(buildShieldMesh(kWholeSheet, kCanonical, c.hand, c.blocking));
        ASSERT_TRUE(f.found);
        const Vec3 centre{(f.front_centre.x + f.back_centre.x) / 2.f, (f.front_centre.y + f.back_centre.y) / 2.f, (f.front_centre.z + f.back_centre.z) / 2.f};
        const Vec3 want = canonicalOfModel(c.cx, c.cy, c.cz);
        EXPECT_NEAR(centre.x, want.x, 0.01f * kCmPerModelPixel);
        EXPECT_NEAR(centre.y, want.y, 0.01f * kCmPerModelPixel);
        EXPECT_NEAR(centre.z, want.z, 0.01f * kCmPerModelPixel);
        // the front face looks along the outward normal: front centre minus back centre
        Vec3 n{f.front_centre.x - f.back_centre.x, f.front_centre.y - f.back_centre.y, f.front_centre.z - f.back_centre.z};
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        const Vec3 wn{-c.nz, c.nx, -c.ny}; // the linear part of canonicalOfModel
        EXPECT_NEAR(n.x / len, wn.x, 0.01f);
        EXPECT_NEAR(n.y / len, wn.y, 0.01f);
        EXPECT_NEAR(n.z / len, wn.z, 0.01f);
    }
}

TEST(ShieldModel, TheLeftAndRightShieldsSitOnOppositeSidesOfTheBody) {
    auto centreY = [](const RigMesh& m) { float y = 0.f; for (const RigVertex& v : m.vertices) y += v.y; return y / static_cast<float>(m.vertices.size()); };
    // canonical host: +Y is the character's left
    for (const bool blocking : {false, true}) {
        EXPECT_GT(centreY(buildShieldMesh(kWholeSheet, kCanonical, Hand::Left, blocking)), 0.f);
        EXPECT_LT(centreY(buildShieldMesh(kWholeSheet, kCanonical, Hand::Right, blocking)), 0.f);
    }
}

TEST(ShieldModel, TheHandleReachesBackFromThePlateTowardsTheHand) {
    // the handle is behind the plate (further from the front face along the normal) and ends beside the forearm, not in front of the plate
    const RigMesh m = buildShieldMesh(kWholeSheet, kCanonical, Hand::Right, false);
    const FaceFrame f = plateFaces(m);
    ASSERT_TRUE(f.found);
    Vec3 n{f.front_centre.x - f.back_centre.x, f.front_centre.y - f.back_centre.y, f.front_centre.z - f.back_centre.z};
    const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    n = {n.x / len, n.y / len, n.z / len};
    float max_along = -1e9f, min_along = 1e9f;
    for (const RigVertex& v : m.vertices) {
        const float along = (v.x - f.front_centre.x) * n.x + (v.y - f.front_centre.y) * n.y + (v.z - f.front_centre.z) * n.z;
        max_along = std::max(max_along, along);
        min_along = std::min(min_along, along);
    }
    EXPECT_NEAR(max_along, 0.f, 1e-3f);                      // nothing sticks out in front of the rivets
    EXPECT_NEAR(min_along, -6.f * kCmPerModelPixel - 1.f * kCmPerModelPixel, 0.01f * kCmPerModelPixel); // plate thickness 1 + handle depth 6
}

TEST(ShieldModel, FirstPersonMeshIsTheModelInMinecraftsItemSpaceWithTheFrontTowardsTheViewer) {
    // item space after the shield renderer's scale(1, -1, -1) and the translate(-0.5, -0.5, -0.5) that precedes it: x right, y up, z towards the viewer,
    // 1 unit = 1 block = 16 model pixels; the display transform (eldenring_fp.hpp) is applied on top.
    const RigMesh m = buildShieldFirstPersonMesh(kWholeSheet);
    EXPECT_EQ(m.vertices.size(), 48u);
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f, min_z = 1e9f, max_z = -1e9f;
    for (const RigVertex& v : m.vertices) {
        min_x = std::min(min_x, v.x); max_x = std::max(max_x, v.x);
        min_y = std::min(min_y, v.y); max_y = std::max(max_y, v.y);
        min_z = std::min(min_z, v.z); max_z = std::max(max_z, v.z);
    }
    EXPECT_NEAR(min_x, -0.5f - 6.f / 16.f, 1e-5f);  EXPECT_NEAR(max_x, -0.5f + 6.f / 16.f, 1e-5f);  // 12 px wide
    EXPECT_NEAR(min_y, -0.5f - 11.f / 16.f, 1e-5f); EXPECT_NEAR(max_y, -0.5f + 11.f / 16.f, 1e-5f); // 22 px tall
    EXPECT_NEAR(max_z, -0.5f + 2.f / 16.f, 1e-5f);                                                // the front face (model z = -2)
    EXPECT_NEAR(min_z, -0.5f - 5.f / 16.f, 1e-5f);                                                // the handle's end (model z = 5)
    EXPECT_TRUE(hasFaceCovering(m, 1, 1, 13, 23));
    EXPECT_GT(signedVolume(m), 0.f);
    for (size_t q = 0; q + 3 < m.vertices.size(); q += 4) {
        const RigMesh face{{m.vertices[q], m.vertices[q + 1], m.vertices[q + 2], m.vertices[q + 3]}, {}};
        if (!hasFaceCovering(face, 1, 1, 13, 23)) continue;
        for (const RigVertex& v : face.vertices) {
            EXPECT_NEAR(v.z, -0.5f + 2.f / 16.f, 1e-5f);
            if (std::abs(v.u - 13.f / 64.f) < 1e-5f) EXPECT_GT(v.x, -0.5f); // larger u = to the right
            if (std::abs(v.v - 1.f / 64.f) < 1e-5f) EXPECT_GT(v.y, -0.5f);  // smaller v = up
        }
    }
}
