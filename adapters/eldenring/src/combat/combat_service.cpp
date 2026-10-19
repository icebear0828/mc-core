#include "eldenring_combat_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "eldenring_steve.hpp"
#include "mc/consumables.hpp"
#if ENABLE_CS2_VIEWMODEL
#include "cs_impact_decals.hpp"
#endif

namespace eldenring::combat {

namespace los = eldenring::live::los;
using eldenring::live::setKnockbackStrength;

thread_local bool CombatService::our_bullet_fire_{false};
thread_local ForcedDamage CombatService::forced_damage_{};
thread_local RayResult CombatService::last_ray_{};

CombatService::CombatService() = default;

void CombatService::resetForTesting() {
    callbacks_ = CombatCallbacks{};
    guard_sound_ms_.store(0);
    knockdown_log_ms_.store(0);
    raycast_fn_ = nullptr;
    aimcal_enabled_.store(false);
    proj_log_.store(false);
    arrow_damage_.store(false);
    bullet_fire_enabled_.store(false);
    pdc_hooked_.store(false);
    bolt_attach_poly_.store(-1);
    last_bullet_id_.store(0);
    last_shot_damage_.store(eldenring::bullet::kCrossbowDamage);
    pending_shot_damage_.store(eldenring::bullet::kCrossbowDamage);
    last_bullet_tick_ms_.store(0);
    last_shot_ms_.store(0);
    last_bullet_fire_ms_.store(0);
    for (int i = 0; i < 3; ++i) last_shot_pos_[i].store(0.f);
    proj_log_left_.store(80);
    our_bullet_fire_ = false;
    forced_damage_ = ForcedDamage{};
    last_ray_ = RayResult{};
    aimcal_ = AimCalState{};
    std::lock_guard<std::mutex> g(bullet_mutex_);
    bullet_template_valid_ = false;
    std::memset(bullet_template_, 0, sizeof(bullet_template_));
}

void CombatService::log(const char* fmt, ...) {
    if (!callbacks_.log) return;
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    callbacks_.log(buffer);
}

ShieldVerdict CombatService::evaluateShield(uintptr_t player, uintptr_t attacker_chr) {
    ShieldVerdict v;
    if (!callbacks_.read_physics_pos) return v;

    v.ok_player = callbacks_.read_physics_pos(player, v.pp);
    v.ok_attacker = attacker_chr != 0 && callbacks_.read_physics_pos(attacker_chr, v.ap);
    v.ok_orient = callbacks_.read_physics_orient && callbacks_.read_physics_orient(player, v.q);

    CameraPose cam{};
    v.ok_cam = callbacks_.read_camera && callbacks_.read_camera(cam);

    if (v.ok_orient) v.body_yaw = eldenring::render::yawFromQuat(v.q[0], v.q[1], v.q[2], v.q[3]);
    if (v.ok_cam) v.cam_yaw = eldenring::shield::yawFromForward(cam.forward[0], cam.forward[2]);

    const bool have_facing = v.ok_cam || v.ok_orient;
    v.yaw = v.ok_cam ? v.cam_yaw : v.body_yaw;
    const bool has_pos = v.ok_player && v.ok_attacker && have_facing;

    if (has_pos) {
        v.dot = eldenring::shield::frontDot(v.pp, v.yaw, v.ap);
        if (v.ok_orient) v.body_dot = eldenring::shield::frontDot(v.pp, v.body_yaw, v.ap);
    }
    v.blocks = eldenring::shield::shieldBlocksHit(attacker_chr != 0, has_pos, v.pp, v.yaw, v.ap);
    return v;
}

int CombatService::evaluateExplosionShieldBlock(uintptr_t player, const float explosion_pos[3], int raw_damage, RuntimeContext& ctx) {
    if (raw_damage <= 0 || !ctx.mode().shield_blocking.load(std::memory_order_relaxed) || !callbacks_.read_physics_pos) {
        return raw_damage;
    }
    float pp[3]{};
    if (!callbacks_.read_physics_pos(player, pp)) {
        return raw_damage;
    }

    CameraPose cam{};
    float q[4]{};
    const bool ok_cam = callbacks_.read_camera && callbacks_.read_camera(cam);
    const bool ok_orient = callbacks_.read_physics_orient && callbacks_.read_physics_orient(player, q);
    if (!ok_cam && !ok_orient) {
        return raw_damage;
    }

    const float yaw = ok_cam ? eldenring::shield::yawFromForward(cam.forward[0], cam.forward[2])
                             : eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]);

    if (mc::isAttackBlocked(pp, yaw, explosion_pos)) {
        if (callbacks_.play_sound) {
            callbacks_.play_sound("item.shield.block", 1.0f);
        }
        log("SHIELD: blocked explosion at (%.2f %.2f %.2f) (dmg %d -> 0)", explosion_pos[0], explosion_pos[1], explosion_pos[2], raw_damage);
        return 0;
    }
    return raw_damage;
}

