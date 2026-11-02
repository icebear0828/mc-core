#include "eldenring_input_handlers.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_ow_dash.hpp"
#include "eldenring_ow_feel.hpp"
#include "ow_viewmodel.hpp"
#include "input_keys.hpp"

#include <algorithm>

namespace eldenring::input {

void OwHeroSkillHandler::reset() {
    prev_ow_e_ = false;
    prev_ow_q_ = false;
    ow_punch_released_ = false;
    ow_fire_cooldown_ = 0.f;
    ow_punch_cooldown_left_ = 0.f;
    ow_uppercut_cooldown_left_ = 0.f;
    ow_slam_cooldown_left_ = 0.f;
    ow_hitstop_left_ = 0.f;
    ow_since_shot_ = 10.f;
}

bool OwHeroSkillHandler::tickRightClick(const InputTickContext& ctx) {
#if ENABLE_OW_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    if (mode.ow_mode.load() && ctx.fg && !ctx.is_dead && ctx.right_edge &&
        eldenring::ow::owReady(ow_punch_cooldown_left_)) {
        // RMB: Rocket Punch charge. The authored controller starts the charge
        // on press and fires the straight punch on release, so the state stays
        // in the windup until the button comes up (see the timer block).
        mode.ow_state.store(3);
        mode.ow_anim_time.store(0.f);
        mode.ow_action_variant.store(static_cast<uint8_t>((mode.ow_action_variant.load() + 1u) & 1u));
        ow_punch_released_ = false;
        ow_charge_looping_ = true;
        if (ctx.cb->play_sound_loop) ctx.cb->play_sound_loop("ow.punch.charge", 1.0f);
        else if (ctx.cb->play_sound) ctx.cb->play_sound("ow.punch.charge", 1.0f);
        return true;
    }
#else
    (void)ctx;
#endif
    return false;
}

void OwHeroSkillHandler::stopChargeLoop(const HotkeyCallbacks& cb) {
    if (!ow_charge_looping_) {
        return;
    }
    ow_charge_looping_ = false;
    if (cb.stop_sound_loop) cb.stop_sound_loop("ow.punch.charge");
}

void OwHeroSkillHandler::resetRuntimeForEnable() {
    ow_fire_cooldown_ = 0.f;
    ow_punch_cooldown_left_ = 0.f;
    ow_uppercut_cooldown_left_ = 0.f;
    ow_slam_cooldown_left_ = 0.f;
    ow_hitstop_left_ = 0.f;
    ow_since_shot_ = 10.f;
    ow_slam_impact_played_ = false;
    ow_charge_looping_ = false;
}

void OwHeroSkillHandler::tickActionTimer(const InputTickContext& ctx) {
#if ENABLE_OW_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    if (!mode.ow_mode.load()) {
        return;
    }
    ow_fire_cooldown_ = std::max(0.f, ow_fire_cooldown_ - ctx.dt);
    ow_punch_cooldown_left_ = std::max(0.f, ow_punch_cooldown_left_ - ctx.dt);
    ow_uppercut_cooldown_left_ = std::max(0.f, ow_uppercut_cooldown_left_ - ctx.dt);
    ow_slam_cooldown_left_ = std::max(0.f, ow_slam_cooldown_left_ - ctx.dt);
    ow_since_shot_ += ctx.dt;
    mode.ow_since_shot.store(ow_since_shot_);

    const uint8_t state = mode.ow_state.load();
    const float prev_t = mode.ow_anim_time.load();
    // Idle keeps its authored loop rate; attacks honour ow_anim_speed and
    // freeze for ow_hitstop seconds when the strike connects.
    float step = (state == 0) ? ctx.dt : ctx.dt * mode.ow_anim_speed.load();
    if (ow_hitstop_left_ > 0.f) {
        ow_hitstop_left_ = std::max(0.f, ow_hitstop_left_ - ctx.dt);
        step = 0.f;
    }
    const float t = prev_t + step;
    if ((state != 3 || ctx.is_dead || !ctx.fg) && ow_charge_looping_) {
        stopChargeLoop(*ctx.cb);
    }
    if (state == 5 && !ow_slam_impact_played_ && t >= 1.45f) {
        ow_slam_impact_played_ = true;
        if (ctx.cb->play_sound) ctx.cb->play_sound("ow.slam.impact", 1.0f);
    }
    const bool holding = ctx.cb->right_held ? ctx.cb->right_held() : true;
    float max_t = 0.f;
    if (state != 0) {
        const char* clip = eldenring::ow::owClipNameForState(state);
        if (ctx.cb->ow_clip_duration && clip != nullptr) max_t = ctx.cb->ow_clip_duration(clip);
        if (max_t <= 0.f) max_t = eldenring::ow::owClipDurationFallback(state);
    }

    if (state == 3) {
        // Authored controller flow: hold RMB to coil the punch, release to
        // fire. The windup holds at the release frame while the button is
        // down; letting go jumps to that frame and plays the strike.
        const float release_time = eldenring::ow::kRocketPunchReleaseTime;
        if (!ow_punch_released_) {
            if (holding) {
                mode.ow_anim_time.store(std::min(t, release_time));
            } else {
                mode.ow_anim_time.store(release_time);
                ow_punch_released_ = true;
                ow_punch_cooldown_left_ = mode.ow_punch_cooldown.load();
                if (ow_charge_looping_) {
                    stopChargeLoop(*ctx.cb);
                }
                if (ctx.cb->play_sound) ctx.cb->play_sound("ow.punch.release", 1.0f);
                if (ctx.cb->ow_strike && ctx.cb->ow_strike(1)) {
                    ow_hitstop_left_ = mode.ow_hitstop.load();
                    if (ctx.cb->play_sound) ctx.cb->play_sound("ow.punch.impact", 1.0f);
                }
            }
        } else if (t >= eldenring::ow::rocketPunchStateEnd(release_time, mode.ow_punch_tail.load(), max_t)) {
            mode.ow_state.store(0);
            mode.ow_anim_time.store(0.f);
            ow_punch_released_ = false;
        } else {
            mode.ow_anim_time.store(t);
        }
    } else if (state != 0 && max_t > 0.f && t >= max_t) {
        mode.ow_state.store(0); // Return to Idle
        mode.ow_anim_time.store(0.f);
    } else {
        mode.ow_anim_time.store(t);
    }
