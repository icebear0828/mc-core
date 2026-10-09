#include "mc/item_model.hpp"

#include <array>
#include <cmath>

namespace mc::rig {

namespace {

constexpr float kDeg = 3.14159265358979f / 180.0f;
constexpr uint8_t kOpaque = 128;

struct Display {
    float rot[3];   // degrees
    float trans[3]; // sixteenths of a block (== model pixels)
    float scale;
};

Display displayFor(HeldItemStyle s) {
    switch (s) {
        case HeldItemStyle::Handheld: return {{0, -90, 55}, {0, 4, 0.5f}, 0.85f};
        case HeldItemStyle::Bow: return {{-80, 260, -40}, {-1, -2, 2.5f}, 0.9f};
        case HeldItemStyle::Generated: break;
    }
    return {{0, 0, 0}, {0, 3, 1}, 0.55f};
}

struct M3 {
    float m[3][3];
};

M3 mul(const M3& a, const M3& b) {
    M3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
        }
    }
    return r;
}
M3 rotX(float a) { return {{{1, 0, 0}, {0, std::cos(a), -std::sin(a)}, {0, std::sin(a), std::cos(a)}}}; }
M3 rotY(float a) { return {{{std::cos(a), 0, std::sin(a)}, {0, 1, 0}, {-std::sin(a), 0, std::cos(a)}}}; }
M3 rotZ(float a) { return {{{std::cos(a), -std::sin(a), 0}, {std::sin(a), std::cos(a), 0}, {0, 0, 1}}}; }
Vec3 apply(const M3& r, const Vec3& v) {
    return {r.m[0][0] * v.x + r.m[0][1] * v.y + r.m[0][2] * v.z, r.m[1][0] * v.x + r.m[1][1] * v.y + r.m[1][2] * v.z,
            r.m[2][0] * v.x + r.m[2][1] * v.y + r.m[2][2] * v.z};
}

// Model space (x = character's left, y = down, z = back), pixels, rest pose of the right arm.
// Sprite space: x right, y up, z toward the viewer, 16 pixels = 1 block, centred on the sprite.
Vec3 spriteToModel(const Vec3& p, const Display& d) {
    // display transform: translate * (rotX * rotY * rotZ) * scale   (the model is centred before it)
    const M3 r = mul(mul(rotX(d.rot[0] * kDeg), rotY(d.rot[1] * kDeg)), rotZ(d.rot[2] * kDeg));
    Vec3 q = apply(r, p * d.scale) + Vec3{d.trans[0], d.trans[1], d.trans[2]};
    // ItemInHandLayer: rotX(-90), rotY(180), translate(1/16, 0.125, -0.625) blocks = (1, 2, -10) pixels
    q = q + Vec3{1.0f, 2.0f, -10.0f};
    q = apply(mul(rotX(-90.0f * kDeg), rotY(180.0f * kDeg)), q);
    // the right arm's pivot in model pixels
    return q + Vec3{-5.0f, 2.0f, 0.0f};
}

} // namespace

bool isHeldAsFlatSprite(ItemId item) {
    switch (item) {
        case ItemId::DiamondSword:
        case ItemId::DiamondPickaxe:
        case ItemId::Bow:
        case ItemId::Arrow:
        case ItemId::Trident:
        case ItemId::FlintAndSteel:
        case ItemId::EnderPearl:
        case ItemId::GoldenApple:
        case ItemId::EnchantedGoldenApple:
        case ItemId::Bread:
        case ItemId::CookedBeef:
        case ItemId::TotemOfUndying:
        case ItemId::FireworkRocket: return true;
        default: return false; // None, blocks, elytra
    }
}

HeldItemStyle heldItemStyle(ItemId item) {
    switch (item) {
        case ItemId::DiamondSword:
        case ItemId::DiamondPickaxe: return HeldItemStyle::Handheld;
        case ItemId::Bow: return HeldItemStyle::Bow;
        default: return HeldItemStyle::Generated;
    }
}

