#pragma once

// Overwatch (Doomfist) first-person gauntlet: viewmodel projection, the skinned
// CSVM parser and the clip player. Pure logic, no Windows headers, so it is
// unit-tested on every host. The D3D12 renderer that uploads this data lives in
// ow_renderer_d3d12.cpp and depends on nothing from the CS2 viewmodel
// subsystem.
//
// The shipped asset is a CSVM v4 (`tools/ow/build_ow_skinned.py`): section 0
// of the gauntlet mesh, a compact skin bone table and one named track per bone
// per clip. Every clip frame stores the full bind-pose -> viewmodel-space
// palette matrix, so the renderer only has to blend four bone matrices per
// vertex and never runs a skeleton of its own.

#include "eldenring_fp.hpp"
#include "mc/rig.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace eldenring::ow {

// Must match the HLSL palette size in ow_renderer_d3d12.cpp and MAX_BONES in
// tools/ow/build_ow_skinned.py.
inline constexpr uint32_t kOwMaxSkinBones = 64;

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
    // How much of the authored shoot clip's hand motion to keep (1 = the raw
    // clip, which throws the gauntlet 0.35 m towards the camera and 0.34 m up;
    // too floaty in first person). Blends each bone towards the idle pose.
    float shoot_motion{0.4f};
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

// Authored clips, in asset order. The names must match the CSVM clips written
// by tools/ow/build_ow_skinned.py; the state numbers must match the runtime
// context (`0` idle, `2` hand cannon, `3` rocket punch, `4` uppercut, `5` slam).
struct OwClipSpec {
    const char* name;
    uint8_t state;
};

inline constexpr OwClipSpec kOwClips[] = {
    {"idle", 0},
    {"shoot", 2},
    {"punch", 3},
    {"uppercut", 4},
    {"slam", 5},
};

inline const char* owClipNameForState(uint8_t state) {
    for (const OwClipSpec& spec : kOwClips) {
        if (spec.state == state) return spec.name;
    }
    return nullptr;
}

// Duration used when the asset is not loaded yet. Values are the authored
// frame counts measured to the last sample: (frame_count - 1) / fps.
inline float owClipDurationFallback(uint8_t state) {
    switch (state) {
        case 2: return 28.0f / 31.071428f;  // shoot: 29 frames @ 31.07 fps
        case 3: return 108.0f / 30.277779f; // punch: 109 frames @ 30.28 fps
        case 4: return 26.0f / 31.153847f;  // uppercut: 27 frames @ 31.15 fps
        case 5: return 60.0f / 30.5f;       // slam: 61 frames @ 30.5 fps
        default: return 0.0f;
    }
}

struct OwPose {
    fp::M4 base{fp::scale(1.0f)};
};

// Linear blend of two palette matrices. The clips are dense (30-40 fps), so
// interpolating the 16 entries is smooth and, unlike a TRS decompose, keeps
// the authored keyframes bit-exact (several bones carry non-uniform scale and
// the bind inverse introduces shear, which decompose/slerp cannot represent).
inline fp::M4 owLerpMatrix(const fp::M4& a, const fp::M4& b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    fp::M4 out;
    for (size_t i = 0; i < 16; ++i) out.m[i] = a.m[i] + (b.m[i] - a.m[i]) * t;
    return out;
}

// Rigid CSVM mesh. v1-v3 vertices are drawn as-is; v4 vertices carry four bone
// indices and four normalised uint8 weights and are skinned on the GPU.
struct OwVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    uint8_t joints[4];
    uint8_t weights[4];
};
static_assert(sizeof(OwVertex) == 40, "CSVM v4 vertex stride must stay 40 bytes");

struct OwSubmesh {
    uint32_t first_index{0};
    int32_t base_vertex{0};
    uint32_t index_count{0};
    uint32_t flags{0};

    [[nodiscard]] bool skinned() const { return (flags & 1u) != 0u; }
};

struct OwBone {
    std::string name;
    fp::M4 rest{};
};

