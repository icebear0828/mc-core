#include "eldenring_projectile_service.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "eldenring_steve.hpp"
#if ENABLE_CS2_VIEWMODEL
#include "cs_impact_decals.hpp"
#endif

namespace eldenring::combat {

namespace layout = ::eldenring::live::layout;
using eldenring::live::CameraPose;
using eldenring::live::Vitals;

thread_local bool ProjectileService::our_bullet_fire_{false};

void ProjectileService::setCallbacks(CombatCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
    aim_.setCallbacks(&callbacks_);
}

void ProjectileService::log(const char* fmt, ...) {
    if (!callbacks_.log) return;
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    callbacks_.log(buffer);
}

void ProjectileService::resetForTesting() {
    callbacks_ = CombatCallbacks{};
    static_ray_ = nullptr;
    proj_log_.store(false);
    arrow_damage_.store(false);
    bullet_fire_enabled_.store(false);
    cs_shot_log_enabled_.store(false);
    bolt_attach_poly_.store(-1);
    last_bullet_id_.store(0);
    last_shot_damage_.store(eldenring::bullet::kCrossbowDamage);
    pending_shot_damage_.store(eldenring::bullet::kCrossbowDamage);
    last_bullet_tick_ms_.store(0);
    last_shot_ms_.store(0);
    last_bullet_fire_ms_.store(0);
    for (int i = 0; i < 3; ++i) last_shot_pos_[i].store(0.f);
    our_bullet_fire_ = false;
    aim_.reset();
    std::lock_guard<std::mutex> g(bullet_mutex_);
    bullet_template_valid_ = false;
    std::memset(bullet_template_, 0, sizeof(bullet_template_));
}

void ProjectileService::recordBulletTemplate(const void* request) {
    if (request == nullptr || !callbacks_.safe_read) return;
    uint8_t req[eldenring::bullet::kFullRequestBytes] = {};
    if (!callbacks_.safe_read(reinterpret_cast<uintptr_t>(request), req, sizeof(req))) return;
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    uint64_t handle = 0, owner = 0;
    std::memcpy(&owner, req, sizeof(owner));
    if (player == 0 || !callbacks_.safe_read(player + 8, &handle, sizeof(handle)) || handle != owner) return;
    if (!eldenring::bullet::templateUsable(req, sizeof(req))) return;

    std::lock_guard<std::mutex> g(bullet_mutex_);
    std::memcpy(bullet_template_, req, sizeof(req));
    bullet_template_valid_ = true;
}

void ProjectileService::loadBulletTemplate(const std::string& game_dir) {
    uint8_t body[eldenring::bullet::kFullRequestBytes] = {};
    FILE* f = nullptr;
    std::string path = game_dir + "mc_er_bullet.bin";
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) {
        path = game_dir + "mods\\mc_adapter\\mc_er_bullet.bin";
        fopen_s(&f, path.c_str(), "rb");
    }
    size_t got = 0;
    if (f != nullptr) {
        got = fread(body, 1, sizeof(body), f);
        fclose(f);
    }
    if (got == sizeof(body) && eldenring::bullet::templateUsable(body, sizeof(body))) {
        std::lock_guard<std::mutex> g(bullet_mutex_);
        std::memcpy(bullet_template_, body, sizeof(body));
        bullet_template_valid_ = true;
        log("bullet: template loaded from %s", path.c_str());
        return;
    }
    if (eldenring::bullet::kDefaultBoltTemplateAvailable) {
        std::lock_guard<std::mutex> g(bullet_mutex_);
        std::memcpy(bullet_template_, eldenring::bullet::kDefaultBoltTemplate, sizeof(body));
        bullet_template_valid_ = true;
        log("bullet: loaded built-in golden bolt template");
        saveBulletTemplate(eldenring::bullet::kDefaultBoltTemplate, game_dir);
    }
}

void ProjectileService::saveBulletTemplate(const uint8_t* body, const std::string& game_dir) {
    if (body == nullptr) return;
    const std::string path = game_dir + "mc_er_bullet.bin";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || f == nullptr) return;
    fwrite(body, 1, eldenring::bullet::kFullRequestBytes, f);
    fclose(f);
    log("bullet: template saved to %s", path.c_str());
}

bool ProjectileService::hasValidBulletTemplate() const {
    std::lock_guard<std::mutex> g(bullet_mutex_);
    return bullet_template_valid_;
}

