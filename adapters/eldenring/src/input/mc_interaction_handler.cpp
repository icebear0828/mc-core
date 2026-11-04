#include "eldenring_input_handlers.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_state.hpp"
#include "eldenring_creative.hpp"
#include "eldenring_bow.hpp"
#include "eldenring_shield.hpp"
#include "input_keys.hpp"

#include <mutex>
#include <string>

namespace eldenring::input {

void McInteractionHandler::reset() {
    prev_gameplay_f_ = false;
    for (bool& key : prev_digit_) key = false;
}

void McInteractionHandler::tickRightClick(const InputTickContext& ctx) {
    if (!(ctx.right_edge && ctx.fg && ctx.want_suppress && !ctx.is_dead)) {
        return;
    }
    auto& runtime = RuntimeContext::Instance();
    auto& player = runtime.player();
    auto& combat = runtime.combat();

    bool handled = false;
    if (ctx.cb->try_ignite_tnt && ctx.cb->try_ignite_tnt()) handled = true;
    else if (ctx.cb->try_ignite_creeper && ctx.cb->try_ignite_creeper()) handled = true;
    else if (ctx.cb->try_place_block && ctx.cb->try_place_block()) handled = true;
    else if (ctx.mc_on && ctx.cb->try_use_spawn_egg && ctx.cb->try_use_spawn_egg()) handled = true;
    else if (ctx.mc_on && ctx.cb->use_elytra_firework && ctx.cb->use_elytra_firework()) handled = true;
    else if (ctx.mc_on && ctx.cb->throw_ender_pearl && ctx.cb->throw_ender_pearl()) handled = true;
    else {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        const mc::ItemId item = combat.melee.heldItem();
        if (item == mc::ItemId::Trident) return; // charged in tickTrident; never eaten
        if (!player.eating.isEating() && combat.melee.countAt(combat.melee.selectedSlot()) > 0 &&
            player.hunger.canEat(item) && player.eating.startEating(item)) {
            if (ctx.cb->log) ctx.cb->log("SURVIVAL: started eating item " + std::to_string(static_cast<int>(item)));
        }
    }
    (void)handled;
}

void McInteractionHandler::tickTrident(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();
    auto& combat = runtime.combat();

    bool holding = false;
    bool offhand = false;
    {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        const mc::ItemId main_item = combat.melee.heldItem();
        const unsigned main_count = combat.melee.countAt(combat.melee.selectedSlot());
        holding = main_count > 0 && main_item == mc::ItemId::Trident;
        // Like the bow: the offhand trident is used only when the main hand has no right-click action of its own.
        if (!holding && combat.melee.offhandCount() > 0 && combat.melee.offhandItem() == mc::ItemId::Trident &&
            !(main_count > 0 && mc::mainHandClaimsRightClick(main_item))) {
            holding = true;
            offhand = true;
        }
    }
    mode.trident_offhand.store(offhand, std::memory_order_relaxed);
    const bool usable = holding && mode.riptide_level.load(std::memory_order_relaxed) > 0 && ctx.mc_on && ctx.fg &&
                        ctx.want_suppress && !ctx.inv_open && ctx.input_enabled && !ctx.is_dead;
    const bool right_held = ctx.input_enabled &&
                            ((ctx.cb->right_held && ctx.cb->right_held()) || queryKeyDown(*ctx.cb, VK_RBUTTON));
    if (usable && right_held) {
        trident_charge_ticks_ += ctx.dt * 20.f;
        if (trident_charge_ticks_ > 20.f) trident_charge_ticks_ = 20.f;
        mode.trident_charge.store(std::min(1.f, trident_charge_ticks_ / 10.f), std::memory_order_relaxed);
        trident_charging_ = true;
    } else {
        if (trident_charging_ && trident_charge_ticks_ >= 10.f && usable && ctx.cb->start_riptide) {
            ctx.cb->start_riptide();
        }
        trident_charging_ = false;
        trident_charge_ticks_ = 0.f;
        mode.trident_charge.store(0.f, std::memory_order_relaxed);
    }
}

void McInteractionHandler::tickBow(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();
    auto& combat = runtime.combat();

    bool usable = false;
    {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        combat.melee.select(combat.melee.selectedSlot());
        const bool in_offhand = bow::bowInOffhand(combat.melee.heldItem(), combat.melee.countAt(combat.melee.selectedSlot()),
                                                  combat.melee.offhandItem(), combat.melee.offhandCount());
        mode.bow_offhand.store(in_offhand, std::memory_order_relaxed);
        usable = bow::bowHandUsable(combat.melee.heldItem(), combat.melee.countAt(combat.melee.selectedSlot()),
                                    combat.melee.offhandItem(), combat.melee.offhandCount()) &&
                 (!creative::consumesItems(runtime.currentGameMode()) || combat.melee.has(mc::ItemId::Arrow));
    }
    usable = usable && mode.bullet_fire_enabled.load() && ctx.mc_on && ctx.fg && ctx.want_suppress &&
             !ctx.inv_open && ctx.input_enabled && !ctx.is_dead;
    const bool right_held = ctx.input_enabled &&
                            ((ctx.cb->right_held && ctx.cb->right_held()) || queryKeyDown(*ctx.cb, VK_RBUTTON));
    const bow::Update u = bow_.update(ctx.dt, right_held, usable);
    mode.bow_power.store(bow_.power(), std::memory_order_relaxed);
    mode.bow_ticks.store(bow_.ticks(), std::memory_order_relaxed);
    if (u.released) {
        const float damage = bow::damageForPower(u.power);
        if (ctx.cb->queue_bullet_fire && ctx.cb->queue_bullet_fire(damage)) {
            if (creative::consumesItems(runtime.currentGameMode())) {
                std::lock_guard<std::mutex> g(combat.melee_mutex);
                combat.melee.consumeFirst(mc::ItemId::Arrow);
            }
            if (ctx.cb->play_sound) ctx.cb->play_sound("entity.arrow.shoot", 1.0f);
        }
    }
}

void McInteractionHandler::tickShield(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();
    auto& player = runtime.player();
    auto& combat = runtime.combat();

    bool has_shield = false;
    bool shield_in_offhand = false;
    {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        has_shield = combat.melee.holds(mc::ItemId::Shield);
        shield_in_offhand = !(combat.melee.heldItem() == mc::ItemId::Shield && combat.melee.countAt(combat.melee.selectedSlot()) > 0);
    }
    mode.shield_offhand.store(shield_in_offhand, std::memory_order_relaxed);
    const bool right_held = ctx.input_enabled &&
                            ((ctx.cb->right_held && ctx.cb->right_held()) || queryKeyDown(*ctx.cb, VK_RBUTTON)) &&
                            ctx.fg && ctx.want_suppress && !ctx.inv_open && !ctx.is_dead && ctx.mc_on;
    const bool is_drawing_bow = mode.bow_ticks.load(std::memory_order_relaxed) > 0;
    const bool is_eating = player.eating.isEating();
    const bool shield_usable = has_shield && !is_drawing_bow && !is_eating;
    shield_.update(ctx.dt, right_held, shield_usable);
    combat.shield.update(ctx.dt, right_held, shield_usable);
    const bool prev_blocking = mode.shield_blocking.load(std::memory_order_relaxed);
    const bool now_blocking = shield_.isBlocking();
    if (!prev_blocking && now_blocking) {
        player.sprint.cancel();
        player.sprint_active.store(false, std::memory_order_relaxed);
    }
    mode.shield_blocking.store(now_blocking, std::memory_order_relaxed);
}

void McInteractionHandler::tickWheel(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& combat = runtime.combat();
    // MC hotbar only; CS mode suppresses the wheel. While the inventory is open
    // the wheel belongs to the palette pager, so do not drain it here.
    if (ctx.input_enabled && ctx.mc_on && !ctx.inv_open_policy && ctx.cb->take_wheel_notches) {
        const int notches = ctx.cb->take_wheel_notches();
        if (notches != 0 && ctx.fg && ctx.want_suppress) {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            combat.melee.scroll(-notches);
        }
    }
}

void McInteractionHandler::tickGameplayKeys(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& combat = runtime.combat();
    auto& mode = runtime.mode();

    // F: swap main and offhand in MC. CS mode owns the key for inspect instead.
    const bool f_down = queryKeyDown(*ctx.cb, 'F');
    if (f_down && !prev_gameplay_f_) {
        if (!mode.cs_mode.load() && ctx.mc_on) {
            mc::contracts::ModInputEvent f_ev{'F', true};
            if (!runtime.mods().dispatchInput(f_ev)) {
                std::lock_guard<std::mutex> g(combat.melee_mutex);
                combat.melee.inventory().swapMainAndOffhand(combat.melee.selectedSlot());
                if (ctx.cb->log) ctx.cb->log("inventory: swapped main and offhand via F key");
            }
        }
    }
    prev_gameplay_f_ = f_down;

    for (int i = 0; i < 9; ++i) {
        const bool d = queryKeyDown(*ctx.cb, '1' + i);
        if (d && !prev_digit_[i]) {
            // CS weapon slots 1-4 own the key while CS mode is active.
            if (!(mode.cs_mode.load() && i < 4) && ctx.mc_on) {
                std::lock_guard<std::mutex> g(combat.melee_mutex);
                combat.melee.select(i);
            }
        }
        prev_digit_[i] = d;
    }
}

void McInteractionHandler::tickMelee(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();
    auto& player = runtime.player();
    auto& combat = runtime.combat();

    const bool mining_consumed = ctx.mc_on && ctx.fg && mode.click_attack.load() && ctx.want_suppress_click &&
                                 ctx.cb->step_mining && ctx.cb->step_mining(ctx.dt, ctx.left_held);
    if (ctx.mc_melee_owner && !mining_consumed && !ctx.cs_gun_equipped && ctx.click_edge && ctx.fg &&
        mode.click_attack.load() && ctx.want_suppress_click) {
        float charged = 1.0f;
        {
            std::lock_guard<std::mutex> g(combat.melee_mutex);
            charged = combat.melee.startSwing();
            player.hunger.addExhaustion(0.1f);
        }
        bool hit = true;
        if (mode.damage_enabled.load() && ctx.cb->click_attack) {
            hit = ctx.cb->click_attack(charged);
        }
#if ENABLE_CS2_VIEWMODEL
        // The knife shares the MC melee path; use its result for the hit/miss take.
        if (mode.cs_mode.load() && mode.cs_weapon_slot.load() == 0) {
            mode.cs_action_hit.store(hit);
        }
#endif
    }
}

} // namespace eldenring::input