struct OwAnimClip {
    std::string name;
    float fps{0.0f};
    uint32_t frame_count{0};
    // One track per model bone; empty when the clip does not animate that bone.
    std::vector<std::vector<fp::M4>> tracks;
};

struct OwModel {
    std::vector<OwVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<OwSubmesh> submeshes;
    std::vector<OwBone> bones;
    std::map<std::string, OwAnimClip> clips;

    [[nodiscard]] bool valid() const { return !vertices.empty() && !indices.empty() && !submeshes.empty(); }
    [[nodiscard]] bool skinned() const {
        return std::any_of(submeshes.begin(), submeshes.end(), [](const OwSubmesh& s) { return s.skinned(); });
    }
    [[nodiscard]] float clipDuration(const char* name) const {
        if (name == nullptr) return 0.0f;
        const auto it = clips.find(name);
        if (it == clips.end() || it->second.fps <= 0.0f || it->second.frame_count <= 1) return 0.0f;
        return static_cast<float>(it->second.frame_count - 1) / it->second.fps;
    }
};

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
            std::memcpy(&sub.flags, data + offset, 4);
            offset += 4;
        }
        out.submeshes.push_back(sub);
    }

    // v4: compact skin bone table (name + 64-byte rest matrix).
    if (version >= 4) {
        if (offset + 4 > size) return false;
        uint32_t num_bones = 0;
        std::memcpy(&num_bones, data + offset, 4);
        offset += 4;
        if (num_bones > kOwMaxSkinBones) return false;
        out.bones.reserve(num_bones);
        for (uint32_t b = 0; b < num_bones; ++b) {
            if (offset + 4 > size) return false;
            uint32_t name_len = 0;
            std::memcpy(&name_len, data + offset, 4);
            offset += 4;
            if (name_len > size || offset + name_len + sizeof(fp::M4) > size) return false;
            OwBone bone;
            bone.name.assign(reinterpret_cast<const char*>(data + offset), name_len);
            offset += name_len;
            std::memcpy(bone.rest.m.data(), data + offset, sizeof(fp::M4));
            offset += sizeof(fp::M4);
            out.bones.push_back(std::move(bone));
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
        if (version >= 4) {
            std::memcpy(out.vertices[v].joints, src + 32, 4);
            std::memcpy(out.vertices[v].weights, src + 36, 4);
        } else {
            std::memset(out.vertices[v].joints, 0, 4);
            std::memset(out.vertices[v].weights, 0, 4);
        }
    }
    offset += vb_bytes;

    const size_t ib_bytes = static_cast<size_t>(total_indices) * 4;
    if (offset + ib_bytes > size) return false;
    out.indices.resize(total_indices);
    if (ib_bytes > 0) std::memcpy(out.indices.data(), data + offset, ib_bytes);
    offset += ib_bytes;

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

    // Skinned submeshes require a real palette and non-zero weights: otherwise
    // the shader would read out of bounds or collapse the mesh to the origin.
    const bool has_skinned = out.skinned();
    if (has_skinned) {
        if (out.bones.empty() || out.bones.size() > kOwMaxSkinBones) return false;
        for (const OwSubmesh& sub : out.submeshes) {
            if (!sub.skinned()) continue;
            for (uint32_t k = 0; k < sub.index_count; ++k) {
                const size_t vi = static_cast<size_t>(sub.base_vertex) + out.indices[sub.first_index + k];
                const OwVertex& v = out.vertices[vi];
                uint32_t weight_sum = 0;
                for (int j = 0; j < 4; ++j) {
                    if (v.weights[j] == 0) continue;
                    if (v.joints[j] >= out.bones.size()) return false;
                    weight_sum += v.weights[j];
                }
                if (weight_sum == 0) return false;
            }
        }
    }

    // Version 3+: named per-bone tracks. Each track is `clip.frame_count`
    // matrices. Unknown track names are consumed and ignored.
    if (version >= 3 && offset + 4 <= size) {
        uint32_t num_clips = 0;
        std::memcpy(&num_clips, data + offset, 4);
        offset += 4;
        for (uint32_t c = 0; c < num_clips; ++c) {
            if (offset + 4 > size) return false;
            uint32_t c_name_len = 0;
            std::memcpy(&c_name_len, data + offset, 4);
            offset += 4;
            if (c_name_len > size || offset + c_name_len > size) return false;
            OwAnimClip clip;
            clip.name.assign(reinterpret_cast<const char*>(data + offset), c_name_len);
            offset += c_name_len;
            if (offset + sizeof(float) + 8 > size) return false;
            std::memcpy(&clip.fps, data + offset, sizeof(float));
            offset += sizeof(float);
            std::memcpy(&clip.frame_count, data + offset, 4);
            offset += 4;
            uint32_t num_tracks = 0;
            std::memcpy(&num_tracks, data + offset, 4);
            offset += 4;
            if (clip.frame_count > size / sizeof(fp::M4)) return false;
            clip.tracks.resize(out.bones.size());
            for (uint32_t t = 0; t < num_tracks; ++t) {
                if (offset + 4 > size) return false;
                uint32_t t_name_len = 0;
                std::memcpy(&t_name_len, data + offset, 4);
                offset += 4;
                if (t_name_len > size || offset + t_name_len > size) return false;
                const std::string track_name(reinterpret_cast<const char*>(data + offset), t_name_len);
                offset += t_name_len;
                const size_t track_bytes = static_cast<size_t>(clip.frame_count) * sizeof(fp::M4);
                if (offset + track_bytes > size) return false;
                auto bone_it = std::find_if(out.bones.begin(), out.bones.end(),
                                            [&track_name](const OwBone& b) { return b.name == track_name; });
                if (bone_it != out.bones.end()) {
                    const size_t bone_index = static_cast<size_t>(bone_it - out.bones.begin());
                    std::vector<fp::M4> frames(clip.frame_count);
                    if (track_bytes > 0) std::memcpy(frames.data(), data + offset, track_bytes);
                    clip.tracks[bone_index] = std::move(frames);
                }
                offset += track_bytes;
            }
            out.clips[clip.name] = std::move(clip);
        }
    }

    // A skinned model without any clip has no palette and cannot be drawn.
    if (has_skinned && out.clips.empty()) return false;
    return out.valid();
}

