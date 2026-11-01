#pragma once

// Ranged combat: Elden Ring bullet request injection, bullet template persistence, the CS2 hitscan
// orchestration and the diagnostic aim-calibration probe. Extracted from CombatService so melee,
// guard and damage interception stay in one place and projectiles in another.
//
// Everything here is injected through CombatCallbacks (fake memory in tests); no game code is called
// except the spawn_bullet callback and the static-ray function supplied by CombatService.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>

#include "eldenring_bullet.hpp"
#include "eldenring_bow.hpp"
#include "eldenring_combat_callbacks.hpp"
#include "eldenring_damage.hpp"
#include "eldenring_entity_manager.hpp"
#include "eldenring_los.hpp"
#include "eldenring_runtime_context.hpp"
#include "mc/combat.hpp"
#if ENABLE_CS2_VIEWMODEL
#include "cs_ballistics.hpp"
#endif

namespace eldenring::combat {

using eldenring::live::DamageQueue;
using eldenring::live::RayResult;
using mc::HitFeedback;

// Read-only state of the aim-calibration probe (diagnostic only; it never affects gameplay).
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

// Diagnostic probe: after a real shot it samples the live bullet at 250 ms steps and logs how the
// flight direction compares with the requested aim, the camera and the player's body yaw.
// The clock is injectable so the sampling schedule is deterministic in tests.
class AimCalibrator {
public:
    void setCallbacks(const CombatCallbacks* cb) { callbacks_ = cb; }
    void setClock(std::function<uint64_t()> now_ms) { now_ms_ = std::move(now_ms); }
    void setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }

    void start(uint64_t manager, uint32_t handle, const float dir[3]);
    void tick();
    const AimCalState& state() const { return state_; }

    void reset() {
        state_ = AimCalState{};
        enabled_.store(false, std::memory_order_relaxed);
        now_ms_ = nullptr;
    }

private:
    uint64_t nowMs() const;
    void log(const char* fmt, ...);

    const CombatCallbacks* callbacks_{nullptr};
    std::function<uint64_t()> now_ms_;
    std::atomic<bool> enabled_{false};
    AimCalState state_{};
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

class ProjectileService {
public:
    static ProjectileService& Instance() {
        static ProjectileService s_instance;
        return s_instance;
    }

    void setCallbacks(CombatCallbacks callbacks);
    const CombatCallbacks& callbacks() const { return callbacks_; }

    // Static-geometry ray for the CS hitscan wall test. Supplied by CombatService, which owns the
    // Havok raycast wiring shared with melee line-of-sight.
    void setRayCast(std::function<RayResult(const float from[3], const float disp[3])> fn) {
        static_ray_ = std::move(fn);
    }

    // Bullet template management
    void recordBulletTemplate(const void* request);
    void loadBulletTemplate(const std::string& game_dir);
    void saveBulletTemplate(const uint8_t* body, const std::string& game_dir);
    bool hasValidBulletTemplate() const;
    void getBulletTemplate(uint8_t out[eldenring::bullet::kFullRequestBytes]) const;

    // spawn_bullet / ProcessDamageContext hooks
    void onSpawnBullet(const void* request);
    void handleProjectileHit(void* module, void* attacker, uint8_t* hit_ctx, HitFeedback& feedback, RuntimeContext& ctx);
    bool isOurBulletFire() const { return our_bullet_fire_; }

    // Bullet firing (F3 / bow release) and CS hitscan
    void runBulletFire(RuntimeContext& ctx);
    bool queueBulletFire(float mc_damage, RuntimeContext& ctx);
#if ENABLE_CS2_VIEWMODEL
    CsShotResult fireCsShot(const eldenring::cs::CsShotParams& params, DamageQueue& queue, uint64_t tick, RuntimeContext& ctx);
#endif

    // Diagnostic aim probe
    void setAimCalEnabled(bool enabled) { aim_.setEnabled(enabled); }
    bool aimCalEnabled() const { return aim_.enabled(); }
    void aimCalStart(uint64_t manager, uint32_t handle, const float dir[3]) { aim_.start(manager, handle, dir); }
    void aimCalTick() { aim_.tick(); }
    const AimCalState& aimCalState() const { return aim_.state(); }
    AimCalibrator& aimCalibrator() { return aim_; }

    // Configuration / diagnostic toggles
    void setProjLog(bool enabled) { proj_log_.store(enabled, std::memory_order_relaxed); }
    bool projLog() const { return proj_log_.load(std::memory_order_relaxed); }
    void setArrowDamage(bool enabled) { arrow_damage_.store(enabled, std::memory_order_relaxed); }
    bool arrowDamage() const { return arrow_damage_.load(std::memory_order_relaxed); }
    void setBulletFireEnabled(bool enabled) { bullet_fire_enabled_.store(enabled, std::memory_order_relaxed); }
    bool bulletFireEnabled() const { return bullet_fire_enabled_.load(std::memory_order_relaxed); }
    void setCsShotLogEnabled(bool enabled) { cs_shot_log_enabled_.store(enabled, std::memory_order_relaxed); }
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
    ProjectileService() = default;

    void log(const char* fmt, ...);

    CombatCallbacks callbacks_;
    std::function<RayResult(const float from[3], const float disp[3])> static_ray_;
    mutable std::mutex bullet_mutex_;
    bool bullet_template_valid_{false};
    uint8_t bullet_template_[eldenring::bullet::kFullRequestBytes]{};

    std::atomic<bool> proj_log_{false};
    std::atomic<bool> arrow_damage_{false};
    std::atomic<bool> bullet_fire_enabled_{false};
    std::atomic<bool> cs_shot_log_enabled_{false};
    std::atomic<int> bolt_attach_poly_{-1};

    std::atomic<uint32_t> last_bullet_id_{0};
    std::atomic<float> last_shot_damage_{eldenring::bullet::kCrossbowDamage};
    std::atomic<float> pending_shot_damage_{eldenring::bullet::kCrossbowDamage};
    std::atomic<uint64_t> last_bullet_tick_ms_{0};
    std::atomic<uint64_t> last_shot_ms_{0};
    std::atomic<uint64_t> last_bullet_fire_ms_{0};
    std::atomic<float> last_shot_pos_[3]{};

    thread_local static bool our_bullet_fire_;
    AimCalibrator aim_;
};

} // namespace eldenring::combat
