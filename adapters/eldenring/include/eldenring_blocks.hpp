#pragma once

// Minecraft blocks placed into the Elden Ring world (phase A: data, targeting and "soft" collision; the game's own physics world
// knows nothing about them). 1 block = 1 m on a world-aligned grid, Y up, game metres. Pure logic, unit-tested without the game.

#include "mc/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace eldenring::blocks {

struct Cell {
    int x{0}, y{0}, z{0};
    bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct CellHash {
    size_t operator()(const Cell& c) const noexcept {
        return (static_cast<size_t>(static_cast<uint32_t>(c.x)) * 73856093u) ^ (static_cast<size_t>(static_cast<uint32_t>(c.y)) * 19349663u) ^
               (static_cast<size_t>(static_cast<uint32_t>(c.z)) * 83492791u);
    }
};

inline Cell cellOf(float x, float y, float z) {
    return {static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(z))};
}

inline mc::BlockId blockForItem(mc::ItemId item) {
    switch (item) {
        case mc::ItemId::BlockDirt: return mc::BlockId::Dirt;
        case mc::ItemId::BlockStone: return mc::BlockId::Stone;
        case mc::ItemId::BlockTnt: return mc::BlockId::Tnt;
        default: return mc::BlockId::Air;
    }
}
inline mc::ItemId itemForBlock(mc::BlockId block) {
    switch (block) {
        case mc::BlockId::Dirt: return mc::ItemId::BlockDirt;
        case mc::BlockId::Stone: return mc::ItemId::BlockStone;
        case mc::BlockId::Tnt: return mc::ItemId::BlockTnt;
        default: return mc::ItemId::None;
    }
}

class BlockGrid {
public:
    static constexpr int kMaxBlocks = 2048; // the mesh is rebuilt on every change: keep it small

    bool place(const Cell& c, mc::BlockId id) {
        if (id == mc::BlockId::Air || blocks_.count(c) != 0 || static_cast<int>(blocks_.size()) >= kMaxBlocks) return false;
        blocks_[c] = id;
        ++version_;
        return true;
    }
    bool remove(const Cell& c) {
        if (blocks_.erase(c) == 0) return false;
        ++version_;
        return true;
    }
    [[nodiscard]] mc::BlockId get(const Cell& c) const {
        const auto it = blocks_.find(c);
        return it == blocks_.end() ? mc::BlockId::Air : it->second;
    }
    [[nodiscard]] size_t count() const { return blocks_.size(); }
    [[nodiscard]] unsigned version() const { return version_; }
    [[nodiscard]] const std::unordered_map<Cell, mc::BlockId, CellHash>& all() const { return blocks_; }
    void clear() {
        if (!blocks_.empty()) ++version_;
        blocks_.clear();
    }

private:
    std::unordered_map<Cell, mc::BlockId, CellHash> blocks_;
    unsigned version_{0};
};

struct GridHit {
    Cell cell;
    int normal[3]{0, 0, 0}; // outward normal of the face the ray entered through (0 when it starts inside the block)
    float distance{0.f};
};

// Voxel traversal (Amanatides & Woo) from `origin` along the unit vector `dir`, up to `max_dist` metres.
inline std::optional<GridHit> raycastGrid(const BlockGrid& grid, const float origin[3], const float dir[3], float max_dist) {
    if (grid.count() == 0) return std::nullopt;
    Cell c = cellOf(origin[0], origin[1], origin[2]);
    GridHit hit;
    if (grid.get(c) != mc::BlockId::Air) {
        hit.cell = c;
        return hit;
    }
    int step[3];
    float t_max[3], t_delta[3];
    const int cell_of[3] = {c.x, c.y, c.z};
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(dir[a]) < 1e-9f) {
            step[a] = 0;
            t_max[a] = t_delta[a] = 1e30f;
        } else if (dir[a] > 0.f) {
            step[a] = 1;
            t_delta[a] = 1.f / dir[a];
            t_max[a] = (static_cast<float>(cell_of[a] + 1) - origin[a]) / dir[a];
        } else {
            step[a] = -1;
            t_delta[a] = -1.f / dir[a];
            t_max[a] = (static_cast<float>(cell_of[a]) - origin[a]) / dir[a];
        }
    }
    int idx[3] = {c.x, c.y, c.z};
    for (int guard = 0; guard < 512; ++guard) {
        int a = 0;
        if (t_max[1] < t_max[a]) a = 1;
        if (t_max[2] < t_max[a]) a = 2;
        if (t_max[a] > max_dist) return std::nullopt;
        idx[a] += step[a];
        const float t = t_max[a];
        t_max[a] += t_delta[a];
        const Cell cur{idx[0], idx[1], idx[2]};
        if (grid.get(cur) != mc::BlockId::Air) {
            hit.cell = cur;
            hit.distance = t;
            hit.normal[a] = -step[a];
            return hit;
        }
    }
    return std::nullopt;
}

// The cell a block placed against a block's face occupies.
inline Cell placementCellForBlockHit(const GridHit& h) { return {h.cell.x + h.normal[0], h.cell.y + h.normal[1], h.cell.z + h.normal[2]}; }

// The cell a block placed against the game's terrain occupies: half a block along the dominant axis of the surface normal.
inline Cell placementCellForWorldHit(const float pos[3], const float normal[3]) {
    int axis = 0;
    if (std::fabs(normal[1]) > std::fabs(normal[axis])) axis = 1;
    if (std::fabs(normal[2]) > std::fabs(normal[axis])) axis = 2;
    float p[3] = {pos[0], pos[1], pos[2]};
    p[axis] += normal[axis] >= 0.f ? 0.5f : -0.5f;
    return cellOf(p[0], p[1], p[2]);
}

