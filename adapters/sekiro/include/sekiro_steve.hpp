#pragma once

// Platform independent geometry and camera math for drawing the 12-part Steve rig inside Sekiro's
// world. Everything is expressed in Dantelion space (metres, Y up, left-handed, row vectors:
// v' = v * M) so the Windows renderer can upload it unchanged.

#include "mc/animator.hpp"
#include "sekiro_live.hpp"
#include "sekiro_native.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace sekiro::render {

struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // row-major
    [[nodiscard]] float at(int row, int col) const { return m[static_cast<size_t>(row * 4 + col)]; }
};

// Row-vector convention: (a * b) applies a first, then b.
Mat4 operator*(const Mat4& a, const Mat4& b);
Mat4 translation(const native::FVector3& t);
Mat4 rotationY(float radians); // +Z turns toward +X
Mat4 rotationFromQuat(const native::FQuat& q);

std::array<float, 4> transform(const Mat4& m, const native::FVector3& p); // homogeneous result (w kept)
native::FVector3 transformPoint(const Mat4& m, const native::FVector3& p); // affine: xyz only

Mat4 perspectiveLH(float fov_y_radians, float aspect, float z_near, float z_far);
// World -> view for a camera whose world matrix rows are right, up, forward, position.
Mat4 viewFromCamera(const live::LiveSample& cam);
Mat4 viewProjection(const live::LiveSample& cam, float fov_y_radians, float aspect, float z_near = 0.05f,
                    float z_far = 500.0f);

// The game's scene depth is a reverse-Z buffer (cleared to 0, nearer = larger) with an effectively infinite
// far plane, so depth * viewZ is constant. The constant was measured on the real game by projecting points
// on the Wolf and reading the buffer there. A few percent of error is absorbed by the bias.
constexpr float kSceneDepthNear = 0.0806f;
constexpr float kSceneOcclusionRelativeBias = 0.08f; // surface must be this fraction nearer...
constexpr float kSceneOcclusionBiasMetres = 0.05f;   // ...plus this much
// True when the game's surface at this pixel is nearer to the camera than Steve's by more than the bias.
bool sceneOccludes(float game_depth, float steve_view_z);

// ---------------------------------------------------------------------------------------------
// Steve model: the same box/UV table as tools/extract_mc_assets.py (PARTS), 64x64 skin layout.
// Model pixels use Minecraft's model space (x = character's left, y = down, z = back). Mesh vertices
// come out in Dantelion metres with the feet at y = 0 and the character facing +Z.
// ---------------------------------------------------------------------------------------------
constexpr int kSkinSize = 64;
constexpr float kMetresPerModelPixel = 1.8f / 32.0f; // 32 model pixels = 1.8 m

struct ModelPoint {
    float x{0}, y{0}, z{0};
};

struct SteveVertex {
    float x, y, z; // metres, rest pose, absolute (pivot already applied)
    float u, v;    // skin texture coordinates in [0, 1], origin top-left
};

struct SteveMesh {
    std::vector<SteveVertex> vertices;
    std::vector<uint16_t> indices; // triangles, wound outward in Dantelion space
};

ModelPoint modelPivot(mc::StevePart part); // model pixels
SteveMesh buildPartMesh(mc::StevePart part);
native::FVector3 partPivot(mc::StevePart part); // rest-pose pivot, metres

// Every skin coordinate a point `local_px` (relative to the part's pivot, model pixels) can have on
// the part's box. Edge points lie on two faces, hence a list. Empty when the point is not on the box.
std::vector<std::array<float, 2>> skinUvForLocalPoint(mc::StevePart part, ModelPoint local_px, float tolerance = 1e-3f);

// Rotates the part about its pivot by `rot` (already in Dantelion axes), then yaws the whole rig about
// the vertical axis through the feet and moves it to `root_pos`.
Mat4 partMatrix(mc::StevePart part, const native::FQuat& rot, const native::FVector3& root_pos, float root_yaw);

} // namespace sekiro::render
