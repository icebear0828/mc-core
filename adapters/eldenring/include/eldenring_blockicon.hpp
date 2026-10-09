#pragma once

// The GUI icon of a block as Minecraft draws it: the cube turned by the item's `gui` display rotation (30, 225, 0), seen from the front
// with no perspective, and lit from above (top 1.0, the two sides darker). Instead of one pre-rendered 16x16 picture the icon is three
// textured quadrilaterals made from the block's full face textures, so it stays sharp at any GUI scale.

#include "mc/hud_atlas.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <cmath>

namespace eldenring::blocks {

struct IconFace {
    float p[4][2]{};          // corners in the 0..1 square of the slot, y down; order matches the uv corners below
    mc::hud::HudUV uv;        // the face's atlas cell: p[0] = (u0,v0), p[1] = (u1,v0), p[2] = (u1,v1), p[3] = (u0,v1)
    float shade{1.f};         // 1.0 top; the sides are darker
    bool is_top{false};
};

struct IconCube {
    IconFace faces[3];
    int count{0};
};

inline IconCube blockIcon(mc::BlockId id) {
    const mc::hud::HudUV *top_uv, *side_uv;
    switch (id) {
        case mc::BlockId::Stone: top_uv = side_uv = &mc::hud::kUV_BLOCK_STONE; break;
        case mc::BlockId::Tnt: top_uv = &mc::hud::kUV_BLOCK_TNT_TOP; side_uv = &mc::hud::kUV_BLOCK_TNT_SIDE; break;
        default: top_uv = side_uv = &mc::hud::kUV_BLOCK_DIRT; break;
    }
    constexpr float kDeg = 3.14159265358979f / 180.f;
    const float cx = std::cos(30.f * kDeg), sx = std::sin(30.f * kDeg), cy = std::cos(225.f * kDeg), sy = std::sin(225.f * kDeg);
    // v' = Rx * Ry * v (Minecraft: rotationXYZ(30, 225, 0)); x right, y up, z towards the viewer
    auto rot = [&](const float v[3], float out[3]) {
        const float x1 = cy * v[0] + sy * v[2], y1 = v[1], z1 = -sy * v[0] + cy * v[2];
        out[0] = x1;
        out[1] = cx * y1 - sx * z1;
        out[2] = sx * y1 + cx * z1;
    };
    struct Face {
        float n[3];
        float c[4][3]; // top-left, top-right, bottom-right, bottom-left seen from outside
        bool top;
    };
    static const Face faces[6] = {
        {{0, 1, 0}, {{-.5f, .5f, -.5f}, {.5f, .5f, -.5f}, {.5f, .5f, .5f}, {-.5f, .5f, .5f}}, true},
        {{0, -1, 0}, {{-.5f, -.5f, .5f}, {.5f, -.5f, .5f}, {.5f, -.5f, -.5f}, {-.5f, -.5f, -.5f}}, false},
        {{1, 0, 0}, {{.5f, .5f, -.5f}, {.5f, .5f, .5f}, {.5f, -.5f, .5f}, {.5f, -.5f, -.5f}}, false},
        {{-1, 0, 0}, {{-.5f, .5f, .5f}, {-.5f, .5f, -.5f}, {-.5f, -.5f, -.5f}, {-.5f, -.5f, .5f}}, false},
        {{0, 0, 1}, {{.5f, .5f, .5f}, {-.5f, .5f, .5f}, {-.5f, -.5f, .5f}, {.5f, -.5f, .5f}}, false},
        {{0, 0, -1}, {{-.5f, .5f, -.5f}, {.5f, .5f, -.5f}, {.5f, -.5f, -.5f}, {-.5f, -.5f, -.5f}}, false},
    };
    IconCube icon;
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    float pts[3][4][2];
    for (const Face& f : faces) {
        float n[3];
        rot(f.n, n);
        if (n[2] <= 1e-4f) continue; // facing away from the viewer
        IconFace& out = icon.faces[icon.count];
        for (int k = 0; k < 4; ++k) {
            float v[3];
            rot(f.c[k], v);
            pts[icon.count][k][0] = v[0];
            pts[icon.count][k][1] = -v[1]; // screen y is down
            lo_x = std::min(lo_x, v[0]);
            hi_x = std::max(hi_x, v[0]);
            lo_y = std::min(lo_y, -v[1]);
            hi_y = std::max(hi_y, -v[1]);
        }
        out.is_top = f.top;
        out.uv = f.top ? *top_uv : *side_uv;
        // the two sides are told apart by whether they lean left or right on the screen
        out.shade = f.top ? 1.0f : (n[0] < 0.f ? 0.8f : 0.6f);
        ++icon.count;
        if (icon.count == 3) break;
    }
    // fit the cube's bounding box into the slot with a 1/16 margin, centred, same scale on both axes
    const float w = hi_x - lo_x, h = hi_y - lo_y;
    const float scale = (14.f / 16.f) / std::max(w, h);
    for (int i = 0; i < icon.count; ++i) {
        for (int k = 0; k < 4; ++k) {
            icon.faces[i].p[k][0] = 0.5f + (pts[i][k][0] - (lo_x + hi_x) * 0.5f) * scale;
            icon.faces[i].p[k][1] = 0.5f + (pts[i][k][1] - (lo_y + hi_y) * 0.5f) * scale;
        }
    }
    return icon;
}

} // namespace eldenring::blocks
