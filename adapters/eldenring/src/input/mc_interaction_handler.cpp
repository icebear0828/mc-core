#include "eldenring_input_handlers.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_state.hpp"
#include "eldenring_creative.hpp"
#include "eldenring_bow.hpp"
#include "eldenring_shield.hpp"
#include "input_keys.hpp"

#include <algorithm>
#include <mutex>
#include <string>

namespace eldenring::input {

namespace {
// Minecraft repeats a held right-click placement every 4 ticks (0.2 s).
constexpr float kPlaceRepeatSec = 0.2f;

bool rightButtonDown(const InputTickContext& ctx) {
    return (ctx.cb->right_held && ctx.cb->right_held()) || queryKeyDown(*ctx.cb, VK_RBUTTON);
}
} // namespace

void McInteractionHandler::reset() {
    prev_gameplay_f_ = false;
    for (bool& key : prev_digit_) key = false;
    use_.reset();
    trident_charge_ticks_ = 0.f;
    trident_charging_ = false;
    bow_.update(0.f, false, false);    // cancel a draw
    shield_.update(0.f, false, false); // lower the shield
    auto& mode = RuntimeContext::Instance().mode();
    mode.bow_power.store(0.f, std::memory_order_relaxed);
    mode.bow_ticks.store(0.f, std::memory_order_relaxed);
    mode.trident_charge.store(0.f, std::memory_order_relaxed);
    mode.shield_blocking.store(false, std::memory_order_relaxed);
    mode.eating_offhand.store(false, std::memory_order_relaxed);
}

// One entry point for every right-click item. The item-use table says what each item does; the
// resolver says which hand can consume this click (main first, offhand only when the main hand's
// use cannot start); the controllers below implement the four hold kinds. Adding a new
// right-click item is one row in mc::useSpec, not another branch here.
void McInteractionHandler::tickUse(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();
    auto& player = runtime.player();
    auto& combat = runtime.combat();

    const bool active = ctx.fg && ctx.want_suppress && !ctx.is_dead && !ctx.inv_open && ctx.input_enabled && ctx.mc_on;
    const bool right_down = active && rightButtonDown(ctx);

    // One inventory snapshot for both hands and the arrow check.
    mc::ItemId main_item = mc::ItemId::None;
    mc::ItemId off_item = mc::ItemId::None;
    unsigned main_count = 0;
    unsigned off_count = 0;
    bool has_arrow = false;
    {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        main_item = combat.melee.heldItem();
        main_count = combat.melee.countAt(combat.melee.selectedSlot());
        off_item = combat.melee.offhandItem();
        off_count = combat.melee.offhandCount();
        has_arrow = combat.melee.has(mc::ItemId::Arrow);
    }
    const mc::ItemId main = main_count > 0 ? main_item : mc::ItemId::None;
    const mc::ItemId off = off_count > 0 ? off_item : mc::ItemId::None;

    // A use that lost its item or its condition ends now. The check is cheap on purpose: the
    // availability probe (a raycast for placement, a creeper scan for ignition) only runs on the
    // click edge.
    if (use_ && !useStillValid(*use_, main, off, has_arrow)) {
        if (use_->kind == mc::UseKind::Eat) mode.eating_offhand.store(false, std::memory_order_relaxed);
        use_.reset();
    }

    // A fresh click starts the first hand that can consume it.
    bool started_this_tick = false;
    if (active && !use_ && ctx.right_edge) {
        auto availabilityFor = [&](mc::Hand hand, mc::ItemId item) {
            mc::UseAvailability available;
            if (item == mc::ItemId::None) return available;
            switch (mc::useSpec(item).kind) {
                case mc::UseKind::Place:
                    available.place = ctx.cb->can_place_block && ctx.cb->can_place_block(hand);
                    break;
                case mc::UseKind::Ignite:
                    available.ignite = ctx.cb->can_ignite && ctx.cb->can_ignite(hand);
                    break;
                case mc::UseKind::SpawnEgg: available.spawn_egg = true; break;
                case mc::UseKind::Eat: available.eat = player.hunger.canEat(item); break;
                case mc::UseKind::Bow:
                    available.bow = !creative::consumesItems(runtime.currentGameMode()) || has_arrow;
                    break;
                case mc::UseKind::Shield: available.shield = true; break;
                case mc::UseKind::Trident:
                    available.trident = mode.riptide_level.load(std::memory_order_relaxed) > 0;
                    break;
                case mc::UseKind::ThrowPearl: available.throw_pearl = true; break;
                case mc::UseKind::Firework:
                    available.firework = mode.elytra_gliding.load(std::memory_order_relaxed);
                    break;
                default: break;
            }
            return available;
        };
        const auto resolution = mc::resolveUse(main, off, availabilityFor(mc::Hand::Main, main),
                                               availabilityFor(mc::Hand::Off, off));
        if (resolution.has_value()) {
            startUse(*resolution, ctx);
            started_this_tick = true;
        }
    }

    // Presentation follows the hand that actually owns the use, not a category rule.
    mode.bow_offhand.store(use_ && use_->kind == mc::UseKind::Bow && use_->hand == mc::Hand::Off,
                           std::memory_order_relaxed);
    mode.shield_offhand.store(use_ && use_->kind == mc::UseKind::Shield && use_->hand == mc::Hand::Off,
                              std::memory_order_relaxed);
    mode.trident_offhand.store(use_ && use_->kind == mc::UseKind::Trident && use_->hand == mc::Hand::Off,
                               std::memory_order_relaxed);

    // The hold controllers see every frame, including the release frame, so a released bow fires,
    // a released trident riptides and a released shield lowers. When no use owns the click they
    // receive active=false and cancel cleanly.
    tickBowControl(ctx, use_ && use_->kind == mc::UseKind::Bow, right_down, mode, combat);
    tickShieldControl(ctx, use_ && use_->kind == mc::UseKind::Shield, right_down, mode, player, combat);
    tickTridentControl(ctx, use_ && use_->kind == mc::UseKind::Trident, right_down, mode);

    if (!use_) return;
    switch (use_->kind) {
        case mc::UseKind::Bow:
        case mc::UseKind::Shield:
        case mc::UseKind::Trident:
            if (!right_down) use_.reset();
            break;
        case mc::UseKind::Eat:
            // Eating runs to completion like the old single-click meal; releasing does not cancel
            // it, but the session blocks another use until the meal is done.
            if (!player.eating.isEating()) {
                mode.eating_offhand.store(false, std::memory_order_relaxed);
                use_.reset();
            }
            break;
        case mc::UseKind::Place:
            if (!right_down) {
                use_.reset();
            } else if (!started_this_tick) {
                // The edge tick already placed once; the cadence starts on the next frame.
                use_->repeat_sec += ctx.dt;
                if (use_->repeat_sec >= kPlaceRepeatSec) {
                    use_->repeat_sec = 0.f;
                    useInstant(*use_, ctx);
                }
            }
            break;
        default:
            use_.reset();
            break;
    }
}

void McInteractionHandler::startUse(const mc::UseResolution& resolution, const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();
    auto& player = runtime.player();
    auto& combat = runtime.combat();

    mc::ItemId item = mc::ItemId::None;
    {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        item = resolution.hand == mc::Hand::Main ? combat.melee.heldItem() : combat.melee.offhandItem();
    }

    switch (resolution.kind) {
        case mc::UseKind::Eat:
            if (player.hunger.canEat(item) && player.eating.startEating(item)) {
                mode.eating_offhand.store(resolution.hand == mc::Hand::Off, std::memory_order_relaxed);
                use_ = ActiveUse{resolution.hand, item, resolution.kind, 0.f};
                if (ctx.cb->log) ctx.cb->log("SURVIVAL: started eating item " + std::to_string(static_cast<int>(item)));
            }
            return;
        case mc::UseKind::Place:
            use_ = ActiveUse{resolution.hand, item, resolution.kind, 0.f};
            useInstant(*use_, ctx);
            return;
        case mc::UseKind::Bow:
        case mc::UseKind::Shield:
        case mc::UseKind::Trident:
            use_ = ActiveUse{resolution.hand, item, resolution.kind, 0.f};
            return;
        default:
            // Ignite / ThrowPearl / SpawnEgg / Firework: one click, no session.
            useInstant(ActiveUse{resolution.hand, item, resolution.kind, 0.f}, ctx);
            return;
    }
}

bool McInteractionHandler::useInstant(const ActiveUse& use, const InputTickContext& ctx) {
    switch (use.kind) {
        case mc::UseKind::Place: return ctx.cb->try_place_block && ctx.cb->try_place_block(use.hand);
        case mc::UseKind::Ignite:
            return (ctx.cb->try_ignite_tnt && ctx.cb->try_ignite_tnt(use.hand)) ||
                   (ctx.cb->try_ignite_creeper && ctx.cb->try_ignite_creeper(use.hand));
        case mc::UseKind::SpawnEgg: return ctx.cb->try_use_spawn_egg && ctx.cb->try_use_spawn_egg(use.hand);
        case mc::UseKind::ThrowPearl: return ctx.cb->throw_ender_pearl && ctx.cb->throw_ender_pearl(use.hand);
        case mc::UseKind::Firework: return ctx.cb->use_elytra_firework && ctx.cb->use_elytra_firework(use.hand);
        default: return false;
    }
}

bool McInteractionHandler::useStillValid(const ActiveUse& use, mc::ItemId main, mc::ItemId off, bool has_arrow) const {
    const mc::ItemId item = use.hand == mc::Hand::Main ? main : off;
    if (item == mc::ItemId::None || item != use.item) return false;
    if (mc::useSpec(item).kind != use.kind) return false;
    switch (use.kind) {
        case mc::UseKind::Eat: return RuntimeContext::Instance().player().eating.isEating();
        case mc::UseKind::Bow:
            return !creative::consumesItems(RuntimeContext::Instance().currentGameMode()) || has_arrow;
        case mc::UseKind::Trident:
            return RuntimeContext::Instance().mode().riptide_level.load(std::memory_order_relaxed) > 0;
        default: return true;
    }
}

void McInteractionHandler::tickBowControl(const InputTickContext& ctx, bool active, bool right_down,
                                          ModeConfig& mode, CombatSystems& combat) {
    const bow::Update u = bow_.update(ctx.dt, right_down, active);
    mode.bow_power.store(bow_.power(), std::memory_order_relaxed);
    mode.bow_ticks.store(bow_.ticks(), std::memory_order_relaxed);
    if (u.released) {
        const float damage = bow::damageForPower(u.power);
        if (ctx.cb->queue_bullet_fire && ctx.cb->queue_bullet_fire(damage)) {
            if (creative::consumesItems(RuntimeContext::Instance().currentGameMode())) {
                std::lock_guard<std::mutex> g(combat.melee_mutex);
                combat.melee.consumeFirst(mc::ItemId::Arrow);
            }
            if (ctx.cb->play_sound) ctx.cb->play_sound("entity.arrow.shoot", 1.0f);
        }
    }
}

void McInteractionHandler::tickShieldControl(const InputTickContext& ctx, bool active, bool right_down,
                                             ModeConfig& mode, PlayerSystems& player, CombatSystems& combat) {
    shield_.update(ctx.dt, right_down, active);
    combat.shield.update(ctx.dt, right_down, active);
    const bool prev_blocking = mode.shield_blocking.load(std::memory_order_relaxed);
    const bool now_blocking = shield_.isBlocking();
    if (!prev_blocking && now_blocking) {
        player.sprint.cancel();
        player.sprint_active.store(false, std::memory_order_relaxed);
    }
    mode.shield_blocking.store(now_blocking, std::memory_order_relaxed);
}

void McInteractionHandler::tickTridentControl(const InputTickContext& ctx, bool active, bool right_down, ModeConfig& mode) {
    if (active && right_down) {
        trident_charge_ticks_ += ctx.dt * 20.f;
        if (trident_charge_ticks_ > 20.f) trident_charge_ticks_ = 20.f;
        mode.trident_charge.store(std::min(1.f, trident_charge_ticks_ / 10.f), std::memory_order_relaxed);
        trident_charging_ = true;
    } else {
        if (trident_charging_ && trident_charge_ticks_ >= 10.f && active && ctx.cb->start_riptide) {
            ctx.cb->start_riptide(use_ ? use_->hand : mc::Hand::Main);
        }
        trident_charging_ = false;
        trident_charge_ticks_ = 0.f;
        mode.trident_charge.store(0.f, std::memory_order_relaxed);
    }
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
