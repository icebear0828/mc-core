#if defined(WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "eldenring_inventory_controller.hpp"

#if defined(WIN32) && !defined(MC_CORE_TESTS)
#include "../input_hook.hpp"
#include "../overlay_d3d12.hpp"
#endif

namespace eldenring::ui {

void InventoryController::tickWithInput(const InventoryInputState& input, mc::Inventory& inv, RuntimeContext& ctx) {
    const bool want = input.fg && input.want_suppress && ctx.mode().mc_mode.load(std::memory_order_relaxed) && !input.player_dead;
    bool is_open = ctx.ui().inv_open.load(std::memory_order_relaxed);

    if (input.key_down && !prev_key_) {
        if (is_open) {
            close("key", inv, ctx);
            is_open = false;
        } else if (want && ctx.mode().input_enabled.load(std::memory_order_relaxed)) {
            open(input.screen_width, input.screen_height, ctx);
            is_open = true;
        }
    }

    if (is_open && input.esc_down && !prev_esc_) {
        close("Esc", inv, ctx);
        is_open = false;
    }

    if (is_open && !want) {
        close("menu, loading or MC mode off", inv, ctx);
        is_open = false;
    }

    prev_key_ = input.key_down;
    prev_esc_ = input.esc_down;

    if (!is_open) {
        for (bool& d : prev_digit_) d = false;
        return;
    }

    // Virtual pointer motion
    const float sens = ctx.ui().inv_sens.load(std::memory_order_relaxed);
    const float mx = std::clamp(ctx.ui().inv_mx.load(std::memory_order_relaxed) + static_cast<float>(input.mouse_dx) * sens,
                                0.f, std::max(0.f, input.screen_width - 1.f));
    const float my = std::clamp(ctx.ui().inv_my.load(std::memory_order_relaxed) + static_cast<float>(input.mouse_dy) * sens,
                                0.f, std::max(0.f, input.screen_height - 1.f));
    ctx.ui().inv_mx.store(mx, std::memory_order_relaxed);
    ctx.ui().inv_my.store(my, std::memory_order_relaxed);

    // Hit test against mc::InventoryLayout
    const mc::InventoryLayout lay(input.screen_width, input.screen_height, creative::showsPalette(ctx.currentGameMode()));
    const mc::SlotRef ref = lay.hitTest(mx, my);

    auto click = [&](mc::Button button) {
        switch (ref.kind) {
            case mc::SlotRef::Kind::Inventory: inv.click(ref.index, button, input.shift_down); break;
            case mc::SlotRef::Kind::Offhand: inv.clickOffhand(button, input.shift_down); break;
            case mc::SlotRef::Kind::Armor: inv.clickArmor(ref.index, button, input.shift_down); break;
            case mc::SlotRef::Kind::Crafting:
            case mc::SlotRef::Kind::CraftResult:
                break;
            case mc::SlotRef::Kind::Palette:
                if (static_cast<size_t>(ref.index) < mc::paletteItems().size()) {
                    inv.clickPalette(mc::paletteItems()[static_cast<size_t>(ref.index)], button);
                }
                break;
            case mc::SlotRef::Kind::None: inv.clickOutside(); break;
            case mc::SlotRef::Kind::Panel: break;
        }
    };

    if (input.left_click) click(mc::Button::Left);
    if (input.right_click) click(mc::Button::Right);

    if (input.f_down && !prev_f_) {
        inv.swapMainAndOffhand(ctx.combat().melee.selectedSlot());
    }
    prev_f_ = input.f_down;

    for (int i = 0; i < mc::Inventory::kHotbar; ++i) {
        if (input.digit_down[i] && !prev_digit_[i] && ref.kind == mc::SlotRef::Kind::Inventory) {
            inv.swapWithHotbar(ref.index, i);
        }
        prev_digit_[i] = input.digit_down[i];
    }
}

void InventoryController::open(float screen_w, float screen_h, RuntimeContext& ctx) {
    ctx.ui().inv_mx.store(screen_w * 0.5f, std::memory_order_relaxed);
    ctx.ui().inv_my.store(screen_h * 0.5f, std::memory_order_relaxed);
#if defined(WIN32) && !defined(MC_CORE_TESTS)
    int dx = 0, dy = 0;
    erin::TakeMouseDelta(dx, dy);
    erin::TakeLeftClick();
    erin::TakeRightClick();
    erin::TakeWheelNotches();
#endif
    if (callbacks_.set_suppress_mouse_motion) callbacks_.set_suppress_mouse_motion(true);
    if (callbacks_.set_suppress_keyboard) callbacks_.set_suppress_keyboard(true);
    ctx.ui().inv_open.store(true, std::memory_order_release);
    if (callbacks_.log) {
        char buf[128];
        snprintf(buf, sizeof(buf), "inventory: opened (%.0fx%.0f, gate calls so far %u)",
                 screen_w, screen_h, ctx.ui().isblocked_forced.load(std::memory_order_relaxed));
        callbacks_.log(buf);
    }
}

void InventoryController::close(const char* why, mc::Inventory& inv, RuntimeContext& ctx) {
    inv.returnCursor();
    ctx.ui().inv_open.store(false, std::memory_order_release);
    if (callbacks_.set_suppress_mouse_motion) callbacks_.set_suppress_mouse_motion(false);
    if (callbacks_.set_suppress_keyboard) callbacks_.set_suppress_keyboard(false);
    if (callbacks_.log) {
        char buf[128];
        snprintf(buf, sizeof(buf), "inventory: closed (%s)", why);
        callbacks_.log(buf);
    }
}

uint64_t __fastcall InventoryController::IsInputBlockedDetour() {
    auto& ctx = RuntimeContext::Instance();
    if (ctx.ui().inv_open.load(std::memory_order_relaxed)) {
        ctx.ui().isblocked_forced.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    if (s_isblocked_orig) return s_isblocked_orig();
    return 0;
}

uint8_t __fastcall InventoryController::MenuFreezeDetour(void* self) {
    auto& ctx = RuntimeContext::Instance();
    if (ctx.ui().inv_open.load(std::memory_order_relaxed)) {
        ctx.ui().menufreeze_forced.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    if (s_menufreeze_orig) return s_menufreeze_orig(self);
    return 0;
}

#if defined(WIN32) && !defined(MC_CORE_TESTS)
void InventoryController::tick(bool fg, bool want_suppress, bool player_dead, mc::Inventory& inv, RuntimeContext& ctx) {
    InventoryInputState input{};
    input.fg = fg;
    input.want_suppress = want_suppress;
    input.player_dead = player_dead;
    const int vk = ctx.ui().inv_vk.load(std::memory_order_relaxed);
    const bool key_custom = (vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0);
    const bool key_e = (GetAsyncKeyState('E') & 0x8000) != 0;
    const bool key_i = (GetAsyncKeyState('I') & 0x8000) != 0;
    input.key_down = fg && (key_custom || key_e || key_i);
    input.esc_down = fg && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    input.shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    input.f_down = (GetAsyncKeyState('F') & 0x8000) != 0;
    for (int i = 0; i < mc::Inventory::kHotbar; ++i) {
        input.digit_down[i] = (GetAsyncKeyState('1' + i) & 0x8000) != 0;
    }
    erov::ScreenSize(input.screen_width, input.screen_height);

    const bool is_open = ctx.ui().inv_open.load(std::memory_order_relaxed);
    if (is_open) {
        erin::TakeMouseDelta(input.mouse_dx, input.mouse_dy);
        input.left_click = erin::TakeLeftClick();
        input.right_click = erin::TakeRightClick();
        input.wheel_notches = erin::TakeWheelNotches();
    }

    tickWithInput(input, inv, ctx);
}
#endif

} // namespace eldenring::ui