// Plays the authored clips. State 0 loops the idle clip; attack states play
// once and the dispatcher returns to state 0 when the clip duration elapses.
// A short crossfade hides the pose difference between the idle held pose and
// each attack clip's first frame.
class OwAnimator {
public:
    void setModel(const OwModel* model) {
        model_ = model;
        palette_.clear();
        blend_from_.clear();
        last_state_ = 0;
        last_anim_time_ = 0.0f;
        blend_left_ = 0.0f;
    }

    void tick(float dt, uint8_t state, float anim_time, uint8_t variant = 0) {
        (void)variant;
        const uint8_t previous_state = last_state_;
        last_state_ = state;
        current_state_ = state;
        anim_time_ = anim_time;
        float effective_time = anim_time;

        const float bob = (state == 0) ? std::sin(anim_time * 2.5f) * 0.003f : 0.0f;
        pose_.base = fp::translate(0.0f, bob, 0.0f);

        // Q (state 5) is the ultimate presentation: the gauntlet fades out
        // during the leap, then fades back in with an impact scale bump when
        // the slam lands. The top-down camera itself needs game-camera control.
        fade_ = 1.0f;
        scale_pulse_ = 1.0f;
        if (state == 5) {
            const float t = anim_time;
            if (t < 0.15f) fade_ = 1.0f;
            else if (t < 0.35f) fade_ = 1.0f - (t - 0.15f) / 0.20f;
            else if (t < 1.42f) fade_ = 0.0f;
            else if (t < 1.56f) fade_ = (t - 1.42f) / 0.14f;
            fade_ = std::clamp(fade_, 0.0f, 1.0f);
            const float d = (t - 1.50f) / 0.10f;
            scale_pulse_ = 1.0f + 0.35f * std::exp(-d * d);
        }

        std::vector<fp::M4> target;
        if (model_ != nullptr && !model_->bones.empty()) {
            target.assign(model_->bones.size(), fp::M4{});
            const char* name = owClipNameForState(state);
            auto clip_it = (name != nullptr) ? model_->clips.find(name) : model_->clips.end();
            if (clip_it == model_->clips.end()) clip_it = model_->clips.find("idle");
            if (clip_it != model_->clips.end()) {
                const OwAnimClip& clip = clip_it->second;
                float time = anim_time;
                if (state == 0 && clip.fps > 0.0f && clip.frame_count > 1) {
                    time = std::fmod(anim_time, static_cast<float>(clip.frame_count) / clip.fps);
                }
                effective_time = time;
                for (size_t b = 0; b < target.size(); ++b) {
                    if (b >= clip.tracks.size() || clip.tracks[b].empty()) continue;
                    const std::vector<fp::M4>& track = clip.tracks[b];
                    const float frame_float = std::clamp(time * clip.fps, 0.0f, static_cast<float>(track.size() - 1));
                    const size_t f0 = static_cast<size_t>(frame_float);
                    if (f0 + 1 >= track.size()) {
                        target[b] = track.back();
                    } else {
                        target[b] = owLerpMatrix(track[f0], track[f0 + 1], frame_float - static_cast<float>(f0));
                    }
                }
            }
        }

        if (target.empty()) {
            palette_.clear();
            blend_from_.clear();
            blend_left_ = 0.0f;
            return;
        }

        // Crossfade on state changes and on deliberate time jumps (releasing a
        // charged punch seeks from the windup to the release frame). Compare
        // the sampled time so the idle loop wrap is not mistaken for a seek.
        const float time_delta = effective_time - last_anim_time_;
        last_anim_time_ = effective_time;
        const bool seeked = state == previous_state && std::fabs(time_delta) > 0.15f;
        if ((state != previous_state || seeked) && !palette_.empty() && palette_.size() == target.size()) {
            blend_from_ = palette_;
            blend_left_ = kBlendSeconds;
        } else if (palette_.size() != target.size()) {
            blend_from_.clear();
            blend_left_ = 0.0f;
        }

        if (blend_left_ > 0.0f && blend_from_.size() == target.size()) {
            const float alpha = std::clamp(1.0f - blend_left_ / kBlendSeconds, 0.0f, 1.0f);
            const float smooth = alpha * alpha * (3.0f - 2.0f * alpha);
            for (size_t b = 0; b < target.size(); ++b) {
                target[b] = owLerpMatrix(blend_from_[b], target[b], smooth);
            }
            blend_left_ = std::max(0.0f, blend_left_ - std::max(0.0f, dt));
        } else {
            blend_from_.clear();
        }

        palette_ = std::move(target);
    }