// The player's collision box, in metres.
inline constexpr float kPlayerHalfWidth = 0.3f;
inline constexpr float kPlayerHeight = 1.8f;
inline constexpr float kOverlapEps = 1e-4f;

inline bool cellTouchesPlayer(const Cell& c, const float feet[3]) {
    return static_cast<float>(c.x + 1) > feet[0] - kPlayerHalfWidth + kOverlapEps && static_cast<float>(c.x) < feet[0] + kPlayerHalfWidth - kOverlapEps &&
           static_cast<float>(c.z + 1) > feet[2] - kPlayerHalfWidth + kOverlapEps && static_cast<float>(c.z) < feet[2] + kPlayerHalfWidth - kOverlapEps &&
           static_cast<float>(c.y + 1) > feet[1] + kOverlapEps && static_cast<float>(c.y) < feet[1] + kPlayerHeight - kOverlapEps;
}

struct Resolve {
    float feet[3]{0.f, 0.f, 0.f};
    bool moved{false};
    bool standing{false}; // lifted onto the top of a block
};

// Pushes the player out of any blocks it overlaps along the axis that needs the least movement. Gives up (and changes nothing)
// when that would move the player more than `max_push` in total: a teleport guard, so a bad state never throws the player around.
inline Resolve resolvePlayer(const BlockGrid& grid, const float feet_in[3], float max_push = 1.0f) {
    Resolve r;
    for (int i = 0; i < 3; ++i) r.feet[i] = feet_in[i];
    if (grid.count() == 0) return r;
    bool standing = false;
    for (int iter = 0; iter < 6; ++iter) {
        const int x0 = static_cast<int>(std::floor(r.feet[0] - kPlayerHalfWidth)), x1 = static_cast<int>(std::floor(r.feet[0] + kPlayerHalfWidth));
        const int y0 = static_cast<int>(std::floor(r.feet[1])), y1 = static_cast<int>(std::floor(r.feet[1] + kPlayerHeight));
        const int z0 = static_cast<int>(std::floor(r.feet[2] - kPlayerHalfWidth)), z1 = static_cast<int>(std::floor(r.feet[2] + kPlayerHalfWidth));
        float best = 1e9f;
        float best_delta[3] = {0.f, 0.f, 0.f};
        bool best_up = false;
        bool any = false;
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                for (int z = z0; z <= z1; ++z) {
                    const Cell c{x, y, z};
                    if (grid.get(c) == mc::BlockId::Air || !cellTouchesPlayer(c, r.feet)) continue;
                    any = true;
                    struct Option {
                        float amount;
                        int axis;
                        float signed_delta;
                        bool up;
                    } options[5] = {
                        {static_cast<float>(c.x + 1) - (r.feet[0] - kPlayerHalfWidth), 0, static_cast<float>(c.x + 1) - (r.feet[0] - kPlayerHalfWidth), false},
                        {(r.feet[0] + kPlayerHalfWidth) - static_cast<float>(c.x), 0, -((r.feet[0] + kPlayerHalfWidth) - static_cast<float>(c.x)), false},
                        {static_cast<float>(c.z + 1) - (r.feet[2] - kPlayerHalfWidth), 2, static_cast<float>(c.z + 1) - (r.feet[2] - kPlayerHalfWidth), false},
                        {(r.feet[2] + kPlayerHalfWidth) - static_cast<float>(c.z), 2, -((r.feet[2] + kPlayerHalfWidth) - static_cast<float>(c.z)), false},
                        {static_cast<float>(c.y + 1) - r.feet[1], 1, static_cast<float>(c.y + 1) - r.feet[1], true},
                    };
                    for (const Option& o : options) {
                        if (o.amount < best) {
                            best = o.amount;
                            best_delta[0] = best_delta[1] = best_delta[2] = 0.f;
                            best_delta[o.axis] = o.signed_delta;
                            best_up = o.up;
                        }
                    }
                    // the head against an underside
                    const float down = (r.feet[1] + kPlayerHeight) - static_cast<float>(c.y);
                    if (down < best) {
                        best = down;
                        best_delta[0] = best_delta[2] = 0.f;
                        best_delta[1] = -down;
                        best_up = false;
                    }
                }
            }
        }
        if (!any) break;
        for (int i = 0; i < 3; ++i) r.feet[i] += best_delta[i];
        r.moved = true;
        standing = standing || best_up;
    }
    const float dx = r.feet[0] - feet_in[0], dy = r.feet[1] - feet_in[1], dz = r.feet[2] - feet_in[2];
    // still overlapping after the iterations, or pushed too far: do nothing
    bool overlapping = false;
    if (r.moved) {
        const int x0 = static_cast<int>(std::floor(r.feet[0] - kPlayerHalfWidth)), x1 = static_cast<int>(std::floor(r.feet[0] + kPlayerHalfWidth));
        const int y0 = static_cast<int>(std::floor(r.feet[1])), y1 = static_cast<int>(std::floor(r.feet[1] + kPlayerHeight));
        const int z0 = static_cast<int>(std::floor(r.feet[2] - kPlayerHalfWidth)), z1 = static_cast<int>(std::floor(r.feet[2] + kPlayerHalfWidth));
        for (int x = x0; x <= x1 && !overlapping; ++x)
            for (int y = y0; y <= y1 && !overlapping; ++y)
                for (int z = z0; z <= z1 && !overlapping; ++z) overlapping = grid.get({x, y, z}) != mc::BlockId::Air && cellTouchesPlayer({x, y, z}, r.feet);
    }
    if (r.moved && (overlapping || std::sqrt(dx * dx + dy * dy + dz * dz) > max_push)) {
        Resolve none;
        for (int i = 0; i < 3; ++i) none.feet[i] = feet_in[i];
        return none;
    }
    r.standing = standing;
    return r;
}

} // namespace eldenring::blocks
