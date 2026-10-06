#pragma once

#include "mc/contracts/physics_adapter.hpp"
#include "mc/contracts/render_adapter.hpp"
#include "mc/contracts/combat_adapter.hpp"
#include "mc/contracts/input_adapter.hpp"
#include "mc/animator.hpp"
#include "sekiro_native.hpp"

#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <array>

namespace mc::adapter {

struct SekiroBlockColliderRecord {
    uint64_t handle{0};
    GridPos grid_pos{};
    BlockId block_id{BlockId::Air};
    std::unique_ptr<sekiro::native::HavokStaticBoxCollider> collider;
};

struct SekiroBlockVisualRecord {
    uint64_t handle{0};
    GridPos grid_pos{};
    BlockId block_id{BlockId::Air};
    int crack_stage{-1};
    std::unique_ptr<sekiro::native::SekiroVisualMeshComponent> mesh;
};

class SekiroAdapter : public IPhysicsAdapter,
                      public IRenderAdapter,
                      public ICombatAdapter,
                      public IInputAdapter {
public:
    SekiroAdapter();
    explicit SekiroAdapter(sekiro::native::ChrIns* player, sekiro::native::ChrCam* camera = nullptr);
    ~SekiroAdapter() override;

    // Host engine context setup
    void setPlayerCharacter(sekiro::native::ChrIns* player);
    void setPlayerCamera(sekiro::native::ChrCam* camera);
    void registerEntity(uint64_t entity_id, sekiro::native::ChrIns* entity);
    void unregisterEntity(uint64_t entity_id);
    sekiro::native::ChrIns* getRegisteredEntity(uint64_t entity_id) const;
    void setCustomRaycastHandler(sekiro::native::DantelionEngineContext::RaycastHandler handler);

    // --- IPhysicsAdapter ---
    RaycastResult raycastWorld(const Vec3& start, const Vec3& end, uint64_t ignore_entity = 0) override;
    uint64_t createBlockCollider(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) override;
    void destroyBlockCollider(uint64_t collider_handle) override;
    void applyLinearImpulse(uint64_t entity_id, const Vec3& impulse) override;

    // --- IRenderAdapter ---
    void setNativePlayerVisible(bool visible) override;
    bool spawnSteveParts() override;
    void destroySteveParts() override;
    void updateStevePartTransforms(const SteveAnimator::PartTransforms& transforms) override;
    void setHeldItemVisual(ItemId item, bool is_offhand = false) override;
    uint64_t spawnBlockVisual(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) override;
    void setBlockCrackStage(uint64_t block_handle, int stage) override;
    void destroyBlockVisual(uint64_t block_handle) override;

    // --- ICombatAdapter ---
    bool processHit(const HitIntent& intent) override;
    float getMaxHealth(uint64_t entity_id) override;
    void triggerStaggerOrRagdoll(uint64_t entity_id, const Vec3& direction, float force) override;

    // --- IInputAdapter ---
    ItemId getEquippedMainHand() const override;
    ItemId getEquippedOffHand() const override;
    Vec3 getCameraPosition() const override;
    Vec3 getCameraForward() const override;
    Vec3 getPlayerPosition() const override;
    Vec3 getPlayerVelocity() const override;

    // Auxiliary methods & inspections
    void setEquippedItems(ItemId main, ItemId off);
    [[nodiscard]] bool isSteveSpawned() const { return steve_parts_spawned_; }
    [[nodiscard]] bool isNativePlayerHidden() const { return native_player_hidden_; }
    [[nodiscard]] size_t getBlockColliderCount() const { return colliders_.size(); }
    [[nodiscard]] size_t getBlockVisualCount() const { return visuals_.size(); }
    const sekiro::native::SekiroVisualMeshComponent* getStevePartVisual(StevePart part) const;

    // Coordinate conversions between MC coordinate space (Z-up) and Dantelion space (Y-up)
    // MC: X=lateral, Y=depth/forward, Z=vertical
    // Dantelion: X=lateral, Y=vertical, Z=depth/forward
    static sekiro::native::FVector3 toNative(const Vec3& v) {
        return {v.x, v.z, v.y};
    }
    static Vec3 toMc(const sekiro::native::FVector3& v) {
        return {v.X, v.Z, v.Y};
    }

private:
    sekiro::native::ChrIns* player_{nullptr};
    sekiro::native::ChrCam* camera_{nullptr};

    uint64_t next_collider_handle_{1};
    uint64_t next_visual_handle_{1};

    std::unordered_map<uint64_t, std::unique_ptr<SekiroBlockColliderRecord>> colliders_;
    std::unordered_map<uint64_t, std::unique_ptr<SekiroBlockVisualRecord>> visuals_;
    std::unordered_map<uint64_t, sekiro::native::ChrIns*> registered_entities_;
    sekiro::native::DantelionEngineContext::RaycastHandler custom_raycast_{nullptr};

    // 12 Steve parts
    bool steve_parts_spawned_{false};
    std::array<std::unique_ptr<sekiro::native::SekiroVisualMeshComponent>, SteveAnimator::kPartCount> steve_parts_{};
    std::unique_ptr<sekiro::native::SekiroVisualMeshComponent> weapon_mesh_;
    std::unique_ptr<sekiro::native::SekiroVisualMeshComponent> prosthetic_offhand_mesh_;

    bool native_player_hidden_{false};
    float cached_player_alpha_{1.0f};

    ItemId main_hand_item_{ItemId::DiamondSword};
    ItemId off_hand_item_{ItemId::None};
};

} // namespace mc::adapter
