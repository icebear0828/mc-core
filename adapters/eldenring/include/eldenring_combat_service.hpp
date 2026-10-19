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
#include "eldenring_bow.hpp"
#include "eldenring_xp.hpp"
#include "eldenring_hunger.hpp"
#include "eldenring_melee.hpp"
#include "eldenring_los.hpp"
#include "eldenring_entity_manager.hpp"
#if ENABLE_CS2_VIEWMODEL
#include "cs_ballistics.hpp"
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

struct AimCalState {
    bool active{false};
    uintptr_t manager{0};
    uint32_t handle{0};
    float aim[3]{};
    uint64_t t0_ms{0};
    int samples{0};
    bool isolated{true};
    bool have_cam{false};
    bool have_body{false};
    float body_yaw_raw_deg{0.f}, body_yaw_deg{0.f};
    float cam_forward[3]{};
    bool have2{false};
    float pos2[3]{}, q2[4]{};
};

struct CombatCallbacks {
    std::function<uintptr_t()> get_player_chr;
    std::function<bool(uintptr_t chr, float feet[3])> read_physics_pos;
    std::function<bool(uintptr_t chr, float quat[4])> read_physics_orient;
    std::function<bool(CameraPose& out_cam)> read_camera;
    std::function<bool(uintptr_t addr, void* dst, size_t size)> safe_read;
    std::function<bool(uintptr_t addr, const void* src, size_t size)> safe_write;
    std::function<bool(const float from[3], const float to[3])> has_line_of_sight;
    std::function<bool(uintptr_t chr, Vitals& out_vitals)> read_vitals;
    std::function<void(uintptr_t chr, int new_hp)> set_chr_hp;
    std::function<bool(uintptr_t dmg_module, uintptr_t attacker, void* ctx_ptr)> call_vfunc7;
    std::function<void(const char* sound, float vol)> play_sound;
    std::function<void(int fx_kind, const float pos[3], int count)> spawn_fx;
    std::function<void(const float from[3], const float to[3], float life_s)> spawn_tracer;
#if ENABLE_CS2_VIEWMODEL
    // A bullet hole on static geometry: position and outward surface normal, both game metres.
    std::function<void(const float pos[3], const float normal[3])> spawn_decal;
#endif
    std::function<void(const std::string&)> log;
    const IMemoryReader* reader{nullptr};
    uintptr_t image_base{0};
    std::function<uint32_t()> get_game_tid;
    std::function<bool(void* manager, uint32_t* out_handle, const void* req, void* status)> spawn_bullet;
    std::function<bool(uint8_t* ctx, uintptr_t atk, uintptr_t vic, int val, int* eng_val)> override_final_damage;
    std::function<bool()> is_falling;
    std::function<bool(const std::string& path)> file_exists;
};

#if ENABLE_CS2_VIEWMODEL
// One hitscan bullet: where it came from, where it stopped, what it hit and the ER damage queued.
struct CsShotResult {
    bool fired{false};
    bool hit{false};
    bool wall_hit{false};   // the bullet stopped on static geometry (floor/wall), not an enemy
    uintptr_t victim_chr{0};
    float distance{0.f};
    int er_damage{0};
    float from[3]{};
    float to[3]{};
    float normal[3]{0.f, 1.f, 0.f}; // outward surface normal at a wall hit, oriented toward the shooter
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

    // Bullet template management
    void recordBulletTemplate(const void* request);
    void loadBulletTemplate(const std::string& game_dir);
    void saveBulletTemplate(const uint8_t* body, const std::string& game_dir);
    bool hasValidBulletTemplate() const;
    void getBulletTemplate(uint8_t out[eldenring::bullet::kFullRequestBytes]) const;

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
    void handleProjectileHit(void* module, void* attacker, uint8_t* hit_ctx, HitFeedback& feedback, RuntimeContext& ctx);
    void onSpawnBullet(const void* request);
    bool isOurBulletFire() const { return our_bullet_fire_; }

    // Bullet firing & aim calibration
    void runBulletFire(RuntimeContext& ctx);
    bool queueBulletFire(float mc_damage, RuntimeContext& ctx);
#if ENABLE_CS2_VIEWMODEL
    // CS hitscan: spread -> torso ray -> LOS -> DamageQueue. Called on the input thread; the damage is
    // applied by the existing drain on the game thread. Returns what the bullet did for logging/tracers.
    CsShotResult fireCsShot(const eldenring::cs::CsShotParams& params, DamageQueue& queue, uint64_t tick, RuntimeContext& ctx);
#endif
    void aimCalStart(uint64_t manager, uint32_t handle, const float dir[3]);
    void aimCalTick();
    const AimCalState& aimCalState() const { return aimcal_; }

