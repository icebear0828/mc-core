#include <gtest/gtest.h>
#include "mc/collision_field.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

mc::Aabb makeBox(float x0, float y0, float z0, float x1, float y1, float z1) {
    return mc::Aabb{{x0, y0, z0}, {x1, y1, z1}};
}

// Section key containing a point, computed here so the test does not lean on the module.
mc::SectionKey keyAt(float x, float y, float z) {
    return mc::SectionKey{
        static_cast<int32_t>(std::floor(x / 16.f)),
        static_cast<int32_t>(std::floor(y / 16.f)),
        static_cast<int32_t>(std::floor(z / 16.f))};
}

bool sameBox(const mc::Aabb& a, const mc::Aabb& b, float tolerance) {
    return std::abs(a.min.x - b.min.x) <= tolerance && std::abs(a.min.y - b.min.y) <= tolerance &&
           std::abs(a.min.z - b.min.z) <= tolerance && std::abs(a.max.x - b.max.x) <= tolerance &&
           std::abs(a.max.y - b.max.y) <= tolerance && std::abs(a.max.z - b.max.z) <= tolerance;
}

} // namespace

// A slope exported as 1/8-block micro-steps must rise by less than Minecraft's 0.6 step height
// between neighbours, otherwise the player cannot walk up it.
TEST(CollisionFieldTest, RampMicroStepsStayUnderStepHeight) {
    mc::CollisionField field;

    constexpr int kSteps = 32;
    constexpr float kStepWidth = 0.125f; // one sub-block per step
    for (int i = 0; i < kSteps; ++i) {
        const float x0 = static_cast<float>(i) * kStepWidth;
        const float top = static_cast<float>(i + 1) * kStepWidth * 0.5f; // ~27 degree slope
        field.addAabb(makeBox(x0, 0.f, 0.f, x0 + kStepWidth, top, 1.f));
    }

    auto steps = field.collect(makeBox(0.f, 0.f, 0.f, 4.f, 4.f, 1.f));
    ASSERT_EQ(steps.size(), static_cast<size_t>(kSteps));

    std::sort(steps.begin(), steps.end(), [](const mc::Aabb& a, const mc::Aabb& b) {
        return a.min.x < b.min.x;
    });
    for (size_t i = 1; i < steps.size(); ++i) {
        const float rise = steps[i].max.y - steps[i - 1].max.y;
        EXPECT_GE(rise, 0.f);
        EXPECT_LE(rise, mc::CollisionField::kStepHeightBlocks);
    }

    // The ramp as a whole still climbs: it is not a flat floor.
    EXPECT_GT(steps.back().max.y, steps.front().max.y + 1.f);
}

// A near-vertical surface is extruded into a wall column that fills its section's height, so
// Minecraft's step-up rule refuses it. It must therefore be solid at body height.
TEST(CollisionFieldTest, SteepTriangleBecomesWallColumn) {
    mc::CollisionField field;

    // Normal is +X: 90 degrees from vertical, far steeper than the 50 degree threshold.
    field.addTriangle({2.f, 0.f, 0.f}, {2.f, 1.f, 0.f}, {2.f, 0.f, 1.f});

    EXPECT_TRUE(field.overlaps(makeBox(2.f, 4.f, 0.f, 3.f, 5.8f, 1.f)));     // body height
    EXPECT_TRUE(field.overlaps(makeBox(2.f, 0.2f, 0.2f, 2.5f, 1.8f, 0.8f))); // feet height
    EXPECT_FALSE(field.overlaps(makeBox(6.f, 4.f, 6.f, 7.f, 5.8f, 7.f)));    // far away

    // The column is a wall, not a thin sliver: it is still solid near the top of the section.
    EXPECT_TRUE(field.overlaps(makeBox(2.f, 15.f, 0.f, 3.f, 16.f, 1.f)));
}

// A walkable surface contributes its bounding box only; it must not become a wall.
TEST(CollisionFieldTest, WalkableTriangleDoesNotBecomeWall) {
    mc::CollisionField field;

    // Normal is +Y after orientation: a flat floor at y = 1.
    field.addTriangle({0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}, {0.f, 1.f, 1.f});

    EXPECT_TRUE(field.overlaps(makeBox(0.25f, 0.9f, 0.25f, 0.75f, 1.05f, 0.75f)));
    EXPECT_FALSE(field.overlaps(makeBox(0.f, 2.f, 0.f, 1.f, 3.f, 1.f)));
    EXPECT_FALSE(field.overlaps(makeBox(0.f, 4.f, 0.f, 1.f, 5.8f, 1.f)));
}

