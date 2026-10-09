#pragma once

// Game-neutral geometry for drawing the 12-part Steve rig in a host's world: row-vector matrices (D3D
// convention: v' = v * M), a left-handed perspective camera, the Minecraft box model and its skin layout,
// and the depth-buffer occlusion rule. Nothing here knows a game: a port describes its coordinate
// system with a HostBasis and its depth buffer with a DepthConvention, then reuses the rest.
//
// Pipeline: Minecraft model pixels -> rig frame (canonical axes, feet at the origin) -> host axes/units.

#include "mc/animator.hpp"
#include "mc/types.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace mc::rig {

struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // row-major
    [[nodiscard]] float at(int row, int col) const { return m[static_cast<size_t>(row * 4 + col)]; }
};

// (a * b) applies a first, then b.
Mat4 operator*(const Mat4& a, const Mat4& b);
Mat4 translation(const Vec3& t);
// Rotation by `radians` about the unit vector `axis` (right-hand rule on the numbers: about +Y it takes +Z to +X).
Mat4 rotationAboutAxis(const Vec3& axis, float radians);
// `q` is a Hamilton quaternion already expressed in the host's axes.
Mat4 rotationFromQuat(const Quat& q);

std::array<float, 4> transform(const Mat4& m, const Vec3& p); // homogeneous result (w kept)
Vec3 transformPoint(const Mat4& m, const Vec3& p);            // affine: xyz only

Mat4 perspectiveLH(float fov_y_radians, float aspect, float z_near, float z_far);

// A camera as most engines expose it: world-space basis vectors and position, vertical FOV.
struct Camera {
    Vec3 right{1.f, 0.f, 0.f};
    Vec3 up{0.f, 1.f, 0.f};
    Vec3 forward{0.f, 0.f, 1.f};
    Vec3 position{};
};
Mat4 viewFromCamera(const Camera& cam);
Mat4 viewProjection(const Camera& cam, float fov_y_radians, float aspect, float z_near = 0.05f, float z_far = 500.0f);

// ---------------------------------------------------------------------------------------------
// Depth-buffer occlusion
// ---------------------------------------------------------------------------------------------

// How a host's depth buffer relates to distance. Measured per game (docs/PORTING_PLAYBOOK.md 6.5).
struct DepthConvention {
    bool reverse_z{true};           // cleared to 0, nearer = larger
    float depth_times_distance{0.f}; // reverse-Z with an infinite far plane: depth * viewZ is this constant
    float relative_bias{0.08f};     // the surface must be this fraction nearer than the rig...
    float absolute_bias{0.05f};     // ...plus this many host units
};

// True when the host's surface at this pixel is nearer to the camera than the rig by more than the bias.
// Only reverse-Z depth is understood so far; anything else never occludes.
bool sceneOccludes(const DepthConvention& conv, float game_depth, float rig_view_z);

// ---------------------------------------------------------------------------------------------
// Steve model
// ---------------------------------------------------------------------------------------------

constexpr int kSkinSize = 64;
constexpr float kCmPerModelPixel = 180.0f / 32.0f; // 32 model pixels = 1.8 m

struct ModelPoint {
    float x{0}, y{0}, z{0};
};

// How the host's axes and units relate to the canonical rig frame (Z up, X forward, Y left, centimetres).
// `forward`, `left` and `up` are the host-axis directions the canonical +X, +Y and +Z map to (orthonormal).
// A basis with a negative determinant is a reflection (left-handed host): rotations about the vertical
// axis then turn the other way, which yawMatrix accounts for.
struct HostBasis {
    Vec3 forward{1.f, 0.f, 0.f};
    Vec3 left{0.f, 1.f, 0.f};
    Vec3 up{0.f, 0.f, 1.f};
    float units_per_cm{1.f};

    [[nodiscard]] Vec3 fromCanonical(const Vec3& cm) const {
        return (forward * cm.x + left * cm.y + up * cm.z) * units_per_cm;
    }
    [[nodiscard]] bool isReflection() const;
};

struct RigVertex {
    float x, y, z; // host units, rest pose, absolute (pivot applied), feet at the origin
    float u, v;    // skin coordinates in [0, 1], origin top-left
};

struct RigMesh {
    std::vector<RigVertex> vertices;
    std::vector<uint16_t> indices; // triangles, wound so the cross product of the host coordinates points outward
};

ModelPoint modelPivot(StevePart part); // model pixels
Vec3 partPivot(StevePart part, const HostBasis& basis);
RigMesh buildPartMesh(StevePart part, const HostBasis& basis);

// Every skin coordinate a point `local_px` (relative to the part's pivot, model pixels) can have on the part's
// box. Edge points lie on two faces, hence a list. Empty when the point is not on the box.
std::vector<std::array<float, 2>> skinUvForLocalPoint(StevePart part, ModelPoint local_px, float tolerance = 1e-3f);

// A flat square on the feet plane (1 cm above it, so it does not fight the ground), `radius_cm` to each side, UV
// 0..1 across it: the renderer fades it into Minecraft's round blob shadow. Empty for a non-positive radius.
RigMesh buildGroundShadowMesh(const HostBasis& basis, float radius_cm);

// The whole figure turned about the host's vertical axis by a CANONICAL yaw (0 = forward, counter-clockwise
// positive seen from above). Handles left-handed hosts.
Mat4 yawMatrix(const HostBasis& basis, float canonical_yaw);

// Rotates the part about its pivot by `rot` (host axes), then turns by `host_yaw` about the host's vertical
// axis through the feet (the angle as that axis' right-hand rotation, i.e. already converted from canonical),
// then moves to `root_pos`.
Mat4 partMatrix(StevePart part, const Quat& rot, const Vec3& root_pos, float host_yaw, const HostBasis& basis);

} // namespace mc::rig
