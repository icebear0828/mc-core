#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>
#include "cs_animation_tracks.hpp"
#include "eldenring_fp.hpp"
#include "mc/rig.hpp"

namespace eldenring::cs {

enum class SubmeshType {
    Blade,
    HandleFront,
    HandleRear,
    Lock,
    Arms,     // gloved hand
    BareArm,  // bare forearm / upper-arm sleeve (skin material)
    Sleeve,   // agent uniform sleeve (fabric material)
    Base
};

#pragma pack(push, 1)
struct CSVMHeader {
    char magic[4];           // "CSVM"
    uint32_t version;        // 1
    uint32_t num_submeshes;
    uint32_t total_vertices;
    uint32_t total_indices;
};

struct CSVMVertex {
    float pos[3];
    float normal[3];
    float uv[2];
};
#pragma pack(pop)

struct CSVMSubmesh {
    std::string bone_name;
    uint32_t bone_index{0};
    uint32_t first_index{0};
    int32_t base_vertex{0};
    uint32_t index_count{0};
};

struct CSVMAnimClip {
    std::string name;
    float fps{30.0f};
    uint32_t frame_count{0};

    // CSVM v3: named animation tracks. A weapon stores one track per animated
    // part ("weapon_offset", "clip", "bolt", "trigger", "hand_L", "hand_R"...)
    // so adding a gun needs no C++ change. CSVM v2 knife clips also populate
    // this map with their five legacy part names.
    std::map<std::string, std::vector<eldenring::fp::M4>> tracks;

    // Legacy v2 knife slots, kept for backward compatibility and existing tests.
    std::vector<eldenring::fp::M4> blade_frames;
    std::vector<eldenring::fp::M4> front_frames;
    std::vector<eldenring::fp::M4> rear_frames;
    std::vector<eldenring::fp::M4> lock_frames;
    std::vector<eldenring::fp::M4> arms_frames;
};

class CSViewModel {
public:
    CSVMHeader header_{};
    std::vector<CSVMSubmesh> submeshes;
    std::vector<CSVMVertex> vertices;
    std::vector<uint32_t> indices;
    std::map<std::string, CSVMAnimClip> anim_clips;

    [[nodiscard]] const CSVMHeader& header() const { return header_; }