    // Forced damage override tracking (used during queue drainage / vfunc[7])
    bool isForcedDamageArmed() const;
    void handleForcedDamage(uint8_t* ctx);
    ForcedDamage forcedDamage() const;

    // Configuration / diagnostic toggles
    void setAimCalEnabled(bool enabled) { aimcal_enabled_.store(enabled, std::memory_order_relaxed); }
    bool aimCalEnabled() const { return aimcal_enabled_.load(std::memory_order_relaxed); }
    void setProjLog(bool enabled) { proj_log_.store(enabled, std::memory_order_relaxed); }
    bool projLog() const { return proj_log_.load(std::memory_order_relaxed); }
    void setArrowDamage(bool enabled) { arrow_damage_.store(enabled, std::memory_order_relaxed); }
    bool arrowDamage() const { return arrow_damage_.load(std::memory_order_relaxed); }
    void setBulletFireEnabled(bool enabled) { bullet_fire_enabled_.store(enabled, std::memory_order_relaxed); }
    void setCsShotLogEnabled(bool enabled) { cs_shot_log_enabled_.store(enabled, std::memory_order_relaxed); }
    bool bulletFireEnabled() const { return bullet_fire_enabled_.load(std::memory_order_relaxed); }
    void setPdcHooked(bool hooked) { pdc_hooked_.store(hooked, std::memory_order_relaxed); }
    bool pdcHooked() const { return pdc_hooked_.load(std::memory_order_relaxed); }
    void setBoltAttachPoly(int poly) { bolt_attach_poly_.store(poly, std::memory_order_relaxed); }
    int boltAttachPoly() const { return bolt_attach_poly_.load(std::memory_order_relaxed); }

    // Shot state getters
    float lastShotDamage() const { return last_shot_damage_.load(std::memory_order_relaxed); }
    uint32_t lastBulletId() const { return last_bullet_id_.load(std::memory_order_relaxed); }
    uint64_t lastBulletTickMs() const { return last_bullet_tick_ms_.load(std::memory_order_relaxed); }
    uint64_t lastShotMs() const { return last_shot_ms_.load(std::memory_order_relaxed); }
    void getLastShotPos(float out_pos[3]) const {
        for (int i = 0; i < 3; ++i) out_pos[i] = last_shot_pos_[i].load(std::memory_order_relaxed);
    }

    void resetForTesting();

private:
    CombatService();

    void log(const char* fmt, ...);

    CombatCallbacks callbacks_;
    std::atomic<uint64_t> guard_sound_ms_{0};
    std::atomic<uint64_t> knockdown_log_ms_{0};

    mutable std::mutex bullet_mutex_;
    bool bullet_template_valid_{false};
    uint8_t bullet_template_[eldenring::bullet::kFullRequestBytes]{};

    RaycastFn raycast_fn_{nullptr};

    std::atomic<bool> aimcal_enabled_{false};
    std::atomic<bool> proj_log_{false};
    std::atomic<bool> arrow_damage_{false};
    std::atomic<bool> bullet_fire_enabled_{false};
    std::atomic<bool> cs_shot_log_enabled_{false};
    std::atomic<bool> pdc_hooked_{false};
    std::atomic<int> bolt_attach_poly_{-1};

    std::atomic<uint32_t> last_bullet_id_{0};
    std::atomic<float> last_shot_damage_{eldenring::bullet::kCrossbowDamage};
    std::atomic<float> pending_shot_damage_{eldenring::bullet::kCrossbowDamage};
    std::atomic<uint64_t> last_bullet_tick_ms_{0};
    std::atomic<uint64_t> last_shot_ms_{0};
    std::atomic<uint64_t> last_bullet_fire_ms_{0};
    std::atomic<float> last_shot_pos_[3]{};
    std::atomic<int> proj_log_left_{80};

    thread_local static bool our_bullet_fire_;
    thread_local static ForcedDamage forced_damage_;
    thread_local static RayResult last_ray_;

    AimCalState aimcal_{};
};

} // namespace eldenring::combat
