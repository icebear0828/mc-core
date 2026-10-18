#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "mc/rig.hpp"

namespace eldenring::cs {

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

class CSViewModel {
public:
    CSVMHeader header_{};
    std::vector<CSVMSubmesh> submeshes;
    std::vector<CSVMVertex> vertices;
    std::vector<uint32_t> indices;

    bool loadFromMemory(const uint8_t* data, size_t size) {
        if (!data || size < sizeof(CSVMHeader)) return false;

        std::memcpy(&header_, data, sizeof(CSVMHeader));
        if (std::memcmp(header_.magic, "CSVM", 4) != 0 || header_.version != 1) {
            return false;
        }

        size_t offset = sizeof(CSVMHeader);
        submeshes.clear();
        submeshes.reserve(header_.num_submeshes);

        for (uint32_t i = 0; i < header_.num_submeshes; ++i) {
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
            submeshes.push_back(std::move(sub));
        }

        const size_t vb_bytes = header_.total_vertices * sizeof(CSVMVertex);
        if (offset + vb_bytes > size) return false;
        vertices.resize(header_.total_vertices);
        std::memcpy(vertices.data(), data + offset, vb_bytes);
        offset += vb_bytes;

        const size_t ib_bytes = header_.total_indices * sizeof(uint32_t);
        if (offset + ib_bytes > size) return false;
        indices.resize(header_.total_indices);
        std::memcpy(indices.data(), data + offset, ib_bytes);

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
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float NdotH2 = NdotH * NdotH;
    const float denom = NdotH2 * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (kPi * denom * denom + 1e-7f);
}

inline float computeSmithG(float NdotV, float NdotL, float roughness) {
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    const float g1_v = NdotV / (NdotV * (1.0f - k) + k);
    const float g1_l = NdotL / (NdotL * (1.0f - k) + k);
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
    const float y = -((static_cast<float>(g) / 255.0f) * 2.0f - 1.0f); // DirectX green channel inversion
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

} // namespace eldenring::cs

