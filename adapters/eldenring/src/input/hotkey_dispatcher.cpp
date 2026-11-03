#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <windows.h>
#endif

#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_control_policy.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_state.hpp"
#include "eldenring_creative.hpp"
#include "eldenring_roll.hpp"
#include "eldenring_flight.hpp"
#include "input_keys.hpp"

#include <iterator>
#include <mutex>
#include <span>

namespace eldenring::input {

HotkeyDispatcher& HotkeyDispatcher::Instance() {
    static HotkeyDispatcher s_instance;
    return s_instance;
}

HotkeyDispatcher::HotkeyDispatcher() : debug_(ow_) {}

void HotkeyDispatcher::setCallbacks(const HotkeyCallbacks& cb) {
    cb_ = cb;
}

void HotkeyDispatcher::resetKeyStates() {
    cs_.reset();
    ow_.reset();
    mc_.reset();
    movement_.reset();
    debug_.reset();
}

bool HotkeyDispatcher::queryKey(int vk) const {
    return queryKeyDown(cb_, vk);
}

void HotkeyDispatcher::stop() {
    running_.store(false, std::memory_order_relaxed);
}

void HotkeyDispatcher::tick(float dt) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();
    auto& player = runtime.player();
    auto& combat = runtime.combat();
    auto& ui = runtime.ui();

    InputTickContext ctx;
    ctx.dt = dt;
    ctx.cb = &cb_;
    ctx.slot_cursor = &slot_cursor_;
    ctx.fg = cb_.is_game_in_foreground ? cb_.is_game_in_foreground() : true;
    ctx.want_suppress = cb_.want_suppress ? cb_.want_suppress() : false;
    ctx.is_dead = cb_.is_player_dead ? cb_.is_player_dead() : false;
    ctx.inv_open = ui.inv_open.load(std::memory_order_relaxed);
    ctx.mc_on = mode.mc_mode.load(std::memory_order_relaxed);

    // Tick all pluggable mods
    runtime.mods().tick(dt);

    // Melee & Feedback tick
    {
        std::lock_guard<std::mutex> g(combat.melee_mutex);
        combat.melee.tick(dt);
        combat.feedback.tick(dt);
    }

    movement_.tickSprint(ctx);
    movement_.tickFootsteps(ctx);

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
                if (creative::consumesItems(runtime.currentGameMode())) {
                    combat.melee.consumeAt(combat.melee.selectedSlot());
                }
            }
        }
        if (heal > 0) mode.heal_pending.fetch_add(heal);
    }

    // Subsystem ticks
    if (cb_.inventory_tick) cb_.inventory_tick(ctx.fg);
    if (cb_.blocks_tick) cb_.blocks_tick();
    if (cb_.mc_flags_step) cb_.mc_flags_step();

    // Control policy: one plan decides which native inputs the active modes own.
    // Adding a mode means adding a request in eldenring_control_policy.hpp, not
    // another branch here.
    // inventory_tick above may have just opened/closed the inventory, so read the
    // state again instead of using the stale value captured at the top of the tick.
    const bool inv_open_for_policy = ui.inv_open.load(std::memory_order_relaxed);
    ctx.inv_open_policy = inv_open_for_policy;
    eldenring::control::ModeInputs control_inputs;
    control_inputs.foreground = ctx.fg;
    control_inputs.input_enabled = mode.input_enabled.load(std::memory_order_relaxed);
    control_inputs.inventory_open = inv_open_for_policy;
    control_inputs.native_input_allowed = ctx.want_suppress;
    control_inputs.mc_mode = ctx.mc_on;
    control_inputs.cs_mode = mode.cs_mode.load(std::memory_order_relaxed);
    control_inputs.first_person = mode.first_person.load(std::memory_order_relaxed);
    control_inputs.mc_jump = mode.mc_jump.load(std::memory_order_relaxed);
    control_inputs.disable_vanilla_roll = mode.disable_vanilla_roll.load(std::memory_order_relaxed);
    control_inputs.flying = mode.flying.load(std::memory_order_relaxed);
    control_inputs.block_movement = mode.block_supported.load(std::memory_order_relaxed);
    control_inputs.block_move_keys =
        std::span<const int>(eldenring::control::kBlockMoveKeys, std::size(eldenring::control::kBlockMoveKeys));

    roll::RollMaskResult roll_mask{};
    if (control_inputs.disable_vanilla_roll && ctx.mc_on && ctx.fg && !inv_open_for_policy) {
        roll_mask = roll::computeRollMask(true, true, mode.roll_vk.load(std::memory_order_relaxed));
    }
    control_inputs.roll_keys = std::span<const int>(roll_mask.scancodes, roll_mask.count);
    control_inputs.flight_keys = std::span<const int>(flight::kHiddenKeys, std::size(flight::kHiddenKeys));

#ifdef _WIN32
    if (control_inputs.mc_jump && ctx.mc_on && ctx.fg && !inv_open_for_policy) {
        control_inputs.mc_jump_scan = static_cast<int>(MapVirtualKeyW(
            static_cast<UINT>(mode.mc_jump_vk.load(std::memory_order_relaxed)), MAPVK_VK_TO_VSC));
    }
#endif