// A surface just inside the 50 degree limit is walkable; just outside it becomes a wall.
// The ramp plane rises along +x and extends along +z, so its normal's Y is cos(slope angle).
TEST(CollisionFieldTest, SteepThresholdIsFiftyDegrees) {
    constexpr float kRun = 10.f;
    constexpr float kWidth = 4.f;
    const float walkable_rise = kRun * 0.8391f; // tan(40 degrees)
    const float steep_rise = kRun * 1.2799f;    // tan(52 degrees)

    mc::CollisionField walkable;
    walkable.addTriangle({0.f, 0.f, 0.f}, {kRun, walkable_rise, 0.f}, {0.f, 0.f, kWidth});
    EXPECT_FALSE(walkable.overlaps(makeBox(1.f, 12.f, 1.f, 2.f, 13.8f, 2.f)));

    mc::CollisionField steep;
    steep.addTriangle({0.f, 0.f, 0.f}, {kRun, steep_rise, 0.f}, {0.f, 0.f, kWidth});
    EXPECT_TRUE(steep.overlaps(makeBox(1.f, 12.f, 1.f, 2.f, 13.8f, 2.f)));
}

// Boxes are snapped to 1/8 block and never shrink, so collision is not lost.
TEST(CollisionFieldTest, QuantizesToEighthBlock) {
    mc::CollisionField field;
    field.addAabb(makeBox(0.1f, 0.1f, 0.1f, 1.1f, 1.1f, 1.1f));

    auto boxes = field.collect(makeBox(-1.f, -1.f, -1.f, 3.f, 3.f, 3.f));
    ASSERT_EQ(boxes.size(), 1u);
    const mc::Aabb& box = boxes.front();
    EXPECT_NEAR(box.min.x, 0.1f, 0.125f);
    EXPECT_NEAR(box.min.y, 0.1f, 0.125f);
    EXPECT_NEAR(box.min.z, 0.1f, 0.125f);
    EXPECT_NEAR(box.max.x, 1.1f, 0.125f);
    EXPECT_NEAR(box.max.y, 1.1f, 0.125f);
    EXPECT_NEAR(box.max.z, 1.1f, 0.125f);

    for (float v : {box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z}) {
        EXPECT_FLOAT_EQ(std::round(v * 8.f) / 8.f, v);
    }
    // Snapping expands outward, so the original box is fully covered.
    EXPECT_LE(box.min.x, 0.1f);
    EXPECT_GE(box.max.x, 1.1f);
}

// A box already on the 1/8 grid round-trips unchanged.
TEST(CollisionFieldTest, EighthGridBoxRoundTrips) {
    mc::CollisionField field;
    const mc::Aabb input = makeBox(1.f, 2.f, 3.f, 1.125f, 2.25f, 3.375f);
    field.addAabb(input);

    auto boxes = field.collect(makeBox(0.f, 0.f, 0.f, 4.f, 4.f, 4.f));
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_TRUE(sameBox(boxes.front(), input, 1e-5f));
}

// Degenerate input carries no volume and is dropped.
TEST(CollisionFieldTest, DegenerateBoxesAreIgnored) {
    mc::CollisionField field;
    field.addAabb(makeBox(1.f, 1.f, 1.f, 1.f, 2.f, 2.f));
    field.addAabb(makeBox(1.f, 1.f, 1.f, 2.f, 1.f, 2.f));
    field.addAabb(makeBox(2.f, 2.f, 2.f, 1.f, 1.f, 1.f));
    EXPECT_EQ(field.boxCount(), 0u);
    EXPECT_EQ(field.sectionCount(), 0u);
}

// A box spanning a section boundary is stored in both sections, so a query near either end
// still finds it.
TEST(CollisionFieldTest, BoxStraddlingSectionBoundaryIsFoundFromBothSides) {
    mc::CollisionField field;
    field.addAabb(makeBox(15.5f, 0.5f, 0.5f, 16.5f, 1.f, 1.f));
    EXPECT_EQ(field.sectionCount(), 2u);

    EXPECT_TRUE(field.overlaps(makeBox(15.25f, 0.6f, 0.6f, 15.75f, 0.9f, 0.9f)));
    EXPECT_TRUE(field.overlaps(makeBox(16.25f, 0.6f, 0.6f, 16.75f, 0.9f, 0.9f)));
    EXPECT_FALSE(field.overlaps(makeBox(20.f, 0.6f, 0.6f, 21.f, 0.9f, 0.9f)));
}

