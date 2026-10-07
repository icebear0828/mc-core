#pragma once

#include "mc/animator.hpp"
#include "mc/types.hpp"

namespace mc {

class IRenderAdapter {
public:
    virtual ~IRenderAdapter() = default;

    // Toggle host native player visibility (hide mesh while keeping physics capsule)
    virtual void setNativePlayerVisible(bool visible) = 0;

    // Spawn / destroy 12 rigid Steve mesh parts
    virtual bool spawnSteveParts() = 0;
    virtual void destroySteveParts() = 0;

    // Place the rig in the world: feet position (canonical MC space, cm) and body yaw (radians,
    // 0 = +X, counter-clockwise positive). Part transforms below are relative to this root.
    virtual void setSteveRoot(const Vec3& feet_position, float body_yaw) = 0;

    // Update 12 Steve parts relative transforms computed by SteveAnimator
    virtual void updateStevePartTransforms(const SteveAnimator::PartTransforms& transforms) = 0;

    // Held item attachments
    virtual void setHeldItemVisual(ItemId item, bool is_offhand = false) = 0;

    // Voxel visuals
    virtual uint64_t spawnBlockVisual(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) = 0;
    virtual void setBlockCrackStage(uint64_t block_handle, int stage) = 0;
    virtual void destroyBlockVisual(uint64_t block_handle) = 0;
};

} // namespace mc
