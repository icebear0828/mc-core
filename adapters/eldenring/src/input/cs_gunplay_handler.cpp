#include "eldenring_input_handlers.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_runtime_context.hpp"
#include "cs_fire_events.hpp"
#include "cs_reload_events.hpp"
#include "cs_economy.hpp"
#include "cs_weapon_registry.hpp"
#include "input_keys.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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
float fallbackCsDuration(uint8_t state, uint8_t slot) {
    const bool knife = slot == 0;
    switch (state) {
        case 1: return 4.1f;   // lookat01
        case 2: return knife ? 1.167f : 0.767f;
        case 3: return 1.167f;
        case 4: return 1.0f;
        case 5:
            // Reload lengths differ per gun: AK-47 74f, AWP 111f, M4A1-S 93f
            // @30fps. A single 2.433 s value cut the M4A1-S reload 20 frames
            // short whenever the renderer had not published its clip yet.
            return (slot == 2) ? 3.6667f : (slot == 3) ? 3.0667f : 2.433f;
        case 6: return 1.167f;
        default: return 0.f;
    }
}

// Slots: 0 = butterfly knife, 1 = AK-47, 2 = AWP, 3 = M4A1-S.
const char* weaponIdForSlot(uint8_t slot) {
    switch (slot) {
        case 1: return "weapon_ak47";
        case 2: return "weapon_awp";
        case 3: return "weapon_m4a1_silencer";
        default: return "weapon_knife_butterfly";
    }
}

// Sound-event namespace for the active weapon (cs.ak47.fire, cs.awp.draw, ...).
const char* soundPrefixForSlot(uint8_t slot) {
    switch (slot) {
        case 1: return "ak47";
        case 2: return "awp";
        case 3: return "m4a1_s";
        default: return "knife";
    }
}

} // namespace

