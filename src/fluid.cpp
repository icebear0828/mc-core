#include "mc/fluid.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

namespace mc {
namespace {

BlockId airLookup(const Cell&) noexcept { return BlockId::Air; }

// One queued spread step. `rank` orders sources before spread cells at the same level, so a source
// is never replaced by a falling column that happens to reach its cell.
struct Node {
    int level{0};
    int rank{0};
    int fall_depth{0};
    bool falling{false};
    Cell cell{};
};

// std::priority_queue pops the greatest element, so this orders by lowest level, then source first,
// then lowest cell. The full tie-break keeps the result reproducible.
struct NodeGreater {
    bool operator()(const Node& a, const Node& b) const noexcept {
        if (a.level != b.level) return a.level > b.level;
        if (a.rank != b.rank) return a.rank > b.rank;
        return CellOrder{}(b.cell, a.cell);
    }
};

constexpr int kSideDx[4] = {-1, 1, 0, 0};
constexpr int kSideDz[4] = {0, 0, -1, 1};

} // namespace

int FluidRules::maxLevel(FluidType type) const noexcept {
    switch (type) {
        case FluidType::Water: return water_spread;
        case FluidType::Lava: return in_nether ? lava_spread_nether : lava_spread_overworld;
        case FluidType::None: return 0;
    }
    return 0;
}

uint32_t FluidRules::tickInterval(FluidType type) const noexcept {
    switch (type) {
        case FluidType::Water: return water_tick_interval;
        case FluidType::Lava: return lava_tick_interval;
        case FluidType::None: return 0;
    }
    return 0;
}

void HostWaterGrid::set(int x, int z, float surface_y) noexcept {
    if (!inRange(x, z)) return;
    surface[static_cast<std::size_t>(z) * static_cast<std::size_t>(kSize) + static_cast<std::size_t>(x)] = surface_y;
}

void HostWaterGrid::setWorld(int x, int z, float surface_y) noexcept {
    set(x - origin_x, z - origin_z, surface_y);
}

float HostWaterGrid::surfaceAt(int x, int z) const noexcept {
    const int dx = x - origin_x;
    const int dz = z - origin_z;
    if (!inRange(dx, dz)) return std::numeric_limits<float>::quiet_NaN();
    const float height = surface[static_cast<std::size_t>(dz) * static_cast<std::size_t>(kSize) + static_cast<std::size_t>(dx)];
    // SkyCraft's sentinel test: anything far below the world is "no water".
    if (height < -1.0e20f) return std::numeric_limits<float>::quiet_NaN();
    return height;
}

bool HostWaterGrid::hasWater(int x, int z) const noexcept { return !std::isnan(surfaceAt(x, z)); }

float HostWaterGrid::depthIn(const Cell& cell) const noexcept {
    const float height = surfaceAt(cell.x, cell.z);
    if (std::isnan(height)) return 0.0f;
    const float depth = height - static_cast<float>(cell.y);
    if (depth < 0.02f) return 0.0f;
    return std::min(1.0f, depth);
}

bool HostWaterGrid::isUnderwater(const Vec3& point) const noexcept {
    const float height = surfaceAt(static_cast<int>(std::floor(point.x)), static_cast<int>(std::floor(point.z)));
    if (std::isnan(height)) return false;
    return point.y < height;
}

FluidSimulation::FluidSimulation(const FluidRules& rules, BlockLookup block_lookup)
    : rules_(rules), block_lookup_(block_lookup ? std::move(block_lookup) : BlockLookup(airLookup)) {}

void FluidSimulation::setBlockLookup(BlockLookup block_lookup) {
    block_lookup_ = block_lookup ? std::move(block_lookup) : BlockLookup(airLookup);
}

bool FluidSimulation::setSource(const Cell& cell, FluidType type) {
    if (type == FluidType::None) return false;
    if (isSolid(cell)) return false;
    const auto existing = field_.find(cell);
    if (existing != field_.end() && existing->second.type != FluidType::None && existing->second.type != type) {
        return false;
    }
    const FluidCell source{type, 0, false};
    sources_[cell] = source;
    field_[cell] = source;
    return true;
}

bool FluidSimulation::removeSource(const Cell& cell) {
    if (sources_.erase(cell) == 0) return false;
    const auto existing = field_.find(cell);
    if (existing != field_.end() && existing->second.isSource()) field_.erase(existing);
    return true;
}

void FluidSimulation::clear() {
    sources_.clear();
    field_.clear();
    tick_counter_ = 0;
}

const FluidCell* FluidSimulation::get(const Cell& cell) const noexcept {
    const auto it = field_.find(cell);
    return it != field_.end() ? &it->second : nullptr;
}

bool FluidSimulation::isSolid(const Cell& cell) const { return block_lookup_(cell) != BlockId::Air; }

bool FluidSimulation::isBlocked(const Cell& cell, FluidType type, const std::set<Cell, CellOrder>& blocked) const {
    if (blocked.find(cell) != blocked.end()) return true;
    if (isSolid(cell)) return true;
    // A cell holding the other fluid is closed to this one; the interaction pass deals with the pair.
    const auto existing = field_.find(cell);
    if (existing != field_.end() && existing->second.type != FluidType::None && existing->second.type != type) {
        return true;
    }
    return false;
}

bool FluidSimulation::isDue(FluidType type) const noexcept {
    const uint32_t interval = rules_.tickInterval(type);
    if (interval <= 1) return true;
    return tick_counter_ % interval == 0;
}

FluidTickResult FluidSimulation::step() {
    FluidTickResult out;
    std::set<Cell, CellOrder> blocked;
    collectInteractions(blocked, out);
    recompute(FluidType::Water, blocked, out);
    recompute(FluidType::Lava, blocked, out);
    return out;
}

FluidTickResult FluidSimulation::tick(uint32_t game_ticks) {
    FluidTickResult out;
    for (uint32_t i = 0; i < game_ticks; ++i) {
        ++tick_counter_;
        const bool water_due = isDue(FluidType::Water);
        const bool lava_due = isDue(FluidType::Lava);
        if (!water_due && !lava_due) continue;

        std::set<Cell, CellOrder> blocked;
        collectInteractions(blocked, out);
        if (water_due) recompute(FluidType::Water, blocked, out);
        if (lava_due) recompute(FluidType::Lava, blocked, out);
    }
    return out;
}

void FluidSimulation::collectInteractions(std::set<Cell, CellOrder>& blocked, FluidTickResult& out) {
    if (field_.empty()) return;

    // Snapshot: a conversion made here must not chain into another one in the same pass.
    const std::map<Cell, FluidCell, CellOrder> snapshot = field_;
    std::set<Cell, CellOrder> converted;
    std::set<Cell, CellOrder> handled_lava;

    // A lava column that falls straight onto water turns that water into stone.
    for (const auto& [cell, fluid] : snapshot) {
        if (fluid.type != FluidType::Water) continue;
        const Cell above{cell.x, cell.y + 1, cell.z};
        const auto it = snapshot.find(above);
        if (it == snapshot.end()) continue;
        if (it->second.type != FluidType::Lava || !it->second.falling) continue;
        converted.insert(cell);
        handled_lava.insert(above);
        out.interactions.push_back(FluidInteraction{cell, BlockId::Stone});
    }

    // Lava that meets water sideways sets: a source becomes obsidian, flowing lava cobblestone.
    for (const auto& [cell, fluid] : snapshot) {
        if (fluid.type != FluidType::Lava) continue;
        if (handled_lava.find(cell) != handled_lava.end()) continue;

        bool touches_water = false;
        for (int i = 0; i < 4 && !touches_water; ++i) {
            const Cell side{cell.x + kSideDx[i], cell.y, cell.z + kSideDz[i]};
            const auto it = snapshot.find(side);
            if (it == snapshot.end() || it->second.type != FluidType::Water) continue;
            if (converted.find(side) != converted.end()) continue;
            touches_water = true;
        }
        if (!touches_water) continue;

        converted.insert(cell);
        out.interactions.push_back(FluidInteraction{cell, fluid.isSource() ? BlockId::Obsidian : BlockId::Cobblestone});
    }

    for (const Cell& cell : converted) {
        field_.erase(cell);
        sources_.erase(cell);
        blocked.insert(cell);
    }
}

void FluidSimulation::recompute(FluidType type, const std::set<Cell, CellOrder>& blocked, FluidTickResult& out) {
    std::map<Cell, FluidCell, CellOrder> next;
    std::priority_queue<Node, std::vector<Node>, NodeGreater> queue;
    std::set<Cell, CellOrder> settled;

    for (const auto& [cell, source] : sources_) {
        if (source.type != type) continue;
        queue.push(Node{0, 0, 0, false, cell});
    }

    const int max_level = std::clamp(rules_.maxLevel(type), 0, static_cast<int>(kMaxFluidLevel));
    while (!queue.empty()) {
        const Node node = queue.top();
        queue.pop();
        if (!settled.insert(node.cell).second) continue;
        if (static_cast<int>(next.size()) >= rules_.max_cells) break;

        next[node.cell] = FluidCell{type, static_cast<uint8_t>(node.level), node.falling};

        const Cell below{node.cell.x, node.cell.y - 1, node.cell.z};
        // Vanilla flows down first; only a fluid that cannot fall runs sideways.
        if (node.fall_depth < rules_.max_fall_depth && !isBlocked(below, type, blocked)) {
            queue.push(Node{0, 1, node.fall_depth + 1, true, below});
            continue;
        }
        const int side_level = node.falling ? 1 : node.level + 1;
        if (side_level > max_level) continue;
        for (int i = 0; i < 4; ++i) {
            const Cell side{node.cell.x + kSideDx[i], node.cell.y, node.cell.z + kSideDz[i]};
            if (isBlocked(side, type, blocked)) continue;
            queue.push(Node{side_level, 1, 0, false, side});
        }
    }

    // Diff against what this fluid occupied before, in cell order so the result is stable.
    for (const auto& [cell, fluid] : field_) {
        if (fluid.type != type) continue;
        if (next.find(cell) == next.end()) out.updates.push_back(FluidUpdate{cell, FluidCell{}});
    }
    for (const auto& [cell, fluid] : next) {
        const auto it = field_.find(cell);
        if (it != field_.end() && it->second.type == fluid.type && it->second.level == fluid.level &&
            it->second.falling == fluid.falling) {
            continue;
        }
        out.updates.push_back(FluidUpdate{cell, fluid});
    }

    // Replace this fluid's cells and leave the other fluid untouched.
    for (auto it = field_.begin(); it != field_.end();) {
        if (it->second.type == type) {
            it = field_.erase(it);
        } else {
            ++it;
        }
    }
    field_.insert(next.begin(), next.end());
}

} // namespace mc