#if ENABLE_CS2_VIEWMODEL && defined(_WIN32)
    // CS mode owns these keys. Hide them from the game, or R uses a quick item
    // while reloading and 1-4 select vanilla quick slots.
    int cs_scans[8] = {};
    size_t cs_scan_count = 0;
    if (control_inputs.cs_mode && ctx.fg && !inv_open_for_policy) {
        const int cs_keys[] = {'R', 'F', 'Q', 'B', '1', '2', '3', '4'};
        for (const int vk : cs_keys) {
            const int sc = static_cast<int>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
            if (sc > 0) cs_scans[cs_scan_count++] = sc;
        }
    }
    control_inputs.cs_keys = std::span<const int>(cs_scans, cs_scan_count);
#endif

#if defined(_WIN32) && ENABLE_OW_VIEWMODEL
    control_inputs.ow_mode = mode.ow_mode.load(std::memory_order_relaxed);
    int ow_scans[12] = {};
    size_t ow_scan_count = 0;
    if (control_inputs.ow_mode && ctx.fg && !inv_open_for_policy) {
        // Mask both Shift keys: VK_SHIFT alone maps to DIK_LSHIFT, and the native
        // sprint/roll still fires from DIK_RSHIFT. R and the quick slots are
        // native-only actions and must not fire while the gauntlet is out.
        const int ow_keys[] = {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, 'E', 'Q', 'R', '1', '2', '3', '4', VK_F7};
        for (const int vk : ow_keys) {
            const int sc = static_cast<int>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
            if (sc > 0) ow_scans[ow_scan_count++] = sc;
        }
    }
    control_inputs.ow_keys = std::span<const int>(ow_scans, ow_scan_count);
#endif

    const mc::ControlPlan control_plan = eldenring::control::buildControlPlan(control_inputs);
    mode.control_authority_bits.store(control_plan.packedAuthorities(), std::memory_order_relaxed);

    if (cb_.set_masked_keys) {
        cb_.set_masked_keys(control_plan.masked_keys.data(), control_plan.masked_key_count);
    }

    // Right-click interaction cascade: OW, then CS, then MC. Each handler
    // reports whether it owned the click so the chain keeps the original
    // if / else-if / else semantics.
    const bool input_enabled = mode.input_enabled.load(std::memory_order_relaxed);
    ctx.input_enabled = input_enabled;
    const bool right_edge = !ctx.inv_open && input_enabled && cb_.take_right_click && cb_.take_right_click();
    ctx.right_edge = right_edge;

    bool right_consumed = false;
#if ENABLE_OW_VIEWMODEL
    right_consumed = ow_.tickRightClick(ctx);
#endif
#if ENABLE_CS2_VIEWMODEL
    if (!right_consumed) right_consumed = cs_.tickRightClick(ctx);
#endif
    if (!right_consumed) mc_.tickRightClick(ctx);

    mc_.tickBow(ctx);
    mc_.tickTrident(ctx);
    mc_.tickShield(ctx);
    mc_.tickWheel(ctx);

    cs_.tickActionTimer(ctx);
    ow_.tickActionTimer(ctx);

    // F key (inspect in CS mode / swap offhand in MC) & Digits 1-9 (slot select / deploy)
    if (ctx.fg && !ctx.inv_open && ctx.want_suppress) {
        cs_.tickGameplayKeys(ctx);
        mc_.tickGameplayKeys(ctx);
    }

    if (input_enabled && cb_.set_suppress_mouse_buttons) {
        cb_.set_suppress_mouse_buttons(control_plan.blocksNative(mc::ControlDomain::MouseButtons));
    }
    if (cb_.set_suppress_mouse_wheel) {
        cb_.set_suppress_mouse_wheel(control_plan.blocksNative(mc::ControlDomain::MouseWheel));
    }
    if (cb_.set_suppress_mouse_motion) {
        cb_.set_suppress_mouse_motion(control_plan.blocksNative(mc::ControlDomain::MouseMotion));
    }
    if (cb_.set_suppress_keyboard) {
        cb_.set_suppress_keyboard(control_plan.blocksNative(mc::ControlDomain::Keyboard));
    }

    // Space key (Jump & Exhaustion)
    movement_.tickJump(ctx);

    // Left click / Mining vs Click Attack
    const bool alive = !ctx.is_dead;
    const bool click_edge = !ctx.inv_open && input_enabled && cb_.take_left_click && cb_.take_left_click() && alive;
    const bool left_held = !ctx.inv_open && input_enabled &&
                           ((cb_.left_held && cb_.left_held()) || queryKey(VK_LBUTTON)) && alive;
    ctx.click_edge = click_edge;
    ctx.left_held = left_held;
    ctx.want_suppress_click = (click_edge || left_held) ? ctx.want_suppress : false;

    ow_.tickAttack(ctx);
    cs_.tickAttack(ctx);

    // The generic MC swing belongs to MC mode, and to the CS knife (which shares
    // the melee path for its hit/miss take). OW owns its own attacks.
    bool cs_gun_equipped = false;
    bool cs_knife_owns_melee = false;
#if ENABLE_CS2_VIEWMODEL
    cs_gun_equipped = mode.cs_mode.load() && mode.cs_weapon_slot.load() != 0;
    cs_knife_owns_melee = mode.cs_mode.load() && mode.cs_weapon_slot.load() == 0;
#endif
    ctx.cs_gun_equipped = cs_gun_equipped;
    ctx.cs_knife_owns_melee = cs_knife_owns_melee;
    ctx.mc_melee_owner = ctx.mc_on || cs_knife_owns_melee;

    mc_.tickMelee(ctx);

    // Function keys, CS/OW view tuning and mode toggles.
    debug_.tick(ctx);

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
