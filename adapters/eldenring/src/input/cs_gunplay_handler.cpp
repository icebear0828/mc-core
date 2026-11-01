#include "eldenring_input_handlers.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_runtime_context.hpp"
#include "cs_weapon_registry.hpp"
#include "input_keys.hpp"

#include <algorithm>
#include <cmath>

namespace {

// Enumerate the CS2 clip ids for a state in priority order. CS2 ships two
// takes for most actions and three inspect takes; the runtime alternates them.
template <typename Fn>
void forEachCsClip(uint8_t state, uint8_t variant, bool hit, bool knife, Fn&& fn) {
    const bool alt = (variant & 1u) != 0u;
    switch (state) {
        case 1: { // Inspect: lookat01/02/03
            const uint8_t take = static_cast<uint8_t>(variant % 3u);
            if (take == 2) fn("inspect3");
            else if (take == 1) fn("inspect2");
            fn("inspect");
            break;
        }
        case 2: // Primary: gun fire, or knife light hit/miss
            if (knife) {
                if (hit) {
                    if (alt) fn("light_hit2");
                    fn("light_hit");
                } else {
                    if (alt) fn("light_miss2");
                    fn("light_miss");
                    fn("light_hit");
                }
            } else {
                fn("fire");
            }
            break;
        case 3: // Secondary: knife heavy hit/miss (guns have no secondary clip)
            if (knife) {
                if (hit) {
                    fn("heavy_hit");
                    fn("backstab");
                } else {
                    fn("heavy_miss");
                    fn("heavy_hit");
                }
            }
            break;
        case 4: // Deploy/draw
            if (alt) fn("deploy2");
            fn("deploy");
            fn("draw");
            break;
        case 5: // Reload
            fn("reload");
            break;
        case 6: // Backstab
            if (alt) fn("backstab_light2");
            fn("backstab_light");
            fn("backstab");
            break;
        case 0:
        default:
            fn("idle");
            break;
    }
}

// Used when no clip-duration callback is wired (headless tests, asset missing).
float fallbackCsDuration(uint8_t state, bool knife) {
    switch (state) {
        case 1: return 4.1f;   // lookat01
        case 2: return knife ? 1.167f : 0.767f;
        case 3: return 1.167f;
        case 4: return 1.0f;
        case 5: return 2.433f;
        case 6: return 1.167f;
        default: return 0.f;
    }
}

} // namespace

namespace eldenring::input {

void CsGunplayHandler::reset() {
    cs_fire_cooldown_ = 0.f;
    prev_reload_r_ = false;
    prev_gameplay_f_ = false;
    for (bool& key : prev_digit_) key = false;
}

bool CsGunplayHandler::tickRightClick(const InputTickContext& ctx) {
#if ENABLE_CS2_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    if (mode.cs_mode.load() && ctx.fg && !ctx.is_dead && ctx.right_edge) {
        // RMB is the knife's heavy attack. Guns consume the click but have no
        // secondary action yet (AWP scope is a separate HUD feature).
        if (mode.cs_weapon_slot.load() == 0) {
            mode.cs_knife_state.store(3); // Heavy attack
            mode.cs_knife_inspect.store(false);
            mode.cs_action_hit.store(true);
            mode.cs_knife_anim_time.store(0.f);
            if (ctx.cb->play_sound) ctx.cb->play_sound("cs.knife.hit", 1.0f);
        }
        return true;
    }
#else
    (void)ctx;
#endif
    return false;
}

