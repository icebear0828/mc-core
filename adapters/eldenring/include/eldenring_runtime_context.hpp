#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

#include "eldenring_blocks.hpp"
#include "eldenring_creative.hpp"
#include "eldenring_damage.hpp"
#include "eldenring_hunger.hpp"
#include "eldenring_jump.hpp"
#include "eldenring_melee.hpp"
#include "eldenring_mining.hpp"
#include "eldenring_roll.hpp"
#include "eldenring_shield.hpp"
#include "eldenring_sprint.hpp"
#include "eldenring_survival.hpp"
#include "eldenring_tnt.hpp"
#include "eldenring_xp.hpp"
#include "mc/consumables.hpp"

namespace eldenring {

enum class InputCommandType : uint8_t {
    None = 0,
    ToggleMcMode,
    ToggleGameMode,
    ToggleFirstPerson,
    ToggleFlight,
    Jump,
    Sprint,
    Sneak,
    PlaceBlock,
    BreakBlock,
    AttackSwing,
    ShootBow,
    FireBullet,
    SummonEgg,
    SwapHand,
    HotbarSelect,
    Custom
};

struct InputCommand {
    InputCommandType type{InputCommandType::None};
    uint64_t tick{0};
    float x{0.f};
    float y{0.f};
    float z{0.f};
    int slot{0};
    uint32_t extra{0};
};

template <typename T, size_t Capacity>
class ThreadSafeCommandQueue {
public:
    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ >= Capacity) return false;
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        return true;
    }

    bool pop(T& out_item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == 0) return false;
        out_item = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

private:
    mutable std::mutex mutex_;
    std::array<T, Capacity> buffer_{};
    size_t head_{0};
    size_t tail_{0};
    size_t size_{0};
};

struct ModeConfig {
    std::atomic<bool> mc_mode{true};
    std::atomic<uint8_t> game_mode{static_cast<uint8_t>(creative::GameMode::Survival)};
    std::atomic<uint64_t> mode_toast_ms{0};
    std::atomic<bool> damage_enabled{false};
    std::atomic<bool> input_enabled{false};
    std::atomic<bool> click_attack{false};
    std::atomic<bool> steve_enabled{true};
    std::atomic<bool> hide_native{false};
    std::atomic<bool> first_person{false};
    std::atomic<float> eye_height{1.65f};
    std::atomic<bool> fp_persist{true};
    std::atomic<bool> shadow_enabled{true};
    std::atomic<bool> block_anchor{false}; // Always default to false to prevent coordinate lock
    std::atomic<bool> blocks_enabled{true};
    std::atomic<bool> block_drive{true};
    std::atomic<bool> block_ground{true};
    std::atomic<bool> block_collision{true};
    std::atomic<bool> enemy_block_collision{true};
    std::atomic<bool> block_supported{false};
    std::atomic<float> reach{4.5f};
    std::atomic<float> block_walk_speed{4.317f};
    std::atomic<float> sneak_speed{1.295f};
    std::atomic<float> sprint_speed{5.612f};
    std::atomic<int> sneak_vk{roll::kVkShift};
    std::atomic<bool> sneak_active{false};
    std::atomic<bool> instant_turn{true};
    std::atomic<float> turn_speed{15.0f};
    std::atomic<bool> disable_vanilla_roll{true};
    std::atomic<int> roll_vk{roll::kVkShift};
    std::atomic<float> damage_scale{20.0f};
    std::atomic<float> damage_hp_percent{0.02f};
    std::atomic<float> sweep_radius{3.0f};
    std::atomic<bool> require_victim_updating{false};
    std::atomic<float> sound_volume{0.8f};
    std::atomic<float> walk_speed{0.0f};
    std::atomic<bool> on_ground{true};
    std::atomic<bool> bullet_fire_pending{false};
    std::atomic<bool> bullet_fire_enabled{false};
    std::atomic<bool> summon_pending{false};
    std::atomic<int> heal_pending{0};
    std::atomic<bool> shield_blocking{false};
    std::atomic<bool> shield_offhand{false};
    std::atomic<bool> knockdown_fix{true};
    std::atomic<bool> los_enabled{false};
    std::atomic<bool> mc_jump{false};
    std::atomic<int> mc_jump_vk{0x20}; // VK_SPACE
    std::atomic<bool> flying{false};
    std::atomic<bool> bow_offhand{false};
    std::atomic<float> bow_power{0.f};
    std::atomic<float> bow_ticks{0.f};
};

