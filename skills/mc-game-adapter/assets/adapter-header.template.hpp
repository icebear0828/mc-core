#pragma once

#include "mc/contracts/physics_adapter.hpp"
#include "mc/contracts/render_adapter.hpp"
#include "mc/contracts/combat_adapter.hpp"
#include "mc/contracts/input_adapter.hpp"

namespace mc::adapter {

class {{GAME_NAME}}Adapter : public IPhysicsAdapter,
                            public IRenderAdapter,
                            public ICombatAdapter,
                            public IInputAdapter {
public:
    {{GAME_NAME}}Adapter();
    ~{{GAME_NAME}}Adapter() override;

    // --- IPhysicsAdapter ---
    RaycastResult raycastWorld(const Vec3& start, const Vec3& end, EntityId ignore_entity = EntityId::None) override;
    uint64_t createBlockCollider(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) override;
    void destroyBlockCollider(uint64_t collider_handle) override;
    void applyLinearImpulse(EntityId entity_id, const Vec3& impulse) override;
    void setLinearVelocity(EntityId entity_id, const Vec3& velocity) override;

    // --- IRenderAdapter ---
    void setNativePlayerVisible(bool visible) override;
    bool spawnSteveParts() override;
    void destroySteveParts() override;
    void setSteveRoot(const Vec3& feet_position, float body_yaw) override;
    void updateStevePartTransforms(const SteveAnimator::PartTransforms& transforms) override;
    void setHeldItemVisual(ItemId item, bool is_offhand = false) override;
    uint64_t spawnBlockVisual(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) override;
    void setBlockCrackStage(uint64_t block_handle, int stage) override;
    void destroyBlockVisual(uint64_t block_handle) override;

    // --- ICombatAdapter ---
    bool processHit(const HitIntent& intent) override;
    float getMaxHealth(EntityId entity_id) override;
    void triggerStaggerOrRagdoll(EntityId entity_id, const Vec3& direction, float force) override;

    // --- IInputAdapter ---
    ItemId getEquippedMainHand() const override;
    ItemId getEquippedOffHand() const override;
    Vec3 getCameraPosition() const override;
    Vec3 getCameraForward() const override;
    Vec3 getPlayerPosition() const override;
    Vec3 getPlayerVelocity() const override;
};

} // namespace mc::adapter
