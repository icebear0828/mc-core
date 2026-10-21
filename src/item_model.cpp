#include "mc/item_model.hpp"

#include <array>
#include <cmath>
#include <vector>

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
        case HeldItemStyle::Shield: return {{0, 0, 0}, {0, 0, 0}, 0.95f};
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

// Model space (x = character's left, y = down, z = back), pixels, rest pose of the given arm.
// Sprite space: x right, y up, z toward the viewer, 16 pixels = 1 block, centred on the sprite.
Vec3 spriteToModel(const Vec3& p, const Display& d, HeldItemStyle style, Hand hand) {
    if (style == HeldItemStyle::Shield) {
        const float s = d.scale;
        if (hand == Hand::Left) {
            // Left forearm: outer side is +X, forward is -Z, upward is -Y
            const Vec3 q{p.x * s + 2.0f, -p.y * s + 7.0f, -p.z * s - 1.5f};
            return q + Vec3{5.0f, 2.0f, 0.0f};
        } else {
            // Right forearm: outer side is -X, forward is -Z, upward is -Y
            const Vec3 q{-p.x * s - 2.0f, -p.y * s + 7.0f, -p.z * s - 1.5f};
            return q + Vec3{-5.0f, 2.0f, 0.0f};
        }
    }

    // display transform: translate * (rotX * rotY * rotZ) * scale   (the model is centred before it)
    const M3 r = mul(mul(rotX(d.rot[0] * kDeg), rotY(d.rot[1] * kDeg)), rotZ(d.rot[2] * kDeg));
    Vec3 q = apply(r, p * d.scale) + Vec3{d.trans[0], d.trans[1], d.trans[2]};
    // ItemInHandLayer: rotX(-90), rotY(180), translate(1/16, 0.125, -0.625) blocks = (1, 2, -10) pixels
    q = q + Vec3{1.0f, 2.0f, -10.0f};
    q = apply(mul(rotX(-90.0f * kDeg), rotY(180.0f * kDeg)), q);

    if (hand == Hand::Left) {
        // Mirrored across character's sagittal plane, relative to left arm pivot {+5, 2, 0}
        q.x = -q.x;
        return q + Vec3{5.0f, 2.0f, 0.0f};
    }
    // the right arm's pivot in model pixels
    return q + Vec3{-5.0f, 2.0f, 0.0f};
}

} // namespace

bool isHeldAsFlatSprite(ItemId item) {
    switch (item) {
        case ItemId::DiamondSword:
        case ItemId::DiamondPickaxe:
        case ItemId::WoodenPickaxe:
        case ItemId::StonePickaxe:
        case ItemId::IronPickaxe:
        case ItemId::GoldenPickaxe:
        case ItemId::NetheritePickaxe:
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
        case ItemId::FireworkRocket:
        case ItemId::ZombieSpawnEgg:
        case ItemId::CreeperSpawnEgg:
        case ItemId::SkeletonSpawnEgg:
        case ItemId::BowPulling0:
        case ItemId::BowPulling1:
        case ItemId::BowPulling2:
        case ItemId::Shield: return true;
        default: return false; // None, blocks, elytra
    }
}

HeldItemStyle heldItemStyle(ItemId item) {
    switch (item) {
        case ItemId::DiamondSword:
        case ItemId::DiamondPickaxe:
        case ItemId::WoodenPickaxe:
        case ItemId::StonePickaxe:
        case ItemId::IronPickaxe:
        case ItemId::GoldenPickaxe:
        case ItemId::NetheritePickaxe: return HeldItemStyle::Handheld;
        case ItemId::Bow:
        case ItemId::BowPulling0:
        case ItemId::BowPulling1:
        case ItemId::BowPulling2: return HeldItemStyle::Bow;
        case ItemId::Shield: return HeldItemStyle::Shield;
        default: return HeldItemStyle::Generated;
    }
}

RigMesh buildHeldItemMesh(const ItemSprite& s, HeldItemStyle style, const HostBasis& basis, Hand hand) {
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
                const Vec3 model = spriteToModel(c[k], display, style, hand);
                return Vec3{-model.z * kCmPerModelPixel, model.x * kCmPerModelPixel, (24.0f - model.y) * kCmPerModelPixel};
            }());
            mesh.vertices.push_back({host[k].x, host[k].y, host[k].z, u, v});
        }
        // Outward in host space: the sprite-space normal carried through the same transform.
        const Vec3 p0 = c[0];
        const Vec3 tip = spriteToModel(p0 + outward_sprite, display, style, hand);
        const Vec3 root = spriteToModel(p0, display, style, hand);
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

