#include "wukong_adapter.hpp"
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

} // namespace

WukongAdapter::WukongAdapter() = default;

WukongAdapter::WukongAdapter(b1::native::APlayerController* controller)
    : controller_(controller) {
    if (controller_ && controller_->ControlledPawn) {
        registerEntity(EntityId::LocalPlayer, controller_->ControlledPawn);
    }
}

WukongAdapter::~WukongAdapter() {
    if (steve_parts_spawned_) {
        destroySteveParts();
    }
    if (native_player_hidden_) {
        setNativePlayerVisible(true);
    }
    colliders_.clear();
    visuals_.clear();
}

void WukongAdapter::setPlayerController(b1::native::APlayerController* controller) {
    controller_ = controller;
    if (controller_ && controller_->ControlledPawn) {
        registerEntity(EntityId::LocalPlayer, controller_->ControlledPawn);
    }
}

void WukongAdapter::setWorld(b1::native::UWorld* world) {
    world_ = world;
}

bool WukongAdapter::registerEntity(EntityId entity_id, b1::native::ABGUCharacter* character) {
    if (entity_id == EntityId::None || !character) {
        return false;
    }
    registered_entities_[entity_id] = character;
    return true;
}

void WukongAdapter::unregisterEntity(EntityId entity_id) {
    registered_entities_.erase(entity_id);
}

// =========================================================================
// IPhysicsAdapter Implementation
// =========================================================================

