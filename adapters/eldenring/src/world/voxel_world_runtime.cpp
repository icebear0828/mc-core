#include "eldenring_world_runtime.hpp"

#include "eldenring_sneak.hpp"

#include <cstdarg>
#include <cmath>
#include <cstring>

namespace eldenring::world {

VoxelWorldRuntime::VoxelWorldRuntime() {
    is_key_down_ = [](int vk) -> bool {
#ifdef _WIN32
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
        (void)vk;
        return false;
#endif
    };
}

void VoxelWorldRuntime::log(const char* fmt, ...) {
    if (!callbacks_.log) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    callbacks_.log(std::string(buf));
}

bool VoxelWorldRuntime::isKeyDown(int vk) const {
    if (is_key_down_) {
        return is_key_down_(vk);
    }
#ifdef _WIN32
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    (void)vk;
    return false;
#endif
}

void VoxelWorldRuntime::resetForTesting() {
    have_prev_player_ = false;
    std::memset(prev_player_feet_, 0, sizeof(prev_player_feet_));
    player_pushes_ = 0;
    anchored_ = false;
    std::memset(anchor_, 0, sizeof(anchor_));

    enemy_pushes_ = 0;
    prev_enemy_feet_.clear();
    delta_time_override_ = 0.f;
#ifdef _WIN32
    last_block_qpc_.QuadPart = 0;
#endif

    callbacks_ = WorldRuntimeCallbacks{};
    is_key_down_ = [](int vk) -> bool {
#ifdef _WIN32
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
        (void)vk;
        return false;
#endif
    };

    std::lock_guard<std::mutex> g(tnt_intent_mutex_);
    (void)tnt_damage_intents_.drain();
}

void VoxelWorldRuntime::enqueueTntIntent(const float center[3], float radius, float max_damage) {
    std::lock_guard<std::mutex> g(tnt_intent_mutex_);
    tnt_damage_intents_.enqueue(center, radius, max_damage);
}

size_t VoxelWorldRuntime::pendingTntIntentCount() const {
    std::lock_guard<std::mutex> g(tnt_intent_mutex_);
    return tnt_damage_intents_.pendingCount();
}

void VoxelWorldRuntime::stepShadowGround(RuntimeContext& ctx) {
    if (!ctx.mode().shadow_enabled.load(std::memory_order_relaxed) ||
        !ctx.mode().steve_enabled.load(std::memory_order_relaxed) ||
        !ctx.mode().mc_mode.load(std::memory_order_relaxed)) {
        ctx.player().ground_valid.store(false, std::memory_order_relaxed);
        return;
    }

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    float feet[3] = {};
    if (player == 0 || !callbacks_.read_physics_pos || !callbacks_.read_physics_pos(player, feet)) {
        ctx.player().ground_valid.store(false, std::memory_order_relaxed);
        return;
    }

    const RayCastFn cast = callbacks_.cast_ray ? RayCastFn(callbacks_.cast_ray) : RayCastFn{};
    const auto block_top = [&](float x, float feet_y, float z, float max_drop) -> std::optional<float> {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        if (ctx.world().block_grid.count() == 0) return std::nullopt;
        const int top_cell = static_cast<int>(std::floor(feet_y + 0.06f));
        const int low_cell = static_cast<int>(std::floor(feet_y - max_drop));
        for (int y = top_cell; y >= low_cell; --y) {
            if (ctx.world().block_grid.get(blocks::cellOf(x, static_cast<float>(y), z)) != mc::BlockId::Air) {
                return static_cast<float>(y + 1);
            }
        }
        return std::nullopt;
    };

    const std::optional<float> ground = eldenring::shadow::groundBelow(cast, block_top, feet);
    ctx.player().ground_valid.store(ground.has_value(), std::memory_order_relaxed);
    if (ground) {
        ctx.player().ground_y.store(*ground, std::memory_order_relaxed);
    }
}

void VoxelWorldRuntime::stepSneakEyeHeight(float dt, RuntimeContext& ctx) {
    if (!ctx.mode().mc_mode.load(std::memory_order_relaxed)) {
        return;
    }

    // Sample the sneak key here instead of relying on stepPlayerBlockCollision(), which early-returns when the
    // player/physics cannot be read. This keeps the signal fresh on every camera step, blocks or no blocks.
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    const bool sneak_key_down = isKeyDown(ctx.mode().sneak_vk.load(std::memory_order_relaxed)) ||
                                isKeyDown('X');
    const bool sneaking = fg && sneak_key_down;
    ctx.mode().sneak_active.store(sneaking, std::memory_order_relaxed);

    const bool first_person = ctx.mode().first_person.load(std::memory_order_relaxed);
    const float current = ctx.mode().eye_height.load(std::memory_order_relaxed);
    ctx.mode().eye_height.store(sneak::stepSneakEyeHeight(current, sneaking, first_person, dt),
                                std::memory_order_relaxed);
}

void VoxelWorldRuntime::stepPlayerBlockCollision(RuntimeContext& ctx) {
    if (!ctx.mode().mc_mode.load(std::memory_order_relaxed)) {
        have_prev_player_ = false;
        ctx.mode().block_supported.store(false, std::memory_order_relaxed);
        return;
    }

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    float feet[3] = {};
    if (player == 0 || !callbacks_.read_physics_pos || !callbacks_.read_physics_pos(player, feet)) {
        have_prev_player_ = false;
        ctx.mode().block_supported.store(false, std::memory_order_relaxed);
        return;
    }

    float dx = 0.f, dy = 0.f, dz = 0.f;
    if (have_prev_player_) {
        dx = feet[0] - prev_player_feet_[0];
        dy = feet[1] - prev_player_feet_[1];
        dz = feet[2] - prev_player_feet_[2];

        // Check for Dantelion / Havok Floating Origin Rebase (shifts in whole multiples of 8m)
        float rebase_shift[3] = {0.0f, 0.0f, 0.0f};
        bool is_rebase = false;
        const float diffs[3] = {dx, dy, dz};
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(diffs[i]) >= 4.0f) {
                const float step = std::round(diffs[i] / 8.0f) * 8.0f;
                if (std::fabs(step) >= 8.0f && std::fabs(step) <= 64.0f && std::fabs(diffs[i] - step) < 2.5f) {
                    rebase_shift[i] = step;
                    is_rebase = true;
                }
            }
        }

