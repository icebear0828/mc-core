#include "mc/static_mesh.hpp"

#include <cstring>

namespace mc::model {
namespace {

constexpr uint32_t kMaxVertices = 65535;      // indices are uint16_t
constexpr uint32_t kMaxIndices = 10'000'000;  // ~3.3M triangles, far above any baked creature
constexpr uint32_t kMaxMaterials = 64;
constexpr uint32_t kMaxStringBytes = 1024;

class Reader {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}

    bool u32(uint32_t& out) {
        if (offset_ + 4 > bytes_.size()) return false;
        const auto* p = reinterpret_cast<const unsigned char*>(bytes_.data() + offset_);
        out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
              (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        offset_ += 4;
        return true;
    }

    bool u16(uint16_t& out) {
        if (offset_ + 2 > bytes_.size()) return false;
        const auto* p = reinterpret_cast<const unsigned char*>(bytes_.data() + offset_);
        out = static_cast<uint16_t>(p[0] | (p[1] << 8));
        offset_ += 2;
        return true;
    }

    bool f32(float& out) {
        uint32_t bits = 0;
        if (!u32(bits)) return false;
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }

    bool str(std::string& out) {
        uint32_t len = 0;
        if (!u32(len) || len > kMaxStringBytes) return false;
        if (offset_ + len > bytes_.size()) return false;
        out.assign(bytes_.data() + offset_, len);
        offset_ += len;
        return true;
    }

    [[nodiscard]] size_t offset() const { return offset_; }

private:
    std::string_view bytes_;
    size_t offset_{0};
};

} // namespace

std::optional<StaticMesh> StaticMesh::fromBytes(std::string_view bytes) {
    Reader r(bytes);
    uint32_t magic = 0, version = 0, vertex_count = 0, index_count = 0, material_count = 0;
    if (!r.u32(magic) || magic != kMagic) return std::nullopt;
    if (!r.u32(version) || version != kVersion) return std::nullopt;
    if (!r.u32(vertex_count) || vertex_count == 0 || vertex_count > kMaxVertices) return std::nullopt;
    if (!r.u32(index_count) || index_count == 0 || index_count > kMaxIndices || index_count % 3 != 0) return std::nullopt;
    if (!r.u32(material_count) || material_count > kMaxMaterials) return std::nullopt;

    StaticMesh mesh;
    if (!r.str(mesh.identifier)) return std::nullopt;
    if (!r.f32(mesh.yaw_offset)) return std::nullopt;

    mesh.materials.reserve(material_count);
    for (uint32_t i = 0; i < material_count; ++i) {
        StaticMeshMaterial material;
        if (!r.str(material.name) || !r.str(material.texture) || !r.u32(material.first_index) ||
            !r.u32(material.index_count)) {
            return std::nullopt;
        }
        if (material.first_index > index_count || material.index_count > index_count - material.first_index) {
            return std::nullopt;
        }
        mesh.materials.push_back(std::move(material));
    }

    mesh.vertices.resize(vertex_count);
    for (auto& v : mesh.vertices) {
        if (!r.f32(v.x) || !r.f32(v.y) || !r.f32(v.z) || !r.f32(v.u) || !r.f32(v.v)) return std::nullopt;
    }

    mesh.indices.resize(index_count);
    for (auto& index : mesh.indices) {
        if (!r.u16(index)) return std::nullopt;
        if (index >= vertex_count) return std::nullopt;
    }
    return mesh;
}

} // namespace mc::model