void ProjectileService::getBulletTemplate(uint8_t out[eldenring::bullet::kFullRequestBytes]) const {
    std::lock_guard<std::mutex> g(bullet_mutex_);
    std::memcpy(out, bullet_template_, eldenring::bullet::kFullRequestBytes);
}

void ProjectileService::onSpawnBullet(const void* request) {
    if (!our_bullet_fire_) {
        uint32_t id = 0;
        if (request != nullptr && callbacks_.safe_read) {
            callbacks_.safe_read(reinterpret_cast<uintptr_t>(request) + 0x1C, &id, sizeof(id));
        }
        last_bullet_id_.store(id, std::memory_order_relaxed);
        last_shot_damage_.store(eldenring::bullet::kCrossbowDamage, std::memory_order_relaxed);
        const uint64_t shot_now = GetTickCount64();
        last_bullet_tick_ms_.store(shot_now, std::memory_order_relaxed);
        last_shot_ms_.store(shot_now, std::memory_order_relaxed);
        float pos[3] = {};
        if (request != nullptr && callbacks_.safe_read &&
            callbacks_.safe_read(reinterpret_cast<uintptr_t>(request) + 0x80, pos, sizeof(pos))) {
            for (int i = 0; i < 3; ++i) last_shot_pos_[i].store(pos[i], std::memory_order_relaxed);
        }
    }
    recordBulletTemplate(request);
}

void ProjectileService::handleProjectileHit(void* module, void* attacker, uint8_t* hit_ctx, HitFeedback& feedback, RuntimeContext& ctx_rt) {
    if (hit_ctx == nullptr || !callbacks_.safe_read) return;
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    uint64_t victim = 0, ctx_attacker = 0, ctx_victim = 0;
    uint8_t kind = 0;
    int32_t engine_damage = 0;
    const uintptr_t base = reinterpret_cast<uintptr_t>(hit_ctx);
    const bool readable = module != nullptr &&
                          callbacks_.safe_read(reinterpret_cast<uintptr_t>(module) + layout::kOwnerInDataModule, &victim, sizeof(victim)) && victim != 0 &&
                          callbacks_.safe_read(base + 0xDA, &kind, sizeof(kind)) &&
                          callbacks_.safe_read(base + layout::kHitAttacker, &ctx_attacker, sizeof(ctx_attacker)) &&
                          callbacks_.safe_read(base + layout::kHitVictim, &ctx_victim, sizeof(ctx_victim)) &&
                          callbacks_.safe_read(base + layout::kHitDamage, &engine_damage, sizeof(engine_damage));

    if (!readable || victim == player) return;

    const uint64_t now = GetTickCount64();
    const uint64_t since_shot = now - last_bullet_tick_ms_.load(std::memory_order_relaxed);
    const bool mine = eldenring::bullet::isPlayersBoltHit(
        kind, reinterpret_cast<uintptr_t>(attacker), player, victim,
        last_bullet_id_.load(std::memory_order_relaxed), since_shot);

    if (mine && callbacks_.read_physics_pos) {
        float vp[3] = {};
        const uint64_t shot_ms = last_shot_ms_.load(std::memory_order_relaxed);
        if (shot_ms != 0 && callbacks_.read_physics_pos(static_cast<uintptr_t>(victim), vp)) {
            const float dx = vp[0] - last_shot_pos_[0].load(std::memory_order_relaxed),
                        dy = vp[1] + 1.0f - last_shot_pos_[1].load(std::memory_order_relaxed),
                        dz = vp[2] - last_shot_pos_[2].load(std::memory_order_relaxed);
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float since = static_cast<float>(now - shot_ms);
            log("ARROW-TIMING: the hit was processed %.0f ms after the shot; target dist=%.1f m; avg speed=%.1f m/s",
                since, dist, since > 0.f ? dist / (since / 1000.f) : 0.f);
        }
    }

    if (mine && ctx_rt.mode().mc_mode.load(std::memory_order_relaxed)) {
        const float mc_damage_hit = last_shot_damage_.load(std::memory_order_relaxed);
        const bool crit = eldenring::bullet::isCriticalShot(mc_damage_hit);
        if (callbacks_.play_sound) callbacks_.play_sound("entity.arrow.hit", 1.0f);
        feedback.onHit(crit);
        if (callbacks_.spawn_fx && callbacks_.read_physics_pos) {
            float vpos[3] = {};
            if (callbacks_.read_physics_pos(static_cast<uintptr_t>(victim), vpos)) {
                vpos[1] += 1.0f;
                const int hearts = eldenring::bullet::hitHearts(mc_damage_hit);
                if (hearts > 0) callbacks_.spawn_fx(0 /* Damage */, vpos, hearts);
                if (crit) callbacks_.spawn_fx(1 /* Crit */, vpos, 0);
            }
        }
    }

    if (arrow_damage_.load(std::memory_order_relaxed) && mine) {
        Vitals v{};
        if (callbacks_.read_vitals && callbacks_.read_vitals(static_cast<uintptr_t>(victim), v) && v.max_hp > 0) {
            const float mc_damage = last_shot_damage_.load(std::memory_order_relaxed);
            const int32_t wanted = eldenring::bullet::boltDamageEr(v.max_hp, mc_damage);
            int32_t engine = 0;
            if (wanted > 0 && callbacks_.override_final_damage &&
                callbacks_.override_final_damage(hit_ctx, ctx_attacker, ctx_victim, wanted, &engine)) {
                log("ARROW: the player's shot hit %p: engine %d -> MC %.1f = %d (victim max hp %d)",
                    reinterpret_cast<void*>(victim), engine, mc_damage, wanted, v.max_hp);
            }
        }
    }
}