        if (is_rebase) {
            log("world: floating origin rebase detected: shift=(%.2f, %.2f, %.2f)",
                rebase_shift[0], rebase_shift[1], rebase_shift[2]);
            prev_player_feet_[0] += rebase_shift[0];
            prev_player_feet_[1] += rebase_shift[1];
            prev_player_feet_[2] += rebase_shift[2];
            if (anchored_) {
                anchor_[0] += rebase_shift[0];
                anchor_[1] += rebase_shift[2];
            }
            {
                std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
                ctx.world().block_grid.shift(static_cast<int>(rebase_shift[0]),
                                             static_cast<int>(rebase_shift[1]),
                                             static_cast<int>(rebase_shift[2]));
            }
            if (callbacks_.on_origin_rebase) {
                callbacks_.on_origin_rebase(rebase_shift[0], rebase_shift[1], rebase_shift[2]);
            }
            dx = feet[0] - prev_player_feet_[0];
            dy = feet[1] - prev_player_feet_[1];
            dz = feet[2] - prev_player_feet_[2];
        }
    }
    const bool jumped = std::sqrt(dx * dx + dy * dy + dz * dz) > 20.f;

    bool has_blocks = false;
    if (ctx.mode().block_collision.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        has_blocks = (ctx.world().block_grid.count() > 0);
    }

    blocks::Resolve r{};
    if (has_blocks) {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        r = have_prev_player_ && !jumped ?
            blocks::resolvePlayerSwept(ctx.world().block_grid, prev_player_feet_, feet) :
            blocks::resolvePlayer(ctx.world().block_grid, feet);
    }

    float final_feet[3] = {r.moved ? r.feet[0] : feet[0], r.moved ? r.feet[1] : feet[1], r.moved ? r.feet[2] : feet[2]};
    bool supported = false;
    if (has_blocks) {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        supported = blocks::supportedByBlock(ctx.world().block_grid, final_feet);
    }
    ctx.mode().block_supported.store(supported, std::memory_order_relaxed);

    const uintptr_t module = callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module != 0 && r.moved && callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, r.feet, sizeof(r.feet));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, r.feet, sizeof(r.feet));
        const uint8_t one = 1;
        callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, sizeof(one));
    }

    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    const bool sneak_key_down = isKeyDown(ctx.mode().sneak_vk.load(std::memory_order_relaxed)) ||
                                isKeyDown('X');
    const bool sneaking = fg && sneak_key_down;
    ctx.mode().sneak_active.store(sneaking, std::memory_order_relaxed);
    // The first-person eye-height interpolation is not driven here: it lives in stepSneakEyeHeight(), which runs
    // from the general per-frame camera update so it does not depend on this function (or on placed blocks).

    bool pinned = false;
    const bool key_w = fg && isKeyDown('W');
    const bool key_a = fg && isKeyDown('A');
    const bool key_s = fg && isKeyDown('S');
    const bool key_d = fg && isKeyDown('D');
    const bool key_space = fg && isKeyDown(VK_SPACE);
    const bool should_anchor = ctx.mode().block_anchor.load(std::memory_order_relaxed) &&
                               blocks::shouldAnchorBlockPosition(supported, key_w, key_a, key_s, key_d, key_space);

    if (should_anchor) {
        if (!anchored_) {
            anchor_[0] = final_feet[0];
            anchor_[1] = final_feet[2];
            anchored_ = true;
        }
        final_feet[0] = anchor_[0];
        final_feet[2] = anchor_[1];
        pinned = true;
    } else {
        anchored_ = false;
    }

    bool was_supported = false;
    if (have_prev_player_ && has_blocks) {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        was_supported = blocks::supportedByBlock(ctx.world().block_grid, prev_player_feet_);
    }

    float block_dt = 0.016f;
    if (delta_time_override_ > 0.f) {
        block_dt = delta_time_override_;
    } else {
#ifdef _WIN32
        LARGE_INTEGER qpc, freq;
        QueryPerformanceCounter(&qpc);
        QueryPerformanceFrequency(&freq);
        if (last_block_qpc_.QuadPart != 0 && freq.QuadPart > 0) {
            block_dt = static_cast<float>(qpc.QuadPart - last_block_qpc_.QuadPart) / static_cast<float>(freq.QuadPart);
            block_dt = std::clamp(block_dt, 0.001f, 0.1f);
        }
        last_block_qpc_ = qpc;
#endif
    }

    bool sneak_clamped = false;
    if (sneaking && (supported || was_supported) && have_prev_player_ && !jumped && !pinned) {
        const float desired_dx = final_feet[0] - prev_player_feet_[0];
        const float desired_dz = final_feet[2] - prev_player_feet_[2];
        blocks::SneakMoveResult sneak_res;
        {
            std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
            sneak_res = blocks::clampSneakEdgeMovement(ctx.world().block_grid, prev_player_feet_, desired_dx, desired_dz);
        }
        if (sneak_res.clamped_x || sneak_res.clamped_z) {
            final_feet[0] = prev_player_feet_[0] + sneak_res.dx;
            final_feet[2] = prev_player_feet_[2] + sneak_res.dz;
            supported = true;
            sneak_clamped = true;
            ctx.mode().block_supported.store(true, std::memory_order_relaxed);
        }
    }

    // Direct position drive on blocks:
    // When the player is standing on top of placed blocks and moving (WASD keys held),
    // directly advance player coordinates by block_walk_speed (4.317 m/s) clamped by block collision.
    // This restores continuous, responsive locomotion on custom voxel surfaces.
    bool direct_moved = false;
    if (supported && fg && !jumped) {
        CameraPose cam{};
        if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
            const float fwd_key = (isKeyDown('W') ? 1.f : 0.f) - (isKeyDown('S') ? 1.f : 0.f);
            const float right_key = (isKeyDown('D') ? 1.f : 0.f) - (isKeyDown('A') ? 1.f : 0.f);
            const float len_sq = fwd_key * fwd_key + right_key * right_key;
            if (len_sq > 1e-4f) {
                const float inv_l = 1.0f / std::sqrt(len_sq);
                const float nfwd = fwd_key * inv_l;
                const float nright = right_key * inv_l;
                float dir[3] = {
                    cam.forward[0] * nfwd + cam.right[0] * nright,
                    0.0f,
                    cam.forward[2] * nfwd + cam.right[2] * nright
                };
                const float dlen = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
                if (dlen > 1e-4f) {
                    dir[0] /= dlen;
                    dir[2] /= dlen;
                }
                const bool blocking = ctx.mode().shield_blocking.load(std::memory_order_relaxed) || ctx.combat().shield.isBlocking();
                const float target_speed = eldenring::shield::effectiveMovementSpeed(
                    ctx.mode().block_walk_speed.load(std::memory_order_relaxed),
                    ctx.mode().sprint_speed.load(std::memory_order_relaxed),
                    ctx.mode().sneak_speed.load(std::memory_order_relaxed),
                    sneaking,
                    false,
                    blocking
                );
                const float dir_2d[2] = {dir[0], dir[2]};
                float stepped_feet[3] = {};
                if (blocks::stepBlockMovement(final_feet, dir_2d, target_speed, block_dt, true, stepped_feet)) {
                    if (sneaking && has_blocks) {
                        const float desired_dx = stepped_feet[0] - final_feet[0];
                        const float desired_dz = stepped_feet[2] - final_feet[2];
                        blocks::SneakMoveResult sneak_res;
                        {
                            std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
                            sneak_res = blocks::clampSneakEdgeMovement(ctx.world().block_grid, final_feet, desired_dx, desired_dz);
                        }
                        stepped_feet[0] = final_feet[0] + sneak_res.dx;
                        stepped_feet[2] = final_feet[2] + sneak_res.dz;
                    }
                    blocks::Resolve step_r;
                    if (has_blocks) {
                        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
                        step_r = blocks::resolvePlayerSwept(ctx.world().block_grid, final_feet, stepped_feet);
                    } else {
                        step_r.feet[0] = stepped_feet[0];
                        step_r.feet[1] = stepped_feet[1];
                        step_r.feet[2] = stepped_feet[2];
                        step_r.moved = true;
                    }
                    final_feet[0] = step_r.feet[0];
                    final_feet[1] = step_r.feet[1];
                    final_feet[2] = step_r.feet[2];
                    direct_moved = true;
                }
            }
        }
    }

    // Native terrain sneak & shield speed limiting:
    // When walking on Elden Ring's native ground, clamp horizontal displacement per frame to Minecraft sneak speed (1.295 m/s)
    const bool clamp_speed = sneaking || ctx.mode().shield_blocking.load(std::memory_order_relaxed) || ctx.combat().shield.isBlocking();
    if (!supported && clamp_speed && have_prev_player_ && !jumped && !pinned && fg) {
        const float cur_dx = final_feet[0] - prev_player_feet_[0];
        const float cur_dz = final_feet[2] - prev_player_feet_[2];
        const float h_dist = std::sqrt(cur_dx * cur_dx + cur_dz * cur_dz);
        const float max_h = ctx.mode().sneak_speed.load(std::memory_order_relaxed) * block_dt;
        if (h_dist > max_h && h_dist > 1e-4f) {
            const float scale = max_h / h_dist;
            final_feet[0] = prev_player_feet_[0] + cur_dx * scale;
            final_feet[2] = prev_player_feet_[2] + cur_dz * scale;
            direct_moved = true;
        }
    }

    if (module != 0 && (pinned || sneak_clamped || direct_moved) && callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, final_feet, sizeof(final_feet));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, final_feet, sizeof(final_feet));
        const uint8_t one = 1;
        callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, sizeof(one));
    }

    if (module != 0 && supported && callbacks_.safe_write) {
        if (ctx.mode().block_ground.load(std::memory_order_relaxed)) {
            const uint8_t one = 1;
            callbacks_.safe_write(module + 0x92, &one, sizeof(one));
            callbacks_.safe_write(module + 0x1D1, &one, sizeof(one));
            callbacks_.safe_write(module + 0x1D0, &one, sizeof(one));
        }
        const float zero = 0.f;
        callbacks_.safe_write(module + 0x120 + 4, &zero, sizeof(zero));
        callbacks_.safe_write(module + 0x1B8, &zero, sizeof(zero));
    }

    if (module != 0 && supported && ctx.mode().block_drive.load(std::memory_order_relaxed) && callbacks_.drive_velocity) {
        callbacks_.drive_velocity(module, pinned, ctx);
    }
    if (supported) {
        ctx.player().movement_layer_ms.store(GetTickCount64(), std::memory_order_relaxed);
        if (callbacks_.reset_fall_timer) callbacks_.reset_fall_timer(player);
        if (callbacks_.unlock_air_movement) callbacks_.unlock_air_movement(player);
    }

    std::memcpy(prev_player_feet_, final_feet, sizeof(prev_player_feet_));
    have_prev_player_ = true;
}

