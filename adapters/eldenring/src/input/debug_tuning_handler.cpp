#include "eldenring_input_handlers.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_creative.hpp"
#include "input_keys.hpp"

#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

namespace eldenring::input {

DebugTuningHandler::DebugTuningHandler(OwHeroSkillHandler& ow) : ow_(ow) {}

void DebugTuningHandler::reset() {
    prev8_ = false;
    prev6_ = false;
    prev7_ = false;
    prev9_ = false;
    prev10_ = false;
    prev11_ = false;
    prev12_ = false;
    prev5_ = false;
    prev4_ = false;
    prev_f3_ = false;
    prev_f2_ = false;
    prev_f1_ = false;
    for (bool& key : prev_owview_keys_) key = false;
}

void DebugTuningHandler::tick(const InputTickContext& ctx) {
    auto& runtime = RuntimeContext::Instance();
    auto& mode = runtime.mode();
    const HotkeyCallbacks& cb = *ctx.cb;
    const bool fg = ctx.fg;

#if ENABLE_CS2_VIEWMODEL
    // F1 toggles CS mode.
    const bool d1 = fg && queryKeyDown(cb, VK_F1);
    if (d1 && !prev_f1_) {
        const bool new_cs = !mode.cs_mode.load();
        mode.cs_mode.store(new_cs);
        if (new_cs) {
            // CS mode is standalone: stop every MC and OW behaviour while it is active.
            mode.mc_mode.store(false);
            mode.ow_mode.store(false);
            mode.first_person.store(true);
            mode.cs_knife_state.store(4); // Deploy
            mode.cs_knife_inspect.store(false);
            mode.cs_action_variant.store(0);
            mode.cs_action_hit.store(true);
            mode.cs_knife_anim_time.store(0.f);
            if (cb.play_sound) cb.play_sound("cs.knife.deploy", 1.0f);
            if (cb.log) cb.log("[MODE] Counter-Strike Weapon Mode: ACTIVE (MC/OW mode off)");
        } else {
            mode.cs_knife_state.store(0);
            mode.cs_knife_inspect.store(false);
            mode.cs_action_variant.store(0);
            mode.cs_action_hit.store(true);
            mode.cs_knife_anim_time.store(0.f);
#if ENABLE_MC_MODE
            // Combined build: leaving CS returns to the MC mode it replaced.
            mode.mc_mode.store(true);
#endif
            if (cb.log) cb.log("[MODE] Counter-Strike Weapon Mode: OFF");
        }
    }
    prev_f1_ = d1;

    // CS viewmodel tuning keys. [ ] move the view along Z (forward / back),
    // , . along X, ; ' along Y, - = change FOV. Shift gives a 5x step.
    if (mode.cs_mode.load() && cb.adjust_cs_view) {
        static const int kVk[8] = {VK_OEM_4, VK_OEM_6, VK_OEM_COMMA, VK_OEM_PERIOD,
                                   VK_OEM_1, VK_OEM_7, VK_OEM_MINUS, VK_OEM_PLUS};
        static const int kAxis[8] = {2, 2, 0, 0, 1, 1, 3, 3};
        static const float kDir[8] = {-1.f, 1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f};
        const bool shift = queryKeyDown(cb, VK_SHIFT);
        for (int i = 0; i < 8; ++i) {
            const bool down = fg && queryKeyDown(cb, kVk[i]);
            if (down && !prev_csview_keys_[i]) {
                const float step = (kAxis[i] == 3) ? (shift ? 5.f : 1.f) : (shift ? 0.05f : 0.01f);
                cb.adjust_cs_view(kAxis[i], kDir[i] * step);
            }
            prev_csview_keys_[i] = down;
        }
    }
#endif

    // OW viewmodel tuning keys, mirroring the CS set: [ ] move along Z, , . along
    // X, ; ' along Y, - = change FOV, \ / change scale. Shift gives a 5x step.
#if ENABLE_OW_VIEWMODEL
    if (mode.ow_mode.load() && cb.adjust_ow_view) {
        static const int kVk[10] = {VK_OEM_4, VK_OEM_6, VK_OEM_COMMA, VK_OEM_PERIOD,
                                    VK_OEM_1, VK_OEM_7, VK_OEM_MINUS, VK_OEM_PLUS,
                                    VK_OEM_5, VK_OEM_2};
        static const int kAxis[10] = {2, 2, 0, 0, 1, 1, 3, 3, 4, 4};
        static const float kDir[10] = {-1.f, 1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f};
        const bool shift = queryKeyDown(cb, VK_SHIFT);
        for (int i = 0; i < 10; ++i) {
            const bool down = fg && queryKeyDown(cb, kVk[i]);
            if (down && !prev_owview_keys_[i]) {
                const float step = (kAxis[i] == 3) ? (shift ? 5.f : 1.f)
                                 : (kAxis[i] == 4) ? (shift ? 0.1f : 0.02f)
                                 : (shift ? 0.05f : 0.01f);
                cb.adjust_ow_view(kAxis[i], kDir[i] * step);
            }
            prev_owview_keys_[i] = down;
        }
    }

    // OW muzzle-flash debug: PageUp toggles the additive pass depth test,
    // PageDown cycles the flash scale. Live, no rebuild or file edits.
    if (mode.ow_mode.load() && fg) {
        static const float kScales[] = {0.002f, 0.004f, 0.006f, 0.008f, 0.012f};
        const bool vfx_depth_key = queryKeyDown(cb, VK_PRIOR);
        if (vfx_depth_key && !prev_owvfx_keys_[0]) {
            const bool nodepth = !mode.ow_vfx_nodepth.load();
            mode.ow_vfx_nodepth.store(nodepth);
            if (cb.log) cb.log(nodepth ? "OW VFX depth: off" : "OW VFX depth: on");
        }
        prev_owvfx_keys_[0] = vfx_depth_key;
        const bool vfx_scale_key = queryKeyDown(cb, VK_NEXT);
        if (vfx_scale_key && !prev_owvfx_keys_[1]) {
            const float current = mode.ow_vfx_scale.load();
            size_t idx = 0;
            for (size_t i = 0; i < std::size(kScales); ++i) {
                if (std::fabs(kScales[i] - current) < 1e-5f) {
                    idx = i;
                    break;
                }
            }
            idx = (idx + 1) % std::size(kScales);
            mode.ow_vfx_scale.store(kScales[idx]);
            if (cb.log) {
                char msg[64];
                snprintf(msg, sizeof(msg), "OW VFX scale: %.4f", kScales[idx]);
                cb.log(msg);
            }
        }
        prev_owvfx_keys_[1] = vfx_scale_key;
    }
#endif

    const bool d8 = fg && queryKeyDown(cb, VK_F8);
    if (d8 && !prev8_ && mode.damage_enabled.load() && cb.enqueue_nearest_hostile) {
        cb.enqueue_nearest_hostile();
    }
    prev8_ = d8;

    const bool d5 = fg && queryKeyDown(cb, VK_F5);
    if (d5 && !prev5_) {
        const auto next_mode = creative::toggled(runtime.currentGameMode());
        mode.game_mode.store(static_cast<uint8_t>(next_mode));
#ifdef _WIN32
        mode.mode_toast_ms.store(GetTickCount64());
#endif
        if (cb.log) cb.log(std::string("F5: ") + creative::modeName(next_mode));
    }
    prev5_ = d5;

    const bool d6 = fg && queryKeyDown(cb, VK_F6);
    if (d6 && !prev6_) {
        if (mode.ow_mode.load()) {
            // F6 leaves OW without silently turning another mode on.
            mode.ow_mode.store(false);
            mode.ow_state.store(0);
            mode.ow_anim_time.store(0.f);
            ow_.stopChargeLoop(cb);
            if (cb.log) cb.log("[MODE] Overwatch Mode: OFF (via F6)");
        } else if (mode.cs_mode.load()) {
#if ENABLE_CS2_VIEWMODEL
            mode.cs_mode.store(false);
            mode.cs_knife_state.store(0);
            mode.cs_knife_inspect.store(false);
            mode.cs_action_variant.store(0);
            mode.cs_action_hit.store(true);
            mode.cs_knife_anim_time.store(0.f);
#if ENABLE_MC_MODE
            mode.mc_mode.store(true);
#endif
            if (cb.log) cb.log("[MODE] Counter-Strike Weapon Mode: OFF (via F6)");
#endif
        } else {
#if ENABLE_MC_MODE
            const bool new_mc = !mode.mc_mode.load();
            mode.mc_mode.store(new_mc);
            if (cb.log) cb.log(std::string("F6: MC mode ") + (new_mc ? "on" : "off"));
            if (cb.reset_slot_probe && ctx.slot_cursor) cb.reset_slot_probe(*ctx.slot_cursor);
#endif
        }
    }
    prev6_ = d6;

    const bool d3 = fg && queryKeyDown(cb, VK_F3);
    if (d3 && !prev_f3_ && cb.queue_bullet_fire) {
        cb.queue_bullet_fire(10.0f);
    }
    prev_f3_ = d3;

    const bool d2 = fg && queryKeyDown(cb, VK_F2);
    if (d2 && !prev_f2_ && cb.queue_summon) {
        cb.queue_summon("F2");
    }
    prev_f2_ = d2;

    const bool d9 = fg && queryKeyDown(cb, VK_F9);
    if (d9 && !prev9_ && mode.hide_native.load() && cb.cycle_hidden_slot && ctx.slot_cursor) {
        cb.cycle_hidden_slot(*ctx.slot_cursor);
    }
    prev9_ = d9;

    const bool d12 = fg && queryKeyDown(cb, VK_F12);
    if (d12 && !prev12_ && cb.request_frame_trace) {
        cb.request_frame_trace(240);
    }
    prev12_ = d12;

    const bool d11 = fg && queryKeyDown(cb, VK_F11);
    if (d11 && !prev11_ && cb.snapshot_native_parts) {
        cb.snapshot_native_parts();
    }
    prev11_ = d11;

    const bool d10 = fg && queryKeyDown(cb, VK_F10);
    if (d10 && !prev10_) {
        mode.first_person.store(!mode.first_person.load());
        if (cb.reset_slot_probe && ctx.slot_cursor) cb.reset_slot_probe(*ctx.slot_cursor);
    }
    prev10_ = d10;

    const bool d4 = fg && queryKeyDown(cb, VK_F4);
    if (d4 && !prev4_ && cb.request_screenshot) {
        char name[64];
        snprintf(name, sizeof(name), "mc_er_shot_%03u.png", ++shot_no_);
        cb.request_screenshot(name);
    }
    prev4_ = d4;

    const bool shift_down = fg && (queryKeyDown(cb, VK_SHIFT) || queryKeyDown(cb, VK_LSHIFT) || queryKeyDown(cb, VK_RSHIFT));
    const bool d7 = fg && queryKeyDown(cb, VK_F7);
    if (d7 && !prev7_) {
        if (shift_down) {
#if ENABLE_OW_VIEWMODEL
            const bool new_ow = !mode.ow_mode.load();
            mode.ow_mode.store(new_ow);
            if (new_ow) {
                mode.mc_mode.store(false);
                mode.cs_mode.store(false);
                mode.first_person.store(true);
                mode.ow_state.store(0);
                mode.ow_anim_time.store(0.f);
                ow_.resetRuntimeForEnable();
                if (cb.log) cb.log("[MODE] Overwatch Doomfist Mode: ACTIVE (MC/CS mode off)");
            } else {
                mode.ow_state.store(0);
                mode.ow_anim_time.store(0.f);
                ow_.stopChargeLoop(cb);
                if (cb.log) cb.log("[MODE] Overwatch Doomfist Mode: OFF");
            }
#endif
        } else if (mode.ow_mode.load()) {
            const bool new_fp = !mode.first_person.load();
            mode.first_person.store(new_fp);
            if (cb.log) cb.log(new_fp ? "[OW] First-Person View (F7)" : "[OW] Third-Person View (F7)");
        } else if (cb.cycle_depth_candidate) {
            cb.cycle_depth_candidate();
        }
    }
    prev7_ = d7;
}

} // namespace eldenring::input
