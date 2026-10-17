#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eldenring::hooks {

struct GameHooksCallbacks {
    // Logging
    std::function<void(const std::string&)> log;

    // Game Thread queries
    std::function<uint32_t()> get_game_tid;
    std::function<uintptr_t()> get_player_chr;
    std::function<bool(uintptr_t addr, void* dst, size_t sz)> safe_copy;

    // Heartbeat / CameraStep hooks
    std::function<void(float dt)> on_camera_step_pre;
    std::function<bool(uint64_t self, uintptr_t& out_cam_addr, float out_before[3])> check_chr_cam;
    std::function<void(bool have_before, uintptr_t cam_addr, const float before[3])> on_camera_step_post;

    // Clamp / Damage drainage tick
    std::function<void(uintptr_t module)> on_clamp_tick;

    // Damage / Survival
    std::function<bool(void* module, void* attacker, uint8_t* ctx, uint8_t a5, uint32_t& heal_to)> player_hit_survival;
    std::function<void(void* module, void* attacker, uint8_t* ctx, uint8_t a5)> log_player_hit;
    std::function<void(void* module, void* attacker, uint8_t* ctx)> handle_projectile_hit;
    std::function<void(uint8_t* ctx)> handle_forced_damage;
    std::function<bool(uint8_t* ctx, uintptr_t attacker, uintptr_t victim, int val, int* engine_val)> override_final_damage;
    std::function<void(uint32_t heal_to)> on_totem_popped_heal;

    // Kill character / Fall protection
    std::function<bool(void* chr)> should_skip_kill_chr;

    // Hit effects & reactions
    std::function<bool(void* a1, void* a2, uint8_t* ctx, void* flags)> should_skip_hit_vfx;
    std::function<bool(void* self, void* out)> should_skip_hit_react;
    std::function<void(void* self, const std::function<void()>& orig)> on_render_cam_copy;

    // Bullet log
    std::function<void(const void* request)> on_spawn_bullet;
    std::function<void(void* manager, void* request, void* r9, uint32_t* out_handle)> on_spawn_bullet_post;

    // Creative mode event probes
    std::function<bool()> is_fall_damage_disabled;
    std::function<bool(void* chr)> should_block_creative_kill;
    std::function<bool(void* fall_module, float threshold)> should_deny_fall_time_exceeded;
    std::function<void(const char* probe_name, void* obj)> on_creative_probe;
};

class GameHooks {
public:
    static GameHooks& Instance();

    void setCallbacks(const GameHooksCallbacks& cb);
    const GameHooksCallbacks& callbacks() const { return cb_; }

    // MinHook setup methods
    bool initMinHook();
    bool setupDamageHooks(uintptr_t base,
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
                          uintptr_t kill_addr);

    bool setupCreativeProbes(uintptr_t base,
                             uintptr_t kill_wrapper_addr,
                             uintptr_t hard_landing_addr,
                             uintptr_t fall_height_addr,
                             uintptr_t sp_effect_addr,
                             uintptr_t fall_exceeded_addr,
                             uintptr_t event_dispatch_addr);

    bool setupBulletLog(uintptr_t base, uintptr_t spawn_bullet_addr);

    // Call trampoline for bullet firing
    uint32_t* callSpawnBullet(void* manager, uint32_t* out_handle, void* request, void* ctx);

    // Direct invocation / test mock helpers for detours
    static void* InvokeClampDetour(void* module, int32_t value);
    static uint64_t InvokeCameraStepDetour(uint64_t self, float dt, uint64_t chr, uint64_t flag);
    static uint64_t InvokeProcessDamageDetour(void* module, void* attacker, uint8_t* ctx, uint32_t a4, uint8_t a5);
    static bool InvokeGuardCheckDetour(void* attacker_module, void* victim_chr, uint32_t atk_attr, uint8_t def_type, uint8_t break_flag);
    static void InvokeDamageVfunc7Detour(void* damage_module, void* attacker, uint8_t* ctx, uint8_t a4);
    static bool InvokeBreakTestDetour(void* damage_module, uint8_t* ctx);
    static void InvokeKillChrDetour(void* chr);
    static uint8_t InvokeHitVfxDetour(void* a1, void* a2, uint8_t* ctx, void* flags);
    static void InvokeRenderCamCopyDetour(void* self);
    static uint32_t* InvokeSpawnBulletDetour(void* manager, uint32_t* out_handle, void* request, void* r9);

    // Trampoline setter helpers for testing
    void setClampOrigForTesting(void* (*fn)(void*, int32_t));
    void setCameraStepOrigForTesting(uint64_t (*fn)(uint64_t, float, uint64_t, uint64_t));
    void setProcessDamageOrigForTesting(uint64_t (*fn)(void*, void*, uint8_t*, uint32_t, uint8_t));
    void setGuardCheckOrigForTesting(bool (*fn)(void*, void*, uint32_t, uint8_t, uint8_t));
    void setDamageVfunc7OrigForTesting(void (*fn)(void*, void*, uint8_t*, uint8_t));
    void setBreakTestOrigForTesting(bool (*fn)(void*, uint8_t*));
    void setKillChrOrigForTesting(void (*fn)(void*));
    void setHitVfxOrigForTesting(uint8_t (*fn)(void*, void*, uint8_t*, void*));
    void setSpawnBulletOrigForTesting(uint32_t* (*fn)(void*, uint32_t*, void*, void*));

    void resetForTesting();

private:
    GameHooks();
    ~GameHooks() = default;

    GameHooksCallbacks cb_;
};

} // namespace eldenring::hooks
