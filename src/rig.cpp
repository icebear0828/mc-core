#include "mc/rig.hpp"

#include <cmath>

namespace mc::rig {

namespace {

struct PartDef {
    ModelPoint pivot;
    ModelPoint origin; // relative to pivot (Minecraft ModelPart: setPos(pivot) + addBox(origin, size))
    ModelPoint size;
    float u, v;
    float inflate;
};

// Same table as PARTS in tools/extract_mc_assets.py. Order matches mc::StevePart.
constexpr PartDef kParts[] = {
    /* Head        */ {{0, 0, 0}, {-4, -8, -4}, {8, 8, 8}, 0, 0, 0.0f},
    /* Body        */ {{0, 0, 0}, {-4, 0, -2}, {8, 12, 4}, 16, 16, 0.0f},
    /* RightArm    */ {{-5, 2, 0}, {-3, -2, -2}, {4, 12, 4}, 40, 16, 0.0f},
    /* LeftArm     */ {{5, 2, 0}, {-1, -2, -2}, {4, 12, 4}, 32, 48, 0.0f},
    /* RightLeg    */ {{-1.9f, 12, 0}, {-2, 0, -2}, {4, 12, 4}, 0, 16, 0.0f},
    /* LeftLeg     */ {{1.9f, 12, 0}, {-2, 0, -2}, {4, 12, 4}, 16, 48, 0.0f},
    /* Hat         */ {{0, 0, 0}, {-4, -8, -4}, {8, 8, 8}, 32, 0, 0.5f},
    /* Jacket      */ {{0, 0, 0}, {-4, 0, -2}, {8, 12, 4}, 16, 32, 0.25f},
    /* RightSleeve */ {{-5, 2, 0}, {-3, -2, -2}, {4, 12, 4}, 40, 32, 0.25f},
    /* LeftSleeve  */ {{5, 2, 0}, {-1, -2, -2}, {4, 12, 4}, 48, 48, 0.25f},
    /* RightPants  */ {{-1.9f, 12, 0}, {-2, 0, -2}, {4, 12, 4}, 0, 32, 0.25f},
    /* LeftPants   */ {{1.9f, 12, 0}, {-2, 0, -2}, {4, 12, 4}, 0, 48, 0.25f},
};
static_assert(sizeof(kParts) / sizeof(kParts[0]) == static_cast<size_t>(StevePart::Count));

const PartDef& def(StevePart part) { return kParts[static_cast<size_t>(part)]; }

struct Face {
    ModelPoint v[4];   // corners in part-local model pixels
    float u, v0, w, h; // skin rectangle (h may be negative: bottom faces are mirrored)
};

// Minecraft box unwrap; identical to cuboid() in the Python exporter.
std::array<Face, 6> boxFaces(const PartDef& d) {
    const float x0 = d.origin.x - d.inflate, y0 = d.origin.y - d.inflate, z0 = d.origin.z - d.inflate;
    const float x1 = d.origin.x + d.size.x + d.inflate, y1 = d.origin.y + d.size.y + d.inflate,
                z1 = d.origin.z + d.size.z + d.inflate;
    const float u = d.u, v = d.v, w = d.size.x, h = d.size.y, dd = d.size.z;
    return {{
        {{{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}}, u + dd, v + dd, w, h},            // front  (-z)
        {{{x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}}, u + dd + w + dd, v + dd, w, h},   // back   (+z)
        {{{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}}, u + dd + w, v + dd, dd, h},       // +x side
        {{{x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}}, u, v + dd, dd, h},                // -x side
        {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, u + dd, v, w, dd},               // top    (y0)
        {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, u + dd + w, v + dd, w, -dd},      // bottom (y1)
    }};
}

// Model pixels (x = character's left, y = down, z = back, pivot-relative handled by the caller) ->
// rig frame in centimetres (X forward, Y left, Z up, feet at the origin).
Vec3 modelToCanonicalCm(const ModelPoint& p) {
    constexpr float s = kCmPerModelPixel;
    return {-p.z * s, p.x * s, (24.0f - p.y) * s};
}

ModelPoint operator+(const ModelPoint& a, const ModelPoint& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
ModelPoint operator-(const ModelPoint& a, const ModelPoint& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(const ModelPoint& a, const ModelPoint& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

} // namespace

// ----------------------------------------------------------------------------------------- math

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.at(row, k) * b.at(k, col);
            r.m[static_cast<size_t>(row * 4 + col)] = sum;
        }
    }
    return r;
}

Mat4 translation(const Vec3& t) {
    Mat4 r;
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
}

Mat4 rotationAboutAxis(const Vec3& axis, float radians) {
    const Vec3 a = axis.normalized();
    const float c = std::cos(radians), s = std::sin(radians), t = 1.0f - c;
    // Column-vector (Rodrigues) matrix, transposed for the row-vector convention.
    const float rc[3][3] = {
        {t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y},
        {t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x},
        {t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c},
    };
    Mat4 r;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) r.m[static_cast<size_t>(row * 4 + col)] = rc[col][row];
    }
    return r;
}

