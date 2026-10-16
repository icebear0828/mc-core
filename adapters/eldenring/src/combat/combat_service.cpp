#include "eldenring_combat_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "eldenring_steve.hpp"
#include "mc/consumables.hpp"

namespace eldenring::combat {

CombatService::CombatService() = default;

void CombatService::resetForTesting() {
    callbacks_ = CombatCallbacks{};
    guard_sound_ms_.store(0);
    knockdown_log_ms_.store(0);
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
    if (ctx.mode().los_enabled.load(std::memory_order_relaxed) && callbacks_.has_line_of_sight) {
        c.line_of_sight = [&](const float from[3], const float to[3]) {
            return callbacks_.has_line_of_sight(from, to);
        };
    }
    c.max_per_drain = 1;
    c.expire_after_ticks = 60 * 5;

    c.invoke = [&](uintptr_t dmg, uintptr_t attacker, void* ctx_ptr) {
        if (callbacks_.call_vfunc7) {
            callbacks_.call_vfunc7(dmg, attacker, ctx_ptr);
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
            if (callbacks_.play_sound) {
                callbacks_.play_sound((r.request.tag & 1u) ? "entity.player.attack.crit"
                                      : (r.request.tag & 2u) ? "entity.player.attack.sweep"
                                      : (r.request.tag & 4u) ? "entity.player.attack.strong"
                                                             : "entity.player.attack.weak", 1.0f);
            }
            Vitals victim{};
            if (callbacks_.read_vitals && callbacks_.read_vitals(r.request.victim_chr, victim) && victim.hp <= 0) {
                feedback.onKill();
                const int earned_xp = xpForKill(victim.max_hp);
                const int ups = xp.add(earned_xp);
                if (callbacks_.play_sound) {
                    callbacks_.play_sound(ups > 0 ? "entity.player.levelup" : "entity.experience_orb.pickup", 0.7f);
                }
            }
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

} // namespace eldenring::combat
