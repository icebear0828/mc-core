#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <unknwn.h>

#if !defined(MC_CORE_TESTS)
#include <MinHook.h>
#else
enum MH_STATUS { MH_OK = 0, MH_ERROR_ALREADY_INITIALIZED = 1 };
inline MH_STATUS MH_Initialize() { return MH_OK; }
inline MH_STATUS MH_CreateHook(void*, void*, void**) { return MH_OK; }
inline MH_STATUS MH_EnableHook(void*) { return MH_OK; }
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "eldenring_hooks.hpp"
#include "eldenring_state.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_diagnostics.hpp"
#include "eldenring_memory_scanner.hpp"
#include "eldenring_locomotion.hpp"
#include "eldenring_world_runtime.hpp"
#include "eldenring_combat_service.hpp"
#include "eldenring_inventory_controller.hpp"
#include "eldenring_debug_probes.hpp"
#include "eldenring_hud_bridge.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "input_hook.hpp"
#include "overlay_d3d12.hpp"
#include "audio_xaudio2.hpp"
#include "eldenring_camera.hpp"
#include "eldenring_damage.hpp"
#include "eldenring_blockmesh.hpp"
#include "eldenring_blocks.hpp"
#include "eldenring_mining.hpp"
#include "eldenring_tnt.hpp"
#include "eldenring_creeper.hpp"
#include "eldenring_buddy.hpp"
#include "eldenring_chrscan.hpp"
#include "eldenring_mobs.hpp"
#include "eldenring_creative.hpp"
#include "eldenring_flight.hpp"
#include "eldenring_los.hpp"
#include "eldenring_bow.hpp"
#include "eldenring_bullet.hpp"
#include "eldenring_shadow.hpp"
#include "eldenring_jump.hpp"
#include "eldenring_sprint.hpp"
#include "eldenring_orientation.hpp"
#include "eldenring_roll.hpp"
#include "eldenring_entity_manager.hpp"
#include "eldenring_singletons.hpp"

namespace eldenring::hooks {

using diagnostics::Log;
using memory::SafeCopy;
using memory::SafeWrite8;
using memory::SafeWrite32;
using memory::SelfReader;
using memory::ImageInfo;
using memory::LocateByPrefix;
using live::readSingleton;
using live::readVitals;
using live::readCamera;
using live::readMenuState;
using live::readLoadingState;
using live::readFadeAlpha;
using live::firstPersonEye;
using live::Vitals;
using live::MenuState;
using live::LoadingState;
using live::CameraPose;
using live::EnemyInfo;
using live::enumerateEnemies;
using live::sigs::kWorldChrMan;
using live::sigs::kCSMenuMan;
using live::sigs::kCSNowLoadingHelper;
using live::sigs::kCSFade;

static const SelfReader g_reader;
static ImageInfo g_img;
static std::string g_game_dir;

bool EnsureMinHook() {
    static std::mutex m;
    static bool done = false, ok = false;
    std::lock_guard<std::mutex> g(m);
    if (!done) {
        const MH_STATUS st = MH_Initialize();
        ok = st == MH_OK || st == MH_ERROR_ALREADY_INITIALIZED;
        done = true;
    }
    return ok;
}

bool FileExists(const std::string& path) {
    const DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ReadFileAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    const size_t got = out.empty() ? 0 : fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

uint64_t NowTick() {
    return GetTickCount64() * 60ull / 1000ull;
}

uintptr_t PlayerChrPtr() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, 0, kWorldChrMan);
    uint64_t p = 0;
    return world != 0 && SafeCopy(world + live::layout::kPlayerInsInWorldChrMan, &p, sizeof(p)) ? static_cast<uintptr_t>(p) : 0;
}

bool PlayerDead() {
    const uintptr_t player = PlayerChrPtr();
    Vitals v;
    return player != 0 && readVitals(g_reader, g_img.base, player, v) && v.max_hp > 0 && v.hp <= 0;
}

bool PlayerAirborne() {
    const uintptr_t w = readSingleton(g_reader, g_img.base, 0, kWorldChrMan);
    uint64_t p = 0;
    return w != 0 && SafeCopy(w + live::layout::kPlayerInsInWorldChrMan, &p, sizeof(p)) && p != 0 &&
           live::detail::readAirborne(g_reader, g_img.base, static_cast<uintptr_t>(p));
}

