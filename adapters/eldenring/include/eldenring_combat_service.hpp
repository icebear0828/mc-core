#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "eldenring_runtime_context.hpp"
#include "eldenring_camera.hpp"
#include "eldenring_steve.hpp"
#include "eldenring_shield.hpp"
#include "eldenring_survival.hpp"
#include "eldenring_damage.hpp"
#include "eldenring_bullet.hpp"
#include "eldenring_xp.hpp"
#include "eldenring_hunger.hpp"
#include "eldenring_melee.hpp"
#include "eldenring_los.hpp"
#include "eldenring_entity_manager.hpp"
#include "eldenring_combat_callbacks.hpp"
#include "eldenring_projectile_service.hpp"
#if ENABLE_CS2_VIEWMODEL
#include "cs_ballistics.hpp"
#endif
#if ENABLE_OW_VIEWMODEL
#include "eldenring_ow_combat.hpp"
#endif
#include "mc/combat.hpp"
#include "mc/inventory.hpp"

namespace eldenring::combat {

namespace layout = ::eldenring::live::layout;
using eldenring::live::CameraPose;
using eldenring::live::Vitals;
using eldenring::live::DamageQueue;
using eldenring::live::HitTemplate;
using eldenring::live::DamageResult;
using eldenring::live::DamageOutcome;
using eldenring::live::SurvivalState;
using eldenring::live::HungerModel;
using eldenring::live::XpModel;
using eldenring::live::DrainContext;
using eldenring::live::scaleToEr;
using eldenring::live::IMemoryReader;
using eldenring::live::xpForKill;
using eldenring::live::RayResult;
using eldenring::live::HkVec4;
using eldenring::live::RaycastFn;
using eldenring::live::EnemyInfo;
using eldenring::live::EntityManager;
using mc::CombatEngine;
using mc::HitFeedback;
using mc::MeleeController;
using mc::ArmorDamageReductionResult;
using mc::computeArmorDamageReduction;
using mc::TotemOutcome;
using mc::totemClamp;

struct ShieldVerdict {
    bool ok_player{false}, ok_attacker{false}, ok_orient{false}, ok_cam{false};
    bool blocks{false};
    float pp[3]{}, ap[3]{}, q[4]{};
    float body_yaw{0.f}, cam_yaw{0.f}, yaw{0.f};
    float dot{0.f}, body_dot{0.f};
};

struct ForcedDamage {
    bool armed{false};
    uint64_t attacker{0};
    uint64_t victim{0};
    int32_t value{0};
    bool applied{false};
    int32_t engine_value{0};
};

#if ENABLE_OW_VIEWMODEL
// One Doomfist strike. `hits` is > 1 for the seismic slam's radial damage.
struct OwStrikeResult {
    bool fired{false};
    bool hit{false};
    int hits{0};
    int total_er_damage{0};
    uintptr_t victim_chr{0};
    float distance{0.f};
    int er_damage{0};
};
#endif

class CombatService {
public:
    static CombatService& Instance() {
        static CombatService s_instance;
        return s_instance;
    }

    void setCallbacks(CombatCallbacks callbacks) {
        callbacks_ = std::move(callbacks);
        // ProjectileService owns bullets/aim; it gets its own copy plus the static-ray function that
        // CombatService already wires for melee line-of-sight (one-way dependency, no include cycle).
        ProjectileService::Instance().setCallbacks(callbacks_);
        ProjectileService::Instance().setRayCast([this](const float from[3], const float disp[3]) {
            return this->castStaticRay(from, disp);
        });
    }

    // Shield evaluation
    ShieldVerdict evaluateShield(uintptr_t player, uintptr_t attacker_chr);
    int evaluateExplosionShieldBlock(uintptr_t player, const float explosion_pos[3], int raw_damage, RuntimeContext& ctx);

    // Natural hits on player: shield blocking, absorption, armor reduction, totem of undying
    bool playerHitSurvival(void* module, void* attacker, uint8_t* ctx_ptr, uint8_t blocked,
                           uint32_t& popped_heal_target, MeleeController& melee,
                           SurvivalState& survival,
                           HitFeedback& feedback,
                           HungerModel& hunger,
                           RuntimeContext& ctx);

    // GuardCheck, DamageVfunc7, and BreakTest hooks
    bool handleGuardCheck(void* attacker_module, void* victim_chr, RuntimeContext& ctx);
    void handleDamageVfunc7(void* damage_module, void* attacker, uint8_t* ctx_ptr, RuntimeContext& ctx);
    bool handleBreakTest(void* damage_module, uint8_t* ctx_ptr, bool original_broke, RuntimeContext& ctx);

    // Damage queue drainage
    void drainDamageQueue(DamageQueue& queue, const HitTemplate* tmpl,
                          uintptr_t updating_data_module, uint64_t tick,
                          mc::Inventory& inv, HitFeedback& feedback,
                          XpModel& xp, RuntimeContext& ctx);

    // Ranged combat: thin forwarding to ProjectileService so existing call sites and tests keep
    // working. The implementation lives in src/combat/projectile_service.cpp.
    void recordBulletTemplate(const void* request) { ProjectileService::Instance().recordBulletTemplate(request); }
    void loadBulletTemplate(const std::string& game_dir) { ProjectileService::Instance().loadBulletTemplate(game_dir); }
    void saveBulletTemplate(const uint8_t* body, const std::string& game_dir) { ProjectileService::Instance().saveBulletTemplate(body, game_dir); }
    bool hasValidBulletTemplate() const { return ProjectileService::Instance().hasValidBulletTemplate(); }
    void getBulletTemplate(uint8_t out[eldenring::bullet::kFullRequestBytes]) const { ProjectileService::Instance().getBulletTemplate(out); }

