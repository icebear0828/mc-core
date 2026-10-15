#include "eldenring_hud_bridge.hpp"

#include <algorithm>

namespace eldenring::ui {

void PopulateHudStateFromContext(erov::HudState& out, const RuntimeContext& ctx) {
    out.mc_mode = ctx.mode().mc_mode.load(std::memory_order_relaxed);
    out.creative = !creative::showsVitals(ctx.currentGameMode());
    const uint64_t toast_ms = ctx.mode().mode_toast_ms.load(std::memory_order_relaxed);
#if defined(WIN32)
    const uint64_t since = GetTickCount64() - toast_ms;
    out.mode_toast = (toast_ms != 0 && since < 2500) ? 1.f - static_cast<float>(since) / 2500.f : 0.f;
#else
    out.mode_toast = 0.f;
#endif
    out.slot_probe = ctx.ui().slot_probe.load(std::memory_order_relaxed);
    out.inv_open = ctx.ui().inv_open.load(std::memory_order_relaxed);
    out.mouse_x = ctx.ui().inv_mx.load(std::memory_order_relaxed);
    out.mouse_y = ctx.ui().inv_my.load(std::memory_order_relaxed);
    out.absorption_mc = ctx.player().survival.absorptionMc();
}

void PopulateSteveStateFromContext(erov::SteveState& steve, const RuntimeContext& ctx, bool dead, bool sleeping) {
    steve.dead = dead;
    steve.sleeping = sleeping;
    steve.shadow = ctx.mode().shadow_enabled.load(std::memory_order_relaxed);
    steve.ground_valid = ctx.player().ground_valid.load(std::memory_order_relaxed);
    steve.ground_y = ctx.player().ground_y.load(std::memory_order_relaxed);
    steve.first_person = ctx.mode().first_person.load(std::memory_order_relaxed) && ctx.ui().slot_probe.load(std::memory_order_relaxed) < 0;
    steve.speed_mps = ctx.mode().walk_speed.load(std::memory_order_relaxed);
    steve.on_ground = ctx.mode().on_ground.load(std::memory_order_relaxed);
    const bool sneaking = !dead && ctx.mode().sneak_active.load(std::memory_order_relaxed);
    const bool blocking = !dead && ctx.combat().shield.isBlocking();
    steve.sneaking = sneaking;
    steve.blocking = blocking;
    steve.sprinting = (!dead && !sneaking && !blocking) ? ctx.player().sprint_active.load(std::memory_order_relaxed) : false;
}

} // namespace eldenring::ui
