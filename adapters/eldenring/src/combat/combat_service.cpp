#include "eldenring_combat_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "eldenring_steve.hpp"
#include "mc/consumables.hpp"
#if ENABLE_CS2_VIEWMODEL
#include "cs_ballistics.hpp" // kCsShotTag: suppress the melee hit sound for CS bullets
#endif

namespace eldenring::combat {

namespace los = eldenring::live::los;
using eldenring::live::setKnockbackStrength;

thread_local ForcedDamage CombatService::forced_damage_{};
thread_local RayResult CombatService::last_ray_{};

CombatService::CombatService() = default;

void CombatService::resetForTesting() {
    callbacks_ = CombatCallbacks{};
    guard_sound_ms_.store(0);
    knockdown_log_ms_.store(0);
    raycast_fn_ = nullptr;
    pdc_hooked_.store(false);
    forced_damage_ = ForcedDamage{};
    last_ray_ = RayResult{};
    // Ranged state (templates, toggles, aim probe) lives in ProjectileService; reset it too so a test
    // that starts from CombatService::Instance() sees a clean projectile state.
    ProjectileService::Instance().resetForTesting();
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
