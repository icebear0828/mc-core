#pragma once

#include "mc/blocks.hpp"
#include "mc/hud_atlas.hpp"
#include "mc/item_model.hpp"
#include "mc/rig.hpp"

#include <array>
#include <optional>
#include <vector>

namespace mc {

struct FaceCells {
    const mc::hud::HudUV* top;
    const mc::hud::HudUV* side;
    const mc::hud::HudUV* bottom;
};

inline FaceCells faceCellsFor(mc::BlockId id) {
    switch (id) {
        case mc::BlockId::Stone: return {&mc::hud::kUV_BLOCK_STONE, &mc::hud::kUV_BLOCK_STONE, &mc::hud::kUV_BLOCK_STONE};
        case mc::BlockId::Cobblestone: return {&mc::hud::kUV_BLOCK_COBBLESTONE, &mc::hud::kUV_BLOCK_COBBLESTONE, &mc::hud::kUV_BLOCK_COBBLESTONE};
        case mc::BlockId::Wood: return {&mc::hud::kUV_BLOCK_WOOD_TOP, &mc::hud::kUV_BLOCK_WOOD_SIDE, &mc::hud::kUV_BLOCK_WOOD_TOP};
        case mc::BlockId::Obsidian: return {&mc::hud::kUV_BLOCK_OBSIDIAN, &mc::hud::kUV_BLOCK_OBSIDIAN, &mc::hud::kUV_BLOCK_OBSIDIAN};
        case mc::BlockId::Tnt: return {&mc::hud::kUV_BLOCK_TNT_TOP, &mc::hud::kUV_BLOCK_TNT_SIDE, &mc::hud::kUV_BLOCK_TNT_BOTTOM};
        case mc::BlockId::CraftingTable:
            return {&mc::hud::kUV_BLOCK_CRAFTING_TABLE_TOP, &mc::hud::kUV_BLOCK_CRAFTING_TABLE_FRONT,
                    &mc::hud::kUV_BLOCK_CRAFTING_TABLE_TOP};
        case mc::BlockId::Sand: return {&mc::hud::kUV_BLOCK_SAND, &mc::hud::kUV_BLOCK_SAND, &mc::hud::kUV_BLOCK_SAND};
        case mc::BlockId::Gravel:
            return {&mc::hud::kUV_BLOCK_GRAVEL, &mc::hud::kUV_BLOCK_GRAVEL, &mc::hud::kUV_BLOCK_GRAVEL};
        case mc::BlockId::WoodPlanks:
            return {&mc::hud::kUV_BLOCK_PLANKS, &mc::hud::kUV_BLOCK_PLANKS, &mc::hud::kUV_BLOCK_PLANKS};
        case mc::BlockId::StoneBricks:
            return {&mc::hud::kUV_BLOCK_STONE_BRICKS, &mc::hud::kUV_BLOCK_STONE_BRICKS, &mc::hud::kUV_BLOCK_STONE_BRICKS};
        case mc::BlockId::Bricks:
            return {&mc::hud::kUV_BLOCK_BRICKS, &mc::hud::kUV_BLOCK_BRICKS, &mc::hud::kUV_BLOCK_BRICKS};
        case mc::BlockId::WhiteWool:
            return {&mc::hud::kUV_BLOCK_WHITE_WOOL, &mc::hud::kUV_BLOCK_WHITE_WOOL, &mc::hud::kUV_BLOCK_WHITE_WOOL};
        case mc::BlockId::Anvil:
            return {&mc::hud::kUV_BLOCK_ANVIL_TOP, &mc::hud::kUV_BLOCK_ANVIL_SIDE, &mc::hud::kUV_BLOCK_ANVIL_TOP};
        case mc::BlockId::Glass: return {&mc::hud::kUV_BLOCK_GLASS, &mc::hud::kUV_BLOCK_GLASS, &mc::hud::kUV_BLOCK_GLASS};
        case mc::BlockId::Leaves: return {&mc::hud::kUV_BLOCK_LEAVES, &mc::hud::kUV_BLOCK_LEAVES, &mc::hud::kUV_BLOCK_LEAVES};
        case mc::BlockId::StoneSlab:
            return {&mc::hud::kUV_BLOCK_STONE_SLAB, &mc::hud::kUV_BLOCK_STONE_SLAB, &mc::hud::kUV_BLOCK_STONE_SLAB};
        case mc::BlockId::Torch: return {&mc::hud::kUV_BLOCK_TORCH, &mc::hud::kUV_BLOCK_TORCH, &mc::hud::kUV_BLOCK_TORCH};
        default: return {&mc::hud::kUV_BLOCK_DIRT, &mc::hud::kUV_BLOCK_DIRT, &mc::hud::kUV_BLOCK_DIRT};
    }
}

struct MiningOverlay {
    Cell cell{};
    int stage{-1}; // -1 = none, 0..9 = crack stage
};

// A crack decal on the host's static geometry (ground mining). The adapter supplies the terrain
// ray's hit position and normal; the quad is 0.9 m square, lifted 1 cm along the normal.
struct TerrainMiningOverlay {
    float pos[3]{};
    float normal[3]{0.f, 1.f, 0.f};
    int stage{-1};
    float lift{0.01f};      // metres along the normal; negative sinks the decal into the collision surface
    float half_size{0.45f}; // half of the quad's edge length
};

inline const mc::hud::HudUV* crackUVForStage(int stage) {
    switch (stage) {
        case 0: return &mc::hud::kUV_DESTROY_STAGE_0;
        case 1: return &mc::hud::kUV_DESTROY_STAGE_1;
        case 2: return &mc::hud::kUV_DESTROY_STAGE_2;
        case 3: return &mc::hud::kUV_DESTROY_STAGE_3;
        case 4: return &mc::hud::kUV_DESTROY_STAGE_4;
        case 5: return &mc::hud::kUV_DESTROY_STAGE_5;
        case 6: return &mc::hud::kUV_DESTROY_STAGE_6;
        case 7: return &mc::hud::kUV_DESTROY_STAGE_7;
        case 8: return &mc::hud::kUV_DESTROY_STAGE_8;
        case 9: return &mc::hud::kUV_DESTROY_STAGE_9;
        default: return nullptr;
    }
}

// One face of a block's shape: the neighbour it looks at, which cell of the atlas it samples, and
// whether it lies on a cell boundary (only then can an opaque neighbour hide it).
struct BlockFace {
    int dx, dy, dz;
    int kind; // 0 top, 1 side, 2 bottom
    bool boundary;
    float c[4][3];
};

inline void buildShapeFaces(mc::BlockId id, BlockFace out[6], int& count) {
    const mc::BlockBox b = mc::blockBox(id);
    const float x0 = b.x0, y0 = b.y0, z0 = b.z0, x1 = b.x1, y1 = b.y1, z1 = b.z1;
    const bool full_x = x0 <= 0.001f && x1 >= 0.999f;
    const bool full_y_top = y1 >= 0.999f;
    const bool full_y_bottom = y0 <= 0.001f;
    const bool full_z = z0 <= 0.001f && z1 >= 0.999f;
    const BlockFace faces[6] = {
        {0, 1, 0, 0, full_y_top, {{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}},
        {0, -1, 0, 2, full_y_bottom, {{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}},
        {1, 0, 0, 1, full_x, {{x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {x1, y0, z0}}},
        {-1, 0, 0, 1, full_x, {{x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}, {x0, y0, z1}}},
        {0, 0, 1, 1, full_z, {{x1, y1, z1}, {x0, y1, z1}, {x0, y0, z1}, {x1, y0, z1}}},
        {0, 0, -1, 1, full_z, {{x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}, {x0, y0, z0}}},
    };
    for (int i = 0; i < 6; ++i) out[i] = faces[i];
    count = 6;
}

// A face on a cell boundary is hidden only by an opaque neighbour that covers the whole side. A
// transparent neighbour (glass, leaves, air) keeps it: an opaque face is visible through glass, and
// two glass blocks keep the face between them so a wall does not look hollow. An opaque partial
// neighbour (a slab) only hides an identical shape's face; a full cube beside it keeps its own.
inline bool blockFaceVisible(mc::BlockId self, mc::BlockId neighbor, bool boundary) {
    if (!boundary) return true;
    if (!mc::isOpaqueBlock(neighbor)) return true;
    if (mc::blockShape(neighbor) == mc::BlockShape::FullCube) return false;
    return !(self == neighbor && mc::blockShape(self) != mc::BlockShape::FullCube);
}

inline mc::hud::HudUV subUV(const mc::hud::HudUV& uv, float x0, float y0, float x1, float y1) {
    const float du = uv.u1 - uv.u0, dv = uv.v1 - uv.v0;
    return {uv.u0 + du * x0 / 16.f, uv.v0 + dv * y0 / 16.f, uv.u0 + du * x1 / 16.f, uv.v0 + dv * y1 / 16.f};
}

// The atlas cell for one face. A torch maps the middle 2x10 column of its 16x16 sprite onto the post
// (the vanilla model's UVs); a bottom slab's sides use the bottom half of the side sprite, as the
// vanilla slab model does. Every other block uses the whole cell.
inline mc::hud::HudUV faceUV(mc::BlockId id, int kind) {
    const FaceCells cells = faceCellsFor(id);
    const mc::hud::HudUV& base = kind == 0 ? *cells.top : (kind == 1 ? *cells.side : *cells.bottom);
    if (id == mc::BlockId::Torch) {
        if (kind == 0) return subUV(base, 7.f, 6.f, 9.f, 8.f);   // the flame
        if (kind == 2) return subUV(base, 7.f, 14.f, 9.f, 16.f); // the stick's end
        return subUV(base, 7.f, 6.f, 9.f, 16.f);                 // the whole post
    }
    if (id == mc::BlockId::StoneSlab && kind == 1) return subUV(base, 0.f, 8.f, 16.f, 16.f);
    return base;
}

inline mc::rig::RigMesh buildBlockMesh(const BlockGrid& grid,
                                       const std::vector<std::array<float, 3>>& primed_tnt_positions = {},
                                       const std::optional<MiningOverlay>& mining = std::nullopt,
                                       const std::optional<TerrainMiningOverlay>& terrain_mining = std::nullopt) {
    mc::rig::RigMesh mesh;
    // a quarter of a source pixel inside the cell: the 4x enlarged atlas never samples a neighbouring sprite
    const float inset = 0.25f / static_cast<float>(mc::hud::kHudAtlasWidth);
    auto emitFace = [&](const BlockFace& f, const mc::hud::HudUV& uv, float ox, float oy, float oz) {
        if (mesh.vertices.size() + 4 > 65535) return false;
        const float us[4] = {uv.u0 + inset, uv.u1 - inset, uv.u1 - inset, uv.u0 + inset};
        const float vs[4] = {uv.v0 + inset, uv.v0 + inset, uv.v1 - inset, uv.v1 - inset};
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        for (int k = 0; k < 4; ++k) {
            mesh.vertices.push_back({ox + f.c[k][0], oy + f.c[k][1], oz + f.c[k][2], us[k], vs[k]});
        }
        static constexpr uint16_t kQuad[6] = {0, 1, 2, 0, 2, 3};
        for (uint16_t i : kQuad) mesh.indices.push_back(static_cast<uint16_t>(base + i));
        return true;
    };
    for (const auto& [cell, id] : grid.all()) {
        BlockFace faces[6];
        int face_count = 0;
        buildShapeFaces(id, faces, face_count);
        for (int i = 0; i < face_count; ++i) {
            const BlockFace& f = faces[i];
            const mc::BlockId neighbor = grid.get({cell.x + f.dx, cell.y + f.dy, cell.z + f.dz});
            if (!blockFaceVisible(id, neighbor, f.boundary)) continue;
            if (!emitFace(f, faceUV(id, f.kind), static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.z))) {
                return mesh;
            }
        }
    }

    // Render active primed TNT cubes so they remain fully visible while ticking down!
    for (const auto& tpos : primed_tnt_positions) {
        BlockFace faces[6];
        int face_count = 0;
        buildShapeFaces(mc::BlockId::Tnt, faces, face_count);
        const float bx = tpos[0] - 0.5f;
        const float by = tpos[1];
        const float bz = tpos[2] - 0.5f;
        for (int i = 0; i < face_count; ++i) {
            if (!emitFace(faces[i], faceUV(mc::BlockId::Tnt, faces[i].kind), bx, by, bz)) return mesh;
        }
    }

    // Render progressive crack overlay on the block being mined
    if (mining.has_value() && mining->stage >= 0 && mining->stage <= 9 && grid.get(mining->cell) != mc::BlockId::Air) {
        const mc::hud::HudUV* crack_uv = crackUVForStage(mining->stage);
        if (crack_uv != nullptr) {
            const mc::BlockId mined = grid.get(mining->cell);
            BlockFace faces[6];
            int face_count = 0;
            buildShapeFaces(mined, faces, face_count);
            for (int i = 0; i < face_count; ++i) {
                const BlockFace& f = faces[i];
                const mc::BlockId neighbor = grid.get({mining->cell.x + f.dx, mining->cell.y + f.dy, mining->cell.z + f.dz});
                if (!blockFaceVisible(mined, neighbor, f.boundary)) continue;
                const float nx = static_cast<float>(f.dx) * 0.001f;
                const float ny = static_cast<float>(f.dy) * 0.001f;
                const float nz = static_cast<float>(f.dz) * 0.001f;
                if (!emitFace(f, *crack_uv, static_cast<float>(mining->cell.x) + nx, static_cast<float>(mining->cell.y) + ny,
                              static_cast<float>(mining->cell.z) + nz)) {
                    return mesh;
                }
            }
        }
    }

    // Ground mining: a single crack quad on the host's static geometry, using the same 10-stage
    // destroy textures as a placed block. The mesh is already in world (game metre) space.
    if (terrain_mining.has_value()) {
        const TerrainMiningOverlay& o = *terrain_mining;
        const mc::hud::HudUV* crack_uv = (o.stage >= 0 && o.stage <= 9) ? crackUVForStage(o.stage) : nullptr;
        if (crack_uv != nullptr && mesh.vertices.size() + 4 <= 65535) {
            float n[3] = {o.normal[0], o.normal[1], o.normal[2]};
            float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (!(len > 1e-4f)) {
                n[0] = 0.f; n[1] = 1.f; n[2] = 0.f; len = 1.f;
            }
            for (float& v : n) v /= len;
            const bool up = std::abs(n[1]) > 0.9f;
            const float ref[3] = {0.f, up ? 0.f : 1.f, up ? 1.f : 0.f};
            float t[3] = {ref[1] * n[2] - ref[2] * n[1], ref[2] * n[0] - ref[0] * n[2], ref[0] * n[1] - ref[1] * n[0]};
            len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            if (len > 1e-4f) {
                for (float& v : t) v /= len;
                const float b[3] = {n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0]};
                const float half = o.half_size;
                const float lift = o.lift;
                const float c[3] = {o.pos[0] + n[0] * lift, o.pos[1] + n[1] * lift, o.pos[2] + n[2] * lift};
                const float corners[4][3] = {
                    {c[0] - t[0] * half - b[0] * half, c[1] - t[1] * half - b[1] * half, c[2] - t[2] * half - b[2] * half},
                    {c[0] + t[0] * half - b[0] * half, c[1] + t[1] * half - b[1] * half, c[2] + t[2] * half - b[2] * half},
                    {c[0] + t[0] * half + b[0] * half, c[1] + t[1] * half + b[1] * half, c[2] + t[2] * half + b[2] * half},
                    {c[0] - t[0] * half + b[0] * half, c[1] - t[1] * half + b[1] * half, c[2] - t[2] * half + b[2] * half},
                };
                const float us[4] = {crack_uv->u0 + inset, crack_uv->u1 - inset, crack_uv->u1 - inset, crack_uv->u0 + inset};
                const float vs[4] = {crack_uv->v0 + inset, crack_uv->v0 + inset, crack_uv->v1 - inset, crack_uv->v1 - inset};
                const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
                for (int k = 0; k < 4; ++k) {
                    mesh.vertices.push_back({corners[k][0], corners[k][1], corners[k][2], us[k], vs[k]});
                }
                static constexpr uint16_t kQuad[6] = {0, 1, 2, 0, 2, 3};
                for (uint16_t i : kQuad) mesh.indices.push_back(static_cast<uint16_t>(base + i));
            }
        }
    }

    return mesh;
}

// The ground crack as its own mesh so the overlay can draw it with a much smaller occlusion bias than
// the placed blocks: the block bias (8% + 5 cm) forces a floor decal to float to stay visible.
inline mc::rig::RigMesh buildTerrainCrackMesh(const TerrainMiningOverlay& o) {
    mc::rig::RigMesh mesh;
    const float inset = 0.25f / static_cast<float>(mc::hud::kHudAtlasWidth);
    const mc::hud::HudUV* crack_uv = (o.stage >= 0 && o.stage <= 9) ? crackUVForStage(o.stage) : nullptr;
    if (crack_uv == nullptr) return mesh;
    float n[3] = {o.normal[0], o.normal[1], o.normal[2]};
    float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(len > 1e-4f)) {
        n[0] = 0.f; n[1] = 1.f; n[2] = 0.f; len = 1.f;
    }
    for (float& v : n) v /= len;
    const bool up = std::abs(n[1]) > 0.9f;
    const float ref[3] = {0.f, up ? 0.f : 1.f, up ? 1.f : 0.f};
    float t[3] = {ref[1] * n[2] - ref[2] * n[1], ref[2] * n[0] - ref[0] * n[2], ref[0] * n[1] - ref[1] * n[0]};
    len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    if (!(len > 1e-4f)) return mesh;
    for (float& v : t) v /= len;
    const float b[3] = {n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0]};
    const float half = o.half_size;
    const float lift = o.lift;
    const float c[3] = {o.pos[0] + n[0] * lift, o.pos[1] + n[1] * lift, o.pos[2] + n[2] * lift};
    const float corners[4][3] = {
        {c[0] - t[0] * half - b[0] * half, c[1] - t[1] * half - b[1] * half, c[2] - t[2] * half - b[2] * half},
        {c[0] + t[0] * half - b[0] * half, c[1] + t[1] * half - b[1] * half, c[2] + t[2] * half - b[2] * half},
        {c[0] + t[0] * half + b[0] * half, c[1] + t[1] * half + b[1] * half, c[2] + t[2] * half + b[2] * half},
        {c[0] - t[0] * half + b[0] * half, c[1] - t[1] * half + b[1] * half, c[2] - t[2] * half + b[2] * half},
    };
    const float us[4] = {crack_uv->u0 + inset, crack_uv->u1 - inset, crack_uv->u1 - inset, crack_uv->u0 + inset};
    const float vs[4] = {crack_uv->v0 + inset, crack_uv->v0 + inset, crack_uv->v1 - inset, crack_uv->v1 - inset};
    for (int k = 0; k < 4; ++k) mesh.vertices.push_back({corners[k][0], corners[k][1], corners[k][2], us[k], vs[k]});
    static constexpr uint16_t kQuad[6] = {0, 1, 2, 0, 2, 3};
    for (uint16_t i : kQuad) mesh.indices.push_back(i);
    return mesh;
}

// A block as an item in the hand: the shape in block space with the cell centre at the origin (first person; 1 unit = 1
// block, y up, +z towards the viewer). The cell centre is the origin the item display transform expects, so a slab stays in
// the lower half and a torch keeps its post below the centre, exactly like the vanilla models.
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
inline mc::rig::RigMesh buildHeldCubeMesh(mc::BlockId id, const mc::rig::HostBasis& basis,
                                          mc::rig::Hand hand = mc::rig::Hand::Right) {
    const float inset = 0.25f / static_cast<float>(mc::hud::kHudAtlasWidth);
    auto rect = [&](const mc::hud::HudUV& uv) { return mc::rig::UvRect{uv.u0 + inset, uv.v0 + inset, uv.u1 - inset, uv.v1 - inset}; };
    return mc::rig::buildHeldBlockMesh(rect(faceUV(id, 0)), rect(faceUV(id, 1)), rect(faceUV(id, 2)), basis, mc::blockBox(id), hand);
}

} // namespace mc
