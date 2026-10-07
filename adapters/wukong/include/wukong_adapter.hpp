#pragma once

#include "mc/contracts/physics_adapter.hpp"
#include "mc/contracts/render_adapter.hpp"
#include "mc/contracts/combat_adapter.hpp"
#include "mc/contracts/input_adapter.hpp"
#include "wukong_native.hpp"

#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace mc::adapter {

struct NativeBlockColliderRecord {
    uint64_t handle{0};
    GridPos grid_pos{};
    BlockId block_id{BlockId::Air};
    std::unique_ptr<b1::native::AActor> actor;
    b1::native::UBoxComponent* box_component{nullptr};
};

struct NativeBlockVisualRecord {
    uint64_t handle{0};
    GridPos grid_pos{};
    BlockId block_id{BlockId::Air};
    int crack_stage{-1};
    std::unique_ptr<b1::native::AActor> actor;
    b1::native::UProceduralMeshComponent* mesh_component{nullptr};
};

class WukongAdapter : public IPhysicsAdapter,
                      public IRenderAdapter,
                      public ICombatAdapter,
                      public IInputAdapter {
public:
    WukongAdapter();
    explicit WukongAdapter(b1::native::APlayerController* controller);
    ~WukongAdapter() override;

    // Host engine context setup
    void setPlayerController(b1::native::APlayerController* controller);
    void setWorld(b1::native::UWorld* world);
    // false for EntityId::None. LocalPlayer is bound automatically by setPlayerController().
    bool registerEntity(EntityId entity_id, b1::native::ABGUCharacter* character);
    void unregisterEntity(EntityId entity_id);

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

    // Utility setters & inspectors
    void setEquippedItems(ItemId main, ItemId off);
    [[nodiscard]] bool isSteveSpawned() const { return steve_parts_spawned_; }

    // Rig root as UE5 sees it: feet in cm and yaw in degrees (positive turns to the right).
    struct SteveRoot {
        b1::native::FVector position{};
        float yaw_degrees{0.0f};
    };
    [[nodiscard]] const SteveRoot& getSteveRoot() const { return steve_root_; }
    [[nodiscard]] bool isNativePlayerHidden() const { return native_player_hidden_; }
    [[nodiscard]] size_t getBlockColliderCount() const { return colliders_.size(); }
    [[nodiscard]] size_t getBlockVisualCount() const { return visuals_.size(); }

    // Vertical-velocity heuristic: the shadow native layer exposes no movement-mode query.
    [[nodiscard]] bool isPlayerOnGround() const;
    [[nodiscard]] const b1::native::UProceduralMeshComponent* getStevePartComponent(StevePart part) const;

    // Canonical MC space (Z-up, right-handed, X=forward, Y=left)  <->  UE5 (Z-up, left-handed, X=forward, Y=right).
    // Only Y flips. Units are centimetres on both sides.
    static b1::native::FVector toNative(const Vec3& v) { return {v.x, -v.y, v.z}; }
    static Vec3 toMc(const b1::native::FVector& v) { return {v.X, -v.Y, v.Z}; }
    // Same physical rotation as an FRotator (degrees). Mirroring Y flips the pseudovector rotation axis.
    static b1::native::FRotator toNativeRotator(const Quat& q);

private:
    b1::native::APlayerController* controller_{nullptr};
    b1::native::UWorld* world_{nullptr};

    uint64_t next_collider_handle_{1};
    uint64_t next_visual_handle_{1};

    std::unordered_map<uint64_t, std::unique_ptr<NativeBlockColliderRecord>> colliders_;
    std::unordered_map<uint64_t, std::unique_ptr<NativeBlockVisualRecord>> visuals_;
    std::unordered_map<EntityId, b1::native::ABGUCharacter*> registered_entities_;

    // 12 Steve parts
    bool steve_parts_spawned_{false};
    SteveRoot steve_root_{};
    std::array<std::unique_ptr<b1::native::UProceduralMeshComponent>, SteveAnimator::kPartCount> steve_parts_{};
    std::unique_ptr<b1::native::UProceduralMeshComponent> weapon_mesh_;
    std::unique_ptr<b1::native::UProceduralMeshComponent> shield_mesh_;

    bool native_player_hidden_{false};
    std::vector<std::pair<b1::native::USceneComponent*, bool>> hidden_components_cache_;

    ItemId main_hand_item_{ItemId::DiamondSword};
    ItemId off_hand_item_{ItemId::None};
};

} // namespace mc::adapter
