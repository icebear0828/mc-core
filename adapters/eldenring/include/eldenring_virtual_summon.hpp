#pragma once

#include "eldenring_mobs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eldenring::summon {

struct PlayerPose {
    float pos[3]{0.0f, 0.0f, 0.0f};
    float yaw{0.0f};
};

struct EnemyTarget {
    uintptr_t chr_ins{0};
    float pos[3]{0.0f, 0.0f, 0.0f};
    int hp{100};
    int max_hp{100};
    bool is_hostile{true};
};

struct DamageIntent {
    uintptr_t target_chr_ins{0};
    float damage_amount{6.0f};
    uint64_t source_mob_id{0};
};

struct CreeperExplosionEvent {
    uint64_t source_mob_id{0};
    float pos[3]{0.0f, 0.0f, 0.0f};
    float radius{7.0f};
    float damage{1500.0f};
};

struct VirtualMob {
    uint64_t id{0};
    std::string model_type{"zombie"};
    float pos[3]{0.0f, 0.0f, 0.0f};
    float yaw{0.0f};
    int hp{20};
    int max_hp{20};
    bool is_dying{false};
    float hurt_timer{0.0f};
    float death_timer{0.0f};
    float attack_cooldown{0.0f};
    uintptr_t target_chr_ins{0};
    bool manual_primed{false};
    float scale{1.0f};
    float speed{3.8f};
    bool is_primed{false};
    float fuse_timer{1.5f};
    float fuse_duration{1.5f};
    bool exploded{false};
    float white_flash{0.0f};
};

class VirtualSummonManager {
public:
    static constexpr size_t kMaxVirtualSummons = 16;
    static constexpr float kFollowRadiusMin = 2.5f;
    static constexpr float kFollowRadiusMax = 32.0f;
    static constexpr float kAttackRange = 1.8f;
    static constexpr float kCreeperIgniteDistance = 3.5f;
    static constexpr float kCreeperDefuseDistance = 7.0f;
    static constexpr float kDetectRadius = 16.0f;
    static constexpr float kAttackCooldownSeconds = 1.0f;
    static constexpr float kDeathMaxSeconds = 1.0f;
    static constexpr float kHurtMaxSeconds = 0.5f;

    [[nodiscard]] size_t activeCount() const noexcept {
        return mobs_.size();
    }

    [[nodiscard]] const VirtualMob* findMob(uint64_t id) const noexcept {
        for (const auto& mob : mobs_) {
            if (mob.id == id) return &mob;
        }
        return nullptr;
    }

    [[nodiscard]] VirtualMob* findMobMut(uint64_t id) noexcept {
        for (auto& mob : mobs_) {
            if (mob.id == id) return &mob;
        }
        return nullptr;
    }

    uint64_t spawnMob(std::string_view model_type, const PlayerPose& player, float forward_distance = 2.5f) {
        // Fix #1: Rolling eviction when at maximum capacity so summoning always succeeds
        if (mobs_.size() >= kMaxVirtualSummons) {
            mobs_.erase(mobs_.begin());
        }

        VirtualMob mob;
        mob.id = next_id_++;
        mob.model_type = model_type.empty() ? "zombie" : std::string(model_type);
        mob.hp = 20;
        mob.max_hp = 20;
        mob.is_dying = false;
        mob.hurt_timer = 0.0f;
        mob.death_timer = 0.0f;
        mob.attack_cooldown = 0.0f;
        mob.target_chr_ins = 0;
        mob.manual_primed = false;
        mob.scale = 1.0f;
        mob.speed = 3.8f;
        mob.is_primed = false;
        mob.fuse_timer = 1.5f;
        mob.fuse_duration = 1.5f;
        mob.exploded = false;
        mob.white_flash = 0.0f;

        // Spawn position ahead of player matching heading
        mob.pos[0] = player.pos[0] - std::sin(player.yaw) * forward_distance;
        mob.pos[1] = player.pos[1];
        mob.pos[2] = player.pos[2] - std::cos(player.yaw) * forward_distance;
        mob.yaw = player.yaw;

        mobs_.push_back(mob);
        return mob.id;
    }

    bool igniteCreeper(uint64_t id) {
        VirtualMob* mob = findMobMut(id);
        if (!mob || mob->is_dying || mob->model_type != "creeper") return false;
        mob->is_primed = true;
        mob->manual_primed = true;
        mob->fuse_timer = mob->fuse_duration;
        return true;
    }

    bool applyDamage(uint64_t id, int damage) {
        VirtualMob* mob = findMobMut(id);
        if (!mob || mob->is_dying) return false;

        mob->hp = std::max(0, mob->hp - damage);
        mob->hurt_timer = kHurtMaxSeconds;

        if (mob->hp <= 0) {
            mob->is_dying = true;
            mob->death_timer = 0.0f;
            return false;
        }
        return true;
    }

