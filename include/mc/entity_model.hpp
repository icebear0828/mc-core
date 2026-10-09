#pragma once

#include "mc/rig.hpp"
#include "mc/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mc::model {

// A single cuboid element of a bone in a Minecraft entity model
struct Cube {
    Vec3 origin{0.0f, 0.0f, 0.0f}; // Minimum corner [x, y, z] in model pixels
    Vec3 size{0.0f, 0.0f, 0.0f};   // Dimensions [width, height, depth]
    float u{0.0f};                 // Texture U offset in pixels
    float v{0.0f};                 // Texture V offset in pixels
    float inflate{0.0f};           // Expand all faces by this amount
    bool mirror{false};            // Mirror UV coordinates horizontally
};

// A bone containing one or more cubes and an optional parent hierarchy
struct Bone {
    std::string name;
    std::string parent;
    Vec3 pivot{0.0f, 0.0f, 0.0f};    // Pivot position [x, y, z] in model pixels
    Vec3 rotation{0.0f, 0.0f, 0.0f}; // Default rotation [rx, ry, rz] in degrees
    std::vector<Cube> cubes;
};

// A full entity model description (compatible with Bedrock / Blockbench geometry.json)
struct EntityModel {
    std::string identifier;
    float texture_width{64.0f};
    float texture_height{64.0f};
    std::vector<Bone> bones;

    [[nodiscard]] const Bone* findBone(std::string_view name) const;

    // Build a RigMesh for a single bone (vertices in host units)
    [[nodiscard]] rig::RigMesh buildBoneMesh(const Bone& bone, const rig::HostBasis& basis) const;
    [[nodiscard]] rig::RigMesh buildBoneMesh(std::string_view bone_name, const rig::HostBasis& basis) const;

    // Build a single combined RigMesh of all bones in the rest pose
    [[nodiscard]] rig::RigMesh buildCombinedMesh(const rig::HostBasis& basis) const;

    // Parse from Bedrock geometry JSON (both 1.12.0+ "minecraft:geometry" and 1.8.0 "geometry.<id>")
    static std::optional<EntityModel> fromJson(std::string_view json_str, std::string_view target_id = "");
};

} // namespace mc::model
