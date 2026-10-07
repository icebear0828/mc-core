#include "sekiro_steve.hpp"

#include <cmath>

namespace sekiro::render {

namespace {

using mc::StevePart;

// Same table as PARTS in tools/extract_mc_assets.py. `origin` is relative to `pivot` (Minecraft's
// ModelPart convention: setPos(pivot) + addBox(origin, size)). Order matches mc::StevePart.
struct PartDef {
    ModelPoint pivot;
    ModelPoint origin;
    ModelPoint size;
    float u, v;
    float inflate;
};

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
    ModelPoint v[4];      // corners in part-local model pixels
    float u, v0, w, h;    // skin rectangle (h may be negative: bottom faces are mirrored)
};

// Minecraft box unwrap; identical to cuboid() in the Python exporter.
std::array<Face, 6> boxFaces(const PartDef& d) {
    const float x0 = d.origin.x - d.inflate, y0 = d.origin.y - d.inflate, z0 = d.origin.z - d.inflate;
    const float x1 = d.origin.x + d.size.x + d.inflate, y1 = d.origin.y + d.size.y + d.inflate,
                z1 = d.origin.z + d.size.z + d.inflate;
    const float u = d.u, v = d.v, w = d.size.x, h = d.size.y, dd = d.size.z;
    return {{
        {{{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}}, u + dd, v + dd, w, h},                    // front  (-z)
        {{{x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}}, u + dd + w + dd, v + dd, w, h},           // back   (+z)
        {{{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}}, u + dd + w, v + dd, dd, h},               // +x side
        {{{x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}}, u, v + dd, dd, h},                        // -x side
        {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, u + dd, v, w, dd},                       // top    (y0)
        {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, u + dd + w, v + dd, w, -dd},              // bottom (y1)
    }};
}

// Model pixels (x = left, y = down, z = back) -> Dantelion metres (X = right, Y = up, Z = forward),
// feet on the ground. This is a reflection, as is every change of handedness.
native::FVector3 modelToNative(const ModelPoint& p) {
    constexpr float s = kMetresPerModelPixel;
    return {-p.x * s, (24.0f - p.y) * s, -p.z * s};
}

ModelPoint operator+(const ModelPoint& a, const ModelPoint& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
ModelPoint operator-(const ModelPoint& a, const ModelPoint& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(const ModelPoint& a, const ModelPoint& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

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

Mat4 translation(const native::FVector3& t) {
    Mat4 r;
    r.m[12] = t.X;
    r.m[13] = t.Y;
    r.m[14] = t.Z;
    return r;
}

Mat4 rotationY(float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    Mat4 r;
    r.m[0] = c;   // x' = x*c + z*s
    r.m[2] = -s;  // z' = -x*s + z*c
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

Mat4 rotationFromQuat(const native::FQuat& q) {
    const float x = q.X, y = q.Y, z = q.Z, w = q.W;
    // Column-vector rotation matrix, transposed for the row-vector convention.
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

std::array<float, 4> transform(const Mat4& m, const native::FVector3& p) {
    std::array<float, 4> out{};
    for (int col = 0; col < 4; ++col) {
        out[static_cast<size_t>(col)] = p.X * m.at(0, col) + p.Y * m.at(1, col) + p.Z * m.at(2, col) + m.at(3, col);
    }
    return out;
}

native::FVector3 transformPoint(const Mat4& m, const native::FVector3& p) {
    const auto o = transform(m, p);
    return {o[0], o[1], o[2]};
}

bool sceneOccludes(float game_depth, float steve_view_z) {
    if (!(game_depth > 0.0f)) return false; // 0 = far plane, negatives and NaN are not depth
    return kSceneDepthNear / game_depth < steve_view_z * (1.0f - kSceneOcclusionRelativeBias) - kSceneOcclusionBiasMetres;
}

Mat4 perspectiveLH(float fov_y_radians, float aspect, float z_near, float z_far) {
    const float ys = 1.0f / std::tan(fov_y_radians * 0.5f);
    const float xs = ys / aspect;
    const float a = z_far / (z_far - z_near);
    Mat4 r;
    r.m = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, a, 1, 0, 0, -z_near * a, 0};
    return r;
}

Mat4 viewFromCamera(const live::LiveSample& cam) {
    const native::FVector3 right = cam.cam_right, up = cam.cam_up, fwd = cam.cam_forward, p = cam.cam_pos;
    auto d = [&](const native::FVector3& a) { return a.X * p.X + a.Y * p.Y + a.Z * p.Z; };
    Mat4 r;
    r.m = {right.X, up.X, fwd.X, 0, right.Y, up.Y, fwd.Y, 0, right.Z, up.Z, fwd.Z, 0, -d(right), -d(up), -d(fwd), 1};
    return r;
}

Mat4 viewProjection(const live::LiveSample& cam, float fov_y_radians, float aspect, float z_near, float z_far) {
    return viewFromCamera(cam) * perspectiveLH(fov_y_radians, aspect, z_near, z_far);
}

// ----------------------------------------------------------------------------------------- model

ModelPoint modelPivot(StevePart part) { return def(part).pivot; }

native::FVector3 partPivot(StevePart part) { return modelToNative(def(part).pivot); }

SteveMesh buildPartMesh(StevePart part) {
    const PartDef& d = def(part);
    SteveMesh mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    const auto faces = boxFaces(d);
    native::FVector3 centre{};
    std::vector<native::FVector3> positions;
    for (const Face& f : faces) {
        for (const ModelPoint& corner : f.v) {
            const native::FVector3 p = modelToNative(corner + d.pivot);
            positions.push_back(p);
            centre = centre + p;
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
            const native::FVector3 p = positions[fi * 4 + c];
            mesh.vertices.push_back({p.X, p.Y, p.Z, (f.u + s_of[c] * f.w) * inv, (f.v0 + t_of[c] * f.h) * inv});
        }
        // Wind so the geometric normal (cross product, as written in the unit test) points away from the box centre.
        const native::FVector3 a = positions[fi * 4 + 0], b = positions[fi * 4 + 1], c2 = positions[fi * 4 + 2];
        const native::FVector3 e1 = b - a, e2 = c2 - a;
        const native::FVector3 n{e1.Y * e2.Z - e1.Z * e2.Y, e1.Z * e2.X - e1.X * e2.Z, e1.X * e2.Y - e1.Y * e2.X};
        const native::FVector3 out = a - centre;
        const bool outward = n.X * out.X + n.Y * out.Y + n.Z * out.Z > 0.0f;
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

Mat4 partMatrix(StevePart part, const native::FQuat& rot, const native::FVector3& root_pos, float root_yaw) {
    const native::FVector3 pivot = partPivot(part);
    return translation(pivot * -1.0f) * rotationFromQuat(rot) * translation(pivot) * rotationY(root_yaw) * translation(root_pos);
}

} // namespace sekiro::render
