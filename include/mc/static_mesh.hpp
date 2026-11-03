#pragma once

// A baked, unskinned triangle mesh in model space: metres, Y up, feet at the origin, facing +Z.
// The Elden Ring adapter uploads it as one vertex/index buffer and draws it with the existing
// world-mob pipeline (mc::rig::RigVertex, one world matrix per draw). The binary is produced by
// tools/pokemon/build_static_mesh.py; see that script for the atlas/UV conventions.

#include "mc/rig.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mc::model {

struct StaticMeshMaterial {
    std::string name;    // source material name (for diagnostics)
    std::string texture; // atlas PNG file name, relative to the mesh file
    uint32_t first_index{0};
    uint32_t index_count{0};
};

struct StaticMesh {
    std::string identifier;
    float yaw_offset{0.f}; // radians added to the mob heading (corrects the exporter's facing)
    std::vector<mc::rig::RigVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<StaticMeshMaterial> materials;

    static constexpr uint32_t kMagic = 0x4D53434D; // "MCSM", little-endian
    static constexpr uint32_t kVersion = 1;

    // Parses an MCSM v1 file. Returns nullopt on a bad magic/version, truncation, an out-of-range
    // material range or an index that points past the vertex array.
    static std::optional<StaticMesh> fromBytes(std::string_view bytes);
};

} // namespace mc::model