    bool loadFromMemory(const uint8_t* data, size_t size) {
        if (!data || size < sizeof(CSVMHeader)) return false;

        // Parse into a scratch model: a rejected file must never leave the
        // live model half-overwritten while the old GPU buffers stay bound.
        CSViewModel parsed;
        std::memcpy(&parsed.header_, data, sizeof(CSVMHeader));
        if (std::memcmp(parsed.header_.magic, "CSVM", 4) != 0 ||
            (parsed.header_.version != 1 && parsed.header_.version != 2 && parsed.header_.version != 3)) {
            return false;
        }

        size_t offset = sizeof(CSVMHeader);
        parsed.submeshes.clear();
        parsed.submeshes.reserve(parsed.header_.num_submeshes);

        for (uint32_t i = 0; i < parsed.header_.num_submeshes; ++i) {
            if (offset + sizeof(uint32_t) > size) return false;
            uint32_t name_len = 0;
            std::memcpy(&name_len, data + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            if (offset + name_len > size) return false;
            std::string name(reinterpret_cast<const char*>(data + offset), name_len);
            offset += name_len;

            if (offset + 16 > size) return false; // uint32(bone_idx), uint32(first_idx), int32(base_v), uint32(count)
            CSVMSubmesh sub;
            sub.bone_name = std::move(name);
            std::memcpy(&sub.bone_index, data + offset, sizeof(uint32_t)); offset += sizeof(uint32_t);
            std::memcpy(&sub.first_index, data + offset, sizeof(uint32_t)); offset += sizeof(uint32_t);
            std::memcpy(&sub.base_vertex, data + offset, sizeof(int32_t)); offset += sizeof(int32_t);
            std::memcpy(&sub.index_count, data + offset, sizeof(uint32_t)); offset += sizeof(uint32_t);
            parsed.submeshes.push_back(std::move(sub));
        }

        const size_t vb_bytes = parsed.header_.total_vertices * sizeof(CSVMVertex);
        if (offset + vb_bytes > size) return false;
        parsed.vertices.resize(parsed.header_.total_vertices);
        std::memcpy(parsed.vertices.data(), data + offset, vb_bytes);
        offset += vb_bytes;

        const size_t ib_bytes = parsed.header_.total_indices * sizeof(uint32_t);
        if (offset + ib_bytes > size) return false;
        parsed.indices.resize(parsed.header_.total_indices);
        std::memcpy(parsed.indices.data(), data + offset, ib_bytes);
        offset += ib_bytes;

        // Validate every submesh range before the renderer trusts it. A bad
        // CSVM must fail the load, never reach DrawIndexedInstanced.
        for (const auto& sub : parsed.submeshes) {
            if (sub.first_index > parsed.indices.size()) return false;
            if (sub.index_count > parsed.indices.size() - sub.first_index) return false;
            if (sub.base_vertex < 0) return false;
            if (sub.index_count == 0) continue;
            if (static_cast<size_t>(sub.base_vertex) >= parsed.vertices.size()) return false;
            size_t max_index = 0;
            for (uint32_t k = 0; k < sub.index_count; ++k) {
                max_index = std::max<size_t>(max_index, parsed.indices[sub.first_index + k]);
            }
            if (static_cast<size_t>(sub.base_vertex) + max_index >= parsed.vertices.size()) return false;
        }

        // Version 2: Read animation clips
        parsed.anim_clips.clear();
        if (parsed.header_.version >= 2 && offset + sizeof(uint32_t) <= size) {
            uint32_t num_clips = 0;
            std::memcpy(&num_clips, data + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            for (uint32_t c = 0; c < num_clips; ++c) {
                if (offset + sizeof(uint32_t) > size) return false;
                uint32_t c_name_len = 0;
                std::memcpy(&c_name_len, data + offset, sizeof(uint32_t));
                offset += sizeof(uint32_t);

                if (offset + c_name_len > size) return false;
                std::string clip_name(reinterpret_cast<const char*>(data + offset), c_name_len);
                offset += c_name_len;

                if (offset + sizeof(float) + sizeof(uint32_t) > size) return false;
                CSVMAnimClip clip;
                clip.name = clip_name;
                std::memcpy(&clip.fps, data + offset, sizeof(float)); offset += sizeof(float);
                std::memcpy(&clip.frame_count, data + offset, sizeof(uint32_t)); offset += sizeof(uint32_t);

                const size_t m4_size = sizeof(eldenring::fp::M4);

                if (parsed.header_.version >= 3) {
                    // CSVM v3: variable named tracks.
                    uint32_t num_tracks = 0;
                    if (offset + sizeof(uint32_t) > size) return false;
                    std::memcpy(&num_tracks, data + offset, sizeof(uint32_t)); offset += sizeof(uint32_t);

                    for (uint32_t t = 0; t < num_tracks; ++t) {
                        if (offset + sizeof(uint32_t) > size) return false;
                        uint32_t t_name_len = 0;
                        std::memcpy(&t_name_len, data + offset, sizeof(uint32_t)); offset += sizeof(uint32_t);
                        if (offset + t_name_len > size) return false;
                        std::string track_name(reinterpret_cast<const char*>(data + offset), t_name_len);
                        offset += t_name_len;

                        const size_t track_bytes = static_cast<size_t>(clip.frame_count) * m4_size;
                        if (offset + track_bytes > size) return false;
                        std::vector<eldenring::fp::M4> frames(clip.frame_count);
                        if (track_bytes > 0) {
                            std::memcpy(frames.data(), data + offset, track_bytes);
                            offset += track_bytes;
                        }
                        clip.tracks[track_name] = std::move(frames);
                    }
                } else {
                    // CSVM v2: fixed five knife part tracks.
                    const size_t frame_bytes = 5 * m4_size;
                    if (offset + clip.frame_count * frame_bytes > size) return false;

                    clip.blade_frames.resize(clip.frame_count);
                    clip.front_frames.resize(clip.frame_count);
                    clip.rear_frames.resize(clip.frame_count);
                    clip.lock_frames.resize(clip.frame_count);
                    clip.arms_frames.resize(clip.frame_count);

                    for (uint32_t f = 0; f < clip.frame_count; ++f) {
                        std::memcpy(&clip.blade_frames[f], data + offset, m4_size); offset += m4_size;
                        std::memcpy(&clip.front_frames[f], data + offset, m4_size); offset += m4_size;
                        std::memcpy(&clip.rear_frames[f], data + offset, m4_size); offset += m4_size;
                        std::memcpy(&clip.lock_frames[f], data + offset, m4_size); offset += m4_size;
                        std::memcpy(&clip.arms_frames[f], data + offset, m4_size); offset += m4_size;
                    }

                    clip.tracks["blade"] = clip.blade_frames;
                    clip.tracks["front"] = clip.front_frames;
                    clip.tracks["rear"] = clip.rear_frames;
                    clip.tracks["lock"] = clip.lock_frames;
                    clip.tracks["arms"] = clip.arms_frames;
                }
                parsed.anim_clips[clip_name] = std::move(clip);
            }
        }

        *this = std::move(parsed);
        return true;
    }

    bool loadFromFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;
        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;
        return loadFromMemory(buffer.data(), buffer.size());
    }
};

// -----------------------------------------------------------------------------
// Source 2 / CS2 Independent Viewmodel Projection Matrix
// Decoupled from the host game's main camera FOV (Source 2 standard 68 degrees)
// -----------------------------------------------------------------------------
struct ViewmodelProjectionConfig {
    float fov_deg{68.0f};           // Official CS2 viewmodel FOV (68 deg)
    float offset_x{0.0f};           // viewmodel_offset_x
    float offset_y{0.0f};           // viewmodel_offset_y
    float offset_z{0.0f};           // viewmodel_offset_z
    float z_near{0.01f};            // Near clip tuned for viewmodels
    float z_far{20.0f};             // Far clip tuned for viewmodels
    bool use_game_projection{false};// Decoupled by default
};

inline mc::rig::Mat4 computeViewmodelProjection(unsigned width, unsigned height, const ViewmodelProjectionConfig& cfg = {}) {
    const float aspect = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : (16.0f / 9.0f);
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kDeg = kPi / 180.0f;
    mc::rig::Mat4 proj = mc::rig::perspectiveLH(cfg.fov_deg * kDeg, aspect, cfg.z_near, cfg.z_far);
    if (cfg.offset_x != 0.0f || cfg.offset_y != 0.0f || cfg.offset_z != 0.0f) {
        mc::rig::Mat4 offset_mat = mc::rig::translation(mc::Vec3{cfg.offset_x, cfg.offset_y, cfg.offset_z});
        proj = offset_mat * proj;
    }
    return proj;
}

// -----------------------------------------------------------------------------
// Cook-Torrance PBR BRDF & Normal Map Math Helpers
// -----------------------------------------------------------------------------
inline float computeGgxD(float NdotH, float roughness) {
    constexpr float kPi = 3.14159265358979323846f;
    const float cl_ndoth = std::clamp(NdotH, 0.0f, 1.0f);
    const float cl_rough = std::clamp(roughness, 0.05f, 0.98f);
    const float alpha = cl_rough * cl_rough;
    const float alpha2 = alpha * alpha;
    const float NdotH2 = cl_ndoth * cl_ndoth;
    const float denom = NdotH2 * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (kPi * denom * denom + 1e-7f);
}

inline float computeSmithG(float NdotV, float NdotL, float roughness) {
    const float cl_ndotv = std::clamp(NdotV, 0.0f, 1.0f);
    const float cl_ndotl = std::clamp(NdotL, 0.0f, 1.0f);
    if (cl_ndotv <= 1e-5f || cl_ndotl <= 1e-5f) return 0.0f;
    const float cl_rough = std::clamp(roughness, 0.05f, 0.98f);
    const float r = cl_rough + 1.0f;
    const float k = (r * r) / 8.0f;
    const float g1_v = cl_ndotv / (cl_ndotv * (1.0f - k) + k);
    const float g1_l = cl_ndotl / (cl_ndotl * (1.0f - k) + k);
    return g1_v * g1_l;
}

inline void computeFresnelSchlick(float cos_theta, const float f0[3], float out_f[3]) {
    const float clamped_cos = std::clamp(cos_theta, 0.0f, 1.0f);
    const float p = std::pow(1.0f - clamped_cos, 5.0f);
    for (int i = 0; i < 3; ++i) {
        out_f[i] = f0[i] + (1.0f - f0[i]) * p;
    }
}

inline void unpackDirectXNormal(uint8_t r, uint8_t g, uint8_t b, float out_n[3]) {
    const float x = (static_cast<float>(r) / 255.0f) * 2.0f - 1.0f;
    const float y = -((static_cast<float>(g) / 255.0f) * 2.0f - 1.0f); // DirectX green channel inversion (V down -> -Y in 3D)
    float z = (static_cast<float>(b) / 255.0f) * 2.0f - 1.0f;
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len > 1e-6f) {
        out_n[0] = x / len;
        out_n[1] = y / len;
        out_n[2] = z / len;
    } else {
        out_n[0] = 0.0f;
        out_n[1] = 0.0f;
        out_n[2] = 1.0f;
    }
}

inline void enhanceGloveTextureAlbedo(const float in_rgb[3], float out_rgb[3]) {
    constexpr float kBaseline = 77.0f / 255.0f;
    for (int i = 0; i < 3; ++i) {
        const float val = kBaseline + (in_rgb[i] - kBaseline) * 2.2f;
        out_rgb[i] = std::clamp(val * 1.3f, 0.0f, 1.0f);
    }
}

inline void computeCookTorranceBrdf(float NdotV, float NdotL, float NdotH, float VdotH,
                                    float roughness, float metallic,
                                    const float albedo[3], float out_radiance[3]) {
    constexpr float kPi = 3.14159265358979323846f;
    const float cl_NdotV = std::clamp(NdotV, 0.0f, 1.0f);
    const float cl_NdotL = std::clamp(NdotL, 0.0f, 1.0f);
    const float cl_NdotH = std::clamp(NdotH, 0.0f, 1.0f);
    const float cl_VdotH = std::clamp(VdotH, 0.0f, 1.0f);
    const float cl_rough = std::clamp(roughness, 0.05f, 0.98f);
    const float cl_metal = std::clamp(metallic, 0.0f, 1.0f);

    if (cl_NdotV <= 1e-5f || cl_NdotL <= 1e-5f) {
        out_radiance[0] = out_radiance[1] = out_radiance[2] = 0.0f;
        return;
    }

    const float D = computeGgxD(cl_NdotH, cl_rough);
    const float G = computeSmithG(cl_NdotV, cl_NdotL, cl_rough);

    float f0[3] = {0.04f, 0.04f, 0.04f};
    for (int i = 0; i < 3; ++i) {
        f0[i] = f0[i] + (albedo[i] - f0[i]) * cl_metal;
    }

    float F[3]{};
    computeFresnelSchlick(cl_VdotH, f0, F);

    const float denom = 4.0f * cl_NdotV * cl_NdotL + 1e-4f;
    for (int i = 0; i < 3; ++i) {
        const float spec = (D * G * F[i]) / denom;
        const float kD = (1.0f - F[i]) * (1.0f - cl_metal);
        const float diff = (kD * albedo[i]) / kPi;
        out_radiance[i] = (diff + spec) * cl_NdotL;
    }
}

inline bool packOrmTexture(const uint8_t* ao_rgba, unsigned ao_w, unsigned ao_h,
                           const uint8_t* rough_rgba, unsigned rough_w, unsigned rough_h,
                           uint8_t default_metal,
                           std::vector<uint8_t>& out_orm, unsigned& out_w, unsigned& out_h) {
    out_w = std::max(ao_w, rough_w);
    out_h = std::max(ao_h, rough_h);
    if (out_w == 0 || out_h == 0) {
        out_w = 16;
        out_h = 16;
        out_orm.assign(out_w * out_h * 4, 0);
        for (size_t p = 0; p < out_orm.size(); p += 4) {
            out_orm[p] = 255;            // AO
            out_orm[p + 1] = 128;        // Roughness
            out_orm[p + 2] = default_metal;
            out_orm[p + 3] = 255;
        }
        return true;
    }

    out_orm.resize(static_cast<size_t>(out_w) * out_h * 4);
    for (unsigned y = 0; y < out_h; ++y) {
        for (unsigned x = 0; x < out_w; ++x) {
            const size_t dst_idx = (static_cast<size_t>(y) * out_w + x) * 4;

            // Sample AO
            uint8_t ao_val = 255;
            if (ao_rgba && ao_w > 0 && ao_h > 0) {
                const unsigned sx = (x * ao_w) / out_w;
                const unsigned sy = (y * ao_h) / out_h;
                ao_val = ao_rgba[(sy * ao_w + sx) * 4];
            }

            // Sample Roughness
            uint8_t rough_val = 128;
            if (rough_rgba && rough_w > 0 && rough_h > 0) {
                const unsigned sx = (x * rough_w) / out_w;
                const unsigned sy = (y * rough_h) / out_h;
                rough_val = rough_rgba[(sy * rough_w + sx) * 4];
            }

            out_orm[dst_idx + 0] = ao_val;
            out_orm[dst_idx + 1] = rough_val;
            out_orm[dst_idx + 2] = default_metal;
            out_orm[dst_idx + 3] = 255;
        }
    }
    return true;
}

// In CS2 Source 2, weapon textures use 32-bit RGBA where alpha channel is wear/paint mask (NOT transparency).
// Default/vanilla knife has alpha == 0.0 across 90%+ of the texture.
// The pipeline must NEVER discard or replace texture RGB with solid tint based on alpha < 0.05.
inline void resolveAlbedoColor(const float tex_sample[4], const float tint[4], bool is_arm, float out_albedo[4]) {
    (void)is_arm;
    (void)tint;
    out_albedo[0] = tex_sample[0];
    out_albedo[1] = tex_sample[1];
    out_albedo[2] = tex_sample[2];
    out_albedo[3] = 1.0f; // Viewmodels are 100% opaque
}

// Resolves texture candidate paths ensuring absolute game_dir takes priority and no duplicated folder nesting occurs
inline std::vector<std::string> resolveTextureCandidatePaths(const std::string& game_dir,
                                                             const std::string& base_or_rel_path,
                                                             const std::string& filename) {
    std::vector<std::string> candidates;
    std::filesystem::path bp(base_or_rel_path);
    std::filesystem::path parent = bp.parent_path();

    if (!parent.empty()) {
        std::filesystem::path full = parent / filename;
        if (!game_dir.empty()) {
            std::filesystem::path g_full = std::filesystem::path(game_dir) / full;
            candidates.push_back(g_full.string());
        }
        candidates.push_back(full.string());
    }

    if (!game_dir.empty()) {
        candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / filename).string());
        candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / filename).string());
    }
    candidates.push_back((std::filesystem::path("mods") / "mc_adapter" / "textures" / filename).string());
    candidates.push_back((std::filesystem::path("mods") / "mc_adapter" / filename).string());
    candidates.push_back(filename);

    return candidates;
}

