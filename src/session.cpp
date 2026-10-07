#include "mc/session.hpp"

#include "mc/ballistics.hpp"
#include "mc/combat.hpp"
#include "mc/consumables.hpp"
#include "mc/contracts/combat_adapter.hpp"
#include "mc/contracts/input_adapter.hpp"
#include "mc/contracts/physics_adapter.hpp"
#include "mc/contracts/render_adapter.hpp"
#include "mc/elytra.hpp"
#include "mc/hud.hpp"
#include "mc/voxel_world.hpp"

#include <algorithm>
#include <cmath>

namespace mc {

namespace {

constexpr float kCmPerMeter = 100.f;
constexpr float kPi = 3.14159265358979f;

float wrapPi(float a) {
    while (a > kPi) a -= 2.f * kPi;
    while (a < -kPi) a += 2.f * kPi;
    return a;
}
constexpr float kFireworkBoostSec = 2.0f;

BlockId blockForItem(ItemId item) {
    switch (item) {
        case ItemId::BlockDirt: return BlockId::Dirt;
        case ItemId::BlockStone: return BlockId::Stone;
        case ItemId::BlockTnt: return BlockId::Tnt;
        default: return BlockId::Air;
    }
}

} // namespace

Session::Session(const Ports& ports)
    : ports_(ports),
      animator_(std::make_unique<SteveAnimator>()),
      voxel_(std::make_unique<VoxelWorld>(ports.physics, ports.render)),
      combat_(std::make_unique<CombatEngine>(ports.combat)),
      ballistics_(std::make_unique<BallisticsEngine>(ports.physics)),
      consumables_(std::make_unique<ConsumableSystem>()),
      elytra_(std::make_unique<ElytraFlight>(ports.physics)),
      hud_(std::make_unique<HudEngine>()) {}

Session::~Session() {
    setActive(false);
}

void Session::setActive(bool active) {
    if (active == active_) {
        return;
    }
    active_ = active;
    if (active_) {
        ports_.render.setNativePlayerVisible(false);
        ports_.render.spawnSteveParts();
        ports_.render.setHeldItemVisual(hud_->getSelectedItem(), false);
    } else {
        consumables_->cancel();
        elytra_->stopGliding();
        swing_elapsed_ = -1.f;
        body_yaw_seeded_ = false;
        ports_.render.destroySteveParts();
        ports_.render.setNativePlayerVisible(true);
    }
}

void Session::loadDefaultHotbar() {
    hud_->setSlot(0, ItemId::DiamondSword, 1);
    hud_->setSlot(1, ItemId::DiamondPickaxe, 1);
    hud_->setSlot(2, ItemId::BlockDirt, 64);
    hud_->setSlot(3, ItemId::BlockStone, 64);
    hud_->setSlot(4, ItemId::BlockTnt, 16);
    hud_->setSlot(5, ItemId::GoldenApple, 8);
    hud_->setSlot(6, ItemId::Bow, 1);
    hud_->setSlot(7, ItemId::Elytra, 1);
    hud_->setSlot(8, ItemId::TotemOfUndying, 1);
    hud_->selectSlot(0);
    consumables_->cancel();
    if (active_) {
        ports_.render.setHeldItemVisual(hud_->getSelectedItem(), false);
    }
}

void Session::consumeSelectedOne() {
    const int slot = hud_->getSelectedSlot();
    const HudSlot s = hud_->getSlot(slot);
    if (s.count <= 1) {
        hud_->setSlot(slot, ItemId::None, 0);
        ports_.render.setHeldItemVisual(ItemId::None, false);
    } else {
        hud_->setSlot(slot, s.item, s.count - 1);
    }
}

void Session::tick(float dt, const InputSnapshot& in) {
    if (!active_) {
        return;
    }

    // --- Hotbar -----------------------------------------------------------
    const int prev_slot = hud_->getSelectedSlot();
    if (in.hotbar_select >= 0) {
        hud_->selectSlot(in.hotbar_select);
    }
    if (in.scroll != 0) {
        hud_->scrollSlot(in.scroll);
    }
    if (hud_->getSelectedSlot() != prev_slot) {
        consumables_->cancel();
        ports_.render.setHeldItemVisual(hud_->getSelectedItem(), false);
    }
    const ItemId held = hud_->getSelectedItem();

    // --- Frame state in canonical MC space --------------------------------
    Vec3 fwd = ports_.input.getCameraForward().normalized();
    if (fwd.lengthSq() < 0.5f) {
        fwd = {1.f, 0.f, 0.f};
    }
    const Vec3 cam_pos = ports_.input.getCameraPosition();
    const Vec3 player_pos = ports_.input.getPlayerPosition();
    const Vec3 player_vel = ports_.input.getPlayerVelocity();
    const float yaw = std::atan2(fwd.y, fwd.x);
    const float pitch = std::asin(std::clamp(fwd.z, -1.f, 1.f));

    attack_cooldown_ = std::min(1.f, attack_cooldown_ + dt / kAttackRechargeSec);

    // --- Target probe (only when an action needs it) ----------------------
    RaycastResult target{};
    if (in.attack_pressed || in.attack_held || in.use_pressed) {
        target = ports_.physics.raycastWorld(cam_pos, cam_pos + fwd * kReachCm, EntityId::LocalPlayer);
    }
    const VoxelBlock* target_voxel = nullptr;
    GridPos target_grid{};
    if (target.has_hit) {
        target_grid = VoxelWorld::worldToGrid(target.point - target.normal * (VoxelWorld::kBlockSizeCm * 0.5f));
        target_voxel = voxel_->getBlock(target_grid);
    }

    // --- Attack / mine ----------------------------------------------------
    if (in.attack_pressed) {
        swing_elapsed_ = 0.f;
        if (target.has_hit && target_voxel == nullptr && !target.is_block &&
            target.hit_entity != EntityId::None && target.hit_entity != EntityId::LocalPlayer) {
            const bool falling = !in.on_ground && player_vel.z < 0.f;
            const HitIntent intent = combat_->calculateMeleeHit(
                EntityId::LocalPlayer, target.hit_entity, held, attack_cooldown_,
                falling, in.on_ground, target.point, fwd);
            combat_->executeHit(intent);
        }
        attack_cooldown_ = 0.f;
    }
    if (in.attack_held && target_voxel != nullptr) {
        voxel_->mineBlock(target_grid, held, dt);
    }

    // --- Use (place / eat / shoot / boost) --------------------------------
    if (in.use_pressed) {
        const uint32_t count = hud_->getSelectedSlotCount();
        const BlockId block = blockForItem(held);
        if (block != BlockId::Air) {
            if (count > 0) {
                if (const auto grid = VoxelWorld::calculatePlacementTarget(target)) {
                    if (voxel_->placeBlock(*grid, block, player_pos)) {
                        consumeSelectedOne();
                    }
                }
            }
        } else if (ConsumableSystem::isEdible(held)) {
            if (count > 0 && !consumables_->isEating()) {
                consumables_->startEating(held);
            }
        } else if (held == ItemId::Bow) {
            ballistics_->launch(ProjectileType::Arrow, cam_pos, fwd, 1.0f);
        } else if (held == ItemId::FireworkRocket) {
            if (count > 0 && elytra_->useFireworkBoost(kFireworkBoostSec)) {
                consumeSelectedOne();
            }
        }
    }

    // --- Eating -----------------------------------------------------------
    const EatingEvent eat = consumables_->update(dt);
    if (eat.completed) {
        consumeSelectedOne();
        hud_->setHealth(std::min(hud_->getMaxHealth(), hud_->getHealth() + eat.health_restored));
    }

    // --- Elytra -----------------------------------------------------------
    if (in.glide_toggle) {
        if (elytra_->getState().is_gliding) {
            elytra_->stopGliding();
        } else {
            elytra_->startGliding(in.on_ground, player_vel);
        }
    }
    if (elytra_->getState().is_gliding) {
        const bool grounded = in.on_ground && !in.on_ground_is_estimate;
        const FlightStepResult flight = elytra_->update(dt, player_pos, pitch, yaw, grounded);
        if (flight.crashed && flight.kinetic_damage > 0.f) {
            HitIntent crash;
            crash.attacker_id = EntityId::None;
            crash.victim_id = EntityId::LocalPlayer;
            crash.damage = flight.kinetic_damage;
            crash.hit_location = flight.new_position;
            combat_->executeHit(crash);
        }
        // Still airborne: the elytra, not the host's walking physics, owns the player's motion.
        if (elytra_->getState().is_gliding) {
            ports_.physics.setLinearVelocity(EntityId::LocalPlayer, elytra_->getState().velocity);
        }
    }

    // --- Swing ------------------------------------------------------------
    float swing_progress = 0.f;
    if (swing_elapsed_ >= 0.f) {
        swing_elapsed_ += dt;
        if (swing_elapsed_ >= kSwingDurationSec) {
            swing_elapsed_ = -1.f;
        } else {
            swing_progress = swing_elapsed_ / kSwingDurationSec;
        }
    }

    // --- Animation + projectiles -----------------------------------------
    const Vec3 right{std::sin(yaw), -std::cos(yaw), 0.f};
    const Vec3 forward_h{std::cos(yaw), std::sin(yaw), 0.f};
    SteveAnimInput anim{};
    anim.forward_speed = (player_vel.x * forward_h.x + player_vel.y * forward_h.y) / kCmPerMeter;
    anim.strafe_speed = (player_vel.x * right.x + player_vel.y * right.y) / kCmPerMeter;
    // Minecraft turns the head freely up to 50 degrees off the body's heading, then drags the body along.
    float host_yaw = 0.f;
    if (ports_.input.getPlayerFacingYaw(host_yaw) && std::isfinite(host_yaw)) {
        // The native character owns its heading; only the head is ours.
        body_yaw_ = wrapPi(host_yaw);
        body_yaw_seeded_ = true;
        anim.look_yaw = std::clamp(wrapPi(yaw - body_yaw_), -kMaxHeadYawRad, kMaxHeadYawRad);
    } else {
        if (!body_yaw_seeded_) {
            body_yaw_ = yaw;
            body_yaw_seeded_ = true;
        }
        const float head_offset = wrapPi(yaw - body_yaw_);
        if (std::fabs(head_offset) > kMaxHeadYawRad) {
            body_yaw_ = wrapPi(body_yaw_ + head_offset - std::copysign(kMaxHeadYawRad, head_offset));
        }
        anim.look_yaw = wrapPi(yaw - body_yaw_);
    }
    anim.look_pitch = -pitch; // our canonical pitch is positive-up; the rig expects positive-down
    anim.swing_progress = swing_progress;
    anim.eating_progress = consumables_->getProgress();
    anim.is_gliding = elytra_->getState().is_gliding;
    anim.roll_angle = elytra_->getState().bank_roll;
    last_anim_input_ = anim;

    animator_->update(dt, anim);
    ports_.render.setSteveRoot(player_pos, body_yaw_);
    ports_.render.updateStevePartTransforms(animator_->getTransforms());
    ballistics_->update(dt, player_pos);
}

} // namespace mc