    // Raycasting & Line of sight
    void setRaycastFn(RaycastFn fn) { raycast_fn_ = fn; }
    RaycastFn raycastFn() const { return raycast_fn_; }
    RayResult castStaticRay(const float from[3], const float disp[3]);
    bool playerCanSee(const float player_feet[3], const float victim_feet[3]);
    RayResult lastRay() const;

    // Melee attack
    bool clickAttack(float charged, MeleeController& melee, CombatEngine& engine, DamageQueue& queue, RuntimeContext& ctx);
    void enqueueNearestHostile(DamageQueue& queue, uint64_t tick, float max_dist = 25.0f);

    // Projectile hit handler
    void handleProjectileHit(void* module, void* attacker, uint8_t* hit_ctx, HitFeedback& feedback, RuntimeContext& ctx) {
        ProjectileService::Instance().handleProjectileHit(module, attacker, hit_ctx, feedback, ctx);
    }
    void onSpawnBullet(const void* request) { ProjectileService::Instance().onSpawnBullet(request); }
    bool isOurBulletFire() const { return ProjectileService::Instance().isOurBulletFire(); }

    // Bullet firing & aim calibration
    void runBulletFire(RuntimeContext& ctx) { ProjectileService::Instance().runBulletFire(ctx); }
    bool queueBulletFire(float mc_damage, RuntimeContext& ctx) { return ProjectileService::Instance().queueBulletFire(mc_damage, ctx); }
#if ENABLE_CS2_VIEWMODEL
    // CS hitscan: spread -> torso ray -> LOS -> DamageQueue. Called on the input thread; the damage is
    // applied by the existing drain on the game thread. Returns what the bullet did for logging/tracers.
    CsShotResult fireCsShot(const eldenring::cs::CsShotParams& params, DamageQueue& queue, uint64_t tick, RuntimeContext& ctx) {
        return ProjectileService::Instance().fireCsShot(params, queue, tick, ctx);
    }
#endif
#if ENABLE_OW_VIEWMODEL
    // Doomfist strikes: hand cannon, rocket punch, uppercut and slam. Enqueues
    // the damage in the same DamageQueue the CS bullets and MC melee use.
    OwStrikeResult fireOwStrike(eldenring::ow::OwStrikeKind kind, DamageQueue& queue, uint64_t tick, RuntimeContext& ctx);
#endif
    void aimCalStart(uint64_t manager, uint32_t handle, const float dir[3]) { ProjectileService::Instance().aimCalStart(manager, handle, dir); }
    void aimCalTick() { ProjectileService::Instance().aimCalTick(); }
    const AimCalState& aimCalState() const { return ProjectileService::Instance().aimCalState(); }
    // Forced damage override tracking (used during queue drainage / vfunc[7])
    bool isForcedDamageArmed() const;
    void handleForcedDamage(uint8_t* ctx);
    ForcedDamage forcedDamage() const;

    // Configuration / diagnostic toggles (ranged ones forward to ProjectileService)
    void setAimCalEnabled(bool enabled) { ProjectileService::Instance().setAimCalEnabled(enabled); }
    bool aimCalEnabled() const { return ProjectileService::Instance().aimCalEnabled(); }
    void setProjLog(bool enabled) { ProjectileService::Instance().setProjLog(enabled); }
    bool projLog() const { return ProjectileService::Instance().projLog(); }
    void setArrowDamage(bool enabled) { ProjectileService::Instance().setArrowDamage(enabled); }
    bool arrowDamage() const { return ProjectileService::Instance().arrowDamage(); }
    void setBulletFireEnabled(bool enabled) { ProjectileService::Instance().setBulletFireEnabled(enabled); }
    void setCsShotLogEnabled(bool enabled) { ProjectileService::Instance().setCsShotLogEnabled(enabled); }
    bool bulletFireEnabled() const { return ProjectileService::Instance().bulletFireEnabled(); }
    void setPdcHooked(bool hooked) { pdc_hooked_.store(hooked, std::memory_order_relaxed); }
    bool pdcHooked() const { return pdc_hooked_.load(std::memory_order_relaxed); }
    void setBoltAttachPoly(int poly) { ProjectileService::Instance().setBoltAttachPoly(poly); }
    int boltAttachPoly() const { return ProjectileService::Instance().boltAttachPoly(); }

    // Shot state getters (forward to ProjectileService)
    float lastShotDamage() const { return ProjectileService::Instance().lastShotDamage(); }
    uint32_t lastBulletId() const { return ProjectileService::Instance().lastBulletId(); }
    uint64_t lastBulletTickMs() const { return ProjectileService::Instance().lastBulletTickMs(); }
    uint64_t lastShotMs() const { return ProjectileService::Instance().lastShotMs(); }
    void getLastShotPos(float out_pos[3]) const { ProjectileService::Instance().getLastShotPos(out_pos); }

    void resetForTesting();

private:
    CombatService();

    void log(const char* fmt, ...);

    CombatCallbacks callbacks_;
    std::atomic<uint64_t> guard_sound_ms_{0};
    std::atomic<uint64_t> knockdown_log_ms_{0};

    RaycastFn raycast_fn_{nullptr};

    std::atomic<bool> pdc_hooked_{false};

    thread_local static ForcedDamage forced_damage_;
    thread_local static RayResult last_ray_;
};

} // namespace eldenring::combat
