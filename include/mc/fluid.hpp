#pragma once

// Vanilla-consistent water and lava simulation, plus SkyCraft-style host water ingestion.
//
// Flow follows Minecraft Java Edition's FlowingFluid: a source is level 0, each block the fluid
// runs sideways raises the level by one, and it stops where the level would pass its reach (water
// 7, lava 3 in the overworld and 7 in the nether). A fluid with a non-solid cell below falls
// instead of spreading, and the falling column carries full strength, so it re-levels and runs its
// full reach again where it lands. Fluids never enter a solid cell from the host grid.
//
// Water and lava meeting produce the vanilla blocks: obsidian from a lava source, cobblestone from
// flowing lava, stone where a lava column lands on water. The simulation reports those as
// FluidInteraction values; the caller applies them to its own block grid.
//
// The simulation owns no blocks. It reads the host grid through a callback and keeps its own fluid
// cells, so an adapter can run it next to any voxel store. All ordering is by cell coordinate, so
// the same inputs always produce the same result.

#include "mc/blocks.hpp"
#include "mc/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace mc {

enum class FluidType : uint8_t { None = 0, Water, Lava };

// Vanilla levels are 0 (a source) through 7 (the last flowing block of water).
inline constexpr uint8_t kMaxFluidLevel = 7;

// SkyCraft's WaterGrid (protocol/skycraft_protocol.h): 16x16 columns and the "no water" sentinel.
inline constexpr int kHostWaterGridSize = 16;
inline constexpr float kNoWater = -1.0e30f;

// Total order on cells. Every container in this module uses it, so iteration is reproducible.
struct CellOrder {
    bool operator()(const Cell& a, const Cell& b) const noexcept {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    }
};

struct FluidCell {
    FluidType type{FluidType::None};
    uint8_t level{0};      // 0 = source, 1..7 = flowing; a falling column is level 0
    bool falling{false};   // true for the cells of a downward column

    [[nodiscard]] bool isSource() const noexcept {
        return type != FluidType::None && level == 0 && !falling;
    }
};

struct FluidRules {
    int water_spread{7};           // horizontal reach in blocks, one level per block
    int lava_spread_overworld{3};
    int lava_spread_nether{7};
    bool in_nether{false};
    uint32_t water_tick_interval{5}; // game ticks between water passes
    uint32_t lava_tick_interval{30}; // game ticks between lava passes
    int max_fall_depth{256};         // a single falling column is cut off below this depth
    int max_cells{4096};             // hard cap on the fluid field, so a host without a floor is safe

    [[nodiscard]] int maxLevel(FluidType type) const noexcept;
    [[nodiscard]] uint32_t tickInterval(FluidType type) const noexcept;
};

// Minecraft's water movement constants, per 20 Hz tick. Vanilla LivingEntity.travel multiplies the
// velocity by the drags each tick while submerged, applies gravity at a fraction of the normal
// 0.08 blocks/tick^2, and adds the buoyancy impulse while the swim-up input is held.
struct SwimConstants {
    float horizontal_drag{0.8f}; // velocity.x/z *= this
    float vertical_drag{0.9f};   // velocity.y *= this
    float gravity_scale{0.25f};  // submerged gravity is kEntityGravity * this (0.02 blocks/tick^2)
    float buoyancy{0.04f};       // upward impulse per tick while swimming up
};

inline constexpr float kEntityGravity = 0.08f;

// One tick of water drag; the adapter applies gravity and buoyancy itself.
inline Vec3 applyWaterDrag(const Vec3& velocity, const SwimConstants& constants = {}) noexcept {
    return {velocity.x * constants.horizontal_drag,
            velocity.y * constants.vertical_drag,
            velocity.z * constants.horizontal_drag};
}

// One cell that changed this pass. FluidType::None means the cell drained.
struct FluidUpdate {
    Cell cell{};
    FluidCell fluid{};
};

// A block the caller should place in its own grid.
struct FluidInteraction {
    Cell cell{};
    BlockId result{BlockId::Air};
};

