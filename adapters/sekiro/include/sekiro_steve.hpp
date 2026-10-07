#pragma once

// Sekiro's view of the shared Steve rig (mc/rig.hpp): the Dantelion coordinate system as a HostBasis, the
// measured depth-buffer convention, and thin wrappers that speak Dantelion types (FVector3, FQuat,
// LiveSample). Dantelion space: metres, Y up, left-handed, row vectors (v' = v * M). The geometry and
// math themselves live in the game-neutral core so other hosts reuse them.

#include "mc/rig.hpp"
#include "sekiro_live.hpp"
#include "sekiro_native.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace sekiro::render {

using Mat4 = mc::rig::Mat4;
using mc::rig::operator*;
using mc::rig::perspectiveLH;
using mc::rig::modelPivot;           // model pixels, host independent
using mc::rig::skinUvForLocalPoint;  // model space, host independent
using ModelPoint = mc::rig::ModelPoint;
using SteveVertex = mc::rig::RigVertex;
using SteveMesh = mc::rig::RigMesh;

constexpr int kSkinSize = mc::rig::kSkinSize;
constexpr float kMetresPerModelPixel = mc::rig::kCmPerModelPixel * 0.01f; // 32 model pixels = 1.8 m

// Canonical (X forward, Y left, Z up, cm) -> Dantelion (Z forward, -X left i.e. X right, Y up, metres).
// The mapping is a reflection (right-handed canonical space to a left-handed host).
inline constexpr mc::rig::HostBasis kSekiroBasis{{0.f, 0.f, 1.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 0.01f};

// The game's scene depth is a reverse-Z buffer (cleared to 0, nearer = larger) with an effectively infinite
// far plane, so depth * viewZ is constant. The constant was measured on the real game by projecting points
// on the Wolf and reading the buffer there. A few percent of error is absorbed by the bias.
constexpr float kSceneDepthNear = 0.0806f;
constexpr float kSceneOcclusionRelativeBias = 0.08f; // surface must be this fraction nearer...
constexpr float kSceneOcclusionBiasMetres = 0.05f;   // ...plus this much
inline constexpr mc::rig::DepthConvention kSekiroDepth{true, kSceneDepthNear, kSceneOcclusionRelativeBias,
                                                       kSceneOcclusionBiasMetres};
// True when the game's surface at this pixel is nearer to the camera than Steve's by more than the bias.
bool sceneOccludes(float game_depth, float steve_view_z);

// ---- Dantelion-typed wrappers over the core math ---------------------------------------------------------

Mat4 translation(const native::FVector3& t);
Mat4 rotationY(float radians); // +Z turns toward +X
Mat4 rotationFromQuat(const native::FQuat& q);
std::array<float, 4> transform(const Mat4& m, const native::FVector3& p); // homogeneous result (w kept)
native::FVector3 transformPoint(const Mat4& m, const native::FVector3& p); // affine: xyz only

// World -> view for a camera whose world matrix rows are right, up, forward, position.
mc::rig::Camera toRigCamera(const live::LiveSample& cam);
Mat4 viewFromCamera(const live::LiveSample& cam);
Mat4 viewProjection(const live::LiveSample& cam, float fov_y_radians, float aspect, float z_near = 0.05f,
                    float z_far = 500.0f);

// ---- Steve model in Dantelion metres, feet at y = 0, facing +Z ------------------------------------------

SteveMesh buildPartMesh(mc::StevePart part);
native::FVector3 partPivot(mc::StevePart part); // rest-pose pivot, metres

// Rotates the part about its pivot by `rot` (already in Dantelion axes), then yaws the whole rig about
// the vertical axis through the feet and moves it to `root_pos`.
Mat4 partMatrix(mc::StevePart part, const native::FQuat& rot, const native::FVector3& root_pos, float root_yaw);

} // namespace sekiro::render
