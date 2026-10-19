#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <windows.h>
#endif

#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_state.hpp"
#include "eldenring_creative.hpp"
#include "eldenring_audio_data.hpp"
#include "eldenring_bow.hpp"
#include "eldenring_shield.hpp"
#include "eldenring_roll.hpp"
#include "eldenring_flight.hpp"
#include "cs_weapon_registry.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#ifndef VK_SPACE
#define VK_SPACE 0x20
#endif
#ifndef VK_CONTROL
#define VK_CONTROL 0x11
#endif
#ifndef VK_LBUTTON
#define VK_LBUTTON 0x01
#endif
#ifndef VK_RBUTTON
#define VK_RBUTTON 0x02
#endif
#ifndef VK_F1
#define VK_F1 0x70
#endif
#ifndef VK_F2
#define VK_F2 0x71
#endif
#ifndef VK_F3
#define VK_F3 0x72
#endif
#ifndef VK_F4
#define VK_F4 0x73
#endif
#ifndef VK_F5
#define VK_F5 0x74
#endif
#ifndef VK_F6
#define VK_F6 0x75
#endif
#ifndef VK_F7
#define VK_F7 0x76
#endif
#ifndef VK_F8
#define VK_F8 0x77
#endif
#ifndef VK_F9
#define VK_F9 0x78
#endif
#ifndef VK_F10
#define VK_F10 0x79
#endif
#ifndef VK_F11
#define VK_F11 0x7A
#endif
#ifndef VK_F12
#define VK_F12 0x7B
#endif

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

HotkeyDispatcher& HotkeyDispatcher::Instance() {
    static HotkeyDispatcher s_instance;
    return s_instance;
}

HotkeyDispatcher::HotkeyDispatcher() = default;

void HotkeyDispatcher::setCallbacks(const HotkeyCallbacks& cb) {
    cb_ = cb;
}

void HotkeyDispatcher::resetKeyStates() {
    prev8_ = false;
    prev6_ = false;
    prev7_ = false;
    for (int i = 0; i < 9; ++i) prev_digit_[i] = false;
    prev_space_ = false;
    prev9_ = false;
    prev10_ = false;
    prev11_ = false;
    prev12_ = false;
    prev5_ = false;
    prev4_ = false;
    prev_f3_ = false;
    prev_f2_ = false;
    prev_f1_ = false;
    prev_gameplay_f_ = false;
    prev_reload_r_ = false;
    cs_fire_cooldown_ = 0.f;
}

bool HotkeyDispatcher::queryKey(int vk) const {
    if (cb_.is_key_down) {
        return cb_.is_key_down(vk);
    }
#ifdef _WIN32
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    (void)vk;
    return false;
#endif
}

void HotkeyDispatcher::stop() {
    running_.store(false, std::memory_order_relaxed);
}