// Determines effective submesh tint. When textures are loaded, returns neutral 1.0f so textures are not suppressed.
inline void getEffectiveSubmeshTint(SubmeshType type, bool has_texture, float out_tint[4]) {
    if (has_texture) {
        out_tint[0] = 1.0f;
        out_tint[1] = 1.0f;
        out_tint[2] = 1.0f;
        out_tint[3] = 1.0f;
        return;
    }
    switch (type) {
        case SubmeshType::Blade:
            out_tint[0] = 0.95f; out_tint[1] = 0.95f; out_tint[2] = 0.98f; out_tint[3] = 1.0f;
            break;
        case SubmeshType::HandleFront:
        case SubmeshType::HandleRear:
        case SubmeshType::Base:
            out_tint[0] = 0.25f; out_tint[1] = 0.25f; out_tint[2] = 0.28f; out_tint[3] = 1.0f;
            break;
        case SubmeshType::Lock:
            out_tint[0] = 0.85f; out_tint[1] = 0.85f; out_tint[2] = 0.88f; out_tint[3] = 1.0f;
            break;
        case SubmeshType::Arms:
            out_tint[0] = 0.20f; out_tint[1] = 0.22f; out_tint[2] = 0.24f; out_tint[3] = 1.0f;
            break;
        case SubmeshType::BareArm:
            out_tint[0] = 0.72f; out_tint[1] = 0.58f; out_tint[2] = 0.48f; out_tint[3] = 1.0f;
            break;
        case SubmeshType::Sleeve:
            out_tint[0] = 0.30f; out_tint[1] = 0.32f; out_tint[2] = 0.30f; out_tint[3] = 1.0f;
            break;
    }
}