void VoxelWorldRuntime::stepEnemyBlockCollision(const float player_feet[3], RuntimeContext& ctx) {
    if (!ctx.mode().mc_mode.load(std::memory_order_relaxed) ||
        !ctx.mode().enemy_block_collision.load(std::memory_order_relaxed)) {
        return;
    }

    std::vector<EnemyInfo> enemies;
    if (callbacks_.enumerate_enemies && !callbacks_.enumerate_enemies(enemies)) {
        return;
    }

    {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        if (ctx.world().block_grid.count() == 0) return;
    }

    if (prev_enemy_feet_.size() > 256) prev_enemy_feet_.clear();

    for (const EnemyInfo& e : enemies) {
        if (e.chr == 0 || e.hp <= 0) continue;
        float e_feet[3] = {};
        if (!callbacks_.read_physics_pos || !callbacks_.read_physics_pos(e.chr, e_feet)) continue;

        const float dx = e_feet[0] - player_feet[0];
        const float dy = e_feet[1] - player_feet[1];
        const float dz = e_feet[2] - player_feet[2];
        if (dx * dx + dy * dy + dz * dz > 50.f * 50.f) continue;

        blocks::Resolve er{};
        {
            std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
            if (ctx.world().block_grid.count() == 0) return;
            auto it = prev_enemy_feet_.find(e.chr);
            if (it != prev_enemy_feet_.end()) {
                const auto& prev = it->second;
                const float mx = e_feet[0] - prev[0], my = e_feet[1] - prev[1], mz = e_feet[2] - prev[2];
                if (mx * mx + my * my + mz * mz < 25.f) {
                    er = blocks::resolveBoxSwept(ctx.world().block_grid, prev.data(), e_feet, 0.35f, 1.8f);
                } else {
                    er = blocks::resolveBox(ctx.world().block_grid, e_feet, 0.35f, 1.8f);
                }
            } else {
                er = blocks::resolveBox(ctx.world().block_grid, e_feet, 0.35f, 1.8f);
            }
        }

        float final_e_feet[3] = {er.moved ? er.feet[0] : e_feet[0], er.moved ? er.feet[1] : e_feet[1], er.moved ? er.feet[2] : e_feet[2]};
        prev_enemy_feet_[e.chr] = {final_e_feet[0], final_e_feet[1], final_e_feet[2]};

        if (er.moved) {
            const uintptr_t emodule = callbacks_.get_physics_module ? callbacks_.get_physics_module(e.chr) : 0;
            if (emodule != 0 && callbacks_.safe_write) {
                callbacks_.safe_write(emodule + layout::kPhysicsPosition, er.feet, sizeof(er.feet));
                callbacks_.safe_write(emodule + layout::kPhysicsPosition + 0x10, er.feet, sizeof(er.feet));
            }
        }
    }
}

