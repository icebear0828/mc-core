#pragma once

// The game camera (ChrCam) and projection of world points to screen pixels. ChrCam: WorldChrMan+0x1ECE0 (A),
// 4x4 row-major matrix at +0x10 holding Right, Up, Forward, Position (Position at +0x40), vertical FOV in radians
// at +0x50 (A). Left-handed: R x U = +F, Forward points where the camera looks.

#include "eldenring_rtti.hpp"

#include <cmath>
#include <optional>

namespace eldenring::live {

namespace layout {
inline constexpr uintptr_t kChrCamInWorldChrMan = 0x1ECE0;
inline constexpr uint32_t kChrCamVtableRva = 0x2A2AA08;
inline constexpr uintptr_t kCamMatrix = 0x10;
inline constexpr uintptr_t kCamFovY = 0x50;
} // namespace layout

struct CameraPose {
    float right[3]{}, up[3]{}, forward[3]{}, position[3]{};
    float fov_y{0.f}; // radians, vertical
};

inline bool readCamera(const IMemoryReader& reader, uintptr_t image_base, uintptr_t world_chr_man, CameraPose& out) {
    uint64_t cam = 0;
    if (world_chr_man == 0 || !reader.read(world_chr_man + layout::kChrCamInWorldChrMan, &cam, sizeof(cam)) || cam == 0) return false;
    const auto c = static_cast<uintptr_t>(cam);
    if (!objectIsClass(reader, image_base, c, layout::kChrCamVtableRva, ".?AVChrCam@CS@@")) return false;
    float m[16];
    float fov = 0.f;
    if (!reader.read(c + layout::kCamMatrix, m, sizeof(m)) || !reader.read(c + layout::kCamFovY, &fov, sizeof(fov))) return false;
    for (float v : m) {
        if (!std::isfinite(v)) return false;
    }
    if (!(fov > 0.05f && fov < 3.0f)) return false; // 3 deg .. 172 deg
    CameraPose p;
    for (int i = 0; i < 3; ++i) {
        p.right[i] = m[0 + i];
        p.up[i] = m[4 + i];
        p.forward[i] = m[8 + i];
        p.position[i] = m[12 + i];
    }
    p.fov_y = fov;
    out = p;
    return true;
}

struct ScreenPoint {
    float x{0.f}, y{0.f}; // pixels, origin top-left
    float depth{0.f};     // distance along the view axis
};

// Perspective projection. `width`/`height` must be the real back buffer size (read from the swap chain, not the
// window's client rect). Returns nullopt for points behind the near distance.
inline std::optional<ScreenPoint> projectToScreen(const CameraPose& cam, const float world[3], float width, float height,
                                                  float near_distance = 0.05f) {
    if (!(width > 0.f) || !(height > 0.f)) return std::nullopt;
    const float d[3] = {world[0] - cam.position[0], world[1] - cam.position[1], world[2] - cam.position[2]};
    const auto dot = [&](const float* a) { return d[0] * a[0] + d[1] * a[1] + d[2] * a[2]; };
    const float xc = dot(cam.right), yc = dot(cam.up), zc = dot(cam.forward);
    if (!(zc > near_distance)) return std::nullopt;
    const float tan_half = std::tan(cam.fov_y * 0.5f);
    const float aspect = width / height;
    const float ndc_x = xc / (zc * tan_half * aspect);
    const float ndc_y = yc / (zc * tan_half);
    ScreenPoint s;
    s.x = (ndc_x + 1.f) * 0.5f * width;
    s.y = (1.f - ndc_y) * 0.5f * height;
    s.depth = zc;
    return s;
}

} // namespace eldenring::live
