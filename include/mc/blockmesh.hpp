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

inline mc::rig::RigMesh buildBlockMesh(const BlockGrid& grid,
                                       const std::vector<std::array<float, 3>>& primed_tnt_positions = {},
                                       const std::optional<MiningOverlay>& mining = std::nullopt,
                                       const std::optional<TerrainMiningOverlay>& terrain_mining = std::nullopt) {
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

    // Render active primed TNT cubes so they remain fully visible while ticking down!
    for (const auto& tpos : primed_tnt_positions) {
        const FaceCells cells = faceCellsFor(mc::BlockId::Tnt);
        const float bx = tpos[0] - 0.5f;
        const float by = tpos[1];
        const float bz = tpos[2] - 0.5f;
        for (const Face& f : faces) {
            if (mesh.vertices.size() + 4 > 65535) return mesh;
            const mc::hud::HudUV& uv = f.kind == 0 ? *cells.top : (f.kind == 1 ? *cells.side : *cells.bottom);
            const float us[4] = {uv.u0 + inset, uv.u1 - inset, uv.u1 - inset, uv.u0 + inset};
            const float vs[4] = {uv.v0 + inset, uv.v0 + inset, uv.v1 - inset, uv.v1 - inset};
            const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
            for (int k = 0; k < 4; ++k) {
                mesh.vertices.push_back({bx + f.c[k][0], by + f.c[k][1], bz + f.c[k][2], us[k], vs[k]});
            }
            static constexpr uint16_t kQuad[6] = {0, 1, 2, 0, 2, 3};
            for (uint16_t i : kQuad) mesh.indices.push_back(static_cast<uint16_t>(base + i));
        }
    }

    // Render progressive crack overlay on the block being mined
    if (mining.has_value() && mining->stage >= 0 && mining->stage <= 9 && grid.get(mining->cell) != mc::BlockId::Air) {
        const mc::hud::HudUV* crack_uv = crackUVForStage(mining->stage);
        if (crack_uv != nullptr) {
            for (const Face& f : faces) {
                if (grid.get({mining->cell.x + f.dx, mining->cell.y + f.dy, mining->cell.z + f.dz}) != mc::BlockId::Air) continue;
                if (mesh.vertices.size() + 4 > 65535) return mesh;
                const float us[4] = {crack_uv->u0 + inset, crack_uv->u1 - inset, crack_uv->u1 - inset, crack_uv->u0 + inset};
                const float vs[4] = {crack_uv->v0 + inset, crack_uv->v0 + inset, crack_uv->v1 - inset, crack_uv->v1 - inset};
                const float nx = static_cast<float>(f.dx) * 0.001f;
                const float ny = static_cast<float>(f.dy) * 0.001f;
                const float nz = static_cast<float>(f.dz) * 0.001f;
                const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
                for (int k = 0; k < 4; ++k) {
                    mesh.vertices.push_back({static_cast<float>(mining->cell.x) + f.c[k][0] + nx,
                                             static_cast<float>(mining->cell.y) + f.c[k][1] + ny,
                                             static_cast<float>(mining->cell.z) + f.c[k][2] + nz,
                                             us[k], vs[k]});
                }
                static constexpr uint16_t kQuad[6] = {0, 1, 2, 0, 2, 3};
                for (uint16_t i : kQuad) mesh.indices.push_back(static_cast<uint16_t>(base + i));
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

} // namespace mc