RigMesh buildHeldBlockMesh(const UvRect& top, const UvRect& side, const UvRect& bottom, const HostBasis& basis) {
    RigMesh mesh;
    const Display display{{75, 45, 0}, {0, 2.5f, 0}, 0.375f};
    struct Face {
        float c[4][3]; // corners of the unit cube (-0.5..0.5), seen from outside: top-left, top-right, bottom-right, bottom-left
        int kind;      // 0 top, 1 side, 2 bottom
    };
    static const Face faces[6] = {
        {{{-.5f, .5f, -.5f}, {.5f, .5f, -.5f}, {.5f, .5f, .5f}, {-.5f, .5f, .5f}}, 0},
        {{{-.5f, -.5f, .5f}, {.5f, -.5f, .5f}, {.5f, -.5f, -.5f}, {-.5f, -.5f, -.5f}}, 2},
        {{{.5f, .5f, -.5f}, {.5f, .5f, .5f}, {.5f, -.5f, .5f}, {.5f, -.5f, -.5f}}, 1},
        {{{-.5f, .5f, .5f}, {-.5f, .5f, -.5f}, {-.5f, -.5f, -.5f}, {-.5f, -.5f, .5f}}, 1},
        {{{.5f, .5f, .5f}, {-.5f, .5f, .5f}, {-.5f, -.5f, .5f}, {.5f, -.5f, .5f}}, 1},
        {{{-.5f, .5f, -.5f}, {.5f, .5f, -.5f}, {.5f, -.5f, -.5f}, {-.5f, -.5f, -.5f}}, 1},
    };
    for (const Face& f : faces) {
        const UvRect& r = f.kind == 0 ? top : (f.kind == 1 ? side : bottom);
        const float us[4] = {r.u0, r.u1, r.u1, r.u0};
        const float vs[4] = {r.v0, r.v0, r.v1, r.v1};
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        for (int k = 0; k < 4; ++k) {
            // sprite space is 16 pixels per block
            const Vec3 model = spriteToModel(Vec3{f.c[k][0] * 16.f, f.c[k][1] * 16.f, f.c[k][2] * 16.f}, display, HeldItemStyle::Handheld, Hand::Right);
            const Vec3 host = basis.fromCanonical(Vec3{-model.z * kCmPerModelPixel, model.x * kCmPerModelPixel, (24.0f - model.y) * kCmPerModelPixel});
            mesh.vertices.push_back({host.x, host.y, host.z, us[k], vs[k]});
        }
        static constexpr uint16_t kQuad[6] = {0, 1, 2, 0, 2, 3};
        for (uint16_t i : kQuad) mesh.indices.push_back(static_cast<uint16_t>(base + i));
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
        const uint16_t tri[6] = {0, 1, 2, 0, 2, 3};
        for (uint16_t idx : tri) mesh.indices.push_back(static_cast<uint16_t>(base + idx));
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

namespace {

// A face of a box in the shield's own model space (the game's: x towards the character's left, y down, z back; pixels). Corners are
// top-left, top-right, bottom-right, bottom-left as seen from outside, with the texel rectangle of the 64x64 sheet they show.
struct ShieldFace {
    Vec3 c[4];
    Vec3 out;      // outward direction
    float u[4], v[4]; // texels of the sheet
};

// ModelPart.Cube: texOffs (tu, tv), size (w, h, d); the net is [right d][front w][left d][back w] under [top w][bottom w].
void addBox(std::vector<ShieldFace>& faces, const Vec3& mn, const Vec3& mx, float tu, float tv) {
    const float w = mx.x - mn.x, h = mx.y - mn.y, d = mx.z - mn.z;
    const float uF = tu + d, uL = tu + d + w, uB = tu + d + w + d, vS = tv + d, vE = tv + d + h;
    auto face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 e, Vec3 out, float ua, float va, float ub, float vb) {
        faces.push_back({{a, b, c, e}, out, {ua, ub, ub, ua}, {va, va, vb, vb}});
    };
    // north (-z): the front, seen from -z the character's left (+x) is on the right
    face({mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z}, {0, 0, -1}, uF, vS, uF + w, vE);
    // south (+z): the back, seen from +z the character's left is on the left
    face({mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z}, {0, 0, 1}, uB, vS, uB + w, vE);
    // west (-x): the character's right side, the front edge on the viewer's right
    face({mn.x, mn.y, mx.z}, {mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mn.x, mx.y, mx.z}, {-1, 0, 0}, tu, vS, tu + d, vE);
    // east (+x): the character's left side, the front edge on the viewer's left
    face({mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mx.x, mx.y, mn.z}, {1, 0, 0}, uL, vS, uL + d, vE);
    // top (-y): the front edge at the bottom of the patch
    face({mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mn.y, mn.z}, {mn.x, mn.y, mn.z}, {0, -1, 0}, uF, tv, uF + w, vS);
    // bottom (+y): the front edge at the top of the patch
    face({mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, {0, 1, 0}, uL, tv, uL + w, vS);
}

// ShieldModel: the plate (-6, -11, -2) 12x22x1 at texOffs (0, 0) and the handle (-1, -3, -1) 2x6x6 at texOffs (26, 0).
const std::vector<ShieldFace>& shieldFaces() {
    static const std::vector<ShieldFace> faces = [] {
        std::vector<ShieldFace> f;
        addBox(f, {-6, -11, -2}, {6, 11, -1}, 0, 0);
        addBox(f, {-1, -3, -1}, {1, 3, 5}, 26, 0);
        return f;
    }();
    return faces;
}

// Emits every face: `place` maps a point of the shield's model space to the output space, and winding is fixed so the cross product of the
// output coordinates points along the transformed outward direction.
template <typename Place>
RigMesh emitShield(const UvRect& sheet, Place place) {
    RigMesh mesh;
    for (const ShieldFace& f : shieldFaces()) {
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        Vec3 p[4];
        for (int k = 0; k < 4; ++k) {
            p[k] = place(f.c[k]);
            const float u = sheet.u0 + (f.u[k] / 64.0f) * (sheet.u1 - sheet.u0);
            const float v = sheet.v0 + (f.v[k] / 64.0f) * (sheet.v1 - sheet.v0);
            mesh.vertices.push_back({p[k].x, p[k].y, p[k].z, u, v});
        }
        const Vec3 root = place(f.c[0]), tip = place(f.c[0] + f.out);
        const Vec3 out = tip - root;
        const Vec3 e1 = p[1] - p[0], e2 = p[2] - p[0];
        const Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        const bool outward = n.x * out.x + n.y * out.y + n.z * out.z > 0.0f;
        static constexpr uint16_t kTri[6] = {0, 1, 2, 0, 2, 3};
        static constexpr uint16_t kFlipped[6] = {0, 2, 1, 0, 3, 2};
        for (uint16_t i : outward ? kTri : kFlipped) mesh.indices.push_back(static_cast<uint16_t>(base + i));
    }
    return mesh;
}

} // namespace

namespace {

// models/item/shield.json and shield_blocking.json (1.21.8), thirdperson_righthand / thirdperson_lefthand; the scale is 1 in all four.
struct ShieldDisplayJson {
    float rot[3];   // degrees
    float trans[3]; // sixteenths of a block
};
constexpr ShieldDisplayJson kShieldThirdRight[2] = {{{0, 90, 0}, {10, 6, -4}}, {{45, 155, 0}, {-3.49f, 11, -2}}};
constexpr ShieldDisplayJson kShieldThirdLeft[2] = {{{0, 90, 0}, {10, 6, 12}}, {{45, 155, 0}, {11.51f, 7, 2.5f}}};

// A point of the shield model (ShieldModel pixels) in the absolute model space of the arm's rest pose (x towards the character's left, y down,
// z back; pixels).
Vec3 shieldThirdPersonPoint(const Vec3& l, Hand hand, bool blocking) {
    const bool left = hand == Hand::Left;
    const ShieldDisplayJson& d = (left ? kShieldThirdLeft : kShieldThirdRight)[blocking ? 1 : 0];
    // ItemTransform.apply(leftHand, ...): the left hand negates the y and z rotation and the x translation; rotationXYZ = Rx * Ry * Rz
    const float ry = left ? -d.rot[1] : d.rot[1], rz = left ? -d.rot[2] : d.rot[2];
    const float side = left ? -1.0f : 1.0f;
    const M3 r = mul(mul(rotX(d.rot[0] * kDeg), rotY(ry * kDeg)), rotZ(rz * kDeg));
    // the special renderer's scale(1, -1, -1) on model pixels / 16, and the translate(-0.5, -0.5, -0.5) of the item renderer
    const Vec3 p{l.x / 16.0f - 0.5f, -l.y / 16.0f - 0.5f, -l.z / 16.0f - 0.5f};
    Vec3 q = apply(r, p) + Vec3{side * d.trans[0] / 16.0f, d.trans[1] / 16.0f, d.trans[2] / 16.0f};
    // ItemInHandLayer: translate((left ? -1 : 1) / 16, 0.125, -0.625) after rotX(-90), rotY(180)
    q = q + Vec3{side / 16.0f, 0.125f, -0.625f};
    q = apply(mul(rotX(-90.0f * kDeg), rotY(180.0f * kDeg)), q);
    return Vec3{q.x * 16.0f + (left ? 5.0f : -5.0f), q.y * 16.0f + 2.0f, q.z * 16.0f}; // the arm's pivot
}

} // namespace

RigMesh buildShieldMesh(const UvRect& sheet, const HostBasis& basis, Hand hand, bool blocking) {
    auto place = [&](const Vec3& l) {
        const Vec3 model = shieldThirdPersonPoint(l, hand, blocking);
        return basis.fromCanonical(Vec3{-model.z * kCmPerModelPixel, model.x * kCmPerModelPixel, (24.0f - model.y) * kCmPerModelPixel});
    };
    return emitShield(sheet, place);
}

RigMesh buildShieldFirstPersonMesh(const UvRect& sheet) {
    auto place = [](const Vec3& l) { return Vec3{l.x / 16.0f - 0.5f, -l.y / 16.0f - 0.5f, -l.z / 16.0f - 0.5f}; };
    return emitShield(sheet, place);
}

} // namespace mc::rig