RigMesh buildHeldItemMesh(const ItemSprite& s, HeldItemStyle style, const HostBasis& basis) {
    RigMesh mesh;
    if (!s.rgba || s.w <= 0 || s.h <= 0 || s.x < 0 || s.y < 0 || s.x + s.w > s.atlas_w || s.y + s.h > s.atlas_h) return mesh;

    const Display display = displayFor(style);
    auto opaque = [&](int i, int j) {
        if (i < 0 || j < 0 || i >= s.w || j >= s.h) return false;
        return s.rgba[(static_cast<size_t>(s.y + j) * s.atlas_w + (s.x + i)) * 4 + 3] >= kOpaque;
    };

    // Sprite pixel (i, j): column from the left, row from the top. Sprite space is 16 px per block no matter
    // the sprite's resolution, centred on the sprite.
    const float unit = 16.0f / static_cast<float>(s.w);
    const float thickness = 1.0f; // one pixel deep, in model pixels
    auto corner = [&](float cx, float cy, float cz) {
        return Vec3{(cx - static_cast<float>(s.w) * 0.5f) * unit, (static_cast<float>(s.h) * 0.5f - cy) * unit, cz * thickness};
    };

    auto emit = [&](const Vec3 (&c)[4], const Vec3& outward_sprite, float u, float v) {
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        Vec3 host[4];
        for (int k = 0; k < 4; ++k) {
            host[k] = basis.fromCanonical([&] {
                const Vec3 model = spriteToModel(c[k], display);
                return Vec3{-model.z * kCmPerModelPixel, model.x * kCmPerModelPixel, (24.0f - model.y) * kCmPerModelPixel};
            }());
            mesh.vertices.push_back({host[k].x, host[k].y, host[k].z, u, v});
        }
        // Outward in host space: the sprite-space normal carried through the same transform.
        const Vec3 p0 = c[0];
        const Vec3 tip = spriteToModel(p0 + outward_sprite, display);
        const Vec3 root = spriteToModel(p0, display);
        const Vec3 model_out = tip - root;
        const Vec3 out = basis.fromCanonical({-model_out.z, model_out.x, -model_out.y});
        const Vec3 e1 = host[1] - host[0], e2 = host[2] - host[0];
        const Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        const bool outward = n.x * out.x + n.y * out.y + n.z * out.z > 0.0f;
        const uint16_t tri[6] = {0, 1, 2, 0, 2, 3};
        const uint16_t flipped[6] = {0, 2, 1, 0, 3, 2};
        for (uint16_t idx : outward ? tri : flipped) mesh.indices.push_back(static_cast<uint16_t>(base + idx));
    };

    const float inv_w = 1.0f / static_cast<float>(s.atlas_w), inv_h = 1.0f / static_cast<float>(s.atlas_h);
    constexpr float zf = 0.5f, zb = -0.5f; // front (toward the viewer) and back planes, in pixels * thickness
    for (int j = 0; j < s.h; ++j) {
        for (int i = 0; i < s.w; ++i) {
            if (!opaque(i, j)) continue;
            if (mesh.vertices.size() + 24 > 65535) return mesh; // never overflow 16-bit indices
            const float u = (static_cast<float>(s.x + i) + 0.5f) * inv_w;
            const float v = (static_cast<float>(s.y + j) + 0.5f) * inv_h;
            const float x0 = static_cast<float>(i), x1 = x0 + 1.0f, y0 = static_cast<float>(j), y1 = y0 + 1.0f;
            {
                const Vec3 front[4] = {corner(x0, y1, zf), corner(x1, y1, zf), corner(x1, y0, zf), corner(x0, y0, zf)};
                emit(front, {0, 0, 1}, u, v);
                const Vec3 back[4] = {corner(x1, y1, zb), corner(x0, y1, zb), corner(x0, y0, zb), corner(x1, y0, zb)};
                emit(back, {0, 0, -1}, u, v);
            }
            if (!opaque(i - 1, j)) { // left wall
                const Vec3 c[4] = {corner(x0, y1, zb), corner(x0, y1, zf), corner(x0, y0, zf), corner(x0, y0, zb)};
                emit(c, {-1, 0, 0}, u, v);
            }
            if (!opaque(i + 1, j)) { // right wall
                const Vec3 c[4] = {corner(x1, y1, zf), corner(x1, y1, zb), corner(x1, y0, zb), corner(x1, y0, zf)};
                emit(c, {1, 0, 0}, u, v);
            }
            if (!opaque(i, j - 1)) { // top wall
                const Vec3 c[4] = {corner(x0, y0, zf), corner(x1, y0, zf), corner(x1, y0, zb), corner(x0, y0, zb)};
                emit(c, {0, 1, 0}, u, v);
            }
            if (!opaque(i, j + 1)) { // bottom wall
                const Vec3 c[4] = {corner(x0, y1, zb), corner(x1, y1, zb), corner(x1, y1, zf), corner(x0, y1, zf)};
                emit(c, {0, -1, 0}, u, v);
            }
        }
    }
    return mesh;
}

