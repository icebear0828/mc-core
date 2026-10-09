#pragma once

// The mesh of the placed blocks: one world-space mesh (game metres, Y up) with the faces between neighbours left out, textured from
// the HUD atlas cells of the block faces. Rebuilt whenever the grid changes.

#include "eldenring_blocks.hpp"
#include "mc/hud_atlas.hpp"
#include "mc/item_model.hpp"
#include "mc/rig.hpp"

namespace eldenring::blocks {

struct FaceCells {
    const mc::hud::HudUV* top;
    const mc::hud::HudUV* side;
    const mc::hud::HudUV* bottom;
};

inline FaceCells faceCellsFor(mc::BlockId id) {
    switch (id) {
        case mc::BlockId::Stone: return {&mc::hud::kUV_BLOCK_STONE, &mc::hud::kUV_BLOCK_STONE, &mc::hud::kUV_BLOCK_STONE};
        case mc::BlockId::Tnt: return {&mc::hud::kUV_BLOCK_TNT_TOP, &mc::hud::kUV_BLOCK_TNT_SIDE, &mc::hud::kUV_BLOCK_TNT_BOTTOM};
        default: return {&mc::hud::kUV_BLOCK_DIRT, &mc::hud::kUV_BLOCK_DIRT, &mc::hud::kUV_BLOCK_DIRT};
    }
}

inline mc::rig::RigMesh buildBlockMesh(const BlockGrid& grid) {
    mc::rig::RigMesh mesh;
    // a quarter of a source pixel inside the cell: the 4x enlarged atlas never samples a neighbouring sprite
    const float inset = 0.25f / static_cast<float>(mc::hud::kHudAtlasWidth);
    struct Face {
        int dx, dy, dz;
        int kind; // 0 top, 1 side, 2 bottom
        float c[4][3]; // corners (unit cube), in order: top-left, top-right, bottom-right, bottom-left as seen from outside
    };
    static const Face faces[6] = {
        {0, 1, 0, 0, {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}}},
        {0, -1, 0, 2, {{0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}}},
        {1, 0, 0, 1, {{1, 1, 0}, {1, 1, 1}, {1, 0, 1}, {1, 0, 0}}},
        {-1, 0, 0, 1, {{0, 1, 1}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}}},
        {0, 0, 1, 1, {{1, 1, 1}, {0, 1, 1}, {0, 0, 1}, {1, 0, 1}}},
        {0, 0, -1, 1, {{0, 1, 0}, {1, 1, 0}, {1, 0, 0}, {0, 0, 0}}},
    };
    for (const auto& [cell, id] : grid.all()) {
        const FaceCells cells = faceCellsFor(id);
        for (const Face& f : faces) {
            if (grid.get({cell.x + f.dx, cell.y + f.dy, cell.z + f.dz}) != mc::BlockId::Air) continue;
            if (mesh.vertices.size() + 4 > 65535) return mesh;
            const mc::hud::HudUV& uv = f.kind == 0 ? *cells.top : (f.kind == 1 ? *cells.side : *cells.bottom);
            const float us[4] = {uv.u0 + inset, uv.u1 - inset, uv.u1 - inset, uv.u0 + inset};
            const float vs[4] = {uv.v0 + inset, uv.v0 + inset, uv.v1 - inset, uv.v1 - inset};
            const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
            for (int k = 0; k < 4; ++k) {
                mesh.vertices.push_back({static_cast<float>(cell.x) + f.c[k][0], static_cast<float>(cell.y) + f.c[k][1], static_cast<float>(cell.z) + f.c[k][2],
                                         us[k], vs[k]});
            }
            static constexpr uint16_t kQuad[6] = {0, 1, 2, 0, 2, 3};
            for (uint16_t i : kQuad) mesh.indices.push_back(static_cast<uint16_t>(base + i));
        }
    }
    return mesh;
}

// A block as an item in the hand: the unit cube centred on the origin (first person; 1 unit = 1 block, y up, +z towards the viewer).
inline mc::rig::RigMesh buildCubeMesh(mc::BlockId id) {
    BlockGrid g;
    g.place({0, 0, 0}, id);
    mc::rig::RigMesh mesh = buildBlockMesh(g);
    for (mc::rig::RigVertex& v : mesh.vertices) {
        v.x -= 0.5f;
        v.y -= 0.5f;
        v.z -= 0.5f;
    }
    return mesh;
}

// The same block held in the right hand in third person (see mc::rig::buildHeldBlockMesh).
inline mc::rig::RigMesh buildHeldCubeMesh(mc::BlockId id, const mc::rig::HostBasis& basis) {
    const FaceCells cells = faceCellsFor(id);
    const float inset = 0.25f / static_cast<float>(mc::hud::kHudAtlasWidth);
    auto rect = [&](const mc::hud::HudUV& uv) { return mc::rig::UvRect{uv.u0 + inset, uv.v0 + inset, uv.u1 - inset, uv.v1 - inset}; };
    return mc::rig::buildHeldBlockMesh(rect(*cells.top), rect(*cells.side), rect(*cells.bottom), basis);
}

} // namespace eldenring::blocks