bool CombatService::playerHitSurvival(void* module, void* attacker, uint8_t* ctx_ptr, uint8_t blocked,
                                     uint32_t& popped_heal_target, MeleeController& melee,
                                     SurvivalState& survival,
                                     HitFeedback& feedback,
                                     HungerModel& hunger,
                                     RuntimeContext& ctx) {
    if (blocked != 0) return false;
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    if (player == 0 || !callbacks_.safe_read) return false;

    if (module != nullptr) {
        uint64_t owner = 0;
        if (!callbacks_.safe_read(reinterpret_cast<uintptr_t>(module) + layout::kOwnerInDataModule, &owner, sizeof(owner)) ||
            owner != player) {
            return false;
        }
    }

    Vitals v{};
    if (!callbacks_.read_vitals || !callbacks_.read_vitals(player, v) || v.hp <= 0 || v.max_hp <= 0) {
        return false;
    }

    int32_t dmg = 0;
    if (!callbacks_.safe_read(reinterpret_cast<uintptr_t>(ctx_ptr) + layout::kHitDamage, &dmg, sizeof(dmg)) || dmg <= 0) {
        return false;
    }

    if (ctx.mode().shield_blocking.load(std::memory_order_relaxed)) {
        const ShieldVerdict sv = evaluateShield(player, reinterpret_cast<uintptr_t>(attacker));
        if (sv.blocks) {
            if (callbacks_.play_sound) callbacks_.play_sound("item.shield.block", 1.0f);
            if (callbacks_.safe_write) {
                const uint32_t zero_dmg = 0;
                callbacks_.safe_write(reinterpret_cast<uintptr_t>(ctx_ptr) + layout::kHitDamage, &zero_dmg, sizeof(zero_dmg));
                const uint8_t zero_reaction = 0;
                callbacks_.safe_write(reinterpret_cast<uintptr_t>(ctx_ptr) + layout::kHitEnableReaction, &zero_reaction, sizeof(zero_reaction));
            }
            log("SHIELD: blocked hit from %p (dmg %d -> 0)", attacker, dmg);
            return false;
        }
    }

    bool popped = false;
    int32_t out = dmg;
    int armor_pts = 0;
    float reduction_ratio = 0.0f;
    int32_t dmg_after_absorb = survival.absorb(dmg, v.max_hp);
    armor_pts = melee.inventory().totalArmorPoints();
    reduction_ratio = melee.inventory().defenseReductionRatio();
    const ArmorDamageReductionResult ar = computeArmorDamageReduction(dmg, dmg_after_absorb, armor_pts, reduction_ratio);
    out = ar.final_damage;
    if (armor_pts > 0) {
        feedback.onArmorDamage(dmg, out, armor_pts, reduction_ratio);
    }
    const TotemOutcome t = totemClamp(out, v.hp, melee.holds(mc::ItemId::TotemOfUndying));
    out = t.damage;
    if (t.popped && melee.consumeHeld(mc::ItemId::TotemOfUndying)) {
        popped = true;
        survival.apply({mc::ActiveEffect{mc::EffectType::Regeneration, 2, 45.f},
                        mc::ActiveEffect{mc::EffectType::Absorption, 2, 5.f}});
        feedback.onTotem();
        popped_heal_target = static_cast<uint32_t>(std::max(1, scaleToEr(2.f, v.max_hp)));
    } else if (t.popped) {
        out = dmg;
    }

    if (callbacks_.play_sound) {
        callbacks_.play_sound(popped ? "item.totem.use" : "entity.player.hurt", 1.0f);
    }

    if (attacker != nullptr && callbacks_.read_physics_pos && callbacks_.read_physics_orient) {
        float side = 1.f, pp[3] = {}, ap[3] = {}, q[4] = {};
        if (callbacks_.read_physics_pos(player, pp) &&
            callbacks_.read_physics_pos(reinterpret_cast<uintptr_t>(attacker), ap) &&
            callbacks_.read_physics_orient(player, q)) {
            const float yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]);
            side = ((ap[0] - pp[0]) * std::cos(yaw) - (ap[2] - pp[2]) * std::sin(yaw)) >= 0.f ? 1.f : -1.f;
        }
        feedback.onHurt(side);
        hunger.addExhaustion(0.1f);
    }

    if (out != dmg && callbacks_.safe_write) {
        const uint32_t final_w = static_cast<uint32_t>(out);
        callbacks_.safe_write(reinterpret_cast<uintptr_t>(ctx_ptr) + layout::kHitDamage, &final_w, sizeof(final_w));
    }

    log("ARMOR-DAMAGE: raw=%d absorb=%d armor_pts=%d (-%.1f%%) => final=%d (hp %d/%d)%s",
        dmg, dmg_after_absorb, armor_pts, reduction_ratio * 100.0f, out, v.hp, v.max_hp, popped ? " [TOTEM POPPED]" : "");
    return popped;
}