RigMesh buildFlatItemMesh(const ItemSprite& s) {
    RigMesh mesh;
    if (!s.rgba || s.w <= 0 || s.h <= 0 || s.x < 0 || s.y < 0 || s.x + s.w > s.atlas_w || s.y + s.h > s.atlas_h) return mesh;
    auto opaque = [&](int i, int j) {
        if (i < 0 || j < 0 || i >= s.w || j >= s.h) return false;
        return s.rgba[(static_cast<size_t>(s.y + j) * s.atlas_w + (s.x + i)) * 4 + 3] >= kOpaque;
    };
    const float inv_w = 1.0f / static_cast<float>(s.atlas_w), inv_h = 1.0f / static_cast<float>(s.atlas_h);
    const float px = 1.0f / static_cast<float>(s.w); // one sprite pixel in blocks
    const float half_t = 0.5f / 16.0f;               // half the thickness: one 16th of a block deep in total
    auto corner = [&](float cx, float cy, float z) { return Vec3{(cx - static_cast<float>(s.w) * 0.5f) * px, (static_cast<float>(s.h) * 0.5f - cy) * px, z}; };
    auto emit = [&](const Vec3 (&c)[4], float u, float v) {
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        for (const Vec3& p : c) mesh.vertices.push_back({p.x, p.y, p.z, u, v});
        for (uint16_t idx : {0, 1, 2, 0, 2, 3}) mesh.indices.push_back(static_cast<uint16_t>(base + idx));
    };
    for (int j = 0; j < s.h; ++j) {
        for (int i = 0; i < s.w; ++i) {
            if (!opaque(i, j)) continue;
            if (mesh.vertices.size() + 24 > 65535) return mesh;
            const float u = (static_cast<float>(s.x + i) + 0.5f) * inv_w;
            const float v = (static_cast<float>(s.y + j) + 0.5f) * inv_h;
            const float x0 = static_cast<float>(i), x1 = x0 + 1.0f, y0 = static_cast<float>(j), y1 = y0 + 1.0f;
            const float zf = half_t, zb = -half_t;
            { const Vec3 q[4] = {corner(x0, y1, zf), corner(x1, y1, zf), corner(x1, y0, zf), corner(x0, y0, zf)}; emit(q, u, v); }
            { const Vec3 q[4] = {corner(x1, y1, zb), corner(x0, y1, zb), corner(x0, y0, zb), corner(x1, y0, zb)}; emit(q, u, v); }
            if (!opaque(i - 1, j)) { const Vec3 q[4] = {corner(x0, y1, zb), corner(x0, y1, zf), corner(x0, y0, zf), corner(x0, y0, zb)}; emit(q, u, v); }
            if (!opaque(i + 1, j)) { const Vec3 q[4] = {corner(x1, y1, zf), corner(x1, y1, zb), corner(x1, y0, zb), corner(x1, y0, zf)}; emit(q, u, v); }
            if (!opaque(i, j - 1)) { const Vec3 q[4] = {corner(x0, y0, zf), corner(x1, y0, zf), corner(x1, y0, zb), corner(x0, y0, zb)}; emit(q, u, v); }
            if (!opaque(i, j + 1)) { const Vec3 q[4] = {corner(x0, y1, zb), corner(x1, y1, zb), corner(x1, y1, zf), corner(x0, y1, zf)}; emit(q, u, v); }
        }
    }
    return mesh;
}

} // namespace mc::rig
