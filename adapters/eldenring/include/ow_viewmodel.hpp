#pragma once

// Overwatch (Doomfist) first-person gauntlet: viewmodel projection, the
// procedural punch poses and the rigid CSVM parser. Pure logic, no Windows
// headers, so it is unit-tested on every host. The D3D12 renderer that uploads
// this data lives in ow_renderer_d3d12.cpp and depends on nothing from the CS2
// viewmodel subsystem.

#include "eldenring_fp.hpp"
#include "mc/rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace eldenring::ow {

// Source 2 style decoupled viewmodel projection. The gauntlet has its own FOV,
// scale and translation so it never inherits the CS2 or the host game camera.
struct OwViewmodelConfig {
    float fov_deg{68.0f};
    float offset_x{0.0f};
    float offset_y{0.0f};
    float offset_z{0.0f};
    float scale{1.0f};
    float z_near{0.001f};
    float z_far{20.0f};
};

inline mc::rig::Mat4 computeOwProjection(unsigned width, unsigned height, const OwViewmodelConfig& cfg = {}) {
    const float aspect = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : (16.0f / 9.0f);
    mc::rig::Mat4 proj = mc::rig::perspectiveLH(cfg.fov_deg * fp::kDeg, aspect, cfg.z_near, cfg.z_far);
    if (cfg.offset_x != 0.0f || cfg.offset_y != 0.0f || cfg.offset_z != 0.0f) {
        const mc::rig::Mat4 offset = mc::rig::translation(mc::Vec3{cfg.offset_x, cfg.offset_y, cfg.offset_z});
        proj = offset * proj;
    }
    return proj;
}

struct OwPose {
    fp::M4 base{fp::scale(1.0f)};
};

// The gauntlet's own state machine. 0 = idle sway, 2 = hand cannon jab,
// 3 = rocket punch windup + heavy thrust. The clips are procedural because the
// original UE asset ships skeletal data that the exporter bakes at rest.
class OwAnimator {
public:
    void tick(float dt, uint8_t state, float anim_time, uint8_t variant = 0) {
        (void)dt;
        (void)variant;
        current_state_ = state;
        anim_time_ = anim_time;

        const float bob = std::sin(anim_time * 2.5f) * 0.003f;
        fp::M4 base = fp::translate(0.0f, bob, 0.0f);

        if (state == 2) { // Primary attack: punch jab / hand cannon blast
            const float p = std::clamp(anim_time / 0.40f, 0.0f, 1.0f);
            float forward = 0.0f;
            float roll = 0.0f;
            if (p < 0.375f) {
                const float pp = p / 0.375f;
                forward = -0.16f * std::sin(pp * 1.570796f);
                roll = 10.0f * pp;
            } else {
                const float rp = (p - 0.375f) / 0.625f;
                forward = -0.16f * (1.0f - rp);
                roll = 10.0f * (1.0f - rp);
            }
            base = fp::mul(base, fp::translate(0.0f, 0.0f, forward));
            base = fp::mul(base, fp::rotZ(roll));
        } else if (state == 3) { // Rocket punch: windup and explosive heavy thrust
            const float p = std::clamp(anim_time / 0.80f, 0.0f, 1.0f);
            float thrust = 0.0f;
            float heave = 0.0f;
            if (p < 0.25f) {
                const float wp = p / 0.25f;
                thrust = 0.04f * std::sin(wp * 1.570796f);
            } else if (p < 0.60f) {
                const float pp = (p - 0.25f) / 0.35f;
                thrust = 0.04f - 0.28f * std::sin(pp * 1.570796f);
                heave = 0.07f * std::sin(pp * 3.14159265f);
            } else {
                const float rp = (p - 0.60f) / 0.40f;
                thrust = -0.24f * (1.0f - rp);
            }
            base = fp::mul(base, fp::translate(0.01f, heave, thrust));
            base = fp::mul(base, fp::rotX(heave * 60.0f));
        }

        pose_.base = base;
    }

    [[nodiscard]] const OwPose& pose() const { return pose_; }
    [[nodiscard]] uint8_t currentState() const { return current_state_; }
    [[nodiscard]] float currentAnimTime() const { return anim_time_; }

private:
    OwPose pose_{};
    uint8_t current_state_{0};
    float anim_time_{0.0f};
};

// Rigid CSVM mesh. Only the layout the gauntlet needs is kept: position,
// normal and UV. v4 skinned submeshes are rejected because the shipped asset
// is a v1 pose baked by the exporter.
struct OwVertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct OwSubmesh {
    uint32_t first_index{0};
    int32_t base_vertex{0};
    uint32_t index_count{0};
};

struct OwModel {
    std::vector<OwVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<OwSubmesh> submeshes;