bool CombatService::handleGuardCheck(void* attacker_module, void* victim_chr, RuntimeContext& ctx) {
    if (!ctx.mode().shield_blocking.load(std::memory_order_relaxed)) return false;
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    if (player == 0 || reinterpret_cast<uintptr_t>(victim_chr) != player || !callbacks_.safe_read) {
        return false;
    }

    uint64_t attacker_chr = 0;
    if (attacker_module != nullptr) {
        callbacks_.safe_read(reinterpret_cast<uintptr_t>(attacker_module) + 8, &attacker_chr, sizeof(attacker_chr));
    }
    const ShieldVerdict sv = evaluateShield(player, static_cast<uintptr_t>(attacker_chr));
    if (sv.blocks) {
        const uint64_t now = GetTickCount64();
        if (now - guard_sound_ms_.load(std::memory_order_relaxed) > 150) {
            guard_sound_ms_.store(now, std::memory_order_relaxed);
            if (callbacks_.play_sound) callbacks_.play_sound("item.shield.block", 1.0f);
        }
        return true;
    }
    return false;
}

void CombatService::handleDamageVfunc7(void* damage_module, void* attacker, uint8_t* ctx_ptr, RuntimeContext& ctx) {
    if (!ctx.mode().shield_blocking.load(std::memory_order_relaxed) || damage_module == nullptr || ctx_ptr == nullptr) {
        return;
    }
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    if (player == 0 || !callbacks_.safe_read || !callbacks_.safe_write) return;

    uint64_t victim = 0;
    if (callbacks_.safe_read(reinterpret_cast<uintptr_t>(damage_module) + 8, &victim, sizeof(victim)) && victim == player) {
        const ShieldVerdict sv = evaluateShield(player, reinterpret_cast<uintptr_t>(attacker));
        uint8_t d9 = 0;
        callbacks_.safe_read(reinterpret_cast<uintptr_t>(ctx_ptr) + 0xD9, &d9, 1);
        if (sv.blocks) {
            const uint32_t zero_force = 0;
            callbacks_.safe_write(reinterpret_cast<uintptr_t>(ctx_ptr) + layout::kHitKnockbackIn, &zero_force, sizeof(zero_force));
            if (d9 != 1 && d9 != 3) {
                const uint8_t one = 1;
                callbacks_.safe_write(reinterpret_cast<uintptr_t>(ctx_ptr) + 0xD9, &one, 1);
            }
        }
    }
}

bool CombatService::handleBreakTest(void* damage_module, uint8_t* ctx_ptr, bool original_broke, RuntimeContext& ctx) {
    if (damage_module == nullptr || !callbacks_.safe_read) return original_broke;
    uint64_t victim = 0;
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    const bool is_player = player != 0 && callbacks_.safe_read(reinterpret_cast<uintptr_t>(damage_module) + 8, &victim, sizeof(victim)) && victim == player;

    if (!eldenring::live::shouldSoftenHitReaction(ctx.mode().mc_mode.load(std::memory_order_relaxed), is_player, ctx.mode().knockdown_fix.load(std::memory_order_relaxed))) {
        return original_broke;
    }

    uint64_t buffer = 0;
    if (callbacks_.safe_read(reinterpret_cast<uintptr_t>(damage_module) + layout::kDamageHitBuffer, &buffer, sizeof(buffer)) && buffer != 0 && callbacks_.safe_write) {
        const uint32_t zero_force = 0;
        const uint8_t zero_flag = 0;
        callbacks_.safe_write(static_cast<uintptr_t>(buffer) + layout::kHitBufferForce, &zero_force, sizeof(zero_force));
        callbacks_.safe_write(static_cast<uintptr_t>(buffer) + layout::kHitBufferBreakFlag, &zero_flag, sizeof(zero_flag));
    }
    if (original_broke) {
        const uint64_t now = GetTickCount64();
        if (now - knockdown_log_ms_.load(std::memory_order_relaxed) > 500) {
            knockdown_log_ms_.store(now, std::memory_order_relaxed);
            uint8_t level = 0;
            if (ctx_ptr != nullptr) callbacks_.safe_read(reinterpret_cast<uintptr_t>(ctx_ptr) + 0xDA, &level, 1);
            log("KNOCKDOWN: break test softened for player (ctx+0xDA=%u)", level);
        }
    }
    return false;
}

