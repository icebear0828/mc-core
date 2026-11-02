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

#include "mc/blocks.hpp"

namespace eldenring::blocks {

using mc::Cell;
using mc::CellHash;
using mc::cellOf;
using mc::blockForItem;
using mc::itemForBlock;
using mc::BlockGrid;

struct GridHit {
    Cell cell;
    int normal[3]{0, 0, 0}; // outward normal of the face the ray entered through (0 when it starts inside the block)
    float distance{0.f};
};

// Ray vs a block's actual box inside `c`. A slab only fills the bottom half and a torch is a thin
// post, so the voxel traversal alone would target the empty part of the cell.
inline bool rayHitsBlockBox(const Cell& c, mc::BlockId id, const float origin[3], const float dir[3], float max_dist,
                            float& t_hit, int normal[3]) {
    const mc::BlockBox b = mc::blockBox(id);
    const float lo[3] = {static_cast<float>(c.x) + b.x0, static_cast<float>(c.y) + b.y0, static_cast<float>(c.z) + b.z0};
    const float hi[3] = {static_cast<float>(c.x) + b.x1, static_cast<float>(c.y) + b.y1, static_cast<float>(c.z) + b.z1};
    float tmin = 0.f, tmax = max_dist;
    int axis = -1, sign = 0;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(dir[a]) < 1e-9f) {
            if (origin[a] < lo[a] || origin[a] > hi[a]) return false;
            continue;
        }
        const float inv = 1.f / dir[a];
        float t0 = (lo[a] - origin[a]) * inv;
        float t1 = (hi[a] - origin[a]) * inv;
        int s = -1;
        if (t0 > t1) {
            std::swap(t0, t1);
            s = 1;
        }
        if (t0 > tmin) {
            tmin = t0;
            axis = a;
            sign = s;
        }
        if (t1 < tmax) tmax = t1;
        if (tmin > tmax) return false;
    }
    if (tmin < 0.f || tmin > max_dist) return false;
    t_hit = tmin;
    normal[0] = normal[1] = normal[2] = 0;
    if (axis >= 0) normal[axis] = sign;
    return true;
}

