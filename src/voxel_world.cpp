#include "mc/voxel_world.hpp"
#include "mc/contracts/physics_adapter.hpp"
#include "mc/contracts/render_adapter.hpp"
#include <cmath>

namespace mc {

VoxelWorld::VoxelWorld(IPhysicsAdapter& physics, IRenderAdapter& render)
    : physics_(physics), render_(render) {}

VoxelWorld::~VoxelWorld() {
    clear();
}

GridPos VoxelWorld::worldToGrid(const Vec3& world_pos_cm) {
    auto quantize = [](float val) -> int32_t {
        return static_cast<int32_t>(std::floor((val / kBlockSizeCm) + 0.5f));
    };
    return GridPos{quantize(world_pos_cm.x), quantize(world_pos_cm.y), quantize(world_pos_cm.z)};
}

Vec3 VoxelWorld::gridToWorld(const GridPos& grid_pos) {
    return Vec3{
        static_cast<float>(grid_pos.x) * kBlockSizeCm,
        static_cast<float>(grid_pos.y) * kBlockSizeCm,
        static_cast<float>(grid_pos.z) * kBlockSizeCm
    };
}

std::optional<GridPos> VoxelWorld::calculatePlacementTarget(const RaycastResult& hit) {
    if (!hit.has_hit) {
        return std::nullopt;
    }

    // Offset half a block outward along the hit surface normal
    const Vec3 target_cm = hit.point + hit.normal * (kBlockSizeCm * 0.5f);
    return worldToGrid(target_cm);
}

bool VoxelWorld::placeBlock(const GridPos& target_pos, BlockId block_id, const Vec3& player_pos_cm) {
    if (block_id == BlockId::Air) {
        return false;
    }

    if (blocks_.contains(target_pos)) {
        return false; // Position already occupied by another voxel
    }

    const Vec3 block_world_pos = gridToWorld(target_pos);

    // Bounding check against player capsule (approx. 80cm radius, 180cm height)
    const float dx = std::abs(player_pos_cm.x - block_world_pos.x);
    const float dy = std::abs(player_pos_cm.y - block_world_pos.y);
    const float dz = std::abs(player_pos_cm.z - block_world_pos.z);
    if (dx < 70.f && dy < 70.f && dz < 120.f) {
        return false; // Player is inside this voxel space
    }

    // Delegate creation to adapters
    const uint64_t visual_h = render_.spawnBlockVisual(target_pos, block_id, block_world_pos);
    const uint64_t collider_h = physics_.createBlockCollider(target_pos, block_id, block_world_pos);

    VoxelBlock block;
    block.pos = target_pos;
    block.id = block_id;
    block.visual_handle = visual_h;
    block.collider_handle = collider_h;
    block.mining_stage = -1;
    block.mining_progress = 0.f;

    blocks_[target_pos] = block;
    return true;
}

bool VoxelWorld::mineBlock(const GridPos& target_pos, ItemId held_tool, float dt) {
    auto it = blocks_.find(target_pos);
    if (it == blocks_.end()) {
        return false;
    }

    VoxelBlock& block = it->second;

    // Time to break (seconds) based on block and tool
    float break_time = 1.0f;
    if (block.id == BlockId::Stone) {
        break_time = (held_tool == ItemId::DiamondPickaxe) ? 0.3f : 7.5f;
    } else if (block.id == BlockId::Dirt) {
        break_time = 0.75f;
    } else if (block.id == BlockId::Tnt) {
        break_time = 0.05f;
    }

    block.mining_progress += (dt / break_time);

    if (block.mining_progress >= 1.0f) {
        removeBlock(target_pos);
        return true; // Successfully broken
    }

    // Update crack stage (0 to 9)
    const int new_stage = std::min(9, static_cast<int>(block.mining_progress * 10.0f));
    if (new_stage != block.mining_stage) {
        block.mining_stage = new_stage;
        render_.setBlockCrackStage(block.visual_handle, new_stage);
    }

    return false;
}

bool VoxelWorld::removeBlock(const GridPos& target_pos) {
    auto it = blocks_.find(target_pos);
    if (it == blocks_.end()) {
        return false;
    }

    physics_.destroyBlockCollider(it->second.collider_handle);
    render_.destroyBlockVisual(it->second.visual_handle);
    blocks_.erase(it);
    return true;
}

const VoxelBlock* VoxelWorld::getBlock(const GridPos& pos) const {
    auto it = blocks_.find(pos);
    if (it != blocks_.end()) {
        return &it->second;
    }
    return nullptr;
}

void VoxelWorld::clear() {
    for (auto& [pos, block] : blocks_) {
        physics_.destroyBlockCollider(block.collider_handle);
        render_.destroyBlockVisual(block.visual_handle);
    }
    blocks_.clear();
}

} // namespace mc
