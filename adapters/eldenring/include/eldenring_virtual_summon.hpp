#pragma once

#include "eldenring_mobs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
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

enum class MobAttackStyle {
    Melee,
    Ranged,
    SuicideExplode,
    Passive
};

struct MobArchetype {
    std::string id{"zombie"};
    int max_hp{20};
    float speed{3.8f};
    MobAttackStyle attack_style{MobAttackStyle::Melee};
    float attack_range{1.8f};
    float attack_min_range{0.0f};
    float attack_cooldown{1.0f};
    float damage{6.0f};
    float scale{1.0f};
    float fuse_duration{1.5f};
    float explosion_radius{7.0f};
    float explosion_damage{1500.0f};
};

struct ProjectileIntent {
    uint64_t source_mob_id{0};
    uintptr_t target_chr_ins{0};
    float origin[3]{0.0f, 0.0f, 0.0f};
    float direction[3]{0.0f, 0.0f, 0.0f};
    float damage{7.0f};
    float speed{22.0f};
};

struct VirtualArrow {
    uint64_t id{0};
    uint64_t source_mob_id{0};
    uintptr_t target_chr_ins{0};
    float pos[3]{0.0f, 0.0f, 0.0f};
    float vel[3]{0.0f, 0.0f, 0.0f};
    float damage{7.0f};
    float age{0.0f};
    float max_age{3.0f};
};

struct ArrowDraw {
    uint64_t id{0};
    float pos[3]{0.0f, 0.0f, 0.0f};
    float dir[3]{0.0f, 0.0f, 1.0f};
};

inline mc::rig::Mat4 arrowWorldMatrix(const ArrowDraw& arrow) {
    const float dx = arrow.dir[0];
    const float dy = arrow.dir[1];
    const float dz = arrow.dir[2];
    const float dist_h = std::sqrt(dx * dx + dz * dz);
    const float yaw = std::atan2(dx, dz);
    const float pitch = std::atan2(dy, std::max(dist_h, 1e-4f));

    const mc::rig::Mat4 r_pitch = mc::rig::rotationAboutAxis({1.0f, 0.0f, 0.0f}, -pitch);
    const mc::rig::Mat4 r_yaw = mc::rig::rotationAboutAxis({0.0f, 1.0f, 0.0f}, yaw);
    const mc::rig::Mat4 t = mc::rig::translation({arrow.pos[0], arrow.pos[1], arrow.pos[2]});
    return r_pitch * r_yaw * t;
}

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
    MobAttackStyle attack_style{MobAttackStyle::Melee};
    float attack_range{1.8f};
    float attack_min_range{0.0f};
    float cooldown_duration{1.0f};
    float damage{6.0f};
    float vel_y{0.0f};
    bool on_ground{true};
    float bow_charge{0.0f};     // [0, 1] for ranged archer aim
    float pitch{0.0f};          // aim elevation pitch in radians
    float swing_progress{0.0f}; // [0, 1] for melee attack swing
};

class VirtualSummonManager {
public:
    using GroundSampler = std::function<std::optional<float>(float x, float current_y, float z)>;
    using LineOfSightSampler = std::function<bool(const float from[3], const float to[3])>;
    using WorldRaycastSampler = std::function<bool(const float from[3], const float to[3], float hit_pos[3])>;

    static constexpr size_t kMaxVirtualSummons = 16;
    static constexpr float kFollowRadiusMin = 2.5f;
    static constexpr float kAttackRange = 1.8f;
    static constexpr float kCreeperIgniteDistance = 3.5f;
    static constexpr float kCreeperDefuseDistance = 7.0f;
    static constexpr float kDetectRadius = 16.0f;
    static constexpr float kAttackCooldownSeconds = 1.0f;
    static constexpr float kDeathMaxSeconds = 1.0f;
    static constexpr float kHurtMaxSeconds = 0.5f;

    VirtualSummonManager() {
        initDefaultArchetypes();
    }