#else
    (void)ctx;
#endif
}

void OwHeroSkillHandler::tickAttack(const InputTickContext& ctx) {
#if ENABLE_OW_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    // OW mode attack triggers: hand cannon, rocket punch (right click, above),
    // rising uppercut (E) and seismic slam (Q).
    if (mode.ow_mode.load() && ctx.fg && !ctx.is_dead) {
        const float hitstop = mode.ow_hitstop.load();
        if (ctx.click_edge && eldenring::ow::owReady(ow_fire_cooldown_)) {
            mode.ow_state.store(2); // Hand cannon
            mode.ow_action_variant.store(static_cast<uint8_t>((mode.ow_action_variant.load() + 1u) & 1u));
            mode.ow_anim_time.store(0.f);
            ow_fire_cooldown_ = mode.ow_fire_interval.load();
            ow_since_shot_ = 0.f;
            if (ctx.cb->play_sound) ctx.cb->play_sound("ow.cannon.fire", 1.0f);
            if (ctx.cb->ow_strike && ctx.cb->ow_strike(0)) {
                ow_hitstop_left_ = hitstop;
                if (ctx.cb->play_sound) ctx.cb->play_sound("ow.cannon.hit", 1.0f);
            }
        }

        const bool gameplay_key = !ctx.inv_open && ctx.input_enabled;
        const bool e_down = gameplay_key && queryKeyDown(*ctx.cb, 'E');
        const bool q_down = gameplay_key && queryKeyDown(*ctx.cb, 'Q');
        if (e_down && !prev_ow_e_ && eldenring::ow::owReady(ow_uppercut_cooldown_left_)) {
            mode.ow_state.store(4); // Rising uppercut
            mode.ow_anim_time.store(0.f);
            ow_uppercut_cooldown_left_ = mode.ow_uppercut_cooldown.load();
            if (ctx.cb->play_sound) ctx.cb->play_sound("ow.uppercut.start", 1.0f);
            if (ctx.cb->ow_strike && ctx.cb->ow_strike(2)) {
                ow_hitstop_left_ = hitstop;
                if (ctx.cb->play_sound) ctx.cb->play_sound("ow.uppercut.hit", 1.0f);
            }
        }
        if (q_down && !prev_ow_q_ && eldenring::ow::owReady(ow_slam_cooldown_left_)) {
            mode.ow_state.store(5); // Seismic slam
            mode.ow_anim_time.store(0.f);
            ow_slam_cooldown_left_ = mode.ow_slam_cooldown.load();
            ow_slam_impact_played_ = false;
            if (ctx.cb->play_sound) ctx.cb->play_sound("ow.slam.flight", 1.0f);
            const bool hit = ctx.cb->ow_strike ? ctx.cb->ow_strike(3) : false;
            if (hit) ow_hitstop_left_ = hitstop;
            if (ctx.cb->ow_shake) ctx.cb->ow_shake(mode.ow_slam_shake.load());
        }
        prev_ow_e_ = e_down;
        prev_ow_q_ = q_down;
    } else {
        prev_ow_e_ = false;
        prev_ow_q_ = false;
    }
#else
    (void)ctx;
#endif
}

} // namespace eldenring::input