void CsGunplayHandler::tickActionTimer(const InputTickContext& ctx) {
#if ENABLE_CS2_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    if (!mode.cs_mode.load()) {
        return;
    }
    // The end time comes from the loaded clip, so a new authored take never
    // needs a matching C++ constant.
    const uint8_t state = mode.cs_knife_state.load();
    const uint8_t variant = mode.cs_action_variant.load();
    const bool hit = mode.cs_action_hit.load();
    const float t = mode.cs_knife_anim_time.load() + ctx.dt;
    auto durationFor = [this, &mode, &ctx](uint8_t s, uint8_t v, bool h) -> float {
        const bool is_knife = mode.cs_weapon_slot.load() == 0;
        float duration = 0.f;
        if (ctx.cb->clip_duration) {
            forEachCsClip(s, v, h, is_knife, [&](const char* name) {
                if (duration <= 0.f) {
                    const float d = ctx.cb->clip_duration(name);
                    if (d > 0.f) duration = d;
                }
            });
        }
        return duration > 0.f ? duration : fallbackCsDuration(s, is_knife);
    };
    const float max_t = (state != 0) ? durationFor(state, variant, hit) : 0.f;
    if (state != 0 && max_t > 0.f && t >= max_t) {
        if (state == 5) { // Reload finished: move rounds from reserve to magazine
            const uint32_t mag = mode.cs_mag_capacity.load();
            const uint32_t ammo = mode.cs_ammo.load();
            const uint32_t reserve = mode.cs_reserve.load();
            const uint32_t need = (mag > ammo) ? (mag - ammo) : 0;
            const uint32_t take = std::min(need, reserve);
            mode.cs_ammo.store(ammo + take);
            mode.cs_reserve.store(reserve - take);
            cs_reload_sound_stage_ = 0;
        }
        mode.cs_knife_state.store(0); // Return to Idle
        mode.cs_knife_inspect.store(false);
        mode.cs_knife_anim_time.store(0.f);
    } else {
        mode.cs_knife_anim_time.store(t);
        if (state == 5 && mode.cs_weapon_slot.load() == 1 && max_t > 0.f) {
            // Reload audio follows the animation instead of one sound at
            // t=0: mag out at frame 13, mag in at frame 24, bolt pull at
            // frame 48 of the 74-frame reload clip.
            const float d = max_t;
            if (cs_reload_sound_stage_ == 0 && t >= d * 0.176f) {
                if (ctx.cb->play_sound) ctx.cb->play_sound("cs.ak47.magout", 1.0f);
                cs_reload_sound_stage_ = 1;
            }
            if (cs_reload_sound_stage_ == 1 && t >= d * 0.324f) {
                if (ctx.cb->play_sound) ctx.cb->play_sound("cs.ak47.magin", 1.0f);
                cs_reload_sound_stage_ = 2;
            }
            if (cs_reload_sound_stage_ == 2 && t >= d * 0.649f) {
                if (ctx.cb->play_sound) ctx.cb->play_sound("cs.ak47.bolt", 1.0f);
                cs_reload_sound_stage_ = 3;
            }
        }
    }
#else
    (void)ctx;
#endif
}

void CsGunplayHandler::tickGameplayKeys(const InputTickContext& ctx) {
#if ENABLE_CS2_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
#endif

    // F: inspect in CS mode. The coordinator calls the MC handler afterwards
    // for the offhand swap when CS does not own the key.
    const bool f_down = queryKeyDown(*ctx.cb, 'F');
    if (f_down && !prev_gameplay_f_) {
#if ENABLE_CS2_VIEWMODEL
        if (mode.cs_mode.load()) {
            const uint8_t take = static_cast<uint8_t>((mode.cs_action_variant.load() + 1u) % 3u);
            mode.cs_action_variant.store(take);
            mode.cs_knife_state.store(1); // Inspect!
            mode.cs_knife_inspect.store(true);
            mode.cs_knife_anim_time.store(0.f);
            const uint8_t slot = mode.cs_weapon_slot.load();
            if (ctx.cb->play_sound) ctx.cb->play_sound(slot == 1 ? "cs.ak47.inspect" : "cs.knife.inspect", 1.0f);
            if (ctx.cb->log) ctx.cb->log(slot == 0 ? "CS: Butterfly Knife Inspect (Flower Spin)" : "CS: weapon inspect");
        }
#endif
    }
    prev_gameplay_f_ = f_down;

    for (int i = 0; i < 9; ++i) {
        const bool d = queryKeyDown(*ctx.cb, '1' + i);
        if (d && !prev_digit_[i]) {
#if ENABLE_CS2_VIEWMODEL
            if (mode.cs_mode.load() && i < 4) {
                // CS arsenal slots: 1 = AK-47, 2 = AWP, 3 = butterfly knife, 4 = M4A1-S
                const uint8_t slot = (i == 0) ? 1 : (i == 1) ? 2 : (i == 2) ? 0 : 3;
                mode.cs_weapon_slot.store(slot);
                if (slot == 0) {
                    mode.cs_ammo.store(0);
                    mode.cs_reserve.store(0);
                    mode.cs_mag_capacity.store(0);
                } else {
                    const char* id = (slot == 1) ? "weapon_ak47"
                                   : (slot == 2) ? "weapon_awp"
                                   : "weapon_m4a1_silencer";
                    if (const auto* def = eldenring::cs::CSWeaponRegistry::instance().findWeapon(id)) {
                        mode.cs_ammo.store(def->ballistics.magazine_capacity);
                        mode.cs_reserve.store(def->ballistics.reserve_capacity);
                        mode.cs_mag_capacity.store(def->ballistics.magazine_capacity);
                    }
                }
                mode.cs_knife_state.store(4); // Deploy!
                mode.cs_knife_inspect.store(false);
                mode.cs_action_variant.store(static_cast<uint8_t>((mode.cs_action_variant.load() + 1u) & 1u));
                mode.cs_action_hit.store(true);
                mode.cs_knife_anim_time.store(0.f);
                if (ctx.cb->play_sound) ctx.cb->play_sound(slot == 1 ? "cs.ak47.draw" : "cs.knife.deploy", 1.0f);
            }
#endif
        }
        prev_digit_[i] = d;
    }

#if ENABLE_CS2_VIEWMODEL
    // R: reload the active gun (state 5). Knife has no reload.
    const bool r_down = queryKeyDown(*ctx.cb, 'R');
    if (r_down && !prev_reload_r_ && mode.cs_mode.load() && mode.cs_weapon_slot.load() != 0) {
        mode.cs_knife_state.store(5); // Reload
        mode.cs_knife_inspect.store(false);
        mode.cs_knife_anim_time.store(0.f);
        cs_reload_sound_stage_ = 0;
        if (ctx.cb->log) ctx.cb->log("CS: reload");
    }
    prev_reload_r_ = r_down;
#endif
}