void VoxelWorldRuntime::processTntExplosions(const std::vector<EnemyInfo>& enemies, RuntimeContext& ctx) {
    (void)ctx;
    std::vector<eldenring::tnt::ErExplosionDamageIntent> intents;
    {
        std::lock_guard<std::mutex> g(tnt_intent_mutex_);
        if (tnt_damage_intents_.pendingCount() == 0) return;
        intents = tnt_damage_intents_.drain();
    }
    if (intents.empty() || enemies.empty()) return;

    for (const auto& exp : intents) {
        for (const auto& e : enemies) {
            if (e.chr == 0 || e.hp <= 0) continue;
            float e_feet[3] = {};
            if (!callbacks_.read_physics_pos || !callbacks_.read_physics_pos(e.chr, e_feet)) continue;
            const int dmg = eldenring::tnt::computeTntExplosionDamage(exp.center, e_feet, exp.radius, exp.max_damage);
            if (dmg > 0 && callbacks_.apply_damage_to_chr) {
                callbacks_.apply_damage_to_chr(e.chr, dmg);
                log("tnt: explosion hit enemy %p for %d dmg", reinterpret_cast<void*>(e.chr), dmg);
            }
        }
    }
}

void VoxelWorldRuntime::processCreeperExplosions(const std::vector<EnemyInfo>& enemies, const float player_feet[3], RuntimeContext& ctx) {
    (void)enemies;
    (void)player_feet;
    (void)ctx;
}

