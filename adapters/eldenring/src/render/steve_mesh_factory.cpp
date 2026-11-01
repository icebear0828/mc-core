#include "render/steve_mesh_factory.hpp"

#include <vector>

#include "eldenring_blockmesh.hpp"
#include "mc/blockmesh.hpp"
#include "mc/hud_atlas.hpp"
#include "mc/shadow.hpp"

namespace erov::steve {
namespace {

// The whole 64x64 shield entity texture inside the HUD atlas: the 3D shield is mapped onto it like the game does.
mc::rig::UvRect ShieldSheet() {
    return {mc::hud::kUV_ENTITY_SHIELD.u0, mc::hud::kUV_ENTITY_SHIELD.v0, mc::hud::kUV_ENTITY_SHIELD.u1, mc::hud::kUV_ENTITY_SHIELD.v1};
}

DrawRange AppendMesh(const mc::rig::RigMesh& mesh, std::vector<mc::rig::RigVertex>& vertices,
                     std::vector<uint16_t>& indices) {
    DrawRange d;
    d.base_vertex = static_cast<int>(vertices.size());
    d.first_index = static_cast<unsigned>(indices.size());
    d.index_count = static_cast<unsigned>(mesh.indices.size());
    vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
    return d;
}

// The crossed four-plane arrow mesh used for flying arrows (two extruded profiles plus two billboards).
void BuildArrowMesh(const mc::rig::ItemSprite& sprite, unsigned atlas_w, unsigned atlas_h, const FpItemCell& cell,
                    std::vector<mc::rig::RigVertex>& out_vertices, std::vector<uint16_t>& out_indices) {
    const mc::rig::RigMesh base_mesh = mc::rig::buildFlatItemMesh(sprite);
    if (base_mesh.vertices.empty()) return;
    constexpr float inv_sqrt2 = 0.70710678f;
    constexpr float scale = 0.55f;

    // Plane 1: extruded horizontal profile (XZ plane).
    const uint16_t base1 = static_cast<uint16_t>(out_vertices.size());
    for (mc::rig::RigVertex v : base_mesh.vertices) {
        const float x1 = (v.x + v.y) * inv_sqrt2;
        const float y1 = (-v.x + v.y) * inv_sqrt2;
        const float z1 = v.z;
        v.x = y1 * scale;
        v.y = z1 * 2.0f;
        v.z = x1 * scale;
        out_vertices.push_back(v);
    }
    for (uint16_t idx : base_mesh.indices) out_indices.push_back(static_cast<uint16_t>(base1 + idx));

    // Plane 2: extruded vertical profile (YZ plane), rotated 90 degrees around the flight axis (Z).
    const uint16_t base2 = static_cast<uint16_t>(out_vertices.size());
    for (mc::rig::RigVertex v : base_mesh.vertices) {
        const float x1 = (v.x + v.y) * inv_sqrt2;
        const float y1 = (-v.x + v.y) * inv_sqrt2;
        const float z1 = v.z;
        v.x = z1 * 2.0f;
        v.y = y1 * scale;
        v.z = x1 * scale;
        out_vertices.push_back(v);
    }
    for (uint16_t idx : base_mesh.indices) out_indices.push_back(static_cast<uint16_t>(base2 + idx));

    // Planes 3 & 4: dual crossed billboard quads along the shaft (XZ and YZ) for solid visibility at distance.
    const float u0 = static_cast<float>(cell.x) / static_cast<float>(atlas_w);
    const float v0 = static_cast<float>(cell.y) / static_cast<float>(atlas_h);
    const float u1 = static_cast<float>(cell.x + cell.w) / static_cast<float>(atlas_w);
    const float v1 = static_cast<float>(cell.y + cell.h) / static_cast<float>(atlas_h);
    constexpr float half_len = 0.38f;
    constexpr float half_w = 0.055f;

    // Horizontal billboard in XZ.
    const uint16_t b_h = static_cast<uint16_t>(out_vertices.size());
    out_vertices.push_back({-half_w, 0.0f, -half_len, u0, v1});
    out_vertices.push_back({half_w, 0.0f, -half_len, u1, v1});
    out_vertices.push_back({half_w, 0.0f, half_len, u1, v0});
    out_vertices.push_back({-half_w, 0.0f, half_len, u0, v0});
    out_indices.insert(out_indices.end(), {b_h, static_cast<uint16_t>(b_h + 1), static_cast<uint16_t>(b_h + 2),
                                           b_h, static_cast<uint16_t>(b_h + 2), static_cast<uint16_t>(b_h + 3)});

    // Vertical billboard in YZ.
    const uint16_t b_v = static_cast<uint16_t>(out_vertices.size());
    out_vertices.push_back({0.0f, -half_w, -half_len, u0, v1});
    out_vertices.push_back({0.0f, half_w, -half_len, u1, v1});
    out_vertices.push_back({0.0f, half_w, half_len, u1, v0});
    out_vertices.push_back({0.0f, -half_w, half_len, u0, v0});
    out_indices.insert(out_indices.end(), {b_v, static_cast<uint16_t>(b_v + 1), static_cast<uint16_t>(b_v + 2),
                                           b_v, static_cast<uint16_t>(b_v + 2), static_cast<uint16_t>(b_v + 3)});
}

} // namespace

RigCpuMesh BuildSteveRigMesh() {
    RigCpuMesh out;
    for (size_t p = 0; p < static_cast<size_t>(mc::StevePart::Count); ++p) {
        const mc::rig::RigMesh mesh = mc::rig::buildPartMesh(static_cast<mc::StevePart>(p), eldenring::render::kBasis);
        out.parts[p] = AppendMesh(mesh, out.vertices, out.indices);
    }
    return out;
}

FirstPersonMesh BuildFirstPerson(const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                                 const std::vector<FpItemCell>& cells) {
    FirstPersonMesh out;
    // The arm in Minecraft's model space (blocks, y down, relative to the shoulder), then moved to the shoulder
    // (-5, 2, 0) / 16 as ModelPart.render does before the cube.
    const mc::rig::HostBasis model_basis{{0.f, 0.f, -1.f}, {1.f, 0.f, 0.f}, {0.f, -1.f, 0.f}, 1.0f / (mc::rig::kCmPerModelPixel * 16.0f)};
    const mc::StevePart parts[2] = {mc::StevePart::RightArm, mc::StevePart::RightSleeve};
    for (int i = 0; i < 2; ++i) {
        mc::rig::RigMesh mesh = mc::rig::buildPartMesh(parts[i], model_basis);
        const mc::Vec3 pivot = mc::rig::partPivot(parts[i], model_basis);
        for (mc::rig::RigVertex& v : mesh.vertices) {
            v.x = v.x - pivot.x + (-5.0f / 16.0f);
            v.y = v.y - pivot.y + (2.0f / 16.0f);
            v.z = v.z - pivot.z;
        }
        out.arm_ranges[i] = AppendMesh(mesh, out.vertices, out.indices);
    }
    if (atlas_rgba) {
        for (const FpItemCell& cell : cells) {
            const mc::BlockId block = eldenring::blocks::blockForItem(static_cast<mc::ItemId>(cell.item));
            mc::rig::RigMesh mesh;
            if (block != mc::BlockId::Air) {
                mesh = eldenring::blocks::buildCubeMesh(block); // blocks are held as textured cubes, not as their GUI icon
            } else if (static_cast<mc::ItemId>(cell.item) == mc::ItemId::Shield) {
                mesh = mc::rig::buildShieldFirstPersonMesh(ShieldSheet()); // the real model, not the sprite pushed out one pixel
            } else {
                const mc::rig::ItemSprite sprite{atlas_rgba, static_cast<int>(atlas_w), static_cast<int>(atlas_h), cell.x, cell.y, cell.w, cell.h};
                mesh = mc::rig::buildFlatItemMesh(sprite);
            }
            if (!mesh.vertices.empty()) out.items[cell.item] = AppendMesh(mesh, out.vertices, out.indices);
        }
    }
    return out;
}

HeldItemsMesh BuildHeldItems(const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                             const std::vector<FpItemCell>& cells) {
    HeldItemsMesh out;
    for (const FpItemCell& cell : cells) {
        const mc::ItemId item = static_cast<mc::ItemId>(cell.item);
        const mc::BlockId block = eldenring::blocks::blockForItem(item);
        mc::rig::RigMesh mesh;
        if (block != mc::BlockId::Air) {
            mesh = eldenring::blocks::buildHeldCubeMesh(block, eldenring::render::kBasis);
        } else {
            if (!mc::rig::isHeldAsFlatSprite(item)) continue;
            const mc::rig::ItemSprite sprite{atlas_rgba, static_cast<int>(atlas_w), static_cast<int>(atlas_h), cell.x, cell.y, cell.w, cell.h};
            mesh = item == mc::ItemId::Shield ? mc::rig::buildShieldMesh(ShieldSheet(), eldenring::render::kBasis, mc::rig::Hand::Right)
                                              : mc::rig::buildHeldItemMesh(sprite, mc::rig::heldItemStyle(item), eldenring::render::kBasis);
        }
        if (mesh.vertices.empty()) continue;
        out.draws[cell.item] = AppendMesh(mesh, out.vertices, out.indices);
        if (item == mc::ItemId::Arrow) {
            const mc::rig::ItemSprite arrow_sprite{atlas_rgba, static_cast<int>(atlas_w), static_cast<int>(atlas_h), cell.x, cell.y, cell.w, cell.h};
            BuildArrowMesh(arrow_sprite, atlas_w, atlas_h, cell, out.arrow_vertices, out.arrow_indices);
            out.arrow_valid = !out.arrow_vertices.empty();
        }
        if (item == mc::ItemId::Shield) { // the raised shield has its own display transform
            const mc::rig::RigMesh up = mc::rig::buildShieldMesh(ShieldSheet(), eldenring::render::kBasis, mc::rig::Hand::Right, true);
            out.draws[static_cast<uint16_t>(cell.item | kBlockingKeyBit)] = AppendMesh(up, out.vertices, out.indices);
        }
    }

    // Offhand items (left arm, mirrored or forearm-mounted shield).
    for (const FpItemCell& cell : cells) {
        const mc::ItemId item = static_cast<mc::ItemId>(cell.item);
        const mc::BlockId block = eldenring::blocks::blockForItem(item);
        mc::rig::RigMesh mesh;
        if (block != mc::BlockId::Air) {
            mesh = eldenring::blocks::buildHeldCubeMesh(block, eldenring::render::kBasis);
        } else {
            if (!mc::rig::isHeldAsFlatSprite(item)) continue;
            const mc::rig::ItemSprite sprite{atlas_rgba, static_cast<int>(atlas_w), static_cast<int>(atlas_h), cell.x, cell.y, cell.w, cell.h};
            mesh = item == mc::ItemId::Shield ? mc::rig::buildShieldMesh(ShieldSheet(), eldenring::render::kBasis, mc::rig::Hand::Left)
                                              : mc::rig::buildHeldItemMesh(sprite, mc::rig::heldItemStyle(item), eldenring::render::kBasis, mc::rig::Hand::Left);
        }
        if (mesh.vertices.empty()) continue;
        out.off_draws[cell.item] = AppendMesh(mesh, out.off_vertices, out.off_indices);
        if (item == mc::ItemId::Shield) {
            const mc::rig::RigMesh up = mc::rig::buildShieldMesh(ShieldSheet(), eldenring::render::kBasis, mc::rig::Hand::Left, true);
            out.off_draws[static_cast<uint16_t>(cell.item | kBlockingKeyBit)] = AppendMesh(up, out.off_vertices, out.off_indices);
        }
    }
    return out;
}

MobCpuMesh BuildMobMesh(const mc::model::EntityModel& model) {
    MobCpuMesh out;
    for (const mc::model::Bone& bone : model.bones) {
        const mc::rig::RigMesh mesh = model.buildBoneMesh(bone, eldenring::render::kBasis);
        out.bone_ranges.push_back(AppendMesh(mesh, out.vertices, out.indices));
    }
    for (size_t i = 0; i < model.bones.size(); ++i) {
        if (model.bones[i].name == "right_arm") out.right_arm_ix = static_cast<int>(i);
        else if (model.bones[i].name == "left_arm") out.left_arm_ix = static_cast<int>(i);
    }
    return out;
}

mc::rig::RigMesh BuildShadowDisc(unsigned segments) {
    return mc::buildShadowDisc(static_cast<int>(segments));
}

} // namespace erov::steve