void CombatService::drainDamageQueue(DamageQueue& queue, const HitTemplate* tmpl,
                                    uintptr_t updating_data_module, uint64_t tick,
                                    mc::Inventory& inv, HitFeedback& feedback,
                                    XpModel& xp, RuntimeContext& ctx) {
    (void)inv;
    DrainContext c;
    c.reader = callbacks_.reader;
    c.image_base = callbacks_.image_base;
    c.on_game_thread = true;
    c.tick = tick;
    c.updating_data_module = updating_data_module;
    c.require_victim_updating = ctx.mode().require_victim_updating.load(std::memory_order_relaxed);
    c.hit_template = tmpl;
    c.allow_neutral = true;
    if (ctx.mode().los_enabled.load(std::memory_order_relaxed)) {
        if (callbacks_.has_line_of_sight) {
            c.line_of_sight = callbacks_.has_line_of_sight;
        } else if (raycast_fn_ != nullptr) {
            c.line_of_sight = [this](const float f[3], const float t[3]) {
                return this->playerCanSee(f, t);
            };
        }
    }
    c.max_per_drain = 1;
    c.expire_after_ticks = 60 * 5;

    int32_t hp_after = -1;

    c.invoke = [&](uintptr_t dmg, uintptr_t attacker, void* ctx_ptr) {
        uint64_t owner = 0;
        if (callbacks_.safe_read) {
            callbacks_.safe_read(dmg + layout::kOwnerInDataModule, &owner, sizeof(owner));
        }
        Vitals before_v{};
        const int32_t before = (callbacks_.read_vitals && owner != 0 && callbacks_.read_vitals(static_cast<uintptr_t>(owner), before_v))
            ? before_v.hp : -1;

        int32_t wanted = 0;
        if (ctx_ptr != nullptr && callbacks_.safe_read) {
            callbacks_.safe_read(reinterpret_cast<uintptr_t>(ctx_ptr) + layout::kHitDamage, &wanted, sizeof(wanted));
        }

        if (pdc_hooked_.load(std::memory_order_relaxed)) {
            forced_damage_ = ForcedDamage{};
            forced_damage_.armed = true;
            forced_damage_.attacker = attacker;
            forced_damage_.victim = owner;
            forced_damage_.value = wanted;
        }

        const float kb_force = ctx.ui().kb_force.load(std::memory_order_relaxed);
        if (kb_force >= 0.f && ctx_ptr != nullptr) {
            const float kb_before = setKnockbackStrength(static_cast<uint8_t*>(ctx_ptr), kb_force);
            log("KNOCKBACK: ctx+0xFC %.2f -> %.2f", kb_before, kb_force);
        }

        const bool ok = callbacks_.call_vfunc7 ? callbacks_.call_vfunc7(dmg, attacker, ctx_ptr) : false;

        const ForcedDamage forced = forced_damage_;
        forced_damage_ = ForcedDamage{};

        Vitals after_v{};
        const int32_t after = (callbacks_.read_vitals && owner != 0 && callbacks_.read_vitals(static_cast<uintptr_t>(owner), after_v))
            ? after_v.hp : -1;
        hp_after = after;

        if (pdc_hooked_.load(std::memory_order_relaxed)) {
            log("DAMAGE: wanted=%d engine=%d override=%s hp delta=%d",
                wanted, forced.engine_value, forced.applied ? "applied" : "NOT applied", before - after);
        }

        uint32_t computed = 0;
        if (ctx_ptr != nullptr && callbacks_.safe_read) {
            callbacks_.safe_read(reinterpret_cast<uintptr_t>(ctx_ptr) + layout::kHitDamage, &computed, sizeof(computed));
        }

        if (!ok) {
            ctx.mode().damage_enabled.store(false, std::memory_order_relaxed);
            log("DAMAGE: vfunc[7] raised an exception on chr=%p, damage disabled", reinterpret_cast<void*>(owner));
        } else {
            log("DAMAGE: vfunc[7] chr=%p hp %d -> %d (ctx+0x228 now %u)", reinterpret_cast<void*>(owner), before, after, computed);
        }
    };

    for (const DamageResult& r : queue.drain(c)) {
        if (r.outcome == DamageOutcome::Applied) {
            feedback.onHit((r.request.tag & 1u) != 0);
            if (callbacks_.spawn_fx && callbacks_.read_physics_pos) {
                float vpos[3] = {};
                if (callbacks_.read_physics_pos(r.request.victim_chr, vpos)) {
                    vpos[1] += 1.0f;
                    const int hearts = static_cast<int>((r.request.tag >> 8) & 0xFFu) / 2;
                    if (hearts > 0) callbacks_.spawn_fx(0 /* Damage */, vpos, hearts);
                    if (r.request.tag & 1u) callbacks_.spawn_fx(1 /* Crit */, vpos, 0);
                    if (r.request.tag & 2u) callbacks_.spawn_fx(2 /* Sweep */, vpos, 0);
                }
            }
            // CS bullets have no arrow hit sound: the crosshair hitmarker is the feedback.
            if (callbacks_.play_sound) {
#if ENABLE_CS2_VIEWMODEL
                if (!(r.request.tag & eldenring::cs::kCsShotTag))
#endif
                callbacks_.play_sound((r.request.tag & 1u) ? "entity.player.attack.crit"
                                      : (r.request.tag & 2u) ? "entity.player.attack.sweep"
                                      : (r.request.tag & 4u) ? "entity.player.attack.strong"
                                                             : "entity.player.attack.weak", 1.0f);
            }
            if (hp_after == 0) {
                feedback.onKill();
                Vitals victim{};
                const int earned_xp = (callbacks_.read_vitals && callbacks_.read_vitals(r.request.victim_chr, victim)) ? xpForKill(victim.max_hp) : 0;
                const int ups = xp.add(earned_xp);
                if (callbacks_.play_sound) {
                    callbacks_.play_sound(ups > 0 ? "entity.player.levelup" : "entity.experience_orb.pickup", 0.7f);
                }
                log("XP: +%d (level %d, %.0f%%)%s", earned_xp, xp.level(), xp.progress() * 100.f, ups > 0 ? " LEVEL UP" : "");
            }
        }
        if (r.outcome != DamageOutcome::Applied) {
            log("DAMAGE: request chr=%p -> %d", reinterpret_cast<void*>(r.request.victim_chr), static_cast<int>(r.outcome));
        }
    }
}