void VoxelWorldRuntime::triggerTntExplosion(const float center[3], RuntimeContext& ctx) {
    (void)ctx;
    if (callbacks_.play_sound) {
        callbacks_.play_sound("entity.generic.explode", 1.0f);
    }
    if (callbacks_.spawn_fx) {
        for (float dx : {-1.0f, 0.0f, 1.0f}) {
            for (float dz : {-1.0f, 0.0f, 1.0f}) {
                const float ppos[3] = {center[0] + dx * 0.8f, center[1] + 0.5f, center[2] + dz * 0.8f};
                callbacks_.spawn_fx(1 /* Crit */, ppos, 16);
            }
        }
        callbacks_.spawn_fx(2 /* Sweep */, center, 0);
    }
    log("tnt: DETONATION at (%.2f %.2f %.2f)!", center[0], center[1], center[2]);

    enqueueTntIntent(center, 8.0f, 1500.0f);

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    float feet[3] = {};
    if (player != 0 && callbacks_.read_physics_pos && callbacks_.read_physics_pos(player, feet)) {
        const int p_dmg = eldenring::tnt::computeTntExplosionDamage(center, feet, 8.0f, 400.0f);
        if (p_dmg > 0 && callbacks_.apply_damage_to_chr) {
            callbacks_.apply_damage_to_chr(player, p_dmg);
            if (callbacks_.play_sound) callbacks_.play_sound("entity.player.hurt", 1.0f);
            log("tnt: explosion hit player for %d dmg", p_dmg);
        }
    }
}

} // namespace eldenring::world