    [[nodiscard]] const OwPose& pose() const { return pose_; }
    [[nodiscard]] const std::vector<fp::M4>& palette() const { return palette_; }
    [[nodiscard]] bool hasAnimation() const { return !palette_.empty(); }
    [[nodiscard]] float fade() const { return fade_; }
    [[nodiscard]] float scalePulse() const { return scale_pulse_; }
    [[nodiscard]] uint8_t currentState() const { return current_state_; }
    [[nodiscard]] float currentAnimTime() const { return anim_time_; }
    [[nodiscard]] float clipDuration(uint8_t state) const {
        if (model_ == nullptr) return owClipDurationFallback(state);
        const char* name = owClipNameForState(state);
        if (name == nullptr) return 0.0f;
        const float duration = model_->clipDuration(name);
        return duration > 0.0f ? duration : owClipDurationFallback(state);
    }

private:
    static constexpr float kBlendSeconds = 0.08f;

    const OwModel* model_{nullptr};
    OwPose pose_{};
    std::vector<fp::M4> palette_{};
    std::vector<fp::M4> blend_from_{};
    float blend_left_{0.0f};
    uint8_t current_state_{0};
    uint8_t last_state_{0};
    float anim_time_{0.0f};
    float last_anim_time_{0.0f};
    float fade_{1.0f};
    float scale_pulse_{1.0f};
};

} // namespace eldenring::ow