Mat4 rotationFromQuat(const Quat& q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    const float rc[3][3] = {
        {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
        {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
        {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)},
    };
    Mat4 r;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) r.m[static_cast<size_t>(row * 4 + col)] = rc[col][row];
    }
    return r;
}

std::array<float, 4> transform(const Mat4& m, const Vec3& p) {
    std::array<float, 4> out{};
    for (int col = 0; col < 4; ++col) {
        out[static_cast<size_t>(col)] = p.x * m.at(0, col) + p.y * m.at(1, col) + p.z * m.at(2, col) + m.at(3, col);
    }
    return out;
}

Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    const auto o = transform(m, p);
    return {o[0], o[1], o[2]};
}

Mat4 perspectiveLH(float fov_y_radians, float aspect, float z_near, float z_far) {
    const float ys = 1.0f / std::tan(fov_y_radians * 0.5f);
    const float xs = ys / aspect;
    const float a = z_far / (z_far - z_near);
    Mat4 r;
    r.m = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, a, 1, 0, 0, -z_near * a, 0};
    return r;
}

Mat4 viewFromCamera(const Camera& cam) {
    const Vec3 right = cam.right, up = cam.up, fwd = cam.forward, p = cam.position;
    auto d = [&](const Vec3& a) { return a.x * p.x + a.y * p.y + a.z * p.z; };
    Mat4 r;
    r.m = {right.x, up.x, fwd.x, 0, right.y, up.y, fwd.y, 0, right.z, up.z, fwd.z, 0, -d(right), -d(up), -d(fwd), 1};
    return r;
}

Mat4 viewProjection(const Camera& cam, float fov_y_radians, float aspect, float z_near, float z_far) {
    return viewFromCamera(cam) * perspectiveLH(fov_y_radians, aspect, z_near, z_far);
}

bool sceneOccludes(const DepthConvention& conv, float game_depth, float rig_view_z) {
    if (!conv.reverse_z || !(conv.depth_times_distance > 0.0f)) return false;
    if (!(game_depth > 0.0f)) return false; // 0 = far plane; negatives and NaN are not depth
    return conv.depth_times_distance / game_depth < rig_view_z * (1.0f - conv.relative_bias) - conv.absolute_bias;
}

// ----------------------------------------------------------------------------------------- model

bool HostBasis::isReflection() const { return dot3(forward, cross(left, up)) < 0.0f; }

ModelPoint modelPivot(StevePart part) { return def(part).pivot; }

Vec3 partPivot(StevePart part, const HostBasis& basis) {
    return basis.fromCanonical(modelToCanonicalCm(def(part).pivot));
}