    void update(float dt, const PlayerPose& player, const std::vector<EnemyTarget>& enemies) {
        for (auto it = mobs_.begin(); it != mobs_.end();) {
            VirtualMob& mob = *it;

            if (mob.is_dying) {
                mob.death_timer += dt;
                if (mob.death_timer >= kDeathMaxSeconds) {
                    it = mobs_.erase(it);
                    continue;
                }
                ++it;
                continue;
            }

            if (mob.hurt_timer > 0.0f) {
                mob.hurt_timer = std::max(0.0f, mob.hurt_timer - dt);
            }
            if (mob.attack_cooldown > 0.0f) {
                mob.attack_cooldown = std::max(0.0f, mob.attack_cooldown - dt);
            }

            const bool is_creeper = (mob.model_type == "creeper");

            // Handle manual ignition (e.g. from Flint and Steel) even without hostile enemies
            if (is_creeper && mob.manual_primed) {
                mob.fuse_timer -= dt;
                const float swell = std::clamp(1.0f - mob.fuse_timer / mob.fuse_duration, 0.0f, 1.0f);
                mob.scale = 1.0f + 0.35f * swell;
                mob.white_flash = std::fmod(mob.fuse_timer, 0.2f) < 0.1f ? 1.0f : 0.0f;

                if (mob.fuse_timer <= 0.0f) {
                    triggerDetonation(mob, it);
                    continue;
                }
            }

            // Find closest active hostile enemy
            uintptr_t best_target = 0;
            float best_dist_sq = kDetectRadius * kDetectRadius;
            float target_pos[3]{0.0f, 0.0f, 0.0f};

            for (const auto& enemy : enemies) {
                if (!enemy.is_hostile || enemy.hp <= 0 || enemy.chr_ins == 0) continue;
                float dx = enemy.pos[0] - mob.pos[0];
                float dz = enemy.pos[2] - mob.pos[2];
                float d_sq = dx * dx + dz * dz;
                if (d_sq < best_dist_sq) {
                    best_dist_sq = d_sq;
                    best_target = enemy.chr_ins;
                    target_pos[0] = enemy.pos[0];
                    target_pos[1] = enemy.pos[1];
                    target_pos[2] = enemy.pos[2];
                }
            }

            mob.target_chr_ins = best_target;

            if (best_target != 0) {
                // Moving toward or attacking target (As designed per item 2: mob pursues target)
                float dx = target_pos[0] - mob.pos[0];
                float dz = target_pos[2] - mob.pos[2];
                float dist = std::sqrt(dx * dx + dz * dz);

                if (dist > 1e-4f) {
                    mob.yaw = std::atan2(-dx, -dz);
                }

                if (is_creeper) {
                    // Autonomous fuse, swelling, and detonation pipeline
                    if (!mob.manual_primed) {
                        if (!mob.is_primed) {
                            if (dist <= kCreeperIgniteDistance) {
                                mob.is_primed = true;
                                mob.fuse_timer = std::max(0.0f, mob.fuse_duration - dt);
                            }
                        } else {
                            // Smooth defuse if enemy moves too far (> 7.0m) and fuse is not in final commitment stage (<= 0.5s)
                            if (dist > kCreeperDefuseDistance && mob.fuse_timer > 0.5f) {
                                mob.fuse_timer = std::min(mob.fuse_duration, mob.fuse_timer + 0.5f * dt);
                                if (mob.fuse_timer >= mob.fuse_duration) {
                                    mob.is_primed = false;
                                }
                            } else {
                                mob.fuse_timer -= dt;
                            }

                            // Detonation!
                            if (mob.fuse_timer <= 0.0f) {
                                triggerDetonation(mob, it);
                                continue;
                            }
                        }

                        if (mob.is_primed) {
                            const float swell = std::clamp(1.0f - mob.fuse_timer / mob.fuse_duration, 0.0f, 1.0f);
                            mob.scale = 1.0f + 0.35f * swell;
                            mob.white_flash = std::fmod(mob.fuse_timer, 0.2f) < 0.1f ? 1.0f : 0.0f;
                        } else {
                            mob.scale = 1.0f;
                            mob.white_flash = 0.0f;
                        }
                    }

                    // Approach target if not yet in detonation contact
                    if (dist > 1.2f) {
                        float step = std::min(dist, mob.speed * dt);
                        float dir_x = dx / dist;
                        float dir_z = dz / dist;
                        mob.pos[0] += dir_x * step;
                        mob.pos[2] += dir_z * step;
                        mob.pos[1] = target_pos[1];
                    }
                } else {
                    // Standard melee attack for zombie/skeleton/iron_golem (Creeper never enters here)
                    if (dist <= kAttackRange) {
                        if (mob.attack_cooldown <= 0.0f) {
                            DamageIntent intent;
                            intent.target_chr_ins = best_target;
                            intent.damage_amount = 6.0f;
                            intent.source_mob_id = mob.id;
                            damage_intents_.push_back(intent);
                            mob.attack_cooldown = kAttackCooldownSeconds;
                        }
                    } else {
                        float step = std::min(dist, mob.speed * dt);
                        float dir_x = dx / dist;
                        float dir_z = dz / dist;
                        mob.pos[0] += dir_x * step;
                        mob.pos[2] += dir_z * step;
                        mob.pos[1] = target_pos[1];
                    }
                }
            } else if (!is_creeper || !mob.manual_primed) {
                // Follow player when idle (if not manually primed creeper)
                float dx = player.pos[0] - mob.pos[0];
                float dz = player.pos[2] - mob.pos[2];
                float dist = std::sqrt(dx * dx + dz * dz);

                // Creepers NEVER teleport snap to player (teleport is only for tamed pets or generic minions)
                if (dist > kFollowRadiusMax && !is_creeper) {
                    mob.pos[0] = player.pos[0] - std::sin(player.yaw) * 2.0f;
                    mob.pos[1] = player.pos[1];
                    mob.pos[2] = player.pos[2] - std::cos(player.yaw) * 2.0f;
                    mob.yaw = player.yaw;
                } else if (dist > kFollowRadiusMin) {
                    float step = std::min(dist - kFollowRadiusMin, mob.speed * dt);
                    float dir_x = dx / dist;
                    float dir_z = dz / dist;
                    mob.pos[0] += dir_x * step;
                    mob.pos[2] += dir_z * step;
                    mob.pos[1] = player.pos[1];
                    mob.yaw = std::atan2(-dx, -dz);
                }
            }

            ++it;
        }
    }

private:
    void triggerDetonation(VirtualMob& mob, std::vector<VirtualMob>::iterator& it) {
        mob.exploded = true;
        CreeperExplosionEvent cev;
        cev.source_mob_id = mob.id;
        cev.pos[0] = mob.pos[0];
        cev.pos[1] = mob.pos[1];
        cev.pos[2] = mob.pos[2];
        cev.radius = 7.0f;
        cev.damage = 1500.0f;
        explosion_events_.push_back(cev);

        const uint64_t exploding_id = mob.id;
        const float exp_pos[3] = {mob.pos[0], mob.pos[1], mob.pos[2]};
        const float exp_radius = cev.radius;
        const float exp_dmg = cev.damage;

        // Creeper is consumed by explosion
        it = mobs_.erase(it);

        // Chain damage to other nearby summons within explosion radius!
        for (auto& other : mobs_) {
            if (other.id == exploding_id || other.is_dying) continue;
            float odx = other.pos[0] - exp_pos[0];
            float ody = other.pos[1] - exp_pos[1];
            float odz = other.pos[2] - exp_pos[2];
            float odist = std::sqrt(odx * odx + ody * ody + odz * odz);
            if (odist <= exp_radius) {
                float falloff = std::clamp(1.0f - odist / exp_radius, 0.0f, 1.0f);
                int dmg = static_cast<int>(exp_dmg * falloff);
                if (dmg > 0) {
                    other.hp = std::max(0, other.hp - dmg);
                    other.hurt_timer = kHurtMaxSeconds;
                    if (other.hp <= 0) {
                        other.is_dying = true;
                        other.death_timer = 0.0f;
                    }
                }
            }
        }
    }

public:
    [[nodiscard]] std::vector<DamageIntent> drainDamageIntents() {
        std::vector<DamageIntent> out = std::move(damage_intents_);
        damage_intents_.clear();
        return out;
    }

