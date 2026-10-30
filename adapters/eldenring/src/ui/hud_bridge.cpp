#include "eldenring_hud_bridge.hpp"

#include <algorithm>
#include <cmath>

#include "eldenring_model.hpp"
#include "eldenring_chrscan.hpp"

namespace eldenring::ui {

void PopulateHudStateFromContext(erov::HudState& out, const RuntimeContext& ctx) {
    out.mc_mode = ctx.mode().mc_mode.load(std::memory_order_relaxed);
#if ENABLE_CS2_VIEWMODEL
    out.cs_mode = ctx.mode().cs_mode.load(std::memory_order_relaxed);
#else
    out.cs_mode = false;
#endif
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
    out.eating = ctx.player().eating.getProgress();

    const auto& melee = ctx.combat().melee;
    out.selected_slot = melee.selectedSlot();
    for (int i = 0; i < mc::MeleeController::kSlots; ++i) {
        out.hotbar[i] = static_cast<uint16_t>(melee.itemAt(i));
        out.hotbar_count[i] = static_cast<uint8_t>(std::min(255u, melee.countAt(i)));
    }

    const auto& inv = melee.inventory();
    for (int i = 0; i < mc::Inventory::kSlots; ++i) {
        out.inv_item[i] = static_cast<uint16_t>(inv.slot(i).item);
        out.inv_count[i] = static_cast<uint8_t>(std::min(255u, inv.slot(i).count));
    }
    out.cursor_item = static_cast<uint16_t>(inv.cursor().item);
    out.cursor_count = static_cast<uint8_t>(std::min(255u, inv.cursor().count));
    out.offhand_item = static_cast<uint16_t>(inv.offhand().item);
    out.offhand_count = static_cast<uint8_t>(std::min(255u, inv.offhand().count));

    for (int i = 0; i < mc::Inventory::kArmorSlots; ++i) {
        out.armor_item[i] = static_cast<uint16_t>(inv.armor(i).item);
        out.armor_count[i] = static_cast<uint8_t>(std::min(255u, inv.armor(i).count));
    }
    out.armor_points = inv.totalArmorPoints();

    out.food = ctx.player().hunger.food();
    out.food_shaking = (ctx.player().hunger.saturation() <= 0.f) && (out.food <= 1);
    out.regen = ctx.player().survival.regenerating();
    out.xp_level = ctx.player().xp.level();
    out.xp_progress = ctx.player().xp.progress();

    const auto& feedback = ctx.combat().feedback;
    out.totem = feedback.totem();
    out.hit = feedback.hit();
    out.hit_crit = feedback.crit();
    out.kill = feedback.kill();

    const auto& at = feedback.armorToast();
    out.armor_toast_timer = at.timer;
    out.armor_toast_raw = at.raw;
    out.armor_toast_final = at.final_dmg;
    out.armor_toast_pts = at.armor_pts;
    out.armor_toast_ratio = at.ratio;
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
    const bool blocking = !dead && (ctx.mode().shield_blocking.load(std::memory_order_relaxed) || ctx.combat().shield.isBlocking());
    steve.sneaking = sneaking;
    steve.blocking = blocking;
    steve.sprinting = (!dead && !sneaking && !blocking) ? ctx.player().sprint_active.load(std::memory_order_relaxed) : false;

    steve.held_item = static_cast<uint16_t>(ctx.combat().melee.heldItem());
    steve.offhand_item = static_cast<uint16_t>(ctx.combat().melee.inventory().offhand().item);
    steve.cooldown = ctx.combat().melee.cooldown();
    steve.eating = dead ? 0.f : ctx.player().eating.getProgress();
    steve.swing = dead ? 0.f : ctx.combat().melee.swingProgress();
    steve.hurt = ctx.combat().feedback.hurt();
    steve.hurt_side = ctx.combat().feedback.hurtSide();

    steve.bow_charge = dead ? 0.f : ctx.mode().bow_power.load(std::memory_order_relaxed);
    steve.bow_ticks = dead ? 0.f : ctx.mode().bow_ticks.load(std::memory_order_relaxed);
    steve.bow_offhand = ctx.mode().bow_offhand.load(std::memory_order_relaxed);
    steve.block_offhand = ctx.mode().shield_offhand.load(std::memory_order_relaxed);
}

#if defined(WIN32)
static bool safeRead32(uintptr_t addr, uint32_t* out) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), sizeof(uint32_t));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool safeWrite32(uintptr_t addr, uint32_t val) {
    __try {
        std::memcpy(reinterpret_cast<void*>(addr), &val, sizeof(val));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void restoreAll(std::unordered_map<uintptr_t, debug::HiddenWord>& hidden_flags) {
    for (const auto& [a, w] : hidden_flags) {
        uint32_t flags = 0;
        if (safeRead32(a, &flags)) {
            const uint32_t restored = live::restoreBits(flags, w.original, w.mask);
            safeWrite32(a, restored);
        }
    }
    hidden_flags.clear();
}

void RestoreNativeModel(std::unordered_map<uintptr_t, debug::HiddenWord>& hidden_flags, std::mutex& native_mutex) {
    std::lock_guard<std::mutex> lock(native_mutex);
    restoreAll(hidden_flags);
}

void UpdateNativeModel(uintptr_t player, bool hide, const live::IMemoryReader& reader, uintptr_t base,
                       RuntimeContext& ctx, std::unordered_map<uintptr_t, debug::HiddenWord>& hidden_flags,
                       std::mutex& native_mutex) {
    std::lock_guard<std::mutex> lock(native_mutex);
    if (ctx.ui().slots_changed.exchange(false)) {
        // A weapon/model slot change frees and reuses the old display
        // entities. Writing the cached flag words back would then land in a
        // new object (a vtable/function pointer) and crash on the next
        // virtual call. Drop the cache and hide the new model from scratch.
        hidden_flags.clear();
    }
    if (!hide) {
        restoreAll(hidden_flags);
        return;
    }

    // RTTI-validated collection. A weapon switch can leave stale part pointers
    // in the model; the fixed-offset scan would then compute a flag address
    // inside a freed/reused object and corrupt its vtable (the 0x200000001
    // crash). The RTTI scan only returns real CSModelDispEntity objects.
    // The returned addresses are the disp_flags1 words, which is what the
    // loop below expects.
    const std::vector<uintptr_t> addrs = live::collectAsmModelDispFlagAddresses(reader, base, player);
    const uint32_t masks[2] = {
        ctx.ui().hide_mask1.load(std::memory_order_relaxed),
        ctx.ui().hide_mask2.load(std::memory_order_relaxed)
    };

    for (uintptr_t b : addrs) {
        for (unsigned w = 0; w < 2; ++w) {
            const uintptr_t a = b + w * (live::layout::kDispFlags2 - live::layout::kDispFlags1);
            if (masks[w] == 0) continue;
            uint32_t flags = 0;
            if (!safeRead32(a, &flags)) continue;
            if ((flags & masks[w]) != 0) {
                if (!hidden_flags.emplace(a, debug::HiddenWord{flags, masks[w]}).second) {
                    ctx.ui().native_reshown.fetch_add(1, std::memory_order_relaxed);
                }
                const uint32_t hidden = live::hideBits(flags, masks[w]);
                safeWrite32(a, hidden);
            }
        }

        // CSModelDispEntity + 0x220 shadow flags (bit 0: mesh visible, bit 4: shadow visible)
        const uintptr_t shadow_flags_addr = b + (live::layout::kDispShadowFlags - live::layout::kDispFlags1);
        uint32_t sflags = 0;
        if (safeRead32(shadow_flags_addr, &sflags)) {
            constexpr uint32_t kShadowMask = live::layout::kDispShadowBits; // 0x11
            if ((sflags & kShadowMask) != 0) {
                hidden_flags.emplace(shadow_flags_addr, debug::HiddenWord{sflags, kShadowMask});
                safeWrite32(shadow_flags_addr, live::hideBits(sflags, kShadowMask));
            }
        }
    }

    // CSChrAsmModelIns shadow toggles: +0x20 bit 2 and +0x2F0 cast_shadow byte
    uint64_t asm_model = 0;
    if (reader.read(player + live::layout::kAsmModelInPlayerIns, &asm_model, sizeof(asm_model)) && asm_model != 0) {
        const uintptr_t asm_addr = static_cast<uintptr_t>(asm_model);
        const uintptr_t asm_flags_addr = asm_addr + live::layout::kAsmFlags;
        uint32_t aflags = 0;
        if (safeRead32(asm_flags_addr, &aflags)) {
            constexpr uint32_t kAsmShadowMask = live::layout::kAsmShadowBit; // 0x4
            if ((aflags & kAsmShadowMask) != 0) {
                hidden_flags.emplace(asm_flags_addr, debug::HiddenWord{aflags, kAsmShadowMask});
                safeWrite32(asm_flags_addr, aflags & ~kAsmShadowMask);
            }
        }

        const uintptr_t cast_shadow_addr = asm_addr + live::layout::kAsmCastShadow;
        uint32_t cshadow_word = 0;
        if (safeRead32(cast_shadow_addr, &cshadow_word)) {
            constexpr uint32_t kByteMask = 0xFFu;
            if ((cshadow_word & kByteMask) != 0) {
                hidden_flags.emplace(cast_shadow_addr, debug::HiddenWord{cshadow_word, kByteMask});
                safeWrite32(cast_shadow_addr, cshadow_word & ~kByteMask);
            }
        }
    }
}
#endif

} // namespace eldenring::ui

