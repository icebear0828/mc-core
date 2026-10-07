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
    // false for EntityId::None. LocalPlayer is bound automatically by setPlayerCharacter().
    bool registerEntity(EntityId entity_id, sekiro::native::ChrIns* entity);
    void unregisterEntity(EntityId entity_id);
    sekiro::native::ChrIns* getRegisteredEntity(EntityId entity_id) const;

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

    // Auxiliary methods & inspections
    void setEquippedItems(ItemId main, ItemId off);
    [[nodiscard]] bool isSteveSpawned() const { return steve_parts_spawned_; }
    [[nodiscard]] bool isNativePlayerHidden() const { return native_player_hidden_; }
    [[nodiscard]] size_t getBlockColliderCount() const { return colliders_.size(); }
    [[nodiscard]] size_t getBlockVisualCount() const { return visuals_.size(); }
    const sekiro::native::SekiroVisualMeshComponent* getStevePartVisual(StevePart part) const;

    // Vertical-velocity heuristic: Dantelion exposes no grounded flag we can read yet.
    [[nodiscard]] bool isPlayerOnGround() const;

    // Dantelion units are metres (verified in-game: sprint ~5.6 u/s); MC space is centimetres.
    static constexpr float kCmPerNativeUnit = 100.0f;

    // Canonical MC space (Z-up, right-handed, X=forward, Y=left)  <->  Dantelion (Y-up, X=right, Z=forward).
    // Dantelion is left-handed (verified: turning the camera right moves forward toward the camera's
    // right vector), so this is a proper change of basis. Positions, extents and velocities are scaled
    // (Point); unit directions such as normals and view vectors are not (Dir).
    static sekiro::native::FVector3 toNativeDir(const Vec3& v) { return {-v.y, v.z, v.x}; }
    static Vec3 toMcDir(const sekiro::native::FVector3& v) { return {v.Z, -v.X, v.Y}; }
    static sekiro::native::FVector3 toNativePoint(const Vec3& v) { return toNativeDir(v * (1.0f / kCmPerNativeUnit)); }
    static Vec3 toMcPoint(const sekiro::native::FVector3& v) { return toMcDir(v) * kCmPerNativeUnit; }
    // Same physical rotation expressed in Dantelion axes. The basis change is a reflection, so the
    // rotation axis (a pseudovector) flips sign relative to the mapped vector part.
    static sekiro::native::FQuat toNativeQuat(const Quat& q) {
        return {q.y, -q.z, -q.x, q.w};
    }

private:
    sekiro::native::ChrIns* player_{nullptr};
    sekiro::native::ChrCam* camera_{nullptr};

    uint64_t next_collider_handle_{1};
    uint64_t next_visual_handle_{1};

    std::unordered_map<uint64_t, std::unique_ptr<SekiroBlockColliderRecord>> colliders_;
    std::unordered_map<uint64_t, std::unique_ptr<SekiroBlockVisualRecord>> visuals_;
    std::unordered_map<EntityId, sekiro::native::ChrIns*> registered_entities_;

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