TEST(CollisionFieldTest, RaycastHitsNearestBox) {
    mc::CollisionField field;
    field.addAabb(makeBox(5.f, 0.f, 0.f, 6.f, 1.f, 1.f)); // added first, but farther
    field.addAabb(makeBox(1.f, 0.f, 0.f, 2.f, 1.f, 1.f));

    const auto hit = field.raycast({0.f, 0.5f, 0.5f}, {1.f, 0.f, 0.f}, 10.f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_FLOAT_EQ(hit->distance, 1.f);
    EXPECT_FLOAT_EQ(hit->point.x, 1.f);
    EXPECT_FLOAT_EQ(hit->point.y, 0.5f);
    EXPECT_FLOAT_EQ(hit->point.z, 0.5f);
    EXPECT_FLOAT_EQ(hit->normal.x, -1.f);
    EXPECT_FLOAT_EQ(hit->normal.y, 0.f);
    EXPECT_FLOAT_EQ(hit->normal.z, 0.f);
}

TEST(CollisionFieldTest, RaycastMissesBeyondMaxDistance) {
    mc::CollisionField field;
    field.addAabb(makeBox(5.f, 0.f, 0.f, 6.f, 1.f, 1.f));

    EXPECT_FALSE(field.raycast({0.f, 0.5f, 0.5f}, {1.f, 0.f, 0.f}, 4.5f).has_value());
    EXPECT_TRUE(field.raycast({0.f, 0.5f, 0.5f}, {1.f, 0.f, 0.f}, 5.5f).has_value());
    // A direction pointing away from the box never hits.
    EXPECT_FALSE(field.raycast({0.f, 0.5f, 0.5f}, {-1.f, 0.f, 0.f}, 10.f).has_value());
}

TEST(CollisionFieldTest, RaycastReturnsSurfaceNormal) {
    mc::CollisionField field;
    field.addAabb(makeBox(1.f, 1.f, 1.f, 2.f, 2.f, 2.f));

    const auto top = field.raycast({1.5f, 5.f, 1.5f}, {0.f, -1.f, 0.f}, 10.f);
    ASSERT_TRUE(top.has_value());
    EXPECT_FLOAT_EQ(top->distance, 3.f);
    EXPECT_FLOAT_EQ(top->point.y, 2.f);
    EXPECT_FLOAT_EQ(top->normal.y, 1.f);

    const auto side = field.raycast({5.f, 1.5f, 1.5f}, {-1.f, 0.f, 0.f}, 10.f);
    ASSERT_TRUE(side.has_value());
    EXPECT_FLOAT_EQ(side->distance, 3.f);
    EXPECT_FLOAT_EQ(side->point.x, 2.f);
    EXPECT_FLOAT_EQ(side->normal.x, 1.f);

    // An unnormalized direction still reports a distance in blocks.
    const auto scaled = field.raycast({1.5f, 5.f, 1.5f}, {0.f, -4.f, 0.f}, 10.f);
    ASSERT_TRUE(scaled.has_value());
    EXPECT_FLOAT_EQ(scaled->distance, 3.f);
}

TEST(CollisionFieldTest, EvictionRemovesOnlySectionsOutsideRadius) {
    mc::CollisionField field;
    field.addAabb(makeBox(0.5f, 0.5f, 0.5f, 1.f, 1.f, 1.f));     // section (0, 0, 0)
    field.addAabb(makeBox(80.5f, 0.5f, 0.5f, 81.f, 1.f, 1.f));   // section (5, 0, 0)
    field.addAabb(makeBox(0.5f, -40.5f, 0.5f, 1.f, -40.f, 1.f)); // section (0, -3, 0)
    ASSERT_EQ(field.sectionCount(), 3u);

    field.evictOutside({0.5f, 0.5f, 0.5f}, 20.f);

    EXPECT_EQ(field.sectionCount(), 1u);
    EXPECT_EQ(field.sectionVersion(keyAt(0.f, 0.f, 0.f)), 1u);
    EXPECT_EQ(field.sectionVersion(keyAt(80.5f, 0.5f, 0.5f)), 0u);
    EXPECT_EQ(field.sectionVersion(keyAt(0.5f, -40.5f, 0.5f)), 0u);
    EXPECT_TRUE(field.overlaps(makeBox(0.6f, 0.6f, 0.6f, 0.9f, 0.9f, 0.9f)));
    EXPECT_FALSE(field.overlaps(makeBox(80.6f, 0.6f, 0.6f, 80.9f, 0.9f, 0.9f)));
}

TEST(CollisionFieldTest, SectionVersionBumpsOnMutation) {
    mc::CollisionField field;
    const mc::SectionKey a = keyAt(0.5f, 0.5f, 0.5f);
    const mc::SectionKey b = keyAt(16.5f, 0.5f, 0.5f);

    EXPECT_EQ(field.sectionVersion(a), 0u);

    field.addAabb(makeBox(0.5f, 0.5f, 0.5f, 1.f, 1.f, 1.f));
    EXPECT_EQ(field.sectionVersion(a), 1u);
    EXPECT_EQ(field.sectionVersion(b), 0u); // unrelated section untouched

    field.addAabb(makeBox(0.5f, 2.5f, 0.5f, 1.f, 3.f, 1.f));
    EXPECT_EQ(field.sectionVersion(a), 2u);

    field.addAabb(makeBox(16.5f, 0.5f, 0.5f, 17.f, 1.f, 1.f));
    EXPECT_EQ(field.sectionVersion(b), 1u);
    EXPECT_EQ(field.sectionVersion(a), 2u);

    field.clearSection(a);
    EXPECT_EQ(field.sectionVersion(a), 3u);
    EXPECT_EQ(field.sectionVersion(b), 1u);
    EXPECT_EQ(field.boxCount(), 1u);
    EXPECT_FALSE(field.overlaps(makeBox(0.5f, 0.5f, 0.5f, 1.f, 1.f, 1.f)));

    // Clearing an already empty section is not a mutation.
    field.clearSection(a);
    EXPECT_EQ(field.sectionVersion(a), 3u);
}

TEST(CollisionFieldTest, CollectOrderIsDeterministic) {
    mc::CollisionField field;
    field.addAabb(makeBox(40.5f, 0.5f, 0.5f, 41.f, 1.f, 1.f)); // section (2, 0, 0)
    field.addAabb(makeBox(0.5f, 0.5f, 0.5f, 1.f, 1.f, 1.f));   // section (0, 0, 0)
    field.addAabb(makeBox(0.5f, 20.5f, 0.5f, 1.f, 21.f, 1.f)); // section (0, 1, 0)
    field.addAabb(makeBox(0.5f, 0.5f, 40.5f, 1.f, 1.f, 41.f)); // section (0, 0, 2)

    const mc::Aabb query = makeBox(-5.f, -5.f, -5.f, 60.f, 60.f, 60.f);
    const auto first = field.collect(query);
    const auto second = field.collect(query);

    ASSERT_EQ(first.size(), 4u);
    ASSERT_EQ(second.size(), first.size());
    for (size_t i = 0; i < first.size(); ++i) {
        EXPECT_TRUE(sameBox(first[i], second[i], 0.f));
    }

    // Section key order is (x, then y, then z), so (0,0,0), (0,0,2), (0,1,0), (2,0,0).
    EXPECT_FLOAT_EQ(first[0].min.x, 0.5f);
    EXPECT_FLOAT_EQ(first[0].min.y, 0.5f);
    EXPECT_FLOAT_EQ(first[0].min.z, 0.5f);
    EXPECT_FLOAT_EQ(first[1].min.z, 40.5f);
    EXPECT_FLOAT_EQ(first[2].min.y, 20.5f);
    EXPECT_FLOAT_EQ(first[3].min.x, 40.5f);
}

TEST(CollisionFieldTest, SectionCountAndBoxCount) {
    mc::CollisionField field;
    EXPECT_EQ(field.sectionCount(), 0u);
    EXPECT_EQ(field.boxCount(), 0u);

    field.addAabb(makeBox(0.5f, 0.5f, 0.5f, 1.f, 1.f, 1.f));
    field.addAabb(makeBox(0.5f, 2.5f, 0.5f, 1.f, 3.f, 1.f));
    field.addAabb(makeBox(20.5f, 0.5f, 0.5f, 21.f, 1.f, 1.f));
    EXPECT_EQ(field.sectionCount(), 2u);
    EXPECT_EQ(field.boxCount(), 3u);
}