    void setGroundSampler(GroundSampler sampler) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        ground_sampler_ = std::move(sampler);
    }

    void setLineOfSightSampler(LineOfSightSampler sampler) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        los_sampler_ = std::move(sampler);
    }

    void setWorldRaycastSampler(WorldRaycastSampler sampler) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        raycast_sampler_ = std::move(sampler);
    }

    void initDefaultArchetypes() {
        archetypes_.clear();

        // 1. Zombie: standard melee humanoid
        MobArchetype zombie;
        zombie.id = "zombie";
        zombie.max_hp = 20;
        zombie.speed = 3.8f;
        zombie.attack_style = MobAttackStyle::Melee;
        zombie.attack_range = 1.8f;
        zombie.attack_min_range = 0.0f;
        zombie.attack_cooldown = 1.0f;
        zombie.damage = 6.0f;
        zombie.scale = 1.0f;
        archetypes_.push_back(zombie);

        // 2. Creeper: autonomous suicide detonation
        MobArchetype creeper;
        creeper.id = "creeper";
        creeper.max_hp = 20;
        creeper.speed = 3.8f;
        creeper.attack_style = MobAttackStyle::SuicideExplode;
        creeper.attack_range = 3.5f; // ignite distance
        creeper.attack_min_range = 0.0f;
        creeper.attack_cooldown = 0.0f;
        creeper.damage = 0.0f; // damages via explosion
        creeper.fuse_duration = 1.5f;
        creeper.explosion_radius = 7.0f;
        creeper.explosion_damage = 1500.0f;
        creeper.scale = 1.0f;
        archetypes_.push_back(creeper);

        // 3. Skeleton: ranged archer with kiting
        MobArchetype skeleton;
        skeleton.id = "skeleton";
        skeleton.max_hp = 20;
        skeleton.speed = 4.0f;
        skeleton.attack_style = MobAttackStyle::Ranged;
        skeleton.attack_range = 14.0f;
        skeleton.attack_min_range = 4.0f;
        skeleton.attack_cooldown = 1.5f;
        skeleton.damage = 7.0f;
        skeleton.scale = 1.0f;
        archetypes_.push_back(skeleton);

        // 4. Iron Golem: tank melee powerhouse
        MobArchetype iron_golem;
        iron_golem.id = "iron_golem";
        iron_golem.max_hp = 100;
        iron_golem.speed = 3.2f;
        iron_golem.attack_style = MobAttackStyle::Melee;
        iron_golem.attack_range = 2.5f;
        iron_golem.attack_min_range = 0.0f;
        iron_golem.attack_cooldown = 1.2f;
        iron_golem.damage = 21.0f;
        iron_golem.scale = 1.2f;
        archetypes_.push_back(iron_golem);

        // 5. Wolf: fast melee companion
        MobArchetype wolf;
        wolf.id = "wolf";
        wolf.max_hp = 20;
        wolf.speed = 4.5f;
        wolf.attack_style = MobAttackStyle::Melee;
        wolf.attack_range = 1.6f;
        wolf.attack_min_range = 0.0f;
        wolf.attack_cooldown = 0.8f;
        wolf.damage = 5.0f;
        wolf.scale = 0.8f;
        archetypes_.push_back(wolf);
    }

    void registerArchetype(const MobArchetype& archetype) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (auto& a : archetypes_) {
            if (a.id == archetype.id) {
                a = archetype;
                return;
            }
        }
        archetypes_.push_back(archetype);
    }

    [[nodiscard]] const MobArchetype* findArchetype(std::string_view id) const noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto& a : archetypes_) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }

    [[nodiscard]] size_t activeCount() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return mobs_.size();
    }

    [[nodiscard]] const VirtualMob* findMob(uint64_t id) const noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto& mob : mobs_) {
            if (mob.id == id) return &mob;
        }
        return nullptr;
    }

    [[nodiscard]] VirtualMob* findMobMut(uint64_t id) noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (auto& mob : mobs_) {
            if (mob.id == id) return &mob;
        }
        return nullptr;
    }

    uint64_t spawnMob(std::string_view model_type, const PlayerPose& player, float forward_distance = 2.5f) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        // Fix #1: Rolling eviction when at maximum capacity so summoning always succeeds
        if (mobs_.size() >= kMaxVirtualSummons) {
            mobs_.erase(mobs_.begin());
        }

        std::string target_type = model_type.empty() ? "zombie" : std::string(model_type);
        const MobArchetype* arch = findArchetype(target_type);
        if (!arch) arch = findArchetype("zombie");

        VirtualMob mob;
        mob.id = next_id_++;
        mob.model_type = target_type;
        mob.hp = arch ? arch->max_hp : 20;
        mob.max_hp = arch ? arch->max_hp : 20;
        mob.is_dying = false;
        mob.hurt_timer = 0.0f;
        mob.death_timer = 0.0f;
        mob.attack_cooldown = 0.0f;
        mob.target_chr_ins = 0;
        mob.manual_primed = false;
        mob.scale = arch ? arch->scale : 1.0f;
        mob.speed = arch ? arch->speed : 3.8f;
        mob.is_primed = false;
        mob.fuse_duration = arch ? arch->fuse_duration : 1.5f;
        mob.fuse_timer = mob.fuse_duration;
        mob.exploded = false;
        mob.white_flash = 0.0f;
        mob.attack_style = arch ? arch->attack_style : MobAttackStyle::Melee;
        mob.attack_range = arch ? arch->attack_range : 1.8f;
        mob.attack_min_range = arch ? arch->attack_min_range : 0.0f;
        mob.cooldown_duration = arch ? arch->attack_cooldown : 1.0f;
        mob.damage = arch ? arch->damage : 6.0f;

        // Spawn position ahead of player matching heading (+sin(yaw), +cos(yaw))
        mob.pos[0] = player.pos[0] + std::sin(player.yaw) * forward_distance;
        mob.pos[1] = player.pos[1];
        mob.pos[2] = player.pos[2] + std::cos(player.yaw) * forward_distance;
        mob.yaw = player.yaw;

        mobs_.push_back(mob);
        return mob.id;
    }

    bool igniteCreeper(uint64_t id) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        VirtualMob* mob = findMobMut(id);
        if (!mob || mob->is_dying || mob->attack_style != MobAttackStyle::SuicideExplode) return false;
        mob->is_primed = true;
        mob->manual_primed = true;
        mob->fuse_timer = mob->fuse_duration;
        return true;
    }

    bool applyDamage(uint64_t id, int damage) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        // 1. Update existing flying arrows with continuous swept collision
        for (auto it = active_arrows_.begin(); it != active_arrows_.end();) {
            it->age += dt;
            const float prev_x = it->pos[0];
            const float prev_y = it->pos[1];
            const float prev_z = it->pos[2];

            it->pos[0] += it->vel[0] * dt;
            it->pos[1] += it->vel[1] * dt;
            it->pos[2] += it->vel[2] * dt;
            it->vel[1] -= 9.8f * dt; // gravity arc

            const float next_x = it->pos[0];
            const float next_y = it->pos[1];
            const float next_z = it->pos[2];

            const float seg_x = next_x - prev_x;
            const float seg_y = next_y - prev_y;
            const float seg_z = next_z - prev_z;
            const float seg_len_sq = seg_x * seg_x + seg_y * seg_y + seg_z * seg_z;

            // 1a. Check physical world/terrain geometry collision
            const float from_pt[3] = {prev_x, prev_y, prev_z};
            const float to_pt[3] = {next_x, next_y, next_z};
            float hit_pos[3] = {};
            if (raycast_sampler_ && raycast_sampler_(from_pt, to_pt, hit_pos)) {
                it = active_arrows_.erase(it);
                continue;
            }

            bool hit = false;
            for (const auto& enemy : enemies) {
                if (enemy.hp <= 0) continue;
                const float to_enemy_x = enemy.pos[0] - prev_x;
                const float to_enemy_y = (enemy.pos[1] + 1.0f) - prev_y;
                const float to_enemy_z = enemy.pos[2] - prev_z;

                float t = 0.0f;
                if (seg_len_sq > 1e-4f) {
                    t = std::clamp((to_enemy_x * seg_x + to_enemy_y * seg_y + to_enemy_z * seg_z) / seg_len_sq, 0.0f, 1.0f);
                }
                const float close_x = prev_x + seg_x * t;
                const float close_y = prev_y + seg_y * t;
                const float close_z = prev_z + seg_z * t;

                const float dx = close_x - enemy.pos[0];
                const float dy = close_y - (enemy.pos[1] + 1.0f);
                const float dz = close_z - enemy.pos[2];
                if (dx * dx + dy * dy + dz * dz <= 1.5f * 1.5f) {
                    DamageIntent intent;
                    intent.target_chr_ins = enemy.chr_ins;
                    intent.damage_amount = it->damage;
                    intent.source_mob_id = it->source_mob_id;
                    damage_intents_.push_back(intent);
                    hit = true;
                    break;
                }
            }

            if (hit || it->age >= it->max_age) {
                it = active_arrows_.erase(it);
            } else {
                ++it;
            }
        }

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
            if (mob.swing_progress > 0.0f) {
                mob.swing_progress += dt / 0.35f;
                if (mob.swing_progress >= 1.0f) {
                    mob.swing_progress = 0.0f;
                }
            }

            const bool is_creeper = (mob.attack_style == MobAttackStyle::SuicideExplode);

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

            // Find closest active hostile enemy with Line of Sight
            uintptr_t best_target = 0;
            float best_dist_sq = kDetectRadius * kDetectRadius;
            float target_pos[3]{0.0f, 0.0f, 0.0f};

            for (const auto& enemy : enemies) {
                if (!enemy.is_hostile || enemy.hp <= 0 || enemy.chr_ins == 0) continue;
                float dx = enemy.pos[0] - mob.pos[0];
                float dy = enemy.pos[1] - mob.pos[1];
                float dz = enemy.pos[2] - mob.pos[2];
                float d_sq = dx * dx + dy * dy + dz * dz;
                if (d_sq < best_dist_sq) {
                    if (los_sampler_) {
                        const float eye_pos[3] = {mob.pos[0], mob.pos[1] + 1.4f, mob.pos[2]};
                        const float enemy_mid[3] = {enemy.pos[0], enemy.pos[1] + 1.0f, enemy.pos[2]};
                        if (!los_sampler_(eye_pos, enemy_mid)) {
                            continue; // Blocked by obstacle / wall / terrain
                        }
                    }
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
                float dy = (target_pos[1] + 1.0f) - (mob.pos[1] + 1.4f);
                float dz = target_pos[2] - mob.pos[2];
                float dist = std::sqrt(dx * dx + dz * dz);
                float dist_3d = std::sqrt(dx * dx + dy * dy + dz * dz);

                if (dist > 1e-4f) {
                    mob.yaw = std::atan2(dx, dz);
                    mob.pitch = std::atan2(dy, dist);
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
                    }
                } else if (mob.attack_style == MobAttackStyle::Ranged) {
                    // Ranged attack (e.g. skeleton bow, snow golem snowball)
                    if (dist > mob.attack_range) {
                        float step = std::min(dist - mob.attack_range * 0.9f, mob.speed * dt);
                        float dir_x = dx / dist;
                        float dir_z = dz / dist;
                        mob.pos[0] += dir_x * step;
                        mob.pos[2] += dir_z * step;
                    } else if (mob.attack_min_range > 0.0f && dist < mob.attack_min_range) {
                        // Enemy rushed too close: kite backwards away from target!
                        float back_step = std::min(mob.attack_min_range - dist, mob.speed * 0.75f * dt);
                        float dir_x = dx / dist;
                        float dir_z = dz / dist;
                        mob.pos[0] -= dir_x * back_step;
                        mob.pos[2] -= dir_z * back_step;
                    }

                    // Fire ranged attack when within attack range and cooldown ready
                    if (dist <= mob.attack_range) {
                        if (mob.attack_cooldown <= 0.0f) {
                            mob.bow_charge = std::min(1.0f, mob.bow_charge + dt / 0.8f);
                            if (mob.bow_charge >= 1.0f) {
                                ProjectileIntent proj;
                                proj.source_mob_id = mob.id;
                                proj.target_chr_ins = best_target;
                                proj.origin[0] = mob.pos[0];
                                proj.origin[1] = mob.pos[1] + 1.4f;
                                proj.origin[2] = mob.pos[2];
                                const float aim_len = std::max(dist_3d, 1e-4f);
                                proj.direction[0] = dx / aim_len;
                                proj.direction[1] = dy / aim_len;
                                proj.direction[2] = dz / aim_len;
                                proj.damage = mob.damage;
                                proj.speed = 22.0f;
                                projectile_intents_.push_back(proj);

                                VirtualArrow va;
                                va.id = next_arrow_id_++;
                                va.source_mob_id = mob.id;
                                va.target_chr_ins = best_target;
                                va.pos[0] = proj.origin[0];
                                va.pos[1] = proj.origin[1];
                                va.pos[2] = proj.origin[2];
                                va.vel[0] = proj.direction[0] * proj.speed;
                                va.vel[1] = proj.direction[1] * proj.speed + 0.8f;
                                va.vel[2] = proj.direction[2] * proj.speed;
                                va.damage = mob.damage;
                                active_arrows_.push_back(va);

                                mob.bow_charge = 0.0f;
                                mob.attack_cooldown = mob.cooldown_duration;
                            }
                        } else {
                            mob.bow_charge = 0.0f;
                        }
                    } else {
                        mob.bow_charge = std::max(0.0f, mob.bow_charge - dt * 2.0f);
                    }
                }
 else if (mob.attack_style == MobAttackStyle::Melee) {
                    // Standard melee attack for zombie/iron_golem/wolf
                    if (dist <= mob.attack_range) {
                        if (mob.attack_cooldown <= 0.0f) {
                            DamageIntent intent;
                            intent.target_chr_ins = best_target;
                            intent.damage_amount = mob.damage;
                            intent.source_mob_id = mob.id;
                            damage_intents_.push_back(intent);
                            mob.attack_cooldown = mob.cooldown_duration;
                            mob.swing_progress = 0.01f;
                        }
                    } else {
                        float step = std::min(dist, mob.speed * dt);
                        float dir_x = dx / dist;
                        float dir_z = dz / dist;
                        mob.pos[0] += dir_x * step;
                        mob.pos[2] += dir_z * step;
                    }
                }
            } else if (!is_creeper || !mob.manual_primed) {
                mob.pitch = 0.0f;
                mob.bow_charge = std::max(0.0f, mob.bow_charge - dt * 2.0f);
                // Follow player when idle (if not manually primed creeper)
                // In Minecraft / natural companion behaviour: summons smoothly follow player on XZ ground.
                float dx = player.pos[0] - mob.pos[0];
                float dz = player.pos[2] - mob.pos[2];
                float dist = std::sqrt(dx * dx + dz * dz);

                if (dist > kFollowRadiusMin) {
                    float step = std::min(dist - kFollowRadiusMin, mob.speed * dt);
                    float dir_x = dx / dist;
                    float dir_z = dz / dist;
                    mob.pos[0] += dir_x * step;
                    mob.pos[2] += dir_z * step;
                    mob.yaw = std::atan2(dx, dz);
                }
            }

            // Vertical physics & ground snapping pass
            if (ground_sampler_) {
                auto gy = ground_sampler_(mob.pos[0], mob.pos[1], mob.pos[2]);
                if (gy.has_value()) {
                    const float floor_y = *gy;
                    if (mob.pos[1] < floor_y) {
                        // Step up if small elevation (<= 1.1m)
                        if (floor_y - mob.pos[1] <= 1.1f) {
                            mob.pos[1] = floor_y;
                            mob.vel_y = 0.0f;
                            mob.on_ground = true;
                        } else {
                            mob.on_ground = true;
                        }
                    } else if (mob.pos[1] > floor_y) {
                        // Fall under gravity
                        mob.vel_y -= 18.0f * dt;
                        if (mob.vel_y < -25.0f) mob.vel_y = -25.0f;
                        mob.pos[1] += mob.vel_y * dt;
                        if (mob.pos[1] <= floor_y) {
                            mob.pos[1] = floor_y;
                            mob.vel_y = 0.0f;
                            mob.on_ground = true;
                        } else {
                            mob.on_ground = false;
                        }
                    } else {
                        mob.vel_y = 0.0f;
                        mob.on_ground = true;
                    }
                }
            } else {
                // Without ground sampler (unit tests without terrain / offline simulation):
                // If enemy target is at a climbable step (<= 1.1m), match target Y
                if (best_target != 0) {
                    if (std::fabs(target_pos[1] - mob.pos[1]) <= 1.1f) {
                        mob.pos[1] = target_pos[1];
                    }
                } else {
                    float dx_idle = player.pos[0] - mob.pos[0];
                    float dz_idle = player.pos[2] - mob.pos[2];
                    float dist_idle = std::sqrt(dx_idle * dx_idle + dz_idle * dz_idle);
                    if (dist_idle > kFollowRadiusMin && std::fabs(player.pos[1] - mob.pos[1]) <= 1.1f) {
                        // Small ground slope: step to player Y
                        mob.pos[1] = player.pos[1];
                    }
                }
                // If player is flying high (|player.pos[1] - mob.pos[1]| > 1.2m), NEVER drag mob up!
            }

            ++it;
        }

        // Anti-clipping Pushout Pass: separate overlapping summons from each other
        constexpr float kMinMobSeparation = 0.6f; // diameter of typical mob (2 * 0.3m)
        for (size_t i = 0; i < mobs_.size(); ++i) {
            if (mobs_[i].is_dying) continue;
            for (size_t j = i + 1; j < mobs_.size(); ++j) {
                if (mobs_[j].is_dying) continue;
                float pdx = mobs_[j].pos[0] - mobs_[i].pos[0];
                float pdz = mobs_[j].pos[2] - mobs_[i].pos[2];
                float pdist_sq = pdx * pdx + pdz * pdz;
                if (pdist_sq < kMinMobSeparation * kMinMobSeparation) {
                    float pdist = std::sqrt(pdist_sq);
                    float push = (kMinMobSeparation - pdist) * 0.5f;
                    float nx = 1.0f, nz = 0.0f;
                    if (pdist > 1e-4f) {
                        nx = pdx / pdist;
                        nz = pdz / pdist;
                    }
                    mobs_[i].pos[0] -= nx * push;
                    mobs_[i].pos[2] -= nz * push;
                    mobs_[j].pos[0] += nx * push;
                    mobs_[j].pos[2] += nz * push;
                }
            }
        }

        // Anti-clipping Target Boundary Pass: ensure mobs do not penetrate inside target enemies
        constexpr float kMinTargetSeparation = 0.7f; // mob radius ~0.3m + target radius ~0.35m = ~0.65m
        for (auto& mob : mobs_) {
            if (mob.is_dying || mob.target_chr_ins == 0) continue;
            for (const auto& enemy : enemies) {
                if (enemy.chr_ins != mob.target_chr_ins) continue;
                float tdx = mob.pos[0] - enemy.pos[0];
                float tdz = mob.pos[2] - enemy.pos[2];
                float tdist_sq = tdx * tdx + tdz * tdz;
                if (tdist_sq < kMinTargetSeparation * kMinTargetSeparation) {
                    float tdist = std::sqrt(tdist_sq);
                    float push = kMinTargetSeparation - tdist;
                    float nx = 0.0f, nz = -1.0f;
                    if (tdist > 1e-4f) {
                        nx = tdx / tdist;
                        nz = tdz / tdist;
                    }
                    mob.pos[0] += nx * push;
                    mob.pos[2] += nz * push;
                }
                break;
            }
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
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<DamageIntent> out = std::move(damage_intents_);
        damage_intents_.clear();
        return out;
    }

    [[nodiscard]] std::vector<ProjectileIntent> drainProjectileIntents() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<ProjectileIntent> out = std::move(projectile_intents_);
        projectile_intents_.clear();
        return out;
    }

    [[nodiscard]] std::vector<CreeperExplosionEvent> drainExplosionEvents() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<CreeperExplosionEvent> out = std::move(explosion_events_);
        explosion_events_.clear();
        return out;
    }

    [[nodiscard]] std::vector<eldenring::mobs::MobSnapshot> generateSnapshots() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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
            s.bow_charge = mob.bow_charge;
            s.pitch = mob.pitch;
            s.swing_progress = mob.swing_progress;
            out.push_back(s);
        }
        return out;
    }

    [[nodiscard]] std::vector<ArrowDraw> getActiveArrows() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<ArrowDraw> out;
        out.reserve(active_arrows_.size());
        for (const auto& a : active_arrows_) {
            ArrowDraw d;
            d.id = a.id;
            d.pos[0] = a.pos[0];
            d.pos[1] = a.pos[1];
            d.pos[2] = a.pos[2];
            float len = std::sqrt(a.vel[0] * a.vel[0] + a.vel[1] * a.vel[1] + a.vel[2] * a.vel[2]);
            if (len > 1e-4f) {
                d.dir[0] = a.vel[0] / len;
                d.dir[1] = a.vel[1] / len;
                d.dir[2] = a.vel[2] / len;
            } else {
                d.dir[0] = 0.0f; d.dir[1] = 0.0f; d.dir[2] = 1.0f;
            }
            out.push_back(d);
        }
        return out;
    }

    void dismissAll() noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        mobs_.clear();
        damage_intents_.clear();
        projectile_intents_.clear();
        explosion_events_.clear();
        active_arrows_.clear();
    }

    void shift(float dx, float dy, float dz) {
        if (dx == 0.0f && dy == 0.0f && dz == 0.0f) return;
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (auto& mob : mobs_) {
            mob.pos[0] += dx;
            mob.pos[1] += dy;
            mob.pos[2] += dz;
        }
        for (auto& proj : projectile_intents_) {
            proj.origin[0] += dx;
            proj.origin[1] += dy;
            proj.origin[2] += dz;
        }
        for (auto& a : active_arrows_) {
            a.pos[0] += dx;
            a.pos[1] += dy;
            a.pos[2] += dz;
        }
    }

private:
    mutable std::recursive_mutex mutex_;
    uint64_t next_id_{0x10001};
    uint64_t next_arrow_id_{1};
    std::vector<MobArchetype> archetypes_;
    std::vector<VirtualMob> mobs_;
    std::vector<DamageIntent> damage_intents_;
    std::vector<ProjectileIntent> projectile_intents_;
    std::vector<CreeperExplosionEvent> explosion_events_;
    std::vector<VirtualArrow> active_arrows_;
    GroundSampler ground_sampler_;
    LineOfSightSampler los_sampler_;
    WorldRaycastSampler raycast_sampler_;
};

} // namespace eldenring::summon
