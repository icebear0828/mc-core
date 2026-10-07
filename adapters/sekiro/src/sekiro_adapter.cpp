#include "sekiro_adapter.hpp"
#include "mc/voxel_world.hpp"

#include <algorithm>
#include <cmath>

namespace mc::adapter {

namespace {

const char* blockIdToModelName(BlockId id) {
    switch (id) {
        case BlockId::Dirt: return "dirt";
        case BlockId::Stone: return "stone";
        case BlockId::Tnt: return "tnt";
        case BlockId::Wood: return "wood";
        case BlockId::Obsidian: return "obsidian";
        case BlockId::Air:
        default: return "air";
    }
}

const char* itemIdToModelName(ItemId id) {
    switch (id) {
        case ItemId::DiamondSword: return "diamond_sword";
        case ItemId::DiamondPickaxe: return "diamond_pickaxe";
        case ItemId::Bow: return "bow";
        case ItemId::Arrow: return "arrow";
        case ItemId::Trident: return "trident";
        case ItemId::FlintAndSteel: return "flint_and_steel";
        case ItemId::EnderPearl: return "ender_pearl";
        case ItemId::GoldenApple: return "golden_apple";
        case ItemId::TotemOfUndying: return "totem_of_undying";
        case ItemId::BlockDirt: return "dirt";
        case ItemId::BlockStone: return "stone";
        case ItemId::BlockTnt: return "tnt";
        case ItemId::None:
        default: return "empty";
    }
}

const char* stevePartIndexToName(size_t index) {
    switch (static_cast<StevePart>(index)) {
        case StevePart::Head: return "steve_head";
        case StevePart::Hat: return "steve_hat";
        case StevePart::Body: return "steve_body";
        case StevePart::Jacket: return "steve_jacket";
        case StevePart::LeftArm: return "steve_left_arm";
        case StevePart::LeftSleeve: return "steve_left_sleeve";
        case StevePart::RightArm: return "steve_right_arm";
        case StevePart::RightSleeve: return "steve_right_sleeve";
        case StevePart::LeftLeg: return "steve_left_leg";
        case StevePart::LeftPants: return "steve_left_pants";
        case StevePart::RightLeg: return "steve_right_leg";
        case StevePart::RightPants: return "steve_right_pants";
        default: return "steve_part";
    }
}

} // namespace

SekiroAdapter::SekiroAdapter() = default;

SekiroAdapter::SekiroAdapter(sekiro::native::ChrIns* player, sekiro::native::ChrCam* camera)
    : player_(player), camera_(camera) {
    if (player_) {
        registerEntity(EntityId::LocalPlayer, player_);
    }
}

SekiroAdapter::~SekiroAdapter() {
    if (steve_parts_spawned_) {
        destroySteveParts();
    }
    if (native_player_hidden_) {
        setNativePlayerVisible(true);
    }
    colliders_.clear();
    visuals_.clear();
}

void SekiroAdapter::setPlayerCharacter(sekiro::native::ChrIns* player) {
    player_ = player;
    if (player_) {
        registerEntity(EntityId::LocalPlayer, player_);
        if (native_player_hidden_) {
            player_->bModelHidden = true;
            player_->ModelAlpha = 0.0f;
        } else {
            player_->bModelHidden = false;
            player_->ModelAlpha = (cached_player_alpha_ > 0.0f) ? cached_player_alpha_ : 1.0f;
        }
    }
}

void SekiroAdapter::setPlayerCamera(sekiro::native::ChrCam* camera) {
    camera_ = camera;
}

bool SekiroAdapter::registerEntity(EntityId entity_id, sekiro::native::ChrIns* entity) {
    if (entity_id == EntityId::None || !entity) {
        return false;
    }
    registered_entities_[entity_id] = entity;
    return true;
}

void SekiroAdapter::unregisterEntity(EntityId entity_id) {
    registered_entities_.erase(entity_id);
}

sekiro::native::ChrIns* SekiroAdapter::getRegisteredEntity(EntityId entity_id) const {
    auto it = registered_entities_.find(entity_id);
    return (it != registered_entities_.end()) ? it->second : nullptr;
}

const sekiro::native::SekiroVisualMeshComponent* SekiroAdapter::getStevePartVisual(StevePart part) const {
    size_t idx = static_cast<size_t>(part);
    if (idx < steve_parts_.size()) {
        return steve_parts_[idx].get();
    }
    return nullptr;
}

// =========================================================================
// IPhysicsAdapter Implementation
// =========================================================================

RaycastResult SekiroAdapter::raycastWorld(const Vec3& start, const Vec3& end, EntityId ignore_entity) {
    sekiro::native::FVector3 native_start = toNativePoint(start);
    sekiro::native::FVector3 native_end = toNativePoint(end);
    sekiro::native::HavokHitResult hit{};

    // Havok knows its own handles, not our EntityIds
    uint64_t ignore_handle = 0;
    if (const sekiro::native::ChrIns* ignored = getRegisteredEntity(ignore_entity)) {
        ignore_handle = ignored->Handle;
    }

    bool bEngineHit = sekiro::native::DantelionEngineContext::RaycastWorld(
        native_start,
        native_end,
        hit,
        ignore_handle
    );

    if (bEngineHit && hit.bHit) {
        RaycastResult res{};
        res.has_hit = true;
        res.point = toMcPoint(hit.HitPoint);
        res.normal = toMcDir(hit.HitNormal);

        // Check if hit one of our placed block colliders
        for (const auto& [handle, record] : colliders_) {
            if (record->handle == hit.HitColliderHandle) {
                res.is_block = true;
                res.hit_collider_handle = handle;
                return res;
            }
        }

        // Check if hit a registered character
        for (const auto& [id, chr] : registered_entities_) {
            if (chr && hit.HitEntityHandle != 0 && chr->Handle == hit.HitEntityHandle) {
                res.is_block = false;
                res.hit_entity = id;
                return res;
            }
        }

        // Terrain or an actor we do not track: never leak a raw host handle as an EntityId
        res.is_block = hit.bIsStaticBlock;
        return res;
    }

    // Direct geometric probe against placed block colliders (AABB ray intersection in MC coordinates)
    Vec3 dir = end - start;
    float dir_len = dir.length();
    if (dir_len < 1e-4f) {
        return RaycastResult{};
    }
    Vec3 norm_dir = dir.normalized();

    float closest_t = dir_len;
    RaycastResult best_result{};

    for (const auto& [handle, record] : colliders_) {
        Vec3 block_center = VoxelWorld::gridToWorld(record->grid_pos);
        // Block is 100cm cube centered at block_center
        Vec3 box_min = block_center - Vec3{50.f, 50.f, 50.f};
        Vec3 box_max = block_center + Vec3{50.f, 50.f, 50.f};

        float tmin = 0.0f;
        float tmax = dir_len;
        Vec3 hit_norm{0.f, 0.f, 1.f};

        bool slab_hit = true;
        for (int axis = 0; axis < 3; ++axis) {
            float origin = (axis == 0) ? start.x : (axis == 1) ? start.y : start.z;
            float d = (axis == 0) ? norm_dir.x : (axis == 1) ? norm_dir.y : norm_dir.z;
            float bmin = (axis == 0) ? box_min.x : (axis == 1) ? box_min.y : box_min.z;
            float bmax = (axis == 0) ? box_max.x : (axis == 1) ? box_max.y : box_max.z;

            if (std::abs(d) < 1e-6f) {
                if (origin < bmin || origin > bmax) {
                    slab_hit = false;
                    break;
                }
            } else {
                float inv_d = 1.0f / d;
                float t1 = (bmin - origin) * inv_d;
                float t2 = (bmax - origin) * inv_d;
                float norm_sign = -1.0f;
                if (t1 > t2) {
                    std::swap(t1, t2);
                    norm_sign = 1.0f;
                }
                if (t1 > tmin) {
                    tmin = t1;
                    hit_norm = Vec3{0.f, 0.f, 0.f};
                    if (axis == 0) hit_norm.x = norm_sign;
                    else if (axis == 1) hit_norm.y = norm_sign;
                    else hit_norm.z = norm_sign;
                }
                tmax = std::min(tmax, t2);
                if (tmin > tmax) {
                    slab_hit = false;
                    break;
                }
            }
        }

        if (slab_hit && tmin >= 0.0f && tmin < closest_t) {
            closest_t = tmin;
            best_result.has_hit = true;
            best_result.point = start + norm_dir * tmin;
            best_result.normal = hit_norm;
            best_result.hit_collider_handle = handle;
            best_result.is_block = true;
        }
    }

    return best_result;
}

uint64_t SekiroAdapter::createBlockCollider(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) {
    uint64_t handle = next_collider_handle_++;
    auto record = std::make_unique<SekiroBlockColliderRecord>();
    record->handle = handle;
    record->grid_pos = grid_pos;
    record->block_id = block_id;

    // Spawn Havok static box collider in Dantelion physics space
    auto box = std::make_unique<sekiro::native::HavokStaticBoxCollider>();
    box->Handle = handle;
    box->Center = toNativePoint(world_pos);
    box->HalfExtents = {0.49f, 0.49f, 0.49f}; // 1 m cube with 1 cm inset for seam-free collision (metres)
    box->bActive = true;

    record->collider = std::move(box);
    colliders_[handle] = std::move(record);
    return handle;
}

void SekiroAdapter::destroyBlockCollider(uint64_t collider_handle) {
    auto it = colliders_.find(collider_handle);
    if (it != colliders_.end()) {
        if (it->second->collider) {
            it->second->collider->bActive = false;
        }
        colliders_.erase(it);
    }
}

void SekiroAdapter::applyLinearImpulse(EntityId entity_id, const Vec3& impulse) {
    auto it = registered_entities_.find(entity_id);
    if (it != registered_entities_.end() && it->second) {
        it->second->ApplyImpulse(toNativePoint(impulse));
    }
}

void SekiroAdapter::setLinearVelocity(EntityId entity_id, const Vec3& velocity) {
    if (sekiro::native::ChrIns* chr = getRegisteredEntity(entity_id)) {
        chr->Velocity = toNativePoint(velocity);
    }
}

// =========================================================================
// IRenderAdapter Implementation
// =========================================================================

void SekiroAdapter::setNativePlayerVisible(bool visible) {
    if (!player_) {
        native_player_hidden_ = !visible;
        return;
    }

    if (!visible) {
        // Hide Dantelion character model while preserving Havok capsule physics
        cached_player_alpha_ = player_->ModelAlpha;
        player_->bModelHidden = true;
        player_->ModelAlpha = 0.0f;
        player_->bCapsulePhysicsActive = true;
        native_player_hidden_ = true;
    } else {
        // Restore Wolf model visibility
        player_->bModelHidden = false;
        player_->ModelAlpha = (cached_player_alpha_ > 0.0f) ? cached_player_alpha_ : 1.0f;
        native_player_hidden_ = false;
    }
}

bool SekiroAdapter::spawnSteveParts() {
    if (steve_parts_spawned_) {
        return true;
    }

    for (size_t i = 0; i < SteveAnimator::kPartCount; ++i) {
        auto part = std::make_unique<sekiro::native::SekiroVisualMeshComponent>();
        part->Handle = i + 1;
        part->ModelName = stevePartIndexToName(i);
        part->bVisible = true;
        part->Alpha = 1.0f;
        steve_parts_[i] = std::move(part);
    }

    // Main weapon and prosthetic off-hand visuals
    weapon_mesh_ = std::make_unique<sekiro::native::SekiroVisualMeshComponent>();
    weapon_mesh_->Handle = 100;
    weapon_mesh_->ModelName = itemIdToModelName(main_hand_item_);
    weapon_mesh_->bVisible = (main_hand_item_ != ItemId::None);

    prosthetic_offhand_mesh_ = std::make_unique<sekiro::native::SekiroVisualMeshComponent>();
    prosthetic_offhand_mesh_->Handle = 101;
    prosthetic_offhand_mesh_->ModelName = itemIdToModelName(off_hand_item_);
    prosthetic_offhand_mesh_->bVisible = (off_hand_item_ != ItemId::None);

    steve_parts_spawned_ = true;
    return true;
}

void SekiroAdapter::destroySteveParts() {
    if (!steve_parts_spawned_) {
        return;
    }

    weapon_mesh_.reset();
    prosthetic_offhand_mesh_.reset();

    for (size_t i = 0; i < SteveAnimator::kPartCount; ++i) {
        steve_parts_[i].reset();
    }
    steve_parts_spawned_ = false;
}

void SekiroAdapter::updateStevePartTransforms(const SteveAnimator::PartTransforms& transforms) {
    if (!steve_parts_spawned_) {
        return;
    }

    for (size_t i = 0; i < SteveAnimator::kPartCount; ++i) {
        if (steve_parts_[i]) {
            const auto& t = transforms[i];
            steve_parts_[i]->Position = toNativePoint(t.pos);
            const sekiro::native::FQuat q = toNativeQuat(t.rot);
            steve_parts_[i]->RotationQuat = q;
            // Debug-only Euler view of the converted quaternion (degrees)
            constexpr float kRadToDeg = 180.0f / 3.14159265f;
            const float pitch = std::atan2(2.0f * (q.W * q.X + q.Y * q.Z), 1.0f - 2.0f * (q.X * q.X + q.Y * q.Y)) * kRadToDeg;
            const float yaw = std::asin(std::clamp(2.0f * (q.W * q.Y - q.Z * q.X), -1.0f, 1.0f)) * kRadToDeg;
            const float roll = std::atan2(2.0f * (q.W * q.Z + q.X * q.Y), 1.0f - 2.0f * (q.Y * q.Y + q.Z * q.Z)) * kRadToDeg;
            steve_parts_[i]->RotationEuler = {pitch, yaw, roll};
        }
    }
}

void SekiroAdapter::setHeldItemVisual(ItemId item, bool is_offhand) {
    const char* model_name = itemIdToModelName(item);
    if (!is_offhand) {
        main_hand_item_ = item;
        if (weapon_mesh_) {
            weapon_mesh_->ModelName = model_name;
            weapon_mesh_->bVisible = (item != ItemId::None);
        }
    } else {
        off_hand_item_ = item;
        if (prosthetic_offhand_mesh_) {
            prosthetic_offhand_mesh_->ModelName = model_name;
            prosthetic_offhand_mesh_->bVisible = (item != ItemId::None);
        }
    }
}

uint64_t SekiroAdapter::spawnBlockVisual(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) {
    uint64_t handle = next_visual_handle_++;
    auto record = std::make_unique<SekiroBlockVisualRecord>();
    record->handle = handle;
    record->grid_pos = grid_pos;
    record->block_id = block_id;

    auto mesh = std::make_unique<sekiro::native::SekiroVisualMeshComponent>();
    mesh->Handle = handle;
    mesh->Position = toNativePoint(world_pos);
    mesh->ModelName = blockIdToModelName(block_id);
    mesh->bVisible = true;
    mesh->Alpha = 1.0f;

    record->mesh = std::move(mesh);
    visuals_[handle] = std::move(record);
    return handle;
}

void SekiroAdapter::setBlockCrackStage(uint64_t block_handle, int stage) {
    auto it = visuals_.find(block_handle);
    if (it != visuals_.end()) {
        it->second->crack_stage = stage;
        if (it->second->mesh) {
            std::string model = blockIdToModelName(it->second->block_id);
            if (stage >= 0 && stage <= 9) {
                model += "_crack_" + std::to_string(stage);
            }
            it->second->mesh->ModelName = model;
            it->second->mesh->CrackStage = stage;
        }
    }
}

void SekiroAdapter::destroyBlockVisual(uint64_t block_handle) {
    auto it = visuals_.find(block_handle);
    if (it != visuals_.end()) {
        if (it->second->mesh) {
            it->second->mesh->bVisible = false;
        }
        visuals_.erase(it);
    }
}

// =========================================================================
// ICombatAdapter Implementation
// =========================================================================

bool SekiroAdapter::processHit(const HitIntent& intent) {
    auto it = registered_entities_.find(intent.victim_id);
    if (it == registered_entities_.end() || !it->second) {
        return false;
    }

    sekiro::native::ChrIns* victim = it->second;
    if (victim->bIsDead) {
        return false;
    }

    // Damage calculation: flat damage or percentage of max health (Boss balance)
    float damage_to_apply = intent.damage;
    if (intent.max_hp_percent > 0.0f && victim->MaxHealth > 0.0f) {
        damage_to_apply = victim->MaxHealth * intent.max_hp_percent;
    }

    victim->Health = std::max(0.0f, victim->Health - damage_to_apply);

    // Sekiro specific: Accumulate Posture (躯干值) based on attack force & damage
    float posture_damage = std::max(10.0f, damage_to_apply * 0.25f + intent.knockback_force * 5.0f);
    victim->Posture = std::min(victim->MaxPosture, victim->Posture + posture_damage);

    // If Posture breaks (>= MaxPosture) or Health reaches 0, trigger Deathblow readiness (忍杀就绪)
    if (victim->Posture >= victim->MaxPosture || victim->Health <= 0.0f) {
        victim->bDeathblowReady = true;
    }

    if (victim->Health <= 0.0f) {
        victim->bIsDead = true;
    }

    // Stagger / knockback is triggered by CombatEngine::executeHit after this returns true.
    return true;
}

float SekiroAdapter::getMaxHealth(EntityId entity_id) {
    auto it = registered_entities_.find(entity_id);
    if (it != registered_entities_.end() && it->second) {
        return it->second->MaxHealth;
    }
    return 100.0f;
}

void SekiroAdapter::triggerStaggerOrRagdoll(EntityId entity_id, const Vec3& direction, float force) {
    auto it = registered_entities_.find(entity_id);
    if (it != registered_entities_.end() && it->second) {
        it->second->StaggerLevel = static_cast<int32_t>(std::clamp(force * 0.5f, 1.0f, 5.0f));
        // force is a cm/s impulse speed (same unit as projectile speeds)
        sekiro::native::FVector3 impulse = toNativePoint(direction.normalized() * force);
        it->second->ApplyImpulse(impulse);
    }
}

// =========================================================================
// IInputAdapter Implementation
// =========================================================================

ItemId SekiroAdapter::getEquippedMainHand() const {
    return main_hand_item_;
}

ItemId SekiroAdapter::getEquippedOffHand() const {
    return off_hand_item_;
}

Vec3 SekiroAdapter::getCameraPosition() const {
    if (camera_) {
        return toMcPoint(camera_->Position);
    }
    return Vec3{};
}

Vec3 SekiroAdapter::getCameraForward() const {
    if (camera_) {
        return toMcDir(camera_->Forward);
    }
    return Vec3{0.f, 1.f, 0.f};
}

Vec3 SekiroAdapter::getPlayerPosition() const {
    if (player_) {
        return toMcPoint(player_->Position);
    }
    return Vec3{};
}

Vec3 SekiroAdapter::getPlayerVelocity() const {
    if (player_) {
        return toMcPoint(player_->Velocity);
    }
    return Vec3{};
}

bool SekiroAdapter::isPlayerOnGround() const {
    constexpr float kAirborneVerticalSpeed = 2.0f; // m/s
    if (!player_) {
        return true;
    }
    return std::abs(player_->Velocity.Y) < kAirborneVerticalSpeed;
}

void SekiroAdapter::setEquippedItems(ItemId main, ItemId off) {
    setHeldItemVisual(main, false);
    setHeldItemVisual(off, true);
}

} // namespace mc::adapter