void CombatService::recordBulletTemplate(const void* request) {
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

void CombatService::loadBulletTemplate(const std::string& game_dir) {
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

void CombatService::saveBulletTemplate(const uint8_t* body, const std::string& game_dir) {
    if (body == nullptr) return;
    const std::string path = game_dir + "mc_er_bullet.bin";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || f == nullptr) return;
    fwrite(body, 1, eldenring::bullet::kFullRequestBytes, f);
    fclose(f);
    log("bullet: template saved to %s", path.c_str());
}

bool CombatService::hasValidBulletTemplate() const {
    std::lock_guard<std::mutex> g(bullet_mutex_);
    return bullet_template_valid_;
}

void CombatService::getBulletTemplate(uint8_t out[eldenring::bullet::kFullRequestBytes]) const {
    std::lock_guard<std::mutex> g(bullet_mutex_);
    std::memcpy(out, bullet_template_, eldenring::bullet::kFullRequestBytes);
}

static bool invokeNativeRaycast(RaycastFn fn, void* world, uint32_t filter,
                               const HkVec4* from, const HkVec4* disp,
                               HkVec4* out_pos, HkVec4* out_normal, float* out_fraction) {
    __try {
        return fn(world, filter, from, disp, out_pos, out_normal, out_fraction);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

RayResult CombatService::castStaticRay(const float from[3], const float disp[3]) {
    last_ray_ = RayResult{};
    RayResult r;
    if (!raycast_fn_ || !callbacks_.safe_read) return r;

    uint64_t havok = 0, world = 0;
    if (!callbacks_.safe_read(callbacks_.image_base + los::kHavokManGlobalRva, &havok, sizeof(havok)) || havok == 0) return r;
    uint64_t vt = 0;
    if (!callbacks_.safe_read(static_cast<uintptr_t>(havok), &vt, sizeof(vt)) || vt != callbacks_.image_base + los::kHavokManVtableRva) return r;
    if (!callbacks_.safe_read(static_cast<uintptr_t>(havok) + los::kPhysWorldInHavokMan, &world, sizeof(world)) || world == 0) return r;

    alignas(16) HkVec4 f = {{from[0], from[1], from[2], 0.f}};
    alignas(16) HkVec4 d = {{disp[0], disp[1], disp[2], 0.f}};
    alignas(16) HkVec4 pos = {}, normal = {};
    float fraction = 1.f;

    r.hit = invokeNativeRaycast(raycast_fn_, reinterpret_cast<void*>(world), los::kStaticGeometryFilter, &f, &d, &pos, &normal, &fraction);
    r.ok = std::isfinite(fraction);
    r.fraction = fraction;
    for (int i = 0; i < 3; ++i) {
        r.pos[i] = pos.v[i];
        r.normal[i] = normal.v[i];
    }
    last_ray_ = r;
    return r;
}

RayResult CombatService::lastRay() const {
    return last_ray_;
}

bool CombatService::playerCanSee(const float player_feet[3], const float victim_feet[3]) {
    const float from[3] = {player_feet[0], player_feet[1] + los::kEyeHeight, player_feet[2]};
    const float to[3] = {victim_feet[0], victim_feet[1] + los::kTargetHeight, victim_feet[2]};
    const bool visible = eldenring::live::hasLineOfSight([this](const float f[3], const float d[3]) {
        return this->castStaticRay(f, d);
    }, from, to);
    const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    log("los: dist=%.2f ray ok=%d hit=%d at=%.2f m -> %s", dist, last_ray_.ok ? 1 : 0, last_ray_.hit ? 1 : 0,
        last_ray_.fraction * dist, visible ? "visible" : "BLOCKED");
    return visible;
}

bool CombatService::clickAttack(float charged, MeleeController& melee, CombatEngine& engine, DamageQueue& queue, RuntimeContext& ctx) {
    CameraPose cam;
    if (!callbacks_.read_camera || !callbacks_.read_camera(cam)) {
        log("click: no camera or player");
        return false;
    }
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    if (player == 0 || !callbacks_.read_physics_pos) {
        log("click: no camera or player");
        return false;
    }
    float feet[3] = {};
    if (!callbacks_.read_physics_pos(player, feet)) {
        log("click: no camera or player");
        return false;
    }

    const float cam_rel[3] = {cam.position[0] - feet[0], cam.position[1] - feet[1], cam.position[2] - feet[2]};
    if (!callbacks_.reader) return false;
    const uint64_t now_tick = GetTickCount64();
    const auto pick_res = EntityManager::instance().pickTargetWithFallback(
        cam_rel, cam.forward, *callbacks_.reader, callbacks_.image_base, now_tick, feet);
    const int idx = pick_res.index;
    const auto& list = pick_res.list;
    if (idx < 0) {
        int near_count = 0;
        char detail_buf[512];
        int dn = snprintf(detail_buf, sizeof(detail_buf), "[");
        for (const EnemyInfo& e : list) {
            const float d = std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z);
            if (d > 8.f || near_count >= 5) continue;
            ++near_count;
            const float c[3] = {e.rel_x - cam_rel[0], e.rel_y + 1.f - cam_rel[1], e.rel_z - cam_rel[2]};
            const float t = c[0] * cam.forward[0] + c[1] * cam.forward[1] + c[2] * cam.forward[2];
            const float q[3] = {c[0] - cam.forward[0] * t, c[1] - cam.forward[1] * t, c[2] - cam.forward[2] * t};
            dn += snprintf(detail_buf + dn, sizeof(detail_buf) - static_cast<size_t>(dn),
                           " (%s npc=%d team=%u dist=%.1f t=%.1f off-ray=%.1f)",
                           e.hostile ? "H" : "N", e.npc_id, static_cast<unsigned>(e.team), d, t,
                           std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]));
        }
        snprintf(detail_buf + dn, sizeof(detail_buf) - static_cast<size_t>(dn), " ]");
        log("click: no target (cam_rel %.1f %.1f %.1f, fwd %.2f %.2f %.2f, entities<=8m: %s)",
            cam_rel[0], cam_rel[1], cam_rel[2], cam.forward[0], cam.forward[1], cam.forward[2], detail_buf);
        return false;
    }

    const EnemyInfo& e = list[static_cast<size_t>(idx)];
    const bool falling = callbacks_.is_falling ? callbacks_.is_falling() : false;
    const mc::ItemId held = melee.heldItem();
    const mc::HitIntent intent = melee.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None,
                                                 charged, falling, !falling, {},
                                                 {cam.forward[0], cam.forward[1], cam.forward[2]});

    const float dmg_scale = ctx.mode().damage_scale.load(std::memory_order_relaxed);
    const float hp_pct = ctx.mode().damage_hp_percent.load(std::memory_order_relaxed);
    const int dmg = live::erDamage(intent, e.max_hp, dmg_scale, hp_pct);
    const bool ok = dmg > 0 && queue.enqueue(e.chr, dmg, now_tick,
        (intent.is_critical ? 1u : 0u) | (intent.is_sweeping ? 2u : 0u) | (charged > 0.848f ? 4u : 0u) |
        (static_cast<uint32_t>(std::min(255.f, std::floor(intent.damage))) << 8));

    log("click: item=%d charged=%.2f falling=%d crit=%d sweep=%d mc_dmg=%.2f -> %d on chr=%p npc=%d team=%u (%s) hp=%d/%d (%s)",
        static_cast<int>(held), charged, falling ? 1 : 0, intent.is_critical ? 1 : 0, intent.is_sweeping ? 1 : 0,
        intent.damage, dmg, reinterpret_cast<void*>(e.chr), e.npc_id, static_cast<unsigned>(e.team),
        e.hostile ? "hostile" : "neutral", e.hp, e.max_hp, ok ? "ok" : "not queued");

    if (intent.is_sweeping) {
        int sweep_count = 0;
        const float r_max = ctx.mode().sweep_radius.load(std::memory_order_relaxed);
        const float r2_max = r_max * r_max;
        for (size_t i = 0; i < list.size(); ++i) {
            if (i == static_cast<size_t>(idx)) continue;
            const EnemyInfo& sec = list[i];
            if (!sec.hostile || sec.chr == 0 || sec.hp <= 0) continue;
            const float dx = sec.rel_x - e.rel_x;
            const float dy = sec.rel_y - e.rel_y;
            const float dz = sec.rel_z - e.rel_z;
            if (dx * dx + dy * dy + dz * dz <= r2_max) {
                mc::HitIntent sweep_intent = intent;
                sweep_intent.damage = std::max(1.0f, intent.damage * 0.4f);
                sweep_intent.is_sweeping = true;
                const int sec_dmg = live::erDamage(sweep_intent, sec.max_hp, dmg_scale, hp_pct);
                if (sec_dmg > 0) {
                    const uint32_t sec_tag = 2u | (static_cast<uint32_t>(std::min(255.f, std::floor(sweep_intent.damage))) << 8);
                    if (queue.enqueue(sec.chr, sec_dmg, now_tick, sec_tag)) {
                        ++sweep_count;
                    }
                }
            }
        }
        if (sweep_count > 0) {
            log("click: sweep splash hit %d secondary targets within %.1fm", sweep_count, r_max);
        }
    }
    return dmg > 0;
}