RaycastResult WukongAdapter::raycastWorld(const Vec3& start, const Vec3& end, EntityId ignore_entity) {
    b1::native::FVector native_start = toNative(start);
    b1::native::FVector native_end = toNative(end);
    b1::native::FHitResult hit{};

    std::vector<b1::native::AActor*> actors_to_ignore;
    auto ign_it = registered_entities_.find(ignore_entity);
    if (ign_it != registered_entities_.end()) {
        actors_to_ignore.push_back(ign_it->second);
    }

    bool bHit = b1::native::UKismetSystemLibrary::LineTraceSingle(
        nullptr,
        native_start,
        native_end,
        b1::native::ETraceTypeQuery::TraceTypeQuery1,
        false,
        actors_to_ignore,
        b1::native::EDrawDebugTrace::None,
        hit,
        true
    );

    if (bHit) {
        RaycastResult res{};
        res.has_hit = true;
        res.point = toMc(hit.ImpactPoint);
        res.normal = toMc(hit.ImpactNormal);

        // Check if hit one of our placed block colliders
        for (const auto& [handle, record] : colliders_) {
            if (record->box_component == hit.Component || record->actor.get() == hit.Actor) {
                res.is_block = true;
                res.hit_collider_handle = handle;
                return res;
            }
        }

        // Check if hit a registered character
        for (const auto& [id, char_ptr] : registered_entities_) {
            if (char_ptr == hit.Actor) {
                res.is_block = false;
                res.hit_entity = id;
                return res;
            }
        }

        // Terrain or an actor we do not track: never leak a host pointer as an EntityId
        res.is_block = false;
        return res;
    }

    // Direct geometric probe against placed block colliders (AABB ray intersection)
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

uint64_t WukongAdapter::createBlockCollider(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) {
    uint64_t handle = next_collider_handle_++;
    auto record = std::make_unique<NativeBlockColliderRecord>();
    record->handle = handle;
    record->grid_pos = grid_pos;
    record->block_id = block_id;

    // Spawn native Actor and BoxComponent in UE5 world
    b1::native::FVector native_loc = toNative(world_pos);
    record->actor.reset(b1::native::UBGUFunctionLibrary::BGUSpawnActor(world_, native_loc, {0.f, 0.f, 0.f}));

    auto* root = new b1::native::USceneComponent();
    record->actor->RootComponent = root;
    b1::native::UGSE_EngineFuncLib::RegisterComponent(root);

    auto* box = new b1::native::UBoxComponent();
    box->AttachToComponent(root, b1::native::EAttachmentRule::KeepRelative);
    // Extent of 49x49x49 cm ensures 100cm cube with slight inset for seam-free collision
    box->SetBoxExtent({49.0f, 49.0f, 49.0f});
    box->SetCollisionObjectType(b1::native::ECollisionChannel::ECC_WorldDynamic);
    box->SetCollisionResponseToAllChannels(b1::native::ECollisionResponse::ECR_Block);
    box->SetCollisionEnabled(b1::native::ECollisionEnabled::QueryAndPhysics);
    b1::native::UGSE_EngineFuncLib::RegisterComponent(box);

    record->box_component = box;
    colliders_[handle] = std::move(record);
    return handle;
}

void WukongAdapter::destroyBlockCollider(uint64_t collider_handle) {
    auto it = colliders_.find(collider_handle);
    if (it != colliders_.end()) {
        if (it->second->box_component) {
            b1::native::UGSE_EngineFuncLib::UnregisterComponent(it->second->box_component);
            delete it->second->box_component;
            it->second->box_component = nullptr;
        }
        if (it->second->actor) {
            it->second->actor->Destroy();
        }
        colliders_.erase(it);
    }
}

void WukongAdapter::applyLinearImpulse(EntityId entity_id, const Vec3& impulse) {
    auto it = registered_entities_.find(entity_id);
    if (it != registered_entities_.end() && it->second) {
        b1::native::UBGUFunctionLibrary::BGUApplyImpulse(it->second, toNative(impulse));
    }
}

void WukongAdapter::setLinearVelocity(EntityId entity_id, const Vec3& velocity) {
    auto it = registered_entities_.find(entity_id);
    if (it != registered_entities_.end() && it->second) {
        // UE5 semantics: XY/Z override flags replace the component instead of adding to it
        it->second->LaunchCharacter(toNative(velocity), true, true);
    }
}

// =========================================================================
// IRenderAdapter Implementation
// =========================================================================

void WukongAdapter::setNativePlayerVisible(bool visible) {
    if (!controller_ || !controller_->ControlledPawn) {
        native_player_hidden_ = !visible;
        return;
    }

    auto* pawn = controller_->ControlledPawn;
    if (!visible) {
        // Hide mesh component while preserving UCapsuleComponent physical state
        if (pawn->MeshComponent) {
            hidden_components_cache_.emplace_back(pawn->MeshComponent, pawn->MeshComponent->bHiddenInGame);
            pawn->MeshComponent->SetHiddenInGame(true);
        }
        native_player_hidden_ = true;
    } else {
        // Restore visibility
        for (const auto& [comp, prev_hidden] : hidden_components_cache_) {
            if (comp) {
                comp->SetHiddenInGame(prev_hidden);
            }
        }
        hidden_components_cache_.clear();
        native_player_hidden_ = false;
    }
}

bool WukongAdapter::spawnSteveParts() {
    if (steve_parts_spawned_) {
        return true;
    }

    b1::native::USceneComponent* root = nullptr;
    if (controller_ && controller_->ControlledPawn) {
        root = controller_->ControlledPawn->RootComponent;
    }

    for (size_t i = 0; i < SteveAnimator::kPartCount; ++i) {
        auto part = std::make_unique<b1::native::UProceduralMeshComponent>();
        if (root) {
            part->AttachToComponent(root, b1::native::EAttachmentRule::KeepRelative);
        }
        part->SetCollisionEnabled(b1::native::ECollisionEnabled::NoCollision);
        b1::native::UGSE_EngineFuncLib::RegisterComponent(part.get());
        steve_parts_[i] = std::move(part);
    }

    // Weapons and offhand attachments
    weapon_mesh_ = std::make_unique<b1::native::UProceduralMeshComponent>();
    weapon_mesh_->SetCollisionEnabled(b1::native::ECollisionEnabled::NoCollision);
    if (steve_parts_[static_cast<size_t>(StevePart::RightArm)]) {
        weapon_mesh_->AttachToComponent(steve_parts_[static_cast<size_t>(StevePart::RightArm)].get());
    }
    b1::native::UGSE_EngineFuncLib::RegisterComponent(weapon_mesh_.get());

    shield_mesh_ = std::make_unique<b1::native::UProceduralMeshComponent>();
    shield_mesh_->SetCollisionEnabled(b1::native::ECollisionEnabled::NoCollision);
    if (steve_parts_[static_cast<size_t>(StevePart::LeftArm)]) {
        shield_mesh_->AttachToComponent(steve_parts_[static_cast<size_t>(StevePart::LeftArm)].get());
    }
    b1::native::UGSE_EngineFuncLib::RegisterComponent(shield_mesh_.get());

    steve_parts_spawned_ = true;
    return true;
}

void WukongAdapter::destroySteveParts() {
    if (!steve_parts_spawned_) {
        return;
    }

    if (weapon_mesh_) {
        b1::native::UGSE_EngineFuncLib::UnregisterComponent(weapon_mesh_.get());
        weapon_mesh_.reset();
    }
    if (shield_mesh_) {
        b1::native::UGSE_EngineFuncLib::UnregisterComponent(shield_mesh_.get());
        shield_mesh_.reset();
    }

    for (size_t i = 0; i < SteveAnimator::kPartCount; ++i) {
        if (steve_parts_[i]) {
            b1::native::UGSE_EngineFuncLib::UnregisterComponent(steve_parts_[i].get());
            steve_parts_[i].reset();
        }
    }
    steve_parts_spawned_ = false;
}

void WukongAdapter::updateStevePartTransforms(const SteveAnimator::PartTransforms& transforms) {
    if (!steve_parts_spawned_) {
        return;
    }

    for (size_t i = 0; i < SteveAnimator::kPartCount; ++i) {
        if (steve_parts_[i]) {
            const auto& t = transforms[i];
            b1::native::FVector loc = toNative(t.pos);
            steve_parts_[i]->SetRelativeLocationAndRotation(loc, toNativeRotator(t.rot));
        }
    }
}

void WukongAdapter::setHeldItemVisual(ItemId item, bool is_offhand) {
    const char* model_name = itemIdToModelName(item);
    if (!is_offhand) {
        main_hand_item_ = item;
        if (weapon_mesh_) {
            weapon_mesh_->SetMeshSection(0, model_name);
            weapon_mesh_->SetHiddenInGame(item == ItemId::None);
        }
    } else {
        off_hand_item_ = item;
        if (shield_mesh_) {
            shield_mesh_->SetMeshSection(0, model_name);
            shield_mesh_->SetHiddenInGame(item == ItemId::None);
        }
    }
}

uint64_t WukongAdapter::spawnBlockVisual(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) {
    uint64_t handle = next_visual_handle_++;
    auto record = std::make_unique<NativeBlockVisualRecord>();
    record->handle = handle;
    record->grid_pos = grid_pos;
    record->block_id = block_id;

    b1::native::FVector native_loc = toNative(world_pos);
    record->actor.reset(b1::native::UBGUFunctionLibrary::BGUSpawnActor(world_, native_loc, {0.f, 0.f, 0.f}));

    auto* mesh = new b1::native::UProceduralMeshComponent();
    mesh->SetMeshSection(0, blockIdToModelName(block_id));
    mesh->SetCollisionEnabled(b1::native::ECollisionEnabled::NoCollision);
    b1::native::UGSE_EngineFuncLib::RegisterComponent(mesh);

    record->mesh_component = mesh;
    visuals_[handle] = std::move(record);
    return handle;
}

void WukongAdapter::setBlockCrackStage(uint64_t block_handle, int stage) {
    auto it = visuals_.find(block_handle);
    if (it != visuals_.end()) {
        it->second->crack_stage = stage;
        if (it->second->mesh_component) {
            std::string model = blockIdToModelName(it->second->block_id);
            if (stage >= 0 && stage <= 9) {
                model += "_crack_" + std::to_string(stage);
            }
            it->second->mesh_component->SetMeshSection(0, model);
        }
    }
}

void WukongAdapter::destroyBlockVisual(uint64_t block_handle) {
    auto it = visuals_.find(block_handle);
    if (it != visuals_.end()) {
        if (it->second->mesh_component) {
            b1::native::UGSE_EngineFuncLib::UnregisterComponent(it->second->mesh_component);
            delete it->second->mesh_component;
            it->second->mesh_component = nullptr;
        }
        if (it->second->actor) {
            it->second->actor->Destroy();
        }
        visuals_.erase(it);
    }
}

// =========================================================================
// ICombatAdapter Implementation
// =========================================================================

bool WukongAdapter::processHit(const HitIntent& intent) {
    auto it = registered_entities_.find(intent.victim_id);
    if (it == registered_entities_.end() || !it->second) {
        return false;
    }

    b1::native::ABGUCharacter* victim = it->second;
    if (b1::native::UBGUFunctionLibrary::BGUIsUnitDead(victim)) {
        return false;
    }

    // Dynamic balance damage calculation
    float damage_to_apply = intent.damage;
    float max_hp = b1::native::UBGUFunctionLibrary::BGUGetFloatAttr(victim, b1::native::EBGUAttrFloat::HpMax);
    if (intent.max_hp_percent > 0.0f && max_hp > 0.0f) {
        damage_to_apply = max_hp * intent.max_hp_percent;
    }

    float current_hp = b1::native::UBGUFunctionLibrary::BGUGetFloatAttr(victim, b1::native::EBGUAttrFloat::Hp);
    float new_hp = std::max(0.0f, current_hp - damage_to_apply);
    b1::native::UBGUFunctionLibrary::BGUSetFloatAttr(victim, b1::native::EBGUAttrFloat::Hp, new_hp);

    if (new_hp <= 0.0f) {
        victim->bIsDead = true;
    }

    // Stagger / knockback is triggered by CombatEngine::executeHit after this returns true.
    return true;
}

float WukongAdapter::getMaxHealth(EntityId entity_id) {
    auto it = registered_entities_.find(entity_id);
    if (it != registered_entities_.end() && it->second) {
        return b1::native::UBGUFunctionLibrary::BGUGetFloatAttr(it->second, b1::native::EBGUAttrFloat::HpMax);
    }
    return 100.0f;
}

void WukongAdapter::triggerStaggerOrRagdoll(EntityId entity_id, const Vec3& direction, float force) {
    auto it = registered_entities_.find(entity_id);
    if (it != registered_entities_.end() && it->second) {
        it->second->LastStaggerLevel = static_cast<int32_t>(std::clamp(force * 0.5f, 1.0f, 5.0f));
        b1::native::FVector impulse = toNative(direction.normalized() * (force * 100.0f));
        b1::native::UBGUFunctionLibrary::BGUApplyImpulse(it->second, impulse);
    }
}

// =========================================================================
// IInputAdapter Implementation
// =========================================================================

ItemId WukongAdapter::getEquippedMainHand() const {
    return main_hand_item_;
}

ItemId WukongAdapter::getEquippedOffHand() const {
    return off_hand_item_;
}

Vec3 WukongAdapter::getCameraPosition() const {
    if (controller_ && controller_->PlayerCameraManager) {
        return toMc(controller_->PlayerCameraManager->GetCameraLocation());
    }
    return Vec3{};
}

Vec3 WukongAdapter::getCameraForward() const {
    if (controller_ && controller_->PlayerCameraManager) {
        b1::native::FRotator rot = controller_->PlayerCameraManager->GetCameraRotation();
        return toMc(rot.Vector());
    }
    return Vec3{0.f, 1.f, 0.f};
}

Vec3 WukongAdapter::getPlayerPosition() const {
    if (controller_ && controller_->ControlledPawn) {
        return toMc(controller_->ControlledPawn->GetActorLocation());
    }
    return Vec3{};
}

Vec3 WukongAdapter::getPlayerVelocity() const {
    if (controller_ && controller_->ControlledPawn) {
        return toMc(controller_->ControlledPawn->GetVelocity());
    }
    return Vec3{};
}

bool WukongAdapter::isPlayerOnGround() const {
    constexpr float kAirborneVerticalSpeed = 200.0f;
    if (!controller_ || !controller_->ControlledPawn) {
        return true;
    }
    return std::abs(controller_->ControlledPawn->GetVelocity().Z) < kAirborneVerticalSpeed;
}

const b1::native::UProceduralMeshComponent* WukongAdapter::getStevePartComponent(StevePart part) const {
    const size_t idx = static_cast<size_t>(part);
    return idx < steve_parts_.size() ? steve_parts_[idx].get() : nullptr;
}

b1::native::FRotator WukongAdapter::toNativeRotator(const Quat& q) {
    // Quaternion in UE axes: vector part = -M * v (M = diag(1,-1,1), det -1), w unchanged.
    const float x = -q.x;
    const float y = q.y;
    const float z = -q.z;
    const float w = q.w;
    constexpr float kRadToDeg = 180.0f / 3.14159265f;
    constexpr float kSingularity = 0.4999995f;

    // Mirrors FQuat::Rotator()
    const float singularity_test = z * x - w * y;
    const float yaw_y = 2.0f * (w * z + x * y);
    const float yaw_x = 1.0f - 2.0f * (y * y + z * z);

    auto wrap = [](float deg) {
        while (deg > 180.0f) deg -= 360.0f;
        while (deg < -180.0f) deg += 360.0f;
        return deg;
    };

    b1::native::FRotator r;
    if (singularity_test < -kSingularity) {
        r.Pitch = -90.0f;
        r.Yaw = std::atan2(yaw_y, yaw_x) * kRadToDeg;
        r.Roll = wrap(-r.Yaw - 2.0f * std::atan2(x, w) * kRadToDeg);
    } else if (singularity_test > kSingularity) {
        r.Pitch = 90.0f;
        r.Yaw = std::atan2(yaw_y, yaw_x) * kRadToDeg;
        r.Roll = wrap(r.Yaw - 2.0f * std::atan2(x, w) * kRadToDeg);
    } else {
        r.Pitch = std::asin(std::clamp(2.0f * singularity_test, -1.0f, 1.0f)) * kRadToDeg;
        r.Yaw = std::atan2(yaw_y, yaw_x) * kRadToDeg;
        r.Roll = std::atan2(-2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y)) * kRadToDeg;
    }
    return r;
}

void WukongAdapter::setEquippedItems(ItemId main, ItemId off) {
    setHeldItemVisual(main, false);
    setHeldItemVisual(off, true);
}

} // namespace mc::adapter
