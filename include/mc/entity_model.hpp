#pragma once

#include "mc/animator.hpp"
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

// ---- Pose: pivots, rest rotations and the bone hierarchy ------------------------------------------------------------------------
// The meshes from buildBoneMesh are rest-pose absolute (feet at the origin). A pose is one matrix per bone, in the order of EntityModel::bones:
// the bone turns about its own pivot by its rest rotation (the file's "rotation", degrees about the Bedrock axes, X then Y then Z) and then by
// `extra[i]` (a host-space quaternion, for example from the animator), then follows its parent bone, then the whole figure is turned by
// `host_yaw` about the host's vertical axis and moved to `root`. Row-vector convention like rig::partMatrix.

// A bone's pivot in host units (rest pose, feet at the origin).
Vec3 bonePivotHost(const Bone& bone, const rig::HostBasis& basis);

// The rest rotation of a bone as a matrix about its pivot's axes in host space (identity when the file gives none). Positive angles turn
// like the right-hand rule in the canonical frame (X forward, Y left, Z up): the zombie's [-90, 0, 0] arms point forward.
rig::Mat4 boneRestRotation(const Bone& bone, const rig::HostBasis& basis);

// `extra` may be shorter than `model.bones` or empty (identity for the missing ones).
std::vector<rig::Mat4> boneMatrices(const EntityModel& model, const std::vector<Quat>& extra, const Vec3& root, float host_yaw,
                                    const rig::HostBasis& basis);

// The bone names of the humanoid models (zombie, skeleton...) are the Steve rig's parts in snake_case; this lets the Steve animator drive them.
std::optional<StevePart> humanoidPartForBone(std::string_view bone_name);

} // namespace mc::model