void CombatService::enqueueNearestHostile(DamageQueue& queue, uint64_t tick, float max_dist) {
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    float feet[3] = {};
    if (player != 0 && callbacks_.read_physics_pos) {
        callbacks_.read_physics_pos(player, feet);
    }
    if (!callbacks_.reader) return;
    const auto res = EntityManager::instance().findNearestHostileWithFallback(
        max_dist, *callbacks_.reader, callbacks_.image_base, tick, feet);
    if (!res.found) {
        log("F8: no hostile enemy within %.0f m", max_dist);
        return;
    }
    const float best_d = std::sqrt(res.info.rel_x * res.info.rel_x + res.info.rel_y * res.info.rel_y + res.info.rel_z * res.info.rel_z);
    const bool ok = queue.enqueue(res.info.chr, 50, tick);
    log("F8: queued 50 on chr=%p npc=%d team=%u hp=%d dist=%.1f m (%s)",
        reinterpret_cast<void*>(res.info.chr), res.info.npc_id,
        static_cast<unsigned>(res.info.team), res.info.hp, best_d, ok ? "ok" : "queue full");
}

void CombatService::onSpawnBullet(const void* request) {
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

void CombatService::handleProjectileHit(void* module, void* attacker, uint8_t* hit_ctx, HitFeedback& feedback, RuntimeContext& ctx_rt) {
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

void CombatService::runBulletFire(RuntimeContext& ctx) {
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
        aimCalStart(manager, out, cam.forward);
    }
}

bool CombatService::queueBulletFire(float mc_damage, RuntimeContext& ctx) {
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
CsShotResult CombatService::fireCsShot(const eldenring::cs::CsShotParams& params, DamageQueue& queue, uint64_t tick,
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
    const std::vector<EnemyInfo> enemies = EntityManager::instance().getCachedEnemies();
    const eldenring::cs::CsShotTrace trace = eldenring::cs::traceTargets(cam_rel, dir, enemies, params);

    out.fired = true;
    for (int i = 0; i < 3; ++i) {
        out.from[i] = cam.position[i] + dir[i] * 0.5f;
        out.to[i] = cam.position[i] + dir[i] * params.max_range_m;
    }

    if (trace.index >= 0) {
        const EnemyInfo& victim = enemies[static_cast<size_t>(trace.index)];
        // Check the wall along the actual shot ray, not player-feet -> victim-torso: the latter blocks a
        // head shot over low cover and disagrees with what the crosshair sees.
        bool visible = true;
        if (raycast_fn_ != nullptr) {
            const float to[3] = {cam.position[0] + dir[0] * trace.distance,
                                 cam.position[1] + dir[1] * trace.distance,
                                 cam.position[2] + dir[2] * trace.distance};
            visible = eldenring::live::hasLineOfSight([this](const float f[3], const float d[3]) {
                return this->castStaticRay(f, d);
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
    if (!out.hit && raycast_fn_ != nullptr) {
        // Stop the tracer on the first wall so a miss visibly ends where the bullet would.
        const float disp[3] = {dir[0] * params.max_range_m, dir[1] * params.max_range_m, dir[2] * params.max_range_m};
        const RayResult r = castStaticRay(cam.position, disp);
        if (r.ok && r.hit && r.fraction > 0.f && r.fraction <= 1.f) {
            for (int i = 0; i < 3; ++i) out.to[i] = cam.position[i] + dir[i] * (params.max_range_m * r.fraction);
            // The Havok normal can point into the wall; orient it toward the shooter for the decal.
            eldenring::cs::orientDecalNormal(r.normal, dir, out.normal);
            out.wall_hit = true;
            wall_hit = true;
            if (shot_log) {
                log("CS-SHOT: wall hit dist=%.1f m normal=(%.2f %.2f %.2f) decal=%d",
                    params.max_range_m * r.fraction, out.normal[0], out.normal[1], out.normal[2],
                    callbacks_.spawn_decal ? 1 : 0);
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
#if ENABLE_CS2_VIEWMODEL
    // Static-geometry impacts leave a bullet hole. Enemy hits do not: a decal on a moving character would
    // need bone attachment, and the field note's "holes on enemies" gotcha comes from exactly that.
    if (out.wall_hit && callbacks_.spawn_decal) {
        callbacks_.spawn_decal(out.to, out.normal);
    }
#endif
    return out;
}
#endif

void CombatService::aimCalStart(uint64_t manager, uint32_t handle, const float dir[3]) {
    if (!aimcal_enabled_.load(std::memory_order_relaxed)) {
        if (!callbacks_.file_exists || !callbacks_.file_exists("mc_er_aimcal.txt")) return;
    }
    aimcal_ = AimCalState{};
    aimcal_.manager = static_cast<uintptr_t>(manager);
    aimcal_.handle = handle;
    std::memcpy(aimcal_.aim, dir, sizeof(aimcal_.aim));
    aimcal_.t0_ms = GetTickCount64();
    static uint64_t s_prev_start_ms = 0;
    aimcal_.isolated = aimcal_.t0_ms - s_prev_start_ms > 4000;
    s_prev_start_ms = aimcal_.t0_ms;
    aimcal_.active = true;

    CameraPose cam;
    if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
        std::memcpy(aimcal_.cam_forward, cam.forward, sizeof(aimcal_.cam_forward));
        aimcal_.have_cam = true;
    }
    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    float q[4] = {};
    if (player != 0 && callbacks_.read_physics_orient && callbacks_.read_physics_orient(player, q)) {
        const float raw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]);
        aimcal_.body_yaw_raw_deg = raw * 180.f / 3.14159265f;
        aimcal_.body_yaw_deg = (raw + 3.14159265f) * 180.f / 3.14159265f;
        aimcal_.have_body = true;
    }
}

void CombatService::aimCalTick() {
    if (!aimcal_.active) return;
    if (callbacks_.get_game_tid && GetCurrentThreadId() != callbacks_.get_game_tid()) return;

    const uint64_t since = GetTickCount64() - aimcal_.t0_ms;
    if (since < 250u * static_cast<uint64_t>(aimcal_.samples + 1)) return;
    ++aimcal_.samples;

    auto read_bullet = [&](float pos[3], float q[4]) {
        uint64_t obj = 0;
        return callbacks_.safe_read &&
               callbacks_.safe_read(aimcal_.manager, &obj, sizeof(obj)) && obj != 0 &&
               callbacks_.safe_read(static_cast<uintptr_t>(obj) + 0x10, pos, 12) &&
               callbacks_.safe_read(static_cast<uintptr_t>(obj) + 0x20, q, 16);
    };

    if (aimcal_.samples == 2) aimcal_.have2 = read_bullet(aimcal_.pos2, aimcal_.q2);
    if (aimcal_.samples < 4) return;
    aimcal_.active = false;
    float pos4[3], q4[4];
    if (!aimcal_.have2 || !read_bullet(pos4, q4)) return;

    const float flight[3] = {pos4[0] - aimcal_.pos2[0], pos4[1] - aimcal_.pos2[1], pos4[2] - aimcal_.pos2[2]};
    const float moved = std::sqrt(flight[0] * flight[0] + flight[1] * flight[1] + flight[2] * flight[2]);
    float qf[3];
    eldenring::bow::quatForward(q4, qf);
    const eldenring::bow::AimError by_motion = eldenring::bow::aimError(aimcal_.aim, flight);
    const eldenring::bow::AimError by_quat = eldenring::bow::aimError(aimcal_.aim, qf);
    if (aimcal_.have_cam) {
        const eldenring::bow::AimError req_vs_cam = eldenring::bow::aimError(aimcal_.cam_forward, aimcal_.aim);
        log("AIMCAL: the aim is yaw %+.1f pitch %+.1f deg from the camera's forward at the shot", req_vs_cam.yaw_deg, req_vs_cam.pitch_deg);
    }
    const float fy_abs = std::atan2(qf[0], qf[2]) * 180.f / 3.14159265f;
    const float cam_yaw = aimcal_.have_cam ? std::atan2(aimcal_.cam_forward[0], aimcal_.cam_forward[2]) * 180.f / 3.14159265f : 0.f;
    const float aim_yaw = std::atan2(aimcal_.aim[0], aimcal_.aim[2]) * 180.f / 3.14159265f;
    const float body = aimcal_.have_body ? aimcal_.body_yaw_deg : 0.f, body_raw = aimcal_.have_body ? aimcal_.body_yaw_raw_deg : 0.f;
    log("AIMCAL: %s aim=(%.3f %.3f %.3f); moved %.1f m in 0.5 s; flight by motion: yaw %+.1f pitch %+.1f deg off the aim; by quaternion: yaw %+.1f pitch %+.1f",
        aimcal_.isolated ? "ISOLATED" : "overlapping (do not trust)", aimcal_.aim[0], aimcal_.aim[1], aimcal_.aim[2], moved, by_motion.yaw_deg, by_motion.pitch_deg, by_quat.yaw_deg,
        by_quat.pitch_deg);
    log("AIMCAL-YAWS: flight %+.1f | the aim given %+.1f | camera %+.1f | player body %+.1f (raw %+.1f) | flight minus body %+.1f, minus raw body %+.1f, minus camera %+.1f",
        fy_abs, aim_yaw, cam_yaw, body, body_raw, fy_abs - body, fy_abs - body_raw, fy_abs - cam_yaw);
}

bool CombatService::isForcedDamageArmed() const {
    return forced_damage_.armed;
}

ForcedDamage CombatService::forcedDamage() const {
    return forced_damage_;
}

void CombatService::handleForcedDamage(uint8_t* ctx) {
    if (forced_damage_.armed && !forced_damage_.applied && callbacks_.override_final_damage) {
        forced_damage_.applied = callbacks_.override_final_damage(
            ctx, forced_damage_.attacker, forced_damage_.victim, forced_damage_.value, &forced_damage_.engine_value);
    }
}

} // namespace eldenring::combat