namespace eldenring::input {

void CsGunplayHandler::reset() {
    cs_fire_cooldown_ = 0.f;
    cs_since_shot_ = 10.f;
    cs_reload_sound_stage_ = 0;
    prev_reload_r_ = false;
    prev_gameplay_f_ = false;
    prev_q_ = false;
    prev_buy_b_ = false;
    last_slot_ = 0;
    RuntimeContext::Instance().mode().cs_buy_open.store(false);
    for (uint32_t& ammo : slot_ammo_) ammo = 0;
    for (uint32_t& reserve : slot_reserve_) reserve = 0;
    for (bool& ready : slot_ammo_ready_) ready = false;
    for (bool& key : prev_digit_) key = false;
}

void CsGunplayHandler::selectWeaponSlot(uint8_t slot, const InputTickContext& ctx) {
#if ENABLE_CS2_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    if (!mode.cs_mode.load() || slot > 3) return;
    const uint8_t current = mode.cs_weapon_slot.load();
    // Pressing the key of the weapon already in hand is a no-op in CS2; it
    // must not refill the magazine or restart the deploy take.
    if (slot == current) return;

    // Remember each weapon's magazine so switching back does not refill it.
    if (current < 4) {
        slot_ammo_[current] = mode.cs_ammo.load();
        slot_reserve_[current] = mode.cs_reserve.load();
        slot_ammo_ready_[current] = true;
        syncSlotReserve(current);
    }
    last_slot_ = current;

    if (slot == 0) {
        mode.cs_ammo.store(0);
        mode.cs_reserve.store(0);
        mode.cs_mag_capacity.store(0);
    } else {
        if (!slot_ammo_ready_[slot]) {
            const auto* def = eldenring::cs::CSWeaponRegistry::instance().findWeapon(weaponIdForSlot(slot));
            slot_ammo_[slot] = def ? def->ballistics.magazine_capacity : 0u;
            slot_reserve_[slot] = def ? def->ballistics.reserve_capacity : 0u;
            slot_ammo_ready_[slot] = true;
        }
        const auto* def = eldenring::cs::CSWeaponRegistry::instance().findWeapon(weaponIdForSlot(slot));
        mode.cs_ammo.store(slot_ammo_[slot]);
        mode.cs_reserve.store(slot_reserve_[slot]);
        mode.cs_mag_capacity.store(def ? def->ballistics.magazine_capacity : slot_ammo_[slot]);
        syncSlotReserve(slot);
    }

    mode.cs_weapon_slot.store(slot);
    mode.cs_knife_state.store(4); // Deploy
    mode.cs_knife_inspect.store(false);
    mode.cs_action_variant.store(static_cast<uint8_t>((mode.cs_action_variant.load() + 1u) & 1u));
    mode.cs_action_hit.store(true);
    mode.cs_knife_anim_time.store(0.f);
    cs_reload_sound_stage_ = 0;
    // A freshly drawn weapon fires on its first click and is perfectly
    // accurate, matching CS2's first-shot behaviour after a switch.
    cs_fire_cooldown_ = 0.f;
    cs_since_shot_ = 10.f;
    if (ctx.cb->play_sound) {
        if (slot == 0) {
            ctx.cb->play_sound("cs.knife.deploy", 1.0f);
        } else {
            char event[48];
            snprintf(event, sizeof(event), "cs.%s.draw", soundPrefixForSlot(slot));
            ctx.cb->play_sound(event, 1.0f);
        }
    }
#else
    (void)slot;
    (void)ctx;
#endif
}

bool CsGunplayHandler::tickRightClick(const InputTickContext& ctx) {
#if ENABLE_CS2_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    if (mode.cs_mode.load() && ctx.fg && !ctx.is_dead && !mode.cs_buy_open.load() && ctx.right_edge) {
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
    mode.cs_money_flash.store(std::max(0.f, mode.cs_money_flash.load() - ctx.dt * 1.25f));
    mode.cs_ammo_flash.store(std::max(0.f, mode.cs_ammo_flash.load() - ctx.dt * 2.5f));
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
        return duration > 0.f ? duration : fallbackCsDuration(s, mode.cs_weapon_slot.load());
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
            const uint8_t slot = mode.cs_weapon_slot.load();
            if (slot < 4) {
                slot_ammo_[slot] = ammo + take;
                slot_reserve_[slot] = reserve - take;
                slot_ammo_ready_[slot] = true;
                syncSlotReserve(slot);
            }
            cs_reload_sound_stage_ = 0;
        }
        mode.cs_knife_state.store(0); // Return to Idle
        mode.cs_knife_inspect.store(false);
        mode.cs_knife_anim_time.store(0.f);
    } else {
        mode.cs_knife_anim_time.store(t);
        if (state == 5 && mode.cs_weapon_slot.load() != 0 && max_t > 0.f) {
            // Reload audio follows the authored clip events instead of one
            // sound at t=0. The frames come from the viewmodel sound-event
            // tracks where the clip has them, otherwise from the deployed
            // CSVM curves (tools/cs2/reload_events.py ->
            // cs_reload_events.hpp); stages the weapon does not have are
            // skipped.
            const uint8_t slot = mode.cs_weapon_slot.load();
            const char* prefix = soundPrefixForSlot(slot);
            const eldenring::cs::ReloadSoundEvents events =
                eldenring::cs::reloadSoundEventsForSlot(slot);
            eldenring::cs::ReloadSoundEvent stages[6];
            const int stage_count = eldenring::cs::reloadSoundEventList(events, stages);
            const float d = max_t;
            while (cs_reload_sound_stage_ < stage_count &&
                   t >= d * stages[cs_reload_sound_stage_].at) {
                if (ctx.cb->play_sound) {
                    char event[48];
                    snprintf(event, sizeof(event), "cs.%s.%s", prefix,
                             stages[cs_reload_sound_stage_].stage);
                    ctx.cb->play_sound(event, 1.0f);
                }
                ++cs_reload_sound_stage_;
            }
        } else if (state == 2 && mode.cs_weapon_slot.load() != 0 && max_t > 0.f) {
            // Post-shot mechanism sounds follow the authored shoot clip (the
            // AWP works its bolt); stages the weapon does not have are skipped.
            const uint8_t slot = mode.cs_weapon_slot.load();
            const char* prefix = soundPrefixForSlot(slot);
            const eldenring::cs::FireSoundEvents events =
                eldenring::cs::fireSoundEventsForSlot(slot);
            eldenring::cs::FireSoundEvent stages[2];
            const int stage_count = eldenring::cs::fireSoundEventList(events, stages);
            const float d = max_t;
            while (cs_fire_sound_stage_ < stage_count &&
                   t >= d * stages[cs_fire_sound_stage_].at) {
                if (ctx.cb->play_sound) {
                    char event[48];
                    snprintf(event, sizeof(event), "cs.%s.%s", prefix,
                             stages[cs_fire_sound_stage_].stage);
                    ctx.cb->play_sound(event, 1.0f);
                }
                ++cs_fire_sound_stage_;
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
    // Bank kill money before the buy keys run, so a payout from the last tick
    // can be spent in this one.
    tickEconomy(ctx);
    if (!mode.cs_mode.load()) mode.cs_buy_open.store(false);
    // B opens the buy panel; while it is open 1/2/3 buy a magazine and the
    // gameplay keys (F/Q/R) stay out of the way.
    const bool b_down = queryKeyDown(*ctx.cb, 'B');
    if (b_down && !prev_buy_b_ && mode.cs_mode.load()) {
        const bool open = !mode.cs_buy_open.load();
        mode.cs_buy_open.store(open);
        if (ctx.cb->log) ctx.cb->log(open ? "CS: buy menu open" : "CS: buy menu closed");
    }
    prev_buy_b_ = b_down;
    if (ctx.is_dead) mode.cs_buy_open.store(false);
#endif

    // F: inspect in CS mode. The coordinator calls the MC handler afterwards
    // for the offhand swap when CS does not own the key.
    const bool f_down = queryKeyDown(*ctx.cb, 'F');
    if (f_down && !prev_gameplay_f_) {
#if ENABLE_CS2_VIEWMODEL
        if (mode.cs_mode.load() && !mode.cs_buy_open.load()) {
            const uint8_t take = static_cast<uint8_t>((mode.cs_action_variant.load() + 1u) % 3u);
            mode.cs_action_variant.store(take);
            mode.cs_knife_state.store(1); // Inspect!
            mode.cs_knife_inspect.store(true);
            mode.cs_knife_anim_time.store(0.f);
            const uint8_t slot = mode.cs_weapon_slot.load();
            if (ctx.cb->play_sound) {
                if (slot == 0) {
                    ctx.cb->play_sound("cs.knife.inspect", 1.0f);
                } else {
                    char event[48];
                    snprintf(event, sizeof(event), "cs.%s.inspect", soundPrefixForSlot(slot));
                    ctx.cb->play_sound(event, 1.0f);
                }
            }
            if (ctx.cb->log) ctx.cb->log(slot == 0 ? "CS: Butterfly Knife Inspect (Flower Spin)" : "CS: weapon inspect");
        }
#endif
    }
    prev_gameplay_f_ = f_down;

    for (int i = 0; i < 9; ++i) {
        const bool d = queryKeyDown(*ctx.cb, '1' + i);
        if (d && !prev_digit_[i]) {
#if ENABLE_CS2_VIEWMODEL
            if (mode.cs_mode.load()) {
                if (mode.cs_buy_open.load()) {
                    if (i < 3) buyMagazine(static_cast<uint8_t>(i + 1), ctx);
                } else if (i < 4) {
                    // CS arsenal slots: 1 = AK-47, 2 = AWP, 3 = butterfly knife, 4 = M4A1-S
                    const uint8_t slot = (i == 0) ? 1 : (i == 1) ? 2 : (i == 2) ? 0 : 3;
                    selectWeaponSlot(slot, ctx);
                }
            }
#endif
        }
        prev_digit_[i] = d;
    }

#if ENABLE_CS2_VIEWMODEL
    // Q: CS2-style quick switch back to the previously held weapon.
    const bool q_down = queryKeyDown(*ctx.cb, 'Q');
    if (q_down && !prev_q_ && mode.cs_mode.load() && !mode.cs_buy_open.load()) {
        const uint8_t current = mode.cs_weapon_slot.load();
        uint8_t target = last_slot_;
        if (target == current) target = (current == 0) ? 1 : 0;
        selectWeaponSlot(target, ctx);
    }
    prev_q_ = q_down;
#endif

#if ENABLE_CS2_VIEWMODEL
    // R: reload the active gun (state 5). Knife has no reload.
    const bool r_down = queryKeyDown(*ctx.cb, 'R');
    if (r_down && !prev_reload_r_ && mode.cs_mode.load() && !mode.cs_buy_open.load() && mode.cs_weapon_slot.load() != 0) {
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
    tickEconomy(ctx);
    if (mode.cs_buy_open.load()) {
        return;
    }
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
            const auto* def = eldenring::cs::CSWeaponRegistry::instance().findWeapon(weaponIdForSlot(slot));
            const float rpm = (def && def->ballistics.fire_rate_rpm > 1.f) ? def->ballistics.fire_rate_rpm : 600.f;
            const bool automatic = def ? def->ballistics.is_automatic : true;
            const bool trigger = automatic ? ctx.left_held : ctx.click_edge;
            if (trigger && cs_fire_cooldown_ <= 0.f) {
                if (mode.cs_ammo.load() > 0) {
                    mode.cs_ammo.fetch_sub(1);
                    mode.cs_knife_state.store(2); // Fire
                    mode.cs_knife_inspect.store(false);
                    mode.cs_knife_anim_time.store(0.f);
                    cs_fire_sound_stage_ = 0;
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
                    if (ctx.cb->play_sound) {
                        char event[48];
                        snprintf(event, sizeof(event), "cs.%s.fire", soundPrefixForSlot(slot));
                        ctx.cb->play_sound(event, 1.0f);
                    }
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

void CsGunplayHandler::tickEconomy(const InputTickContext& ctx) {
#if ENABLE_CS2_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    const uint32_t gained = mode.cs_pending_money.exchange(0, std::memory_order_relaxed);
    if (gained == 0) return;
    const uint32_t balance = mode.cs_money.fetch_add(gained, std::memory_order_relaxed) + gained;
    mode.cs_money_gain.store(gained, std::memory_order_relaxed);
    mode.cs_money_flash.store(1.f, std::memory_order_relaxed);
    if (ctx.cb->play_sound) ctx.cb->play_sound("entity.experience_orb.pickup", 0.55f);
    if (ctx.cb->log) {
        char line[96];
        snprintf(line, sizeof(line), "CS: +$%u (balance $%u)", gained, balance);
        ctx.cb->log(line);
    }
#else
    (void)ctx;
#endif
}

void CsGunplayHandler::buyMagazine(uint8_t buy_key, const InputTickContext& ctx) {
#if ENABLE_CS2_VIEWMODEL
    auto& mode = RuntimeContext::Instance().mode();
    if (!mode.cs_mode.load() || !mode.cs_buy_open.load() || ctx.is_dead) return;
    const eldenring::cs::CsAmmoOffer* offer = eldenring::cs::csAmmoOfferForKey(buy_key);
    if (!offer) return;
    const uint8_t slot = offer->slot;
    const auto* def = eldenring::cs::CSWeaponRegistry::instance().findWeapon(offer->weapon_id);
    if (!def) return;
    const uint32_t cap = def->ballistics.reserve_capacity;
    // A slot that was never drawn still holds its first-draw reserve, so buying
    // now would only be overwritten by the full reserve on selection.
    if (!slot_ammo_ready_[slot]) {
        if (ctx.cb->log) ctx.cb->log("CS: buy refused - reserve already full");
        return;
    }
    const eldenring::cs::CsPurchase purchase = eldenring::cs::csBuyMagazine(
        mode.cs_money.load(), slot_reserve_[slot], cap, def->ballistics.magazine_capacity, offer->price);
    if (!purchase.bought) {
        if (ctx.cb->play_sound) ctx.cb->play_sound("cs.knife.clack", 0.4f);
        if (ctx.cb->log) {
            ctx.cb->log(slot_reserve_[slot] >= cap ? "CS: buy refused - reserve full"
                                                   : "CS: buy refused - not enough money");
        }
        return;
    }
    mode.cs_money.fetch_sub(purchase.spent, std::memory_order_relaxed);
    slot_reserve_[slot] = purchase.reserve;
    syncSlotReserve(slot);
    if (mode.cs_weapon_slot.load() == slot) mode.cs_reserve.store(purchase.reserve);
    mode.cs_ammo_flash.store(1.f, std::memory_order_relaxed);
    if (ctx.cb->play_sound) ctx.cb->play_sound("entity.experience_orb.pickup", 0.7f);
    if (ctx.cb->log) {
        char line[112];
        snprintf(line, sizeof(line), "CS: bought %s mag for $%u (reserve %u/%u)", offer->name,
                 purchase.spent, purchase.reserve, cap);
        ctx.cb->log(line);
    }
#else
    (void)buy_key;
    (void)ctx;
#endif
}

void CsGunplayHandler::syncSlotReserve(uint8_t slot) {
#if ENABLE_CS2_VIEWMODEL
    if (slot > 3) return;
    auto& mode = RuntimeContext::Instance().mode();
    mode.cs_slot_reserve[slot].store(slot_reserve_[slot], std::memory_order_relaxed);
    mode.cs_slot_ready[slot].store(slot_ammo_ready_[slot], std::memory_order_relaxed);
#else
    (void)slot;
#endif
}

} // namespace eldenring::input
