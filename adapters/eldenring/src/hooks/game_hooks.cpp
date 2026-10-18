#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <windows.h>
#include <MinHook.h>
#endif

#include "eldenring_game_hooks.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_combat_service.hpp"
#include "eldenring_inventory_controller.hpp"
#include "eldenring_damage.hpp"

namespace eldenring::hooks {

namespace {

// Trampoline function pointers
using ClampFn = void* (*)(void* module, int32_t value);
using CameraStepFn = uint64_t (*)(uint64_t self, float dt, uint64_t chr, uint64_t flag);
using ProcessDamageFn = uint64_t (*)(void* module, void* attacker, uint8_t* ctx, uint32_t a4, uint8_t a5);
using GuardCheckFn = bool (*)(void* attacker_module, void* victim_chr, uint32_t atk_attr, uint8_t def_type, uint8_t break_flag);
using DamageVfunc7Fn = void (*)(void* damage_module, void* attacker, uint8_t* ctx, uint8_t a4);
using BreakTestFn = bool (*)(void* damage_module, uint8_t* ctx);
using KillChrFn = void (*)(void* chr);
using HitVfxFn = uint8_t (*)(void* a1, void* a2, uint8_t* ctx, void* flags);
using RenderCamCopyFn = void (*)(void* self);
using SpawnBulletFn = uint32_t* (*)(void* manager, uint32_t* out_handle, void* request, void* r9);
using HitReactFn = void (*)(void* self, void* a, void* b, void* out, uint64_t five);
using HitReactHeavyFn = void (*)(void* self, void* a, void* b, void* out, uint64_t five, uint64_t six);

// Creative trampolines
using CreativeKillFn = void (*)(void* chr);
using CreativeFallFn = float (*)(void* fall_module);
using CreativeFallExceededFn = bool (*)(void* fall_module, float threshold);

ClampFn g_clamp_orig = nullptr;
CameraStepFn g_camstep_orig = nullptr;
ProcessDamageFn g_pdc_orig = nullptr;
GuardCheckFn g_guardcheck_orig = nullptr;
DamageVfunc7Fn g_vfunc7_orig = nullptr;
BreakTestFn g_breaktest_orig = nullptr;
KillChrFn g_kill_orig = nullptr;
HitVfxFn g_hitvfx_orig = nullptr;
RenderCamCopyFn g_rcc_orig = nullptr;
SpawnBulletFn g_spawn_bullet_orig = nullptr;
HitReactFn g_hitreact_orig = nullptr;
HitReactHeavyFn g_hitreact_heavy_orig = nullptr;

CreativeKillFn g_cr_kill_orig = nullptr;
CreativeFallFn g_cr_fall_orig = nullptr;
CreativeFallExceededFn g_cr_exceeded_orig = nullptr;

#ifdef _WIN32
// Native detours
void* __fastcall ClampDetourInternal(void* module, int32_t value) {
    return GameHooks::InvokeClampDetour(module, value);
}

uint64_t __fastcall CameraStepDetourInternal(uint64_t self, float dt, uint64_t chr, uint64_t flag) {
    return GameHooks::InvokeCameraStepDetour(self, dt, chr, flag);
}

uint64_t __fastcall ProcessDamageDetourInternal(void* module, void* attacker, uint8_t* ctx, uint32_t a4, uint8_t a5) {
    return GameHooks::InvokeProcessDamageDetour(module, attacker, ctx, a4, a5);
}

bool __fastcall GuardCheckDetourInternal(void* attacker_module, void* victim_chr, uint32_t atk_attr, uint8_t def_type, uint8_t break_flag) {
    return GameHooks::InvokeGuardCheckDetour(attacker_module, victim_chr, atk_attr, def_type, break_flag);
}

void __fastcall DamageVfunc7DetourInternal(void* damage_module, void* attacker, uint8_t* ctx, uint8_t a4) {
    GameHooks::InvokeDamageVfunc7Detour(damage_module, attacker, ctx, a4);
}

bool __fastcall BreakTestDetourInternal(void* damage_module, uint8_t* ctx) {
    return GameHooks::InvokeBreakTestDetour(damage_module, ctx);
}

void __fastcall KillChrDetourInternal(void* chr) {
    GameHooks::InvokeKillChrDetour(chr);
}

uint8_t __fastcall HitVfxDetourInternal(void* a1, void* a2, uint8_t* ctx, void* flags) {
    return GameHooks::InvokeHitVfxDetour(a1, a2, ctx, flags);
}

void __fastcall RenderCamCopyDetourInternal(void* self) {
    GameHooks::InvokeRenderCamCopyDetour(self);
}

uint32_t* __fastcall SpawnBulletDetourInternal(void* manager, uint32_t* out_handle, void* request, void* r9) {
    return GameHooks::InvokeSpawnBulletDetour(manager, out_handle, request, r9);
}

void __fastcall HitReactDetourInternal(void* self, void* a, void* b, void* out, uint64_t five) {
    auto& inst = GameHooks::Instance();
    if (inst.callbacks().should_skip_hit_react && inst.callbacks().should_skip_hit_react(self, out)) {
        if (out != nullptr) *reinterpret_cast<uint32_t*>(out) = 0;
        return;
    }
    if (g_hitreact_orig) g_hitreact_orig(self, a, b, out, five);
}

void __fastcall HitReactHeavyDetourInternal(void* self, void* a, void* b, void* out, uint64_t five, uint64_t six) {
    auto& inst = GameHooks::Instance();
    if (inst.callbacks().should_skip_hit_react && inst.callbacks().should_skip_hit_react(self, out)) {
        if (out != nullptr) *reinterpret_cast<uint32_t*>(out) = 0;
        return;
    }
    if (g_hitreact_heavy_orig) g_hitreact_heavy_orig(self, a, b, out, five, six);
}

void __fastcall CreativeKillDetourInternal(void* chr) {
    auto& inst = GameHooks::Instance();
    if (inst.callbacks().should_block_creative_kill && inst.callbacks().should_block_creative_kill(chr)) {
        return;
    }
    if (g_cr_kill_orig) g_cr_kill_orig(chr);
}

float __fastcall CreativeFallDetourInternal(void* fall_module) {
    auto& inst = GameHooks::Instance();
    if (inst.callbacks().is_fall_damage_disabled && inst.callbacks().is_fall_damage_disabled()) return 0.f;
    return g_cr_fall_orig ? g_cr_fall_orig(fall_module) : 0.f;
}

bool __fastcall CreativeFallExceededDetourInternal(void* fall_module, float threshold) {
    auto& inst = GameHooks::Instance();
    if (inst.callbacks().should_deny_fall_time_exceeded && inst.callbacks().should_deny_fall_time_exceeded(fall_module, threshold)) {
        return false;
    }
    return g_cr_exceeded_orig ? g_cr_exceeded_orig(fall_module, threshold) : false;
}
#endif

} // namespace

GameHooks& GameHooks::Instance() {
    static GameHooks s_instance;
    return s_instance;
}

GameHooks::GameHooks() = default;

void GameHooks::setCallbacks(const GameHooksCallbacks& cb) {
    cb_ = cb;
}

void GameHooks::resetForTesting() {
    cb_ = GameHooksCallbacks{};
    g_clamp_orig = nullptr;
    g_camstep_orig = nullptr;
    g_pdc_orig = nullptr;
    g_guardcheck_orig = nullptr;
    g_vfunc7_orig = nullptr;
    g_breaktest_orig = nullptr;
    g_kill_orig = nullptr;
    g_hitvfx_orig = nullptr;
    g_rcc_orig = nullptr;
    g_spawn_bullet_orig = nullptr;
    g_hitreact_orig = nullptr;
    g_hitreact_heavy_orig = nullptr;
    g_cr_kill_orig = nullptr;
    g_cr_fall_orig = nullptr;
    g_cr_exceeded_orig = nullptr;
}

void GameHooks::setClampOrigForTesting(void* (*fn)(void*, int32_t)) { g_clamp_orig = fn; }
void GameHooks::setCameraStepOrigForTesting(uint64_t (*fn)(uint64_t, float, uint64_t, uint64_t)) { g_camstep_orig = fn; }
void GameHooks::setProcessDamageOrigForTesting(uint64_t (*fn)(void*, void*, uint8_t*, uint32_t, uint8_t)) { g_pdc_orig = fn; }
void GameHooks::setGuardCheckOrigForTesting(bool (*fn)(void*, void*, uint32_t, uint8_t, uint8_t)) { g_guardcheck_orig = fn; }
void GameHooks::setDamageVfunc7OrigForTesting(void (*fn)(void*, void*, uint8_t*, uint8_t)) { g_vfunc7_orig = fn; }
void GameHooks::setBreakTestOrigForTesting(bool (*fn)(void*, uint8_t*)) { g_breaktest_orig = fn; }
void GameHooks::setKillChrOrigForTesting(void (*fn)(void*)) { g_kill_orig = fn; }
void GameHooks::setHitVfxOrigForTesting(uint8_t (*fn)(void*, void*, uint8_t*, void*)) { g_hitvfx_orig = fn; }
void GameHooks::setSpawnBulletOrigForTesting(uint32_t* (*fn)(void*, uint32_t*, void*, void*)) { g_spawn_bullet_orig = fn; }
void GameHooks::setCreativeKillOrigForTesting(void (*fn)(void*)) { g_cr_kill_orig = fn; }
void GameHooks::setCreativeFallOrigForTesting(float (*fn)(void*)) { g_cr_fall_orig = fn; }
void GameHooks::setCreativeFallExceededOrigForTesting(bool (*fn)(void*, float)) { g_cr_exceeded_orig = fn; }

void* GameHooks::InvokeClampDetour(void* module, int32_t value) {
    void* r = g_clamp_orig ? g_clamp_orig(module, value) : module;
    auto& self = Instance();
#ifdef _WIN32
    const DWORD cur_tid = GetCurrentThreadId();
#else
    const uint32_t cur_tid = 12345;
#endif
    if (self.cb_.get_game_tid && self.cb_.get_game_tid() == cur_tid) {
        if (self.cb_.on_clamp_tick) {
            self.cb_.on_clamp_tick(reinterpret_cast<uintptr_t>(module));
        }
    }
    return r;
}

uint64_t GameHooks::InvokeCameraStepDetour(uint64_t self_ptr, float dt, uint64_t chr, uint64_t flag) {
    auto& inst = Instance();
    if (inst.cb_.on_camera_step_pre) {
        inst.cb_.on_camera_step_pre(dt);
    }

    uintptr_t cam_addr = 0;
    float before[3] = {};
    bool have_before = false;
    if (inst.cb_.check_chr_cam) {
        have_before = inst.cb_.check_chr_cam(self_ptr, cam_addr, before);
    }

    uint64_t r = g_camstep_orig ? g_camstep_orig(self_ptr, dt, chr, flag) : self_ptr;

    if (inst.cb_.on_camera_step_post) {
        inst.cb_.on_camera_step_post(have_before, cam_addr, before);
    }
    return r;
}

uint64_t GameHooks::InvokeProcessDamageDetour(void* module, void* attacker, uint8_t* ctx, uint32_t a4, uint8_t a5) {
    auto& inst = Instance();
    uint32_t heal_to = 0;
    bool popped = false;

    if (inst.cb_.player_hit_survival) {
        popped = inst.cb_.player_hit_survival(module, attacker, ctx, a5, heal_to);
    }
    if (inst.cb_.log_player_hit) {
        inst.cb_.log_player_hit(module, attacker, ctx, a5);
    }
    if (inst.cb_.handle_projectile_hit) {
        inst.cb_.handle_projectile_hit(module, attacker, ctx);
    }
    if (inst.cb_.handle_forced_damage) {
        inst.cb_.handle_forced_damage(ctx);
    }

    uint64_t r = g_pdc_orig ? g_pdc_orig(module, attacker, ctx, a4, a5) : 0;

    if (popped && inst.cb_.on_totem_popped_heal) {
        inst.cb_.on_totem_popped_heal(heal_to);
    }
    return r;
}

bool GameHooks::InvokeGuardCheckDetour(void* attacker_module, void* victim_chr, uint32_t atk_attr, uint8_t def_type, uint8_t break_flag) {
    auto& r_ctx = RuntimeContext::Instance();
    if (eldenring::combat::CombatService::Instance().handleGuardCheck(attacker_module, victim_chr, r_ctx)) {
        return true;
    }
    return g_guardcheck_orig ? g_guardcheck_orig(attacker_module, victim_chr, atk_attr, def_type, break_flag) : false;
}

void GameHooks::InvokeDamageVfunc7Detour(void* damage_module, void* attacker, uint8_t* ctx, uint8_t a4) {
    auto& r_ctx = RuntimeContext::Instance();
    eldenring::combat::CombatService::Instance().handleDamageVfunc7(damage_module, attacker, ctx, r_ctx);
    if (g_vfunc7_orig) g_vfunc7_orig(damage_module, attacker, ctx, a4);
}

bool GameHooks::InvokeBreakTestDetour(void* damage_module, uint8_t* ctx) {
    const bool broke = g_breaktest_orig ? g_breaktest_orig(damage_module, ctx) : true;
    auto& r_ctx = RuntimeContext::Instance();
    return eldenring::combat::CombatService::Instance().handleBreakTest(damage_module, ctx, broke, r_ctx);
}

void GameHooks::InvokeKillChrDetour(void* chr) {
    auto& inst = Instance();
    if (inst.cb_.should_skip_kill_chr && inst.cb_.should_skip_kill_chr(chr)) {
        return; // Skip kill (fall protection)
    }
    if (g_kill_orig) g_kill_orig(chr);
}

uint8_t GameHooks::InvokeHitVfxDetour(void* a1, void* a2, uint8_t* ctx, void* flags) {
    auto& inst = Instance();
    if (inst.cb_.should_skip_hit_vfx && inst.cb_.should_skip_hit_vfx(a1, a2, ctx, flags)) {
        return 0; // Skip player's hit effect
    }
    return g_hitvfx_orig ? g_hitvfx_orig(a1, a2, ctx, flags) : 0;
}

void GameHooks::InvokeRenderCamCopyDetour(void* self) {
    auto& inst = Instance();
    if (inst.cb_.on_render_cam_copy) {
        inst.cb_.on_render_cam_copy(self, [&]() {
            if (g_rcc_orig) g_rcc_orig(self);
        });
    } else {
        if (g_rcc_orig) g_rcc_orig(self);
    }
}

uint32_t* GameHooks::InvokeSpawnBulletDetour(void* manager, uint32_t* out_handle, void* request, void* r9) {
    auto& inst = Instance();
    if (inst.cb_.on_spawn_bullet) {
        inst.cb_.on_spawn_bullet(request);
    }
    uint32_t* r = g_spawn_bullet_orig ? g_spawn_bullet_orig(manager, out_handle, request, r9) : out_handle;
    if (inst.cb_.on_spawn_bullet_post) {
        inst.cb_.on_spawn_bullet_post(manager, request, r9, out_handle);
    }
    return r;
}

void GameHooks::InvokeCreativeKillDetour(void* chr) {
#ifdef _WIN32
    CreativeKillDetourInternal(chr);
#else
    auto& inst = Instance();
    if (inst.cb_.should_block_creative_kill && inst.cb_.should_block_creative_kill(chr)) return;
    if (g_cr_kill_orig) g_cr_kill_orig(chr);
#endif
}

float GameHooks::InvokeCreativeFallDetour(void* fall_module) {
#ifdef _WIN32
    return CreativeFallDetourInternal(fall_module);
#else
    auto& inst = Instance();
    if (inst.cb_.is_fall_damage_disabled && inst.cb_.is_fall_damage_disabled()) return 0.f;
    return g_cr_fall_orig ? g_cr_fall_orig(fall_module) : 0.f;
#endif
}

bool GameHooks::InvokeCreativeFallExceededDetour(void* fall_module, float threshold) {
#ifdef _WIN32
    return CreativeFallExceededDetourInternal(fall_module, threshold);
#else
    auto& inst = Instance();
    if (inst.cb_.should_deny_fall_time_exceeded && inst.cb_.should_deny_fall_time_exceeded(fall_module, threshold)) return false;
    return g_cr_exceeded_orig ? g_cr_exceeded_orig(fall_module, threshold) : false;
#endif
}

bool GameHooks::initMinHook() {
#ifdef _WIN32
    static std::mutex m;
    static bool done = false, ok = false;
    std::lock_guard<std::mutex> g(m);
    if (!done) {
        const MH_STATUS st = MH_Initialize();
        ok = (st == MH_OK || st == MH_ERROR_ALREADY_INITIALIZED);
        done = true;
    }
    return ok;
#else
    return true;
#endif
}

bool GameHooks::setupDamageHooks(uintptr_t base,
                                 uintptr_t clamp_addr,
                                 uintptr_t pdc_addr,
                                 uintptr_t break_addr,
                                 uintptr_t guard_addr,
                                 uintptr_t vfunc7_addr,
                                 uintptr_t rcc_addr,
                                 uintptr_t hitvfx_addr,
                                 uintptr_t camstep_addr,
                                 uintptr_t isblocked_addr,
                                 uintptr_t menufreeze_addr,
                                 uintptr_t react_addr,
                                 uintptr_t heavy_react_addr,
                                 uintptr_t kill_addr) {
    (void)base;
#ifdef _WIN32
    if (!initMinHook()) return false;

    if (clamp_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(clamp_addr), reinterpret_cast<void*>(&ClampDetourInternal), reinterpret_cast<void**>(&g_clamp_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(clamp_addr));
        }
    }
    if (pdc_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(pdc_addr), reinterpret_cast<void*>(&ProcessDamageDetourInternal), reinterpret_cast<void**>(&g_pdc_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(pdc_addr));
        }
    }
    if (break_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(break_addr), reinterpret_cast<void*>(&BreakTestDetourInternal), reinterpret_cast<void**>(&g_breaktest_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(break_addr));
        }
    }
    if (guard_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(guard_addr), reinterpret_cast<void*>(&GuardCheckDetourInternal), reinterpret_cast<void**>(&g_guardcheck_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(guard_addr));
        }
    }
    if (vfunc7_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(vfunc7_addr), reinterpret_cast<void*>(&DamageVfunc7DetourInternal), reinterpret_cast<void**>(&g_vfunc7_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(vfunc7_addr));
        }
    }
    if (rcc_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(rcc_addr), reinterpret_cast<void*>(&RenderCamCopyDetourInternal), reinterpret_cast<void**>(&g_rcc_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(rcc_addr));
        }
    }
    if (hitvfx_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(hitvfx_addr), reinterpret_cast<void*>(&HitVfxDetourInternal), reinterpret_cast<void**>(&g_hitvfx_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(hitvfx_addr));
        }
    }
    if (camstep_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(camstep_addr), reinterpret_cast<void*>(&CameraStepDetourInternal), reinterpret_cast<void**>(&g_camstep_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(camstep_addr));
        }
    }
    if (isblocked_addr != 0) {
        void* orig = nullptr;
        if (MH_CreateHook(reinterpret_cast<void*>(isblocked_addr),
                          reinterpret_cast<void*>(&eldenring::ui::InventoryController::IsInputBlockedDetour),
                          &orig) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(isblocked_addr));
        }
    }
    if (menufreeze_addr != 0) {
        void* orig = nullptr;
        if (MH_CreateHook(reinterpret_cast<void*>(menufreeze_addr),
                          reinterpret_cast<void*>(&eldenring::ui::InventoryController::MenuFreezeDetour),
                          &orig) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(menufreeze_addr));
        }
    }
    if (react_addr != 0 && heavy_react_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(react_addr), reinterpret_cast<void*>(&HitReactDetourInternal), reinterpret_cast<void**>(&g_hitreact_orig)) == MH_OK &&
            MH_CreateHook(reinterpret_cast<void*>(heavy_react_addr), reinterpret_cast<void*>(&HitReactHeavyDetourInternal), reinterpret_cast<void**>(&g_hitreact_heavy_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(react_addr));
            MH_EnableHook(reinterpret_cast<void*>(heavy_react_addr));
        }
    }
    if (kill_addr != 0) {
        if (MH_CreateHook(reinterpret_cast<void*>(kill_addr), reinterpret_cast<void*>(&KillChrDetourInternal), reinterpret_cast<void**>(&g_kill_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(kill_addr));
        }
    }
    return true;
#else
    (void)base; (void)clamp_addr; (void)pdc_addr; (void)break_addr; (void)guard_addr;
    (void)vfunc7_addr; (void)rcc_addr; (void)hitvfx_addr; (void)camstep_addr;
    (void)isblocked_addr; (void)menufreeze_addr; (void)react_addr; (void)heavy_react_addr; (void)kill_addr;
    return true;
#endif
}