struct FluidTickResult {
    std::vector<FluidUpdate> updates;
    std::vector<FluidInteraction> interactions;

    [[nodiscard]] bool empty() const noexcept { return updates.empty() && interactions.empty(); }
};

// A host game's lakes, rivers and sea as Minecraft water (SkyCraft's WaterGrid). The host sends the
// water surface over the block columns around the player; where a column has no water it sends
// kNoWater. Nothing here creates blocks: it answers the queries an adapter needs to make an entity
// swim, float and drown in the host's water.
struct HostWaterGrid {
    static constexpr int kSize = kHostWaterGridSize;

    uint32_t world_id{0};
    int32_t origin_x{0}; // world column of surface[0]
    int32_t origin_z{0};
    std::array<float, static_cast<std::size_t>(kSize) * static_cast<std::size_t>(kSize)> surface{};

    HostWaterGrid() { surface.fill(kNoWater); }

    [[nodiscard]] static bool inRange(int x, int z) noexcept { return x >= 0 && z >= 0 && x < kSize && z < kSize; }

    // Grid-local column, 0..15 on each axis.
    void set(int x, int z, float surface_y) noexcept;
    // World column coordinates; out-of-range writes are ignored.
    void setWorld(int x, int z, float surface_y) noexcept;

    // World column coordinates. Returns NaN where the column is outside the grid or has no water.
    [[nodiscard]] float surfaceAt(int x, int z) const noexcept;
    [[nodiscard]] bool hasWater(int x, int z) const noexcept;

    // How much of the cell (0..1) is under the water surface; 0 above the surface or off-grid.
    [[nodiscard]] float depthIn(const Cell& cell) const noexcept;
    // True when the point (world blocks) sits below the water surface of its column.
    [[nodiscard]] bool isUnderwater(const Vec3& point) const noexcept;
};

class FluidSimulation {
public:
    using BlockLookup = std::function<BlockId(const Cell&)>;

    explicit FluidSimulation(const FluidRules& rules = {}, BlockLookup block_lookup = {});

    void setRules(const FluidRules& rules) noexcept { rules_ = rules; }
    [[nodiscard]] const FluidRules& rules() const noexcept { return rules_; }
    // An empty function restores the default, which reports air everywhere.
    void setBlockLookup(BlockLookup block_lookup);

    // Place or replace a level-0 source. Fails when the cell is solid in the host grid or holds the
    // other fluid.
    bool setSource(const Cell& cell, FluidType type);
    bool removeSource(const Cell& cell);
    void clear();

    [[nodiscard]] const FluidCell* get(const Cell& cell) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return field_.size(); }
    [[nodiscard]] bool empty() const noexcept { return field_.empty(); }
    [[nodiscard]] const std::map<Cell, FluidCell, CellOrder>& cells() const noexcept { return field_; }
    [[nodiscard]] uint32_t tickCount() const noexcept { return tick_counter_; }

    // One flow pass for both fluids now, ignoring the tick cadence.
    FluidTickResult step();
    // Advance the internal game-tick counter and run whichever fluid passes are due.
    FluidTickResult tick(uint32_t game_ticks = 1);

private:
    [[nodiscard]] bool isSolid(const Cell& cell) const;
    [[nodiscard]] bool isBlocked(const Cell& cell, FluidType type, const std::set<Cell, CellOrder>& blocked) const;
    [[nodiscard]] bool isDue(FluidType type) const noexcept;
    void collectInteractions(std::set<Cell, CellOrder>& blocked, FluidTickResult& out);
    void recompute(FluidType type, const std::set<Cell, CellOrder>& blocked, FluidTickResult& out);

    FluidRules rules_{};
    BlockLookup block_lookup_;
    std::map<Cell, FluidCell, CellOrder> sources_;
    std::map<Cell, FluidCell, CellOrder> field_;
    uint32_t tick_counter_{0};
};

} // namespace mc