void ProjectileService::runBulletFire(RuntimeContext& ctx) {
    (void)ctx;
    if (!hasValidBulletTemplate()) {
        log("bullet fire: no template yet, fire one real crossbow bolt first");
        return;
    }
    alignas(16) uint8_t real[eldenring::bullet::kFullRequestBytes] = {};
    getBulletTemplate(real);

    uint64_t manager = 0;
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    uint64_t handle = 0;
    CameraPose cam;
    if (!callbacks_.safe_read || !callbacks_.read_camera ||
        !callbacks_.safe_read(callbacks_.image_base + 0x3D667A8, &manager, sizeof(manager)) || manager == 0 ||
        player == 0 || !callbacks_.safe_read(player + 8, &handle, sizeof(handle)) ||
        !callbacks_.read_camera(cam)) {
        log("bullet fire: the manager, the player or the camera cannot be read");
        return;
    }

    eldenring::bullet::FireParams fp;
    fp.owner = handle;
    std::memcpy(&fp.row_id, real + 8, sizeof(fp.row_id));
    std::memcpy(&fp.flags, real + 0x44, sizeof(fp.flags));
    fp.flags |= 0x08u;
    fp.flags &= ~0x02u;

    float basis_right[3], basis_up[3];
    eldenring::bow::buildBasis(cam.forward, basis_right, basis_up);
    std::memcpy(fp.right, basis_right, sizeof(fp.right));
    std::memcpy(fp.up, basis_up, sizeof(fp.up));
    std::memcpy(fp.forward, cam.forward, sizeof(fp.forward));
    eldenring::bullet::muzzle(cam.position, cam.forward, 0.8f, fp.position);
    fp.attach_poly = bolt_attach_poly_.load(std::memory_order_relaxed);

    const std::vector<uint8_t> built = eldenring::bullet::buildFireRequest(real, sizeof(real), fp);
    if (built.size() != eldenring::bullet::kFullRequestBytes) {
        log("bullet fire: the template is unusable");
        return;
    }

    alignas(16) uint8_t req[eldenring::bullet::kFullRequestBytes];
    alignas(16) uint8_t status[64] = {};
    std::memcpy(req, built.data(), sizeof(req));
    uint32_t out = 0xFFFFFFFFu;

    const float shot_damage = pending_shot_damage_.load(std::memory_order_relaxed);
    last_shot_damage_.store(shot_damage, std::memory_order_relaxed);
    last_bullet_id_.store(eldenring::bullet::kBoltBulletId, std::memory_order_relaxed);
    const uint64_t shot_now = GetTickCount64();
    last_bullet_tick_ms_.store(shot_now, std::memory_order_relaxed);
    last_shot_ms_.store(shot_now, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i) last_shot_pos_[i].store(fp.position[i], std::memory_order_relaxed);

    our_bullet_fire_ = true;
    const bool ok = callbacks_.spawn_bullet ?
        callbacks_.spawn_bullet(reinterpret_cast<void*>(manager), &out, req, status) : false;
    our_bullet_fire_ = false;

    uint32_t code = 0;
    std::memcpy(&code, status, sizeof(code));
    log("bullet fire: MC damage %.1f param row 0x%08X +0x1C(dummy poly)=%d flags=0x%X from (%.2f %.2f %.2f) along (%.3f %.3f %.3f) tid=%lu -> %s handle 0x%08X status %u -> %s",
        shot_damage, *reinterpret_cast<uint32_t*>(req + 8),
        *reinterpret_cast<int32_t*>(req + 0x1C), fp.flags, fp.position[0], fp.position[1], fp.position[2], fp.forward[0], fp.forward[1], fp.forward[2],
        callbacks_.get_game_tid ? static_cast<unsigned long>(callbacks_.get_game_tid()) : 0ul,
        ok ? "returned" : "FAULTED", out, code, !ok ? "FAULT" : (eldenring::bullet::spawnFailed(out) ? "REFUSED" : "spawned"));

    if (ok && !eldenring::bullet::spawnFailed(out)) {
        aim_.start(manager, out, cam.forward);
    }
}

