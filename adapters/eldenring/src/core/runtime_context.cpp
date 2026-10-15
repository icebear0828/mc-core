#include "eldenring_runtime_context.hpp"

namespace eldenring {

RuntimeContext& RuntimeContext::Instance() {
    static RuntimeContext instance;
    return instance;
}

void RuntimeContext::resetForTesting() {
    // Reset ModeConfig
    mode_.mc_mode.store(false);
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
        combat_.shield.reset();
        combat_.damage_queue.clear();
        combat_.feedback = live::HitFeedback{};
        combat_.hit_template = std::nullopt;
    }

    // Clear Command Queue
    command_queue_.clear();
}

} // namespace eldenring
