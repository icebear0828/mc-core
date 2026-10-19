#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "eldenring_runtime_context.hpp"
#include "mc/inventory.hpp"
#include "mc/inventory_layout.hpp"

namespace eldenring::ui {

struct InventoryInputState {
    bool fg{false};
    bool want_suppress{false};
    bool player_dead{false};
    bool key_down{false};
    bool esc_down{false};
    bool shift_down{false};
    bool f_down{false};
    bool digit_down[mc::Inventory::kHotbar]{};
    int mouse_dx{0};
    int mouse_dy{0};
    bool left_click{false};
    bool right_click{false};
    int wheel_notches{0};
    float screen_width{1920.f};
    float screen_height{1080.f};
};

struct InventoryCallbacks {
    // Host input suppression is applied by HotkeyDispatcher from the ControlPlan;
    // the inventory controller only owns its open/close state and UI logic.
    std::function<void(const char*)> log;
};

class InventoryController {
public:
    static InventoryController& Instance() {
        static InventoryController s_instance;
        return s_instance;
    }

    void setCallbacks(InventoryCallbacks callbacks) {
        callbacks_ = std::move(callbacks);
    }

    // Direct, deterministic tick used both by loader and unit tests
    void tickWithInput(const InventoryInputState& input, mc::Inventory& inv, RuntimeContext& ctx);

    // Live tick that samples Windows input and drives tickWithInput
    void tick(bool fg, bool want_suppress, bool player_dead, mc::Inventory& inv, RuntimeContext& ctx);

    void open(float screen_w, float screen_h, RuntimeContext& ctx);
    void close(const char* why, mc::Inventory& inv, RuntimeContext& ctx);

    bool isOpen(const RuntimeContext& ctx) const {
        return ctx.ui().inv_open.load(std::memory_order_relaxed);
    }

    // Check if a virtual key matches the inventory toggle key:
    // returns true if key equals configured inv_vk, or 'E' (0x45), or 'I' (0x49 / 73)
    static bool isToggleKey(int vk, int configured_vk) {
        if (vk == 0) return false;
        if (configured_vk > 0 && vk == configured_vk) return true;
        if (vk == 'E' || vk == 'e' || vk == 0x45) return true;
        if (vk == 'I' || vk == 'i' || vk == 0x49 || vk == 73) return true;
        return false;
    }

    // Detours for Elden Ring input gate & camera freeze
    static uint64_t __fastcall IsInputBlockedDetour();
    static uint8_t __fastcall MenuFreezeDetour(void* self);

    using IsInputBlockedFn = uint64_t(__fastcall*)();
    using MenuFreezeFn = uint8_t(__fastcall*)(void*);

    static void setOriginalHooks(IsInputBlockedFn is_blocked, MenuFreezeFn menu_freeze) {
        s_isblocked_orig = is_blocked;
        s_menufreeze_orig = menu_freeze;
    }

    static IsInputBlockedFn originalIsInputBlocked() { return s_isblocked_orig; }
    static MenuFreezeFn originalMenuFreeze() { return s_menufreeze_orig; }

    void resetForTesting() {
        prev_key_ = false;
        prev_esc_ = false;
        prev_f_ = false;
        for (bool& d : prev_digit_) d = false;
    }

private:
    InventoryController() = default;

    InventoryCallbacks callbacks_;
    bool prev_key_{false};
    bool prev_esc_{false};
    bool prev_f_{false};
    bool prev_digit_[mc::Inventory::kHotbar]{};

    static inline IsInputBlockedFn s_isblocked_orig{nullptr};
    static inline MenuFreezeFn s_menufreeze_orig{nullptr};
};

} // namespace eldenring::ui