    [[nodiscard]] std::vector<CreeperExplosionEvent> drainExplosionEvents() {
        std::vector<CreeperExplosionEvent> out = std::move(explosion_events_);
        explosion_events_.clear();
        return out;
    }

    [[nodiscard]] std::vector<eldenring::mobs::MobSnapshot> generateSnapshots() const {
        std::vector<eldenring::mobs::MobSnapshot> out;
        out.reserve(mobs_.size());

        for (const auto& mob : mobs_) {
            eldenring::mobs::MobSnapshot s;
            s.id = static_cast<uintptr_t>(mob.id);
            s.feet[0] = mob.pos[0];
            s.feet[1] = mob.pos[1];
            s.feet[2] = mob.pos[2];
            s.yaw = mob.yaw;
            s.hp = mob.hp;
            s.max_hp = mob.max_hp;
            s.model_type = mob.model_type;
            s.scale = mob.scale;
            s.white_flash = mob.white_flash;
            out.push_back(s);
        }
        return out;
    }

    void dismissAll() noexcept {
        mobs_.clear();
        damage_intents_.clear();
        explosion_events_.clear();
    }

private:
    uint64_t next_id_{0x10001};
    std::vector<VirtualMob> mobs_;
    std::vector<DamageIntent> damage_intents_;
    std::vector<CreeperExplosionEvent> explosion_events_;
};

} // namespace eldenring::summon