void CsGunplayHandler::tickAttack(const InputTickContext& ctx) {
#if ENABLE_CS2_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    cs_fire_cooldown_ = std::max(0.f, cs_fire_cooldown_ - ctx.dt);
    cs_since_shot_ += ctx.dt;
    // CS mode attack triggers: knife slash, or gun fire (automatic aware).
    if (mode.cs_mode.load() && ctx.fg && !ctx.is_dead) {
        const uint8_t slot = mode.cs_weapon_slot.load();
        if (slot == 0) {
            if (ctx.click_edge) {
                mode.cs_knife_state.store(2); // Primary light attack
                mode.cs_knife_inspect.store(false);
                mode.cs_action_variant.store(static_cast<uint8_t>((mode.cs_action_variant.load() + 1u) & 1u));
                mode.cs_action_hit.store(true);
                mode.cs_knife_anim_time.store(0.f);
                if (ctx.cb->play_sound) ctx.cb->play_sound("cs.knife.slash", 1.0f);
            }
        } else {
            const char* id = (slot == 1) ? "weapon_ak47"
                           : (slot == 2) ? "weapon_awp"
                           : "weapon_m4a1_silencer";
            const auto* def = eldenring::cs::CSWeaponRegistry::instance().findWeapon(id);
            const float rpm = (def && def->ballistics.fire_rate_rpm > 1.f) ? def->ballistics.fire_rate_rpm : 600.f;
            const bool automatic = def ? def->ballistics.is_automatic : true;
            const bool trigger = automatic ? ctx.left_held : ctx.click_edge;
            if (trigger && cs_fire_cooldown_ <= 0.f) {
                if (mode.cs_ammo.load() > 0) {
                    mode.cs_ammo.fetch_sub(1);
                    mode.cs_knife_state.store(2); // Fire
                    mode.cs_knife_inspect.store(false);
                    mode.cs_knife_anim_time.store(0.f);
                    cs_fire_cooldown_ = 60.f / rpm;
                    if (ctx.cb->queue_cs_shot) {
                        const float damage = def ? def->ballistics.damage : 36.f;
                        // CS: the first shot after a pause is perfectly accurate; sustained fire spreads.
                        const float spread = (cs_since_shot_ > 0.25f)
                            ? 0.f
                            : (def ? def->ballistics.spread_deg : 0.45f);
                        const float range = def ? def->ballistics.max_range_meters : 300.f;
                        const uint32_t tracer = def ? def->ballistics.tracer_frequency : 3u;
                        ctx.cb->queue_cs_shot(damage, spread, range, tracer, slot);
                        cs_since_shot_ = 0.f;
                    }
                    if (ctx.cb->play_sound) ctx.cb->play_sound("cs.ak47.fire", 1.0f); // placeholder until AWP/M4 fire sounds are extracted
                } else if (ctx.click_edge && ctx.cb->log) {
                    ctx.cb->log("CS: empty magazine - press R to reload");
                }
            }
        }
    }
#else
    (void)ctx;
#endif
}

} // namespace eldenring::input
