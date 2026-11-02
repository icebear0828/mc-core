#include "eldenring_input_handlers.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_runtime_context.hpp"
#include "input_keys.hpp"

#include <cmath>
#include <cstring>
#include <mutex>

namespace eldenring::input {

void McMovementHandler::reset() {
    prev_space_ = false;
}

void McMovementHandler::tickSprint(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();
    auto& player = runtime.player();
    auto& combat = runtime.combat();

    // MC only: OW/CS do not own the Steve sprint state.
    if (ctx.mc_on && ctx.fg) {
        const bool key_w = queryKeyDown(*ctx.cb, 'W');
        const bool key_s = queryKeyDown(*ctx.cb, 'S');
        const bool key_ctrl = queryKeyDown(*ctx.cb, VK_CONTROL);
        int food_lvl = 20;
        {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            food_lvl = player.hunger.food();
        }
        const bool blocking = mode.shield_blocking.load(std::memory_order_relaxed);
        player.sprint.update(ctx.dt, key_w, key_s, key_ctrl, food_lvl, blocking);
    } else {
        player.sprint.reset();
    }
    player.sprint_active.store(player.sprint.isSprinting(), std::memory_order_relaxed);
    player.sprint_fov.store(player.sprint.fovMultiplier(), std::memory_order_relaxed);
    player.sprint_anim.store(player.sprint.animSpeedMultiplier(), std::memory_order_relaxed);
}

void McMovementHandler::tickFootsteps(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();

    if (!(ctx.mc_on && ctx.fg && ctx.cb->get_player_chr && ctx.cb->read_physics_pos)) {
        return;
    }
    const uintptr_t player_chr = ctx.cb->get_player_chr();
    float pos[3] = {};
    if (player_chr != 0 && ctx.cb->read_physics_pos(player_chr, pos)) {
        if (have_prev_ && ctx.dt > 0.f) {
            const float dx = pos[0] - prev_pos_[0], dz = pos[2] - prev_pos_[2];
            const float step_len = std::sqrt(dx * dx + dz * dz);
            const bool grounded = ctx.cb->is_player_airborne ? !ctx.cb->is_player_airborne() : true;
            mode.walk_speed.store(step_len < 1.5f ? step_len / ctx.dt : 0.f);
            mode.on_ground.store(grounded);
            if (step_len < 1.5f && steps_.update(step_len / ctx.dt, ctx.dt, grounded) && ctx.want_suppress) {
                if (ctx.cb->play_sound) ctx.cb->play_sound("block.grass.step", 0.9f);
            }
            if (step_len >= 1.5f) steps_ = audio::StepClock{};
        }
        std::memcpy(prev_pos_, pos, sizeof(pos));
        have_prev_ = true;
    } else {
        have_prev_ = false;
    }
}

void McMovementHandler::tickJump(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& player = runtime.player();
    auto& combat = runtime.combat();

    const bool space = ctx.fg && queryKeyDown(*ctx.cb, VK_SPACE);
    const bool airborne = ctx.cb->is_player_airborne ? ctx.cb->is_player_airborne() : false;
    player.jump.update(ctx.dt, space && !prev_space_, ctx.mc_on && airborne);
    if (space && !prev_space_ && ctx.mc_on && !airborne) {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        player.hunger.addExhaustion(0.05f);
    }
    prev_space_ = space;
}

} // namespace eldenring::input