// -----------------------------------------------------------------------------
// CS2 Viewmodel State Machine & Animation Evaluation
// -----------------------------------------------------------------------------
struct CSViewModelPose {
    eldenring::fp::M4 blade_pose{eldenring::fp::scale(1.0f)};
    eldenring::fp::M4 handle_front_pose{eldenring::fp::scale(1.0f)};
    eldenring::fp::M4 handle_rear_pose{eldenring::fp::scale(1.0f)};
    eldenring::fp::M4 lock_pose{eldenring::fp::scale(1.0f)};
    eldenring::fp::M4 arms_pose{eldenring::fp::scale(1.0f)};
    eldenring::fp::M4 base_pose{eldenring::fp::scale(1.0f)};

    // CSVM v3: sampled poses keyed by part/track name. A submesh looks up its
    // own bone name here, so gun parts need no hard-coded pose slot.
    std::map<std::string, eldenring::fp::M4> tracks;

    [[nodiscard]] const eldenring::fp::M4* findTrack(const std::string& name) const {
        const auto it = tracks.find(name);
        return (it != tracks.end()) ? &it->second : nullptr;
    }
};

class CSViewModelAnimator {
public:
    void setModel(const CSViewModel* model) {
        model_ = model;
    }

    void tick(float dt, uint8_t state, float anim_time, uint8_t variant = 0, bool hit = true) {
        (void)dt;
        current_state_ = state;
        anim_time_ = anim_time;
        clip_duration_ = 0.f;

        const float bob = std::sin(anim_time * 2.5f) * 0.003f;
        const eldenring::fp::M4 base = eldenring::fp::translate(0.f, bob, 0.f);

        // Reset legacy slots so a gun clip never inherits stale knife poses.
        pose_.blade_pose = base;
        pose_.handle_front_pose = base;
        pose_.handle_rear_pose = base;
        pose_.lock_pose = base;
        pose_.arms_pose = base;
        pose_.base_pose = base;
        pose_.tracks.clear();

        auto findClip = [this](std::initializer_list<const char*> names) -> const CSVMAnimClip* {
            if (!model_) return nullptr;
            for (const char* name : names) {
                const auto it = model_->anim_clips.find(name);
                if (it != model_->anim_clips.end() && !it->second.tracks.empty()) {
                    return &it->second;
                }
            }
            return nullptr;
        };

        // Authentic CS2 state -> clip mapping. Variants alternate the two/three
        // authored takes CS2 ships for the same action; `hit` selects the
        // hit/miss take for melee attacks.
        const bool alt = (variant & 1u) != 0u;
        const CSVMAnimClip* clip = nullptr;
        switch (state) {
            case 1: { // Inspect: lookat01/02/03
                const uint8_t take = static_cast<uint8_t>(variant % 3u);
                clip = (take == 2) ? findClip({"inspect3", "inspect"})
                     : (take == 1) ? findClip({"inspect2", "inspect"})
                                   : findClip({"inspect"});
                break;
            }
            case 2: // Primary attack: gun fire, or the knife light hit/miss take
                if (hit) {
                    clip = alt ? findClip({"light_hit2", "light_hit", "fire"})
                               : findClip({"light_hit", "fire"});
                } else {
                    clip = alt ? findClip({"light_miss2", "light_miss", "light_hit"})
                               : findClip({"light_miss", "light_hit"});
                }
                break;
            case 3: // Secondary attack: knife heavy hit/miss (guns have no clip)
                clip = hit ? findClip({"heavy_hit", "backstab"})
                           : findClip({"heavy_miss", "heavy_hit"});
                break;
            case 4: // Deploy/draw
                clip = alt ? findClip({"deploy2", "deploy", "draw"})
                           : findClip({"deploy", "draw"});
                break;
            case 5: // Reload (guns only)
                clip = findClip({"reload"});
                break;
            case 6: // Backstab (contextual)
                clip = alt ? findClip({"backstab_light2", "backstab_light", "backstab"})
                           : findClip({"backstab_light", "backstab"});
                break;
            case 0:
            default: // Idle/held pose
                clip = findClip({"idle"});
                break;
        }

        if (clip && !clip->tracks.empty()) {
            clip_duration_ = (clip->frame_count > 1 && clip->fps > 0.f)
                ? static_cast<float>(clip->frame_count - 1) / clip->fps
                : 0.f;
            // Idle is a looping take; one-shot actions clamp on their last frame.
            float clip_time = anim_time;
            if (state == 0 && clip_duration_ > 0.f) {
                clip_time = std::fmod(anim_time, clip_duration_);
            }
            for (const auto& entry : clip->tracks) {
                if (entry.second.empty()) continue;
                eldenring::fp::M4 sampled = eldenring::fp::M4{};
                erov::cs_anims::SampleDynamicTrack(entry.second, clip->fps, clip_time, sampled);
                pose_.tracks[entry.first] = eldenring::fp::mul(base, sampled);
            }

            // Mirror generic tracks onto the legacy knife accessors.
            auto mirror = [this](const char* track, eldenring::fp::M4& dst) {
                const auto it = pose_.tracks.find(track);
                if (it != pose_.tracks.end()) dst = it->second;
            };
            mirror("blade", pose_.blade_pose);
            mirror("front", pose_.handle_front_pose);
            mirror("rear", pose_.handle_rear_pose);
            mirror("lock", pose_.lock_pose);
            mirror("arms", pose_.arms_pose);
            mirror("hand_R", pose_.arms_pose);

            const auto body = pose_.tracks.find("weapon_offset");
            pose_.base_pose = (body != pose_.tracks.end()) ? body->second : pose_.arms_pose;
            return;
        }

        if (state == 2) { // Knife light attack: procedural forward slash
            const float p = std::clamp(anim_time / 0.35f, 0.f, 1.f);
            const float slash_rot = -30.f + p * 60.f;
            const float forward = -std::sin(p * 3.14159f) * 0.08f;
            eldenring::fp::M4 slash_base = eldenring::fp::mul(base, eldenring::fp::translate(0.f, 0.f, forward));
            slash_base = eldenring::fp::mul(slash_base, eldenring::fp::rotZ(slash_rot));
            pose_.blade_pose = slash_base;
            pose_.handle_front_pose = slash_base;
            pose_.handle_rear_pose = slash_base;
            pose_.lock_pose = slash_base;
            pose_.base_pose = slash_base;
            pose_.arms_pose = slash_base;
        }
        // Idle / any state without an authored clip keeps the resting stance.
    }