bool ProjectileService::queueBulletFire(float mc_damage, RuntimeContext& ctx) {
    if (!bullet_fire_enabled_.load(std::memory_order_relaxed) &&
        !ctx.mode().bullet_fire_enabled.load(std::memory_order_relaxed)) {
        return false;
    }
    if (!hasValidBulletTemplate()) {
        log("bullet fire: no template yet (mc_er_bullet.bin), fire one real crossbow bolt first");
        return false;
    }
    const uint64_t now = GetTickCount64();
    if (now - last_bullet_fire_ms_.load(std::memory_order_relaxed) < 400) return false;
    last_bullet_fire_ms_.store(now, std::memory_order_relaxed);
    pending_shot_damage_.store(mc_damage, std::memory_order_relaxed);
    ctx.mode().bullet_fire_pending.store(true, std::memory_order_relaxed);
    return true;
}

#if ENABLE_CS2_VIEWMODEL
CsShotResult ProjectileService::fireCsShot(const eldenring::cs::CsShotParams& params, DamageQueue& queue, uint64_t tick,
                                           RuntimeContext& ctx) {
    CsShotResult out;
    if (!ctx.mode().cs_mode.load(std::memory_order_relaxed)) return out;
    if (!callbacks_.get_player_chr || !callbacks_.read_camera || !callbacks_.read_physics_pos) return out;
    const bool shot_log = cs_shot_log_enabled_.load(std::memory_order_relaxed);

    const uintptr_t player = callbacks_.get_player_chr();
    CameraPose cam{};
    float feet[3]{};
    if (player == 0 || !callbacks_.read_camera(cam) || !callbacks_.read_physics_pos(player, feet)) {
        if (shot_log) log("CS-SHOT: camera or player unavailable");
        return out;
    }

    // One RNG per input thread; the two uniforms are the whole spread sample.
    static thread_local eldenring::cs::CsShotRng rng(0x51ED270Bu);
    float dir[3];
    eldenring::cs::spreadDirection(cam.forward, params.spread_deg, rng.next01(), rng.next01(), dir);

    const float cam_rel[3] = {cam.position[0] - feet[0], cam.position[1] - feet[1], cam.position[2] - feet[2]};
    const std::vector<eldenring::live::EnemyInfo> enemies = eldenring::live::EntityManager::instance().getCachedEnemies();
    const eldenring::cs::CsShotTrace trace = eldenring::cs::traceTargets(cam_rel, dir, enemies, params);

    out.fired = true;
    for (int i = 0; i < 3; ++i) {
        out.from[i] = cam.position[i] + dir[i] * 0.5f;
        out.to[i] = cam.position[i] + dir[i] * params.max_range_m;
    }

    if (trace.index >= 0) {
        const eldenring::live::EnemyInfo& victim = enemies[static_cast<size_t>(trace.index)];
        // Check the wall along the actual shot ray, not player-feet -> victim-torso: the latter blocks a
        // head shot over low cover and disagrees with what the crosshair sees.
        bool visible = true;
        if (static_ray_) {
            const float to[3] = {cam.position[0] + dir[0] * trace.distance,
                                 cam.position[1] + dir[1] * trace.distance,
                                 cam.position[2] + dir[2] * trace.distance};
            visible = eldenring::live::hasLineOfSight([this](const float f[3], const float d[3]) {
                return static_ray_(f, d);
            }, cam.position, to);
        }
        if (visible) {
            const int er_damage = eldenring::cs::damageToEr(victim.max_hp, params.damage, trace.distance, params);
            const bool queued = er_damage > 0 &&
                queue.enqueue(victim.chr, er_damage, tick, eldenring::cs::kCsShotTag, true);
            out.hit = queued;
            out.victim_chr = victim.chr;
            out.distance = trace.distance;
            out.er_damage = er_damage;
            for (int i = 0; i < 3; ++i) out.to[i] = cam.position[i] + dir[i] * trace.distance;
            if (shot_log) log("CS-SHOT: hit chr=%p npc=%d dist=%.1f m cs=%.1f er=%d queued=%d",
                reinterpret_cast<void*>(victim.chr), victim.npc_id, trace.distance, params.damage, er_damage, queued ? 1 : 0);
        } else {
            if (shot_log) log("CS-SHOT: target chr=%p blocked by static geometry at %.1f m", reinterpret_cast<void*>(victim.chr), trace.distance);
        }
    }

    bool wall_hit = false;
    if (!out.hit && static_ray_) {
        // Stop the tracer on the first wall so a miss visibly ends where the bullet would.
        const float disp[3] = {dir[0] * params.max_range_m, dir[1] * params.max_range_m, dir[2] * params.max_range_m};
        const RayResult r = static_ray_(cam.position, disp);
        if (r.ok && r.hit && r.fraction > 0.f && r.fraction <= 1.f) {
            const float hit_dist = params.max_range_m * r.fraction;
            // Tall-grass/bush collision proxies surround the player's camera and
            // sit inside the view. A static hit this close is the player's own
            // surroundings, not a surface: a decal there is a hole in mid-air.
            constexpr float kMinImpactDistanceM = 1.5f;
            if (hit_dist < kMinImpactDistanceM) {
                if (shot_log) {
                    log("CS-SHOT: ignored near static hit at %.1f m (foliage/player surroundings)", hit_dist);
                }
            } else {
                for (int i = 0; i < 3; ++i) out.to[i] = cam.position[i] + dir[i] * hit_dist;
                // The Havok normal can point into the wall; orient it toward the shooter for the decal.
                eldenring::cs::orientDecalNormal(r.normal, dir, out.normal);
                out.wall_hit = true;
                wall_hit = true;
                if (shot_log) {
                    log("CS-SHOT: wall hit dist=%.1f m normal=(%.2f %.2f %.2f) decal=%d",
                        hit_dist, out.normal[0], out.normal[1], out.normal[2],
                        callbacks_.spawn_decal ? 1 : 0);
                }
            }
        }
    }

    if (!out.hit) {
        if (shot_log) log("CS-SHOT: miss cs=%.1f spread=%.2f dir=(%.3f %.3f %.3f)", params.damage, params.spread_deg, dir[0], dir[1], dir[2]);
    }

    // CS2 shows a tracer every N rounds (AK/M4: 3, AWP: 1), not on every bullet.
    bool show_tracer = false;
    if (params.tracer_frequency > 0u) {
        static thread_local uint32_t s_shot_index[8]{};
        const uint32_t index = s_shot_index[params.weapon_slot & 7u]++;
        show_tracer = eldenring::cs::shouldSpawnTracer(index, params.tracer_frequency);
    }
    if (show_tracer && callbacks_.spawn_tracer) {
        const float dx = out.to[0] - out.from[0];
        const float dy = out.to[1] - out.from[1];
        const float dz = out.to[2] - out.from[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        callbacks_.spawn_tracer(out.from, out.to, eldenring::cs::tracerLifetime(distance));
    }
    // Impact sparks: on the victim, or on the wall a miss stopped at.
    if (callbacks_.spawn_fx && (out.hit || wall_hit)) {
        callbacks_.spawn_fx(3 /* Spark */, out.to, 0);
    }
    // Static-geometry impacts leave a bullet hole. Enemy hits do not: a decal on a moving character would
    // need bone attachment, and the field note's "holes on enemies" gotcha comes from exactly that.
    if (out.wall_hit && callbacks_.spawn_decal) {
        callbacks_.spawn_decal(out.to, out.normal);
    }
    return out;
}
#endif

uint64_t AimCalibrator::nowMs() const {
    return now_ms_ ? now_ms_() : GetTickCount64();
}

void AimCalibrator::log(const char* fmt, ...) {
    if (!callbacks_ || !callbacks_->log) return;
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    callbacks_->log(buffer);
}

void AimCalibrator::start(uint64_t manager, uint32_t handle, const float dir[3]) {
    if (!enabled_.load(std::memory_order_relaxed)) {
        if (!callbacks_ || !callbacks_->file_exists || !callbacks_->file_exists("mc_er_aimcal.txt")) return;
    }
    state_ = AimCalState{};
    state_.manager = static_cast<uintptr_t>(manager);
    state_.handle = handle;
    std::memcpy(state_.aim, dir, sizeof(state_.aim));
    state_.t0_ms = nowMs();
    static uint64_t s_prev_start_ms = 0;
    state_.isolated = state_.t0_ms - s_prev_start_ms > 4000;
    s_prev_start_ms = state_.t0_ms;
    state_.active = true;

    CameraPose cam;
    if (callbacks_ && callbacks_->read_camera && callbacks_->read_camera(cam)) {
        std::memcpy(state_.cam_forward, cam.forward, sizeof(state_.cam_forward));
        state_.have_cam = true;
    }
    const uintptr_t player = (callbacks_ && callbacks_->get_player_chr) ? callbacks_->get_player_chr() : 0;
    float q[4] = {};
    if (player != 0 && callbacks_->read_physics_orient && callbacks_->read_physics_orient(player, q)) {
        const float raw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]);
        state_.body_yaw_raw_deg = raw * 180.f / 3.14159265f;
        state_.body_yaw_deg = (raw + 3.14159265f) * 180.f / 3.14159265f;
        state_.have_body = true;
    }
}

