#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

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

} // namespace eldenring::cs
