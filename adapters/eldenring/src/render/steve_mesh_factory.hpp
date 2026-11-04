#pragma once

// CPU-only mesh builders for the Steve renderer. This header deliberately avoids d3d12.h (and
// eldenring_shadow.hpp, which pulls in the Windows-only LOS layer) so the geometry can be unit-tested on
// every platform. The renderer uploads the returned buffers and only submits draws.

#include <cstdint>
#include <map>
#include <vector>

#include "eldenring_steve.hpp"
#include "mc/entity_model.hpp"
#include "mc/item_model.hpp"
#include "mc/rig.hpp"
#include "render/steve_types.hpp"

namespace erov::steve {

static_assert(sizeof(mc::rig::RigVertex) == 20, "the figure input layout assumes a 20-byte RigVertex");

// Minecraft's first-person item space (blocks, +X right, +Y up, -Z forward), the space the item display
// transforms are written in. A Bedrock model built with it can be placed by the same item pose the flat
// sprites use.
inline constexpr mc::rig::HostBasis kFirstPersonItemBasis{{0.f, 0.f, -1.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                                                          1.0f / (mc::rig::kCmPerModelPixel * 16.0f)};

// The 12-part player rig, in one vertex/index buffer with one range per part.
struct RigCpuMesh {
    std::vector<mc::rig::RigVertex> vertices;
    std::vector<uint16_t> indices;
    DrawRange parts[static_cast<size_t>(mc::StevePart::Count)]{};
};

// The armor boxes for one 64x32 equipment layer, in one buffer with one range per Steve part.
// Parts the layer does not cover have an empty range.
struct ArmorCpuMesh {
    std::vector<mc::rig::RigVertex> vertices;
    std::vector<uint16_t> indices;
    DrawRange parts[static_cast<size_t>(mc::StevePart::Count)]{};
};

// First-person arm (bare arm + sleeve) and the held items, all in one buffer as the original renderer built
// them: the item ranges' base_vertex continues after the arm vertices.
struct FirstPersonMesh {
    std::vector<mc::rig::RigVertex> vertices;
    std::vector<uint16_t> indices;
    DrawRange arm_ranges[2]{};
    std::map<uint16_t, DrawRange> items; // key = item id
};

// Third-person held items: right hand and left hand each in their own buffer, plus the optional crossed
// arrow mesh (a separate buffer).
struct HeldItemsMesh {
    std::vector<mc::rig::RigVertex> vertices;
    std::vector<uint16_t> indices;
    std::map<uint16_t, DrawRange> draws;
    std::vector<mc::rig::RigVertex> off_vertices;
    std::vector<uint16_t> off_indices;
    std::map<uint16_t, DrawRange> off_draws;
    std::vector<mc::rig::RigVertex> arrow_vertices;
    std::vector<uint16_t> arrow_indices;
    bool arrow_valid{false};
    // The flying ender pearl: three crossed billboard quads centred on the origin.
    std::vector<mc::rig::RigVertex> pearl_vertices;
    std::vector<uint16_t> pearl_indices;
    bool pearl_valid{false};
};

// A Bedrock entity model flattened into one buffer, one draw range per bone.
struct MobCpuMesh {
    std::vector<mc::rig::RigVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<DrawRange> bone_ranges;
    int right_arm_ix{-1};
    int left_arm_ix{-1};
};

[[nodiscard]] RigCpuMesh BuildSteveRigMesh();
[[nodiscard]] ArmorCpuMesh BuildArmorRigMesh(mc::rig::ArmorLayer layer);
[[nodiscard]] FirstPersonMesh BuildFirstPerson(const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                                               const std::vector<FpItemCell>& cells);
[[nodiscard]] HeldItemsMesh BuildHeldItems(const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                                           const std::vector<FpItemCell>& cells);
[[nodiscard]] MobCpuMesh BuildMobMesh(const mc::model::EntityModel& model,
                                      const mc::rig::HostBasis& basis = eldenring::render::kBasis);

// A flat disc of `segments` triangles plus its centre vertex.
[[nodiscard]] mc::rig::RigMesh BuildShadowDisc(unsigned segments = 24);

} // namespace erov::steve