uintptr_t DataModuleOfChr(uintptr_t chr) {
    if (chr == 0) return 0;
    uint64_t container = 0, data = 0;
    if (!SafeCopy(chr + live::layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0 ||
        !SafeCopy(static_cast<uintptr_t>(container) + live::layout::kChrDataModuleSlot * sizeof(uint64_t), &data, sizeof(data))) {
        return 0;
    }
    return static_cast<uintptr_t>(data);
}

using ApplyHpFn = void*(__fastcall*)(void* data_module, int32_t hp, uint8_t flag);
static ApplyHpFn g_apply_hp = nullptr;


bool GiveHp(uintptr_t data_module, int amount) {
    if (!g_apply_hp || data_module == 0 || amount <= 0) return false;
    int32_t hp = 0, max_hp = 0;
    if (!SafeCopy(data_module + live::layout::kDataHp, &hp, sizeof(hp)) || !SafeCopy(data_module + live::layout::kDataMaxHp, &max_hp, sizeof(max_hp)) || hp <= 0 ||
        max_hp <= 0) {
        return false;
    }
    __try {
        g_apply_hp(reinterpret_cast<void*>(data_module), std::min(max_hp, hp + amount), 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_apply_hp = nullptr;
        return false;
    }
}

bool SetChrHp(uintptr_t chr, int32_t new_hp) {
    if (chr == 0) return false;
    const uintptr_t data = DataModuleOfChr(chr);
    if (data == 0) return false;
    if (g_apply_hp) {
        __try {
            g_apply_hp(reinterpret_cast<void*>(data), new_hp, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_apply_hp = nullptr;
        }
    }
    SafeWrite32(data + live::layout::kDataHp, new_hp);
    return true;
}

bool KillChr(uintptr_t chr) {
    return SetChrHp(chr, 0);
}

using DamageFn = uint64_t(__fastcall*)(void*, void*, void*);

bool CallVfunc7(uintptr_t damage_module, uintptr_t attacker, void* ctx) {
    __try {
        void** vtable = *reinterpret_cast<void***>(damage_module);
        auto fn = reinterpret_cast<DamageFn>(vtable[live::layout::kDamageVfuncIndex]);
        fn(reinterpret_cast<void*>(damage_module), reinterpret_cast<void*>(attacker), ctx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Line of sight static ray
struct alignas(16) HkVec4 {
    float v[4];
};

using RaycastFn = bool(__fastcall*)(void* phys_world, uint32_t filter, const HkVec4* from, const HkVec4* disp, HkVec4* out_pos,
                                    HkVec4* out_normal, float* out_fraction);
static RaycastFn g_raycast = nullptr;
static thread_local live::RayResult t_last_ray;

live::RayResult CastStaticRay(const float from[3], const float disp[3]) {
    t_last_ray = live::RayResult{};
    live::RayResult r;
    uint64_t havok = 0, world = 0;
    if (!SafeCopy(g_img.base + live::los::kHavokManGlobalRva, &havok, sizeof(havok)) || havok == 0) return r;
    uint64_t vt = 0;
    if (!SafeCopy(static_cast<uintptr_t>(havok), &vt, sizeof(vt)) || vt != g_img.base + live::los::kHavokManVtableRva) return r;
    if (!SafeCopy(static_cast<uintptr_t>(havok) + live::los::kPhysWorldInHavokMan, &world, sizeof(world)) || world == 0) return r;
    alignas(16) HkVec4 f = {{from[0], from[1], from[2], 0.f}};
    alignas(16) HkVec4 d = {{disp[0], disp[1], disp[2], 0.f}};
    alignas(16) HkVec4 pos = {}, normal = {};
    float fraction = 1.f;
    __try {
        r.hit = g_raycast(reinterpret_cast<void*>(world), live::los::kStaticGeometryFilter, &f, &d, &pos, &normal, &fraction);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        RuntimeContext::Instance().mode().los_enabled.store(false);
        return live::RayResult{};
    }
    r.ok = std::isfinite(fraction);
    r.fraction = fraction;
    for (int i = 0; i < 3; ++i) {
        r.pos[i] = pos.v[i];
        r.normal[i] = normal.v[i];
    }
    t_last_ray = r;
    return r;
}

bool PlayerCanSee(const float player_feet[3], const float victim_feet[3]) {
    const float from[3] = {player_feet[0], player_feet[1] + live::los::kEyeHeight, player_feet[2]};
    const float to[3] = {victim_feet[0], victim_feet[1] + live::los::kTargetHeight, victim_feet[2]};
    const bool visible = live::hasLineOfSight(CastStaticRay, from, to);
    const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    Log("los: dist=%.2f ray ok=%d hit=%d at=%.2f m -> %s", std::sqrt(dx * dx + dy * dy + dz * dz), t_last_ray.ok ? 1 : 0, t_last_ray.hit ? 1 : 0,
        t_last_ray.fraction * std::sqrt(dx * dx + dy * dy + dz * dz), visible ? "visible" : "BLOCKED");
    return visible;
}

// Creative detours
using KillWrapperFn = void(__fastcall*)(void* chr);
using HardLandingFn = void(__fastcall*)(void* fall_module);
using FallHeightFn = float(__fastcall*)(void* fall_module);
using HasSpEffectFn = bool(__fastcall*)(void* container, int sp_effect);
using FallTimeExceededFn = bool(__fastcall*)(void* fall_module, float threshold);
using ChrEventFn = uint64_t(__fastcall*)(void* self, void* event);

static KillWrapperFn g_cr_kill_orig = nullptr;
static HardLandingFn g_cr_land_orig = nullptr;
static FallHeightFn g_cr_fall_orig = nullptr;
static HasSpEffectFn g_cr_sp_orig = nullptr;
static FallTimeExceededFn g_cr_exceeded_orig = nullptr;
static ChrEventFn g_cr_event_orig = nullptr;

static uintptr_t ModuleOwner(void* fall_module) {
    uint64_t owner = 0;
    return SafeCopy(reinterpret_cast<uintptr_t>(fall_module) + 8, &owner, sizeof(owner)) ? static_cast<uintptr_t>(owner) : 0;
}

static void __fastcall CreativeKillDetour(void* chr) {
    const uintptr_t player = PlayerChrPtr();
    auto& ctx = RuntimeContext::Instance();
    if (creative::isPlayer(reinterpret_cast<uintptr_t>(chr), player)) {
        const bool block = creative::shouldBlockPlayerKill(ctx.currentGameMode(), ctx.mode().mc_mode.load(std::memory_order_relaxed), PlayerDead() ? 0 : 1);
        if (block) return;
    }
    g_cr_kill_orig(chr);
}

static void __fastcall CreativeLandingDetour(void* fall_module) {
    if (creative::isPlayer(ModuleOwner(fall_module), PlayerChrPtr())) Log("%s", creative::formatLanding(true).c_str());
    g_cr_land_orig(fall_module);
}

static float __fastcall CreativeFallDetour(void* fall_module) {
    const float metres = g_cr_fall_orig(fall_module);
    auto& ctx = RuntimeContext::Instance();
    if (!creative::isPlayer(ModuleOwner(fall_module), PlayerChrPtr())) return metres;
    const bool zero = creative::shouldZeroFall(creative::zeroesFall(ctx.currentGameMode()), ctx.mode().mc_mode.load(std::memory_order_relaxed), true);
    return zero ? 0.f : metres;
}

static bool __fastcall CreativeFallTimeExceededDetour(void* fall_module, float threshold) {
    const bool real = g_cr_exceeded_orig(fall_module, threshold);
    if (!real) return false;
    auto& ctx = RuntimeContext::Instance();
    const bool held_up = ctx.world().block_grid.count() > 0;
    if (!creative::shouldDenyLongFall(ctx.currentGameMode(), ctx.mode().mc_mode.load(std::memory_order_relaxed),
                                      creative::isPlayer(ModuleOwner(fall_module), PlayerChrPtr()), held_up)) return real;
    return false;
}

static uint64_t __fastcall CreativeEventDetour(void* self, void* event) {
    return g_cr_event_orig(self, event);
}

static bool __fastcall CreativeSpEffectDetour(void* container, int sp_effect) {
    return g_cr_sp_orig(container, sp_effect);
}

void SetupCreativeProbes(const std::string& game_dir, uintptr_t image_base, size_t) {
    g_game_dir = game_dir;
    g_img.base = image_base;
    struct Probe {
        const char* name;
        const char* sig;
        void* detour;
        void** orig;
    };
    const Probe probes[] = {
        {"kill wrapper", live::sigs::kKillWrapper, reinterpret_cast<void*>(&CreativeKillDetour), reinterpret_cast<void**>(&g_cr_kill_orig)},
        {"hard landing", live::sigs::kHardLanding, reinterpret_cast<void*>(&CreativeLandingDetour), reinterpret_cast<void**>(&g_cr_land_orig)},
        {"fall height", live::sigs::kFallHeight, reinterpret_cast<void*>(&CreativeFallDetour), reinterpret_cast<void**>(&g_cr_fall_orig)},
        {"sp effect query", live::sigs::kHasSpEffect, reinterpret_cast<void*>(&CreativeSpEffectDetour), reinterpret_cast<void**>(&g_cr_sp_orig)},
        {"fall time exceeded", live::sigs::kFallTimeExceeded, reinterpret_cast<void*>(&CreativeFallTimeExceededDetour), reinterpret_cast<void**>(&g_cr_exceeded_orig)},
        {"chr event dispatch", live::sigs::kChrEventDispatch, reinterpret_cast<void*>(&CreativeEventDetour), reinterpret_cast<void**>(&g_cr_event_orig)},
    };
    if (!EnsureMinHook()) return;
    for (const Probe& p : probes) {
        uintptr_t target = 0;
        if (LocateByPrefix(g_img, p.sig, target)) {
            if (MH_CreateHook(reinterpret_cast<void*>(target), p.detour, p.orig) == MH_OK) {
                MH_EnableHook(reinterpret_cast<void*>(target));
            }
        }
    }
}

// Bullet logging and fire
using SpawnBulletFn = uint32_t*(__fastcall*)(void* manager, uint32_t* out_handle, void* request, void* r9);
static SpawnBulletFn g_spawn_bullet_orig = nullptr;
static std::atomic<uint64_t> g_last_bullet_fire_ms{0};
static std::atomic<float> g_pending_shot_damage{10.0f};
static thread_local bool t_our_bullet_fire = false;

bool QueueBulletFire(float mc_damage) {
    auto& ctx = RuntimeContext::Instance();
    if (!ctx.mode().bullet_fire_enabled.load()) return false;
    if (!combat::CombatService::Instance().hasValidBulletTemplate()) return false;
    const uint64_t now = GetTickCount64();
    if (now - g_last_bullet_fire_ms.load(std::memory_order_relaxed) < 400) return false;
    g_last_bullet_fire_ms.store(now);
    g_pending_shot_damage.store(mc_damage);
    ctx.mode().bullet_fire_pending.store(true);
    return true;
}

static bool SpawnBulletSafe(void* manager, uint32_t* out_handle, void* request, void* ctx) {
    __try {
        g_spawn_bullet_orig(manager, out_handle, request, ctx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void RunBulletFire() {
    alignas(16) uint8_t real[bullet::kFullRequestBytes];
    auto& combat_svc = combat::CombatService::Instance();
    if (!combat_svc.hasValidBulletTemplate()) return;
    combat_svc.getBulletTemplate(real);

    uint64_t manager = 0;
    const uintptr_t player = PlayerChrPtr();
    uint64_t handle = 0;
    CameraPose cam;
    const uintptr_t world = readSingleton(g_reader, g_img.base, 0, kWorldChrMan);
    if (!SafeCopy(g_img.base + 0x3D667A8, &manager, sizeof(manager)) || manager == 0 || player == 0 ||
        !SafeCopy(player + 8, &handle, sizeof(handle)) || world == 0 || !readCamera(g_reader, g_img.base, world, cam)) {
        return;
    }
    bullet::FireParams fp;
    fp.owner = handle;
    std::memcpy(&fp.row_id, real + 8, sizeof(fp.row_id));
    std::memcpy(&fp.flags, real + 0x44, sizeof(fp.flags));
    fp.flags |= 0x08u;
    fp.flags &= ~0x02u;

    float basis_right[3], basis_up[3];
    bow::buildBasis(cam.forward, basis_right, basis_up);
    std::memcpy(fp.right, basis_right, sizeof(fp.right));
    std::memcpy(fp.up, basis_up, sizeof(fp.up));
    std::memcpy(fp.forward, cam.forward, sizeof(fp.forward));
    bullet::muzzle(cam.position, cam.forward, 0.8f, fp.position);
    fp.attach_poly = -1;

    const std::vector<uint8_t> built = bullet::buildFireRequest(real, sizeof(real), fp);
    if (built.size() != bullet::kFullRequestBytes) return;

    alignas(16) uint8_t req[bullet::kFullRequestBytes];
    alignas(16) uint8_t status[64] = {};
    std::memcpy(req, built.data(), sizeof(req));
    uint32_t out = 0xFFFFFFFFu;

    t_our_bullet_fire = true;
    SpawnBulletSafe(reinterpret_cast<void*>(manager), &out, req, status);
    t_our_bullet_fire = false;
}

static uint32_t* __fastcall SpawnBulletDetour(void* manager, uint32_t* out_handle, void* request, void* r9) {
    if (!t_our_bullet_fire && request != nullptr) {
        combat::CombatService::Instance().recordBulletTemplate(request);
    }
    return g_spawn_bullet_orig(manager, out_handle, request, r9);
}

void SetupBulletLog(const std::string& game_dir, uintptr_t image_base, size_t) {
    g_game_dir = game_dir;
    g_img.base = image_base;
    uintptr_t target = 0;
    if (EnsureMinHook() && LocateByPrefix(g_img, live::sigs::kSpawnBullet, target)) {
        if (MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&SpawnBulletDetour), reinterpret_cast<void**>(&g_spawn_bullet_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(target));
        }
    }
}

// Damage Detours
using ClampFn = void*(__fastcall*)(void*, int32_t);
using ProcessDamageFn = uint64_t (*)(void* module, void* attacker, uint8_t* ctx, uint32_t a4, uint8_t a5);
using GuardCheckFn = bool (*)(void* attacker_module, void* victim_chr, uint32_t atk_attr, uint8_t def_type, uint8_t break_flag);
using DamageVfunc7Fn = void (*)(void* damage_module, void* attacker, uint8_t* ctx, uint8_t a4);
using BreakTestFn = bool (*)(void* damage_module, uint8_t* ctx);
using HitReactFn = void(__fastcall*)(void*, void*, void*, void*, uint64_t);
using HitReactHeavyFn = void(__fastcall*)(void*, void*, void*, void*, uint64_t, uint64_t);
using KillChrFn = void(__fastcall*)(void* chr);
using HitVfxFn = uint8_t(__fastcall*)(void* damage_module, void* attacker, uint8_t* ctx, void* flags);

static ClampFn g_clamp_orig = nullptr;
static ProcessDamageFn g_pdc_orig = nullptr;
static GuardCheckFn g_guardcheck_orig = nullptr;
static DamageVfunc7Fn g_vfunc7_orig = nullptr;
static BreakTestFn g_breaktest_orig = nullptr;
static HitReactFn g_hitreact_orig = nullptr;
static HitReactHeavyFn g_hitreact_heavy_orig = nullptr;
static KillChrFn g_kill_orig = nullptr;
static HitVfxFn g_hitvfx_orig = nullptr;

void DrainOnce(uintptr_t updating_data_module) {
    auto& ctx = RuntimeContext::Instance();
    live::DrainContext c;
    c.reader = &g_reader;
    c.image_base = g_img.base;
    c.on_game_thread = true;
    c.tick = NowTick();
    c.updating_data_module = updating_data_module;
    c.require_victim_updating = ctx.mode().require_victim_updating.load();
    c.hit_template = ctx.combat().hit_template ? &*ctx.combat().hit_template : nullptr;
    c.allow_neutral = true;
    if (ctx.mode().los_enabled.load()) c.line_of_sight = PlayerCanSee;
    c.max_per_drain = 1;
    c.expire_after_ticks = 60 * 5;
    c.invoke = [](uintptr_t dmg, uintptr_t attacker, void* hctx) {
        CallVfunc7(dmg, attacker, hctx);
    };
    ctx.combat().damage_queue.drain(c);
}

static void* __fastcall ClampDetour(void* module, int32_t value) {
    void* r = g_clamp_orig(module, value);
    auto& ctx = RuntimeContext::Instance();
    if (ctx.mode().damage_enabled.load(std::memory_order_relaxed) && ctx.combat().damage_queue.pending() > 0) {
        DrainOnce(reinterpret_cast<uintptr_t>(module));
    }
    if (ctx.mode().bullet_fire_pending.load(std::memory_order_relaxed) && ctx.mode().bullet_fire_pending.exchange(false)) {
        RunBulletFire();
    }
    if (ctx.mode().heal_pending.load(std::memory_order_relaxed) > 0) {
        uint64_t owner = 0;
        if (SafeCopy(reinterpret_cast<uintptr_t>(module) + live::layout::kOwnerInDataModule, &owner, sizeof(owner)) &&
            owner != 0 && owner == PlayerChrPtr()) {
            const int h = ctx.mode().heal_pending.exchange(0);
            if (h > 0) GiveHp(reinterpret_cast<uintptr_t>(module), h);
        }
    }
    return r;
}

static uint64_t ProcessDamageDetour(void* module, void* attacker, uint8_t* ctx, uint32_t a4, uint8_t a5) {
    return g_pdc_orig(module, attacker, ctx, a4, a5);
}

static bool GuardCheckDetour(void* attacker_module, void* victim_chr, uint32_t atk_attr, uint8_t def_type, uint8_t break_flag) {
    auto& ctx = RuntimeContext::Instance();
    if (combat::CombatService::Instance().handleGuardCheck(attacker_module, victim_chr, ctx)) {
        return true;
    }
    return g_guardcheck_orig(attacker_module, victim_chr, atk_attr, def_type, break_flag);
}

static void DamageVfunc7Detour(void* damage_module, void* attacker, uint8_t* ctx, uint8_t a4) {
    auto& r_ctx = RuntimeContext::Instance();
    combat::CombatService::Instance().handleDamageVfunc7(damage_module, attacker, ctx, r_ctx);
    g_vfunc7_orig(damage_module, attacker, ctx, a4);
}

static bool BreakTestDetour(void* damage_module, uint8_t* ctx) {
    const bool broke = g_breaktest_orig(damage_module, ctx);
    auto& r_ctx = RuntimeContext::Instance();
    return combat::CombatService::Instance().handleBreakTest(damage_module, ctx, broke, r_ctx);
}

static void __fastcall HitReactDetour(void* self, void* a, void* b, void* out, uint64_t five) {
    g_hitreact_orig(self, a, b, out, five);
}

static void __fastcall HitReactHeavyDetour(void* self, void* a, void* b, void* out, uint64_t five, uint64_t six) {
    g_hitreact_heavy_orig(self, a, b, out, five, six);
}

static void __fastcall KillChrDetour(void* chr) {
    if (reinterpret_cast<uintptr_t>(chr) == PlayerChrPtr() && PlayerChrPtr() != 0) {
        auto& ctx = RuntimeContext::Instance();
        if (creative::shouldSkipPlayerKill(true, ctx.mode().mc_mode.load(), ctx.world().block_grid.count() > 0, PlayerDead() ? 0 : 1)) {
            return;
        }
    }
    g_kill_orig(chr);
}

static uint8_t __fastcall HitVfxDetour(void* a1, void* a2, uint8_t* ctx, void* flags) {
    return g_hitvfx_orig(a1, a2, ctx, flags);
}

// Camera and Model detours
using RenderCamCopyFn = void(__fastcall*)(void* self);
using CameraStepFn = uint64_t(__fastcall*)(uint64_t, float, uint64_t, uint64_t);

static RenderCamCopyFn g_rcc_orig = nullptr;
static CameraStepFn g_camstep_orig = nullptr;

static void __fastcall RenderCamCopyDetour(void* self) {
    g_rcc_orig(self);
}

static uint64_t __fastcall CameraStepDetour(uint64_t self, float dt, uint64_t chr, uint64_t flag) {
    auto& ctx = RuntimeContext::Instance();
    movement::PlayerLocomotion::Instance().stepFlight(dt, GetTickCount64(), ctx);
    world::VoxelWorldRuntime::Instance().stepPlayerBlockCollision(ctx);
    movement::PlayerLocomotion::Instance().stepJump(dt, GetTickCount64(), ctx);
    movement::PlayerLocomotion::Instance().stepGroundSprint(dt, ctx);
    world::VoxelWorldRuntime::Instance().stepShadowGround(ctx);
    return g_camstep_orig(self, dt, chr, flag);
}

void SetupDamage(const std::string& game_dir, uintptr_t image_base, size_t) {
    g_game_dir = game_dir;
    g_img.base = image_base;
    uintptr_t clamp = 0, pdc = 0, brk = 0, guard = 0, vf7 = 0, rcc = 0, step = 0, kill = 0, ray = 0, hp_fn = 0;
    if (!EnsureMinHook()) return;

    if (LocateByPrefix(g_img, live::sigs::kClampHp, clamp)) {
        MH_CreateHook(reinterpret_cast<void*>(clamp), reinterpret_cast<void*>(&ClampDetour), reinterpret_cast<void**>(&g_clamp_orig));
        MH_EnableHook(reinterpret_cast<void*>(clamp));
    }
    if (LocateByPrefix(g_img, live::sigs::kProcessDamageContext, pdc)) {
        MH_CreateHook(reinterpret_cast<void*>(pdc), reinterpret_cast<void*>(&ProcessDamageDetour), reinterpret_cast<void**>(&g_pdc_orig));
        MH_EnableHook(reinterpret_cast<void*>(pdc));
    }
    if (LocateByPrefix(g_img, live::sigs::kBreakTest, brk)) {
        MH_CreateHook(reinterpret_cast<void*>(brk), reinterpret_cast<void*>(&BreakTestDetour), reinterpret_cast<void**>(&g_breaktest_orig));
        MH_EnableHook(reinterpret_cast<void*>(brk));
    }
    if (LocateByPrefix(g_img, live::sigs::kGuardCheck, guard)) {
        MH_CreateHook(reinterpret_cast<void*>(guard), reinterpret_cast<void*>(&GuardCheckDetour), reinterpret_cast<void**>(&g_guardcheck_orig));
        MH_EnableHook(reinterpret_cast<void*>(guard));
    }
    if (LocateByPrefix(g_img, live::sigs::kDamageVfunc7, vf7)) {
        MH_CreateHook(reinterpret_cast<void*>(vf7), reinterpret_cast<void*>(&DamageVfunc7Detour), reinterpret_cast<void**>(&g_vfunc7_orig));
        MH_EnableHook(reinterpret_cast<void*>(vf7));
    }
    if (LocateByPrefix(g_img, live::sigs::kRenderCameraCopy, rcc)) {
        MH_CreateHook(reinterpret_cast<void*>(rcc), reinterpret_cast<void*>(&RenderCamCopyDetour), reinterpret_cast<void**>(&g_rcc_orig));
        MH_EnableHook(reinterpret_cast<void*>(rcc));
    }
    if (LocateByPrefix(g_img, live::sigs::kCameraStepExecute, step)) {
        MH_CreateHook(reinterpret_cast<void*>(step), reinterpret_cast<void*>(&CameraStepDetour), reinterpret_cast<void**>(&g_camstep_orig));
        MH_EnableHook(reinterpret_cast<void*>(step));
    }
    if (LocateByPrefix(g_img, live::sigs::kKillChr, kill)) {
        MH_CreateHook(reinterpret_cast<void*>(kill), reinterpret_cast<void*>(&KillChrDetour), reinterpret_cast<void**>(&g_kill_orig));
        MH_EnableHook(reinterpret_cast<void*>(kill));
    }
    uintptr_t vfx = 0;
    if (FileExists(g_game_dir + "mc_er_hitvfx.txt") && LocateByPrefix(g_img, live::sigs::kHitVfxSpawn, vfx)) {
        if (MH_CreateHook(reinterpret_cast<void*>(vfx), reinterpret_cast<void*>(&HitVfxDetour), reinterpret_cast<void**>(&g_hitvfx_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(vfx));
        }
    }
    uintptr_t react = 0, heavy = 0;
    if (LocateByPrefix(g_img, live::sigs::kHitReactDefault, react) && LocateByPrefix(g_img, live::sigs::kHitReactHeavy, heavy)) {
        if (MH_CreateHook(reinterpret_cast<void*>(react), reinterpret_cast<void*>(&HitReactDetour), reinterpret_cast<void**>(&g_hitreact_orig)) == MH_OK &&
            MH_CreateHook(reinterpret_cast<void*>(heavy), reinterpret_cast<void*>(&HitReactHeavyDetour), reinterpret_cast<void**>(&g_hitreact_heavy_orig)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(react));
            MH_EnableHook(reinterpret_cast<void*>(heavy));
        }
    }
    if (LocateByPrefix(g_img, live::sigs::kRaycastWrapper, ray)) {
        g_raycast = reinterpret_cast<RaycastFn>(ray);
        RuntimeContext::Instance().mode().los_enabled.store(true);
    }
    if (LocateByPrefix(g_img, live::sigs::kApplyHpChange, hp_fn)) {
        g_apply_hp = reinterpret_cast<ApplyHpFn>(hp_fn);
    }
}

// World and Mining helpers
bool StepMining(float, bool) {
    return false;
}

bool TryIgniteTnt() {
    return false;
}

bool TryIgniteCreeper() {
    return false;
}

bool TryPlaceBlock() {
    return false;
}

bool TryUseSpawnEgg() {
    return false;
}

void BlocksTick() {
}

void SendBlockMesh() {
}

void ClickAttack(float) {
}

void EnqueueNearestHostile() {
}

bool QueueSummon(const char*) {
    return false;
}

void RunSummonExperiment() {
}

void SetupAllDomainServices(uintptr_t image_base, uint32_t rva_world) {
    g_img.base = image_base;
    auto& ctx = RuntimeContext::Instance();

    auto is_foreground = []() {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        return pid == GetCurrentProcessId();
    };

    movement::LocomotionCallbacks lcb;
    lcb.get_player_chr = PlayerChrPtr;
    lcb.get_physics_module = [](uintptr_t chr) { return live::detail::readPhysicsModule(g_reader, g_img.base, chr); };
    lcb.safe_read = [](uintptr_t addr, void* dst, size_t sz) { return memory::SafeCopy(addr, dst, sz); };
    lcb.safe_write = [](uintptr_t addr, const void* src, size_t sz) { return memory::SafeWriteBytes(addr, src, sz); };
    lcb.is_game_in_foreground = is_foreground;
    lcb.want_suppress = [&ctx]() { return ctx.mode().mc_mode.load(); };
    lcb.is_player_dead = PlayerDead;
    lcb.is_player_airborne = PlayerAirborne;
    lcb.read_camera = [rva_world](CameraPose& out_cam) {
        const uintptr_t world = live::readSingleton(g_reader, g_img.base, rva_world, live::sigs::kWorldChrMan);
        return world != 0 && live::readCamera(g_reader, g_img.base, world, out_cam);
    };
    lcb.cast_ray = CastStaticRay;
    lcb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    movement::PlayerLocomotion::Instance().setCallbacks(lcb);

    world::WorldRuntimeCallbacks wcb;
    wcb.get_player_chr = PlayerChrPtr;
    wcb.get_physics_module = [](uintptr_t chr) { return live::detail::readPhysicsModule(g_reader, g_img.base, chr); };
    wcb.read_physics_pos = [](uintptr_t chr, float out_pos[3]) { return live::detail::readPhysicsPosition(g_reader, g_img.base, chr, out_pos); };
    wcb.safe_read = [](uintptr_t addr, void* dst, size_t sz) { return memory::SafeCopy(addr, dst, sz); };
    wcb.safe_write = [](uintptr_t addr, const void* src, size_t sz) { return memory::SafeWriteBytes(addr, src, sz); };
    wcb.is_game_in_foreground = is_foreground;
    wcb.read_camera = lcb.read_camera;
    wcb.cast_ray = CastStaticRay;
    wcb.enumerate_enemies = [](std::vector<EnemyInfo>& out) { return live::enumerateEnemies(g_reader, g_img.base, out, 512); };
    wcb.apply_damage_to_chr = [&ctx](uintptr_t chr, int dmg) {
        ctx.combat().damage_queue.enqueue(chr, dmg, NowTick(), 4u);
    };
    wcb.play_sound = [](const char* snd, float vol) { eraudio::Play(snd, vol); };
    wcb.spawn_fx = [](int kind, const float pos[3], int count) { erov::SpawnFx(static_cast<erov::FxKind>(kind), pos, count); };
    wcb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    world::VoxelWorldRuntime::Instance().setCallbacks(wcb);

    combat::CombatCallbacks ccb;
    ccb.get_player_chr = PlayerChrPtr;
    ccb.read_physics_pos = wcb.read_physics_pos;
    ccb.read_physics_orient = [](uintptr_t chr, float quat[4]) { return live::detail::readPhysicsOrientation(g_reader, g_img.base, chr, quat); };
    ccb.read_camera = lcb.read_camera;
    ccb.safe_read = wcb.safe_read;
    ccb.safe_write = wcb.safe_write;
    ccb.has_line_of_sight = PlayerCanSee;
    ccb.read_vitals = [](uintptr_t chr, Vitals& out_v) { return live::readVitals(g_reader, g_img.base, chr, out_v); };
    ccb.set_chr_hp = SetChrHp;
    ccb.call_vfunc7 = CallVfunc7;
    ccb.play_sound = wcb.play_sound;
    ccb.spawn_fx = wcb.spawn_fx;
    ccb.log = wcb.log;
    ccb.reader = &g_reader;
    ccb.image_base = g_img.base;
    combat::CombatService::Instance().setCallbacks(ccb);

    ui::InventoryCallbacks icb;
    icb.set_suppress_mouse_motion = erin::SetSuppressMouseMotion;
    icb.set_suppress_keyboard = erin::SetSuppressKeyboard;
    icb.log = [](const char* msg) { Log("%s", msg); };
    ui::InventoryController::Instance().setCallbacks(icb);

    input::HotkeyCallbacks hcb;
    hcb.is_game_in_foreground = is_foreground;
    hcb.want_suppress = lcb.want_suppress;
    hcb.is_player_dead = PlayerDead;
    hcb.is_player_airborne = PlayerAirborne;
    hcb.get_player_chr = PlayerChrPtr;
    hcb.read_physics_pos = wcb.read_physics_pos;
    hcb.play_sound = wcb.play_sound;
    hcb.log = wcb.log;
    hcb.take_left_click = erin::TakeLeftClick;
    hcb.take_right_click = erin::TakeRightClick;
    hcb.left_held = erin::LeftHeld;
    hcb.right_held = erin::RightHeld;
    hcb.take_wheel_notches = erin::TakeWheelNotches;
    hcb.set_suppress_mouse_buttons = erin::SetSuppressMouseButtons;
    hcb.set_masked_keys = erin::SetMaskedKeys;
    hcb.take_keyboard_failures = erin::TakeKeyboardFailures;
    hcb.step_mining = StepMining;
    hcb.try_ignite_tnt = TryIgniteTnt;
    hcb.try_ignite_creeper = TryIgniteCreeper;
    hcb.try_place_block = TryPlaceBlock;
    hcb.try_use_spawn_egg = TryUseSpawnEgg;
    hcb.click_attack = ClickAttack;
    hcb.enqueue_nearest_hostile = EnqueueNearestHostile;
    hcb.queue_bullet_fire = QueueBulletFire;
    hcb.queue_summon = QueueSummon;
    hcb.blocks_tick = BlocksTick;
    input::HotkeyDispatcher::Instance().setCallbacks(hcb);
}

bool InstallAllHooks(const std::string& game_dir, uintptr_t image_base, size_t image_size) {
    g_game_dir = game_dir;
    g_img.base = image_base;
    g_img.image_size = static_cast<uint32_t>(image_size);
    SetupCreativeProbes(game_dir, image_base, image_size);
    SetupBulletLog(game_dir, image_base, image_size);
    SetupDamage(game_dir, image_base, image_size);
    return true;
}

} // namespace eldenring::hooks
