#pragma once

#include "mc/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mc {

// An axis-aligned collision box, in blocks.
struct Aabb {
    Vec3 min{};
    Vec3 max{};
};

// Where a ray entered the nearest box, plus the face normal it crossed.
struct CollisionRayHit {
    Vec3 point{};
    Vec3 normal{};
    float distance{0.f};
};

// Identity of one 16x16x16-block section.
struct SectionKey {
    int32_t x{0};
    int32_t y{0};
    int32_t z{0};

    constexpr bool operator==(const SectionKey&) const = default;
};

struct SectionKeyHash {
    size_t operator()(const SectionKey& key) const noexcept {
        // FNV-1a over the three coordinates.
        uint32_t h = 2166136261u;
        h = (h ^ static_cast<uint32_t>(key.x)) * 16777619u;
        h = (h ^ static_cast<uint32_t>(key.y)) * 16777619u;
        h = (h ^ static_cast<uint32_t>(key.z)) * 16777619u;
        return static_cast<size_t>(h);
    }
};

// Host geometry as Minecraft sees it: a sparse store of axis-aligned boxes that an adapter feeds
// from the host game's world, so host terrain can be collided against without occupying the block
// grid. Host geometry and placed blocks therefore coexist, and Minecraft's own movement code
// (collision resolution, step-up, gravity, sneaking at edges) runs unchanged against these boxes.
//
// Resolution: every box is snapped outward to 1/8 block. That is the trade-off SkyCraft makes:
// 1/8 is fine enough that a slope becomes a staircase of micro-steps below Minecraft's 0.6-block
// step height, so walking uphill is effectively smooth, while keeping the data small enough to
// stream per section. Snapping finer would multiply the box count without changing what the
// movement code can do with it; snapping coarser would produce steps the player cannot climb.
// Snapping is outward (floor the minimum, ceil the maximum), so a box never shrinks below the
// geometry it came from.
//
// Slopes: addTriangle classifies a surface by its up-facing normal. Walkable surfaces (normal
// within 50 degrees of straight up, matching Skyrim's walkable slope) contribute their quantized
// bounding box, i.e. micro-steps. Steeper surfaces are extruded into a full-height wall column
// inside every section they touch, so Minecraft's step-up rule refuses them and cliffs stay
// unclimbable. That is a data decision, not a physics change.
//
// Storage: boxes are bucketed per 16x16x16-block section. A box that straddles a section
// boundary is clipped and stored once per section it overlaps, so a query in either section finds
// it. Query results are deterministic: sections in ascending (x, y, z) key order, boxes within a
// section in insertion order.
//
// Coordinates are in blocks (1 block = 100 cm, matching VoxelWorld::kBlockSizeCm); an adapter
// converts host-space centimetres by dividing by kBlockSizeCm before calling in.
class CollisionField {
public:
    static constexpr int kSubBlocksPerBlock = 8;
    static constexpr int kSectionBlocks = 16;

    // One sub-block in blocks (0.125).
    static constexpr float kSubBlockSize = 1.0f / static_cast<float>(kSubBlocksPerBlock);
    // Minecraft's step-up limit, in blocks. Micro-steps must stay below it.
    static constexpr float kStepHeightBlocks = 0.6f;
    // cos(50 degrees): surfaces at least this vertical are walkable, steeper ones become walls.
    static constexpr float kWalkableNormalY = 0.64278761f;

    // Section that contains a point.
    [[nodiscard]] static SectionKey sectionKeyFor(const Vec3& point);

    // Adds a box, snapped outward to the 1/8-block grid. Boxes without volume are ignored.
    void addAabb(const Aabb& box);

    // Adds a triangle: a walkable one as its quantized bounding box (micro-steps), a steeper one
    // as a full-height wall column per section. Degenerate triangles are ignored.
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c);

    // Drops a section's boxes but keeps its version, so delta consumers see the clear.
    void clearSection(const SectionKey& key);

    // Drops every section farther than radius blocks from center, including its version.
    void evictOutside(const Vec3& center, float radius);

    // Bumped on every mutation of that section; 0 for a section that was never touched.
    [[nodiscard]] uint32_t sectionVersion(const SectionKey& key) const;

    // Every box overlapping the query, in deterministic order.
    [[nodiscard]] std::vector<Aabb> collect(const Aabb& query) const;

    // True when any stored box overlaps the query.
    [[nodiscard]] bool overlaps(const Aabb& query) const;

    // Nearest box hit along the ray. distance is in blocks, and direction need not be normalized.
    [[nodiscard]] std::optional<CollisionRayHit> raycast(const Vec3& origin, const Vec3& direction,
                                                         float max_distance) const;

    // Sections that currently hold at least one box.
    [[nodiscard]] size_t sectionCount() const;
    // Stored boxes; a box spanning section boundaries counts once per section.
    [[nodiscard]] size_t boxCount() const;

private:
    struct Section {
        std::vector<Aabb> boxes;
        uint32_t version{0};
    };

    void insertSnapped(const Aabb& box);
    void addWallColumns(const Aabb& bounds);

    std::unordered_map<SectionKey, Section, SectionKeyHash> sections_;
};

} // namespace mc