void AimCalibrator::tick() {
    if (!state_.active || !callbacks_) return;
    if (callbacks_->get_game_tid && GetCurrentThreadId() != callbacks_->get_game_tid()) return;

    const uint64_t since = nowMs() - state_.t0_ms;
    if (since < 250u * static_cast<uint64_t>(state_.samples + 1)) return;
    ++state_.samples;

    auto read_bullet = [&](float pos[3], float q[4]) {
        uint64_t obj = 0;
        return callbacks_->safe_read &&
               callbacks_->safe_read(state_.manager, &obj, sizeof(obj)) && obj != 0 &&
               callbacks_->safe_read(static_cast<uintptr_t>(obj) + 0x10, pos, 12) &&
               callbacks_->safe_read(static_cast<uintptr_t>(obj) + 0x20, q, 16);
    };

    if (state_.samples == 2) state_.have2 = read_bullet(state_.pos2, state_.q2);
    if (state_.samples < 4) return;
    state_.active = false;
    float pos4[3], q4[4];
    if (!state_.have2 || !read_bullet(pos4, q4)) return;

    const float flight[3] = {pos4[0] - state_.pos2[0], pos4[1] - state_.pos2[1], pos4[2] - state_.pos2[2]};
    const float moved = std::sqrt(flight[0] * flight[0] + flight[1] * flight[1] + flight[2] * flight[2]);
    float qf[3];
    eldenring::bow::quatForward(q4, qf);
    const eldenring::bow::AimError by_motion = eldenring::bow::aimError(state_.aim, flight);
    const eldenring::bow::AimError by_quat = eldenring::bow::aimError(state_.aim, qf);
    if (state_.have_cam) {
        const eldenring::bow::AimError req_vs_cam = eldenring::bow::aimError(state_.cam_forward, state_.aim);
        log("AIMCAL: the aim is yaw %+.1f pitch %+.1f deg from the camera's forward at the shot", req_vs_cam.yaw_deg, req_vs_cam.pitch_deg);
    }
    const float fy_abs = std::atan2(qf[0], qf[2]) * 180.f / 3.14159265f;
    const float cam_yaw = state_.have_cam ? std::atan2(state_.cam_forward[0], state_.cam_forward[2]) * 180.f / 3.14159265f : 0.f;
    const float aim_yaw = std::atan2(state_.aim[0], state_.aim[2]) * 180.f / 3.14159265f;
    const float body = state_.have_body ? state_.body_yaw_deg : 0.f, body_raw = state_.have_body ? state_.body_yaw_raw_deg : 0.f;
    log("AIMCAL: %s aim=(%.3f %.3f %.3f); moved %.1f m in 0.5 s; flight by motion: yaw %+.1f pitch %+.1f deg off the aim; by quaternion: yaw %+.1f pitch %+.1f",
        state_.isolated ? "ISOLATED" : "overlapping (do not trust)", state_.aim[0], state_.aim[1], state_.aim[2], moved, by_motion.yaw_deg, by_motion.pitch_deg, by_quat.yaw_deg,
        by_quat.pitch_deg);
    log("AIMCAL-YAWS: flight %+.1f | the aim given %+.1f | camera %+.1f | player body %+.1f (raw %+.1f) | flight minus body %+.1f, minus raw body %+.1f, minus camera %+.1f",
        fy_abs, aim_yaw, cam_yaw, body, body_raw, fy_abs - body, fy_abs - body_raw, fy_abs - cam_yaw);
}

} // namespace eldenring::combat