void HotkeyDispatcher::tick(float dt) {
    auto& ctx = RuntimeContext::Instance();
    auto& mode = ctx.mode();
    auto& player = ctx.player();
    auto& combat = ctx.combat();
    auto& ui = ctx.ui();

    const bool fg = cb_.is_game_in_foreground ? cb_.is_game_in_foreground() : true;
    const bool want_suppress = cb_.want_suppress ? cb_.want_suppress() : false;
    const bool is_dead = cb_.is_player_dead ? cb_.is_player_dead() : false;
    const bool inv_open = ui.inv_open.load(std::memory_order_relaxed);
    const bool mc_on = mode.mc_mode.load(std::memory_order_relaxed);

    // Tick all pluggable mods
    ctx.mods().tick(dt);

    // Melee & Feedback tick
    {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        combat.melee.tick(dt);
        combat.feedback.tick(dt);
    }

    // Sprinting update
    if (fg) {
        const bool key_w = queryKey('W');
        const bool key_s = queryKey('S');
        const bool key_ctrl = queryKey(VK_CONTROL);
        int food_lvl = 20;
        {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            food_lvl = player.hunger.food();
        }
        const bool blocking = mode.shield_blocking.load(std::memory_order_relaxed);
        player.sprint.update(dt, key_w, key_s, key_ctrl, food_lvl, blocking);
    } else {
        player.sprint.reset();
    }
    player.sprint_active.store(player.sprint.isSprinting(), std::memory_order_relaxed);
    player.sprint_fov.store(player.sprint.fovMultiplier(), std::memory_order_relaxed);
    player.sprint_anim.store(player.sprint.animSpeedMultiplier(), std::memory_order_relaxed);

    // Footsteps
    if (mc_on && fg && cb_.get_player_chr && cb_.read_physics_pos) {
        static float prev_pos[3] = {};
        static bool have_prev = false;
        static audio::StepClock steps;
        const uintptr_t player_chr = cb_.get_player_chr();
        float pos[3] = {};
        if (player_chr != 0 && cb_.read_physics_pos(player_chr, pos)) {
            if (have_prev && dt > 0.f) {
                const float dx = pos[0] - prev_pos[0], dz = pos[2] - prev_pos[2];
                const float step_len = std::sqrt(dx * dx + dz * dz);
                const bool grounded = cb_.is_player_airborne ? !cb_.is_player_airborne() : true;
                mode.walk_speed.store(step_len < 1.5f ? step_len / dt : 0.f);
                mode.on_ground.store(grounded);
                if (step_len < 1.5f && steps.update(step_len / dt, dt, grounded) && want_suppress) {
                    if (cb_.play_sound) cb_.play_sound("block.grass.step", 0.9f);
                }
                if (step_len >= 1.5f) steps = audio::StepClock{};
            }
            std::memcpy(prev_pos, pos, sizeof(pos));
            have_prev = true;
        } else {
            have_prev = false;
        }
    }

    // Regeneration, hunger, and eating
    {
        int heal = 0;
        {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            heal += player.survival.tick(dt, 100);
            player.hunger_heal_carry += player.hunger.tick(dt, true) * 100.f / 20.f;
            const int whole = static_cast<int>(player.hunger_heal_carry);
            player.hunger_heal_carry -= static_cast<float>(whole);
            heal += whole;
            if (player.eating.isEating() && combat.melee.heldItem() != player.eating.getCurrentItem()) {
                player.eating.cancel();
            }
            const mc::ItemId meal = player.eating.getCurrentItem();
            const mc::EatingEvent e = player.eating.update(dt);
            if (e.chew_sound && cb_.play_sound) cb_.play_sound("entity.generic.eat", 0.8f);
            if (e.completed) {
                if (e.play_burp_sound && cb_.play_sound) cb_.play_sound("entity.player.burp", 1.0f);
                player.survival.apply(e.effects);
                player.hunger.eat(meal);
                if (creative::consumesItems(ctx.currentGameMode())) {
                    combat.melee.consumeAt(combat.melee.selectedSlot());
                }
            }
        }
        if (heal > 0) mode.heal_pending.fetch_add(heal);
    }

    // Subsystem ticks
    if (cb_.inventory_tick) cb_.inventory_tick(fg);
    if (cb_.blocks_tick) cb_.blocks_tick();
    if (cb_.mc_flags_step) cb_.mc_flags_step();

    // Key masking
    if (cb_.set_masked_keys) {
        int hidden[16];
        size_t n_hidden = 0;
#ifdef _WIN32
        if (mode.mc_jump.load() && mc_on && fg && !inv_open) {
            hidden[n_hidden++] = static_cast<int>(MapVirtualKeyW(static_cast<UINT>(mode.mc_jump_vk.load()), MAPVK_VK_TO_VSC));
        }
#endif
        if (mode.disable_vanilla_roll.load() && mc_on && fg && !inv_open) {
            const auto roll_mask = roll::computeRollMask(true, true, mode.roll_vk.load());
            n_hidden = roll::mergeMaskedKeys(hidden, sizeof(hidden) / sizeof(hidden[0]), n_hidden, roll_mask.scancodes, roll_mask.count);
        }
        if (mode.flying.load()) {
            n_hidden = roll::mergeMaskedKeys(hidden, sizeof(hidden) / sizeof(hidden[0]), n_hidden, flight::kHiddenKeys, sizeof(flight::kHiddenKeys) / sizeof(flight::kHiddenKeys[0]));
        }
        cb_.set_masked_keys(hidden, n_hidden);
    }

    // Right-click interaction cascade
    const bool input_enabled = mode.input_enabled.load(std::memory_order_relaxed);
    const bool right_edge = !inv_open && input_enabled && cb_.take_right_click && cb_.take_right_click();
#if ENABLE_CS2_VIEWMODEL
    if (mode.cs_mode.load() && fg && !is_dead && right_edge) {
        // RMB is the knife's heavy attack. Guns consume the click but have no
        // secondary action yet (AWP scope is a separate HUD feature).
        if (mode.cs_weapon_slot.load() == 0) {
            mode.cs_knife_state.store(3); // Heavy attack
            mode.cs_knife_inspect.store(false);
            mode.cs_action_hit.store(true);
            mode.cs_knife_anim_time.store(0.f);
            if (cb_.play_sound) cb_.play_sound("cs.knife.hit", 1.0f);
        }
    } else
#endif
    if (right_edge && fg && want_suppress && !is_dead) {
        bool handled = false;
        if (cb_.try_ignite_tnt && cb_.try_ignite_tnt()) handled = true;
        else if (cb_.try_ignite_creeper && cb_.try_ignite_creeper()) handled = true;
        else if (cb_.try_place_block && cb_.try_place_block()) handled = true;
        else if (mc_on && cb_.try_use_spawn_egg && cb_.try_use_spawn_egg()) handled = true;
        else {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            const mc::ItemId item = combat.melee.heldItem();
            if (!player.eating.isEating() && combat.melee.countAt(combat.melee.selectedSlot()) > 0 &&
                player.hunger.canEat(item) && player.eating.startEating(item)) {
                if (cb_.log) cb_.log("SURVIVAL: started eating item " + std::to_string(static_cast<int>(item)));
            }
        }
        (void)handled;
    }

    // Bow update
    {
        static bow::BowController s_bow;
        bool usable = false;
        {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            combat.melee.select(combat.melee.selectedSlot());
            const bool in_offhand = bow::bowInOffhand(combat.melee.heldItem(), combat.melee.countAt(combat.melee.selectedSlot()),
                                                      combat.melee.offhandItem(), combat.melee.offhandCount());
            mode.bow_offhand.store(in_offhand, std::memory_order_relaxed);
            usable = bow::bowHandUsable(combat.melee.heldItem(), combat.melee.countAt(combat.melee.selectedSlot()),
                                        combat.melee.offhandItem(), combat.melee.offhandCount()) &&
                     (!creative::consumesItems(ctx.currentGameMode()) || combat.melee.has(mc::ItemId::Arrow));
        }
        usable = usable && mode.bullet_fire_enabled.load() && mc_on && fg && want_suppress && !inv_open && input_enabled && !is_dead;
        const bool right_held = input_enabled && ((cb_.right_held && cb_.right_held()) || queryKey(VK_RBUTTON));
        const bow::Update u = s_bow.update(dt, right_held, usable);
        mode.bow_power.store(s_bow.power(), std::memory_order_relaxed);
        mode.bow_ticks.store(s_bow.ticks(), std::memory_order_relaxed);
        if (u.released) {
            const float damage = bow::damageForPower(u.power);
            if (cb_.queue_bullet_fire && cb_.queue_bullet_fire(damage)) {
                if (creative::consumesItems(ctx.currentGameMode())) {
                    std::lock_guard<std::mutex> g(combat.melee_mutex);
                    combat.melee.consumeFirst(mc::ItemId::Arrow);
                }
                if (cb_.play_sound) cb_.play_sound("entity.arrow.shoot", 1.0f);
            }
        }
    }

    // Shield update
    {
        static shield::ShieldController s_shield;
        bool has_shield = false;
        bool shield_in_offhand = false;
        {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            has_shield = combat.melee.holds(mc::ItemId::Shield);
            shield_in_offhand = !(combat.melee.heldItem() == mc::ItemId::Shield && combat.melee.countAt(combat.melee.selectedSlot()) > 0);
        }
        mode.shield_offhand.store(shield_in_offhand, std::memory_order_relaxed);
        const bool right_held = input_enabled && ((cb_.right_held && cb_.right_held()) || queryKey(VK_RBUTTON)) &&
                                fg && want_suppress && !inv_open && !is_dead && mc_on;
        const bool is_drawing_bow = mode.bow_ticks.load(std::memory_order_relaxed) > 0;
        const bool is_eating = player.eating.isEating();
        const bool shield_usable = has_shield && !is_drawing_bow && !is_eating;
        s_shield.update(dt, right_held, shield_usable);
        combat.shield.update(dt, right_held, shield_usable);
        const bool prev_blocking = mode.shield_blocking.load(std::memory_order_relaxed);
        const bool now_blocking = s_shield.isBlocking();
        if (!prev_blocking && now_blocking) {
            player.sprint.cancel();
            player.sprint_active.store(false, std::memory_order_relaxed);
        }
        mode.shield_blocking.store(now_blocking, std::memory_order_relaxed);
    }

    // Mouse wheel slot selection
    if (input_enabled && cb_.take_wheel_notches) {
        const int notches = cb_.take_wheel_notches();
        if (notches != 0 && fg && !inv_open && want_suppress) {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            combat.melee.scroll(-notches);
        }
    }

#if ENABLE_CS2_VIEWMODEL
    // CS action timer advance. The end time comes from the loaded clip, so a
    // new authored take never needs a matching C++ constant.
    if (mode.cs_mode.load()) {
        const uint8_t state = mode.cs_knife_state.load();
        const uint8_t variant = mode.cs_action_variant.load();
        const bool hit = mode.cs_action_hit.load();
        const float t = mode.cs_knife_anim_time.load() + dt;
        auto durationFor = [this, &mode](uint8_t s, uint8_t v, bool h) -> float {
            const bool is_knife = mode.cs_weapon_slot.load() == 0;
            float duration = 0.f;
            if (cb_.clip_duration) {
                forEachCsClip(s, v, h, is_knife, [&](const char* name) {
                    if (duration <= 0.f) {
                        const float d = cb_.clip_duration(name);
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
                if (cb_.play_sound) {
                    cb_.play_sound(mode.cs_weapon_slot.load() == 1 ? "cs.ak47.bolt" : "cs.knife.deploy", 1.0f);
                }
            }
            mode.cs_knife_state.store(0); // Return to Idle
            mode.cs_knife_inspect.store(false);
            mode.cs_knife_anim_time.store(0.f);
        } else {
            mode.cs_knife_anim_time.store(t);
        }
    }
#endif

    // F key (inspect in CS mode / swap offhand in MC) & Digits 1-9 (slot select / deploy)
    if (fg && !inv_open && want_suppress) {
        const bool f_down = queryKey('F');
        if (f_down && !prev_gameplay_f_) {
#if ENABLE_CS2_VIEWMODEL
            if (mode.cs_mode.load()) {
                const uint8_t take = static_cast<uint8_t>((mode.cs_action_variant.load() + 1u) % 3u);
                mode.cs_action_variant.store(take);
                mode.cs_knife_state.store(1); // Inspect!
                mode.cs_knife_inspect.store(true);
                mode.cs_knife_anim_time.store(0.f);
                const uint8_t slot = mode.cs_weapon_slot.load();
                if (cb_.play_sound) cb_.play_sound(slot == 1 ? "cs.ak47.inspect" : "cs.knife.inspect", 1.0f);
                if (cb_.log) cb_.log(slot == 0 ? "CS: Butterfly Knife Inspect (Flower Spin)" : "CS: weapon inspect");
            } else
#endif
            {
                mc::contracts::ModInputEvent f_ev{'F', true};
                if (!ctx.mods().dispatchInput(f_ev)) {
                    std::lock_guard<std::mutex> g(combat.melee_mutex);
                    combat.melee.inventory().swapMainAndOffhand(combat.melee.selectedSlot());
                    if (cb_.log) cb_.log("inventory: swapped main and offhand via F key");
                }
            }
        }
        prev_gameplay_f_ = f_down;

        for (int i = 0; i < 9; ++i) {
            const bool d = queryKey('1' + i);
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
                    if (cb_.play_sound) cb_.play_sound(slot == 1 ? "cs.ak47.draw" : "cs.knife.deploy", 1.0f);
                } else
#endif
                {
                    std::lock_guard<std::mutex> g(combat.melee_mutex);
                    combat.melee.select(i);
                }
            }
            prev_digit_[i] = d;
        }

#if ENABLE_CS2_VIEWMODEL
        // R: reload the active gun (state 5). Knife has no reload.
        const bool r_down = queryKey('R');
        if (r_down && !prev_reload_r_ && mode.cs_mode.load() && mode.cs_weapon_slot.load() != 0) {
            mode.cs_knife_state.store(5); // Reload
            mode.cs_knife_inspect.store(false);
            mode.cs_knife_anim_time.store(0.f);
            if (cb_.play_sound) cb_.play_sound(mode.cs_weapon_slot.load() == 1 ? "cs.ak47.reload" : "cs.knife.deploy", 1.0f);
            if (cb_.log) cb_.log("CS: reload");
        }
        prev_reload_r_ = r_down;
#endif
    }

    if (input_enabled && cb_.set_suppress_mouse_buttons) {
        cb_.set_suppress_mouse_buttons(fg && want_suppress);
    }

    // Space key (Jump & Exhaustion)
    const bool space = fg && queryKey(VK_SPACE);
    const bool airborne = cb_.is_player_airborne ? cb_.is_player_airborne() : false;
    player.jump.update(dt, space && !prev_space_, mc_on && airborne);
    if (space && !prev_space_ && mc_on && !airborne) {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        player.hunger.addExhaustion(0.05f);
    }
    prev_space_ = space;

    // Left click / Mining vs Click Attack
    const bool alive = !is_dead;
    const bool click_edge = !inv_open && input_enabled && cb_.take_left_click && cb_.take_left_click() && alive;
    const bool left_held = !inv_open && input_enabled && ((cb_.left_held && cb_.left_held()) || queryKey(VK_LBUTTON)) && alive;
    const bool want_suppress_click = (click_edge || left_held) ? want_suppress : false;

#if ENABLE_CS2_VIEWMODEL
    cs_fire_cooldown_ = std::max(0.f, cs_fire_cooldown_ - dt);
    cs_since_shot_ += dt;
    // CS mode attack triggers: knife slash, or gun fire (automatic aware).
    if (mode.cs_mode.load() && fg && alive) {
        const uint8_t slot = mode.cs_weapon_slot.load();
        if (slot == 0) {
            if (click_edge) {
                mode.cs_knife_state.store(2); // Primary light attack
                mode.cs_knife_inspect.store(false);
                mode.cs_action_variant.store(static_cast<uint8_t>((mode.cs_action_variant.load() + 1u) & 1u));
                mode.cs_action_hit.store(true);
                mode.cs_knife_anim_time.store(0.f);
                if (cb_.play_sound) cb_.play_sound("cs.knife.slash", 1.0f);
            }
        } else {
            const char* id = (slot == 1) ? "weapon_ak47"
                           : (slot == 2) ? "weapon_awp"
                           : "weapon_m4a1_silencer";
            const auto* def = eldenring::cs::CSWeaponRegistry::instance().findWeapon(id);
            const float rpm = (def && def->ballistics.fire_rate_rpm > 1.f) ? def->ballistics.fire_rate_rpm : 600.f;
            const bool automatic = def ? def->ballistics.is_automatic : true;
            const bool trigger = automatic ? left_held : click_edge;
            if (trigger && cs_fire_cooldown_ <= 0.f) {
                if (mode.cs_ammo.load() > 0) {
                    mode.cs_ammo.fetch_sub(1);
                    mode.cs_knife_state.store(2); // Fire
                    mode.cs_knife_inspect.store(false);
                    mode.cs_knife_anim_time.store(0.f);
                    cs_fire_cooldown_ = 60.f / rpm;
                    if (cb_.queue_cs_shot) {
                        const float damage = def ? def->ballistics.damage : 36.f;
                        // CS: the first shot after a pause is perfectly accurate; sustained fire spreads.
                        const float spread = (cs_since_shot_ > 0.25f)
                            ? 0.f
                            : (def ? def->ballistics.spread_deg : 0.45f);
                        const float range = def ? def->ballistics.max_range_meters : 300.f;
                        const uint32_t tracer = def ? def->ballistics.tracer_frequency : 3u;
                        cb_.queue_cs_shot(damage, spread, range, tracer, slot);
                        cs_since_shot_ = 0.f;
                    }
                    if (cb_.play_sound) cb_.play_sound("cs.ak47.fire", 1.0f); // placeholder until AWP/M4 fire sounds are extracted
                } else if (click_edge && cb_.log) {
                    cb_.log("CS: empty magazine - press R to reload");
                }
            }
        }
    }
#endif

    // A CS gun owns the left click: the generic MC swing must not fire too.
    bool cs_gun_equipped = false;
#if ENABLE_CS2_VIEWMODEL
    cs_gun_equipped = mode.cs_mode.load() && mode.cs_weapon_slot.load() != 0;
#endif
    const bool mining_consumed = fg && mode.click_attack.load() && want_suppress_click && cb_.step_mining && cb_.step_mining(dt, left_held);
    if (!mining_consumed && !cs_gun_equipped && click_edge && fg && mode.click_attack.load() && want_suppress_click) {
        float charged = 1.0f;
        {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            charged = combat.melee.startSwing();
            player.hunger.addExhaustion(0.1f);
        }
        bool hit = true;
        if (mode.damage_enabled.load() && cb_.click_attack) {
            hit = cb_.click_attack(charged);
        }
#if ENABLE_CS2_VIEWMODEL
        // The knife shares the MC melee path; use its result for the hit/miss take.
        if (mode.cs_mode.load() && mode.cs_weapon_slot.load() == 0) {
            mode.cs_action_hit.store(hit);
        }
#endif
    }

#if ENABLE_CS2_VIEWMODEL
    // Function keys: F1 for CS mode toggle
    const bool d1 = fg && queryKey(VK_F1);
    if (d1 && !prev_f1_) {
        const bool new_cs = !mode.cs_mode.load();
        mode.cs_mode.store(new_cs);
        if (new_cs) {
            mode.first_person.store(true);
            mode.cs_knife_state.store(4); // Deploy
            mode.cs_knife_inspect.store(false);
            mode.cs_action_variant.store(0);
            mode.cs_action_hit.store(true);
            mode.cs_knife_anim_time.store(0.f);
            if (cb_.play_sound) cb_.play_sound("cs.knife.deploy", 1.0f);
            if (cb_.log) cb_.log("[MODE] Counter-Strike Weapon Mode: ACTIVE (Butterfly Knife)");
        } else {
            mode.cs_knife_state.store(0);
            mode.cs_knife_inspect.store(false);
            mode.cs_action_variant.store(0);
            mode.cs_action_hit.store(true);
            mode.cs_knife_anim_time.store(0.f);
            if (cb_.log) cb_.log("[MODE] Counter-Strike Weapon Mode: OFF");
        }
    }
    prev_f1_ = d1;
#endif

    const bool d8 = fg && queryKey(VK_F8);
    if (d8 && !prev8_ && mode.damage_enabled.load() && cb_.enqueue_nearest_hostile) {
        cb_.enqueue_nearest_hostile();
    }
    prev8_ = d8;

    const bool d5 = fg && queryKey(VK_F5);
    if (d5 && !prev5_) {
        const auto next_mode = creative::toggled(ctx.currentGameMode());
        mode.game_mode.store(static_cast<uint8_t>(next_mode));
#ifdef _WIN32
        mode.mode_toast_ms.store(GetTickCount64());
#endif
        if (cb_.log) cb_.log(std::string("F5: ") + creative::modeName(next_mode));
    }
    prev5_ = d5;

    const bool d6 = fg && queryKey(VK_F6);
    if (d6 && !prev6_) {
#if ENABLE_CS2_VIEWMODEL
        if (mode.cs_mode.load()) {
            mode.cs_mode.store(false);
            mode.cs_knife_state.store(0);
            mode.cs_knife_inspect.store(false);
            mode.cs_action_variant.store(0);
            mode.cs_action_hit.store(true);
            mode.cs_knife_anim_time.store(0.f);
            if (cb_.log) cb_.log("[MODE] Counter-Strike Weapon Mode: OFF (via F6)");
        } else
#endif
        {
            const bool new_mc = !mode.mc_mode.load();
            mode.mc_mode.store(new_mc);
            if (cb_.log) cb_.log(std::string("F6: MC mode ") + (new_mc ? "on" : "off"));
            if (cb_.reset_slot_probe) cb_.reset_slot_probe(slot_cursor_);
        }
    }
    prev6_ = d6;

    const bool d3 = fg && queryKey(VK_F3);
    if (d3 && !prev_f3_ && cb_.queue_bullet_fire) {
        cb_.queue_bullet_fire(10.0f);
    }
    prev_f3_ = d3;

    const bool d2 = fg && queryKey(VK_F2);
    if (d2 && !prev_f2_ && cb_.queue_summon) {
        cb_.queue_summon("F2");
    }
    prev_f2_ = d2;

    const bool d9 = fg && queryKey(VK_F9);
    if (d9 && !prev9_ && mode.hide_native.load() && cb_.cycle_hidden_slot) {
        cb_.cycle_hidden_slot(slot_cursor_);
    }
    prev9_ = d9;

    const bool d12 = fg && queryKey(VK_F12);
    if (d12 && !prev12_ && cb_.request_frame_trace) {
        cb_.request_frame_trace(240);
    }
    prev12_ = d12;

    const bool d11 = fg && queryKey(VK_F11);
    if (d11 && !prev11_ && cb_.snapshot_native_parts) {
        cb_.snapshot_native_parts();
    }
    prev11_ = d11;

    const bool d10 = fg && queryKey(VK_F10);
    if (d10 && !prev10_) {
        mode.first_person.store(!mode.first_person.load());
        if (cb_.reset_slot_probe) cb_.reset_slot_probe(slot_cursor_);
    }
    prev10_ = d10;

    const bool d4 = fg && queryKey(VK_F4);
    if (d4 && !prev4_ && cb_.request_screenshot) {
        static unsigned shot_no = 0;
        char name[64];
        snprintf(name, sizeof(name), "mc_er_shot_%03u.png", ++shot_no);
        cb_.request_screenshot(name);
    }
    prev4_ = d4;

    const bool d7 = fg && queryKey(VK_F7);
    if (d7 && !prev7_ && cb_.cycle_depth_candidate) {
        cb_.cycle_depth_candidate();
    }
    prev7_ = d7;

    if (cb_.check_summon_watch) {
        cb_.check_summon_watch(0);
    }
}

void HotkeyDispatcher::runKeyLoop() {
    running_.store(true, std::memory_order_relaxed);
#ifdef _WIN32
    uint64_t last_ms = GetTickCount64();

    while (running_.load(std::memory_order_relaxed)) {
        Sleep(15);
        const uint64_t now_ms = GetTickCount64();
        const float dt = static_cast<float>(now_ms - last_ms) / 1000.f;
        last_ms = now_ms;
        tick(dt);
    }
#endif
}

} // namespace eldenring::input