RigMesh buildPartMesh(StevePart part, const HostBasis& basis) {
    const PartDef& d = def(part);
    RigMesh mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    const auto faces = boxFaces(d);
    Vec3 centre{};
    std::vector<Vec3> positions;
    for (const Face& f : faces) {
        for (const ModelPoint& corner : f.v) {
            const Vec3 p = basis.fromCanonical(modelToCanonicalCm(corner + d.pivot));
            positions.push_back(p);
            centre += p;
        }
    }
    centre = centre * (1.0f / static_cast<float>(positions.size()));

    constexpr float inv = 1.0f / static_cast<float>(kSkinSize);
    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const Face& f = faces[fi];
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        // corner (s,t): v0 (0,0), v1 (1,0), v2 (1,1), v3 (0,1)
        const float s_of[4] = {0, 1, 1, 0};
        const float t_of[4] = {0, 0, 1, 1};
        for (size_t c = 0; c < 4; ++c) {
            const Vec3 p = positions[fi * 4 + c];
            mesh.vertices.push_back({p.x, p.y, p.z, (f.u + s_of[c] * f.w) * inv, (f.v0 + t_of[c] * f.h) * inv});
        }
        // Wind so the geometric normal (cross product of the host coordinates) points away from the box centre,
        // whatever the handedness of the basis.
        const Vec3 a = positions[fi * 4 + 0], b = positions[fi * 4 + 1], c2 = positions[fi * 4 + 2];
        const Vec3 n = cross(b - a, c2 - a);
        const bool outward = dot3(n, a - centre) > 0.0f;
        const uint16_t tri[6] = {0, 1, 2, 0, 2, 3};
        const uint16_t flipped[6] = {0, 2, 1, 0, 3, 2};
        for (uint16_t idx : outward ? tri : flipped) mesh.indices.push_back(static_cast<uint16_t>(base + idx));
    }
    return mesh;
}

std::vector<std::array<float, 2>> skinUvForLocalPoint(StevePart part, ModelPoint local_px, float tolerance) {
    std::vector<std::array<float, 2>> out;
    for (const Face& f : boxFaces(def(part))) {
        const ModelPoint e1 = f.v[1] - f.v[0];
        const ModelPoint e3 = f.v[3] - f.v[0];
        const float l1 = dot(e1, e1), l3 = dot(e3, e3);
        if (l1 <= 0.0f || l3 <= 0.0f) continue;
        const ModelPoint rel = local_px - f.v[0];
        const float s = dot(rel, e1) / l1, t = dot(rel, e3) / l3;
        const ModelPoint on_face{f.v[0].x + s * e1.x + t * e3.x, f.v[0].y + s * e1.y + t * e3.y, f.v[0].z + s * e1.z + t * e3.z};
        const ModelPoint off = local_px - on_face;
        if (std::sqrt(dot(off, off)) > tolerance) continue;
        const float s_tol = tolerance / std::sqrt(l1), t_tol = tolerance / std::sqrt(l3);
        if (s < -s_tol || s > 1.0f + s_tol || t < -t_tol || t > 1.0f + t_tol) continue;
        out.push_back({(f.u + s * f.w) / static_cast<float>(kSkinSize), (f.v0 + t * f.h) / static_cast<float>(kSkinSize)});
    }
    return out;
}

RigMesh buildGroundShadowMesh(const HostBasis& basis, float radius_cm) {
    RigMesh mesh;
    if (!(radius_cm > 0.0f)) return mesh;
    constexpr float kLiftCm = 1.0f;
    const float r = radius_cm;
    const float corners[4][2] = {{-r, -r}, {r, -r}, {r, r}, {-r, r}}; // (forward, left)
    const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) {
        const Vec3 p = basis.fromCanonical({corners[i][0], corners[i][1], kLiftCm});
        mesh.vertices.push_back({p.x, p.y, p.z, uvs[i][0], uvs[i][1]});
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

Mat4 yawMatrix(const HostBasis& basis, float canonical_yaw) {
    // Canonical yaw is counter-clockwise about +Z. A reflecting basis turns it into a clockwise rotation about
    // the image of +Z.
    return rotationAboutAxis(basis.up, basis.isReflection() ? -canonical_yaw : canonical_yaw);
}

Mat4 partMatrix(StevePart part, const Quat& rot, const Vec3& root_pos, float host_yaw, const HostBasis& basis) {
    const Vec3 pivot = partPivot(part, basis);
    return translation(pivot * -1.0f) * rotationFromQuat(rot) * translation(pivot) *
           rotationAboutAxis(basis.up, host_yaw) * translation(root_pos);
}

} // namespace mc::rig