bool GameHooks::setupCreativeHooks(uintptr_t base,
                                   uintptr_t kill_wrapper_addr,
                                   uintptr_t fall_height_addr,
                                   uintptr_t fall_exceeded_addr) {
    (void)base;
#ifdef _WIN32
    if (!initMinHook()) return false;
    if (kill_wrapper_addr != 0 && MH_CreateHook(reinterpret_cast<void*>(kill_wrapper_addr), reinterpret_cast<void*>(&CreativeKillDetourInternal), reinterpret_cast<void**>(&g_cr_kill_orig)) == MH_OK) {
        MH_EnableHook(reinterpret_cast<void*>(kill_wrapper_addr));
    }
    if (fall_height_addr != 0 && MH_CreateHook(reinterpret_cast<void*>(fall_height_addr), reinterpret_cast<void*>(&CreativeFallDetourInternal), reinterpret_cast<void**>(&g_cr_fall_orig)) == MH_OK) {
        MH_EnableHook(reinterpret_cast<void*>(fall_height_addr));
    }
    if (fall_exceeded_addr != 0 && MH_CreateHook(reinterpret_cast<void*>(fall_exceeded_addr), reinterpret_cast<void*>(&CreativeFallExceededDetourInternal), reinterpret_cast<void**>(&g_cr_exceeded_orig)) == MH_OK) {
        MH_EnableHook(reinterpret_cast<void*>(fall_exceeded_addr));
    }
    return true;
#else
    (void)base; (void)kill_wrapper_addr; (void)fall_height_addr; (void)fall_exceeded_addr;
    return true;
#endif
}

bool GameHooks::setupBulletLog(uintptr_t base, uintptr_t spawn_bullet_addr) {
    (void)base;
#ifdef _WIN32
    if (!initMinHook()) return false;
    if (spawn_bullet_addr != 0 && MH_CreateHook(reinterpret_cast<void*>(spawn_bullet_addr), reinterpret_cast<void*>(&SpawnBulletDetourInternal), reinterpret_cast<void**>(&g_spawn_bullet_orig)) == MH_OK) {
        MH_EnableHook(reinterpret_cast<void*>(spawn_bullet_addr));
    }
    return true;
#else
    (void)base; (void)spawn_bullet_addr;
    return true;
#endif
}

uint32_t* GameHooks::callSpawnBullet(void* manager, uint32_t* out_handle, void* request, void* ctx) {
#ifdef _WIN32
    __try {
        return g_spawn_bullet_orig ? g_spawn_bullet_orig(manager, out_handle, request, ctx) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
#else
    return g_spawn_bullet_orig ? g_spawn_bullet_orig(manager, out_handle, request, ctx) : nullptr;
#endif
}

} // namespace eldenring::hooks