struct PlayerSystems {
    live::SurvivalState survival;
    live::HungerModel hunger;
    live::XpModel xp;
    float hunger_heal_carry{0.f};
    mc::ConsumableSystem eating;
    live::JumpTracker jump;
    sprint::SprintController sprint;
    std::atomic<bool> sprint_active{false};
    std::atomic<float> sprint_fov{1.0f};
    std::atomic<float> sprint_anim{1.0f};
    std::atomic<float> ground_y{0.f};
    std::atomic<bool> ground_valid{false};
};

struct WorldSystems {
    std::mutex blocks_mutex;
    blocks::BlockGrid block_grid;
    tnt::ErTntManager tnt_mgr;
    mining::MiningController mining;
    unsigned blocks_sent_version{~0u};
};

struct CombatSystems {
    std::mutex melee_mutex;
    live::MeleeController melee;
    shield::ShieldController shield;
    live::DamageQueue damage_queue;
    live::HitFeedback feedback;
    std::optional<live::HitTemplate> hit_template;
};

struct UiSystems {
    std::atomic<bool> inv_open{false};
    std::atomic<float> inv_mx{0.f};
    std::atomic<float> inv_my{0.f};
    std::atomic<int> inv_vk{'I'};
    std::atomic<float> inv_sens{1.f};
    std::atomic<unsigned> isblocked_forced{0};
    std::atomic<unsigned> menufreeze_forced{0};
    std::atomic<int> slot_probe{-1};
    std::atomic<uint32_t> hide_slots{0xFFFFFFFFu};
    std::atomic<uint32_t> hide_mask1{0x100A1};
    std::atomic<uint32_t> hide_mask2{1};
    std::atomic<bool> slots_changed{false};
    std::atomic<bool> native_hide_wanted{false};
    std::atomic<unsigned> native_reshown{0};
    std::atomic<bool> summon_hide{false};
    std::atomic<float> mob_yaw_offset{0.f};
    std::atomic<float> steve_yaw_offset{3.14159265f};
    std::atomic<bool> no_player_hit_vfx{false};
    std::atomic<float> kb_force{-1.f};
};

class RuntimeContext {
public:
    static RuntimeContext& Instance() {
        static RuntimeContext instance;
        return instance;
    }

    ModeConfig& mode() { return mode_; }
    const ModeConfig& mode() const { return mode_; }

    PlayerSystems& player() { return player_; }
    const PlayerSystems& player() const { return player_; }

    WorldSystems& world() { return world_; }
    const WorldSystems& world() const { return world_; }

    CombatSystems& combat() { return combat_; }
    const CombatSystems& combat() const { return combat_; }

    UiSystems& ui() { return ui_; }
    const UiSystems& ui() const { return ui_; }

    creative::GameMode currentGameMode() const {
        return static_cast<creative::GameMode>(mode_.game_mode.load(std::memory_order_relaxed));
    }

    bool enqueueCommand(const InputCommand& cmd) {
        return command_queue_.push(cmd);
    }

    bool dequeueCommand(InputCommand& out_cmd) {
        return command_queue_.pop(out_cmd);
    }

