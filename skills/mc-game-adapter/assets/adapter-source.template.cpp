#include "{{GAME_NAME_LOWER}}_adapter.hpp"

namespace mc::adapter {

{{GAME_NAME}}Adapter::{{GAME_NAME}}Adapter() = default;
{{GAME_NAME}}Adapter::~{{GAME_NAME}}Adapter() = default;

// =========================================================================
// IPhysicsAdapter Implementation
// =========================================================================

RaycastResult {{GAME_NAME}}Adapter::raycastWorld(const Vec3& start, const Vec3& end, EntityId ignore_entity) {
    // TODO: Call native game engine raycast API (e.g. RAGE ShapeTest / REDengine SpatialQueries)
    (void)start; (void)end; (void)ignore_entity;
    return RaycastResult{};
}

uint64_t {{GAME_NAME}}Adapter::createBlockCollider(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) {
    // TODO: Spawn physical prop or register static physics box in engine
    (void)grid_pos; (void)block_id; (void)world_pos;
    return 0;
}

void {{GAME_NAME}}Adapter::destroyBlockCollider(uint64_t collider_handle) {
    // TODO: Remove physical collider from engine
    (void)collider_handle;
}

void {{GAME_NAME}}Adapter::applyLinearImpulse(EntityId entity_id, const Vec3& impulse) {
    // TODO: Apply physics force to target entity
    (void)entity_id; (void)impulse;
}

void {{GAME_NAME}}Adapter::setLinearVelocity(EntityId entity_id, const Vec3& velocity) {
    // TODO: REPLACE (do not add to) the entity's velocity; velocity is canonical MC space, cm/s
    (void)entity_id; (void)velocity;
}

// =========================================================================
// IRenderAdapter Implementation
// =========================================================================

void {{GAME_NAME}}Adapter::setNativePlayerVisible(bool visible) {
    // TODO: Call native hide mesh API (e.g. ENTITY::SET_ENTITY_VISIBLE)
    (void)visible;
}

bool {{GAME_NAME}}Adapter::spawnSteveParts() {
    // TODO: Spawn 12 cooked mesh parts and attach to player root
    return true;
}

void {{GAME_NAME}}Adapter::destroySteveParts() {
    // TODO: Destroy 12 mesh parts
}

void {{GAME_NAME}}Adapter::updateStevePartTransforms(const SteveAnimator::PartTransforms& transforms) {
    // TODO: Write relative rotations and pivots to each of the 12 parts
    (void)transforms;
}

void {{GAME_NAME}}Adapter::setHeldItemVisual(ItemId item, bool is_offhand) {
    // TODO: Attach/detach held item model to hand socket
    (void)item; (void)is_offhand;
}

uint64_t {{GAME_NAME}}Adapter::spawnBlockVisual(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) {
    // TODO: Spawn block visual model in world
    (void)grid_pos; (void)block_id; (void)world_pos;
    return 0;
}

void {{GAME_NAME}}Adapter::setBlockCrackStage(uint64_t block_handle, int stage) {
    // TODO: Switch crack stage material / overlay (0..9)
    (void)block_handle; (void)stage;
}

void {{GAME_NAME}}Adapter::destroyBlockVisual(uint64_t block_handle) {
    // TODO: Delete block visual model from world
    (void)block_handle;
}

// =========================================================================
// ICombatAdapter Implementation
// =========================================================================

bool {{GAME_NAME}}Adapter::processHit(const HitIntent& intent) {
    // TODO: Apply damage / death / posture to the host entity.
    // Do NOT trigger knockback or stagger here: CombatEngine::executeHit calls
    // triggerStaggerOrRagdoll() exactly once after this returns true.
    (void)intent;
    return true;
}

float {{GAME_NAME}}Adapter::getMaxHealth(EntityId entity_id) {
    // TODO: Read max health from entity state
    (void)entity_id;
    return 100.0f;
}

void {{GAME_NAME}}Adapter::triggerStaggerOrRagdoll(EntityId entity_id, const Vec3& direction, float force) {
    // TODO: Trigger native stagger animation or ragdoll
    (void)entity_id; (void)direction; (void)force;
}

// =========================================================================
// IInputAdapter Implementation
// =========================================================================

ItemId {{GAME_NAME}}Adapter::getEquippedMainHand() const {
    // TODO: Check weapon slot or custom mod inventory
    return ItemId::None;
}

ItemId {{GAME_NAME}}Adapter::getEquippedOffHand() const {
    return ItemId::None;
}

Vec3 {{GAME_NAME}}Adapter::getCameraPosition() const {
    // TODO: Query camera matrix
    return Vec3{};
}

Vec3 {{GAME_NAME}}Adapter::getCameraForward() const {
    // TODO: Query camera forward vector
    return Vec3{0.f, 1.f, 0.f};
}

Vec3 {{GAME_NAME}}Adapter::getPlayerPosition() const {
    // TODO: Query player ped/puppet position
    return Vec3{};
}

Vec3 {{GAME_NAME}}Adapter::getPlayerVelocity() const {
    // TODO: Query player velocity
    return Vec3{};
}

} // namespace mc::adapter
