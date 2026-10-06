#pragma once

#include "mc/types.hpp"
#include <optional>
#include <unordered_map>

namespace mc {

class IPhysicsAdapter;
class IRenderAdapter;

struct VoxelBlock {
    GridPos pos;
    BlockId id{BlockId::Air};
    uint64_t visual_handle{0};
    uint64_t collider_handle{0};
    int mining_stage{-1}; // -1 = undamaged, 0..9 = cracking
    float mining_progress{0.f}; // 0.0 to 1.0
};

// GridPos hash for unordered_map
struct GridPosHash {
    size_t operator()(const GridPos& p) const noexcept {
        size_t h1 = std::hash<int32_t>{}(p.x);
        size_t h2 = std::hash<int32_t>{}(p.y);
        size_t h3 = std::hash<int32_t>{}(p.z);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

class VoxelWorld {
public:
    static constexpr float kBlockSizeCm = 100.0f; // 1 meter = 100cm

    explicit VoxelWorld(IPhysicsAdapter& physics, IRenderAdapter& render);
    ~VoxelWorld();

    // Snap a world position (in cm) to voxel integer grid coordinates
    static GridPos worldToGrid(const Vec3& world_pos_cm);
    static Vec3 gridToWorld(const GridPos& grid_pos);

    // Calculate placement candidate given a surface raycast hit
    static std::optional<GridPos> calculatePlacementTarget(const RaycastResult& hit);

    bool placeBlock(const GridPos& target_pos, BlockId block_id, const Vec3& player_pos_cm);
    bool mineBlock(const GridPos& target_pos, ItemId held_tool, float dt);
    bool removeBlock(const GridPos& target_pos);

    [[nodiscard]] const VoxelBlock* getBlock(const GridPos& pos) const;
    [[nodiscard]] size_t getActiveBlockCount() const { return blocks_.size(); }

    void clear();

private:
    IPhysicsAdapter& physics_;
    IRenderAdapter& render_;
    std::unordered_map<GridPos, VoxelBlock, GridPosHash> blocks_;
};

} // namespace mc