    void resetForTesting() {
        // Reset ModeConfig
        mode_.mc_mode.store(true);
        mode_.game_mode.store(static_cast<uint8_t>(creative::GameMode::Survival));
        mode_.mode_toast_ms.store(0);
        mode_.damage_enabled.store(false);
        mode_.input_enabled.store(false);
        mode_.click_attack.store(false);
        mode_.steve_enabled.store(true);
        mode_.hide_native.store(false);
        mode_.first_person.store(false);
        mode_.eye_height.store(1.65f);
        mode_.fp_persist.store(true);
        mode_.shadow_enabled.store(true);
        mode_.block_anchor.store(false);
        mode_.blocks_enabled.store(true);
        mode_.block_drive.store(true);
        mode_.block_ground.store(true);
        mode_.block_collision.store(true);
        mode_.enemy_block_collision.store(true);
        mode_.block_supported.store(false);
        mode_.reach.store(4.5f);
        mode_.block_walk_speed.store(4.317f);
        mode_.sneak_speed.store(1.295f);
        mode_.sprint_speed.store(5.612f);
        mode_.sneak_vk.store(roll::kVkShift);
        mode_.sneak_active.store(false);
        mode_.instant_turn.store(true);
        mode_.turn_speed.store(15.0f);
        mode_.disable_vanilla_roll.store(true);
        mode_.roll_vk.store(roll::kVkShift);
        mode_.damage_scale.store(20.0f);
        mode_.damage_hp_percent.store(0.02f);
        mode_.sweep_radius.store(3.0f);
        mode_.require_victim_updating.store(false);
        mode_.sound_volume.store(0.8f);
        mode_.walk_speed.store(0.0f);
        mode_.on_ground.store(true);
        mode_.bullet_fire_pending.store(false);
        mode_.bullet_fire_enabled.store(false);
        mode_.summon_pending.store(false);
        mode_.heal_pending.store(0);
        mode_.shield_blocking.store(false);
        mode_.shield_offhand.store(false);
        mode_.knockdown_fix.store(true);
        mode_.los_enabled.store(false);

        // Reset Player
        player_.survival = live::SurvivalState{};
        player_.hunger = live::HungerModel{};
        player_.xp = live::XpModel{};
        player_.hunger_heal_carry = 0.f;
        player_.eating = mc::ConsumableSystem{};
        player_.jump = live::JumpTracker{};
        player_.sprint = sprint::SprintController{};
        player_.sprint_active.store(false);
        player_.sprint_fov.store(1.0f);
        player_.sprint_anim.store(1.0f);
        player_.ground_y.store(0.f);
        player_.ground_valid.store(false);

        // Reset World
        {
            std::lock_guard<std::mutex> g(world_.blocks_mutex);
            world_.block_grid = blocks::BlockGrid{};
            world_.tnt_mgr = tnt::ErTntManager{};
            world_.mining.reset();
            world_.blocks_sent_version = ~0u;
        }

        // Reset Combat
        {
            std::lock_guard<std::mutex> g(combat_.melee_mutex);
            combat_.melee = live::MeleeController{};
            combat_.shield = shield::ShieldController{};
            combat_.damage_queue.clear();
            combat_.feedback = live::HitFeedback{};
            combat_.hit_template = std::nullopt;
        }

        // Reset Ui
        ui_.inv_open.store(false);
        ui_.inv_mx.store(0.f);
        ui_.inv_my.store(0.f);
        ui_.inv_vk.store('I');
        ui_.inv_sens.store(1.f);
        ui_.isblocked_forced.store(0);
        ui_.menufreeze_forced.store(0);
        ui_.slot_probe.store(-1);
        ui_.hide_slots.store(0xFFFFFFFFu);
        ui_.hide_mask1.store(0x100A1);
        ui_.hide_mask2.store(1);
        ui_.slots_changed.store(false);
        ui_.native_hide_wanted.store(false);
        ui_.native_reshown.store(0);
        ui_.summon_hide.store(false);
        ui_.mob_yaw_offset.store(0.f);
        ui_.steve_yaw_offset.store(3.14159265f);
        ui_.no_player_hit_vfx.store(false);
        ui_.kb_force.store(-1.f);

        // Clear Command Queue
        command_queue_.clear();
    }

private:
    RuntimeContext() = default;
    ModeConfig mode_;
    PlayerSystems player_;
    WorldSystems world_;
    CombatSystems combat_;
    UiSystems ui_;
    ThreadSafeCommandQueue<InputCommand, 256> command_queue_;
};

} // namespace eldenring