// Voxel traversal (Amanatides & Woo) from `origin` along the unit vector `dir`, up to `max_dist` metres.
inline std::optional<GridHit> raycastGrid(const BlockGrid& grid, const float origin[3], const float dir[3], float max_dist) {
    if (grid.count() == 0) return std::nullopt;
    Cell c = cellOf(origin[0], origin[1], origin[2]);
    GridHit hit;
    if (grid.get(c) != mc::BlockId::Air) {
        const mc::BlockId id = grid.get(c);
        hit.cell = c;
        if (mc::blockShape(id) == mc::BlockShape::FullCube) return hit;
        float t = 0.f;
        if (rayHitsBlockBox(c, id, origin, dir, max_dist, t, hit.normal)) {
            hit.distance = t;
            return hit;
        }
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
        const mc::BlockId id = grid.get(cur);
        if (id != mc::BlockId::Air) {
            if (mc::blockShape(id) == mc::BlockShape::FullCube) {
                hit.cell = cur;
                hit.distance = t;
                hit.normal[a] = -step[a];
                return hit;
            }
            float t_box = 0.f;
            int n[3] = {0, 0, 0};
            if (rayHitsBlockBox(cur, id, origin, dir, max_dist, t_box, n)) {
                hit.cell = cur;
                hit.distance = t_box;
                hit.normal[0] = n[0];
                hit.normal[1] = n[1];
                hit.normal[2] = n[2];
                return hit;
            }
            continue; // the ray goes through the empty part of a slab/torch cell
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

inline bool cellTouchesBox(const Cell& c, mc::BlockId id, const float feet[3], float half_width = kPlayerHalfWidth,
                           float height = kPlayerHeight) {
    if (!mc::hasBlockCollision(id)) return false;
    const mc::BlockBox b = mc::blockBox(id);
    const float x0 = static_cast<float>(c.x) + b.x0, x1 = static_cast<float>(c.x) + b.x1;
    const float y0 = static_cast<float>(c.y) + b.y0, y1 = static_cast<float>(c.y) + b.y1;
    const float z0 = static_cast<float>(c.z) + b.z0, z1 = static_cast<float>(c.z) + b.z1;
    const float px0 = feet[0] - half_width + kOverlapEps, px1 = feet[0] + half_width - kOverlapEps;
    const float py0 = feet[1] + kOverlapEps, py1 = feet[1] + height - kOverlapEps;
    const float pz0 = feet[2] - half_width + kOverlapEps, pz1 = feet[2] + half_width - kOverlapEps;
    return px0 < x1 && px1 > x0 && py0 < y1 && py1 > y0 && pz0 < z1 && pz1 > z0;
}

// Full-cube test for callers that only have a cell.
inline bool cellTouchesBox(const Cell& c, const float feet[3], float half_width = kPlayerHalfWidth,
                           float height = kPlayerHeight) {
    return cellTouchesBox(c, mc::BlockId::Stone, feet, half_width, height);
}

inline bool cellTouchesPlayer(const Cell& c, mc::BlockId id, const float feet[3]) {
    return cellTouchesBox(c, id, feet, kPlayerHalfWidth, kPlayerHeight);
}

inline bool cellTouchesPlayer(const Cell& c, const float feet[3]) {
    return cellTouchesBox(c, feet, kPlayerHalfWidth, kPlayerHeight);
}

struct Resolve {
    float feet[3]{0.f, 0.f, 0.f};
    bool moved{false};
    bool standing{false}; // lifted onto the top of a block
};

// Pushes an entity AABB out of any blocks it overlaps along the axis that needs the least movement. Gives up (and changes nothing)
// when that would move the entity more than `max_push` in total: a teleport guard, so a bad state never throws the entity around.
inline Resolve resolveBox(const BlockGrid& grid, const float feet_in[3], float half_width = kPlayerHalfWidth, float height = kPlayerHeight, float max_push = 1.0f) {
    Resolve r;
    for (int i = 0; i < 3; ++i) r.feet[i] = feet_in[i];
    if (grid.count() == 0) return r;
    bool standing = false;
    for (int iter = 0; iter < 6; ++iter) {
        const int x0 = static_cast<int>(std::floor(r.feet[0] - half_width)), x1 = static_cast<int>(std::floor(r.feet[0] + half_width));
        const int y0 = static_cast<int>(std::floor(r.feet[1])), y1 = static_cast<int>(std::floor(r.feet[1] + height));
        const int z0 = static_cast<int>(std::floor(r.feet[2] - half_width)), z1 = static_cast<int>(std::floor(r.feet[2] + half_width));
        float best = 1e9f;
        float best_delta[3] = {0.f, 0.f, 0.f};
        bool best_up = false;
        bool any = false;
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                for (int z = z0; z <= z1; ++z) {
                    const Cell c{x, y, z};
                    const mc::BlockId id = grid.get(c);
                    if (!mc::hasBlockCollision(id) || !cellTouchesBox(c, id, r.feet, half_width, height)) continue;
                    any = true;
                    const mc::BlockBox b = mc::blockBox(id);
                    const float bx0 = static_cast<float>(c.x) + b.x0, bx1 = static_cast<float>(c.x) + b.x1;
                    const float by0 = static_cast<float>(c.y) + b.y0, by1 = static_cast<float>(c.y) + b.y1;
                    const float bz0 = static_cast<float>(c.z) + b.z0, bz1 = static_cast<float>(c.z) + b.z1;
                    struct Option {
                        float amount;
                        int axis;
                        float signed_delta;
                        bool up;
                    } options[5] = {
                        {bx1 - (r.feet[0] - half_width), 0, bx1 - (r.feet[0] - half_width), false},
                        {(r.feet[0] + half_width) - bx0, 0, -((r.feet[0] + half_width) - bx0), false},
                        {bz1 - (r.feet[2] - half_width), 2, bz1 - (r.feet[2] - half_width), false},
                        {(r.feet[2] + half_width) - bz0, 2, -((r.feet[2] + half_width) - bz0), false},
                        {by1 - r.feet[1], 1, by1 - r.feet[1], true},
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
                    const float down = (r.feet[1] + height) - by0;
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
        const int x0 = static_cast<int>(std::floor(r.feet[0] - half_width)), x1 = static_cast<int>(std::floor(r.feet[0] + half_width));
        const int y0 = static_cast<int>(std::floor(r.feet[1])), y1 = static_cast<int>(std::floor(r.feet[1] + height));
        const int z0 = static_cast<int>(std::floor(r.feet[2] - half_width)), z1 = static_cast<int>(std::floor(r.feet[2] + half_width));
        for (int x = x0; x <= x1 && !overlapping; ++x)
            for (int y = y0; y <= y1 && !overlapping; ++y)
                for (int z = z0; z <= z1 && !overlapping; ++z) {
                    const Cell c{x, y, z};
                    const mc::BlockId id = grid.get(c);
                    overlapping = mc::hasBlockCollision(id) && cellTouchesBox(c, id, r.feet, half_width, height);
                }
    }
    if (r.moved && (overlapping || std::sqrt(dx * dx + dy * dy + dz * dz) > max_push)) {
        Resolve none;
        for (int i = 0; i < 3; ++i) none.feet[i] = feet_in[i];
        return none;
    }
    r.standing = standing;
    return r;
}

inline Resolve resolvePlayer(const BlockGrid& grid, const float feet_in[3], float max_push = 1.0f) {
    return resolveBox(grid, feet_in, kPlayerHalfWidth, kPlayerHeight, max_push);
}

inline bool boxOverlapsBlocks(const BlockGrid& grid, const float feet[3], float half_width = kPlayerHalfWidth, float height = kPlayerHeight) {
    const int x0 = static_cast<int>(std::floor(feet[0] - half_width)), x1 = static_cast<int>(std::floor(feet[0] + half_width));
    const int y0 = static_cast<int>(std::floor(feet[1])), y1 = static_cast<int>(std::floor(feet[1] + height));
    const int z0 = static_cast<int>(std::floor(feet[2] - half_width)), z1 = static_cast<int>(std::floor(feet[2] + half_width));
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                const Cell c{x, y, z};
                const mc::BlockId id = grid.get(c);
                if (mc::hasBlockCollision(id) && cellTouchesBox(c, id, feet, half_width, height)) return true;
            }
    return false;
}

inline bool playerOverlapsBlocks(const BlockGrid& grid, const float feet[3]) {
    return boxOverlapsBlocks(grid, feet, kPlayerHalfWidth, kPlayerHeight);
}

// True when the feet rest on the top of a block: a block top within 6 cm below to 1 mm above the feet, under the footprint. (The
// player sunk into a block is not "standing": resolvePlayer lifts it first.)
inline bool supportedByBlock(const BlockGrid& grid, const float feet[3]) {
    const int x0 = static_cast<int>(std::floor(feet[0] - kPlayerHalfWidth + kOverlapEps)), x1 = static_cast<int>(std::floor(feet[0] + kPlayerHalfWidth - kOverlapEps));
    const int z0 = static_cast<int>(std::floor(feet[2] - kPlayerHalfWidth + kOverlapEps)), z1 = static_cast<int>(std::floor(feet[2] + kPlayerHalfWidth - kOverlapEps));
    const int y = static_cast<int>(std::floor(feet[1] - 0.06f)); // the cell whose top is just under the feet
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            const Cell c{x, y, z};
            const mc::BlockId id = grid.get(c);
            if (!mc::hasBlockCollision(id)) continue;
            const float top = static_cast<float>(y) + mc::blockBox(id).y1;
            if (feet[1] >= top - 1e-3f && feet[1] <= top + 0.06f) return true;
        }
    return false;
}

// Like resolveBox, but a fall that crossed the top of a block between two frames is caught: the entity ends up standing on the
// highest block top it passed through (a frame hitch or a high falling speed would otherwise take it through a thin block).
inline Resolve resolveBoxSwept(const BlockGrid& grid, const float prev[3], const float cur[3], float half_width = kPlayerHalfWidth, float height = kPlayerHeight, float max_push = 1.0f) {
    if (grid.count() != 0 && cur[1] < prev[1] - 1e-4f && prev[1] - cur[1] < 64.f) {
        const int x0 = static_cast<int>(std::floor(cur[0] - half_width + kOverlapEps)), x1 = static_cast<int>(std::floor(cur[0] + half_width - kOverlapEps));
        const int z0 = static_cast<int>(std::floor(cur[2] - half_width + kOverlapEps)), z1 = static_cast<int>(std::floor(cur[2] + half_width - kOverlapEps));
        const int y_top = static_cast<int>(std::floor(prev[1] + kOverlapEps)), y_low = static_cast<int>(std::floor(cur[1]));
        for (int y = y_top; y >= y_low; --y) { // from the highest block top the entity passed
            float top = 0.f;
            bool found = false;
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z) {
                    const Cell c{x, y, z};
                    const mc::BlockId id = grid.get(c);
                    if (!mc::hasBlockCollision(id)) continue;
                    top = std::max(top, static_cast<float>(y) + mc::blockBox(id).y1);
                    found = true;
                }
            if (!found) continue;
            if (top > prev[1] + kOverlapEps || top <= cur[1]) continue;
            Resolve r;
            r.feet[0] = cur[0];
            r.feet[1] = top;
            r.feet[2] = cur[2];
            // standing there must not leave the entity inside another block (a ceiling right above the block)
            if (boxOverlapsBlocks(grid, r.feet, half_width, height)) continue;
            r.moved = true;
            r.standing = true;
            return r;
        }
    }
    return resolveBox(grid, cur, half_width, height, max_push);
}

inline Resolve resolvePlayerSwept(const BlockGrid& grid, const float prev[3], const float cur[3], float max_push = 1.0f) {
    return resolveBoxSwept(grid, prev, cur, kPlayerHalfWidth, kPlayerHeight, max_push);
}

struct SneakMoveResult {
    float dx{0.f};
    float dz{0.f};
    bool clamped_x{false};
    bool clamped_z{false};
};

// Clamps horizontal movement (dx, dz) while sneaking so the player cannot step off a supporting block into the void.
// Follows Minecraft's authentic sneak edge protection: checks support under feet, binary searches safe step if an edge is reached.
inline SneakMoveResult clampSneakEdgeMovement(
    const BlockGrid& grid,
    const float feet[3],
    float desired_dx,
    float desired_dz) {
    SneakMoveResult res{desired_dx, desired_dz, false, false};
    if (desired_dx == 0.f && desired_dz == 0.f) return res;
    if (!supportedByBlock(grid, feet)) return res; // sneak protection only applies when grounded

    // 1. Check X-axis movement
    if (desired_dx != 0.f) {
        const float test_x[3] = {feet[0] + desired_dx, feet[1], feet[2]};
        if (supportedByBlock(grid, test_x)) {
            res.dx = desired_dx;
        } else {
            // Binary search the furthest safe step along X
            float low = 0.f, high = desired_dx;
            for (int it = 0; it < 8; ++it) {
                const float mid = (low + high) * 0.5f;
                const float probe[3] = {feet[0] + mid, feet[1], feet[2]};
                if (supportedByBlock(grid, probe)) low = mid;
                else high = mid;
            }
            res.dx = low;
            res.clamped_x = true;
        }
    }

    // 2. Check Z-axis movement (combined with safe X)
    if (desired_dz != 0.f) {
        const float test_z[3] = {feet[0] + res.dx, feet[1], feet[2] + desired_dz};
        if (supportedByBlock(grid, test_z)) {
            res.dz = desired_dz;
        } else {
            // Binary search the furthest safe step along Z
            float low = 0.f, high = desired_dz;
            for (int it = 0; it < 8; ++it) {
                const float mid = (low + high) * 0.5f;
                const float probe[3] = {feet[0] + res.dx, feet[1], feet[2] + mid};
                if (supportedByBlock(grid, probe)) low = mid;
                else high = mid;
            }
            res.dz = low;
            res.clamped_z = true;
        }
    }

    return res;
}

// Determines whether the player's position on placed blocks should be pinned to the anchor.
// Pinning is ONLY active when the player is supported by a block, the game window is in foreground,
// and no movement keys (W, A, S, D, Space) are held.
inline bool shouldAnchorBlockPosition(
    bool supported,
    bool key_w,
    bool key_a,
    bool key_s,
    bool key_d,
    bool key_space) {
    if (!supported) return false;
    const bool has_movement_input = key_w || key_a || key_s || key_d || key_space;
    return !has_movement_input;
}

// Directly steps horizontal position on blocks when moving, matching the Direct Drive approach.
inline bool stepBlockMovement(
    const float current_feet[3],
    const float dir[2],
    float speed,
    float dt,
    bool moving,
    float out_feet[3]) {
    out_feet[0] = current_feet[0];
    out_feet[1] = current_feet[1];
    out_feet[2] = current_feet[2];
    if (!moving || speed <= 0.f || dt <= 0.f) return false;
    out_feet[0] += dir[0] * speed * dt;
    out_feet[2] += dir[1] * speed * dt;
    return true;
}

// Checks if Havok character proxy synchronization (+0x91 = 1) is needed.
inline bool shouldSyncPhysicsProxy(
    bool collision_moved,
    bool direct_driven,
    bool sneak_clamped,
    bool pinned) {
    return collision_moved || direct_driven || sneak_clamped || pinned;
}

} // namespace eldenring::blocks