    [[nodiscard]] bool valid() const { return !vertices.empty() && !indices.empty() && !submeshes.empty(); }
};

// Parses CSVM v1-v4. v4 vertices are read with their 40-byte stride, but a
// submesh flagged as skinned is rejected: this renderer draws the baked pose.
inline bool parseOwModel(const uint8_t* data, size_t size, OwModel& out) {
    out = OwModel{};
    if (!data || size < 20) return false;

    char magic[4];
    uint32_t version = 0, num_submeshes = 0, total_vertices = 0, total_indices = 0;
    std::memcpy(magic, data, 4);
    std::memcpy(&version, data + 4, 4);
    std::memcpy(&num_submeshes, data + 8, 4);
    std::memcpy(&total_vertices, data + 12, 4);
    std::memcpy(&total_indices, data + 16, 4);
    if (std::memcmp(magic, "CSVM", 4) != 0 || version < 1 || version > 4) return false;
    // Each submesh needs at least 4 (name length) + 16 (fixed fields) bytes.
    if (num_submeshes > size / 20) return false;

    size_t offset = 20;
    out.submeshes.reserve(num_submeshes);
    for (uint32_t i = 0; i < num_submeshes; ++i) {
        if (offset + 4 > size) return false;
        uint32_t name_len = 0;
        std::memcpy(&name_len, data + offset, 4);
        offset += 4;
        if (name_len > size || offset + name_len + 16 > size) return false;
        offset += name_len;

        OwSubmesh sub;
        uint32_t bone_index = 0;
        std::memcpy(&bone_index, data + offset, 4);
        offset += 4;
        std::memcpy(&sub.first_index, data + offset, 4);
        offset += 4;
        std::memcpy(&sub.base_vertex, data + offset, 4);
        offset += 4;
        std::memcpy(&sub.index_count, data + offset, 4);
        offset += 4;
        if (version >= 4) {
            if (offset + 4 > size) return false;
            uint32_t flags = 0;
            std::memcpy(&flags, data + offset, 4);
            offset += 4;
            if ((flags & 1u) != 0u) return false; // skinned: not supported by the rigid renderer
        }
        out.submeshes.push_back(sub);
    }

    // v4: compact bone table (name + 64-byte rest matrix). Skipped, but the
    // bytes must be consumed to reach the vertex buffer.
    if (version >= 4) {
        if (offset + 4 > size) return false;
        uint32_t num_bones = 0;
        std::memcpy(&num_bones, data + offset, 4);
        offset += 4;
        if (num_bones > 256) return false;
        for (uint32_t b = 0; b < num_bones; ++b) {
            if (offset + 4 > size) return false;
            uint32_t name_len = 0;
            std::memcpy(&name_len, data + offset, 4);
            offset += 4;
            if (name_len > size || offset + name_len + sizeof(fp::M4) > size) return false;
            offset += name_len + sizeof(fp::M4);
        }
    }

    const size_t stride = (version >= 4) ? 40 : 32;
    if (static_cast<size_t>(total_vertices) > size / stride) return false;
    const size_t vb_bytes = static_cast<size_t>(total_vertices) * stride;
    if (offset + vb_bytes > size) return false;
    out.vertices.resize(total_vertices);
    for (uint32_t v = 0; v < total_vertices; ++v) {
        const uint8_t* src = data + offset + static_cast<size_t>(v) * stride;
        std::memcpy(out.vertices[v].pos, src, 12);
        std::memcpy(out.vertices[v].normal, src + 12, 12);
        std::memcpy(out.vertices[v].uv, src + 24, 8);
    }
    offset += vb_bytes;

    const size_t ib_bytes = static_cast<size_t>(total_indices) * 4;
    if (offset + ib_bytes > size) return false;
    out.indices.resize(total_indices);
    if (ib_bytes > 0) std::memcpy(out.indices.data(), data + offset, ib_bytes);

    // Validate every submesh range before the renderer trusts it.
    for (const OwSubmesh& sub : out.submeshes) {
        if (sub.first_index > out.indices.size()) return false;
        if (sub.index_count > out.indices.size() - sub.first_index) return false;
        if (sub.base_vertex < 0) return false;
        if (sub.index_count == 0) continue;
        if (static_cast<size_t>(sub.base_vertex) >= out.vertices.size()) return false;
        size_t max_index = 0;
        for (uint32_t k = 0; k < sub.index_count; ++k) {
            max_index = std::max<size_t>(max_index, out.indices[sub.first_index + k]);
        }
        if (static_cast<size_t>(sub.base_vertex) + max_index >= out.vertices.size()) return false;
    }

    return out.valid();
}

} // namespace eldenring::ow