    [[nodiscard]] const CSViewModelPose& currentPose() const { return pose_; }
    [[nodiscard]] const eldenring::fp::M4& bladePose() const { return pose_.blade_pose; }
    [[nodiscard]] const eldenring::fp::M4& handleFrontPose() const { return pose_.handle_front_pose; }
    [[nodiscard]] const eldenring::fp::M4& handleRearPose() const { return pose_.handle_rear_pose; }
    [[nodiscard]] const eldenring::fp::M4& lockPose() const { return pose_.lock_pose; }
    [[nodiscard]] const eldenring::fp::M4& armsPose() const { return pose_.arms_pose; }
    [[nodiscard]] const eldenring::fp::M4& basePose() const { return pose_.base_pose; }
    [[nodiscard]] uint8_t currentState() const { return current_state_; }
    [[nodiscard]] float currentAnimTime() const { return anim_time_; }
    [[nodiscard]] float currentClipDuration() const { return clip_duration_; }

    // Duration of a clip in the loaded model, in seconds, measured to the last
    // authored sample. Returns 0 when the clip is absent.
    [[nodiscard]] float clipDuration(const char* name) const {
        if (!model_ || name == nullptr) return 0.f;
        const auto it = model_->anim_clips.find(name);
        if (it == model_->anim_clips.end() || it->second.fps <= 0.f) return 0.f;
        if (it->second.frame_count <= 1) return 0.f;
        return static_cast<float>(it->second.frame_count - 1) / it->second.fps;
    }

private:
    const CSViewModel* model_{nullptr};
    CSViewModelPose pose_{};
    uint8_t current_state_{0};
    float anim_time_{0.0f};
    float clip_duration_{0.0f};
};

} // namespace eldenring::cs


